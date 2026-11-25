#include "Render3D/DynamicGI.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bdg {
namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDepthHistory = 1.f; // weight of a texel's stored depth against this update's rays near it
glm::vec3 V(const Color& c, float s = 1.f) { return glm::vec3(c.r, c.g, c.b) * s; }
void Basis(const glm::vec3& d, float b[9]) {
  b[0] = 0.282095f;
  b[1] = 0.488603f * d.y;
  b[2] = 0.488603f * d.z;
  b[3] = 0.488603f * d.x;
  b[4] = 1.092548f * d.x * d.y;
  b[5] = 1.092548f * d.y * d.z;
  b[6] = 0.315392f * (3 * d.z * d.z - 1);
  b[7] = 1.092548f * d.x * d.z;
  b[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}
uint64_t Fold(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}
// Rotation of the ray set for one update (golden-ratio steps: a fresh set each time, no repeats)
float Sgn(float v) { return v >= 0 ? 1.f : -1.f; }
float o0Sum(const float* o) { return o[0] + o[1] + o[2]; } // band-0 rgb of a stored probe
glm::mat3 RayRotation(uint32_t frame) {
  const float a = (float)frame * 2.39996323f, b = std::acos(std::clamp(1.f - 2.f * std::fmod(frame * 0.618034f, 1.f), -1.f, 1.f));
  const glm::vec3 axis = glm::normalize(glm::vec3(std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a)));
  const float angle = std::fmod(frame * 1.7f, 6.2831853f);
  const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
  const glm::vec3 u = axis;
  return glm::mat3(t * u.x * u.x + c, t * u.x * u.y + s * u.z, t * u.x * u.z - s * u.y, t * u.x * u.y - s * u.z, t * u.y * u.y + c,
                   t * u.y * u.z + s * u.x, t * u.x * u.z + s * u.y, t * u.y * u.z - s * u.x, t * u.z * u.z + c);
}
}

glm::vec2 DynamicGI::OctUV(const glm::vec3& d) {
  const glm::vec3 p = d / std::max(std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z), 1e-8f);
  glm::vec2 e = p.y >= 0 ? glm::vec2(p.x, p.z) : glm::vec2((1 - std::fabs(p.z)) * Sgn(p.x), (1 - std::fabs(p.x)) * Sgn(p.z));
  return e * 0.5f + 0.5f;
}

glm::vec3 DynamicGI::OctDir(const glm::vec2& uv) {
  const glm::vec2 e = uv * 2.f - 1.f;
  glm::vec3 d(e.x, 1 - std::fabs(e.x) - std::fabs(e.y), e.y);
  if (d.y < 0) {
    const float x = d.x, z = d.z;
    d.x = (1 - std::fabs(z)) * Sgn(x);
    d.z = (1 - std::fabs(x)) * Sgn(z);
  }
  return glm::normalize(d);
}

void DynamicGI::Configure(const ProbeVolume3D& volume) {
  const glm::ivec3 cnt = glm::clamp(volume.count, glm::ivec3(1), glm::ivec3(16));
  if (cnt == count_ && volume.boxMin == volume_.boxMin && volume.boxMax == volume_.boxMax) {
    volume_.intensity = volume.intensity;
    return;
  }
  volume_ = volume;
  count_ = cnt;
  const size_t n = Probes();
  sh_.assign(n * 36, 0.f);
  vis_.assign(n * kVisStride, 0.f);
  seeded_.assign(n, 0);
  maskDirty_.assign(n, 1);
  next_ = 0;
  ++version_;
}

bool DynamicGI::Seeded() const {
  return !seeded_.empty() && std::all_of(seeded_.begin(), seeded_.end(), [](uint8_t v) { return v != 0; });
}

glm::vec3 DynamicGI::ProbePosition(int x, int y, int z) const {
  const glm::vec3 cell = (volume_.boxMax - volume_.boxMin) / glm::vec3(count_);
  return volume_.boxMin + (glm::vec3(x, y, z) + 0.5f) * cell;
}

bool DynamicGI::SetScene(const std::vector<DrawItem3D>& items, const std::function<glm::vec3(TextureHandle)>& textureMean) {
  uint64_t key = 1469598103934665603ull;
  for (const DrawItem3D& it : items) {
    if (!it.mesh || !it.mesh->source || it.material.alpha == Material3D::Alpha::Blend) continue;
    key = Fold(key, &it.mesh->contentHash, sizeof(it.mesh->contentHash));
    key = Fold(key, &it.model, sizeof(it.model));
    key = Fold(key, &it.material.baseColor, sizeof(it.material.baseColor));
    key = Fold(key, &it.material.emissive, sizeof(it.material.emissive));
    key = Fold(key, &it.material.baseMap.id, sizeof(it.material.baseMap.id));
    key = Fold(key, &it.material.emissiveMap.id, sizeof(it.material.emissiveMap.id));
  }
  if (key == sceneKey_ && scene_.Triangles()) return false;
  sceneKey_ = key;
  std::vector<glm::vec3> corners;
  albedo_.clear();
  emissive_.clear();
  normal_.clear();
  for (const DrawItem3D& it : items) {
    if (!it.mesh || !it.mesh->source || it.material.alpha == Material3D::Alpha::Blend) continue;
    const MeshData& src = *it.mesh->source;
    auto tint = [&](TextureHandle t) { return !t.Valid() ? glm::vec3(1.f) : textureMean ? textureMean(t) : glm::vec3(0.5f); };
    const glm::vec3 base = V(it.material.baseColor) * tint(it.material.baseMap);
    const glm::vec3 albedo = it.material.unlit ? glm::vec3(0.f) : base * (1.f - std::clamp(it.material.metallic, 0.f, 1.f) * 0.8f);
    const glm::vec3 glow = V(it.material.emissive) * tint(it.material.emissiveMap) + (it.material.unlit ? base : glm::vec3(0.f));
    for (size_t t = 0; t + 2 < src.indices.size(); t += 3) {
      glm::vec3 p[3];
      for (int c = 0; c < 3; ++c) {
        const Vertex3D& v = src.vertices[src.indices[t + c]];
        p[c] = glm::vec3(it.model * glm::vec4(v.px, v.py, v.pz, 1));
        corners.push_back(p[c]);
      }
      const glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
      normal_.push_back(glm::length(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0, 1, 0));
      albedo_.push_back(albedo);
      emissive_.push_back(glow);
    }
  }
  scene_.Build(std::move(corners));
  maskDirty_.assign(Probes(), 1);
  return true;
}

glm::vec3 DynamicGI::Sky(const Light3D& light, const glm::vec3& d) const {
  if (!light.background.draw) return d.y >= 0 ? V(light.sky) : V(light.ground);
  const SkyBackground3D& bg = light.background;
  const glm::vec3 up = glm::mix(V(bg.horizon, bg.intensity), V(bg.zenith, bg.intensity), std::pow(std::clamp(d.y, 0.f, 1.f), 0.5f));
  const float b = 0.03f, t = std::clamp((d.y + b) / (2 * b), 0.f, 1.f);
  return glm::mix(V(bg.ground, bg.intensity), up, t * t * (3 - 2 * t));
}

int DynamicGI::Update(const Light3D& light, const Settings& s) {
  const size_t total = Probes();
  if (!total) return 0;
  const int rays = std::clamp(s.raysPerProbe, 8, 512);
  const int todo = (int)std::min<size_t>((size_t)std::max(s.probesPerUpdate, 1), total);
  const glm::vec3 cell = (volume_.boxMax - volume_.boxMin) / glm::vec3(count_);
  const float maxDist = 2.f * glm::length(cell); // what a probe needs to know about its surroundings
  const glm::vec3 L = glm::length(light.toLight) > 1e-6f ? glm::normalize(light.toLight) : glm::vec3(0, 1, 0);
  const glm::mat3 rot = RayRotation(++frame_);
  const bool hasScene = scene_.Triangles() > 0;
  constexpr int kTexels = kVisRes * kVisRes;
  glm::vec3 texDir[kTexels];
  for (int t = 0; t < kTexels; ++t) texDir[t] = OctDir((glm::vec2(t % kVisRes, t / kVisRes) + 0.5f) / (float)kVisRes);
  std::vector<glm::vec3> dirs((size_t)rays);
  for (int i = 0; i < rays; ++i) { // spherical Fibonacci, turned each update
    const float y = 1 - (i + 0.5f) * 2.f / rays, r = std::sqrt(std::max(1 - y * y, 0.f)), phi = i * 2.39996323f;
    dirs[(size_t)i] = rot * glm::vec3(r * std::cos(phi), y, r * std::sin(phi));
  }
  for (int k = 0; k < todo; ++k, next_ = (next_ + 1) % total) {
    const size_t pi = next_;
    const int x = (int)(pi % count_.x), y = (int)((pi / count_.x) % count_.y), z = (int)(pi / ((size_t)count_.x * count_.y));
    const glm::vec3 at = ProbePosition(x, y, z);
    float sh[9][3] = {};
    // depth moments per texel: a sharp lobe (kept over updates) and a wide one (a probe's first update only)
    float mean[kTexels] = {}, sq[kTexels] = {}, wsum[kTexels] = {}, wideMean[kTexels] = {}, wideSq[kTexels] = {}, wideSum[kTexels] = {};
    int back = 0;
    for (const glm::vec3& d : dirs) {
      glm::vec3 radiance(0.f);
      float dist = maxDist;
      glm::vec3 hitN(0.f); // the hit's plane, to move its distance onto each texel's own direction
      float hitDN = 0;
      RayHit hit;
      if (hasScene && scene_.Intersect(at, d, 1e30f, hit)) {
        dist = std::min(hit.t, maxDist);
        hitN = normal_[hit.tri];
        hitDN = glm::dot(d, hitN);
        if (hit.backface) ++back;
        else {
          const glm::vec3 p = at + d * hit.t, n = normal_[hit.tri], a = albedo_[hit.tri];
          glm::vec3 e(0.f); // light arriving at the hit, as the forward shader's Lambert term sees it (before / pi)
          const float nl = glm::dot(n, L);
          if (light.intensity > 0 && nl > 0 && !(light.shadows && scene_.Occluded(p + n * 0.01f, L, 1e30f))) e += V(light.color, light.intensity) * nl;
          for (const LocalLight3D& l : light.locals) {
            const glm::vec3 toL = l.position - p;
            const float d2 = std::max(glm::dot(toL, toL), 1e-4f), range = std::max(l.range, 1e-3f);
            if (d2 >= range * range || l.intensity <= 0) continue;
            const glm::vec3 dir = toL / std::sqrt(d2);
            const float ln = glm::dot(n, dir);
            if (ln <= 0) continue;
            const float win = std::clamp(1.f - (d2 * d2) / (range * range * range * range), 0.f, 1.f);
            float atten = win * win / (d2 + 1.f);
            if (l.spot) {
              const float co = std::cos(glm::radians(std::max(l.outerAngle, 0.1f) * 0.5f)),
                          ci = std::cos(glm::radians(std::clamp(l.innerAngle, 0.f, l.outerAngle - 0.01f) * 0.5f));
              const float t = std::clamp((glm::dot(-dir, glm::normalize(l.direction)) - co) / std::max(ci - co, 1e-4f), 0.f, 1.f);
              atten *= t * t * (3 - 2 * t);
            }
            if (atten <= 0 || scene_.Occluded(p + n * 0.01f, dir, std::sqrt(d2) - 0.02f)) continue;
            e += V(l.color, l.intensity) * atten * ln;
          }
          // what the grid gives the hit now (infinite bounces over time), with visibility (no leaks into the bounce)
          radiance = a * (e / kPi + Irradiance(p + n * 0.05f, n, true)) + emissive_[hit.tri];
        }
      } else {
        radiance = Sky(light, d);
      }
      float b[9];
      Basis(d, b);
      for (int c = 0; c < 9; ++c)
        for (int ch = 0; ch < 3; ++ch) sh[c][ch] += b[c] * radiance[ch];
      // distance moments per octahedral texel, from the rays close to its direction. A hit's distance is taken along
      // the texel's own direction through the hit's plane (exact for flat walls; a plain average of the rays' lengths
      // over the lobe comes out too long, as they grow with 1 / cos toward the lobe's edge)
      for (int t = 0; t < kTexels; ++t) {
        const float c = glm::dot(d, texDir[t]);
        if (c <= 0) continue;
        float td = dist;
        if (hitDN != 0) {
          const float cn = glm::dot(texDir[t], hitN);
          td = cn * hitDN > 1e-4f ? std::min(hit.t * hitDN / cn, maxDist) : dist;
        }
        const float c2 = c * c, c4 = c2 * c2, c8 = c4 * c4, c16 = c8 * c8, c64 = (c16 * c16) * (c16 * c16);
        wideMean[t] += c16 * td;
        wideSq[t] += c16 * td * td;
        wideSum[t] += c16;
        mean[t] += c64 * td;
        sq[t] += c64 * td * td;
        wsum[t] += c64;
      }
    }
    rays_ += (uint64_t)rays;
    if (maskDirty_[pi]) { // what the probe sees around it, box by box (geometry only: traced when the scene changed)
      uint32_t words[kMaskWords] = {};
      for (int b = 0; b < kMaskRes * kMaskRes * kMaskRes; ++b) {
        const glm::vec3 sub(b % kMaskRes, (b / kMaskRes) % kMaskRes, b / (kMaskRes * kMaskRes));
        const glm::vec3 to = ((sub + 0.5f) / (float)kMaskRes * 2.f - 1.f) * cell;
        const float len = glm::length(to);
        if (!hasScene || !scene_.Occluded(at, to / len, len)) words[b >> 5] |= 1u << (b & 31);
      }
      std::memcpy(&vis_[pi * kVisStride + kTexels * 2], words, sizeof(words));
      maskDirty_[pi] = 0;
    }
    const bool inside = back > rays * 0.3f; // mostly back faces: inside an object
    const float band[9] = {1, 2.f / 3, 2.f / 3, 2.f / 3, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f}; // cosine lobe / pi
    float* o = &sh_[pi * 36];
    float* v = &vis_[pi * kVisStride];
    float keep = seeded_[pi] ? std::clamp(s.hysteresis, 0.f, 0.99f) : 0.f;
    { // a big change (a light switched on or off, light still bouncing in) is taken faster: less of the old value
      // kept from 10 % of change on, a tenth of it from 30 %; small changes (ray noise) keep the full smoothing
      const float now = (sh[0][0] + sh[0][1] + sh[0][2]) * (4 * kPi / rays), old = o0Sum(&sh_[pi * 36]);
      const float change = std::fabs(now - old) / std::max(std::max(now, old), 1e-3f);
      keep *= std::clamp((0.3f - change) / 0.2f, 0.1f, 1.f);
    }
    for (int c = 0; c < 9; ++c)
      for (int ch = 0; ch < 3; ++ch) {
        const float sample = sh[c][ch] * (4 * kPi / rays) * band[c];
        o[c * 4 + ch] = o[c * 4 + ch] * keep + sample * (1 - keep);
      }
    o[3] = inside ? 0.f : 1.f;
    for (int t = 0; t < kTexels; ++t) {
      if (!seeded_[pi]) { // first update: the wide lobe fills every texel
        v[t * 2] = wideSum[t] > 1e-6f ? wideMean[t] / wideSum[t] : maxDist;
        v[t * 2 + 1] = wideSum[t] > 1e-6f ? wideSq[t] / wideSum[t] : maxDist * maxDist;
      } else if (wsum[t] > 1e-6f) { // then the sharp lobe, blended by how many rays came near the texel this time
        const float a = wsum[t] / (wsum[t] + kDepthHistory);
        v[t * 2] += (mean[t] / wsum[t] - v[t * 2]) * a;
        v[t * 2 + 1] += (sq[t] / wsum[t] - v[t * 2 + 1]) * a;
      }
    }
    seeded_[pi] = 1;
  }
  inside_ = 0;
  for (size_t i = 0; i < total; ++i) inside_ += seeded_[i] && sh_[i * 36 + 3] < 0.5f ? 1 : 0;
  ++version_;
  return todo;
}

glm::vec3 DynamicGI::Irradiance(const glm::vec3& surface, const glm::vec3& n, bool visibility) const {
  if (!Probes()) return glm::vec3(0.f);
  const glm::vec3 size = volume_.boxMax - volume_.boxMin, cell = size / glm::vec3(count_);
  // pushed a quarter cell off the surface (normal bias, like DDGI), as the forward shader does before its lookup: a
  // probe just behind a thin wall then sees the wall clearly in front of the point
  const glm::vec3 p = surface + n * (0.25f * std::min(cell.x, std::min(cell.y, cell.z)));
  const glm::vec3 g = glm::clamp((p - volume_.boxMin) / size * glm::vec3(count_) - 0.5f, glm::vec3(0.f), glm::vec3(count_ - 1));
  const glm::ivec3 i0 = glm::min(glm::ivec3(glm::floor(g)), count_ - 1), i1 = glm::min(i0 + 1, count_ - 1);
  const glm::vec3 f = g - glm::vec3(i0);
  float b[9];
  Basis(n, b);
  glm::vec3 e(0.f);
  float wsum = 0;
  for (int c = 0; c < 8; ++c) {
    const glm::ivec3 q((c & 1) ? i1.x : i0.x, (c & 2) ? i1.y : i0.y, (c & 4) ? i1.z : i0.z);
    const glm::vec3 w3((c & 1) ? f.x : 1 - f.x, (c & 2) ? f.y : 1 - f.y, (c & 4) ? f.z : 1 - f.z);
    const size_t pi = ((size_t)q.z * count_.y + q.y) * count_.x + q.x;
    const float* o = &sh_[pi * 36];
    float w = w3.x * w3.y * w3.z * std::max(o[3], 1e-4f);
    if (visibility) {
      const glm::vec3 probe = volume_.boxMin + (glm::vec3(q) + 0.5f) * cell, to = p - probe;
      const float dist = glm::length(to);
      const glm::vec3 dir = dist > 1e-5f ? to / dist : glm::vec3(0, 1, 0);
      const float facing = glm::dot(-dir, n) * 0.5f + 0.5f;
      w *= facing * facing + 0.05f;
      const float* v = &vis_[pi * kVisStride];
      const glm::vec2 st = OctUV(dir) * (float)kVisRes - 0.5f;
      const glm::ivec2 t0 = glm::clamp(glm::ivec2(glm::floor(st)), glm::ivec2(0), glm::ivec2(kVisRes - 1)), t1 = glm::min(t0 + 1, glm::ivec2(kVisRes - 1));
      const glm::vec2 fr = glm::clamp(st - glm::vec2(t0), glm::vec2(0.f), glm::vec2(1.f));
      auto tex = [&](int tx, int ty, int k) { return v[(ty * kVisRes + tx) * 2 + k]; };
      auto bil = [&](int k) {
        return glm::mix(glm::mix(tex(t0.x, t0.y, k), tex(t1.x, t0.y, k), fr.x), glm::mix(tex(t0.x, t1.y, k), tex(t1.x, t1.y, k), fr.x), fr.y);
      };
      const float mean = bil(0), mom2 = bil(1);
      // the mask first: the small box around the point hidden from the probe gets (almost) nothing
      const glm::ivec3 sub = glm::clamp(glm::ivec3(glm::floor((to / cell + 1.f) * (float)kMaskRes * 0.5f)), glm::ivec3(0), glm::ivec3(kMaskRes - 1));
      const int bit = (sub.z * kMaskRes + sub.y) * kMaskRes + sub.x;
      uint32_t word;
      std::memcpy(&word, &v[kVisRes * kVisRes * 2 + (bit >> 5)], sizeof(word));
      if (((word >> (bit & 31)) & 1u) == 0) w *= 1e-3f;
      else if (dist > mean) {
        const float var = std::max(mom2 - mean * mean, 1e-4f), dd = dist - mean;
        float pr = var / (var + dd * dd);
        pr = std::max((pr - 0.2f) / 0.8f, 0.f); // light-bleed reduction (same as the shader)
        w *= std::max(pr * pr * pr, 1e-3f);
      }
    }
    glm::vec3 s(0.f);
    for (int k = 0; k < 9; ++k) s += glm::vec3(o[k * 4], o[k * 4 + 1], o[k * 4 + 2]) * b[k];
    e += s * w;
    wsum += w;
  }
  return glm::max(e / std::max(wsum, 1e-8f), glm::vec3(0.f)) * volume_.intensity;
}
}
