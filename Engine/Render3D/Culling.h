#pragma once
// View frustum culling (queue 3-10): planes taken from a view-projection matrix (OpenGL clip space), boxes tested
// conservatively (a box that might touch the frustum counts as visible). Used for the camera pass and, with the
// light's matrix, for the shadow map pass.
#include <glm/glm.hpp>

namespace bdg {
struct Frustum {
  glm::vec4 planes[6]; // xyz = normal pointing inside, w = distance; inside when dot(n, p) + w >= 0
  static Frustum FromMatrix(const glm::mat4& viewProj);
  bool Intersects(const glm::vec3& lo, const glm::vec3& hi) const; // world-space axis-aligned box
};

// World-space box around a local box under `m` (exact for the transformed box's extents).
void TransformBounds(const glm::mat4& m, const glm::vec3& lo, const glm::vec3& hi, glm::vec3& outLo, glm::vec3& outHi);
}
