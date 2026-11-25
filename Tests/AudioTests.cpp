// Audio (queue 6-3): miniaudio without an output device; the mixed level is measured, so the tests hear the
// sound: it plays, gets quieter with distance, follows group volumes, pauses and stops.
#include "Audio/Audio.h"
#include "bdg_test.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
// A mono 16-bit WAV with a sine tone (what the checks play).
void WriteToneWav(const fs::path& p, int rate, double seconds, double hz) {
  const uint32_t frames = (uint32_t)(rate * seconds);
  std::vector<int16_t> pcm(frames);
  for (uint32_t i = 0; i < frames; ++i) pcm[i] = (int16_t)(std::sin(6.283185307 * hz * i / rate) * 16000);
  std::ofstream f(p, std::ios::binary);
  auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
  auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
  f.write("RIFF", 4); u32(36 + frames * 2); f.write("WAVE", 4);
  f.write("fmt ", 4); u32(16); u16(1); u16(1); u32((uint32_t)rate); u32((uint32_t)rate * 2); u16(2); u16(16);
  f.write("data", 4); u32(frames * 2);
  f.write(reinterpret_cast<const char*>(pcm.data()), (std::streamsize)pcm.size() * 2);
}
std::string S(const fs::path& p) {
  const std::u8string u = p.generic_u8string();
  return std::string(u.begin(), u.end());
}
} // namespace

TEST_CASE("audio: sounds play, fade with distance, follow mixer groups, pause and stop (queue 6-3)") {
  const fs::path d = fs::temp_directory_path() / "bdg_audio";
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d / "Sounds");
  WriteToneWav(d / "Sounds" / "Beep.wav", 48000, 2.0, 440);
  AudioSystem& a = AudioSystem::Instance();
  std::string err;
  CHECK(a.Init(48000, false, &err)); // no device: mixed and measured
  CHECK(!a.HasDevice() && a.SampleRate() == 48000);
  a.SetSearchFolders({S(d)});
  CHECK(a.ResolveClip("Sounds/Beep.wav").ends_with("Sounds/Beep.wav"));
  CHECK(a.ResolveClip("Beep.wav").ends_with("Sounds/Beep.wav")); // a bare file name is found too
  CHECK(a.Play({"Nope.wav"}, &err) == 0 && err.find("Nope.wav") != std::string::npos);
  a.SetListener({0, 0, 0}, {0, 0, -1}, {0, 1, 0});

  // 2D: full level
  AudioPlay p;
  p.clip = "Beep.wav";
  const uint32_t flat = a.Play(p, &err);
  CHECK(flat != 0 && a.IsPlaying(flat));
  a.Pump(0.1);
  const float loud = a.Stats().rms;
  CHECK(loud > 0.1f);
  a.Stop(flat);
  CHECK(!a.IsPlaying(flat));
  a.Pump(0.05); // (the mixer's buffered tail)
  a.Pump(0.05);
  CHECK(a.Stats().rms < 1e-4f);

  // 3D: the same sound at 1 m and at 30 m in front of the listener
  p.spatial = true;
  p.loop = true;
  p.position = {0, 0, -1};
  const uint32_t s3 = a.Play(p, &err);
  a.Pump(0.1);
  const float near = a.Stats().rms;
  a.SetPosition(s3, {0, 0, -30});
  a.Pump(0.1);
  a.Pump(0.1); // (the spatializer smooths the change)
  const float far = a.Stats().rms;
  std::printf("  audio levels: flat %.3f, near %.3f, far %.3f\n", loud, near, far);
  CHECK(near > 0.05f && far < near * 0.2f && far > 0.f);

  // mixer groups: Effects at 0 silences it, back to 1 brings it back; unknown groups are refused
  a.SetPosition(s3, {0, 0, -1});
  CHECK(a.SetGroupVolume("Effects", 0.f) && a.GroupVolume("Effects") == 0.f);
  a.Pump(0.1);
  a.Pump(0.1);
  CHECK(a.Stats().rms < 1e-3f);
  CHECK(a.SetGroupVolume("Effects", 1.f));
  CHECK(!a.SetGroupVolume("Nothing", 1.f) && a.GroupVolume("Nothing") < 0);
  a.SetGroups({{"Footsteps", "Effects", 0.5f}});
  CHECK(a.GroupVolume("Footsteps") == 0.5f);

  // pause keeps the sound, resume continues it; StopAll ends everything
  a.SetPaused(true);
  a.Pump(0.05);
  a.Pump(0.05);
  CHECK(a.Stats().rms < 1e-4f && a.IsPlaying(s3));
  a.SetPaused(false);
  a.Pump(0.1);
  CHECK(a.Stats().rms > 0.05f);
  // a short sound ends by itself and is forgotten
  p.loop = false;
  p.spatial = false;
  const uint32_t once = a.Play(p, &err);
  a.Pump(2.1);
  a.Update();
  CHECK(!a.IsPlaying(once));
  a.StopAll();
  CHECK(a.Stats().playing == 0 && a.Stats().started >= 3);
  a.Shutdown();
  fs::remove_all(d, ec);
}
