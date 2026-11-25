#include "UI/Shortcuts.h"
#include "Core/Utf8Path.h"
#include "Core/Log.h"
#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>

namespace bdg {
namespace {
// Built-in presets: action -> keys. Missing actions are unbound in that preset.
struct PresetRow { const char* action; const char* unity; const char* unreal; };
const PresetRow kTable[] = {
    {"edit.undo", "Ctrl+Z", "Ctrl+Z"},
    {"edit.redo", "Ctrl+Y, Ctrl+Shift+Z", "Ctrl+Y"},
    {"edit.duplicate", "Ctrl+D", "Ctrl+W"},
    {"edit.delete", "Delete", "Delete"},
    {"edit.rename", "F2", "F2"},
    {"file.save", "Ctrl+S", "Ctrl+S"},
    {"file.new", "Ctrl+N", "Ctrl+N"},
    {"file.quit", "Ctrl+Q", "Ctrl+Q"},
    {"file.saveAll", "Ctrl+Shift+S", "Ctrl+Shift+S"},
    {"view.zoomIn", "Ctrl+=", "Ctrl+="},
    {"view.zoomOut", "Ctrl+-", "Ctrl+-"},
    {"view.zoomReset", "Ctrl+0", "Ctrl+0"},
    {"play.toggle", "Ctrl+P", "Alt+P"},
    {"play.pause", "Ctrl+Shift+P", "Ctrl+Shift+P"},
    {"play.step", "Ctrl+Alt+P", "Ctrl+Alt+P"},
    {"tool.view", "Q", ""},
    {"tool.select", "", "Q"},
    {"tool.move", "W", "W"},
    {"tool.rotate", "E", "E"},
    {"tool.scale", "R", "R"},
    {"tool.measure", "M", "M"},
    {"tool.blockout", "B", "B"},
    {"tool.terrain", "T", "T"}, // round 6: sculpt / paint terrains
    {"view.focus", "F", "F"},
    {"view.gallery", "F1", "F1"},
    {"view.nextPanel", "Ctrl+F6", "Ctrl+F6"},
    {"help.bug", "Ctrl+Shift+B", "Ctrl+Shift+B"},
};

std::string Trim(std::string_view s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string_view::npos ? std::string() : std::string(s.substr(a, b - a + 1));
}

bool ParseList(std::string_view text, std::vector<KeyCombo>& out, std::string* error) {
  out.clear();
  size_t start = 0;
  while (start <= text.size()) {
    size_t comma = text.find(',', start);
    std::string part = Trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
    if (!part.empty()) {
      KeyCombo c;
      if (!KeyCombo::Parse(part, c)) {
        if (error) *error = std::format("cannot read key combo \"{}\" (examples: Ctrl+D, Alt+P, Shift+F5, Delete)", part);
        return false;
      }
      out.push_back(c);
    }
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return true;
}
}

const char* const Shortcuts::kPresets[] = {"unity", "unreal", nullptr};

std::string KeyCombo::ToString() const {
  std::string s;
  if (ctrl) s += "Ctrl+";
  if (shift) s += "Shift+";
  if (alt) s += "Alt+";
  return s + KeyName(key);
}

bool KeyCombo::Parse(std::string_view text, KeyCombo& out) {
  out = {};
  size_t start = 0;
  while (true) {
    size_t plus = text.find('+', start);
    std::string part = Trim(text.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start));
    if (plus == std::string_view::npos) {
      out.key = KeyFromName(part);
      return out.key != Key::None;
    }
    std::string lower = part;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower == "ctrl" || lower == "control" || lower == "cmd") out.ctrl = true;
    else if (lower == "shift") out.shift = true;
    else if (lower == "alt" || lower == "option") out.alt = true;
    else return false;
    start = plus + 1;
  }
}

Shortcuts::Action* Shortcuts::Find(std::string_view name) {
  for (auto& a : actions_) if (a.name == name) return &a;
  return nullptr;
}

void Shortcuts::Register(std::string name, std::string label, std::function<void()> run) {
  if (Action* a = Find(name)) { a->label = std::move(label); a->run = std::move(run); return; }
  actions_.push_back({std::move(name), std::move(label), {}, std::move(run)});
  ApplyPreset(preset_); // give it its preset keys
}

bool Shortcuts::ApplyPreset(std::string_view preset) {
  bool unreal = preset == "unreal";
  if (!unreal && preset != "unity") return false;
  preset_ = std::string(preset);
  for (auto& a : actions_) {
    a.keys.clear();
    for (const PresetRow& r : kTable)
      if (a.name == r.action) ParseList(unreal ? r.unreal : r.unity, a.keys, nullptr);
  }
  for (auto& [action, keys] : overrides_) // user changes stay on top of the preset
    if (Action* a = Find(action)) ParseList(keys, a->keys, nullptr);
  return true;
}

bool Shortcuts::Bind(std::string_view action, std::string_view keys, std::string* error) {
  Action* a = Find(action);
  if (!a) {
    if (error) *error = std::format("unknown action \"{}\"", action);
    return false;
  }
  std::vector<KeyCombo> list;
  if (!ParseList(keys, list, error)) return false;
  a->keys = list;
  auto it = std::find_if(overrides_.begin(), overrides_.end(), [&](auto& o) { return o.first == action; });
  if (it != overrides_.end()) it->second = std::string(keys);
  else overrides_.emplace_back(std::string(action), std::string(keys));
  return true;
}

std::string Shortcuts::KeysOf(std::string_view action) const {
  for (auto& a : actions_)
    if (a.name == action) {
      std::string s;
      for (auto& k : a.keys) s += (s.empty() ? "" : ", ") + k.ToString();
      return s;
    }
  return {};
}

std::vector<Shortcuts::Conflict> Shortcuts::Conflicts() const {
  std::vector<Conflict> out;
  for (size_t i = 0; i < actions_.size(); ++i)
    for (size_t j = i + 1; j < actions_.size(); ++j)
      for (auto& a : actions_[i].keys)
        for (auto& b : actions_[j].keys)
          if (a == b) out.push_back({a.ToString(), actions_[i].name, actions_[j].name});
  return out;
}

bool Shortcuts::Handle(const InputEvent& e) {
  if (e.type != InputEvent::Type::KeyDown || e.key == Key::None) return false;
  for (auto& a : actions_)
    for (auto& k : a.keys)
      if (k.Matches(e)) {
        if (a.run) a.run();
        return true;
      }
  return false;
}

bool Shortcuts::Load(const std::string& path, std::string* error) {
  std::FILE* f = OpenU8(path, "rb");
  if (!f) {
    if (error) *error = "no shortcut file";
    return false;
  }
  char line[1024];
  std::string preset = "unity";
  std::vector<std::pair<std::string, std::string>> bindings;
  bool ok = true;
  while (ok && std::fgets(line, sizeof line, f)) {
    if (line[0] == '\n' || line[0] == '\r' || !line[0]) continue;
    yyjson_doc* d = yyjson_read(line, std::strlen(line), 0);
    yyjson_val* o = d ? yyjson_doc_get_root(d) : nullptr;
    if (const char* p = yyjson_get_str(yyjson_obj_get(o, "preset"))) preset = p;
    else if (const char* a = yyjson_get_str(yyjson_obj_get(o, "action")); a && yyjson_get_str(yyjson_obj_get(o, "keys")))
      bindings.emplace_back(a, yyjson_get_str(yyjson_obj_get(o, "keys")));
    else {
      ok = false;
      if (error) *error = std::format("{}: cannot read line: {}", path, Trim(line));
    }
    yyjson_doc_free(d);
  }
  std::fclose(f);
  if (!ok) return false;
  overrides_.clear();
  if (!ApplyPreset(preset)) {
    if (error) *error = std::format("{}: unknown preset \"{}\" (use unity or unreal)", path, preset);
    ApplyPreset("unity");
    return false;
  }
  for (auto& [a, k] : bindings) {
    std::string err;
    if (!Bind(a, k, &err)) LogWarn("shortcuts: {}: {}", path, err); // keep the rest
  }
  return true;
}

bool Shortcuts::Save(const std::string& path) const {
  std::error_code ec;
  std::filesystem::create_directories(PathU8(path).parent_path(), ec);
  std::FILE* f = OpenU8(path, "w");
  if (!f) return false;
  std::fprintf(f, "{\"preset\":\"%s\"}\n", preset_.c_str());
  for (auto& [a, k] : overrides_) std::fprintf(f, "{\"action\":\"%s\",\"keys\":\"%s\"}\n", a.c_str(), k.c_str());
  std::fclose(f);
  return true;
}
}
