#pragma once
// Command bus (queue 1-3): every edit — from the UI, the terminal, scripts or an AI agent — is a command.
// Executing one applies it, records its inverse for Undo, and appends a JSON line to the log, so a session
// can be saved and replayed (--replay) with the same result. Network and co-editing reuse the same lines.
//
//   {"cmd":"set","target":"g7","args":{"path":"Transform.position","value":[1,2,3]},"source":"ui","t":1.25}
//   set     {path: "Component.field[.sub]", value}     rename  {name}
//   add     {name, parent?, index?, id?} | {snapshot}  del     {}
//   mv      {parent, index?}                            addcomp {type, value?, index?}   rmcomp {type}
//   undo / redo / begin {label} / end                   (log only)
#include "Scene/Scene.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {
struct Command {
  std::string name;          // set, rename, add, del, mv, addcomp, rmcomp
  std::string target;        // object id (add: optional, filled in when created)
  std::string args = "{}";   // JSON object
  std::string source = "ui"; // ui | terminal | script | ai | replay
  double time = -1;          // seconds; -1 = now (the log keeps it so replays merge the same way)
  std::string ToJson() const;
};

class CommandBus {
public:
  explicit CommandBus(Scene& scene) : scene_(scene) {}

  bool Execute(Command cmd, std::string* error = nullptr);
  bool Undo(std::string* error = nullptr);
  bool Redo(std::string* error = nullptr);
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  size_t UndoDepth() const { return undo_.size(); }
  void BeginGroup(std::string label, std::string source = "ai"); // until EndGroup: one undo step (one AI request)
  void EndGroup();
  // Ends the open group and takes back everything it did, leaving neither an undo nor a redo step and the
  // earlier redo history untouched (a gizmo drag cancelled with Esc). The log keeps the lines plus an "undo".
  bool CancelGroup(std::string* error = nullptr);

  // Continuous edits of the same field from the UI within this window merge into one undo step.
  double mergeWindow = 0.3;
  std::function<double()> clock;     // seconds; default: steady clock since the bus was created
  std::function<void(const Command&)> onChanged; // after every execute / undo / redo (UI refresh)

  const std::vector<std::string>& Log() const { return log_; }
  bool SaveLog(const std::string& path) const;
  bool ExecuteLine(std::string_view jsonLine, std::string* error = nullptr);
  bool Replay(const std::string& path, std::string* error = nullptr, size_t* count = nullptr);
  void Reset(); // forget history and log (a different scene was opened: old commands do not apply to it)

  static bool IsStructural(const std::string& name) { return name != "set"; } // changes the object tree

  // Undo history for the History panel: done steps (oldest first) then undone steps (future = true).
  struct HistoryEntry {
    std::string label, source; // source of the first command: ui, ai, script, terminal, replay
    size_t commands = 0;       // > 1 for groups
    bool future = false;
  };
  std::vector<HistoryEntry> History() const;
  bool JumpTo(size_t depth, std::string* error = nullptr); // undo / redo until UndoDepth() == depth

private:
  struct Entry {
    std::vector<Command> forward, inverse; // inverse in the order to apply (reverse of forward)
    std::string label, mergeKey, source;
    double time = 0;
  };
  bool Apply(Command& cmd, Command& inverse, std::string* error);
  std::string Label(const Command& cmd) const; // "Player: Transform.position = [3,4,5]"
  double Now() const;
  void Push(Entry e);

  Scene& scene_;
  std::vector<Entry> undo_, redo_;
  std::vector<std::string> log_;
  int groupDepth_ = 0;
  Entry group_;
  double start_ = -1;
};
}
