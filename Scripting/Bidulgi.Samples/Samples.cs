// Sample Behaviours (queue 4-2). Event methods are found by name like Unity's (private is fine).
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

// Turns its object around Y at 90 degrees per second of game time.
public class Spinner : Behaviour {
  void Update() {
    Transform t = transform;
    Vector3 r = t.Rotation;
    t.Rotation = new Vector3(r.X, r.Y + 90f * Time.deltaTime, r.Z);
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
