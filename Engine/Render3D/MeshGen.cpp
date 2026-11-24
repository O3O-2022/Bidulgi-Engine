#include "Render3D/MeshGen.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace bdg {
namespace {
struct V3 { float x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Normalize(V3 a) { float l = std::sqrt(Dot(a, a)); return l > 0 ? a * (1.f / l) : a; }

uint32_t Push(MeshData& m, V3 p, V3 n, float u, float v) {
  m.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u, v});
  return (uint32_t)m.vertices.size() - 1;
}
void Tri(MeshData& m, uint32_t a, uint32_t b, uint32_t c) { m.indices.insert(m.indices.end(), {a, b, c}); }

// Quad with normal n = u x v: corners c -u -v, c +u -v, c +u +v, c -u +v are counter-clockwise seen from n.
// Picture top is +v, so uv (0,0) sits at c - u + v.
void Face(MeshData& m, V3 c, V3 u, V3 v) {
  V3 n = Normalize(Cross(u, v));
  uint32_t i0 = Push(m, c - u - v, n, 0, 1), i1 = Push(m, c + u - v, n, 1, 1);
  uint32_t i2 = Push(m, c + u + v, n, 1, 0), i3 = Push(m, c - u + v, n, 0, 0);
  Tri(m, i0, i1, i2);
  Tri(m, i0, i2, i3);
}

// Point on the unit circle in the XZ plane. Increasing angle turns +X toward -Z (right-hand rule around +Y).
V3 Ring(float a) { return {std::cos(a), 0, -std::sin(a)}; }

void Disc(MeshData& m, float radius, float y, bool up, int slices) {
  V3 n{0, up ? 1.f : -1.f, 0};
  uint32_t c = Push(m, {0, y, 0}, n, 0.5f, 0.5f);
  uint32_t first = (uint32_t)m.vertices.size();
  for (int j = 0; j <= slices; ++j) {
    V3 r = Ring(2 * std::numbers::pi_v<float> * j / slices);
    Push(m, V3{r.x * radius, y, r.z * radius}, n, 0.5f + r.x * 0.5f, 0.5f + r.z * 0.5f);
  }
  for (int j = 0; j < slices; ++j)
    if (up) Tri(m, c, first + j, first + j + 1);
    else Tri(m, c, first + j + 1, first + j);
}
}

void MeshData::FlipWinding() {
  for (size_t i = 0; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
}

MeshData MakeQuad(float w, float h) {
  MeshData m;
  Face(m, {0, 0, 0}, {w * 0.5f, 0, 0}, {0, h * 0.5f, 0});
  return m;
}

MeshData MakePlaneXZ(float w, float d) {
  MeshData m;
  Face(m, {0, 0, 0}, {w * 0.5f, 0, 0}, {0, 0, -d * 0.5f}); // +X x -Z = +Y; picture top is the far edge
  return m;
}

MeshData MakeBox(float sx, float sy, float sz) {
  MeshData m;
  float x = sx * 0.5f, y = sy * 0.5f, z = sz * 0.5f;
  Face(m, {0, 0, z}, {x, 0, 0}, {0, y, 0});   // +Z
  Face(m, {0, 0, -z}, {-x, 0, 0}, {0, y, 0}); // -Z
  Face(m, {x, 0, 0}, {0, 0, -z}, {0, y, 0});  // +X
  Face(m, {-x, 0, 0}, {0, 0, z}, {0, y, 0});  // -X
  Face(m, {0, y, 0}, {x, 0, 0}, {0, 0, -z});  // +Y
  Face(m, {0, -y, 0}, {x, 0, 0}, {0, 0, z});  // -Y
  return m;
}

MeshData MakeSphere(float radius, int slices, int stacks) {
  MeshData m;
  const float pi = std::numbers::pi_v<float>;
  for (int i = 0; i <= stacks; ++i) {
    float phi = pi * i / stacks; // 0 = top
    for (int j = 0; j <= slices; ++j) {
      V3 r = Ring(2 * pi * j / slices);
      V3 n{r.x * std::sin(phi), std::cos(phi), r.z * std::sin(phi)};
      Push(m, n * radius, n, (float)j / slices, (float)i / stacks);
    }
  }
  const uint32_t row = (uint32_t)slices + 1;
  for (int i = 0; i < stacks; ++i)
    for (int j = 0; j < slices; ++j) {
      uint32_t a = i * row + j, b = (i + 1) * row + j, c = b + 1, d = a + 1; // a above b; d, c one step around
      if (i < stacks - 1) Tri(m, a, b, c);     // the top and bottom rows are fans (skip zero-area triangles)
      if (i > 0) Tri(m, a, c, d);
    }
  return m;
}

MeshData MakeCylinder(float radius, float height, int slices) {
  MeshData m;
  const float pi = std::numbers::pi_v<float>;
  uint32_t first = (uint32_t)m.vertices.size();
  for (int j = 0; j <= slices; ++j) {
    V3 r = Ring(2 * pi * j / slices);
    Push(m, V3{r.x * radius, height, r.z * radius}, r, (float)j / slices, 0);
    Push(m, V3{r.x * radius, 0, r.z * radius}, r, (float)j / slices, 1);
  }
  for (int j = 0; j < slices; ++j) {
    uint32_t a = first + j * 2, b = a + 1, c = a + 3, d = a + 2; // top j, bottom j, bottom j+1, top j+1
    Tri(m, a, b, c);
    Tri(m, a, c, d);
  }
  Disc(m, radius, height, true, slices);
  Disc(m, radius, 0, false, slices);
  return m;
}

MeshData MakeCone(float radius, float height, int slices) {
  MeshData m;
  const float pi = std::numbers::pi_v<float>;
  for (int j = 0; j < slices; ++j) {
    float a0 = 2 * pi * j / slices, a1 = 2 * pi * (j + 1) / slices, am = (a0 + a1) * 0.5f;
    auto normal = [&](float a) { V3 r = Ring(a); return Normalize(V3{r.x * height, radius, r.z * height}); };
    uint32_t apex = Push(m, {0, height, 0}, normal(am), 0.5f, 0);
    V3 r0 = Ring(a0), r1 = Ring(a1);
    uint32_t b0 = Push(m, r0 * radius, normal(a0), (float)j / slices, 1);
    uint32_t b1 = Push(m, r1 * radius, normal(a1), (float)(j + 1) / slices, 1);
    Tri(m, apex, b0, b1);
  }
  Disc(m, radius, 0, false, slices);
  return m;
}

int CountInwardTriangles(const MeshData& m) {
  int bad = 0;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    const Vertex3D& a = m.vertices[m.indices[i]];
    const Vertex3D& b = m.vertices[m.indices[i + 1]];
    const Vertex3D& c = m.vertices[m.indices[i + 2]];
    V3 pa{a.px, a.py, a.pz}, pb{b.px, b.py, b.pz}, pc{c.px, c.py, c.pz};
    V3 n{a.nx + b.nx + c.nx, a.ny + b.ny + c.ny, a.nz + b.nz + c.nz};
    if (Dot(Cross(pb - pa, pc - pa), n) <= 0) ++bad;
  }
  return bad;
}
}

namespace bdg {
namespace {
// World-scale texture coordinates (1 unit = 1 m) projected along each face's main axis: a 1 m grid texture lies the
// same on every block whatever its size (greybox, queue 3-3c).
void WorldUVs(MeshData& m) {
  for (Vertex3D& v : m.vertices) {
    const float ax = std::fabs(v.nx), ay = std::fabs(v.ny), az = std::fabs(v.nz);
    if (ax >= ay && ax >= az) { v.u = v.nx > 0 ? -v.pz : v.pz; v.v = -v.py; }
    else if (ay >= az) { v.u = v.px; v.v = v.ny > 0 ? v.pz : -v.pz; }
    else { v.u = v.nz > 0 ? v.px : -v.px; v.v = -v.py; }
  }
}
void FlatTri(MeshData& m, V3 a, V3 b, V3 c) { // counter-clockwise seen from the front
  V3 n = Normalize(Cross(b - a, c - a));
  uint32_t i = Push(m, a, n, 0, 0), j = Push(m, b, n, 0, 0), k = Push(m, c, n, 0, 0);
  Tri(m, i, j, k);
}
} // namespace

MeshData MakeBlock(BlockShape shape, float sx, float sy, float sz, int steps) {
  sx = std::max(sx, 0.01f);
  sy = std::max(sy, 0.01f);
  sz = std::max(sz, 0.01f);
  const float x = sx * 0.5f, z = sz * 0.5f;
  MeshData m;
  switch (shape) {
    case BlockShape::Box: {
      const float y = sy * 0.5f;
      Face(m, {0, y, z}, {x, 0, 0}, {0, y, 0});    // +Z
      Face(m, {0, y, -z}, {-x, 0, 0}, {0, y, 0});  // -Z
      Face(m, {x, y, 0}, {0, 0, -z}, {0, y, 0});   // +X
      Face(m, {-x, y, 0}, {0, 0, z}, {0, y, 0});   // -X
      Face(m, {0, sy, 0}, {x, 0, 0}, {0, 0, -z});  // +Y
      Face(m, {0, 0, 0}, {x, 0, 0}, {0, 0, z});    // -Y
      break;
    }
    case BlockShape::Stairs: { // rising toward -Z (forward), the first step at the +Z end
      const int n = std::clamp(steps, 1, 256);
      const float d = sz / n, h = sy / n;
      for (int k = 0; k < n; ++k) {
        const float zFront = z - k * d, zMid = zFront - d * 0.5f, top = h * (k + 1), below = h * k;
        Face(m, {0, top, zMid}, {x, 0, 0}, {0, 0, -d * 0.5f});                   // tread
        Face(m, {0, (top + below) * 0.5f, zFront}, {x, 0, 0}, {0, h * 0.5f, 0}); // riser
        Face(m, {x, top * 0.5f, zMid}, {0, 0, -d * 0.5f}, {0, top * 0.5f, 0});   // +X side of this step's column
        Face(m, {-x, top * 0.5f, zMid}, {0, 0, d * 0.5f}, {0, top * 0.5f, 0});   // -X side
      }
      Face(m, {0, sy * 0.5f, -z}, {-x, 0, 0}, {0, sy * 0.5f, 0}); // back
      Face(m, {0, 0, 0}, {x, 0, 0}, {0, 0, z});                   // bottom
      break;
    }
    case BlockShape::Ramp: { // the slope rises from the +Z edge on the ground to the top of the -Z edge
      Face(m, {0, sy * 0.5f, 0}, {x, 0, 0}, {0, sy * 0.5f, -z});  // slope
      Face(m, {0, sy * 0.5f, -z}, {-x, 0, 0}, {0, sy * 0.5f, 0}); // back
      Face(m, {0, 0, 0}, {x, 0, 0}, {0, 0, z});                   // bottom
      FlatTri(m, {x, 0, z}, {x, 0, -z}, {x, sy, -z});            // +X side
      FlatTri(m, {-x, 0, z}, {-x, sy, -z}, {-x, 0, -z});         // -X side
      break;
    }
    case BlockShape::Cylinder: {
      m = MakeCylinder(0.5f, 1.f, 32);
      for (Vertex3D& v : m.vertices) {
        v.px *= sx;
        v.py *= sy;
        v.pz *= sz;
        V3 n = Normalize(V3{v.nx / sx, v.ny / sy, v.nz / sz}); // normals of a stretched shape
        v.nx = n.x;
        v.ny = n.y;
        v.nz = n.z;
      }
      break;
    }
  }
  WorldUVs(m);
  return m;
}
}
