#include "bdg_test.h"
#include "Render3D/MeshGen.h"
#include "Render3D/Renderer3D.h"
#include "Render3D/ReferenceBoard.h"
#include "Assets/GltfImport.h"
#include "Runtime/Gizmo.h"
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
