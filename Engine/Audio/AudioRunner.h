#pragma once
// Plays the AudioSource components of the play world (queue 6-3), like BehaviourRunner does for C# scripts.
// Begin (Play): the audio system starts (ProjectSettings audio.sampleRate / masterVolume, AudioMixer.json groups;
// BDG_AUDIO_NULL=1 = no device) and sources with playOnStart begin. Frame: the listener follows the first
// AudioListener (else the first Camera), spatial sources follow their objects, Play / Stop calls and Inspector
// edits (volume, pitch) are applied, sources of deleted objects stop; without a device the frame's audio is mixed
// and measured. End (Stop): everything stops.
#include "Audio/Audio.h"
#include "Audio/AudioComponents.h"
#include <string>
#include <vector>

namespace bdg {
class Scene;

class AudioRunner {
public:
  void Begin(Scene& scene);
  void Frame(Scene& scene, double dt);
  void SetPaused(bool paused);
  void End();
  bool Active() const { return active_; }
  size_t Sources() const { return srcs_.size(); }
  uint32_t SoundOf(const std::string& objectId) const; // the sound an object's AudioSource plays (0 = none)

private:
  struct Src {
    std::string object;
    uint32_t sound = 0;
    float volume = 1, pitch = 1;
    bool seen = false, warned = false;
  };
  void Sync(Scene& scene);
  void Start(Src& s, const AudioSource& a, const Vec3& pos);
  std::vector<Src> srcs_;
  bool active_ = false;
};

} // namespace bdg
