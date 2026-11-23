#pragma once
#include "Core/Math.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bdg {
class Window;

// RHI: engine code never calls a graphics API directly. First backend: OpenGL 4.5 (DSA).

struct BufferHandle   { uint32_t id = 0; bool Valid() const { return id != 0; } };
struct PipelineHandle { uint32_t id = 0; bool Valid() const { return id != 0; } };

struct TextureHandle  { uint32_t id = 0; bool Valid() const { return id != 0; } };
struct RenderTargetHandle { uint32_t id = 0; bool Valid() const { return id != 0; } };

enum class BufferUsage { Vertex, Index, Uniform };
// SRGBA8: 8-bit sRGB-encoded colour, decoded to linear when sampled (albedo textures, colour pictures).
enum class TextureFormat { R8, RGBA8, SRGBA8 };
enum class CullMode { None, Back };

struct TextureDesc {
  int32_t w = 1, h = 1;
  TextureFormat format = TextureFormat::RGBA8;
  bool mipmaps = false; // full chain generated from level 0 (trilinear sampling)
  bool repeat = false;  // wrap UVs (model textures); otherwise clamp to edge
  bool nearest = false; // nearest magnification (minification stays filtered)
};

struct VertexAttr {
  uint32_t location;
  uint32_t components; // float components (1-4)
  uint32_t offset;     // bytes
};

struct PipelineDesc {
  const char* vertexSource = nullptr;   // GLSL 450 for now; later one shader language cross-compiled
  const char* fragmentSource = nullptr;
  VertexAttr  attrs[8] = {};
  uint32_t    attrCount = 0;
  uint32_t    stride = 0;               // bytes per vertex
  bool        alphaBlend = false;
  bool        depthTest = false;        // less-or-equal test (3D); 2D pipelines leave it off
  bool        depthWrite = true;        // with depthTest: false for blended surfaces
  CullMode    cull = CullMode::None;    // front faces wind counter-clockwise (Core/Space.h)
  const char* debugName = "pipeline";
};

class RHI {
public:
  bool Init(Window& window);
  void Shutdown();
  void BeginFrame(int32_t width, int32_t height, Color clear);
  void EndFrame();

  BufferHandle CreateBuffer(BufferUsage usage, const void* data, size_t size, bool dynamic = false);
  void         UpdateBuffer(BufferHandle buffer, const void* data, size_t size, size_t offset = 0);
  void         DestroyBuffer(BufferHandle buffer);
  void         BindUniformBuffer(uint32_t slot, BufferHandle buffer); // GLSL: layout(std140, binding = slot)
  void         BindUniformBufferRange(uint32_t slot, BufferHandle buffer, size_t offset, size_t size);
  size_t       UniformAlignment() const { return uboAlign_; } // offsets for BindUniformBufferRange are multiples

  TextureHandle CreateTexture2D(int32_t w, int32_t h, TextureFormat format, const void* pixels = nullptr);
  TextureHandle CreateTexture(const TextureDesc& desc, const void* pixels); // pixels: level 0, top row first
  void          UpdateTexture2D(TextureHandle t, int32_t x, int32_t y, int32_t w, int32_t h, TextureFormat format, const void* pixels);
  void          DestroyTexture(TextureHandle t);
  void          BindTexture(uint32_t slot, TextureHandle t); // GLSL: layout(binding = slot) uniform sampler2D

  // Offscreen colour (RGBA8, already display-encoded) + depth. samples > 1 renders with MSAA and resolves into
  // the colour texture at EndRenderTarget. The texture is bottom-up (OpenGL): draw it with v flipped.
  RenderTargetHandle CreateRenderTarget(int32_t w, int32_t h, int32_t samples = 1);
  void          DestroyRenderTarget(RenderTargetHandle rt);
  TextureHandle RenderTargetColor(RenderTargetHandle rt) const;
  void          BeginRenderTarget(RenderTargetHandle rt, Color clear); // viewport = the whole target
  void          EndRenderTarget();                                     // resolve, back to the window
  void          ReadRenderTarget(RenderTargetHandle rt, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba); // bottom-up rows

  PipelineHandle CreatePipeline(const PipelineDesc& desc);
  void           DestroyPipeline(PipelineHandle pipeline);

  void Draw(PipelineHandle pipeline, BufferHandle vertices, uint32_t vertexCount, uint32_t firstVertex = 0);
  void DrawIndexed(PipelineHandle pipeline, BufferHandle vertices, BufferHandle indices, uint32_t indexCount);

  Color ReadPixel(int32_t x, int32_t y); // test hook (slow): reads the back buffer
  void  ReadPixels(int32_t w, int32_t h, std::vector<uint8_t>& rgba); // whole back buffer, bottom-up rows
  void  DebugTriggerError();             // test hook: issues an invalid GL call to verify debug output
  uint32_t DrawCallsThisFrame() const { return drawCalls_; }
  std::string glVersion, gpuName; // filled by Init (bug reports, logs)

private:
  struct PipelineGL { uint32_t program = 0, vao = 0, stride = 0; bool alphaBlend = false, depthTest = false, depthWrite = true; CullMode cull = CullMode::None; };
  struct TargetGL { uint32_t fbo = 0, color = 0, depth = 0, msFbo = 0, msColor = 0; int32_t w = 0, h = 0, samples = 1; };
  std::vector<TargetGL> targets_ = std::vector<TargetGL>(1);
  uint32_t currentTarget_ = 0;
  int32_t frameW_ = 0, frameH_ = 0;
  size_t uboAlign_ = 256;
  std::vector<PipelineGL> pipelines_ = std::vector<PipelineGL>(1); // index 0 = invalid
  uint32_t drawCalls_ = 0;
  const PipelineGL* Bind(PipelineHandle p, BufferHandle vertices);
};
}
