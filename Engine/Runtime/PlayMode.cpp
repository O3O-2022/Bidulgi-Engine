#include "Runtime/PlayMode.h"
#include "Core/Log.h"
#include "Runtime/EditorModel.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include "Assets/AssetDatabase.h"
#include "Core/Utf8Path.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace bdg {
namespace {
// "path" of a set command's args ({"path":"Transform.position","value":...}); empty when it has none.
std::string SetPath(const Command& c) {
  const std::string key = "\"path\":\"";
  size_t at = c.args.find(key);
  if (at == std::string::npos) return {};
  at += key.size();
  size_t end = c.args.find('"', at);
  return end == std::string::npos ? std::string() : c.args.substr(at, end - at);
}
} // namespace

bool PlayMode::Start(EditorModel& m) {
  if (playing_) return false;
  snapshot_ = WriteSceneText(m.scene, m.sceneName);
  selection_ = m.Selection();
  m.bus.PushLayer();
  pending_.clear();
  shaker_.StopAll(true);
  shakesStarted_ = false;
  shakeCache_.clear();
  time_ = 0;
  frames_ = 0;
  playing_ = true;
  m.playing = true;
  LogInfo("play: started ({} objects){}", m.scene.ObjectCount(), paused_ ? ", paused" : "");
  return true;
}

bool PlayMode::Stop(EditorModel& m) {
  if (!playing_) return false;
  shaker_.StopAll(true);
  // What the user changed while playing (done steps only): the last value per object field.
  std::map<std::pair<std::string, std::string>, size_t> latest;
  std::vector<Change> changes;
  for (const Command& c : m.bus.LayerCommands()) {
    if (c.name != "set") continue;
    const std::string path = SetPath(c);
    if (path.empty()) continue;
    auto key = std::make_pair(c.target, path);
    auto it = latest.find(key);
    if (it != latest.end()) changes[it->second].cmd = c;
    else {
      latest[key] = changes.size();
      changes.push_back({c, {}});
    }
  }
  m.bus.PopLayer();
  // Back to the edited world, exactly as it was (the same ids, so references and the undo history still fit).
  m.scene.Clear();
  std::string err;
  if (!ReadSceneText(m.scene, snapshot_, nullptr, nullptr, &err)) LogError("play: could not restore the edited world: {}", err);
  std::vector<std::string> sel;
  for (const std::string& id : selection_)
    if (m.scene.Find(id)) sel.push_back(id);
  m.SetSelection(sel);
  m.SceneReplaced();
  // Offer only the fields of objects that exist in the edited world (objects made while playing are gone).
  pending_.clear();
  for (Change& c : changes) {
    const SceneObject* o = m.scene.Find(c.cmd.target);
    if (!o) continue;
    c.label = std::format("{}: {}", o->name, SetPath(c.cmd));
    c.cmd.source = "ui";
    c.cmd.time = -1;
    pending_.push_back(std::move(c));
  }
  playing_ = false;
  paused_ = step_ = false;
  m.playing = false;
  LogInfo("play: stopped after {:.2f} s ({} frames), {} changed value(s) can be kept", time_, frames_, pending_.size());
  return true;
}

void PlayMode::SetTimeScale(float s) { timeScale_ = std::clamp(s, 0.1f, 4.f); }

void PlayMode::Tick(EditorModel& m, double realDt) {
  if (!playing_) return;
  double dt = realDt * timeScale_;
  if (paused_) {
    if (!step_) return;
    dt = kStepSeconds; // one frame, whatever the time scale
    step_ = false;
  }
  time_ += dt;
  ++frames_;
  bool changed = false;
  std::vector<std::string> stack(m.scene.Roots().begin(), m.scene.Roots().end());
  while (!stack.empty()) {
    SceneObject* o = m.scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.begin(), o->children.end());
    Component* rc = o->Find("Rotator");
    Component* tc = rc ? o->Find("Transform") : nullptr;
    if (!tc) continue;
    const Rotator& r = *static_cast<const Rotator*>(rc->data);
    Transform& t = *static_cast<Transform*>(tc->data);
    auto wrap = [](float a) { return a - 360.f * std::floor((a + 180.f) / 360.f); };
    t.rotation = {wrap(t.rotation.x + r.speed.x * (float)dt), wrap(t.rotation.y + r.speed.y * (float)dt), wrap(t.rotation.z + r.speed.z * (float)dt)};
    changed = true;
  }
  if (changed) m.ValuesChanged();
  TickShakes(m, (float)dt);
}

// A shake by name: built-in ("bdg:Heavy") or a .camshake asset by path, file name or name without extension.
bool PlayMode::FindShake(EditorModel& m, const std::string& name, CameraShakeDef& out) {
  if (auto it = shakeCache_.find(name); it != shakeCache_.end()) { out = it->second; return true; }
  CameraShakeDef d;
  bool ok = BuiltinCameraShake(name, d);
  if (!ok && m.assets)
    for (const AssetRecord* r : m.assets->All()) {
      const std::string file = r->path.substr(r->path.find_last_of('/') == std::string::npos ? 0 : r->path.find_last_of('/') + 1);
      if (!file.ends_with(".camshake") || (r->path != name && file != name && file.substr(0, file.size() - 9) != name)) continue;
      std::string text, err;
      const std::u8string root = std::filesystem::path(std::u8string((const char8_t*)m.assets->Root().c_str())).u8string() + u8"/";
      if (ReadTextU8(std::string((const char*)root.data(), root.size()) + r->path, text) && ParseCameraShake(text, d, &err)) ok = true;
      else LogWarn("play: camera shake {}: {}", r->path, err.empty() ? "cannot read" : err);
      break;
    }
  if (!ok) {
    LogWarn("play: no camera shake \"{}\" (bdg:Light, bdg:Medium, bdg:Heavy, bdg:Explosion, bdg:Handheld or a .camshake asset)", name);
    d = CameraShakeDef{}; // remember the miss: warn once
    d.location = d.rotation = glm::vec3(0);
  }
  shakeCache_[name] = d;
  out = d;
  return ok;
}

void PlayMode::TickShakes(EditorModel& m, float dt) {
  // the game camera: the first Camera in hierarchy order (as the Game view)
  glm::vec3 camPos(0);
  std::vector<std::string> order, stack(m.scene.Roots().rbegin(), m.scene.Roots().rend());
  while (!stack.empty()) {
    const std::string id = stack.back();
    stack.pop_back();
    const SceneObject* o = m.scene.Find(id);
    if (!o) continue;
    order.push_back(id);
    for (auto c = o->children.rbegin(); c != o->children.rend(); ++c) stack.push_back(*c);
  }
  bool haveCam = false;
  for (const std::string& id : order)
    if (SceneObject* o = m.scene.Find(id); o && o->Find("Camera") && !haveCam) {
      camPos = glm::vec3(m.scene.WorldMatrix(id)[3]);
      haveCam = true;
    }
  const bool first = !shakesStarted_;
  shakesStarted_ = true;
  for (const std::string& id : order) {
    SceneObject* o = m.scene.Find(id);
    if (Component* c = o->Find("Camera")) { // Camera.Shake(...) calls
      Camera& cam = *static_cast<Camera*>(c->data);
      for (auto& [name, scale] : cam.pendingShakes) {
        CameraShakeDef d;
        if (FindShake(m, name, d)) shaker_.Start(d, scale);
      }
      cam.pendingShakes.clear();
    }
    if (Component* c = o->Find("CameraShakeSource")) {
      CameraShakeSource& src = *static_cast<CameraShakeSource*>(c->data);
      int plays = src.pendingPlays + (first && src.playOnStart ? 1 : 0);
      src.pendingPlays = 0;
      const float k = src.scale * WorldShakeScale(glm::vec3(m.scene.WorldMatrix(id)[3]), camPos, src.innerRadius, src.outerRadius, src.falloff);
      CameraShakeDef d;
      for (; plays > 0; --plays)
        if (k > 0 && FindShake(m, src.shake, d)) shaker_.Start(d, k);
    }
  }
  shaker_.Update(dt);
}

int PlayMode::KeepPending(EditorModel& m) {
  if (pending_.empty()) return 0;
  m.bus.BeginGroup(std::format("Keep {} value(s) from play mode", pending_.size()), "ui");
  int kept = 0;
  for (const Change& c : pending_) {
    std::string err;
    if (m.bus.Execute(c.cmd, &err)) ++kept;
    else LogWarn("play: could not keep {}: {}", c.label, err);
  }
  if (kept) m.bus.EndGroup();
  else { std::string err; m.bus.CancelGroup(&err); }
  LogInfo("play: kept {} value(s)", kept);
  pending_.clear();
  return kept;
}
}
