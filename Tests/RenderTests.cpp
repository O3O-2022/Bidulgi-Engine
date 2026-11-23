#include "bdg_test.h"
#include "Render3D/MeshGen.h"
#include "Render3D/Renderer3D.h"
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
