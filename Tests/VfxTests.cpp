// Particles (queue 6-VFX P-1): .vfx parsing, the CPU module stack (bursts, rate, lifetime, gravity, collision,
// colour / size over life), seeded determinism, the quality tier's particle budget, ParticleSystem components.
#include "bdg_test.h"
#include "Scene/SceneFile.h"
#include "VFX/VfxAsset.h"
#include "VFX/VfxComponents.h"
#include "VFX/VfxWorld.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace bdg;

namespace {
const char* kSpark = R"({"vfx":"Spark"}
{"emitter":"Sparks","max":40,"duration":0.2,"loop":false}
{"stage":"emitter-update","module":"SpawnBurst","count":24}
{"stage":"particle-spawn","module":"Lifetime","value":[0.3,0.5]}
{"stage":"particle-spawn","module":"VelocityCone","angle":60,"speed":[2,5]}
{"stage":"particle-spawn","module":"Size","value":0.05}
{"stage":"particle-update","module":"Gravity","value":[0,-9.8,0]}
{"stage":"particle-update","module":"Collision","plane":0,"bounce":0.4}
{"stage":"particle-update","module":"ColorOverLife","gradient":[[0,[1,0.8,0.3,1]],[1,[1,0.2,0,0]]]}
{"stage":"render","module":"Sprite","blend":"additive","intensity":4}
)";
const char* kSmoke = R"({"vfx":"Smoke"}
{"emitter":"Puffs","max":500,"duration":1,"loop":true}
{"stage":"emitter-update","module":"SpawnRate","rate":30}
{"stage":"particle-spawn","module":"Lifetime","value":1}
{"stage":"particle-spawn","module":"PositionSphere","radius":0.2}
{"stage":"particle-spawn","module":"Velocity","value":[0,1,0],"random":[0.2,0,0.2]}
{"stage":"particle-update","module":"Drag","value":0.5}
{"stage":"particle-update","module":"SizeOverLife","curve":[[0,0.5],[1,2]]}
{"stage":"particle-update","module":"Warp","value":1}
{"stage":"render","module":"Sprite","blend":"alpha"}
)";

std::shared_ptr<vfx::EffectDef> Parse(const char* text, std::vector<std::string>* problems = nullptr) {
  auto d = std::make_shared<vfx::EffectDef>();
  std::string err;
  CHECK(vfx::ParseEffect(text, *d, problems, &err));
  return d;
}

uint64_t StateHash(const VfxWorld& w) {
  uint64_t h = 1469598103934665603ull;
  for (const VfxWorld::Effect& in : w.All())
    for (const VfxWorld::EmitterState& e : in.emitters)
      for (const VfxWorld::Particle& p : e.particles)
        for (float f : {p.pos.x, p.pos.y, p.pos.z, p.vel.y, p.life}) {
          uint32_t bits;
          std::memcpy(&bits, &f, 4);
          h = (h ^ bits) * 1099511628211ull;
        }
  return h;
}
} // namespace

TEST_CASE("vfx: .vfx text becomes an effect; unknown modules are reported, the rest loads (queue 6-VFX P-1)") {
  auto spark = Parse(kSpark);
  CHECK(spark->name == "Spark" && spark->emitters.size() == 1);
  const vfx::EmitterDef& e = spark->emitters[0];
  CHECK(e.max == 40 && !e.loop && std::abs(e.duration - 0.2f) < 1e-6f && e.modules.size() == 8);
  CHECK(e.blend == vfx::Blend::Additive && e.intensity == 4);
  CHECK(e.modules[1].kind == vfx::Kind::Lifetime && e.modules[1].value.lo == 0.3f && e.modules[1].value.hi == 0.5f);
  CHECK(std::abs(spark->LongestLife() - 0.5f) < 1e-6f);
  std::vector<std::string> problems;
  auto smoke = Parse(kSmoke, &problems);
  CHECK(smoke->emitters[0].modules.size() == 7 && problems.size() == 1 && problems[0].find("Warp") != std::string::npos);
  vfx::EffectDef none;
  std::string err;
  CHECK(!vfx::ParseEffect(R"({"vfx":"Empty"})", none, nullptr, &err) && !err.empty());
  CHECK(std::abs(vfx::SampleCurve({{0.f, 0.5f}, {1.f, 2.f}}, 0.5f) - 1.25f) < 1e-5f);
  CHECK(std::abs(vfx::SampleGradient({{0.f, glm::vec4(1)}, {1.f, glm::vec4(0)}}, 0.25f).a - 0.75f) < 1e-5f);
}

TEST_CASE("vfx: bursts and rates spawn by count, particles age and die, gravity and the ground act (queue 6-VFX P-1)") {
  VfxWorld w;
  w.Reset();
  const uint32_t spark = w.Play(Parse(kSpark), {0, 1, 0});
  w.Step(VfxWorld::kStep);
  CHECK(w.Alive() == 24 && w.Totals().spawned == 24); // the burst at time 0
  float lowest = 1e9f;
  for (int i = 0; i < 12; ++i) w.Step(VfxWorld::kStep); // 0.2 s: none dead yet (life >= 0.3 s)
  CHECK(w.Alive() == 24);
  for (const VfxWorld::Particle& p : w.All()[0].emitters[0].particles) lowest = std::min(lowest, p.pos.y);
  CHECK(lowest >= 0.f); // above the ground plane
  for (int i = 0; i < 30; ++i) w.Step(VfxWorld::kStep); // 0.7 s in all: every spark has died, the one-shot is gone
  CHECK(w.Alive() == 0 && w.Totals().died == 24 && !w.Playing(spark) && w.Instances() == 0);

  // one drop: gravity pulls it down, the ground plane bounces it (0.5 of the speed back), never below the plane
  const uint32_t drop = w.Play(Parse(R"({"vfx":"Drop"}
{"emitter":"D","max":1,"duration":0.1,"loop":false}
{"stage":"emitter-update","module":"SpawnBurst","count":1}
{"stage":"particle-spawn","module":"Lifetime","value":3}
{"stage":"particle-update","module":"Gravity","value":[0,-10,0]}
{"stage":"particle-update","module":"Collision","plane":0,"bounce":0.5}
)"), {0, 1, 0});
  float minY = 1e9f, yAt30 = 0, bounceUp = 0;
  for (int i = 1; i <= 90; ++i) {
    w.Step(VfxWorld::kStep);
    const VfxWorld::Particle& p = w.All()[0].emitters[0].particles[0];
    minY = std::min(minY, p.pos.y);
    if (i == 18) yAt30 = p.pos.y; // 0.3 s: 1 - 0.5 g t^2 = 0.55 (explicit Euler: a little lower)
    if (p.vel.y > 0) bounceUp = std::max(bounceUp, p.vel.y);
  }
  CHECK(yAt30 < 0.6f && yAt30 > 0.4f && minY >= 0.f && bounceUp > 1.5f && bounceUp < 2.6f); // hit at ~4.5 m/s, half back
  w.Stop(drop, true);
  w.Step(VfxWorld::kStep);
  CHECK(w.Alive() == 0 && !w.Playing(drop));

  const uint32_t smoke = w.Play(Parse(kSmoke), {0, 0, 0});
  for (int i = 0; i < 30; ++i) w.Step(VfxWorld::kStep); // 0.5 s at 30/s: 15
  CHECK(w.Alive(smoke) == 15);
  for (int i = 0; i < 90; ++i) w.Step(VfxWorld::kStep); // 2 s: steady state = rate x lifetime = 30
  CHECK(w.Alive(smoke) >= 29 && w.Alive(smoke) <= 31);
  w.Stop(smoke); // no new ones; the live ones finish
  for (int i = 0; i < 61; ++i) w.Step(VfxWorld::kStep);
  CHECK(w.Alive() == 0 && !w.Playing(smoke));
}

TEST_CASE("vfx: the same seed gives the same particles; the quality tier caps the scene's particles (queue 6-VFX P-1)") {
  auto def = Parse(kSpark);
  uint64_t hashes[2];
  for (uint64_t& h : hashes) {
    VfxWorld w;
    w.Reset();
    w.Play(def, {0, 2, 0}, {1, 0, 0, 0}, 42);
    for (int i = 0; i < 10; ++i) w.Step(VfxWorld::kStep);
    h = StateHash(w);
  }
  CHECK(hashes[0] == hashes[1]);
  VfxWorld other;
  other.Reset();
  other.Play(def, {0, 2, 0}, {1, 0, 0, 0}, 43);
  for (int i = 0; i < 10; ++i) other.Step(VfxWorld::kStep);
  CHECK(StateHash(other) != hashes[0]);

  CHECK(VfxWorld::TierLimit(QualityTier::Potato) == 500 && VfxWorld::TierLimit(QualityTier::Low) == 2000 &&
        VfxWorld::TierLimit(QualityTier::Medium) == 10000 && VfxWorld::TierLimit(QualityTier::High) == 100000 &&
        VfxWorld::TierLimit(QualityTier::Ultra) == 500000);
  const char* rain = R"({"vfx":"Rain"}
{"emitter":"Drops","max":100000,"duration":1}
{"stage":"emitter-update","module":"SpawnBurst","count":3000}
{"stage":"particle-spawn","module":"Lifetime","value":5}
)";
  for (QualityTier t : {QualityTier::Potato, QualityTier::Low, QualityTier::Medium}) {
    VfxWorld w;
    w.Reset();
    w.SetTier(t);
    w.Play(Parse(rain), {0, 0, 0});
    w.Play(Parse(rain), {5, 0, 0}); // two effects share the budget
    w.Step(VfxWorld::kStep);
    const size_t want = std::min<size_t>(6000, VfxWorld::TierLimit(t));
    CHECK(w.Alive() == want && w.Totals().dropped == 6000 - want);
  }
  VfxWorld w; // an emitter's own max holds too
  w.Reset();
  w.Play(Parse(kSpark), {0, 1, 0});
  w.Play(Parse(R"({"vfx":"Big"}
{"emitter":"E","max":10,"duration":1,"loop":false}
{"stage":"emitter-update","module":"SpawnBurst","count":50}
)"), {0, 1, 0});
  w.Step(VfxWorld::kStep);
  CHECK(w.Alive() == 34 && w.Totals().dropped == 40);
}

TEST_CASE("vfx: ParticleSystem components play on start, follow their object, Play / Stop, removed objects stop (queue 6-VFX P-1)") {
  Scene s;
  std::string err;
  CHECK(ReadSceneText(s, R"({"v":1,"scene":"P"}
{"id":"fire","name":"Fire","Transform":{"position":[3,0,0]},"ParticleSystem":{"effect":"Smoke","loop":"Loop"}}
{"id":"idle","name":"Idle","ParticleSystem":{"effect":"Smoke","playOnStart":false}}
)",
                      nullptr, nullptr, &err));
  // the effects come from a folder: write one
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bdg-vfx-test" / "Effects";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  { std::ofstream(dir / "Smoke.vfx", std::ios::binary) << kSmoke; }
  const std::u8string root = dir.parent_path().generic_u8string();
  VfxWorld w;
  w.Begin({std::string(root.begin(), root.end())});
  w.Sync(s);
  CHECK(w.Instances() == 1 && w.All()[0].object == "fire" && std::abs(w.All()[0].position.x - 3) < 1e-5f);
  for (int i = 0; i < 30; ++i) w.Step(VfxWorld::kStep);
  w.Sync(s);
  auto* fire = static_cast<ParticleSystem*>(s.Find("fire")->Find("ParticleSystem")->data);
  auto* idle = static_cast<ParticleSystem*>(s.Find("idle")->Find("ParticleSystem")->data);
  CHECK(fire->particles == 15 && idle->particles == 0);
  bool near = true;
  for (const VfxWorld::Particle& p : w.All()[0].emitters[0].particles) near = near && std::abs(p.pos.x - 3) < 1.f; // spawned at the object
  CHECK(near);
  idle->Play();
  w.Sync(s);
  CHECK(w.Instances() == 2);
  fire->Stop();
  w.Sync(s);
  for (int i = 0; i < 70; ++i) w.Step(VfxWorld::kStep);
  w.Sync(s);
  CHECK(fire->particles == 0 && idle->particles > 0 && w.Instances() == 1);
  s.Destroy("idle"); // its object went away: the effect stops emitting and ends
  w.Sync(s);
  for (int i = 0; i < 70; ++i) w.Step(VfxWorld::kStep);
  CHECK(w.Instances() == 0 && w.Alive() == 0);
  std::string why;
  CHECK(!w.Find("Nope", &why) && why.find("Nope") != std::string::npos);
  w.End();
  std::filesystem::remove_all(dir.parent_path(), ec);
}
