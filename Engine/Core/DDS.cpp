#include "Core/DDS.h"
#include "Core/Image.h"
#include <bc7decomp.h>
#include <rgbcx.h>
#include <algorithm>
#include <cstring>

namespace bdg {
namespace {
constexpr uint32_t FourCC(char a, char b, char c, char d) {
  return (uint32_t)(uint8_t)a | (uint32_t)(uint8_t)b << 8 | (uint32_t)(uint8_t)c << 16 | (uint32_t)(uint8_t)d << 24;
}
// Header words after the "DDS " magic (DDS_HEADER, 124 bytes) and its pixel format (DDS_PIXELFORMAT at word 18).
enum : int { kFlags = 1, kHeight = 2, kWidth = 3, kDepth = 5, kMipCount = 6, kPfFlags = 19, kFourCC = 20, kBits = 21,
             kRMask = 22, kGMask = 23, kBMask = 24, kAMask = 25, kCaps2 = 27 };
constexpr uint32_t kPfAlpha = 0x1, kPfFourCC = 0x4, kPfRGB = 0x40;
constexpr uint32_t kCaps2Volume = 0x200000;
constexpr uint32_t kDepthFlag = 0x800000;

bool FromDxgi(uint32_t dxgi, DdsFormat& f, bool& srgb, std::string& why) {
  srgb = false;
  switch (dxgi) {
    case 28: f = DdsFormat::RGBA8; return true;             // R8G8B8A8_UNORM
    case 29: f = DdsFormat::RGBA8; srgb = true; return true; // _SRGB
    case 87: f = DdsFormat::BGRA8; return true;             // B8G8R8A8_UNORM
    case 91: f = DdsFormat::BGRA8; srgb = true; return true;
    case 88: f = DdsFormat::BGRX8; return true;             // B8G8R8X8_UNORM
    case 93: f = DdsFormat::BGRX8; srgb = true; return true;
    case 70: case 71: f = DdsFormat::BC1; return true;      // typeless, unorm
    case 72: f = DdsFormat::BC1; srgb = true; return true;
    case 73: case 74: f = DdsFormat::BC2; return true;
    case 75: f = DdsFormat::BC2; srgb = true; return true;
    case 76: case 77: f = DdsFormat::BC3; return true;
    case 78: f = DdsFormat::BC3; srgb = true; return true;
    case 79: case 80: f = DdsFormat::BC4; return true;
    case 82: case 83: f = DdsFormat::BC5; return true;
    case 94: case 95: case 96: f = DdsFormat::BC6H; return true;
    case 97: case 98: f = DdsFormat::BC7; return true;
    case 99: f = DdsFormat::BC7; srgb = true; return true;
    case 81: case 84: why = "signed BC4 / BC5 (SNORM) is not supported"; return false;
    default: why = "DXGI format " + std::to_string(dxgi) + " is not supported"; return false;
  }
}

bool FromLegacy(const uint32_t* hd, DdsFormat& f, std::string& why) {
  const uint32_t pf = hd[kPfFlags];
  if (pf & kPfFourCC) {
    switch (hd[kFourCC]) {
      case FourCC('D', 'X', 'T', '1'): f = DdsFormat::BC1; return true;
      case FourCC('D', 'X', 'T', '2'): case FourCC('D', 'X', 'T', '3'): f = DdsFormat::BC2; return true;
      case FourCC('D', 'X', 'T', '4'): case FourCC('D', 'X', 'T', '5'): f = DdsFormat::BC3; return true;
      case FourCC('A', 'T', 'I', '1'): case FourCC('B', 'C', '4', 'U'): f = DdsFormat::BC4; return true;
      case FourCC('A', 'T', 'I', '2'): case FourCC('B', 'C', '5', 'U'): f = DdsFormat::BC5; return true;
      default: break;
    }
    const uint32_t c = hd[kFourCC];
    why = std::string("four-CC '") + (char)(c & 0xff) + (char)(c >> 8 & 0xff) + (char)(c >> 16 & 0xff) + (char)(c >> 24) + "' is not supported";
    return false;
  }
  if (pf & kPfRGB) {
    const uint32_t bits = hd[kBits], r = hd[kRMask], g = hd[kGMask], b = hd[kBMask], a = (pf & kPfAlpha) ? hd[kAMask] : 0;
    if (bits == 32 && r == 0xff && g == 0xff00 && b == 0xff0000) { f = DdsFormat::RGBA8; return true; } // alpha or not: read as is
    if (bits == 32 && r == 0xff0000 && g == 0xff00 && b == 0xff) { f = a == 0xff000000u ? DdsFormat::BGRA8 : DdsFormat::BGRX8; return true; }
    if (bits == 24 && r == 0xff0000 && g == 0xff00 && b == 0xff) { f = DdsFormat::BGR8; return true; }
  }
  why = "uncompressed layout (" + std::to_string(hd[kBits]) + " bits, masks) is not supported";
  return false;
}
}

bool IsDDS(const uint8_t* data, size_t size) { return data && size >= 4 && std::memcmp(data, "DDS ", 4) == 0; }

bool IsBlockFormat(DdsFormat f) { return DdsBlockBytes(f) != 0; }

size_t DdsBlockBytes(DdsFormat f) {
  switch (f) {
    case DdsFormat::BC1: case DdsFormat::BC4: return 8;
    case DdsFormat::BC2: case DdsFormat::BC3: case DdsFormat::BC5: case DdsFormat::BC6H: case DdsFormat::BC7: return 16;
    default: return 0;
  }
}

const char* DdsFormatName(DdsFormat f) {
  switch (f) {
    case DdsFormat::RGBA8: return "RGBA8";
    case DdsFormat::BGRA8: return "BGRA8";
    case DdsFormat::BGRX8: return "BGRX8";
    case DdsFormat::BGR8: return "BGR8";
    case DdsFormat::BC1: return "BC1";
    case DdsFormat::BC2: return "BC2";
    case DdsFormat::BC3: return "BC3";
    case DdsFormat::BC4: return "BC4";
    case DdsFormat::BC5: return "BC5";
    case DdsFormat::BC6H: return "BC6H";
    case DdsFormat::BC7: return "BC7";
  }
  return "?";
}

size_t DdsLevelBytes(DdsFormat f, int32_t w, int32_t h) {
  if (const size_t bb = DdsBlockBytes(f)) return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * bb;
  return (size_t)w * (size_t)h * (f == DdsFormat::BGR8 ? 3 : 4);
}

bool ParseDDS(const uint8_t* data, size_t size, DdsImage& out, std::string* error) {
  out = {};
  std::string why;
  auto fail = [&](const std::string& w) {
    if (error) *error = w;
    return false;
  };
  if (!IsDDS(data, size)) return fail("not a DDS file");
  if (size < 4 + 124) return fail("header cut short");
  uint32_t hd[31];
  std::memcpy(hd, data + 4, sizeof(hd));
  if (hd[0] != 124) return fail("bad header size");
  size_t at = 128;
  if ((hd[kPfFlags] & kPfFourCC) && hd[kFourCC] == FourCC('D', 'X', '1', '0')) {
    if (size < at + 20) return fail("DX10 header cut short");
    uint32_t dx10[5];
    std::memcpy(dx10, data + at, sizeof(dx10));
    at += 20;
    if (dx10[1] == 4) return fail("3D (volume) textures are not supported");
    if (!FromDxgi(dx10[0], out.format, out.srgb, why)) return fail(why);
  } else {
    if ((hd[27] & kCaps2Volume) || ((hd[kFlags] & kDepthFlag) && hd[kDepth] > 1)) return fail("3D (volume) textures are not supported");
    if (!FromLegacy(hd, out.format, why)) return fail(why);
  }
  out.w = (int32_t)hd[kWidth];
  out.h = (int32_t)hd[kHeight];
  if (out.w <= 0 || out.h <= 0 || out.w > 65536 || out.h > 65536) return fail("bad size");
  int full = 1;
  for (int32_t s = std::max(out.w, out.h); s > 1; s >>= 1) ++full;
  const int levels = std::clamp((int)std::max(hd[kMipCount], 1u), 1, full);
  for (int m = 0; m < levels; ++m) {
    const int32_t w = std::max(1, out.w >> m), h = std::max(1, out.h >> m);
    const size_t bytes = DdsLevelBytes(out.format, w, h);
    if (at + bytes > size) break; // cut short: keep the levels that are complete
    out.mips.emplace_back(data + at, data + at + bytes);
    at += bytes;
  }
  if (out.mips.empty()) return fail("pixel data cut short");
  return true;
}

bool ReadDDSFile(const std::filesystem::path& path, DdsImage& out, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!ReadFileBytes(path, bytes)) {
    if (error) *error = "cannot read the file";
    return false;
  }
  return ParseDDS(bytes.data(), bytes.size(), out, error);
}

bool DecodeDDS(const DdsImage& img, int mip, std::vector<uint8_t>& rgba, int32_t& w, int32_t& h) {
  if (mip < 0 || mip >= (int)img.mips.size() || img.format == DdsFormat::BC6H) return false;
  w = std::max(1, img.w >> mip);
  h = std::max(1, img.h >> mip);
  const std::vector<uint8_t>& src = img.mips[(size_t)mip];
  if (src.size() != DdsLevelBytes(img.format, w, h)) return false;
  const size_t n = (size_t)w * h;
  rgba.assign(n * 4, 255);
  switch (img.format) {
    case DdsFormat::RGBA8: std::memcpy(rgba.data(), src.data(), n * 4); return true;
    case DdsFormat::BGRA8: case DdsFormat::BGRX8:
      for (size_t i = 0; i < n; ++i) {
        rgba[i * 4] = src[i * 4 + 2];
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4];
        rgba[i * 4 + 3] = img.format == DdsFormat::BGRA8 ? src[i * 4 + 3] : 255;
      }
      return true;
    case DdsFormat::BGR8:
      for (size_t i = 0; i < n; ++i) {
        rgba[i * 4] = src[i * 3 + 2];
        rgba[i * 4 + 1] = src[i * 3 + 1];
        rgba[i * 4 + 2] = src[i * 3];
      }
      return true;
    default: break;
  }
  const size_t bb = DdsBlockBytes(img.format);
  const int bw = (w + 3) / 4, bh = (h + 3) / 4;
  uint8_t block[16 * 4];
  for (int by = 0; by < bh; ++by)
    for (int bx = 0; bx < bw; ++bx) {
      const uint8_t* b = &src[((size_t)by * bw + bx) * bb];
      std::memset(block, 0, sizeof(block));
      for (int i = 0; i < 16; ++i) block[i * 4 + 3] = 255;
      switch (img.format) {
        case DdsFormat::BC1: rgbcx::unpack_bc1(b, block, true); break; // 3-colour blocks: transparent black (cut-outs)
        case DdsFormat::BC2:
          rgbcx::unpack_bc1(b + 8, block, false);
          for (int i = 0; i < 16; ++i) { // explicit 4-bit alpha, two texels per byte
            const uint8_t a4 = (uint8_t)((b[i / 2] >> ((i & 1) * 4)) & 0xf);
            block[i * 4 + 3] = (uint8_t)(a4 * 17);
          }
          break;
        case DdsFormat::BC3: rgbcx::unpack_bc3(b, block); break;
        case DdsFormat::BC4:
          rgbcx::unpack_bc4(b, block, 4);
          for (int i = 0; i < 16; ++i) block[i * 4 + 1] = block[i * 4 + 2] = block[i * 4]; // grey
          break;
        case DdsFormat::BC5: rgbcx::unpack_bc5(b, block, 0, 1, 4); break; // X, Y; Z rebuilt by the shader
        case DdsFormat::BC7: bc7decomp::unpack_bc7(b, (bc7decomp::color_rgba*)block); break;
        default: return false;
      }
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
          const int px = bx * 4 + x, py = by * 4 + y;
          if (px < w && py < h) std::memcpy(&rgba[((size_t)py * w + px) * 4], &block[(y * 4 + x) * 4], 4);
        }
    }
  return true;
}

} // namespace bdg
