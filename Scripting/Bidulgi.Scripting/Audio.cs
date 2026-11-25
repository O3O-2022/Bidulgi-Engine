// Sound from C# (queue 6-3), the easy-API way (Docs/features/easy-api.md):
//
//   Audio.Play("Shot.wav");                                   // 2D (UI, music, ...)
//   Audio.Play("Boom.wav", at: transform.Position, volume: 0.8f); // 3D at a place: quieter with distance
//   Sound music = Audio.Play("Theme.mp3", group: "Music", loop: true);  music.Stop();
//   Audio.SetGroupVolume("Music", 0.3f);
//
// Clips are found like an AudioSource's: an Assets-relative path or just the file name. Sounds play in mixer groups
// (Master > Music, Effects, Voice, UI, Ambient; more in ProjectSettings/AudioMixer.json). Everything stops when the
// game stops. A missing clip is a warning and an empty Sound (Playing = false), not an exception.
// An AudioSource component can be played from scripts too: Get<AudioSource>().Play().
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

// A playing sound. Stop() any time; Playing turns false when it ends.
public sealed class Sound {
  public static readonly Sound None = new(0);
  public int Id { get; }
  internal Sound(int id) => Id = id;
  public bool Playing => Id != 0 && Engine.Request(new JsonObject { ["op"] = "audio.playing", ["id"] = Id })!.GetValue<bool>();
  public void Stop() { if (Id != 0) Engine.Request(new JsonObject { ["op"] = "audio.stop", ["id"] = Id }); }
}

public static class Audio {
  public static Sound Play(string clip, Vector3? at = null, float volume = 1f, float pitch = 1f, string group = "Effects", bool loop = false,
                           float minDistance = 1f, float maxDistance = 50f) {
    var r = new JsonObject { ["op"] = "audio.play", ["clip"] = clip, ["group"] = group, ["volume"] = volume, ["pitch"] = pitch, ["loop"] = loop };
    if (at is Vector3 p) { r["at"] = Engine.ToNode(p); r["minDistance"] = minDistance; r["maxDistance"] = maxDistance; }
    try { return new Sound(Engine.Request(r)!.GetValue<int>()); }
    catch (EngineException e) { Debug.LogWarning("Audio.Play: " + e.Message); return Sound.None; }
  }
  public static void SetGroupVolume(string group, float volume) =>
    Engine.Request(new JsonObject { ["op"] = "audio.volume", ["group"] = group, ["volume"] = volume });
  public static float GroupVolume(string group) =>
    Engine.Request(new JsonObject { ["op"] = "audio.volume", ["group"] = group })!.GetValue<float>();
  public static void StopAll() => Engine.Request(new JsonObject { ["op"] = "audio.stopall" });
}
