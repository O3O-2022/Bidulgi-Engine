#include "Runtime/Screens.h"
#include "Render2D/Batch2D.h"
#include "UI/Dock.h"
#include "UI/IconAtlas.h"
#include "UI/Theme.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace bdg {
namespace {
Color Alpha(Color c, float a) { c.a = a; return c; }
Color Hex(uint32_t rgb) { return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1.f}; }

std::unique_ptr<Widget> DimLabel(std::string text, float size = 0) {
  auto l = MakeLabel(std::move(text));
  l->variant = 1;
  l->fontSize = size;
  return l;
}

std::unique_ptr<Widget> Spacer() { auto s = MakeBox(Axis::Row); s->width = Length::Fill(); return s; }

// Scene viewport placeholder: grid, selected object with gizmo, camera label. Real 3D arrives in M3.
void DrawSceneView(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"));
  Color grid = Alpha(t.C("line"), 0.7f);
  for (float gx = w.x + 40; gx < w.x + w.w; gx += 40) b.Line(gx, w.y, gx, w.y + w.h, 1, grid);
  for (float gy = w.y + 40; gy < w.y + w.h; gy += 40) b.Line(w.x, gy, w.x + w.w, gy, 1, grid);
  float cx = w.x + w.w * 0.5f, cy = w.y + w.h * 0.55f;
  b.Rect(cx - 48, cy - 48, 96, 96, Alpha(t.C("prefab"), 0.85f), t.N("radius-lg", 5), 2, t.C("accent"));
  b.Line(cx, cy, cx + 86, cy, 3, t.C("axis-x"));
  b.Line(cx, cy, cx, cy - 86, 3, t.C("axis-y"));
  b.Line(cx, cy, cx - 52, cy + 46, 3, t.C("axis-z"));
  ui.Text(w.x + 10, w.y + 8, t.FontSize("overlay", 11), t.C("fg-dim"), "Perspective · Lit · 중사양");
  ui.Text(w.x + w.w - 92, w.y + 8, t.FontSize("overlay", 11), t.C("fg-dim"), "Draw 182 / 200");
}

// Project browser placeholder: asset tiles (thumbnails arrive with the asset system in M2).
void DrawProjectTiles(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  const char* names[] = {"Characters", "Weapon.cpp", "Player.cs", "quest.lua", "Door.graph", "Knight.prefab",
                         "M_Gold.mat", "forest.scene", "hit.wav", "Attack.anim", "AG_Knight", "HUD.widget"};
  float x = w.x + 12, y = w.y + 10, tile = 72, cap = t.FontSize("caption", 10.5f);
  for (int i = 0; i < 12; ++i) {
    if (x + tile > w.x + w.w) { x = w.x + 12; y += tile + 30; }
    if (y + tile > w.y + w.h) break;
    bool prefab = i == 5 || i == 10;
    b.Rect(x, y, tile, tile, prefab ? Alpha(t.C("prefab"), 0.35f) : t.C("raise"), t.N("radius-lg", 5), 1, t.C("line-strong"));
    ui.Text(x, y + tile + 6, cap, t.C("fg-dim"), names[i]);
    x += tile + 18;
  }
}

}

namespace {
std::vector<float> g_frameTimes;
}

void PushFrameTime(float ms) {
  g_frameTimes.push_back(ms);
  if (g_frameTimes.size() > 240) g_frameTimes.erase(g_frameTimes.begin());
}

namespace {
// Brand mark in the menu bar: amber rounded square with a "B" (the logo's beak colour).
void DrawBrandMark(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  ui.Batch().Rect(w.x, w.y + 4, 20, 20, t.C("accent"), 5);
  ui.Text(w.x + 5.5f, w.y + 6, 14, t.C("on-accent"), "B");
}

// Scene viewport placeholder with a crisp two-level grid, origin axes, overlay chips and an axis widget.
void DrawSceneViewV2(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"));
  float ox = w.x + w.w * 0.5f, oy = w.y + w.h * 0.62f;
  Color minor = Alpha(t.C("line"), 0.45f), major = Alpha(t.C("line-strong"), 0.9f);
  for (int i = -60; i <= 60; ++i) {
    float gx = ox + i * 24.f, gy = oy + i * 24.f;
    bool m = i % 5 == 0;
    if (gx > w.x && gx < w.x + w.w) b.Line(gx, w.y, gx, w.y + w.h, 1, m ? major : minor);
    if (gy > w.y && gy < w.y + w.h) b.Line(w.x, gy, w.x + w.w, gy, 1, m ? major : minor);
  }
  b.Line(w.x, oy, w.x + w.w, oy, 1.5f, Alpha(t.C("axis-x"), 0.8f));
  b.Line(ox, w.y, ox, w.y + w.h, 1.5f, Alpha(t.C("axis-z"), 0.8f));
  float cx = ox, cy = oy - 70;
  b.Rect(cx - 46, cy - 46, 92, 92, Alpha(t.C("prefab"), 0.85f), t.N("radius-lg", 5), 2, t.C("accent"));
  b.Line(cx, cy, cx + 84, cy, 3, t.C("axis-x"));
  b.Line(cx, cy, cx, cy - 84, 3, t.C("axis-y"));
  b.Line(cx, cy, cx - 50, cy + 44, 3, t.C("axis-z"));
  b.Rect(cx + 80, cy - 4, 8, 8, t.C("axis-x"), 1);
  b.Rect(cx - 4, cy - 88, 8, 8, t.C("axis-y"), 1);
  // Overlay chips (top-left) and stats (top-right)
  float px = w.x + 10;
  for (const char* chip : {"Perspective", "Lit", "Show", "중사양"}) {
    float cw = ui.Measure(11, chip) + 18;
    b.Rect(px, w.y + 8, cw, 22, Alpha(t.C("win"), 0.85f), 11, 1, t.C("line"));
    ui.Text(px + 9, w.y + 12, 11, t.C("fg-dim"), chip);
    px += cw + 6;
  }
  ui.Text(w.x + w.w - 150, w.y + 12, 11, t.C("fg-dim"), "Draw 1  ·  Tris 12  ·  1.4 ms");
  // Axis widget (bottom-left)
  float ax = w.x + 36, ay = w.y + w.h - 36;
  b.Rect(ax - 26, ay - 26, 52, 52, Alpha(t.C("win"), 0.6f), 26);
  b.Line(ax, ay, ax + 18, ay, 2, t.C("axis-x"));
  b.Line(ax, ay, ax, ay - 18, 2, t.C("axis-y"));
  b.Line(ax, ay, ax - 12, ay + 10, 2, t.C("axis-z"));
  ui.Text(ax + 19, ay - 7, 10, t.C("axis-x"), "X");
  ui.Text(ax - 3, ay - 31, 10, t.C("axis-y"), "Y");
}

// File-type icon: page with a coloured language strip, or a thumbnail-like swatch for assets.
void DrawFileIcon(UIContext& ui, float x, float y, float s, const std::string& name) {
  if (ui.Icon(FileIconName(name), x + s * 0.08f, y + s * 0.08f, s * 0.84f)) return; // PhotoCraft icon set (Assets/Icons)
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  std::string ext = name.substr(name.find_last_of('.') == std::string::npos ? name.size() : name.find_last_of('.') + 1);
  struct Kind { const char* ext; const char* token; const char* tag; };
  static const Kind code[] = {{"cpp", "lang-cpp", ".CPP"}, {"h", "lang-cpp", ".H"}, {"cs", "lang-cs", "C#"},
                              {"lua", "lang-lua", "LUA"}, {"py", "lang-py", "PY"}, {"graph", "lang-graph", "G"},
                              {"animgraph", "lang-anim", "AG"}};
  for (const Kind& k : code) {
    if (ext != k.ext) continue;
    b.Rect(x + s * 0.18f, y + s * 0.08f, s * 0.64f, s * 0.84f, t.C("raise"), 4, 1, t.C("line-strong"));
    b.Rect(x + s * 0.18f, y + s * 0.62f, s * 0.64f, s * 0.2f, t.C(k.token), 2);
    ui.Text(x + s * 0.5f - ui.Measure(10, k.tag) * 0.5f, y + s * 0.645f, 10, {1, 1, 1, 1}, k.tag);
    for (int i = 0; i < 3; ++i) b.Rect(x + s * 0.28f, y + s * (0.24f + i * 0.1f), s * (0.44f - i * 0.08f), 2, t.C("fg-faint"), 1);
    return;
  }
  if (ext.empty()) { // folder
    b.Rect(x + s * 0.1f, y + s * 0.22f, s * 0.36f, s * 0.14f, Hex(0xc9a35a), 3);
    b.Rect(x + s * 0.1f, y + s * 0.3f, s * 0.8f, s * 0.5f, Hex(0xe0bd73), 4);
    return;
  }
  Color c = ext == "prefab" ? t.C("prefab") : ext == "mat" ? t.C("accent") : ext == "scene" ? t.C("ok") : t.C("fg-faint");
  b.Rect(x + s * 0.1f, y + s * 0.1f, s * 0.8f, s * 0.8f, Alpha(c, 0.22f), 6, 1, Alpha(c, 0.7f));
  if (ext == "mat") b.Rect(x + s * 0.28f, y + s * 0.28f, s * 0.44f, s * 0.44f, c, s * 0.22f);
  else if (ext == "prefab") b.Rect(x + s * 0.3f, y + s * 0.3f, s * 0.4f, s * 0.4f, c, 4);
  else if (ext == "wav") for (int i = 0; i < 9; ++i) { float hgt = s * (0.1f + 0.08f * ((i * 7) % 5)); b.Rect(x + s * (0.2f + i * 0.07f), y + s * 0.5f - hgt * 0.5f, 2.5f, hgt, c, 1); }
  else { b.Line(x + s * 0.2f, y + s * 0.7f, x + s * 0.45f, y + s * 0.4f, 2, c); b.Line(x + s * 0.45f, y + s * 0.4f, x + s * 0.8f, y + s * 0.7f, 2, c); }
}

// Bottom dock: Project / Console / Build Log / Profiler in one panel (tab index picks the view).
void DrawBottomDock(Widget& w, UIContext& ui, const Widget* panel) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  int tab = panel ? panel->selectedIndex : 0;
  float cap = t.FontSize("caption", 10.5f), body = t.FontSize("label", 12);
  if (tab == 0) { // Project: folder tree + tiles
    float tw = 180;
    b.Rect(w.x, w.y, tw, w.h, t.C("win"));
    const char* tree[] = {"★ Favorites", "  All Prefabs", "  All Scripts", "▼ Assets", "  ▶ Characters", "  ▶ Scenes", "  ▶ Scripts", "  ▶ Materials", "▶ Packages"};
    for (int i = 0; i < 9; ++i) {
      if (i == 6) b.Rect(w.x, w.y + 6 + i * 20.f, tw, 20, t.C("selection"));
      ui.Text(w.x + 10, w.y + 9 + i * 20.f, body, i == 6 ? t.C("fg") : t.C("fg-dim"), tree[i]);
    }
    // C++ header + source with the same name are shown as ONE linked "C++ class" tile (like Rider / Visual Studio).
    std::vector<std::string> files = {"Characters", "Weapon.cpp", "Weapon.h", "Health.h", "Health.cpp", "PlayerMove.cs",
                                      "quest.lua", "level_tools.py", "Door.graph", "AG_Knight.animgraph", "Knight.prefab",
                                      "M_Gold.mat", "forest.scene", "hit.wav", "Utils.h"};
    auto stem = [](const std::string& f) { size_t d = f.find_last_of('.'); return d == std::string::npos ? f : f.substr(0, d); };
    auto extOf = [](const std::string& f) { size_t d = f.find_last_of('.'); return d == std::string::npos ? std::string() : f.substr(d + 1); };
    struct Entry { std::string name; bool pair; };
    std::vector<Entry> entries;
    for (const std::string& f : files) {
      std::string e = extOf(f);
      if (e == "h" || e == "cpp") {
        std::string other = stem(f) + (e == "h" ? ".cpp" : ".h");
        bool hasOther = std::find(files.begin(), files.end(), other) != files.end();
        if (hasOther) {
          bool seen = false;
          for (auto& en : entries) seen |= en.pair && en.name == stem(f);
          if (!seen) entries.push_back({stem(f), true});
          continue;
        }
      }
      entries.push_back({f, false});
    }
    float x = w.x + tw + 14, y = w.y + 10, s = 64;
    for (const Entry& en : entries) {
      float tileW = en.pair ? s * 2 + 10 : s;
      if (x + tileW > w.x + w.w) { x = w.x + tw + 14; y += s + 28; }
      if (y + s > w.y + w.h) break;
      std::string label = en.name;
      if (en.pair) {
        Color cpp = t.C("lang-cpp"), soft = cpp;
        soft.a = 0.12f;
        b.Rect(x - 4, y - 2, tileW + 8, s + 4, soft, 8, 1, Alpha(cpp, 0.55f));     // shared frame = one class
        DrawFileIcon(ui, x, y, s, en.name + ".h");
        DrawFileIcon(ui, x + s + 10, y, s, en.name + ".cpp");
        float ly = y + s * 0.42f;                                               // link between the two files
        b.Line(x + s * 0.82f, ly, x + s + 10 + s * 0.18f, ly, 2, cpp);
        b.Rect(x + s + 5 - 4, ly - 4, 8, 8, cpp, 4);
        label = en.name + "  · C++ class";
      } else {
        DrawFileIcon(ui, x, y, s, en.name);
      }
      while (ui.Measure(cap, label) > tileW + 14 && label.size() > 4) label = label.substr(0, label.size() - 4) + "…";
      ui.Text(x + tileW * 0.5f - ui.Measure(cap, label) * 0.5f, y + s + 4, cap, en.pair ? t.C("fg") : t.C("fg-dim"), label);
      x += tileW + 22;
    }
  } else if (tab == 1 || tab == 2) { // Console / Build Log
    struct Line { const char* lvl; const char* text; };
    static const Line console[] = {{"info", "Bidulgi Engine 0.0.1"}, {"info", "rhi: OpenGL 4.5 · NVIDIA GeForce RTX 5070 Ti"},
                                   {"info", "font: Pretendard (SDF atlas 2048)"}, {"info", "tokens: 40 colors, 18 sizes, 12 type styles"},
                                   {"warn", "vsync ignored by the driver, frame limiter 240 Hz"}, {"info", "PlayerMove.cs compiled in 0.42 s"},
                                   {"error", "quest.lua:12  attempt to index nil value 'npc'"}};
    static const Line build[] = {{"info", "> bdg build --platform windows --profile development"}, {"info", "[1/5] collect   38 assets"},
                                 {"info", "[2/5] cook      BC7 · binary scenes (cache 92%)"}, {"info", "[3/5] scripts   C# 3 · Lua 2 · Graph 4"},
                                 {"info", "[4/5] pack      base.bpak 12.4 MB"}, {"ok", "[5/5] done      Bin/MyGame.exe  (6.8 s)"}};
    const Line* lines = tab == 1 ? console : build;
    int n = tab == 1 ? 7 : 6;
    for (int i = 0; i < n; ++i) {
      std::string lvl = lines[i].lvl;
      Color c = lvl == "error" ? t.C("err") : lvl == "warn" ? t.C("warn") : lvl == "ok" ? t.C("ok") : t.C("fg-faint");
      float y = w.y + 8 + i * 20.f;
      if (lvl == "error") b.Rect(w.x, y - 2, w.w, 20, Alpha(t.C("err"), 0.1f));
      b.Rect(w.x + 12, y + 5, 6, 6, c, 3);
      ui.Text(w.x + 26, y + 1, body, lvl == "info" ? t.C("fg-dim") : c, lines[i].text, true);
    }
  } else { // Profiler: frame time bars (real values from the engine loop)
    float maxMs = 16.7f;
    for (float v : g_frameTimes) maxMs = std::max(maxMs, v);
    float gx = w.x + 50, gw = w.w - 70, gh = w.h - 40, gy = w.y + 12;
    b.Line(gx, gy + gh * (1 - 16.7f / maxMs), gx + gw, gy + gh * (1 - 16.7f / maxMs), 1, Alpha(t.C("warn"), 0.7f));
    ui.Text(w.x + 6, gy + gh * (1 - 16.7f / maxMs) - 7, 10, t.C("warn"), "16.7ms");
    float bw = gw / 240.f, avg = 0;
    for (size_t i = 0; i < g_frameTimes.size(); ++i) {
      float v = g_frameTimes[i], hgt = gh * (v / maxMs);
      avg += v;
      b.Rect(gx + i * bw, gy + gh - hgt, std::max(1.f, bw - 1), hgt, v > 16.7f ? t.C("err") : t.C("prefab"));
    }
    if (!g_frameTimes.empty()) avg /= g_frameTimes.size();
    char buf[96];
    std::snprintf(buf, sizeof buf, "frame avg %.2f ms  ·  %zu frames  ·  CPU main thread", avg, g_frameTimes.size());
    ui.Text(gx, w.y + w.h - 22, cap, t.C("fg-dim"), buf, true);
  }
}
}

std::unique_ptr<Widget> BuildEditorScreen() {
  auto root = MakeBox(Axis::Column, 1);

  // Menu bar: brand mark + menus + project name
  auto& menu = root->Add(MakeBox(Axis::Row, 20));
  menu.height = Length::Px(30);
  menu.padL = 10; menu.padR = 12;
  menu.crossAlign = Align::Center;
  auto& mark = menu.Add(MakeCustom("brand", DrawBrandMark));
  mark.width = Length::Px(20); mark.height = Length::Px(28);
  for (const char* m : {"File", "Edit", "Assets", "GameObject", "Component", "Build", "Window", "Help"}) menu.Add(DimLabel(m));
  menu.Add(Spacer());
  menu.Add(DimLabel("SampleProject  ·  Main.scene", 12));

  // Toolbar: workspaces, play controls, quality/platform
  auto& ws = root->Add(MakeBox(Axis::Row, 8));
  ws.height = Length::Px(40);
  ws.padL = 8; ws.padR = 8;
  ws.crossAlign = Align::Center;
  auto& wsTabs = ws.Add(MakeTabs("ws.tabs", {"Level", "Graph", "Animation", "UI Designer", "Scripting"}, 0));
  wsTabs.variant = 1;
  ws.Add(Spacer());
  ws.Add(MakeButton("tool.play", "▶  Play", true));
  ws.Add(MakeButton("tool.pause", "||"));
  ws.Add(MakeButton("tool.step", "▶|"));
  ws.Add(Spacer());
  ws.Add(MakeBadge("중사양")).id = "tool.quality"; // quality tier; a click steps through the project's tiers (3-5)
  ws.Add(MakeBadge("Windows"));
  ws.Add(MakeButton("tool.build", "Build"));

  // Main area: docked panels (drag splitters to resize, drag tabs to move; layout saved per user).
  auto state = std::make_shared<DockState>();
  state->titles = {{"hierarchy", "Hierarchy"}, {"scene", "Scene"}, {"game", "Game"}, {"project", "Project"},
                   {"console", "Console"}, {"buildlog", "Build Log"}, {"profiler", "Profiler"}, {"history", "History"},
                   {"inspector", "Inspector"}, {"settings", "Project Settings"}};
  int hierLeaf = state->AddLeaf({"hierarchy"});
  int sceneLeaf = state->AddLeaf({"scene", "game"});
  int bottomLeaf = state->AddLeaf({"project", "console", "buildlog", "profiler", "history"});
  int inspLeaf = state->AddLeaf({"inspector", "settings"});
  int center = state->AddSplit(Axis::Column, 0.68f, sceneLeaf, bottomLeaf);
  int right = state->AddSplit(Axis::Row, 0.675f, center, inspLeaf);
  state->AddSplit(Axis::Row, 0.2f, hierLeaf, right);
  // First run: choose the shortcut preset (hidden once a project shortcut file exists)
  auto& banner = root->Add(MakeBox(Axis::Row, 8));
  banner.id = "shortcut.banner";
  banner.hidden = true;
  banner.padL = 12; banner.padT = 4; banner.padB = 6;
  banner.crossAlign = Align::Center;
  banner.Add(MakeLabel("단축키를 어떤 방식으로 쓸까요? (Project Settings에서 언제든 바꿀 수 있습니다)"));
  banner.Add(MakeButton("shortcut.unity", "Unity 방식", true));
  banner.Add(MakeButton("shortcut.unreal", "Unreal 방식"));

  // After play mode: keep the values changed while playing? (queue 3-6)
  auto& keep = root->Add(MakeBox(Axis::Row, 8));
  keep.id = "play.keep.banner";
  keep.hidden = true;
  keep.padL = 12; keep.padT = 4; keep.padB = 6;
  keep.crossAlign = Align::Center;
  keep.Add(MakeLabel("")).id = "play.keep.text";
  keep.Add(MakeButton("play.keep", "유지", true));
  keep.Add(MakeButton("play.discard", "버리기"));

  auto& dock = root->Add(MakeDock("dock", state));
  dock.height = Length::Fill();

  // Hierarchy with type icons
  auto& hier = dock.Add(MakeBox(Axis::Column));
  hier.id = "hierarchy";
  hier.padT = 4;
  // rows come from the scene (EditorModel)

  // Viewports
  dock.Add(MakeCustom("scene", DrawSceneViewV2));
  dock.Add(MakeCustom("game", [](Widget& w, UIContext& ui) {
    const Theme& t = ui.Tokens();
    ui.Batch().Rect(w.x, w.y, w.w, w.h, t.C("ground"));
    const char* msg = "Game view: press Play";
    float fs = t.FontSize("label", 12);
    ui.Text(w.x + (w.w - ui.Measure(fs, msg)) * 0.5f, w.y + w.h * 0.5f - fs, fs, t.C("fg-faint"), msg);
  }));

  // Bottom tools: one panel per tab (Project, Console, Build Log, Profiler)
  const char* bottom[] = {"project", "console", "buildlog", "profiler"};
  for (int i = 0; i < 4; ++i) {
    auto tab = std::make_shared<Widget>();
    tab->selectedIndex = i;
    auto draw = [tab](Widget& w, UIContext& ui) { DrawBottomDock(w, ui, tab.get()); };
    if (i == 0) { // Project: the real asset folder when one is open (EditorModel), else the sample drawing
      auto& project = dock.Add(MakeBox(Axis::Column));
      project.id = "project";
      project.Add(MakeCustom("project.sample", draw)).height = Length::Fill();
      continue;
    }
    dock.Add(MakeCustom(bottom[i], draw));
  }

  // History: every undo step; click one to go back or forward to it (EditorModel fills the rows)
  auto& history = dock.Add(MakeBox(Axis::Column));
  history.id = "history";
  history.padT = 4;

  // Inspector: component cards (click a header to fold)
  auto& insp = dock.Add(MakeBox(Axis::Column, 4));
  insp.id = "inspector";
  insp.padT = 4;
  // cards come from the selected object's components by reflection (EditorModel)

  auto& settings = dock.Add(MakeBox(Axis::Column, 2));
  settings.id = "settings";
  settings.padT = 4;
  settings.Add(MakeLabel("Rendering")).variant = 2;
  settings.Add(MakeField("s.quality", "Quality", "Medium"));
  settings.Add(MakeField("s.vsync", "VSync", "Adaptive"));
  settings.Add(MakeLabel("Shortcuts")).variant = 2;
  auto& presets = settings.Add(MakeBox(Axis::Row, 6));
  presets.padL = 12; presets.padT = 4; presets.padB = 4;
  presets.Add(MakeButton("shortcut.preset.unity", "Unity"));
  presets.Add(MakeButton("shortcut.preset.unreal", "Unreal"));
  auto& list = settings.Add(MakeBox(Axis::Column));
  list.id = "settings.shortcuts"; // action rows, filled by the engine

  // Status bar: build state, live FPS / memory, branch, encoding
  auto& status = root->Add(MakeBox(Axis::Row, 10));
  status.height = Length::Px(24);
  status.padL = 10; status.padR = 10;
  status.crossAlign = Align::Center;
  status.Add(MakeBadge("Build OK", 1));
  auto& perf = status.Add(DimLabel("— fps", 10.5f));
  perf.id = "status.perf";
  status.Add(Spacer());
  status.Add(DimLabel("main  ·  UTF-8  ·  F1 갤러리", 10.5f));
  auto& crash = status.Add(MakeButton("status.crash", "지난 실행이 비정상 종료됨 — 제보하기"));
  crash.height = Length::Px(20);
  crash.hidden = true; // shown by the engine when the previous session crashed
  auto& bug = status.Add(MakeButton("status.bug", "버그 제보 (Ctrl+Shift+B)"));
  bug.height = Length::Px(20);
  return root;
}

std::unique_ptr<Widget> BuildGalleryScreen(int* clickCounter) {
  auto root = MakeBox(Axis::Column, 16);
  root->Pad(16);
  // Title (the 960x300 Cover component itself is checked by the parity screen: BDG_SCREEN=parity:Cover).
  auto& head = root->Add(MakeBox(Axis::Column, 4));
  head.padL = 4;
  auto& title = head.Add(MakeLabel("비둘기 엔진 디자인 시스템"));
  title.fontSize = 20; title.lineHeight = 1.2f;
  head.Add(MakeLabel("부품 8개: Panel · Tabs · Button · Field · ListRow · Badge · Node · Cover")).variant = 1;

  auto& row = root->Add(MakeBox(Axis::Row, 16));
  row.height = Length::Fill();

  auto& hier = row.Add(MakePanel("panel.hierarchy", "Hierarchy"));
  hier.width = Length::Px(280);
  hier.Add(MakeListRow("row.camera", "Main Camera", "Camera"));
  hier.Add(MakeListRow("row.sun", "Sun", "Light"));
  hier.Add(MakeListRow("row.sky", "Sky"));
  hier.Add(MakeListRow("row.world", "World Settings"));
  hier.Add(MakeListRow("row.player", "Player", "Prefab")).selected = true;
  hier.Add(MakeListRow("row.weapon", "Weapon", "", 1));
  hier.Add(MakeListRow("row.crystal", "Crystal", "Prefab"));

  auto& insp = row.Add(MakePanel("panel.inspector", "Inspector"));
  insp.width = Length::Fill();
  insp.gap = 2;
  insp.Add(MakeField("field.position", "Position", "0, 1, 0"));
  insp.Add(MakeField("field.rotation", "Rotation", "0, 90, 0"));
  insp.Add(MakeField("field.scale", "Scale", "1, 1, 1"));
  insp.Add(MakeField("field.speed", "Speed", "5.0"));
  auto& buttons = insp.Add(MakeBox(Axis::Row, 8));
  buttons.padL = 12; buttons.padT = 10; buttons.padB = 6;
  buttons.crossAlign = Align::Center;
  auto& apply = buttons.Add(MakeButton("btn.apply", "Apply", true));
  apply.onClick = [clickCounter](Widget&) { ++*clickCounter; };
  buttons.Add(MakeButton("btn.revert", "Revert"));
  buttons.Add(MakeButton("btn.select", "Select Prefab"));
  auto& badges = insp.Add(MakeBox(Axis::Row, 6));
  badges.padL = 12; badges.padT = 6;
  badges.crossAlign = Align::Center;
  badges.Add(MakeBadge("C#"));
  badges.Add(MakeBadge("Saved", 1));
  badges.Add(MakeBadge("Prefab"));
  auto& card = insp.Add(MakeCard("gallery.card", "PlayerMove", "C#")); // component header (.bdg-comp-head)
  card.Add(MakeField("gallery.card.speed", "Move Speed", "4.5"));

  auto& side = row.Add(MakeBox(Axis::Column, 16));
  side.width = Length::Px(440);
  side.Add(MakeTabs("tabs.view", {"Scene", "Game", "Graph", "Animation"}, 2));
  auto& nodes = side.Add(MakeBox(Axis::Row, 28));
  nodes.crossAlign = Align::Start;
  nodes.padT = 8; nodes.padL = 4;
  nodes.Add(MakeNode("node.start", "Start", "Event", 1, {"out|exec|", "out|object|Self"}));
  nodes.Add(MakeNode("node.rotate", "Rotate", "Transform", 2, {"in|exec|", "in|object|Target", "in|float|Speed", "out|exec|"}));
  return root;
}

// ---- Design parity screens -------------------------------------------------------------------
// Each mirrors a component preview: the root box has the preview root's padding/gap, children use the
// same ids. Pieces the engine does not draw yet (icon button groups) are fixed-size boxes sized from tokens.
std::unique_ptr<Widget> BuildParityScreen(const std::string& c) {
  auto root = MakeBox(Axis::Column);
  auto withId = [](std::unique_ptr<Widget> w, const char* id) { w->id = id; return w; };
  if (c == "Button") {
    auto& r = root->Add(withId(MakeBox(Axis::Row, 12), "button.root"));
    r.Pad(16); r.crossAlign = Align::Center;
    r.Add(MakeButton("button.compile", "Compile", true));
    r.Add(MakeButton("button.compiled", "컴파일됨")).variant = 1;
    r.Add(MakeButton("button.diff", "Diff"));
    for (const char* id : {"button.group.play", "button.group.tools"}) { // .bdg-group with two .bdg-iconbtn
      auto& g = r.Add(withId(MakeBox(Axis::Row), id));
      g.width = Length::Px(2 * 2 + 2 + 2 * 28 + 2); // padding + border + 2 icon buttons (size-icon-button) + gap
      g.height = Length::Px(24 + 2 * 2 + 2);
    }
  } else if (c == "Badge") {
    auto& r = root->Add(withId(MakeBox(Axis::Row, 8), "badge.root"));
    r.Pad(16); r.crossAlign = Align::Center;
    struct B { const char* id; const char* text; int variant; };
    for (B b : {B{"badge.cs", "C#", 0}, B{"badge.luau", "Luau", 0}, B{"badge.rust", "Rust", 0}, B{"badge.ok", "OK 컴파일됨", 1},
                B{"badge.warn", "WARN 예산 초과", 2}, B{"badge.err", "ERROR 참조 없음", 3}, B{"badge.unity", "Unity", 10},
                B{"badge.unreal", "Unreal", 11}, B{"badge.blender", "Blender", 12}, B{"badge.native", "비둘기 고유", 13}})
      r.Add(withId(MakeBadge(b.text, b.variant), b.id));
  } else if (c == "Tabs") {
    auto& r = root->Add(withId(MakeBox(Axis::Column, 12), "tabs.root"));
    r.Pad(12); r.width = Length::Px(640 + 24);
    r.Add(MakeTabs("tabs.panel", {"Scene", "Game"}, 0));
    r.Add(MakeTabs("tabs.ws", {"Level", "Graph", "Animation", "UI Designer", "Scripting"}, 0)).variant = 2;
  } else if (c == "ListRow") {
    auto& r = root->Add(withId(MakeBox(Axis::Column), "list.root"));
    r.width = Length::Px(302); r.padL = r.padR = 1; r.padT = r.padB = 1 + 2; // panel border + list padding
    r.Add(MakeListRow("list.camera", "Main Camera", "Camera"));
    r.Add(MakeListRow("list.sun", "Sun", "Light"));
    r.Add(MakeListRow("list.player", "Player", "Mesh"));
    r.Add(MakeListRow("list.sign", "Shop Sign", "Widget")).selected = true;
    r.Add(MakeListRow("list.enemies", "▾ Enemies", "Empty"));
    r.Add(MakeListRow("list.goblin", "Goblin", "BP_Goblin", 1));
  } else if (c == "Field") {
    auto& r = root->Add(withId(MakeBox(Axis::Column), "field.root"));
    r.width = Length::Px(362); r.Pad(1);
    auto head = [&](const char* id, const char* text) { auto& l = r.Add(withId(MakeLabel(text), id)); l.variant = 2; };
    head("field.head.transform", "Transform");
    r.Add(MakeField("field.position", "Position", "-4, 1.6, 0"));
    head("field.head.widget", "Widget");
    r.Add(MakeField("field.ui", "UI", "ui/shop.ui"));
    r.Add(MakeField("field.space", "Space", "World"));
    r.Add(MakeField("field.interactive", "Interactive", "true"));
    r.Add(MakeField("field.hp", "Hp", "12"));
  } else if (c == "Panel") {
    auto& r = root->Add(withId(MakeBox(Axis::Row, 1), "panel.root"));
    r.width = Length::Px(642); r.Pad(1);
    auto& left = r.Add(MakePanelTabs("panel.hierarchy", {"Hierarchy", "Search"}, 0));
    left.width = Length::Fill();
    left.Add(withId(MakeLabel("Scene"), "panel.caption")).variant = 3;
    auto& list = left.Add(MakeBox(Axis::Column));
    list.padT = list.padB = 2;
    list.Add(MakeListRow("panel.camera", "Main Camera", "Camera"));
    list.Add(MakeListRow("panel.sign", "Shop Sign", "Widget")).selected = true;
    auto& right = r.Add(MakePanelTabs("panel.inspector", {"Inspector", "Text"}, 0));
    right.width = Length::Fill();
    right.Add(MakeField("panel.name", "Name", "Shop Sign"));
    right.Add(MakeField("panel.space", "Space", "World"));
  } else if (c == "Node") {
    auto& r = root->Add(withId(MakeBox(Axis::Row, 18), "node.root"));
    r.Pad(18); r.crossAlign = Align::Start;
    r.Add(MakeNode("node.update", "Event Update", "", 1, {"out|exec|", "out|float|Delta Time"}));
    r.Add(MakeNode("node.distance", "Distance", "Vector", 3, {"in|vector|A", "in|vector|B", "out|float|"}));
    r.Add(MakeNode("node.move", "Move Towards", "Transform", 2, {"in|exec|", "in|vector|Target", "in|float|Speed", "out|exec|"})).selected = true;
    r.Add(MakeNode("node.branch", "Branch", "", 0, {"in|exec|", "in|bool|Condition", "out|exec|True", "out|exec|False"}));
  } else if (c == "Cover") {
    root->crossAlign = Align::Start;
    auto& cv = root->Add(MakeCover("cover.root", "Bidulgi\nEditor", "AI와 사람이 같은 방식으로 다루는 게임 엔진 에디터"));
    cv.children[0]->id = "cover.name";
    cv.children[1]->id = "cover.tag";
  } else {
    return nullptr;
  }
  return root;
}
}
