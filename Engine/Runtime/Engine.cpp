// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Render2D/Batch2D.h"
#include "Runtime/Screens.h"
#include "Text/Font.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#undef DrawText // windows.h macro would rename bdg::Font::DrawText in this file
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

void DrawStress(bdg::Batch2D& b, float W, float H) { // queue 0-4 exit check: 10k rects in one draw call
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
  std::unique_ptr<bdg::UIContext> editor, gallery;
  bool showGallery = false, selfTest = false, stress = false, hasFont = false;
  std::chrono::steady_clock::time_point createdAt, lastFrame, fpsWindow;
  int clicks = 0;
  int32_t maxFrames = 0;
};

namespace {
// Scripted input for automated checks (BDG_UI_SELFTEST=1 with BDG_SCREEN=gallery).
void InjectSelfTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::UIContext& ui = *e->gallery;
  auto key = [&](bdg::Key k, bool ctrl = false) { bdg::InputEvent ev{T::KeyDown}; ev.key = k; ev.ctrl = ctrl; e->window.InjectEvent(ev); };
  auto text = [&](T type, const char* s) { bdg::InputEvent ev{type}; ev.text = s; e->window.InjectEvent(ev); };
  auto click = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  if (frame == 2) { // click Apply, Tab twice, Enter
    bdg::Widget* btn = ui.Find("btn.apply");
    click(btn->x + btn->w * 0.5f, btn->y + btn->h * 0.5f);
    key(bdg::Key::Tab); key(bdg::Key::Tab); key(bdg::Key::Enter);
  }
  if (frame == 4) { // text input: select all, type Korean, backspace, undo, IME composition
    bdg::Widget* f = ui.Find("field.speed");
    click(f->x + f->w - 20, f->y + f->h * 0.5f);
    key(bdg::Key::A, true);
    text(T::Text, "한글");
    text(T::Text, " 입력");
    key(bdg::Key::Backspace);
    key(bdg::Key::Z, true);
    text(T::TextEditing, "ㅋ");
  }
}
}

extern "C" {

bdg_result bdg_engine_create(const bdg_engine_desc* desc, bdg_engine** out) {
  if (!desc || !out) return BDG_ERR_INVALID_ARG;
  *out = nullptr;
  bdg::LogInfo("Bidulgi Engine {}", bdg_version_string());
  auto* e = new (std::nothrow) bdg_engine();
  if (!e) return BDG_ERR_PLATFORM;
  e->createdAt = e->lastFrame = e->fpsWindow = std::chrono::steady_clock::now();
  e->maxFrames = desc->max_frames;
  e->stress = std::getenv("BDG_BATCH_STRESS") != nullptr;
  e->selfTest = std::getenv("BDG_UI_SELFTEST") != nullptr;
  const char* screen = std::getenv("BDG_SCREEN");
  e->showGallery = screen && std::string(screen) == "gallery";
  if (!e->window.Create(desc->title ? desc->title : "Bidulgi Engine", desc->width, desc->height)) { delete e; return BDG_ERR_PLATFORM; }
  if (!e->rhi.Init(e->window)) { e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  if (std::getenv("BDG_GL_ERROR_TEST")) e->rhi.DebugTriggerError(); // verifies GL debug output reaches the log
  if (!e->batch.Init(e->rhi)) { e->rhi.Shutdown(); e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  std::string fontPath = FindFont();
  e->hasFont = !fontPath.empty() && e->font.Load(e->rhi, fontPath.c_str());
  if (e->hasFont) {
    e->batch.SetTextAtlas(e->font.Atlas());
    for (const char* mono : {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"}) // code font (system, not redistributed)
      if (std::filesystem::exists(mono) && e->font.LoadMono(mono)) break;
  }
  else bdg::LogWarn("no font found: text is not drawn (set BDG_FONT)");
  std::string tokens = FindUp("BDG_TOKENS", {"design-system/project/tokens.json"});
  if (tokens.empty() || !e->theme.Load(tokens)) bdg::LogWarn("design tokens not found: colors show as magenta (set BDG_TOKENS)");

  bdg::Font* font = e->hasFont ? &e->font : nullptr;
  e->editor = std::make_unique<bdg::UIContext>(e->batch, font, e->theme);
  e->editor->SetRoot(bdg::BuildEditorScreen());
  e->editor->SetWindow(&e->window);
  if (const char* tab = std::getenv("BDG_DOCK_TAB")) // 0 Project, 1 Console, 2 Build Log, 3 Profiler (screenshots/tests)
    if (bdg::Widget* dock = e->editor->Find("panel.bottom")) dock->selectedIndex = std::atoi(tab);
  e->gallery = std::make_unique<bdg::UIContext>(e->batch, font, e->theme);
  e->gallery->SetRoot(bdg::BuildGalleryScreen(&e->clicks));
  e->gallery->SetWindow(&e->window);
  *out = e;
  return BDG_OK;
}

bdg_result bdg_engine_run(bdg_engine* e) {
  if (!e) return BDG_ERR_INVALID_ARG;
  int32_t frames = 0;
  auto t0 = std::chrono::steady_clock::now();
  while (e->window.PumpEvents()) {
    if (frames % 30 == 0) e->theme.ReloadIfChanged(); // live token edits
    for (const bdg::InputEvent& ev : e->window.Events())
      if (ev.type == bdg::InputEvent::Type::KeyDown && ev.key == bdg::Key::F1) e->showGallery = !e->showGallery;
    if (e->selfTest && e->showGallery) InjectSelfTest(e, frames);

    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    // UI works in logical units: drawable pixels / display scale (150% Windows scaling -> 1.5).
    float scale = e->window.GetDisplayScale();
    if (const char* s = std::getenv("BDG_UI_SCALE")) scale = std::strtof(s, nullptr);
    if (scale < 0.5f) scale = 1.f;
    float lw = w / scale, lh = h / scale;
    e->rhi.BeginFrame(w, h, e->theme.C("win"));
    e->batch.Begin((int32_t)lw, (int32_t)lh);
    bdg::UIContext& ui = e->showGallery ? *e->gallery : *e->editor;
    if (e->stress) DrawStress(e->batch, lw, lh);
    else {
      std::vector<bdg::InputEvent> events = e->window.Events();
      for (bdg::InputEvent& ev : events) { ev.x /= scale; ev.y /= scale; }
      ui.SetScale(scale);
      ui.HandleEvents(events);
      ui.Layout(lw, lh);
      ui.Draw();
    }
    e->batch.End();
    e->rhi.EndFrame();
    ++frames;

    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last) {
      bdg::LogInfo("frame stats: quads={} flushes={} draws={} glyphs={} tokens={} ui_scale={:.2f} display_scale={:.2f} drawable={}x{}",
                   e->batch.QuadsThisFrame(), e->batch.FlushesThisFrame(), e->rhi.DrawCallsThisFrame(),
                   e->hasFont ? e->font.GlyphCount() : 0, e->theme.TokenCount(), scale, e->window.GetDisplayScale(), w, h);
      if (e->showGallery) {
        bdg::Widget* f = e->gallery->Focused();
        bdg::Widget* speed = e->gallery->Find("field.speed");
        bdg::LogInfo("ui: clicks(Apply)={} focused={} tabs.view={} field.speed=\"{}\" composing=\"{}\" cursor={}",
                     e->clicks, f ? f->id : "-", e->gallery->Find("tabs.view")->selectedIndex, speed->text2,
                     speed->composing, speed->cursor);
      }
      if (const char* dump = std::getenv("BDG_DUMP_WIDGETS"))
        bdg::LogInfo("widget dump {}: {}", ui.DumpJSONL(dump) ? "saved" : "FAILED", dump);
      if (const char* shot = std::getenv("BDG_SCREENSHOT")) {
        std::vector<uint8_t> px;
        e->rhi.ReadPixels(w, h, px);
        bool ok = bdg::WritePNG(shot, w, h, px.data(), true);
        bdg::LogInfo("screenshot {}: {}", ok ? "saved" : "FAILED", shot);
      }
    }
    e->window.Present();
    { // live status bar + profiler feed
      auto now = std::chrono::steady_clock::now();
      float ms = std::chrono::duration<float, std::milli>(now - e->lastFrame).count();
      e->lastFrame = now;
      if (frames > 1) bdg::PushFrameTime(ms);
      if (frames % 30 == 0) {
        float sec = std::chrono::duration<float>(now - e->fpsWindow).count();
        e->fpsWindow = now;
        PROCESS_MEMORY_COUNTERS pmc{};
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
        if (bdg::Widget* perf = e->editor->Find("status.perf"))
          perf->text = std::format("{:.0f} fps  ·  {:.2f} ms  ·  RAM {:.0f} MB", 30.f / std::max(sec, 1e-4f), sec * 1000.f / 30.f,
                                   pmc.WorkingSetSize / (1024.0 * 1024.0));
      }
    }
    if (frames == 1) { // startup = engine create -> first frame on screen (target: < 1000 ms)
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - e->createdAt).count();
      bdg::LogInfo("perf: startup_to_first_frame_ms={:.0f} (target < 1000)", ms);
    }
    if (last) break;
  }
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  bdg::LogInfo("frames: {} in {:.2f}s", frames, sec);
  if (frames > 1) bdg::LogInfo("perf: avg_frame_ms={:.3f} (target < 16.7 at 60 fps; editor idle UI)", sec * 1000.0 / frames);
  return BDG_OK;
}

void bdg_engine_destroy(bdg_engine* e) {
  if (!e) return;
  e->editor.reset();
  e->gallery.reset();
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
