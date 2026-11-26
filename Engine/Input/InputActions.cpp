#include "Input/InputActions.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <yyjson.h>

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string Str(yyjson_val* o, const char* k) {
  const char* s = yyjson_get_str(yyjson_obj_get(o, k));
  return s ? s : "";
}
float Num(yyjson_val* o, const char* k, float fallback) {
  yyjson_val* v = yyjson_obj_get(o, k);
  return yyjson_is_num(v) ? (float)yyjson_get_num(v) : fallback;
}
bool Bool(yyjson_val* o, const char* k, bool fallback) {
  yyjson_val* v = yyjson_obj_get(o, k);
  return yyjson_is_bool(v) ? yyjson_get_bool(v) : fallback;
}
template <class F> bool EachLine(std::string_view text, F&& f, std::string* error) {
  int lineNo = 0;
  for (size_t a = 0; a < text.size();) {
    size_t b = text.find('\n', a);
    if (b == std::string_view::npos) b = text.size();
    std::string_view line = text.substr(a, b - a);
    a = b + 1;
    ++lineNo;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty() || line.starts_with("//")) continue;
    yyjson_doc* d = yyjson_read(line.data(), line.size(), 0);
    yyjson_val* root = d ? yyjson_doc_get_root(d) : nullptr;
    std::string why;
    const bool ok = yyjson_is_obj(root) && f(root, why);
    yyjson_doc_free(d);
    if (!ok) {
      if (error) *error = std::format("line {}: {}", lineNo, why.empty() ? "not a JSON object" : why);
      return false;
    }
  }
  return true;
}
std::string U8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}
std::string ReadText(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return in ? std::string((std::istreambuf_iterator<char>(in)), {}) : std::string();
}
}

InputSystem& InputSystem::Instance() {
  static InputSystem s;
  return s;
}

bool InputSystem::ParseActions(std::string_view text, std::vector<InputActionDef>& out, std::string* error) {
  out.clear();
  return EachLine(text, [&](yyjson_val* o, std::string& why) {
    InputActionDef a;
    a.name = Str(o, "action");
    const std::string t = Str(o, "type");
    if (a.name.empty()) { why = "\"action\" (the name) is missing"; return false; }
    if (t.empty() || t == "button" || t == "bool") a.type = ActionType::Button;
    else if (t == "axis" || t == "float" || t == "axis1d") a.type = ActionType::Axis;
    else if (t == "vec2" || t == "axis2d") a.type = ActionType::Vec2;
    else if (t == "vec3" || t == "axis3d") a.type = ActionType::Vec3;
    else { why = "unknown type \"" + t + "\" (button, axis, vec2, vec3)"; return false; }
    out.push_back(a);
    return true;
  }, error);
}

bool InputSystem::ParseContext(std::string_view text, InputContext& out, std::string* error) {
  out = {};
  bool header = false;
  return EachLine(text, [&](yyjson_val* o, std::string& why) {
    if (!header) {
      out.name = Str(o, "context");
      if (out.name.empty()) { why = "the first line names the context: {\"context\":\"OnFoot\",\"priority\":0}"; return false; }
      out.priority = (int)Num(o, "priority", 0);
      out.enabled = Bool(o, "enabled", true);
      header = true;
      return true;
    }
    InputBinding b;
    b.action = Str(o, "action");
    b.key = Str(o, "key");
    if (b.action.empty() || b.key.empty()) { why = "a binding needs \"action\" and \"key\""; return false; }
    b.axis = Str(o, "axis");
    b.hold = Num(o, "hold", 0);
    auto mod = [&](yyjson_val* m) {
      b.deadzone = Num(m, "deadzone", b.deadzone);
      b.scale = Num(m, "scale", b.scale);
      if (Bool(m, "invert", false)) b.invertX = b.invertY = true;
      b.invertX = Bool(m, "invertX", b.invertX);
      b.invertY = Bool(m, "invertY", b.invertY);
    };
    mod(o); // modifiers may also sit on the binding itself
    if (yyjson_val* mods = yyjson_obj_get(o, "mods"); yyjson_is_arr(mods)) {
      size_t i, n;
      yyjson_val* m;
      yyjson_arr_foreach(mods, i, n, m) mod(m);
    }
    out.bindings.push_back(b);
    return true;
  }, error);
}

void InputSystem::Set(std::vector<InputActionDef> actions, std::vector<InputContext> contexts) {
  actions_ = std::move(actions);
  contexts_ = std::move(contexts);
  states_.clear();
  held_.clear();
}

void InputSystem::UseDefaults() {
  std::vector<InputActionDef> a = {{"Move", ActionType::Vec2}, {"Look", ActionType::Vec2}, {"Jump", ActionType::Button},
                                   {"Fire", ActionType::Button}, {"Interact", ActionType::Button}};
  InputContext c;
  c.name = "Default";
  auto bind = [&](const char* action, const char* key, float deadzone = 0, float scale = 1) {
    InputBinding b;
    b.action = action;
    b.key = key;
    b.deadzone = deadzone;
    b.scale = scale;
    c.bindings.push_back(b);
  };
  bind("Move", "WASD");
  bind("Move", "Arrows");
  bind("Move", "Gamepad.LeftStick", 0.15f);
  bind("Look", "Mouse.Delta", 0, 0.1f);
  bind("Look", "Gamepad.RightStick", 0.15f);
  bind("Jump", "Space");
  bind("Jump", "Gamepad.South");
  bind("Fire", "Mouse.Left");
  bind("Fire", "Gamepad.RightTrigger", 0.2f);
  bind("Interact", "E");
  bind("Interact", "Gamepad.West");
  Set(std::move(a), {c});
}

bool InputSystem::Load(const fs::path& root) {
  const fs::path actionsFile = root / "Input" / "Actions.jsonl";
  std::error_code ec;
  if (!fs::exists(actionsFile, ec)) {
    UseDefaults();
    LogInfo("input: no Input/Actions.jsonl in {}; built-in actions (Move, Look, Jump, Fire, Interact)", U8(root));
    return true;
  }
  std::vector<InputActionDef> actions;
  std::string err;
  if (!ParseActions(ReadText(actionsFile), actions, &err)) {
    LogWarn("input: Input/Actions.jsonl: {}; built-in actions instead", err);
    UseDefaults();
    return false;
  }
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(root / "Input" / "Contexts", ec))
    if (entry.path().extension() == ".jsonl") files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  std::vector<InputContext> contexts;
  for (const fs::path& f : files) {
    InputContext c;
    if (!ParseContext(ReadText(f), c, &err)) {
      LogWarn("input: {}: {}", U8(f.filename()), err);
      continue;
    }
    c.file = U8(f.filename());
    for (const InputBinding& b : c.bindings)
      if (std::none_of(actions.begin(), actions.end(), [&](const InputActionDef& a) { return a.name == b.action; }))
        LogWarn("input: {}: \"{}\" is not in Actions.jsonl", c.file, b.action);
    contexts.push_back(std::move(c));
  }
  LogInfo("input: {} actions, {} contexts from {}", actions.size(), contexts.size(), U8(root / "Input"));
  Set(std::move(actions), std::move(contexts));
  return true;
}

void InputSystem::Feed(const InputEvent& ev) {
  using T = InputEvent::Type;
  shift_ = ev.shift;
  ctrl_ = ev.ctrl;
  alt_ = ev.alt;
  switch (ev.type) {
    case T::KeyDown:
      if (ev.key != Key::None) keys_.insert(ev.key);
      if (ev.key == Key::Backspace) ++uiKeys_.backspaces;
      if (ev.key == Key::Enter) uiKeys_.enter = true;
      break;
    case T::Text: uiKeys_.text += ev.text; break;
    case T::KeyUp: keys_.erase(ev.key); break;
    case T::MouseDown: if (ev.button >= 1 && ev.button <= 3) mouse_[ev.button] = true; break;
    case T::MouseUp: if (ev.button >= 1 && ev.button <= 3) mouse_[ev.button] = false; break;
    case T::MouseMove:
      if (haveMouse_) mouseDelta_ += glm::vec2(ev.x, ev.y) - lastMouse_;
      lastMouse_ = {ev.x, ev.y};
      haveMouse_ = true;
      break;
    case T::Wheel:
      wheel_ += ev.wheel;
      uiKeys_.wheel += ev.wheel;
      break;
    default: break;
  }
}

InputSystem::UIKeys InputSystem::TakeUIKeys() {
  UIKeys k = std::move(uiKeys_);
  uiKeys_ = {};
  return k;
}

void InputSystem::ReleaseAll() {
  keys_.clear();
  for (bool& b : mouse_) b = false;
  shift_ = ctrl_ = alt_ = false;
  haveMouse_ = false;
  mouseDelta_ = {};
  wheel_ = 0;
}

void InputSystem::Reset() {
  ReleaseAll();
  states_.clear();
  held_.clear();
  gameFocused = false;
}

float InputSystem::KeyValue(const std::string& key, const GamepadState& pad) const {
  if (key == "Shift") return shift_ ? 1.f : 0.f;
  if (key == "Ctrl") return ctrl_ ? 1.f : 0.f;
  if (key == "Alt") return alt_ ? 1.f : 0.f;
  if (key == "Mouse.Left") return mouse_[1] ? 1.f : 0.f;
  if (key == "Mouse.Right") return mouse_[3] ? 1.f : 0.f;
  if (key == "Mouse.Middle") return mouse_[2] ? 1.f : 0.f;
  if (key == "Mouse.Wheel") return wheel_;
  if (key.starts_with("Gamepad.")) {
    const std::string_view k = std::string_view(key).substr(8);
    if (k == "LeftTrigger") return pad.leftTrigger;
    if (k == "RightTrigger") return pad.rightTrigger;
    for (uint32_t i = 0; i < (uint32_t)PadButton::Count; ++i)
      if (k == PadButtonName((PadButton)i)) return pad.Down((PadButton)i) ? 1.f : 0.f;
    bool vec = false;
    return glm::length(VectorValue(key, pad, vec));
  }
  bool vec = false;
  const glm::vec2 v = VectorValue(key, pad, vec);
  if (vec) return glm::length(v);
  const Key k = KeyFromName(key);
  return k != Key::None && keys_.count(k) ? 1.f : 0.f;
}

glm::vec2 InputSystem::VectorValue(const std::string& key, const GamepadState& pad, bool& isVector) const {
  isVector = true;
  auto k = [&](Key x) { return keys_.count(x) ? 1.f : 0.f; };
  auto unit = [](glm::vec2 v) { const float l = glm::length(v); return l > 1.f ? v / l : v; };
  if (key == "WASD") return unit({k(Key::D) - k(Key::A), k(Key::W) - k(Key::S)});
  if (key == "Arrows") return unit({k(Key::Right) - k(Key::Left), k(Key::Up) - k(Key::Down)});
  if (key == "Mouse.Delta") return {mouseDelta_.x, -mouseDelta_.y}; // screen down -> up is +y
  if (key == "Gamepad.LeftStick") return {pad.leftX, pad.leftY};
  if (key == "Gamepad.RightStick") return {pad.rightX, pad.rightY};
  if (key == "Gamepad.DPad")
    return unit({(pad.Down(PadButton::DPadRight) ? 1.f : 0.f) - (pad.Down(PadButton::DPadLeft) ? 1.f : 0.f),
                 (pad.Down(PadButton::DPadUp) ? 1.f : 0.f) - (pad.Down(PadButton::DPadDown) ? 1.f : 0.f)});
  isVector = false;
  return {};
}

void InputSystem::Update(double dt, const GamepadState* padIn) {
  const GamepadState none;
  const GamepadState& pad = padIn ? *padIn : none;
  std::vector<const InputContext*> order;
  for (const InputContext& c : contexts_)
    if (c.enabled) order.push_back(&c);
  std::stable_sort(order.begin(), order.end(), [](const InputContext* a, const InputContext* b) { return a->priority > b->priority; });
  std::map<std::string, glm::vec3, std::less<>> sum;
  std::set<std::string> consumed; // keys a higher-priority context binds
  for (const InputContext* c : order) {
    std::set<std::string> mine;
    for (size_t i = 0; i < c->bindings.size(); ++i) {
      const InputBinding& b = c->bindings[i];
      if (consumed.count(b.key)) continue;
      mine.insert(b.key);
      auto def = std::find_if(actions_.begin(), actions_.end(), [&](const InputActionDef& a) { return a.name == b.action; });
      if (def == actions_.end()) continue;
      const bool vectorAction = def->type == ActionType::Vec2 || def->type == ActionType::Vec3;
      glm::vec3 v(0);
      bool isVector = false;
      const glm::vec2 vv = vectorAction && b.axis.empty() ? VectorValue(b.key, pad, isVector) : glm::vec2(0);
      if (isVector) {
        glm::vec2 x = vv;
        const float len = glm::length(x);
        if (b.deadzone > 0 && b.key != "Mouse.Delta") // radial deadzone, rescaled so the edge of it is 0
          x = len <= b.deadzone ? glm::vec2(0) : x / len * std::min((len - b.deadzone) / (1 - std::min(b.deadzone, 0.99f)), 1.f);
        v = {x.x, x.y, 0};
      } else {
        float s = KeyValue(b.key, pad);
        if (s < b.deadzone) s = 0;
        if (!b.axis.empty()) {
          const float sign = b.axis[0] == '-' ? -1.f : 1.f;
          const char ax = b.axis.back();
          v[ax == 'y' ? 1 : ax == 'z' ? 2 : 0] = sign * s;
        } else {
          v.x = s;
        }
      }
      if (b.invertX) v.x = -v.x;
      if (b.invertY) v.y = -v.y;
      v *= b.scale;
      if (b.hold > 0) { // the source must stay down this long before it counts
        double& h = held_[c->name + "|" + std::to_string(i)];
        h = glm::length(v) > 1e-4f ? h + dt : 0;
        if (h < b.hold) v = glm::vec3(0);
      }
      sum[b.action] += v;
    }
    consumed.insert(mine.begin(), mine.end());
  }
  for (const InputActionDef& a : actions_) {
    ActionState& s = states_[a.name];
    const bool was = s.down;
    glm::vec3 v = sum.count(a.name) ? sum[a.name] : glm::vec3(0);
    if (a.type == ActionType::Button) v = {std::min(std::abs(v.x), 1.f), 0, 0};
    else if (a.type == ActionType::Axis) v = {v.x, 0, 0};
    else if (a.type == ActionType::Vec2) v.z = 0;
    s.value = v;
    s.down = a.type == ActionType::Button ? v.x >= 0.5f : glm::length(v) > 1e-4f;
    s.pressed = s.down && !was;
    s.released = !s.down && was;
    if (s.pressed) ++s.presses;
    if (s.released) ++s.releases;
  }
  mouseDelta_ = {};
  wheel_ = 0;
}

const ActionState* InputSystem::State(std::string_view action) const {
  auto it = states_.find(action);
  return it == states_.end() ? nullptr : &it->second;
}

std::string InputSystem::StateJson(std::string_view action) const {
  const ActionState* s = State(action);
  if (!s) return "null";
  return std::format(R"({{"down":{},"pressed":{},"released":{},"value":[{},{},{}],"presses":{},"releases":{}}})", s->down, s->pressed, s->released,
                     s->value.x, s->value.y, s->value.z, s->presses, s->releases);
}

bool InputSystem::EnableContext(std::string_view name, bool on) {
  for (InputContext& c : contexts_)
    if (c.name == name) {
      if (c.enabled != on) LogInfo("input: context {} {}", c.name, on ? "on" : "off");
      c.enabled = on;
      return true;
    }
  return false;
}
}
