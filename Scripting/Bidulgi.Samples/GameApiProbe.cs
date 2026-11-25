// Exercises the game API of queue 6-5b and the particles of 6-VFX P-1 in play mode (BDG_GAMEAPI_TEST) and reports
// one line per run. The scene: this object at the origin, a static "Ground" box (top at y = 0), a static "Crate" box
// at (0, 1, -5), a "Listener" object with a second GameApiProbe (listener = true), and Effects/TestSpark.vfx in the
// assets. The test plays twice: the second run must read what the first one saved.
using System;
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public record ProbeHit(string what, int points);

public class GameApiProbe : Behaviour {
  [Edit] bool listener = false;
  [Edit] int heard; // listener: how many "Ping" events reached it

  int _pings, _scores, _typed;
  bool _save, _events, _raycast, _overlap, _vfx, _vfxSpawned, _missing;
  Effect _spark = Effect.None;

  void Start() {
    if (listener) {
      Events.On("Ping", () => heard++);
      return;
    }
    // saves: the first run finds nothing and writes; the second finds what the first wrote
    int runs = Save.Get("runs", 0);
    bool restored = runs == 0 || (Save.Get("where", Vector3.Zero) == new Vector3(1, 2, 3) && Save.Get("names", new List<string>()).Count == 2 &&
                                  Save.Get("title", "") == "비둘기" && Save.Get("missing", 7) == 7);
    Save.Set("runs", runs + 1);
    Save.Set("where", new Vector3(1, 2, 3));
    Save.Set("names", new List<string> { "a", "b" });
    Save.Set("title", "비둘기");
    _save = restored && Save.Write() && Save.Exists("main") && Save.Get("runs", 0) == runs + 1;
    Debug.Log($"game api probe: run={runs + 1} restored={(restored ? 1 : 0)} folder={Save.Folder}");

    // events: by name, with a value, by type; Off; a handler of a destroyed behaviour stops
    Action ping = () => _pings++;
    Events.On("Ping", ping);
    Events.On<int>("Score", v => _scores += v);
    Events.On<ProbeHit>(e => _typed += e.points);
    int ran = Events.Emit("Ping"); // this one and the listener's
    Events.Emit("Score", 5);
    Events.Send(new ProbeHit("crate", 3));
    Events.Off("Ping", ping);
    int ranAfterOff = Events.Emit("Ping"); // only the listener
    GameObject listenerObj = Find("Listener");
    Destroy(listenerObj); // its handler goes with it
    int ranAfterDestroy = Events.Emit("Ping");
    _events = ran == 2 && ranAfterOff == 1 && ranAfterDestroy == 0 && _pings == 1 && _scores == 5 && _typed == 3 && Events.Count("Ping") == 0;

    After(0.1f, Queries); // after the first physics steps
  }

  void Queries() {
    bool ahead = Raycast(new Vector3(0, 1, 0), new Vector3(0, 0, -1), out RaycastHit hit, 100);
    bool down = Physics.Raycast(new Vector3(2, 5, 2), new Vector3(0, -1, 0), out RaycastHit floor);
    bool miss = !Raycast(new Vector3(0, 1, 0), new Vector3(0, 0, 1), out _, 100);
    _raycast = ahead && hit.gameObject.Name == "Crate" && MathF.Abs(hit.distance - 4.5f) < 0.05f && hit.normal.Z > 0.99f &&
               down && floor.gameObject.Name == "Ground" && MathF.Abs(floor.point.Y) < 0.05f && miss;
    List<GameObject> near = OverlapSphere(new Vector3(0, 1, -4.2f), 0.5f);
    _overlap = near.Count == 1 && near[0].Name == "Crate" && OverlapSphere(new Vector3(0, 3, 0), 0.5f).Count == 0 &&
               OverlapSphere(new Vector3(0, 1, -4.2f), 0.5f, "Ground").Count == 0;

    _spark = Vfx.Play("TestSpark", new Vector3(0, 1, -2));
    _missing = Vfx.Play("NoSuchEffect", Vector3.Zero) == Effect.None;
    After(0.05f, () => _vfxSpawned = _spark.Playing && _spark.Particles == 12 && Vfx.Particles >= 12);
    After(1.0f, Report); // the sparks live 0.3 s: gone by now, the one-shot effect removed itself
  }

  void Report() {
    _vfx = _vfxSpawned && !_spark.Playing && _spark.Particles == 0 && _missing;
    Debug.Log($"game api probe: save={(_save ? 1 : 0)} events={(_events ? 1 : 0)} raycast={(_raycast ? 1 : 0)} overlap={(_overlap ? 1 : 0)} vfx={(_vfx ? 1 : 0)}");
  }
}
