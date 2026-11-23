#include "Render3D/Renderer3D.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace bdg {
namespace {
const char* kVS = R"(#version 450 core
layout(std140, binding = 1) uniform Frame3D { mat4 uViewProj; vec4 uCamPos; vec4 uToLight; vec4 uLight; vec4 uSky; vec4 uGround; };
layout(std140, binding = 2) uniform Object3D { mat4 uModel; vec4 uBaseColor; vec4 uParams; };
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
out vec3 vWorld; out vec3 vNormal; out vec2 vUV;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = transpose(inverse(mat3(uModel))) * aNormal;
  vUV = aUV;
  gl_Position = uViewProj * w;
}
)";
const char* kFS = R"(#version 450 core
layout(std140, binding = 1) uniform Frame3D { mat4 uViewProj; vec4 uCamPos; vec4 uToLight; vec4 uLight; vec4 uSky; vec4 uGround; };
layout(std140, binding = 2) uniform Object3D { mat4 uModel; vec4 uBaseColor; vec4 uParams; }; // metallic, roughness, unlit, textured
layout(binding = 0) uniform sampler2D uBaseMap;
in vec3 vWorld; in vec3 vNormal; in vec2 vUV;
out vec4 oColor;
const float PI = 3.14159265;
vec3 ToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
float D_GGX(float NoH, float a) { float a2 = a * a; float d = NoH * NoH * (a2 - 1.0) + 1.0; return a2 / (PI * d * d); }
float V_SmithGGX(float NoV, float NoL, float a) { // height-correlated Smith visibility (Filament)
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2), gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(float VoH, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
vec2 EnvBRDFApprox(float NoV, float rough) { // Karis, mobile split-sum approximation
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022), c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}
void main() {
  vec4 tex = texture(uBaseMap, vUV); // sampled outside branches; a white texture when there is no map
  vec3 base = uBaseColor.rgb * (uParams.w > 0.5 ? tex.rgb : vec3(1.0));
  if (uParams.z > 0.5) { oColor = vec4(ToSrgb(base), 1.0); return; }
  vec3 N = normalize(vNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(uCamPos.xyz - vWorld), L = normalize(uToLight.xyz), H = normalize(V + L);
  float NoV = max(dot(N, V), 1e-4), NoL = max(dot(N, L), 0.0), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  float metallic = clamp(uParams.x, 0.0, 1.0), rough = clamp(uParams.y, 0.045, 1.0), a = rough * rough;
  vec3 f0 = mix(vec3(0.04), base, metallic), diffuse = base * (1.0 - metallic);
  vec3 F = F_Schlick(VoH, f0);
  vec3 color = (diffuse / PI * (1.0 - F) + D_GGX(NoH, a) * V_SmithGGX(NoV, NoL, a) * F) * uLight.rgb * NoL;
  vec3 R = reflect(-V, N);
  vec3 ambient = mix(uGround.rgb, uSky.rgb, N.y * 0.5 + 0.5);
  vec3 env = mix(uGround.rgb, uSky.rgb, smoothstep(-0.2, 0.2, R.y)); // sharp horizon in reflections
  vec2 ab = EnvBRDFApprox(NoV, rough);
  color += diffuse * ambient + env * (f0 * ab.x + ab.y);
  oColor = vec4(ToSrgb(color), 1.0);
}
)";

struct FrameData { float viewProj[16]; float camPos[4], toLight[4], light[4], sky[4], ground[4]; };
struct ObjectData { float model[16]; float baseColor[4]; float params[4]; };
}

glm::mat4 Camera3D::View() const { return glm::lookAt(position, target, glm::vec3(0, 1, 0)); }
glm::mat4 Camera3D::Projection(float aspect) const { return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ); }

Color SrgbToLinear(uint8_t r, uint8_t g, uint8_t b) {
  auto f = [](uint8_t v) {
    float c = v / 255.f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
  };
  return {f(r), f(g), f(b), 1};
}

bool ProjectToPixel(const Camera3D& cam, int32_t w, int32_t h, const glm::vec3& p, float& px, float& py) {
  glm::vec4 c = cam.Projection((float)w / (float)std::max(h, 1)) * cam.View() * glm::vec4(p, 1.f);
  if (c.w <= 0.f) return false;
  px = (c.x / c.w * 0.5f + 0.5f) * w;
  py = (c.y / c.w * 0.5f + 0.5f) * h;
  return true;
}

bool Renderer3D::Init(RHI& rhi) {
  rhi_ = &rhi;
  PipelineDesc pd;
  pd.vertexSource = kVS;
  pd.fragmentSource = kFS;
  pd.attrs[0] = {0, 3, offsetof(Vertex3D, px)};
  pd.attrs[1] = {1, 3, offsetof(Vertex3D, nx)};
  pd.attrs[2] = {2, 2, offsetof(Vertex3D, u)};
  pd.attrCount = 3;
  pd.stride = sizeof(Vertex3D);
  pd.depthTest = true;
  pd.cull = CullMode::Back;
  pd.debugName = "forward3d";
  pipeline_ = rhi.CreatePipeline(pd);
  if (!pipeline_.Valid()) return false;
  frameUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(FrameData), true);
  const uint8_t white[4] = {255, 255, 255, 255};
  white_ = rhi.CreateTexture2D(1, 1, TextureFormat::SRGBA8, white);
  size_t a = rhi.UniformAlignment();
  objectStride_ = (sizeof(ObjectData) + a - 1) / a * a;
  return true;
}

void Renderer3D::Shutdown() {
  if (!rhi_) return;
  rhi_->DestroyBuffer(frameUbo_);
  rhi_->DestroyBuffer(objectUbo_);
  rhi_->DestroyTexture(white_);
  rhi_->DestroyPipeline(pipeline_);
  rhi_ = nullptr;
}

Mesh3D Renderer3D::CreateMesh(const MeshData& d) {
  Mesh3D m;
  m.vertices = rhi_->CreateBuffer(BufferUsage::Vertex, d.vertices.data(), d.vertices.size() * sizeof(Vertex3D));
  m.indices = rhi_->CreateBuffer(BufferUsage::Index, d.indices.data(), d.indices.size() * sizeof(uint32_t));
  m.indexCount = (uint32_t)d.indices.size();
  return m;
}

void Renderer3D::DestroyMesh(Mesh3D& m) {
  if (!rhi_) return;
  rhi_->DestroyBuffer(m.vertices);
  rhi_->DestroyBuffer(m.indices);
  m = {};
}

void Renderer3D::Render(RenderTargetHandle rt, int32_t w, int32_t h, const Camera3D& cam, const Light3D& light,
                        const std::vector<DrawItem3D>& items, Color clear) {
  draws_ = 0;
  if (!rhi_ || !rt.Valid() || w <= 0 || h <= 0) return;
  FrameData f{};
  glm::mat4 vp = cam.Projection((float)w / (float)h) * cam.View();
  std::memcpy(f.viewProj, glm::value_ptr(vp), sizeof(f.viewProj));
  f.camPos[0] = cam.position.x; f.camPos[1] = cam.position.y; f.camPos[2] = cam.position.z;
  f.toLight[0] = light.toLight.x; f.toLight[1] = light.toLight.y; f.toLight[2] = light.toLight.z;
  f.light[0] = light.color.r * light.intensity; f.light[1] = light.color.g * light.intensity; f.light[2] = light.color.b * light.intensity;
  f.sky[0] = light.sky.r; f.sky[1] = light.sky.g; f.sky[2] = light.sky.b;
  f.ground[0] = light.ground.r; f.ground[1] = light.ground.g; f.ground[2] = light.ground.b;
  rhi_->UpdateBuffer(frameUbo_, &f, sizeof(f));

  // All object constants in one buffer, one aligned slice per draw (no buffer rewrite between draws).
  if (items.size() > objectCapacity_) {
    rhi_->DestroyBuffer(objectUbo_);
    objectCapacity_ = std::max<size_t>(items.size(), 64) * 2;
    objectUbo_ = rhi_->CreateBuffer(BufferUsage::Uniform, nullptr, objectCapacity_ * objectStride_, true);
  }
  std::vector<uint8_t> blob(items.size() * objectStride_, 0);
  for (size_t i = 0; i < items.size(); ++i) {
    const DrawItem3D& it = items[i];
    ObjectData o{};
    std::memcpy(o.model, glm::value_ptr(it.model), sizeof(o.model));
    const Material3D& m = it.material;
    o.baseColor[0] = m.baseColor.r; o.baseColor[1] = m.baseColor.g; o.baseColor[2] = m.baseColor.b; o.baseColor[3] = m.baseColor.a;
    o.params[0] = m.metallic; o.params[1] = m.roughness; o.params[2] = m.unlit ? 1.f : 0.f; o.params[3] = m.baseMap.Valid() ? 1.f : 0.f;
    std::memcpy(blob.data() + i * objectStride_, &o, sizeof(o));
  }
  if (!blob.empty()) rhi_->UpdateBuffer(objectUbo_, blob.data(), blob.size());

  rhi_->BeginRenderTarget(rt, clear);
  rhi_->BindUniformBuffer(1, frameUbo_);
  for (size_t i = 0; i < items.size(); ++i) {
    const DrawItem3D& it = items[i];
    if (!it.mesh || !it.mesh->indexCount) continue;
    rhi_->BindUniformBufferRange(2, objectUbo_, i * objectStride_, sizeof(ObjectData));
    rhi_->BindTexture(0, it.material.baseMap.Valid() ? it.material.baseMap : white_);
    rhi_->DrawIndexed(pipeline_, it.mesh->vertices, it.mesh->indices, it.mesh->indexCount);
    ++draws_;
  }
  rhi_->EndRenderTarget();
}
}
