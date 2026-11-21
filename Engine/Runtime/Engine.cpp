// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include <chrono>
#include <new>

struct bdg_engine {
  bdg::Window window;
  bdg::RHI rhi;
  int32_t maxFrames = 0;
};

extern "C" {

bdg_result bdg_engine_create(const bdg_engine_desc* desc, bdg_engine** out) {
  if (!desc || !out) return BDG_ERR_INVALID_ARG;
  *out = nullptr;
  bdg::LogInfo("Bidulgi Engine {}", bdg_version_string());
  auto* e = new (std::nothrow) bdg_engine();
  if (!e) return BDG_ERR_PLATFORM;
  e->maxFrames = desc->max_frames;
  if (!e->window.Create(desc->title ? desc->title : "Bidulgi Engine", desc->width, desc->height)) { delete e; return BDG_ERR_PLATFORM; }
  if (!e->rhi.Init(e->window)) { e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  *out = e;
  return BDG_OK;
}

bdg_result bdg_engine_run(bdg_engine* e) {
  if (!e) return BDG_ERR_INVALID_ARG;
  const bdg::Color ground{0x16 / 255.f, 0x18 / 255.f, 0x1d / 255.f, 1.f}; // design token --ground
  int32_t frames = 0;
  auto t0 = std::chrono::steady_clock::now();
  while (e->window.PumpEvents()) {
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    e->rhi.BeginFrame(w, h, ground);
    e->rhi.EndFrame();
    e->window.Present();
    ++frames;
    if (e->maxFrames > 0 && frames >= e->maxFrames) break;
  }
  double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  bdg::LogInfo("frames: {} in {:.2f}s", frames, sec);
  return BDG_OK;
}

void bdg_engine_destroy(bdg_engine* e) {
  if (!e) return;
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
