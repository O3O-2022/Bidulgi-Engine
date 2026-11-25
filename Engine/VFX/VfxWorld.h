#pragma once
// Particle simulation on the CPU (queue 6-VFX P-1, Docs/features/particles.md). Each playing effect is an instance
// with one particle list per emitter; every fixed step (60 Hz, at most 4 per frame) runs the emitter's module stack:
// emitter update (spawn rate, bursts) -> particle spawn modules for the new ones -> particle update modules -> age and
// death. Effects come from ParticleSystem components (Sync) and from scripts (C# Vfx.Play -> Play). Seeded: the same
// effect, seed and steps give the same particles.
//
// The quality tier caps the particles alive in the whole scene (potato 500, low 2000, medium 10000, high 100000,
// ultra 500000); spawns over the cap are dropped (counted). Play mode owns the world: Begin at Play, End at Stop.
#include "Render3D/Quality.h"
#include "VFX/VfxAsset.h"
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bdg {
class Scene;

class VfxWorld {
public:
  static VfxWorld& Instance(); // the game's (play mode, renderer, script requests)

  struct Particle {
    glm::vec3 pos{0}, vel{0};
    glm::vec4 color{1};
    float size = 0.1f, age = 0, life = 1;
  };
  struct EmitterState {
    std::vector<Particle> particles;
    float time = 0;     // since the instance started
    float spawnAcc = 0; // fractional SpawnRate particles carried to the next step
    int cycle = -1;     // the loop cycle whose bursts have fired
    float cycleTime = 0;
  };
  struct Effect {
    uint32_t id = 0;
    std::shared_ptr<const vfx::EffectDef> def;
    std::string object;        // owning object (ParticleSystem) or empty (Vfx.Play)
    glm::vec3 position{0};
    glm::quat rotation{1, 0, 0, 0};
    vfx::Rng rng;
    int loop = 0;              // 0 = as the effect says, 1 = loop, 2 = once
    bool emitting = true;
    std::vector<EmitterState> emitters;
  };
  struct Stats {
    uint64_t spawned = 0, died = 0, dropped = 0; // since Begin
  };

  void Begin(const std::vector<std::string>& assetFolders); // play started: where effects are looked for
  void End();                                                // play stopped: everything goes
  bool Active() const { return active_; }

  // Effects by name: a .vfx path relative to an assets folder, or a file name with or without .vfx found anywhere
  // under them ("Spark" -> Assets/Effects/Spark.vfx). Read once per play; a broken one is reported once.
  std::shared_ptr<const vfx::EffectDef> Find(const std::string& effect, std::string* error = nullptr);
  uint32_t Play(std::shared_ptr<const vfx::EffectDef> def, const glm::vec3& position, const glm::quat& rotation = {1, 0, 0, 0},
                uint32_t seed = 0, int loop = 0, const std::string& object = {});
  uint32_t Play(const std::string& effect, const glm::vec3& position, const glm::quat& rotation = {1, 0, 0, 0}, uint32_t seed = 0,
                std::string* error = nullptr);
  void Stop(uint32_t id, bool clear = false); // no new particles (the live ones finish), or all gone at once
  bool Playing(uint32_t id) const;           // still emitting or has live particles
  void Move(uint32_t id, const glm::vec3& position, const glm::quat& rotation);

  // ParticleSystem components: play on start, Play / Stop calls, follow the object, removed objects stop; writes the
  // live count back into the component.
  void Sync(Scene& scene);
  void Tick(float dt);              // fixed steps of 1/60 s (at most 4 per call)
  void Step(float dt);              // exactly one step (tests)

  void SetTier(QualityTier t) { limit_ = TierLimit(t); }
  void SetLimit(size_t n) { limit_ = n; }
  size_t Limit() const { return limit_; }
  static size_t TierLimit(QualityTier t);
  size_t Alive() const { return alive_; }
  size_t Instances() const { return instances_.size(); }
  size_t Alive(uint32_t id) const;
  const Stats& Totals() const { return stats_; }
  double SimTime() const { return simTime_; } // seconds simulated since Begin (slow frames drop time)
  const std::vector<Effect>& All() const { return instances_; }
  void Reset(); // empty, active, for tests without play mode

  static constexpr float kStep = 1.f / 60.f;

private:
  void StepInstance(Effect& in, float dt);
  void Spawn(Effect& in, const vfx::EmitterDef& e, EmitterState& st, int count);
  Effect* Get(uint32_t id);
  const Effect* Get(uint32_t id) const;

  std::vector<Effect> instances_;
  std::map<std::string, std::shared_ptr<const vfx::EffectDef>> cache_;
  std::map<std::string, std::string> failed_;
  std::vector<std::string> folders_;
  std::map<std::string, uint32_t> owned_; // object id -> instance (ParticleSystem)
  std::map<std::string, bool> started_;   // objects whose playOnStart already ran
  uint32_t nextId_ = 1, nextSeed_ = 1;
  size_t limit_ = 10000, alive_ = 0;
  float acc_ = 0;
  double simTime_ = 0;
  bool active_ = false;
  Stats stats_;
};

} // namespace bdg
