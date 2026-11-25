#pragma once
// DDS texture files (DirectDraw Surface), the engine's own reader: the legacy header (DXT1-5 / ATI1 / ATI2 four-CCs,
// 24 / 32-bit RGB masks) and the DX10 header (DXGI formats), block-compressed BC1-BC7 and uncompressed 8-bit RGBA /
// BGRA / BGR, with their mip chains. Cube maps and texture arrays keep their first face / slice; volume textures are
// refused. Rows are top row first (as DDS stores them, and as the rest of the engine's images are).
// Decoding to RGBA8 (for drivers without a block format, thumbnails, the cooker) uses rgbcx / bc7decomp (MIT);
// BC6H (HDR) is read but not decoded.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bdg {

enum class DdsFormat { RGBA8, BGRA8, BGRX8, BGR8, BC1, BC2, BC3, BC4, BC5, BC6H, BC7 };

struct DdsImage {
  int32_t w = 0, h = 0;
  DdsFormat format = DdsFormat::RGBA8;
  bool srgb = false;                      // the DX10 header names an _SRGB format (legacy files cannot say)
  std::vector<std::vector<uint8_t>> mips; // level 0 first; block formats: ceil(w/4) x ceil(h/4) blocks per level
};

bool IsDDS(const uint8_t* data, size_t size);  // starts with "DDS "
bool IsBlockFormat(DdsFormat f);
size_t DdsBlockBytes(DdsFormat f);              // 8 (BC1, BC4) or 16; 0 for uncompressed formats
const char* DdsFormatName(DdsFormat f);         // "BC1", "RGBA8", ...
// Bytes of one level: blocks for block formats, rows for the others.
size_t DdsLevelBytes(DdsFormat f, int32_t w, int32_t h);

// Reads the header and every mip level present (a file cut short keeps the levels it has, at least one).
bool ParseDDS(const uint8_t* data, size_t size, DdsImage& out, std::string* error = nullptr);
bool ReadDDSFile(const std::filesystem::path& path, DdsImage& out, std::string* error = nullptr);
// One level as RGBA8, top row first. False for BC6H and levels that are missing.
bool DecodeDDS(const DdsImage& img, int mip, std::vector<uint8_t>& rgba, int32_t& w, int32_t& h);

} // namespace bdg
