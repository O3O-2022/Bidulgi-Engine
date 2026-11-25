#include "Render3D/Quality.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <yyjson.h>

namespace bdg {
const char* TextureGroupKey(TextureGroup g) {
  switch (g) {
    case TextureGroup::World: return "world";
    case TextureGroup::Normal: return "normal";
    case TextureGroup::Masks: return "masks";
    case TextureGroup::Grayscale: return "grayscale";
    case TextureGroup::UI: return "ui";
  }
  return "world";
}

int FirstMipWithin(int w, int h, int levels, int maxSide) {
  int level = 0;
  while (level + 1 < levels && std::max(w, h) > maxSide) {
    w = std::max(w / 2, 1);
    h = std::max(h / 2, 1);
    ++level;
  }
  return level;
}

QualitySettings QualityPreset(QualityTier t) {
  QualitySettings q;
  q.tier = t;
  // texture groups (3-5c): world colour, normal, masks, grayscale, UI
  static const int kTexMax[5][kTextureGroups] = {{512, 256, 256, 256, 16384},     // potato
                                                 {1024, 512, 512, 512, 16384},    // low
                                                 {2048, 1024, 1024, 1024, 16384}, // medium
                                                 {4096, 4096, 4096, 4096, 16384}, // high
                                                 {8192, 8192, 8192, 8192, 16384}};// ultra
  std::copy(kTexMax[(int)t], kTexMax[(int)t] + kTextureGroups, q.textureMax);
  switch (t) {
    case QualityTier::Potato:
      q.lighting = LightingMode::Unlit;
      q.maxLights = 0;
      q.shadows = false;
      q.localShadows = 0;
      q.textureBias = 2;
      q.toneMap = false; // unlit colours stay what the artist picked
      q.fxaa = false;
      q.viewDistance = 0.5f;
      break;
    case QualityTier::Low:
      q.lighting = LightingMode::Vertex;
      q.maxLights = 4;
      q.blobShadows = true; // no shadow maps here: a soft spot under objects instead (3-5b)
      q.shadows = false;
      q.localShadows = 0; // per-vertex lighting: no shadow lookups
      q.textureBias = 2;
      q.toneMap = true;
      q.fxaa = false;
      q.viewDistance = 0.5f;
      break;
    case QualityTier::Medium:
      q.anisotropy = 4;
      break; // the other defaults above are medium
    case QualityTier::High:
    case QualityTier::Ultra:
      q.maxLights = t == QualityTier::Ultra ? 1024 : 256; // clustered (3-5b)
      q.clustered = true;
      q.anisotropy = t == QualityTier::Ultra ? 16 : 8;
      q.shadowMapSize = 2048;
      q.textureBias = 0;
      q.fxaa = false; // MSAA instead
      q.msaa = 4;
      q.bloom = true;
      q.viewDistance = t == QualityTier::Ultra ? 1.25f : 1.f;
      q.shadowCascades = 3;       // cascaded shadow maps (3-5b)
      q.shadowSoftness = 0.025f;  // rotated Poisson filter, the same width in every cascade
      q.ssao = true;
      q.localShadows = t == QualityTier::Ultra ? 8 : 4; // point / spot shadows (3-4b)
      q.localShadowSize = t == QualityTier::Ultra ? 1024 : 512;
      q.taa = t == QualityTier::Ultra; // round 6: temporal AA on ultra (MSAA stays on high)
      q.ssr = true;                    // round 6: screen-space reflections
      q.ssrSteps = t == QualityTier::Ultra ? 64 : 40;
      q.volumetricFog = true;          // round 6: froxel fog where a post volume asks for it
      q.fogGridX = t == QualityTier::Ultra ? 96 : 64;
      q.fogGridY = t == QualityTier::Ultra ? 54 : 36;
      q.fogSlices = t == QualityTier::Ultra ? 64 : 48;
      break;
  }
  return q;
}

QualitySettings ReferenceQuality() {
  QualitySettings q = QualityPreset(QualityTier::High);
  q.maxLights = 16;
  q.toneMap = false;
  q.fxaa = false;
  q.bloom = false;
  q.textureBias = 0;
  q.viewDistance = 1.f;
  q.shadowCascades = 1; // the boards' shadow checks were made with one map and the 3x3 filter
  q.shadowSoftness = 0;
  q.ssao = false;
  q.maxLights = 16;
  q.clustered = false;
  q.anisotropy = 1;
  q.localShadows = 0; // the board's point / spot checks were made without local shadows
  std::fill(q.textureMax, q.textureMax + kTextureGroups, 16384); // full textures
  return q;
}

const char* TierKey(QualityTier t) {
  switch (t) {
    case QualityTier::Potato: return "potato";
    case QualityTier::Low: return "low";
    case QualityTier::Medium: return "medium";
    case QualityTier::High: return "high";
    case QualityTier::Ultra: return "ultra";
  }
  return "medium";
}

const char* TierLabel(QualityTier t) {
  switch (t) {
    case QualityTier::Potato: return "감자";
    case QualityTier::Low: return "저사양";
    case QualityTier::Medium: return "중사양";
    case QualityTier::High: return "고사양";
    case QualityTier::Ultra: return "초고사양";
  }
  return "중사양";
}

bool TierFromString(std::string_view s, QualityTier& out) {
  for (QualityTier t : {QualityTier::Potato, QualityTier::Low, QualityTier::Medium, QualityTier::High, QualityTier::Ultra})
    if (s == TierKey(t) || s == TierLabel(t)) { out = t; return true; }
  return false;
}

QualityTier ProjectQuality::Clamp(QualityTier t) const { return std::clamp(t, min, max); }

QualityTier ProjectQuality::Next(QualityTier t) const {
  t = Clamp(t);
  return t == max ? min : (QualityTier)((int)t + 1);
}

bool ParseProjectQuality(std::string_view json, ProjectQuality& out, std::string* error) {
  out = {};
  yyjson_doc* d = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_COMMENTS);
  yyjson_val* root = d ? yyjson_doc_get_root(d) : nullptr;
  bool ok = yyjson_is_obj(root);
  auto read = [&](const char* key, QualityTier& t) {
    const char* s = yyjson_get_str(yyjson_obj_get(root, key));
    if (s && !TierFromString(s, t)) { ok = false; if (error) *error = std::string("unknown tier \"") + s + "\" for " + key; }
  };
  if (ok) {
    read("min", out.min);
    read("max", out.max);
    if (const char* s = yyjson_get_str(yyjson_obj_get(root, "default")); s && std::string_view(s) == "auto") out.autoTier = true;
    else read("default", out.fallback);
    if (out.min > out.max) { ok = false; if (error) *error = "min is above max"; }
    else out.fallback = out.Clamp(out.fallback); // only a valid range can clamp
  } else if (error) *error = "not a JSON object";
  yyjson_doc_free(d);
  if (!ok) { ProjectQuality defaults; out.min = defaults.min; out.max = defaults.max; out.fallback = defaults.fallback; }
  return ok;
}

bool ApplyQualityOverrides(std::string_view list, QualitySettings& q, std::string* error) {
  while (!list.empty()) {
    size_t comma = list.find(',');
    std::string_view item = list.substr(0, comma);
    list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
    size_t eq = item.find('=');
    if (eq == std::string_view::npos) continue;
    std::string key(item.substr(0, eq)), value(item.substr(eq + 1));
    float v = std::strtof(value.c_str(), nullptr);
    if (key == "msaa") q.msaa = (int)v;
    else if (key == "bloom") q.bloom = v != 0;
    else if (key == "fxaa") q.fxaa = v != 0;
    else if (key == "taa") q.taa = v != 0;
    else if (key == "ssr") q.ssr = v != 0;
    else if (key == "volfog") q.volumetricFog = v != 0;
    else if (key == "fogslices") q.fogSlices = std::clamp((int)v, 8, 128);
    else if (key == "ssrsteps") q.ssrSteps = std::clamp((int)v, 8, 128);
    else if (key == "ssrdistance") q.ssrDistance = std::max(v, 1.f);
    else if (key == "sharpen") q.taaSharpen = std::clamp(v, 0.f, 1.f);
    else if (key == "shadows") q.shadows = v != 0;
    else if (key == "tonemap") q.toneMap = v != 0;
    else if (key == "lights") q.maxLights = (int)v;
    else if (key == "bias") q.textureBias = v;
    else if (key == "exposure") q.exposure = v;
    else if (key == "shadowmap") q.shadowMapSize = (int)v;
    else if (key == "cascades") q.shadowCascades = std::clamp((int)v, 1, 4);
    else if (key == "softness") q.shadowSoftness = std::max(v, 0.f);
    else if (key == "ssao") q.ssao = v != 0;
    else if (key == "clustered") q.clustered = v != 0;
    else if (key == "aniso") q.anisotropy = std::clamp((int)v, 1, 16);
    else if (key == "blob") q.blobShadows = v != 0;
    else if (key == "aoradius") q.ssaoRadius = std::max(v, 0.01f);
    else if (key == "aointensity") q.ssaoIntensity = std::max(v, 0.f);
    else if (key == "view") q.viewDistance = v;
    else if (key == "instancing") q.instancing = v != 0;
    else if (key == "culling") q.culling = v != 0;
    else if (key == "localshadows") q.localShadows = std::clamp((int)v, 0, 16);
    else if (key == "localshadowmap") q.localShadowSize = std::clamp((int)v, 64, 2048);
    else if (key.starts_with("tex.")) { // tex.world=1024, tex.normal=512, ... (texture groups, 3-5c)
      bool known = false;
      for (int g = 0; g < kTextureGroups; ++g)
        if (key.substr(4) == TextureGroupKey((TextureGroup)g)) { q.textureMax[g] = std::clamp((int)v, 1, 16384); known = true; }
      if (!known) { if (error) *error = "unknown texture group " + key.substr(4); return false; }
    }
    else { if (error) *error = "unknown quality key " + key; return false; }
  }
  return true;
}

float TierCost(QualityTier t) {
  switch (t) {
    case QualityTier::Potato: return 0.15f;
    case QualityTier::Low: return 0.25f;
    case QualityTier::Medium: return 0.45f;
    case QualityTier::High: return 1.f;
    case QualityTier::Ultra: return 1.6f;
  }
  return 1.f;
}

QualityTier TierFromBenchmark(double highMs, double budgetMs) {
  if (!(highMs > 0) || !std::isfinite(highMs)) return QualityTier::Medium; // no measurement: the usual default
  for (int t = (int)QualityTier::Ultra; t > (int)QualityTier::Potato; --t)
    if (TierCost((QualityTier)t) * highMs <= 0.6 * budgetMs) return (QualityTier)t;
  return QualityTier::Potato;
}

std::string BenchmarkCacheJson(const std::string& device, double highMs, QualityTier tier) {
  std::string esc;
  for (char c : device) {
    if (c == '"' || c == '\\') esc += '\\';
    if ((unsigned char)c >= 0x20) esc += c;
  }
  return std::format("{{\"v\":1,\"device\":\"{}\",\"high_ms\":{:.3f},\"tier\":\"{}\"}}\n", esc, highMs, TierKey(tier));
}

bool ParseBenchmarkCache(std::string_view json, const std::string& device, double& highMs, QualityTier& tier) {
  yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
  yyjson_val* root = d ? yyjson_doc_get_root(d) : nullptr;
  const char* dev = yyjson_get_str(yyjson_obj_get(root, "device"));
  const char* t = yyjson_get_str(yyjson_obj_get(root, "tier"));
  yyjson_val* ms = yyjson_obj_get(root, "high_ms");
  bool ok = yyjson_get_int(yyjson_obj_get(root, "v")) == 1 && dev && device == dev && t && TierFromString(t, tier) && yyjson_is_num(ms);
  if (ok) highMs = yyjson_get_num(ms);
  yyjson_doc_free(d);
  return ok;
}

void QualityGovernor::Reset() {
  head_ = count_ = 0;
  quietUntil_ = -1e30;
}

double QualityGovernor::AverageMs() const {
  double sum = 0;
  for (int i = 0; i < count_; ++i) sum += ms_[(head_ - 1 - i + kMax) % kMax];
  return count_ ? sum / count_ : 0;
}

bool QualityGovernor::Feed(double now, double frameMs) {
  t_[head_] = now;
  ms_[head_] = frameMs;
  head_ = (head_ + 1) % kMax;
  count_ = std::min(count_ + 1, kMax);
  while (count_ > 0 && now - t_[(head_ - count_ + kMax) % kMax] > window) --count_; // drop frames older than the window
  if (now < quietUntil_ || count_ < minFrames || AverageMs() <= budgetMs * over) return false;
  quietUntil_ = now + cooldown;
  lastStepAverageMs = AverageMs();
  count_ = 0; // judge the new tier on its own frames
  return true;
}

float ToneMapACES(float x) {
  x = std::max(x, 0.f);
  return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.f, 1.f);
}
}
