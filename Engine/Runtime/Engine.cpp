// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Render2D/Batch2D.h"
#include <chrono>
#include <cstdlib>
#include <new>

namespace {
constexpr bdg::Color Hex(uint32_t rgb, float a = 1.f) {
  return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}
// Design system tokens (hard-coded until queue item 0-6 reads tokens.json).
constexpr bdg::Color kGround = Hex(0x16181d), kPanel = Hex(0x1d2026), kRaised = Hex(0x262a31),
                     kLine = Hex(0x2f343c), kAccent = Hex(0x4ea1ff), kFaint = Hex(0x3a404a),
                     kTeal = Hex(0x2bb3a3), kOrange = Hex(0xf08a3c);

// Placeholder for a text label until fonts land (queue item 0-5).
void TextBar(bdg::Batch2D& b, float x, float y, float w, bdg::Color c = kFaint) { b.Rect(x, y, w, 8, c, 4); }

void DrawEditorMock(bdg::Batch2D& b, float W, float H) {
  const float menuH = 28, toolH = 36, leftW = 260, rightW = 320, bottomH = 220, gap = 4;
  b.Rect(0, 0, W, menuH, kPanel);                                     // menu bar
  for (int i = 0; i < 6; ++i) TextBar(b, 14 + i * 64.f, 10, 40);
  b.Rect(0, menuH, W, toolH, kGround);                                // toolbar
  for (int i = 0; i < 4; ++i) b.Rect(10 + i * 34.f, menuH + 5, 28, 26, kRaised, 6);
  b.Rect(W * 0.5f - 46, menuH + 5, 28, 26, kAccent, 6);               // play
  b.Rect(W * 0.5f - 14, menuH + 5, 28, 26, kRaised, 6);
  b.Rect(W * 0.5f + 18, menuH + 5, 28, 26, kRaised, 6);

  float top = menuH + toolH, midH = H - top - bottomH - gap;
  // Hierarchy
  b.Rect(gap, top, leftW, midH, kPanel, 6, 1, kLine);
  b.Rect(gap, top, leftW, 30, kRaised, 6);
  TextBar(b, gap + 12, top + 11, 70, kAccent);
  for (int i = 0; i < 9; ++i) {
    float y = top + 44 + i * 26.f;
    if (i == 2) b.Rect(gap + 6, y - 7, leftW - 12, 24, Hex(0x4ea1ff, 0.22f), 4, 1, kAccent);
    TextBar(b, gap + 18 + (i % 3 == 1 ? 14 : 0), y, 90.f + (float)(i * 23 % 60));
  }
  // Scene view
  float sx = gap * 2 + leftW, sw = W - leftW - rightW - gap * 4;
  b.Rect(sx, top, sw, midH, Hex(0x101216), 6, 1, kLine);
  for (float gx = sx + 40; gx < sx + sw; gx += 40) b.Line(gx, top + 1, gx, top + midH - 1, 1, Hex(0xffffff, 0.04f));
  for (float gy = top + 40; gy < top + midH; gy += 40) b.Line(sx + 1, gy, sx + sw - 1, gy, 1, Hex(0xffffff, 0.04f));
  float cx = sx + sw * 0.5f, cy = top + midH * 0.55f;
  b.Rect(cx - 70, cy - 70, 140, 140, kTeal, 12, 3, Hex(0xffffff, 0.85f)); // "selected object"
  b.Line(cx, cy, cx + 110, cy, 3, Hex(0xe5484d));                        // gizmo X
  b.Line(cx, cy, cx, cy - 110, 3, Hex(0x46c463));                        // gizmo Y
  b.Line(cx, cy, cx - 70, cy + 60, 3, kAccent);                          // gizmo Z
  b.Rect(cx + 140, cy - 20, 60, 60, kOrange, 30);                        // sphere-ish
  // Inspector
  float ix = W - rightW - gap;
  b.Rect(ix, top, rightW, midH, kPanel, 6, 1, kLine);
  b.Rect(ix, top, rightW, 30, kRaised, 6);
  TextBar(b, ix + 12, top + 11, 64, kAccent);
  for (int i = 0; i < 8; ++i) {
    float y = top + 46 + i * 32.f;
    TextBar(b, ix + 14, y + 6, 80);
    b.Rect(ix + 120, y, rightW - 136, 22, kGround, 4, 1, kLine);   // field
  }
  // Project / Console
  float by = H - bottomH;
  b.Rect(gap, by, W - gap * 2, bottomH - gap, kPanel, 6, 1, kLine);
  b.Rect(gap, by, W - gap * 2, 30, kRaised, 6);
  TextBar(b, gap + 12, by + 11, 56, kAccent);
  TextBar(b, gap + 90, by + 11, 56);
  for (int i = 0; i < 10; ++i) {
    float tx = gap + 16 + i * 96.f;
    if (tx + 80 > W) break;
    b.Rect(tx, by + 46, 80, 80, i == 3 ? kTeal : kRaised, 8);
    TextBar(b, tx + 8, by + 138, 64);
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
    else DrawEditorMock(e->batch, (float)w, (float)h);
    e->batch.End();
    e->rhi.EndFrame();
    ++frames;
    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last) {
      bdg::Color c = e->rhi.ReadPixel(w / 2, h / 2);
      bdg::LogInfo("frame stats: quads={} flushes={} draws={} | center rgb=({:.2f},{:.2f},{:.2f})",
                   e->batch.QuadsThisFrame(), e->batch.FlushesThisFrame(), e->rhi.DrawCallsThisFrame(), c.r, c.g, c.b);
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
