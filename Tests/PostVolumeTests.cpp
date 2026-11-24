// Post-process volume blending (queue E-6).
#include "Render3D/PostVolume.h"
#include "bdg_test.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

using namespace bdg;

TEST_CASE("post volumes: influence, weight and priority (queue E-6)") {
  PostVolume3D global; // unbound: everywhere
  global.fx.vignette = 0.4f;
  CHECK(VolumeInfluence(global, {100, 0, 0}) == 1.f);

  PostVolume3D box; // a 4 m box at x = 10 with 2 m of fade
  box.unbound = false;
  box.world = glm::translate(glm::mat4(1.f), glm::vec3(10, 0, 0));
  box.size = {4, 4, 4};
  box.blendRadius = 2;
  box.priority = 1;
  box.fx.vignette = 1.f;
  box.fx.exposureEV = 2.f;
  box.fx.autoExposure = true;
  CHECK(VolumeInfluence(box, {10, 0, 0}) == 1.f);   // inside
  CHECK(VolumeInfluence(box, {11.9f, 0, 0}) == 1.f);
  CHECK(std::abs(VolumeInfluence(box, {13, 0, 0}) - 0.5f) < 1e-5f); // 1 m outside of 2 m fade
  CHECK(VolumeInfluence(box, {20, 0, 0}) == 0.f);
  box.world = glm::scale(box.world, glm::vec3(2.f)); // scaled object: the box is 8 m, distances in metres
  CHECK(VolumeInfluence(box, {13.9f, 0, 0}) == 1.f);
  CHECK(std::abs(VolumeInfluence(box, {15, 0, 0}) - 0.5f) < 1e-5f);

  const std::vector<PostVolume3D> vols = {box, global}; // global applies first (priority 0), then the box
  PostEffects3D far = BlendPostVolumes(vols, {100, 0, 0});
  CHECK(std::abs(far.vignette - 0.4f) < 1e-6f && far.exposureEV == 0 && !far.autoExposure);
  PostEffects3D in = BlendPostVolumes(vols, {10, 0, 0});
  CHECK(std::abs(in.vignette - 1.f) < 1e-6f && std::abs(in.exposureEV - 2.f) < 1e-6f && in.autoExposure);
  PostEffects3D edge = BlendPostVolumes(vols, {15, 0, 0}); // half way: 0.4 -> 1 by 0.5
  CHECK(std::abs(edge.vignette - 0.7f) < 1e-5f && std::abs(edge.exposureEV - 1.f) < 1e-5f && edge.autoExposure);

  std::vector<PostVolume3D> weighted = {global};
  weighted[0].weight = 0.25f;
  CHECK(std::abs(BlendPostVolumes(weighted, {}).vignette - 0.1f) < 1e-6f);
  CHECK(BlendPostVolumes({}, {}).vignette == 0 && BlendPostVolumes({}, {}).bloomScale == 1);
}
