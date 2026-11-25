#include "RHI/RHI.h"
#include "Platform/Window.h"
#include "Core/Log.h"
#include <glad/gl.h>
#include <algorithm>
#include <string>
#include <string_view>

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
  glVersion = (const char*)glGetString(GL_VERSION);
  gpuName = (const char*)glGetString(GL_RENDERER);
  LogInfo("rhi: OpenGL {} | {}", glVersion, gpuName);
  GLint extensions = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
  for (GLint i = 0; i < extensions; ++i)
    if (const char* x = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i)) {
      const std::string_view name(x);
      if (name == "GL_NVX_gpu_memory_info") nvxMemory_ = true;
      if (name == "GL_ARB_texture_filter_anisotropic" || name == "GL_EXT_texture_filter_anisotropic") { // core in GL 4.6
        GLfloat most = 1;
        glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */, &most);
        maxAniso_ = std::max(most, 1.f);
      }
    }
  GLint align = 256;
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
  uboAlign_ = (size_t)std::max(align, 16);

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
  for (uint32_t i = 1; i < targets_.size(); ++i) DestroyRenderTarget({i});
  if (texFbo_) glDeleteFramebuffers(1, &texFbo_);
  texFbo_ = 0;
  if (mainFbo_) {
    glDeleteFramebuffers(1, &mainFbo_);
    glDeleteRenderbuffers(1, &mainColor_);
    glDeleteRenderbuffers(1, &mainDepth_);
  }
  mainFbo_ = mainColor_ = mainDepth_ = 0;
}

void RHI::BeginFrame(int32_t w, int32_t h, Color c) {
  drawCalls_ = 0;
  frameW_ = w;
  frameH_ = h;
  if (offscreenFrame_ && (!mainFbo_ || mainW_ != w || mainH_ != h)) { // the frame's own colour + depth (hidden window)
    if (mainFbo_) {
      glDeleteFramebuffers(1, &mainFbo_);
      glDeleteRenderbuffers(1, &mainColor_);
      glDeleteRenderbuffers(1, &mainDepth_);
    }
    glCreateRenderbuffers(1, &mainColor_);
    glNamedRenderbufferStorage(mainColor_, GL_RGBA8, std::max(w, 1), std::max(h, 1));
    glCreateRenderbuffers(1, &mainDepth_);
    glNamedRenderbufferStorage(mainDepth_, GL_DEPTH24_STENCIL8, std::max(w, 1), std::max(h, 1));
    glCreateFramebuffers(1, &mainFbo_);
    glNamedFramebufferRenderbuffer(mainFbo_, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mainColor_);
    glNamedFramebufferRenderbuffer(mainFbo_, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mainDepth_);
    if (glCheckNamedFramebufferStatus(mainFbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LogError("rhi: offscreen frame is incomplete");
    mainW_ = w;
    mainH_ = h;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, mainFbo_);
  glViewport(0, 0, w, h);
  glDepthMask(GL_TRUE); // glClear honours the depth write mask
  glClearColor(c.r, c.g, c.b, c.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RHI::EndFrame() {}

// ---- Buffers (DSA) ----
BufferHandle RHI::CreateBuffer(BufferUsage, const void* data, size_t size, bool dynamic) {
  GLuint b = 0;
  glCreateBuffers(1, &b);
  glNamedBufferStorage(b, (GLsizeiptr)size, data, dynamic ? GL_DYNAMIC_STORAGE_BIT : 0);
  bufferBytes_[b] = size;
  mem_.buffers += size;
  return {b};
}

void RHI::UpdateBuffer(BufferHandle b, const void* data, size_t size, size_t offset) {
  glNamedBufferSubData(b.id, (GLintptr)offset, (GLsizeiptr)size, data);
}

void RHI::DestroyBuffer(BufferHandle b) {
  if (!b.Valid()) return;
  if (auto it = bufferBytes_.find(b.id); it != bufferBytes_.end()) { mem_.buffers -= it->second; bufferBytes_.erase(it); }
  glDeleteBuffers(1, &b.id);
}

void RHI::BindUniformBuffer(uint32_t slot, BufferHandle b) {
  glBindBufferBase(GL_UNIFORM_BUFFER, slot, b.id);
}

void RHI::BindStorageBuffer(uint32_t slot, BufferHandle b) { glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, b.id); }

void RHI::BindUniformBufferRange(uint32_t slot, BufferHandle b, size_t offset, size_t size) {
  glBindBufferRange(GL_UNIFORM_BUFFER, slot, b.id, (GLintptr)offset, (GLsizeiptr)size);
}

// ---- Textures (DSA) ----
TextureHandle RHI::CreateTexture(const TextureDesc& d, const void* pixels) {
  int32_t levels = 1;
  if (d.mipmaps)
    for (int32_t s = std::max(d.w, d.h); s > 1; s >>= 1) ++levels;
  GLuint t = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &t);
  GLenum internal = d.format == TextureFormat::R8 ? GL_R8 : d.format == TextureFormat::SRGBA8 ? GL_SRGB8_ALPHA8
                  : d.format == TextureFormat::RGBA16F ? GL_RGBA16F : GL_RGBA8;
  glTextureStorage2D(t, levels, internal, d.w, d.h);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, d.nearest ? GL_NEAREST : GL_LINEAR);
  GLint wrap = d.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTextureParameteri(t, GL_TEXTURE_WRAP_S, wrap);
  glTextureParameteri(t, GL_TEXTURE_WRAP_T, wrap);
  if (pixels) {
    UpdateTexture2D({t}, 0, 0, d.w, d.h, d.format, pixels);
    if (levels > 1) glGenerateTextureMipmap(t);
  }
  uint64_t bytes = (uint64_t)d.w * d.h * (d.format == TextureFormat::R8 ? 1 : d.format == TextureFormat::RGBA16F ? 8 : 4);
  bytes = levels > 1 ? bytes * 4 / 3 : bytes;
  textureBytes_[t] = bytes;
  mem_.textures += bytes;
  return {t};
}

TextureHandle RHI::CreateTextureMips(const TextureDesc& d, const std::vector<const void*>& levels, const std::vector<size_t>& sizes) {
  if (levels.empty() || levels.size() != sizes.size()) return {};
  GLenum internal = 0;
  switch (d.format) {
    case TextureFormat::BC7: internal = GL_COMPRESSED_RGBA_BPTC_UNORM; break;
    case TextureFormat::BC7_SRGB: internal = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM; break;
    case TextureFormat::BC5: internal = GL_COMPRESSED_RG_RGTC2; break;
    case TextureFormat::BC4: internal = GL_COMPRESSED_RED_RGTC1; break;
    case TextureFormat::SRGBA8: internal = GL_SRGB8_ALPHA8; break;
    case TextureFormat::RGBA8: internal = GL_RGBA8; break;
    case TextureFormat::R8: internal = GL_R8; break;
  }
  const bool block = d.format == TextureFormat::BC7 || d.format == TextureFormat::BC7_SRGB || d.format == TextureFormat::BC5 ||
                     d.format == TextureFormat::BC4;
  GLuint t = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &t);
  glTextureStorage2D(t, (GLsizei)levels.size(), internal, d.w, d.h);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, levels.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, d.nearest ? GL_NEAREST : GL_LINEAR);
  GLint wrap = d.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTextureParameteri(t, GL_TEXTURE_WRAP_S, wrap);
  glTextureParameteri(t, GL_TEXTURE_WRAP_T, wrap);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  uint64_t bytes = 0;
  for (size_t sz : sizes) bytes += sz;
  textureBytes_[t] = bytes;
  mem_.textures += bytes;
  for (size_t m = 0; m < levels.size(); ++m) {
    GLsizei w = std::max(1, d.w >> m), h = std::max(1, d.h >> m);
    if (block)
      glCompressedTextureSubImage2D(t, (GLint)m, 0, 0, w, h, internal, (GLsizei)sizes[m], levels[m]);
    else
      glTextureSubImage2D(t, (GLint)m, 0, 0, w, h, d.format == TextureFormat::R8 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, levels[m]);
  }
  return {t};
}

TextureHandle RHI::CreateTexture2D(int32_t w, int32_t h, TextureFormat f, const void* pixels) {
  GLuint t = 0;
  glCreateTextures(GL_TEXTURE_2D, 1, &t);
  glTextureStorage2D(t, 1, f == TextureFormat::R8 ? GL_R8 : f == TextureFormat::SRGBA8 ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (pixels) UpdateTexture2D({t}, 0, 0, w, h, f, pixels);
  uint64_t bytes = (uint64_t)w * h * (f == TextureFormat::R8 ? 1 : 4);
  textureBytes_[t] = bytes;
  mem_.textures += bytes;
  return {t};
}

void RHI::UpdateTexture2D(TextureHandle t, int32_t x, int32_t y, int32_t w, int32_t h, TextureFormat f, const void* pixels) {
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTextureSubImage2D(t.id, 0, x, y, w, h, f == TextureFormat::R8 ? GL_RED : GL_RGBA, f == TextureFormat::RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE,
                      pixels);
}

TextureHandle RHI::CreateCubemap(int32_t size, int32_t levels, int32_t layers) {
  GLuint t = 0;
  glCreateTextures(layers > 0 ? GL_TEXTURE_CUBE_MAP_ARRAY : GL_TEXTURE_CUBE_MAP, 1, &t);
  if (layers > 0) glTextureStorage3D(t, levels, GL_RGBA16F, size, size, 6 * layers);
  else glTextureStorage2D(t, levels, GL_RGBA16F, size, size);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTextureParameteri(t, w, GL_CLAMP_TO_EDGE);
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  uint64_t bytes = (uint64_t)size * size * 8 * 6 * std::max(layers, 1) * (levels > 1 ? 4 : 3) / 3;
  textureBytes_[t] = bytes;
  mem_.textures += bytes;
  texInfo_[t] = {size, layers, true};
  return {t};
}

void RHI::BeginTextureTarget(TextureHandle t, int32_t level, int32_t layer) {
  auto it = texInfo_.find(t.id);
  if (!t.Valid() || it == texInfo_.end()) return;
  if (!texFbo_) glCreateFramebuffers(1, &texFbo_);
  glNamedFramebufferTextureLayer(texFbo_, GL_COLOR_ATTACHMENT0, t.id, level, layer);
  glBindFramebuffer(GL_FRAMEBUFFER, texFbo_);
  int32_t s = std::max(it->second.size >> level, 1);
  glViewport(0, 0, s, s);
}

void RHI::EndTextureTarget() {
  glBindFramebuffer(GL_FRAMEBUFFER, mainFbo_);
  glViewport(0, 0, frameW_, frameH_);
}

void RHI::CopyToCubeFace(TextureHandle src, int32_t size, TextureHandle cube, int32_t layer, int32_t level) {
  auto it = texInfo_.find(cube.id);
  if (it == texInfo_.end()) return;
  GLenum target = it->second.layers > 0 ? GL_TEXTURE_CUBE_MAP_ARRAY : GL_TEXTURE_CUBE_MAP;
  glCopyImageSubData(src.id, GL_TEXTURE_2D, 0, 0, 0, 0, cube.id, target, level, 0, 0, layer, size, size, 1);
}

void RHI::GenerateMips(TextureHandle t) { if (t.Valid()) glGenerateTextureMipmap(t.id); }

bool RHI::ReadTextureLevel(TextureHandle t, int32_t level, std::vector<float>& rgba) {
  auto it = texInfo_.find(t.id);
  if (!t.Valid() || it == texInfo_.end()) return false;
  int32_t s = std::max(it->second.size >> level, 1), faces = 6 * std::max(it->second.layers, 1);
  rgba.resize((size_t)s * s * faces * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glGetTextureImage(t.id, level, GL_RGBA, GL_FLOAT, (GLsizei)(rgba.size() * sizeof(float)), rgba.data());
  return true;
}

void RHI::UploadCubeFaces(TextureHandle t, int32_t level, int32_t firstFace, int32_t faces, const float* rgba) {
  auto it = texInfo_.find(t.id);
  if (!t.Valid() || it == texInfo_.end() || faces <= 0) return;
  int32_t s = std::max(it->second.size >> level, 1);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTextureSubImage3D(t.id, level, 0, 0, firstFace, s, s, faces, GL_RGBA, GL_FLOAT, rgba);
}

void RHI::SetTextureAnisotropy(TextureHandle t, float level) {
  if (!t.Valid()) return;
  level = std::clamp(level, 1.f, maxAniso_);
  auto it = texAniso_.find(t.id);
  if (it != texAniso_.end() ? it->second == level : level == 1.f) return;
  if (maxAniso_ > 1) glTextureParameterf(t.id, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, level);
  texAniso_[t.id] = level;
}

float RHI::TextureAnisotropy(TextureHandle t) const {
  if (!t.Valid() || maxAniso_ <= 1) return 1;
  GLfloat v = 1;
  glGetTextureParameterfv(t.id, 0x84FE, &v);
  return v;
}

TextureHandle RHI::CreateTexture3D(int32_t size, const uint8_t* rgba) {
  GLuint t = 0;
  glCreateTextures(GL_TEXTURE_3D, 1, &t);
  glTextureStorage3D(t, 1, GL_RGBA8, size, size, size);
  for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTextureParameteri(t, w, GL_CLAMP_TO_EDGE);
  glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (rgba) glTextureSubImage3D(t, 0, 0, 0, 0, size, size, size, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  const uint64_t bytes = (uint64_t)size * size * size * 4;
  textureBytes_[t] = bytes;
  mem_.textures += bytes;
  return {t};
}

void RHI::DestroyTexture(TextureHandle t) {
  if (!t.Valid()) return;
  if (auto it = textureBytes_.find(t.id); it != textureBytes_.end()) { mem_.textures -= it->second; textureBytes_.erase(it); }
  texInfo_.erase(t.id);
  texAniso_.erase(t.id);
  glDeleteTextures(1, &t.id);
}

void RHI::BindTexture(uint32_t slot, TextureHandle t) { glBindTextureUnit(slot, t.id); }

void RHI::ReadPixels(int32_t w, int32_t h, std::vector<uint8_t>& rgba) {
  rgba.resize((size_t)w * h * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  if (mainFbo_) { // offscreen frame
    glNamedFramebufferReadBuffer(mainFbo_, GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, mainFbo_);
  } else {
    glReadBuffer(GL_BACK);
  }
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

// ---- Render targets ----
RenderTargetHandle RHI::CreateRenderTarget(int32_t w, int32_t h, int32_t samples) { return CreateRenderTargetEx(w, h, samples, false, true); }

void RHI::CopyDepth(RenderTargetHandle from, RenderTargetHandle to) {
  if (!from.Valid() || !to.Valid() || from.id >= targets_.size() || to.id >= targets_.size()) return;
  const TargetGL& s = targets_[from.id];
  const TargetGL& d = targets_[to.id];
  if (!s.depth || !d.depth || s.w != d.w || s.h != d.h) return;
  glBlitNamedFramebuffer(s.samples > 1 ? s.msFbo : s.fbo, d.samples > 1 ? d.msFbo : d.fbo, 0, 0, s.w, s.h, 0, 0, d.w, d.h,
                         GL_DEPTH_BUFFER_BIT, GL_NEAREST);
}

RenderTargetHandle RHI::CreateRenderTargetEx(int32_t w, int32_t h, int32_t samples, bool hdr, bool withDepth) {
  const GLenum colorFormat = hdr ? GL_RGBA16F : GL_RGBA8;
  TargetGL t;
  t.w = std::max(w, 1);
  t.h = std::max(h, 1);
  t.samples = std::max(samples, 1);
  glCreateTextures(GL_TEXTURE_2D, 1, &t.color);
  glTextureStorage2D(t.color, 1, colorFormat, t.w, t.h);
  glTextureParameteri(t.color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(t.color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(t.color, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(t.color, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glCreateFramebuffers(1, &t.fbo);
  glNamedFramebufferTexture(t.fbo, GL_COLOR_ATTACHMENT0, t.color, 0);
  if (withDepth) glCreateRenderbuffers(1, &t.depth);
  if (t.samples > 1) { // draw into multisampled buffers, resolve into the texture
    glCreateRenderbuffers(1, &t.msColor);
    glNamedRenderbufferStorageMultisample(t.msColor, t.samples, colorFormat, t.w, t.h);
    glCreateFramebuffers(1, &t.msFbo);
    glNamedFramebufferRenderbuffer(t.msFbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, t.msColor);
    if (withDepth) {
      glNamedRenderbufferStorageMultisample(t.depth, t.samples, GL_DEPTH_COMPONENT24, t.w, t.h);
      glNamedFramebufferRenderbuffer(t.msFbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    }
  } else if (withDepth) {
    glNamedRenderbufferStorage(t.depth, GL_DEPTH_COMPONENT24, t.w, t.h);
    glNamedFramebufferRenderbuffer(t.fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
  }
  t.bytes = (uint64_t)t.w * t.h * ((hdr ? 8 : 4) * (t.samples > 1 ? 1 + t.samples : 1) + (withDepth ? 4 * t.samples : 0));
  mem_.targets += t.bytes;
  GLuint drawFbo = t.samples > 1 ? t.msFbo : t.fbo;
  if (glCheckNamedFramebufferStatus(drawFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
      glCheckNamedFramebufferStatus(t.fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    LogError("rhi: render target {}x{} (x{}) is incomplete", t.w, t.h, t.samples);
  }
  for (uint32_t i = 1; i < targets_.size(); ++i)
    if (!targets_[i].fbo) { targets_[i] = t; return {i}; }
  targets_.push_back(t);
  return {(uint32_t)targets_.size() - 1};
}

RenderTargetHandle RHI::CreateDepthTarget(int32_t size) { return CreateDepthTarget(size, size); }

void RHI::SetViewport(int32_t x, int32_t y, int32_t w, int32_t h) { glViewport(x, y, w, h); }

RenderTargetHandle RHI::CreateDepthTarget(int32_t w, int32_t h) {
  TargetGL t;
  t.w = std::max(w, 1);
  t.h = std::max(h, 1);
  glCreateTextures(GL_TEXTURE_2D, 1, &t.depthTex);
  glTextureStorage2D(t.depthTex, 1, GL_DEPTH_COMPONENT32F, t.w, t.h);
  glTextureParameteri(t.depthTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // linear + compare = 2x2 PCF per tap
  glTextureParameteri(t.depthTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(t.depthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTextureParameteri(t.depthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  const float border[4] = {1, 1, 1, 1}; // outside the map: nothing in front, lit
  glTextureParameterfv(t.depthTex, GL_TEXTURE_BORDER_COLOR, border);
  glTextureParameteri(t.depthTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTextureParameteri(t.depthTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glCreateFramebuffers(1, &t.fbo);
  glNamedFramebufferTexture(t.fbo, GL_DEPTH_ATTACHMENT, t.depthTex, 0);
  glNamedFramebufferDrawBuffer(t.fbo, GL_NONE);
  glNamedFramebufferReadBuffer(t.fbo, GL_NONE);
  if (glCheckNamedFramebufferStatus(t.fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LogError("rhi: depth target {} is incomplete", t.w);
  t.bytes = (uint64_t)t.w * t.h * 4;
  mem_.targets += t.bytes;
  for (uint32_t i = 1; i < targets_.size(); ++i)
    if (!targets_[i].fbo) { targets_[i] = t; return {i}; }
  targets_.push_back(t);
  return {(uint32_t)targets_.size() - 1};
}

TextureHandle RHI::RenderTargetDepth(RenderTargetHandle rt) const {
  return rt.Valid() && rt.id < targets_.size() ? TextureHandle{targets_[rt.id].depthTex} : TextureHandle{};
}

void RHI::DestroyRenderTarget(RenderTargetHandle rt) {
  if (!rt.Valid() || rt.id >= targets_.size()) return;
  TargetGL& t = targets_[rt.id];
  mem_.targets -= std::min(mem_.targets, t.bytes);
  if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
  if (t.msFbo) glDeleteFramebuffers(1, &t.msFbo);
  if (t.color) glDeleteTextures(1, &t.color);
  if (t.depth) glDeleteRenderbuffers(1, &t.depth);
  if (t.msColor) glDeleteRenderbuffers(1, &t.msColor);
  if (t.depthTex) glDeleteTextures(1, &t.depthTex);
  t = {};
}

TextureHandle RHI::RenderTargetColor(RenderTargetHandle rt) const {
  return rt.Valid() && rt.id < targets_.size() ? TextureHandle{targets_[rt.id].color} : TextureHandle{};
}

void RHI::BeginRenderTarget(RenderTargetHandle rt, Color c) {
  if (!rt.Valid() || rt.id >= targets_.size() || !targets_[rt.id].fbo) return;
  const TargetGL& t = targets_[rt.id];
  currentTarget_ = rt.id;
  glBindFramebuffer(GL_FRAMEBUFFER, t.samples > 1 ? t.msFbo : t.fbo);
  glViewport(0, 0, t.w, t.h);
  glDepthMask(GL_TRUE);
  if (t.depthTex && !t.color) { glClear(GL_DEPTH_BUFFER_BIT); return; } // depth only
  glClearColor(c.r, c.g, c.b, c.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void RHI::BeginRenderTargetKeep(RenderTargetHandle rt) {
  if (!rt.Valid() || rt.id >= targets_.size() || !targets_[rt.id].fbo) return;
  const TargetGL& t = targets_[rt.id];
  currentTarget_ = rt.id;
  glBindFramebuffer(GL_FRAMEBUFFER, t.samples > 1 ? t.msFbo : t.fbo);
  glViewport(0, 0, t.w, t.h);
}

void RHI::EndRenderTarget() {
  if (currentTarget_ && currentTarget_ < targets_.size()) {
    const TargetGL& t = targets_[currentTarget_];
    if (t.samples > 1) glBlitNamedFramebuffer(t.msFbo, t.fbo, 0, 0, t.w, t.h, 0, 0, t.w, t.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  currentTarget_ = 0;
  glBindFramebuffer(GL_FRAMEBUFFER, mainFbo_);
  glViewport(0, 0, frameW_, frameH_);
}

void RHI::ReadRenderTarget(RenderTargetHandle rt, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) {
  w = h = 0;
  rgba.clear();
  if (!rt.Valid() || rt.id >= targets_.size() || !targets_[rt.id].color) return;
  const TargetGL& t = targets_[rt.id];
  w = t.w;
  h = t.h;
  rgba.resize((size_t)w * h * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glGetTextureImage(t.color, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)rgba.size(), rgba.data());
}

// ---- Pipelines: program + vertex layout (VAO) + blend / depth / cull state ----
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
    glVertexArrayAttribBinding(vao, a.location, a.binding);
  }
  if (d.instanceStride) glVertexArrayBindingDivisor(vao, 1, 1); // binding 1 advances once per instance
  pipelines_.push_back({prog, vao, d.stride, d.alphaBlend, d.additive, d.wireframe, d.depthTest, d.depthWrite, d.cull, d.depthBiasConstant,
                        d.depthBiasSlope, d.instanceStride});
  return {(uint32_t)pipelines_.size() - 1};
}

PipelineHandle RHI::CreateComputePipeline(const char* source, const char* debugName) {
  GLuint cs = CompileStage(GL_COMPUTE_SHADER, source, debugName);
  if (!cs) return {};
  GLuint prog = glCreateProgram();
  glAttachShader(prog, cs);
  glLinkProgram(prog);
  glDeleteShader(cs);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    LogError("compute pipeline '{}' link failed: {}", debugName, log);
    glDeleteProgram(prog);
    return {};
  }
  glObjectLabel(GL_PROGRAM, prog, -1, debugName);
  PipelineGL g;
  g.program = prog;
  pipelines_.push_back(g);
  return {(uint32_t)pipelines_.size() - 1};
}

void RHI::Dispatch(PipelineHandle p, uint32_t x, uint32_t y, uint32_t z) {
  if (!p.Valid() || p.id >= pipelines_.size() || !pipelines_[p.id].program || !x || !y || !z) return;
  glUseProgram(pipelines_[p.id].program);
  glDispatchCompute(x, y, z);
}

void RHI::ComputeBarrier() { glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT); }

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
  if (g.additive) { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); }
  else if (g.alphaBlend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
  else glDisable(GL_BLEND);
  if (g.depthTest) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(g.depthWrite ? GL_TRUE : GL_FALSE); }
  else { glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); }
  glPolygonMode(GL_FRONT_AND_BACK, g.wireframe ? GL_LINE : GL_FILL);
  if (g.biasC != 0 || g.biasS != 0) {
    glEnable(g.wireframe ? GL_POLYGON_OFFSET_LINE : GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(g.biasS, g.biasC);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
  }
  if (g.cull == CullMode::Back) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); }
  else glDisable(GL_CULL_FACE);
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

uint32_t RHI::CreateQuery() {
  GLuint q = 0;
  glCreateQueries(GL_TIMESTAMP, 1, &q);
  return q;
}

void RHI::DestroyQuery(uint32_t q) {
  if (q) glDeleteQueries(1, &q);
}

void RHI::Timestamp(uint32_t q) { glQueryCounter(q, GL_TIMESTAMP); }

bool RHI::QueryNs(uint32_t q, uint64_t& ns) {
  GLint ready = 0;
  glGetQueryObjectiv(q, GL_QUERY_RESULT_AVAILABLE, &ready);
  if (!ready) return false;
  GLuint64 v = 0;
  glGetQueryObjectui64v(q, GL_QUERY_RESULT, &v);
  ns = v;
  return true;
}

void RHI::DrawIndexedInstanced(PipelineHandle p, BufferHandle vertices, BufferHandle indices, uint32_t count, uint32_t instances,
                               uint32_t firstInstance, BufferHandle instanceData) {
  const PipelineGL* g = Bind(p, vertices);
  if (!g || instances == 0) return;
  if (g->instanceStride && instanceData.Valid()) glVertexArrayVertexBuffer(g->vao, 1, instanceData.id, 0, (GLsizei)g->instanceStride);
  glVertexArrayElementBuffer(g->vao, indices.id);
  glDrawElementsInstancedBaseInstance(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, nullptr, (GLsizei)instances, firstInstance);
  ++drawCalls_;
}

bool RHI::VideoMemoryKB(int64_t& total, int64_t& available) const {
  if (!nvxMemory_) return false;
  GLint t = 0, a = 0;
  glGetIntegerv(0x9047, &t); // GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
  glGetIntegerv(0x9049, &a); // GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
  total = t;
  available = a;
  return t > 0;
}

Color RHI::ReadPixel(int32_t x, int32_t y) {
  float px[4] = {};
  glReadBuffer(GL_BACK);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, px);
  return {px[0], px[1], px[2], px[3]};
}

void RHI::DebugTriggerError() { glEnable(0xFFFF); } // invalid enum on purpose
}
