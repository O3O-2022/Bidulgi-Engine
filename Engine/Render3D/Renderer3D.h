#pragma once
// Forward 3D renderer, first stage (queue 3-0): one directional light, a sky/ground hemisphere for ambient
// light and reflections, metal/roughness GGX shading. Colours are linear inside the shader and written
// sRGB-encoded into an RGBA8 render target, which the UI then draws as an image. No tone mapping yet
// (values above 1 clip); that arrives with post-processing.
#include "Render3D/MeshGen.h"
#include "RHI/RHI.h"
#include <glm/glm.hpp>
#include <vector>

namespace bdg {
struct Mesh3D {
  BufferHandle vertices, indices;
  uint32_t indexCount = 0;
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

struct Light3D {
  glm::vec3 toLight{0.3f, 1.f, 0.5f};    // direction toward the light (normalised in the shader)
  Color color{1, 1, 1, 1};
  float intensity = 2.5f;
  Color sky{0.3f, 0.36f, 0.45f, 1}, ground{0.1f, 0.09f, 0.08f, 1}; // ambient hemisphere (linear)
};

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
              const std::vector<DrawItem3D>& items, Color clear);
  uint32_t DrawsLastFrame() const { return draws_; }

private:
  RHI* rhi_ = nullptr;
  PipelineHandle opaque_, twoSided_, blend_;
  BufferHandle frameUbo_, objectUbo_;
  size_t objectCapacity_ = 0, objectStride_ = 0;
  TextureHandle white_;
  uint32_t draws_ = 0;
};
}
