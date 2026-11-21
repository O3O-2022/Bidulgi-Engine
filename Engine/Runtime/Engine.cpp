// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Render2D/Batch2D.h"
#include "Text/Font.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <new>

namespace {
// Looks for a file relative to the working folder and up to three parent folders
// (works from the repo root, Bin/ and build/<Config>/).
std::string FindUp(const char* envName, std::initializer_list<const char*> relPaths) {
  if (const char* env = std::getenv(envName)) return env;
  for (const char* rel : relPaths) {
    std::filesystem::path base = std::filesystem::current_path();
    for (int up = 0; up < 4; ++up, base = base.parent_path())
      if (std::filesystem::exists(base / rel)) return (base / rel).string();
  }
  return {};
}

std::string FindFont() {
  std::string p = FindUp("BDG_FONT", {"Assets/Fonts/Pretendard-Regular.otf"});
  if (!p.empty()) return p;
  for (const char* sys : {"C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/segoeui.ttf"}) // development fallback
    if (std::filesystem::exists(sys)) return sys;
  return {};
}

bdg::Color WithAlpha(bdg::Color c, float a) { c.a = a; return c; }

struct Ui {
  bdg::Batch2D& b;
  bdg::Font* f;
  const bdg::Theme& t;
  void Text(float x, float y, float size, bdg::Color c, std::string_view s) { if (f) f->DrawText(b, x, y, size, c, s); }
  float Measure(float size, std::string_view s) { return f ? f->Measure(size, s) : 40.f; }
};

// Panel = 1px seam (line) + body, header strip in "win" with the title; the active tab gets the accent top border.
void Panel(Ui& u, float x, float y, float w, float h, std::string_view title, std::string_view second = {}) {
  const bdg::Theme& t = u.t;
  float hh = t.N("size-panel-header", 30), body = t.FontSize("body", 13);
  u.b.Rect(x, y, w, h, t.C("panel"), t.N("radius-sm", 3), 1, t.C("line"));
  u.b.Rect(x + 1, y + 1, w - 2, hh, t.C("win"));
  float tw = u.Measure(body, title) + 20;
  u.b.Rect(x + 6, y + 5, tw, hh - 8, t.C("raise"), t.N("radius-sm", 3));
  u.b.Rect(x + 6, y + 5, tw, 2, t.C("accent"));
  u.Text(x + 16, y + (hh - body) * 0.5f - 1, body, t.C("fg"), title);
  if (!second.empty()) u.Text(x + tw + 20, y + (hh - body) * 0.5f - 1, body, t.C("fg-dim"), second);
}

void DrawEditorMock(Ui& u, float W, float H) {
  const bdg::Theme& t = u.t;
  bdg::Batch2D& b = u.b;
  const float body = t.FontSize("body", 13), label = t.FontSize("label", 12), caption = t.FontSize("caption", 10.5f);
  const float menuH = 28, wsH = t.N("size-workspace-bar", 38), statusH = t.N("size-status-bar", 24);
  const float row = t.N("size-row", 22), gap = 1, leftW = 260, rightW = 320, bottomH = 210;

  b.Rect(0, 0, W, H, t.C("win"));
  float mx = 12;
  for (const char* m : {"File", "Edit", "Assets", "GameObject", "Window", "Help"}) {
    u.Text(mx, (menuH - body) * 0.5f - 1, body, t.C("fg-dim"), m);
    mx += u.Measure(body, m) + 22;
  }
  u.Text(W - 150, (menuH - body) * 0.5f - 1, body, t.C("fg-faint"), "비둘기 엔진 0.0.1");
  // Workspace bar
  float wx = 10;
  for (const char* ws : {"Level", "Graph", "Animation", "UI Designer", "Scripting"}) {
    float w = u.Measure(label, ws) + 24;
    bool on = wx == 10;
    if (on) { b.Rect(wx, menuH + 6, w, wsH - 12, t.C("raise"), t.N("radius-sm", 3)); b.Rect(wx, menuH + 6, w, 2, t.C("accent")); }
    u.Text(wx + 12, menuH + (wsH - label) * 0.5f - 1, label, on ? t.C("fg") : t.C("fg-dim"), ws);
    wx += w + 4;
  }
  float ib = t.N("size-icon-button", 28);
  b.Rect(W * 0.5f - ib * 1.5f - 2, menuH + (wsH - ib) * 0.5f, ib, ib, t.C("accent"), t.N("radius-md", 4));
  b.Rect(W * 0.5f - ib * 0.5f, menuH + (wsH - ib) * 0.5f, ib, ib, t.C("raise"), t.N("radius-md", 4), 1, t.C("line-strong"));
  b.Rect(W * 0.5f + ib * 0.5f + 2, menuH + (wsH - ib) * 0.5f, ib, ib, t.C("raise"), t.N("radius-md", 4), 1, t.C("line-strong"));

  float top = menuH + wsH, midH = H - top - bottomH - statusH - gap * 2;
  // Hierarchy
  Panel(u, gap, top, leftW, midH, "Hierarchy");
  const char* items[] = {"Main", "  Main Camera", "  Sun", "  Sky", "  World Settings", "  Ground", "  Player", "    Weapon", "  Crystal"};
  for (int i = 0; i < 9; ++i) {
    float y = top + t.N("size-panel-header", 30) + 6 + i * row;
    if (i == 6) b.Rect(gap + 1, y, leftW - 2, row, t.C("selection"));
    u.Text(gap + 12, y + (row - body) * 0.5f - 1, body, i == 0 ? t.C("fg") : (i == 6 ? t.C("fg") : t.C("fg-dim")), items[i]);
  }
  // Scene view
  float sx = gap * 2 + leftW, sw = W - leftW - rightW - gap * 4;
  Panel(u, sx, top, sw, midH, "Scene", "Game");
  float vy = top + t.N("size-panel-header", 30) + 1, vh = midH - t.N("size-panel-header", 30) - 2;
  b.Rect(sx + 1, vy, sw - 2, vh, t.C("sunk"));
  for (float gx = sx + 40; gx < sx + sw; gx += 40) b.Line(gx, vy, gx, vy + vh, 1, WithAlpha(t.C("line"), 0.6f));
  for (float gy = vy + 40; gy < vy + vh; gy += 40) b.Line(sx + 1, gy, sx + sw - 1, gy, 1, WithAlpha(t.C("line"), 0.6f));
  u.Text(sx + 24, vy + 18, 40, t.C("fg"), "비둘기 엔진 Hello 123");
  u.Text(sx + 24, vy + 66, 72, WithAlpha(t.C("fg"), 0.85f), "가나다 Ag");
  float cx = sx + sw * 0.5f, cy = vy + vh * 0.68f;
  b.Rect(cx - 50, cy - 50, 100, 100, t.C("prefab"), t.N("radius-lg", 5), 2, t.C("accent"));
  b.Line(cx, cy, cx + 90, cy, 3, t.C("axis-x"));
  b.Line(cx, cy, cx, cy - 90, 3, t.C("axis-y"));
  b.Line(cx, cy, cx - 55, cy + 50, 3, t.C("axis-z"));
  // Inspector
  float ix = W - rightW - gap;
  Panel(u, ix, top, rightW, midH, "Inspector");
  float iy = top + t.N("size-panel-header", 30) + 8;
  b.Rect(ix + 1, iy, rightW - 2, row + 4, t.C("raise"));
  u.Text(ix + 12, iy + (row + 4 - body) * 0.5f - 1, body, t.C("fg"), "Transform");
  const char* labels[] = {"Position", "Rotation", "Scale", "Speed", "Jump Height", "Max Health", "Team", "Tag"};
  const char* values[] = {"0, 1, 0", "0, 90, 0", "1, 1, 1", "5.0", "4.5", "100", "Blue", "Player"};
  for (int i = 0; i < 8; ++i) {
    float y = iy + row + 12 + i * (row + 6);
    u.Text(ix + 12, y + (row - label) * 0.5f - 1, label, t.C("fg-dim"), labels[i]);
    b.Rect(ix + 120, y, rightW - 132, row, t.C("sunk"), t.N("radius-sm", 3), 1, t.C("line"));
    u.Text(ix + 128, y + (row - label) * 0.5f - 1, label, t.C("fg"), values[i]);
  }
  // Project / Console
  float by = top + midH + gap;
  Panel(u, gap, by, W - gap * 2, bottomH, "Project", "Console");
  const char* assets[] = {"Characters", "Weapon.cpp", "Player.cs", "quest.lua", "Door.graph", "Knight.prefab", "M_Gold.mat", "forest.scene", "hit.wav", "Attack.anim"};
  for (int i = 0; i < 10; ++i) {
    float tx = gap + 14 + i * 92.f;
    if (tx + 76 > W) break;
    b.Rect(tx, by + 44, 76, 76, i == 5 ? t.C("prefab") : t.C("raise"), t.N("radius-lg", 5), 1, t.C("line-strong"));
    u.Text(tx, by + 126, caption, t.C("fg-dim"), assets[i]);
  }
  // Status bar
  float sy = H - statusH;
  b.Rect(0, sy, W, statusH, t.C("win"));
  b.Rect(10, sy + statusH * 0.5f - 3, 6, 6, t.C("ok"), 3);
  u.Text(22, sy + (statusH - caption) * 0.5f - 1, caption, t.C("fg-dim"), "Saved · tokens.json live");
  u.Text(W - 170, sy + (statusH - caption) * 0.5f - 1, caption, t.C("fg-dim"), "중사양 · Windows · 60 fps");
}

// Component gallery built with the retained UI framework (queue item 0-7).
std::unique_ptr<bdg::Widget> BuildGallery(int* clickCounter) {
  using namespace bdg;
  auto root = MakeBox(Axis::Column, 16);
  root->Pad(16);
  auto& cover = root->Add(MakeCover("비둘기 엔진 디자인 시스템", "부품 8개: Panel · Tabs · Button · Field · ListRow · Badge · Node · Cover"));
  cover.height = Length::Px(112);

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
  auto& play = buttons.Add(MakeButton("btn.apply", "Apply", true));
  play.onClick = [clickCounter](Widget&) { ++*clickCounter; };
  buttons.Add(MakeButton("btn.revert", "Revert"));
  buttons.Add(MakeButton("btn.select", "Select Prefab"));
  auto& badges = insp.Add(MakeBox(Axis::Row, 6));
  badges.padL = 12; badges.padT = 6;
  badges.crossAlign = Align::Center;
  badges.Add(MakeBadge("C#"));
  badges.Add(MakeBadge("Saved", 1));
  badges.Add(MakeBadge("Prefab"));

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

void DrawStress(bdg::Batch2D& b, float W, float H) { // queue 0-4 exit check: 10k rects
  for (int i = 0; i < 10000; ++i) {
    uint32_t rgb = 0x2040a0 + (i * 2654435761u >> 8 & 0x3f3f3f);
    float x = (i % 125) * (W / 125.f), y = (i / 125) * (H / 80.f);
    b.Rect(x, y, W / 125.f - 2, H / 80.f - 2, {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1}, 3);
  }
}
}

struct bdg_engine {
  bdg::Window window;
  bdg::RHI rhi;
  bdg::Batch2D batch;
  bdg::Font font;
  bdg::Theme theme;
  std::unique_ptr<bdg::UIContext> ui;
  bool gallery = false, selfTest = false;
  int clicks = 0;
  bool hasFont = false;
  int32_t maxFrames = 0;
  bool stress = false;
};

extern "C" {

bdg_result bdg_engine_create(const bdg_engine_desc* desc, bdg_engine** out) {
  if (!desc || !out) return BDG_ERR_INVALID_ARG;
  *out = nullptr;
  bdg::LogInfo("Bidulgi Engine {}", bdg_version_string());
  auto* e = new (std::nothrow) bdg_engine();
  if (!e) return BDG_ERR_PLATFORM;
  e->maxFrames = desc->max_frames;
  e->stress = std::getenv("BDG_BATCH_STRESS") != nullptr;
  if (!e->window.Create(desc->title ? desc->title : "Bidulgi Engine", desc->width, desc->height)) { delete e; return BDG_ERR_PLATFORM; }
  if (!e->rhi.Init(e->window)) { e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  if (std::getenv("BDG_GL_ERROR_TEST")) e->rhi.DebugTriggerError(); // verifies GL debug output reaches the log
  if (!e->batch.Init(e->rhi)) { e->rhi.Shutdown(); e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  std::string fontPath = FindFont();
  e->hasFont = !fontPath.empty() && e->font.Load(e->rhi, fontPath.c_str());
  if (e->hasFont) e->batch.SetTextAtlas(e->font.Atlas());
  else bdg::LogWarn("no font found: text is not drawn (set BDG_FONT)");
  std::string tokens = FindUp("BDG_TOKENS", {"design-system/project/tokens.json"});
  if (tokens.empty() || !e->theme.Load(tokens)) bdg::LogWarn("design tokens not found: colors show as magenta (set BDG_TOKENS)");
  e->ui = std::make_unique<bdg::UIContext>(e->batch, e->hasFont ? &e->font : nullptr, e->theme);
  e->ui->SetRoot(BuildGallery(&e->clicks));
  const char* screen = std::getenv("BDG_SCREEN");
  e->gallery = screen && std::string(screen) == "gallery";
  e->selfTest = std::getenv("BDG_UI_SELFTEST") != nullptr;
  *out = e;
  return BDG_OK;
}

bdg_result bdg_engine_run(bdg_engine* e) {
  if (!e) return BDG_ERR_INVALID_ARG;
  int32_t frames = 0;
  auto t0 = std::chrono::steady_clock::now();
  while (e->window.PumpEvents()) {
    if (frames % 30 == 0) e->theme.ReloadIfChanged(); // live token edits
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    e->rhi.BeginFrame(w, h, e->theme.C("ground"));
    e->batch.Begin(w, h);
    for (const bdg::InputEvent& ev : e->window.Events())
      if (ev.type == bdg::InputEvent::Type::KeyDown && ev.key == bdg::Key::F1) e->gallery = !e->gallery; // F1: gallery <-> mock
    if (e->stress) DrawStress(e->batch, (float)w, (float)h);
    else if (e->gallery) {
      if (e->selfTest && frames == 2) { // scripted input: click Apply, then Tab twice and Enter
        bdg::Widget* btn = e->ui->Find("btn.apply");
        float bx = btn->x + btn->w * 0.5f, by = btn->y + btn->h * 0.5f;
        using T = bdg::InputEvent::Type;
        e->window.InjectEvent({T::MouseMove, bx, by});
        e->window.InjectEvent({T::MouseDown, bx, by, 1});
        e->window.InjectEvent({T::MouseUp, bx, by, 1});
        for (int i = 0; i < 2; ++i) e->window.InjectEvent({T::KeyDown, 0, 0, 0, bdg::Key::Tab});
        e->window.InjectEvent({T::KeyDown, 0, 0, 0, bdg::Key::Enter});
      }
      e->ui->HandleEvents(e->window.Events());
      e->ui->Layout((float)w, (float)h);
      e->ui->Draw();
    } else { Ui u{e->batch, e->hasFont ? &e->font : nullptr, e->theme}; DrawEditorMock(u, (float)w, (float)h); }
    e->batch.End();
    e->rhi.EndFrame();
    ++frames;
    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last) {
      bdg::Color c = e->rhi.ReadPixel(4, h - 14); // menu bar area -> token "win"
      bdg::LogInfo("frame stats: quads={} flushes={} draws={} glyphs={} tokens={} | menubar rgb=({:.3f},{:.3f},{:.3f})",
                   e->batch.QuadsThisFrame(), e->batch.FlushesThisFrame(), e->rhi.DrawCallsThisFrame(),
                   e->hasFont ? e->font.GlyphCount() : 0, e->theme.TokenCount(), c.r, c.g, c.b);
      if (e->gallery) {
        bdg::Widget* f = e->ui->Focused();
        bdg::LogInfo("ui: clicks(Apply)={} focused={} tabs.view={}", e->clicks, f ? f->id : "-",
                     e->ui->Find("tabs.view")->selectedIndex);
      }
      if (const char* dump = std::getenv("BDG_DUMP_WIDGETS"))
        bdg::LogInfo("widget dump {}: {}", e->ui->DumpJSONL(dump) ? "saved" : "FAILED", dump);
      if (const char* shot = std::getenv("BDG_SCREENSHOT")) {
        std::vector<uint8_t> px;
        e->rhi.ReadPixels(w, h, px);
        bool ok = bdg::WritePNG(shot, w, h, px.data(), true);
        bdg::LogInfo("screenshot {}: {}", ok ? "saved" : "FAILED", shot);
      }
    }
    e->window.Present();
    if (last) break;
  }
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  bdg::LogInfo("frames: {} in {:.2f}s", frames, sec);
  return BDG_OK;
}

void bdg_engine_destroy(bdg_engine* e) {
  if (!e) return;
  e->font.Shutdown();
  e->batch.Shutdown();
  e->rhi.Shutdown();
  e->window.Destroy();
  delete e;
  bdg::LogInfo("shutdown ok");
}

const char* bdg_result_string(bdg_result r) {
  switch (r) {
    case BDG_OK: return "ok";
    case BDG_ERR_PLATFORM: return "platform error";
    case BDG_ERR_GRAPHICS: return "graphics error";
    case BDG_ERR_INVALID_ARG: return "invalid argument";
  }
  return "unknown";
}

const char* bdg_version_string(void) { return "0.0.1"; }

} // extern "C"
