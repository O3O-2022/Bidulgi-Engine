#include "VFX/VfxRender.h"
#include "RHI/RHI.h"
#include "VFX/VfxWorld.h"
#include <algorithm>
#include <vector>

namespace bdg {
namespace {
struct Vertex {
  float pos[3];
  float uv[2];
  float color[4]; // linear light (intensity applied); alpha = coverage
};

const char* kVs = R"(#version 450
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(std140, binding = 10) uniform VfxFrame { mat4 uViewProj; vec4 uMode; };
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
void main() {
  vUv = aUv;
  vColor = aColor;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";
// A soft round sprite. Additive: premultiplied (ONE, ONE); alpha: straight (SRC_ALPHA, ONE_MINUS_SRC_ALPHA).
const char* kFs = R"(#version 450
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(std140, binding = 10) uniform VfxFrame { mat4 uViewProj; vec4 uMode; };
layout(location = 0) out vec4 oColor;
void main() {
  vec2 d = vUv * 2.0 - 1.0;
  float r2 = dot(d, d);
  if (r2 >= 1.0) discard;
  float a = vColor.a * (1.0 - r2) * (1.0 - r2);
  oColor = uMode.x > 0.5 ? vec4(vColor.rgb * a, a) : vec4(vColor.rgb, a);
}
)";

struct State {
  PipelineHandle alpha, additive;
  BufferHandle vertices, frame;
  size_t capacity = 0; // vertices
  std::vector<Vertex> alphaVerts, addVerts;
  bool tried = false;
};
State& S() {
  static State s;
  return s;
}

bool Init(RHI& rhi) {
  State& s = S();
  if (s.tried) return s.alpha.Valid();
  s.tried = true;
  PipelineDesc d;
  d.vertexSource = kVs;
  d.fragmentSource = kFs;
  d.attrs[0] = {0, 3, offsetof(Vertex, pos)};
  d.attrs[1] = {1, 2, offsetof(Vertex, uv)};
  d.attrs[2] = {2, 4, offsetof(Vertex, color)};
  d.attrCount = 3;
  d.stride = sizeof(Vertex);
  d.depthTest = true;
  d.depthWrite = false;
  d.alphaBlend = true;
  d.debugName = "particles (alpha)";
  s.alpha = rhi.CreatePipeline(d);
  d.alphaBlend = false;
  d.additive = true;
  d.debugName = "particles (additive)";
  s.additive = rhi.CreatePipeline(d);
  s.frame = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(float) * 20, true);
  return s.alpha.Valid() && s.additive.Valid();
}

struct Sorted { float depth; size_t first; };
} // namespace

size_t DrawParticles(RHI& rhi, const VfxWorld& world, const glm::mat4& view, const glm::mat4& proj) {
  if (!world.Active() || world.Alive() == 0 || !Init(rhi)) return 0;
  State& s = S();
  s.alphaVerts.clear();
  s.addVerts.clear();
  const glm::mat4 inv = glm::inverse(view);
  const glm::vec3 right(inv[0]), up(inv[1]);
  std::vector<Sorted> order; // alpha quads by distance (farthest first)
  size_t drawn = 0;
  for (const VfxWorld::Effect& in : world.All())
    for (size_t ei = 0; ei < in.def->emitters.size(); ++ei) {
      const vfx::EmitterDef& e = in.def->emitters[ei];
      const std::vector<VfxWorld::Particle>& ps = in.emitters[ei].particles;
      const std::vector<std::pair<float, glm::vec4>>* gradient = nullptr;
      const std::vector<std::pair<float, float>>* curve = nullptr;
      for (const vfx::Module& m : e.modules) {
        if (m.kind == vfx::Kind::ColorOverLife) gradient = &m.gradient;
        if (m.kind == vfx::Kind::SizeOverLife) curve = &m.curve;
      }
      std::vector<Vertex>& out = e.blend == vfx::Blend::Additive ? s.addVerts : s.alphaVerts;
      for (const VfxWorld::Particle& p : ps) {
        const float t = std::clamp(p.age / p.life, 0.f, 1.f);
        glm::vec4 c = p.color * (gradient ? vfx::SampleGradient(*gradient, t) : glm::vec4(1));
        const float size = p.size * (curve ? vfx::SampleCurve(*curve, t) : 1.f);
        if (size <= 0 || c.a <= 0) continue;
        c = glm::vec4(glm::vec3(c) * e.intensity, std::min(c.a, 1.f));
        const glm::vec3 center = e.local ? in.position + in.rotation * p.pos : p.pos;
        const glm::vec3 rx = right * (size * 0.5f), ry = up * (size * 0.5f);
        const glm::vec3 corner[4] = {center - rx - ry, center + rx - ry, center + rx + ry, center - rx + ry};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        if (&out == &s.alphaVerts) order.push_back({(view * glm::vec4(center, 1)).z, out.size()});
        for (int k : {0, 1, 2, 0, 2, 3}) out.push_back({{corner[k].x, corner[k].y, corner[k].z}, {uv[k][0], uv[k][1]}, {c.r, c.g, c.b, c.a}});
        ++drawn;
      }
    }
  if (!order.empty()) { // view space looks down -z: the most negative z is the farthest
    std::stable_sort(order.begin(), order.end(), [](const Sorted& a, const Sorted& b) { return a.depth < b.depth; });
    std::vector<Vertex> sorted;
    sorted.reserve(s.alphaVerts.size());
    for (const Sorted& q : order) sorted.insert(sorted.end(), s.alphaVerts.begin() + (ptrdiff_t)q.first, s.alphaVerts.begin() + (ptrdiff_t)q.first + 6);
    s.alphaVerts.swap(sorted);
  }
  const size_t total = s.alphaVerts.size() + s.addVerts.size();
  if (total == 0) return 0;
  if (total > s.capacity) {
    rhi.DestroyBuffer(s.vertices);
    s.capacity = std::max<size_t>(total + total / 2, 6 * 256);
    s.vertices = rhi.CreateBuffer(BufferUsage::Vertex, nullptr, s.capacity * sizeof(Vertex), true);
  }
  if (!s.alphaVerts.empty()) rhi.UpdateBuffer(s.vertices, s.alphaVerts.data(), s.alphaVerts.size() * sizeof(Vertex));
  if (!s.addVerts.empty()) rhi.UpdateBuffer(s.vertices, s.addVerts.data(), s.addVerts.size() * sizeof(Vertex), s.alphaVerts.size() * sizeof(Vertex));
  struct Frame { glm::mat4 vp; glm::vec4 mode; } f{proj * view, {0, 0, 0, 0}};
  rhi.UpdateBuffer(s.frame, &f, sizeof f);
  rhi.BindUniformBuffer(10, s.frame);
  if (!s.alphaVerts.empty()) rhi.Draw(s.alpha, s.vertices, (uint32_t)s.alphaVerts.size());
  if (!s.addVerts.empty()) {
    // the mode switch needs its own uniform data: a second small update after the alpha draw
    f.mode.x = 1;
    rhi.UpdateBuffer(s.frame, &f, sizeof f);
    rhi.Draw(s.additive, s.vertices, (uint32_t)s.addVerts.size(), (uint32_t)s.alphaVerts.size());
  }
  return drawn;
}

void ShutdownParticleRenderer(RHI& rhi) {
  State& s = S();
  rhi.DestroyPipeline(s.alpha);
  rhi.DestroyPipeline(s.additive);
  rhi.DestroyBuffer(s.vertices);
  rhi.DestroyBuffer(s.frame);
  s = State{};
}
} // namespace bdg
