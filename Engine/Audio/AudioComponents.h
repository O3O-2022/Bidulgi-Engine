#pragma once
// Audio components (queue 6-3), Unity-style: an AudioSource plays a clip from its object (3D when spatial), the
// AudioListener (usually on the camera) hears it. Played by play mode (Engine/Audio/AudioRunner); scripts call
// Play / Stop like any Callable, or use the easy API (C# Audio.Play).
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BENUM()
enum class AudioRolloff { Inverse, Linear, Exponential };

BCLASS(Component)
struct AudioSource {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Sound file (Assets-relative path or file name): .wav, .mp3, .flac") std::string clip;
  BPROPERTY(Edit, Tooltip = "Mixer group: Master, Music, Effects, Voice, UI, Ambient (ProjectSettings/AudioMixer.json adds more)") std::string group = "Effects";
  BPROPERTY(Edit, Range = "0,2") float volume = 1.f;
  BPROPERTY(Edit, Range = "0.1,3", Tooltip = "1 = original speed and pitch") float pitch = 1.f;
  BPROPERTY(Edit, Tooltip = "Start over when it ends") bool loop = false;
  BPROPERTY(Edit, Tooltip = "Play when the game starts") bool playOnStart = true;
  BPROPERTY(Edit, Header = "3D", Tooltip = "Heard from the object's place: quieter with distance, panned left / right") bool spatial = true;
  BPROPERTY(Edit, Range = "0.01,1000", Tooltip = "Full volume within this distance (m)") float minDistance = 1.f;
  BPROPERTY(Edit, Range = "0.1,10000", Tooltip = "No quieter beyond this distance (m)") float maxDistance = 50.f;
  BPROPERTY(Edit, Tooltip = "How the volume falls with distance") AudioRolloff rolloff = AudioRolloff::Inverse;
  BPROPERTY(Edit, Range = "0,10", Tooltip = "How fast it falls (1 = natural)") float rolloffFactor = 1.f;
  BFUNCTION(Callable) void Play() { ++pendingPlays; }
  BFUNCTION(Callable) void Stop() { ++pendingStops; }
  int32_t pendingPlays = 0, pendingStops = 0; // not saved: taken by play mode
};

BCLASS(Component)
struct AudioListener {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "0,2", Tooltip = "Volume of everything this listener hears") float volume = 1.f;
};

} // namespace bdg
