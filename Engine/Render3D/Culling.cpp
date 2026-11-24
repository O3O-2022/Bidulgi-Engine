#include "Render3D/Culling.h"
#include <cmath>

namespace bdg {
Frustum Frustum::FromMatrix(const glm::mat4& m) {
  // Gribb & Hartmann: clip-space -w <= x, y, z <= w becomes six planes from the matrix rows.
  auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
  const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  Frustum f;
  const glm::vec4 p[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2}; // left right bottom top near far
  for (int i = 0; i < 6; ++i) {
    float len = glm::length(glm::vec3(p[i]));
    f.planes[i] = len > 0 ? p[i] / len : p[i];
  }
  return f;
}

bool Frustum::Intersects(const glm::vec3& lo, const glm::vec3& hi) const {
  const glm::vec3 c = (lo + hi) * 0.5f, e = (hi - lo) * 0.5f;
  for (const glm::vec4& p : planes) {
    float r = e.x * std::fabs(p.x) + e.y * std::fabs(p.y) + e.z * std::fabs(p.z);
    if (glm::dot(glm::vec3(p), c) + p.w < -r) return false; // completely outside this plane
  }
  return true;
}

void TransformBounds(const glm::mat4& m, const glm::vec3& lo, const glm::vec3& hi, glm::vec3& outLo, glm::vec3& outHi) {
  // Arvo: centre through the matrix, extents through its absolute 3x3 part.
  const glm::vec3 c = (lo + hi) * 0.5f, e = (hi - lo) * 0.5f;
  const glm::vec3 wc = glm::vec3(m * glm::vec4(c, 1.f));
  glm::vec3 we{0};
  for (int col = 0; col < 3; ++col)
    for (int r = 0; r < 3; ++r) we[r] += std::fabs(m[col][r]) * e[col];
  outLo = wc - we;
  outHi = wc + we;
}
}
