#include "RHI/RHI.h"
#include "Platform/Window.h"
#include "Core/Log.h"
#include <glad/gl.h>
#include <string>

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

GLuint CompileStage(GLenum stage, const char* src, const char* name) {
  GLuint s = glCreateShader(stage);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    LogError("shader '{}' ({}) compile failed: {}", name, stage == GL_VERTEX_SHADER ? "vs" : "fs", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
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

void RHI::Shutdown() {
  for (uint32_t i = 1; i < pipelines_.size(); ++i) DestroyPipeline({i});
}

void RHI::BeginFrame(int32_t w, int32_t h, Color c) {
  drawCalls_ = 0;
  glViewport(0, 0, w, h);
  glClearColor(c.r, c.g, c.b, c.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RHI::EndFrame() {}

// ---- Buffers (DSA) ----
BufferHandle RHI::CreateBuffer(BufferUsage, const void* data, size_t size, bool dynamic) {
  GLuint b = 0;
  glCreateBuffers(1, &b);
  glNamedBufferStorage(b, (GLsizeiptr)size, data, dynamic ? GL_DYNAMIC_STORAGE_BIT : 0);
  return {b};
}

void RHI::UpdateBuffer(BufferHandle b, const void* data, size_t size, size_t offset) {
  glNamedBufferSubData(b.id, (GLintptr)offset, (GLsizeiptr)size, data);
}

void RHI::DestroyBuffer(BufferHandle b) {
  if (b.Valid()) glDeleteBuffers(1, &b.id);
}

// ---- Pipelines: program + vertex layout (VAO) + blend state ----
PipelineHandle RHI::CreatePipeline(const PipelineDesc& d) {
  GLuint vs = CompileStage(GL_VERTEX_SHADER, d.vertexSource, d.debugName);
  GLuint fs = CompileStage(GL_FRAGMENT_SHADER, d.fragmentSource, d.debugName);
  if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return {}; }
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    LogError("pipeline '{}' link failed: {}", d.debugName, log);
    glDeleteProgram(prog);
    return {};
  }
  glObjectLabel(GL_PROGRAM, prog, -1, d.debugName);

  GLuint vao = 0;
  glCreateVertexArrays(1, &vao);
  for (uint32_t i = 0; i < d.attrCount; ++i) {
    const VertexAttr& a = d.attrs[i];
    glEnableVertexArrayAttrib(vao, a.location);
    glVertexArrayAttribFormat(vao, a.location, (GLint)a.components, GL_FLOAT, GL_FALSE, a.offset);
    glVertexArrayAttribBinding(vao, a.location, 0);
  }
  pipelines_.push_back({prog, vao, d.stride, d.alphaBlend});
  return {(uint32_t)pipelines_.size() - 1};
}

void RHI::DestroyPipeline(PipelineHandle p) {
  if (!p.Valid() || p.id >= pipelines_.size()) return;
  PipelineGL& g = pipelines_[p.id];
  if (g.program) glDeleteProgram(g.program);
  if (g.vao) glDeleteVertexArrays(1, &g.vao);
  g = {};
}

const RHI::PipelineGL* RHI::Bind(PipelineHandle p, BufferHandle vertices) {
  if (!p.Valid() || p.id >= pipelines_.size() || !pipelines_[p.id].program) return nullptr;
  const PipelineGL& g = pipelines_[p.id];
  glUseProgram(g.program);
  glVertexArrayVertexBuffer(g.vao, 0, vertices.id, 0, (GLsizei)g.stride);
  glBindVertexArray(g.vao);
  if (g.alphaBlend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
  else glDisable(GL_BLEND);
  return &g;
}

void RHI::Draw(PipelineHandle p, BufferHandle vertices, uint32_t count, uint32_t first) {
  if (!Bind(p, vertices)) return;
  glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
  ++drawCalls_;
}

void RHI::DrawIndexed(PipelineHandle p, BufferHandle vertices, BufferHandle indices, uint32_t count) {
  const PipelineGL* g = Bind(p, vertices);
  if (!g) return;
  glVertexArrayElementBuffer(g->vao, indices.id);
  glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, nullptr);
  ++drawCalls_;
}

Color RHI::ReadPixel(int32_t x, int32_t y) {
  float px[4] = {};
  glReadBuffer(GL_BACK);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, px);
  return {px[0], px[1], px[2], px[3]};
}

void RHI::DebugTriggerError() { glEnable(0xFFFF); } // invalid enum on purpose
}
