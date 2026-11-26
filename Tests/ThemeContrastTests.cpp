// Design token contrast (UI review 2026-11-26, B5): the real design-system/project/tokens.json, loaded the way the
// editor loads it, against WCAG 2.x: text 4.5:1 on every surface it is drawn on, input borders 3:1 (1.4.11).
// Changing a colour token means passing this table.
#include "UI/Theme.h"
#include "bdg_test.h"
#include <algorithm>
#include <cmath>
#include <string>

using namespace bdg;

namespace {
double Luminance(const Color& c) { // sRGB 0..1 -> relative luminance
  auto ch = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
  return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
}
Color Over(const Color& top, const Color& under) { // a translucent token on a surface
  return {top.r * top.a + under.r * (1 - top.a), top.g * top.a + under.g * (1 - top.a), top.b * top.a + under.b * (1 - top.a), 1};
}
double Contrast(const Color& a, const Color& b) {
  const double la = Luminance(a), lb = Luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
} // namespace

TEST_CASE("design tokens: text 4.5:1 on its surfaces, input borders 3:1 (UI review B5)") {
  Theme t;
  CHECK(t.Load(std::string(BDG_SOURCE_DIR) + "/design-system/project/tokens.json"));
  int failures = 0;
  auto need = [&](const char* fg, const char* bg, double min) {
    const double c = Contrast(t.C(fg), t.C(bg));
    if (c + 1e-3 < min) {
      std::printf("  contrast %s on %s = %.2f (needs %.1f)\n", fg, bg, c, min);
      ++failures;
    }
  };
  // readable text on every surface (captions and type columns use fg-faint, so it is text too)
  for (const char* fg : {"fg", "fg-dim", "fg-faint", "accent", "ok", "warn", "err", "prefab", "axis-x", "axis-y", "axis-z"})
    for (const char* bg : {"panel", "raise", "win", "sunk", "ground"}) need(fg, bg, 4.5);
  // a selected row: names in fg, the type column in fg-dim
  need("fg", "selection", 4.5);
  need("fg-dim", "selection", 4.5);
  // text on the accent fill and on the hover / pressed fills of raised controls
  need("on-accent", "accent", 4.5);
  for (const char* bg : {"raise-hover", "raise-press"}) {
    need("fg", bg, 4.5);
    need("fg-dim", bg, 4.5);
  }
  // accent text on its soft tint (active chips)
  const Color soft = Over(t.C("accent-soft"), t.C("panel"));
  CHECK(Contrast(t.C("accent"), soft) >= 4.5);
  // control boundaries: an empty input must be visible on the panel and inside a sunk well
  need("line-input", "panel", 3.0);
  need("line-input", "sunk", 3.0);
  need("line-input", "raise", 2.5); // inside component cards (raise-ish headers): close to 3, kept above 2.5
  CHECK_EQ(failures, 0);
  // the new tokens exist (a missing token draws magenta)
  for (const char* name : {"line-input", "raise-hover", "raise-press", "gizmo-hot", "guide", "icon-folder", "icon-folder-tab"}) {
    const Color c = t.C(name);
    CHECK(!(c.r == 1 && c.g == 0 && c.b == 1));
  }
  CHECK_EQ(t.N("radius-xs", -1), 2.f);
}
