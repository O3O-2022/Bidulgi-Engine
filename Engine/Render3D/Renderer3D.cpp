#include "Render3D/Renderer3D.h"
#include "Assets/GltfImport.h"
#include "Render3D/Culling.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <format>
#include <functional>
#include <tuple>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace bdg {
namespace {
// Frame constants shared by every 3D shader (std140; FrameData below matches it).
//   uSun = (cos of half the sun disk angle, 1 = no disk, -, -); uShadow = (on, depth bias, normal offset, map size)
const char* kFrameUBO = R"(layout(std140, binding = 1) uniform Frame3D {
  mat4 uViewProj; mat4 uInvViewProj; mat4 uLightViewProj;
  vec4 uCamPos; vec4 uToLight; vec4 uLight; vec4 uSky; vec4 uGround; vec4 uZenith; vec4 uHorizon; vec4 uSun; vec4 uShadow; vec4 uCounts;
};
)";
// Sky colour along a direction; blur widens the horizon (rough reflections). Used by the sky pass and shading.
const char* kSkyFn = R"(vec3 SkyColor(vec3 d, float blur) {
  vec3 up = mix(uHorizon.rgb, uZenith.rgb, pow(clamp(d.y, 0.0, 1.0), 0.5));
  float b = 0.03 + blur * 0.5;
  return mix(uGround.rgb, up, smoothstep(-b, b, d.y));
}
)";
// Object constants (queue 3-9): one std430 array for the whole frame, indexed per instance, so a run of alike
// objects is one instanced draw. The old uniform names stay as macros.
const char* kObjectsCommon = R"(struct Object3D { mat4 model; vec4 baseColor; vec4 params; vec4 emissive; vec4 maps; vec4 extra; };
layout(std430, binding = 0) readonly buffer Objects3D { Object3D uObjects[]; };
)";
const char* kObjectsVS = R"(layout(location = 4) in float aObject; // this instance's index into uObjects
flat out int vObject;
#define uModel uObjects[int(aObject + 0.5)].model
)";
const char* kObjectsFS = R"(flat in int vObject;
#define uModel uObjects[vObject].model
#define uBaseColor uObjects[vObject].baseColor
#define uParams uObjects[vObject].params
#define uEmissive uObjects[vObject].emissive
#define uMaps uObjects[vObject].maps
#define uExtra uObjects[vObject].extra
)";
std::string Src(const char* text) { // expands #FRAME_UBO, #SKY_FN, #OBJECTS_VS and #OBJECTS_FS
  std::string s = text;
  const std::string vsObjects = std::string(kObjectsCommon) + kObjectsVS, fsObjects = std::string(kObjectsCommon) + kObjectsFS;
  for (auto [token, body] : {std::pair{"#FRAME_UBO", kFrameUBO}, std::pair{"#SKY_FN", kSkyFn}, std::pair{"#OBJECTS_VS", vsObjects.c_str()},
                             std::pair{"#OBJECTS_FS", fsObjects.c_str()}})
    for (size_t at; (at = s.find(token)) != std::string::npos;) s.replace(at, std::strlen(token), body);
  return s;
}

const char* kVS = R"(#version 450 core
#FRAME_UBO
#OBJECTS_VS
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
layout(std140, binding = 3) uniform Lights3D { vec4 uLPos[16]; vec4 uLColor[16]; vec4 uLDir[16]; vec4 uLCone[16]; };
out vec3 vWorld; out vec3 vNormal; out vec2 vUV; out vec4 vTangent; out vec3 vLit;
void main() {
  vObject = int(aObject + 0.5);
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = transpose(inverse(mat3(uModel))) * aNormal;
  vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
  vUV = aUV;
  vLit = vec3(0.0);
  if (uCounts.y > 0.5 && uCounts.y < 1.5) { // low tier: diffuse light per vertex (sun, sky, local lights)
    vec3 n = normalize(vNormal);
    vLit = uLight.rgb * max(dot(n, normalize(uToLight.xyz)), 0.0) / 3.14159265 +
           mix(uGround.rgb, mix(uHorizon.rgb, uZenith.rgb, 0.6), n.y * 0.5 + 0.5);
    for (int i = 0; i < int(uCounts.x); ++i) {
      vec3 toL = uLPos[i].xyz - vWorld;
      float d2 = max(dot(toL, toL), 1e-4), range = max(uLPos[i].w, 1e-3);
      vec3 L = toL * inversesqrt(d2);
      float win = clamp(1.0 - pow(d2 / (range * range), 2.0), 0.0, 1.0);
      float atten = win * win / (d2 + 1.0);
      if (uLCone[i].y > 0.5) atten *= smoothstep(uLDir[i].w, uLCone[i].x, dot(-L, uLDir[i].xyz));
      vLit += uLColor[i].rgb * atten * max(dot(n, L), 0.0) / 3.14159265;
    }
  }
  gl_Position = uViewProj * w;
}
)";
// Object3D: params = (metallic, roughness, unlit, alpha mode 0 opaque / 1 mask / 2 blend),
// emissive = (rgb, normal scale), maps = has (base, metal-rough, normal, occlusion), extra = (has emissive,
// occlusion strength, alpha cutoff, -).
const char* kFS = R"(#version 450 core
#FRAME_UBO
#OBJECTS_FS
layout(binding = 0) uniform sampler2D uBaseMap;
layout(binding = 1) uniform sampler2D uMetalRoughMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uOcclusionMap;
layout(binding = 4) uniform sampler2D uEmissiveMap;
in vec3 vWorld; in vec3 vNormal; in vec2 vUV; in vec4 vTangent; in vec3 vLit;
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
layout(std140, binding = 3) uniform Lights3D { vec4 uLPos[16]; vec4 uLColor[16]; vec4 uLDir[16]; vec4 uLCone[16]; };
layout(binding = 5) uniform sampler2DShadow uShadowMap;
#SKY_FN
vec3 Brdf(vec3 N, vec3 V, vec3 L, vec3 diffuse, vec3 f0, float a) {
  vec3 H = normalize(V + L);
  float NoL = max(dot(N, L), 0.0), NoV = max(dot(N, V), 1e-4), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  vec3 F = F_Schlick(VoH, f0);
  return (diffuse / PI * (1.0 - F) + D_GGX(NoH, a) * V_SmithGGX(NoV, NoL, a) * F) * NoL;
}
vec3 Irradiance(vec3 n) { // light from the whole sky hemisphere above n (ground below)
  return mix(uGround.rgb, mix(uHorizon.rgb, uZenith.rgb, 0.6), clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}
float ShadowFactor(vec3 world, vec3 n) { // 1 = lit; 3x3 taps of 2x2 hardware PCF
  if (uShadow.x < 0.5) return 1.0;
  vec4 c = uLightViewProj * vec4(world + n * uShadow.z, 1.0); // normal offset against acne
  vec3 s = c.xyz / c.w * 0.5 + 0.5;
  if (s.z >= 1.0) return 1.0;
  float t = 1.0 / uShadow.w, sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) sum += textureLod(uShadowMap, vec3(s.xy + vec2(x, y) * t, s.z - uShadow.y), 0.0);
  float edge = smoothstep(0.85, 1.0, max(abs(s.x * 2.0 - 1.0), abs(s.y * 2.0 - 1.0))); // fade at the map's edge
  return mix(sum / 9.0, 1.0, edge);
}
vec3 LocalLight(int i, vec3 world, vec3 N, vec3 V, vec3 diffuse, vec3 f0, float a) {
  vec3 toL = uLPos[i].xyz - world;
  float d2 = max(dot(toL, toL), 1e-4);
  vec3 L = toL * inversesqrt(d2);
  float range = max(uLPos[i].w, 1e-3);
  float win = clamp(1.0 - pow(d2 / (range * range), 2.0), 0.0, 1.0);
  float atten = win * win / (d2 + 1.0); // windowed inverse square (no hard cut at the range)
  if (uLCone[i].y > 0.5) atten *= smoothstep(uLDir[i].w, uLCone[i].x, dot(-L, uLDir[i].xyz)); // spot cone
  return Brdf(N, V, L, diffuse, f0, a) * uLColor[i].rgb * atten;
}
void main() {
  // Every map is sampled and every derivative taken before any branch (implicit LOD needs uniform control flow).
  float bias = uCounts.z; // lower tiers read smaller mips (texture resolution setting)
  vec4 baseTex = texture(uBaseMap, vUV, bias), mr = texture(uMetalRoughMap, vUV, bias), nTex = texture(uNormalMap, vUV, bias);
  vec4 occ = texture(uOcclusionMap, vUV, bias), emTex = texture(uEmissiveMap, vUV, bias);
  vec3 dpx = dFdx(vWorld), dpy = dFdy(vWorld);
  vec2 duvx = dFdx(vUV), duvy = dFdy(vUV);

  vec4 base = uBaseColor * (uMaps.x > 0.5 ? baseTex : vec4(1.0));
  if (uParams.w > 0.5 && uParams.w < 1.5 && base.a < uExtra.z) discard; // alpha mask
  float alpha = uParams.w > 1.5 ? base.a : 1.0;
  vec3 emissive = uEmissive.rgb * (uExtra.x > 0.5 ? emTex.rgb : vec3(1.0));
  int view = int(uCounts.w + 0.5); // debug view (queue 3-8): 0 lit, 1 albedo, 2 normal, 3 rough, 4 metal, 5 depth, 6 shadow
  if (view == 1) { oColor = vec4(base.rgb, 1.0); return; }
  if (uParams.z > 0.5 && view == 0) { oColor = vec4(base.rgb, alpha); return; }        // unlit material
  if (uCounts.y < 0.5 && view == 0) { oColor = vec4(base.rgb + emissive, alpha); return; }          // potato tier: no lighting
  if (uCounts.y < 1.5 && view == 0) { oColor = vec4(base.rgb * vLit + emissive, alpha); return; }   // low tier: per vertex

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
    vec3 tn;
    tn.xy = nTex.xy * 2.0 - 1.0; // X and Y from the map; Z rebuilt so BC5 (two channel) maps work too
    tn.z = sqrt(max(1.0 - dot(tn.xy, tn.xy), 0.0));
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
  if (view >= 2) {
    vec3 d = view == 2 ? N * 0.5 + 0.5 : view == 3 ? vec3(rough) : view == 4 ? vec3(metallic)
           : view == 5 ? vec3(clamp(log2(1.0 + length(vWorld - uCamPos.xyz)) / log2(201.0), 0.0, 1.0)) // 0..200 m, log
           : vec3(ShadowFactor(vWorld, ng));
    oColor = vec4(pow(d, vec3(2.2)), 1.0); // data, not colour: undo the display curve applied in post
    return;
  }
  vec3 f0 = mix(vec3(0.04), base.rgb, metallic), diffuse = base.rgb * (1.0 - metallic);
  vec3 color = Brdf(N, V, L, diffuse, f0, a) * uLight.rgb * ShadowFactor(vWorld, ng);
  for (int i = 0; i < int(uCounts.x); ++i) color += LocalLight(i, vWorld, N, V, diffuse, f0, a);
  vec3 R = reflect(-V, N);
  vec3 ambient = Irradiance(N);
  vec3 env = SkyColor(R, rough); // the sky seen in reflections, softer the rougher the surface
  vec2 ab = EnvBRDFApprox(NoV, rough);
  float ao = uMaps.w > 0.5 ? 1.0 + uExtra.y * (occ.r - 1.0) : 1.0; // occlusion dims indirect light only
  color += (diffuse * ambient + env * (f0 * ab.x + ab.y)) * ao;
  color += emissive;
  oColor = vec4(color, alpha); // linear light; tone mapping and display encoding happen in post
}
)";

// Editor grid on y = 0 around the camera. Colours are display values (the target holds display-encoded colour).
const char* kGridVS = R"(#version 450 core
#FRAME_UBO
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
#FRAME_UBO
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
// Sky behind everything: a fullscreen triangle at the far plane, drawn after the opaque surfaces.
const char* kSkyVS = R"(#version 450 core
layout(location = 0) in vec3 aPos;
out vec2 vNdc;
void main() { vNdc = aPos.xy; gl_Position = vec4(aPos.xy, 1.0, 1.0); }
)";
const char* kSkyFS = R"(#version 450 core
#FRAME_UBO
in vec2 vNdc;
out vec4 oColor;
#SKY_FN
vec3 ToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
  vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 d = normalize(p.xyz / p.w - uCamPos.xyz);
  vec3 c = uCounts.w > 0.5 ? vec3(0.0) : SkyColor(d, 0.0);
  if (uSun.x < 1.0 && uCounts.w < 0.5) { // sun disk with a soft rim
    float k = smoothstep(uSun.x - 0.00008, uSun.x + 0.00008, dot(d, normalize(uToLight.xyz)));
    c += uLight.rgb * 4.0 * k;
  }
  oColor = vec4(c, 1.0);
}
)";
// Shadow casters: depth only; alpha-masked materials cut their holes.
const char* kShadowVS = R"(#version 450 core
#FRAME_UBO
#OBJECTS_VS
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
void main() { vObject = int(aObject + 0.5); vUV = aUV; gl_Position = uLightViewProj * (uModel * vec4(aPos, 1.0)); }
)";
const char* kShadowFS = R"(#version 450 core
#OBJECTS_FS
layout(binding = 0) uniform sampler2D uBaseMap;
in vec2 vUV;
void main() {
  float alpha = uBaseColor.a * (uMaps.x > 0.5 ? texture(uBaseMap, vUV).a : 1.0);
  if (uParams.w > 0.5 && uParams.w < 1.5 && alpha < uExtra.z) discard;
}
)";

// ---- Post-processing (queue 3-5) ----
const char* kPostVS = R"(#version 450 core
layout(location = 0) in vec3 aPos;
out vec2 vUV;
void main() { vUV = aPos.xy * 0.5 + 0.5; gl_Position = vec4(aPos.xy, 0.0, 1.0); }
)";
// uPost = (exposure, bloom intensity, tone map on, bloom on)
const char* kToneMapFS = R"(#version 450 core
layout(std140, binding = 4) uniform Post3D { vec4 uPost; };
layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uBloom;
in vec2 vUV;
out vec4 oColor;
vec3 ACES(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); } // Narkowicz fit
vec3 ToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
  vec3 c = texture(uHdr, vUV).rgb;
  if (uPost.w > 0.5) c += texture(uBloom, vUV).rgb * (uPost.y / 5.0); // the chain sums 5 levels: average them
  c *= uPost.x;
  if (uPost.z > 0.5) c = ACES(c);
  oColor = vec4(ToSrgb(c), 1.0); // opaque: the UI draws this picture with alpha blending
}
)";
// FXAA (the compact NVIDIA FXAA 2 kernel): blends along the local edge direction where contrast is high.
const char* kFxaaFS = R"(#version 450 core
layout(binding = 0) uniform sampler2D uLdr;
in vec2 vUV;
out vec4 oColor;
float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
  vec2 px = 1.0 / vec2(textureSize(uLdr, 0));
  float lM = Luma(texture(uLdr, vUV).rgb);
  float lNW = Luma(textureOffset(uLdr, vUV, ivec2(-1, 1)).rgb), lNE = Luma(textureOffset(uLdr, vUV, ivec2(1, 1)).rgb);
  float lSW = Luma(textureOffset(uLdr, vUV, ivec2(-1, -1)).rgb), lSE = Luma(textureOffset(uLdr, vUV, ivec2(1, -1)).rgb);
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE))), lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
  float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcp, -8.0, 8.0) * px;
  vec3 a = 0.5 * (texture(uLdr, vUV + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uLdr, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
  vec3 b = a * 0.5 + 0.25 * (texture(uLdr, vUV - dir * 0.5).rgb + texture(uLdr, vUV + dir * 0.5).rgb);
  float lB = dot(b, vec3(0.299, 0.587, 0.114));
  oColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
)";
// Bloom (Jimenez, Call of Duty 2014): 13-tap downsample, 3x3 tent upsample added into the next larger level.
const char* kBloomDownFS = R"(#version 450 core
layout(binding = 0) uniform sampler2D uSrc;
in vec2 vUV;
out vec4 oColor;
void main() {
  vec2 t = 1.0 / vec2(textureSize(uSrc, 0));
  vec3 a = texture(uSrc, vUV + t * vec2(-2, 2)).rgb, b = texture(uSrc, vUV + t * vec2(0, 2)).rgb, c = texture(uSrc, vUV + t * vec2(2, 2)).rgb;
  vec3 d = texture(uSrc, vUV + t * vec2(-2, 0)).rgb, e = texture(uSrc, vUV).rgb, f = texture(uSrc, vUV + t * vec2(2, 0)).rgb;
  vec3 g = texture(uSrc, vUV + t * vec2(-2, -2)).rgb, h = texture(uSrc, vUV + t * vec2(0, -2)).rgb, i = texture(uSrc, vUV + t * vec2(2, -2)).rgb;
  vec3 j = texture(uSrc, vUV + t * vec2(-1, 1)).rgb, k = texture(uSrc, vUV + t * vec2(1, 1)).rgb;
  vec3 l = texture(uSrc, vUV + t * vec2(-1, -1)).rgb, m = texture(uSrc, vUV + t * vec2(1, -1)).rgb;
  vec3 o = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
  oColor = vec4(min(o, vec3(64.0)), 1.0); // no fireflies from single very bright pixels
}
)";
const char* kBloomUpFS = R"(#version 450 core
layout(binding = 0) uniform sampler2D uSrc;
in vec2 vUV;
out vec4 oColor;
void main() {
  vec2 t = 1.0 / vec2(textureSize(uSrc, 0));
  vec3 s = texture(uSrc, vUV + t * vec2(-1, 1)).rgb + 2.0 * texture(uSrc, vUV + t * vec2(0, 1)).rgb + texture(uSrc, vUV + t * vec2(1, 1)).rgb
         + 2.0 * texture(uSrc, vUV + t * vec2(-1, 0)).rgb + 4.0 * texture(uSrc, vUV).rgb + 2.0 * texture(uSrc, vUV + t * vec2(1, 0)).rgb
         + texture(uSrc, vUV + t * vec2(-1, -1)).rgb + 2.0 * texture(uSrc, vUV + t * vec2(0, -1)).rgb + texture(uSrc, vUV + t * vec2(1, -1)).rgb;
  oColor = vec4(s / 16.0, 1.0);
}
)";

const char* kWireFS = R"(#version 450 core
out vec4 oColor;
void main() { oColor = vec4(0.25, 0.9, 1.0, 0.55); }
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

struct FrameData {
  float viewProj[16], invViewProj[16], lightViewProj[16];
  float camPos[4], toLight[4], light[4], sky[4], ground[4], zenith[4], horizon[4], sun[4], shadow[4], counts[4];
};
struct LightsData { float pos[16][4], color[16][4], dir[16][4], cone[16][4]; };
struct PostData { float post[4]; };

void Put(float* dst, const glm::vec3& v, float w = 0) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = w; }
void Put(float* dst, Color c, float scale = 1) { dst[0] = c.r * scale; dst[1] = c.g * scale; dst[2] = c.b * scale; dst[3] = c.a; }

// Orthographic light camera around the bounding sphere of the view slice [near, distance]; the centre snaps to
// whole shadow texels so the map does not shimmer while the camera moves.
glm::mat4 FitShadow(const Camera3D& cam, float aspect, const glm::vec3& toLight, float distance, int mapSize, float& texelWorld,
                    float& depthRange) {
  float nearZ = cam.nearZ, farZ = std::min(cam.farZ, distance);
  float ty = std::tan(glm::radians(cam.fovY * 0.5f)), tx = ty * aspect;
  glm::vec3 f = glm::normalize(cam.target - cam.position), r = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
  if (!std::isfinite(r.x)) r = {1, 0, 0};
  glm::vec3 u = glm::cross(r, f), corners[8];
  for (int i = 0; i < 8; ++i) {
    float z = i < 4 ? nearZ : farZ;
    corners[i] = cam.position + f * z + r * (z * tx * ((i & 1) ? 1.f : -1.f)) + u * (z * ty * ((i & 2) ? 1.f : -1.f));
  }
  glm::vec3 center{0};
  for (const glm::vec3& c : corners) center += c / 8.f;
  float radius = 0;
  for (const glm::vec3& c : corners) radius = std::max(radius, glm::distance(c, center));
  radius = std::ceil(radius * 16.f) / 16.f; // steady size: no flicker from tiny changes
  glm::vec3 L = glm::normalize(toLight), up = std::fabs(L.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  texelWorld = 2.f * radius / (float)mapSize;
  glm::mat4 rot = glm::lookAt(glm::vec3(0), -L, up);
  glm::vec3 ls = glm::vec3(rot * glm::vec4(center, 1));
  ls.x = std::floor(ls.x / texelWorld) * texelWorld;
  ls.y = std::floor(ls.y / texelWorld) * texelWorld;
  center = glm::vec3(glm::inverse(rot) * glm::vec4(ls, 1));
  const float casterReach = 100.f; // casters this far toward the light still throw shadows into the slice
  depthRange = 2.f * radius + casterReach;
  glm::mat4 view = glm::lookAt(center + L * (radius + casterReach), center, up);
  return glm::ortho(-radius, radius, -radius, radius, 0.f, depthRange) * view;
}
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
  const std::string vs = Src(kVS), fs = Src(kFS), gridVS = Src(kGridVS), gridFS = Src(kGridFS), skyFS = Src(kSkyFS),
                    shadowVS = Src(kShadowVS), shadowFS = Src(kShadowFS);
  PipelineDesc pd;
  pd.vertexSource = vs.c_str();
  pd.fragmentSource = fs.c_str();
  pd.attrs[0] = {0, 3, offsetof(Vertex3D, px)};
  pd.attrs[1] = {1, 3, offsetof(Vertex3D, nx)};
  pd.attrs[2] = {2, 2, offsetof(Vertex3D, u)};
  pd.attrs[3] = {3, 4, offsetof(Vertex3D, tx)};
  pd.attrs[4] = {4, 1, 0, 1}; // instance stream: object index
  pd.attrCount = 5;
  pd.stride = sizeof(Vertex3D);
  pd.instanceStride = sizeof(float);
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
  gp.vertexSource = gridVS.c_str();
  gp.fragmentSource = gridFS.c_str();
  gp.attrs[0] = {0, 3, 0};
  gp.attrCount = 1;
  gp.stride = 12;
  gp.alphaBlend = true;
  gp.depthTest = true;
  gp.depthWrite = false;
  gp.depthBiasConstant = -4.f; // pulled toward the camera, more on slopes: an MSAA depth copy keeps one sample
  gp.depthBiasSlope = -4.f;    // (off the pixel centre), which a fixed lift misses at grazing angles
  gp.debugName = "editor.grid";
  grid_ = rhi.CreatePipeline(gp);
  gp.vertexSource = kFullscreenVS;
  gp.fragmentSource = kOutlineFS;
  gp.depthTest = false;
  gp.depthBiasConstant = gp.depthBiasSlope = 0;
  gp.debugName = "editor.outline";
  outline_ = rhi.CreatePipeline(gp);
  gp.vertexSource = kSkyVS;
  gp.fragmentSource = skyFS.c_str();
  gp.alphaBlend = false;
  gp.depthTest = true; // only where nothing was drawn (depth still at the far plane)
  gp.depthWrite = false;
  gp.debugName = "sky";
  sky_ = rhi.CreatePipeline(gp);
  pd.vertexSource = shadowVS.c_str();
  pd.fragmentSource = shadowFS.c_str();
  pd.depthTest = true;
  pd.depthWrite = true;
  pd.alphaBlend = false;
  pd.cull = CullMode::None;      // thin and open meshes cast from both sides
  pd.depthBiasConstant = 2.f;    // with the normal offset in the lit pass: no acne, little peter-panning
  pd.depthBiasSlope = 2.5f;
  pd.debugName = "shadow";
  shadowPipe_ = rhi.CreatePipeline(pd);
  if (!mask_.Valid() || !grid_.Valid() || !outline_.Valid() || !sky_.Valid() || !shadowPipe_.Valid()) return false;
  {
    PipelineDesc wd;
    wd.vertexSource = vs.c_str();
    wd.fragmentSource = kWireFS;
    wd.attrs[0] = {0, 3, offsetof(Vertex3D, px)};
    wd.attrs[1] = {1, 3, offsetof(Vertex3D, nx)};
    wd.attrs[2] = {2, 2, offsetof(Vertex3D, u)};
    wd.attrs[3] = {3, 4, offsetof(Vertex3D, tx)};
    wd.attrs[4] = {4, 1, 0, 1};
    wd.attrCount = 5;
    wd.stride = sizeof(Vertex3D);
    wd.instanceStride = sizeof(float);
    wd.alphaBlend = true;
    wd.wireframe = true;
    wd.depthTest = true;
    wd.depthWrite = false;
    wd.depthBiasConstant = -2.f; // lines win over their own faces
    wd.depthBiasSlope = -1.f;
    wd.debugName = "editor.wireframe";
    wire_ = rhi.CreatePipeline(wd);
    if (!wire_.Valid()) return false;
  }
  for (TimerFrame& t : timers_)
    for (uint32_t& q : t.q) q = rhi.CreateQuery();
  lightsUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(LightsData), true);
  postUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(PostData), true);
  PipelineDesc pp; // fullscreen post passes
  pp.vertexSource = kPostVS;
  pp.attrs[0] = {0, 3, 0};
  pp.attrCount = 1;
  pp.stride = 12;
  pp.fragmentSource = kToneMapFS;
  pp.debugName = "post.tonemap";
  toneMap_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = kFxaaFS;
  pp.debugName = "post.fxaa";
  fxaa_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = kBloomDownFS;
  pp.debugName = "post.bloom.down";
  bloomDown_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = kBloomUpFS;
  pp.additive = true;
  pp.debugName = "post.bloom.up";
  bloomUp_ = rhi.CreatePipeline(pp);
  if (!toneMap_.Valid() || !fxaa_.Valid() || !bloomDown_.Valid() || !bloomUp_.Valid()) return false;
  noShadowRT_ = rhi.CreateDepthTarget(1);
  rhi.BeginRenderTarget(noShadowRT_, {0, 0, 0, 0});
  rhi.EndRenderTarget();
  const float quad[18] = {-1, 0, -1, -1, 0, 1, 1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0, -1};
  gridQuad_ = rhi.CreateBuffer(BufferUsage::Vertex, quad, sizeof(quad));
  const float tri[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
  fullscreen_ = rhi.CreateBuffer(BufferUsage::Vertex, tri, sizeof(tri));
  frameUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(FrameData), true);
  const uint8_t white[4] = {255, 255, 255, 255};
  white_ = rhi.CreateTexture2D(1, 1, TextureFormat::SRGBA8, white);

  return true;
}

void Renderer3D::Shutdown() {
  if (!rhi_) return;
  rhi_->DestroyBuffer(frameUbo_);
  rhi_->DestroyBuffer(objectBuffer_);
  rhi_->DestroyBuffer(instanceIds_);
  rhi_->DestroyTexture(white_);
  for (PipelineHandle p : {opaque_, twoSided_, blend_, grid_, mask_, outline_, sky_, shadowPipe_, toneMap_, fxaa_, bloomDown_, bloomUp_, wire_})
    rhi_->DestroyPipeline(p);
  for (TimerFrame& t : timers_)
    for (uint32_t& q : t.q) rhi_->DestroyQuery(q);
  rhi_->DestroyBuffer(postUbo_);
  for (RenderTargetHandle t : bloom_) rhi_->DestroyRenderTarget(t);
  rhi_->DestroyRenderTarget(hdrRT_);
  rhi_->DestroyRenderTarget(ldrRT_);
  rhi_->DestroyBuffer(lightsUbo_);
  rhi_->DestroyRenderTarget(shadowRT_);
  rhi_->DestroyRenderTarget(noShadowRT_);
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
  for (size_t i = 0; i < d.vertices.size(); ++i) {
    glm::vec3 p(d.vertices[i].px, d.vertices[i].py, d.vertices[i].pz);
    m.boundsMin = i ? glm::min(m.boundsMin, p) : p;
    m.boundsMax = i ? glm::max(m.boundsMax, p) : p;
  }
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

void Renderer3D::Render(RenderTargetHandle rt, int32_t w, int32_t h, const Camera3D& camIn, const Light3D& light,
                        const std::vector<DrawItem3D>& items, Color clear, const RenderExtras* extras, const QualitySettings* quality) {
  if (!rhi_ || !rt.Valid() || w <= 0 || h <= 0) return;
  QualitySettings q = quality ? *quality : ReferenceQuality();
  const DebugView view = extras ? extras->view : DebugView::Lit;
  if (view != DebugView::Lit) { // show the buffer itself: per-pixel shading, no tone curve or post effects
    q.lighting = LightingMode::Pixel;
    q.toneMap = q.bloom = q.fxaa = false;
    q.exposure = 1;
  }
  lastQuality_ = q;
  CollectTimers();
  stats_.shadowCasters = 0;
  TimerFrame& timer = timers_[timerFrame_];
  timerFrame_ = (timerFrame_ + 1) % kTimerFrames;
  bool timing = !timer.pending; // this slot's previous queries were read (or never used)
  if (timing) { timer.marks = 0; rhi_->Timestamp(timer.q[timer.marks++]); }
  Camera3D cam = camIn;
  cam.farZ *= q.viewDistance;
  FrameData f{};
  const float aspect = (float)w / (float)h;
  glm::mat4 vp = cam.Projection(aspect) * cam.View(), ivp = glm::inverse(vp);
  std::memcpy(f.viewProj, glm::value_ptr(vp), sizeof(f.viewProj));
  std::memcpy(f.invViewProj, glm::value_ptr(ivp), sizeof(f.invViewProj));
  Put(f.camPos, cam.position);
  Put(f.toLight, light.toLight);
  Put(f.light, light.color, light.intensity);
  const SkyBackground3D& bg = light.background;
  if (bg.draw) { // the sky gives the ambient light and the reflections
    Put(f.zenith, bg.zenith, bg.intensity);
    Put(f.horizon, bg.horizon, bg.intensity);
    Put(f.ground, bg.ground, bg.intensity);
    f.sun[0] = bg.sunSize > 0 ? std::cos(glm::radians(bg.sunSize * 0.5f)) : 1.f;
  } else { // no sky: the plain two-colour hemisphere
    Put(f.zenith, light.sky);
    Put(f.horizon, light.sky);
    Put(f.ground, light.ground);
    f.sun[0] = 1.f;
  }
  Put(f.sky, light.sky);
  int locals = std::min({(int)light.locals.size(), kMaxLocalLights, q.lighting == LightingMode::Unlit ? 0 : q.maxLights});
  f.counts[0] = (float)locals;
  f.counts[1] = (float)q.lighting; // 0 unlit, 1 per vertex, 2 per pixel
  f.counts[2] = q.textureBias;
  f.counts[3] = (float)view;
  LightsData ld{};
  for (int i = 0; i < locals; ++i) {
    const LocalLight3D& l = light.locals[i];
    Put(ld.pos[i], l.position, l.range);
    Put(ld.color[i], l.color, l.intensity);
    Put(ld.dir[i], glm::normalize(l.direction), std::cos(glm::radians(std::max(l.outerAngle, 0.1f) * 0.5f)));
    ld.cone[i][0] = std::cos(glm::radians(std::clamp(l.innerAngle, 0.f, l.outerAngle - 0.01f) * 0.5f));
    ld.cone[i][1] = l.spot ? 1.f : 0.f;
  }
  rhi_->UpdateBuffer(lightsUbo_, &ld, sizeof(ld));
  float texelWorld = 0, depthRange = 1;
  if (light.shadows && q.shadows && q.lighting == LightingMode::Pixel && light.intensity > 0) {
    shadowMatrix_ = FitShadow(cam, aspect, light.toLight, light.shadowDistance * q.viewDistance, q.shadowMapSize, texelWorld, depthRange);
    std::memcpy(f.lightViewProj, glm::value_ptr(shadowMatrix_), sizeof(f.lightViewProj));
    f.shadow[0] = 1.f;
    f.shadow[1] = 0.02f / depthRange;   // 2 cm in depth
    f.shadow[2] = texelWorld * 1.5f;    // normal offset
    f.shadow[3] = (float)q.shadowMapSize;
  }
  rhi_->UpdateBuffer(frameUbo_, &f, sizeof(f));

  // Draw lists (queue 3-9, 3-10). Object constants: one storage buffer element per item, in item order. Each pass
  // lists the objects it draws (inside its frustum: the camera's, or the light's for the shadow map), sorted so alike
  // objects (same pipeline, mesh and textures) sit together; each run of alike objects is one instanced draw whose
  // instances read their object index from a per-frame index stream.
  auto pipeOf = [&](const Material3D& m) { return m.alpha == Material3D::Alpha::Blend ? blend_ : m.doubleSided ? twoSided_ : opaque_; };
  auto runKey = [&](size_t i) {
    const Material3D& m = items[i].material;
    return std::make_tuple(pipeOf(m).id, (uintptr_t)items[i].mesh, m.baseMap.id, m.metalRoughMap.id, m.normalMap.id, m.occlusionMap.id,
                           m.emissiveMap.id);
  };
  auto shadowKey = [&](size_t i) { // depth only: the base map matters for alpha-masked holes alone
    const Material3D& m = items[i].material;
    bool masked = m.alpha == Material3D::Alpha::Mask;
    return std::make_tuple((uint32_t)0, (uintptr_t)items[i].mesh, masked ? m.baseMap.id : 0u, (uint32_t)0, (uint32_t)0, (uint32_t)0,
                           (uint32_t)0); // same shape as runKey
  };
  std::vector<glm::vec3> worldLo(items.size()), worldHi(items.size());
  std::vector<bool> drawable(items.size(), false);
  for (size_t i = 0; i < items.size(); ++i) {
    const Mesh3D* m = items[i].mesh;
    if (!m || !m->indexCount) continue;
    drawable[i] = true;
    TransformBounds(items[i].model, m->boundsMin, m->boundsMax, worldLo[i], worldHi[i]);
  }
  auto inside = [&](const Frustum& fr, size_t i) {
    const Mesh3D* m = items[i].mesh;
    return !q.culling || m->boundsMin == m->boundsMax || fr.Intersects(worldLo[i], worldHi[i]);
  };
  const Frustum camFrustum = Frustum::FromMatrix(vp);
  std::vector<float> indexStream;
  struct Run { uint32_t first, count; size_t item; };
  using Key = decltype(runKey(0));
  auto buildRuns = [&](const std::vector<size_t>& list, const std::function<Key(size_t)>& key, bool group) {
    std::vector<Run> out;
    for (size_t i : list) {
      if (group && q.instancing && !out.empty() && key(out.back().item) == key(i)) ++out.back().count;
      else out.push_back({(uint32_t)indexStream.size(), 1, i});
      indexStream.push_back((float)i);
    }
    return out;
  };
  // camera pass: opaque runs, then blended objects back to front
  std::vector<size_t> opaqueList, blendList;
  size_t culled = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    if (!drawable[i]) continue;
    if (!inside(camFrustum, i)) { ++culled; continue; }
    (items[i].material.alpha == Material3D::Alpha::Blend ? blendList : opaqueList).push_back(i);
  }
  std::stable_sort(opaqueList.begin(), opaqueList.end(), [&](size_t x, size_t y) { return runKey(x) < runKey(y); });
  auto dist = [&](size_t i) { return glm::distance(glm::vec3(items[i].model[3]), cam.position); };
  std::sort(blendList.begin(), blendList.end(), [&](size_t x, size_t y) { return dist(x) > dist(y); });
  std::vector<Run> runs = buildRuns(opaqueList, runKey, true);
  const size_t opaqueRuns = runs.size();
  for (const Run& r : buildRuns(blendList, runKey, false)) runs.push_back(r);
  // shadow pass: opaque and masked objects inside the light's frustum
  std::vector<Run> shadowRuns;
  size_t shadowCulled = 0;
  if (f.shadow[0] > 0.5f) {
    const Frustum lightFrustum = Frustum::FromMatrix(shadowMatrix_);
    std::vector<size_t> casters;
    for (size_t i = 0; i < items.size(); ++i) {
      if (!drawable[i] || items[i].material.alpha == Material3D::Alpha::Blend) continue;
      if (inside(lightFrustum, i)) casters.push_back(i);
      else ++shadowCulled;
    }
    std::stable_sort(casters.begin(), casters.end(), [&](size_t x, size_t y) { return shadowKey(x) < shadowKey(y); });
    shadowRuns = buildRuns(casters, shadowKey, true);
  }
  // selection mask: each outlined object on its own
  std::vector<Run> maskRuns;
  if (extras)
    for (size_t i : extras->outlined)
      if (i < items.size() && drawable[i] && inside(camFrustum, i)) maskRuns.push_back(buildRuns({i}, runKey, false)[0]);
  if (items.size() > objectCapacity_) {
    rhi_->DestroyBuffer(objectBuffer_);
    objectCapacity_ = std::max<size_t>(items.size(), 256) * 2;
    objectBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, objectCapacity_ * sizeof(ObjectData), true);
  }
  if (indexStream.size() > indexCapacity_) {
    rhi_->DestroyBuffer(instanceIds_);
    indexCapacity_ = std::max<size_t>(indexStream.size(), 1024) * 2;
    instanceIds_ = rhi_->CreateBuffer(BufferUsage::Vertex, nullptr, indexCapacity_ * sizeof(float), true);
  }
  if (!indexStream.empty()) rhi_->UpdateBuffer(instanceIds_, indexStream.data(), indexStream.size() * sizeof(float));
  std::vector<ObjectData> objects(items.size());
  for (size_t k = 0; k < items.size(); ++k) {
    if (!drawable[k]) continue;
    const DrawItem3D& it = items[k];
    ObjectData& o = objects[k];
    std::memcpy(o.model, glm::value_ptr(it.model), sizeof(o.model));
    const Material3D& m = it.material;
    o.baseColor[0] = m.baseColor.r; o.baseColor[1] = m.baseColor.g; o.baseColor[2] = m.baseColor.b; o.baseColor[3] = m.baseColor.a;
    o.params[0] = m.metallic; o.params[1] = m.roughness; o.params[2] = m.unlit ? 1.f : 0.f; o.params[3] = (float)m.alpha;
    o.emissive[0] = m.emissive.r; o.emissive[1] = m.emissive.g; o.emissive[2] = m.emissive.b; o.emissive[3] = m.normalScale;
    auto has = [](TextureHandle t) { return t.Valid() ? 1.f : 0.f; };
    o.maps[0] = has(m.baseMap); o.maps[1] = has(m.metalRoughMap); o.maps[2] = has(m.normalMap); o.maps[3] = has(m.occlusionMap);
    o.extra[0] = has(m.emissiveMap); o.extra[1] = m.occlusionStrength; o.extra[2] = m.alphaCutoff;
  }
  if (!objects.empty()) rhi_->UpdateBuffer(objectBuffer_, objects.data(), objects.size() * sizeof(ObjectData));
  rhi_->BindStorageBuffer(0, objectBuffer_);
  auto drawRun = [&](PipelineHandle p, const Run& r) {
    const Mesh3D& mesh = *items[r.item].mesh;
    rhi_->DrawIndexedInstanced(p, mesh.vertices, mesh.indices, mesh.indexCount, r.count, r.first, instanceIds_);
    ++draws_;
  };
  auto bindMaps = [&](const Material3D& m) {
    const TextureHandle maps[5] = {m.baseMap, m.metalRoughMap, m.normalMap, m.occlusionMap, m.emissiveMap};
    for (uint32_t slot = 0; slot < 5; ++slot) rhi_->BindTexture(slot, maps[slot].Valid() ? maps[slot] : white_);
  };

  rhi_->BindUniformBuffer(1, frameUbo_);
  rhi_->BindUniformBuffer(3, lightsUbo_);
  if (f.shadow[0] > 0.5f) { // shadow map: every opaque and masked item, depth only
    if (!shadowRT_.Valid() || shadowSize_ != q.shadowMapSize) {
      rhi_->DestroyRenderTarget(shadowRT_);
      shadowRT_ = rhi_->CreateDepthTarget(q.shadowMapSize);
      shadowSize_ = q.shadowMapSize;
    }
    rhi_->BeginRenderTarget(shadowRT_, {0, 0, 0, 0});
    for (const Run& r : shadowRuns) { // alike casters in one draw too
      const Material3D& m = items[r.item].material;
      rhi_->BindTexture(0, m.baseMap.Valid() ? m.baseMap : white_);
      drawRun(shadowPipe_, r);
      stats_.shadowCasters += r.count;
    }
    rhi_->EndRenderTarget();
    if (timing) Mark(timer, "shadow");
  }
  rhi_->BindTexture(5, rhi_->RenderTargetDepth(f.shadow[0] > 0.5f ? shadowRT_ : noShadowRT_));
  stats_ = RenderStats{w, h, std::clamp(q.msaa, 1, 8), f.shadow[0] > 0.5f ? q.shadowMapSize : 0, locals, stats_.shadowCasters, 0, 0,
                       q.toneMap, q.fxaa, q.bloom, view, stats_.passes};
  bool outline = extras && !extras->outlined.empty();
  if (outline) { // selection mask first (its own target, no depth: the outline shows through other objects)
    if (!maskRT_.Valid() || maskW_ != w || maskH_ != h) {
      rhi_->DestroyRenderTarget(maskRT_);
      maskRT_ = rhi_->CreateRenderTarget(w, h, 1);
      maskW_ = w;
      maskH_ = h;
    }
    rhi_->BeginRenderTarget(maskRT_, {0, 0, 0, 0});
    for (const Run& r : maskRuns) drawRun(mask_, r);
    rhi_->EndRenderTarget();
  }
  const int samples = std::clamp(q.msaa, 1, 8);
  if (!hdrRT_.Valid() || hdrW_ != w || hdrH_ != h || hdrSamples_ != samples) {
    rhi_->DestroyRenderTarget(hdrRT_);
    rhi_->DestroyRenderTarget(ldrRT_);
    hdrRT_ = rhi_->CreateRenderTargetEx(w, h, samples, true, true);
    ldrRT_ = rhi_->CreateRenderTargetEx(w, h, 1, false, false);
    hdrW_ = w;
    hdrH_ = h;
    hdrSamples_ = samples;
  }
  Color linearClear = SrgbToLinear((uint8_t)std::lround(std::clamp(clear.r, 0.f, 1.f) * 255),
                                   (uint8_t)std::lround(std::clamp(clear.g, 0.f, 1.f) * 255),
                                   (uint8_t)std::lround(std::clamp(clear.b, 0.f, 1.f) * 255));
  rhi_->BeginRenderTarget(hdrRT_, linearClear);
  uint32_t sceneDraws = 0;
  for (size_t r = 0; r <= runs.size(); ++r) {
    if (r == opaqueRuns && bg.draw) { // sky where no surface was drawn
      rhi_->Draw(sky_, fullscreen_, 3);
      ++draws_;
    }
    if (r == runs.size()) break;
    const Material3D& m = items[runs[r].item].material;
    bindMaps(m);
    drawRun(pipeOf(m), runs[r]);
    ++sceneDraws;
    triangles_ += (uint64_t)(items[runs[r].item].mesh->indexCount / 3) * runs[r].count;
  }
  stats_.sceneDraws = sceneDraws;
  stats_.objects = opaqueList.size() + blendList.size();
  stats_.culled = culled;
  stats_.shadowCulled = shadowCulled;
  rhi_->EndRenderTarget(); // resolves MSAA
  if (timing) Mark(timer, "scene");

  // Bloom: blur the bright parts over a chain of half-size targets.
  TextureHandle hdr = rhi_->RenderTargetColor(hdrRT_);
  if (q.bloom) {
    int bw = std::max(w / 2, 1), bh = std::max(h / 2, 1);
    if (!bloom_[0].Valid() || bloomW_ != bw || bloomH_ != bh) {
      for (int l = 0; l < kBloomLevels; ++l) {
        rhi_->DestroyRenderTarget(bloom_[l]);
        bloom_[l] = rhi_->CreateRenderTargetEx(std::max(bw >> l, 1), std::max(bh >> l, 1), 1, true, false);
      }
      bloomW_ = bw;
      bloomH_ = bh;
    }
    for (int l = 0; l < kBloomLevels; ++l) {
      rhi_->BeginRenderTarget(bloom_[l], {0, 0, 0, 1});
      rhi_->BindTexture(0, l == 0 ? hdr : rhi_->RenderTargetColor(bloom_[l - 1]));
      rhi_->Draw(bloomDown_, fullscreen_, 3);
      rhi_->EndRenderTarget();
      ++draws_;
    }
    for (int l = kBloomLevels - 1; l > 0; --l) {
      rhi_->BeginRenderTargetKeep(bloom_[l - 1]);
      rhi_->BindTexture(0, rhi_->RenderTargetColor(bloom_[l]));
      rhi_->Draw(bloomUp_, fullscreen_, 3);
      rhi_->EndRenderTarget();
      ++draws_;
    }
    if (timing) Mark(timer, "bloom");
  }
  PostData pd{{q.exposure, q.bloomIntensity, q.toneMap ? 1.f : 0.f, q.bloom ? 1.f : 0.f}};
  rhi_->UpdateBuffer(postUbo_, &pd, sizeof(pd));
  rhi_->BindUniformBuffer(4, postUbo_);
  rhi_->BindTexture(0, hdr);
  rhi_->BindTexture(1, q.bloom ? rhi_->RenderTargetColor(bloom_[0]) : white_);
  if (q.fxaa) { // tone map into the LDR copy, then FXAA into the final picture
    rhi_->BeginRenderTarget(ldrRT_, {0, 0, 0, 1});
    rhi_->Draw(toneMap_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    rhi_->BeginRenderTarget(rt, {0, 0, 0, 1});
    rhi_->BindTexture(0, rhi_->RenderTargetColor(ldrRT_));
    rhi_->Draw(fxaa_, fullscreen_, 3);
    draws_ += 2;
  } else {
    rhi_->BeginRenderTarget(rt, {0, 0, 0, 1});
    rhi_->Draw(toneMap_, fullscreen_, 3);
    ++draws_;
  }
  if (timing) Mark(timer, q.fxaa ? "tonemap+fxaa" : "tonemap");
  // Editor overlays on the finished picture: the grid hides behind objects (scene depth copied over).
  rhi_->CopyDepth(hdrRT_, rt);
  if (extras && extras->wireframe)
    for (const Run& r : runs) drawRun(wire_, r);
  if (extras && extras->grid) {
    rhi_->Draw(grid_, gridQuad_, 6);
    ++draws_;
  }
  if (outline) {
    rhi_->BindTexture(0, rhi_->RenderTargetColor(maskRT_));
    rhi_->Draw(outline_, fullscreen_, 3);
    ++draws_;
  }
  rhi_->EndRenderTarget();
  if (timing) {
    Mark(timer, "overlays");
    timer.pending = true;
  }
  stats_.draws = draws_;
  stats_.triangles = triangles_;
}

void Renderer3D::Mark(TimerFrame& t, const char* passEnded) {
  if (t.marks >= kMarks) return;
  t.names[t.marks] = passEnded;
  rhi_->Timestamp(t.q[t.marks++]);
}

void Renderer3D::CollectTimers() {
  // The oldest pending frame first; stop at the first one the GPU has not finished.
  for (int k = 1; k <= kTimerFrames; ++k) {
    TimerFrame& t = timers_[(timerFrame_ + k) % kTimerFrames];
    if (!t.pending) continue;
    std::vector<uint64_t> ns(t.marks);
    bool ready = true;
    for (int i = 0; i < t.marks && ready; ++i) ready = rhi_->QueryNs(t.q[i], ns[i]);
    if (!ready) break;
    std::vector<RenderStats::Pass> passes;
    for (int i = 1; i < t.marks; ++i) passes.push_back({t.names[i], (double)(ns[i] - ns[i - 1]) / 1e6});
    stats_.passes = std::move(passes);
    t.pending = false;
  }
}

const char* DebugViewName(DebugView v) {
  switch (v) {
    case DebugView::Lit: return "Lit";
    case DebugView::Albedo: return "Albedo";
    case DebugView::Normal: return "Normal";
    case DebugView::Roughness: return "Roughness";
    case DebugView::Metallic: return "Metallic";
    case DebugView::Depth: return "Depth";
    case DebugView::Shadow: return "Shadow";
  }
  return "Lit";
}

std::string Renderer3D::DescribePasses() const {
  std::string out;
  double total = 0;
  for (const RenderStats::Pass& p : stats_.passes) {
    out += std::format("{}{} {:.2f} ms", out.empty() ? "" : " | ", p.name, p.ms);
    total += p.ms;
  }
  return out.empty() ? "(no GPU timings yet)" : out + std::format(" | total {:.2f} ms", total);
}

std::string Renderer3D::DescribeStats() const {
  const RenderStats& s = stats_;
  return std::format("{}x{} msaa={} view={} draws={} scene_draws={} objects={} culled={} shadow_culled={} tris={} lights={} shadow_map={} casters={} tonemap={} fxaa={} bloom={}", s.w, s.h,
                     s.msaa, DebugViewName(s.view), s.draws, s.sceneDraws, s.objects, s.culled, s.shadowCulled, s.triangles, s.localLights, s.shadowMap, s.shadowCasters, s.toneMap ? 1 : 0,
                     s.fxaa ? 1 : 0, s.bloom ? 1 : 0);
}
}
