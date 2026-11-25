#include "Terrain/TerrainData.h"
#include "Core/Command.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Render3D/MeshGen.h"
#include "Terrain/TerrainComponents.h"
#include <yyjson.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}
uint32_t Hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(Hash(seed) | 1) {}
  float Unit() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) * (1.f / 16777216.f);
  }
};
// Smooth value noise for the starter hills.
float ValueNoise(float x, float z, uint32_t seed) {
  const int xi = (int)std::floor(x), zi = (int)std::floor(z);
  const float fx = x - xi, fz = z - zi;
  auto h = [&](int a, int b) { return (Hash((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ seed) & 0xffff) / 65535.f; };
  const float sx = fx * fx * (3 - 2 * fx), sz = fz * fz * (3 - 2 * fz);
  const float a = h(xi, zi) + (h(xi + 1, zi) - h(xi, zi)) * sx, b = h(xi, zi + 1) + (h(xi + 1, zi + 1) - h(xi, zi + 1)) * sx;
  return a + (b - a) * sz;
}
bool IsPow2Plus1(int n) { return n >= 33 && n <= 4097 && ((n - 1) & (n - 2)) == 0; }
float Num(yyjson_val* v, float def) { return yyjson_is_num(v) ? (float)yyjson_get_num(v) : def; }
template <int N>
bool Vec(yyjson_val* v, glm::vec<N, float>& out) {
  if (!yyjson_is_arr(v) || yyjson_arr_size(v) < (size_t)N) return false;
  for (int i = 0; i < N; ++i) out[i] = (float)yyjson_get_num(yyjson_arr_get(v, i));
  return true;
}
std::string Q(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o + "\"";
}
bool Fail(std::string* e, std::string why) {
  if (e) *e = std::move(why);
  return false;
}
} // namespace

// ---- sampling ----
float TerrainData::HeightAtSample(int x, int z) const {
  x = std::clamp(x, 0, resolution - 1);
  z = std::clamp(z, 0, resolution - 1);
  return heights.empty() ? 0.f : heights[(size_t)z * resolution + x] * (height / 65535.f);
}
float TerrainData::HeightAt(float x, float z) const {
  const float gx = std::clamp(x / SampleSpacingX(), 0.f, float(resolution - 1)), gz = std::clamp(z / SampleSpacingZ(), 0.f, float(resolution - 1));
  const int x0 = std::min((int)gx, resolution - 2), z0 = std::min((int)gz, resolution - 2);
  const float fx = gx - x0, fz = gz - z0;
  const float a = HeightAtSample(x0, z0) + (HeightAtSample(x0 + 1, z0) - HeightAtSample(x0, z0)) * fx;
  const float b = HeightAtSample(x0, z0 + 1) + (HeightAtSample(x0 + 1, z0 + 1) - HeightAtSample(x0, z0 + 1)) * fx;
  return a + (b - a) * fz;
}
glm::vec3 TerrainData::NormalAt(float x, float z) const {
  const float dx = SampleSpacingX(), dz = SampleSpacingZ();
  const float hl = HeightAt(x - dx, z), hr = HeightAt(x + dx, z), hd = HeightAt(x, z - dz), hu = HeightAt(x, z + dz);
  return glm::normalize(glm::vec3((hl - hr) / (2 * dx), 1.f, (hd - hu) / (2 * dz)));
}
float TerrainData::LayerWeight(int layer, float x, float z) const {
  if (layer < 0 || layer / 4 >= (int)splat.size()) return 0;
  const int sx = std::clamp((int)std::lround(x / SampleSpacingX()), 0, resolution - 1), sz = std::clamp((int)std::lround(z / SampleSpacingZ()), 0, resolution - 1);
  return splat[layer / 4][((size_t)sz * resolution + sx) * 4 + layer % 4] / 255.f;
}
float TerrainData::Density(int f, float x, float z) const {
  if (f < 0 || f > 3 || density.empty()) return 0;
  const int sx = std::clamp((int)std::lround(x / SampleSpacingX()), 0, resolution - 1), sz = std::clamp((int)std::lround(z / SampleSpacingZ()), 0, resolution - 1);
  return density[((size_t)sz * resolution + sx) * 4 + f] / 255.f;
}
void TerrainData::MarkDirty(int x0, int z0, int x1, int z1, bool h, bool s, bool d) {
  if (dirtyX1 < dirtyX0) { dirtyX0 = x0; dirtyZ0 = z0; dirtyX1 = x1; dirtyZ1 = z1; }
  else { dirtyX0 = std::min(dirtyX0, x0); dirtyZ0 = std::min(dirtyZ0, z0); dirtyX1 = std::max(dirtyX1, x1); dirtyZ1 = std::max(dirtyZ1, z1); }
  heightDirty |= h;
  splatDirty |= s;
  densityDirty |= d;
  ++revision;
}
void TerrainData::ClearDirty() {
  dirtyX0 = dirtyZ0 = 0;
  dirtyX1 = dirtyZ1 = -1;
  heightDirty = splatDirty = densityDirty = false;
}

// ---- making, saving, loading ----
TerrainData MakeTerrain(const std::string& name, int resolution, glm::vec2 size, float height, float hills, uint32_t seed) {
  TerrainData t;
  t.name = name;
  t.resolution = IsPow2Plus1(resolution) ? resolution : 129;
  t.size = glm::max(size, glm::vec2(1));
  t.height = std::max(height, 0.1f);
  const size_t n = (size_t)t.resolution * t.resolution;
  t.heights.assign(n, 0);
  if (hills > 0)
    for (int z = 0; z < t.resolution; ++z)
      for (int x = 0; x < t.resolution; ++x) {
        const float wx = x * t.SampleSpacingX(), wz = z * t.SampleSpacingZ();
        float h = 0, amp = 0.5f, freq = 1.f / 40.f;
        for (int o = 0; o < 4; ++o, amp *= 0.5f, freq *= 2) h += amp * ValueNoise(wx * freq, wz * freq, seed + o);
        t.heights[(size_t)z * t.resolution + x] = (uint16_t)std::clamp(std::lround(h * hills / t.height * 65535.f), 0L, 65535L);
      }
  t.layers = {{"Grass", {0.30f, 0.42f, 0.17f}, 0.9f, {}, 4}, {"Rock", {0.42f, 0.40f, 0.37f}, 0.75f, {}, 4},
              {"Dirt", {0.40f, 0.30f, 0.20f}, 0.95f, {}, 4}, {"Sand", {0.76f, 0.70f, 0.52f}, 0.9f, {}, 4}};
  t.splat.assign(1, std::vector<uint8_t>(n * 4, 0));
  for (size_t i = 0; i < n; ++i) t.splat[0][i * 4] = 255;
  t.density.assign(n * 4, 255);
  return t;
}

bool SaveTerrain(const TerrainData& t, const std::string& path, std::string* error) {
  const fs::path p = PathU8(path), dir = p.parent_path();
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string stem = U(p.stem());
  // the map files are named in the header too, so "used by" and the build's unused-asset stripping see them
  std::string maps;
  for (size_t i = 0; i < t.splat.size(); ++i) maps += std::format("{}\"{}.splat{}.png\"", i ? "," : "", stem, i);
  std::string text = std::format("{{\"terrain\":{},\"resolution\":{},\"size\":[{},{}],\"height\":{},\"heightmap\":\"{}.height.r16\",\"splatmaps\":[{}],\"foliagemap\":\"{}.foliage.png\"}}\n",
                                 Q(t.name), t.resolution, t.size.x, t.size.y, t.height, stem, maps, stem);
  for (const TerrainLayer& l : t.layers) {
    text += std::format("{{\"layer\":{},\"color\":[{},{},{}],\"roughness\":{}", Q(l.name), l.color.r, l.color.g, l.color.b, l.roughness);
    if (!l.texture.empty()) text += std::format(",\"texture\":{},\"tiling\":{}", Q(l.texture), l.tiling);
    text += "}\n";
  }
  for (const FoliageLayer& f : t.foliage)
    text += std::format("{{\"foliage\":{},\"kind\":\"{}\",\"density\":{},\"layer\":{},\"scale\":[{},{}],\"fade\":[{},{}],\"impostor\":{},\"maxSlope\":{},\"color\":[{},{},{}]}}\n",
                        Q(f.name), f.kind == FoliageKind::Tree ? "tree" : "grass", f.density, f.layer, f.minScale, f.maxScale, f.fadeStart, f.fadeEnd,
                        f.impostor, f.maxSlope, f.color.r, f.color.g, f.color.b);
  {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) return Fail(error, "cannot write " + path);
  }
  {
    std::ofstream out(dir / PathU8(stem + ".height.r16"), std::ios::binary | std::ios::trunc);
    for (uint16_t h : t.heights) {
      const char b[2] = {(char)(h & 0xff), (char)(h >> 8)};
      out.write(b, 2);
    }
    if (!out) return Fail(error, "cannot write the heights of " + path);
  }
  for (size_t i = 0; i < t.splat.size(); ++i)
    if (!WritePNG(U(dir / PathU8(std::format("{}.splat{}.png", stem, i))).c_str(), t.resolution, t.resolution, t.splat[i].data(), false))
      return Fail(error, "cannot write the splat map of " + path);
  if (!t.density.empty() && !WritePNG(U(dir / PathU8(stem + ".foliage.png")).c_str(), t.resolution, t.resolution, t.density.data(), false))
    return Fail(error, "cannot write the foliage map of " + path);
  return true;
}

bool LoadTerrain(const std::string& path, TerrainData& out, std::vector<std::string>* problems, std::string* error) {
  std::string text;
  if (!ReadTextU8(path, text)) return Fail(error, "cannot open " + path);
  TerrainData t;
  t.path = U(fs::absolute(PathU8(path)));
  auto problem = [&](std::string s) { if (problems) problems->push_back(std::move(s)); };
  bool header = false;
  size_t pos = 0;
  int lineNo = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string_view line(text.data() + pos, end - pos);
    pos = end + 1;
    ++lineNo;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty()) continue;
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(line.data(), line.size(), 0), yyjson_doc_free);
    yyjson_val* o = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    if (!yyjson_is_obj(o)) { problem(std::format("line {}: not a JSON object", lineNo)); continue; }
    if (const char* name = yyjson_get_str(yyjson_obj_get(o, "terrain"))) {
      header = true;
      t.name = name;
      t.resolution = (int)Num(yyjson_obj_get(o, "resolution"), 129);
      glm::vec2 size(100);
      Vec<2>(yyjson_obj_get(o, "size"), size);
      t.size = glm::max(size, glm::vec2(1));
      t.height = std::max(Num(yyjson_obj_get(o, "height"), 20), 0.1f);
      if (!IsPow2Plus1(t.resolution)) return Fail(error, std::format("{}: resolution {} must be 2^n + 1 (33, 65, ..., 4097)", path, t.resolution));
    } else if (const char* lname = yyjson_get_str(yyjson_obj_get(o, "layer"))) {
      TerrainLayer l;
      l.name = lname;
      Vec<3>(yyjson_obj_get(o, "color"), l.color);
      l.roughness = std::clamp(Num(yyjson_obj_get(o, "roughness"), 0.85f), 0.f, 1.f);
      if (const char* tex = yyjson_get_str(yyjson_obj_get(o, "texture"))) l.texture = tex;
      l.tiling = std::max(Num(yyjson_obj_get(o, "tiling"), 4), 0.01f);
      if (t.layers.size() < 8) t.layers.push_back(l);
      else problem(std::format("line {}: at most 8 layers", lineNo));
    } else if (const char* fname = yyjson_get_str(yyjson_obj_get(o, "foliage"))) {
      FoliageLayer f;
      f.name = fname;
      const char* kind = yyjson_get_str(yyjson_obj_get(o, "kind"));
      f.kind = kind && std::string_view(kind) == "tree" ? FoliageKind::Tree : FoliageKind::Grass;
      if (f.kind == FoliageKind::Tree) { f.density = 0.01f; f.fadeStart = 160; f.fadeEnd = 200; f.color = {0.2f, 0.38f, 0.15f}; f.maxSlope = 30; }
      f.density = std::clamp(Num(yyjson_obj_get(o, "density"), f.density), 0.f, 64.f);
      f.layer = (int)Num(yyjson_obj_get(o, "layer"), -1);
      glm::vec2 s(f.minScale, f.maxScale), fade(f.fadeStart, f.fadeEnd);
      Vec<2>(yyjson_obj_get(o, "scale"), s);
      Vec<2>(yyjson_obj_get(o, "fade"), fade);
      f.minScale = std::max(std::min(s.x, s.y), 0.01f);
      f.maxScale = std::max(s.x, s.y);
      f.fadeStart = std::max(std::min(fade.x, fade.y), 1.f);
      f.fadeEnd = std::max(fade.y, f.fadeStart + 0.5f);
      f.impostor = std::max(Num(yyjson_obj_get(o, "impostor"), f.impostor), 1.f);
      f.maxSlope = std::clamp(Num(yyjson_obj_get(o, "maxSlope"), f.maxSlope), 0.f, 90.f);
      Vec<3>(yyjson_obj_get(o, "color"), f.color);
      if (t.foliage.size() < 4) t.foliage.push_back(f);
      else problem(std::format("line {}: at most 4 foliage layers", lineNo));
    } else {
      problem(std::format("line {}: needs \"terrain\", \"layer\" or \"foliage\"", lineNo));
    }
  }
  if (!header) return Fail(error, path + ": no {\"terrain\":...} header line");
  if (t.layers.empty()) t.layers.push_back(TerrainLayer{});
  const fs::path p = PathU8(path), dir = p.parent_path();
  const std::string stem = U(p.stem());
  const size_t n = (size_t)t.resolution * t.resolution;
  t.heights.assign(n, 0);
  {
    std::ifstream in(dir / PathU8(stem + ".height.r16"), std::ios::binary);
    std::vector<char> raw(n * 2);
    if (in && in.read(raw.data(), (std::streamsize)raw.size()))
      for (size_t i = 0; i < n; ++i) t.heights[i] = (uint16_t)((uint8_t)raw[i * 2] | ((uint8_t)raw[i * 2 + 1] << 8));
    else
      problem(stem + ".height.r16 missing or too short: flat");
  }
  const size_t maps = (t.layers.size() + 3) / 4;
  for (size_t m = 0; m < maps; ++m) {
    int32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    if (LoadImageRGBA(dir / PathU8(std::format("{}.splat{}.png", stem, m)), w, h, px) && w == t.resolution && h == t.resolution) {
      t.splat.push_back(std::move(px));
    } else {
      std::vector<uint8_t> blank(n * 4, 0);
      if (m == 0)
        for (size_t i = 0; i < n; ++i) blank[i * 4] = 255;
      problem(std::format("{}.splat{}.png missing or not {}x{}: layer {} everywhere", stem, m, t.resolution, t.resolution, m * 4 + 1));
      t.splat.push_back(std::move(blank));
    }
  }
  {
    int32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    if (LoadImageRGBA(dir / PathU8(stem + ".foliage.png"), w, h, px) && w == t.resolution && h == t.resolution) t.density = std::move(px);
    else t.density.assign(n * 4, 255);
  }
  out = std::move(t);
  return true;
}

// ---- brushes ----
void ApplyTerrainBrush(TerrainData& t, TerrainBrushMode mode, glm::vec2 c, float radius, float strength, int layer, float flattenHeight) {
  if (radius <= 0 || strength <= 0 || t.heights.empty()) return;
  const float sx = t.SampleSpacingX(), sz = t.SampleSpacingZ();
  const int x0 = std::max(0, (int)std::floor((c.x - radius) / sx)), x1 = std::min(t.resolution - 1, (int)std::ceil((c.x + radius) / sx));
  const int z0 = std::max(0, (int)std::floor((c.y - radius) / sz)), z1 = std::min(t.resolution - 1, (int)std::ceil((c.y + radius) / sz));
  if (x0 > x1 || z0 > z1) return;
  const float scale = 65535.f / t.height;
  std::vector<uint16_t> before;
  if (mode == TerrainBrushMode::Smooth) before = t.heights;
  const bool heightsMode = mode == TerrainBrushMode::Raise || mode == TerrainBrushMode::Lower || mode == TerrainBrushMode::Smooth || mode == TerrainBrushMode::Flatten;
  for (int z = z0; z <= z1; ++z)
    for (int x = x0; x <= x1; ++x) {
      const float d = glm::length(glm::vec2(x * sx, z * sz) - c) / radius;
      if (d >= 1) continue;
      const float fall = 1 - d * d * (3 - 2 * d); // smoothstep falloff
      const float w = std::clamp(fall * strength, 0.f, 1.f);
      const size_t i = (size_t)z * t.resolution + x;
      if (heightsMode) {
        float h = t.heights[i] / scale;
        if (mode == TerrainBrushMode::Raise) h += w * t.height * 0.05f;
        else if (mode == TerrainBrushMode::Lower) h -= w * t.height * 0.05f;
        else if (mode == TerrainBrushMode::Flatten) h += (flattenHeight - h) * w;
        else { // smooth: toward the 3x3 average
          float sum = 0;
          int cnt = 0;
          for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
              const int xx = std::clamp(x + dx, 0, t.resolution - 1), zz = std::clamp(z + dz, 0, t.resolution - 1);
              sum += before[(size_t)zz * t.resolution + xx];
              ++cnt;
            }
          h += (sum / cnt / scale - h) * w;
        }
        t.heights[i] = (uint16_t)std::clamp(std::lround(h * scale), 0L, 65535L);
      } else if (mode == TerrainBrushMode::Paint) {
        if (layer < 0 || layer >= (int)t.layers.size()) return;
        // move weight toward the layer, keep the sum at 255
        float wts[8] = {};
        float total = 0;
        for (size_t l = 0; l < t.layers.size(); ++l) total += wts[l] = t.splat[l / 4][i * 4 + l % 4];
        if (total <= 0) wts[layer] = total = 255;
        for (size_t l = 0; l < t.layers.size(); ++l) wts[l] = wts[l] / total * (1 - w) + ((int)l == layer ? w : 0.f);
        int sum = 0, biggest = 0;
        for (size_t l = 0; l < t.layers.size(); ++l) {
          const int v = (int)std::lround(wts[l] * 255);
          t.splat[l / 4][i * 4 + l % 4] = (uint8_t)v;
          sum += v;
          if (wts[l] > wts[biggest]) biggest = (int)l;
        }
        t.splat[biggest / 4][i * 4 + biggest % 4] = (uint8_t)std::clamp(t.splat[biggest / 4][i * 4 + biggest % 4] + 255 - sum, 0, 255);
      } else if (layer >= 0 && layer < 4 && !t.density.empty()) {
        uint8_t& v = t.density[i * 4 + layer];
        const float cur = v / 255.f, target = mode == TerrainBrushMode::FoliagePaint ? 1.f : 0.f;
        v = (uint8_t)std::lround((cur + (target - cur) * w) * 255);
      }
    }
  t.MarkDirty(x0, z0, x1, z1, heightsMode, mode == TerrainBrushMode::Paint, mode == TerrainBrushMode::FoliagePaint || mode == TerrainBrushMode::FoliageErase);
}

// ---- chunks ----
int TerrainChunksX(const TerrainData& t) { return (t.resolution - 1 + kChunkQuads - 1) / kChunkQuads; }
int TerrainChunksZ(const TerrainData& t) { return TerrainChunksX(t); }
int TerrainLods(const TerrainData& t) {
  int lods = 1;
  while ((std::min(kChunkQuads, t.resolution - 1) >> lods) >= 2) ++lods;
  return lods;
}
int ChunkLod(float distance, float lodDistance, int lods) {
  if (distance <= lodDistance || lodDistance <= 0) return 0;
  return std::min(lods - 1, (int)std::floor(std::log2(distance / lodDistance)) + 1);
}

void ChunkBounds(const TerrainData& t, int cx, int cz, glm::vec3& lo, glm::vec3& hi) {
  const int x0 = cx * kChunkQuads, z0 = cz * kChunkQuads, x1 = std::min(x0 + kChunkQuads, t.resolution - 1), z1 = std::min(z0 + kChunkQuads, t.resolution - 1);
  float mn = 1e30f, mx = -1e30f;
  for (int z = z0; z <= z1; ++z)
    for (int x = x0; x <= x1; ++x) {
      const float h = t.HeightAtSample(x, z);
      mn = std::min(mn, h);
      mx = std::max(mx, h);
    }
  lo = {x0 * t.SampleSpacingX(), mn, z0 * t.SampleSpacingZ()};
  hi = {x1 * t.SampleSpacingX(), mx, z1 * t.SampleSpacingZ()};
}

void BuildChunkMesh(const TerrainData& t, int cx, int cz, int lod, float skirt, MeshData& out) {
  out.vertices.clear();
  out.indices.clear();
  const int step = 1 << lod;
  const int x0 = cx * kChunkQuads, z0 = cz * kChunkQuads, x1 = std::min(x0 + kChunkQuads, t.resolution - 1), z1 = std::min(z0 + kChunkQuads, t.resolution - 1);
  std::vector<int> xs, zs;
  for (int x = x0; x < x1; x += step) xs.push_back(x);
  xs.push_back(x1);
  for (int z = z0; z < z1; z += step) zs.push_back(z);
  zs.push_back(z1);
  const float sx = t.SampleSpacingX(), sz = t.SampleSpacingZ();
  auto vertex = [&](int x, int z, float drop) {
    Vertex3D v{};
    v.px = x * sx;
    v.pz = z * sz;
    v.py = t.HeightAtSample(x, z) - drop;
    const float hl = t.HeightAtSample(x - step, z), hr = t.HeightAtSample(x + step, z), hd = t.HeightAtSample(x, z - step), hu = t.HeightAtSample(x, z + step);
    const glm::vec3 n = glm::normalize(glm::vec3((hl - hr) / (2 * sx * step), 1.f, (hd - hu) / (2 * sz * step)));
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
    v.u = x / float(t.resolution - 1);
    v.v = z / float(t.resolution - 1);
    v.tx = 1; // tangent along +X
    v.tw = 1;
    return v;
  };
  const uint32_t w = (uint32_t)xs.size(), h = (uint32_t)zs.size();
  for (int z : zs)
    for (int x : xs) out.vertices.push_back(vertex(x, z, 0));
  for (uint32_t j = 0; j + 1 < h; ++j)
    for (uint32_t i = 0; i + 1 < w; ++i) {
      const uint32_t a = j * w + i, b = a + 1, c = a + w, d = c + 1;
      out.indices.insert(out.indices.end(), {a, c, b, b, c, d}); // CCW seen from above (+Y)
    }
  if (skirt > 0) { // a strip hanging down along each edge: hides cracks next to coarser / finer chunks
    auto edge = [&](std::vector<uint32_t> ring) {
      const uint32_t base = (uint32_t)out.vertices.size();
      for (uint32_t k : ring) {
        Vertex3D v = out.vertices[k];
        v.py -= skirt;
        out.vertices.push_back(v);
      }
      for (uint32_t k = 0; k + 1 < ring.size(); ++k) {
        const uint32_t a = ring[k], b = ring[k + 1], c = base + k, d = base + k + 1;
        out.indices.insert(out.indices.end(), {a, b, c, b, d, c}); // both windings: seen from either side
        out.indices.insert(out.indices.end(), {a, c, b, b, c, d});
      }
    };
    std::vector<uint32_t> top, bottom, left, right;
    for (uint32_t i = 0; i < w; ++i) { top.push_back(i); bottom.push_back((h - 1) * w + i); }
    for (uint32_t j = 0; j < h; ++j) { left.push_back(j * w); right.push_back(j * w + w - 1); }
    edge(top);
    edge(bottom);
    edge(left);
    edge(right);
  }
}

// ---- foliage ----
void ScatterFoliage(const TerrainData& t, int f, int cx, int cz, float densityScale, std::vector<FoliageInstance>& out) {
  out.clear();
  if (f < 0 || f >= (int)t.foliage.size()) return;
  const FoliageLayer& L = t.foliage[f];
  const float x0 = cx * kChunkQuads * t.SampleSpacingX(), z0 = cz * kChunkQuads * t.SampleSpacingZ();
  const float x1 = std::min((cx + 1) * kChunkQuads, t.resolution - 1) * t.SampleSpacingX();
  const float z1 = std::min((cz + 1) * kChunkQuads, t.resolution - 1) * t.SampleSpacingZ();
  const float area = (x1 - x0) * (z1 - z0);
  Rng rng((uint32_t)cx * 92821u ^ (uint32_t)cz * 68917u ^ (uint32_t)f * 7919u ^ 0x5eedu);
  const float expected = L.density * area * std::max(densityScale, 0.f);
  const int count = (int)expected + (rng.Unit() < expected - std::floor(expected) ? 1 : 0);
  const float cosMax = std::cos(glm::radians(L.maxSlope));
  for (int k = 0; k < count; ++k) {
    // every candidate draws the same numbers whatever is accepted: edits elsewhere never move other plants
    const float x = x0 + rng.Unit() * (x1 - x0), z = z0 + rng.Unit() * (z1 - z0);
    const float keep = rng.Unit(), s = rng.Unit(), yaw = rng.Unit(), phase = rng.Unit();
    float p = t.Density(f, x, z);
    if (L.layer >= 0) p *= t.LayerWeight(L.layer, x, z);
    if (keep >= p) continue;
    if (t.NormalAt(x, z).y < cosMax) continue;
    out.push_back({{x, t.HeightAt(x, z), z}, L.minScale + (L.maxScale - L.minScale) * s, yaw * 6.2831853f, phase * 6.2831853f});
  }
}

// ---- base64 ----
std::string Base64Encode(const uint8_t* d, size_t n) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  o.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += k[v >> 18 & 63];
    o += k[v >> 12 & 63];
    o += i + 1 < n ? k[v >> 6 & 63] : '=';
    o += i + 2 < n ? k[v & 63] : '=';
  }
  return o;
}
bool Base64Decode(std::string_view s, std::vector<uint8_t>& out) {
  out.clear();
  uint32_t buf = 0;
  int bits = 0;
  for (char c : s) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else if (c == '=') break;
    else return false;
    buf = buf << 6 | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)(buf >> bits));
    }
  }
  return true;
}

// ---- regions and the command ----
namespace {
// bytes per sample and the array of a map name
bool MapOf(TerrainData& t, const std::string& map, uint8_t*& data, int& bytes) {
  if (map == "height") { data = reinterpret_cast<uint8_t*>(t.heights.data()); bytes = 2; return !t.heights.empty(); }
  if (map == "density") { data = t.density.data(); bytes = 4; return !t.density.empty(); }
  if (map.rfind("splat", 0) == 0) {
    const size_t i = (size_t)std::atoi(map.c_str() + 5);
    if (i >= t.splat.size()) return false;
    data = t.splat[i].data();
    bytes = 4;
    return true;
  }
  return false;
}
} // namespace

std::string TerrainRegionBase64(const TerrainData& tc, const std::string& map, int x, int z, int w, int h) {
  TerrainData& t = const_cast<TerrainData&>(tc);
  uint8_t* data = nullptr;
  int bytes = 0;
  if (!MapOf(t, map, data, bytes) || w <= 0 || h <= 0 || x < 0 || z < 0 || x + w > t.resolution || z + h > t.resolution) return {};
  std::vector<uint8_t> region;
  region.reserve((size_t)w * h * bytes);
  for (int j = 0; j < h; ++j) {
    const uint8_t* row = data + ((size_t)(z + j) * t.resolution + x) * bytes; // heights: little-endian in memory (x86, ARM)
    region.insert(region.end(), row, row + (size_t)w * bytes);
  }
  return Base64Encode(region.data(), region.size());
}

bool SetTerrainRegion(TerrainData& t, const std::string& map, int x, int z, int w, int h, const std::string& b64, std::string* error) {
  uint8_t* data = nullptr;
  int bytes = 0;
  if (!MapOf(t, map, data, bytes)) return Fail(error, "terrain: no map \"" + map + "\"");
  if (x < 0 || z < 0 || w <= 0 || h <= 0 || x + w > t.resolution || z + h > t.resolution) return Fail(error, "terrain: region outside the map");
  std::vector<uint8_t> region;
  if (!Base64Decode(b64, region) || region.size() != (size_t)w * h * bytes) return Fail(error, "terrain: region data has the wrong size");
  for (int j = 0; j < h; ++j) std::copy_n(region.data() + (size_t)j * w * bytes, (size_t)w * bytes, data + ((size_t)(z + j) * t.resolution + x) * bytes);
  t.MarkDirty(x, z, x + w - 1, z + h - 1, map == "height", map.rfind("splat", 0) == 0, map == "density");
  return true;
}

void RegisterTerrainCommand() {
  static bool done = false;
  if (done) return;
  done = true;
  CommandBus::RegisterHandler(
      "terrain",
      [](Scene& scene, Command& cmd, Command& inv, std::string* error) {
        std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(cmd.args.data(), cmd.args.size(), 0), yyjson_doc_free);
        yyjson_val* a = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
        auto str = [&](const char* k) { const char* s = yyjson_get_str(yyjson_obj_get(a, k)); return std::string(s ? s : ""); };
        auto num = [&](const char* k) { return (int)yyjson_get_num(yyjson_obj_get(a, k)); };
        std::string asset = str("asset"), map = str("map"), data = str("data");
        if (asset.empty() && !cmd.target.empty()) // the object's Terrain.asset
          if (SceneObject* o = scene.Find(cmd.target))
            if (Component* c = o->Find("Terrain")) asset = static_cast<const Terrain*>(c->data)->asset;
        std::string err;
        TerrainData* t = TerrainRegistry::Instance().Get(asset, {}, &err);
        if (!t) return Fail(error, "terrain: " + err);
        const int x = num("x"), z = num("z"), w = num("w"), h = num("h");
        const std::string before = TerrainRegionBase64(*t, map, x, z, w, h);
        if (before.empty()) return Fail(error, "terrain: bad region or map");
        if (!SetTerrainRegion(*t, map, x, z, w, h, data, error)) return false;
        if (!SaveTerrain(*t, t->path, &err)) LogWarn("terrain: {}", err); // the files follow every edit (and its undo)
        inv.name = "terrain";
        inv.target = cmd.target;
        inv.args = std::format(R"({{"asset":{},"map":{},"x":{},"z":{},"w":{},"h":{},"data":"{}"}})", Q(asset), Q(map), x, z, w, h, before);
        return true;
      },
      [](const Scene& scene, const Command& cmd) {
        const SceneObject* o = scene.Find(cmd.target);
        const bool paint = cmd.args.find("\"map\":\"splat") != std::string::npos, foliage = cmd.args.find("\"map\":\"density\"") != std::string::npos;
        return std::format("{}: {}", o ? o->name : std::string("Terrain"), paint ? "paint layer" : foliage ? "paint foliage" : "sculpt");
      });
}

// ---- registry ----
TerrainRegistry& TerrainRegistry::Instance() {
  static TerrainRegistry r;
  return r;
}
std::string TerrainRegistry::Resolve(const std::string& asset, const std::string& root) const {
  std::error_code ec;
  if (asset.empty()) return {};
  const fs::path a = PathU8(asset);
  if (a.is_absolute()) return fs::exists(a, ec) ? U(a) : std::string();
  std::vector<fs::path> roots;
  if (!root.empty()) roots.push_back(PathU8(root));
  roots.push_back(fs::current_path(ec) / "Assets");
  if (const char* extra = std::getenv("BDG_ASSETS")) roots.push_back(PathU8(extra));
  for (const fs::path& r : roots)
    if (fs::exists(r / a, ec)) return U(fs::absolute(r / a, ec));
  return {};
}
TerrainData* TerrainRegistry::Get(const std::string& asset, const std::string& root, std::string* error) {
  const std::string path = Resolve(asset, root);
  if (path.empty()) {
    if (error) *error = "no terrain asset \"" + asset + "\"";
    return nullptr;
  }
  if (auto it = loaded_.find(path); it != loaded_.end()) return it->second.get();
  if (auto it = failed_.find(path); it != failed_.end()) {
    if (error) *error = it->second;
    return nullptr;
  }
  auto t = std::make_unique<TerrainData>();
  std::vector<std::string> problems;
  std::string err;
  if (!LoadTerrain(path, *t, &problems, &err)) {
    LogWarn("terrain: {}", err);
    failed_[path] = err;
    if (error) *error = err;
    return nullptr;
  }
  for (const std::string& p : problems) LogWarn("terrain: {}: {}", path, p);
  t->path = path;
  return (loaded_[path] = std::move(t)).get();
}
void TerrainRegistry::Forget(const std::string& p) {
  loaded_.erase(p);
  failed_.erase(p);
}
void TerrainRegistry::Clear() {
  loaded_.clear();
  failed_.clear();
}
} // namespace bdg
