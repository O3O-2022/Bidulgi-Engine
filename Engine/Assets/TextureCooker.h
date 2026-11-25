#pragma once
// Texture import settings and cooking (queue 3-1b, Docs/features/materials-and-textures.md section 4).
//
// Every texture asset gets a preset when its .meta is created, chosen from the file name and editable later:
//   default    colour (sRGB)                  -> BC7 sRGB        names: anything else (_D, _BaseColor, _Albedo)
//   normal     tangent-space normal (linear)  -> BC5 (X, Y; Z rebuilt in the shader)   _N, _Normal, _Nrm
//   masks      packed data, R AO / G rough / B metal (linear) -> BC7                   _ORM, _MRA, _Mask(s)
//   grayscale  one channel (linear)           -> BC4             _R, _Rough, _AO, _Height, _Gray, _Mask1
//   ui         sRGB, no mips, no compression  -> RGBA8           _UI, files under UI/
// Cooking: decode, shrink to maxSize, mip chain (sRGB averaged in linear light, normals renormalised), block
// compression (bc7enc / rgbcx, MIT), written as a standard DDS (DX10 header) to
// <project>/.cache/cooked/<guid>-<content hash>-<settings hash>.dds. Later loads read the DDS only.
// A .dds source (Core/DDS.h) whose blocks already are the cooked format (BC7 / BC5 / BC4, the mips the settings ask
// for, not over maxSize) is kept as it is; other DDS files are decoded and cooked like any image.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bdg {
struct AssetRecord;
struct DdsImage;

enum class TexturePreset { Default, Normal, Masks, Grayscale, UI };
enum class CookedFormat { RGBA8, RGBA8_SRGB, BC7, BC7_SRGB, BC5, BC4 };

struct TextureSettings {
  TexturePreset preset = TexturePreset::Default;
  bool srgb = true, mips = true, repeat = true;
  int maxSize = 2048;
};

const char* PresetName(TexturePreset p);                       // "default", "normal", ...
bool PresetFromString(std::string_view s, TexturePreset& out);
TexturePreset PresetFromPath(const std::string& assetPath);    // the naming rules above
TextureSettings DefaultTextureSettings(const std::string& assetPath);
std::string TextureMetaFields(const TextureSettings& s);       // ,"preset":"normal","srgb":false,... (appended to .meta)
// Settings from the .meta text; fields that are missing keep the defaults for the file name.
TextureSettings ParseTextureSettings(std::string_view metaJson, const std::string& assetPath);
CookedFormat FormatFor(const TextureSettings& s);

struct CookedTexture {
  int32_t w = 0, h = 0;
  CookedFormat format = CookedFormat::RGBA8;
  std::vector<std::vector<uint8_t>> mips; // level 0 first; block-compressed levels are ceil(w/4) x ceil(h/4) blocks
};

// rgba: w x h RGBA8, top row first.
bool CookTexture(const uint8_t* rgba, int32_t w, int32_t h, const TextureSettings& s, CookedTexture& out);
// A DDS source taken as cooked output without re-encoding, when its blocks match the settings (see above).
bool CookFromDDS(const DdsImage& dds, const TextureSettings& s, CookedTexture& out);
// Back to RGBA8 (tests, previews): level `mip` decoded.
bool DecodeCooked(const CookedTexture& t, int mip, std::vector<uint8_t>& rgba, int32_t& w, int32_t& h);
bool WriteDDS(const std::filesystem::path& path, const CookedTexture& t);
bool ReadDDS(const std::filesystem::path& path, CookedTexture& t);

class TextureCookCache {
public:
  struct Stats { int cooked = 0, loaded = 0, failed = 0; double cookMs = 0; };
  void SetDirectory(const std::filesystem::path& dir) { dir_ = dir; }
  // source: the image file; meta: its .meta text (settings). Cooks once, then reads the cached DDS.
  bool Get(const AssetRecord& r, const std::filesystem::path& source, std::string_view metaJson, CookedTexture& out,
           TextureSettings* settings = nullptr);
  const Stats& GetStats() const { return stats_; }

private:
  std::filesystem::path dir_;
  Stats stats_;
};
}
