#include "Audio/AudioRunner.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Project/ProjectSettings.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <glm/glm.hpp>

namespace bdg {
namespace fs = std::filesystem;

namespace {
std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}
// Hierarchy order, the same every run.
template <class F> void Walk(Scene& scene, F&& f) {
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
    f(*o);
  }
}
} // namespace

uint32_t AudioRunner::SoundOf(const std::string& objectId) const {
  for (const Src& s : srcs_)
    if (s.object == objectId) return s.sound;
  return 0;
}

void AudioRunner::Begin(Scene& scene) {
  End();
  AudioSystem& audio = AudioSystem::Instance();
  ProjectSettings settings;
  const fs::path root = fs::current_path();
  std::error_code ec;
  if (fs::exists(root / "ProjectSettings" / "Project.json", ec)) LoadProjectSettings(U(root / "ProjectSettings" / "Project.json"), settings);
  std::string err;
  if (!audio.Running() && !audio.Init(settings.audio.sampleRate, std::getenv("BDG_AUDIO_NULL") == nullptr, &err)) {
    LogWarn("audio: {}", err);
    return;
  }
  audio.SetMasterVolume(settings.audio.masterVolume);
  if (!audio.LoadMixer(U(root / "ProjectSettings" / "AudioMixer.json"), &err)) LogWarn("audio: {}", err);
  std::vector<std::string> folders{U(root / "Assets")};
  if (const char* assets = std::getenv("BDG_ASSETS")) folders.push_back(assets);
  audio.SetSearchFolders(folders);
  audio.SetPaused(false);
  active_ = true;
  Sync(scene);
}

void AudioRunner::Start(Src& s, const AudioSource& a, const Vec3& pos) {
  AudioSystem& audio = AudioSystem::Instance();
  if (s.sound) audio.Stop(s.sound);
  AudioPlay p;
  p.clip = a.clip;
  p.group = a.group;
  p.volume = a.volume;
  p.pitch = a.pitch;
  p.loop = a.loop;
  p.spatial = a.spatial;
  p.position = pos;
  p.minDistance = a.minDistance;
  p.maxDistance = a.maxDistance;
  p.rolloff = (int)a.rolloff;
  p.rolloffFactor = a.rolloffFactor;
  std::string err;
  s.sound = audio.Play(p, &err);
  s.volume = a.volume;
  s.pitch = a.pitch;
  if (!s.sound && !s.warned) {
    LogWarn("audio: {} AudioSource: {}", s.object, err);
    s.warned = true;
  }
}

void AudioRunner::Sync(Scene& scene) {
  AudioSystem& audio = AudioSystem::Instance();
  for (Src& s : srcs_) s.seen = false;
  // the listener: the first AudioListener, else the first Camera (hierarchy order)
  std::string ear;
  const AudioListener* earComp = nullptr;
  Walk(scene, [&](SceneObject& o) {
    if (earComp) return;
    if (Component* l = o.Find("AudioListener")) { ear = o.id; earComp = static_cast<const AudioListener*>(l->data); }
    else if (ear.empty() && o.Find("Camera")) ear = o.id;
  });
  if (!ear.empty()) {
    const glm::mat4 m = scene.WorldMatrix(ear);
    const glm::vec3 fwd = -glm::normalize(glm::vec3(m[2])), up = glm::normalize(glm::vec3(m[1]));
    audio.SetListener({m[3].x, m[3].y, m[3].z}, {fwd.x, fwd.y, fwd.z}, {up.x, up.y, up.z}, earComp ? earComp->volume : 1.f);
  }
  Walk(scene, [&](SceneObject& o) {
    Component* c = o.Find("AudioSource");
    if (!c) return;
    AudioSource& a = *static_cast<AudioSource*>(c->data);
    const Vec3 pos = scene.WorldPosition(o.id);
    auto it = std::find_if(srcs_.begin(), srcs_.end(), [&](const Src& s) { return s.object == o.id; });
    if (it == srcs_.end()) { // new: at Play, or made while playing (Spawn)
      srcs_.push_back({o.id});
      it = srcs_.end() - 1;
      if (a.playOnStart) Start(*it, a, pos);
    }
    Src& s = *it;
    s.seen = true;
    for (; a.pendingStops > 0; --a.pendingStops)
      if (s.sound) { audio.Stop(s.sound); s.sound = 0; }
    if (a.pendingPlays > 0) {
      a.pendingPlays = 0;
      Start(s, a, pos);
    }
    if (s.sound) {
      if (a.spatial) audio.SetPosition(s.sound, pos);
      if (a.volume != s.volume) audio.SetVolume(s.sound, s.volume = a.volume);
      if (a.pitch != s.pitch) audio.SetPitch(s.sound, s.pitch = a.pitch);
    }
  });
  for (Src& s : srcs_)
    if (!s.seen && s.sound) audio.Stop(s.sound);
  std::erase_if(srcs_, [](const Src& s) { return !s.seen; });
}

void AudioRunner::Frame(Scene& scene, double dt) {
  if (!active_) return;
  Sync(scene);
  AudioSystem& audio = AudioSystem::Instance();
  audio.Update();
  audio.Pump(dt); // no device: this frame's audio is mixed (and measured)
}

void AudioRunner::SetPaused(bool paused) {
  if (active_) AudioSystem::Instance().SetPaused(paused);
}

void AudioRunner::End() {
  if (!active_) return;
  AudioSystem::Instance().StopAll();
  AudioSystem::Instance().SetPaused(false);
  srcs_.clear();
  active_ = false;
}

} // namespace bdg
