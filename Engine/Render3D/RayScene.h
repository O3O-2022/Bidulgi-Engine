#pragma once
// Triangle soup with a bounding volume hierarchy for CPU ray casts (lightmap baking, GI Tier 0-C; picking later).
// Built once from world-space triangles; queries are const and safe from many threads at once.
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace bdg {

struct RayHit {
  float t = 0;          // distance along the (unit) direction
  uint32_t tri = 0;     // triangle index (the order given to Build)
  float u = 0, v = 0;   // barycentrics of corners 1 and 2 (corner 0 = 1 - u - v)
  bool backface = false; // the ray met the triangle's back (clockwise seen from the ray's origin)
};

class RayScene {
public:
  // positions: 3 corners per triangle, counter-clockwise seen from the front.
  void Build(std::vector<glm::vec3> positions);
  size_t Triangles() const { return pos_.size() / 3; }
  // Nearest hit in (0, tMax]; dir must be normalised.
  bool Intersect(const glm::vec3& origin, const glm::vec3& dir, float tMax, RayHit& hit) const;
  // Anything in (0, tMax]? (shadow rays; stops at the first hit)
  bool Occluded(const glm::vec3& origin, const glm::vec3& dir, float tMax) const;

private:
  struct Node {
    glm::vec3 lo, hi;
    uint32_t first = 0, count = 0; // leaf: triangles [first, first + count) of order_; inner: count = 0, children first, first + 1
  };
  std::vector<glm::vec3> pos_;
  std::vector<uint32_t> order_;
  std::vector<Node> nodes_;
  template <bool AnyHit> bool Trace(const glm::vec3& o, const glm::vec3& d, float tMax, RayHit* hit) const;
};

} // namespace bdg
