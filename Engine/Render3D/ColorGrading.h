#pragma once
// Colour grading (queue 3-5b): a 3D lookup table applied to the final display colour (after tone mapping and the
// sRGB curve). The table is built on the CPU from an optional .cube file and a few controls, so artists can grade
// in another tool (DaVinci Resolve, Photoshop ... export .cube) and still nudge it in the editor.
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {

struct ColorGrading3D {
  float contrast = 1;     // around mid grey; 1 = unchanged
  float saturation = 1;   // 0 = grey, 1 = unchanged
  float temperature = 0;  // -1 cool (blue) .. +1 warm (orange)
  float tint = 0;         // -1 green .. +1 magenta
  float intensity = 1;    // blend of the graded colour over the original
  int lutSize = 0;        // optional .cube table (display colour in and out), lutSize^3 RGB, red fastest
  std::vector<float> lut;
  bool Neutral() const;   // changes nothing (no table, default controls)
};

// Adobe / Resolve .cube text: LUT_3D_SIZE n, then n^3 lines "r g b" (red fastest). Comments (#), TITLE and
// DOMAIN_MIN / DOMAIN_MAX of 0 0 0 / 1 1 1 are accepted. 1D tables are refused.
bool ParseCubeLut(std::string_view text, int& size, std::vector<float>& rgb, std::string* error = nullptr);
// One display colour (0..1) through the grading: the .cube table (trilinear), white balance, contrast, saturation.
glm::vec3 ApplyGrading(const ColorGrading3D& g, glm::vec3 c);
// The whole grading as a size^3 RGBA8 table for the GPU (red fastest, then green, then blue).
std::vector<uint8_t> BuildGradingLut(const ColorGrading3D& g, int size = 32);

} // namespace bdg
