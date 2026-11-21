#include "Text/Font.h"
#include "Render2D/Batch2D.h"
#include "Core/Log.h"
#include <cstring>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

namespace bdg {
namespace {
// Decodes one UTF-8 code point; advances i. Invalid bytes become U+FFFD.
uint32_t NextCodepoint(std::string_view s, size_t& i) {
  unsigned char c = (unsigned char)s[i++];
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
  if (extra < 0) return 0xFFFD;
  uint32_t cp = c & (0x3F >> extra);
  for (int k = 0; k < extra; ++k) {
    if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
  }
  return cp;
}
}

bool Font::Load(RHI& rhi, const char* path, int basePixelSize) {
  rhi_ = &rhi;
  basePx_ = basePixelSize;
  FT_Library lib = nullptr;
  if (FT_Init_FreeType(&lib)) { LogError("FreeType init failed"); return false; }
  library_ = lib;
  FT_Int spread = 8; // SDF range in pixels at the base size
  FT_Property_Set(lib, "sdf", "spread", &spread);
  FT_Property_Set(lib, "bsdf", "spread", &spread);

  FT_Face face = nullptr;
  if (FT_New_Face(lib, path, 0, &face)) { LogError("font load failed: {}", path); return false; }
  face_ = face;
  FT_Set_Pixel_Sizes(face, 0, (FT_UInt)basePx_);
  ascender_ = face->size->metrics.ascender / 64.f;
  lineHeight_ = face->size->metrics.height / 64.f;

  atlas_ = rhi.CreateTexture2D(kAtlasSize, kAtlasSize, TextureFormat::R8, nullptr);
  LogInfo("font: {} ({}, base {}px, SDF atlas {}x{})", path, face->family_name ? face->family_name : "?",
          basePx_, kAtlasSize, kAtlasSize);
  return true;
}

void Font::Shutdown() {
  if (face_) FT_Done_Face((FT_Face)face_);
  if (library_) FT_Done_FreeType((FT_Library)library_);
  if (rhi_) rhi_->DestroyTexture(atlas_);
  face_ = library_ = nullptr;
  rhi_ = nullptr;
  glyphs_.clear();
}

const Font::Glyph& Font::Get(uint32_t cp) {
  auto it = glyphs_.find(cp);
  if (it != glyphs_.end()) return it->second;
  Glyph g;
  FT_Face face = (FT_Face)face_;
  if (face && !FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) {
    g.advance = face->glyph->advance.x / 64.f;
    if (face->glyph->outline.n_points > 0 && !FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF)) {
      const FT_Bitmap& bm = face->glyph->bitmap;
      int w = (int)bm.width, h = (int)bm.rows;
      if (penX_ + w + 1 >= kAtlasSize) { penX_ = 1; penY_ += rowH_ + 1; rowH_ = 0; } // next shelf
      if (w > 0 && h > 0 && penY_ + h + 1 < kAtlasSize) {
        // Copy row by row: FreeType pitch may differ from width.
        std::vector<uint8_t> pixels((size_t)w * h);
        for (int row = 0; row < h; ++row)
          std::memcpy(&pixels[(size_t)row * w], bm.buffer + (ptrdiff_t)row * bm.pitch, (size_t)w);
        rhi_->UpdateTexture2D(atlas_, penX_, penY_, w, h, TextureFormat::R8, pixels.data());
        g.w = w; g.h = h;
        g.left = face->glyph->bitmap_left;
        g.top = face->glyph->bitmap_top;
        g.u0 = penX_ / (float)kAtlasSize; g.v0 = penY_ / (float)kAtlasSize;
        g.u1 = (penX_ + w) / (float)kAtlasSize; g.v1 = (penY_ + h) / (float)kAtlasSize;
        penX_ += w + 1;
        if (h > rowH_) rowH_ = h;
      } else if (w > 0) {
        LogWarn("font atlas full (U+{:04X})", cp);
      }
    }
  }
  return glyphs_.emplace(cp, g).first->second;
}

float Font::DrawText(Batch2D& batch, float x, float y, float sizePx, Color color, std::string_view s) {
  float scale = sizePx / (float)basePx_, pen = x, baseline = y + ascender_ * scale;
  for (size_t i = 0; i < s.size();) {
    const Glyph& g = Get(NextCodepoint(s, i));
    if (g.w > 0)
      batch.Glyph(pen + g.left * scale, baseline - g.top * scale, g.w * scale, g.h * scale,
                  g.u0, g.v0, g.u1, g.v1, color);
    pen += g.advance * scale;
  }
  return pen - x;
}

float Font::Measure(float sizePx, std::string_view s) {
  float scale = sizePx / (float)basePx_, w = 0;
  for (size_t i = 0; i < s.size();) w += Get(NextCodepoint(s, i)).advance * scale;
  return w;
}
}
