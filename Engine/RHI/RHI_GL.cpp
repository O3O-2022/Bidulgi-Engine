#include "RHI/RHI.h"
#include "Core/Log.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

// Only GL 1.1 entry points (glClear etc.) are used so far, so no loader yet.
// Next step: glad for the full GL 4.5 function set.
namespace bdg {
bool RHI::Init(Window&) {
  const char* ver = (const char*)glGetString(GL_VERSION);
  const char* gpu = (const char*)glGetString(GL_RENDERER);
  if (!ver) { LogError("could not read OpenGL info"); return false; }
  LogInfo("rhi: OpenGL {} | {}", ver, gpu ? gpu : "?");
  return true;
}

void RHI::BeginFrame(int32_t w, int32_t h, Color c) {
  glViewport(0, 0, w, h);
  glClearColor(c.r, c.g, c.b, c.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RHI::EndFrame() {}
}
