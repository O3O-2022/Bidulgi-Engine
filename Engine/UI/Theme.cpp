#include "UI/Theme.h"
#include "Core/Utf8Path.h"
#include "Core/Log.h"
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>

namespace bdg {
namespace {
long long FileTime(const std::string& p) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(p, ec);
  return ec ? 0 : (long long)t.time_since_epoch().count();
}

// Reads every {"name","value"} entry of group[key].tokens.
template <class F> void ForTokens(yyjson_val* root, const char* group, F&& fn) {
  yyjson_val* tokens = yyjson_obj_get(yyjson_obj_get(root, group), "tokens");
  size_t i, n;
  yyjson_val* t;
  yyjson_arr_foreach(tokens, i, n, t) {
    const char* name = yyjson_get_str(yyjson_obj_get(t, "name"));
    const char* value = yyjson_get_str(yyjson_obj_get(t, "value"));
    if (name && value) fn(name, value, yyjson_get_str(yyjson_obj_get(t, "usage")));
  }
}
int Q8(float x) { return (int)std::lround(std::clamp(x, 0.f, 1.f) * 255.f); }
bool Same(const Color& a, const Color& b) { return Q8(a.r) == Q8(b.r) && Q8(a.g) == Q8(b.g) && Q8(a.b) == Q8(b.b) && Q8(a.a) == Q8(b.a); }
}

bool Theme::ParseColor(std::string_view sv, Color& out) {
  std::string s(sv);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  if (s.empty()) return false;
  if (s[0] == '#') {
    std::string hex = s.substr(1);
    if (hex.empty() || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
    if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    unsigned v = (unsigned)std::strtoul(hex.c_str(), nullptr, 16);
    if (hex.size() == 6) { out = {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, 1.f}; return true; }
    if (hex.size() == 8) { out = {((v >> 24) & 255) / 255.f, ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; return true; }
    return false;
  }
  float r, g, b, a = 1.f;
  if (std::sscanf(s.c_str(), "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) >= 3 || std::sscanf(s.c_str(), "rgb(%f,%f,%f)", &r, &g, &b) == 3) {
    out = {r / 255.f, g / 255.f, b / 255.f, a};
    return true;
  }
  return false;
}

std::string Theme::FormatColor(Color c) {
  if (Q8(c.a) == 255) return std::format("#{:02x}{:02x}{:02x}", Q8(c.r), Q8(c.g), Q8(c.b));
  return std::format("#{:02x}{:02x}{:02x}{:02x}", Q8(c.r), Q8(c.g), Q8(c.b), Q8(c.a));
}

void Theme::ToHSV(Color c, float& h, float& s, float& v) {
  const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
  v = mx;
  s = mx > 0 ? d / mx : 0;
  if (d <= 0) { h = 0; return; }
  if (mx == c.r) h = 60.f * std::fmod((c.g - c.b) / d, 6.f);
  else if (mx == c.g) h = 60.f * ((c.b - c.r) / d + 2.f);
  else h = 60.f * ((c.r - c.g) / d + 4.f);
  if (h < 0) h += 360.f;
}

Color Theme::FromHSV(float h, float s, float v, float a) {
  h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f);
  s = std::clamp(s, 0.f, 1.f);
  v = std::clamp(v, 0.f, 1.f);
  const float c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.f, 2.f) - 1)), m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  return {r + m, g + m, b + m, std::clamp(a, 0.f, 1.f)};
}

bool Theme::Load(const std::string& path) {
  missing_.clear(); // asked-for names are checked against this load
  path_ = path;
  yyjson_read_err err{};
  std::string fileText;
  yyjson_doc* doc = ReadTextU8(path, fileText) ? yyjson_read_opts(fileText.data(), fileText.size(), 0, nullptr, &err) : nullptr;
  if (!doc && fileText.empty()) err.msg = "cannot open", err.pos = 0;
  if (!doc) { LogError("tokens: cannot read {} ({} at {})", path, err.msg, err.pos); return false; }
  yyjson_val* root = yyjson_doc_get_root(doc);
  colors_.clear(); numbers_.clear(); fontSizes_.clear(); shadows_.clear();
  colorOrder_.clear(); usage_.clear();
  themeName_.clear();
  yyjson_val* themes = yyjson_obj_get(yyjson_obj_get(root, "color"), "themes");
  if (const char* tn = yyjson_get_str(yyjson_obj_get(yyjson_arr_get_first(themes), "name"))) themeName_ = tn;

  ForTokens(root, "color", [&](const char* name, const char* value, const char* usage) {
    Color c;
    if (ParseColor(value, c)) {
      if (!colors_.count(name)) colorOrder_.push_back(name);
      colors_[name] = c;
      if (usage) usage_[name] = usage;
    } else LogWarn("tokens: bad color {} = {}", name, value);
  });
  for (const char* group : {"spacing", "radius", "size"})
    ForTokens(root, group, [&](const char* name, const char* value, const char*) { numbers_[name] = std::strtof(value, nullptr); });
  ForTokens(root, "shadow", [&](const char* name, const char* value, const char*) { // "x y blur color" (CSS box-shadow)
    Shadow sh;
    char* end = nullptr;
    sh.x = std::strtof(value, &end);
    if (end && std::strncmp(end, "px", 2) == 0) end += 2;
    sh.y = std::strtof(end, &end);
    if (end && std::strncmp(end, "px", 2) == 0) end += 2;
    sh.blur = std::strtof(end, &end);
    if (end && std::strncmp(end, "px", 2) == 0) end += 2;
    while (end && *end == ' ') ++end;
    if (end && ParseColor(end, sh.color)) shadows_[name] = sh;
    else LogWarn("tokens: bad shadow {} = {}", name, value);
  });

  size_t gi, gn, si, sn;
  yyjson_val *g, *s;
  yyjson_arr_foreach(yyjson_obj_get(yyjson_obj_get(root, "type"), "groups"), gi, gn, g) {
    yyjson_arr_foreach(yyjson_obj_get(g, "styles"), si, sn, s) {
      const char* name = yyjson_get_str(yyjson_obj_get(s, "name"));
      const char* size = yyjson_get_str(yyjson_obj_get(s, "fontSize"));
      if (name && size) fontSizes_[name] = std::strtof(size, nullptr);
    }
  }
  yyjson_doc_free(doc);
  mtime_ = FileTime(path);
  LogInfo("tokens: {} colors, {} sizes, {} type styles, {} shadows from {}", colors_.size(), numbers_.size(), fontSizes_.size(), shadows_.size(), path);
  return true;
}

bool Theme::ReloadIfChanged() {
  bool changed = false;
  if (!path_.empty()) {
    long long t = FileTime(path_);
    if (t != 0 && t != mtime_) {
      mtime_ = t;
      changed = Load(path_);
    }
  }
  if (!overridesPath_.empty() && FileTime(overridesPath_) != overridesMtime_) {
    std::string err;
    if (!LoadOverrides(overridesPath_, &err)) LogWarn("theme: {}", err);
    changed = true;
  }
  return changed;
}

std::string Theme::Usage(std::string_view name) const {
  auto it = usage_.find(std::string(name));
  return it != usage_.end() ? it->second : std::string();
}

Color Theme::Base(std::string_view name) const {
  auto it = colors_.find(std::string(name));
  return it != colors_.end() ? it->second : Color{1, 0, 1, 1};
}

bool Theme::SetOverride(std::string_view name, Color c) {
  auto it = colors_.find(std::string(name));
  if (it == colors_.end()) return false;
  if (Same(it->second, c)) overrides_.erase(std::string(name));
  else overrides_[std::string(name)] = c;
  return true;
}

void Theme::ClearOverride(std::string_view name) { overrides_.erase(std::string(name)); }

bool Theme::LoadOverrides(const std::string& path, std::string* err) {
  overridesPath_ = path;
  overridesMtime_ = FileTime(path);
  overrides_.clear();
  if (overridesMtime_ == 0) return true; // no file: no overrides
  yyjson_read_err rerr{};
  std::string fileText;
  const bool opened = ReadTextU8(path, fileText);
  yyjson_doc* doc = opened ? yyjson_read_opts(fileText.data(), fileText.size(), 0, nullptr, &rerr) : nullptr;
  if (!opened) { rerr.code = YYJSON_READ_ERROR_FILE_OPEN; rerr.msg = "cannot open"; rerr.pos = 0; }
  if (!doc) {
    if (rerr.code == YYJSON_READ_ERROR_FILE_OPEN) overridesMtime_ = 0; // held by another program: read it on the next poll
    if (err) *err = std::format("{}: {} at {}", path, rerr.msg, rerr.pos);
    return false;
  }
  yyjson_val* colors = yyjson_obj_get(yyjson_doc_get_root(doc), "colors");
  size_t i, n;
  yyjson_val *k, *v;
  std::string bad;
  yyjson_obj_foreach(colors, i, n, k, v) {
    const char* name = yyjson_get_str(k);
    const char* value = yyjson_get_str(v);
    Color c;
    if (!value || !ParseColor(value, c)) bad += std::format(" {} (bad color)", name);
    else if (!SetOverride(name, c)) bad += std::format(" {} (no such token)", name);
  }
  yyjson_doc_free(doc);
  if (!bad.empty() && err) *err = path + ": skipped" + bad;
  LogInfo("theme: {} override(s) from {}", overrides_.size(), path);
  return bad.empty();
}

bool Theme::SaveOverrides(const std::string& path) {
  overridesPath_ = path;
  std::error_code ec;
  if (overrides_.empty()) {
    std::filesystem::remove(path, ec);
    overridesMtime_ = 0;
    return !ec;
  }
  std::string text = "{\"theme\":\"" + themeName_ + "\",\"colors\":{";
  bool first = true;
  for (const std::string& name : colorOrder_) { // tokens.json order: diffs stay small and readable
    auto it = overrides_.find(name);
    if (it == overrides_.end()) continue;
    text += std::format("{}\n  \"{}\":\"{}\"", first ? "" : ",", name, FormatColor(it->second));
    first = false;
  }
  text += "\n}}\n";
  if (PathU8(path).has_parent_path()) std::filesystem::create_directories(PathU8(path).parent_path(), ec);
  {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << text;
  }
  overridesMtime_ = FileTime(path);
  return true;
}

bool Theme::DumpJSON(const std::string& path) const {
  std::FILE* f = OpenU8(path, "w");
  if (!f) return false;
  auto color = [&](const Color& c) { std::fprintf(f, "[%.6f,%.6f,%.6f,%.6f]", c.r, c.g, c.b, c.a); };
  std::fprintf(f, "{\n \"colors\": {");
  const char* sep = "";
  for (auto& [k, v] : colors_) { std::fprintf(f, "%s\n  \"%s\": ", sep, k.c_str()); color(v); sep = ","; }
  std::fprintf(f, "\n },\n \"numbers\": {");
  sep = "";
  for (auto& [k, v] : numbers_) { std::fprintf(f, "%s\n  \"%s\": %g", sep, k.c_str(), v); sep = ","; }
  std::fprintf(f, "\n },\n \"fontSizes\": {");
  sep = "";
  for (auto& [k, v] : fontSizes_) { std::fprintf(f, "%s\n  \"%s\": %g", sep, k.c_str(), v); sep = ","; }
  std::fprintf(f, "\n },\n \"shadows\": {");
  sep = "";
  for (auto& [k, v] : shadows_) {
    std::fprintf(f, "%s\n  \"%s\": {\"x\": %g, \"y\": %g, \"blur\": %g, \"color\": ", sep, k.c_str(), v.x, v.y, v.blur);
    color(v.color);
    std::fprintf(f, "}");
    sep = ",";
  }
  std::fprintf(f, "\n }\n}\n");
  std::fclose(f);
  return true;
}

Theme::Shadow Theme::S(std::string_view name) const {
  auto it = shadows_.find(std::string(name));
  return it != shadows_.end() ? it->second : Shadow{};
}

Color Theme::C(std::string_view name) const {
  if (!overrides_.empty())
    if (auto o = overrides_.find(std::string(name)); o != overrides_.end()) return o->second;
  auto it = colors_.find(std::string(name));
  if (it != colors_.end()) return it->second;
  if (!colors_.empty()) missing_.insert("color:" + std::string(name));
  return Color{1, 0, 1, 1};
}

float Theme::N(std::string_view name, float fallback) const {
  auto it = numbers_.find(std::string(name));
  if (it != numbers_.end()) return it->second;
  if (!numbers_.empty()) missing_.insert("number:" + std::string(name));
  return fallback;
}

float Theme::FontSize(std::string_view style, float fallback) const {
  auto it = fontSizes_.find(std::string(style));
  if (it != fontSizes_.end()) return it->second;
  if (!fontSizes_.empty()) missing_.insert("font:" + std::string(style));
  return fallback;
}
}
