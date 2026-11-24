#pragma once
// Lightmap baking (GI Tier 0-C): indirect light for static objects, stored per texel of one atlas.
//
// Items marked `lightmapped` are unwrapped together (xatlas, world-space size so texel density is even) and packed
// into one atlas; each gets its own copy of its mesh carrying the atlas coordinates (Vertex3D::lu, lv). Every texel
// then shoots cosine-weighted rays (CPU, BVH over every opaque item): what they hit contributes its direct light
// (sun and local lights, with shadow rays) times its colour, plus the previous bounce; rays that leave see the sky.
// The result is irradiance / pi (the same "ambient" the probes give): direct light stays real time, the lightmap
// replaces only the diffuse ambient term. Noise is smoothed between alike neighbours; empty texels around charts
// are filled from their neighbours so bilinear sampling does not bleed black.
#include "Render3D/Renderer3D.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {

struct LightmapBakeInput {
  const std::vector<DrawItem3D>* items = nullptr;
  const Light3D* light = nullptr;
  std::function<glm::vec3(const glm::vec3& dir)> sky;                          // radiance seen by a ray that leaves
  std::function<glm::vec3(const glm::vec3& p, const glm::vec3& n)> ambient;    // E/pi on surfaces without a lightmap
};

struct LightmapResult {
  int width = 0, height = 0;
  std::vector<float> rgba;            // width * height texels, E/pi linear, alpha 1 where covered
  std::vector<int> itemInstance;      // per input item: its lightmap instance, or -1
  std::vector<MeshData> meshes;       // per instance: the item's mesh with atlas coordinates (model space)
  std::vector<size_t> instanceItem;   // per instance: the input item index
  // Covered texels (test hooks, diagnostics): world position, normal, atlas index.
  std::vector<glm::vec3> texelPos, texelNormal;
  std::vector<uint32_t> texelIndex;
  float texelsPerMeter = 0;
  int insideTexels = 0;               // texels whose rays mostly met back faces (inside geometry), filled from neighbours
  double ms = 0;
};

// World triangles of every opaque item with mesh data (3 corners each), for a RayScene.
void GatherOpaqueTriangles(const std::vector<DrawItem3D>& items, std::vector<glm::vec3>& corners);
class RayScene;
// Share of 64 rays from p (evenly spread) that meet a back face: about 1 inside a closed object, near 0 in the open.
float BackfaceShare(const RayScene& scene, const glm::vec3& p);

// false (with a reason) when nothing could be baked: no lightmapped items with mesh data, or the unwrap failed.
bool BakeLightmapAtlas(const LightmapBakeInput& in, const LightmapSettings& settings, LightmapResult& out, std::string* error);

} // namespace bdg
