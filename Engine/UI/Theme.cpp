#include "UI/Theme.h"
#include "Core/Log.h"
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace bdg {
namespace {
bool ParseColor(const char* s, Color& out) {
  if (s[0] == '#') {
    unsigned v = (unsigned)std::strtoul(s + 1, nullptr, 16);
    size_t len = std::strlen(s + 1);
    if (len == 6) { out = {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, 1.f}; return true; }
    if (len == 8) { out = {((v >> 24) & 255) / 255.f, ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; return true; }
    return false;
  }
  float r, g, b, a = 1.f;
  if (std::sscanf(s, "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) >= 3 || std::sscanf(s, "rgb(%f,%f,%f)", &r, &g, &b) == 3) {
    out = {r / 255.f, g / 255.f, b / 255.f, a};
    return true;
  }
  return false;
}

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
    if (name && value) fn(name, value);
  }
}
}

bool Theme::Load(const std::string& path) {
  path_ = path;
  yyjson_read_err err;
  yyjson_doc* doc = yyjson_read_file(path.c_str(), 0, nullptr, &err);
  if (!doc) { LogError("tokens: cannot read {} ({} at {})", path, err.msg, err.pos); return false; }
  yyjson_val* root = yyjson_doc_get_root(doc);
  colors_.clear(); numbers_.clear(); fontSizes_.clear(); shadows_.clear();

  ForTokens(root, "color", [&](const char* name, const char* value) {
    Color c;
    if (ParseColor(value, c)) colors_[name] = c;
    else LogWarn("tokens: bad color {} = {}", name, value);
  });
  for (const char* group : {"spacing", "radius", "size"})
    ForTokens(root, group, [&](const char* name, const char* value) { numbers_[name] = std::strtof(value, nullptr); });
  ForTokens(root, "shadow", [&](const char* name, const char* value) { // "x y blur color" (CSS box-shadow)
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
  if (path_.empty()) return false;
  long long t = FileTime(path_);
  if (t == 0 || t == mtime_) return false;
  mtime_ = t;
  return Load(path_);
}

bool Theme::DumpJSON(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "w");
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
  auto it = colors_.find(std::string(name));
  return it != colors_.end() ? it->second : Color{1, 0, 1, 1};
}

float Theme::N(std::string_view name, float fallback) const {
  auto it = numbers_.find(std::string(name));
  return it != numbers_.end() ? it->second : fallback;
}

float Theme::FontSize(std::string_view style, float fallback) const {
  auto it = fontSizes_.find(std::string(style));
  return it != fontSizes_.end() ? it->second : fallback;
}
}
