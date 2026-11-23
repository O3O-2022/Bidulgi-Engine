#include "Render3D/Renderer3D.h"
#include "Assets/GltfImport.h"
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
layout(std140, binding = 2) uniform Object3D { mat4 uModel; vec4 uBaseColor; vec4 uParams; vec4 uEmissive; vec4 uMaps; vec4 uExtra; };
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
out vec3 vWorld; out vec3 vNormal; out vec2 vUV; out vec4 vTangent;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = transpose(inverse(mat3(uModel))) * aNormal;
  vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
  vUV = aUV;
  gl_Position = uViewProj * w;
}
)";
// Object3D: params = (metallic, roughness, unlit, alpha mode 0 opaque / 1 mask / 2 blend),
// emissive = (rgb, normal scale), maps = has (base, metal-rough, normal, occlusion), extra = (has emissive,
// occlusion strength, alpha cutoff, -).
const char* kFS = R"(#version 450 core
layout(std140, binding = 1) uniform Frame3D { mat4 uViewProj; vec4 uCamPos; vec4 uToLight; vec4 uLight; vec4 uSky; vec4 uGround; };
layout(std140, binding = 2) uniform Object3D { mat4 uModel; vec4 uBaseColor; vec4 uParams; vec4 uEmissive; vec4 uMaps; vec4 uExtra; };
layout(binding = 0) uniform sampler2D uBaseMap;
layout(binding = 1) uniform sampler2D uMetalRoughMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uOcclusionMap;
layout(binding = 4) uniform sampler2D uEmissiveMap;
in vec3 vWorld; in vec3 vNormal; in vec2 vUV; in vec4 vTangent;
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
  // Every map is sampled and every derivative taken before any branch (implicit LOD needs uniform control flow).
  vec4 baseTex = texture(uBaseMap, vUV), mr = texture(uMetalRoughMap, vUV), nTex = texture(uNormalMap, vUV);
  vec4 occ = texture(uOcclusionMap, vUV), emTex = texture(uEmissiveMap, vUV);
  vec3 dpx = dFdx(vWorld), dpy = dFdy(vWorld);
  vec2 duvx = dFdx(vUV), duvy = dFdy(vUV);

  vec4 base = uBaseColor * (uMaps.x > 0.5 ? baseTex : vec4(1.0));
  if (uParams.w > 0.5 && uParams.w < 1.5 && base.a < uExtra.z) discard; // alpha mask
  float alpha = uParams.w > 1.5 ? base.a : 1.0;
  if (uParams.z > 0.5) { oColor = vec4(ToSrgb(base.rgb), alpha); return; }

  vec3 ng = normalize(vNormal);
  // Tangent frame: from the mesh when it has tangents, otherwise from screen-space UV derivatives
  // (the glTF sample viewer's approach); bitangent = cross(N, T) * w.
  float det = duvx.x * duvy.y - duvy.x * duvx.y;
  vec3 t = (duvy.y * dpx - duvx.y * dpy) / (abs(det) > 1e-12 ? det : 1e-12);
  float w = 1.0;
  if (vTangent.w != 0.0) { t = vTangent.xyz; w = vTangent.w; }
  t = t - ng * dot(ng, t);
  t = dot(t, t) > 1e-16 ? normalize(t) : vec3(1.0, 0.0, 0.0);
  vec3 b = cross(ng, t) * w;
  if (!gl_FrontFacing) { t = -t; b = -b; ng = -ng; }
  vec3 N = ng;
  if (uMaps.z > 0.5) {
    vec3 tn = nTex.xyz * 2.0 - 1.0;
    tn.xy *= uEmissive.w;
    N = normalize(mat3(t, b, ng) * tn);
  }

  vec3 V = normalize(uCamPos.xyz - vWorld), L = normalize(uToLight.xyz), H = normalize(V + L);
  float NoV = max(dot(N, V), 1e-4), NoL = max(dot(N, L), 0.0), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  float metallic = uParams.x, rough = uParams.y;
  if (uMaps.y > 0.5) { rough *= mr.g; metallic *= mr.b; }
  metallic = clamp(metallic, 0.0, 1.0);
  rough = clamp(rough, 0.045, 1.0);
  float a = rough * rough;
  vec3 f0 = mix(vec3(0.04), base.rgb, metallic), diffuse = base.rgb * (1.0 - metallic);
  vec3 F = F_Schlick(VoH, f0);
  vec3 color = (diffuse / PI * (1.0 - F) + D_GGX(NoH, a) * V_SmithGGX(NoV, NoL, a) * F) * uLight.rgb * NoL;
  vec3 R = reflect(-V, N);
  vec3 ambient = mix(uGround.rgb, uSky.rgb, N.y * 0.5 + 0.5);
  vec3 env = mix(uGround.rgb, uSky.rgb, smoothstep(-0.2, 0.2, R.y)); // sharp horizon in reflections
  vec2 ab = EnvBRDFApprox(NoV, rough);
  float ao = uMaps.w > 0.5 ? 1.0 + uExtra.y * (occ.r - 1.0) : 1.0; // occlusion dims indirect light only
  color += (diffuse * ambient + env * (f0 * ab.x + ab.y)) * ao;
  color += uEmissive.rgb * (uExtra.x > 0.5 ? emTex.rgb : vec3(1.0));
  oColor = vec4(ToSrgb(color), alpha);
}
)";

// Editor grid on y = 0 around the camera. Colours are display values (the target holds display-encoded colour).
const char* kGridVS = R"(#version 450 core
layout(std140, binding = 1) uniform Frame3D { mat4 uViewProj; vec4 uCamPos; vec4 uToLight; vec4 uLight; vec4 uSky; vec4 uGround; };
layout(location = 0) in vec3 aPos;
out vec3 vWorld;
void main() {
  float r = clamp(abs(uCamPos.y) * 40.0, 60.0, 1500.0);
  vWorld = vec3(uCamPos.x + aPos.x * r, 0.0, uCamPos.z + aPos.z * r);
  // Lifted a hair above y = 0 so it stays visible on a ground plane at the same height (no z-fighting).
  float lift = 0.0015 * clamp(abs(uCamPos.y), 1.0, 1000.0);
  gl_Position = uViewProj * vec4(vWorld + vec3(0.0, lift, 0.0), 1.0);
}
)";
const char* kGridFS = R"(#version 450 core
layout(std140, binding = 1) uniform Frame3D { mat4 uViewProj; vec4 uCamPos; vec4 uToLight; vec4 uLight; vec4 uSky; vec4 uGround; };
in vec3 vWorld;
out vec4 oColor;
float Lines(vec2 p, float spacing, float width) {
  vec2 c = p / spacing, d = fwidth(c);
  vec2 g = abs(fract(c - 0.5) - 0.5) / max(d, vec2(1e-6));
  return 1.0 - min(min(g.x, g.y) / width, 1.0);
}
void main() {
  vec2 p = vWorld.xz, d = fwidth(p);
  float density = max(d.x, d.y);                                       // metres per pixel here
  float minor = Lines(p, 1.0, 1.0) * (1.0 - smoothstep(0.08, 0.3, density));
  float major = Lines(p, 10.0, 1.2) * (1.0 - smoothstep(0.8, 3.0, density));
  float ax = 1.0 - min(abs(p.y) / max(d.y * 1.5, 1e-6), 1.0);          // X axis: the line z = 0
  float az = 1.0 - min(abs(p.x) / max(d.x * 1.5, 1e-6), 1.0);          // Z axis: the line x = 0
  float fadeEnd = clamp(abs(uCamPos.y) * 30.0, 40.0, 1200.0);
  float fade = 1.0 - smoothstep(fadeEnd * 0.35, fadeEnd, length(vWorld - uCamPos.xyz));
  vec3 col = vec3(0.62);
  float a = max(minor * 0.28, major * 0.5);
  col = mix(col, vec3(0.88, 0.27, 0.27), ax);
  col = mix(col, vec3(0.3, 0.5, 0.95), az * (1.0 - ax));
  a = max(a, max(ax, az) * 0.85);
  if (a * fade < 0.004) discard;
  oColor = vec4(col, a * fade);
}
)";
// Selection: selected items are drawn white into a mask; the outline pass marks pixels within 2 px of the mask.
const char* kMaskFS = R"(#version 450 core
out vec4 oColor;
void main() { oColor = vec4(1.0); }
)";
const char* kFullscreenVS = R"(#version 450 core
layout(location = 0) in vec3 aPos;
void main() { gl_Position = vec4(aPos.xy, 0.0, 1.0); }
)";
const char* kOutlineFS = R"(#version 450 core
layout(binding = 0) uniform sampler2D uMask;
out vec4 oColor;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy), last = textureSize(uMask, 0) - 1;
  if (texelFetch(uMask, clamp(p, ivec2(0), last), 0).r > 0.5) discard;
  float m = 0.0;
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
      if (x * x + y * y <= 5) m = max(m, texelFetch(uMask, clamp(p + ivec2(x, y), ivec2(0), last), 0).r);
  if (m < 0.5) discard;
  oColor = vec4(1.0, 0.58, 0.12, 1.0);
}
)";

struct FrameData { float viewProj[16]; float camPos[4], toLight[4], light[4], sky[4], ground[4]; };
struct ObjectData { float model[16]; float baseColor[4], params[4], emissive[4], maps[4], extra[4]; };
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
  pd.attrs[3] = {3, 4, offsetof(Vertex3D, tx)};
  pd.attrCount = 4;
  pd.stride = sizeof(Vertex3D);
  pd.depthTest = true;
  pd.cull = CullMode::Back;
  pd.debugName = "forward3d";
  opaque_ = rhi.CreatePipeline(pd);
  pd.cull = CullMode::None;
  pd.debugName = "forward3d.twosided";
  twoSided_ = rhi.CreatePipeline(pd);
  pd.alphaBlend = true;
  pd.depthWrite = false;
  pd.debugName = "forward3d.blend";
  blend_ = rhi.CreatePipeline(pd);
  if (!opaque_.Valid() || !twoSided_.Valid() || !blend_.Valid()) return false;
  pd.fragmentSource = kMaskFS; // same vertex stage and layout as the scene
  pd.alphaBlend = false;
  pd.depthTest = false;
  pd.debugName = "editor.mask";
  mask_ = rhi.CreatePipeline(pd);
  PipelineDesc gp;
  gp.vertexSource = kGridVS;
  gp.fragmentSource = kGridFS;
  gp.attrs[0] = {0, 3, 0};
  gp.attrCount = 1;
  gp.stride = 12;
  gp.alphaBlend = true;
  gp.depthTest = true;
  gp.depthWrite = false;
  gp.debugName = "editor.grid";
  grid_ = rhi.CreatePipeline(gp);
  gp.vertexSource = kFullscreenVS;
  gp.fragmentSource = kOutlineFS;
  gp.depthTest = false;
  gp.debugName = "editor.outline";
  outline_ = rhi.CreatePipeline(gp);
  if (!mask_.Valid() || !grid_.Valid() || !outline_.Valid()) return false;
  const float quad[18] = {-1, 0, -1, -1, 0, 1, 1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0, -1};
  gridQuad_ = rhi.CreateBuffer(BufferUsage::Vertex, quad, sizeof(quad));
  const float tri[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  fullscreen_ = rhi.CreateBuffer(BufferUsage::Vertex, tri, sizeof(tri));
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
  for (PipelineHandle p : {opaque_, twoSided_, blend_, grid_, mask_, outline_}) rhi_->DestroyPipeline(p);
  rhi_->DestroyBuffer(gridQuad_);
  rhi_->DestroyBuffer(fullscreen_);
  rhi_->DestroyRenderTarget(maskRT_);
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

Model3D Renderer3D::UploadModel(const ImportedModel& im) {
  Model3D m;
  for (const ImportedTexture& t : im.textures) {
    if (t.rgba.empty()) { m.textures.push_back({}); continue; }
    TextureDesc d;
    d.w = t.w;
    d.h = t.h;
    d.format = t.srgb ? TextureFormat::SRGBA8 : TextureFormat::RGBA8;
    d.mipmaps = true;
    d.repeat = !t.clamp;
    d.nearest = t.nearest;
    m.textures.push_back(rhi_->CreateTexture(d, t.rgba.data()));
  }
  auto tex = [&](int i) { return i >= 0 && i < (int)m.textures.size() ? m.textures[i] : TextureHandle{}; };
  for (const ImportedPrimitive& p : im.primitives) {
    m.meshes.push_back(p.mesh.indices.empty() ? Mesh3D{} : CreateMesh(p.mesh));
    Material3D mat; // glTF default material: white, metallic 1, roughness 1
    mat.baseColor = {1, 1, 1, 1};
    mat.metallic = 1;
    mat.roughness = 1;
    if (p.material >= 0 && p.material < (int)im.materials.size()) {
      const ImportedMaterial& s = im.materials[p.material];
      mat.baseColor = {s.baseColor[0], s.baseColor[1], s.baseColor[2], s.baseColor[3]};
      mat.metallic = s.metallic;
      mat.roughness = s.roughness;
      mat.emissive = {s.emissive[0], s.emissive[1], s.emissive[2], 1};
      mat.unlit = s.unlit;
      mat.doubleSided = s.doubleSided;
      mat.alpha = (Material3D::Alpha)s.alpha;
      mat.alphaCutoff = s.alphaCutoff;
      mat.normalScale = s.normalScale;
      mat.occlusionStrength = s.occlusionStrength;
      mat.baseMap = tex(s.baseColorTex);
      mat.metalRoughMap = tex(s.metalRoughTex);
      mat.normalMap = tex(s.normalTex);
      mat.occlusionMap = tex(s.occlusionTex);
      mat.emissiveMap = tex(s.emissiveTex);
    }
    m.materials.push_back(mat);
  }
  for (const ImportedModel::Instance& in : im.instances) m.instances.push_back({in.primitive, in.world});
  m.boundsMin = im.boundsMin;
  m.boundsMax = im.boundsMax;
  return m;
}

void Renderer3D::DestroyModel(Model3D& m) {
  for (Mesh3D& mesh : m.meshes) DestroyMesh(mesh);
  if (rhi_)
    for (TextureHandle t : m.textures) rhi_->DestroyTexture(t);
  m = {};
}

void Renderer3D::AppendModel(const Model3D& m, const glm::mat4& placement, std::vector<DrawItem3D>& items) {
  for (const Model3D::Instance& in : m.instances)
    if (in.mesh >= 0 && in.mesh < (int)m.meshes.size() && m.meshes[in.mesh].indexCount)
      items.push_back({&m.meshes[in.mesh], placement * in.world, m.materials[in.mesh]});
}

void Renderer3D::Render(RenderTargetHandle rt, int32_t w, int32_t h, const Camera3D& cam, const Light3D& light,
                        const std::vector<DrawItem3D>& items, Color clear, const RenderExtras* extras) {
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
    o.params[0] = m.metallic; o.params[1] = m.roughness; o.params[2] = m.unlit ? 1.f : 0.f; o.params[3] = (float)m.alpha;
    o.emissive[0] = m.emissive.r; o.emissive[1] = m.emissive.g; o.emissive[2] = m.emissive.b; o.emissive[3] = m.normalScale;
    auto has = [](TextureHandle t) { return t.Valid() ? 1.f : 0.f; };
    o.maps[0] = has(m.baseMap); o.maps[1] = has(m.metalRoughMap); o.maps[2] = has(m.normalMap); o.maps[3] = has(m.occlusionMap);
    o.extra[0] = has(m.emissiveMap); o.extra[1] = m.occlusionStrength; o.extra[2] = m.alphaCutoff;
    std::memcpy(blob.data() + i * objectStride_, &o, sizeof(o));
  }
  if (!blob.empty()) rhi_->UpdateBuffer(objectUbo_, blob.data(), blob.size());

  rhi_->BindUniformBuffer(1, frameUbo_);
  bool outline = extras && !extras->outlined.empty();
  if (outline) { // selection mask first (its own target, no depth: the outline shows through other objects)
    if (!maskRT_.Valid() || maskW_ != w || maskH_ != h) {
      rhi_->DestroyRenderTarget(maskRT_);
      maskRT_ = rhi_->CreateRenderTarget(w, h, 1);
      maskW_ = w;
      maskH_ = h;
    }
    rhi_->BeginRenderTarget(maskRT_, {0, 0, 0, 0});
    for (size_t i : extras->outlined)
      if (i < items.size() && items[i].mesh && items[i].mesh->indexCount) {
        rhi_->BindUniformBufferRange(2, objectUbo_, i * objectStride_, sizeof(ObjectData));
        rhi_->DrawIndexed(mask_, items[i].mesh->vertices, items[i].mesh->indices, items[i].mesh->indexCount);
        ++draws_;
      }
    rhi_->EndRenderTarget();
  }
  rhi_->BeginRenderTarget(rt, clear);
  std::vector<size_t> order, blended;
  for (size_t i = 0; i < items.size(); ++i)
    if (items[i].mesh && items[i].mesh->indexCount) (items[i].material.alpha == Material3D::Alpha::Blend ? blended : order).push_back(i);
  auto dist = [&](size_t i) { return glm::distance(glm::vec3(items[i].model[3]), cam.position); };
  std::sort(blended.begin(), blended.end(), [&](size_t a, size_t b) { return dist(a) > dist(b); }); // back to front
  size_t opaqueCount = order.size();
  order.insert(order.end(), blended.begin(), blended.end());
  for (size_t n = 0; n <= order.size(); ++n) {
    if (n == opaqueCount && extras && extras->grid) { // after opaque surfaces, before blended ones
      rhi_->Draw(grid_, gridQuad_, 6);
      ++draws_;
    }
    if (n == order.size()) break;
    size_t i = order[n];
    const DrawItem3D& it = items[i];
    const Material3D& m = it.material;
    rhi_->BindUniformBufferRange(2, objectUbo_, i * objectStride_, sizeof(ObjectData));
    const TextureHandle maps[5] = {m.baseMap, m.metalRoughMap, m.normalMap, m.occlusionMap, m.emissiveMap};
    for (uint32_t slot = 0; slot < 5; ++slot) rhi_->BindTexture(slot, maps[slot].Valid() ? maps[slot] : white_);
    PipelineHandle p = m.alpha == Material3D::Alpha::Blend ? blend_ : m.doubleSided ? twoSided_ : opaque_;
    rhi_->DrawIndexed(p, it.mesh->vertices, it.mesh->indices, it.mesh->indexCount);
    ++draws_;
    triangles_ += it.mesh->indexCount / 3;
  }
  if (outline) {
    rhi_->BindTexture(0, rhi_->RenderTargetColor(maskRT_));
    rhi_->Draw(outline_, fullscreen_, 3);
    ++draws_;
  }
  rhi_->EndRenderTarget();
}
}
