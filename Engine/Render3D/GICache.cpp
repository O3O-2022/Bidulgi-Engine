// Baked GI cache and diagnosis (GI Tier 0-D): Renderer3D::GIInputHash / SaveGI / LoadGI / DiagnoseGI.
//
// File (little endian, one per scene and input hash, e.g. .cache/gi/render-gi-<hash>.bdggi):
//   "BDGGI\0\0\0", u32 version, u64 input hash
//   u32 probes; per probe: position, box min, box max (3 f32 each), u8 box projection, f32 intensity, SH9 (27 f32);
//     then for each prefilter level: the probes' 6 faces (half floats, RGBA)
//   u8 volume; if 1: box min, box max, i32 count x3, f32 intensity, 36 f32 per probe (SH9 + validity in coefficient 0's w)
//   u32 lightmap instances; if > 0: i32 w, h, f32 density, f64 bake ms, i32 inside texels, atlas (half RGBA);
//     per instance: u64 content key, u32 vertex count, Vertex3D[], u32 index count, u32[];
//     u32 covered texels; per texel: u32 atlas index, position, normal (3 f32 each)
//   f64 probe bake ms
#include "Render3D/Renderer3D.h"
#include "Render3D/Lightmap.h"
#include "Render3D/RayScene.h"
#include "Core/Log.h"
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>

namespace bdg {
namespace {

constexpr uint32_t kGIVersion = 1;
constexpr char kMagic[8] = {'B', 'D', 'G', 'G', 'I', 0, 0, 0};

struct Fnv {
  uint64_t h = 1469598103934665603ull;
  void Bytes(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  }
  void F(float v) { Bytes(&v, 4); }
  void V(const glm::vec3& v) { F(v.x); F(v.y); F(v.z); }
  void C(Color c) { F(c.r); F(c.g); F(c.b); F(c.a); }
  void U(uint64_t v) { Bytes(&v, 8); }
};

struct Writer {
  std::ofstream f;
  template <class T> void Pod(const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
  void V(const glm::vec3& v) { Pod(v.x); Pod(v.y); Pod(v.z); }
  void Raw(const void* p, size_t n) { f.write(static_cast<const char*>(p), (std::streamsize)n); }
  void Halves(const float* p, size_t n) {
    std::vector<uint16_t> h(n);
    for (size_t i = 0; i < n; ++i) h[i] = glm::packHalf1x16(p[i]);
    Raw(h.data(), n * 2);
  }
};

struct Reader {
  std::ifstream f;
  template <class T> bool Pod(T& v) { return (bool)f.read(reinterpret_cast<char*>(&v), sizeof(T)); }
  bool V(glm::vec3& v) { return Pod(v.x) && Pod(v.y) && Pod(v.z); }
  bool Raw(void* p, size_t n) { return (bool)f.read(static_cast<char*>(p), (std::streamsize)n); }
  bool Halves(std::vector<float>& out, size_t n) {
    std::vector<uint16_t> h(n);
    if (!Raw(h.data(), n * 2)) return false;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = glm::unpackHalf1x16(h[i]);
    return true;
  }
};

std::string Mb(double bytes) { return bytes >= 1 << 20 ? std::format("{:.1f} MB", bytes / (1 << 20)) : std::format("{:.0f} KB", bytes / 1024); }

} // namespace

uint64_t Renderer3D::GIInputHash(const std::vector<DrawItem3D>& items, const Light3D& light, const LightmapSettings& s) const {
  Fnv h;
  h.U(kGIVersion);
  for (const DrawItem3D& it : items) {
    if (!it.mesh) continue;
    h.U(it.mesh->contentHash);
    h.Bytes(&it.model, sizeof(it.model));
    const Material3D& m = it.material;
    h.C(m.baseColor);
    h.F(m.metallic);
    h.F(m.roughness);
    h.C(m.emissive);
    h.U(((uint64_t)m.unlit << 0) | ((uint64_t)m.doubleSided << 1) | ((uint64_t)it.lightmapped << 2) | ((uint64_t)m.alpha << 4));
  }
  h.V(light.toLight);
  h.C(light.color);
  h.F(light.intensity);
  h.C(light.sky);
  h.C(light.ground);
  for (const LocalLight3D& l : light.locals) {
    h.V(l.position);
    h.V(l.direction);
    h.C(l.color);
    h.F(l.intensity);
    h.F(l.range);
    h.F(l.spot ? 1.f : 0.f);
    h.F(l.innerAngle);
    h.F(l.outerAngle);
  }
  const SkyBackground3D& b = light.background;
  h.F(b.draw ? 1.f : 0.f);
  h.C(b.zenith);
  h.C(b.horizon);
  h.C(b.ground);
  h.F(b.intensity);
  for (const ReflectionProbe3D& p : probes_) {
    h.V(p.position);
    h.V(p.boxMin);
    h.V(p.boxMax);
    h.F(p.boxProjection ? 1.f : 0.f);
    h.F(p.intensity);
  }
  if (hasVolume_) {
    h.V(volume_.boxMin);
    h.V(volume_.boxMax);
    h.Bytes(&volume_.count, sizeof(volume_.count));
    h.F(volume_.intensity);
  }
  h.F(s.texelsPerMeter);
  h.U((uint64_t)s.samples | ((uint64_t)s.bounces << 20) | ((uint64_t)s.maxSize << 32) | ((uint64_t)s.padding << 56));
  return h.h;
}

bool Renderer3D::SaveGI(const std::string& path, uint64_t inputHash, std::string* error) const {
  auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
  if (!rhi_) return fail("no renderer");
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  const std::string temp = path + ".tmp";
  {
    Writer w;
    w.f.open(temp, std::ios::binary | std::ios::trunc);
    if (!w.f) return fail("cannot write " + temp);
    w.Raw(kMagic, 8);
    w.Pod(kGIVersion);
    w.Pod(inputHash);
    const uint32_t probes = (uint32_t)bakedProbes_;
    w.Pod(probes);
    for (uint32_t i = 0; i < probes; ++i) {
      const ReflectionProbe3D& p = probes_[i];
      w.V(p.position);
      w.V(p.boxMin);
      w.V(p.boxMax);
      w.Pod((uint8_t)(p.boxProjection ? 1 : 0));
      w.Pod(p.intensity);
      w.Raw(probeSH_[i + 1], sizeof(probeSH_[i + 1]));
    }
    if (probes) {
      std::vector<float> level;
      for (int l = 0; l < kEnvLevels; ++l) {
        if (!rhi_->ReadTextureLevel(envArray_, l, level)) return fail("cannot read the probe cubes");
        const size_t face = (size_t)std::max(kEnvSize >> l, 1) * std::max(kEnvSize >> l, 1) * 4;
        w.Halves(level.data() + 6 * face, 6 * face * probes); // layer 0 is the sky: rebuilt from the light
      }
    }
    w.Pod((uint8_t)(hasVolume_ && volumeBaked_ ? 1 : 0));
    if (hasVolume_ && volumeBaked_) {
      w.V(volume_.boxMin);
      w.V(volume_.boxMax);
      w.Pod(volume_.count);
      w.Pod(volume_.intensity);
      w.Raw(volumeSH_.data(), volumeSH_.size() * sizeof(float));
    }
    const uint32_t instances = (uint32_t)lightmaps_.size();
    w.Pod(instances);
    if (instances) {
      w.Pod(lightmapW_);
      w.Pod(lightmapH_);
      w.Pod(lightmapDensity_);
      w.Pod(lightmapMs_);
      w.Pod(lightmapInside_);
      w.Halves(lightmapRGBA_.data(), lightmapRGBA_.size());
      for (const LightmapInstance& li : lightmaps_) {
        const MeshData& md = *li.mesh.source;
        w.Pod(li.key);
        w.Pod((uint32_t)md.vertices.size());
        w.Raw(md.vertices.data(), md.vertices.size() * sizeof(Vertex3D));
        w.Pod((uint32_t)md.indices.size());
        w.Raw(md.indices.data(), md.indices.size() * sizeof(uint32_t));
      }
      w.Pod((uint32_t)lmTexelIndex_.size());
      for (size_t i = 0; i < lmTexelIndex_.size(); ++i) {
        w.Pod(lmTexelIndex_[i]);
        w.V(lmTexelPos_[i]);
        w.V(lmTexelN_[i]);
      }
    }
    w.Pod(probeBakeMs_);
    if (!w.f) return fail("write failed: " + temp);
  }
  std::filesystem::rename(temp, path, ec); // whole files only: a crash mid-write leaves the old cache
  if (ec) return fail("cannot replace " + path + ": " + ec.message());
  return true;
}

bool Renderer3D::LoadGI(const std::string& path, const std::vector<DrawItem3D>& items, uint64_t inputHash, std::string* error) {
  auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
  if (!rhi_) return fail("no renderer");
  Reader r;
  r.f.open(path, std::ios::binary);
  if (!r.f) return fail("no file");
  char magic[8];
  uint32_t version = 0;
  uint64_t hash = 0;
  if (!r.Raw(magic, 8) || std::memcmp(magic, kMagic, 8) != 0 || !r.Pod(version) || version != kGIVersion || !r.Pod(hash))
    return fail("not a baked GI file of this version");
  if (hash != inputHash) return fail("baked for other inputs");
  // Read everything first; apply only a complete file.
  uint32_t probes = 0;
  if (!r.Pod(probes) || probes > (uint32_t)kMaxProbes) return fail("bad probe count");
  std::vector<ReflectionProbe3D> list(probes);
  std::vector<std::array<float, 27>> sh(probes);
  for (uint32_t i = 0; i < probes; ++i) {
    uint8_t box = 0;
    if (!r.V(list[i].position) || !r.V(list[i].boxMin) || !r.V(list[i].boxMax) || !r.Pod(box) || !r.Pod(list[i].intensity) ||
        !r.Raw(sh[i].data(), sizeof(float) * 27))
      return fail("truncated probes");
    list[i].boxProjection = box != 0;
  }
  std::vector<std::vector<float>> levels(probes ? kEnvLevels : 0);
  for (int l = 0; l < (int)levels.size(); ++l) {
    const size_t face = (size_t)std::max(kEnvSize >> l, 1) * std::max(kEnvSize >> l, 1) * 4;
    if (!r.Halves(levels[l], 6 * face * probes)) return fail("truncated probe cubes");
  }
  uint8_t hasVol = 0;
  ProbeVolume3D vol;
  std::vector<float> volSH;
  if (!r.Pod(hasVol)) return fail("truncated volume");
  if (hasVol) {
    if (!r.V(vol.boxMin) || !r.V(vol.boxMax) || !r.Pod(vol.count) || !r.Pod(vol.intensity)) return fail("truncated volume");
    if (glm::any(glm::lessThan(vol.count, glm::ivec3(1))) || glm::any(glm::greaterThan(vol.count, glm::ivec3(16)))) return fail("bad volume");
    volSH.resize((size_t)vol.count.x * vol.count.y * vol.count.z * 36);
    if (!r.Raw(volSH.data(), volSH.size() * sizeof(float))) return fail("truncated volume");
  }
  uint32_t instances = 0;
  if (!r.Pod(instances) || instances > 1u << 20) return fail("bad lightmap count");
  int w = 0, h = 0, inside = 0;
  float density = 0;
  double lmMs = 0;
  std::vector<float> atlas;
  std::vector<uint64_t> keys(instances);
  std::vector<MeshData> meshes(instances);
  std::vector<uint32_t> tIndex;
  std::vector<glm::vec3> tPos, tN;
  if (instances) {
    if (!r.Pod(w) || !r.Pod(h) || !r.Pod(density) || !r.Pod(lmMs) || !r.Pod(inside) || w <= 0 || h <= 0 || w > 16384 || h > 16384)
      return fail("bad lightmap");
    if (!r.Halves(atlas, (size_t)w * h * 4)) return fail("truncated lightmap");
    for (uint32_t k = 0; k < instances; ++k) {
      uint32_t vc = 0, ic = 0;
      if (!r.Pod(keys[k]) || !r.Pod(vc) || vc > 1u << 26) return fail("truncated lightmap meshes");
      meshes[k].vertices.resize(vc);
      if (!r.Raw(meshes[k].vertices.data(), vc * sizeof(Vertex3D)) || !r.Pod(ic) || ic > 1u << 28) return fail("truncated lightmap meshes");
      meshes[k].indices.resize(ic);
      if (!r.Raw(meshes[k].indices.data(), ic * sizeof(uint32_t))) return fail("truncated lightmap meshes");
    }
    uint32_t texels = 0;
    if (!r.Pod(texels) || texels > (uint32_t)w * h) return fail("bad texel list");
    tIndex.resize(texels);
    tPos.resize(texels);
    tN.resize(texels);
    for (uint32_t i = 0; i < texels; ++i)
      if (!r.Pod(tIndex[i]) || !r.V(tPos[i]) || !r.V(tN[i]) || tIndex[i] >= (uint32_t)w * h) return fail("truncated texels");
  }
  double probeMs = 0;
  if (!r.Pod(probeMs)) return fail("truncated");

  // Apply: probes, volume, lightmaps (matched to the items by mesh content and placement).
  SetProbes(list);
  for (uint32_t i = 0; i < probes; ++i) std::memcpy(probeSH_[i + 1], sh[i].data(), sizeof(float) * 27);
  for (int l = 0; l < (int)levels.size(); ++l) rhi_->UploadCubeFaces(envArray_, l, 6, 6 * (int)probes, levels[l].data());
  bakedProbes_ = (int)probes;
  if (hasVol) {
    SetVolume(&vol);
    volumeSH_ = std::move(volSH);
    const size_t n = volumeSH_.size() / 4;
    if (n > volumeCapacity_) {
      rhi_->DestroyBuffer(volumeBuffer_);
      volumeCapacity_ = n;
      volumeBuffer_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, volumeCapacity_ * 16, true);
    }
    rhi_->UpdateBuffer(volumeBuffer_, volumeSH_.data(), volumeSH_.size() * sizeof(float));
    volumeBaked_ = true;
    invalidVolumeProbes_ = 0;
    for (size_t i = 0; i < n / 9; ++i) invalidVolumeProbes_ += volumeSH_[i * 36 + 3] < 0.5f ? 1 : 0;
  } else {
    SetVolume(nullptr);
  }
  ClearLightmaps();
  if (instances) {
    std::unordered_map<uint64_t, uint32_t> byKey;
    for (uint32_t k = 0; k < instances; ++k) byKey[keys[k]] = k;
    for (const DrawItem3D& it : items) {
      if (!it.lightmapped || !it.mesh) continue;
      auto found = byKey.find(ContentKey(it.mesh, it.model));
      if (found == byKey.end()) continue;
      LightmapInstance li;
      li.mesh = CreateMesh(meshes[found->second]);
      li.source = it.mesh;
      li.model = it.model;
      li.key = found->first;
      lightmapIndex_[LightmapKey(li.source, li.model)] = (int)lightmaps_.size();
      lightmaps_.push_back(std::move(li));
    }
    TextureDesc td;
    td.w = w;
    td.h = h;
    td.format = TextureFormat::RGBA16F;
    lightmapTex_ = rhi_->CreateTexture(td, atlas.data());
    lightmapW_ = w;
    lightmapH_ = h;
    lightmapDensity_ = density;
    lightmapMs_ = lmMs;
    lightmapInside_ = inside;
    lightmapTexels_ = (int)tIndex.size();
    for (uint32_t idx : tIndex) lmTexelValue_.push_back({atlas[idx * 4], atlas[idx * 4 + 1], atlas[idx * 4 + 2]});
    lmTexelIndex_ = std::move(tIndex);
    lmTexelPos_ = std::move(tPos);
    lmTexelN_ = std::move(tN);
    lightmapRGBA_ = std::move(atlas);
  }
  probeBakeMs_ = probeMs;
  UploadProbes();
  return true;
}

std::string Renderer3D::DiagnoseGI(const std::vector<DrawItem3D>& items) const {
  std::vector<glm::vec3> corners;
  GatherOpaqueTriangles(items, corners);
  RayScene solids;
  solids.Build(std::move(corners));
  std::vector<std::string> fixes;
  auto at = [](const glm::vec3& p) { return std::format("({:.2f},{:.2f},{:.2f})", p.x, p.y, p.z); };
  std::string out;
  // reflection probes
  int insideProbes = 0;
  std::string where;
  for (int i = 0; i < (int)probes_.size(); ++i)
    if (BackfaceShare(solids, probes_[i].position) > 0.5f) {
      ++insideProbes;
      where += " " + at(probes_[i].position);
    }
  out += std::format("reflection probes {} · baked {} · inside objects {}{}\n", probes_.size(), bakedProbes_, insideProbes, where);
  if (insideProbes) fixes.push_back("move reflection probes out of objects (they capture the inside)");
  // volume: probes inside objects, and walls between neighbouring probes (light leaks through them)
  if (hasVolume_ && volumeBaked_) {
    const glm::ivec3 cnt = volume_.count;
    const glm::vec3 cell = (volume_.boxMax - volume_.boxMin) / glm::vec3(cnt);
    auto pos = [&](int x, int y, int z) { return volume_.boxMin + (glm::vec3(x, y, z) + 0.5f) * cell; };
    auto valid = [&](int x, int y, int z) { return volumeSH_[((size_t)(z * cnt.y + y) * cnt.x + x) * 36 + 3] > 0.5f; };
    std::string off;
    int offCount = 0;
    for (int z = 0; z < cnt.z; ++z)
      for (int y = 0; y < cnt.y; ++y)
        for (int x = 0; x < cnt.x; ++x)
          if (!valid(x, y, z) && ++offCount <= 4) off += " " + at(pos(x, y, z));
    out += std::format("volume {}x{}x{} = {} probes · cell {:.2f} x {:.2f} x {:.2f} m · valid {} · inside objects {} (switched off){}{}\n", cnt.x,
                       cnt.y, cnt.z, cnt.x * cnt.y * cnt.z, cell.x, cell.y, cell.z, cnt.x * cnt.y * cnt.z - offCount, offCount, off,
                       offCount > 4 ? " ..." : "");
    if (offCount * 4 > cnt.x * cnt.y * cnt.z) fixes.push_back("over a quarter of the volume probes are inside objects: shrink the volume to the open space");
    int walls = 0, thin = 0;
    std::string leakAt;
    for (int z = 0; z < cnt.z; ++z)
      for (int y = 0; y < cnt.y; ++y)
        for (int x = 0; x < cnt.x; ++x) {
          if (!valid(x, y, z)) continue;
          const glm::ivec3 steps[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
          for (const glm::ivec3& d : steps) {
            const glm::ivec3 q = glm::ivec3(x, y, z) + d;
            if (q.x >= cnt.x || q.y >= cnt.y || q.z >= cnt.z || !valid(q.x, q.y, q.z)) continue;
            const glm::vec3 a = pos(x, y, z), b = pos(q.x, q.y, q.z);
            const float len = glm::distance(a, b);
            const glm::vec3 dir = (b - a) / len;
            RayHit enter;
            if (!solids.Intersect(a, dir, len, enter) || enter.backface) continue;
            ++walls; // a surface between two probes that blend: the far side's light reaches this side
            RayHit exit;
            const glm::vec3 inPoint = a + dir * enter.t;
            float thickness = solids.Intersect(inPoint + dir * 1e-4f, dir, len, exit) && exit.backface ? exit.t : len;
            if (thickness < 0.2f && ++thin <= 3) leakAt += std::format(" {} wall {:.2f} m", at(inPoint), thickness);
          }
        }
    out += std::format("leaks {} suspected (walls thinner than 0.20 m between volume probes){} · {} probe pairs with an object between them\n",
                       thin, leakAt, walls);
    if (thin) fixes.push_back("thicken walls to 0.2 m or more, or use one volume per room, so probes do not blend through them");
  } else {
    out += hasVolume_ ? "volume not baked\n" : "volume none\n";
  }
  // lightmaps
  if (!lightmaps_.empty()) {
    const int covered = lightmapTexels_ + lightmapInside_;
    const float insideShare = covered ? (float)lightmapInside_ / covered : 0.f;
    size_t stale = 0, wanted = 0;
    for (const DrawItem3D& it : items) {
      if (!it.lightmapped) continue;
      ++wanted;
      auto found = lightmapIndex_.find(LightmapKey(it.mesh, it.model));
      if (found == lightmapIndex_.end() || lightmaps_[found->second].model != it.model) ++stale;
    }
    out += std::format("lightmaps {} objects · atlas {}x{} · {:.1f} texels/m · {} texels · inside objects {} ({:.0f}%) · not baked or moved {} of {}\n",
                       lightmaps_.size(), lightmapW_, lightmapH_, lightmapDensity_, lightmapTexels_, lightmapInside_, insideShare * 100, stale,
                       wanted);
    if (insideShare > 0.1f) fixes.push_back("lightmapped objects overlap (texels inside other objects): trim hidden faces or separate them");
    if (stale) fixes.push_back("bake again (Bake GI): some lightmapped objects moved or are new");
  } else {
    out += "lightmaps none\n";
  }
  const double envBytes = (double)kEnvSize * kEnvSize * 8 * 6 * (kMaxProbes + 1) * 4 / 3;
  out += std::format("memory reflection {} · volume {} · lightmap {}\n", Mb(envBytes), Mb((double)volumeSH_.size() * 4),
                     Mb((double)lightmapW_ * lightmapH_ * 8));
  out += std::format("bake lightmaps {:.0f} ms · probes {:.0f} ms (last pass) · cache {}\n", lightmapMs_, probeBakeMs_, giCacheNote.empty() ? "none" : giCacheNote);
  out += "fixes " + (fixes.empty() ? std::string("none") : fixes[0]);
  for (size_t i = 1; i < fixes.size(); ++i) out += " | " + fixes[i];
  return out;
}

} // namespace bdg
