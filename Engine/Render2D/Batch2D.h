#pragma once
#include "RHI/RHI.h"
#include <vector>

namespace bdg {
// 2D batch renderer: rects, rounded rects (SDF), borders, lines and SDF text glyphs in one pipeline.
// Coordinates are pixels, origin top-left, y down. Basis for the UI framework.
class Batch2D {
public:
  bool Init(RHI& rhi);
  void Shutdown();

  void Begin(int32_t viewportW, int32_t viewportH);
  void Rect(float x, float y, float w, float h, Color fill, float radius = 0.f,
            float border = 0.f, Color borderColor = {0, 0, 0, 0});
  void Line(float x0, float y0, float x1, float y1, float thickness, Color color);
  // One glyph quad sampled from the SDF text atlas (see SetTextAtlas).
  void Glyph(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color color);
  void SetTextAtlas(TextureHandle atlas); // single atlas for now
  void End(); // flushes remaining quads

  uint32_t QuadsThisFrame() const { return quadsFrame_; }
  uint32_t FlushesThisFrame() const { return flushesFrame_; }

private:
  struct Vertex {
    float x, y;           // pixel position
    float lx, ly;         // local position relative to shape center (shape frame)
    float hw, hh;         // half size
    float radius, border;
    float r, g, b, a;     // fill
    float br, bg, bb, ba; // border color
    float u, v, mode, pad; // mode 0 = shape, 1 = SDF glyph
  };
  static constexpr uint32_t kMaxQuads = 16384;

  void PushQuad(const float corners[4][2], const float local[4][2], float hw, float hh,
                float radius, float border, Color fill, Color borderColor);
  void Flush();

  RHI* rhi_ = nullptr;
  PipelineHandle pipeline_;
  BufferHandle vertexBuffer_, indexBuffer_, uniformBuffer_;
  TextureHandle dummyTexture_, atlas_;
  std::vector<Vertex> vertices_;
  uint32_t quadsFrame_ = 0, flushesFrame_ = 0;
};
}
