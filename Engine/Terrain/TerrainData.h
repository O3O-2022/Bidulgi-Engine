#pragma once
// Terrain (queue 6-T, round 6): a heightmap asset with painted material layers and foliage, CPU side. Nothing here
// touches the GPU; Terrain/TerrainView draws it, Physics makes a Jolt height field from it, the editor's brushes
// change it through the command bus ("terrain" command, undoable).
//
// Files (next to each other under Assets):
//   Hills.terrain           JSONL: the header, then one line per material layer and per foliage layer
//   Hills.height.r16        heights, resolution^2 little-endian uint16 (0 = base, 65535 = base + height)
//   Hills.splat0.png        layer weights, 4 layers per RGBA image (Hills.splat1.png for layers 5..8)
//   Hills.foliage.png       foliage density per foliage layer (RGBA = foliage layers 1..4), 255 = as dense as set
//
//   {"terrain":"Hills","resolution":257,"size":[200,200],"height":40}
//   {"layer":"Grass","color":[0.32,0.45,0.18],"roughness":0.9,"texture":"Textures/grass.png","tiling":4}
//   {"layer":"Rock","color":[0.45,0.43,0.40],"roughness":0.8}
//   {"foliage":"Grass","kind":"grass","density":6,"layer":0,"scale":[0.6,1.2],"fade":[25,35],"color":[0.35,0.55,0.2]}
//   {"foliage":"Trees","kind":"tree","density":0.01,"layer":-1,"scale":[0.8,1.4],"fade":[160,200],"impostor":70}
//
// The terrain object's position is the terrain's (min x, min z) corner at height 0; it spans +X and +Z by `size`.
// Rotation and scale of the object are ignored. resolution is 2^n + 1 (33 .. 4097).
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bdg {
struct MeshData;

struct TerrainLayer {
  std::string name = "Layer";
  glm::vec3 color{0.5f, 0.5f, 0.5f}; // sRGB tint (the texture is multiplied by it)
  float roughness = 0.85f;
  std::string texture;               // Assets-relative image (optional)
  float tiling = 4;                  // texture repeats per 10 m
};

enum class FoliageKind { Grass, Tree };
struct FoliageLayer {
  std::string name = "Grass";
  FoliageKind kind = FoliageKind::Grass;
  float density = 4;          // instances per square metre at full density
  int layer = -1;             // material layer that gates it (its weight scales the density); -1 = anywhere
  float minScale = 0.7f, maxScale = 1.3f;
  float fadeStart = 25, fadeEnd = 35; // metres: shrinks away between them (tier scaled)
  float impostor = 70;        // trees: a camera-facing card from here on (LOD 2); LOD 1 at half of it
  float maxSlope = 35;        // degrees: none on steeper ground
  glm::vec3 color{0.35f, 0.55f, 0.2f}; // sRGB
};

struct FoliageInstance {
  glm::vec3 position{0}; // world, relative to the terrain origin
  float scale = 1, yaw = 0, phase = 0;
};

struct TerrainData {
  std::string name = "Terrain";
  int resolution = 129;
  glm::vec2 size{100, 100};
  float height = 20;
  std::vector<uint16_t> heights;              // resolution^2, row = z
  std::vector<TerrainLayer> layers;
  std::vector<std::vector<uint8_t>> splat;    // per 4 layers: resolution^2 RGBA
  std::vector<FoliageLayer> foliage;
  std::vector<uint8_t> density;               // resolution^2 RGBA (foliage layers 1..4)
  std::string path;                           // the .terrain file it came from (absolute, '/')
  uint64_t revision = 1;                      // bumped by every change (views rebuild what they cached)
  // What the last change touched (sample rectangle, inclusive): views rebuild only those chunks.
  int dirtyX0 = 0, dirtyZ0 = 0, dirtyX1 = -1, dirtyZ1 = -1;
  bool splatDirty = false, heightDirty = false, densityDirty = false;

  // Sampling (terrain-local metres; outside: clamped to the edge)
  float SampleSpacingX() const { return size.x / float(resolution - 1); }
  float SampleSpacingZ() const { return size.y / float(resolution - 1); }
  float HeightAtSample(int x, int z) const;      // metres
  float HeightAt(float x, float z) const;        // bilinear, metres
  glm::vec3 NormalAt(float x, float z) const;    // from the height slope
  float LayerWeight(int layer, float x, float z) const; // 0..1 (nearest sample)
  float Density(int foliage, float x, float z) const;   // 0..1
  bool Contains(float x, float z) const { return x >= 0 && z >= 0 && x <= size.x && z <= size.y; }
  void MarkDirty(int x0, int z0, int x1, int z1, bool heights, bool splat, bool density);
  void ClearDirty();
};

// New terrain: flat (or gentle procedural hills when hills > 0), layer 0 painted everywhere, foliage density full.
TerrainData MakeTerrain(const std::string& name, int resolution, glm::vec2 size, float height, float hills = 0, uint32_t seed = 1);
bool SaveTerrain(const TerrainData& t, const std::string& path, std::string* error = nullptr);   // writes the .terrain and its files
bool LoadTerrain(const std::string& path, TerrainData& out, std::vector<std::string>* problems = nullptr, std::string* error = nullptr);

// Brushes (editor and tests). Centre in terrain-local metres, radius in metres, strength 0..1 per application (dt
// scaled by the caller). Falloff: smooth (1 at the centre, 0 at the radius). Each marks what it touched.
enum class TerrainBrushMode { Raise, Lower, Smooth, Flatten, Paint, FoliagePaint, FoliageErase };
void ApplyTerrainBrush(TerrainData& t, TerrainBrushMode mode, glm::vec2 center, float radius, float strength, int layer = 0,
                       float flattenHeight = 0);

// Chunks: the terrain is cut into square chunks of kChunkQuads quads (the last ones may be smaller). LOD n uses every
// 2^n-th sample; skirts (a strip hanging down along every chunk edge) hide the cracks between different LODs.
constexpr int kChunkQuads = 32;
int TerrainChunksX(const TerrainData& t);
int TerrainChunksZ(const TerrainData& t);
int TerrainLods(const TerrainData& t); // LOD levels with at least 2 quads per chunk side
// Mesh of chunk (cx, cz) at a LOD: positions terrain-local, normals from the heights, uv 0..1 over the terrain
// (the baked colour map), skirt depth in metres.
void BuildChunkMesh(const TerrainData& t, int cx, int cz, int lod, float skirt, MeshData& out);
void ChunkBounds(const TerrainData& t, int cx, int cz, glm::vec3& lo, glm::vec3& hi);
// The LOD for a chunk at `distance` metres from the camera: 0 within lodDistance, +1 for each doubling.
int ChunkLod(float distance, float lodDistance, int lods);

// Foliage instances of one chunk for one foliage layer: seeded by chunk and layer, so the same data gives the same
// plants; densityScale (quality tier) thins them.
void ScatterFoliage(const TerrainData& t, int foliage, int cx, int cz, float densityScale, std::vector<FoliageInstance>& out);

// Edits as commands ("terrain", undoable): region data as base64. Registered with the command bus on first use.
//   {"cmd":"terrain","target":"<object id>","args":{"asset":"<path>","map":"height|splat0|splat1|density","x":0,"z":0,"w":8,"h":8,"data":"<base64>"}}
void RegisterTerrainCommand();
std::string TerrainRegionBase64(const TerrainData& t, const std::string& map, int x, int z, int w, int h);
bool SetTerrainRegion(TerrainData& t, const std::string& map, int x, int z, int w, int h, const std::string& base64, std::string* error);

// Loaded terrains by path (the editor, the views, physics and scripts share one copy per file).
class TerrainRegistry {
public:
  static TerrainRegistry& Instance();
  // `asset`: Assets-relative path (or absolute). Folders searched: assetsRoot, then <working dir>/Assets, then BDG_ASSETS.
  TerrainData* Get(const std::string& asset, const std::string& assetsRoot = {}, std::string* error = nullptr);
  void Forget(const std::string& resolvedPath);
  void Clear();
  std::string Resolve(const std::string& asset, const std::string& assetsRoot = {}) const;
private:
  std::map<std::string, std::unique_ptr<TerrainData>> loaded_;
  std::map<std::string, std::string> failed_;
};

std::string Base64Encode(const uint8_t* data, size_t n);
bool Base64Decode(std::string_view text, std::vector<uint8_t>& out);
} // namespace bdg
