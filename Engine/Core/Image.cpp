#include "Core/Image.h"
#include <cstdio>
#include <vector>

namespace bdg {
namespace {
uint32_t Crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  Put32(out, (uint32_t)data.size());
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  Put32(out, Crc32(&out[start], out.size() - start));
}
}

bool WritePNG(const char* path, int32_t w, int32_t h, const uint8_t* rgba, bool flipY) {
  // Raw scanlines: filter byte 0 + RGBA row.
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 4 + 1) * h);
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* row = rgba + (size_t)(flipY ? h - 1 - y : y) * w * 4;
    raw.push_back(0);
    raw.insert(raw.end(), row, row + (size_t)w * 4);
  }
  // zlib stream with stored (uncompressed) deflate blocks.
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t pos = 0; pos < raw.size() || raw.empty();) {
    size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
    bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
    z.insert(z.end(), raw.begin() + (ptrdiff_t)pos, raw.begin() + (ptrdiff_t)(pos + n));
    pos += n;
    if (last) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  Put32(z, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  Put32(ihdr, (uint32_t)w); Put32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
  Chunk(png, "IHDR", ihdr);
  Chunk(png, "IDAT", z);
  Chunk(png, "IEND", {});

  std::FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  std::fclose(f);
  return ok;
}
}
