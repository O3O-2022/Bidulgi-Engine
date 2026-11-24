#include "Render3D/PostVolume.h"
#include <algorithm>
#include <cmath>

namespace bdg {

float VolumeInfluence(const PostVolume3D& v, const glm::vec3& camera) {
  if (v.unbound) return 1.f;
  // distance from the box, measured in world metres: the box is the volume's local unit cube scaled by `size`
  // and placed by `world` (its scale included)
  const glm::mat4 toLocal = glm::inverse(v.world);
  const glm::vec3 local = glm::vec3(toLocal * glm::vec4(camera, 1.f));
  const glm::vec3 half = glm::abs(v.size) * 0.5f;
  const glm::vec3 outside = glm::max(glm::abs(local) - half, glm::vec3(0.f));
  const glm::vec3 scale(glm::length(glm::vec3(v.world[0])), glm::length(glm::vec3(v.world[1])), glm::length(glm::vec3(v.world[2])));
  const float d = glm::length(outside * scale);
  if (d <= 0.f) return 1.f;
  if (v.blendRadius <= 0.f) return 0.f;
  return std::clamp(1.f - d / v.blendRadius, 0.f, 1.f);
}

PostEffects3D BlendPostVolumes(const std::vector<PostVolume3D>& volumes, const glm::vec3& camera) {
  std::vector<const PostVolume3D*> order;
  for (const PostVolume3D& v : volumes) order.push_back(&v);
  std::stable_sort(order.begin(), order.end(), [](const PostVolume3D* a, const PostVolume3D* b) { return a->priority < b->priority; });
  PostEffects3D out;
  for (const PostVolume3D* v : order) {
    const float a = std::clamp(v->weight, 0.f, 1.f) * VolumeInfluence(*v, camera);
    if (a <= 0.f) continue;
    auto mix = [a](float& to, float from) { to += (from - to) * a; };
    const PostEffects3D& f = v->fx;
    mix(out.exposureEV, f.exposureEV);
    mix(out.autoMinEV, f.autoMinEV);
    mix(out.autoMaxEV, f.autoMaxEV);
    mix(out.adaptSpeed, f.adaptSpeed);
    mix(out.bloomScale, f.bloomScale);
    mix(out.vignette, f.vignette);
    mix(out.vignetteSmooth, f.vignetteSmooth);
    mix(out.aberration, f.aberration);
    mix(out.grain, f.grain);
    if (a >= 0.5f) out.autoExposure = f.autoExposure; // an on/off switch flips half way into the blend
  }
  return out;
}

} // namespace bdg
