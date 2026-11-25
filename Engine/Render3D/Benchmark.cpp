#include "Render3D/Benchmark.h"
#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg {
bool RunQualityBenchmark(RHI& rhi, Renderer3D& r, BenchmarkResult& out, int frames, int32_t w, int32_t h) {
  out = {};
  Mesh3D box = r.CreateMesh(MakeBox(1, 1, 1)), ground = r.CreateMesh(MakePlaneXZ(1, 1));
  RenderTargetHandle rt = rhi.CreateRenderTarget(w, h, 1);
  if (!rt.Valid() || !box.indexCount) {
    r.DestroyMesh(box);
    r.DestroyMesh(ground);
    return false;
  }
  std::vector<DrawItem3D> items;
  Material3D g;
  g.baseColor = {0.5f, 0.5f, 0.5f, 1};
  g.roughness = 0.8f;
  items.push_back({&ground, glm::scale(glm::mat4(1.f), glm::vec3(60, 1, 60)), g});
  for (int z = 0; z < 20; ++z)
    for (int x = 0; x < 20; ++x) {
      Material3D m;
      m.baseColor = {0.3f + 0.035f * x, 0.4f, 0.3f + 0.035f * z, 1};
      m.roughness = 0.2f + 0.03f * ((x + z) % 20);
      m.metallic = (x + z) % 5 == 0 ? 1.f : 0.f;
      const float hgt = 0.5f + 0.25f * ((x * 7 + z * 3) % 6);
      items.push_back({&box, glm::scale(glm::translate(glm::mat4(1.f), glm::vec3(x * 2.5f - 24, hgt * 0.5f, z * 2.5f - 24)),
                                        glm::vec3(1, hgt, 1)), m});
    }
  Light3D light;
  light.toLight = glm::normalize(glm::vec3(0.4f, 1, 0.3f));
  light.shadows = true;
  light.background.draw = true;
  for (int i = 0; i < 64; ++i) {
    LocalLight3D l;
    l.position = {(i % 8) * 6.f - 21, 1.5f, (i / 8) * 6.f - 21};
    l.color = {0.5f + 0.5f * (i % 3 == 0), 0.5f + 0.5f * (i % 3 == 1), 0.5f + 0.5f * (i % 3 == 2), 1};
    l.intensity = 4;
    l.range = 7;
    l.castShadows = i < 4;
    light.locals.push_back(l);
  }
  Camera3D cam;
  cam.position = {-26, 9, 26};
  cam.target = {0, 0, 0};
  const QualitySettings q = QualityPreset(QualityTier::High);
  std::vector<uint8_t> px;
  int32_t rw = 0, rh = 0;
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  for (int i = 0; i < 2; ++i) r.Render(rt, w, h, cam, light, items, {0, 0, 0, 1}, nullptr, &q); // warm up (targets, shaders)
  rhi.ReadRenderTarget(rt, rw, rh, px);
  auto t0 = Clock::now();
  r.Render(rt, w, h, cam, light, items, {0, 0, 0, 1}, nullptr, &q);
  rhi.ReadRenderTarget(rt, rw, rh, px);
  const double probeMs = ms(t0, Clock::now());
  t0 = Clock::now();
  rhi.ReadRenderTarget(rt, rw, rh, px); // the cost of one read-back alone, taken off each round below
  const double readMs = ms(t0, Clock::now());
  std::vector<double> rounds;
  const int perRound = probeMs > 100 ? 1 : std::max(frames / 3, 1); // a slow device (software GL): no need for many
  for (int k = 0; k < 3; ++k) {
    t0 = Clock::now();
    for (int i = 0; i < perRound; ++i) {
      cam.position = glm::vec3(glm::rotate(glm::mat4(1.f), 0.02f * (k * perRound + i), glm::vec3(0, 1, 0)) * glm::vec4(-26, 9, 26, 1));
      r.Render(rt, w, h, cam, light, items, {0, 0, 0, 1}, nullptr, &q);
    }
    rhi.ReadRenderTarget(rt, rw, rh, px); // waits for the GPU
    rounds.push_back(std::max(ms(t0, Clock::now()) - readMs, 0.01) / perRound);
  }
  std::sort(rounds.begin(), rounds.end());
  out.highMs = rounds[rounds.size() / 2];
  out.frames = perRound * 3;
  out.w = w;
  out.h = h;
  rhi.DestroyRenderTarget(rt);
  r.DestroyMesh(box);
  r.DestroyMesh(ground);
  return true;
}
}
