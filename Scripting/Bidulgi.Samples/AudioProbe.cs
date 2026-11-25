// Sound from C# (queue 6-3), checked by BDG_AUDIO_TEST: Audio.Play (2D and 3D), a Sound handle, mixer group
// volumes, a missing clip (a warning, no exception) and the AudioSource component's own Play.
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class AudioProbe : Behaviour {
  [Edit] string clip = "Beep.wav";

  async void Start() {
    Sound flat = Audio.Play(clip, volume: 0.5f, group: "UI");
    Sound spatial = Audio.Play(clip, at: new Vector3(3, 0, 0), loop: true);
    bool play = flat.Playing && spatial.Playing;
    Audio.SetGroupVolume("UI", 0.25f);
    bool group = System.MathF.Abs(Audio.GroupVolume("UI") - 0.25f) < 1e-4f;
    bool missing = !Audio.Play("No such sound.wav").Playing;
    AudioSource? source = Get<AudioSource>();
    source?.Play();
    await NextFrame();
    spatial.Stop();
    bool stopped = !spatial.Playing;
    Audio.SetGroupVolume("UI", 1f);
    int B(bool v) => v ? 1 : 0;
    Debug.Log($"audio probe: play={B(play)} group={B(group)} missing={B(missing)} component={B(source != null)} stopped={B(stopped)}");
  }
}
