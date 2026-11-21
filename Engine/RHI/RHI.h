#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bdg {
class Window;

// RHI: engine code never calls a graphics API directly. First backend: OpenGL 4.5 (DSA).
struct Color { float r, g, b, a; };

struct BufferHandle   { uint32_t id = 0; bool Valid() const { return id != 0; } };
struct PipelineHandle { uint32_t id = 0; bool Valid() const { return id != 0; } };

enum class BufferUsage { Vertex, Index, Uniform };

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

  PipelineHandle CreatePipeline(const PipelineDesc& desc);
  void           DestroyPipeline(PipelineHandle pipeline);

  void Draw(PipelineHandle pipeline, BufferHandle vertices, uint32_t vertexCount, uint32_t firstVertex = 0);
  void DrawIndexed(PipelineHandle pipeline, BufferHandle vertices, BufferHandle indices, uint32_t indexCount);

  Color ReadPixel(int32_t x, int32_t y); // test hook (slow): reads the back buffer
  void  DebugTriggerError();             // test hook: issues an invalid GL call to verify debug output
  uint32_t DrawCallsThisFrame() const { return drawCalls_; }

private:
  struct PipelineGL { uint32_t program = 0, vao = 0, stride = 0; bool alphaBlend = false; };
  std::vector<PipelineGL> pipelines_ = std::vector<PipelineGL>(1); // index 0 = invalid
  uint32_t drawCalls_ = 0;
  const PipelineGL* Bind(PipelineHandle p, BufferHandle vertices);
};
}
