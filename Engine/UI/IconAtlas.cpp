#include "UI/IconAtlas.h"
#include "Core/Image.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace bdg {

bool BuildIconAtlas(RHI& rhi, const std::filesystem::path& dir, IconAtlas& out, std::string* error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(dir, ec))
    if (e.is_regular_file() && e.path().extension() == ".png") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    if (error) *error = "no icons in " + dir.string();
    return false;
  }
  constexpr int kIcon = 64, kPad = 4, kCell = kIcon + 2 * kPad, kCols = 8;
  const int rows = (int)(files.size() + kCols - 1) / kCols;
  out.w = kCols * kCell;
  out.h = rows * kCell;
  std::vector<uint8_t> atlas((size_t)out.w * out.h * 4, 0);
  int placed = 0;
  for (const fs::path& f : files) {
    int32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    if (!LoadImageRGBA(f, w, h, px) || w != kIcon || h != kIcon) continue;
    // Spread colours into transparent pixels (a few rings) so filtering and mipmaps do not pull in black fringes.
    for (int ring = 0; ring < 4; ++ring) {
      std::vector<uint8_t> next = px;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          uint8_t* o = &next[((size_t)y * w + x) * 4];
          if (px[((size_t)y * w + x) * 4 + 3] != 0) continue;
          int sum[3] = {0, 0, 0}, n = 0;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              const int nx = x + dx, ny = y + dy;
              if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
              const uint8_t* q = &px[((size_t)ny * w + nx) * 4];
              if (q[3] == 0 && !(q[0] | q[1] | q[2])) continue;
              for (int c = 0; c < 3; ++c) sum[c] += q[c];
              ++n;
            }
          if (n)
            for (int c = 0; c < 3; ++c) o[c] = (uint8_t)(sum[c] / n);
        }
      px.swap(next);
    }
    const int cx = (placed % kCols) * kCell + kPad, cy = (placed / kCols) * kCell + kPad;
    for (int y = 0; y < kIcon; ++y) std::memcpy(&atlas[((size_t)(cy + y) * out.w + cx) * 4], &px[(size_t)y * kIcon * 4], kIcon * 4);
    out.uv[f.stem().string()] = {(float)cx / out.w, (float)cy / out.h, (float)(cx + kIcon) / out.w, (float)(cy + kIcon) / out.h};
    ++placed;
  }
  if (!placed) {
    if (error) *error = "no 64x64 icons could be read in " + dir.string();
    return false;
  }
  TextureDesc d;
  d.w = out.w;
  d.h = out.h;
  d.format = TextureFormat::RGBA8; // display values, like every UI colour
  d.mipmaps = true;
  out.texture = rhi.CreateTexture(d, atlas.data());
  return out.texture.Valid();
}

const char* FileIconName(const std::string& name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return "folder";
  std::string ext = name.substr(dot + 1);
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  struct Map { const char* ext; const char* icon; };
  static const Map kMap[] = {
      {"cpp", "file-cpp"}, {"cc", "file-cpp"}, {"cxx", "file-cpp"}, {"h", "file-h"}, {"hpp", "file-h"}, {"cs", "file-cs"},
      {"scene", "file-scene"}, {"material", "file-material"}, {"mat", "file-material"}, {"matinst", "file-matinst"},
      {"png", "file-texture"}, {"jpg", "file-texture"}, {"jpeg", "file-texture"}, {"tga", "file-texture"}, {"dds", "file-texture"},
      {"bmp", "file-texture"}, {"hdr", "file-texture"}, {"exr", "file-texture"}, {"glb", "file-model"}, {"gltf", "file-model"},
      {"fbx", "file-model"}, {"obj", "file-model"}, {"prefab", "file-prefab"}, {"wav", "file-audio"}, {"ogg", "file-audio"},
      {"mp3", "file-audio"}, {"flac", "file-audio"}, {"cube", "file-lut"}, {"json", "file-json"}, {"jsonl", "file-json"},
      {"meta", "file-json"}, {"glsl", "file-shader"}, {"hlsl", "file-shader"}, {"shader", "file-shader"}, {"vert", "file-shader"},
      {"frag", "file-shader"},
      // Lua, Python and graph files keep their coloured language strip (drawn by the caller).
      {"lua", ""}, {"py", ""}, {"graph", ""}, {"animgraph", ""},
  };
  for (const Map& m : kMap)
    if (ext == m.ext) return m.icon;
  return "file";
}

const char* ComponentIconName(const std::string& n) {
  struct Map { const char* name; const char* icon; };
  static const Map kMap[] = {
      {"Transform", "comp-transform"}, {"Mesh Renderer", "comp-mesh"}, {"Light", "comp-light"}, {"Camera", "comp-camera"},
      {"Camera Bookmark", "comp-bookmark"}, {"Reflection Probe", "comp-reflection-probe"}, {"Light Probe Volume", "comp-light-probe"},
      {"Sky", "comp-sky"}, {"Color Grading", "comp-color-grading"}, {"Blockout", "comp-blockout"},
  };
  for (const Map& m : kMap)
    if (n == m.name) return m.icon;
  return "comp-component";
}

} // namespace bdg
