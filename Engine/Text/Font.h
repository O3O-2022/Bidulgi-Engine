#pragma once
#include "RHI/RHI.h"
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace bdg {
class Batch2D;

// SDF font: glyphs are rendered on demand by FreeType's SDF renderer into one R8 atlas,
// so one atlas serves every text size and text stays sharp when scaled.
// Shaping (HarfBuzz) comes later; precomposed Hangul and Latin render correctly without it.
class Font {
public:
  bool Load(RHI& rhi, const char* path, int basePixelSize = 48);
  bool LoadMono(const char* path); // code font (e.g. Consolas); missing glyphs fall back to the main face
  void Shutdown();

  // Draws UTF-8 text with its top-left at (x, y). Returns the advance width in pixels.
  float DrawText(Batch2D& batch, float x, float y, float sizePx, Color color, std::string_view utf8, bool mono = false);
  float Measure(float sizePx, std::string_view utf8, bool mono = false);
  // CSS line-height: normal, rounded like browsers do (ascent and descent rounded separately).
  float NormalLineHeight(float sizePx) const {
    return std::round(ascEm_ * sizePx) + std::round(descEm_ * sizePx) + std::round(gapEm_ * sizePx);
  }
  float LineHeight(float sizePx) const { return lineHeight_ * sizePx / (float)basePx_; }

  TextureHandle Atlas() const { return atlas_; }
  size_t GlyphCount() const { return glyphs_.size(); }

private:
  struct Glyph { float u0 = 0, v0 = 0, u1 = 0, v1 = 0; int w = 0, h = 0, left = 0, top = 0; float advance = 0; };
  const Glyph& Get(uint32_t codepoint, bool mono);

  RHI* rhi_ = nullptr;
  void* library_ = nullptr; // FT_Library (kept out of the header)
  void* face_ = nullptr;    // FT_Face
  void* monoFace_ = nullptr;
  int basePx_ = 48;
  float ascender_ = 0, lineHeight_ = 0;
  float ascEm_ = 0.95f, descEm_ = 0.25f, gapEm_ = 0; // face metrics in em (hhea)
  TextureHandle atlas_;
  static constexpr int kAtlasSize = 2048;
  int penX_ = 1, penY_ = 1, rowH_ = 0;
  std::unordered_map<uint32_t, Glyph> glyphs_;
};
}
