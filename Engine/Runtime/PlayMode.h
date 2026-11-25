#pragma once
// Play mode (queue 3-6). Play runs the game on a copy of the edited world: Start() keeps the edited scene as a
// snapshot and opens a fresh command-history layer, so the open scene becomes the play world (the Scene view,
// Hierarchy, Inspector and gizmos all work on it, and Ctrl+Z only undoes edits made while playing). Stop()
// puts the edited world back exactly as it was, with its own undo history. Values the user changed while
// playing (set commands still done at Stop) are offered afterwards: KeepPending() applies them to the edited
// world through commands, as one undo step; DiscardPending() forgets them. What the game itself changes
// (behaviours such as Rotator) is never kept.
#include "Core/Command.h"
#include "Scene/CameraShake.h"
#include "Scripting/BehaviourRunner.h"
#include <map>
#include <string>
#include <vector>

namespace bdg {
class EditorModel;

class PlayMode {
public:
  bool Start(EditorModel& model);
  bool Stop(EditorModel& model);
  // Runs the built-in behaviours for dt seconds of game time (Rotator for now; scripts come with M4).
  void Tick(EditorModel& model, double dt);
  bool Playing() const { return playing_; }
  // Play mode tools (queue 3-6b): pause (also before Play: the game starts paused), one frame at a time, and a
  // time scale for game time (0.1x .. 4x).
  void SetPaused(bool p) { paused_ = p; if (!p) step_ = false; }
  bool Paused() const { return paused_; }
  void Step() { if (playing_) { paused_ = true; step_ = true; } } // advance exactly one frame (1/60 s), then hold
  void SetTimeScale(float s);
  float TimeScale() const { return timeScale_; }
  static constexpr double kStepSeconds = 1.0 / 60.0;
  double Time() const { return time_; }      // game seconds since Start
  // Camera shake on the game camera (E-9): CameraShakeSource components (play on start, Play button) and
  // Camera.Shake calls, attenuated by distance; the Game view adds this offset to its camera.
  ShakeOffset Shake() const { return playing_ ? shaker_.Current() : ShakeOffset{}; }
  size_t ShakesPlaying() const { return shaker_.Active(); }
  const BehaviourRunner& Scripts() const { return scripts_; }
  BehaviourRunner& Scripts() { return scripts_; } // fixed step from the project settings
  uint64_t Frames() const { return frames_; } // ticks since Start

  struct Change { Command cmd; std::string label; }; // "Crystal: Transform.position"
  const std::vector<Change>& Pending() const { return pending_; }
  int KeepPending(EditorModel& model); // returns how many values were applied
  void DiscardPending() { pending_.clear(); }

private:
  bool playing_ = false;
  std::string snapshot_;          // the edited world (scene text)
  std::vector<std::string> selection_;
  std::vector<Change> pending_;
  double time_ = 0;
  uint64_t frames_ = 0;
  bool paused_ = false, step_ = false;
  float timeScale_ = 1.f;
  CameraShaker shaker_;
  BehaviourRunner scripts_; // C# Behaviours (4-2)
  bool shakesStarted_ = false;
  std::map<std::string, CameraShakeDef> shakeCache_; // asset name -> definition (read once per play)
  bool FindShake(EditorModel& m, const std::string& name, CameraShakeDef& out);
  void TickShakes(EditorModel& m, float dt);
};
}
