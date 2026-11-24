#include "Runtime/Snapping.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace bdg {

float SnapValue(float v, float step) { return step > 0 ? std::round(v / step) * step : v; }

glm::vec3 SnapMoveToGrid(const glm::vec3& start, const glm::vec3& move, const glm::bvec3& axes, float step) {
  glm::vec3 end = start + move;
  for (int i = 0; i < 3; ++i)
    if (axes[i]) end[i] = SnapValue(end[i], step);
  return end - start;
}

float RestOffset(const glm::vec3& pivot, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& n) {
  float most = 0;
  for (int k = 0; k < 8; ++k) {
    const glm::vec3 c{(k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z};
    most = std::max(most, glm::dot(pivot - c, n));
  }
  return most;
}

std::vector<float> AlignOffsets(const std::vector<AlignBox>& boxes, int axis, AlignMode mode) {
  std::vector<float> out(boxes.size(), 0.f);
  if (boxes.size() < 2 || axis < 0 || axis > 2) return out;
  float lo = boxes[0].lo[axis], hi = boxes[0].hi[axis];
  for (const AlignBox& b : boxes) {
    lo = std::min(lo, b.lo[axis]);
    hi = std::max(hi, b.hi[axis]);
  }
  if (mode != AlignMode::Distribute) {
    for (size_t i = 0; i < boxes.size(); ++i) {
      const float a = boxes[i].lo[axis], b = boxes[i].hi[axis];
      out[i] = mode == AlignMode::Min ? lo - a : mode == AlignMode::Max ? hi - b : (lo + hi) * 0.5f - (a + b) * 0.5f;
    }
    return out;
  }
  if (boxes.size() < 3) return out;
  std::vector<size_t> order(boxes.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
    return boxes[x].lo[axis] + boxes[x].hi[axis] < boxes[y].lo[axis] + boxes[y].hi[axis];
  });
  float sizes = 0;
  for (const AlignBox& b : boxes) sizes += b.hi[axis] - b.lo[axis];
  const float first = boxes[order.front()].lo[axis], last = boxes[order.back()].hi[axis];
  const float gap = (last - first - sizes) / (float)(boxes.size() - 1);
  float at = first;
  for (size_t k = 0; k < order.size(); ++k) {
    const AlignBox& b = boxes[order[k]];
    if (k > 0 && k + 1 < order.size()) out[order[k]] = at - b.lo[axis];
    at += (b.hi[axis] - b.lo[axis]) + gap;
  }
  return out;
}

} // namespace bdg
