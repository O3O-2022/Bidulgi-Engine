#pragma once
// Post-process volumes (queue E-6), like Unreal's: each volume holds a full set of camera effects; where the camera
// is decides how much of each applies. Unbound volumes cover the whole scene; bound ones are boxes whose effect
// fades out over `blendRadius` metres outside them. Volumes apply in priority order (low first), each blended in
// by its weight times how much the camera is inside it. Pure maths: unit-tested without a GPU.
#include <glm/glm.hpp>
#include <vector>

namespace bdg {

struct PostEffects3D {
  float exposureEV = 0;        // exposure compensation in stops
  bool autoExposure = false;   // eye adaptation: the average brightness is brought to middle grey
  float autoMinEV = -2, autoMaxEV = 2, adaptSpeed = 2; // how far it may correct, how fast (per second)
  float bloomScale = 1;        // times the quality tier's bloom intensity
  float vignette = 0, vignetteSmooth = 0.5f;
  float aberration = 0;        // chromatic aberration (0..1)
  float grain = 0;             // film grain (0..1)
};

struct PostVolume3D {
  glm::mat4 world{1.f};
  glm::vec3 size{10.f};
  bool unbound = true;
  float blendRadius = 2, weight = 1;
  int priority = 0;
  PostEffects3D fx;
};

// 0..1: 1 inside the box (or unbound), falling to 0 at blendRadius metres outside (the weight is not included).
float VolumeInfluence(const PostVolume3D& v, const glm::vec3& camera);
// The effects at the camera: defaults (no effect) with every volume blended in, lowest priority first.
PostEffects3D BlendPostVolumes(const std::vector<PostVolume3D>& volumes, const glm::vec3& camera);

} // namespace bdg
