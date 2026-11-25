#pragma once
// Drawing terrains and their foliage (round 6). A view (one per SceneView) turns every Terrain component of the scene
// into ordinary draw items for Renderer3D, so terrain and plants get the renderer's lighting, shadows, fog and
// instancing with no renderer change:
//  - terrain: chunks of 32 x 32 quads, LOD per chunk from its distance (geo-mipmapping, skirts hide the cracks), chunks
//    outside the camera frustum skipped; the material layers are baked into a colour map and a roughness map
//    (texture x tint, triplanar on slopes at medium and above), rebaked only where a brush painted.
//  - foliage: plants scattered per chunk from the foliage layers (seeded: the same data gives the same plants), only
//    in chunks inside the frustum and the fade distance, nearest first up to the tier's budget; grass clumps and
//    trees as instanced meshes, trees in 3 LODs (full, simple, a camera-facing card), shrinking away between the fade
//    distances, wind sway.
// Quality tiers (TerrainTier): LOD distance, colour map size, triplanar, foliage density, fade distance, budgets, wind.
#include "Render3D/Quality.h"
#include "Render3D/Renderer3D.h"
#include "Terrain/TerrainData.h"
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bdg {
class Scene;

struct TerrainTierSettings {
  float lodScale = 1;        // multiplies Terrain.lodDistance
  int colorMap = 1024;       // baked colour map side (texels)
  bool triplanar = true;     // layer textures projected three ways on steep slopes
  float grassDensity = 0.5f, treeDensity = 1; // multiply the foliage layers' density
  float fadeScale = 1;       // multiplies the fade (and tree LOD) distances
  int grassBudget = 15000, treeBudget = 2000; // instances drawn at most (nearest first)
  bool wind = true;
};
TerrainTierSettings TerrainTier(QualityTier t);

class TerrainView {
public:
  struct Stats {
    int terrains = 0, chunks = 0, culled = 0, triangles = 0;
    int grass = 0, trees = 0, impostors = 0, foliageCulled = 0, treesLod1 = 0; // trees: all LODs; treesLod1, impostors: of them
    int lodCount[8] = {};
    double ms = 0;   // CPU time of the last Append (LOD, culling, scattering, rebuilding)
    double bakeMs = 0; // of the last colour map bake (whole or a painted part)
  };
  // Adds draw items for every Terrain in the scene seen from `cam` (aspect w / h). assetsRoot: the project's Assets.
  void Append(const Scene& scene, const std::string& assetsRoot, RHI& rhi, Renderer3D& renderer, const Camera3D& cam, float aspect,
              const QualitySettings& quality, std::vector<DrawItem3D>& items);
  void Shutdown(RHI& rhi, Renderer3D& renderer);
  const Stats& LastStats() const { return stats_; }
  double windTime = -1; // seconds for the wind (< 0: wall clock)

private:
  struct ChunkMesh { Mesh3D mesh; uint64_t revision = 0; int triangles = 0; };
  struct Plants { std::vector<std::vector<FoliageInstance>> perLayer; uint64_t revision = 0; float density = -1; };
  struct Entry {
    std::vector<ChunkMesh> meshes; // [chunk][lod]
    std::vector<uint64_t> chunkRev;  // revision when the chunk last changed (heights)
    std::vector<uint64_t> plantRev;  // revision when the chunk's plants last changed
    std::vector<Plants> plants;
    std::vector<glm::vec3> boundsLo, boundsHi;
    uint64_t seen = 0;
    TextureHandle color, rough;
    int mapSize = 0;
    bool triplanar = false;
    uint64_t bakedRevision = 0;
    std::map<std::string, std::vector<uint8_t>> textures; // layer images (CPU) by path
    std::map<std::string, glm::ivec2> textureSize;
  };
  void Sync(Entry& e, TerrainData& t, RHI& rhi, Renderer3D& r, const TerrainTierSettings& tier, const std::string& assetsRoot);
  void Bake(Entry& e, const TerrainData& t, RHI& rhi, int x0, int z0, int x1, int z1, const std::string& assetsRoot);
  void FoliageMeshes(RHI& rhi, Renderer3D& r);

  std::map<std::string, Entry> entries_; // by terrain path
  Mesh3D grass_, trunk_, crown_, trunkLow_, crownLow_, card_;
  TextureHandle cardTex_;
  bool foliageReady_ = false;
  uint64_t frame_ = 0;
  Stats stats_;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};
} // namespace bdg
