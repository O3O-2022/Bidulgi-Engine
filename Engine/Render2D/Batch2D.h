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
  // Soft-edged rounded rect for drop shadows (CSS box-shadow): blur = edge falloff width in pixels.
  void Shadow(float x, float y, float w, float h, Color color, float radius, float blur);
  // Isosceles triangle inside the box, pointing right (Graph exec pins). border > 0 draws an outline.
  void Triangle(float x, float y, float w, float h, Color fill, float border = 0.f, Color borderColor = {0, 0, 0, 0});
  // Filled convex quad, corners in order (gizmo plane handles); no anti-aliased edge.
  void Quad(const float corners[4][2], Color fill);
  // One glyph quad sampled from the SDF text atlas (see SetTextAtlas).
  void Glyph(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color color);
  void SetTextAtlas(TextureHandle atlas); // single atlas for now
  // Editor icons (UI/IconAtlas): their own texture slot, so icons never split the batch. uv as for Glyph.
  void SetIconAtlas(TextureHandle atlas) { icons_ = atlas; }
  void Icon(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint = {1, 1, 1, 1});
  // Textured quad (RGBA, colour multiplied by tint). A different texture than the previous image flushes the
  // batch. uv (0,0) = the texture's first row: pass v0 = 1, v1 = 0 for a render target (bottom-up).
  void Image(float x, float y, float w, float h, TextureHandle texture, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
             Color tint = {1, 1, 1, 1});
  // Clip rectangles (intersected when nested). Stored per vertex, so clipping never splits the batch.
  void PushClip(float x, float y, float w, float h);
  void PopClip();
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
    float u, v, mode, blur; // mode 0 = rounded rect, 1 = SDF glyph, 2 = triangle, 3 = image, 4 = icon; blur = soft edge width
    float cx0, cy0, cx1, cy1; // clip rectangle
  };
  static constexpr uint32_t kMaxQuads = 16384;

  void PushQuad(const float corners[4][2], const float local[4][2], float hw, float hh,
                float radius, float border, Color fill, Color borderColor, float mode = 0.f, float blur = 0.f);
  void Flush();

  RHI* rhi_ = nullptr;
  PipelineHandle pipeline_;
  BufferHandle vertexBuffer_, indexBuffer_, uniformBuffer_;
  TextureHandle dummyTexture_, atlas_, image_, icons_;
  std::vector<Vertex> vertices_;
  struct Clip { float x0, y0, x1, y1; };
  std::vector<Clip> clips_;
  Clip clip_{-1e9f, -1e9f, 1e9f, 1e9f};
  uint32_t quadsFrame_ = 0, flushesFrame_ = 0;
};
}
