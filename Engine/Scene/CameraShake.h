#pragma once
// Camera shake (queue E-9), the Unreal way: a shake is an asset (pattern, amplitudes, frequencies, duration, blend
// in / out) played on the camera with a scale — `Camera.Shake("bdg:Heavy", 1)` from code or a Graph node, or a
// CameraShakeSource in the world whose strength falls off between an inner and an outer radius (Unreal's
// CameraShakeSourceComponent). Several shakes add up. Pure maths here; play mode runs it on the Game view camera.
//
// .camshake file (one JSON object):
//   {"camshake":"Hit","pattern":"perlin","duration":0.4,"blendIn":0.05,"blendOut":0.15,
//    "location":[0.04,0.04,0.02],"rotation":[1.5,1.5,0.6],"frequency":[18,14],"fov":0}
//   location: metres along the camera's right / up / forward; rotation: degrees of pitch / yaw / roll;
//   frequency: [location Hz, rotation Hz]; duration 0 = until stopped. Built-ins: bdg:Light, bdg:Medium,
//   bdg:Heavy, bdg:Explosion, bdg:Handheld (endless, gentle).
#include <glm/glm.hpp>
#include <cmath>
#include <string>
#include <vector>

namespace bdg {

struct CameraShakeDef {
  enum class Pattern { Perlin, Sine };
  std::string name;
  Pattern pattern = Pattern::Perlin;
  float duration = 0.5f, blendIn = 0.05f, blendOut = 0.2f;
  glm::vec3 location{0.f}, rotation{0.f}; // amplitudes
  float locationFrequency = 12.f, rotationFrequency = 12.f, fov = 0.f;
};

bool ParseCameraShake(const std::string& json, CameraShakeDef& out, std::string* error = nullptr);
bool BuiltinCameraShake(const std::string& name, CameraShakeDef& out); // "bdg:Heavy" (case-insensitive)

struct ShakeOffset {
  glm::vec3 location{0.f}; // camera space: right, up, forward (m)
  glm::vec3 rotation{0.f}; // pitch, yaw, roll (degrees)
  float fov = 0.f;         // degrees
  float Size() const { return glm::length(location) + glm::length(rotation) * 0.01f + std::abs(fov) * 0.01f; }
};

class CameraShaker {
public:
  int Start(const CameraShakeDef& def, float scale = 1.f, unsigned seed = 0); // returns an id (0 = scale <= 0)
  void Stop(int id, bool immediately = false);  // otherwise blends out
  void StopAll(bool immediately = false);
  void Update(float dt);
  ShakeOffset Current() const; // the sum of all playing shakes now
  size_t Active() const { return shakes_.size(); }
  // 0..1: how strong a shake is at time t (blend in, hold, blend out at its end or after a Stop at stopAt).
  static float Envelope(const CameraShakeDef& def, float t, float stopAt = -1.f);

private:
  struct Shake { int id; CameraShakeDef def; float scale, time = 0, stopAt = -1; unsigned seed; };
  std::vector<Shake> shakes_;
  int nextId_ = 1;
  unsigned nextSeed_ = 1;
};

// Unreal's world shake: full strength within `inner` metres of the epicentre, none beyond `outer`, between them
// (1 - x)^falloff where x goes 0..1 across the band.
float WorldShakeScale(const glm::vec3& epicentre, const glm::vec3& camera, float inner, float outer, float falloff = 1.f);

} // namespace bdg
