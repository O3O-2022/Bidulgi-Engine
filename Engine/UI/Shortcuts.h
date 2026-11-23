#pragma once
// Shortcut system (queue 1-5): named actions ("edit.undo") bound to key combos ("Ctrl+Z"). Presets give the
// bindings people already know: "unity" (default) and "unreal". The project file stores the preset and any
// changed bindings, one JSON line each:
//   {"preset":"unreal"}
//   {"action":"play.toggle","keys":"Alt+P"}
#include "Platform/Window.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {
struct KeyCombo {
  Key key = Key::None;
  bool ctrl = false, shift = false, alt = false;
  bool operator==(const KeyCombo&) const = default;
  std::string ToString() const;                       // "Ctrl+Shift+Z"
  static bool Parse(std::string_view text, KeyCombo& out);
  bool Matches(const InputEvent& e) const { return e.key == key && e.ctrl == ctrl && e.shift == shift && e.alt == alt; }
};

class Shortcuts {
public:
  struct Action {
    std::string name, label;           // "play.toggle", "Play / Stop"
    std::vector<KeyCombo> keys;
    std::function<void()> run;
  };
  struct Conflict { std::string combo, first, second; };

  void Register(std::string name, std::string label, std::function<void()> run);
  bool ApplyPreset(std::string_view preset);  // "unity" or "unreal": replaces every binding
  const std::string& Preset() const { return preset_; }
  bool Bind(std::string_view action, std::string_view keys, std::string* error = nullptr); // "Ctrl+D" (several: "Ctrl+Y, Ctrl+Shift+Z")
  std::string KeysOf(std::string_view action) const;  // for menus and the settings list
  const std::vector<Action>& Actions() const { return actions_; }
  std::vector<Conflict> Conflicts() const;    // one combo on two actions
  bool Handle(const InputEvent& e);           // runs the matching action; true = consumed

  bool Load(const std::string& path, std::string* error = nullptr); // false if missing or invalid
  bool Save(const std::string& path) const;
  static const char* const kPresets[];       // {"unity", "unreal", nullptr}

private:
  Action* Find(std::string_view name);
  std::vector<Action> actions_;
  std::vector<std::pair<std::string, std::string>> overrides_; // action -> keys changed by the user (saved)
  std::string preset_ = "unity";
};
}
