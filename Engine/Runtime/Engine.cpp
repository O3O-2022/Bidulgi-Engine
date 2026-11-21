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
  if (e->hasFont) e->batch.SetTextAtlas(e->font.Atlas());
  else bdg::LogWarn("no font found: text is not drawn (set BDG_FONT)");
  std::string tokens = FindUp("BDG_TOKENS", {"design-system/project/tokens.json"});
  if (tokens.empty() || !e->theme.Load(tokens)) bdg::LogWarn("design tokens not found: colors show as magenta (set BDG_TOKENS)");

  bdg::Font* font = e->hasFont ? &e->font : nullptr;
  e->editor = std::make_unique<bdg::UIContext>(e->batch, font, e->theme);
  e->editor->SetRoot(bdg::BuildEditorScreen());
  e->editor->SetWindow(&e->window);
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
    e->rhi.BeginFrame(w, h, e->theme.C("win"));
    e->batch.Begin(w, h);
    bdg::UIContext& ui = e->showGallery ? *e->gallery : *e->editor;
    if (e->stress) DrawStress(e->batch, (float)w, (float)h);
    else {
      ui.HandleEvents(e->window.Events());
      ui.Layout((float)w, (float)h);
      ui.Draw();
    }
    e->batch.End();
    e->rhi.EndFrame();
    ++frames;

    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last) {
      bdg::LogInfo("frame stats: quads={} flushes={} draws={} glyphs={} tokens={}", e->batch.QuadsThisFrame(),
                   e->batch.FlushesThisFrame(), e->rhi.DrawCallsThisFrame(), e->hasFont ? e->font.GlyphCount() : 0,
                   e->theme.TokenCount());
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
    if (last) break;
  }
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  bdg::LogInfo("frames: {} in {:.2f}s", frames, sec);
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
