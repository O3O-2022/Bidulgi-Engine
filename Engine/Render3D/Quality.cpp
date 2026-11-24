#include "Render3D/Quality.h"
#include <algorithm>
#include <cstdlib>
#include <yyjson.h>

namespace bdg {
QualitySettings QualityPreset(QualityTier t) {
  QualitySettings q;
  q.tier = t;
  switch (t) {
    case QualityTier::Potato:
      q.lighting = LightingMode::Unlit;
      q.maxLights = 0;
      q.shadows = false;
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
    read("default", out.fallback);
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
    else { if (error) *error = "unknown quality key " + key; return false; }
  }
  return true;
}

float ToneMapACES(float x) {
  x = std::max(x, 0.f);
  return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.f, 1.f);
}
}
