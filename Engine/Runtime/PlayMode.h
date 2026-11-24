#pragma once
// Play mode (queue 3-6). Play runs the game on a copy of the edited world: Start() keeps the edited scene as a
// snapshot and opens a fresh command-history layer, so the open scene becomes the play world (the Scene view,
// Hierarchy, Inspector and gizmos all work on it, and Ctrl+Z only undoes edits made while playing). Stop()
// puts the edited world back exactly as it was, with its own undo history. Values the user changed while
// playing (set commands still done at Stop) are offered afterwards: KeepPending() applies them to the edited
// world through commands, as one undo step; DiscardPending() forgets them. What the game itself changes
// (behaviours such as Rotator) is never kept.
#include "Core/Command.h"
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
  double Time() const { return time_; }      // game seconds since Start
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
};
}
