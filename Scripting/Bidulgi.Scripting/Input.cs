// Input actions for scripts (queue 6-4, Docs/features/input.md): game code reads actions, never keys. The mapping
// (keys, mouse, gamepad -> "Jump", "Move") lives in Input/Actions.jsonl and Input/Contexts/*.jsonl.
//
//   if (Input.Action("Jump").pressed) Jump();            // this frame (in FixedUpdate: since the last fixed step)
//   Vector2 move = Input.Value<Vector2>("Move");
//   if (Input.Down("Fire")) ...;  float t = Input.Value<float>("Throttle");
//   Input.EnableContext("Vehicle");                       // its bindings win over lower-priority contexts
using System;
using System.Collections.Generic;
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

public readonly struct InputActionState {
  public string name { get; init; }
  public bool down { get; init; }     // held now
  public bool pressed { get; init; }  // went down this frame (FixedUpdate: since the previous fixed step)
  public bool released { get; init; } // went up this frame (FixedUpdate: since the previous fixed step)
  public float value { get; init; }   // button 0 / 1, axis, or the x of a vector
  public Vector2 vector { get; init; }
  public Vector3 vector3 { get; init; }
  public bool exists { get; init; }   // false: no such action in Actions.jsonl
}

public static class Input {
  static bool s_inFixed;
  static readonly Dictionary<string, (long presses, long releases)> s_fixedSeen = new(); // counts at the last fixed step
  static readonly Dictionary<string, (long presses, long releases)> s_latest = new();

  public static InputActionState Action(string name) {
    JsonNode? n = Engine.Request(new JsonObject { ["op"] = "input", ["action"] = name });
    if (n is not JsonObject o) return new InputActionState { name = name };
    var v = o["value"] as JsonArray;
    Vector3 v3 = v != null && v.Count == 3 ? new Vector3((float)v[0]!, (float)v[1]!, (float)v[2]!) : Vector3.Zero;
    bool pressed = o["pressed"]?.GetValue<bool>() ?? false, released = o["released"]?.GetValue<bool>() ?? false;
    long presses = o["presses"]?.GetValue<long>() ?? 0, releases = o["releases"]?.GetValue<long>() ?? 0;
    s_latest[name] = (presses, releases);
    if (s_inFixed) { // a press between two fixed steps counts once, in the next FixedUpdate
      s_fixedSeen.TryGetValue(name, out var seen); // counts start at 0 when play starts
      pressed = presses > seen.presses;
      released = releases > seen.releases;
    }
    return new InputActionState {
      name = name, exists = true, down = o["down"]?.GetValue<bool>() ?? false, pressed = pressed, released = released,
      value = v3.X, vector = new Vector2(v3.X, v3.Y), vector3 = v3,
    };
  }

  public static bool Pressed(string action) => Action(action).pressed;
  public static bool Released(string action) => Action(action).released;
  public static bool Down(string action) => Action(action).down;
  public static T Value<T>(string action) {
    InputActionState s = Action(action);
    object v = typeof(T) == typeof(Vector2) ? s.vector : typeof(T) == typeof(Vector3) ? s.vector3 : typeof(T) == typeof(bool) ? s.down
             : typeof(T) == typeof(double) ? (double)s.value : (object)s.value;
    return (T)v;
  }
  // Turns a mapping context (Input/Contexts/<file>.jsonl, by its "context" name) on or off.
  public static void EnableContext(string name, bool on = true) =>
    Engine.Request(new JsonObject { ["op"] = "inputcontext", ["name"] = name, ["on"] = on });

  // Driven by the behaviour runner around each fixed step, and at the end of play.
  internal static void BeginFixed() => s_inFixed = true;
  internal static void EndFixed() {
    s_inFixed = false;
    foreach (var (name, counts) in s_latest) s_fixedSeen[name] = counts; // what this fixed step has seen
  }
  internal static void Reset() {
    s_inFixed = false;
    s_fixedSeen.Clear();
    s_latest.Clear();
  }
}
