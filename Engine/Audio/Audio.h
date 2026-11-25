#pragma once
// Audio (queue 6-3): miniaudio (public domain / MIT-0) behind a small engine API; no miniaudio types here.
//
// One AudioSystem per process. Sounds play into mixer groups (Master > Music, Effects, Voice, UI, Ambient, plus
// groups from ProjectSettings/AudioMixer.json), each with its own volume. Spatial sounds are attenuated and panned
// from the listener (min / max distance, inverse / linear / exponential rolloff), Unreal / Unity style.
//
// Without an output device (none present, or BDG_AUDIO_NULL=1 for automated runs) the system still runs: the
// caller pumps game time with Pump() and the mixed level is measured (Stats), so checks can hear without speakers.
#include "Core/Math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bdg {

struct AudioPlay {
  std::string clip;              // path (absolute, or found in the search folders)
  std::string group = "Effects";
  float volume = 1.f, pitch = 1.f;
  bool loop = false;
  bool spatial = false;
  Vec3 position{};
  float minDistance = 1.f, maxDistance = 50.f, rolloffFactor = 1.f;
  int rolloff = 0;               // AudioRolloff: 0 inverse, 1 linear, 2 exponential
};

struct AudioStats {
  int playing = 0;               // sounds playing now
  int started = 0;               // sounds started since Init
  uint64_t framesMixed = 0;      // no-device mixing only
  float rms = 0, peak = 0;       // level of the last Pump (no device)
};

struct AudioGroupDef {
  std::string name, parent = "Master";
  float volume = 1.f;
};

class AudioSystem {
public:
  static AudioSystem& Instance();
  ~AudioSystem();
  // device: try the default output (falls back to no device when there is none). Safe to call again (keeps running).
  bool Init(int sampleRate, bool device, std::string* error = nullptr);
  void Shutdown();
  bool Running() const;
  bool HasDevice() const;
  int SampleRate() const;

  void SetMasterVolume(float v);
  void SetMuted(bool muted); // e.g. the game window lost focus (audio.playInBackground off)
  // Mixer groups: the built-in ones plus `extra` (parents first). Unknown group names play in Effects.
  void SetGroups(const std::vector<AudioGroupDef>& extra);
  bool LoadMixer(const std::string& path, std::string* error = nullptr); // ProjectSettings/AudioMixer.json (optional)
  bool SetGroupVolume(const std::string& group, float volume);
  float GroupVolume(const std::string& group) const; // -1 = no such group
  std::vector<std::string> Groups() const;

  // Folders searched for relative clip names (in order); a bare file name is also looked up under them. A path
  // relative to the working folder (the project) works too.
  void SetSearchFolders(std::vector<std::string> folders);
  std::string ResolveClip(const std::string& clip) const; // "" = not found

  uint32_t Play(const AudioPlay& p, std::string* error = nullptr); // 0 = failed (error says why)
  void Stop(uint32_t id);
  void StopAll();
  bool IsPlaying(uint32_t id) const;
  void SetPosition(uint32_t id, const Vec3& p);
  void SetVolume(uint32_t id, float v);
  void SetPitch(uint32_t id, float p);
  void SetPaused(bool paused); // pauses every playing sound (play mode paused) and resumes them
  void SetListener(const Vec3& position, const Vec3& forward, const Vec3& up, float volume = 1.f);

  void Update();             // forgets finished sounds
  void Pump(double seconds); // no device: mixes this much audio and measures it
  AudioStats Stats() const;

private:
  AudioSystem() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace bdg
