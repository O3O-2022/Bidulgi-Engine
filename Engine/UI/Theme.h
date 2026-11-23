#pragma once
#include "RHI/RHI.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace bdg {
// Design tokens read from design-system/project/tokens.json (the single source of truth).
// Colors ("#rrggbb", "rgba(r,g,b,a)"), px numbers (spacing, radius, size) and type style font sizes.
// Reload() is cheap; ReloadIfChanged() polls the file time so token edits show up live.
class Theme {
public:
  bool Load(const std::string& path);
  bool ReloadIfChanged();
  bool DumpJSON(const std::string& path) const; // every value as the engine sees it (design parity test)

  Color C(std::string_view name) const;          // missing token -> magenta (easy to spot)
  float N(std::string_view name, float fallback) const;
  float FontSize(std::string_view style, float fallback) const;
  struct Shadow { float x = 0, y = 0, blur = 0; Color color{0, 0, 0, 0}; };
  Shadow S(std::string_view name) const; // CSS box-shadow token "x y blur color"; missing -> no shadow
  size_t TokenCount() const { return colors_.size() + numbers_.size() + fontSizes_.size() + shadows_.size(); }
  const std::string& Path() const { return path_; }

private:
  std::string path_;
  long long mtime_ = 0;
  std::unordered_map<std::string, Color> colors_;
  std::unordered_map<std::string, float> numbers_, fontSizes_;
  std::unordered_map<std::string, Shadow> shadows_;
};
}
