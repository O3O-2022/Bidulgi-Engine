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
  float dofBlur = 0;           // depth of field (E-6b): 0 = off, 1 = the widest blur
  float dofFocus = 10, dofRange = 2; // metres in focus: from focus - range/2 to focus + range/2
  float lensFlare = 0;         // ghosts and a halo from the brightest parts (needs bloom)
  // Volumetric fog (round 6): light scattered in the air, lit by the sun (with its shadows) and point / spot lights.
  float fogDensity = 0;        // extinction per metre at fogHeight (0 = no volumetric fog)
  float fogHeight = 0;         // world height (m) where the density is fogDensity
  float fogFalloff = 0.2f;     // density halves every ln2 / falloff metres above fogHeight (0 = the same everywhere)
  glm::vec3 fogAlbedo{1.f};    // colour of the scattered light (linear)
  float fogAnisotropy = 0.6f;  // -1 back .. 0 even .. 1 forward scattering (Henyey-Greenstein): glow around the sun
  float fogDistance = 64;      // metres of fog simulated in front of the camera
  float fogLight = 1;          // scattering of local lights, times their intensity
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
