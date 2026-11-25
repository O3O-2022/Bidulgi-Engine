#include "Terrain/TerrainView.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Render3D/MeshGen.h"
#include "Scene/Scene.h"
#include "Terrain/TerrainComponents.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace bdg {
namespace {
float Smooth(float a, float b, float x) {
  const float t = std::clamp((x - a) / std::max(b - a, 1e-4f), 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
float SrgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
Color Lin(const glm::vec3& srgb) { return {SrgbToLin(srgb.r), SrgbToLin(srgb.g), SrgbToLin(srgb.b), 1}; }

struct Frustum {
  glm::vec4 p[6];
  explicit Frustum(const glm::mat4& m) {
    const glm::mat4 t = glm::transpose(m);
    p[0] = t[3] + t[0];
    p[1] = t[3] - t[0];
    p[2] = t[3] + t[1];
    p[3] = t[3] - t[1];
    p[4] = t[3] + t[2];
    p[5] = t[3] - t[2];
  }
  bool Box(const glm::vec3& lo, const glm::vec3& hi) const {
    for (const glm::vec4& pl : p) {
      const glm::vec3 v(pl.x >= 0 ? hi.x : lo.x, pl.y >= 0 ? hi.y : lo.y, pl.z >= 0 ? hi.z : lo.z);
      if (glm::dot(glm::vec3(pl), v) + pl.w < 0) return false;
    }
    return true;
  }
};
float BoxDistance(const glm::vec3& p, const glm::vec3& lo, const glm::vec3& hi) { return glm::length(glm::max(glm::max(lo - p, p - hi), glm::vec3(0))); }
uint32_t HashF(float x, float z) {
  uint32_t h = (uint32_t)(int)std::floor(x * 7.31f) * 73856093u ^ (uint32_t)(int)std::floor(z * 7.31f) * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  return h ^ (h >> 15);
}
// A clump of three crossed blades (pivot at the bottom centre, 0.5 m tall), normals up so it lights like the ground.
MeshData MakeGrassClump() {
  MeshData m;
  for (int b = 0; b < 3; ++b) {
    const float a = b * 1.0472f + 0.3f, ca = std::cos(a), sa = std::sin(a);
    const float lean = 0.06f * (b - 1);
    const uint32_t base = (uint32_t)m.vertices.size();
    const float hs[3] = {0, 0.28f, 0.5f}, ws[3] = {0.07f, 0.05f, 0.f};
    for (int k = 0; k < 3; ++k) {
      for (int s = -1; s <= 1; s += 2) {
        Vertex3D v{};
        v.px = ca * ws[k] * s + lean * hs[k] * 2;
        v.pz = sa * ws[k] * s;
        v.py = hs[k];
        v.ny = 1;
        v.u = s < 0 ? 0.f : 1.f;
        v.v = hs[k] / 0.5f;
        v.tx = 1;
        v.tw = 1;
        m.vertices.push_back(v);
      }
    }
    for (uint32_t k = 0; k < 2; ++k) {
      const uint32_t i = base + k * 2;
      m.indices.insert(m.indices.end(), {i, i + 1, i + 2, i + 1, i + 3, i + 2});
    }
  }
  return m;
}
void Append(MeshData& to, MeshData from, glm::vec3 offset) {
  const uint32_t base = (uint32_t)to.vertices.size();
  for (Vertex3D& v : from.vertices) {
    v.px += offset.x;
    v.py += offset.y;
    v.pz += offset.z;
    to.vertices.push_back(v);
  }
  for (uint32_t i : from.indices) to.indices.push_back(base + i);
}
// The built-in tree: trunk 2.2 m, two crown cones up to 6 m (pivot at the bottom centre).
void MakeTree(int detail, MeshData& trunk, MeshData& crown) {
  trunk = MakeCylinder(0.16f, 2.2f, detail ? 9 : 5);
  crown = {};
  Append(crown, MakeCone(1.5f, 3.2f, detail ? 14 : 6), {0, 1.6f, 0});
  Append(crown, MakeCone(1.05f, 2.4f, detail ? 12 : 6), {0, 3.4f, 0});
}
// The far LOD card: the tree's silhouette drawn on the CPU (it is the side view of the meshes above), alpha-tested.
std::vector<uint8_t> TreeCard(int w, int h) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  const glm::vec3 bark(0.33f, 0.22f, 0.13f), leaf(0.20f, 0.38f, 0.15f);
  auto put = [&](int x, int y, glm::vec3 c, float shade) {
    uint8_t* p = &px[((size_t)y * w + x) * 4];
    p[0] = (uint8_t)std::lround(std::clamp(c.r * shade, 0.f, 1.f) * 255);
    p[1] = (uint8_t)std::lround(std::clamp(c.g * shade, 0.f, 1.f) * 255);
    p[2] = (uint8_t)std::lround(std::clamp(c.b * shade, 0.f, 1.f) * 255);
    p[3] = 255;
  };
  const float H = 5.8f, W = 3.2f; // metres the card covers (x: -1.6 .. 1.6, y: 0 .. 5.8)
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float mx = ((x + 0.5f) / w - 0.5f) * W, my = (1 - (y + 0.5f) / h) * H; // top row first
      const float side = 0.75f + 0.35f * (0.5f - (mx / W)); // lit from the left
      auto cone = [&](float base, float height, float r) { return my >= base && my <= base + height && std::fabs(mx) <= r * (1 - (my - base) / height); };
      if (cone(1.6f, 3.2f, 1.5f) || cone(3.4f, 2.4f, 1.05f)) put(x, y, leaf, side);
      else if (my <= 2.2f && std::fabs(mx) <= 0.16f) put(x, y, bark, side);
    }
  return px;
}
} // namespace

TerrainTierSettings TerrainTier(QualityTier t) {
  TerrainTierSettings s;
  switch (t) {
    case QualityTier::Potato: s = {0.5f, 256, false, 0.f, 0.5f, 0.5f, 0, 300, false}; break;
    case QualityTier::Low: s = {0.75f, 512, false, 0.2f, 0.75f, 0.7f, 4000, 800, true}; break;
    case QualityTier::Medium: s = {1.f, 1024, true, 0.5f, 1.f, 1.f, 15000, 2000, true}; break;
    case QualityTier::High: s = {1.5f, 2048, true, 1.f, 1.f, 1.5f, 40000, 4000, true}; break;
    case QualityTier::Ultra: s = {2.f, 2048, true, 1.f, 1.f, 2.f, 80000, 8000, true}; break;
  }
  return s;
}

void TerrainView::FoliageMeshes(RHI& rhi, Renderer3D& r) {
  if (foliageReady_) return;
  foliageReady_ = true;
  grass_ = r.CreateMesh(MakeGrassClump());
  MeshData trunk, crown;
  MakeTree(1, trunk, crown);
  trunk_ = r.CreateMesh(trunk);
  crown_ = r.CreateMesh(crown);
  MakeTree(0, trunk, crown);
  trunkLow_ = r.CreateMesh(trunk);
  crownLow_ = r.CreateMesh(crown);
  MeshData card = MakeQuad(3.2f, 5.8f); // XY plane, facing +Z, centred: lift so the pivot is the bottom
  for (Vertex3D& v : card.vertices) v.py += 2.9f;
  card_ = r.CreateMesh(card);
  TextureDesc d;
  d.w = 128;
  d.h = 256;
  d.format = TextureFormat::SRGBA8;
  d.mipmaps = true;
  const std::vector<uint8_t> px = TreeCard(d.w, d.h);
  cardTex_ = rhi.CreateTexture(d, px.data());
}

void TerrainView::Bake(Entry& e, const TerrainData& t, RHI& rhi, int x0, int z0, int x1, int z1, const std::string& assetsRoot) {
  const auto start = std::chrono::steady_clock::now();
  const int n = e.mapSize;
  // layer images (CPU, sRGB) once per path
  std::vector<const std::vector<uint8_t>*> img(t.layers.size(), nullptr);
  std::vector<glm::ivec2> imgSize(t.layers.size());
  for (size_t l = 0; l < t.layers.size(); ++l) {
    const std::string& tex = t.layers[l].texture;
    if (tex.empty()) continue;
    if (!e.textures.count(tex)) {
      std::vector<uint8_t> px;
      int32_t w = 0, h = 0;
      std::vector<std::filesystem::path> tries{PathU8(tex)};
      if (!assetsRoot.empty()) tries.insert(tries.begin(), PathU8(assetsRoot) / PathU8(tex));
      tries.push_back(PathU8(t.path).parent_path() / PathU8(tex));
      bool ok = false;
      for (const auto& p : tries)
        if (LoadImageRGBA(p, w, h, px)) { ok = true; break; }
      if (!ok) LogWarn("terrain: layer {} texture {} not found: its colour only", t.layers[l].name, tex);
      e.textures[tex] = ok ? std::move(px) : std::vector<uint8_t>();
      e.textureSize[tex] = {w, h};
    }
    if (!e.textures[tex].empty()) { img[l] = &e.textures[tex]; imgSize[l] = e.textureSize[tex]; }
  }
  // texel range covering the sample range (one sample of margin for the filtering)
  const float toTexel = (n - 1) / float(t.resolution - 1);
  const int tx0 = std::max(0, (int)std::floor((x0 - 1) * toTexel)), tx1 = std::min(n - 1, (int)std::ceil((x1 + 1) * toTexel));
  const int tz0 = std::max(0, (int)std::floor((z0 - 1) * toTexel)), tz1 = std::min(n - 1, (int)std::ceil((z1 + 1) * toTexel));
  const int w = tx1 - tx0 + 1, h = tz1 - tz0 + 1;
  if (w <= 0 || h <= 0) return;
  std::vector<uint8_t> color((size_t)w * h * 4), rough((size_t)w * h * 4);
  auto sample = [&](size_t l, float u, float v) -> glm::vec3 { // sRGB 0..1, repeating
    const glm::ivec2 sz = imgSize[l];
    const int x = ((int)std::floor(u * sz.x) % sz.x + sz.x) % sz.x, y = ((int)std::floor(v * sz.y) % sz.y + sz.y) % sz.y;
    const uint8_t* p = &(*img[l])[((size_t)y * sz.x + x) * 4];
    return glm::vec3(p[0], p[1], p[2]) / 255.f;
  };
  const size_t layers = t.layers.size();
  for (int tz = tz0; tz <= tz1; ++tz)
    for (int tx = tx0; tx <= tx1; ++tx) {
      const float u = tx / float(n - 1), v = tz / float(n - 1);
      const float wx = u * t.size.x, wz = v * t.size.y;
      const float wy = t.HeightAt(wx, wz);
      const glm::vec3 nrm = t.NormalAt(wx, wz);
      // splat weights, bilinear
      const float gx = u * (t.resolution - 1), gz = v * (t.resolution - 1);
      const int sx = std::min((int)gx, t.resolution - 2), sz = std::min((int)gz, t.resolution - 2);
      const float fx = gx - sx, fz = gz - sz;
      float wts[8] = {};
      float total = 0;
      for (size_t l = 0; l < layers; ++l) {
        const std::vector<uint8_t>& m = t.splat[l / 4];
        auto at = [&](int x, int z) { return m[((size_t)z * t.resolution + x) * 4 + l % 4] / 255.f; };
        const float a = at(sx, sz) + (at(sx + 1, sz) - at(sx, sz)) * fx, b = at(sx, sz + 1) + (at(sx + 1, sz + 1) - at(sx, sz + 1)) * fx;
        total += wts[l] = a + (b - a) * fz;
      }
      if (total <= 1e-4f) { wts[0] = 1; total = 1; }
      glm::vec3 c(0);
      float r = 0;
      // triplanar weights (steep slopes): the side projections take over where the ground is not flat
      glm::vec3 tw = glm::pow(glm::abs(nrm), glm::vec3(4));
      if (!e.triplanar || nrm.y > 0.8f) tw = {0, 1, 0};
      tw /= tw.x + tw.y + tw.z;
      for (size_t l = 0; l < layers; ++l) {
        if (wts[l] <= 0) continue;
        const TerrainLayer& L = t.layers[l];
        const float k = L.tiling / 10.f;
        glm::vec3 col;
        if (img[l]) {
          col = glm::vec3(0);
          if (tw.y > 0) col += sample(l, wx * k, wz * k) * tw.y;
          if (tw.x > 0) col += sample(l, wz * k, wy * k) * tw.x;
          if (tw.z > 0) col += sample(l, wx * k, wy * k) * tw.z;
          col = glm::min(col * L.color * 2.f, glm::vec3(1)); // tint around mid grey: colour 0.5 keeps the texture as it is
        } else { // no texture: the colour with a little variation so large areas are not flat
          const float n1 = (HashF(wx * k, wz * k) & 255) / 255.f, n2 = (HashF(wx * k * 0.25f + 13, wz * k * 0.25f + 7) & 255) / 255.f;
          col = L.color * (0.9f + 0.12f * n1 + 0.08f * (n2 - 0.5f));
        }
        c += col * (wts[l] / total);
        r += L.roughness * (wts[l] / total);
      }
      const size_t o = ((size_t)(tz - tz0) * w + (tx - tx0)) * 4;
      color[o] = (uint8_t)std::lround(std::clamp(c.r, 0.f, 1.f) * 255);
      color[o + 1] = (uint8_t)std::lround(std::clamp(c.g, 0.f, 1.f) * 255);
      color[o + 2] = (uint8_t)std::lround(std::clamp(c.b, 0.f, 1.f) * 255);
      color[o + 3] = 255;
      rough[o] = 255;
      rough[o + 1] = (uint8_t)std::lround(std::clamp(r, 0.f, 1.f) * 255);
      rough[o + 2] = 0;
      rough[o + 3] = 255;
    }
  if (!e.color.Valid()) {
    TextureDesc d;
    d.w = d.h = n;
    d.mipmaps = true;
    d.format = TextureFormat::SRGBA8;
    std::vector<uint8_t> blank((size_t)n * n * 4, 255);
    e.color = rhi.CreateTexture(d, blank.data());
    d.format = TextureFormat::RGBA8;
    e.rough = rhi.CreateTexture(d, blank.data());
  }
  rhi.UpdateTexture2D(e.color, tx0, tz0, w, h, TextureFormat::SRGBA8, color.data());
  rhi.UpdateTexture2D(e.rough, tx0, tz0, w, h, TextureFormat::RGBA8, rough.data());
  rhi.GenerateMips(e.color);
  rhi.GenerateMips(e.rough);
  stats_.bakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void TerrainView::Sync(Entry& e, TerrainData& t, RHI& rhi, Renderer3D& r, const TerrainTierSettings& tier, const std::string& assetsRoot) {
  const int cxN = TerrainChunksX(t), czN = TerrainChunksZ(t), lods = TerrainLods(t);
  const size_t chunks = (size_t)cxN * czN;
  if (e.meshes.size() != chunks * lods) {
    for (ChunkMesh& m : e.meshes) r.DestroyMesh(m.mesh);
    e.meshes.assign(chunks * lods, {});
    e.chunkRev.assign(chunks, t.revision);
    e.plantRev.assign(chunks, t.revision);
    e.plants.assign(chunks, {});
    e.boundsLo.assign(chunks, glm::vec3(0));
    e.boundsHi.assign(chunks, glm::vec3(0));
    for (int cz = 0; cz < czN; ++cz)
      for (int cx = 0; cx < cxN; ++cx) ChunkBounds(t, cx, cz, e.boundsLo[(size_t)cz * cxN + cx], e.boundsHi[(size_t)cz * cxN + cx]);
    e.bakedRevision = 0;
  }
  // a change since the last look: the touched chunks (and their neighbours, for normals and skirts) are stale
  if (t.dirtyX1 >= t.dirtyX0) {
    const int c0 = std::max(0, (t.dirtyX0 - 1) / kChunkQuads), c1 = std::min(cxN - 1, (t.dirtyX1 + 1) / kChunkQuads);
    const int r0 = std::max(0, (t.dirtyZ0 - 1) / kChunkQuads), r1 = std::min(czN - 1, (t.dirtyZ1 + 1) / kChunkQuads);
    for (int cz = r0; cz <= r1; ++cz)
      for (int cx = c0; cx <= c1; ++cx) {
        const size_t i = (size_t)cz * cxN + cx;
        if (t.heightDirty) {
          e.chunkRev[i] = t.revision;
          ChunkBounds(t, cx, cz, e.boundsLo[i], e.boundsHi[i]);
        }
        e.plantRev[i] = t.revision; // heights, layers and density all move plants
      }
    const int mapSize = std::min(tier.colorMap, (t.resolution - 1) * 8 + 1);
    if (e.color.Valid() && e.mapSize == mapSize && e.triplanar == tier.triplanar && e.bakedRevision != 0 && (t.splatDirty || t.heightDirty))
      Bake(e, t, rhi, t.dirtyX0, t.dirtyZ0, t.dirtyX1, t.dirtyZ1, assetsRoot); // only where it changed
    e.bakedRevision = e.bakedRevision ? t.revision : 0;
    t.ClearDirty();
  }
  const int mapSize = std::min(tier.colorMap, (t.resolution - 1) * 8 + 1);
  if (e.bakedRevision == 0 || e.mapSize != mapSize || e.triplanar != tier.triplanar) { // the whole colour map
    if (e.mapSize != mapSize) {
      rhi.DestroyTexture(e.color);
      rhi.DestroyTexture(e.rough);
      e.color = e.rough = {};
    }
    e.mapSize = mapSize;
    e.triplanar = tier.triplanar;
    Bake(e, t, rhi, 0, 0, t.resolution - 1, t.resolution - 1, assetsRoot);
    e.bakedRevision = t.revision;
  }
}

void TerrainView::Append(const Scene& scene, const std::string& assetsRoot, RHI& rhi, Renderer3D& r, const Camera3D& cam, float aspect,
                         const QualitySettings& quality, std::vector<DrawItem3D>& items) {
  const auto start = std::chrono::steady_clock::now();
  stats_ = Stats{.bakeMs = stats_.bakeMs};
  const TerrainTierSettings tier = TerrainTier(quality.tier);
  const glm::mat4 view = cam.View();
  const Frustum frustum(cam.Projection(aspect) * view);
  const float time = windTime >= 0 ? (float)windTime : std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
  ++frame_;
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
    const Component* tc = nullptr;
    for (const Component& c : o->components)
      if (c.type == &Terrain::StaticType()) tc = &c;
    if (!tc) continue;
    const Terrain& comp = *static_cast<const Terrain*>(tc->data);
    TerrainData* t = TerrainRegistry::Instance().Get(comp.asset, assetsRoot);
    if (!t) continue;
    Entry& e = entries_[t->path];
    e.seen = frame_;
    Sync(e, *t, rhi, r, tier, assetsRoot);
    ++stats_.terrains;
    const glm::vec3 origin(scene.WorldMatrix(o->id)[3]);
    const glm::mat4 place = glm::translate(glm::mat4(1.f), origin);
    const int cxN = TerrainChunksX(*t), czN = TerrainChunksZ(*t), lods = TerrainLods(*t);
    const float lodDistance = comp.lodDistance * tier.lodScale;
    const float skirt = 1.f + t->height * 0.03f;
    Material3D mat;
    mat.baseColor = {1, 1, 1, 1};
    mat.roughness = 1;
    mat.metallic = 1; // x map blue (0): not metal
    mat.baseMap = e.color;
    mat.metalRoughMap = e.rough;
    struct Visible { size_t chunk; float distance; };
    std::vector<Visible> visible;
    for (int cz = 0; cz < czN; ++cz)
      for (int cx = 0; cx < cxN; ++cx) {
        const size_t ci = (size_t)cz * cxN + cx;
        const glm::vec3 lo = e.boundsLo[ci] + origin - glm::vec3(0, skirt, 0), hi = e.boundsHi[ci] + origin;
        if (!frustum.Box(lo, hi)) { ++stats_.culled; continue; }
        const float dist = BoxDistance(cam.position, lo, hi);
        visible.push_back({ci, dist});
        const int lod = ChunkLod(dist, lodDistance, lods);
        ChunkMesh& cm = e.meshes[ci * lods + lod];
        if (!cm.mesh.indexCount || cm.revision != e.chunkRev[ci]) {
          r.DestroyMesh(cm.mesh);
          MeshData md;
          BuildChunkMesh(*t, cx, cz, lod, skirt, md);
          cm.mesh = r.CreateMesh(md);
          cm.revision = e.chunkRev[ci];
          cm.triangles = (int)md.indices.size() / 3;
        }
        DrawItem3D it;
        it.mesh = &cm.mesh;
        it.model = place;
        it.material = mat;
        items.push_back(it);
        ++stats_.chunks;
        ++stats_.lodCount[std::min(lod, 7)];
        stats_.triangles += cm.triangles;
      }
    // foliage: nearest visible chunks first, within each layer's fade distance, up to the tier's budgets
    if (!comp.foliage || t->foliage.empty()) continue;
    FoliageMeshes(rhi, r);
    std::sort(visible.begin(), visible.end(), [](const Visible& a, const Visible& b) { return a.distance < b.distance; });
    const float windAmount = tier.wind ? comp.wind : 0.f;
    for (size_t fl = 0; fl < t->foliage.size(); ++fl) {
      const FoliageLayer& L = t->foliage[fl];
      const bool tree = L.kind == FoliageKind::Tree;
      const float densityScale = tree ? tier.treeDensity : tier.grassDensity;
      if (densityScale <= 0) continue;
      const float fadeStart = L.fadeStart * tier.fadeScale, fadeEnd = L.fadeEnd * tier.fadeScale, impostor = L.impostor * tier.fadeScale;
      int& count = tree ? stats_.trees : stats_.grass;
      const int budget = tree ? tier.treeBudget : tier.grassBudget;
      Material3D fm;
      fm.baseColor = Lin(L.color);
      fm.roughness = 0.85f;
      fm.doubleSided = true;
      Material3D bark = fm, card = fm;
      bark.baseColor = Lin({0.33f, 0.22f, 0.13f});
      bark.doubleSided = false;
      card.baseColor = {1, 1, 1, 1};
      card.baseMap = cardTex_;
      card.alpha = Material3D::Alpha::Mask;
      for (const Visible& v : visible) {
        if (v.distance > fadeEnd) { ++stats_.foliageCulled; continue; }
        if (count >= budget) break;
        Plants& p = e.plants[v.chunk];
        if (p.perLayer.size() != t->foliage.size()) p.perLayer.assign(t->foliage.size(), {});
        const float densityKey = tier.grassDensity * 100.f + tier.treeDensity; // a tier change scatters again
        if (p.revision != e.plantRev[v.chunk] || p.density != densityKey) {
          const int cx = (int)(v.chunk % cxN), cz = (int)(v.chunk / cxN);
          for (size_t k = 0; k < t->foliage.size(); ++k)
            ScatterFoliage(*t, (int)k, cx, cz, t->foliage[k].kind == FoliageKind::Tree ? tier.treeDensity : tier.grassDensity, p.perLayer[k]);
          p.revision = e.plantRev[v.chunk];
          p.density = densityKey;
        }
        for (const FoliageInstance& in : p.perLayer[fl]) {
          if (count >= budget) break;
          const glm::vec3 wp = in.position + origin;
          const float d = glm::length(wp - cam.position);
          const float fade = 1 - Smooth(fadeStart, fadeEnd, d);
          if (fade <= 0.02f) continue;
          const float s = in.scale * fade;
          const float sway = windAmount * (tree ? 0.025f : 0.18f) * std::sin(time * (tree ? 0.9f : 1.9f) + in.phase + wp.x * 0.15f + wp.z * 0.1f);
          glm::mat4 shear(1.f); // top moves sideways, the base stays
          shear[1][0] = sway;
          shear[1][2] = sway * 0.6f;
          if (tree && d > impostor) { // LOD 2: a card turned to the camera
            const glm::vec3 to = cam.position - wp;
            const float yaw = std::atan2(to.x, to.z);
            DrawItem3D it;
            it.mesh = &card_;
            it.model = glm::translate(glm::mat4(1.f), wp) * glm::rotate(glm::mat4(1.f), yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.f), glm::vec3(s));
            it.material = card;
            items.push_back(it);
            ++stats_.impostors;
            ++count;
            continue;
          }
          const glm::mat4 m = glm::translate(glm::mat4(1.f), wp) * glm::rotate(glm::mat4(1.f), in.yaw, glm::vec3(0, 1, 0)) * shear * glm::scale(glm::mat4(1.f), glm::vec3(s));
          if (tree) {
            const bool low = d > impostor * 0.5f; // LOD 1
            stats_.treesLod1 += low;
            DrawItem3D trunk, crown;
            trunk.mesh = low ? &trunkLow_ : &trunk_;
            crown.mesh = low ? &crownLow_ : &crown_;
            trunk.model = crown.model = m;
            trunk.material = bark;
            crown.material = fm;
            items.push_back(trunk);
            items.push_back(crown);
          } else {
            DrawItem3D it;
            it.mesh = &grass_;
            it.model = m;
            it.material = fm;
            items.push_back(it);
          }
          ++count;
        }
      }
    }
  }
  // terrains no longer in the scene: free their GPU copies after a while
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (frame_ - it->second.seen > 300) {
      for (ChunkMesh& m : it->second.meshes) r.DestroyMesh(m.mesh);
      rhi.DestroyTexture(it->second.color);
      rhi.DestroyTexture(it->second.rough);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
  stats_.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void TerrainView::Shutdown(RHI& rhi, Renderer3D& r) {
  for (auto& [path, e] : entries_) {
    for (ChunkMesh& m : e.meshes) r.DestroyMesh(m.mesh);
    rhi.DestroyTexture(e.color);
    rhi.DestroyTexture(e.rough);
  }
  entries_.clear();
  if (foliageReady_) {
    for (Mesh3D* m : {&grass_, &trunk_, &crown_, &trunkLow_, &crownLow_, &card_}) r.DestroyMesh(*m);
    rhi.DestroyTexture(cardTex_);
    foliageReady_ = false;
  }
}
} // namespace bdg
