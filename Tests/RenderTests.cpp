#include "bdg_test.h"
#include "Render3D/MeshGen.h"
#include "Render3D/Renderer3D.h"
#include "Render3D/ReferenceBoard.h"
#include "Assets/GltfImport.h"
#include "Runtime/Gizmo.h"
#include "Runtime/Budget.h"
#include "Render3D/Quality.h"
#include "Core/ImageCompare.h"
#include "Render3D/Culling.h"
#include "Render3D/Lightmap.h"
#include "Render3D/RayScene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <filesystem>
#include <cmath>

using namespace bdg;

TEST_CASE("render: generated meshes wind counter-clockwise seen from outside (queue 3-0)") {
  struct { const char* name; MeshData mesh; } meshes[] = {
    {"quad", MakeQuad(1, 1)}, {"plane", MakePlaneXZ(2, 3)}, {"box", MakeBox(1, 2, 3)},
    {"sphere", MakeSphere(1)}, {"cylinder", MakeCylinder(0.5f, 2)}, {"cone", MakeCone(0.5f, 1)}};
  for (auto& m : meshes) {
    CHECK(!m.mesh.indices.empty());
    CHECK(m.mesh.indices.size() % 3 == 0);
    CHECK(CountInwardTriangles(m.mesh) == 0);
  }
  MeshData q = MakeQuad(1, 1);
  q.FlipWinding();
  CHECK(CountInwardTriangles(q) == 2); // the winding probe's clockwise quad really is clockwise
  MeshData quad = MakeQuad(2, 2);      // faces +Z, picture top-left at (-1, +1)
  bool topLeft = false;
  for (const Vertex3D& v : quad.vertices) {
    CHECK(v.nz == 1.f);
    if (v.u == 0 && v.v == 0) topLeft = v.px < 0 && v.py > 0;
  }
  CHECK(topLeft);
  MeshData plane = MakePlaneXZ(1, 1);
  for (const Vertex3D& v : plane.vertices) CHECK(v.ny == 1.f);
}

TEST_CASE("render: sRGB transfer and projection follow the coordinate conventions (queue 3-0)") {
  Color g = SrgbToLinear(118, 118, 118);
  CHECK(std::fabs(g.r - 0.181f) < 0.002f); // the 18% grey card
  CHECK(SrgbToLinear(0, 0, 0).r == 0.f);
  CHECK(std::fabs(SrgbToLinear(255, 255, 255).r - 1.f) < 1e-6f);

  Camera3D cam;
  cam.position = {0, 0, 5};
  cam.target = {0, 0, 0};
  float cx, cy, x, y;
  CHECK(ProjectToPixel(cam, 200, 100, {0, 0, 0}, cx, cy));
  CHECK(std::fabs(cx - 100) < 0.01f && std::fabs(cy - 50) < 0.01f);
  CHECK(ProjectToPixel(cam, 200, 100, {1, 0, 0}, x, y) && x > cx); // +X right
  CHECK(ProjectToPixel(cam, 200, 100, {0, 1, 0}, x, y) && y > cy); // +Y up (rows count from the bottom)
  CHECK(!ProjectToPixel(cam, 200, 100, {0, 0, 10}, x, y));          // behind a camera looking along -Z
}

TEST_CASE("gltf: the reference probe imports through cgltf and meshoptimizer (queue 3-1)") {
  std::string text = ReferenceBoard::ProbeGltf();
  ImportedModel m;
  std::string err;
  CHECK(ImportGltfMemory(text.data(), text.size(), ".", m, &err));
  CHECK(err.empty());
  CHECK(m.primitives.size() == 2);
  CHECK(m.materials.size() == 1 && m.materials[0].unlit && m.materials[0].metallic == 0.f);
  CHECK(m.materials[0].baseColorTex == 0);
  CHECK(m.textures.size() == 1 && m.textures[0].w == 4 && m.textures[0].h == 4 && m.textures[0].srgb);
  CHECK(m.textures[0].rgba.size() == 64 && m.textures[0].rgba[0] == 230); // top-left texel first
  CHECK(m.textures[0].nearest && m.textures[0].clamp);                     // from the glTF sampler
  CHECK(m.instances.size() == 2);
  // Parent (-4.4, 4.6, 0) + child (0, 0, 0.5): the quad's world origin.
  glm::vec3 o = glm::vec3(m.instances[0].world[3]);
  CHECK(std::fabs(o.x + 4.4f) < 1e-5f && std::fabs(o.y - 4.6f) < 1e-5f && std::fabs(o.z - 0.5f) < 1e-5f);
  CHECK(m.instances[0].node == "probe quad");
  const MeshData& quad = m.primitives[0].mesh;
  CHECK(quad.vertices.size() == 4 && quad.indices.size() == 6);
  CHECK(CountInwardTriangles(quad) == 0);
  const MeshData& tri = m.primitives[1].mesh; // no indices, no normals: generated
  CHECK(tri.indices.size() == 3 && m.primitives[1].material == -1);
  CHECK(CountInwardTriangles(tri) == 0 && std::fabs(tri.vertices[0].nz - 1.f) < 1e-5f);
  CHECK(m.stats.triangles == 3 && m.stats.skippedPrimitives == 0);
  CHECK(m.boundsMin.y < -19.f && m.boundsMax.y > 5.f);
}

TEST_CASE("gltf: external buffer under a non-ASCII folder, errors reported (queue 3-1)") {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / fs::path(u8"bdg 모델 테스트");
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::vector<uint8_t> bin;
  std::string text = ReferenceBoard::ProbeGltf(&bin);
  auto write = [](const fs::path& p, const void* d, size_t n) {
    std::FILE* f = _wfopen(p.c_str(), L"wb");
    if (f) { std::fwrite(d, 1, n, f); std::fclose(f); }
  };
  write(dir / u8"프로브.gltf", text.data(), text.size());
  write(dir / "probe data.bin", bin.data(), bin.size()); // the JSON says "probe%20data.bin"
  ImportedModel m;
  std::string err;
  CHECK(ImportGltf(dir / u8"프로브.gltf", m, &err));
  CHECK(m.instances.size() == 2 && m.textures.size() == 1 && !m.textures[0].rgba.empty());

  fs::remove(dir / "probe data.bin");
  CHECK(!ImportGltf(dir / u8"프로브.gltf", m, &err));
  CHECK(err == "file not found");
  const char junk[] = "{\"asset\": {\"version\": \"2.0\"}, \"meshes\": [";
  CHECK(!ImportGltfMemory(junk, sizeof(junk) - 1, dir, m, &err));
  CHECK(!err.empty());
  const char empty[] = "{\"asset\": {\"version\": \"2.0\"}}";
  CHECK(!ImportGltfMemory(empty, sizeof(empty) - 1, dir, m, &err));
  CHECK(err == "no triangle meshes in the default scene");
  fs::remove_all(dir);
}

TEST_CASE("gizmo: hit tests and drags follow the handles (queue 3-3)") {
  GizmoView v;
  v.camera.position = {0, 0, 10};
  v.camera.target = {0, 0, 0};
  v.camera.fovY = 50;
  v.x = 0; v.y = 0; v.w = 800; v.h = 600;
  GizmoFrame f = MakeGizmoFrame(v, {0, 0, 0}, glm::mat3(1.f));
  glm::vec2 o, tip;
  CHECK(v.Project(f.origin, o) && v.Project(f.origin + glm::vec3(f.size, 0, 0), tip));
  CHECK(std::fabs(glm::length(tip - o) - kGizmoPixels) < 0.5f); // constant size on screen
  CHECK(std::fabs(o.x - 400) < 0.01f && std::fabs(o.y - 300) < 0.01f);

  glm::vec2 px;
  CHECK(GizmoHandlePoint(GizmoMode::Move, GizmoHandle::X, f, v, px));
  CHECK(GizmoHit(GizmoMode::Move, f, v, px) == GizmoHandle::X);
  CHECK(GizmoHit(GizmoMode::Move, f, v, o) == GizmoHandle::Center);
  CHECK(GizmoHit(GizmoMode::Move, f, v, {20, 20}) == GizmoHandle::None);
  CHECK(GizmoHit(GizmoMode::Move, f, v, o + glm::vec2(0, -40)) == GizmoHandle::Y); // screen up = +Y
  CHECK(GizmoHit(GizmoMode::Move, f, v, o + glm::vec2(40, -40)) == GizmoHandle::XY); // plane square facing us

  GizmoDrag d; // move along X: 55 px to the right is half an axis length
  CHECK(d.Begin(GizmoMode::Move, GizmoHandle::X, f, v, px));
  GizmoDelta m = d.Update(px + glm::vec2(55, 30), false);
  CHECK(std::fabs(m.move.x - f.size * 0.5f) < f.size * 0.01f && m.move.y == 0 && m.move.z == 0);
  GizmoDelta snapped = d.Update(px + glm::vec2(55, 0), true);
  CHECK(std::fabs(snapped.move.x - std::round(f.size * 0.5f)) < 1e-4f);

  glm::vec2 pp; // move in the XY plane
  CHECK(GizmoHandlePoint(GizmoMode::Move, GizmoHandle::XY, f, v, pp));
  CHECK(d.Begin(GizmoMode::Move, GizmoHandle::XY, f, v, pp));
  m = d.Update(pp + glm::vec2(22, -33), false);
  CHECK(m.move.x > 0 && m.move.y > 0 && std::fabs(m.move.z) < 1e-5f);

  // Rotate around Z (pointing at the viewer): a counter-clockwise screen drag is a positive turn.
  glm::vec2 rz;
  CHECK(GizmoHandlePoint(GizmoMode::Rotate, GizmoHandle::Z, f, v, rz));
  CHECK(GizmoHit(GizmoMode::Rotate, f, v, rz) == GizmoHandle::Z);
  glm::vec2 start = o + glm::vec2(kGizmoPixels, 0), quarter = o + glm::vec2(0, -kGizmoPixels);
  CHECK(d.Begin(GizmoMode::Rotate, GizmoHandle::Z, f, v, start));
  GizmoDelta r = d.Update(quarter, false);
  CHECK(std::fabs(r.angle - 90.f) < 0.01f);
  glm::vec3 turned = r.rotate * glm::vec3(1, 0, 0);
  CHECK(std::fabs(turned.y - 1.f) < 1e-4f); // +X turns to +Y around +Z (right-hand rule)
  CHECK(std::fabs(d.Update(o + glm::vec2(kGizmoPixels, -8), true).angle) < 1e-4f); // 4 degrees snaps to 0

  // Scale along X: dragging the handle one axis length outward doubles it.
  glm::vec2 sx;
  CHECK(GizmoHandlePoint(GizmoMode::Scale, GizmoHandle::X, f, v, sx));
  CHECK(d.Begin(GizmoMode::Scale, GizmoHandle::X, f, v, sx));
  GizmoDelta s = d.Update(sx + glm::vec2(kGizmoPixels, 0), false);
  CHECK(std::fabs(s.scale.x - 2.f) < 1e-3f && s.scale.y == 1.f && s.scale.z == 1.f);
}

TEST_CASE("quality: tiers, project range, overrides and the tone curve (queue 3-5)") {
  QualitySettings potato = QualityPreset(QualityTier::Potato), low = QualityPreset(QualityTier::Low);
  QualitySettings medium = QualityPreset(QualityTier::Medium), high = QualityPreset(QualityTier::High);
  CHECK(potato.lighting == LightingMode::Unlit && potato.maxLights == 0 && !potato.shadows && !potato.toneMap);
  CHECK(low.lighting == LightingMode::Vertex && low.maxLights == 4 && !low.shadows && low.textureBias == 2);
  CHECK(medium.lighting == LightingMode::Pixel && medium.maxLights == 8 && medium.shadows && medium.fxaa && medium.textureBias == 1);
  CHECK(high.bloom && high.msaa == 4 && !high.fxaa && high.shadowMapSize == 2048 && high.textureBias == 0);
  QualitySettings ref = ReferenceQuality();
  CHECK(!ref.toneMap && !ref.bloom && !ref.fxaa && ref.viewDistance == 1.f);

  QualityTier t;
  CHECK(TierFromString("low", t) && t == QualityTier::Low);
  CHECK(TierFromString("고사양", t) && t == QualityTier::High);
  CHECK(!TierFromString("epic", t));

  ProjectQuality pq;
  std::string err;
  CHECK(ParseProjectQuality(R"({"min":"low","max":"high","default":"ultra"})", pq, &err));
  CHECK(pq.min == QualityTier::Low && pq.max == QualityTier::High && pq.fallback == QualityTier::High); // default clamped
  CHECK(pq.Clamp(QualityTier::Potato) == QualityTier::Low);
  CHECK(pq.Next(QualityTier::Medium) == QualityTier::High && pq.Next(QualityTier::High) == QualityTier::Low); // wraps
  CHECK(!ParseProjectQuality(R"({"min":"high","max":"low"})", pq, &err) && pq.min == QualityTier::Potato);
  CHECK(!ParseProjectQuality(R"({"min":"epic"})", pq, &err) && err.find("epic") != std::string::npos);

  QualitySettings q = medium;
  CHECK(ApplyQualityOverrides("msaa=8,bloom=1,lights=2,exposure=1.5", q, &err));
  CHECK(q.msaa == 8 && q.bloom && q.maxLights == 2 && q.exposure == 1.5f && q.fxaa);
  CHECK(!ApplyQualityOverrides("sparkle=1", q, &err));
  // 3-5b: high and ultra get 3 shadow cascades, soft shadows and SSAO; medium and the reference stay as they were
  CHECK(high.shadowCascades == 3 && high.shadowSoftness > 0 && high.ssao);
  CHECK(medium.shadowCascades == 1 && medium.shadowSoftness == 0 && !medium.ssao);
  CHECK(ref.shadowCascades == 1 && !ref.ssao);
  CHECK(high.clustered && high.maxLights == 256 && QualityPreset(QualityTier::Ultra).maxLights == kMaxClusteredLights);
  CHECK(!medium.clustered && !ref.clustered && ref.maxLights == 16); // Forward+ only on the high tiers
  CHECK(ApplyQualityOverrides("cascades=9,softness=0.05,ssao=1,aoradius=0.8", q, &err));
  CHECK(q.shadowCascades == 4 && q.shadowSoftness == 0.05f && q.ssao && q.ssaoRadius == 0.8f); // cascades clamped to 4
  // point / spot shadows (3-4b): none below medium, 1 / 4 / 8 lights, none on the reference board
  CHECK(QualityPreset(QualityTier::Potato).localShadows == 0 && QualityPreset(QualityTier::Low).localShadows == 0);
  CHECK(medium.localShadows == 1 && high.localShadows == 4 && QualityPreset(QualityTier::Ultra).localShadows == 8 && ref.localShadows == 0);
  CHECK(QualityPreset(QualityTier::Ultra).localShadowSize == 1024 && high.localShadowSize == 512);
  CHECK(QualityPreset(QualityTier::Ultra).taa && !high.taa && !medium.taa && !ref.taa); // round 6: TAA on ultra
  CHECK(ApplyQualityOverrides("taa=1,sharpen=3", q, &err) && q.taa && q.taaSharpen == 1.f);
  CHECK(high.ssr && QualityPreset(QualityTier::Ultra).ssr && !medium.ssr); // round 6: SSR on high and ultra
  CHECK(high.volumetricFog && !medium.volumetricFog && QualityPreset(QualityTier::Ultra).fogSlices == 64); // volumetric fog
  CHECK(ApplyQualityOverrides("volfog=0,fogslices=1000", q, &err) && !q.volumetricFog && q.fogSlices == 128);
  CHECK(ApplyQualityOverrides("ssr=1,ssrsteps=500,ssrdistance=0", q, &err) && q.ssr && q.ssrSteps == 128 && q.ssrDistance == 1.f);
  CHECK(ApplyQualityOverrides("localshadows=40,localshadowmap=8", q, &err));
  CHECK(q.localShadows == 16 && q.localShadowSize == 64); // clamped

  // texture groups (3-5c): limits per tier, the first mip that fits, overrides
  CHECK(QualityPreset(QualityTier::Potato).TextureMax(TextureGroup::World) == 512 && medium.TextureMax(TextureGroup::Normal) == 1024);
  CHECK(high.TextureMax(TextureGroup::Masks) == 4096 && QualityPreset(QualityTier::Potato).TextureMax(TextureGroup::UI) == 16384);
  CHECK(FirstMipWithin(2048, 1024, 12, 512) == 2 && FirstMipWithin(512, 512, 10, 4096) == 0 && FirstMipWithin(4096, 4096, 3, 16) == 2);
  CHECK(FirstMipWithin(1000, 600, 11, 500) == 1); // 1000 -> 500
  CHECK(ApplyQualityOverrides("tex.normal=128,tex.world=300", q, &err) && q.TextureMax(TextureGroup::Normal) == 128 && q.textureMax[0] == 300);
  CHECK(!ApplyQualityOverrides("tex.hair=1", q, &err));
  // automatic tier: benchmark time at high -> tier; cache per device
  CHECK(TierFromBenchmark(1.0) == QualityTier::Ultra && TierFromBenchmark(8.0) == QualityTier::High);
  CHECK(TierFromBenchmark(20.0) == QualityTier::Medium && TierFromBenchmark(35.0) == QualityTier::Low);
  CHECK(TierFromBenchmark(300.0) == QualityTier::Potato && TierFromBenchmark(0) == QualityTier::Medium);
  {
    double ms = 0;
    QualityTier tier = QualityTier::Potato;
    const std::string json = BenchmarkCacheJson("GPU \"X\" | 4.5", 3.25, QualityTier::High);
    CHECK(ParseBenchmarkCache(json, "GPU \"X\" | 4.5", ms, tier) && tier == QualityTier::High && std::abs(ms - 3.25) < 1e-6);
    CHECK(!ParseBenchmarkCache(json, "other GPU", ms, tier) && !ParseBenchmarkCache("{", "GPU", ms, tier));
  }
  { // frame drop guard: needs a full window of slow frames, then waits
    QualityGovernor g;
    int steps = 0;
    double now = 0;
    for (int i = 0; i < 200; ++i, now += 0.016) steps += g.Feed(now, 15) ? 1 : 0; // within budget
    CHECK(steps == 0);
    for (int i = 0; i < 29; ++i, now += 0.03) steps += g.Feed(now, 25) ? 1 : 0;
    CHECK(steps == 0); // the window still holds the fast frames
    for (int i = 0; i < 200 && !steps; ++i, now += 0.03) steps += g.Feed(now, 25) ? 1 : 0; // until the window is slow
    CHECK(steps == 1 && g.lastStepAverageMs > 20);
    for (int i = 0; i < 100; ++i, now += 0.03) steps += g.Feed(now, 25) ? 1 : 0; // 3 s: inside the 4 s pause
    CHECK(steps == 1);
    for (int i = 0; i < 100; ++i, now += 0.03) steps += g.Feed(now, 25) ? 1 : 0;
    CHECK(steps == 2);
  }
  {
    ProjectQuality autoPq;
    CHECK(ParseProjectQuality(R"({"min":"low","default":"auto"})", autoPq) && autoPq.autoTier && autoPq.min == QualityTier::Low);
  }

  CHECK(ToneMapACES(0.f) == 0.f && ToneMapACES(100.f) == 1.f);
  float prev = 0;
  for (float x = 0.01f; x < 8.f; x *= 1.3f) { CHECK(ToneMapACES(x) > prev); prev = ToneMapACES(x); } // keeps order
}

TEST_CASE("image compare: FLIP-style error is zero for equal pictures and grows with the difference (queue 3-9)") {
  const int W = 64, H = 48;
  std::vector<uint8_t> a(W * H * 4), b;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t* p = &a[(y * W + x) * 4];
      p[0] = (uint8_t)(x * 4); p[1] = (uint8_t)(y * 5); p[2] = (uint8_t)(((x / 8 + y / 8) % 2) * 200 + 20); p[3] = 255;
    }
  ImageDiff d;
  CHECK(CompareImages(a.data(), a.data(), W, H, d));
  CHECK(d.mean == 0 && d.max == 0 && d.over == 0 && d.heat.size() == (size_t)W * H * 4 && d.heat[0] == 0);

  b = a; // a little brighter everywhere: small error
  for (size_t i = 0; i < b.size(); ++i) if (i % 4 != 3) b[i] = (uint8_t)std::min(255, b[i] + 3);
  CHECK(CompareImages(a.data(), b.data(), W, H, d));
  double small = d.mean;
  CHECK(small > 0 && small < 0.15); // about FLIP's "just noticeable" range

  b = a; // a red square: large error there, none far away
  for (int y = 10; y < 20; ++y) for (int x = 10; x < 20; ++x) { uint8_t* p = &b[(y * W + x) * 4]; p[0] = 255; p[1] = 0; p[2] = 0; }
  CHECK(CompareImages(a.data(), b.data(), W, H, d));
  CHECK(d.error[15 * W + 15] > 0.5f && d.error[40 * W + 60] < 0.01f && d.over > 0.02); // local: strong inside, none far away

  std::vector<uint8_t> black(W * H * 4, 0), white(W * H * 4, 255);
  for (size_t i = 3; i < black.size(); i += 4) black[i] = 255;
  CHECK(CompareImages(black.data(), white.data(), W, H, d));
  CHECK(d.mean > 0.9);
  CHECK(!CompareImages(nullptr, a.data(), W, H, d));
}

TEST_CASE("culling: frustum planes and transformed boxes (queue 3-10)") {
  Camera3D cam;
  cam.position = {0, 0, 10};
  cam.target = {0, 0, 0};
  cam.fovY = 60;
  cam.nearZ = 0.1f;
  cam.farZ = 100;
  Frustum f = Frustum::FromMatrix(cam.Projection(1.f) * cam.View());
  CHECK(f.Intersects({-1, -1, -1}, {1, 1, 1}));         // in front
  CHECK(!f.Intersects({-1, -1, 11}, {1, 1, 12}));       // behind the camera
  CHECK(!f.Intersects({50, -1, -1}, {52, 1, 1}));       // far to the right
  CHECK(!f.Intersects({-1, -1, -200}, {1, 1, -150}));   // beyond the far plane
  CHECK(f.Intersects({5.5f, -1, -1}, {50, 1, 1}));      // straddles the right plane: kept (conservative)
  CHECK(f.Intersects({-1000, -1000, -50}, {1000, 1000, -40})); // larger than the view: kept
  glm::vec3 lo, hi; // a unit box turned 45 degrees about Y and moved: extents grow to sqrt(2) / 2
  glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.f), {3, 0, 0}), glm::radians(45.f), glm::vec3(0, 1, 0));
  TransformBounds(m, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, lo, hi);
  CHECK(std::fabs(hi.x - 3 - 0.70710678f) < 1e-5f && std::fabs(lo.x - 3 + 0.70710678f) < 1e-5f && std::fabs(hi.y - 0.5f) < 1e-6f);
}

TEST_CASE("gi: ray scene hits, misses and back faces (GI Tier 0-C)") {
  MeshData box = MakeBox(2, 2, 2); // centred at the origin, faces wound outward
  std::vector<glm::vec3> tris;
  for (uint32_t i : box.indices) tris.push_back({box.vertices[i].px, box.vertices[i].py, box.vertices[i].pz});
  RayScene scene;
  scene.Build(tris);
  CHECK(scene.Triangles() == box.indices.size() / 3);
  RayHit hit;
  CHECK(scene.Intersect({0, 0, 5}, {0, 0, -1}, 100, hit));
  CHECK(std::abs(hit.t - 4.f) < 1e-4f);
  CHECK(!hit.backface);
  CHECK(scene.Intersect({0, 0, 0}, {1, 0, 0}, 100, hit)); // from inside: the wall's back
  CHECK(hit.backface && std::abs(hit.t - 1.f) < 1e-4f);
  CHECK(!scene.Intersect({0, 3, 5}, {0, 0, -1}, 100, hit)); // passes above
  CHECK(!scene.Occluded({0, 0, 5}, {0, 0, -1}, 3.5f));      // stops short of the box
  CHECK(scene.Occluded({0, 0, 5}, {0, 0, -1}, 4.5f));
}

TEST_CASE("gi: lightmap bake on the CPU: open sky, contact shadow, unwrap (GI Tier 0-C)") {
  // A 10 m floor under a uniform white sky (radiance 1, no lights): open floor gets ambient 1 (E / pi), the floor
  // under a box sitting on it gets less. Meshes need no GPU: the bake reads Mesh3D::source only.
  auto cpuMesh = [](const MeshData& d) {
    Mesh3D m;
    m.indexCount = (uint32_t)d.indices.size();
    m.source = std::make_shared<const MeshData>(d);
    return m;
  };
  Mesh3D floor = cpuMesh(MakePlaneXZ(10, 10)), box = cpuMesh(MakeBox(1, 1, 1));
  std::vector<DrawItem3D> items(2);
  items[0].mesh = &floor;
  items[0].lightmapped = true;
  items[1].mesh = &box;
  items[1].model = glm::translate(glm::mat4(1.f), glm::vec3(0, 0.5f, 0));
  Light3D light;
  light.intensity = 0;
  LightmapBakeInput in;
  in.items = &items;
  in.light = &light;
  in.sky = [](const glm::vec3&) { return glm::vec3(1); };
  LightmapSettings s;
  s.texelsPerMeter = 6;
  s.samples = 64;
  s.bounces = 1;
  LightmapResult r;
  std::string err;
  CHECK(BakeLightmapAtlas(in, s, r, &err));
  CHECK(err.empty());
  CHECK(r.width > 0 && r.height > 0 && r.rgba.size() == (size_t)r.width * r.height * 4);
  CHECK(r.itemInstance.size() == 2 && r.itemInstance[0] == 0 && r.itemInstance[1] == -1);
  CHECK(r.meshes.size() == 1 && !r.meshes[0].indices.empty());
  bool uvInside = true;
  for (const Vertex3D& v : r.meshes[0].vertices) uvInside = uvInside && v.lu >= 0 && v.lu <= 1 && v.lv >= 0 && v.lv <= 1;
  CHECK(uvInside);
  auto at = [&](glm::vec3 p) { // nearest covered texel
    float best = 1e30f;
    float value = -1;
    for (size_t i = 0; i < r.texelPos.size(); ++i) {
      float d = glm::distance(r.texelPos[i], p);
      if (d < best) { best = d; value = r.rgba[r.texelIndex[i] * 4]; }
    }
    return value;
  };
  float open = at({4, 0, 4}), nearBox = at({0.75f, 0, 0});
  CHECK(std::abs(open - 1.f) < 0.12f); // the corner sees almost the whole sky (box far away)
  CHECK(nearBox < open * 0.85f);          // next to the box: part of the sky is blocked
  LightmapResult none;
  items[0].lightmapped = false;
  CHECK(!BakeLightmapAtlas(in, s, none, &err));
  CHECK(!err.empty());
}

TEST_CASE("gi: baked GI cache key follows every input (GI Tier 0-D)") {
  Renderer3D r; // no GPU needed for the hash
  Mesh3D a, b;
  a.contentHash = 1;
  b.contentHash = 2;
  std::vector<DrawItem3D> items(2);
  items[0].mesh = &a;
  items[1].mesh = &b;
  Light3D light;
  const uint64_t base = r.GIInputHash(items, light);
  CHECK(base == r.GIInputHash(items, light)); // stable
  auto changed = [&](auto edit) {
    std::vector<DrawItem3D> it = items;
    Light3D l = light;
    edit(it, l);
    return r.GIInputHash(it, l) != base;
  };
  CHECK(changed([](std::vector<DrawItem3D>& it, Light3D&) { it[1].model[3][0] = 0.5f; }));          // moved
  CHECK(changed([](std::vector<DrawItem3D>& it, Light3D&) { it[0].lightmapped = true; }));          // static GI flag
  CHECK(changed([](std::vector<DrawItem3D>& it, Light3D&) { it[0].material.baseColor.r = 0.1f; })); // colour
  CHECK(changed([&](std::vector<DrawItem3D>& it, Light3D&) { it[1].mesh = &a; }));                  // other mesh data
  CHECK(changed([](std::vector<DrawItem3D>&, Light3D& l) { l.intensity = 3; }));                     // light
  CHECK(changed([](std::vector<DrawItem3D>&, Light3D& l) { l.locals.push_back({}); }));              // a lamp
  LightmapSettings s;
  s.samples = 128;
  CHECK(r.GIInputHash(items, light, s) != base);
  ProbeVolume3D v;
  r.SetVolume(&v); // the probes and volume set on the renderer count too
  CHECK(r.GIInputHash(items, light) != base);
}

TEST_CASE("render: colour grading table and .cube files (queue 3-5b)") {
  // an identity .cube (2^3, red fastest) with comments, title and the 0..1 domain
  const char* identity = "# made by hand\nTITLE \"identity\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n"
                         "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
  int size = 0;
  std::vector<float> rgb;
  std::string err;
  CHECK(ParseCubeLut(identity, size, rgb, &err));
  CHECK(size == 2 && rgb.size() == 24);
  CHECK(!ParseCubeLut("LUT_3D_SIZE 2\n0 0 0\n", size, rgb, &err) && err.find("values") != std::string::npos); // too few
  CHECK(!ParseCubeLut("LUT_1D_SIZE 16\n", size, rgb, &err));
  CHECK(!ParseCubeLut("LUT_3D_SIZE 2\nDOMAIN_MAX 2 2 2\n", size, rgb, &err));
  CHECK(!ParseCubeLut("0 0 0\n", size, rgb, &err));

  ColorGrading3D g;
  CHECK(g.Neutral());
  const glm::vec3 c{0.8f, 0.4f, 0.2f};
  CHECK(glm::distance(ApplyGrading(g, c), c) < 1e-5f); // neutral: unchanged
  ColorGrading3D table;
  CHECK(ParseCubeLut(identity, table.lutSize, table.lut, &err));
  CHECK(!table.Neutral());
  CHECK(glm::distance(ApplyGrading(table, c), c) < 1e-5f); // identity table: unchanged (trilinear)
  ColorGrading3D grey;
  grey.saturation = 0;
  glm::vec3 gc = ApplyGrading(grey, c);
  CHECK(std::abs(gc.r - gc.g) < 1e-5f && std::abs(gc.g - gc.b) < 1e-5f);
  ColorGrading3D warm;
  warm.temperature = 1;
  glm::vec3 wc = ApplyGrading(warm, glm::vec3(0.5f));
  CHECK(wc.r > 0.55f && wc.b < 0.45f);
  warm.intensity = 0; // fully blended back
  CHECK(glm::distance(ApplyGrading(warm, glm::vec3(0.5f)), glm::vec3(0.5f)) < 1e-5f);
  std::vector<uint8_t> lut = BuildGradingLut(g, 8);
  CHECK(lut.size() == 8 * 8 * 8 * 4);
  CHECK(lut[((7 * 8 + 0) * 8 + 7) * 4 + 0] == 255 && lut[((7 * 8 + 0) * 8 + 7) * 4 + 1] == 0 && lut[((7 * 8 + 0) * 8 + 7) * 4 + 2] == 255);

  // the other 3-5b tier settings: anisotropy and blob shadows
  CHECK(QualityPreset(QualityTier::Ultra).anisotropy == 16 && QualityPreset(QualityTier::Medium).anisotropy == 4);
  CHECK(ReferenceQuality().anisotropy == 1);
  CHECK(QualityPreset(QualityTier::Low).blobShadows && !QualityPreset(QualityTier::Medium).blobShadows);
}

TEST_CASE("editor: grid, rest-on-surface, align and distribute maths (queue 3-3b)") {
  CHECK(SnapValue(1.26f, 0.25f) == 1.25f && SnapValue(-0.3f, 0.5f) == -0.5f && SnapValue(0.7f, 0) == 0.7f);
  // a move along x from x = 0.3 lands on the 0.5 grid; y and z keep the free move
  glm::vec3 m = SnapMoveToGrid({0.3f, 0.5f, 4}, {0.9f, 0.2f, 0}, {true, false, false}, 0.5f);
  CHECK(std::abs(0.3f + m.x - 1.f) < 1e-5f && std::abs(m.y - 0.2f) < 1e-6f);
  // a 1 m cube with its pivot in the middle rests 0.5 m above a floor (normal +y), 0.5 m off a wall (normal +x)
  CHECK(std::abs(RestOffset({0, 0.5f, 0}, {-0.5f, 0, -0.5f}, {0.5f, 1, 0.5f}, {0, 1, 0}) - 0.5f) < 1e-6f);
  CHECK(std::abs(RestOffset({0, 0, 0}, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {1, 0, 0}) - 0.5f) < 1e-6f);
  // three boxes on x: [0,1], [1.5,2] and [5,7]
  std::vector<AlignBox> boxes = {{{0, 0, 0}, {1, 1, 1}}, {{1.5f, 0, 0}, {2, 1, 1}}, {{5, 0, 0}, {7, 1, 1}}};
  std::vector<float> lo = AlignOffsets(boxes, 0, AlignMode::Min), hi = AlignOffsets(boxes, 0, AlignMode::Max);
  CHECK(lo[0] == 0 && lo[1] == -1.5f && lo[2] == -5);
  CHECK(hi[0] == 6 && hi[1] == 5 && hi[2] == 0);
  std::vector<float> mid = AlignOffsets(boxes, 0, AlignMode::Center); // all centres to 3.5
  CHECK(std::abs(0.5f + mid[0] - 3.5f) < 1e-6f && std::abs(1.75f + mid[1] - 3.5f) < 1e-6f && std::abs(6 + mid[2] - 3.5f) < 1e-6f);
  std::vector<float> dist = AlignOffsets(boxes, 0, AlignMode::Distribute); // span 0..7, sizes 3.5 -> gaps 1.75
  CHECK(dist[0] == 0 && dist[2] == 0 && std::abs(1.5f + dist[1] - 2.75f) < 1e-5f);
  CHECK(AlignOffsets({boxes[0], boxes[1]}, 0, AlignMode::Distribute) == std::vector<float>(2, 0.f)); // needs three
  CHECK(AlignOffsets({boxes[0]}, 0, AlignMode::Min) == std::vector<float>(1, 0.f));
  Measure ruler{{1, 0, 0}, {4, 4, 0}};
  CHECK(std::abs(ruler.Length() - 5.f) < 1e-6f && ruler.Delta() == glm::vec3(3, 4, 0));
}

TEST_CASE("render: greybox blocks are closed, sized from the bottom centre, with world-scale UVs (queue 3-3c)") {
  for (BlockShape s : {BlockShape::Box, BlockShape::Stairs, BlockShape::Ramp, BlockShape::Cylinder}) {
    MeshData m = MakeBlock(s, 2, 1.5f, 3, 5);
    CHECK(!m.indices.empty() && m.indices.size() % 3 == 0);
    CHECK(CountInwardTriangles(m) == 0);
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const Vertex3D& v : m.vertices) {
      lo = glm::min(lo, glm::vec3(v.px, v.py, v.pz));
      hi = glm::max(hi, glm::vec3(v.px, v.py, v.pz));
    }
    CHECK(glm::length(lo - glm::vec3(-1, 0, -1.5f)) < 1e-4f && glm::length(hi - glm::vec3(1, 1.5f, 1.5f)) < 1e-4f);
  }
  // stairs: five treads at 0.3, 0.6 ... 1.5 m, the lowest at the +Z end
  MeshData st = MakeBlock(BlockShape::Stairs, 2, 1.5f, 3, 5);
  std::vector<float> treads;
  for (const Vertex3D& v : st.vertices)
    if (v.ny > 0.99f && std::find_if(treads.begin(), treads.end(), [&](float t) { return std::abs(t - v.py) < 1e-4f; }) == treads.end())
      treads.push_back(v.py);
  std::sort(treads.begin(), treads.end());
  CHECK(treads.size() == 5 && std::abs(treads[0] - 0.3f) < 1e-4f && std::abs(treads[4] - 1.5f) < 1e-4f);
  bool lowFront = false;
  for (const Vertex3D& v : st.vertices) lowFront = lowFront || (v.ny > 0.99f && std::abs(v.py - 0.3f) < 1e-4f && v.pz > 1.4f);
  CHECK(lowFront);
  // world-scale UVs: across the top of a 2 m box the texture repeats twice
  MeshData box = MakeBlock(BlockShape::Box, 2, 1, 1);
  float umin = 1e9f, umax = -1e9f;
  for (const Vertex3D& v : box.vertices)
    if (v.ny > 0.99f) { umin = std::min(umin, v.u); umax = std::max(umax, v.u); }
  CHECK(std::abs((umax - umin) - 2.f) < 1e-4f);
}

TEST_CASE("editor: platform budgets, tiers, project overrides and bars (queue 3-7)") {
  const BudgetLimits android = DefaultBudget(1);
  CHECK(std::string(PlatformName(1)) == "Android" && android.draws == 200 && android.triangles == 1.0e6);
  const BudgetLimits low = BudgetForTier(android, QualityTier::Low), high = BudgetForTier(android, QualityTier::High);
  CHECK(low.draws == 100 && high.draws == 400 && BudgetForTier(android, QualityTier::Potato).lights == 0);
  std::array<BudgetLimits, kBudgetPlatforms> medium{DefaultBudget(0), DefaultBudget(1), DefaultBudget(2), DefaultBudget(3)};
  std::string err;
  CHECK(ParseBudgets(R"({"Android":{"draws":250,"shadowLights":1},"Web":{"textureMB":256}})", medium, &err));
  CHECK(medium[1].draws == 250 && medium[1].shadowLights == 1 && medium[1].triangles == 1.0e6 && medium[3].textureMB == 256);
  CHECK(!ParseBudgets(R"({"Switch":{"draws":1}})", medium, &err) && err.find("Switch") != std::string::npos);
  CHECK(!ParseBudgets(R"({"Android":{"draws":"many"}})", medium, &err));
  CHECK(!ParseBudgets(R"({"Android":{"polys":1}})", medium, &err) && err.find("polys") != std::string::npos);
  BudgetUse use{182, 1.18e6, 310, 3, 1};
  std::vector<BudgetBar> bars = EvaluateBudget(use, DefaultBudget(1));
  CHECK(bars.size() == 4);
  CHECK(bars[0].level == 1 && bars[0].text == "182 / 200");  // 91 %: near
  CHECK(bars[1].level == 2 && bars[1].text == "1.2M / 1M");   // over
  CHECK(bars[2].level == 0 && bars[2].text == "310 / 512 MB");
  CHECK(bars[3].level == 0 && bars[3].text == "3 / 8 (그림자 1 / 2)");
  use.shadowLights = 3; // more shadowed lights than allowed: the light bar is over even with few lights
  CHECK(EvaluateBudget(use, DefaultBudget(1))[3].level == 2);
  CHECK(FormatCount(980) == "980" && FormatCount(12400) == "12.4k");
}
