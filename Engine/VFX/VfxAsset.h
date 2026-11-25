#pragma once
// Particle effects (queue 6-VFX P-1, Docs/features/particles.md): the .vfx asset. JSONL like scenes: a header line
// for the effect, then an emitter line followed by its modules, one module per line, run in the order written
// (Niagara's module stack). A module can be switched off with "enabled":false.
//
//   {"vfx":"Spark"}
//   {"emitter":"Sparks","max":48,"duration":0.2,"loop":false}
//   {"stage":"emitter-update","module":"SpawnBurst","count":24}
//   {"stage":"particle-spawn","module":"Lifetime","value":[0.25,0.5]}
//   {"stage":"particle-spawn","module":"VelocityCone","angle":70,"speed":[2,6]}
//   {"stage":"particle-update","module":"Gravity","value":[0,-9.8,0]}
//   {"stage":"particle-update","module":"ColorOverLife","gradient":[[0,[1,0.8,0.3,1]],[1,[1,0.2,0,0]]]}
//   {"stage":"render","module":"Sprite","blend":"additive","intensity":4}
//
// Numbers may be a range [min, max] (picked per particle from the effect's seeded generator). The simulation is on the
// CPU here; the GPU path, curves from the editor and system parameters (@Color) come later.
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bdg::vfx {

// Deterministic generator: the same seed gives the same particles (replays, tests, image comparison).
struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  explicit Rng(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ull | 1) {}
  uint32_t Next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (uint32_t)(s >> 32);
  }
  float Unit() { return (Next() >> 8) * (1.f / 16777216.f); } // [0, 1)
  float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
};

struct Range {
  float lo = 0, hi = 0;
  float Pick(Rng& r) const { return lo == hi ? lo : r.Range(lo, hi); }
};

enum class Stage { EmitterUpdate, ParticleSpawn, ParticleUpdate, Render };
enum class Kind {
  SpawnRate, SpawnBurst,                                                   // emitter update
  Lifetime, PositionPoint, PositionSphere, PositionCone, Velocity, VelocityCone, Size, Color, // particle spawn
  Gravity, Drag, ColorOverLife, SizeOverLife, Collision,                   // particle update
  Sprite,                                                                  // render
};
enum class Blend { Alpha, Additive };

struct Module {
  Kind kind = Kind::Lifetime;
  Stage stage = Stage::ParticleSpawn;
  Range value{1, 1};          // SpawnRate (per s), Lifetime (s), Size (m), Drag (1/s); PositionSphere / Cone radius
  Range speed{1, 1};          // VelocityCone (m/s)
  glm::vec3 vec{0};           // PositionPoint offset, Velocity, Gravity, VelocityCone / PositionCone axis
  glm::vec3 spread{0};        // Velocity: +- random per axis
  glm::vec4 color{1};         // Color
  float angle = 30;           // VelocityCone / PositionCone half angle (degrees)
  float time = 0;             // SpawnBurst: when in the emitter's cycle (s)
  int count = 10;             // SpawnBurst
  bool surface = false;       // PositionSphere: on the surface instead of inside
  float plane = 0, bounce = 0.3f, friction = 0.2f; // Collision: ground plane height (world y), kept speed, lost slide
  bool kill = false;          // Collision: die on touching the ground
  std::vector<std::pair<float, glm::vec4>> gradient; // ColorOverLife (life 0..1 -> colour, multiplied)
  std::vector<std::pair<float, float>> curve;        // SizeOverLife (life 0..1 -> size factor)
  Blend blend = Blend::Alpha; // Sprite
  float intensity = 1;        // Sprite: brightness factor (above 1 glows with bloom)
};

struct EmitterDef {
  std::string name = "Emitter";
  int max = 100;              // particles alive at once (also capped by the quality tier's scene budget)
  float duration = 1, delay = 0;
  bool loop = true;
  bool local = false;         // "space":"local": particles move with the object
  std::vector<Module> modules; // in file order; only enabled ones
  // Derived
  Blend blend = Blend::Alpha;
  float intensity = 1;
};

struct EffectDef {
  std::string name;
  std::vector<EmitterDef> emitters;
  float LongestLife() const; // longest lifetime (s) of any emitter's particles, for "how long until it is done"
};

// Parses .vfx text. Problems (unknown module, wrong stage, bad value) are listed; the rest still loads.
// false only when nothing usable is there (no emitter).
bool ParseEffect(std::string_view text, EffectDef& out, std::vector<std::string>* problems = nullptr, std::string* error = nullptr);
bool LoadEffectFile(const std::string& path, EffectDef& out, std::vector<std::string>* problems = nullptr, std::string* error = nullptr);
const char* KindName(Kind k);

// Life-curve helpers (piecewise linear; empty = 1).
glm::vec4 SampleGradient(const std::vector<std::pair<float, glm::vec4>>& g, float t);
float SampleCurve(const std::vector<std::pair<float, float>>& c, float t);

} // namespace bdg::vfx
