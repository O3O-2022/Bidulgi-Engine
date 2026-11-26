#pragma once
// Input actions (queue 6-4, Docs/features/input.md; Unreal's Enhanced Input). Game code never reads keys: it reads
// actions ("Jump", "Move"), and text files map keys, mouse and gamepad to them, so rebinding, more devices and AI
// edits only touch the files.
//
//   Input/Actions.jsonl          {"action":"Move","type":"vec2"}   types: button, axis (float), vec2, vec3
//   Input/Contexts/<name>.jsonl  {"context":"OnFoot","priority":0,"enabled":true}
//                                {"action":"Move","key":"WASD"}
//                                {"action":"Move","key":"Gamepad.LeftStick","mods":[{"deadzone":0.15}]}
//                                {"action":"Jump","key":"Space"}  {"action":"Jump","key":"Gamepad.South"}
//                                {"action":"Aim","key":"Mouse.Right","hold":0.3}
//                                {"action":"Move","key":"D","axis":"x"}   (one key onto an axis: x, -x, y, -y, z, -z)
//
// Keys: the editor's key names (A..Z, 0..9, Space, Enter, Escape, Tab, Left, Right, Up, Down, F1..F12, ...), Shift,
// Ctrl, Alt; composites WASD, Arrows; Mouse.Left / Right / Middle, Mouse.Delta (vec2, UI units per frame, up = +y),
// Mouse.Wheel; Gamepad.South / East / West / North / Back / Start / LeftShoulder / RightShoulder / LeftStick /
// RightStick (buttons when pressed in) / DPadUp..DPadRight, Gamepad.LeftStick / RightStick (vec2 when bound to a
// vec2 action, its press otherwise), Gamepad.DPad (vec2), Gamepad.LeftTrigger / RightTrigger (0..1).
// Modifiers: deadzone (radial), invert, invertX, invertY, scale. Triggers: pressed (default), hold (seconds).
//
// Contexts stack: every enabled context counts, highest priority first, and a key a higher context binds is not seen
// by lower ones (a vehicle context can take Space from "Jump" for "Brake").
// No project files: built-in defaults (Move WASD / Arrows / left stick, Look mouse / right stick, Jump Space / South,
// Fire left mouse / right trigger, Interact E / West).
#include "Platform/Gamepad.h"
#include "Platform/Window.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
enum class ActionType { Button, Axis, Vec2, Vec3 };

struct InputBinding {
  std::string action, key;
  std::string axis;                 // single key onto one axis of a vector action ("x", "-y", ...)
  float deadzone = 0, scale = 1, hold = 0;
  bool invertX = false, invertY = false;
};
struct InputContext {
  std::string name, file;
  int priority = 0;
  bool enabled = true;
  std::vector<InputBinding> bindings;
};
struct InputActionDef {
  std::string name;
  ActionType type = ActionType::Button;
};
struct ActionState {
  glm::vec3 value{0};
  bool down = false, pressed = false, released = false; // pressed / released: this frame
  uint64_t presses = 0, releases = 0;                   // since the game started (scripts count fixed steps with them)
};

class InputSystem {
public:
  static InputSystem& Instance();

  static bool ParseActions(std::string_view text, std::vector<InputActionDef>& out, std::string* error = nullptr);
  static bool ParseContext(std::string_view text, InputContext& out, std::string* error = nullptr);
  // Input/Actions.jsonl and Input/Contexts/*.jsonl under projectRoot (contexts sorted by name for a stable order);
  // the built-in defaults when there is no Actions.jsonl. Problems are logged; true when something usable loaded.
  bool Load(const std::filesystem::path& projectRoot);
  void UseDefaults();
  void Set(std::vector<InputActionDef> actions, std::vector<InputContext> contexts); // tests, tools

  // Devices: events the game gets (keyboard, mouse) and the polled gamepad.
  void Feed(const InputEvent& ev);
  void ReleaseAll();                                          // focus lost: nothing stays held
  void Update(double dt, const GamepadState* pad = nullptr);  // the first step of a game frame: devices -> actions
  void Reset();                                               // play stopped: states and counters back to zero

  const ActionState* State(std::string_view action) const;   // nullptr: no such action
  std::string StateJson(std::string_view action) const;      // {"down":..,"pressed":..,"released":..,"value":[x,y,z],"presses":n,"releases":n}
  bool EnableContext(std::string_view name, bool on);        // false: no such context
  const std::vector<InputActionDef>& Actions() const { return actions_; }
  const std::vector<InputContext>& Contexts() const { return contexts_; }

  // Editor routing (play mode): the Game view has the keyboard and mouse while focused (clicked or just started).
  bool gameFocused = false;
  // The mouse as the game got it (game UI buttons, 7-3): position in the fed events' units, button 1 left 2 middle 3 right.
  glm::vec2 MousePosition() const { return lastMouse_; }
  bool MouseButton(int button) const { return button >= 0 && button < 4 && mouse_[button]; }
  // For the game UI's input fields and scroll views (7-3): what was typed, Backspaces, Enter, wheel notches since
  // the last call (then cleared).
  struct UIKeys { std::string text; int backspaces = 0; bool enter = false; float wheel = 0; };
  UIKeys TakeUIKeys();

private:
  float KeyValue(const std::string& key, const GamepadState& pad) const; // a button / axis source, 0..1 (sticks: magnitude)
  glm::vec2 VectorValue(const std::string& key, const GamepadState& pad, bool& isVector) const;
  std::vector<InputActionDef> actions_;
  std::vector<InputContext> contexts_;
  std::map<std::string, ActionState, std::less<>> states_;
  std::map<std::string, double> held_; // per binding (context|index): seconds its source has been down (hold trigger)
  std::set<Key> keys_;
  bool shift_ = false, ctrl_ = false, alt_ = false;
  bool mouse_[4] = {};
  glm::vec2 mouseDelta_{0}, lastMouse_{0};
  bool haveMouse_ = false;
  float wheel_ = 0;
  UIKeys uiKeys_;
};
}
