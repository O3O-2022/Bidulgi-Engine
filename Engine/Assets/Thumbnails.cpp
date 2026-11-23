#include "Assets/Thumbnails.h"
#include "Assets/AssetDatabase.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <format>

namespace bdg {
namespace fs = std::filesystem;

bool ThumbnailCache::Supported(const AssetRecord& r) {
  if (r.importer != "texture") return false;
  std::string e = fs::path(r.path).extension().string();
  for (char& c : e) c = (char)std::tolower((unsigned char)c);
  return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".psd" || e == ".hdr";
}

bool ThumbnailCache::Get(const AssetRecord& r, const fs::path& source, std::vector<uint8_t>& rgba) {
  if (!Supported(r)) return false;
  fs::path cached = dir_ / std::format("{}-{:016x}.png", r.guid, r.hash);
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  if (fs::exists(cached) && LoadImageRGBA(cached, w, h, px) && w == kSize && h == kSize) {
    rgba = std::move(px);
    ++stats_.loaded;
    return true;
  }
  if (!LoadImageRGBA(source, w, h, px) || w <= 0 || h <= 0) {
    ++stats_.failed;
    return false;
  }
  // Fit into kSize x kSize keeping the aspect ratio; average the source pixels under each thumbnail pixel.
  float scale = std::min((float)kSize / w, (float)kSize / h);
  int tw = std::max(1, (int)(w * scale)), th = std::max(1, (int)(h * scale)), ox = (kSize - tw) / 2, oy = (kSize - th) / 2;
  rgba.assign((size_t)kSize * kSize * 4, 0);
  for (int y = 0; y < th; ++y)
    for (int x = 0; x < tw; ++x) {
      int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw), y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
      unsigned sum[4] = {0, 0, 0, 0}, n = 0;
      for (int sy = y0; sy < y1; ++sy)
        for (int sx = x0; sx < x1; ++sx, ++n)
          for (int c = 0; c < 4; ++c) sum[c] += px[((size_t)sy * w + sx) * 4 + c];
      for (int c = 0; c < 4; ++c) rgba[((size_t)(oy + y) * kSize + ox + x) * 4 + c] = (uint8_t)(sum[c] / n);
    }
  std::error_code ec;
  fs::create_directories(dir_, ec);
  for (auto& old : fs::directory_iterator(dir_, ec)) // the previous thumbnail of this asset is stale now
    if (old.path().filename().string().starts_with(r.guid + "-")) fs::remove(old.path(), ec);
  std::vector<uint8_t> png;
  if (EncodePNG(kSize, kSize, rgba.data(), false, png))
    if (std::FILE* f = _wfopen(cached.c_str(), L"wb")) { std::fwrite(png.data(), 1, png.size(), f); std::fclose(f); }
  ++stats_.generated;
  return true;
}
}
