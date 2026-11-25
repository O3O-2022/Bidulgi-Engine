#include "Anim/Skeleton.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg {
namespace {
bool SameNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}
}

int Skeleton::Find(std::string_view name) const {
  for (size_t i = 0; i < joints.size(); ++i)
    if (joints[i].name == name) return (int)i;
  return -1;
}

const AnimClip* SkinnedModelData::Clip(std::string_view name) const {
  if (clips.empty()) return nullptr;
  if (name.empty()) return &clips[0];
  for (const AnimClip& c : clips)
    if (c.name == name) return &c;
  for (const AnimClip& c : clips)
    if (SameNoCase(c.name, name)) return &c;
  return nullptr;
}

SkinVertex MakeSkinVertex(const int* joints, const float* weights, int n) {
  int idx[4] = {-1, -1, -1, -1};
  float w[4] = {0, 0, 0, 0};
  for (int i = 0; i < n; ++i) {
    if (!(weights[i] > 0.f) || joints[i] < 0 || joints[i] > 0xffff) continue;
    int slot = -1;
    for (int k = 0; k < 4; ++k)
      if (idx[k] == joints[i]) { slot = k; break; }
    if (slot >= 0) { w[slot] += weights[i]; continue; }
    int smallest = 0;
    for (int k = 1; k < 4; ++k)
      if (w[k] < w[smallest]) smallest = k;
    if (weights[i] > w[smallest]) { idx[smallest] = joints[i]; w[smallest] = weights[i]; }
  }
  SkinVertex v;
  float sum = w[0] + w[1] + w[2] + w[3];
  if (sum <= 0.f) return v; // joint 0, weight 1
  for (int k = 0; k < 4; ++k) {
    v.joint[k] = (uint16_t)(idx[k] < 0 ? 0 : idx[k]);
    v.weight[k] = w[k] / sum;
  }
  return v;
}

void RestPose(const Skeleton& sk, LocalPose& pose) {
  const size_t n = sk.joints.size();
  pose.t.resize(n);
  pose.r.resize(n);
  pose.s.resize(n);
  for (size_t i = 0; i < n; ++i) {
    pose.t[i] = sk.joints[i].t;
    pose.r[i] = sk.joints[i].r;
    pose.s[i] = sk.joints[i].s;
  }
}

void SampleClip(const AnimClip& clip, float time, LocalPose& pose) {
  for (const AnimChannel& c : clip.channels) {
    if (c.joint < 0 || (size_t)c.joint >= pose.Size() || c.times.empty() || c.values.size() < c.times.size()) continue;
    size_t i = 0;
    float f = 0.f;
    if (time <= c.times.front()) i = 0;
    else if (time >= c.times.back()) i = c.times.size() - 1;
    else {
      i = (size_t)(std::upper_bound(c.times.begin(), c.times.end(), time) - c.times.begin()) - 1;
      const float span = c.times[i + 1] - c.times[i];
      f = c.step || span <= 0.f ? 0.f : (time - c.times[i]) / span;
    }
    const glm::vec4 a = c.values[i], b = f > 0.f ? c.values[i + 1] : a;
    switch (c.path) {
      case AnimChannel::Translation: pose.t[c.joint] = glm::mix(glm::vec3(a), glm::vec3(b), f); break;
      case AnimChannel::Scale: pose.s[c.joint] = glm::mix(glm::vec3(a), glm::vec3(b), f); break;
      case AnimChannel::Rotation: {
        const glm::quat qa(a.w, a.x, a.y, a.z), qb(b.w, b.x, b.y, b.z);
        pose.r[c.joint] = glm::normalize(f > 0.f ? glm::slerp(qa, qb, f) : qa);
        break;
      }
    }
  }
}

void BlendPose(LocalPose& a, const LocalPose& b, float w) {
  w = std::clamp(w, 0.f, 1.f);
  const size_t n = std::min(a.Size(), b.Size());
  for (size_t i = 0; i < n; ++i) {
    a.t[i] = glm::mix(a.t[i], b.t[i], w);
    a.s[i] = glm::mix(a.s[i], b.s[i], w);
    glm::quat qb = b.r[i];
    if (glm::dot(a.r[i], qb) < 0.f) qb = -qb;
    const glm::quat q(a.r[i].w + (qb.w - a.r[i].w) * w, a.r[i].x + (qb.x - a.r[i].x) * w, a.r[i].y + (qb.y - a.r[i].y) * w,
                      a.r[i].z + (qb.z - a.r[i].z) * w);
    a.r[i] = glm::normalize(q);
  }
}

glm::mat4 JointLocalMatrix(const LocalPose& pose, size_t j) {
  glm::mat4 m = glm::mat4_cast(pose.r[j]);
  m[0] *= pose.s[j].x;
  m[1] *= pose.s[j].y;
  m[2] *= pose.s[j].z;
  m[3] = glm::vec4(pose.t[j], 1.f);
  return m;
}

void PoseToModel(const Skeleton& sk, const LocalPose& pose, std::vector<glm::mat4>& model) {
  const size_t n = std::min(sk.joints.size(), pose.Size());
  model.resize(sk.joints.size(), glm::mat4(1.f));
  for (size_t i = 0; i < n; ++i) {
    const glm::mat4 local = JointLocalMatrix(pose, i);
    const int p = sk.joints[i].parent;
    model[i] = p >= 0 && (size_t)p < i ? model[p] * local : local;
  }
}

void ModelToSkin(const Skeleton& sk, const std::vector<glm::mat4>& model, std::vector<glm::mat4>& skin) {
  skin.resize(sk.joints.size());
  for (size_t i = 0; i < sk.joints.size(); ++i) skin[i] = (i < model.size() ? model[i] : glm::mat4(1.f)) * sk.joints[i].inverseBind;
}

void JointBoxes(const float* positions, size_t stride, const std::vector<SkinVertex>& skin, size_t jointCount, std::vector<JointBox>& out) {
  out.assign(jointCount, JointBox{});
  for (size_t v = 0; v < skin.size(); ++v) {
    const float* p = positions + v * stride;
    const glm::vec3 pos(p[0], p[1], p[2]);
    for (int k = 0; k < 4; ++k) {
      if (skin[v].weight[k] <= 0.f || skin[v].joint[k] >= jointCount) continue;
      JointBox& b = out[skin[v].joint[k]];
      b.lo = glm::min(b.lo, pos);
      b.hi = glm::max(b.hi, pos);
    }
  }
}

bool PoseBounds(const std::vector<JointBox>& boxes, const std::vector<glm::mat4>& skin, glm::vec3& lo, glm::vec3& hi) {
  bool any = false;
  for (size_t j = 0; j < boxes.size() && j < skin.size(); ++j) {
    if (!boxes[j].Valid()) continue;
    const glm::mat4& m = skin[j];
    // transformed box: centre moved, extents through |M| (Arvo)
    const glm::vec3 c = (boxes[j].lo + boxes[j].hi) * 0.5f, e = (boxes[j].hi - boxes[j].lo) * 0.5f;
    const glm::vec3 nc = glm::vec3(m * glm::vec4(c, 1.f));
    glm::vec3 ne(0.f);
    for (int col = 0; col < 3; ++col) ne += glm::abs(glm::vec3(m[col])) * e[col];
    lo = any ? glm::min(lo, nc - ne) : nc - ne;
    hi = any ? glm::max(hi, nc + ne) : nc + ne;
    any = true;
  }
  return any;
}
}
