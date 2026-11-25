#pragma once
// Halo GI Tier 1, first part (round 6, Docs/features/global-illumination.md): the light probe volume's grid kept up to
// date while the game runs, with per-probe visibility so light does not leak through walls.
//
//   - Each update traces rays from a share of the probes (round robin) through the scene's opaque triangles (CPU BVH,
//     Render3D/RayScene). A hit is lit by the sun (shadow ray), the local lights (shadow rays) and the probe grid itself
//     as it is now (so light bounces again with every update), times the surface's colour; a miss sees the sky.
//   - The radiance is projected onto SH9 and turned into irradiance / pi (the shader's ambient term), blended with the
//     probe's previous value (hysteresis): a light switched on shows within a few updates.
//   - Visibility: each probe keeps a small octahedral map (8 x 8 directions) of the mean and mean square of its ray
//     distances, each texel gathering the rays near its direction. A surface point further from a probe than the probe
//     can see in that direction (a wall in between) gets little of its light (Chebyshev test, like DDGI's depth
//     moments); probes behind the surface count less too.
//   - Thin walls: the depth map is too coarse for a point a few centimetres behind a wall from a probe half a cell
//     away. So each probe also keeps an exact mask of what it sees around it: the 2 x 2 x 2 cells it lights, cut into
//     8 x 8 x 8 small boxes, one bit each (an occlusion ray to the box's centre), traced again when the scene changes.
//   - Probes inside geometry (most rays hit back faces) are switched off.
// The doc's Tier 1 traces a scene SDF on the GPU; this first part traces triangles on the CPU, a share per frame.
#include "Render3D/RayScene.h"
#include "Render3D/Renderer3D.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace bdg {
class DynamicGI {
public:
  struct Settings {
    int raysPerProbe = 64;
    int probesPerUpdate = 32;
    float hysteresis = 0.5f;  // share of the old value kept per update of a probe
  };
  // The grid (box, counts). Another box or count starts over.
  void Configure(const ProbeVolume3D& volume);
  // The scene to trace: opaque items with a CPU mesh copy (others are left out). Rebuilt only when the meshes,
  // placements or colours changed. Returns true when it was rebuilt. textureMean: a texture's mean linear colour
  // (base colour and emissive maps tint the surface's colour; without it a textured surface counts half as bright).
  bool SetScene(const std::vector<DrawItem3D>& items, const std::function<glm::vec3(TextureHandle)>& textureMean = {});
  // Traces the next probes (round robin). Returns how many were updated.
  int Update(const Light3D& light, const Settings& s);

  size_t Probes() const { return (size_t)count_.x * count_.y * count_.z; }
  // 9 x vec4 per probe: SH9 of irradiance / pi (rgb), w of the first = valid (0: inside geometry, switched off).
  const std::vector<float>& SH() const { return sh_; }
  // Per probe kVisStride floats: kVisRes x kVisRes texels, 2 floats each (mean, mean square of the distance, metres),
  // rows of the octahedral map (OctUV below); then kMaskWords 32-bit words (stored in the floats' bits) of the
  // visibility mask: bit (z * 8 + y) * 8 + x for the small box x, y, z of the 8 x 8 x 8 over probe -+ one cell.
  static constexpr int kVisRes = 8, kMaskRes = 8, kMaskWords = kMaskRes * kMaskRes * kMaskRes / 32;
  static constexpr int kVisStride = kVisRes * kVisRes * 2 + kMaskWords;
  const std::vector<float>& Visibility() const { return vis_; }
  static glm::vec2 OctUV(const glm::vec3& dir);   // direction -> 0..1 on the octahedral map (y up)
  static glm::vec3 OctDir(const glm::vec2& uv);
  uint64_t Version() const { return version_; }      // changes with each update
  int InsideProbes() const { return inside_; }        // switched off at their last update
  bool Seeded() const;                                 // every probe traced at least once since Configure
  uint64_t RaysTraced() const { return rays_; }
  size_t Triangles() const { return scene_.Triangles(); }
  // CPU version of the shader's lookup (tests, diagnostics, bounces): ambient (irradiance / pi) at a surface point
  // for normal n; pushed a quarter cell off the surface like the shader's call.
  glm::vec3 Irradiance(const glm::vec3& surface, const glm::vec3& n, bool visibility = true) const;
  glm::vec3 ProbePosition(int x, int y, int z) const;

private:
  ProbeVolume3D volume_;
  glm::ivec3 count_{0};
  std::vector<float> sh_, vis_;
  std::vector<uint8_t> seeded_;   // probe has a value (first update takes the sample as is)
  std::vector<uint8_t> maskDirty_; // the probe's visibility mask is traced again at its next update
  RayScene scene_;
  std::vector<glm::vec3> albedo_, emissive_, normal_; // per triangle
  uint64_t sceneKey_ = 0, version_ = 0, rays_ = 0;
  size_t next_ = 0;
  uint32_t frame_ = 0;
  int inside_ = 0;
  glm::vec3 Sky(const Light3D& light, const glm::vec3& d) const;
};
}
