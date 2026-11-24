#include "Render3D/Lightmap.h"
#include "Render3D/RayScene.h"
#include <xatlas.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace bdg {
namespace {

struct Tri {             // per ray-scene triangle
  glm::vec3 n[3];        // world vertex normals
  glm::vec2 luv[3];      // atlas texel coordinates (lightmapped triangles)
  glm::vec3 albedo{0}, emissive{0};
  int instance = -1;     // lightmap instance, -1 = none
};

uint32_t Hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
float Rand(uint32_t& s) { // PCG step, [0, 1)
  s = s * 747796405u + 2891336453u;
  uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
  return (float)(((w >> 22u) ^ w) >> 8) * (1.f / 16777216.f);
}
glm::vec3 Rgb(Color c) { return {c.r, c.g, c.b}; }

} // namespace

void GatherOpaqueTriangles(const std::vector<DrawItem3D>& items, std::vector<glm::vec3>& corners) {
  for (const DrawItem3D& it : items) {
    if (!it.mesh || !it.mesh->source || it.material.alpha == Material3D::Alpha::Blend) continue;
    const MeshData& src = *it.mesh->source;
    for (size_t t = 0; t + 2 < src.indices.size(); t += 3)
      for (int c = 0; c < 3; ++c) {
        const Vertex3D& v = src.vertices[src.indices[t + c]];
        corners.push_back(glm::vec3(it.model * glm::vec4(v.px, v.py, v.pz, 1)));
      }
  }
}

float BackfaceShare(const RayScene& scene, const glm::vec3& p) {
  const int n = 64;
  int back = 0;
  for (int i = 0; i < n; ++i) { // Fibonacci sphere
    const float y = 1 - (i + 0.5f) * 2.f / n, r = std::sqrt(std::max(1 - y * y, 0.f)), phi = i * 2.39996323f;
    RayHit hit;
    if (scene.Intersect(p, {r * std::cos(phi), y, r * std::sin(phi)}, 1e30f, hit) && hit.backface) ++back;
  }
  return (float)back / n;
}

bool BakeLightmapAtlas(const LightmapBakeInput& in, const LightmapSettings& s, LightmapResult& out, std::string* error) {
  const auto t0 = std::chrono::steady_clock::now();
  out = {};
  auto fail = [&](const char* why) { if (error) *error = why; return false; };
  if (!in.items || !in.light) return fail("no items or light");
  const std::vector<DrawItem3D>& items = *in.items;
  const Light3D& light = *in.light;
  out.itemInstance.assign(items.size(), -1);

  // 1) lightmapped instances in world space (the unwrap sees real sizes, so every object gets the same density)
  struct Inst { size_t item; std::vector<glm::vec3> wp, wn; };
  std::vector<Inst> insts;
  auto usable = [&](const DrawItem3D& it) {
    return it.mesh && it.mesh->source && !it.mesh->source->indices.empty() && it.material.alpha != Material3D::Alpha::Blend;
  };
  for (size_t i = 0; i < items.size(); ++i) {
    const DrawItem3D& it = items[i];
    if (!it.lightmapped || !usable(it)) continue;
    Inst x;
    x.item = i;
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(it.model)));
    for (const Vertex3D& v : it.mesh->source->vertices) {
      x.wp.push_back(glm::vec3(it.model * glm::vec4(v.px, v.py, v.pz, 1)));
      glm::vec3 n = nm * glm::vec3(v.nx, v.ny, v.nz);
      x.wn.push_back(glm::dot(n, n) > 1e-12f ? glm::normalize(n) : glm::vec3(0, 1, 0));
    }
    insts.push_back(std::move(x));
  }
  if (insts.empty()) return fail("no lightmapped objects with mesh data");

  // 2) unwrap and pack (xatlas); lower the density until the atlas fits
  xatlas::Atlas* atlas = xatlas::Create();
  for (const Inst& x : insts) {
    const MeshData& src = *items[x.item].mesh->source;
    xatlas::MeshDecl d;
    d.vertexPositionData = x.wp.data();
    d.vertexPositionStride = sizeof(glm::vec3);
    d.vertexNormalData = x.wn.data();
    d.vertexNormalStride = sizeof(glm::vec3);
    d.vertexCount = (uint32_t)x.wp.size();
    d.indexData = src.indices.data();
    d.indexCount = (uint32_t)src.indices.size();
    d.indexFormat = xatlas::IndexFormat::UInt32;
    if (xatlas::AddMesh(atlas, d, (uint32_t)insts.size()) != xatlas::AddMeshError::Success) {
      xatlas::Destroy(atlas);
      return fail("xatlas could not read a mesh");
    }
  }
  xatlas::ComputeCharts(atlas);
  float density = std::max(s.texelsPerMeter, 0.1f);
  for (int attempt = 0; attempt < 10; ++attempt) {
    xatlas::PackOptions po;
    po.padding = (uint32_t)std::max(s.padding, 0);
    po.texelsPerUnit = density;
    po.bilinear = true;
    po.blockAlign = true;
    xatlas::PackCharts(atlas, po);
    const int side = (int)std::max(atlas->width, atlas->height);
    if (side <= s.maxSize) break;
    density *= 0.95f * (float)s.maxSize / (float)side;
  }
  const int W = (int)atlas->width, H = (int)atlas->height;
  if (W <= 0 || H <= 0 || W > s.maxSize || H > s.maxSize || atlas->meshCount != insts.size()) {
    xatlas::Destroy(atlas);
    return fail("the lightmap atlas did not fit");
  }
  out.width = W;
  out.height = H;
  out.texelsPerMeter = density;

  // 3) per instance: the mesh with atlas coordinates, and its world triangles for the ray scene
  std::vector<glm::vec3> triPos;
  std::vector<Tri> tris;
  for (size_t k = 0; k < insts.size(); ++k) {
    const xatlas::Mesh& m = atlas->meshes[k];
    const DrawItem3D& it = items[insts[k].item];
    const MeshData& src = *it.mesh->source;
    MeshData md;
    md.vertices.resize(m.vertexCount);
    for (uint32_t v = 0; v < m.vertexCount; ++v) {
      md.vertices[v] = src.vertices[m.vertexArray[v].xref];
      md.vertices[v].lu = m.vertexArray[v].uv[0] / (float)W;
      md.vertices[v].lv = m.vertexArray[v].uv[1] / (float)H;
    }
    md.indices.assign(m.indexArray, m.indexArray + m.indexCount);
    const glm::vec3 albedo = Rgb(it.material.baseColor) * (1.f - std::clamp(it.material.metallic, 0.f, 1.f));
    for (uint32_t t = 0; t + 2 < m.indexCount; t += 3) {
      Tri tri;
      for (int c = 0; c < 3; ++c) {
        const xatlas::Vertex& xv = m.vertexArray[m.indexArray[t + c]];
        triPos.push_back(insts[k].wp[xv.xref]);
        tri.n[c] = insts[k].wn[xv.xref];
        tri.luv[c] = {xv.uv[0], xv.uv[1]};
      }
      tri.albedo = it.material.unlit ? glm::vec3(0) : albedo;
      tri.emissive = Rgb(it.material.emissive) + (it.material.unlit ? Rgb(it.material.baseColor) : glm::vec3(0));
      tri.instance = (int)k;
      tris.push_back(tri);
    }
    out.itemInstance[insts[k].item] = (int)k;
    out.instanceItem.push_back(insts[k].item);
    out.meshes.push_back(std::move(md));
  }
  xatlas::Destroy(atlas);
  for (size_t i = 0; i < items.size(); ++i) { // everything else that is opaque blocks and bounces light
    const DrawItem3D& it = items[i];
    if (out.itemInstance[i] >= 0 || !usable(it)) continue;
    const MeshData& src = *it.mesh->source;
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(it.model)));
    const glm::vec3 albedo = Rgb(it.material.baseColor) * (1.f - std::clamp(it.material.metallic, 0.f, 1.f));
    for (size_t t = 0; t + 2 < src.indices.size(); t += 3) {
      Tri tri;
      for (int c = 0; c < 3; ++c) {
        const Vertex3D& v = src.vertices[src.indices[t + c]];
        triPos.push_back(glm::vec3(it.model * glm::vec4(v.px, v.py, v.pz, 1)));
        glm::vec3 n = nm * glm::vec3(v.nx, v.ny, v.nz);
        tri.n[c] = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : glm::vec3(0, 1, 0);
      }
      tri.albedo = it.material.unlit ? glm::vec3(0) : albedo;
      tri.emissive = Rgb(it.material.emissive) + (it.material.unlit ? Rgb(it.material.baseColor) : glm::vec3(0));
      tris.push_back(tri);
    }
  }
  std::vector<glm::vec3> geoN(tris.size());
  for (size_t t = 0; t < tris.size(); ++t) {
    glm::vec3 g = glm::cross(triPos[t * 3 + 1] - triPos[t * 3], triPos[t * 3 + 2] - triPos[t * 3]);
    geoN[t] = glm::dot(g, g) > 1e-20f ? glm::normalize(g) : tris[t].n[0];
  }
  RayScene scene;
  scene.Build(triPos);

  // 4) texels: each covered by a lightmapped triangle (centre inside; then a half-texel conservative ring)
  const size_t texelCount = (size_t)W * H;
  std::vector<int> owner(texelCount, -1);
  std::vector<glm::vec3> tPos(texelCount), tN(texelCount), tG(texelCount);
  for (int pass = 0; pass < 2; ++pass)
    for (size_t t = 0; t < tris.size(); ++t) {
      const Tri& tri = tris[t];
      if (tri.instance < 0) continue;
      const glm::vec2 a = tri.luv[0], b = tri.luv[1], c = tri.luv[2];
      const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
      if (std::abs(area) < 1e-8f) continue;
      const float grow = pass == 0 ? 0.f : 1.f;
      const int x0 = std::max((int)std::floor(std::min({a.x, b.x, c.x}) - grow), 0);
      const int x1 = std::min((int)std::ceil(std::max({a.x, b.x, c.x}) + grow), W - 1);
      const int y0 = std::max((int)std::floor(std::min({a.y, b.y, c.y}) - grow), 0);
      const int y1 = std::min((int)std::ceil(std::max({a.y, b.y, c.y}) + grow), H - 1);
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const size_t idx = (size_t)y * W + x;
          if (owner[idx] >= 0) continue;
          const glm::vec2 p(x + 0.5f, y + 0.5f);
          float w1 = ((p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y)) / area;
          float w2 = ((b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y)) / area;
          float w0 = 1 - w1 - w2;
          if (pass == 0) {
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
          } else { // closest point of the triangle within ~0.75 texel
            w0 = std::max(w0, 0.f); w1 = std::max(w1, 0.f); w2 = std::max(w2, 0.f);
            const float sum = w0 + w1 + w2;
            w0 /= sum; w1 /= sum; w2 /= sum;
            const glm::vec2 q = a * w0 + b * w1 + c * w2;
            if (glm::distance(q, p) > 0.75f) continue;
          }
          owner[idx] = tri.instance;
          tPos[idx] = triPos[t * 3] * w0 + triPos[t * 3 + 1] * w1 + triPos[t * 3 + 2] * w2;
          glm::vec3 n = tri.n[0] * w0 + tri.n[1] * w1 + tri.n[2] * w2;
          n = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : geoN[t];
          tN[idx] = glm::dot(n, geoN[t]) > 0 ? n : geoN[t];
          tG[idx] = geoN[t];
        }
    }
  std::vector<uint32_t> cover;
  for (size_t i = 0; i < texelCount; ++i)
    if (owner[i] >= 0) cover.push_back((uint32_t)i);

  // 5) bounces: each pass sees the previous one's result on lightmapped surfaces
  auto direct = [&](const glm::vec3& p, const glm::vec3& n) { // irradiance from the sun and local lights, shadowed
    glm::vec3 e(0);
    const glm::vec3 o = p + n * 2e-3f;
    if (light.intensity > 0 && glm::dot(light.toLight, light.toLight) > 0) {
      const glm::vec3 l = glm::normalize(light.toLight);
      const float nl = glm::dot(n, l);
      if (nl > 0 && !scene.Occluded(o, l, 1e30f)) e += Rgb(light.color) * light.intensity * nl;
    }
    const size_t locals = std::min<size_t>(light.locals.size(), kMaxClusteredLights);
    for (size_t i = 0; i < locals; ++i) {
      const LocalLight3D& L = light.locals[i];
      const glm::vec3 toL = L.position - p;
      const float d2 = std::max(glm::dot(toL, toL), 1e-4f), d = std::sqrt(d2);
      const glm::vec3 dir = toL / d;
      const float nl = glm::dot(n, dir);
      if (nl <= 0) continue;
      const float range = std::max(L.range, 1e-3f);
      const float win = std::clamp(1.f - std::pow(d2 / (range * range), 2.f), 0.f, 1.f);
      float atten = win * win / (d2 + 1.f); // the shader's windowed inverse square
      if (L.spot) {
        const float cosOuter = std::cos(glm::radians(std::max(L.outerAngle, 0.1f) * 0.5f));
        const float cosInner = std::cos(glm::radians(std::clamp(L.innerAngle, 0.f, L.outerAngle - 0.01f) * 0.5f));
        const float x = std::clamp((glm::dot(-dir, glm::normalize(L.direction)) - cosOuter) / std::max(cosInner - cosOuter, 1e-4f), 0.f, 1.f);
        atten *= x * x * (3 - 2 * x);
      }
      if (atten <= 0 || scene.Occluded(o, dir, d - 4e-3f)) continue;
      e += Rgb(L.color) * L.intensity * atten * nl;
    }
    return e;
  };
  std::vector<glm::vec3> prev(texelCount, glm::vec3(0)), cur(texelCount, glm::vec3(0));
  std::vector<uint8_t> inside(texelCount, 0);
  auto sampleAtlas = [&](const std::vector<glm::vec3>& img, glm::vec2 uv) { // bilinear over covered texels
    const float fx = uv.x - 0.5f, fy = uv.y - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float ax = fx - x0, ay = fy - y0;
    glm::vec3 sum(0);
    float wsum = 0;
    for (int k = 0; k < 4; ++k) {
      const int x = x0 + (k & 1), y = y0 + (k >> 1);
      if (x < 0 || y < 0 || x >= W || y >= H) continue;
      const size_t idx = (size_t)y * W + x;
      if (owner[idx] < 0) continue;
      const float w = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay) + 1e-4f;
      sum += img[idx] * w;
      wsum += w;
    }
    return wsum > 0 ? sum / wsum : glm::vec3(0);
  };
  const int side = std::max(1, (int)std::lround(std::sqrt((double)std::max(s.samples, 1))));
  const int rays = side * side;
  const unsigned threads = std::clamp(std::thread::hardware_concurrency(), 1u, 32u);
  for (int bounce = 0; bounce < std::max(s.bounces, 1); ++bounce) {
    std::atomic<size_t> next{0};
    auto work = [&]() {
      for (;;) {
        const size_t begin = next.fetch_add(64);
        if (begin >= cover.size()) break;
        const size_t end = std::min(begin + 64, cover.size());
        for (size_t ci = begin; ci < end; ++ci) {
          const uint32_t idx = cover[ci];
          const glm::vec3 n = tN[idx], g = tG[idx], p = tPos[idx];
          const glm::vec3 helper = std::abs(n.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
          const glm::vec3 tx = glm::normalize(glm::cross(helper, n)), ty = glm::cross(n, tx);
          const float scale = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z), 1.f});
          const glm::vec3 o = p + g * (1e-3f * scale);
          uint32_t seed = Hash(idx * 9781u + (uint32_t)bounce * 6271u + 1u);
          glm::vec3 sum(0);
          int back = 0;
          for (int r = 0; r < rays; ++r) {
            const float u = ((float)(r % side) + Rand(seed)) / (float)side, v = ((float)(r / side) + Rand(seed)) / (float)side;
            const float rad = std::sqrt(u), phi = 6.2831853f * v;
            glm::vec3 dir = tx * (rad * std::cos(phi)) + ty * (rad * std::sin(phi)) + n * std::sqrt(std::max(1 - u, 0.f));
            if (glm::dot(dir, g) < 0) dir -= 2 * glm::dot(dir, g) * g; // stay above the actual surface
            RayHit hit;
            if (!scene.Intersect(o, dir, 1e30f, hit)) {
              if (in.sky) sum += in.sky(dir);
              continue;
            }
            if (hit.backface) { ++back; continue; }
            const Tri& tri = tris[hit.tri];
            const float w0 = 1 - hit.u - hit.v;
            glm::vec3 hn = tri.n[0] * w0 + tri.n[1] * hit.u + tri.n[2] * hit.v;
            hn = glm::dot(hn, hn) > 1e-12f ? glm::normalize(hn) : geoN[hit.tri];
            const glm::vec3 hp = o + dir * hit.t;
            glm::vec3 indirect(0);
            if (tri.instance >= 0) indirect = sampleAtlas(prev, tri.luv[0] * w0 + tri.luv[1] * hit.u + tri.luv[2] * hit.v);
            else if (in.ambient) indirect = in.ambient(hp, hn);
            sum += tri.emissive + tri.albedo * (direct(hp, hn) * (1.f / 3.14159265f) + indirect);
          }
          cur[idx] = sum / (float)rays;
          inside[idx] = back * 4 > rays ? 1 : 0;
        }
      }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (std::thread& t : pool) t.join();
    prev.swap(cur);
  }
  // prev holds the last bounce. Texels inside geometry do not count; they are filled like the empty ones.
  for (uint32_t idx : cover)
    if (inside[idx]) { owner[idx] = -1; ++out.insideTexels; }

  // 6) smooth the noise between neighbours of the same object that face the same way and lie close together
  const float near2 = std::pow(2.5f / density, 2.f);
  std::vector<glm::vec3> smooth(prev);
  for (uint32_t idx : cover) {
    if (owner[idx] < 0) continue;
    const int x = (int)(idx % W), y = (int)(idx / W);
    glm::vec3 sum(0);
    float wsum = 0;
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        const size_t j = (size_t)ny * W + nx;
        if (owner[j] != owner[idx] || glm::dot(tN[j], tN[idx]) < 0.9f) continue;
        const glm::vec3 dp = tPos[j] - tPos[idx];
        if (glm::dot(dp, dp) > near2) continue;
        const float w = std::exp(-(float)(dx * dx + dy * dy) / 4.5f);
        sum += prev[j] * w;
        wsum += w;
      }
    if (wsum > 0) smooth[idx] = sum / wsum;
  }
  // 7) fill empty texels around charts (and inside ones) from covered neighbours, a few rings deep
  std::vector<uint8_t> valid(texelCount, 0);
  for (size_t i = 0; i < texelCount; ++i) valid[i] = owner[i] >= 0 ? 1 : 0;
  for (int ring = 0; ring < 4; ++ring) {
    std::vector<uint32_t> filled;
    std::vector<glm::vec3> values;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const size_t idx = (size_t)y * W + x;
        if (valid[idx]) continue;
        glm::vec3 sum(0);
        int count = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < W && ny < H && valid[(size_t)ny * W + nx]) {
              sum += smooth[(size_t)ny * W + nx];
              ++count;
            }
          }
        if (count) { filled.push_back((uint32_t)idx); values.push_back(sum / (float)count); }
      }
    for (size_t k = 0; k < filled.size(); ++k) { smooth[filled[k]] = values[k]; valid[filled[k]] = 1; }
  }
  out.rgba.assign(texelCount * 4, 0.f);
  for (size_t i = 0; i < texelCount; ++i) {
    out.rgba[i * 4] = smooth[i].r;
    out.rgba[i * 4 + 1] = smooth[i].g;
    out.rgba[i * 4 + 2] = smooth[i].b;
    out.rgba[i * 4 + 3] = valid[i] ? 1.f : 0.f;
  }
  for (uint32_t idx : cover) {
    if (owner[idx] < 0) continue;
    out.texelPos.push_back(tPos[idx]);
    out.texelNormal.push_back(tN[idx]);
    out.texelIndex.push_back(idx);
  }
  out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

} // namespace bdg
