#pragma once
// Drawing particles (queue 6-VFX P-1): camera-facing soft round sprites, built on the CPU each frame and drawn inside
// the 3D renderer's scene pass (RenderExtras::transparent), so they are depth-tested against the scene, in linear
// light, with bloom on bright additive ones. Alpha-blended particles are sorted back to front; additive ones need no
// order. Materials, flipbooks, soft particles and lit particles come later (particles.md).
#include <glm/glm.hpp>
#include <cstddef>

namespace bdg {
class RHI;
class VfxWorld;

// Returns the number of particles drawn.
size_t DrawParticles(RHI& rhi, const VfxWorld& world, const glm::mat4& view, const glm::mat4& proj);
void ShutdownParticleRenderer(RHI& rhi);
} // namespace bdg
