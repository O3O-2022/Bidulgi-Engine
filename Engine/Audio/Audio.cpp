#include "Audio/Audio.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include <miniaudio.h>
#include <yyjson.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>

namespace bdg {
namespace fs = std::filesystem;

struct AudioSystem::Impl {
  ma_engine engine{};
  bool device = false;
  int rate = 48000;
  float master = 1.f, listener = 1.f;
  bool muted = false;
  struct Group {
    std::string name, parent;
    std::unique_ptr<ma_sound_group> g;
  };
  std::vector<Group> groups; // parents before children (Master first)
  struct Sound {
    std::unique_ptr<ma_sound> s;
    bool pausedByUs = false;
  };
  std::map<uint32_t, Sound> sounds;
  uint32_t next = 1;
  bool paused = false;
  std::vector<std::string> folders;
  mutable std::map<std::string, std::string> nameIndex; // file name -> path (lazy, per folder scan)
  mutable bool indexed = false;
  AudioStats stats;

  ma_sound_group* FindGroup(const std::string& name) {
    for (Group& g : groups)
      if (g.name == name) return g.g.get();
    return nullptr;
  }
  void ApplyMaster() { ma_engine_set_volume(&engine, muted ? 0.f : master * listener); }
  ~Impl() {
    for (auto& kv : sounds)
      if (kv.second.s) ma_sound_uninit(kv.second.s.get());
    sounds.clear();
    for (auto it = groups.rbegin(); it != groups.rend(); ++it) ma_sound_group_uninit(it->g.get());
    groups.clear();
    ma_engine_uninit(&engine);
  }
};

namespace {
void UninitSound(std::unique_ptr<ma_sound>& s) {
  if (s) ma_sound_uninit(s.get());
  s.reset();
}
const std::vector<AudioGroupDef> kBuiltinGroups = {
    {"Music", "Master", 1.f}, {"Effects", "Master", 1.f}, {"Voice", "Master", 1.f}, {"UI", "Master", 1.f}, {"Ambient", "Master", 1.f}};
} // namespace

AudioSystem& AudioSystem::Instance() {
  static AudioSystem s;
  return s;
}

AudioSystem::~AudioSystem() { Shutdown(); }

bool AudioSystem::Init(int sampleRate, bool device, std::string* error) {
  if (impl_) return true;
  auto impl = std::make_unique<Impl>();
  impl->rate = std::clamp(sampleRate, 8000, 192000);
  ma_engine_config cfg = ma_engine_config_init();
  cfg.sampleRate = (ma_uint32)impl->rate;
  cfg.channels = 2;
  cfg.listenerCount = 1;
  ma_result r = MA_ERROR;
  if (device) {
    r = ma_engine_init(&cfg, &impl->engine);
    if (r == MA_SUCCESS) impl->device = true;
    else LogWarn("audio: no output device ({}); sound is mixed silently", ma_result_description(r));
  }
  if (!impl->device) {
    cfg.noDevice = MA_TRUE;
    r = ma_engine_init(&cfg, &impl->engine);
    if (r != MA_SUCCESS) {
      if (error) *error = std::string("audio engine: ") + ma_result_description(r);
      return false;
    }
  }
  // Master + built-in groups
  impl->groups.push_back({"Master", "", std::make_unique<ma_sound_group>()});
  ma_sound_group_init(&impl->engine, 0, nullptr, impl->groups.back().g.get());
  impl_ = std::move(impl);
  SetGroups({});
  ma_engine_listener_set_world_up(&impl_->engine, 0, 0, 1, 0);
  LogInfo("audio: miniaudio {} — {} Hz, {}", MA_VERSION_STRING, impl_->rate, impl_->device ? "output device" : "no device (mixed silently)");
  return true;
}

void AudioSystem::Shutdown() { impl_.reset(); }
bool AudioSystem::Running() const { return impl_ != nullptr; }
bool AudioSystem::HasDevice() const { return impl_ && impl_->device; }
int AudioSystem::SampleRate() const { return impl_ ? impl_->rate : 0; }

void AudioSystem::SetMasterVolume(float v) {
  if (!impl_) return;
  impl_->master = std::clamp(v, 0.f, 2.f);
  impl_->ApplyMaster();
}

void AudioSystem::SetMuted(bool muted) {
  if (!impl_ || impl_->muted == muted) return;
  impl_->muted = muted;
  impl_->ApplyMaster();
}

void AudioSystem::SetGroups(const std::vector<AudioGroupDef>& extra) {
  if (!impl_) return;
  std::vector<AudioGroupDef> all = kBuiltinGroups;
  for (const AudioGroupDef& d : extra) {
    auto it = std::find_if(all.begin(), all.end(), [&](const AudioGroupDef& a) { return a.name == d.name; });
    if (it != all.end()) *it = d;
    else all.push_back(d);
  }
  for (const AudioGroupDef& d : all) {
    if (d.name == "Master") { ma_sound_group_set_volume(impl_->groups[0].g.get(), std::max(d.volume, 0.f)); continue; }
    if (ma_sound_group* g = impl_->FindGroup(d.name)) { ma_sound_group_set_volume(g, std::max(d.volume, 0.f)); continue; }
    ma_sound_group* parent = impl_->FindGroup(d.parent.empty() ? "Master" : d.parent);
    if (!parent) {
      LogWarn("audio: group {}: no parent group {} (uses Master)", d.name, d.parent);
      parent = impl_->groups[0].g.get();
    }
    Impl::Group g{d.name, d.parent, std::make_unique<ma_sound_group>()};
    if (ma_sound_group_init(&impl_->engine, 0, parent, g.g.get()) != MA_SUCCESS) { LogWarn("audio: group {} failed", d.name); continue; }
    ma_sound_group_set_volume(g.g.get(), std::max(d.volume, 0.f));
    impl_->groups.push_back(std::move(g));
  }
}

bool AudioSystem::LoadMixer(const std::string& path, std::string* error) {
  std::string text;
  if (!ReadTextU8(path, text)) return true; // optional file
  yyjson_doc* d = yyjson_read(text.data(), text.size(), 0);
  if (!d) {
    if (error) *error = path + ": not JSON";
    return false;
  }
  std::vector<AudioGroupDef> defs;
  size_t i, n;
  yyjson_val* g;
  yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(d), "groups"), i, n, g) {
    AudioGroupDef def;
    const char* name = yyjson_get_str(yyjson_obj_get(g, "name"));
    if (!name || !*name) continue;
    def.name = name;
    if (const char* p = yyjson_get_str(yyjson_obj_get(g, "parent"))) def.parent = p;
    if (yyjson_val* v = yyjson_obj_get(g, "volume"); yyjson_is_num(v)) def.volume = (float)yyjson_get_num(v);
    defs.push_back(std::move(def));
  }
  yyjson_doc_free(d);
  SetGroups(defs);
  return true;
}

bool AudioSystem::SetGroupVolume(const std::string& group, float volume) {
  ma_sound_group* g = impl_ ? impl_->FindGroup(group) : nullptr;
  if (!g) return false;
  ma_sound_group_set_volume(g, std::clamp(volume, 0.f, 4.f));
  return true;
}

float AudioSystem::GroupVolume(const std::string& group) const {
  ma_sound_group* g = impl_ ? impl_->FindGroup(group) : nullptr;
  return g ? ma_sound_group_get_volume(g) : -1.f;
}

std::vector<std::string> AudioSystem::Groups() const {
  std::vector<std::string> out;
  if (impl_)
    for (const Impl::Group& g : impl_->groups) out.push_back(g.name);
  return out;
}

void AudioSystem::SetSearchFolders(std::vector<std::string> folders) {
  if (!impl_) return;
  impl_->folders = std::move(folders);
  impl_->nameIndex.clear();
  impl_->indexed = false;
}

std::string AudioSystem::ResolveClip(const std::string& clip) const {
  if (clip.empty()) return {};
  std::error_code ec;
  const fs::path p = PathU8(clip);
  if (p.is_absolute()) return fs::exists(p, ec) ? clip : std::string();
  if (!impl_) return {};
  if (p.has_parent_path() && fs::exists(p, ec)) return clip; // relative to the project (working folder)
  for (const std::string& f : impl_->folders) {
    const fs::path c = PathU8(f) / p;
    if (fs::exists(c, ec)) {
      const std::u8string u = c.generic_u8string();
      return std::string(u.begin(), u.end());
    }
  }
  if (!impl_->indexed) { // a bare file name: look it up anywhere under the folders (once)
    impl_->indexed = true;
    for (const std::string& f : impl_->folders)
      for (const auto& e : fs::recursive_directory_iterator(PathU8(f), ec))
        if (e.is_regular_file()) {
          const std::u8string name = e.path().filename().u8string(), full = e.path().generic_u8string();
          impl_->nameIndex.emplace(std::string(name.begin(), name.end()), std::string(full.begin(), full.end()));
        }
  }
  auto it = impl_->nameIndex.find(clip);
  return it == impl_->nameIndex.end() ? std::string() : it->second;
}

uint32_t AudioSystem::Play(const AudioPlay& p, std::string* error) {
  if (!impl_) {
    if (error) *error = "audio is not running";
    return 0;
  }
  const std::string file = ResolveClip(p.clip);
  if (file.empty()) {
    if (error) *error = p.clip.empty() ? std::string("no clip") : std::format("no sound file \"{}\"", p.clip);
    return 0;
  }
  ma_sound_group* group = impl_->FindGroup(p.group);
  if (!group) group = impl_->FindGroup("Effects");
  auto s = std::make_unique<ma_sound>();
  std::error_code ec;
  const uintmax_t size = fs::file_size(PathU8(file), ec);
  const ma_uint32 flags = (size > 4u * 1024 * 1024 ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE) | (p.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION);
  const std::wstring wide = PathU8(file).wstring();
  const ma_result r = ma_sound_init_from_file_w(&impl_->engine, wide.c_str(), flags, group, nullptr, s.get());
  if (r != MA_SUCCESS) {
    if (error) *error = std::format("cannot play \"{}\": {}", p.clip, ma_result_description(r));
    return 0;
  }
  ma_sound_set_volume(s.get(), std::max(p.volume, 0.f));
  ma_sound_set_pitch(s.get(), std::clamp(p.pitch, 0.05f, 8.f));
  ma_sound_set_looping(s.get(), p.loop);
  if (p.spatial) {
    ma_sound_set_position(s.get(), p.position.x, p.position.y, p.position.z);
    ma_sound_set_attenuation_model(s.get(), p.rolloff == 1 ? ma_attenuation_model_linear : p.rolloff == 2 ? ma_attenuation_model_exponential : ma_attenuation_model_inverse);
    ma_sound_set_min_distance(s.get(), std::max(p.minDistance, 0.001f));
    ma_sound_set_max_distance(s.get(), std::max(p.maxDistance, p.minDistance));
    ma_sound_set_rolloff(s.get(), std::max(p.rolloffFactor, 0.f));
    ma_sound_set_doppler_factor(s.get(), 0.f);
  }
  if (impl_->paused) {
    impl_->sounds[impl_->next] = {std::move(s), true};
  } else {
    ma_sound_start(s.get());
    impl_->sounds[impl_->next] = {std::move(s), false};
  }
  ++impl_->stats.started;
  return impl_->next++;
}

void AudioSystem::Stop(uint32_t id) {
  if (!impl_) return;
  auto it = impl_->sounds.find(id);
  if (it == impl_->sounds.end()) return;
  UninitSound(it->second.s);
  impl_->sounds.erase(it);
}

void AudioSystem::StopAll() {
  if (!impl_) return;
  for (auto& [id, s] : impl_->sounds) UninitSound(s.s);
  impl_->sounds.clear();
}

bool AudioSystem::IsPlaying(uint32_t id) const {
  if (!impl_) return false;
  auto it = impl_->sounds.find(id);
  return it != impl_->sounds.end() && (it->second.pausedByUs || (ma_sound_is_playing(it->second.s.get()) && !ma_sound_at_end(it->second.s.get())));
}

void AudioSystem::SetPosition(uint32_t id, const Vec3& p) {
  if (!impl_) return;
  if (auto it = impl_->sounds.find(id); it != impl_->sounds.end()) ma_sound_set_position(it->second.s.get(), p.x, p.y, p.z);
}
void AudioSystem::SetVolume(uint32_t id, float v) {
  if (!impl_) return;
  if (auto it = impl_->sounds.find(id); it != impl_->sounds.end()) ma_sound_set_volume(it->second.s.get(), std::max(v, 0.f));
}
void AudioSystem::SetPitch(uint32_t id, float p) {
  if (!impl_) return;
  if (auto it = impl_->sounds.find(id); it != impl_->sounds.end()) ma_sound_set_pitch(it->second.s.get(), std::clamp(p, 0.05f, 8.f));
}

void AudioSystem::SetPaused(bool paused) {
  if (!impl_ || impl_->paused == paused) return;
  impl_->paused = paused;
  for (auto& [id, s] : impl_->sounds) {
    if (paused && ma_sound_is_playing(s.s.get())) {
      ma_sound_stop(s.s.get());
      s.pausedByUs = true;
    } else if (!paused && s.pausedByUs) {
      ma_sound_start(s.s.get());
      s.pausedByUs = false;
    }
  }
}

void AudioSystem::SetListener(const Vec3& position, const Vec3& forward, const Vec3& up, float volume) {
  if (!impl_) return;
  ma_engine_listener_set_position(&impl_->engine, 0, position.x, position.y, position.z);
  ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
  ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
  if (std::fabs(impl_->listener - volume) > 1e-6f) {
    impl_->listener = std::max(volume, 0.f);
    impl_->ApplyMaster();
  }
}

void AudioSystem::Update() {
  if (!impl_) return;
  for (auto it = impl_->sounds.begin(); it != impl_->sounds.end();) {
    ma_sound* s = it->second.s.get();
    if (!it->second.pausedByUs && !ma_sound_is_looping(s) && ma_sound_at_end(s)) {
      UninitSound(it->second.s);
      it = impl_->sounds.erase(it);
    } else {
      ++it;
    }
  }
}

void AudioSystem::Pump(double seconds) {
  if (!impl_ || impl_->device || seconds <= 0) return;
  ma_uint64 frames = (ma_uint64)std::llround(seconds * impl_->rate);
  std::vector<float> buf(4096 * 2);
  double sum = 0;
  float peak = 0;
  ma_uint64 total = 0;
  while (frames > 0) {
    const ma_uint64 n = std::min<ma_uint64>(frames, 4096);
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&impl_->engine, buf.data(), n, &read);
    if (read == 0) break;
    for (ma_uint64 i = 0; i < read * 2; ++i) {
      sum += (double)buf[i] * buf[i];
      peak = std::max(peak, std::fabs(buf[i]));
    }
    total += read;
    frames -= read;
  }
  impl_->stats.framesMixed += total;
  impl_->stats.rms = total ? (float)std::sqrt(sum / (double)(total * 2)) : 0.f;
  impl_->stats.peak = peak;
}

AudioStats AudioSystem::Stats() const {
  if (!impl_) return {};
  AudioStats s = impl_->stats;
  s.playing = 0;
  for (const auto& [id, snd] : impl_->sounds)
    if (snd.pausedByUs || (ma_sound_is_playing(snd.s.get()) && !ma_sound_at_end(snd.s.get()))) ++s.playing;
  return s;
}

} // namespace bdg
