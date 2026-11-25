#include "VFX/VfxWorld.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Scene/Scene.h"
#include "VFX/VfxComponents.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}
uint32_t HashId(const std::string& s) {
  uint32_t h = 2166136261u;
  for (char c : s) h = (h ^ (uint8_t)c) * 16777619u;
  return h ? h : 1;
}
// A unit vector inside a cone of `degrees` around `axis`.
glm::vec3 InCone(vfx::Rng& r, const glm::vec3& axis, float degrees) {
  const float cosMax = std::cos(glm::radians(degrees));
  const float z = 1.f - r.Unit() * (1.f - cosMax), phi = r.Unit() * glm::two_pi<float>();
  const float s = std::sqrt(std::max(0.f, 1.f - z * z));
  const glm::vec3 local(s * std::cos(phi), z, s * std::sin(phi)); // around +Y
  const glm::vec3 up(0, 1, 0);
  const float d = glm::dot(up, axis);
  if (d > 0.9999f) return local;
  if (d < -0.9999f) return -local;
  const glm::vec3 a = glm::normalize(glm::cross(up, axis));
  return glm::angleAxis(std::acos(std::clamp(d, -1.f, 1.f)), a) * local;
}
glm::vec3 InSphere(vfx::Rng& r, bool surface) {
  const float z = r.Range(-1, 1), phi = r.Unit() * glm::two_pi<float>(), s = std::sqrt(std::max(0.f, 1.f - z * z));
  const glm::vec3 dir(s * std::cos(phi), z, s * std::sin(phi));
  return surface ? dir : dir * std::cbrt(r.Unit());
}
} // namespace

VfxWorld& VfxWorld::Instance() {
  static VfxWorld world;
  return world;
}

size_t VfxWorld::TierLimit(QualityTier t) {
  switch (t) {
    case QualityTier::Potato: return 500;
    case QualityTier::Low: return 2000;
    case QualityTier::Medium: return 10000;
    case QualityTier::High: return 100000;
    case QualityTier::Ultra: return 500000;
  }
  return 10000;
}

void VfxWorld::Reset() {
  instances_.clear();
  owned_.clear();
  started_.clear();
  alive_ = 0;
  acc_ = 0;
  simTime_ = 0;
  nextId_ = nextSeed_ = 1;
  stats_ = {};
  active_ = true;
}

void VfxWorld::Begin(const std::vector<std::string>& assetFolders) {
  Reset();
  folders_ = assetFolders;
  cache_.clear();
  failed_.clear();
}

void VfxWorld::End() {
  if (active_ && stats_.spawned) LogInfo("vfx: stopped: {} particles spawned, {} dropped over the budget", stats_.spawned, stats_.dropped);
  instances_.clear();
  owned_.clear();
  started_.clear();
  alive_ = 0;
  active_ = false;
}

std::shared_ptr<const vfx::EffectDef> VfxWorld::Find(const std::string& effect, std::string* error) {
  if (auto it = cache_.find(effect); it != cache_.end()) return it->second;
  if (auto it = failed_.find(effect); it != failed_.end()) {
    if (error) *error = it->second;
    return nullptr;
  }
  std::error_code ec;
  fs::path found;
  const std::string file = effect.ends_with(".vfx") ? effect : effect + ".vfx";
  for (const std::string& f : folders_)
    if (fs::is_regular_file(PathU8(f) / PathU8(file), ec)) { found = PathU8(f) / PathU8(file); break; }
  if (found.empty()) { // just a name: anywhere under the folders (the first in path order)
    const std::string leaf = U(PathU8(file).filename());
    std::vector<fs::path> hits;
    for (const std::string& f : folders_)
      for (auto it = fs::recursive_directory_iterator(PathU8(f), fs::directory_options::skip_permission_denied, ec);
           !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && U(it->path().filename()) == leaf) hits.push_back(it->path());
    std::sort(hits.begin(), hits.end());
    if (!hits.empty()) found = hits.front();
  }
  std::string err;
  auto def = std::make_shared<vfx::EffectDef>();
  std::vector<std::string> problems;
  if (found.empty()) err = "no effect \"" + effect + "\" (a .vfx file under Assets)";
  else if (!vfx::LoadEffectFile(U(found), *def, &problems, &err)) err = U(found) + ": " + err;
  for (const std::string& p : problems) LogWarn("vfx: {}: {}", U(found), p);
  if (!err.empty()) {
    LogWarn("vfx: {}", err);
    failed_[effect] = err;
    if (error) *error = err;
    return nullptr;
  }
  if (def->name.empty()) def->name = U(found.stem());
  cache_[effect] = def;
  return def;
}

uint32_t VfxWorld::Play(std::shared_ptr<const vfx::EffectDef> def, const glm::vec3& position, const glm::quat& rotation, uint32_t seed, int loop,
                        const std::string& object) {
  if (!def) return 0;
  Effect in;
  in.id = nextId_++;
  in.def = std::move(def);
  in.object = object;
  in.position = position;
  in.rotation = rotation;
  in.rng = vfx::Rng(seed ? seed : 0x5EED0000u + nextSeed_++);
  in.loop = loop;
  in.emitters.resize(in.def->emitters.size());
  instances_.push_back(std::move(in));
  return instances_.back().id;
}

uint32_t VfxWorld::Play(const std::string& effect, const glm::vec3& position, const glm::quat& rotation, uint32_t seed, std::string* error) {
  return Play(Find(effect, error), position, rotation, seed);
}

VfxWorld::Effect* VfxWorld::Get(uint32_t id) {
  for (Effect& i : instances_)
    if (i.id == id) return &i;
  return nullptr;
}
const VfxWorld::Effect* VfxWorld::Get(uint32_t id) const {
  for (const Effect& i : instances_)
    if (i.id == id) return &i;
  return nullptr;
}

void VfxWorld::Stop(uint32_t id, bool clear) {
  Effect* in = Get(id);
  if (!in) return;
  in->emitting = false;
  if (clear)
    for (EmitterState& e : in->emitters) {
      alive_ -= e.particles.size();
      e.particles.clear();
    }
}

bool VfxWorld::Playing(uint32_t id) const { return Get(id) != nullptr; }

size_t VfxWorld::Alive(uint32_t id) const {
  const Effect* in = Get(id);
  size_t n = 0;
  if (in)
    for (const EmitterState& e : in->emitters) n += e.particles.size();
  return n;
}

void VfxWorld::Move(uint32_t id, const glm::vec3& position, const glm::quat& rotation) {
  if (Effect* in = Get(id)) {
    in->position = position;
    in->rotation = rotation;
  }
}

void VfxWorld::Sync(Scene& scene) {
  if (!active_) return;
  std::map<std::string, bool> seen;
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
    Component* c = o->Find("ParticleSystem");
    if (!c) continue;
    ParticleSystem& ps = *static_cast<ParticleSystem*>(c->data);
    seen[o->id] = true;
    const glm::mat4 w = scene.WorldMatrix(o->id);
    const glm::vec3 pos(w[3]);
    glm::mat3 basis(w);
    for (int k = 0; k < 3; ++k)
      if (glm::length(basis[k]) > 1e-8f) basis[k] = glm::normalize(basis[k]);
    const glm::quat rot = glm::quat_cast(basis);
    auto owned = owned_.find(o->id);
    uint32_t id = owned != owned_.end() && Get(owned->second) ? owned->second : 0;
    bool play = ps.pendingPlays > 0 || (ps.playOnStart && !started_[o->id]);
    started_[o->id] = true;
    if (ps.pendingStops > 0 && id) Stop(id);
    if (play && !ps.effect.empty()) {
      if (id) Stop(id, true);
      const uint32_t seed = ps.seed ? (uint32_t)ps.seed : HashId(o->id);
      id = Play(Find(ps.effect), pos, rot, seed, (int)ps.loop, o->id);
      if (id) owned_[o->id] = id;
      else owned_.erase(o->id);
    }
    ps.pendingPlays = ps.pendingStops = 0;
    if (id) Move(id, pos, rot);
    ps.particles = (int32_t)Alive(id);
  }
  for (Effect& in : instances_) // the object went away: its effect stops (live particles finish)
    if (!in.object.empty() && !seen.count(in.object)) in.emitting = false;
}

void VfxWorld::Tick(float dt) {
  if (!active_ || dt <= 0) return;
  acc_ += dt;
  int steps = 0;
  while (acc_ >= kStep && steps < 4) {
    Step(kStep);
    acc_ -= kStep;
    ++steps;
  }
  if (acc_ > kStep) acc_ = 0; // too slow: drop the backlog instead of spiralling
}

void VfxWorld::Step(float dt) {
  simTime_ += dt;
  for (Effect& in : instances_) StepInstance(in, dt);
  instances_.erase(std::remove_if(instances_.begin(), instances_.end(), [](const Effect& in) {
                     if (in.emitting) return false;
                     for (const EmitterState& e : in.emitters)
                       if (!e.particles.empty()) return false;
                     return true;
                   }),
                   instances_.end());
  for (auto it = owned_.begin(); it != owned_.end();) it = Get(it->second) ? std::next(it) : owned_.erase(it);
}

void VfxWorld::Spawn(Effect& in, const vfx::EmitterDef& e, EmitterState& st, int count) {
  for (int n = 0; n < count; ++n) {
    if ((int)st.particles.size() >= e.max || alive_ >= limit_) {
      stats_.dropped += (uint64_t)(count - n);
      return;
    }
    Particle p;
    for (const vfx::Module& m : e.modules) {
      if (m.stage != vfx::Stage::ParticleSpawn) continue;
      switch (m.kind) {
        case vfx::Kind::Lifetime: p.life = std::max(m.value.Pick(in.rng), 0.001f); break;
        case vfx::Kind::PositionPoint: p.pos += m.vec; break;
        case vfx::Kind::PositionSphere: p.pos += InSphere(in.rng, m.surface) * m.value.Pick(in.rng); break;
        case vfx::Kind::PositionCone: p.pos += InCone(in.rng, m.vec, m.angle) * (m.value.Pick(in.rng) * in.rng.Unit()); break;
        case vfx::Kind::Velocity:
          p.vel += m.vec + glm::vec3(in.rng.Range(-m.spread.x, m.spread.x), in.rng.Range(-m.spread.y, m.spread.y), in.rng.Range(-m.spread.z, m.spread.z));
          break;
        case vfx::Kind::VelocityCone: p.vel += InCone(in.rng, m.vec, m.angle) * m.speed.Pick(in.rng); break;
        case vfx::Kind::Size: p.size = m.value.Pick(in.rng); break;
        case vfx::Kind::Color: p.color = m.color; break;
        default: break;
      }
    }
    if (!e.local) { // world space: placed and aimed by the instance's transform now
      p.pos = in.position + in.rotation * p.pos;
      p.vel = in.rotation * p.vel;
    }
    st.particles.push_back(p);
    ++alive_;
    ++stats_.spawned;
  }
}

void VfxWorld::StepInstance(Effect& in, float dt) {
  for (size_t ei = 0; ei < in.def->emitters.size(); ++ei) {
    const vfx::EmitterDef& e = in.def->emitters[ei];
    EmitterState& st = in.emitters[ei];
    st.time += dt;
    // emitter update: spawn rate and bursts within the emitter's cycle
    const float t = st.time - e.delay;
    const bool loop = in.loop == 1 || (in.loop == 0 && e.loop);
    if (in.emitting && t >= 0) {
      const int cycle = (int)std::floor(t / e.duration);
      const float local = t - cycle * e.duration;
      const bool live = loop || cycle == 0;
      if (live) {
        if (cycle != st.cycle) {
          st.cycle = cycle;
          st.cycleTime = -1e-6f; // bursts at time 0 fire on the first step of a cycle
        }
        for (const vfx::Module& m : e.modules) {
          if (m.kind == vfx::Kind::SpawnBurst && m.time > st.cycleTime && m.time <= local) Spawn(in, e, st, m.count);
          if (m.kind == vfx::Kind::SpawnRate) {
            st.spawnAcc += m.value.Pick(in.rng) * dt;
            const int n = (int)st.spawnAcc;
            st.spawnAcc -= (float)n;
            if (n > 0) Spawn(in, e, st, n);
          }
        }
        st.cycleTime = local;
      }
    }
    // particle update: forces, motion, collision, age
    for (size_t i = 0; i < st.particles.size();) {
      Particle& p = st.particles[i];
      p.age += dt;
      bool dead = p.age >= p.life;
      if (!dead) {
        for (const vfx::Module& m : e.modules) {
          if (m.kind == vfx::Kind::Gravity) p.vel += m.vec * dt;
          else if (m.kind == vfx::Kind::Drag) p.vel *= std::max(0.f, 1.f - m.value.lo * dt);
        }
        p.pos += p.vel * dt;
        for (const vfx::Module& m : e.modules) {
          if (m.kind != vfx::Kind::Collision) continue;
          const float y = e.local ? (in.position + in.rotation * p.pos).y : p.pos.y;
          if (y < m.plane && p.vel.y < 0) {
            if (m.kind == vfx::Kind::Collision && m.kill) { dead = true; break; }
            if (!e.local) p.pos.y = m.plane;
            p.vel.y = -p.vel.y * m.bounce;
            p.vel.x *= 1.f - m.friction;
            p.vel.z *= 1.f - m.friction;
          }
        }
      }
      if (dead) {
        st.particles[i] = st.particles.back();
        st.particles.pop_back();
        --alive_;
        ++stats_.died;
      } else {
        ++i;
      }
    }
    if (!loop && t > e.duration) st.spawnAcc = 0;
  }
  // a one-shot effect stops emitting once every emitter's cycle is over
  bool anyLive = false;
  for (size_t ei = 0; ei < in.def->emitters.size(); ++ei) {
    const vfx::EmitterDef& e = in.def->emitters[ei];
    const bool loop = in.loop == 1 || (in.loop == 0 && e.loop);
    if (loop || in.emitters[ei].time - e.delay <= e.duration) anyLive = true;
  }
  if (!anyLive) in.emitting = false;
}

} // namespace bdg
