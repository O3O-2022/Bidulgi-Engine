#pragma once
// Forward 3D renderer: metal/roughness GGX, sun + point/spot lights, sky, shadow map (3-0 ... 3-4). The scene is
// drawn in linear light into an HDR target (RGBA16F, MSAA by tier); post-processing (bloom, ACES tone mapping,
// FXAA, queue 3-5) writes the display-encoded result into the caller's RGBA8 target, which the UI shows as an
// image. Editor overlays (grid, selection outline) go on top of the final picture with the scene's depth.
#include "Render3D/MeshGen.h"
#include "Render3D/ColorGrading.h"
#include "Render3D/PostVolume.h"
#include "Render3D/Quality.h"
#include "RHI/RHI.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdg {
struct Mesh3D {
  BufferHandle vertices, indices;
  uint32_t indexCount = 0;
  glm::vec3 boundsMin{0}, boundsMax{0}; // local box (culling); equal = unknown, never culled
  std::shared_ptr<const MeshData> source; // CPU copy (lightmap unwrap and ray casts); null for meshes made elsewhere
  uint64_t contentHash = 0;               // of the vertex and index data (baked GI cache keys)
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
  bool lightmapped = false; // static GI (Tier 0-C): baked into a lightmap; used while mesh and placement stay the same
};

struct Camera3D {
  glm::vec3 position{0, 2, 8}, target{0, 1, 0};
  glm::vec3 up{0, 1, 0};
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
  std::vector<LocalLight3D> locals;      // per pixel: up to the tier's maxLights (<= kMaxClusteredLights); per vertex: first 16
  SkyBackground3D background;
  // World Settings (2-6). Fog: colour = mix(fogColor, surface, exp(-fogDensity * metres past fogStart)); 0 = off.
  // Not applied to probe and lightmap captures (they are lighting, not the view).
  float fogDensity = 0, fogStart = 0;
  Color fogColor{0.62f, 0.70f, 0.80f, 1}; // linear
  float exposure = 1;                     // times the quality tier's exposure
  PostEffects3D post;                     // post-process volumes at the camera (E-6)
  // Light shafts / god rays (E-7, Unreal's): sky seen around the sun streaks toward the camera, blocked by
  // geometry. 0 = off; the sun's colour and intensity tint them.
  float shafts = 0;
};
constexpr int kMaxLocalLights = 16;        // per-vertex lighting (low tier), uniform buffer
constexpr int kMaxClusteredLights = 1024;  // per-pixel lighting, storage buffer (Forward+, 3-5b)
constexpr int kClusterX = 16, kClusterY = 9, kClusterZ = 24; // screen tiles and depth slices (logarithmic)

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
// Reflection probe (GI Tier 0): the scene seen from `position`, prefiltered by roughness for reflections and
// reduced to SH9 for diffuse light. Objects whose centre is inside the box use it; box projection makes a
// room's reflections line up with its walls. Index 0 is always the sky.
struct ReflectionProbe3D {
  glm::vec3 position{0}, boxMin{-5}, boxMax{5};
  bool boxProjection = true;
  float intensity = 1;
};
constexpr int kMaxProbes = 7; // + the sky

// Lightmap bake settings (GI Tier 0-C, see Render3D/Lightmap.h).
struct LightmapSettings {
  float texelsPerMeter = 12;
  int samples = 64;       // rays per texel and bounce (rounded to a square for stratification)
  int bounces = 2;
  int maxSize = 2048;     // atlas side limit; the density drops to fit
  int padding = 2;        // texels between charts
};

// Light probe volume (GI Tier 0-B): a grid of SH9 irradiance probes filling a box, one probe at the centre of each
// cell. Surfaces inside the box blend the 8 nearest probes per pixel (trilinear) for their diffuse light, so the
// light changes across a floor or wall instead of being one value per object. Reflections still use the probes above.
struct ProbeVolume3D {
  glm::vec3 boxMin{-5}, boxMax{5};
  glm::ivec3 count{4, 2, 4}; // probes along x, y, z (each 1..16)
  float intensity = 1;
};

// What the picture shows (render pass viewer, queue 3-8). Data views skip tone mapping and post effects.
enum class DebugView { Lit, Albedo, Normal, Roughness, Metallic, Depth, Shadow, Indirect, Cascades, AO, LightCount };
const char* DebugViewName(DebugView v); // "Lit", "Albedo", ...

struct RenderExtras {
  bool grid = false;             // ground grid at y = 0: 1 m and 10 m lines, red X axis and blue Z axis lines, fades out
  std::vector<size_t> outlined;  // indices into the item list: drawn with a selection outline, visible through walls
  DebugView view = DebugView::Lit;
  bool wireframe = false;        // triangle edges over the picture
  bool linearOutput = false;     // write linear light into an RGBA16F target (probe captures), no display curve
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
  size_t lightmapped = 0;   // objects drawn with a baked lightmap
  int cascades = 0;         // shadow cascades drawn (3-5b)
  bool ssao = false;        // screen-space ambient occlusion pass ran (3-5b)
  bool shafts = false;      // light shafts were drawn (E-7): the sun was on or near the screen
  float sunU = 0, sunV = 0; // where the sun was on the picture (0..1, from the bottom left)
  bool clustered = false;   // Forward+ light lists were used (3-5b)
  uint32_t clusterRefs = 0, clusterMax = 0, clustersUsed = 0; // light references, most lights in one cluster, clusters with any
  int anisotropy = 1;       // texture filtering used (3-5b)
  size_t blobs = 0;         // blob shadows drawn (low tier)
  bool grading = false;     // colour grading table applied
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
  // Reflection probes (GI Tier 0). SetProbes replaces the list (unbaked probes are not used); BakeProbes captures
  // every probe from the current scene: each call adds one bounce of light. Returns how many were baked.
  void SetProbes(const std::vector<ReflectionProbe3D>& probes);
  int BakeProbes(const std::vector<DrawItem3D>& items, const Light3D& light);
  int BakedProbes() const { return bakedProbes_; }
  // Probe volume (GI Tier 0-B): nullptr removes it. Baked by BakeProbes together with the reflection probes.
  void SetVolume(const ProbeVolume3D* volume);
  // Colour grading (3-5b): nullptr or neutral settings remove it. Rebuilds the table only when the settings change.
  void SetColorGrading(const ColorGrading3D* grading);
  int VolumeProbes() const { return volumeBaked_ ? volume_.count.x * volume_.count.y * volume_.count.z : 0; }
  // Test hook: the volume's diffuse light (ambient / pi) at a point for a normal, from the baked coefficients (CPU).
  glm::vec3 VolumeIrradiance(const glm::vec3& p, const glm::vec3& n) const;
  // Lightmaps (GI Tier 0-C): bakes the items marked `lightmapped` (CPU rays, see Render3D/Lightmap.h). Bake them
  // before the probes so the probes see the lightmapped surfaces. Returns how many objects got a lightmap.
  int BakeLightmaps(const std::vector<DrawItem3D>& items, const Light3D& light, const LightmapSettings& settings = {});
  void ClearLightmaps();
  int LightmappedObjects() const { return (int)lightmaps_.size(); }
  std::string DescribeLightmaps() const; // "7 objects, atlas 512x384, 12.0 texels/m, 41230 texels, 1840 ms"
  // Test hook: the baked value (ambient / pi) of the covered texel nearest to p whose normal is within ~45 deg of n.
  bool LightmapAt(const glm::vec3& p, const glm::vec3& n, glm::vec3& value) const;
  // Baked GI cache (GI Tier 0-D, Render3D/GICache.cpp). GIInputHash covers everything a bake depends on: the items
  // (meshes, placements, materials, lightmap flags), the light, the probes and volume set now, the lightmap settings.
  // SaveGI writes what is baked now; LoadGI restores it when the file's hash matches (lightmaps are matched to the
  // items again by mesh and placement). Runtime builds only load; the editor bakes and saves.
  uint64_t GIInputHash(const std::vector<DrawItem3D>& items, const Light3D& light, const LightmapSettings& settings = {}) const;
  bool SaveGI(const std::string& path, uint64_t inputHash, std::string* error = nullptr) const;
  bool LoadGI(const std::string& path, const std::vector<DrawItem3D>& items, uint64_t inputHash, std::string* error = nullptr);
  // `bdg gi diagnose` (text, one finding per line): probes inside objects, light leak suspects between volume
  // probes, lightmap coverage, memory, bake times, and what to change.
  std::string DiagnoseGI(const std::vector<DrawItem3D>& items) const;
  std::string giCacheNote; // "loaded <path>" / "saved <path>" / "none": shown by DiagnoseGI
  // Test hooks: one face of a probe's mirror-sharp level (0 = sky), RGB linear, and its SH9 (ambient / pi).
  bool ReadProbeFace(int probe, int face, std::vector<float>& rgb, int& size);
  const float (&ProbeSH(int probe) const)[9][3] { return probeSH_[probe]; }
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
  RenderTargetHandle shadowRT_, noShadowRT_; // shadow atlas (one tile per cascade, size by tier); 1x1 stand-in when off
  int32_t shadowSize_ = 0, shadowTiles_ = 0;
  glm::mat4 cascadeVP_[4]{};
  BufferHandle shadowPassUbo_;
  // Post-processing: HDR scene, LDR copy for FXAA, bloom chain (half size down to 1/32).
  PipelineHandle toneMap_, fxaa_, bloomDown_, bloomUp_;
  // Auto exposure (E-6): the picture's log luminance on a 32x32 target, then per output target a 1x1 average that
  // moves toward it over time (two 1x1 targets swapped each frame).
  PipelineHandle lum_, adapt_, dof_, shafts_;
  RenderTargetHandle lumRT_, dofRT_, shaftRT_;
  int32_t shaftW_ = 0, shaftH_ = 0;
  int32_t dofW_ = 0, dofH_ = 0;
  struct Adapt { RenderTargetHandle rt[2]; int cur = 0; double last = 0; bool primed = false; };
  std::unordered_map<uint32_t, Adapt> adapt_s;
  uint32_t postFrame_ = 0; // grain seed
  // SSAO (3-5b): normals + view depth of opaque objects (full size), occlusion and its blur (half size).
  PipelineHandle prepass_, prepassTwo_, ssao_, aoBlur_;
  RenderTargetHandle geoRT_, aoRT_[2];
  int32_t geoW_ = 0, geoH_ = 0;
  BufferHandle ssaoUbo_;
  // Forward+ (3-5b): every per-pixel light (4 vec4 each) and the cluster lists (first, count per cluster; indices).
  BufferHandle lightBuffer_, clusterBuffer_;
  TextureHandle gradingLut_, blobTex_; // 32^3 grading table; radial spot for blob shadows
  uint64_t gradingKey_ = 0;
  Mesh3D blobQuad_;
  size_t lightCapacity_ = 0, clusterCapacity_ = 0;
  BufferHandle postUbo_;
  RenderTargetHandle hdrRT_, ldrRT_;
  int32_t hdrW_ = 0, hdrH_ = 0, hdrSamples_ = 0;
  static constexpr int kBloomLevels = 5;
  RenderTargetHandle bloom_[kBloomLevels];
  int32_t bloomW_ = 0, bloomH_ = 0;
  QualitySettings lastQuality_;
  PipelineHandle wire_;
  RenderStats stats_;
  // Image-based lighting: probe 0 = the sky, 1..kMaxProbes = captured probes, one cube map array.
  static constexpr int kEnvSize = 128, kEnvLevels = 5;
  PipelineHandle skyCube_, prefilter_;
  BufferHandle probesUbo_, prefilterUbo_;
  TextureHandle envArray_, scratchCube_;
  RenderTargetHandle captureRT_;
  uint64_t skyKey_ = 0;
  std::vector<ReflectionProbe3D> probes_;
  int bakedProbes_ = 0;
  float probeSH_[kMaxProbes + 1][9][3] = {};
  void BakeSky();
  void PrefilterInto(int layer);      // scratchCube_ (with mips) -> envArray_ layer, all roughness levels
  void ComputeSH(int probe);          // scratchCube_ -> probeSH_[probe]
  bool ComputeSHFrom(TextureHandle cube, int level, float out[9][3]); // a cube's mip level -> SH9 (ambient / pi)
  // The scene seen from `at` into the six faces of `cube` (mip 0) through `rt` (size x size, linear light).
  void CaptureCube(const glm::vec3& at, RenderTargetHandle rt, int size, TextureHandle cube, const std::vector<DrawItem3D>& items,
                   const Light3D& light);
  // Probe volume: small captures (kVolumeSize per face), SH9 per probe in a storage buffer (9 vec4 per probe).
  static constexpr int kVolumeSize = 32, kMaxVolumeProbes = 16 * 16 * 16;
  ProbeVolume3D volume_;
  bool hasVolume_ = false, volumeBaked_ = false;
  std::vector<float> volumeSH_; // 9 * 4 floats per probe
  BufferHandle volumeBuffer_;
  size_t volumeCapacity_ = 0;
  int badTexels_ = 0; // non-finite texels skipped by ComputeSHFrom
  int invalidVolumeProbes_ = 0; // inside objects at the last bake: switched off (neighbours fill in)
  double probeBakeMs_ = 0;
  TextureHandle smallCube_;
  RenderTargetHandle smallCaptureRT_;
  void EnsureSmallCapture();
  glm::vec3 AmbientAt(const glm::vec3& p, const glm::vec3& n) const; // CPU: volume, probe or sky SH (ambient / pi)
  // Lightmaps: one atlas; each lightmapped item has its own mesh copy with atlas coordinates, matched by the
  // original mesh and placement (moving the object drops back to the probes until the next bake).
  struct LightmapInstance { Mesh3D mesh; const Mesh3D* source = nullptr; glm::mat4 model{1.f}; uint64_t key = 0; };
  std::vector<LightmapInstance> lightmaps_;
  std::unordered_map<uint64_t, int> lightmapIndex_;
  static uint64_t ContentKey(const Mesh3D* mesh, const glm::mat4& model); // mesh data + placement
  static uint64_t LightmapKey(const Mesh3D* mesh, const glm::mat4& model); // mesh address + placement (per frame)
  TextureHandle lightmapTex_;
  int lightmapW_ = 0, lightmapH_ = 0, lightmapTexels_ = 0;
  float lightmapDensity_ = 0;
  double lightmapMs_ = 0;
  std::vector<glm::vec3> lmTexelPos_, lmTexelN_, lmTexelValue_;
  std::vector<uint32_t> lmTexelIndex_;
  std::vector<float> lightmapRGBA_; // CPU copy of the atlas (cache)
  int lightmapInside_ = 0;
  void UploadProbes();
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
