#include "Assets/TextureCooker.h"
#include "Core/Utf8Path.h"
#include "Assets/AssetDatabase.h"
#include "Core/DDS.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>
#include <yyjson.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <bc7decomp.h>
#include <bc7enc.h>
#include <rgbcx.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {
constexpr int kCookerVersion = 1; // bump when cooked output changes: every cached DDS is made again

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool EndsWithAny(const std::string& stem, std::initializer_list<const char*> suffixes) {
  for (const char* s : suffixes) {
    size_t n = std::strlen(s);
    if (stem.size() >= n && stem.compare(stem.size() - n, n, s) == 0) return true;
  }
  return false;
}

bool IsBlock(CookedFormat f) { return f == CookedFormat::BC7 || f == CookedFormat::BC7_SRGB || f == CookedFormat::BC5 || f == CookedFormat::BC4; }
size_t BlockBytes(CookedFormat f) { return f == CookedFormat::BC4 ? 8 : 16; }

float ToLinear(uint8_t v) {
  float c = v / 255.f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
uint8_t ToSrgb8(float c) {
  c = std::clamp(c, 0.f, 1.f);
  float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
  return (uint8_t)std::lround(s * 255.f);
}

// Half size (floor, at least 1) with a box filter; sRGB colour is averaged in linear light, normals renormalised.
std::vector<uint8_t> Downsample(const std::vector<uint8_t>& src, int w, int h, int& nw, int& nh, TexturePreset preset, bool srgb) {
  nw = std::max(1, w / 2);
  nh = std::max(1, h / 2);
  std::vector<uint8_t> dst((size_t)nw * nh * 4);
  for (int y = 0; y < nh; ++y)
    for (int x = 0; x < nw; ++x) {
      float acc[4] = {0, 0, 0, 0};
      int n = 0;
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
          int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
          const uint8_t* p = &src[((size_t)sy * w + sx) * 4];
          for (int c = 0; c < 3; ++c) acc[c] += srgb ? ToLinear(p[c]) : p[c] / 255.f;
          acc[3] += p[3] / 255.f;
          ++n;
        }
      for (float& a : acc) a /= (float)n;
      uint8_t* q = &dst[((size_t)y * nw + x) * 4];
      if (preset == TexturePreset::Normal) {
        float vx = acc[0] * 2 - 1, vy = acc[1] * 2 - 1, vz = acc[2] * 2 - 1, l = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (l > 1e-6f) { vx /= l; vy /= l; vz /= l; }
        acc[0] = vx * 0.5f + 0.5f; acc[1] = vy * 0.5f + 0.5f; acc[2] = vz * 0.5f + 0.5f;
      }
      for (int c = 0; c < 3; ++c) q[c] = srgb ? ToSrgb8(acc[c]) : (uint8_t)std::lround(std::clamp(acc[c], 0.f, 1.f) * 255.f);
      q[3] = (uint8_t)std::lround(std::clamp(acc[3], 0.f, 1.f) * 255.f);
    }
  return dst;
}

std::once_flag gEncoderInit;

// One level into blocks; rows of blocks are shared between threads.
std::vector<uint8_t> Encode(const std::vector<uint8_t>& px, int w, int h, CookedFormat f) {
  std::call_once(gEncoderInit, [] { bc7enc_compress_block_init(); rgbcx::init(); });
  int bw = (w + 3) / 4, bh = (h + 3) / 4;
  size_t bb = BlockBytes(f);
  std::vector<uint8_t> out((size_t)bw * bh * bb);
  bc7enc_compress_block_params params;
  bc7enc_compress_block_params_init(&params);
  if (f == CookedFormat::BC7) bc7enc_compress_block_params_init_linear_weights(&params); // data, not colour
  std::atomic<int> nextRow{0};
  auto work = [&] {
    uint8_t block[16 * 4];
    for (int by; (by = nextRow++) < bh;)
      for (int bx = 0; bx < bw; ++bx) {
        for (int y = 0; y < 4; ++y) // edge blocks repeat the last row / column
          for (int x = 0; x < 4; ++x) {
            int sx = std::min(bx * 4 + x, w - 1), sy = std::min(by * 4 + y, h - 1);
            std::memcpy(&block[(y * 4 + x) * 4], &px[((size_t)sy * w + sx) * 4], 4);
          }
        void* dst = &out[((size_t)by * bw + bx) * bb];
        if (f == CookedFormat::BC5) rgbcx::encode_bc5(dst, block, 0, 1, 4);
        else if (f == CookedFormat::BC4) rgbcx::encode_bc4(dst, block, 4);
        else bc7enc_compress_block(dst, block, &params);
      }
  };
  unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
  if (bh < 8) threads = 1;
  std::vector<std::thread> pool;
  for (unsigned i = 1; i < threads; ++i) pool.emplace_back(work);
  work();
  for (std::thread& t : pool) t.join();
  return out;
}

// DDS with the DX10 header (any DDS viewer opens the cooked files).
uint32_t DxgiFormat(CookedFormat f) {
  switch (f) {
    case CookedFormat::RGBA8: return 28;
    case CookedFormat::RGBA8_SRGB: return 29;
    case CookedFormat::BC7: return 98;
    case CookedFormat::BC7_SRGB: return 99;
    case CookedFormat::BC5: return 83;
    case CookedFormat::BC4: return 80;
  }
  return 0;
}
}

const char* PresetName(TexturePreset p) {
  switch (p) {
    case TexturePreset::Default: return "default";
    case TexturePreset::Normal: return "normal";
    case TexturePreset::Masks: return "masks";
    case TexturePreset::Grayscale: return "grayscale";
    case TexturePreset::UI: return "ui";
  }
  return "default";
}

bool PresetFromString(std::string_view s, TexturePreset& out) {
  for (TexturePreset p : {TexturePreset::Default, TexturePreset::Normal, TexturePreset::Masks, TexturePreset::Grayscale, TexturePreset::UI})
    if (s == PresetName(p)) { out = p; return true; }
  return false;
}

TexturePreset PresetFromPath(const std::string& assetPath) {
  fs::path p(assetPath);
  std::string stem = Lower(bdg::PathToU8(p.stem())), dir = Lower(p.parent_path().generic_string());
  if (dir == "ui" || dir.starts_with("ui/") || dir.find("/ui/") != std::string::npos || dir.ends_with("/ui") || EndsWithAny(stem, {"_ui"}))
    return TexturePreset::UI;
  if (EndsWithAny(stem, {"_n", "_normal", "_nrm", "_normalmap"})) return TexturePreset::Normal;
  if (EndsWithAny(stem, {"_orm", "_mra", "_mask", "_masks", "_arm"})) return TexturePreset::Masks;
  if (EndsWithAny(stem, {"_r", "_rough", "_roughness", "_ao", "_height", "_h", "_gray", "_grey", "_mask1"})) return TexturePreset::Grayscale;
  return TexturePreset::Default;
}

TextureSettings DefaultTextureSettings(const std::string& assetPath) {
  TextureSettings s;
  s.preset = PresetFromPath(assetPath);
  s.srgb = s.preset == TexturePreset::Default || s.preset == TexturePreset::UI;
  s.mips = s.preset != TexturePreset::UI;
  s.repeat = s.preset != TexturePreset::UI;
  return s;
}

std::string TextureMetaFields(const TextureSettings& s) {
  return std::format(R"(,"preset":"{}","srgb":{},"mips":{},"wrap":"{}","maxSize":{})", PresetName(s.preset), s.srgb, s.mips,
                     s.repeat ? "repeat" : "clamp", s.maxSize);
}

TextureSettings ParseTextureSettings(std::string_view json, const std::string& assetPath) {
  TextureSettings s = DefaultTextureSettings(assetPath);
  yyjson_doc* d = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_COMMENTS);
  yyjson_val* root = d ? yyjson_doc_get_root(d) : nullptr;
  if (yyjson_is_obj(root)) {
    if (const char* p = yyjson_get_str(yyjson_obj_get(root, "preset"))) {
      TexturePreset preset;
      if (PresetFromString(p, preset)) { // a preset change brings its own defaults; explicit fields below win
        s.preset = preset;
        s.srgb = preset == TexturePreset::Default || preset == TexturePreset::UI;
        s.mips = preset != TexturePreset::UI;
      }
    }
    if (yyjson_val* v = yyjson_obj_get(root, "srgb"); yyjson_is_bool(v)) s.srgb = yyjson_get_bool(v);
    if (yyjson_val* v = yyjson_obj_get(root, "mips"); yyjson_is_bool(v)) s.mips = yyjson_get_bool(v);
    if (const char* w = yyjson_get_str(yyjson_obj_get(root, "wrap"))) s.repeat = std::string_view(w) != "clamp";
    if (yyjson_val* v = yyjson_obj_get(root, "maxSize"); yyjson_is_num(v)) s.maxSize = std::clamp((int)yyjson_get_num(v), 1, 16384);
  }
  yyjson_doc_free(d);
  return s;
}

CookedFormat FormatFor(const TextureSettings& s) {
  switch (s.preset) {
    case TexturePreset::Normal: return CookedFormat::BC5;
    case TexturePreset::Grayscale: return CookedFormat::BC4;
    case TexturePreset::UI: return s.srgb ? CookedFormat::RGBA8_SRGB : CookedFormat::RGBA8;
    case TexturePreset::Masks: return CookedFormat::BC7;
    case TexturePreset::Default: break;
  }
  return s.srgb ? CookedFormat::BC7_SRGB : CookedFormat::BC7;
}

bool CookTexture(const uint8_t* rgba, int32_t w, int32_t h, const TextureSettings& s, CookedTexture& out) {
  out = {};
  if (!rgba || w <= 0 || h <= 0) return false;
  std::vector<uint8_t> level(rgba, rgba + (size_t)w * h * 4);
  const bool linearAverage = s.srgb && s.preset != TexturePreset::Normal;
  while (std::max(w, h) > s.maxSize) { // shrink to the size limit with the same filter as the mips
    int nw, nh;
    level = Downsample(level, w, h, nw, nh, s.preset, linearAverage);
    w = nw;
    h = nh;
  }
  out.w = w;
  out.h = h;
  out.format = FormatFor(s);
  for (;;) {
    out.mips.push_back(IsBlock(out.format) ? Encode(level, w, h, out.format) : level);
    if (!s.mips || (w == 1 && h == 1)) break;
    int nw, nh;
    level = Downsample(level, w, h, nw, nh, s.preset, linearAverage);
    w = nw;
    h = nh;
  }
  return true;
}

bool CookFromDDS(const DdsImage& dds, const TextureSettings& s, CookedTexture& out) {
  const CookedFormat want = FormatFor(s);
  const bool same = (dds.format == DdsFormat::BC7 && (want == CookedFormat::BC7 || want == CookedFormat::BC7_SRGB)) ||
                    (dds.format == DdsFormat::BC5 && want == CookedFormat::BC5) || (dds.format == DdsFormat::BC4 && want == CookedFormat::BC4);
  if (!same || std::max(dds.w, dds.h) > s.maxSize || dds.mips.empty()) return false;
  size_t full = 1;
  for (int32_t n = std::max(dds.w, dds.h); n > 1; n >>= 1) ++full;
  if (s.mips && dds.mips.size() != full) return false; // the cooker's chain goes to 1 x 1
  out = {};
  out.w = dds.w;
  out.h = dds.h;
  out.format = want;
  out.mips.assign(dds.mips.begin(), dds.mips.begin() + (s.mips ? (std::ptrdiff_t)full : 1));
  return true;
}

bool DecodeCooked(const CookedTexture& t, int mip, std::vector<uint8_t>& rgba, int32_t& w, int32_t& h) {
  if (mip < 0 || mip >= (int)t.mips.size()) return false;
  w = std::max(1, t.w >> mip);
  h = std::max(1, t.h >> mip);
  rgba.assign((size_t)w * h * 4, 255);
  if (!IsBlock(t.format)) {
    if (t.mips[mip].size() != rgba.size()) return false;
    rgba = t.mips[mip];
    return true;
  }
  int bw = (w + 3) / 4, bh = (h + 3) / 4;
  size_t bb = BlockBytes(t.format);
  if (t.mips[mip].size() != (size_t)bw * bh * bb) return false;
  uint8_t block[16 * 4];
  for (int by = 0; by < bh; ++by)
    for (int bx = 0; bx < bw; ++bx) {
      const uint8_t* src = &t.mips[mip][((size_t)by * bw + bx) * bb];
      std::memset(block, 0, sizeof(block));
      for (int i = 0; i < 16; ++i) block[i * 4 + 3] = 255;
      if (t.format == CookedFormat::BC5) rgbcx::unpack_bc5(src, block, 0, 1, 4);
      else if (t.format == CookedFormat::BC4) rgbcx::unpack_bc4(src, block, 4);
      else bc7decomp::unpack_bc7(src, (bc7decomp::color_rgba*)block);
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
          int px = bx * 4 + x, py = by * 4 + y;
          if (px < w && py < h) std::memcpy(&rgba[((size_t)py * w + px) * 4], &block[(y * 4 + x) * 4], 4);
        }
    }
  return true;
}

bool WriteDDS(const fs::path& path, const CookedTexture& t) {
  if (t.mips.empty()) return false;
  uint32_t header[31] = {};
  header[0] = 124;                                        // size
  header[1] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000; // caps, height, width, pixelformat, mipmapcount, linearsize
  header[2] = (uint32_t)t.h;
  header[3] = (uint32_t)t.w;
  header[4] = (uint32_t)t.mips[0].size();
  header[6] = (uint32_t)t.mips.size();
  header[18] = 32;                                        // pixel format: size, FOURCC, "DX10"
  header[19] = 0x4;
  header[20] = 0x30315844;
  header[26] = 0x1000 | (t.mips.size() > 1 ? 0x8 | 0x400000 : 0); // texture (+ complex, mipmap)
  const uint32_t dx10[5] = {DxgiFormat(t.format), 3, 0, 1, 0};
  std::FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return false;
  bool ok = std::fwrite("DDS ", 1, 4, f) == 4 && std::fwrite(header, 4, 31, f) == 31 && std::fwrite(dx10, 4, 5, f) == 5;
  for (const auto& m : t.mips) ok = ok && std::fwrite(m.data(), 1, m.size(), f) == m.size();
  std::fclose(f);
  return ok;
}

bool ReadDDS(const fs::path& path, CookedTexture& t) {
  t = {};
  std::vector<uint8_t> b;
  if (!ReadFileBytes(path, b) || b.size() < 4 + 124 + 20 || std::memcmp(b.data(), "DDS ", 4) != 0) return false;
  uint32_t header[31], dx10[5];
  std::memcpy(header, b.data() + 4, sizeof(header));
  std::memcpy(dx10, b.data() + 128, sizeof(dx10));
  if (header[20] != 0x30315844) return false; // only the DX10 files this cooker writes
  bool known = false;
  for (CookedFormat f : {CookedFormat::RGBA8, CookedFormat::RGBA8_SRGB, CookedFormat::BC7, CookedFormat::BC7_SRGB, CookedFormat::BC5, CookedFormat::BC4})
    if (DxgiFormat(f) == dx10[0]) { t.format = f; known = true; }
  if (!known) return false;
  t.h = (int32_t)header[2];
  t.w = (int32_t)header[3];
  uint32_t levels = std::max(1u, header[6]);
  if (t.w <= 0 || t.h <= 0 || levels > 16) return false;
  size_t at = 148;
  for (uint32_t m = 0; m < levels; ++m) {
    int w = std::max(1, t.w >> m), h = std::max(1, t.h >> m);
    size_t bytes = IsBlock(t.format) ? (size_t)((w + 3) / 4) * ((h + 3) / 4) * BlockBytes(t.format) : (size_t)w * h * 4;
    if (at + bytes > b.size()) return false;
    t.mips.emplace_back(b.begin() + (std::ptrdiff_t)at, b.begin() + (std::ptrdiff_t)(at + bytes));
    at += bytes;
  }
  return true;
}

bool TextureCookCache::Get(const AssetRecord& r, const fs::path& source, std::string_view metaJson, CookedTexture& out,
                           TextureSettings* settingsOut) {
  TextureSettings s = ParseTextureSettings(metaJson, r.path);
  if (settingsOut) *settingsOut = s;
  uint64_t sh = 1469598103934665603ull; // settings + cooker version: a changed preset makes a new file
  for (char c : TextureMetaFields(s) + std::to_string(kCookerVersion)) sh = (sh ^ (uint8_t)c) * 1099511628211ull;
  fs::path cached = dir_ / std::format("{}-{:016x}-{:08x}.dds", r.guid, r.hash, (uint32_t)(sh ^ (sh >> 32)));
  if (fs::exists(cached) && ReadDDS(cached, out)) {
    ++stats_.loaded;
    return true;
  }
  int32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  auto t0 = std::chrono::steady_clock::now();
  DdsImage dds;
  bool asIs = false;
  if (std::vector<uint8_t> bytes; ReadFileBytes(source, bytes) && IsDDS(bytes.data(), bytes.size())) {
    std::string why;
    if (!ParseDDS(bytes.data(), bytes.size(), dds, &why)) {
      ++stats_.failed;
      LogWarn("texture: {}: {}", r.path, why);
      return false;
    }
    asIs = CookFromDDS(dds, s, out);
    if (!asIs && DecodeDDS(dds, 0, rgba, w, h) && CookTexture(rgba.data(), w, h, s, out)) asIs = true; // decoded and cooked
    if (!asIs) {
      ++stats_.failed;
      LogWarn("texture: {} ({}) could not be decoded", r.path, DdsFormatName(dds.format));
      return false;
    }
  }
  if (!asIs && (!LoadImageRGBA(source, w, h, rgba) || !CookTexture(rgba.data(), w, h, s, out))) {
    ++stats_.failed;
    LogWarn("texture: {} could not be decoded", r.path);
    return false;
  }
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  stats_.cookMs += ms;
  ++stats_.cooked;
  std::error_code ec;
  fs::create_directories(dir_, ec);
  for (auto& old : fs::directory_iterator(dir_, ec)) // earlier cooks of this texture are stale now
    if (bdg::PathToU8(old.path().filename()).starts_with(r.guid + "-")) fs::remove(old.path(), ec);
  if (!WriteDDS(cached, out)) LogWarn("texture: could not write {}", cached.string());
  LogInfo("texture: cooked {} ({}x{}, {}, {} mips) in {:.0f} ms", r.path, out.w, out.h, PresetName(s.preset), out.mips.size(), ms);
  return true;
}
}
