#include "Runtime/PlayMode.h"
#include "Core/Log.h"
#include "Runtime/EditorModel.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
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
  time_ = 0;
  frames_ = 0;
  playing_ = true;
  m.playing = true;
  LogInfo("play: started ({} objects){}", m.scene.ObjectCount(), paused_ ? ", paused" : "");
  return true;
}

bool PlayMode::Stop(EditorModel& m) {
  if (!playing_) return false;
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
