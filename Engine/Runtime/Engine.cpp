// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Render2D/Batch2D.h"
#include "Text/Font.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <new>

namespace {
constexpr bdg::Color Hex(uint32_t rgb, float a = 1.f) {
  return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}
// Design system tokens (hard-coded until queue item 0-6 reads tokens.json).
constexpr bdg::Color kGround = Hex(0x16181d), kPanel = Hex(0x1d2026), kRaised = Hex(0x262a31),
                     kLine = Hex(0x2f343c), kAccent = Hex(0x4ea1ff), kFg = Hex(0xe6e8eb), kFg2 = Hex(0xa3a9b3),
                     kTeal = Hex(0x2bb3a3), kOrange = Hex(0xf08a3c);

// First font found: project font folder (Pretendard later), then a Windows system font for development.
std::string FindFont() {
  const char* candidates[] = {"Assets/Fonts/Pretendard-Regular.otf", "../Assets/Fonts/Pretendard-Regular.otf",
                              "C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/segoeui.ttf"};
  if (const char* env = std::getenv("BDG_FONT")) return env;
  for (const char* c : candidates) if (std::filesystem::exists(c)) return c;
  return {};
}

struct Ui { bdg::Batch2D& b; bdg::Font* f; };
void Text(Ui& u, float x, float y, float size, bdg::Color c, std::string_view s) {
  if (u.f) u.f->DrawText(u.b, x, y, size, c, s);
}

void PanelHeader(Ui& u, float x, float y, float w, std::string_view title, std::string_view second = {}) {
  u.b.Rect(x, y, w, 30, kRaised, 6);
  u.b.Rect(x + 8, y + 27, 60, 2, kAccent);
  Text(u, x + 12, y + 7, 13, kFg, title);
  if (!second.empty()) Text(u, x + 90, y + 7, 13, kFg2, second);
}

void DrawEditorMock(Ui& u, float W, float H) {
  bdg::Batch2D& b = u.b;
  const float menuH = 28, toolH = 36, leftW = 260, rightW = 320, bottomH = 220, gap = 4;
  b.Rect(0, 0, W, menuH, kPanel);
  const char* menus[] = {"File", "Edit", "Assets", "GameObject", "Window", "Help"};
  float mx = 12;
  for (const char* m : menus) { Text(u, mx, 6, 13, kFg2, m); mx += (u.f ? u.f->Measure(13, m) : 40) + 22; }
  Text(u, W - 190, 6, 13, kFg2, "비둘기 엔진 0.0.1");
  b.Rect(0, menuH, W, toolH, kGround);
  for (int i = 0; i < 4; ++i) b.Rect(10 + i * 34.f, menuH + 5, 28, 26, kRaised, 6);
  b.Rect(W * 0.5f - 46, menuH + 5, 28, 26, kAccent, 6);
  b.Rect(W * 0.5f - 14, menuH + 5, 28, 26, kRaised, 6);
  b.Rect(W * 0.5f + 18, menuH + 5, 28, 26, kRaised, 6);
  Text(u, W - 150, menuH + 10, 12, kFg2, "중사양 · Windows");

  float top = menuH + toolH, midH = H - top - bottomH - gap;
  // Hierarchy
  b.Rect(gap, top, leftW, midH, kPanel, 6, 1, kLine);
  PanelHeader(u, gap, top, leftW, "Hierarchy");
  const char* items[] = {"Main", "  Main Camera", "  Sun", "  Sky", "  World Settings", "  Ground", "  Player", "    Weapon", "  Crystal"};
  for (int i = 0; i < 9; ++i) {
    float y = top + 40 + i * 26.f;
    if (i == 6) b.Rect(gap + 6, y - 4, leftW - 12, 24, Hex(0x4ea1ff, 0.22f), 4, 1, kAccent);
    Text(u, gap + 14, y, 13, i == 0 ? kFg : kFg2, items[i]);
  }
  // Scene view
  float sx = gap * 2 + leftW, sw = W - leftW - rightW - gap * 4;
  b.Rect(sx, top, sw, midH, Hex(0x101216), 6, 1, kLine);
  for (float gx = sx + 40; gx < sx + sw; gx += 40) b.Line(gx, top + 1, gx, top + midH - 1, 1, Hex(0xffffff, 0.04f));
  for (float gy = top + 40; gy < top + midH; gy += 40) b.Line(sx + 1, gy, sx + sw - 1, gy, 1, Hex(0xffffff, 0.04f));
  Text(u, sx + 12, top + 10, 12, kFg2, "Scene  ·  Perspective");
  float cx = sx + sw * 0.5f, cy = top + midH * 0.58f;
  b.Rect(cx - 60, cy - 60, 120, 120, kTeal, 12, 3, Hex(0xffffff, 0.85f));
  b.Line(cx, cy, cx + 100, cy, 3, Hex(0xe5484d));
  b.Line(cx, cy, cx, cy - 100, 3, Hex(0x46c463));
  b.Line(cx, cy, cx - 60, cy + 55, 3, kAccent);
  b.Rect(cx + 120, cy - 20, 56, 56, kOrange, 28);
  // SDF check: the same atlas drawn small and very large must both stay sharp.
  Text(u, sx + 24, top + 40, 40, kFg, "비둘기 엔진 Hello 123");
  Text(u, sx + 24, top + 92, 96, Hex(0xe6e8eb, 0.9f), "가나다 Ag");
  // Inspector
  float ix = W - rightW - gap;
  b.Rect(ix, top, rightW, midH, kPanel, 6, 1, kLine);
  PanelHeader(u, ix, top, rightW, "Inspector");
  Text(u, ix + 14, top + 40, 15, kFg, "Player");
  const char* labels[] = {"Position", "Rotation", "Scale", "Speed", "Jump Height", "Max Health", "Team", "Tag"};
  const char* values[] = {"0, 1, 0", "0, 90, 0", "1, 1, 1", "5.0", "4.5", "100", "Blue", "Player"};
  for (int i = 0; i < 8; ++i) {
    float y = top + 70 + i * 32.f;
    Text(u, ix + 14, y + 3, 13, kFg2, labels[i]);
    b.Rect(ix + 120, y, rightW - 136, 22, kGround, 4, 1, kLine);
    Text(u, ix + 128, y + 3, 13, kFg, values[i]);
  }
  // Project / Console
  float by = H - bottomH;
  b.Rect(gap, by, W - gap * 2, bottomH - gap, kPanel, 6, 1, kLine);
  PanelHeader(u, gap, by, W - gap * 2, "Project", "Console");
  const char* assets[] = {"Characters", "Weapon.cpp", "Player.cs", "quest.lua", "Door.graph", "Knight.prefab", "M_Gold.mat", "forest.scene", "hit.wav", "Attack.anim"};
  for (int i = 0; i < 10; ++i) {
    float tx = gap + 16 + i * 96.f;
    if (tx + 80 > W) break;
    b.Rect(tx, by + 44, 80, 80, i == 5 ? kTeal : kRaised, 8);
    Text(u, tx, by + 132, 11, kFg2, assets[i]);
  }
}

void DrawStress(bdg::Batch2D& b, float W, float H) { // queue 0-4 exit check: 10k rects
  for (int i = 0; i < 10000; ++i) {
    float x = (i % 125) * (W / 125.f), y = (i / 125) * (H / 80.f);
    b.Rect(x, y, W / 125.f - 2, H / 80.f - 2, Hex(0x2040a0 + (i * 2654435761u >> 8 & 0x3f3f3f)), 3);
  }
}
}

struct bdg_engine {
  bdg::Window window;
  bdg::RHI rhi;
  bdg::Batch2D batch;
  bdg::Font font;
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
  *out = e;
  return BDG_OK;
}

bdg_result bdg_engine_run(bdg_engine* e) {
  if (!e) return BDG_ERR_INVALID_ARG;
  int32_t frames = 0;
  auto t0 = std::chrono::steady_clock::now();
  while (e->window.PumpEvents()) {
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    e->rhi.BeginFrame(w, h, kGround);
    e->batch.Begin(w, h);
    if (e->stress) DrawStress(e->batch, (float)w, (float)h);
    else { Ui u{e->batch, e->hasFont ? &e->font : nullptr}; DrawEditorMock(u, (float)w, (float)h); }
    e->batch.End();
    e->rhi.EndFrame();
    ++frames;
    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last) {
      bdg::LogInfo("frame stats: quads={} flushes={} draws={} glyphs={}", e->batch.QuadsThisFrame(),
                   e->batch.FlushesThisFrame(), e->rhi.DrawCallsThisFrame(), e->hasFont ? e->font.GlyphCount() : 0);
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
