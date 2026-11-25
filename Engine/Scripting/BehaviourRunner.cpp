#include "Scripting/BehaviourRunner.h"
#include "Scripting/DotNetHost.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstdlib>
#include <format>
#include <yyjson.h>

namespace bdg {

void BehaviourRunner::Begin(Scene& scene, CommandBus& bus) {
  End();
  DotNetHost& host = DotNetHost::Instance();
  if (!host.Running()) return;
  host.Attach(&scene, &bus);
  active_ = true;
  acc_ = time_ = 0;
  frame_ = 0;
  Sync(scene); // Awake / OnEnable for what is there when Play starts
}

void BehaviourRunner::Sync(Scene& scene) {
  DotNetHost& host = DotNetHost::Instance();
  for (Inst& i : insts_) i.seen = false;
  // hierarchy order, components in their order: the same order every run (deterministic event order)
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
    for (const Component& c : o->components) {
      if (!host.IsBehaviourType(c.type)) continue;
      const bool enabled = *static_cast<const bool*>(c.data); // ScriptComponentData::enabled is the first field
      auto it = std::find_if(insts_.begin(), insts_.end(), [&](const Inst& i) { return i.object == o->id && i.type == c.type->name; });
      const std::string fields = DotNetHost::FieldsJson(c.data, *c.type);
      if (it == insts_.end()) { // the saved values are on the C# fields before Awake (Unity order)
        Inst n{o->id, c.type->name, host.CreateBehaviour(o->id, c.type->name, enabled, fields), enabled, true, fields};
        if (n.handle) insts_.push_back(std::move(n));
        continue;
      }
      it->seen = true;
      if (it->sent != fields) { // edited in the Inspector while playing: the script sees the new values
        it->sent = fields;
        host.SetBehaviourFields(it->handle, fields);
      }
      if (it->enabled != enabled) {
        it->enabled = enabled;
        host.SetBehaviourEnabled(it->handle, enabled);
      }
    }
  }
  for (Inst& i : insts_)
    if (!i.seen) host.DestroyBehaviour(i.handle);
  std::erase_if(insts_, [](const Inst& i) { return !i.seen; });
}

void BehaviourRunner::Frame(Scene& scene, CommandBus& bus, double dt) {
  if (!active_) return;
  DotNetHost& host = DotNetHost::Instance();
  host.Attach(&scene, &bus);
  Sync(scene);
  ++frame_;
  time_ += dt;
  host.TickBehaviours(DotNetHost::Phase::Update, dt, time_, frame_);
  acc_ += dt;
  lastFixed_ = 0;
  while (acc_ >= fixedStep && lastFixed_ < maxFixedSteps) {
    acc_ -= fixedStep;
    ++lastFixed_;
    if (beforeFixedStep) beforeFixedStep(fixedStep);
    host.TickBehaviours(DotNetHost::Phase::FixedUpdate, fixedStep, time_, frame_);
  }
  if (acc_ >= fixedStep) acc_ = 0; // too slow: drop the rest instead of spiralling
  host.TickBehaviours(DotNetHost::Phase::LateUpdate, dt, time_, frame_);
  Pull(scene, bus);
}

void BehaviourRunner::SendEvent(const std::string& object, const char* method, const std::string& json) {
  for (const Inst& i : insts_)
    if (i.object == object && i.enabled) DotNetHost::Instance().SendBehaviourEvent(i.handle, method, json);
}

void BehaviourRunner::Pull(Scene& scene, CommandBus& bus) {
  DotNetHost& host = DotNetHost::Instance();
  for (Inst& i : insts_) {
    SceneObject* o = scene.Find(i.object);
    Component* c = o ? o->Find(i.type) : nullptr;
    if (!c) continue;
    const std::string now = host.GetBehaviourFields(i.handle);
    yyjson_doc* d = now.empty() ? nullptr : yyjson_read(now.data(), now.size(), 0);
    if (!d) continue;
    yyjson_val* root = yyjson_doc_get_root(d);
    for (const FieldInfo& f : c->type->fields) {
      if (f.name == "enabled") continue; // a component setting, not script state
      yyjson_val* v = yyjson_obj_get(root, f.name.c_str());
      if (!v) continue;
      char* text = yyjson_val_write(v, 0, nullptr);
      if (!text) continue;
      // compare in the engine's own JSON form (the C# text may spell the same number differently)
      void* scratch = c->type->create ? c->type->create() : nullptr;
      std::string engineNow = Registry::ToJson(static_cast<unsigned char*>(c->data) + f.offset, f.value), scriptNow;
      if (scratch && Registry::FromJson(static_cast<unsigned char*>(scratch) + f.offset, f.value, text, nullptr))
        scriptNow = Registry::ToJson(static_cast<unsigned char*>(scratch) + f.offset, f.value);
      if (scratch) c->type->destroy(scratch);
      if (!scriptNow.empty() && scriptNow != engineNow) {
        Command cmd; // play mode: applied without an undo step (CommandBus, source "script")
        cmd.name = "set";
        cmd.target = i.object;
        cmd.source = "script";
        cmd.args = std::format("{{\"path\":\"{}.{}\",\"value\":{}}}", i.type, f.name, scriptNow);
        std::string err;
        if (!bus.Execute(cmd, &err)) LogWarn("C#: {}.{} on {}: {}", i.type, f.name, i.object, err);
      }
      std::free(text);
    }
    yyjson_doc_free(d);
    i.sent = DotNetHost::FieldsJson(c->data, *c->type); // nothing to push back next frame
  }
}

void BehaviourRunner::End() {
  DotNetHost& host = DotNetHost::Instance();
  for (const Inst& i : insts_) host.DestroyBehaviour(i.handle);
  if (active_) host.TickBehaviours(DotNetHost::Phase::End, 0, time_, frame_); // timers / waits not tied to a behaviour
  insts_.clear();
  active_ = false;
}

} // namespace bdg
