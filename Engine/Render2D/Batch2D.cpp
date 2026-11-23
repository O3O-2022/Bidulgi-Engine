#include "Render2D/Batch2D.h"
#include "Core/Log.h"
#include <cmath>

namespace bdg {
namespace {
const char* kVS = R"(#version 450 core
layout(std140, binding = 0) uniform Frame { vec2 uViewport; };
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aLocal;
layout(location = 2) in vec2 aHalf;
layout(location = 3) in vec2 aShape;   // x = radius, y = border
layout(location = 4) in vec4 aFill;
layout(location = 5) in vec4 aBorder;
layout(location = 6) in vec4 aUVMode;  // uv, mode, blur
out vec2 vLocal; out vec2 vHalf; out vec2 vShape; out vec4 vFill; out vec4 vBorder; out vec4 vUVMode;
void main() {
  vLocal = aLocal; vHalf = aHalf; vShape = aShape; vFill = aFill; vBorder = aBorder; vUVMode = aUVMode;
  vec2 ndc = aPos / uViewport * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)";
const char* kFS = R"(#version 450 core
layout(binding = 0) uniform sampler2D uAtlas;
in vec2 vLocal; in vec2 vHalf; in vec2 vShape; in vec4 vFill; in vec4 vBorder; in vec4 vUVMode;
out vec4 oColor;
float sdRoundBox(vec2 p, vec2 b, float r) {
  vec2 q = abs(p) - b + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
// Signed distance to the triangle (-h.x,-h.y) (-h.x,h.y) (h.x,0): pointing right.
float sdTriangleRight(vec2 p, vec2 h) {
  vec2 a = vec2(-h.x, -h.y), b = vec2(-h.x, h.y), c = vec2(h.x, 0.0);
  vec2 e0 = b - a, e1 = c - b, e2 = a - c;
  vec2 v0 = p - a, v1 = p - b, v2 = p - c;
  vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
  float s = sign(e0.x * e2.y - e0.y * e2.x);
  vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                   vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                   vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
  return -sqrt(d.x) * sign(d.y);
}
void main() {
  // Sample and take derivatives outside any branch (derivatives need uniform control flow).
  float sd = texture(uAtlas, vUVMode.xy).r - 0.5;
  float aa = max(fwidth(sd), 1e-4);
  if (vUVMode.z > 0.5 && vUVMode.z < 1.5) {               // SDF glyph
    oColor = vec4(vFill.rgb, vFill.a * clamp(sd / aa + 0.5, 0.0, 1.0));
    return;
  }
  if (vUVMode.z > 1.5) {                                  // triangle (+ optional outline)
    float dt = sdTriangleRight(vLocal, vHalf);
    vec4 tc = vShape.y > 0.0 ? mix(vBorder, vFill, clamp(0.5 - (dt + vShape.y), 0.0, 1.0)) : vFill;
    oColor = vec4(tc.rgb, tc.a * clamp(0.5 - dt, 0.0, 1.0));
    return;
  }
  float r = min(vShape.x, min(vHalf.x, vHalf.y));
  float d = sdRoundBox(vLocal, vHalf, r);
  float coverage = clamp(0.5 - d, 0.0, 1.0);              // 1px anti-aliased edge
  if (vUVMode.w > 0.0) coverage = 1.0 - smoothstep(-0.5 * vUVMode.w, 0.5 * vUVMode.w, d); // soft shadow edge
  vec4 color = vFill;
  if (vShape.y > 0.0) {
    vec2 innerHalf = max(vHalf - vShape.y, vec2(0.0));
    float di = sdRoundBox(vLocal, innerHalf, max(r - vShape.y, 0.0));
    color = mix(vBorder, vFill, clamp(0.5 - di, 0.0, 1.0));
  }
  oColor = vec4(color.rgb, color.a * coverage);
}
)";
}

bool Batch2D::Init(RHI& rhi) {
  rhi_ = &rhi;
  PipelineDesc pd;
  pd.vertexSource = kVS;
  pd.fragmentSource = kFS;
  pd.attrs[0] = {0, 2, offsetof(Vertex, x)};
  pd.attrs[1] = {1, 2, offsetof(Vertex, lx)};
  pd.attrs[2] = {2, 2, offsetof(Vertex, hw)};
  pd.attrs[3] = {3, 2, offsetof(Vertex, radius)};
  pd.attrs[4] = {4, 4, offsetof(Vertex, r)};
  pd.attrs[5] = {5, 4, offsetof(Vertex, br)};
  pd.attrs[6] = {6, 4, offsetof(Vertex, u)};
  pd.attrCount = 7;
  pd.stride = sizeof(Vertex);
  pd.alphaBlend = true;
  pd.debugName = "batch2d";
  pipeline_ = rhi.CreatePipeline(pd);
  if (!pipeline_.Valid()) return false;

  std::vector<uint32_t> indices(kMaxQuads * 6);
  for (uint32_t q = 0; q < kMaxQuads; ++q) {
    uint32_t v = q * 4, *i = &indices[q * 6];
    i[0] = v; i[1] = v + 1; i[2] = v + 2; i[3] = v; i[4] = v + 2; i[5] = v + 3;
  }
  indexBuffer_ = rhi.CreateBuffer(BufferUsage::Index, indices.data(), indices.size() * sizeof(uint32_t));
  vertexBuffer_ = rhi.CreateBuffer(BufferUsage::Vertex, nullptr, kMaxQuads * 4 * sizeof(Vertex), true);
  uniformBuffer_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, 16, true);
  const uint8_t zero = 0;
  dummyTexture_ = rhi.CreateTexture2D(1, 1, TextureFormat::R8, &zero);
  vertices_.reserve(kMaxQuads * 4);
  return true;
}

void Batch2D::Shutdown() {
  if (!rhi_) return;
  rhi_->DestroyBuffer(vertexBuffer_);
  rhi_->DestroyBuffer(indexBuffer_);
  rhi_->DestroyBuffer(uniformBuffer_);
  rhi_->DestroyTexture(dummyTexture_);
  rhi_->DestroyPipeline(pipeline_);
  rhi_ = nullptr;
}

void Batch2D::SetTextAtlas(TextureHandle atlas) { atlas_ = atlas; }

void Batch2D::Begin(int32_t w, int32_t h) {
  float vp[4] = {(float)w, (float)h, 0, 0};
  rhi_->UpdateBuffer(uniformBuffer_, vp, sizeof(vp));
  rhi_->BindUniformBuffer(0, uniformBuffer_);
  vertices_.clear();
  quadsFrame_ = flushesFrame_ = 0;
}

void Batch2D::PushQuad(const float c[4][2], const float l[4][2], float hw, float hh, float radius,
                       float border, Color f, Color b, float mode, float blur) {
  if (vertices_.size() >= kMaxQuads * 4) Flush();
  for (int i = 0; i < 4; ++i)
    vertices_.push_back({c[i][0], c[i][1], l[i][0], l[i][1], hw, hh, radius, border,
                         f.r, f.g, f.b, f.a, b.r, b.g, b.b, b.a, 0, 0, mode, blur});
  ++quadsFrame_;
}

void Batch2D::Rect(float x, float y, float w, float h, Color fill, float radius, float border, Color borderColor) {
  const float pad = 1.f; // room for the anti-aliased edge
  float hw = w * 0.5f, hh = h * 0.5f;
  float c[4][2] = {{x - pad, y - pad}, {x + w + pad, y - pad}, {x + w + pad, y + h + pad}, {x - pad, y + h + pad}};
  float l[4][2] = {{-hw - pad, -hh - pad}, {hw + pad, -hh - pad}, {hw + pad, hh + pad}, {-hw - pad, hh + pad}};
  PushQuad(c, l, hw, hh, radius, border, fill, borderColor);
}

void Batch2D::Shadow(float x, float y, float w, float h, Color color, float radius, float blur) {
  const float pad = 1.f + blur; // the soft edge extends blur/2 outside the box
  float hw = w * 0.5f, hh = h * 0.5f;
  float c[4][2] = {{x - pad, y - pad}, {x + w + pad, y - pad}, {x + w + pad, y + h + pad}, {x - pad, y + h + pad}};
  float l[4][2] = {{-hw - pad, -hh - pad}, {hw + pad, -hh - pad}, {hw + pad, hh + pad}, {-hw - pad, hh + pad}};
  PushQuad(c, l, hw, hh, radius, 0.f, color, color, 0.f, blur > 0.f ? blur : 0.f);
}

void Batch2D::Triangle(float x, float y, float w, float h, Color fill, float border, Color borderColor) {
  const float pad = 1.f;
  float hw = w * 0.5f, hh = h * 0.5f;
  float c[4][2] = {{x - pad, y - pad}, {x + w + pad, y - pad}, {x + w + pad, y + h + pad}, {x - pad, y + h + pad}};
  float l[4][2] = {{-hw - pad, -hh - pad}, {hw + pad, -hh - pad}, {hw + pad, hh + pad}, {-hw - pad, hh + pad}};
  PushQuad(c, l, hw, hh, 0.f, border, fill, borderColor, 2.f);
}

void Batch2D::Line(float x0, float y0, float x1, float y1, float thickness, Color color) {
  float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.f) return;
  float ux = dx / len, uy = dy / len, nx = -uy, ny = ux; // along / normal
  const float pad = 1.f;
  float hl = len * 0.5f, ht = thickness * 0.5f, cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  float l[4][2] = {{-hl - pad, -ht - pad}, {hl + pad, -ht - pad}, {hl + pad, ht + pad}, {-hl - pad, ht + pad}};
  float c[4][2];
  for (int i = 0; i < 4; ++i) {
    c[i][0] = cx + ux * l[i][0] + nx * l[i][1];
    c[i][1] = cy + uy * l[i][0] + ny * l[i][1];
  }
  PushQuad(c, l, hl, ht, 0.f, 0.f, color, color);
}

void Batch2D::Glyph(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color c) {
  if (vertices_.size() >= kMaxQuads * 4) Flush();
  const float px[4][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
  const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
  for (int i = 0; i < 4; ++i)
    vertices_.push_back({px[i][0], px[i][1], 0, 0, 0, 0, 0, 0, c.r, c.g, c.b, c.a, 0, 0, 0, 0,
                         uv[i][0], uv[i][1], 1, 0});
  ++quadsFrame_;
}

void Batch2D::Flush() {
  if (vertices_.empty()) return;
  uint32_t quads = (uint32_t)(vertices_.size() / 4);
  rhi_->UpdateBuffer(vertexBuffer_, vertices_.data(), vertices_.size() * sizeof(Vertex));
  rhi_->BindTexture(0, atlas_.Valid() ? atlas_ : dummyTexture_);
  rhi_->DrawIndexed(pipeline_, vertexBuffer_, indexBuffer_, quads * 6);
  vertices_.clear();
  ++flushesFrame_;
}

void Batch2D::End() { Flush(); }
}
