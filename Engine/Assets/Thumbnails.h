#pragma once
// Thumbnail cache (queue 2-5t): 64x64 RGBA previews of image assets, kept as PNG files in
// <project>/.cache/thumbnails/<guid>-<content hash>.png. A thumbnail is decoded from the source once; later
// starts read the small cached PNG; a changed source (new hash) makes a new one and removes the old file.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bdg {
struct AssetRecord;

class ThumbnailCache {
public:
  static constexpr int kSize = 64;
  struct Stats { int generated = 0, loaded = 0, failed = 0; };

  void SetDirectory(const std::filesystem::path& dir) { dir_ = dir; }
  static bool Supported(const AssetRecord& r); // image importers the decoder reads (png, jpg, tga, bmp, psd, hdr)
  // rgba gets kSize*kSize*4 bytes (the image fitted into the square, transparent around it).
  bool Get(const AssetRecord& r, const std::filesystem::path& source, std::vector<uint8_t>& rgba);
  const Stats& GetStats() const { return stats_; }

private:
  std::filesystem::path dir_;
  Stats stats_;
};
}
