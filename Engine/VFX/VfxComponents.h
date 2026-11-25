#pragma once
// The ParticleSystem component (queue 6-VFX P-1): plays a .vfx effect at its object while the game runs (editor play
// mode and the built game). Scripts call Play / Stop like any Callable, or play one-off effects with C# Vfx.Play.
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BENUM()
enum class ParticleLoop { Effect, Loop, Once };

BCLASS(Component)
struct ParticleSystem {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "The effect: a .vfx file (Assets-relative path or just its name)") std::string effect;
  BPROPERTY(Edit, Tooltip = "Start when the game starts") bool playOnStart = true;
  BPROPERTY(Edit, Tooltip = "Effect: as the .vfx says; Loop / Once: override every emitter") ParticleLoop loop = ParticleLoop::Effect;
  BPROPERTY(Edit, Tooltip = "Same seed = the same particles every run (0: from the object's id)") int32_t seed = 0;
  BPROPERTY(ReadOnly, Tooltip = "Particles alive now (while playing)") int32_t particles = 0;
  BFUNCTION(Callable) void Play() { ++pendingPlays; }
  BFUNCTION(Callable) void Stop() { ++pendingStops; }
  int32_t pendingPlays = 0, pendingStops = 0; // not saved: taken by play mode
};

} // namespace bdg
