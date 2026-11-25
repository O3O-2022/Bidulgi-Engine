// Terrain and foliage (round 6): the asset round trip, sampling, brushes, chunk LOD meshes (skirts cover every crack),
// foliage scattering, the undoable "terrain" command and the Jolt height field a body rests on.
#include "bdg_test.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetRefs.h"
#include "Core/Command.h"
#include "Physics/PhysicsWorld.h"
#include "Render3D/MeshGen.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Terrain/TerrainComponents.h"
#include "Terrain/TerrainData.h"
#include "Terrain/TerrainTool.h"
#include "Terrain/TerrainView.h"
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <format>
#include <fstream>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
std::string TempDir(const char* name) {
  const fs::path d = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::u8string u = d.generic_u8string();
  return std::string(u.begin(), u.end());
}
} // namespace

TEST_CASE("terrain: save / load round trip, sampling, normals (round 6)") {
  TerrainData t = MakeTerrain("Hills", 65, {64, 32}, 20, 8, 7);
  CHECK(t.resolution == 65 && t.heights.size() == 65u * 65u && t.splat.size() == 1 && t.layers.size() == 4);
  t.foliage.push_back(FoliageLayer{});
  t.foliage.push_back(FoliageLayer{"Trees", FoliageKind::Tree, 0.02f, -1, 0.8f, 1.4f, 150, 200, 70, 30, {0.2f, 0.4f, 0.15f}});
  const std::string dir = TempDir("bdg-terrain-test");
  std::string err;
  CHECK(SaveTerrain(t, dir + "/Hills.terrain", &err));
  CHECK(fs::exists(dir + "/Hills.height.r16") && fs::exists(dir + "/Hills.splat0.png") && fs::exists(dir + "/Hills.foliage.png"));
  TerrainData u;
  std::vector<std::string> problems;
  CHECK(LoadTerrain(dir + "/Hills.terrain", u, &problems, &err) && problems.empty());
  CHECK(u.heights == t.heights && u.splat == t.splat && u.density == t.density && u.size == t.size && u.height == t.height);
  CHECK(u.layers.size() == 4 && u.layers[1].name == "Rock" && u.foliage.size() == 2 && u.foliage[1].kind == FoliageKind::Tree && u.foliage[1].impostor == 70);
  CHECK(!LoadTerrain(dir + "/missing.terrain", u, nullptr, &err));

  // a known ramp: height = x / 64 * 20 (one metre up per 3.2 m along x)
  TerrainData r = MakeTerrain("Ramp", 33, {64, 64}, 20);
  for (int z = 0; z < 33; ++z)
    for (int x = 0; x < 33; ++x) r.heights[(size_t)z * 33 + x] = (uint16_t)std::lround(x / 32.f * 65535);
  CHECK(Near(r.HeightAt(32, 10), 10, 0.01f) && Near(r.HeightAt(33, 10), 10.3125f, 0.01f) && Near(r.HeightAt(-5, 10), 0, 1e-4f));
  const glm::vec3 n = r.NormalAt(32, 32);
  CHECK(Near(n.x, -0.2952f, 0.01f) && Near(n.z, 0, 1e-3f) && n.y > 0.9f); // slope 20 / 64: normal leans toward -x
  glm::vec3 hit;
  CHECK(TerrainTool::Raycast(r, {32, 50, 32}, {0, -1, 0}, 100, hit) && Near(hit.y, 10, 0.05f));
  CHECK(!TerrainTool::Raycast(r, {32, 50, 32}, {0, 1, 0}, 100, hit));
  fs::remove_all(dir);
}

TEST_CASE("terrain: brushes raise, lower, smooth, flatten, paint layers and foliage (round 6)") {
  TerrainData t = MakeTerrain("B", 65, {64, 64}, 20);
  ApplyTerrainBrush(t, TerrainBrushMode::Raise, {32, 32}, 8, 1);
  CHECK(t.HeightAt(32, 32) > 0.5f && t.HeightAt(32, 42) == 0 && t.HeightAt(36, 32) < t.HeightAt(32, 32));
  CHECK(t.dirtyX0 <= 24 && t.dirtyX1 >= 40 && t.heightDirty && !t.splatDirty);
  const float peak = t.HeightAt(32, 32);
  ApplyTerrainBrush(t, TerrainBrushMode::Lower, {32, 32}, 8, 0.5f);
  CHECK(t.HeightAt(32, 32) < peak);
  // smoothing a spike lowers it and lifts its neighbours
  TerrainData s = MakeTerrain("S", 33, {32, 32}, 10);
  s.heights[16 * 33 + 16] = 65535;
  ApplyTerrainBrush(s, TerrainBrushMode::Smooth, {16, 16}, 3, 1);
  CHECK(s.HeightAtSample(16, 16) < 10 && s.HeightAtSample(17, 16) > 0);
  ApplyTerrainBrush(t, TerrainBrushMode::Flatten, {32, 32}, 4, 1, 0, 0.25f);
  CHECK(Near(t.HeightAt(32, 32), 0.25f, 0.02f));
  // painting layer 2 makes it the strongest there; every sample's weights still sum to 255
  ApplyTerrainBrush(t, TerrainBrushMode::Paint, {10, 10}, 5, 1, 2);
  CHECK(t.LayerWeight(2, 10, 10) > 0.9f && t.LayerWeight(0, 10, 10) < 0.1f && t.LayerWeight(2, 30, 30) == 0);
  bool sums = true;
  for (size_t i = 0; i < t.heights.size(); ++i) sums = sums && t.splat[0][i * 4] + t.splat[0][i * 4 + 1] + t.splat[0][i * 4 + 2] + t.splat[0][i * 4 + 3] == 255;
  CHECK(sums);
  ApplyTerrainBrush(t, TerrainBrushMode::FoliageErase, {50, 50}, 6, 1, 0);
  CHECK(t.Density(0, 50, 50) < 0.05f && t.Density(0, 10, 50) == 1 && t.densityDirty);
  ApplyTerrainBrush(t, TerrainBrushMode::FoliagePaint, {50, 50}, 6, 1, 0);
  CHECK(t.Density(0, 50, 50) > 0.9f);
}

TEST_CASE("terrain: chunk meshes per LOD, skirts cover the cracks between LODs, LOD by distance (round 6)") {
  TerrainData t = MakeTerrain("C", 129, {128, 128}, 30, 12, 3);
  CHECK(TerrainChunksX(t) == 4 && TerrainLods(t) == 5);
  MeshData m0, m2;
  BuildChunkMesh(t, 1, 1, 0, 0, m0);
  CHECK(m0.vertices.size() == 33u * 33u && m0.indices.size() == 32u * 32u * 6);
  BuildChunkMesh(t, 1, 1, 2, 0, m2);
  CHECK(m2.vertices.size() == 9u * 9u && m2.indices.size() == 8u * 8u * 6);
  CHECK(CountInwardTriangles(m0) == 0);
  // the shared edge of a fine chunk (LOD 0) and its coarser neighbour (LOD 2): the fine edge's heights differ from the
  // coarse edge (a straight line between every 4th sample) by less than the skirt hangs down
  const float skirt = 1.f + t.height * 0.03f;
  float worst = 0;
  const int x = 64; // the edge between chunk 1 and 2
  for (int z = 32; z < 64; ++z) {
    const int a = z / 4 * 4, b = a + 4;
    const float coarse = t.HeightAtSample(x, a) + (t.HeightAtSample(x, b) - t.HeightAtSample(x, a)) * (z - a) / 4.f;
    worst = std::max(worst, std::fabs(t.HeightAtSample(x, z) - coarse));
  }
  CHECK(worst < skirt);
  MeshData sk;
  BuildChunkMesh(t, 1, 1, 0, skirt, sk);
  CHECK(sk.vertices.size() == 33u * 33u + 4 * 33u);
  glm::vec3 lo, hi;
  ChunkBounds(t, 1, 1, lo, hi);
  CHECK(lo.x == 32 && hi.x == 64 && lo.y <= hi.y && hi.y <= 30);
  CHECK(ChunkLod(10, 40, 5) == 0 && ChunkLod(50, 40, 5) == 1 && ChunkLod(100, 40, 5) == 2 && ChunkLod(10000, 40, 5) == 4);
  // tiers: more detail and plants higher up
  CHECK(TerrainTier(QualityTier::Potato).grassDensity == 0 && TerrainTier(QualityTier::High).grassDensity == 1 &&
        TerrainTier(QualityTier::Low).lodScale < TerrainTier(QualityTier::Ultra).lodScale && !TerrainTier(QualityTier::Low).triplanar);
}

TEST_CASE("foliage: scattering is seeded, follows density, layer weight and slope (round 6)") {
  TerrainData t = MakeTerrain("F", 65, {64, 64}, 5);
  FoliageLayer grass;
  grass.density = 4;
  grass.layer = 0;
  t.foliage.push_back(grass);
  std::vector<FoliageInstance> a, b;
  ScatterFoliage(t, 0, 0, 0, 1, a);
  ScatterFoliage(t, 0, 0, 0, 1, b);
  CHECK(a.size() > 3000 && a.size() < 5200 && a.size() == b.size() && a[10].position == b[10].position); // ~4 / m2 over 32 x 32 m
  bool onGround = true;
  for (const FoliageInstance& f : a) onGround = onGround && Near(f.position.y, t.HeightAt(f.position.x, f.position.z), 1e-4f) && f.position.x <= 32 && f.position.z <= 32;
  CHECK(onGround);
  ScatterFoliage(t, 0, 0, 0, 0.25f, b);
  CHECK(b.size() < a.size() / 3);
  // erase a patch: no plants in it, the others stay where they were
  ApplyTerrainBrush(t, TerrainBrushMode::FoliageErase, {16, 16}, 6, 4, 0);
  ScatterFoliage(t, 0, 0, 0, 1, b);
  size_t inPatch = 0;
  for (const FoliageInstance& f : b) inPatch += glm::length(glm::vec2(f.position.x, f.position.z) - glm::vec2(16, 16)) < 3.f;
  bool kept = true; // every plant left is one that was there before
  for (const FoliageInstance& f : b) kept = kept && std::any_of(a.begin(), a.end(), [&](const FoliageInstance& g) { return g.position == f.position; });
  CHECK(inPatch == 0 && b.size() < a.size() && kept);
  // gated by layer 0: painting layer 1 over a patch removes the grass there
  ApplyTerrainBrush(t, TerrainBrushMode::Paint, {8, 24}, 5, 4, 1);
  ScatterFoliage(t, 0, 0, 0, 1, b);
  inPatch = 0;
  for (const FoliageInstance& f : b) inPatch += glm::length(glm::vec2(f.position.x, f.position.z) - glm::vec2(8, 24)) < 2.f;
  CHECK(inPatch == 0);
}

TEST_CASE("terrain: the \"terrain\" command changes a region, undo puts it back, the files follow (round 6)") {
  const std::string dir = TempDir("bdg-terrain-cmd");
  TerrainData t = MakeTerrain("Cmd", 33, {32, 32}, 10);
  std::string err;
  CHECK(SaveTerrain(t, dir + "/Cmd.terrain", &err));
  Scene scene;
  scene.Create("ter", "Terrain");
  CHECK(scene.AddComponent("ter", bdg::Terrain::StaticType(), std::format(R"({{"asset":"{}/Cmd.terrain"}})", dir), &err));
  CommandBus bus(scene);
  RegisterTerrainCommand();
  TerrainData* live = TerrainRegistry::Instance().Get(dir + "/Cmd.terrain");
  CHECK(live != nullptr);
  std::vector<uint16_t> region(4 * 4, 40000);
  Command c;
  c.name = "terrain";
  c.target = "ter";
  c.args = std::format(R"({{"map":"height","x":10,"z":12,"w":4,"h":4,"data":"{}"}})", Base64Encode(reinterpret_cast<const uint8_t*>(region.data()), 32));
  CHECK(bus.Execute(c, &err));
  CHECK(live->heights[12 * 33 + 10] == 40000 && live->heights[12 * 33 + 9] == 0);
  TerrainData disk;
  CHECK(LoadTerrain(dir + "/Cmd.terrain", disk, nullptr, &err) && disk.heights[12 * 33 + 10] == 40000);
  CHECK(bus.History().back().label == "Terrain: sculpt");
  CHECK(bus.Undo(&err) && live->heights[12 * 33 + 10] == 0);
  CHECK(LoadTerrain(dir + "/Cmd.terrain", disk, nullptr, &err) && disk.heights[12 * 33 + 10] == 0);
  CHECK(bus.Redo(&err) && live->heights[13 * 33 + 13] == 40000);
  c.args = R"({"map":"height","x":31,"z":31,"w":4,"h":4,"data":""})";
  CHECK(!bus.Execute(c, &err)); // outside the map
  TerrainRegistry::Instance().Forget(live->path);
  fs::remove_all(dir);
}

TEST_CASE("terrain: a static height field collider holds a falling sphere at the terrain's height (round 6)") {
  const std::string dir = TempDir("bdg-terrain-phys");
  TerrainData t = MakeTerrain("P", 65, {64, 64}, 20, 10, 5);
  std::string err;
  CHECK(SaveTerrain(t, dir + "/P.terrain", &err));
  Scene scene;
  scene.Create("ter", "Terrain");
  scene.AddComponent("ter", Transform::StaticType(), R"({"position":[-32,-2,-32]})", &err);
  CHECK(scene.AddComponent("ter", bdg::Terrain::StaticType(), std::format(R"({{"asset":"{}/P.terrain"}})", dir), &err));
  scene.Create("ball", "Ball");
  scene.AddComponent("ball", Transform::StaticType(), R"({"position":[3,30,-5]})", &err);
  CHECK(scene.AddComponent("ball", Rigidbody::StaticType(), "", &err));
  CHECK(scene.AddComponent("ball", Collider::StaticType(), R"({"shape":"Sphere","radius":0.5})", &err));
  PhysicsWorld physics;
  physics.Begin(scene, {}, nullptr);
  CHECK(physics.Bodies3D() == 2);
  for (int i = 0; i < 400; ++i) physics.Step(scene, 1.f / 60.f);
  const Transform& tr = *static_cast<const Transform*>(scene.Find("ball")->Find("Transform")->data);
  const float ground = -2 + t.HeightAt(tr.position.x + 32, tr.position.z + 32);
  CHECK(Near(tr.position.y, ground + 0.5f, 0.12f)); // resting on the terrain (it may roll down the slope a little)
  PhysicsWorld::RayResult hit;
  CHECK(physics.Raycast({10, 50, 10}, {0, -1, 0}, 100, hit) && hit.object == "ter" && Near(hit.point.y, -2 + t.HeightAt(42, 42), 0.05f));
  physics.End();
  TerrainRegistry::Instance().Clear();
  fs::remove_all(dir);
}

TEST_CASE("terrain: a scene's terrain keeps its map files out of the build's unused assets (round 6)") {
  const std::string dir = TempDir("bdg-terrain-refs");
  TerrainData t = MakeTerrain("Isle", 33, {32, 32}, 5);
  std::string err;
  CHECK(SaveTerrain(t, dir + "/Assets/Terrain/Isle.terrain", &err));
  {
    std::ofstream(fs::path(dir) / "Assets/Main.scene", std::ios::binary)
        << "{\"v\":1,\"scene\":\"Main\"}\n{\"id\":\"g1\",\"name\":\"Ground\",\"Terrain\":{\"asset\":\"Terrain/Isle.terrain\"}}\n";
    std::ofstream(fs::path(dir) / "Assets/Terrain/Spare.png", std::ios::binary) << "x";
  }
  AssetDatabase db;
  CHECK(db.Open(dir + "/Assets", &err));
  AssetRefIndex index;
  index.Build(db);
  const std::vector<std::string> unused = index.Unused();
  auto has = [&](const char* p) { return std::find(unused.begin(), unused.end(), p) != unused.end(); };
  CHECK(!has("Terrain/Isle.terrain") && !has("Terrain/Isle.height.r16") && !has("Terrain/Isle.splat0.png") && !has("Terrain/Isle.foliage.png"));
  CHECK(has("Terrain/Spare.png"));
  fs::remove_all(dir);
}
