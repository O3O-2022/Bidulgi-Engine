#include "Scripting/BehaviourRunner.h"
#include "Scripting/DotNetHost.h"
#include <algorithm>

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
      if (it == insts_.end()) {
        Inst n{o->id, c.type->name, host.CreateBehaviour(o->id, c.type->name, enabled), enabled, true};
        if (n.handle) insts_.push_back(std::move(n));
        continue;
      }
      it->seen = true;
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
    host.TickBehaviours(DotNetHost::Phase::FixedUpdate, fixedStep, time_, frame_);
  }
  if (acc_ >= fixedStep) acc_ = 0; // too slow: drop the rest instead of spiralling
  host.TickBehaviours(DotNetHost::Phase::LateUpdate, dt, time_, frame_);
}

void BehaviourRunner::End() {
  DotNetHost& host = DotNetHost::Instance();
  for (const Inst& i : insts_) host.DestroyBehaviour(i.handle);
  insts_.clear();
  active_ = false;
}

} // namespace bdg
