#pragma once
// Rendering reference board, stage 1 (queue 3-0, Docs/features/graphics-test-scenes.md): a plain ground plane,
// one directional light, a 7x7 sphere matrix (X: roughness 0->1, Y: metallic 0->1) and the coordinate/colour
// reference zone: axis arrows, an unlit 18% grey card, a ColorChecker, a letter texture board, a winding probe
// (one counter-clockwise and one clockwise quad) and a depth probe. Check() reads the rendered pixels back and
// tells whether coordinates, winding, depth, gamma and UV direction behave as Core/Space.h says.
// Stage 1b (queue 3-4): a floating box over the ground (sun shadow), a red point light and a white spot light.
#include "Render3D/Renderer3D.h"
#include <filesystem>
#include <string>
#include <vector>

namespace bdg {
class ReferenceBoard {
public:
  struct Label { glm::vec3 position; std::string text; };

  bool Init(RHI& rhi, Renderer3D& renderer);
  // Stage 2 (queue 3-1b): the standard Khronos test models from `tools/testassets.py fetch` side by side instead
  // of the procedural board. Check() then reports import facts and coverage per model.
  bool InitAssets(RHI& rhi, Renderer3D& renderer, const std::filesystem::path& dir);
  // Stage 3 (queue 3-9): `instances` cubes (one mesh, every colour different: instancing keeps them in one draw)
  // and `textures` 4096x4096 BC7 textures with full mips on a wall of quads, for draw call and video memory numbers.
  bool InitStress(RHI& rhi, Renderer3D& renderer, int instances, int textures);
  int StressTextures() const { return (int)stressTextures_.size(); }
  // Optional extra model (BDG_MODEL): fitted into 2.2 m, standing on the ground left of the axes
  // (x = -2.0, z = 1.6), clear of every probe Check() reads.
  bool AddModel(const std::filesystem::path& gltf);
  // The glTF probe placed on the board (queue 3-1): a node hierarchy with a textured unlit quad and an
  // unindexed triangle without normals. externalBin != nullptr: the buffer goes to "probe data.bin" instead
  // of a data URI (the bytes are returned there).
  static std::string ProbeGltf(std::vector<uint8_t>* externalBin = nullptr);
  void Shutdown();
  const std::vector<DrawItem3D>& Items() const { return items_; }
  const std::vector<Label>& Labels() const { return labels_; }
  Camera3D camera;
  Light3D light;
  Color clear{0.11f, 0.12f, 0.14f, 1}; // display value (sRGB) of the empty background

  // rgba: bottom-up RGBA8 rows of a w x h render made with `camera`. Returns "key=value ..." ending in
  // "result=pass" or "result=FAIL(<failed keys>)".
  std::string Check(const std::vector<uint8_t>& rgba, int32_t w, int32_t h) const;

private:
  RHI* rhi_ = nullptr;
  Renderer3D* renderer_ = nullptr;
  Mesh3D plane_, sphere_, quad_, quadCW_, cylinder_, cone_, box_;
  Model3D probe_, model_;
  struct AssetModel { std::string name; Model3D model; glm::vec3 center{0}; std::string facts; bool ok = false; };
  std::vector<AssetModel> assets_;
  std::vector<TextureHandle> stressTextures_;
  bool assetMode_ = false;
  bool probeLoaded_ = false;
  TextureHandle letters_;
  std::vector<DrawItem3D> items_;
  std::vector<Label> labels_;
};
}
