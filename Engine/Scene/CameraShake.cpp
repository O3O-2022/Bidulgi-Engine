#include "Scene/CameraShake.h"
#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace bdg {
namespace {
constexpr float kTau = 6.2831853f;

// 1D gradient noise in -1..1 (smooth, repeatable per seed): Perlin's, as Unreal's PerlinNoiseCameraShakePattern
float Hash(int i, unsigned seed) {
  unsigned h = (unsigned)i * 374761393u + seed * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xffff) / 32767.5f - 1.f; // gradient -1..1
}
float Noise1D(float x, unsigned seed) {
  const int i = (int)std::floor(x);
  const float f = x - (float)i, u = f * f * f * (f * (f * 6 - 15) + 10); // quintic fade
  const float a = Hash(i, seed) * f, b = Hash(i + 1, seed) * (f - 1);
  return (a + (b - a) * u) * 2.f; // the gradient sum stays within about -0.5..0.5: scale to about -1..1
}
float Wave(CameraShakeDef::Pattern p, float t, float freq, unsigned seed, int axis) {
  if (p == CameraShakeDef::Pattern::Sine) return std::sin(kTau * freq * t + (float)((seed * 7 + axis * 13) % 360) * kTau / 360.f);
  return std::clamp(Noise1D(t * freq + (float)axis * 31.7f, seed * 9 + (unsigned)axis), -1.f, 1.f);
}
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
} // namespace

bool ParseCameraShake(const std::string& json, CameraShakeDef& out, std::string* error) {
  yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
  yyjson_val* r = doc ? yyjson_doc_get_root(doc) : nullptr;
  if (!yyjson_is_obj(r)) {
    if (error) *error = "not a JSON object";
    yyjson_doc_free(doc);
    return false;
  }
  CameraShakeDef d;
  if (const char* n = yyjson_get_str(yyjson_obj_get(r, "camshake"))) d.name = n;
  if (const char* p = yyjson_get_str(yyjson_obj_get(r, "pattern"))) {
    const std::string s = Lower(p);
    if (s == "sine" || s == "wave") d.pattern = CameraShakeDef::Pattern::Sine;
    else if (s != "perlin") {
      if (error) *error = std::format("unknown pattern \"{}\" (perlin or sine)", p);
      yyjson_doc_free(doc);
      return false;
    }
  }
  auto num = [&](const char* k, float& v) {
    if (yyjson_val* x = yyjson_obj_get(r, k); yyjson_is_num(x)) v = (float)yyjson_get_num(x);
  };
  auto vec = [&](const char* k, glm::vec3& v) {
    yyjson_val* a = yyjson_obj_get(r, k);
    for (size_t i = 0; i < 3 && yyjson_is_arr(a) && i < yyjson_arr_size(a); ++i) v[(int)i] = (float)yyjson_get_num(yyjson_arr_get(a, i));
  };
  num("duration", d.duration);
  num("blendIn", d.blendIn);
  num("blendOut", d.blendOut);
  num("fov", d.fov);
  vec("location", d.location);
  vec("rotation", d.rotation);
  if (yyjson_val* f = yyjson_obj_get(r, "frequency"); yyjson_is_arr(f)) {
    if (yyjson_arr_size(f) > 0) d.locationFrequency = (float)yyjson_get_num(yyjson_arr_get(f, 0));
    if (yyjson_arr_size(f) > 1) d.rotationFrequency = (float)yyjson_get_num(yyjson_arr_get(f, 1));
  } else if (yyjson_is_num(f)) d.locationFrequency = d.rotationFrequency = (float)yyjson_get_num(f);
  yyjson_doc_free(doc);
  d.duration = std::max(d.duration, 0.f);
  d.blendIn = std::max(d.blendIn, 0.f);
  d.blendOut = std::max(d.blendOut, 0.f);
  out = d;
  return true;
}

bool BuiltinCameraShake(const std::string& name, CameraShakeDef& out) {
  const std::string n = Lower(name);
  CameraShakeDef d;
  d.name = name;
  if (n == "bdg:light") { d.duration = 0.25f; d.blendOut = 0.15f; d.location = {0.01f, 0.01f, 0.f}; d.rotation = {0.4f, 0.4f, 0.2f}; d.locationFrequency = d.rotationFrequency = 18; }
  else if (n == "bdg:medium") { d.duration = 0.45f; d.location = {0.03f, 0.03f, 0.01f}; d.rotation = {1.2f, 1.2f, 0.5f}; d.locationFrequency = d.rotationFrequency = 15; }
  else if (n == "bdg:heavy") { d.duration = 0.8f; d.blendOut = 0.35f; d.location = {0.08f, 0.08f, 0.03f}; d.rotation = {3.f, 3.f, 1.5f}; d.locationFrequency = d.rotationFrequency = 12; d.fov = 1.f; }
  else if (n == "bdg:explosion") { d.duration = 1.2f; d.blendIn = 0.02f; d.blendOut = 0.8f; d.location = {0.15f, 0.15f, 0.08f}; d.rotation = {5.f, 4.f, 3.f}; d.locationFrequency = 20; d.rotationFrequency = 16; d.fov = 3.f; }
  else if (n == "bdg:handheld") { d.duration = 0; d.blendIn = 0.5f; d.blendOut = 0.5f; d.location = {0.004f, 0.006f, 0.f}; d.rotation = {0.35f, 0.45f, 0.15f}; d.locationFrequency = d.rotationFrequency = 0.7f; }
  else return false;
  out = d;
  return true;
}

float CameraShaker::Envelope(const CameraShakeDef& def, float t, float stopAt) {
  if (t < 0) return 0.f;
  float e = def.blendIn > 0 ? std::min(t / def.blendIn, 1.f) : 1.f;
  float end = def.duration > 0 ? def.duration : -1.f; // when it has fully faded out
  if (stopAt >= 0 && (end < 0 || stopAt + def.blendOut < end)) end = stopAt + def.blendOut;
  if (end >= 0) {
    if (t >= end) return 0.f;
    if (def.blendOut > 0 && t > end - def.blendOut) e = std::min(e, (end - t) / def.blendOut);
  }
  return std::clamp(e, 0.f, 1.f);
}

int CameraShaker::Start(const CameraShakeDef& def, float scale, unsigned seed) {
  if (scale <= 0.f) return 0;
  const int id = nextId_++;
  shakes_.push_back({id, def, scale, 0.f, -1.f, seed ? seed : nextSeed_++});
  return id;
}

void CameraShaker::Stop(int id, bool immediately) {
  for (auto it = shakes_.begin(); it != shakes_.end(); ++it)
    if (it->id == id) {
      if (immediately) shakes_.erase(it);
      else if (it->stopAt < 0) it->stopAt = it->time;
      return;
    }
}

void CameraShaker::StopAll(bool immediately) {
  if (immediately) shakes_.clear();
  else
    for (Shake& s : shakes_)
      if (s.stopAt < 0) s.stopAt = s.time;
}

void CameraShaker::Update(float dt) {
  for (Shake& s : shakes_) s.time += std::max(dt, 0.f);
  std::erase_if(shakes_, [](const Shake& s) {
    const bool ended = (s.def.duration > 0 && s.time >= s.def.duration) || (s.stopAt >= 0 && s.time >= s.stopAt + s.def.blendOut);
    return ended;
  });
}

ShakeOffset CameraShaker::Current() const {
  ShakeOffset o;
  for (const Shake& s : shakes_) {
    const float k = Envelope(s.def, s.time, s.stopAt) * s.scale;
    if (k <= 0) continue;
    for (int a = 0; a < 3; ++a) {
      o.location[a] += s.def.location[a] * k * Wave(s.def.pattern, s.time, s.def.locationFrequency, s.seed, a);
      o.rotation[a] += s.def.rotation[a] * k * Wave(s.def.pattern, s.time, s.def.rotationFrequency, s.seed, a + 3);
    }
    o.fov += s.def.fov * k * Wave(s.def.pattern, s.time, s.def.rotationFrequency, s.seed, 6);
  }
  return o;
}

float WorldShakeScale(const glm::vec3& epicentre, const glm::vec3& camera, float inner, float outer, float falloff) {
  const float d = glm::length(camera - epicentre);
  inner = std::max(inner, 0.f);
  if (d <= inner) return 1.f;
  if (outer <= inner || d >= outer) return 0.f;
  return std::pow(1.f - (d - inner) / (outer - inner), std::max(falloff, 0.01f));
}

} // namespace bdg
