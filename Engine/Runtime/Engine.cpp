// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include <chrono>
#include <cstdlib>
#include <new>

namespace {
// M0 test content: one colored triangle drawn through the RHI.
const char* kTriangleVS = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* kTriangleFS = R"(#version 450 core
in vec3 vColor;
out vec4 oColor;
void main() { oColor = vec4(vColor, 1.0); }
)";
struct Vertex { float x, y, r, g, b; };
const Vertex kTriangle[3] = {
  {-0.6f, -0.5f, 0.31f, 0.63f, 1.00f}, // accent blue
  { 0.6f, -0.5f, 0.17f, 0.70f, 0.64f}, // graph teal
  { 0.0f,  0.6f, 0.94f, 0.54f, 0.24f}, // anim orange
};
}

struct bdg_engine {
  bdg::Window window;
  bdg::RHI rhi;
  bdg::BufferHandle triangleVB;
  bdg::PipelineHandle trianglePipeline;
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
  if (std::getenv("BDG_GL_ERROR_TEST")) e->rhi.DebugTriggerError(); // verifies GL debug output reaches the log

  bdg::PipelineDesc pd;
  pd.vertexSource = kTriangleVS;
  pd.fragmentSource = kTriangleFS;
  pd.attrs[0] = {0, 2, 0};
  pd.attrs[1] = {1, 3, sizeof(float) * 2};
  pd.attrCount = 2;
  pd.stride = sizeof(Vertex);
  pd.debugName = "m0_triangle";
  e->trianglePipeline = e->rhi.CreatePipeline(pd);
  e->triangleVB = e->rhi.CreateBuffer(bdg::BufferUsage::Vertex, kTriangle, sizeof(kTriangle));
  if (!e->trianglePipeline.Valid()) { e->rhi.Shutdown(); e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }

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
    e->rhi.Draw(e->trianglePipeline, e->triangleVB, 3);
    e->rhi.EndFrame();
    ++frames;
    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last) {
      // Smoke-test evidence: center pixel should be triangle-colored, corner pixel the background.
      bdg::Color c = e->rhi.ReadPixel(w / 2, h / 2), k = e->rhi.ReadPixel(2, 2);
      bdg::LogInfo("pixel center rgb=({:.2f},{:.2f},{:.2f}) corner rgb=({:.2f},{:.2f},{:.2f}) draws={}",
                   c.r, c.g, c.b, k.r, k.g, k.b, e->rhi.DrawCallsThisFrame());
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
  e->rhi.DestroyBuffer(e->triangleVB);
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
