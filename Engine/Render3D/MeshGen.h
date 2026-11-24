#pragma once
// Procedural meshes for the rendering reference board and editor helpers (queue 3-0). Conventions follow
// Core/Space.h: right-handed, Y up, front faces wind counter-clockwise seen from outside, normals point out.
// UV (0,0) is the top-left of a picture (first row of the image, glTF convention).
#include <cstdint>
#include <vector>

namespace bdg {
// Tangent xyz + handedness w (glTF TANGENT). w == 0: no tangent, the shader derives one from UV derivatives.
// lu, lv: lightmap atlas coordinates (GI Tier 0-C), set on the per-object copies the lightmap bake makes.
struct Vertex3D { float px, py, pz, nx, ny, nz, u, v, tx = 0, ty = 0, tz = 0, tw = 0, lu = 0, lv = 0; };

struct MeshData {
  std::vector<Vertex3D> vertices;
  std::vector<uint32_t> indices;
  void FlipWinding(); // swaps two indices of every triangle (the winding probe draws a deliberately clockwise quad)
};

MeshData MakeQuad(float width, float height);            // XY plane, centred, facing +Z
MeshData MakePlaneXZ(float width, float depth);          // y = 0, centred, facing +Y
MeshData MakeBox(float sx, float sy, float sz);          // centred
MeshData MakeSphere(float radius, int slices = 48, int stacks = 24);
MeshData MakeCylinder(float radius, float height, int slices = 24); // along +Y from y = 0, capped
MeshData MakeCone(float radius, float height, int slices = 24);     // base at y = 0, apex at +Y

// Triangles whose winding disagrees with their vertex normals (0 = every face is CCW seen from outside).
int CountInwardTriangles(const MeshData& m);
}
