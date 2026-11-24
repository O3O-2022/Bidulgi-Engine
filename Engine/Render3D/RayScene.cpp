#include "Render3D/RayScene.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace bdg {

void RayScene::Build(std::vector<glm::vec3> positions) {
  pos_ = std::move(positions);
  const uint32_t n = (uint32_t)(pos_.size() / 3);
  order_.resize(n);
  std::vector<glm::vec3> centre(n);
  for (uint32_t i = 0; i < n; ++i) {
    order_[i] = i;
    centre[i] = (pos_[i * 3] + pos_[i * 3 + 1] + pos_[i * 3 + 2]) / 3.f;
  }
  nodes_.clear();
  nodes_.reserve(n ? n * 2 : 1);
  nodes_.push_back({});
  struct Task { uint32_t node, first, count; };
  std::vector<Task> stack{{0, 0, n}};
  while (!stack.empty()) {
    Task t = stack.back();
    stack.pop_back();
    Node& nd = nodes_[t.node];
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    glm::vec3 clo = lo, chi = hi;
    for (uint32_t k = t.first; k < t.first + t.count; ++k) {
      uint32_t tri = order_[k];
      for (int c = 0; c < 3; ++c) {
        lo = glm::min(lo, pos_[tri * 3 + c]);
        hi = glm::max(hi, pos_[tri * 3 + c]);
      }
      clo = glm::min(clo, centre[tri]);
      chi = glm::max(chi, centre[tri]);
    }
    nd.lo = lo;
    nd.hi = hi;
    if (t.count <= 4) {
      nd.first = t.first;
      nd.count = t.count;
      continue;
    }
    // Split at the median centroid along the longest axis of the centroids' box.
    glm::vec3 ext = chi - clo;
    int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
    uint32_t mid = t.first + t.count / 2;
    std::nth_element(order_.begin() + t.first, order_.begin() + mid, order_.begin() + t.first + t.count,
                     [&](uint32_t a, uint32_t b) { return centre[a][axis] < centre[b][axis]; });
    uint32_t left = (uint32_t)nodes_.size();
    nodes_.push_back({});
    nodes_.push_back({});
    nodes_[t.node].first = left; // nd may have moved with the push_backs
    nodes_[t.node].count = 0;
    stack.push_back({left, t.first, mid - t.first});
    stack.push_back({left + 1, mid, t.first + t.count - mid});
  }
}

namespace {
bool HitBox(const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& o, const glm::vec3& inv, float tMax) {
  glm::vec3 t0 = (lo - o) * inv, t1 = (hi - o) * inv;
  glm::vec3 tn = glm::min(t0, t1), tf = glm::max(t0, t1);
  float enter = std::max(std::max(tn.x, tn.y), std::max(tn.z, 0.f));
  float exit = std::min(std::min(tf.x, tf.y), std::min(tf.z, tMax));
  return enter <= exit;
}
} // namespace

template <bool AnyHit> bool RayScene::Trace(const glm::vec3& o, const glm::vec3& d, float tMax, RayHit* hit) const {
  if (nodes_.empty() || order_.empty()) return false;
  auto safeInv = [](float x) { return std::abs(x) > 1e-12f ? 1.f / x : (x < 0 ? -1e12f : 1e12f); };
  const glm::vec3 inv(safeInv(d.x), safeInv(d.y), safeInv(d.z));
  uint32_t stack[64];
  int sp = 0;
  stack[sp++] = 0;
  bool found = false;
  float best = tMax;
  while (sp > 0) {
    const Node& nd = nodes_[stack[--sp]];
    if (!HitBox(nd.lo, nd.hi, o, inv, best)) continue;
    if (nd.count == 0) {
      if (sp < 62) {
        stack[sp++] = nd.first;
        stack[sp++] = nd.first + 1;
      }
      continue;
    }
    for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) {
      const uint32_t tri = order_[k];
      const glm::vec3 &a = pos_[tri * 3], e1 = pos_[tri * 3 + 1] - a, e2 = pos_[tri * 3 + 2] - a;
      const glm::vec3 p = glm::cross(d, e2);
      const float det = glm::dot(e1, p);
      if (std::abs(det) < 1e-12f) continue;
      const float invDet = 1.f / det;
      const glm::vec3 s = o - a;
      const float u = glm::dot(s, p) * invDet;
      if (u < 0 || u > 1) continue;
      const glm::vec3 q = glm::cross(s, e1);
      const float v = glm::dot(d, q) * invDet;
      if (v < 0 || u + v > 1) continue;
      const float t = glm::dot(e2, q) * invDet;
      if (t <= 1e-5f || t > best) continue;
      if (AnyHit) return true;
      best = t;
      found = true;
      hit->t = t;
      hit->tri = tri;
      hit->u = u;
      hit->v = v;
      hit->backface = det < 0; // front faces (CCW) give det > 0 for a ray coming toward them
    }
  }
  return found;
}

bool RayScene::Intersect(const glm::vec3& origin, const glm::vec3& dir, float tMax, RayHit& hit) const {
  return Trace<false>(origin, dir, tMax, &hit);
}

bool RayScene::Occluded(const glm::vec3& origin, const glm::vec3& dir, float tMax) const {
  return Trace<true>(origin, dir, tMax, nullptr);
}

} // namespace bdg
