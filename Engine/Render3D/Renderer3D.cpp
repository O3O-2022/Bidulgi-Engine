#include "Render3D/Renderer3D.h"
#include "Assets/GltfImport.h"
#include "Render3D/Culling.h"
#include "Render3D/Lightmap.h"
#include "Render3D/RayScene.h"
#include <chrono>
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
  vec4 uCamFwd;                                  // camera forward (cascade choice by view depth)
  mat4 uCascadeVP[4]; vec4 uCascadeSplit;        // cascaded shadows (3-5b): matrices, far view depth of each
  vec4 uCascadeInfo[4];                          // per cascade: depth bias, normal offset, texels per metre, -
  vec4 uShadow2;                                 // cascades, softness (m), blend band (share of a split), -
  vec4 uScreen;                                  // target w, h, 1 / w, 1 / h
  vec4 uFlags;                                   // x: SSAO on (3-5b)
  vec4 uCluster;                                 // tiles x, y, depth slices, per-pixel lights (Forward+, 3-5b)
  vec4 uCluster2;                                // near, slices / log(far / near), clustered on, -
  vec4 uFog;                                     // fog colour (linear), density per metre (0 = off) (2-6)
  vec4 uFog2;                                    // fog start (m), -, -, -
  vec4 uLocalShadow;                             // point / spot shadow atlas (3-4b): tiles across, tiles up, tile size (px), -
  mat4 uPrevViewProj;                            // the previous frame's camera (round 6: SSR reads that frame's colour)
  vec4 uSsr;                                     // SSR (round 6): on (previous frame there), max distance m, thickness m, steps
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
const char* kObjectsCommon = R"(struct Object3D { mat4 model; vec4 baseColor; vec4 params; vec4 emissive; vec4 maps; vec4 extra; vec4 lightmap; };
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
// Reflection probes (GI Tier 0): boxes, positions (w = intensity), SH9 irradiance / pi per probe. Probe 0 = sky.
// Probe volume (GI Tier 0-B): box min (w = 1 when baked), box max, probe counts (w = intensity); SH9 per grid probe.
const char* kProbesFn = R"(layout(std140, binding = 5) uniform Probes3D {
  vec4 uProbeMin[8]; vec4 uProbeMax[8]; vec4 uProbePos[8]; vec4 uSH[72]; vec4 uProbeInfo; // info.x = last mip level
  vec4 uVolMin; vec4 uVolMax; vec4 uVolCount;
};
layout(std430, binding = 1) readonly buffer Volume3D { vec4 uVolSH[]; };
void SHBasis(vec3 n, out float b[9]) {
  b[0] = 0.282095; b[1] = 0.488603 * n.y; b[2] = 0.488603 * n.z; b[3] = 0.488603 * n.x;
  b[4] = 1.092548 * n.x * n.y; b[5] = 1.092548 * n.y * n.z; b[6] = 0.315392 * (3.0 * n.z * n.z - 1.0);
  b[7] = 1.092548 * n.x * n.z; b[8] = 0.546274 * (n.x * n.x - n.y * n.y);
}
vec3 ProbeIrradiance(int p, vec3 n) { // ambient light from all directions around n (already divided by pi)
  float b[9];
  SHBasis(n, b);
  vec3 e = vec3(0.0);
  for (int k = 0; k < 9; ++k) e += uSH[p * 9 + k].rgb * b[k];
  return max(e, vec3(0.0)) * uProbePos[p].w;
}
bool InVolume(vec3 p) {
  return uVolMin.w > 0.5 && all(greaterThanEqual(p, uVolMin.xyz - 0.02)) && all(lessThanEqual(p, uVolMax.xyz + 0.02));
}
vec3 VolumeIrradiance(vec3 p, vec3 n) { // the 8 nearest grid probes, blended by distance (probes at cell centres)
  ivec3 cnt = ivec3(uVolCount.xyz + 0.5);
  vec3 g = clamp((p - uVolMin.xyz) / (uVolMax.xyz - uVolMin.xyz) * vec3(cnt) - 0.5, vec3(0.0), vec3(cnt - 1));
  ivec3 i0 = min(ivec3(floor(g)), cnt - 1), i1 = min(i0 + 1, cnt - 1);
  vec3 f = g - vec3(i0);
  float b[9];
  SHBasis(n, b);
  vec3 e = vec3(0.0);
  float wsum = 0.0;
  for (int c = 0; c < 8; ++c) {
    bvec3 hi = bvec3((c & 1) != 0, (c & 2) != 0, (c & 4) != 0);
    ivec3 q = ivec3(hi.x ? i1.x : i0.x, hi.y ? i1.y : i0.y, hi.z ? i1.z : i0.z);
    vec3 w3 = mix(1.0 - f, f, vec3(hi));
    int base = ((q.z * cnt.y + q.y) * cnt.x + q.x) * 9;
    vec3 s = vec3(0.0);
    for (int k = 0; k < 9; ++k) s += uVolSH[base + k].rgb * b[k];
    float w = w3.x * w3.y * w3.z * max(uVolSH[base].w, 1e-4); // probes inside objects are switched off (w = 0)
    e += s * w;
    wsum += w;
  }
  return max(e / max(wsum, 1e-8), vec3(0.0)) * uVolCount.w;
}
)";
// Direction of a cube face texel: st in 0..1 with t = 0 the first row (OpenGL cube map face layout).
const char* kFaceDir = R"(vec3 FaceDir(int f, vec2 st) {
  vec2 c = st * 2.0 - 1.0;
  if (f == 0) return vec3(1.0, -c.y, -c.x);
  if (f == 1) return vec3(-1.0, -c.y, c.x);
  if (f == 2) return vec3(c.x, 1.0, c.y);
  if (f == 3) return vec3(c.x, -1.0, -c.y);
  if (f == 4) return vec3(c.x, -c.y, 1.0);
  return vec3(-c.x, -c.y, -1.0);
}
)";
const char* kSsrFn = R"(// Screen-space reflection (round 6): march the reflected ray in world space, projecting each step onto this frame's
// depth (the prepass); a hit is a step just behind the visible surface (within a thickness growing with distance),
// refined by bisection. Its colour comes from the previous frame's picture, reprojected with that frame's camera.
// Weight fades with roughness, at the picture's edges, with distance and for rays towards the camera; what is left
// is the probe / sky reflection (fallback).
vec4 TraceSSR(vec3 P, vec3 N, vec3 R, float rough) {
  if (uSsr.x < 0.5 || rough > 0.6) return vec4(0.0);
  int steps = int(uSsr.w);
  float maxD = uSsr.y, stride = maxD / float(steps);
  float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  vec3 start = P + N * 0.03;
  float prev = 0.0, hitT = -1.0;
  for (int i = 1; i <= steps; ++i) {
    float t = stride * (float(i) - 0.5 + noise * 0.5);
    vec3 p = start + R * t;
    vec4 c = uViewProj * vec4(p, 1.0);
    if (c.w <= 0.0) break;
    vec2 uv = c.xy / c.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
    float behind = dot(p - uCamPos.xyz, uCamFwd.xyz) - textureLod(uGeoSSR, uv, 0.0).w;
    if (behind > 0.0 && behind < uSsr.z + t * 0.02) { hitT = t; break; }
    prev = t;
  }
  if (hitT < 0.0) return vec4(0.0);
  float lo = prev, hi = hitT; // bisection between the last step in front and the hit
  for (int k = 0; k < 5; ++k) {
    float mid = 0.5 * (lo + hi);
    vec3 p = start + R * mid;
    vec4 c = uViewProj * vec4(p, 1.0);
    float behind = dot(p - uCamPos.xyz, uCamFwd.xyz) - textureLod(uGeoSSR, c.xy / c.w * 0.5 + 0.5, 0.0).w;
    if (behind > 0.0) hi = mid; else lo = mid;
  }
  vec3 hit = start + R * hi;
  vec4 pc = uPrevViewProj * vec4(hit, 1.0);
  if (pc.w <= 0.0) return vec4(0.0);
  vec2 puv = pc.xy / pc.w * 0.5 + 0.5;
  vec2 edge = min(puv, 1.0 - puv);
  float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y));
  fade *= 1.0 - smoothstep(0.6 * maxD, maxD, hi);
  fade *= 1.0 - smoothstep(0.25, 0.6, rough);
  fade *= smoothstep(-0.6, -0.1, -dot(R, -uCamFwd.xyz)); // rays coming back to the camera find little
  return vec4(textureLod(uPrevColor, puv, 0.0).rgb, clamp(fade, 0.0, 1.0));
}
)";
const char* kFogCommon = R"(
float SliceDepth(float z) { return uFogA.x * pow(uFogA.y / uFogA.x, z / uGrid.z); }
vec3 RayAt(vec2 uv) {
  vec4 p = uFogInvVP * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  return normalize(p.xyz / p.w - uCamPos.xyz);
}
)";
std::string Src(const char* text) { // expands #FRAME_UBO, #SKY_FN, #OBJECTS_VS, #OBJECTS_FS, #PROBES_FN, #FACE_DIR
  std::string s = text;
  const std::string vsObjects = std::string(kObjectsCommon) + kObjectsVS, fsObjects = std::string(kObjectsCommon) + kObjectsFS;
  for (auto [token, body] : {std::pair{"#FRAME_UBO", kFrameUBO}, std::pair{"#SKY_FN", kSkyFn}, std::pair{"#OBJECTS_VS", vsObjects.c_str()},
                             std::pair{"#OBJECTS_FS", fsObjects.c_str()}, std::pair{"#PROBES_FN", kProbesFn}, std::pair{"#FACE_DIR", kFaceDir}, std::pair{"#SSR_FN", kSsrFn}, std::pair{"#FOG_COMMON", kFogCommon}})
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
layout(location = 5) in vec2 aLightUV; // lightmap atlas coordinates (GI Tier 0-C)
layout(std140, binding = 3) uniform Lights3D { vec4 uLPos[16]; vec4 uLColor[16]; vec4 uLDir[16]; vec4 uLCone[16]; };
layout(binding = 7) uniform sampler2D uLightmap;
#PROBES_FN
out vec3 vWorld; out vec3 vNormal; out vec2 vUV; out vec4 vTangent; out vec3 vLit; out vec2 vLightUV;
void main() {
  vObject = int(aObject + 0.5);
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = transpose(inverse(mat3(uModel))) * aNormal;
  vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
  vUV = aUV;
  vLightUV = aLightUV;
  vLit = vec3(0.0);
  if (uCounts.y > 0.5 && uCounts.y < 1.5) { // low tier: diffuse light per vertex (sun, sky, local lights)
    vec3 n = normalize(vNormal);
    vLit = uLight.rgb * max(dot(n, normalize(uToLight.xyz)), 0.0) / 3.14159265 +
           (uObjects[int(aObject + 0.5)].lightmap.x > 0.5 ? textureLod(uLightmap, aLightUV, 0.0).rgb
                                                          : ProbeIrradiance(int(uObjects[int(aObject + 0.5)].extra.w + 0.5), n));
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
layout(binding = 7) uniform sampler2D uLightmap;
layout(binding = 8) uniform sampler2D uSSAO; // screen-space ambient occlusion, half resolution (3-5b)
layout(binding = 10) uniform sampler2D uGeoSSR;   // SSR (round 6): this frame's prepass (normal, view depth)
layout(binding = 11) uniform sampler2D uPrevColor; // SSR: the previous frame's resolved HDR picture
#SSR_FN
layout(std430, binding = 2) readonly buffer LightList3D { vec4 uLL[]; }; // per light: pos + range, colour, dir + cos outer, cos inner + spot
layout(std430, binding = 3) readonly buffer Clusters3D { uint uCl[]; };  // per cluster: first, count; then light indices
in vec3 vWorld; in vec3 vNormal; in vec2 vUV; in vec4 vTangent; in vec3 vLit; in vec2 vLightUV;
out vec4 oColor;
const float PI = 3.14159265;
vec3 Fog(vec3 c) { // distance fog (World Settings, 2-6)
  if (uFog.w <= 0.0) return c;
  float d = max(length(vWorld - uCamPos.xyz) - uFog2.x, 0.0);
  return mix(uFog.rgb, c, exp(-d * uFog.w));
}
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
layout(binding = 6) uniform samplerCubeArray uEnv;
layout(binding = 9) uniform sampler2DShadow uLocalShadowMap;                  // point / spot shadow atlas (3-4b)
layout(std430, binding = 4) readonly buffer LocalShadow3D { mat4 uLSM[]; }; // light-space matrix per atlas tile
#SKY_FN
#PROBES_FN
vec3 ProbeSpecular(int p, vec3 R, vec3 world, float rough) { // prefiltered environment, box-projected in rooms
  vec3 dir = R;
  if (uProbeMin[p].w > 0.5) {
    vec3 r = mix(R, vec3(1e-6), lessThan(abs(R), vec3(1e-6))); // no 0/0 for a surface on the box with R along it
    vec3 t1 = (uProbeMax[p].xyz - world) / r, t2 = (uProbeMin[p].xyz - world) / r, tf = max(t1, t2);
    float t = min(min(tf.x, tf.y), tf.z);
    if (t > 0.0) dir = world + R * t - uProbePos[p].xyz;
  }
  return textureLod(uEnv, vec4(dir, float(p)), rough * uProbeInfo.x).rgb * uProbePos[p].w;
}
vec3 Brdf(vec3 N, vec3 V, vec3 L, vec3 diffuse, vec3 f0, float a) {
  vec3 H = normalize(V + L);
  float NoL = max(dot(N, L), 0.0), NoV = max(dot(N, V), 1e-4), NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
  vec3 F = F_Schlick(VoH, f0);
  return (diffuse / PI * (1.0 - F) + D_GGX(NoH, a) * V_SmithGGX(NoV, NoL, a) * F) * NoL;
}
vec3 Irradiance(vec3 n) { // light from the whole sky hemisphere above n (ground below)
  return mix(uGround.rgb, mix(uHorizon.rgb, uZenith.rgb, 0.6), clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
}
// Cascaded shadow map (3-5b): the cascades sit side by side in one depth atlas. Taps stay inside their tile.
const vec2 kPoisson[12] = vec2[](vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
                                 vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
                                 vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));
float ShadowTap(int c, vec2 uv, float depth) {
  float tiles = uShadow2.x, t = 1.0 / uShadow.w;
  uv = clamp(uv, vec2(t * 0.5), vec2(1.0 - t * 0.5));
  return textureLod(uShadowMap, vec3((float(c) + uv.x) / tiles, uv.y, depth), 0.0);
}
float CascadeShadow(int c, vec3 world, vec3 n, bool last) {
  vec4 p = uCascadeVP[c] * vec4(world + n * uCascadeInfo[c].y, 1.0); // normal offset against acne
  vec3 s = p.xyz / p.w * 0.5 + 0.5;
  if (s.z >= 1.0) return 1.0;
  float t = 1.0 / uShadow.w, depth = s.z - uCascadeInfo[c].x, sum = 0.0;
  if (uShadow2.y <= 0.0) { // 3x3 taps of 2x2 hardware PCF
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x) sum += ShadowTap(c, s.xy + vec2(x, y) * t, depth);
    sum /= 9.0;
  } else { // soft: 12 Poisson taps, turned per pixel, radius in metres -> texels of this cascade
    float a = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));
    float r = clamp(uShadow2.y * uCascadeInfo[c].z, 1.0, 8.0) * t;
    for (int i = 0; i < 12; ++i) sum += ShadowTap(c, s.xy + rot * kPoisson[i] * r, depth);
    sum /= 12.0;
  }
  if (!last) return sum;
  float edge = smoothstep(0.85, 1.0, max(abs(s.x * 2.0 - 1.0), abs(s.y * 2.0 - 1.0))); // fade at the last map's edge
  return mix(sum, 1.0, edge);
}
float CascadeSplit(int c) { return c == 0 ? uCascadeSplit.x : c == 1 ? uCascadeSplit.y : c == 2 ? uCascadeSplit.z : uCascadeSplit.w; }
int ShadowCascade(vec3 world) {
  float z = dot(world - uCamPos.xyz, uCamFwd.xyz);
  int count = int(uShadow2.x + 0.5), c = 0;
  for (int i = 0; i < 3; ++i)
    if (i < count - 1 && z > CascadeSplit(i)) c = i + 1;
  return c;
}
float ShadowFactor(vec3 world, vec3 n) { // 1 = lit
  if (uShadow.x < 0.5) return 1.0;
  int count = int(uShadow2.x + 0.5), c = ShadowCascade(world);
  float s = CascadeShadow(c, world, n, c == count - 1);
  if (c < count - 1) { // blend into the next cascade over the last part of this one (no visible seam)
    float z = dot(world - uCamPos.xyz, uCamFwd.xyz), split = CascadeSplit(c), band = split * uShadow2.z;
    float f = smoothstep(split - band, split, z);
    if (f > 0.0) s = mix(s, CascadeShadow(c + 1, world, n, c + 1 == count - 1), f);
  }
  return s;
}
// Point / spot shadow (3-4b): a spot light has one tile (its cone), a point light six (cube faces +x -x +y -y +z -z,
// picked by the largest axis toward the pixel). 3x3 taps of 2x2 hardware PCF inside the tile; the normal offset grows
// with the distance like the tile's texels do.
float LocalShadow(int first, bool spot, vec3 lightPos, vec3 world, vec3 ng, float offsetPerMetre) {
  vec3 d = world - lightPos;
  int tile = first;
  if (!spot) {
    vec3 a = abs(d);
    tile += a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
  }
  vec4 p = uLSM[tile] * vec4(world + ng * (offsetPerMetre * length(d)), 1.0);
  if (p.w <= 0.0) return 1.0;
  vec3 s = p.xyz / p.w * 0.5 + 0.5;
  if (s.z >= 1.0 || s.x < 0.0 || s.y < 0.0 || s.x > 1.0 || s.y > 1.0) return 1.0;
  int cols = int(uLocalShadow.x + 0.5);
  vec2 cell = vec2(float(tile % cols), float(tile / cols)), grid = uLocalShadow.xy;
  float t = 1.0 / uLocalShadow.z, sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      vec2 uv = clamp(s.xy + vec2(x, y) * t, vec2(t * 0.5), vec2(1.0 - t * 0.5));
      sum += textureLod(uLocalShadowMap, vec3((cell + uv) / grid, s.z), 0.0);
    }
  return sum / 9.0;
}
vec3 LocalLight(int i, vec3 world, vec3 ng, vec3 N, vec3 V, vec3 diffuse, vec3 f0, float a) { // light i of the light list
  vec4 lp = uLL[i * 4], lc = uLL[i * 4 + 1], ld = uLL[i * 4 + 2], lk = uLL[i * 4 + 3];
  vec3 toL = lp.xyz - world;
  float d2 = max(dot(toL, toL), 1e-4);
  float range = max(lp.w, 1e-3);
  if (d2 >= range * range) return vec3(0.0);
  vec3 L = toL * inversesqrt(d2);
  float win = clamp(1.0 - pow(d2 / (range * range), 2.0), 0.0, 1.0);
  float atten = win * win / (d2 + 1.0); // windowed inverse square (no hard cut at the range)
  if (lk.y > 0.5) atten *= smoothstep(ld.w, lk.x, dot(-L, ld.xyz)); // spot cone
  if (atten <= 0.0) return vec3(0.0);
  if (lk.z > -0.5) atten *= LocalShadow(int(lk.z + 0.5), lk.y > 0.5, lp.xyz, world, ng, lk.w); // lk.z = first atlas tile
  return Brdf(N, V, L, diffuse, f0, a) * lc.rgb * atten;
}
void ClusterLights(vec3 world, out uint first, out uint count) { // this pixel's cluster (Forward+, 3-5b)
  ivec2 tile = clamp(ivec2(gl_FragCoord.xy * uScreen.zw * uCluster.xy), ivec2(0), ivec2(uCluster.xy) - 1);
  float z = max(dot(world - uCamPos.xyz, uCamFwd.xyz), uCluster2.x);
  int slice = clamp(int(log(z / uCluster2.x) * uCluster2.y), 0, int(uCluster.z) - 1);
  int c = (slice * int(uCluster.y) + tile.y) * int(uCluster.x) + tile.x;
  first = uCl[c * 2];
  count = uCl[c * 2 + 1];
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
  int view = int(uCounts.w + 0.5); // debug view (queue 3-8): 0 lit, 1 albedo, 2 normal, 3 rough, 4 metal, 5 depth, 6 shadow, 7 indirect, 8 cascades, 9 ao, 10 lights
  if (view == 1) { oColor = vec4(base.rgb, 1.0); return; }
  if (uParams.z > 0.5 && view == 0) { oColor = vec4(Fog(base.rgb), alpha); return; }        // unlit material
  if (uCounts.y < 0.5 && view == 0) { oColor = vec4(Fog(base.rgb + emissive), alpha); return; }          // potato tier: no lighting
  if (uCounts.y < 1.5 && view == 0) { oColor = vec4(Fog(base.rgb * vLit + emissive), alpha); return; }   // low tier: per vertex

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
  int probe = int(uExtra.w + 0.5);
  vec3 ambient = ProbeIrradiance(probe, N);             // GI Tier 0: probe (or sky) light from every direction
  if (InVolume(vWorld)) {                               // inside a probe volume: blended per pixel, nudged off the surface
    vec3 cell = (uVolMax.xyz - uVolMin.xyz) / max(uVolCount.xyz, vec3(1.0));
    ambient = VolumeIrradiance(vWorld + ng * 0.25 * min(cell.x, min(cell.y, cell.z)), N);
  }
  if (uObjects[vObject].lightmap.x > 0.5) ambient = textureLod(uLightmap, vLightUV, 0.0).rgb; // baked (GI Tier 0-C)
  float ssao = uFlags.x > 0.5 ? textureLod(uSSAO, gl_FragCoord.xy * uScreen.zw, 0.0).r : 1.0;
  uint lightFirst = 0u, lightCount = uint(uCluster.w + 0.5);
  if (uCluster2.z > 0.5) ClusterLights(vWorld, lightFirst, lightCount);
  if (view >= 2) {
    float heat = clamp(float(lightCount) / 16.0, 0.0, 1.0); // lights this pixel evaluates: blue 0, green 8, red 16+
    vec3 d = view == 10 ? (lightCount == 0u ? vec3(0.05) : mix(mix(vec3(0.1, 0.2, 1.0), vec3(0.1, 1.0, 0.2), min(heat * 2.0, 1.0)),
                                                              vec3(1.0, 0.15, 0.1), max(heat * 2.0 - 1.0, 0.0)))
           : view == 2 ? N * 0.5 + 0.5 : view == 3 ? vec3(rough) : view == 4 ? vec3(metallic)
           : view == 5 ? vec3(clamp(log2(1.0 + length(vWorld - uCamPos.xyz)) / log2(201.0), 0.0, 1.0)) // 0..200 m, log
           : view == 7 ? ambient / (1.0 + ambient)                                                   // indirect light, compressed
           : view == 9 ? vec3(ssao)                                                                 // ambient occlusion
           : view == 8 ? (uShadow.x < 0.5 ? vec3(0.3)
                          : vec3[](vec3(1.0, 0.35, 0.3), vec3(0.35, 1.0, 0.35), vec3(0.35, 0.5, 1.0), vec3(1.0, 0.9, 0.3))[ShadowCascade(vWorld)]
                            * (0.4 + 0.6 * ShadowFactor(vWorld, ng)))                              // shadow cascades, darkened in shadow
           : vec3(ShadowFactor(vWorld, ng));
    oColor = vec4(pow(d, vec3(2.2)), 1.0); // data, not colour: undo the display curve applied in post
    return;
  }
  vec3 f0 = mix(vec3(0.04), base.rgb, metallic), diffuse = base.rgb * (1.0 - metallic);
  vec3 color = Brdf(N, V, L, diffuse, f0, a) * uLight.rgb * ShadowFactor(vWorld, ng);
  if (uCluster2.z > 0.5)
    for (uint k = 0u; k < lightCount; ++k) color += LocalLight(int(uCl[lightFirst + k]), vWorld, ng, N, V, diffuse, f0, a);
  else
    for (uint k = 0u; k < lightCount; ++k) color += LocalLight(int(k), vWorld, ng, N, V, diffuse, f0, a);
  vec3 R = reflect(-V, N);
  vec3 env = ProbeSpecular(probe, R, vWorld, rough);   // and what the surface mirrors, blurred by roughness
  if (uSsr.x > 0.5 && view == 0) { // screen-space reflection where the ray finds the picture; the probe elsewhere
    vec4 ssr = TraceSSR(vWorld, ng, R, rough);
    env = mix(env, ssr.rgb, ssr.a);
  }
  vec2 ab = EnvBRDFApprox(NoV, rough);
  float ao = (uMaps.w > 0.5 ? 1.0 + uExtra.y * (occ.r - 1.0) : 1.0) * ssao; // occlusion dims indirect light only
  color += (diffuse * ambient + env * (f0 * ab.x + ab.y)) * ao;
  color += emissive;
  // Linear light; tone mapping and display encoding happen in post. Capped below the half-float maximum: a sharp
  // highlight of a point light (GGX on a mirror) would otherwise turn into inf in the RGBA16F target and its mips.
  oColor = vec4(min(Fog(color), vec3(60000.0)), alpha);
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
// Image-based lighting (GI Tier 0): the sky into a cube face, then GGX prefiltering (Karis, filtered importance
// sampling) of a cube into one roughness level of the probe array.
const char* kSkyCubeFS = R"(#version 450 core
#FRAME_UBO
layout(std140, binding = 6) uniform Prefilter3D { vec4 uP; }; // face, roughness, source size, target size
out vec4 oColor;
#SKY_FN
#FACE_DIR
void main() { oColor = vec4(SkyColor(normalize(FaceDir(int(uP.x + 0.5), gl_FragCoord.xy / uP.w)), 0.0), 1.0); }
)";
const char* kPrefilterFS = R"(#version 450 core
layout(binding = 0) uniform samplerCube uSrc;
layout(std140, binding = 6) uniform Prefilter3D { vec4 uP; };
out vec4 oColor;
#FACE_DIR
const float PI = 3.14159265;
float RadicalInverse(uint b) {
  b = (b << 16u) | (b >> 16u);
  b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
  b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
  b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
  b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
  return float(b) * 2.3283064365386963e-10;
}
void main() {
  vec3 N = normalize(FaceDir(int(uP.x + 0.5), gl_FragCoord.xy / uP.w));
  float rough = uP.y;
  if (rough < 0.01) { oColor = vec4(textureLod(uSrc, N, 0.0).rgb, 1.0); return; } // mirror level
  float a = rough * rough;
  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 T = normalize(cross(up, N)), B = cross(N, T);
  const uint COUNT = 128u;
  float saTexel = 4.0 * PI / (6.0 * uP.z * uP.z);
  vec3 sum = vec3(0.0);
  float w = 0.0;
  for (uint i = 0u; i < COUNT; ++i) {
    vec2 xi = vec2(float(i) / float(COUNT), RadicalInverse(i));
    float phi = 2.0 * PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y)), sinT = sqrt(1.0 - cosT * cosT);
    vec3 H = normalize(T * (sinT * cos(phi)) + B * (sinT * sin(phi)) + N * cosT);
    vec3 L = normalize(2.0 * dot(N, H) * H - N);
    float NoL = dot(N, L);
    if (NoL <= 0.0) continue;
    float NoH = max(dot(N, H), 0.0), d = NoH * NoH * (a * a - 1.0) + 1.0;
    float pdf = a * a / (PI * d * d) * 0.25; // V = N
    float mip = 0.5 * log2((1.0 / (float(COUNT) * pdf + 1e-4)) / saTexel) + 1.0;
    sum += textureLod(uSrc, L, max(mip, 0.0)).rgb * NoL;
    w += NoL;
  }
  oColor = vec4(sum / max(w, 1e-4), 1.0);
}
)";

// Shadow casters: depth only; alpha-masked materials cut their holes.
const char* kShadowVS = R"(#version 450 core
#FRAME_UBO
#OBJECTS_VS
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
layout(std140, binding = 2) uniform ShadowPass3D { mat4 uShadowVP; }; // the cascade being drawn
void main() { vObject = int(aObject + 0.5); vUV = aUV; gl_Position = uShadowVP * (uModel * vec4(aPos, 1.0)); }
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
// SSAO (3-5b). Prepass: opaque objects' world normal and view depth (no geometry: depth 65000).
const char* kPrepassFS = R"(#version 450 core
#FRAME_UBO
#OBJECTS_FS
layout(binding = 0) uniform sampler2D uBaseMap;
in vec3 vWorld; in vec3 vNormal; in vec2 vUV;
out vec4 oColor;
void main() {
  if (uParams.w > 0.5 && uParams.w < 1.5 && uBaseColor.a * (uMaps.x > 0.5 ? texture(uBaseMap, vUV).a : 1.0) < uExtra.z) discard;
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  oColor = vec4(n, dot(vWorld - uCamPos.xyz, uCamFwd.xyz));
}
)";
// Occlusion: 12 cosine-weighted samples in the normal's hemisphere within uAO.x metres, turned per pixel; a sample
// counts when the visible surface at its pixel is in front of it, by no more than the radius (range check).
// uAO = (radius, intensity, -, -); uAO2 = (blur step x, y in texels, -, -)
const char* kSsaoFS = R"(#version 450 core
#FRAME_UBO
layout(std140, binding = 8) uniform Ssao3D { vec4 uAO; vec4 uAO2; };
layout(binding = 0) uniform sampler2D uGeo;
in vec2 vUV;
out vec4 oColor;
vec3 WorldAt(vec2 uv, float depth) {
  vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  vec3 ray = normalize(p.xyz / p.w - uCamPos.xyz);
  return uCamPos.xyz + ray * (depth / dot(ray, uCamFwd.xyz));
}
void main() {
  vec4 g = textureLod(uGeo, vUV, 0.0);
  if (g.w > 60000.0) { oColor = vec4(1.0); return; }
  vec3 n = normalize(g.xyz), p = WorldAt(vUV, g.w);
  float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
  vec3 helper = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 t = normalize(cross(helper, n)), b = cross(n, t);
  float radius = uAO.x, bias = 0.01 + g.w * 0.002, occ = 0.0;
  const int N = 12;
  for (int i = 0; i < N; ++i) {
    float u = fract((float(i) + 0.5) / float(N) + noise), phi = 6.2831853 * fract(float(i) * 0.618034 + noise * 3.1);
    float rr = sqrt(u), z = sqrt(1.0 - u), len = mix(0.15, 1.0, fract(float(i) * 0.37 + noise * 1.7));
    vec3 s = p + n * 0.02 + (t * (rr * cos(phi)) + b * (rr * sin(phi)) + n * z) * radius * len;
    vec4 c = uViewProj * vec4(s, 1.0);
    vec2 suv = c.xy / c.w * 0.5 + 0.5;
    if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
    float scene = textureLod(uGeo, suv, 0.0).w;
    occ += step(bias, c.w - scene) * smoothstep(0.0, 1.0, radius / max(abs(g.w - scene), 1e-3));
  }
  float ao = pow(clamp(1.0 - occ / float(N), 0.0, 1.0), uAO.y);
  oColor = vec4(mix(ao, 1.0, smoothstep(60.0, 100.0, g.w)), 0.0, 0.0, 1.0); // fades out far away (depth precision)
}
)";
// Depth-aware blur of the occlusion (one direction per pass): neighbours at another depth do not bleed in.
const char* kAoBlurFS = R"(#version 450 core
layout(std140, binding = 8) uniform Ssao3D { vec4 uAO; vec4 uAO2; };
layout(binding = 0) uniform sampler2D uSrc;
layout(binding = 1) uniform sampler2D uGeo;
in vec2 vUV;
out vec4 oColor;
void main() {
  vec2 step = uAO2.xy / vec2(textureSize(uSrc, 0));
  float d0 = textureLod(uGeo, vUV, 0.0).w, sum = 0.0, wsum = 0.0;
  for (int i = -4; i <= 4; ++i) {
    vec2 uv = vUV + step * float(i);
    float d = textureLod(uGeo, uv, 0.0).w;
    float w = exp(-float(i * i) / 8.0) * exp(-abs(d - d0) / (0.03 * d0 + 0.01) * 4.0);
    sum += textureLod(uSrc, uv, 0.0).r * w;
    wsum += w;
  }
  oColor = vec4(sum / max(wsum, 1e-5), 0.0, 0.0, 1.0);
}
)";
const char* kPostVS = R"(#version 450 core
layout(location = 0) in vec3 aPos;
out vec2 vUV;
void main() { vUV = aPos.xy * 0.5 + 0.5; gl_Position = vec4(aPos.xy, 0.0, 1.0); }
)";
// uPost = (exposure, bloom intensity, tone map on, bloom on)
// uPost3 = (vignette, vignette smoothness, chromatic aberration, film grain), uPost4 = (auto exposure on, min EV,
// max EV, grain seed), uPost5 = (adaptation step 0..1, first frame, aspect w/h, -)   (post-process volumes, E-6)
// uPost6 = (focus distance m, in-focus range m, widest blur px, depth of field on), uPost7 = (lens flare, -, -, -)
// uPost8 = (sun u, sun v, shaft strength incl. fade, shafts on), uPost9 = sun colour x intensity (linear)   (E-7)
#define POST_UBO "layout(std140, binding = 4) uniform Post3D { vec4 uPost; vec4 uPost2; vec4 uPost3; vec4 uPost4; vec4 uPost5; vec4 uPost6; vec4 uPost7; vec4 uPost8; vec4 uPost9; };\n"
const char* kToneMapFS = "#version 450 core\n" POST_UBO R"(
layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uBloom;
layout(binding = 2) uniform sampler3D uGrading; // 32^3 colour grading table (3-5b), uPost2.y = on
layout(binding = 3) uniform sampler2D uAdapted; // 1x1: average log2 luminance the eye has adapted to (E-6)
layout(binding = 4) uniform sampler2D uShafts;  // half-size light shafts (E-7)
in vec2 vUV;
out vec4 oColor;
float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 ACES(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); } // Narkowicz fit
vec3 ToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
  vec3 c;
  if (uPost3.z > 0.0) { // chromatic aberration: red and blue pulled apart toward the edges
    vec2 off = (vUV - 0.5) * uPost3.z * 0.012;
    c = vec3(texture(uHdr, vUV + off).r, texture(uHdr, vUV).g, texture(uHdr, vUV - off).b);
  } else c = texture(uHdr, vUV).rgb;
  if (uPost.w > 0.5) c += texture(uBloom, vUV).rgb * (uPost.y / 5.0); // the chain sums 5 levels: average them
  if (uPost8.w > 0.5) c += texture(uShafts, vUV).r * uPost9.rgb * uPost8.z * 0.5; // god rays (E-7)
  float exposure = uPost.x;
  if (uPost4.x > 0.5) { // eye adaptation: bring the adapted average to middle grey, within the volume's range
    float avgLog = texelFetch(uAdapted, ivec2(0), 0).r;
    exposure *= exp2(clamp(log2(0.18) - avgLog, uPost4.y, uPost4.z));
  }
  c *= exposure;
  if (uPost.w > 0.5 && uPost7.x > 0.0) { // lens flare (E-6b): ghosts of what is brighter than white (after exposure), mirrored through the centre, and a halo
    vec2 toCentre = (vec2(0.5) - vUV) * 0.55;
    vec3 flare = vec3(0.0);
    for (int i = 1; i <= 4; ++i) {
      vec2 g = fract(vUV + toCentre * float(i));
      float fade = pow(1.0 - clamp(length(vec2(0.5) - g) / 0.7071, 0.0, 1.0), 6.0);
      vec3 tint = vec3(1.0, 0.85, 0.7) * (i % 2 == 0 ? vec3(0.7, 0.9, 1.2) : vec3(1.0));
      flare += max(texture(uBloom, g).rgb / 5.0 * exposure - 1.0, 0.0) * fade * tint;
    }
    vec2 halo = fract(vUV + normalize(toCentre + 1e-5) * 0.45);
    flare += max(texture(uBloom, halo).rgb / 5.0 * exposure - 1.0, 0.0) * pow(1.0 - clamp(length(vec2(0.5) - halo) / 0.7071, 0.0, 1.0), 4.0) * 0.5;
    c += flare * uPost7.x;
  }
  if (uPost.z > 0.5) c = ACES(c);
  if (uPost2.x > 0.5) { oColor = vec4(c, 1.0); return; } // linear capture: no display curve, no grading
  c = ToSrgb(c);
  if (uPost2.y > 0.5) c = textureLod(uGrading, c * (31.0 / 32.0) + 0.5 / 32.0, 0.0).rgb;
  if (uPost3.x > 0.0) { // vignette: darker toward the corners, round whatever the aspect (r = 1 at a corner)
    vec2 d = (vUV - 0.5) * vec2(uPost5.z, 1.0);
    float r = length(d) / length(vec2(uPost5.z, 1.0) * 0.5);
    c *= 1.0 - uPost3.x * smoothstep(mix(0.8, 0.15, uPost3.y), 1.05, r);
  }
  if (uPost3.w > 0.0) c += (Hash(gl_FragCoord.xy + vec2(uPost4.w * 7.13, uPost4.w * 3.71)) - 0.5) * uPost3.w * 0.16; // film grain
  oColor = vec4(c, 1.0); // opaque: the UI draws this picture with alpha blending
}
)";
// Depth of field (E-6b): a disc gather around each pixel, as wide as its circle of confusion (from the view depth of
// the depth/normal prepass); samples count only if their own circle reaches this pixel, so sharp objects do not
// smear into a blurred background. The sky (cleared depth) is as blurred as far ground.
const char* kDofFS = "#version 450 core\n" POST_UBO R"(
layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uGeo; // .w = view depth
in vec2 vUV;
out vec4 oColor;
float CoC(float z) {
  float d = abs(min(z, 10000.0) - uPost6.x) - uPost6.y * 0.5;
  return clamp(d / max(uPost6.x * 0.5, 0.05), 0.0, 1.0) * uPost6.z; // pixels
}
void main() {
  vec2 texel = 1.0 / vec2(textureSize(uHdr, 0));
  float c0 = CoC(texture(uGeo, vUV).w);
  vec3 sum = texture(uHdr, vUV).rgb;
  float wsum = 1.0;
  if (c0 > 0.5) {
    const float golden = 2.39996323;
    for (int i = 1; i < 48; ++i) {
      float r = sqrt(float(i) / 48.0) * c0, a = float(i) * golden;
      vec2 uv = vUV + vec2(cos(a), sin(a)) * r * texel;
      float ci = CoC(texture(uGeo, uv).w);
      float w = clamp(ci - r + 1.0, 0.0, 1.0); // its own blur must reach this far
      sum += texture(uHdr, uv).rgb * w;
      wsum += w;
    }
  }
  oColor = vec4(sum / wsum, 1.0);
}
)";
// Light shafts (E-7, Unreal's occlusion shafts): march from each pixel toward the sun on the screen; every step that
// lands on sky (no geometry in the depth prepass) near the sun adds light, decaying along the way. Half size.
const char* kShaftsFS = "#version 450 core\n" POST_UBO R"(
layout(binding = 0) uniform sampler2D uGeo; // .w = view depth (65000 = nothing drawn: sky)
in vec2 vUV;
out vec4 oColor;
void main() {
  const int kSteps = 64;
  vec2 sun = uPost8.xy;
  vec2 stepUV = (sun - vUV) / float(kSteps) * 0.95;
  vec2 uv = vUV;
  float light = 0.0, decay = 1.0;
  for (int i = 0; i < kSteps; ++i) {
    uv += stepUV;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { decay *= 0.96; continue; }
    float sky = texture(uGeo, uv).w > 60000.0 ? 1.0 : 0.0;
    float near = 1.0 - smoothstep(0.0, 0.25, length((uv - sun) * vec2(uPost5.z, 1.0))); // the sky close to the sun
    light += sky * near * near * decay;
    decay *= 0.96;
  }
  oColor = vec4(light / float(kSteps), 0.0, 0.0, 1.0);
}
)";
// Auto exposure, pass 1: log2 luminance of the HDR picture on a 32x32 target (16 samples per texel).
const char* kLumFS = "#version 450 core\n" POST_UBO R"(
layout(binding = 0) uniform sampler2D uHdr;
in vec2 vUV;
out vec4 oColor;
void main() {
  float sum = 0.0;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      vec2 uv = (floor(gl_FragCoord.xy) + (vec2(x, y) + 0.5) / 4.0) / 32.0;
      vec3 c = textureLod(uHdr, uv, 0.0).rgb;
      sum += log2(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-4));
    }
  oColor = vec4(sum / 16.0, 0.0, 0.0, 1.0);
}
)";
// Auto exposure, pass 2 (1x1): the picture's average, approached from last frame's at the adaptation speed.
const char* kAdaptFS = "#version 450 core\n" POST_UBO R"(
layout(binding = 0) uniform sampler2D uLum;  // 32x32 log luminance
layout(binding = 1) uniform sampler2D uPrev; // 1x1 last adapted value
out vec4 oColor;
void main() {
  float sum = 0.0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x) sum += texelFetch(uLum, ivec2(x, y), 0).r;
  float avg = sum / 1024.0;
  float prev = texelFetch(uPrev, ivec2(0), 0).r;
  oColor = vec4(uPost5.y > 0.5 ? avg : mix(prev, avg, uPost5.x), 0.0, 0.0, 1.0);
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
// ---- Volumetric fog (round 6) ----
// A froxel grid: uGrid.x by uGrid.y screen tiles, uGrid.z depth slices spaced exponentially from uFogA.x to uFogA.y
// metres of view depth. uFogA = (near, far, density, fog height), uFogB = (height falloff, anisotropy, local light
// scale, slice jitter 0..1), uFogC = (albedo rgb, -), uGrid.w = local lights; uFogInvVP = unjittered inverse view-proj.
#define FOG_UBO "layout(std140, binding = 10) uniform Fog3D { vec4 uGrid; vec4 uFogA; vec4 uFogB; vec4 uFogC; mat4 uFogInvVP; };\n"
// 1. Each froxel: density (height fog) and the light it scatters toward the camera: the sun through its shadow map
// (Henyey-Greenstein phase), the sky, and point / spot lights (even phase, no shadows).
const char* kFogLightCS = "#version 450 core\nlayout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;\n#FRAME_UBO\n" FOG_UBO R"(
#FOG_COMMON
layout(binding = 5) uniform sampler2DShadow uShadowMap;
layout(std430, binding = 2) readonly buffer LightList3D { vec4 uLL[]; };
layout(std430, binding = 12) writeonly buffer FogMedia { vec4 uMedia[]; };
const float PI = 3.14159265;
float SunShadow(vec3 w) {
  if (uShadow.x < 0.5) return 1.0;
  float z = dot(w - uCamPos.xyz, uCamFwd.xyz);
  int count = int(uShadow2.x + 0.5), c = 0;
  for (int i = 0; i < 3; ++i)
    if (i < count - 1 && z > (i == 0 ? uCascadeSplit.x : i == 1 ? uCascadeSplit.y : uCascadeSplit.z)) c = i + 1;
  vec4 p = uCascadeVP[c] * vec4(w, 1.0);
  vec3 s = p.xyz / p.w * 0.5 + 0.5;
  if (s.z >= 1.0 || any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
  return textureLod(uShadowMap, vec3((float(c) + s.x) / uShadow2.x, s.y, s.z - uCascadeInfo[c].x), 0.0);
}
float Phase(float cosT, float g) { float g2 = g * g; return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-4), 1.5)); }
void main() {
  uvec3 id = gl_GlobalInvocationID;
  if (any(greaterThanEqual(id, uvec3(uGrid.xyz)))) return;
  vec2 uv = (vec2(id.xy) + 0.5) / uGrid.xy;
  vec3 ray = RayAt(uv);
  vec3 w = uCamPos.xyz + ray * (SliceDepth(float(id.z) + uFogB.w) / dot(ray, uCamFwd.xyz));
  float dens = uFogA.z * (uFogB.x > 0.0 ? exp(-uFogB.x * clamp(w.y - uFogA.w, -20.0, 200.0)) : 1.0);
  vec3 L = normalize(uToLight.xyz);
  vec3 light = uLight.rgb * SunShadow(w) * Phase(dot(ray, L), uFogB.y);
  light += mix(uHorizon.rgb, uZenith.rgb, 0.5) * (1.0 / (4.0 * PI)) * 0.5; // the sky, all around
  for (int i = 0; i < int(uGrid.w); ++i) {
    vec4 lp = uLL[i * 4], lc = uLL[i * 4 + 1], ld = uLL[i * 4 + 2], lk = uLL[i * 4 + 3];
    vec3 toL = lp.xyz - w;
    float d2 = max(dot(toL, toL), 1e-4), range = max(lp.w, 1e-3);
    if (d2 >= range * range) continue;
    float win = clamp(1.0 - pow(d2 / (range * range), 2.0), 0.0, 1.0);
    float atten = win * win / (d2 + 1.0);
    if (lk.y > 0.5) atten *= smoothstep(ld.w, lk.x, dot(-toL * inversesqrt(d2), ld.xyz));
    light += lc.rgb * lc.w * atten * uFogB.z / (4.0 * PI);
  }
  uMedia[(id.z * uint(uGrid.y) + id.y) * uint(uGrid.x) + id.x] = vec4(uFogC.rgb * dens * light, dens);
}
)";
// 2. Front to back along each column: light gathered so far and what still gets through (transmittance), at the far
// edge of each slice.
const char* kFogIntegrateCS = "#version 450 core\nlayout(local_size_x = 8, local_size_y = 8) in;\n#FRAME_UBO\n" FOG_UBO R"(
#FOG_COMMON
layout(std430, binding = 12) readonly buffer FogMedia { vec4 uMedia[]; };
layout(std430, binding = 13) writeonly buffer FogAccum { vec4 uAccum[]; };
void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, uvec2(uGrid.xy)))) return;
  float stretch = 1.0 / max(dot(RayAt((vec2(id) + 0.5) / uGrid.xy), uCamFwd.xyz), 0.05); // view depth -> distance
  vec3 S = vec3(0.0);
  float T = 1.0;
  for (uint z = 0u; z < uint(uGrid.z); ++z) {
    uint i = (z * uint(uGrid.y) + id.y) * uint(uGrid.x) + id.x;
    vec4 m = uMedia[i];
    float ds = (SliceDepth(float(z) + 1.0) - SliceDepth(float(z))) * stretch;
    float ext = max(m.a, 1e-6), tr = exp(-ext * ds);
    S += T * m.rgb * (1.0 - tr) / ext;
    T *= tr;
    uAccum[i] = vec4(S, T);
  }
}
)";
// 3. Each pixel: the fog between the camera and its surface (sky: the whole simulated distance), trilinear.
const char* kFogApplyFS = "#version 450 core\n#FRAME_UBO\n" FOG_UBO R"(
#FOG_COMMON
layout(binding = 0) uniform sampler2D uHdr;
layout(binding = 1) uniform sampler2D uGeo;
layout(std430, binding = 13) readonly buffer FogAccum { vec4 uAccum[]; };
in vec2 vUV;
out vec4 oColor;
vec4 At(int x, int y, int z) {
  if (z < 0) return vec4(0.0, 0.0, 0.0, 1.0);
  ivec3 g = ivec3(uGrid.xyz);
  x = clamp(x, 0, g.x - 1); y = clamp(y, 0, g.y - 1); z = min(z, g.z - 1);
  return uAccum[(z * g.y + y) * g.x + x];
}
void main() {
  vec3 c = textureLod(uHdr, vUV, 0.0).rgb;
  float d = min(textureLod(uGeo, vUV, 0.0).w, uFogA.y);
  float s = log(max(d, uFogA.x) / uFogA.x) / log(uFogA.y / uFogA.x) * uGrid.z - 1.0; // slice coordinate (far edges)
  vec2 xy = vUV * uGrid.xy - 0.5;
  ivec2 b = ivec2(floor(xy));
  vec2 f = xy - vec2(b);
  int z0 = int(floor(s));
  float fz = s - float(z0);
  vec4 r = vec4(0.0);
  for (int k = 0; k < 2; ++k) {
    vec4 a = mix(mix(At(b.x, b.y, z0 + k), At(b.x + 1, b.y, z0 + k), f.x), mix(At(b.x, b.y + 1, z0 + k), At(b.x + 1, b.y + 1, z0 + k), f.x), f.y);
    r += a * (k == 0 ? 1.0 - fz : fz);
  }
  oColor = vec4(c * r.a + r.rgb, 1.0);
}
)";
// ---- Temporal anti-aliasing (round 6) ----
// uCurVP / uInvCurVP: this frame without jitter; uPrevVP: the previous frame's (no jitter); uJitVP: what the scene
// pass used. uTaa = (-, -, feedback, history valid), uTaa2 = (sharpen, -, -, -)
#define TEMPORAL_UBO "layout(std140, binding = 9) uniform Temporal3D { mat4 uCurVP; mat4 uPrevVP; mat4 uJitVP; mat4 uInvCurVP; vec4 uTaa; vec4 uTaa2; };\n"
// Motion vectors: where each opaque surface point was on the previous frame's picture (its object's previous
// placement, the previous camera), as a UV offset; alpha 1 marks surfaces (0: background, the camera alone).
const char* kVelocityVS = "#version 450 core\n#OBJECTS_VS\n" TEMPORAL_UBO R"(
layout(location = 0) in vec3 aPos;
layout(std430, binding = 6) readonly buffer PrevModels { mat4 uPrevModel[]; };
out vec4 vCur; out vec4 vPrev;
void main() {
  vObject = int(aObject + 0.5);
  vec4 w = uModel * vec4(aPos, 1.0);
  vCur = uCurVP * w;
  vPrev = uPrevVP * (uPrevModel[vObject] * vec4(aPos, 1.0));
  gl_Position = uJitVP * w;
}
)";
const char* kVelocityFS = R"(#version 450 core
in vec4 vCur; in vec4 vPrev;
out vec4 oColor;
void main() { oColor = vec4((vCur.xy / vCur.w - vPrev.xy / vPrev.w) * 0.5, 0.0, 1.0); }
)";
// Resolve: this frame's pixel mixed with the history found through the motion vector; the history is clamped to the
// colour range of the pixel's 3x3 neighbourhood (YCoCg, in a tone-mapped space so bright pixels do not dominate),
// which removes ghosts of things that moved or appeared. No history (first frame, camera cut, off screen): this frame.
const char* kTaaFS = "#version 450 core\n" TEMPORAL_UBO R"(
layout(binding = 0) uniform sampler2D uCur;
layout(binding = 1) uniform sampler2D uHist;
layout(binding = 2) uniform sampler2D uVel;
in vec2 vUV;
out vec4 oColor;
vec3 ToYCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 FromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
vec3 Squash(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 Unsquash(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }
vec3 Fetch(vec2 uv) { return ToYCoCg(Squash(max(textureLod(uCur, uv, 0.0).rgb, vec3(0.0)))); }
void main() {
  vec2 texel = 1.0 / vec2(textureSize(uCur, 0));
  vec3 cur = Fetch(vUV), lo = cur, hi = cur;
  // velocity of the nearest surface in the neighbourhood (edges keep their object's motion)
  vec4 vel = textureLod(uVel, vUV, 0.0);
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      if (x == 0 && y == 0) continue;
      vec3 c = Fetch(vUV + vec2(x, y) * texel);
      lo = min(lo, c);
      hi = max(hi, c);
      vec4 v = textureLod(uVel, vUV + vec2(x, y) * texel, 0.0);
      if (v.a > vel.a || (v.a > 0.5 && dot(v.xy, v.xy) > dot(vel.xy, vel.xy))) vel = v;
    }
  vec2 motion = vel.xy;
  if (vel.a < 0.5) { // background: the camera's motion at the far plane
    vec4 wp = uInvCurVP * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 pp = uPrevVP * vec4(wp.xyz / wp.w, 1.0);
    motion = vUV - (pp.xy / pp.w * 0.5 + 0.5);
  }
  vec2 huv = vUV - motion;
  if (uTaa.w < 0.5 || any(lessThan(huv, vec2(0.0))) || any(greaterThan(huv, vec2(1.0)))) {
    oColor = vec4(max(textureLod(uCur, vUV, 0.0).rgb, vec3(0.0)), 1.0);
    return;
  }
  vec3 hist = clamp(ToYCoCg(Squash(max(textureLod(uHist, huv, 0.0).rgb, vec3(0.0)))), lo, hi);
  // faster motion keeps less history (less blur), still enough to smooth edges
  float speed = length(motion / texel);
  float fb = mix(uTaa.z, uTaa.z * 0.8, clamp(speed / 8.0, 0.0, 1.0));
  oColor = vec4(Unsquash(FromYCoCg(mix(cur, hist, fb))), 1.0);
}
)";
// A plain copy (SSR without TAA keeps the previous frame's picture this way).
const char* kCopyFS = R"(#version 450 core
layout(binding = 0) uniform sampler2D uSrc;
in vec2 vUV;
out vec4 oColor;
void main() { oColor = vec4(textureLod(uSrc, vUV, 0.0).rgb, 1.0); }
)";
// Sharpen after the resolve (the blend softens a little): unsharp mask on the 4 neighbours, in the squashed space.
const char* kSharpenFS = "#version 450 core\n" TEMPORAL_UBO R"(
layout(binding = 0) uniform sampler2D uSrc;
in vec2 vUV;
out vec4 oColor;
vec3 Squash(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 Unsquash(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }
void main() {
  vec2 t = 1.0 / vec2(textureSize(uSrc, 0));
  vec3 c = Squash(textureLod(uSrc, vUV, 0.0).rgb);
  vec3 n = Squash(textureLod(uSrc, vUV + vec2(0, t.y), 0.0).rgb) + Squash(textureLod(uSrc, vUV - vec2(0, t.y), 0.0).rgb) +
           Squash(textureLod(uSrc, vUV + vec2(t.x, 0), 0.0).rgb) + Squash(textureLod(uSrc, vUV - vec2(t.x, 0), 0.0).rgb);
  vec3 s = clamp(c + (c * 4.0 - n) * uTaa2.x * 0.25, vec3(0.0), vec3(0.999));
  oColor = vec4(Unsquash(s), 1.0);
}
)";
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
  float camFwd[4];
  float cascadeVP[4][16], cascadeSplit[4], cascadeInfo[4][4], shadow2[4];
  float screen[4], flags[4];
  float cluster[4], cluster2[4];
  float fog[4], fog2[4];
  float localShadow[4];
  float prevViewProj[16];
  float ssr[4];
};
struct SsaoData { float ao[4], ao2[4]; };
struct LightsData { float pos[16][4], color[16][4], dir[16][4], cone[16][4]; };
struct PostData { float post[4]; float post2[4]; float post3[4]; float post4[4]; float post5[4]; float post6[4]; float post7[4]; float post8[4]; float post9[4]; };
struct ProbesData { float min[8][4], max[8][4], pos[8][4], sh[72][4], info[4], volMin[4], volMax[4], volCount[4]; };
struct PrefilterData { float p[4]; };

void Put(float* dst, const glm::vec3& v, float w = 0) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = w; }
void Put(float* dst, Color c, float scale = 1) { dst[0] = c.r * scale; dst[1] = c.g * scale; dst[2] = c.b * scale; dst[3] = c.a; }

// Orthographic light camera around the bounding sphere of the view slice [near, distance]; the centre snaps to
// whole shadow texels so the map does not shimmer while the camera moves.
glm::mat4 FitShadow(const Camera3D& cam, float aspect, const glm::vec3& toLight, float sliceNear, float distance, int mapSize,
                    float& texelWorld, float& depthRange) {
  float nearZ = std::max(cam.nearZ, sliceNear), farZ = std::min(cam.farZ, distance);
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
struct ObjectData { float model[16]; float baseColor[4], params[4], emissive[4], maps[4], extra[4], lightmap[4]; };
struct TemporalData { float curVP[16], prevVP[16], jitVP[16], invCurVP[16], taa[4], taa2[4]; };
struct FogData { float grid[4], a[4], b[4], c[4], invVP[16]; };
float Halton(uint32_t i, uint32_t base) {
  float f = 1, r = 0;
  for (; i > 0; i /= base) {
    f /= (float)base;
    r += f * (float)(i % base);
  }
  return r;
}
}

glm::mat4 Camera3D::View() const { return glm::lookAt(position, target, up); }
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
  pd.attrs[5] = {5, 2, offsetof(Vertex3D, lu)};
  pd.attrCount = 6;
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
  {
    const std::string prepassFS = Src(kPrepassFS);
    PipelineDesc pre = pd;
    pre.fragmentSource = prepassFS.c_str();
    pre.alphaBlend = false;
    pre.depthWrite = true;
    pre.cull = CullMode::Back;
    pre.debugName = "ssao.prepass";
    prepass_ = rhi.CreatePipeline(pre);
    pre.cull = CullMode::None;
    pre.debugName = "ssao.prepass.twosided";
    prepassTwo_ = rhi.CreatePipeline(pre);
    if (!prepass_.Valid() || !prepassTwo_.Valid()) return false;
  }
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
    wd.attrs[5] = {5, 2, offsetof(Vertex3D, lu)};
    wd.attrCount = 6;
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
  shadowPassUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(float) * 16, true);
  postUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(PostData), true);
  probesUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(ProbesData), true);
  prefilterUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(PrefilterData), true);
  envArray_ = rhi.CreateCubemap(kEnvSize, kEnvLevels, kMaxProbes + 1);
  int scratchLevels = 1;
  for (int sz = kEnvSize; sz > 1; sz >>= 1) ++scratchLevels;
  scratchCube_ = rhi.CreateCubemap(kEnvSize, scratchLevels);
  volumeCapacity_ = 9;
  volumeBuffer_ = rhi.CreateBuffer(BufferUsage::Storage, nullptr, volumeCapacity_ * 16, true); // bound even when empty
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
  pp.fragmentSource = kLumFS;
  pp.debugName = "post.lum";
  lum_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = kDofFS;
  pp.debugName = "post.dof";
  dof_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = kShaftsFS;
  pp.debugName = "post.shafts";
  shafts_ = rhi.CreatePipeline(pp);
  if (!dof_.Valid() || !shafts_.Valid()) return false;
  pp.fragmentSource = kAdaptFS;
  pp.debugName = "post.adapt";
  adapt_ = rhi.CreatePipeline(pp);
  if (!lum_.Valid() || !adapt_.Valid()) return false;
  const std::string ssaoFS = Src(kSsaoFS);
  pp.fragmentSource = ssaoFS.c_str();
  pp.debugName = "ssao";
  ssao_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = kAoBlurFS;
  pp.debugName = "ssao.blur";
  aoBlur_ = rhi.CreatePipeline(pp);
  if (!ssao_.Valid() || !aoBlur_.Valid()) return false;
  ssaoUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(SsaoData), true);
  { // temporal anti-aliasing (round 6)
    const std::string velVS = Src(kVelocityVS);
    PipelineDesc vd = pd; // the scene's vertex layout
    vd.vertexSource = velVS.c_str();
    vd.fragmentSource = kVelocityFS;
    vd.alphaBlend = vd.additive = vd.wireframe = false;
    vd.depthTest = true;
    vd.depthWrite = true;
    vd.cull = CullMode::None;
    vd.depthBiasConstant = vd.depthBiasSlope = 0;
    vd.debugName = "taa.velocity";
    velocity_ = rhi.CreatePipeline(vd);
    pp.fragmentSource = kTaaFS;
    pp.debugName = "taa.resolve";
    taa_ = rhi.CreatePipeline(pp);
    pp.fragmentSource = kSharpenFS;
    pp.debugName = "taa.sharpen";
    sharpen_ = rhi.CreatePipeline(pp);
    pp.fragmentSource = kCopyFS;
    pp.debugName = "post.copy";
    copy_ = rhi.CreatePipeline(pp);
    const std::string fogLight = Src(kFogLightCS), fogIntegrate = Src(kFogIntegrateCS), fogApply = Src(kFogApplyFS);
    fogLight_ = rhi.CreateComputePipeline(fogLight.c_str(), "fog.light");
    fogIntegrate_ = rhi.CreateComputePipeline(fogIntegrate.c_str(), "fog.integrate");
    pp.fragmentSource = fogApply.c_str();
    pp.debugName = "fog.apply";
    fogApply_ = rhi.CreatePipeline(pp);
    if (!fogLight_.Valid() || !fogIntegrate_.Valid() || !fogApply_.Valid()) return false;
    fogUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(FogData), true);
    if (!velocity_.Valid() || !taa_.Valid() || !sharpen_.Valid() || !copy_.Valid()) return false;
    temporalUbo_ = rhi.CreateBuffer(BufferUsage::Uniform, nullptr, sizeof(TemporalData), true);
  }
  pp.fragmentSource = kBloomUpFS;
  pp.additive = true;
  pp.debugName = "post.bloom.up";
  bloomUp_ = rhi.CreatePipeline(pp);
  const std::string skyCubeFS = Src(kSkyCubeFS), prefilterFS = Src(kPrefilterFS);
  pp.additive = false;
  pp.fragmentSource = skyCubeFS.c_str();
  pp.debugName = "ibl.skycube";
  skyCube_ = rhi.CreatePipeline(pp);
  pp.fragmentSource = prefilterFS.c_str();
  pp.debugName = "ibl.prefilter";
  prefilter_ = rhi.CreatePipeline(pp);
  if (!toneMap_.Valid() || !fxaa_.Valid() || !bloomDown_.Valid() || !bloomUp_.Valid() || !skyCube_.Valid() || !prefilter_.Valid())
    return false;
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
  { // blob shadow spot (3-5b): white, alpha falling off smoothly from the centre
    std::vector<uint8_t> spot(64 * 64 * 4, 255);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const float d = std::min(glm::length(glm::vec2(x + 0.5f, y + 0.5f) / 32.f - 1.f), 1.f);
        // full under the object (its footprint is 1 / 1.6 of the spot), fading out over the rest
        const float t = std::clamp((d - 0.6f) / 0.4f, 0.f, 1.f);
        spot[(y * 64 + x) * 4 + 3] = (uint8_t)std::lround(255 * (1 - t * t * (3 - 2 * t)));
      }
    TextureDesc sd;
    sd.w = sd.h = 64;
    sd.mipmaps = true;
    blobTex_ = rhi.CreateTexture(sd, spot.data());
    blobQuad_ = CreateMesh(MakePlaneXZ(1, 1));
  }

  return true;
}

void Renderer3D::ReleaseTemporal(Temporal& t) {
  for (RenderTargetHandle& r : t.hist) { rhi_->DestroyRenderTarget(r); r = {}; }
  rhi_->DestroyRenderTarget(t.out);
  rhi_->DestroyRenderTarget(t.velocity);
  rhi_->DestroyRenderTarget(t.prevColor);
  t.out = t.velocity = t.prevColor = {};
  t.w = t.h = 0;
  t.valid = false;
}

void Renderer3D::Shutdown() {
  if (!rhi_) return;
  for (auto& [id, t] : temporal_) ReleaseTemporal(t);
  temporal_.clear();
  rhi_->DestroyBuffer(temporalUbo_);
  rhi_->DestroyBuffer(prevModelBuffer_);
  rhi_->DestroyBuffer(fogUbo_);
  rhi_->DestroyBuffer(fogMedia_);
  rhi_->DestroyBuffer(fogAccum_);
  rhi_->DestroyRenderTarget(fogRT_);
  for (PipelineHandle p : {fogLight_, fogIntegrate_, fogApply_})
    if (p.Valid()) rhi_->DestroyPipeline(p);
  for (PipelineHandle p : {velocity_, taa_, sharpen_, copy_})
    if (p.Valid()) rhi_->DestroyPipeline(p);
  rhi_->DestroyBuffer(frameUbo_);
  rhi_->DestroyBuffer(objectBuffer_);
  rhi_->DestroyBuffer(instanceIds_);
  rhi_->DestroyTexture(white_);
  for (auto& [id, a] : adapt_s)
    for (RenderTargetHandle r : a.rt) rhi_->DestroyRenderTarget(r);
  adapt_s.clear();
  if (lumRT_.Valid()) rhi_->DestroyRenderTarget(lumRT_);
  if (dofRT_.Valid()) rhi_->DestroyRenderTarget(dofRT_);
  if (shaftRT_.Valid()) rhi_->DestroyRenderTarget(shaftRT_);
  for (PipelineHandle p : {lum_, adapt_, dof_, shafts_, skin_})
    if (p.Valid()) rhi_->DestroyPipeline(p);
  skin_ = {};
  for (PipelineHandle p : {opaque_, twoSided_, blend_, grid_, mask_, outline_, sky_, shadowPipe_, toneMap_, fxaa_, bloomDown_, bloomUp_, wire_})
    rhi_->DestroyPipeline(p);
  for (TimerFrame& t : timers_)
    for (uint32_t& q : t.q) rhi_->DestroyQuery(q);
  rhi_->DestroyBuffer(postUbo_);
  rhi_->DestroyBuffer(shadowPassUbo_);
  rhi_->DestroyBuffer(ssaoUbo_);
  rhi_->DestroyBuffer(lightBuffer_);
  rhi_->DestroyTexture(gradingLut_);
  rhi_->DestroyTexture(blobTex_);
  DestroyMesh(blobQuad_);
  rhi_->DestroyBuffer(clusterBuffer_);
  for (PipelineHandle p : {prepass_, prepassTwo_, ssao_, aoBlur_}) rhi_->DestroyPipeline(p);
  for (RenderTargetHandle t : {geoRT_, aoRT_[0], aoRT_[1]}) rhi_->DestroyRenderTarget(t);
  rhi_->DestroyBuffer(probesUbo_);
  rhi_->DestroyBuffer(prefilterUbo_);
  rhi_->DestroyTexture(envArray_);
  rhi_->DestroyTexture(scratchCube_);
  rhi_->DestroyRenderTarget(captureRT_);
  rhi_->DestroyBuffer(volumeBuffer_);
  ClearLightmaps();
  rhi_->DestroyTexture(smallCube_);
  rhi_->DestroyRenderTarget(smallCaptureRT_);
  rhi_->DestroyPipeline(skyCube_);
  rhi_->DestroyPipeline(prefilter_);
  for (RenderTargetHandle t : bloom_) rhi_->DestroyRenderTarget(t);
  rhi_->DestroyRenderTarget(hdrRT_);
  rhi_->DestroyRenderTarget(ldrRT_);
  rhi_->DestroyBuffer(lightsUbo_);
  rhi_->DestroyRenderTarget(shadowRT_);
  rhi_->DestroyRenderTarget(noShadowRT_);
  rhi_->DestroyRenderTarget(localShadowRT_);
  rhi_->DestroyBuffer(localShadowBuffer_);
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
  m.source = std::make_shared<const MeshData>(d);
  m.contentHash = 1469598103934665603ull;
  auto fold = [&](const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) m.contentHash = (m.contentHash ^ b[i]) * 1099511628211ull;
  };
  fold(d.vertices.data(), d.vertices.size() * sizeof(Vertex3D));
  fold(d.indices.data(), d.indices.size() * sizeof(uint32_t));
  for (size_t i = 0; i < d.vertices.size(); ++i) {
    glm::vec3 p(d.vertices[i].px, d.vertices[i].py, d.vertices[i].pz);
    m.boundsMin = i ? glm::min(m.boundsMin, p) : p;
    m.boundsMax = i ? glm::max(m.boundsMax, p) : p;
  }
  return m;
}

// 7-1: linear blend skinning of Vertex3D (14 floats) with 4 influences per vertex (skin: 6 uints, joints as two
// 16-bit pairs and weights as float bits). Normals and tangents use the blended matrix's upper 3x3, renormalised.
static const char* kSkinCS = R"(#version 450
layout(local_size_x = 64) in;
layout(std430, binding = 8) readonly buffer SrcV { float src[]; };
layout(std430, binding = 9) readonly buffer SkinW { uint skin[]; };
layout(std430, binding = 10) readonly buffer Joints { mat4 joints[]; };
layout(std430, binding = 11) writeonly buffer DstV { float dst[]; };
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= uint(skin.length()) / 6u) return;
  uint s = i * 6u, b = i * 14u;
  uint j01 = skin[s], j23 = skin[s + 1u];
  uint n = uint(joints.length());
  uint j0 = min(j01 & 0xffffu, n - 1u), j1 = min(j01 >> 16, n - 1u), j2 = min(j23 & 0xffffu, n - 1u), j3 = min(j23 >> 16, n - 1u);
  vec4 w = vec4(uintBitsToFloat(skin[s + 2u]), uintBitsToFloat(skin[s + 3u]), uintBitsToFloat(skin[s + 4u]), uintBitsToFloat(skin[s + 5u]));
  mat4 m = joints[j0] * w.x + joints[j1] * w.y + joints[j2] * w.z + joints[j3] * w.w;
  vec3 p = (m * vec4(src[b], src[b + 1u], src[b + 2u], 1.0)).xyz;
  mat3 r = mat3(m);
  vec3 nrm = r * vec3(src[b + 3u], src[b + 4u], src[b + 5u]);
  nrm = dot(nrm, nrm) > 0.0 ? normalize(nrm) : vec3(0.0, 1.0, 0.0);
  vec3 t = r * vec3(src[b + 8u], src[b + 9u], src[b + 10u]);
  t = dot(t, t) > 0.0 ? normalize(t) : t;
  dst[b] = p.x; dst[b + 1u] = p.y; dst[b + 2u] = p.z;
  dst[b + 3u] = nrm.x; dst[b + 4u] = nrm.y; dst[b + 5u] = nrm.z;
  dst[b + 6u] = src[b + 6u]; dst[b + 7u] = src[b + 7u];
  dst[b + 8u] = t.x; dst[b + 9u] = t.y; dst[b + 10u] = t.z; dst[b + 11u] = src[b + 11u];
  dst[b + 12u] = src[b + 12u]; dst[b + 13u] = src[b + 13u];
}
)";

Renderer3D::SkinSource Renderer3D::CreateSkinSource(const MeshData& bind, const std::vector<SkinVertex>& skin) {
  SkinSource s;
  if (!rhi_ || bind.vertices.empty() || skin.size() != bind.vertices.size()) return s;
  static_assert(sizeof(Vertex3D) == 14 * sizeof(float), "the skinning shader reads 14 floats per vertex");
  std::vector<uint32_t> packed(skin.size() * 6);
  for (size_t i = 0; i < skin.size(); ++i) {
    uint32_t* o = &packed[i * 6];
    o[0] = (uint32_t)skin[i].joint[0] | ((uint32_t)skin[i].joint[1] << 16);
    o[1] = (uint32_t)skin[i].joint[2] | ((uint32_t)skin[i].joint[3] << 16);
    std::memcpy(o + 2, skin[i].weight, 4 * sizeof(float));
  }
  s.vertices = rhi_->CreateBuffer(BufferUsage::Storage, bind.vertices.data(), bind.vertices.size() * sizeof(Vertex3D));
  s.skin = rhi_->CreateBuffer(BufferUsage::Storage, packed.data(), packed.size() * sizeof(uint32_t));
  s.vertexCount = (uint32_t)bind.vertices.size();
  return s;
}

void Renderer3D::DestroySkinSource(SkinSource& s) {
  if (rhi_) {
    rhi_->DestroyBuffer(s.vertices);
    rhi_->DestroyBuffer(s.skin);
  }
  s = {};
}

Mesh3D Renderer3D::CreateSkinTarget(const MeshData& bind) {
  Mesh3D m = CreateMesh(bind);
  m.source.reset();      // the pose changes every frame: no lightmap unwrap or baked-GI cache for it
  m.contentHash = 0;
  return m;
}

bool Renderer3D::Skin(const SkinSource& s, BufferHandle joints, Mesh3D& target) {
  if (!rhi_ || !s.vertices.Valid() || !joints.Valid() || !target.vertices.Valid()) return false;
  if (!skin_.Valid()) {
    skin_ = rhi_->CreateComputePipeline(kSkinCS, "skinning");
    if (!skin_.Valid()) return false;
  }
  rhi_->BindStorageBuffer(8, s.vertices);
  rhi_->BindStorageBuffer(9, s.skin);
  rhi_->BindStorageBuffer(10, joints);
  rhi_->BindStorageBuffer(11, target.vertices);
  rhi_->Dispatch(skin_, (s.vertexCount + 63) / 64);
  skinnedVertices_ += s.vertexCount;
  return true;
}

void Renderer3D::SkinBarrier() {
  if (rhi_) rhi_->ComputeBarrier();
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

uint64_t Renderer3D::LightmapKey(const Mesh3D* mesh, const glm::mat4& model) {
  uint64_t key = 1469598103934665603ull ^ (uint64_t)(uintptr_t)mesh;
  const float* f = glm::value_ptr(model);
  for (int i = 0; i < 16; ++i) {
    uint32_t bits;
    std::memcpy(&bits, &f[i], 4);
    key = (key ^ bits) * 1099511628211ull;
  }
  return key;
}

uint64_t Renderer3D::ContentKey(const Mesh3D* mesh, const glm::mat4& model) {
  uint64_t key = (1469598103934665603ull ^ (mesh ? mesh->contentHash : 0)) * 1099511628211ull;
  const float* f = glm::value_ptr(model);
  for (int i = 0; i < 16; ++i) {
    uint32_t bits;
    std::memcpy(&bits, &f[i], 4);
    key = (key ^ bits) * 1099511628211ull;
  }
  return key;
}

void Renderer3D::Render(RenderTargetHandle rt, int32_t w, int32_t h, const Camera3D& camIn, const Light3D& light,
                        const std::vector<DrawItem3D>& itemsIn, Color clear, const RenderExtras* extras, const QualitySettings* quality) {
  if (!rhi_ || !rt.Valid() || w <= 0 || h <= 0) return;
  // Lightmapped items draw their own mesh copy (atlas coordinates) while mesh and placement match the bake.
  std::vector<DrawItem3D> swapped;
  std::vector<uint8_t> usesLightmap;
  const QualitySettings& tierIn = quality ? *quality : ReferenceQuality();
  const bool blobs = tierIn.blobShadows && light.shadows && light.intensity > 0 && tierIn.lighting != LightingMode::Unlit &&
                     !(tierIn.shadows && tierIn.lighting == LightingMode::Pixel);
  if (!lightmaps_.empty() || blobs) {
    swapped = itemsIn;
    usesLightmap.assign(swapped.size(), 0);
    for (size_t i = 0; i < swapped.size(); ++i) {
      if (!swapped[i].lightmapped) continue;
      auto found = lightmapIndex_.find(LightmapKey(swapped[i].mesh, swapped[i].model));
      if (found == lightmapIndex_.end()) continue;
      const LightmapInstance& li = lightmaps_[found->second];
      if (li.source != swapped[i].mesh || li.model != swapped[i].model) continue;
      swapped[i].mesh = &li.mesh;
      usesLightmap[i] = 1;
    }
  }
  size_t blobCount = 0;
  if (blobs) { // low tier (3-5b): a soft dark spot under each standing object, drawn with the blended objects
    for (const DrawItem3D& it : itemsIn) {
      if (!it.mesh || !it.mesh->indexCount || it.mesh->boundsMin == it.mesh->boundsMax || it.material.alpha == Material3D::Alpha::Blend) continue;
      glm::vec3 lo, hi;
      TransformBounds(it.model, it.mesh->boundsMin, it.mesh->boundsMax, lo, hi);
      const glm::vec3 size = hi - lo;
      const float wide = std::max(size.x, size.z);
      if (size.y < 0.1f * wide || wide > 8.f) continue; // floors and big flat things throw no blob
      DrawItem3D b;
      b.mesh = &blobQuad_;
      b.model = glm::scale(glm::translate(glm::mat4(1.f), glm::vec3((lo.x + hi.x) * 0.5f, lo.y + 0.01f, (lo.z + hi.z) * 0.5f)),
                           glm::vec3(std::max(size.x * 1.6f, 0.3f), 1, std::max(size.z * 1.6f, 0.3f)));
      b.material.unlit = true;
      b.material.alpha = Material3D::Alpha::Blend;
      b.material.baseColor = {0, 0, 0, 0.6f};
      b.material.baseMap = blobTex_;
      swapped.push_back(b);
      ++blobCount;
    }
    usesLightmap.resize(swapped.size(), 0);
  }
  const std::vector<DrawItem3D>& items = lightmaps_.empty() && !blobs ? itemsIn : swapped;
  QualitySettings q = quality ? *quality : ReferenceQuality();
  const DebugView view = extras ? extras->view : DebugView::Lit;
  if (view != DebugView::Lit) { // show the buffer itself: per-pixel shading, no tone curve or post effects
    q.lighting = LightingMode::Pixel;
    q.toneMap = q.bloom = q.fxaa = false;
    q.exposure = 1;
  }
  // Temporal anti-aliasing (round 6): on the lit view of a camera only (not captures or debug views); it replaces
  // MSAA and FXAA. The state (history, previous camera and placements) belongs to the output target.
  const bool taaOn = q.taa && view == DebugView::Lit && !(extras && extras->linearOutput);
  // Screen-space reflections (round 6): per-pixel lighting on a camera's lit view; they read the previous frame.
  const bool ssrOn = q.ssr && q.lighting == LightingMode::Pixel && view == DebugView::Lit && !(extras && extras->linearOutput);
  // Volumetric fog (round 6): where the post volumes ask for it, per-pixel tiers, not on captures or debug views.
  const bool volFogOn = q.volumetricFog && light.post.fogDensity > 0.f && q.lighting == LightingMode::Pixel && view == DebugView::Lit &&
                        !(extras && extras->linearOutput);
  if (taaOn) {
    q.msaa = 1;
    q.fxaa = false;
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
  const glm::mat4 vp0 = cam.Projection(aspect) * cam.View(); // without jitter (motion vectors, reprojection)
  glm::mat4 vp = vp0;
  Temporal* temporal = nullptr;
  glm::vec2 jitter(0.f);
  if (taaOn || ssrOn) {
    Temporal& t = temporal_[rt.id];
    temporal = &t;
    const glm::vec3 fwd = glm::normalize(cam.target - cam.position);
    // a camera cut (a jump, a sharp turn, another size) starts again without history
    if (t.w != w || t.h != h || glm::distance(t.prevPos, cam.position) > 2.f || glm::dot(t.prevFwd, fwd) < 0.85f) {
      t.valid = false;
      t.jitterIndex = 0;
    }
    if (taaOn) {
      jitter = {Halton(t.jitterIndex % 8 + 1, 2) - 0.5f, Halton(t.jitterIndex % 8 + 1, 3) - 0.5f}; // pixels
      glm::mat4 proj = cam.Projection(aspect);
      proj[2][0] -= jitter.x * 2.f / (float)w; // NDC offset = -proj[2][xy] (clip w = -view z)
      proj[2][1] -= jitter.y * 2.f / (float)h;
      vp = proj * cam.View();
    }
    if (t.w != w || t.h != h || t.taaTargets != taaOn) { // targets for this size and mode
      ReleaseTemporal(t);
      if (taaOn) {
        for (RenderTargetHandle& r : t.hist) r = rhi_->CreateRenderTargetEx(w, h, 1, true, false);
        t.out = rhi_->CreateRenderTargetEx(w, h, 1, true, false);
        t.velocity = rhi_->CreateRenderTargetEx(w, h, 1, true, true);
      } else {
        t.prevColor = rhi_->CreateRenderTargetEx(w, h, 1, true, false);
      }
      t.taaTargets = taaOn;
      t.w = w;
      t.h = h;
    }
  }
  const glm::mat4 ivp = glm::inverse(vp);
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
  // Per-pixel lights go to a storage buffer (up to the tier's limit); the first 16 also feed per-vertex lighting.
  const int locals = std::min({(int)light.locals.size(), kMaxClusteredLights, q.lighting == LightingMode::Unlit ? 0 : q.maxLights});
  const int vertexLights = std::min(locals, kMaxLocalLights);
  f.counts[0] = (float)vertexLights;
  f.counts[1] = (float)q.lighting; // 0 unlit, 1 per vertex, 2 per pixel
  f.counts[2] = q.textureBias;
  f.counts[3] = (float)view;
  LightsData ld{};
  for (int i = 0; i < vertexLights; ++i) {
    const LocalLight3D& l = light.locals[i];
    Put(ld.pos[i], l.position, l.range);
    Put(ld.color[i], l.color, l.intensity);
    Put(ld.dir[i], glm::normalize(l.direction), std::cos(glm::radians(std::max(l.outerAngle, 0.1f) * 0.5f)));
    ld.cone[i][0] = std::cos(glm::radians(std::clamp(l.innerAngle, 0.f, l.outerAngle - 0.01f) * 0.5f));
    ld.cone[i][1] = l.spot ? 1.f : 0.f;
  }
  rhi_->UpdateBuffer(lightsUbo_, &ld, sizeof(ld));
  // Point / spot shadows (3-4b): the shadow-casting lights whose range reaches the view, nearest the camera first, up
  // to the tier's limit, get tiles in one depth atlas (spot 1, point 6 cube faces); one light-space matrix per tile.
  std::vector<glm::mat4> tileVP;
  std::vector<float> lightTile((size_t)std::max(locals, 1), -1.f), lightOffset((size_t)std::max(locals, 1), 0.f);
  int localTile = 0, localCols = 0, localRows = 0, localSkipped = 0, localShadowed = 0;
  {
    const Frustum viewFrustum = Frustum::FromMatrix(vp);
    std::vector<int> cand;
    for (int i = 0; i < locals; ++i) {
      const LocalLight3D& l = light.locals[i];
      if (!l.castShadows || l.intensity <= 0 || l.range <= 0) continue;
      if (!viewFrustum.Intersects(l.position - glm::vec3(l.range), l.position + glm::vec3(l.range))) continue; // lights nothing on screen
      cand.push_back(i);
    }
    const int limit = q.lighting == LightingMode::Pixel ? std::max(q.localShadows, 0) : 0;
    auto reach = [&](int i) { return std::max(glm::distance(light.locals[i].position, cam.position) - light.locals[i].range, 0.f); };
    std::stable_sort(cand.begin(), cand.end(), [&](int a, int b) {
      const float ra = reach(a), rb = reach(b);
      return ra != rb ? ra < rb : glm::distance(light.locals[a].position, cam.position) < glm::distance(light.locals[b].position, cam.position);
    });
    if ((int)cand.size() > limit) {
      localSkipped = (int)cand.size() - limit;
      cand.resize(limit);
    }
    int tiles = 0;
    for (int i : cand) tiles += light.locals[i].spot ? 1 : 6;
    if (tiles > 0) {
      constexpr int kAtlasMax = 4096;
      localTile = std::clamp(q.localShadowSize, 64, kAtlasMax);
      while (localTile > 128 && (kAtlasMax / localTile) * (kAtlasMax / localTile) < tiles) localTile /= 2; // keep the atlas <= 4096^2
      localCols = std::min(tiles, kAtlasMax / localTile);
      localRows = (tiles + localCols - 1) / localCols;
      for (int i : cand) {
        const LocalLight3D& l = light.locals[i];
        const float nearZ = std::clamp(l.range * 0.005f, 0.02f, 0.2f);
        float fov = 0;
        lightTile[i] = (float)tileVP.size();
        if (l.spot) { // the cone plus a margin for the filter taps
          fov = std::min(std::max(l.outerAngle, 1.f) + 4.f, 170.f);
          const glm::vec3 d = glm::normalize(l.direction), up = std::fabs(d.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
          tileVP.push_back(glm::perspective(glm::radians(fov), 1.f, nearZ, l.range) * glm::lookAt(l.position, l.position + d, up));
        } else { // cube faces, a little wider than 90 degrees so the taps at a face's edge stay inside its tile
          fov = 2.f * glm::degrees(std::atan(1.f + 3.f / (float)localTile));
          static const glm::vec3 kDirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
          const glm::mat4 proj = glm::perspective(glm::radians(fov), 1.f, nearZ, l.range);
          for (const glm::vec3& d : kDirs)
            tileVP.push_back(proj * glm::lookAt(l.position, l.position + d, std::fabs(d.y) > 0.5f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0)));
        }
        lightOffset[i] = 1.5f * 2.f * std::tan(glm::radians(fov * 0.5f)) / (float)localTile; // 1.5 texels per metre of distance
      }
      localShadowed = (int)cand.size();
    }
    f.localShadow[0] = (float)localCols;
    f.localShadow[1] = (float)localRows;
    f.localShadow[2] = (float)localTile;
  }
  {
    std::vector<float> list((size_t)std::max(locals, 1) * 16, 0.f);
    for (int i = 0; i < locals; ++i) {
      const LocalLight3D& l = light.locals[i];
      float* o = &list[(size_t)i * 16];
      Put(o, l.position, l.range);
      Put(o + 4, l.color, l.intensity);
      Put(o + 8, glm::normalize(l.direction), std::cos(glm::radians(std::max(l.outerAngle, 0.1f) * 0.5f)));
      o[12] = std::cos(glm::radians(std::clamp(l.innerAngle, 0.f, l.outerAngle - 0.01f) * 0.5f));
      o[13] = l.spot ? 1.f : 0.f;
      o[14] = lightTile[i];   // first shadow atlas tile, -1 = no shadow (3-4b)
      o[15] = lightOffset[i]; // shadow normal offset per metre of distance
    }
    if (list.size() * 4 > lightCapacity_) {
      rhi_->DestroyBuffer(lightBuffer_);
      lightCapacity_ = list.size() * 4 * 2;
      lightBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, lightCapacity_, true);
    }
    rhi_->UpdateBuffer(lightBuffer_, list.data(), list.size() * 4);
  }
  // Forward+ (3-5b): each light's sphere is projected to a range of screen tiles and depth slices; the clusters in
  // that range list it. Two passes: count, then fill (first, count per cluster; then the indices).
  const bool clustered = q.clustered && q.lighting == LightingMode::Pixel && locals > 0;
  const float clusterNear = std::max(cam.nearZ, 0.05f), clusterFar = std::max(cam.farZ, clusterNear * 2);
  const float sliceScale = kClusterZ / std::log(clusterFar / clusterNear);
  f.cluster[0] = kClusterX;
  f.cluster[1] = kClusterY;
  f.cluster[2] = kClusterZ;
  f.cluster[3] = (float)locals;
  f.cluster2[0] = clusterNear;
  f.cluster2[1] = sliceScale;
  f.cluster2[2] = clustered ? 1.f : 0.f;
  if (light.fogDensity > 0 && !(extras && extras->linearOutput)) { // distance fog (World Settings, 2-6); not in captures
    f.fog[0] = light.fogColor.r;
    f.fog[1] = light.fogColor.g;
    f.fog[2] = light.fogColor.b;
    f.fog[3] = light.fogDensity;
    f.fog2[0] = std::max(light.fogStart, 0.f);
  }
  uint32_t clusterRefs = 0, clusterMax = 0, clustersUsed = 0;
  {
    constexpr int kClusters = kClusterX * kClusterY * kClusterZ;
    std::vector<uint32_t> cl(kClusters * 2, 0);
    if (clustered) {
      struct Range { int x0, x1, y0, y1, z0, z1; };
      std::vector<Range> ranges;
      std::vector<int> owner;
      const glm::mat4 viewM = cam.View(), proj = cam.Projection(aspect);
      auto slice = [&](float z) { return std::clamp((int)std::floor(std::log(std::max(z, clusterNear) / clusterNear) * sliceScale), 0, kClusterZ - 1); };
      for (int i = 0; i < locals; ++i) {
        const LocalLight3D& l = light.locals[i];
        const glm::vec3 c = glm::vec3(viewM * glm::vec4(l.position, 1));
        const float d = -c.z, r = std::max(l.range, 1e-3f);
        if (d + r < clusterNear || d - r > clusterFar) continue;
        Range g{0, kClusterX - 1, 0, kClusterY - 1, slice(d - r), slice(d + r)};
        if (d - r > clusterNear) { // wholly in front: the screen rectangle of its box
          glm::vec2 lo(1e9f), hi(-1e9f);
          for (int k = 0; k < 8; ++k) {
            glm::vec4 p = proj * glm::vec4(c + glm::vec3((k & 1) ? r : -r, (k & 2) ? r : -r, (k & 4) ? r : -r), 1);
            glm::vec2 ndc = glm::vec2(p) / p.w;
            lo = glm::min(lo, ndc);
            hi = glm::max(hi, ndc);
          }
          if (lo.x > 1 || lo.y > 1 || hi.x < -1 || hi.y < -1) continue; // off screen
          g.x0 = std::clamp((int)std::floor((lo.x * 0.5f + 0.5f) * kClusterX), 0, kClusterX - 1);
          g.x1 = std::clamp((int)std::floor((hi.x * 0.5f + 0.5f) * kClusterX), 0, kClusterX - 1);
          g.y0 = std::clamp((int)std::floor((lo.y * 0.5f + 0.5f) * kClusterY), 0, kClusterY - 1);
          g.y1 = std::clamp((int)std::floor((hi.y * 0.5f + 0.5f) * kClusterY), 0, kClusterY - 1);
        }
        ranges.push_back(g);
        owner.push_back(i);
      }
      auto each = [&](const Range& g, auto&& fn) {
        for (int z = g.z0; z <= g.z1; ++z)
          for (int y = g.y0; y <= g.y1; ++y)
            for (int x = g.x0; x <= g.x1; ++x) fn((z * kClusterY + y) * kClusterX + x);
      };
      for (const Range& g : ranges) each(g, [&](int c) { ++cl[c * 2 + 1]; });
      uint32_t at = kClusters * 2;
      for (int c = 0; c < kClusters; ++c) {
        cl[c * 2] = at;
        at += cl[c * 2 + 1];
        clusterMax = std::max(clusterMax, cl[c * 2 + 1]);
        clustersUsed += cl[c * 2 + 1] ? 1 : 0;
      }
      clusterRefs = at - kClusters * 2;
      cl.resize(at);
      std::vector<uint32_t> fill(kClusters, 0);
      for (size_t k = 0; k < ranges.size(); ++k)
        each(ranges[k], [&](int c) { cl[cl[c * 2] + fill[c]++] = (uint32_t)owner[k]; });
    }
    if (cl.size() * 4 > clusterCapacity_) {
      rhi_->DestroyBuffer(clusterBuffer_);
      clusterCapacity_ = cl.size() * 4 * 2;
      clusterBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, clusterCapacity_, true);
    }
    rhi_->UpdateBuffer(clusterBuffer_, cl.data(), cl.size() * 4);
  }
  Put(f.camFwd, glm::normalize(cam.target - cam.position));
  std::memcpy(f.prevViewProj, glm::value_ptr(temporal && temporal->valid ? temporal->prevVP : vp0), sizeof(f.prevViewProj));
  f.ssr[0] = ssrOn && temporal && temporal->valid ? 1.f : 0.f;
  f.ssr[1] = std::max(q.ssrDistance, 1.f);
  f.ssr[2] = 0.35f; // thickness of what the depth buffer shows (m)
  f.ssr[3] = (float)std::clamp(q.ssrSteps, 8, 128);
  f.screen[0] = (float)w;
  f.screen[1] = (float)h;
  f.screen[2] = 1.f / w;
  f.screen[3] = 1.f / h;
  const bool ssaoOn = q.ssao && q.lighting == LightingMode::Pixel;
  f.flags[0] = ssaoOn ? 1.f : 0.f;
  // depth of field (E-6b) needs the depth / normal prepass too; only on the lit view, never on captures
  const bool dofOn = light.post.dofBlur > 0.001f && !(extras && extras->linearOutput) && (!extras || extras->view == DebugView::Lit);
  // light shafts (E-7): where the sun is on the picture; faded out as it leaves the screen, off behind the camera
  float sunU = 0, sunV = 0, shaftFade = 0;
  if (light.shafts > 0 && light.intensity > 0 && q.lighting == LightingMode::Pixel && !(extras && extras->linearOutput) &&
      (!extras || extras->view == DebugView::Lit)) {
    const glm::vec4 clip = cam.Projection((float)w / std::max(h, 1)) * cam.View() *
                           glm::vec4(cam.position + glm::normalize(light.toLight) * std::max(cam.farZ * 0.5f, 10.f), 1.f);
    if (clip.w > 0) {
      sunU = clip.x / clip.w * 0.5f + 0.5f;
      sunV = clip.y / clip.w * 0.5f + 0.5f;
      const float off = std::max(std::abs(sunU - 0.5f), std::abs(sunV - 0.5f)); // 0.5 = on the edge
      shaftFade = 1.f - std::clamp((off - 0.5f) / 0.5f, 0.f, 1.f);
    }
  }
  const bool shaftsOn = shaftFade > 0.001f;

  const int cascades = std::clamp(q.shadowCascades, 1, 4);
  if (light.shadows && q.shadows && q.lighting == LightingMode::Pixel && light.intensity > 0) {
    // Cascades (3-5b): view depth split between uniform and logarithmic (practical split, lambda 0.8); more cascades
    // also reach further (2.5x). Each cascade is fitted to its slice, starting a blend band early.
    const float farZ = std::min(cam.farZ, light.shadowDistance * q.viewDistance * (cascades > 1 ? 2.5f : 1.f));
    const float nearZ = std::max(cam.nearZ, 0.05f), band = 0.1f;
    float prev = 0;
    for (int c = 0; c < cascades; ++c) {
      const float k = (float)(c + 1) / cascades;
      const float split = c == cascades - 1 ? farZ : 0.8f * nearZ * std::pow(farZ / nearZ, k) + 0.2f * (nearZ + (farZ - nearZ) * k);
      float texelWorld = 0, depthRange = 1;
      cascadeVP_[c] = FitShadow(cam, aspect, light.toLight, prev * (1.f - band), split, q.shadowMapSize, texelWorld, depthRange);
      std::memcpy(f.cascadeVP[c], glm::value_ptr(cascadeVP_[c]), sizeof(f.cascadeVP[c]));
      f.cascadeSplit[c] = split;
      f.cascadeInfo[c][0] = 0.02f / depthRange;  // 2 cm in depth
      f.cascadeInfo[c][1] = texelWorld * 1.5f;   // normal offset
      f.cascadeInfo[c][2] = 1.f / texelWorld;    // texels per metre (softness)
      prev = split;
    }
    shadowMatrix_ = cascadeVP_[0];
    std::memcpy(f.lightViewProj, glm::value_ptr(shadowMatrix_), sizeof(f.lightViewProj));
    f.shadow[0] = 1.f;
    f.shadow[1] = f.cascadeInfo[0][0];
    f.shadow[2] = f.cascadeInfo[0][1];
    f.shadow[3] = (float)q.shadowMapSize;
    f.shadow2[0] = (float)cascades;
    f.shadow2[1] = q.shadowSoftness;
    f.shadow2[2] = band;
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
  std::vector<Run> shadowRuns[4];
  size_t shadowCulled = 0;
  if (f.shadow[0] > 0.5f) {
    for (int c = 0; c < cascades; ++c) {
      const Frustum lightFrustum = Frustum::FromMatrix(cascadeVP_[c]);
      std::vector<size_t> casters;
      for (size_t i = 0; i < items.size(); ++i) {
        if (!drawable[i] || items[i].material.alpha == Material3D::Alpha::Blend) continue;
        if (inside(lightFrustum, i)) casters.push_back(i);
        else ++shadowCulled;
      }
      std::stable_sort(casters.begin(), casters.end(), [&](size_t x, size_t y) { return shadowKey(x) < shadowKey(y); });
      shadowRuns[c] = buildRuns(casters, shadowKey, true);
    }
  }
  // point / spot shadow tiles (3-4b): opaque and masked objects inside each tile's frustum (cone or cube face)
  std::vector<std::vector<Run>> localRuns(tileVP.size());
  for (size_t t = 0; t < tileVP.size(); ++t) {
    const Frustum tileFrustum = Frustum::FromMatrix(tileVP[t]);
    std::vector<size_t> casters;
    for (size_t i = 0; i < items.size(); ++i)
      if (drawable[i] && items[i].material.alpha != Material3D::Alpha::Blend && inside(tileFrustum, i)) casters.push_back(i);
    std::stable_sort(casters.begin(), casters.end(), [&](size_t x, size_t y) { return shadowKey(x) < shadowKey(y); });
    localRuns[t] = buildRuns(casters, shadowKey, true);
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
    o.lightmap[0] = usesLightmap.empty() ? 0.f : (float)usesLightmap[k];
    glm::vec3 c = (worldLo[k] + worldHi[k]) * 0.5f; // GI Tier 0: the baked probe whose box holds the centre, else the sky
    for (int pi = 0; pi < bakedProbes_ && o.extra[3] == 0; ++pi)
      if (glm::all(glm::greaterThanEqual(c, probes_[pi].boxMin)) && glm::all(glm::lessThanEqual(c, probes_[pi].boxMax)))
        o.extra[3] = (float)(pi + 1);
  }
  if (!objects.empty()) rhi_->UpdateBuffer(objectBuffer_, objects.data(), objects.size() * sizeof(ObjectData));
  rhi_->BindStorageBuffer(0, objectBuffer_);
  auto drawRun = [&](PipelineHandle p, const Run& r) {
    const Mesh3D& mesh = *items[r.item].mesh;
    rhi_->DrawIndexedInstanced(p, mesh.vertices, mesh.indices, mesh.indexCount, r.count, r.first, instanceIds_);
    ++draws_;
  };
  // TAA: each item's placement on the previous frame (found by mesh and occurrence: the same object keeps its place in
  // the list from frame to frame), for the motion vectors; this frame's placements are kept for the next one.
  std::vector<glm::mat4> prevModels;
  std::unordered_map<uint64_t, glm::mat4> nextModels;
  if (temporal && taaOn) {
    prevModels.resize(std::max<size_t>(items.size(), 1), glm::mat4(1.f));
    std::unordered_map<const Mesh3D*, uint32_t> seen;
    for (size_t i = 0; i < items.size(); ++i) {
      const uint64_t key = ((uint64_t)(uintptr_t)items[i].mesh * 1099511628211ull) ^ ((uint64_t)seen[items[i].mesh]++ * 0x9E3779B97F4A7C15ull);
      auto it = temporal->prevModels.find(key);
      prevModels[i] = it != temporal->prevModels.end() ? it->second : items[i].model;
      nextModels[key] = items[i].model;
    }
    if (prevModels.size() * sizeof(glm::mat4) > prevModelCapacity_) {
      rhi_->DestroyBuffer(prevModelBuffer_);
      prevModelCapacity_ = prevModels.size() * sizeof(glm::mat4) * 2;
      prevModelBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, prevModelCapacity_, true);
    }
    rhi_->UpdateBuffer(prevModelBuffer_, prevModels.data(), prevModels.size() * sizeof(glm::mat4));
    TemporalData td{};
    std::memcpy(td.curVP, glm::value_ptr(vp0), sizeof(td.curVP));
    std::memcpy(td.prevVP, glm::value_ptr(temporal->valid ? temporal->prevVP : vp0), sizeof(td.prevVP));
    std::memcpy(td.jitVP, glm::value_ptr(vp), sizeof(td.jitVP));
    const glm::mat4 inv0 = glm::inverse(vp0);
    std::memcpy(td.invCurVP, glm::value_ptr(inv0), sizeof(td.invCurVP));
    td.taa[2] = std::clamp(q.taaFeedback, 0.f, 0.98f);
    td.taa[3] = temporal->valid ? 1.f : 0.f;
    td.taa2[0] = std::clamp(q.taaSharpen, 0.f, 1.f);
    rhi_->UpdateBuffer(temporalUbo_, &td, sizeof(td));
    rhi_->BindUniformBuffer(9, temporalUbo_);
    rhi_->BindStorageBuffer(6, prevModelBuffer_);
    rhi_->BeginRenderTarget(temporal->velocity, {0, 0, 0, 0}); // motion vectors of the opaque surfaces
    for (size_t r = 0; r < opaqueRuns; ++r) drawRun(velocity_, runs[r]);
    rhi_->EndRenderTarget();
  }
  const float aniso = std::min((float)std::max(q.anisotropy, 1), rhi_->MaxAnisotropy());
  auto bindMaps = [&](const Material3D& m) {
    const TextureHandle maps[5] = {m.baseMap, m.metalRoughMap, m.normalMap, m.occlusionMap, m.emissiveMap};
    for (uint32_t slot = 0; slot < 5; ++slot) {
      if (maps[slot].Valid()) rhi_->SetTextureAnisotropy(maps[slot], aniso); // 3-5b
      rhi_->BindTexture(slot, maps[slot].Valid() ? maps[slot] : white_);
    }
  };

  rhi_->BindUniformBuffer(1, frameUbo_);
  rhi_->BindUniformBuffer(3, lightsUbo_);
  { // the sky probe follows the sky (GI Tier 0)
    uint64_t key = 1469598103934665603ull;
    for (const float* v : {f.zenith, f.horizon, f.ground})
      for (int k = 0; k < 3; ++k) { uint32_t bits; std::memcpy(&bits, &v[k], 4); key = (key ^ bits) * 1099511628211ull; }
    if (key != skyKey_) {
      skyKey_ = key;
      BakeSky();
    }
  }
  rhi_->BindUniformBuffer(5, probesUbo_);
  rhi_->BindTexture(6, envArray_);
  rhi_->BindStorageBuffer(1, volumeBuffer_);
  rhi_->BindStorageBuffer(2, lightBuffer_);
  rhi_->BindStorageBuffer(3, clusterBuffer_);
  rhi_->BindTexture(7, lightmapTex_.Valid() ? lightmapTex_ : white_);
  if (f.shadow[0] > 0.5f) { // shadow map: every opaque and masked item, depth only
    if (!shadowRT_.Valid() || shadowSize_ != q.shadowMapSize || shadowTiles_ != cascades) { // one tile per cascade
      rhi_->DestroyRenderTarget(shadowRT_);
      shadowRT_ = rhi_->CreateDepthTarget(q.shadowMapSize * cascades, q.shadowMapSize);
      shadowSize_ = q.shadowMapSize;
      shadowTiles_ = cascades;
    }
    rhi_->BeginRenderTarget(shadowRT_, {0, 0, 0, 0});
    for (int c = 0; c < cascades; ++c) {
      rhi_->SetViewport(c * q.shadowMapSize, 0, q.shadowMapSize, q.shadowMapSize);
      rhi_->UpdateBuffer(shadowPassUbo_, glm::value_ptr(cascadeVP_[c]), sizeof(float) * 16);
      rhi_->BindUniformBuffer(2, shadowPassUbo_);
      for (const Run& r : shadowRuns[c]) { // alike casters in one draw too
        const Material3D& m = items[r.item].material;
        rhi_->BindTexture(0, m.baseMap.Valid() ? m.baseMap : white_);
        drawRun(shadowPipe_, r);
        stats_.shadowCasters += r.count;
      }
    }
    rhi_->EndRenderTarget();
  }
  uint32_t localCasters = 0;
  if (!tileVP.empty()) { // point / spot shadow atlas (3-4b)
    const int32_t aw = localCols * localTile, ah = localRows * localTile;
    if (!localShadowRT_.Valid() || localShadowW_ != aw || localShadowH_ != ah) {
      rhi_->DestroyRenderTarget(localShadowRT_);
      localShadowRT_ = rhi_->CreateDepthTarget(aw, ah);
      localShadowW_ = aw;
      localShadowH_ = ah;
    }
    rhi_->BeginRenderTarget(localShadowRT_, {0, 0, 0, 0});
    for (size_t t = 0; t < tileVP.size(); ++t) {
      rhi_->SetViewport((int32_t)(t % localCols) * localTile, (int32_t)(t / localCols) * localTile, localTile, localTile);
      rhi_->UpdateBuffer(shadowPassUbo_, glm::value_ptr(tileVP[t]), sizeof(float) * 16);
      rhi_->BindUniformBuffer(2, shadowPassUbo_);
      for (const Run& r : localRuns[t]) {
        const Material3D& m = items[r.item].material;
        rhi_->BindTexture(0, m.baseMap.Valid() ? m.baseMap : white_);
        drawRun(shadowPipe_, r);
        localCasters += r.count;
      }
    }
    rhi_->EndRenderTarget();
  }
  {
    const size_t bytes = std::max<size_t>(tileVP.size(), 1) * sizeof(glm::mat4);
    if (bytes > localShadowCapacity_) {
      rhi_->DestroyBuffer(localShadowBuffer_);
      localShadowCapacity_ = bytes * 2;
      localShadowBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, localShadowCapacity_, true);
    }
    if (!tileVP.empty()) rhi_->UpdateBuffer(localShadowBuffer_, tileVP.data(), bytes);
    rhi_->BindStorageBuffer(4, localShadowBuffer_);
  }
  if (timing && (f.shadow[0] > 0.5f || !tileVP.empty())) Mark(timer, "shadow");
  rhi_->BindTexture(5, rhi_->RenderTargetDepth(f.shadow[0] > 0.5f ? shadowRT_ : noShadowRT_));
  rhi_->BindTexture(9, rhi_->RenderTargetDepth(!tileVP.empty() ? localShadowRT_ : noShadowRT_));
  stats_ = RenderStats{w, h, std::clamp(q.msaa, 1, 8), f.shadow[0] > 0.5f ? q.shadowMapSize : 0, locals, stats_.shadowCasters, 0, 0,
                       q.toneMap, q.fxaa, q.bloom, view, stats_.passes};
  stats_.shafts = shaftsOn; // light shafts (E-7)
  stats_.sunU = sunU;
  stats_.sunV = sunV;
  stats_.cascades = f.shadow[0] > 0.5f ? cascades : 0;
  stats_.clustered = clustered;
  stats_.anisotropy = (int)aniso;
  stats_.blobs = blobCount;
  stats_.shadowCasters += localCasters;
  stats_.localShadows = localShadowed;
  stats_.localShadowTiles = (int)tileVP.size();
  stats_.localShadowTile = localTile;
  stats_.localShadowSkipped = localSkipped;
  stats_.clusterRefs = clusterRefs;
  stats_.clusterMax = clusterMax;
  stats_.clustersUsed = clustersUsed;
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
  if (ssaoOn || dofOn || shaftsOn || ssrOn || volFogOn) { // SSAO (3-5b): normals and depth of the opaque objects, occlusion at half size, blurred twice
    const int aw = std::max(w / 2, 1), ah = std::max(h / 2, 1);
    if (!geoRT_.Valid() || geoW_ != w || geoH_ != h) {
      for (RenderTargetHandle t : {geoRT_, aoRT_[0], aoRT_[1]}) rhi_->DestroyRenderTarget(t);
      geoRT_ = rhi_->CreateRenderTargetEx(w, h, 1, true, true);
      aoRT_[0] = rhi_->CreateRenderTargetEx(aw, ah, 1, false, false);
      aoRT_[1] = rhi_->CreateRenderTargetEx(aw, ah, 1, false, false);
      geoW_ = w;
      geoH_ = h;
    }
    rhi_->BeginRenderTarget(geoRT_, {0, 0, 0, 65000.f});
    for (size_t r = 0; r < opaqueRuns; ++r) {
      const Material3D& m = items[runs[r].item].material;
      rhi_->BindTexture(0, m.baseMap.Valid() ? m.baseMap : white_);
      drawRun(m.doubleSided ? prepassTwo_ : prepass_, runs[r]);
    }
    rhi_->EndRenderTarget();
    ++draws_;
  }
  if (ssaoOn) {
    SsaoData sd{{q.ssaoRadius, q.ssaoIntensity, 0, 0}, {0, 0, 0, 0}};
    rhi_->UpdateBuffer(ssaoUbo_, &sd, sizeof(sd));
    rhi_->BindUniformBuffer(8, ssaoUbo_);
    rhi_->BeginRenderTarget(aoRT_[0], {1, 1, 1, 1});
    rhi_->BindTexture(0, rhi_->RenderTargetColor(geoRT_));
    rhi_->Draw(ssao_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    for (int pass = 0; pass < 2; ++pass) {
      SsaoData bd{{q.ssaoRadius, q.ssaoIntensity, 0, 0}, {pass == 0 ? 1.f : 0.f, pass == 0 ? 0.f : 1.f, 0, 0}};
      rhi_->UpdateBuffer(ssaoUbo_, &bd, sizeof(bd));
      rhi_->BeginRenderTarget(aoRT_[1 - pass], {1, 1, 1, 1});
      rhi_->BindTexture(0, rhi_->RenderTargetColor(aoRT_[pass]));
      rhi_->BindTexture(1, rhi_->RenderTargetColor(geoRT_));
      rhi_->Draw(aoBlur_, fullscreen_, 3);
      rhi_->EndRenderTarget();
    }
    draws_ += 4;
    if (timing) Mark(timer, "ssao");
  }
  rhi_->BindTexture(8, ssaoOn ? rhi_->RenderTargetColor(aoRT_[0]) : white_);
  if (ssrOn) { // SSR: this frame's depth / normals, the previous frame's picture
    rhi_->BindTexture(10, rhi_->RenderTargetColor(geoRT_));
    rhi_->BindTexture(11, rhi_->RenderTargetColor(taaOn ? temporal->hist[temporal->cur] : temporal->prevColor));
  } else {
    rhi_->BindTexture(10, white_);
    rhi_->BindTexture(11, white_);
  }
  stats_.ssao = ssaoOn;
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
  stats_.lightmapped = 0;
  for (size_t i : opaqueList) stats_.lightmapped += usesLightmap.empty() ? 0 : usesLightmap[i];
  if (extras && extras->transparent) extras->transparent(cam.View(), cam.Projection(aspect)); // particles (6-VFX)
  rhi_->EndRenderTarget(); // resolves MSAA
  if (timing) Mark(timer, "scene");

  // Bloom: blur the bright parts over a chain of half-size targets.
  TextureHandle hdr = rhi_->RenderTargetColor(hdrRT_);
  stats_.taa = taaOn;
  stats_.taaHistory = temporal && temporal->valid;
  stats_.jitterX = jitter.x;
  stats_.jitterY = jitter.y;
  stats_.ssr = ssrOn && f.ssr[0] > 0.5f;
  stats_.volumetricFog = volFogOn;
  if (volFogOn) { // froxels: light them, integrate front to back, then fog each pixel by its depth
    const PostEffects3D& fx = light.post;
    const int gx = std::clamp(q.fogGridX, 8, 256), gy = std::clamp(q.fogGridY, 8, 256), gz = std::clamp(q.fogSlices, 8, 128);
    const size_t bytes = (size_t)gx * gy * gz * 16;
    if (bytes > fogCapacity_) {
      rhi_->DestroyBuffer(fogMedia_);
      rhi_->DestroyBuffer(fogAccum_);
      fogMedia_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, bytes, false);
      fogAccum_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, bytes, false);
      fogCapacity_ = bytes;
    }
    FogData fd{};
    fd.grid[0] = (float)gx;
    fd.grid[1] = (float)gy;
    fd.grid[2] = (float)gz;
    fd.grid[3] = (float)vertexLights;
    fd.a[0] = std::max(cam.nearZ, 0.1f);
    fd.a[1] = std::clamp(fx.fogDistance, fd.a[0] * 4.f, std::max(cam.farZ, fd.a[0] * 4.f));
    fd.a[2] = fx.fogDensity;
    fd.a[3] = fx.fogHeight;
    fd.b[0] = fx.fogFalloff;
    fd.b[1] = std::clamp(fx.fogAnisotropy, -0.95f, 0.95f);
    fd.b[2] = fx.fogLight;
    fd.b[3] = temporal && taaOn ? Halton(temporal->jitterIndex % 16 + 1, 5) : 0.5f; // slice jitter, smoothed by TAA
    fd.c[0] = fx.fogAlbedo.r;
    fd.c[1] = fx.fogAlbedo.g;
    fd.c[2] = fx.fogAlbedo.b;
    const glm::mat4 inv0 = glm::inverse(vp0);
    std::memcpy(fd.invVP, glm::value_ptr(inv0), sizeof(fd.invVP));
    rhi_->UpdateBuffer(fogUbo_, &fd, sizeof(fd));
    rhi_->BindUniformBuffer(10, fogUbo_);
    rhi_->BindUniformBuffer(1, frameUbo_);
    rhi_->BindStorageBuffer(2, lightBuffer_);
    rhi_->BindStorageBuffer(12, fogMedia_);
    rhi_->BindStorageBuffer(13, fogAccum_);
    rhi_->BindTexture(5, rhi_->RenderTargetDepth(f.shadow[0] > 0.5f ? shadowRT_ : noShadowRT_));
    rhi_->Dispatch(fogLight_, (uint32_t)(gx + 3) / 4, (uint32_t)(gy + 3) / 4, (uint32_t)(gz + 3) / 4);
    rhi_->ComputeBarrier();
    rhi_->Dispatch(fogIntegrate_, (uint32_t)(gx + 7) / 8, (uint32_t)(gy + 7) / 8, 1);
    rhi_->ComputeBarrier();
    if (!fogRT_.Valid() || fogW_ != w || fogH_ != h) {
      rhi_->DestroyRenderTarget(fogRT_);
      fogRT_ = rhi_->CreateRenderTargetEx(w, h, 1, true, false);
      fogW_ = w;
      fogH_ = h;
    }
    rhi_->BeginRenderTarget(fogRT_, {0, 0, 0, 1});
    rhi_->BindTexture(0, hdr);
    rhi_->BindTexture(1, rhi_->RenderTargetColor(geoRT_));
    rhi_->Draw(fogApply_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    ++draws_;
    hdr = rhi_->RenderTargetColor(fogRT_);
    if (timing) Mark(timer, "fog");
  }
  if (taaOn) { // TAA resolve into the next history, then sharpen into the picture the post passes read
    Temporal& t = *temporal;
    const int next = 1 - t.cur;
    rhi_->BindUniformBuffer(9, temporalUbo_);
    rhi_->BeginRenderTarget(t.hist[next], {0, 0, 0, 1});
    rhi_->BindTexture(0, hdr);
    rhi_->BindTexture(1, rhi_->RenderTargetColor(t.hist[t.cur]));
    rhi_->BindTexture(2, rhi_->RenderTargetColor(t.velocity));
    rhi_->Draw(taa_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    rhi_->BeginRenderTarget(t.out, {0, 0, 0, 1});
    rhi_->BindTexture(0, rhi_->RenderTargetColor(t.hist[next]));
    rhi_->Draw(sharpen_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    draws_ += 2;
    hdr = rhi_->RenderTargetColor(t.out);
    t.cur = next;
    t.prevModels = std::move(nextModels);
    ++t.jitterIndex;
    if (timing) Mark(timer, "taa");
  } else if (ssrOn) { // keep this frame's picture for the next frame's reflections
    rhi_->BeginRenderTarget(temporal->prevColor, {0, 0, 0, 1});
    rhi_->BindTexture(0, hdr);
    rhi_->Draw(copy_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    ++draws_;
  }
  if (temporal) {
    temporal->valid = true;
    temporal->prevVP = vp0;
    temporal->prevPos = cam.position;
    temporal->prevFwd = glm::normalize(cam.target - cam.position);
  }
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
  const bool grading = gradingLut_.Valid() && !(extras && extras->linearOutput) && view == DebugView::Lit;
  // Post-process volumes (E-6): not on captures (lighting, not the view) or debug views
  const bool camFx = !(extras && extras->linearOutput) && view == DebugView::Lit;
  const PostEffects3D fx = camFx ? light.post : PostEffects3D{};
  ++postFrame_;
  PostData pd{{q.exposure * std::max(light.exposure, 0.f) * std::exp2(fx.exposureEV), q.bloomIntensity * std::max(fx.bloomScale, 0.f),
               q.toneMap ? 1.f : 0.f, q.bloom ? 1.f : 0.f},
              {extras && extras->linearOutput ? 1.f : 0.f, grading ? 1.f : 0.f, 0, 0},
              {std::clamp(fx.vignette, 0.f, 1.f), std::clamp(fx.vignetteSmooth, 0.f, 1.f), std::clamp(fx.aberration, 0.f, 1.f), std::clamp(fx.grain, 0.f, 1.f)},
              {fx.autoExposure ? 1.f : 0.f, std::min(fx.autoMinEV, fx.autoMaxEV), std::max(fx.autoMinEV, fx.autoMaxEV), (float)(postFrame_ % 997)},
              {0, 0, (float)w / (float)std::max(h, 1), 0},
              {std::max(fx.dofFocus, 0.05f), std::max(fx.dofRange, 0.f), std::clamp(fx.dofBlur, 0.f, 1.f) * 14.f * std::max(h, 1) / 1080.f,
               dofOn && camFx ? 1.f : 0.f},
              {std::clamp(fx.lensFlare, 0.f, 1.f), 0, 0, 0},
              {sunU, sunV, light.shafts * shaftFade, shaftsOn ? 1.f : 0.f},
              {light.color.r * light.intensity, light.color.g * light.intensity, light.color.b * light.intensity, 0}};
  if (shaftsOn) { // god rays at half size (E-7)
    const int sw = std::max(w / 2, 1), sh = std::max(h / 2, 1);
    if (!shaftRT_.Valid() || shaftW_ != sw || shaftH_ != sh) {
      rhi_->DestroyRenderTarget(shaftRT_);
      shaftRT_ = rhi_->CreateRenderTargetEx(sw, sh, 1, true, false);
      shaftW_ = sw;
      shaftH_ = sh;
    }
    rhi_->UpdateBuffer(postUbo_, &pd, sizeof(pd));
    rhi_->BindUniformBuffer(4, postUbo_);
    rhi_->BeginRenderTarget(shaftRT_, {0, 0, 0, 1});
    rhi_->BindTexture(0, rhi_->RenderTargetColor(geoRT_));
    rhi_->Draw(shafts_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    ++draws_;
    if (timing) Mark(timer, "shafts");
  }
  if (dofOn && camFx) { // blur by depth into a copy the tone map then reads
    if (!dofRT_.Valid() || dofW_ != w || dofH_ != h) {
      rhi_->DestroyRenderTarget(dofRT_);
      dofRT_ = rhi_->CreateRenderTargetEx(w, h, 1, true, false);
      dofW_ = w;
      dofH_ = h;
    }
    rhi_->UpdateBuffer(postUbo_, &pd, sizeof(pd));
    rhi_->BindUniformBuffer(4, postUbo_);
    rhi_->BeginRenderTarget(dofRT_, {0, 0, 0, 1});
    rhi_->BindTexture(0, hdr);
    rhi_->BindTexture(1, rhi_->RenderTargetColor(geoRT_));
    rhi_->Draw(dof_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    ++draws_;
    hdr = rhi_->RenderTargetColor(dofRT_);
    if (timing) Mark(timer, "dof");
  }
  TextureHandle adapted = white_;
  if (fx.autoExposure) { // average log luminance now, then step the adapted value toward it
    if (!lumRT_.Valid()) lumRT_ = rhi_->CreateRenderTargetEx(32, 32, 1, true, false);
    Adapt& a = adapt_s[rt.id];
    if (!a.rt[0].Valid())
      for (RenderTargetHandle& r : a.rt) r = rhi_->CreateRenderTargetEx(1, 1, 1, true, false);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double dt = a.primed ? std::clamp(now - a.last, 0.0, 0.5) : 0.0;
    a.last = now;
    pd.post5[0] = (float)(1.0 - std::exp(-dt * std::max(fx.adaptSpeed, 0.01f)));
    pd.post5[1] = a.primed ? 0.f : 1.f;
    a.primed = true;
    rhi_->UpdateBuffer(postUbo_, &pd, sizeof(pd));
    rhi_->BindUniformBuffer(4, postUbo_);
    rhi_->BeginRenderTarget(lumRT_, {0, 0, 0, 1});
    rhi_->BindTexture(0, hdr);
    rhi_->Draw(lum_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    const int next = 1 - a.cur;
    rhi_->BeginRenderTarget(a.rt[next], {0, 0, 0, 1});
    rhi_->BindTexture(0, rhi_->RenderTargetColor(lumRT_));
    rhi_->BindTexture(1, rhi_->RenderTargetColor(a.rt[a.cur]));
    rhi_->Draw(adapt_, fullscreen_, 3);
    rhi_->EndRenderTarget();
    a.cur = next;
    adapted = rhi_->RenderTargetColor(a.rt[a.cur]);
    draws_ += 2;
  } else if (auto it = adapt_s.find(rt.id); it != adapt_s.end()) {
    it->second.primed = false; // turned off: start fresh next time
  }
  rhi_->BindTexture(3, adapted);
  rhi_->BindTexture(4, shaftsOn ? rhi_->RenderTargetColor(shaftRT_) : white_);
  stats_.grading = grading;
  if (grading) rhi_->BindTexture(2, gradingLut_);
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

// ---- Image-based lighting (GI Tier 0) ----
void Renderer3D::PrefilterInto(int layer) {
  rhi_->BindTexture(0, scratchCube_);
  for (int level = 0; level < kEnvLevels; ++level)
    for (int face = 0; face < 6; ++face) {
      PrefilterData pd{{(float)face, (float)level / (kEnvLevels - 1), (float)kEnvSize, (float)std::max(kEnvSize >> level, 1)}};
      rhi_->UpdateBuffer(prefilterUbo_, &pd, sizeof(pd));
      rhi_->BindUniformBuffer(6, prefilterUbo_);
      rhi_->BeginTextureTarget(envArray_, level, layer * 6 + face);
      rhi_->Draw(prefilter_, fullscreen_, 3);
      rhi_->EndTextureTarget();
    }
}

void Renderer3D::ComputeSH(int probe) { ComputeSHFrom(scratchCube_, 3, probeSH_[probe]); }

bool Renderer3D::ComputeSHFrom(TextureHandle cube, int level, float out[9][3]) {
  // Project a small mip of the cube onto 9 spherical harmonics, then convolve with the cosine lobe and divide by pi:
  // the shader's ProbeIrradiance(n) is then the ambient term for a diffuse surface facing n.
  std::vector<float> px;
  if (!rhi_->ReadTextureLevel(cube, level, px)) return false;
  const int size = (int)std::lround(std::sqrt((double)px.size() / 24.0)); // 6 faces of size^2 RGBA
  double sh[9][3] = {}, total = 0;
  for (int f = 0; f < 6; ++f)
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        double u = (x + 0.5) / size * 2 - 1, v = (y + 0.5) / size * 2 - 1;
        glm::dvec3 d = f == 0 ? glm::dvec3(1, -v, -u) : f == 1 ? glm::dvec3(-1, -v, u) : f == 2 ? glm::dvec3(u, 1, v)
                     : f == 3 ? glm::dvec3(u, -1, -v) : f == 4 ? glm::dvec3(u, -v, 1) : glm::dvec3(-u, -v, -1);
        d = glm::normalize(d);
        double w = 4.0 / (size * size * std::pow(1 + u * u + v * v, 1.5)); // texel solid angle
        const double basis[9] = {0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x, 1.092548 * d.x * d.y,
                                 1.092548 * d.y * d.z, 0.315392 * (3 * d.z * d.z - 1), 1.092548 * d.x * d.z, 0.546274 * (d.x * d.x - d.y * d.y)};
        const float* c = &px[(((size_t)f * size + y) * size + x) * 4];
        if (!std::isfinite(c[0]) || !std::isfinite(c[1]) || !std::isfinite(c[2])) { ++badTexels_; continue; }
        for (int k = 0; k < 9; ++k)
          for (int ch = 0; ch < 3; ++ch) sh[k][ch] += basis[k] * c[ch] * w;
        total += w;
      }
  const double band[9] = {1, 2.0 / 3, 2.0 / 3, 2.0 / 3, 0.25, 0.25, 0.25, 0.25, 0.25}; // cosine lobe / pi
  for (int k = 0; k < 9; ++k)
    for (int ch = 0; ch < 3; ++ch) out[k][ch] = (float)(sh[k][ch] * (4 * 3.14159265358979 / total) * band[k]);
  return true;
}

void Renderer3D::UploadProbes() {
  ProbesData d{};
  d.pos[0][3] = 1; // the sky: everywhere, no box
  for (int i = 0; i < bakedProbes_; ++i) {
    const ReflectionProbe3D& p = probes_[i];
    Put(d.min[i + 1], p.boxMin, p.boxProjection ? 1.f : 0.f);
    Put(d.max[i + 1], p.boxMax);
    Put(d.pos[i + 1], p.position, p.intensity);
  }
  for (int i = 0; i <= kMaxProbes; ++i)
    for (int k = 0; k < 9; ++k)
      for (int ch = 0; ch < 3; ++ch) d.sh[i * 9 + k][ch] = probeSH_[i][k][ch];
  d.info[0] = (float)(kEnvLevels - 1);
  if (hasVolume_ && volumeBaked_) {
    Put(d.volMin, volume_.boxMin, 1);
    Put(d.volMax, volume_.boxMax);
    Put(d.volCount, glm::vec3(volume_.count), volume_.intensity);
  }
  rhi_->UpdateBuffer(probesUbo_, &d, sizeof(d));
}

void Renderer3D::BakeSky() {
  for (int face = 0; face < 6; ++face) {
    PrefilterData pd{{(float)face, 0, (float)kEnvSize, (float)kEnvSize}};
    rhi_->UpdateBuffer(prefilterUbo_, &pd, sizeof(pd));
    rhi_->BindUniformBuffer(6, prefilterUbo_);
    rhi_->BeginTextureTarget(scratchCube_, 0, face);
    rhi_->Draw(skyCube_, fullscreen_, 3);
    rhi_->EndTextureTarget();
  }
  rhi_->GenerateMips(scratchCube_);
  PrefilterInto(0);
  ComputeSH(0);
  UploadProbes();
}

void Renderer3D::SetProbes(const std::vector<ReflectionProbe3D>& probes) {
  probes_.assign(probes.begin(), probes.begin() + std::min<size_t>(probes.size(), kMaxProbes));
  bakedProbes_ = 0;
  UploadProbes();
}

void Renderer3D::CaptureCube(const glm::vec3& at, RenderTargetHandle rt, int size, TextureHandle cube, const std::vector<DrawItem3D>& items,
                             const Light3D& light) {
  // Faces in OpenGL order; "up" points down for the side faces so the rendered rows land as the cube expects.
  const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  QualitySettings q = ReferenceQuality();
  q.msaa = 1;
  RenderExtras extras;
  extras.linearOutput = true;
  for (int face = 0; face < 6; ++face) {
    Camera3D c;
    c.position = at;
    c.target = c.position + dirs[face];
    c.up = ups[face];
    c.fovY = 90.f;
    c.nearZ = 0.05f;
    c.farZ = 500.f;
    Render(rt, size, size, c, light, items, {0, 0, 0, 1}, &extras, &q);
    rhi_->CopyToCubeFace(rhi_->RenderTargetColor(rt), size, cube, face);
  }
  rhi_->GenerateMips(cube);
}

void Renderer3D::SetColorGrading(const ColorGrading3D* g) {
  if (!rhi_) return;
  uint64_t key = 0;
  if (g && !g->Neutral()) {
    key = 1469598103934665603ull;
    auto fold = [&](const void* p, size_t n) {
      for (size_t i = 0; i < n; ++i) key = (key ^ static_cast<const uint8_t*>(p)[i]) * 1099511628211ull;
    };
    const float v[6] = {g->contrast, g->saturation, g->temperature, g->tint, g->intensity, (float)g->lutSize};
    fold(v, sizeof(v));
    fold(g->lut.data(), g->lut.size() * sizeof(float));
  }
  if (key == gradingKey_) return;
  gradingKey_ = key;
  rhi_->DestroyTexture(gradingLut_);
  gradingLut_ = {};
  if (key) gradingLut_ = rhi_->CreateTexture3D(32, BuildGradingLut(*g, 32).data());
}

void Renderer3D::SetVolume(const ProbeVolume3D* volume) {
  hasVolume_ = volume != nullptr;
  volumeBaked_ = false;
  if (volume) {
    volume_ = *volume;
    volume_.count = glm::clamp(volume_.count, glm::ivec3(1), glm::ivec3(16));
    volume_.boxMax = glm::max(volume_.boxMax, volume_.boxMin + glm::vec3(0.01f));
  }
  if (rhi_) UploadProbes();
}

glm::vec3 Renderer3D::VolumeIrradiance(const glm::vec3& p, const glm::vec3& n) const {
  // Same blend as the shader's VolumeIrradiance (probes at cell centres, clamped at the box edges).
  if (!volumeBaked_) return glm::vec3(0);
  const glm::ivec3 cnt = volume_.count;
  glm::vec3 g = glm::clamp((p - volume_.boxMin) / (volume_.boxMax - volume_.boxMin) * glm::vec3(cnt) - 0.5f, glm::vec3(0),
                           glm::vec3(cnt - 1));
  glm::ivec3 i0 = glm::min(glm::ivec3(glm::floor(g)), cnt - 1), i1 = glm::min(i0 + 1, cnt - 1);
  glm::vec3 f = g - glm::vec3(i0);
  const float b[9] = {0.282095f, 0.488603f * n.y, 0.488603f * n.z, 0.488603f * n.x, 1.092548f * n.x * n.y, 1.092548f * n.y * n.z,
                      0.315392f * (3 * n.z * n.z - 1), 1.092548f * n.x * n.z, 0.546274f * (n.x * n.x - n.y * n.y)};
  glm::vec3 e{0};
  float wsum = 0;
  for (int c = 0; c < 8; ++c) {
    glm::ivec3 q{(c & 1) ? i1.x : i0.x, (c & 2) ? i1.y : i0.y, (c & 4) ? i1.z : i0.z};
    size_t base = ((size_t)(q.z * cnt.y + q.y) * cnt.x + q.x) * 36;
    float w = ((c & 1) ? f.x : 1 - f.x) * ((c & 2) ? f.y : 1 - f.y) * ((c & 4) ? f.z : 1 - f.z) * std::max(volumeSH_[base + 3], 1e-4f);
    for (int k = 0; k < 9; ++k) e += w * b[k] * glm::vec3(volumeSH_[base + k * 4], volumeSH_[base + k * 4 + 1], volumeSH_[base + k * 4 + 2]);
    wsum += w;
  }
  return glm::max(e / std::max(wsum, 1e-8f), glm::vec3(0)) * volume_.intensity;
}

void Renderer3D::EnsureSmallCapture() {
  if (smallCube_.Valid()) return;
  int levels = 1;
  for (int sz = kVolumeSize; sz > 1; sz >>= 1) ++levels;
  smallCube_ = rhi_->CreateCubemap(kVolumeSize, levels);
  smallCaptureRT_ = rhi_->CreateRenderTargetEx(kVolumeSize, kVolumeSize, 1, true, true);
}

namespace {
glm::vec3 EvalSH(const float (&sh)[9][3], const glm::vec3& n) {
  const float b[9] = {0.282095f, 0.488603f * n.y, 0.488603f * n.z, 0.488603f * n.x, 1.092548f * n.x * n.y, 1.092548f * n.y * n.z,
                      0.315392f * (3 * n.z * n.z - 1), 1.092548f * n.x * n.z, 0.546274f * (n.x * n.x - n.y * n.y)};
  glm::vec3 e(0);
  for (int k = 0; k < 9; ++k) e += b[k] * glm::vec3(sh[k][0], sh[k][1], sh[k][2]);
  return glm::max(e, glm::vec3(0));
}
} // namespace

glm::vec3 Renderer3D::AmbientAt(const glm::vec3& p, const glm::vec3& n) const {
  if (hasVolume_ && volumeBaked_ && glm::all(glm::greaterThanEqual(p, volume_.boxMin)) && glm::all(glm::lessThanEqual(p, volume_.boxMax)))
    return VolumeIrradiance(p, n);
  for (int i = 0; i < bakedProbes_; ++i)
    if (glm::all(glm::greaterThanEqual(p, probes_[i].boxMin)) && glm::all(glm::lessThanEqual(p, probes_[i].boxMax)))
      return EvalSH(probeSH_[i + 1], n) * probes_[i].intensity;
  return EvalSH(probeSH_[0], n);
}

void Renderer3D::ClearLightmaps() {
  if (!rhi_) return;
  for (LightmapInstance& li : lightmaps_) DestroyMesh(li.mesh);
  lightmaps_.clear();
  lightmapIndex_.clear();
  rhi_->DestroyTexture(lightmapTex_);
  lightmapTex_ = {};
  lightmapW_ = lightmapH_ = lightmapTexels_ = 0;
  lmTexelPos_.clear();
  lmTexelN_.clear();
  lmTexelValue_.clear();
  lmTexelIndex_.clear();
  lightmapRGBA_.clear();
  lightmapInside_ = 0;
}

int Renderer3D::BakeLightmaps(const std::vector<DrawItem3D>& items, const Light3D& light, const LightmapSettings& settings) {
  if (!rhi_) return 0;
  ClearLightmaps();
  // The sky cube is baked by Render when the sky changes: one small render makes sure it matches this light.
  EnsureSmallCapture();
  {
    const RenderStats keep = stats_;
    QualitySettings q = ReferenceQuality();
    q.msaa = 1;
    RenderExtras extras;
    extras.linearOutput = true;
    Render(smallCaptureRT_, kVolumeSize, kVolumeSize, Camera3D{}, light, {}, {0, 0, 0, 1}, &extras, &q);
    stats_ = keep;
  }
  // Rays that leave the scene see the sky (a CPU copy of the sky cube's 64^2 level, layer 0).
  std::vector<float> env;
  const int skySize = kEnvSize >> 1;
  rhi_->ReadTextureLevel(envArray_, 1, env);
  LightmapBakeInput in;
  in.items = &items;
  in.light = &light;
  in.sky = [&](const glm::vec3& d) {
    const glm::vec3 a = glm::abs(d);
    int face;
    float sc, tc, ma;
    if (a.x >= a.y && a.x >= a.z) { face = d.x > 0 ? 0 : 1; ma = a.x; sc = d.x > 0 ? -d.z : d.z; tc = -d.y; }
    else if (a.y >= a.z) { face = d.y > 0 ? 2 : 3; ma = a.y; sc = d.x; tc = d.y > 0 ? d.z : -d.z; }
    else { face = d.z > 0 ? 4 : 5; ma = a.z; sc = d.z > 0 ? d.x : -d.x; tc = -d.y; }
    const int x = std::clamp((int)((sc / ma * 0.5f + 0.5f) * skySize), 0, skySize - 1);
    const int y = std::clamp((int)((tc / ma * 0.5f + 0.5f) * skySize), 0, skySize - 1);
    const size_t at = (((size_t)face * skySize + y) * skySize + x) * 4;
    return at + 2 < env.size() ? glm::vec3(env[at], env[at + 1], env[at + 2]) : glm::vec3(0);
  };
  in.ambient = [this](const glm::vec3& p, const glm::vec3& n) { return AmbientAt(p, n); };
  LightmapResult r;
  std::string err;
  if (!BakeLightmapAtlas(in, settings, r, &err)) {
    LogWarn("gi: lightmaps not baked: {}", err);
    return 0;
  }
  TextureDesc td;
  td.w = r.width;
  td.h = r.height;
  td.format = TextureFormat::RGBA16F;
  lightmapTex_ = rhi_->CreateTexture(td, r.rgba.data());
  for (size_t k = 0; k < r.meshes.size(); ++k) {
    LightmapInstance li;
    li.mesh = CreateMesh(r.meshes[k]);
    li.source = items[r.instanceItem[k]].mesh;
    li.model = items[r.instanceItem[k]].model;
    li.key = ContentKey(li.source, li.model); // by content, so a cached bake finds the item again
    lightmapIndex_[LightmapKey(li.source, li.model)] = (int)lightmaps_.size();
    lightmaps_.push_back(std::move(li));
  }
  lightmapW_ = r.width;
  lightmapH_ = r.height;
  lightmapTexels_ = (int)r.texelIndex.size();
  lightmapDensity_ = r.texelsPerMeter;
  lightmapMs_ = r.ms;
  lmTexelPos_ = std::move(r.texelPos);
  lmTexelN_ = std::move(r.texelNormal);
  for (uint32_t idx : r.texelIndex) lmTexelValue_.push_back({r.rgba[idx * 4], r.rgba[idx * 4 + 1], r.rgba[idx * 4 + 2]});
  lmTexelIndex_ = std::move(r.texelIndex);
  lightmapRGBA_ = std::move(r.rgba);
  lightmapInside_ = r.insideTexels;
  LogInfo("gi: lightmaps: {}{}", DescribeLightmaps(), r.insideTexels ? std::format(", {} texels inside geometry filled", r.insideTexels) : "");
  return (int)lightmaps_.size();
}

std::string Renderer3D::DescribeLightmaps() const {
  return std::format("{} objects, atlas {}x{}, {:.1f} texels/m, {} texels, {:.0f} ms", lightmaps_.size(), lightmapW_, lightmapH_,
                     lightmapDensity_, lightmapTexels_, lightmapMs_);
}

bool Renderer3D::LightmapAt(const glm::vec3& p, const glm::vec3& n, glm::vec3& value) const {
  float best = 1e30f;
  for (size_t i = 0; i < lmTexelPos_.size(); ++i) {
    if (glm::dot(lmTexelN_[i], n) < 0.7f) continue;
    const glm::vec3 d = lmTexelPos_[i] - p;
    const float d2 = glm::dot(d, d);
    if (d2 < best) { best = d2; value = lmTexelValue_[i]; }
  }
  return best < 1e30f;
}

int Renderer3D::BakeProbes(const std::vector<DrawItem3D>& items, const Light3D& light) {
  if ((probes_.empty() && !hasVolume_) || !rhi_) return 0;
  badTexels_ = 0;
  const auto bakeStart = std::chrono::steady_clock::now();
  const RenderStats keep = stats_;
  // Each probe sees the scene lit by the probes as they are now, so every bake adds one bounce.
  if (!probes_.empty() && !captureRT_.Valid()) captureRT_ = rhi_->CreateRenderTargetEx(kEnvSize, kEnvSize, 1, true, true);
  for (size_t i = 0; i < probes_.size(); ++i) {
    CaptureCube(probes_[i].position, captureRT_, kEnvSize, scratchCube_, items, light);
    PrefilterInto((int)i + 1);
    ComputeSH((int)i + 1);
  }
  if (hasVolume_) { // the grid: small captures, SH from the 16^2 level; all probes see the previous bounce
    EnsureSmallCapture();
    const glm::ivec3 cnt = volume_.count;
    const size_t n = (size_t)cnt.x * cnt.y * cnt.z;
    std::vector<float> next(n * 36, 0.f);
    const glm::vec3 cell = (volume_.boxMax - volume_.boxMin) / glm::vec3(cnt);
    std::vector<glm::vec3> corners; // probes inside objects see only back faces: switched off, not captured
    GatherOpaqueTriangles(items, corners);
    RayScene solids;
    solids.Build(std::move(corners));
    invalidVolumeProbes_ = 0;
    for (int z = 0; z < cnt.z; ++z)
      for (int y = 0; y < cnt.y; ++y)
        for (int x = 0; x < cnt.x; ++x) {
          glm::vec3 at = volume_.boxMin + (glm::vec3(x, y, z) + 0.5f) * cell;
          size_t base = ((size_t)(z * cnt.y + y) * cnt.x + x) * 36;
          if (BackfaceShare(solids, at) > 0.5f) { ++invalidVolumeProbes_; continue; } // weight 0 (coefficient 0's w)
          next[base + 3] = 1.f;
          CaptureCube(at, smallCaptureRT_, kVolumeSize, smallCube_, items, light);
          float sh[9][3];
          if (!ComputeSHFrom(smallCube_, 1, sh)) continue;
          for (int k = 0; k < 9; ++k)
            for (int ch = 0; ch < 3; ++ch) next[base + k * 4 + ch] = sh[k][ch];
        }
    volumeSH_ = std::move(next);
    if (n * 9 > volumeCapacity_) {
      rhi_->DestroyBuffer(volumeBuffer_);
      volumeCapacity_ = n * 9;
      volumeBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, volumeCapacity_ * 16, true);
    }
    rhi_->UpdateBuffer(volumeBuffer_, volumeSH_.data(), volumeSH_.size() * sizeof(float));
    volumeBaked_ = true;
  }
  if (badTexels_) LogWarn("gi: {} captured texel(s) were not finite (NaN/inf) and were left out", badTexels_);
  probeBakeMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bakeStart).count();
  bakedProbes_ = (int)probes_.size();
  UploadProbes();
  stats_ = keep;
  return bakedProbes_ + VolumeProbes();
}

bool Renderer3D::ReadProbeFace(int probe, int face, std::vector<float>& rgb, int& size) {
  std::vector<float> all;
  if (probe < 0 || probe > kMaxProbes || !rhi_->ReadTextureLevel(envArray_, 0, all)) return false;
  size = kEnvSize;
  const size_t faceFloats = (size_t)kEnvSize * kEnvSize * 4, at = ((size_t)probe * 6 + face) * faceFloats;
  rgb.resize((size_t)kEnvSize * kEnvSize * 3);
  for (size_t i = 0; i < (size_t)kEnvSize * kEnvSize; ++i)
    for (int ch = 0; ch < 3; ++ch) rgb[i * 3 + ch] = all[at + i * 4 + ch];
  return true;
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
    case DebugView::Indirect: return "Indirect";
    case DebugView::Cascades: return "Cascades";
    case DebugView::AO: return "AO";
    case DebugView::LightCount: return "Lights";
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
  return std::format("{}x{} msaa={} view={} draws={} scene_draws={} objects={} culled={} shadow_culled={} tris={} lights={} shadow_map={} casters={} tonemap={} fxaa={} bloom={} cascades={} ssao={} clustered={} cluster_refs={} cluster_max={} aniso={} blobs={} grading={} local_shadows={} local_tiles={}x{}", s.w, s.h,
                     s.msaa, DebugViewName(s.view), s.draws, s.sceneDraws, s.objects, s.culled, s.shadowCulled, s.triangles, s.localLights, s.shadowMap, s.shadowCasters, s.toneMap ? 1 : 0,
                     s.fxaa ? 1 : 0, s.bloom ? 1 : 0, s.cascades, s.ssao ? 1 : 0, s.clustered ? 1 : 0, s.clusterRefs, s.clusterMax, s.anisotropy,
                     s.blobs, s.grading ? 1 : 0, s.localShadows, s.localShadowTiles, s.localShadowTile);
}
}
