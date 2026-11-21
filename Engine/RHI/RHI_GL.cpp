#include "RHI/RHI.h"
#include "Platform/Window.h"
#include "Core/Log.h"
#include <glad/gl.h>

namespace bdg {
namespace {
GLADapiproc LoadProc(const char* name) { return (GLADapiproc)Window::GetGLProcAddress(name); }

void GLAD_API_PTR OnGLDebug(GLenum, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* msg, const void*) {
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
  if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH)
    LogError("gl [{}]: {}", id, msg);
  else
    LogWarn("gl [{}]: {}", id, msg);
}
}

bool RHI::Init(Window&) {
  int version = gladLoadGL(LoadProc);
  if (!version) { LogError("failed to load OpenGL functions (glad)"); return false; }
  if (GLAD_VERSION_MAJOR(version) * 10 + GLAD_VERSION_MINOR(version) < 45) {
    LogError("OpenGL 4.5 required, got {}.{}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
    return false;
  }
  LogInfo("rhi: OpenGL {} | {}", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

  GLint flags = 0;
  glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
  if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(OnGLDebug, nullptr);
    LogInfo("rhi: GL debug output on");
  }
  return true;
}

void RHI::BeginFrame(int32_t w, int32_t h, Color c) {
  glViewport(0, 0, w, h);
  glClearColor(c.r, c.g, c.b, c.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RHI::EndFrame() {}

void RHI::DebugTriggerError() { glEnable(0xFFFF); } // invalid enum on purpose (smoke test)
}
