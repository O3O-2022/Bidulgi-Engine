#pragma once
#include "RHI/RHI.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bdg {
// Design tokens read from design-system/project/tokens.json (the single source of truth).
// Colors ("#rrggbb", "rgba(r,g,b,a)"), px numbers (spacing, radius, size) and type style font sizes.
// Reload() is cheap; ReloadIfChanged() polls the file time so token edits show up live.
//
// Project theme (queue 2-5c): a project can override color tokens in ProjectSettings/Theme.json
//   {"theme":"Obsidian Dark","colors":{"accent":"#3b82f6","selection":"#1d3a5f"}}
// Overrides sit on top of tokens.json (C() returns the override when there is one), survive a tokens reload and
// are watched like tokens.json, so editing either file by hand shows up live too.
class Theme {
public:
  bool Load(const std::string& path);
  bool ReloadIfChanged(); // tokens.json or the overrides file changed on disk
  bool DumpJSON(const std::string& path) const; // every value as the engine sees it (design parity test)

  Color C(std::string_view name) const;          // missing token -> magenta (easy to spot)
  float N(std::string_view name, float fallback) const;
  float FontSize(std::string_view style, float fallback) const;
  struct Shadow { float x = 0, y = 0, blur = 0; Color color{0, 0, 0, 0}; };
  Shadow S(std::string_view name) const; // CSS box-shadow token "x y blur color"; missing -> no shadow
  size_t TokenCount() const { return colors_.size() + numbers_.size() + fontSizes_.size() + shadows_.size(); }
  const std::string& Path() const { return path_; }

  // Color tokens in tokens.json order, with their usage notes and tokens.json values.
  const std::vector<std::string>& ColorNames() const { return colorOrder_; }
  std::string Usage(std::string_view name) const;
  Color Base(std::string_view name) const;
  const std::string& ThemeName() const { return themeName_; }

  // Project overrides. SetOverride rejects unknown names; setting the base value removes the override.
  bool SetOverride(std::string_view name, Color c);
  void ClearOverride(std::string_view name);
  void ClearOverrides() { overrides_.clear(); }
  bool Overridden(std::string_view name) const { return overrides_.count(std::string(name)) != 0; }
  size_t OverrideCount() const { return overrides_.size(); }
  // path: the overrides file (remembered, watched by ReloadIfChanged). A missing file = no overrides.
  bool LoadOverrides(const std::string& path, std::string* err = nullptr);
  bool SaveOverrides(const std::string& path); // no overrides -> the file is removed
  const std::string& OverridesPath() const { return overridesPath_; }

  static bool ParseColor(std::string_view s, Color& out); // "#rgb", "#rrggbb", "#rrggbbaa", "rgb(...)", "rgba(...)"
  static std::string FormatColor(Color c);                // "#rrggbb", or "#rrggbbaa" when not opaque
  // HSV with h in degrees [0,360), s and v in [0,1]
  static void ToHSV(Color c, float& h, float& s, float& v);
  static Color FromHSV(float h, float s, float v, float a = 1.f);

private:
  std::string path_, overridesPath_, themeName_;
  long long mtime_ = 0, overridesMtime_ = 0;
  std::vector<std::string> colorOrder_;
  std::unordered_map<std::string, std::string> usage_;
  std::unordered_map<std::string, Color> overrides_;
  std::unordered_map<std::string, Color> colors_;
  std::unordered_map<std::string, float> numbers_, fontSizes_;
  std::unordered_map<std::string, Shadow> shadows_;
};
}
