// Theme editor tests (queue 2-5c): project overrides on top of tokens.json, saving, loading, colors and HSV.
#include "UI/Theme.h"
#include "bdg_test.h"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
std::string ReadAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
fs::path WriteTokens() {
  const fs::path dir = fs::temp_directory_path() / "bdg_theme_test";
  fs::create_directories(dir);
  std::ofstream(dir / "tokens.json", std::ios::binary)
      << R"({"color":{"themes":[{"id":"dark","name":"Test Dark"}],"tokens":[)"
      << R"({"name":"panel","value":"#1c222b","usage":"Panel bodies."},)"
      << R"({"name":"accent","value":"#f59e32","usage":"The one highlight."},)"
      << R"x({"name":"accent-soft","value":"rgba(245,158,50,.14)"}]}})x";
  fs::remove(dir / "Theme.json");
  return dir;
}
}

TEST_CASE("theme: colors parse, print and go through HSV (queue 2-5c)") {
  Color c;
  CHECK(Theme::ParseColor("#3b82f6", c) && Theme::FormatColor(c) == "#3b82f6");
  CHECK(Theme::ParseColor(" #ABC ", c) && Theme::FormatColor(c) == "#aabbcc");
  CHECK(Theme::ParseColor("rgba(245,158,50,.14)", c) && Theme::FormatColor(c) == "#f59e3224");
  CHECK(!Theme::ParseColor("#12345", c));
  CHECK(!Theme::ParseColor("#gggggg", c));
  CHECK(!Theme::ParseColor("blue", c));
  float h, s, v;
  Theme::ToHSV({1, 0, 0, 1}, h, s, v);
  CHECK(h == 0 && s == 1 && v == 1);
  Theme::ToHSV({0, 0.5f, 0, 1}, h, s, v);
  CHECK(std::abs(h - 120) < 0.01f && s == 1 && std::abs(v - 0.5f) < 1e-6f);
  for (const char* hex : {"#3b82f6", "#f59e32", "#1c222b", "#000000", "#ffffff", "#808080"}) {
    Theme::ParseColor(hex, c);
    Theme::ToHSV(c, h, s, v);
    CHECK_EQ(Theme::FormatColor(Theme::FromHSV(h, s, v)), std::string(hex));
  }
  CHECK_EQ(Theme::FormatColor(Theme::FromHSV(480, 1, 1)), std::string("#00ff00")); // hue wraps
}

TEST_CASE("theme: project overrides sit on tokens.json, save and load (queue 2-5c)") {
  const fs::path dir = WriteTokens();
  Theme t;
  CHECK(t.Load((dir / "tokens.json").string()));
  CHECK_EQ(t.ThemeName(), std::string("Test Dark"));
  CHECK(t.ColorNames().size() == 3 && t.ColorNames()[0] == "panel" && t.ColorNames()[1] == "accent");
  CHECK_EQ(t.Usage("accent"), std::string("The one highlight."));
  CHECK(t.LoadOverrides((dir / "Theme.json").string())); // no file: nothing to apply
  CHECK_EQ(t.OverrideCount(), (size_t)0);

  Color blue;
  Theme::ParseColor("#3b82f6", blue);
  CHECK(t.SetOverride("accent", blue));
  CHECK(!t.SetOverride("no-such-token", blue));
  CHECK(t.Overridden("accent") && Theme::FormatColor(t.C("accent")) == "#3b82f6");
  CHECK_EQ(Theme::FormatColor(t.Base("accent")), std::string("#f59e32"));
  CHECK(t.SetOverride("panel", t.Base("panel"))); // the default value is not an override
  CHECK(!t.Overridden("panel"));

  const fs::path file = dir / "Theme.json";
  CHECK(t.SaveOverrides(file.string()));
  CHECK_EQ(ReadAll(file), std::string("{\"theme\":\"Test Dark\",\"colors\":{\n  \"accent\":\"#3b82f6\"\n}}\n"));

  Theme u; // another session of the same project
  u.Load((dir / "tokens.json").string());
  CHECK(u.LoadOverrides(file.string()));
  CHECK(Theme::FormatColor(u.C("accent")) == "#3b82f6" && u.OverrideCount() == 1);
  CHECK(u.Load((dir / "tokens.json").string())); // a tokens reload keeps the project colors
  CHECK_EQ(Theme::FormatColor(u.C("accent")), std::string("#3b82f6"));

  std::ofstream(file, std::ios::binary) << R"({"colors":{"panel":"#203040","ghost":"#ffffff","accent":"nope"}})";
  std::string err;
  CHECK(!u.LoadOverrides(file.string(), &err)); // bad entries are skipped and reported, good ones apply
  CHECK(err.find("ghost") != std::string::npos && err.find("accent") != std::string::npos);
  CHECK(Theme::FormatColor(u.C("panel")) == "#203040" && !u.Overridden("accent"));

  u.ClearOverrides();
  CHECK(u.SaveOverrides(file.string()));
  CHECK(!fs::exists(file)); // no overrides: no file
  CHECK_EQ(Theme::FormatColor(u.C("panel")), std::string("#1c222b"));
  fs::remove_all(dir);
}
