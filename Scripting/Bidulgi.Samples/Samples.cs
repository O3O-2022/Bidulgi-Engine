// Sample Behaviours (queue 4-2). Event methods are found by name like Unity's (private is fine).
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public enum SpinAxis { X, Y, Z }

// Turns its object around an axis. Its [Edit] fields show in the Inspector and are saved with the scene (4-3);
// `frames` is changed by the script itself, so the Inspector shows it counting while the game runs.
public class Spinner : Behaviour {
  [Edit, Range(0, 720), Tooltip("Degrees per second of game time")] public float speed = 90f;
  [Edit] public SpinAxis axis = SpinAxis.Y;
  [Edit, Header("Info")] public string label = "spinner";
  [Edit, Tooltip("Frames this spinner has turned (set by the script)")] int frames;
#if HOTRELOAD_V2
  // The second build of this file (hot reload test, 4-4): a new field, and Update counts it up.
  [Edit] public int bonus = 7;
#endif

  void Update() {
    Transform t = transform;
    Vector3 r = t.Rotation;
    float step = speed * Time.deltaTime;
    t.Rotation = axis == SpinAxis.X ? new Vector3(r.X + step, r.Y, r.Z) : axis == SpinAxis.Y ? new Vector3(r.X, r.Y + step, r.Z) : new Vector3(r.X, r.Y, r.Z + step);
    ++frames;
#if HOTRELOAD_V2
    ++bonus;
#endif
  }
}

// Records the order of its events and reports it when destroyed (lifecycle tests).
public class LifecycleProbe : Behaviour {
  readonly List<string> _events = new();
  int _updates, _fixed, _late, _enables, _disables;
  float _time;
  bool _orderOk = true;

  void Note(string e) {
    if (_events.Count == 0 || _events[^1] != e) _events.Add(e);
  }
  void Awake() { if (_events.Count != 0) _orderOk = false; Note("Awake"); }
  void OnEnable() { ++_enables; Note("OnEnable"); }
  void Start() { if (_updates != 0 || _fixed != 0) _orderOk = false; Note("Start"); }
  void Update() { ++_updates; _time += Time.deltaTime; Note("Update"); }
  void FixedUpdate() { if (_updates == 0) _orderOk = false; ++_fixed; }
  void LateUpdate() { if (_updates == 0) _orderOk = false; ++_late; }
  void OnDisable() { ++_disables; Note("OnDisable"); }
  void OnDestroy() {
    Note("OnDestroy");
    Debug.Log($"probe: {gameObject.Id} awake={(_events.Count > 0 && _events[0] == "Awake" ? 1 : 0)} enable={_enables} start={(_events.Contains("Start") ? 1 : 0)} " +
              $"update={_updates} fixed={_fixed} late={_late} disable={_disables} destroy=1 order={(_orderOk ? "ok" : "bad")} time={_time:0.00} " +
              $"events={string.Join(",", _events)}");
  }
}

// Throws in Update: its errors are logged (at most 10) and the other behaviours keep running.
public class Faulty : Behaviour {
  void Update() => throw new System.InvalidOperationException("sample failure");
}

// Uses another component and another Behaviour from C#.
public class Medic : Behaviour {
  void Start() {
    Health? h = GetComponent<Health>();
    if (h != null) h.Current = h.MaxHealth;
    if (GetComponent<Spinner>() != null) Debug.Log("medic: found the spinner");
  }
}
