// Shortcut system tests (queue 1-5).
#include "UI/Shortcuts.h"
#include "bdg_test.h"
#include <cstdio>
#include <filesystem>

using namespace bdg;

namespace {
InputEvent Press(bdg::Key k, bool ctrl = false, bool shift = false, bool alt = false) {
  InputEvent e{InputEvent::Type::KeyDown};
  e.key = k; e.ctrl = ctrl; e.shift = shift; e.alt = alt;
  return e;
}
}

TEST_CASE("shortcuts: key combos parse and print") {
  KeyCombo c;
  CHECK(KeyCombo::Parse("Ctrl+Shift+Z", c) && c.ctrl && c.shift && !c.alt && c.key == Key::Z);
  CHECK_EQ(c.ToString(), std::string("Ctrl+Shift+Z"));
  CHECK(KeyCombo::Parse("alt + p", c) && c.alt && c.key == Key::P);
  CHECK(KeyCombo::Parse("F5", c) && c.key == Key::F5 && !c.ctrl);
  CHECK(KeyCombo::Parse("Delete", c) && c.key == Key::Delete);
  CHECK(KeyCombo::Parse("Ctrl+1", c) && c.key == Key::Num1);
  CHECK(!KeyCombo::Parse("Hyper+X", c));
  CHECK(!KeyCombo::Parse("Ctrl+", c));
  CHECK(!KeyCombo::Parse("Ctrl+Banana", c));
}

TEST_CASE("shortcuts: presets bind the same action to different keys") {
  Shortcuts s;
  int play = 0, dup = 0;
  s.Register("play.toggle", "Play", [&] { ++play; });
  s.Register("edit.duplicate", "Duplicate", [&] { ++dup; });
  CHECK_EQ(s.Preset(), std::string("unity"));
  CHECK(s.Handle(Press(Key::P, true)));             // Unity: Ctrl+P
  CHECK(!s.Handle(Press(Key::P, false, false, true)));
  CHECK(s.Handle(Press(Key::D, true)));
  CHECK(s.ApplyPreset("unreal"));
  CHECK(!s.Handle(Press(Key::P, true)));
  CHECK(s.Handle(Press(Key::P, false, false, true))); // Unreal: Alt+P
  CHECK(s.Handle(Press(Key::W, true)));               // Unreal: Ctrl+W duplicates
  CHECK(play == 2 && dup == 2);
  CHECK(!s.ApplyPreset("blender"));
  CHECK_EQ(s.KeysOf("play.toggle"), std::string("Alt+P"));
}

TEST_CASE("shortcuts: custom bindings, conflicts, file") {
  Shortcuts s;
  int a = 0, b = 0;
  s.Register("play.toggle", "Play", [&] { ++a; });
  s.Register("tool.move", "Move", [&] { ++b; });
  CHECK(s.Conflicts().empty());
  std::string err;
  CHECK(s.Bind("tool.move", "Ctrl+P, M", &err));
  auto conflicts = s.Conflicts();
  CHECK(conflicts.size() == 1 && conflicts[0].combo == "Ctrl+P");
  CHECK(!s.Bind("tool.fly", "F", &err) && err.find("unknown action") != std::string::npos);
  CHECK(!s.Bind("tool.move", "Ctrl+Nope", &err));
  s.ApplyPreset("unreal");                          // user bindings survive a preset switch
  CHECK_EQ(s.KeysOf("tool.move"), std::string("Ctrl+P, M"));
  std::string path = (std::filesystem::temp_directory_path() / "bdg_shortcuts_test.jsonl").string();
  CHECK(s.Save(path));
  Shortcuts t;
  t.Register("play.toggle", "Play", [] {});
  t.Register("tool.move", "Move", [] {});
  CHECK(t.Load(path, &err));
  CHECK_EQ(t.Preset(), std::string("unreal"));
  CHECK_EQ(t.KeysOf("tool.move"), std::string("Ctrl+P, M"));
  CHECK_EQ(t.KeysOf("play.toggle"), std::string("Alt+P"));
  std::remove(path.c_str());
  CHECK(!t.Load(path, &err)); // missing file
}
