#include "Render3D/ColorGrading.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace bdg {

bool ColorGrading3D::Neutral() const {
  return lutSize == 0 && contrast == 1 && saturation == 1 && temperature == 0 && tint == 0;
}

bool ParseCubeLut(std::string_view text, int& size, std::vector<float>& rgb, std::string* error) {
  auto fail = [&](std::string why) { if (error) *error = std::move(why); return false; };
  size = 0;
  rgb.clear();
  size_t pos = 0;
  int line = 0;
  auto number = [](std::string_view& s, float& out) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    const char* end = s.data() + s.size();
    auto r = std::from_chars(s.data(), end, out);
    if (r.ec != std::errc()) return false;
    s.remove_prefix((size_t)(r.ptr - s.data()));
    return true;
  };
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view l = text.substr(pos, eol - pos);
    pos = eol + 1;
    ++line;
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.remove_suffix(1);
    while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
    if (l.empty() || l.front() == '#') continue;
    if (l.starts_with("TITLE")) continue;
    if (l.starts_with("LUT_1D_SIZE")) return fail("1D tables are not supported (line " + std::to_string(line) + ")");
    if (l.starts_with("LUT_3D_SIZE")) {
      std::string_view v = l.substr(11);
      float n = 0;
      if (!number(v, n) || n < 2 || n > 256) return fail("bad LUT_3D_SIZE (line " + std::to_string(line) + ")");
      size = (int)n;
      rgb.reserve((size_t)size * size * size * 3);
      continue;
    }
    if (l.starts_with("DOMAIN_MIN") || l.starts_with("DOMAIN_MAX")) {
      std::string_view v = l.substr(10);
      float a, b, c;
      const float want = l.starts_with("DOMAIN_MIN") ? 0.f : 1.f;
      if (!number(v, a) || !number(v, b) || !number(v, c) || a != want || b != want || c != want)
        return fail("only the 0..1 domain is supported (line " + std::to_string(line) + ")");
      continue;
    }
    if (!size) return fail("a value before LUT_3D_SIZE (line " + std::to_string(line) + ")");
    std::string_view v = l;
    float r, g, b;
    if (!number(v, r) || !number(v, g) || !number(v, b)) return fail("expected three numbers (line " + std::to_string(line) + ")");
    rgb.push_back(r);
    rgb.push_back(g);
    rgb.push_back(b);
  }
  if (!size) return fail("no LUT_3D_SIZE");
  if (rgb.size() != (size_t)size * size * size * 3)
    return fail(std::format("{} values for a {}^3 table (want {})", rgb.size() / 3, size, (size_t)size * size * size));
  return true;
}

glm::vec3 ApplyGrading(const ColorGrading3D& g, glm::vec3 c) {
  const glm::vec3 in = glm::clamp(c, 0.f, 1.f);
  c = in;
  if (g.lutSize >= 2 && g.lut.size() == (size_t)g.lutSize * g.lutSize * g.lutSize * 3) { // trilinear lookup
    const int n = g.lutSize;
    const glm::vec3 f = c * (float)(n - 1);
    const glm::ivec3 i0 = glm::min(glm::ivec3(glm::floor(f)), n - 2);
    const glm::vec3 t = f - glm::vec3(i0);
    glm::vec3 sum(0);
    for (int k = 0; k < 8; ++k) {
      const glm::ivec3 q = i0 + glm::ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
      const float w = ((k & 1) ? t.x : 1 - t.x) * ((k & 2) ? t.y : 1 - t.y) * ((k & 4) ? t.z : 1 - t.z);
      const size_t at = (((size_t)q.z * n + q.y) * n + q.x) * 3;
      sum += w * glm::vec3(g.lut[at], g.lut[at + 1], g.lut[at + 2]);
    }
    c = sum;
  }
  c *= glm::vec3(1 + 0.2f * g.temperature + 0.1f * g.tint, 1 - 0.2f * g.tint, 1 - 0.2f * g.temperature + 0.1f * g.tint); // white balance
  c = (c - 0.5f) * g.contrast + 0.5f;
  const float luma = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
  c = glm::mix(glm::vec3(luma), c, g.saturation);
  c = glm::clamp(c, 0.f, 1.f);
  return glm::mix(in, c, std::clamp(g.intensity, 0.f, 1.f));
}

std::vector<uint8_t> BuildGradingLut(const ColorGrading3D& g, int size) {
  std::vector<uint8_t> out((size_t)size * size * size * 4);
  for (int b = 0; b < size; ++b)
    for (int gr = 0; gr < size; ++gr)
      for (int r = 0; r < size; ++r) {
        const glm::vec3 c = ApplyGrading(g, glm::vec3(r, gr, b) / (float)(size - 1));
        uint8_t* o = &out[(((size_t)b * size + gr) * size + r) * 4];
        o[0] = (uint8_t)std::lround(c.r * 255);
        o[1] = (uint8_t)std::lround(c.g * 255);
        o[2] = (uint8_t)std::lround(c.b * 255);
        o[3] = 255;
      }
  return out;
}

} // namespace bdg
