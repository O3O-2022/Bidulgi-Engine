// Camera shake (queue E-9).
#include "Scene/CameraShake.h"
#include "bdg_test.h"
#include <cmath>

using namespace bdg;

TEST_CASE("camera shake: envelope, duration, stop and blend out (queue E-9)") {
  CameraShakeDef d;
  d.duration = 1.f;
  d.blendIn = 0.2f;
  d.blendOut = 0.4f;
  CHECK(CameraShaker::Envelope(d, 0.f) == 0.f);
  CHECK(std::abs(CameraShaker::Envelope(d, 0.1f) - 0.5f) < 1e-5f);   // half way in
  CHECK(CameraShaker::Envelope(d, 0.5f) == 1.f);                        // holding
  CHECK(std::abs(CameraShaker::Envelope(d, 0.8f) - 0.5f) < 1e-5f);   // half way out (out starts at 0.6)
  CHECK(CameraShaker::Envelope(d, 1.f) == 0.f && CameraShaker::Envelope(d, 2.f) == 0.f);
  CHECK(std::abs(CameraShaker::Envelope(d, 0.4f, 0.3f) - 0.75f) < 1e-5f); // stopped at 0.3: out over 0.4 s
  d.duration = 0; // endless until stopped
  CHECK(CameraShaker::Envelope(d, 100.f) == 1.f);

  CameraShakeDef heavy;
  CHECK(BuiltinCameraShake("BDG:Heavy", heavy) && heavy.duration > 0 && heavy.rotation.x > 0);
  CHECK(!BuiltinCameraShake("bdg:Nope", heavy));
  CameraShaker s;
  CHECK(s.Start(heavy, 0.f) == 0 && s.Active() == 0); // no strength: nothing starts
  const int id = s.Start(heavy, 1.f, 7);
  s.Update(0.1f);
  CHECK(s.Active() == 1 && s.Current().Size() > 0);
  s.Update(heavy.duration);
  CHECK(s.Active() == 0 && s.Current().Size() == 0); // finished and removed
  CameraShakeDef hand;
  CHECK(BuiltinCameraShake("bdg:Handheld", hand));
  const int h = s.Start(hand, 1.f);
  s.Update(5.f);
  CHECK(s.Active() == 1);
  s.Stop(h);              // blends out over 0.5 s
  s.Update(0.25f);
  CHECK(s.Active() == 1);
  s.Update(0.3f);
  CHECK(s.Active() == 0);
  (void)id;
}

TEST_CASE("camera shake: patterns stay within their amplitude, scale and add up (queue E-9)") {
  CameraShakeDef d;
  d.duration = 0;
  d.blendIn = 0;
  d.location = {0.1f, 0.2f, 0.f};
  d.rotation = {2.f, 3.f, 1.f};
  for (CameraShakeDef::Pattern p : {CameraShakeDef::Pattern::Perlin, CameraShakeDef::Pattern::Sine}) {
    d.pattern = p;
    CameraShaker s;
    s.Start(d, 1.f, 3);
    float maxRot = 0, maxLoc = 0, moved = 0;
    glm::vec3 last(0);
    for (int i = 0; i < 400; ++i) {
      s.Update(1.f / 60.f);
      const ShakeOffset o = s.Current();
      maxRot = std::max(maxRot, std::abs(o.rotation.y));
      maxLoc = std::max(maxLoc, std::abs(o.location.y));
      CHECK(std::abs(o.location.z) < 1e-6f); // no amplitude on that axis
      moved += glm::length(o.rotation - last);
      last = o.rotation;
    }
    CHECK(maxRot <= 3.f + 1e-4f && maxRot > 1.f); // reaches a good part of the amplitude, never past it
    CHECK(maxLoc <= 0.2f + 1e-5f && maxLoc > 0.05f);
    CHECK(moved > 10.f); // it really moves frame to frame
  }
  // the same seed gives the same shake (replays, tests); two shakes add
  CameraShaker a, b, both;
  a.Start(d, 1.f, 11);
  b.Start(d, 1.f, 11);
  both.Start(d, 1.f, 11);
  both.Start(d, 0.5f, 11);
  a.Update(0.37f);
  b.Update(0.37f);
  both.Update(0.37f);
  CHECK(a.Current().rotation == b.Current().rotation);
  CHECK(glm::length(both.Current().rotation - a.Current().rotation * 1.5f) < 1e-4f);
}

TEST_CASE("camera shake: .camshake files and world falloff (queue E-9)") {
  CameraShakeDef d;
  std::string err;
  CHECK(ParseCameraShake(R"({"camshake":"Hit","pattern":"sine","duration":0.4,"blendIn":0.05,"blendOut":0.15,"location":[0.04,0.05,0.02],"rotation":[1.5,1.2,0.6],"frequency":[18,14],"fov":2})", d, &err));
  CHECK(d.name == "Hit" && d.pattern == CameraShakeDef::Pattern::Sine && d.duration == 0.4f && d.location.y == 0.05f);
  CHECK(d.rotation.z == 0.6f && d.locationFrequency == 18.f && d.rotationFrequency == 14.f && d.fov == 2.f);
  CHECK(!ParseCameraShake(R"({"pattern":"bouncy"})", d, &err) && err.find("bouncy") != std::string::npos);
  CHECK(!ParseCameraShake("[1,2]", d, &err));
  CHECK(WorldShakeScale({0, 0, 0}, {1, 0, 0}, 2, 10) == 1.f);           // inside
  CHECK(std::abs(WorldShakeScale({0, 0, 0}, {6, 0, 0}, 2, 10) - 0.5f) < 1e-5f); // half way across the band
  CHECK(std::abs(WorldShakeScale({0, 0, 0}, {6, 0, 0}, 2, 10, 2) - 0.25f) < 1e-5f);
  CHECK(WorldShakeScale({0, 0, 0}, {12, 0, 0}, 2, 10) == 0.f);          // beyond
}
