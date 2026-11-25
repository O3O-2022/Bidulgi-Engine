#pragma once
// Runs the C# Behaviours of a scene (queue 4-2): keeps one C# instance per Behaviour component and calls its
// events in the frame order of Docs/architecture/interop.md §5:
//
//   sync    new components -> Awake (+ OnEnable), removed ones -> OnDisable + OnDestroy, "enabled" changes
//   Update  pending Start first (an enabled behaviour's first frame), then Update
//   Fixed   FixedUpdate 0..maxFixedSteps times per frame at fixedStep (physics joins here in M6)
//   Late    LateUpdate, then [Edit] field values the scripts changed go back to the components (4-3)
//
// Play mode owns one runner: Begin on Play, Frame every game frame (time scale and pause applied by the caller),
// End on Stop (OnDisable + OnDestroy for everything, before the edited world comes back).
#include "Core/Command.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {

class BehaviourRunner {
public:
  double fixedStep = 1.0 / 60.0;
  int maxFixedSteps = 4; // a slow frame runs at most this many fixed steps (the rest of the time is dropped)
  std::function<void(double step)> beforeFixedStep; // physics (6-2): runs in each fixed step, before FixedUpdate

  void Begin(Scene& scene, CommandBus& bus);
  void Frame(Scene& scene, CommandBus& bus, double dt);
  void End();
  bool Active() const { return active_; }
  size_t Instances() const { return insts_.size(); }
  int FixedStepsLastFrame() const { return lastFixed_; }
  // Calls `method` (OnCollisionEnter, OnTriggerEnter, ...) on the enabled behaviours of `object` (6-2).
  void SendEvent(const std::string& object, const char* method, const std::string& json);

private:
  struct Inst {
    std::string object, type;
    int handle = 0;
    bool enabled = true;
    bool seen = false;
    std::string sent; // the component's fields as last given to / taken from C# (4-3)
  };
  void Sync(Scene& scene);
  void Pull(Scene& scene, CommandBus& bus); // C# field values -> components (shown live in the Inspector)
  std::vector<Inst> insts_;
  bool active_ = false;
  double acc_ = 0, time_ = 0;
  int frame_ = 0, lastFixed_ = 0;
};

} // namespace bdg
