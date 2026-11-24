#pragma once
// Forward 3D renderer: metal/roughness GGX, sun + point/spot lights, sky, shadow map (3-0 ... 3-4). The scene is
// drawn in linear light into an HDR target (RGBA16F, MSAA by tier); post-processing (bloom, ACES tone mapping,
// FXAA, queue 3-5) writes the display-encoded result into the caller's RGBA8 target, which the UI shows as an
// image. Editor overlays (grid, selection outline) go on top of the final picture with the scene's depth.
#include "Render3D/MeshGen.h"
#include "Render3D/Quality.h"
#include "RHI/RHI.h"
#include <glm/glm.hpp>
#include <vector>

namespace bdg {
struct Mesh3D {
  BufferHandle vertices, indices;
  uint32_t indexCount = 0;
  glm::vec3 boundsMin{0}, boundsMax{0}; // local box (culling); equal = unknown, never culled
};

struct Material3D {
  enum class Alpha { Opaque, Mask, Blend };
  Color baseColor{0.8f, 0.8f, 0.8f, 1};  // linear
  float metallic = 0, roughness = 0.5f;
  Color emissive{0, 0, 0, 1};            // linear, may exceed 1
  bool unlit = false;                    // output baseColor (x texture) as is: reference cards and patches
  bool doubleSided = false;
  Alpha alpha = Alpha::Opaque;
  float alphaCutoff = 0.5f, normalScale = 1, occlusionStrength = 1;
  // Optional maps (glTF layout): base colour (SRGBA8), metal/rough (G roughness, B metallic), tangent-space
  // normal, occlusion (R), emissive (SRGBA8).
  TextureHandle baseMap, metalRoughMap, normalMap, occlusionMap, emissiveMap;
};

struct DrawItem3D {
  const Mesh3D* mesh = nullptr;
  glm::mat4 model{1.f};
  Material3D material;
};

struct Camera3D {
  glm::vec3 position{0, 2, 8}, target{0, 1, 0};
  float fovY = 45.f;                     // degrees, vertical
  float nearZ = 0.1f, farZ = 500.f;
  glm::mat4 View() const;
  glm::mat4 Projection(float aspect) const;
};

// Point and spot lights (queue 3-4). Falloff: windowed inverse square, intensity 1 = the sun's 1 at 1 m.
struct LocalLight3D {
  glm::vec3 position{0}, direction{0, -1, 0}; // direction: where a spot light points
  Color color{1, 1, 1, 1};
  float intensity = 1, range = 10;
  bool spot = false;
  float innerAngle = 30, outerAngle = 45;      // full cone angles in degrees
};

// Procedural sky drawn behind everything; also the ambient light and the reflections (queue 3-4).
struct SkyBackground3D {
  bool draw = false;                           // false: keep the clear colour, ambient from Light3D sky/ground
  Color zenith{0.16f, 0.32f, 0.62f, 1}, horizon{0.62f, 0.70f, 0.80f, 1}, ground{0.20f, 0.18f, 0.16f, 1}; // linear
  float intensity = 1, sunSize = 1.5f;         // sun disk in degrees (0 = none)
};

struct Light3D {
  glm::vec3 toLight{0.3f, 1.f, 0.5f};    // direction toward the light (normalised in the shader)
  Color color{1, 1, 1, 1};
  float intensity = 2.5f;
  Color sky{0.3f, 0.36f, 0.45f, 1}, ground{0.1f, 0.09f, 0.08f, 1}; // ambient hemisphere (linear) without a sky
  bool shadows = false;                  // directional shadow map fitted to the camera
  float shadowDistance = 40.f;           // metres from the camera that get shadows
  std::vector<LocalLight3D> locals;      // the first kMaxLocalLights are used
  SkyBackground3D background;
};
constexpr int kMaxLocalLights = 16;

Color SrgbToLinear(uint8_t r, uint8_t g, uint8_t b); // exact sRGB transfer function
// World point -> pixel in a w x h target (x from the left, y from the BOTTOM, like the render target rows).
// false when the point is behind the camera.
bool ProjectToPixel(const Camera3D& cam, int32_t w, int32_t h, const glm::vec3& p, float& px, float& py);

struct ImportedModel;

// A glTF model on the GPU: one mesh per primitive, materials with their textures.
struct Model3D {
  std::vector<Mesh3D> meshes;            // per ImportedModel primitive (empty mesh = skipped primitive)
  std::vector<Material3D> materials;     // per primitive (default material when it had none)
  std::vector<TextureHandle> textures;
  struct Instance { int mesh; glm::mat4 world; };
  std::vector<Instance> instances;
  glm::vec3 boundsMin{0}, boundsMax{0};
};

// Editor additions to a render (queue 3-2).
// What the picture shows (render pass viewer, queue 3-8). Data views skip tone mapping and post effects.
enum class DebugView { Lit, Albedo, Normal, Roughness, Metallic, Depth, Shadow };
const char* DebugViewName(DebugView v); // "Lit", "Albedo", ...

struct RenderExtras {
  bool grid = false;             // ground grid at y = 0: 1 m and 10 m lines, red X axis and blue Z axis lines, fades out
  std::vector<size_t> outlined;  // indices into the item list: drawn with a selection outline, visible through walls
  DebugView view = DebugView::Lit;
  bool wireframe = false;        // triangle edges over the picture
};

// What the last Render() did (stats overlay, `bdg render stats`).
struct RenderStats {
  int32_t w = 0, h = 0, msaa = 1, shadowMap = 0, localLights = 0, shadowCasters = 0;
  uint32_t draws = 0;
  uint64_t triangles = 0;
  bool toneMap = false, fxaa = false, bloom = false;
  DebugView view = DebugView::Lit;
  struct Pass { const char* name; double ms; };
  std::vector<Pass> passes; // GPU time per pass of the newest finished frame (empty until one is ready)
  uint32_t sceneDraws = 0;  // draw calls of the scene pass (with instancing: one per run of alike objects)
  size_t objects = 0;       // objects drawn in the scene pass
  size_t culled = 0;        // objects outside the camera frustum (not drawn)
  size_t shadowCulled = 0;  // opaque objects outside the light's frustum (not drawn into the shadow map)
};

class Renderer3D {
public:
  bool Init(RHI& rhi);
  void Shutdown();
  Mesh3D CreateMesh(const MeshData& data);
  void DestroyMesh(Mesh3D& mesh);
  Model3D UploadModel(const ImportedModel& model);
  void DestroyModel(Model3D& model);
  // Adds every instance of the model, placed by `placement` (model space -> world).
  static void AppendModel(const Model3D& model, const glm::mat4& placement, std::vector<DrawItem3D>& items);
  // Draws the items into rt (cleared to `clear`, given in display/sRGB values like UI colours).
  void Render(RenderTargetHandle rt, int32_t w, int32_t h, const Camera3D& cam, const Light3D& light,
              const std::vector<DrawItem3D>& items, Color clear, const RenderExtras* extras = nullptr,
              const QualitySettings* quality = nullptr); // nullptr: ReferenceQuality()
  // rt must be single-sampled with depth (RHI::CreateRenderTarget(w, h, 1)); MSAA happens inside.
  void NewFrame() { draws_ = 0; triangles_ = 0; } // stats count every Render() since the last call
  uint32_t DrawsLastFrame() const { return draws_; }
  uint64_t TrianglesLastFrame() const { return triangles_; }
  const RenderStats& Stats() const { return stats_; }
  std::string DescribePasses() const; // "shadow 0.21 ms | scene 0.80 ms | ..." (GPU)
  std::string DescribeStats() const;  // resolution, MSAA, draws, triangles, lights, shadow casters, post

private:
  RHI* rhi_ = nullptr;
  PipelineHandle opaque_, twoSided_, blend_;
  BufferHandle frameUbo_, objectBuffer_, instanceIds_; // object constants (storage buffer), per-pass object index lists
  size_t objectCapacity_ = 0, indexCapacity_ = 0;
  TextureHandle white_;
  uint32_t draws_ = 0;
  uint64_t triangles_ = 0;
  // Editor passes: grid plane, selection mask (own target) and the outline drawn from it.
  PipelineHandle grid_, mask_, outline_, sky_, shadowPipe_;
  BufferHandle lightsUbo_;
  RenderTargetHandle shadowRT_, noShadowRT_; // shadow map (size by tier); 1x1 stand-in when shadows are off
  int32_t shadowSize_ = 0;
  // Post-processing: HDR scene, LDR copy for FXAA, bloom chain (half size down to 1/32).
  PipelineHandle toneMap_, fxaa_, bloomDown_, bloomUp_;
  BufferHandle postUbo_;
  RenderTargetHandle hdrRT_, ldrRT_;
  int32_t hdrW_ = 0, hdrH_ = 0, hdrSamples_ = 0;
  static constexpr int kBloomLevels = 5;
  RenderTargetHandle bloom_[kBloomLevels];
  int32_t bloomW_ = 0, bloomH_ = 0;
  QualitySettings lastQuality_;
  PipelineHandle wire_;
  RenderStats stats_;
  // GPU timings: a few frames of timestamp queries in flight, read back when the GPU has finished them.
  static constexpr int kTimerFrames = 4, kMarks = 8;
  struct TimerFrame { uint32_t q[kMarks] = {}; const char* names[kMarks] = {}; int marks = 0; bool pending = false; };
  TimerFrame timers_[kTimerFrames];
  int timerFrame_ = 0;
  void Mark(TimerFrame& t, const char* passEnded);
  void CollectTimers();
public:
  const QualitySettings& LastQuality() const { return lastQuality_; }
private:
public:
  static constexpr int kShadowMapSize = 2048;
  // Light-space matrix of the last frame's shadow map (tests read it back to check where shadows fall).
  const glm::mat4& ShadowMatrix() const { return shadowMatrix_; }
private:
  glm::mat4 shadowMatrix_{1.f};
  BufferHandle gridQuad_, fullscreen_;
  RenderTargetHandle maskRT_;
  int32_t maskW_ = 0, maskH_ = 0;
};
}
