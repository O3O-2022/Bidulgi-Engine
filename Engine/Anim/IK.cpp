#include "Anim/IK.h"
#include <algorithm>
#include <cmath>

namespace bdg {
namespace {
glm::vec3 Pos(const glm::mat4& m) { return glm::vec3(m[3]); }
glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
  const float l = glm::length(v);
  return l > 1e-8f ? v / l : fallback;
}
float AngleOf(const glm::quat& q) { return 2.f * std::acos(std::clamp(std::fabs(q.w), 0.f, 1.f)); }
}

glm::quat RotationBetween(const glm::vec3& from, const glm::vec3& to) {
  const glm::vec3 a = SafeNormalize(from, glm::vec3(0, 0, 1)), b = SafeNormalize(to, glm::vec3(0, 0, 1));
  const float d = glm::dot(a, b);
  if (d > 0.999999f) return glm::quat(1, 0, 0, 0);
  if (d < -0.999999f) { // opposite: any axis at right angles
    glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), a);
    if (glm::length(axis) < 1e-6f) axis = glm::cross(glm::vec3(0, 1, 0), a);
    return glm::angleAxis(3.14159265f, glm::normalize(axis));
  }
  const glm::vec3 c = glm::cross(a, b);
  return glm::normalize(glm::quat(1.f + d, c.x, c.y, c.z));
}

glm::quat ModelRotation(const glm::mat4& m) {
  glm::mat3 r(m);
  for (int i = 0; i < 3; ++i) r[i] = SafeNormalize(r[i], glm::vec3(i == 0, i == 1, i == 2));
  return glm::normalize(glm::quat_cast(r));
}

void SetModelRotation(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, int j, const glm::quat& rot) {
  if (j < 0 || (size_t)j >= pose.Size()) return;
  const int p = sk.joints[j].parent;
  const glm::quat parent = p >= 0 ? ModelRotation(model[p]) : glm::quat(1, 0, 0, 0);
  pose.r[j] = glm::normalize(glm::inverse(parent) * rot);
  PoseToModel(sk, pose, model);
}

std::vector<int> JointChain(const Skeleton& sk, int tip, int count) {
  std::vector<int> chain;
  for (int j = tip; j >= 0 && (int)chain.size() < count; j = sk.joints[j].parent) chain.insert(chain.begin(), j);
  if ((int)chain.size() < count) chain.clear();
  return chain;
}

bool TwoBoneIK(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, int upper, int mid, int end, const glm::vec3& target,
               const glm::vec3& pole, float weight) {
  const int n = (int)pose.Size();
  if (upper < 0 || mid < 0 || end < 0 || upper >= n || mid >= n || end >= n || weight <= 0.f) return false;
  const glm::vec3 a = Pos(model[upper]), b = Pos(model[mid]), c = Pos(model[end]);
  const float lab = glm::length(b - a), lcb = glm::length(c - b);
  if (lab < 1e-5f || lcb < 1e-5f) return false;
  const float lat = std::clamp(glm::length(target - a), 1e-4f, lab + lcb - 1e-4f);
  auto acosc = [](float x) { return std::acos(std::clamp(x, -1.f, 1.f)); };
  const float acab0 = acosc(glm::dot(SafeNormalize(c - a, {0, -1, 0}), SafeNormalize(b - a, {0, -1, 0})));
  const float babc0 = acosc(glm::dot(SafeNormalize(a - b, {0, 1, 0}), SafeNormalize(c - b, {0, -1, 0})));
  const float acab1 = acosc((lcb * lcb - lab * lab - lat * lat) / (-2.f * lab * lat));
  const float babc1 = acosc((lat * lat - lab * lab - lcb * lcb) / (-2.f * lab * lcb));
  glm::vec3 axis = glm::cross(c - a, b - a); // the plane the limb bends in now
  if (glm::length(axis) < 1e-6f) axis = glm::cross(c - a, pole);
  if (glm::length(axis) < 1e-6f) axis = glm::cross(c - a, glm::vec3(1, 0, 0));
  axis = SafeNormalize(axis, glm::vec3(1, 0, 0));
  const glm::quat r0 = glm::angleAxis(acab1 - acab0, axis), r1 = glm::angleAxis(babc1 - babc0, axis);
  const glm::quat ra = ModelRotation(model[upper]), rb = ModelRotation(model[mid]), rc = ModelRotation(model[end]);
  const glm::vec3 c1 = a + r0 * (b - a) + (r0 * r1) * (c - b);
  const glm::quat r2 = RotationBetween(c1 - a, target - a);
  const float w = std::min(weight, 1.f);
  SetModelRotation(sk, pose, model, upper, glm::slerp(ra, glm::normalize(r2 * r0 * ra), w));
  SetModelRotation(sk, pose, model, mid, glm::slerp(rb, glm::normalize(r2 * r0 * r1 * rb), w));
  SetModelRotation(sk, pose, model, end, rc); // the end (foot, hand) keeps its orientation
  return true;
}

float LookAt(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, const std::vector<std::pair<int, float>>& chain,
             const glm::vec3& forward, const glm::vec3& target, float weight, float maxDegrees) {
  if (chain.empty() || weight <= 0.f) return 0.f;
  const int tip = chain.back().first;
  if (tip < 0 || (size_t)tip >= pose.Size()) return 0.f;
  // the direction to turn to, limited to maxDegrees from where the tip looks now
  const glm::vec3 fwd0 = ModelRotation(model[tip]) * forward, want0 = target - Pos(model[tip]);
  if (glm::length(want0) < 1e-5f) return 0.f;
  glm::quat full = RotationBetween(fwd0, want0);
  const float ang = AngleOf(full), maxRad = glm::radians(std::max(maxDegrees, 0.f));
  if (ang > maxRad && ang > 1e-6f) full = glm::slerp(glm::quat(1, 0, 0, 0), full, maxRad / ang);
  const glm::vec3 aim = full * SafeNormalize(fwd0, forward);
  for (const auto& [j, share] : chain) {
    if (j < 0 || (size_t)j >= pose.Size()) continue;
    const glm::vec3 fwd = ModelRotation(model[tip]) * forward;
    const glm::quat part = glm::slerp(glm::quat(1, 0, 0, 0), RotationBetween(fwd, aim), std::clamp(share * weight, 0.f, 1.f));
    SetModelRotation(sk, pose, model, j, glm::normalize(part * ModelRotation(model[j])));
  }
  const glm::vec3 fwd = ModelRotation(model[tip]) * forward;
  return glm::degrees(std::acos(std::clamp(glm::dot(SafeNormalize(fwd, forward), SafeNormalize(want0, forward)), -1.f, 1.f)));
}

float Fabrik(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, const std::vector<int>& chain, const glm::vec3& target,
             int iterations, float tolerance) {
  const size_t n = chain.size();
  if (n < 2) return 1e30f;
  for (int j : chain)
    if (j < 0 || (size_t)j >= pose.Size()) return 1e30f;
  std::vector<glm::vec3> p(n);
  std::vector<float> len(n - 1);
  float total = 0;
  for (size_t i = 0; i < n; ++i) p[i] = Pos(model[chain[i]]);
  for (size_t i = 0; i + 1 < n; ++i) total += len[i] = glm::length(p[i + 1] - p[i]);
  const glm::vec3 root = p[0];
  if (glm::length(target - root) >= total) { // out of reach: a straight line towards it
    const glm::vec3 dir = SafeNormalize(target - root, glm::vec3(0, 1, 0));
    for (size_t i = 1; i < n; ++i) p[i] = p[i - 1] + dir * len[i - 1];
  } else {
    for (int it = 0; it < iterations && glm::length(p[n - 1] - target) > tolerance; ++it) {
      p[n - 1] = target;
      for (size_t i = n - 1; i-- > 0;) p[i] = p[i + 1] + SafeNormalize(p[i] - p[i + 1], glm::vec3(0, -1, 0)) * len[i];
      p[0] = root;
      for (size_t i = 1; i < n; ++i) p[i] = p[i - 1] + SafeNormalize(p[i] - p[i - 1], glm::vec3(0, 1, 0)) * len[i - 1];
    }
  }
  for (size_t i = 0; i + 1 < n; ++i) { // turn each joint so its bone points at the solved position of the next
    const int j = chain[i];
    const glm::vec3 cur = Pos(model[chain[i + 1]]) - Pos(model[j]), want = p[i + 1] - Pos(model[j]);
    SetModelRotation(sk, pose, model, j, glm::normalize(RotationBetween(cur, want) * ModelRotation(model[j])));
  }
  return glm::length(Pos(model[chain[n - 1]]) - target);
}
}
