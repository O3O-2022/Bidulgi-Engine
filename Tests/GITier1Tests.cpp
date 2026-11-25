// GI Tier 1, first part (round 6): the traced probe grid — light leaks through thin walls, reaction to a light switched
// on, probes inside geometry, multi-bounce, scene rebuilds.
#include "bdg_test.h"
#include "Render3D/DynamicGI.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

using namespace bdg;

namespace {
struct World {
  std::vector<std::unique_ptr<Mesh3D>> meshes;
  std::vector<DrawItem3D> items;
  void Box(const glm::vec3& centre, const glm::vec3& size, Color albedo = {0.7f, 0.7f, 0.7f, 1}) {
    auto m = std::make_unique<Mesh3D>();
    m->source = std::make_shared<MeshData>(MakeBox(1, 1, 1));
    m->indexCount = (uint32_t)m->source->indices.size();
    m->contentHash = 0x1234;
    DrawItem3D it;
    it.mesh = m.get();
    it.model = glm::scale(glm::translate(glm::mat4(1.f), centre), size);
    it.material.baseColor = albedo;
    items.push_back(it);
    meshes.push_back(std::move(m));
  }
};
// A closed room (x 0..4, y 0..3, z -2..2) with 5 cm walls on a big sunlit ground; the probe grid (1 m cells) spans
// inside and outside, so outside probes sit half a metre from the inner wall faces.
void Room(World& w) {
  w.Box({0, -0.5f, 0}, {40, 1, 40});
  const float t = 0.05f;
  w.Box({0, 1.5f, 0}, {t, 3, 4 + t});  // west wall (x = 0), overlapping the corners
  w.Box({4, 1.5f, 0}, {t, 3, 4 + t});  // east wall
  w.Box({2, 1.5f, -2}, {4, 3, t});     // north
  w.Box({2, 1.5f, 2}, {4, 3, t});      // south
  w.Box({2, 3, 0}, {4 + t, t, 4 + t}); // roof
}
Light3D Sun() {
  Light3D l;
  l.toLight = glm::normalize(glm::vec3(-0.6f, 1, 0.3f));
  l.color = {1, 0.95f, 0.9f, 1};
  l.intensity = 3;
  l.shadows = true;
  l.background.draw = true;
  return l;
}
ProbeVolume3D Grid() {
  ProbeVolume3D v;
  v.boxMin = {-4, 0, -4};
  v.boxMax = {4, 4, 4};
  v.count = {8, 4, 8};
  return v;
}
}

TEST_CASE("GI Tier 1: traced probe grid, no light through thin walls, quick reaction to a light (round 6)") {
  World w;
  Room(w);
  DynamicGI gi;
  gi.Configure(Grid());
  CHECK(gi.SetScene(w.items));
  CHECK(!gi.SetScene(w.items)); // unchanged: not rebuilt
  CHECK(gi.Triangles() == 6 * 12);
  const Light3D sun = Sun();
  DynamicGI::Settings s;
  s.probesPerUpdate = (int)gi.Probes(); // every probe each update (the renderer spreads them over frames)
  for (int i = 0; i < 6; ++i) gi.Update(sun, s);
  CHECK(gi.RaysTraced() == 6 * gi.Probes() * 64);
  // outside on the sunlit ground: bright; inside the closed room: dark, though probes outside are half a metre away
  const glm::vec3 out = gi.Irradiance({-1.5f, 0.05f, 0}, {0, 1, 0});
  const glm::vec3 inVis = gi.Irradiance({0.3f, 0.05f, 0}, {0, 1, 0}, true), inLeak = gi.Irradiance({0.3f, 0.05f, 0}, {0, 1, 0}, false);
  const float o = out.r + out.g + out.b, iv = inVis.r + inVis.g + inVis.b, il = inLeak.r + inLeak.g + inLeak.b;
  std::printf("       outside %.3f  inside with visibility %.4f  without %.4f\n", o, iv, il);
  CHECK(o > 0.2f);
  CHECK(il > o * 0.15f); // without visibility the outside probes light the inside
  CHECK(iv < il * 0.1f && iv < o * 0.02f);
  // a light switched on inside the room: most of its light shows within a few updates
  Light3D lit = sun;
  LocalLight3D lamp;
  lamp.position = {2, 2.2f, 0};
  lamp.range = 6;
  lamp.intensity = 20;
  lamp.color = {1, 0.4f, 0.2f, 1};
  lit.locals.push_back(lamp);
  const glm::vec3 floorP{1.5f, 0.05f, 0.5f}, up{0, 1, 0};
  std::vector<float> trace;
  for (int i = 0; i < 12; ++i) {
    gi.Update(lit, s);
    const glm::vec3 e = gi.Irradiance(floorP, up);
    trace.push_back(e.r);
  }
  const float final = trace.back();
  int reach = 0;
  while (reach < (int)trace.size() && trace[reach] < 0.9f * final) ++reach;
  std::printf("       light on: 90%% after %d update(s) of every probe (final %.3f)\n", reach + 1, final);
  CHECK(final > iv * 5 && reach + 1 <= 5);
  // the west wall taken away (the visibility masks are traced again): the sun comes in
  std::vector<DrawItem3D> open = w.items;
  open.erase(open.begin() + 1);
  CHECK(gi.SetScene(open));
  for (int i = 0; i < 6; ++i) gi.Update(sun, s);
  const glm::vec3 opened = gi.Irradiance({0.3f, 0.05f, 0}, {0, 1, 0});
  CHECK(opened.r + opened.g + opened.b > iv * 20);
  // probes inside a solid block are switched off
  World solid;
  Room(solid);
  solid.Box({-2.5f, 1, -2.5f}, {2.2f, 2.2f, 2.2f}); // holds the probes at (-2.5, 0.5 / 1.5, -2.5)
  DynamicGI g2;
  g2.Configure(Grid());
  CHECK(g2.SetScene(solid.items));
  g2.Update(sun, s);
  CHECK(g2.InsideProbes() >= 2);
  // another grid starts over
  ProbeVolume3D other = Grid();
  other.count = {4, 2, 4};
  gi.Configure(other);
  CHECK(gi.Probes() == 32 && gi.SH()[3] == 0.f);
}
