// Easy API for C# scripts (queue 4-4c, Docs/features/easy-api.md): the common jobs in one line, with the same names
// the other languages will use. Inside a Behaviour they are plain calls (Get<Health>(), Find("Player"),
// After(2, Explode), await Wait(1), transform.Tween(position: p, seconds: 0.5f)); elsewhere use the Game class
// (Game.Find, Game.Spawn, ...).
//
// Safe defaults: Find of a missing object logs a warning and gives an invalid GameObject (false in an `if`), not an
// exception; a handle can be checked with `if (target)` or target.IsValid.
using System;
using System.Collections.Generic;
using System.Numerics;
using System.Reflection;
using System.Text.Json.Nodes;

namespace Bidulgi;

// [Auto] fills a component field before Awake with Get<T>() (InChildren: GetInChildren<T>()); a warning when there
// is none. Works for engine components and C# Behaviours (those earlier on the object, or on other objects).
[AttributeUsage(AttributeTargets.Field)]
public sealed class AutoAttribute : Attribute {
  public bool InChildren { get; set; }
}

public sealed partial class GameObject {
  // An invalid handle (what Find gives when nothing matches).
  public static readonly GameObject None = new("");
  public bool IsValid => Id.Length > 0 && Engine.Attached && Exists(Id);
  public static implicit operator bool(GameObject? g) => g is not null && g.IsValid;
  public static bool operator ==(GameObject? a, GameObject? b) => a is null ? b is null : b is not null && a.Id == b.Id;
  public static bool operator !=(GameObject? a, GameObject? b) => !(a == b);
  static bool Exists(string id) {
    try { return Engine.Request(new JsonObject { ["op"] = "find", ["name"] = id })?.GetValue<string>() == id; }
    catch (EngineException) { return false; }
  }
  public Transform transform => GetComponent<Transform>() ?? throw new EngineException(Id + " has no Transform");
  // World position (parents applied).
  public Vector3 position {
    get {
      foreach (JsonNode? n in Engine.Request(new JsonObject { ["op"] = "findall", ["name"] = Name })!.AsArray())
        if (n?["id"]?.GetValue<string>() == Id) return Engine.FromNode<Vector3>(n["pos"]);
      return Vector3.Zero;
    }
  }
  public GameObject[] Children(bool deep = false) {
    var list = new List<GameObject>();
    foreach (JsonNode? n in Engine.Request(new JsonObject { ["op"] = "children", ["target"] = Id, ["deep"] = deep })!.AsArray())
      list.Add(new GameObject(n!.GetValue<string>()));
    return list.ToArray();
  }
  public override bool Equals(object? obj) => obj is GameObject g && g.Id == Id;
  public override int GetHashCode() => Id.GetHashCode();
}

// The easy functions, usable from any code (Behaviour has the same ones as members).
public static class Game {
  // ---- finding ----
  public static T? Get<T>(GameObject go) where T : class => go.Id.Length == 0 ? null : go.GetComponent<T>();
  public static T? GetInChildren<T>(GameObject go) where T : class {
    if (go.Id.Length == 0) return null;
    if (go.GetComponent<T>() is T own) return own;
    foreach (GameObject c in go.Children(deep: true))
      if (c.GetComponent<T>() is T found) return found;
    return null;
  }
  // By name or id; a warning and GameObject.None when there is none.
  public static GameObject Find(string nameOrId) {
    GameObject? g = GameObject.Find(nameOrId);
    if (g != null) return g;
    Debug.LogWarning($"Find: no object \"{nameOrId}\"");
    return GameObject.None;
  }
  // Every object with this name ("" = every object), in hierarchy order.
  public static List<GameObject> FindAll(string name) {
    var list = new List<GameObject>();
    foreach (var (id, _) in Positions(name)) list.Add(new GameObject(id));
    return list;
  }
  // The nearest object named `name` ("" or null = any) within `radius` of `from`; GameObject.None when none.
  public static GameObject FindNearest(string? name, Vector3 from, float radius = float.PositiveInfinity, GameObject? except = null) {
    string best = "";
    float bestD = radius * radius;
    foreach (var (id, pos) in Positions(name ?? "")) {
      if (except != null && id == except.Id) continue;
      float d = Vector3.DistanceSquared(pos, from);
      if (d <= bestD) { bestD = d; best = id; }
    }
    return best.Length == 0 ? GameObject.None : new GameObject(best);
  }
  static IEnumerable<(string id, Vector3 pos)> Positions(string name) {
    foreach (JsonNode? n in Engine.Request(new JsonObject { ["op"] = "findall", ["name"] = name })!.AsArray())
      yield return (n!["id"]!.GetValue<string>(), Engine.FromNode<Vector3>(n["pos"]));
  }

  // ---- making and removing ----
  // A copy of `original` (with its children and C# behaviours, which wake up at the start of the next frame).
  // Prefab assets come with queue 6-1; until then any object of the scene (hidden away, say) is the template.
  public static GameObject Spawn(GameObject original, Vector3? position = null, Vector3? rotation = null, GameObject? parent = null, string? name = null) {
    if (original.Id.Length == 0) { Debug.LogWarning("Spawn: the original is not a valid object"); return GameObject.None; }
    return SpawnRequest(original.Id, name, position, rotation, parent);
  }
  // A new empty object with a Transform.
  public static GameObject Spawn(string name, Vector3? position = null, Vector3? rotation = null, GameObject? parent = null) =>
    SpawnRequest("", name, position, rotation, parent);
  static GameObject SpawnRequest(string source, string? name, Vector3? position, Vector3? rotation, GameObject? parent) {
    var r = new JsonObject { ["op"] = "spawn", ["source"] = source, ["name"] = name ?? "", ["parent"] = parent?.Id ?? "" };
    if (position is Vector3 p) r["position"] = Engine.ToNode(p);
    if (rotation is Vector3 q) r["rotation"] = Engine.ToNode(q);
    return new GameObject(Engine.Request(r)!.GetValue<string>());
  }
  // Removes the object and its children (after: seconds of game time later). Their behaviours get OnDisable and
  // OnDestroy right away, and their timers, tweens and waits are cancelled.
  public static void Destroy(GameObject go, float after = 0) {
    if (go.Id.Length == 0) return;
    if (after > 0) { Scheduler.AfterUnowned(after, () => Destroy(go)); return; }
    JsonNode? gone;
    try { gone = Engine.Request(new JsonObject { ["op"] = "destroy", ["target"] = go.Id }); }
    catch (EngineException e) { Debug.LogWarning("Destroy: " + e.Message); return; }
    var ids = new HashSet<string>();
    foreach (JsonNode? n in gone!.AsArray()) ids.Add(n!.GetValue<string>());
    Behaviours.ObjectsGone(ids);
  }

  // ---- time ----
  // Runs `action` once, `seconds` of game time from now. Cancelled when the calling behaviour is disabled or destroyed.
  public static Timer After(float seconds, Action action) => Scheduler.Every(seconds, 0, action);
  // Runs `action` every `seconds` (the first time after `first` seconds; default: one interval). Cancel() stops it.
  public static Timer Every(float seconds, Action action, float? first = null) =>
    Scheduler.Every(first ?? seconds, Math.Max(seconds, 1e-4f), action);
  // await Wait(2): resumes 2 seconds of game time later (pause and time scale apply), never in the same frame.
  public static Waiting Wait(float seconds) => Scheduler.Wait(seconds);
  // await NextFrame(): resumes after Update of the next frame.
  public static Waiting NextFrame() => Scheduler.Wait(0);
  // await WaitUntil(() => door.Open): checked once per frame after Update.
  public static Waiting WaitUntil(Func<bool> condition) => Scheduler.Wait(0, condition);

  // Tweens any number: set(value) every frame from `from` to `to`.
  public static Tween Tween(float from, float to, float seconds, Action<float> set, Ease ease = Ease.OutQuad, bool pingPong = false) =>
    new(Scheduler.Tween(seconds, ease, pingPong, p => set(from + (to - from) * p)));
}

// A Vector3 tween target that also takes one number (scale: 1.2f = uniform).
public readonly struct TweenValue {
  public readonly Vector3 Value;
  public TweenValue(Vector3 v) => Value = v;
  public static implicit operator TweenValue(Vector3 v) => new(v);
  public static implicit operator TweenValue(float f) => new(new Vector3(f));
}

// A running tween: await it (completes when it ends; OperationCanceledException when cancelled), or Cancel() it.
public sealed class Tween {
  readonly Scheduler.TweenJob _job;
  internal Tween(Scheduler.TweenJob job) => _job = job;
  public bool Playing => !_job.Done;
  public void Cancel() => _job.Cancel();
  public Waiting GetAwaiter() => _job.W;
}

public static class TweenExtensions {
  // transform.Tween(position: target, seconds: 0.5f, ease: Ease.OutBack): moves the Transform's local values from
  // where they are now to the targets given (others untouched). pingPong: there and back (twice the seconds).
  public static Tween Tween(this Transform t, TweenValue? position = null, TweenValue? rotation = null, TweenValue? scale = null,
                            float seconds = 0.5f, Ease ease = Ease.OutQuad, bool pingPong = false) {
    Vector3 p0 = position.HasValue ? t.Position : default, r0 = rotation.HasValue ? t.Rotation : default, s0 = scale.HasValue ? t.Scale : default;
    return new Tween(Scheduler.Tween(seconds, ease, pingPong, k => {
      if (position is TweenValue p) t.Position = Vector3.Lerp(p0, p.Value, k);
      if (rotation is TweenValue r) t.Rotation = Vector3.Lerp(r0, r.Value, k);
      if (scale is TweenValue s) t.Scale = Vector3.Lerp(s0, s.Value, k);
    }));
  }
}

public enum Ease { Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, InSine, OutSine, InOutSine, InBack, OutBack, InOutBack, OutElastic, OutBounce }

public static class Easing {
  // Eased progress for t in 0..1 (Back and Elastic overshoot a little past 1 on the way).
  public static float Evaluate(Ease e, float t) {
    const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1;
    switch (e) {
      case Ease.InQuad: return t * t;
      case Ease.OutQuad: return 1 - (1 - t) * (1 - t);
      case Ease.InOutQuad: return t < 0.5f ? 2 * t * t : 1 - MathF.Pow(-2 * t + 2, 2) / 2;
      case Ease.InCubic: return t * t * t;
      case Ease.OutCubic: return 1 - MathF.Pow(1 - t, 3);
      case Ease.InOutCubic: return t < 0.5f ? 4 * t * t * t : 1 - MathF.Pow(-2 * t + 2, 3) / 2;
      case Ease.InSine: return 1 - MathF.Cos(t * MathF.PI / 2);
      case Ease.OutSine: return MathF.Sin(t * MathF.PI / 2);
      case Ease.InOutSine: return -(MathF.Cos(MathF.PI * t) - 1) / 2;
      case Ease.InBack: return c3 * t * t * t - c1 * t * t;
      case Ease.OutBack: return 1 + c3 * MathF.Pow(t - 1, 3) + c1 * MathF.Pow(t - 1, 2);
      case Ease.InOutBack:
        return t < 0.5f ? MathF.Pow(2 * t, 2) * ((c2 + 1) * 2 * t - c2) / 2 : (MathF.Pow(2 * t - 2, 2) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2;
      case Ease.OutElastic:
        return t <= 0 ? 0 : t >= 1 ? 1 : MathF.Pow(2, -10 * t) * MathF.Sin((t * 10 - 0.75f) * (2 * MathF.PI / 3)) + 1;
      case Ease.OutBounce: {
        const float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1 / d1) return n1 * t * t;
        if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
        if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
      }
      default: return t;
    }
  }
}

// Random numbers for gameplay. One generator for all scripts; Seed(n) makes a run repeatable (tests, replays).
// Note: with `using System;` write Bidulgi.Random or add `using Random = Bidulgi.Random;` (System.Random has the same name).
public static class Random {
  static ulong s_state = (ulong)DateTime.UtcNow.Ticks | 1;

  public static void Seed(int seed) => s_state = ((ulong)(uint)seed << 1) * 0x9E3779B97F4A7C15UL | 1;
  static ulong Next() { // xorshift64*
    s_state ^= s_state >> 12;
    s_state ^= s_state << 25;
    s_state ^= s_state >> 27;
    return s_state * 0x2545F4914F6CDD1DUL;
  }
  // 0 (inclusive) .. 1 (exclusive)
  public static float Value => (Next() >> 40) / (float)(1 << 24);
  // min..max, both included: Range(1, 6) is a die.
  public static int Range(int min, int max) {
    if (max < min) (min, max) = (max, min);
    return min + (int)(Next() % (ulong)((long)max - min + 1));
  }
  // min..max (floats)
  public static float Range(float min, float max) => min + (max - min) * Value;
  // true with probability p (0..1): Chance(0.2f) is true one time in five.
  public static bool Chance(float p) => Value < p;
  public static int Sign => (Next() & 1) == 0 ? -1 : 1;
  public static Vector2 InsideUnitCircle {
    get { while (true) { var v = new Vector2(Range(-1f, 1f), Range(-1f, 1f)); if (v.LengthSquared() <= 1) return v; } }
  }
  public static Vector3 InsideUnitSphere {
    get { while (true) { var v = new Vector3(Range(-1f, 1f), Range(-1f, 1f), Range(-1f, 1f)); if (v.LengthSquared() <= 1) return v; } }
  }
  public static Vector3 OnUnitSphere {
    get { while (true) { Vector3 v = InsideUnitSphere; float l = v.Length(); if (l > 1e-4f) return v / l; } }
  }
}

public static class RandomExtensions {
  // A random element of the list (default when it is empty).
  public static T? PickRandom<T>(this IReadOnlyList<T> list) => list.Count == 0 ? default : list[Random.Range(0, list.Count - 1)];
  public static T? PickRandom<T>(this List<T> list) => list.Count == 0 ? default : list[Random.Range(0, list.Count - 1)];
  public static T? PickRandom<T>(this T[] list) => list.Length == 0 ? default : list[Random.Range(0, list.Length - 1)];
  // Shuffles the list in place (Fisher–Yates).
  public static void Shuffle<T>(this IList<T> list) {
    for (int i = list.Count - 1; i > 0; --i) {
      int j = Random.Range(0, i);
      (list[i], list[j]) = (list[j], list[i]);
    }
  }
}

public abstract partial class Behaviour {
  internal bool Running; // enabled and not destroyed: timers, tweens and waits may run

  // ---- the easy API as members (see Game) ----
  protected T? Get<T>() where T : class => gameObject.GetComponent<T>();
  protected T? GetInChildren<T>() where T : class => Game.GetInChildren<T>(gameObject);
  protected static GameObject Find(string nameOrId) => Game.Find(nameOrId);
  protected static List<GameObject> FindAll(string name) => Game.FindAll(name);
  // Nearest other object named `name` (null = any) within `radius` of this object.
  protected GameObject FindNearest(string? name = null, float radius = float.PositiveInfinity) =>
    Game.FindNearest(name, gameObject.position, radius, gameObject);
  protected static GameObject Spawn(GameObject original, Vector3? position = null, Vector3? rotation = null, GameObject? parent = null) =>
    Game.Spawn(original, position, rotation, parent);
  protected static GameObject Spawn(string name, Vector3? position = null, Vector3? rotation = null, GameObject? parent = null) =>
    Game.Spawn(name, position, rotation, parent);
  protected static void Destroy(GameObject go, float after = 0) => Game.Destroy(go, after);
  protected Timer After(float seconds, Action action) => Game.After(seconds, action);
  protected Timer Every(float seconds, Action action, float? first = null) => Game.Every(seconds, action, first);
  protected Waiting Wait(float seconds) => Game.Wait(seconds);
  protected Waiting NextFrame() => Game.NextFrame();
  protected Waiting WaitUntil(Func<bool> condition) => Game.WaitUntil(condition);

  // [Auto] fields (before Awake, and again after a hot reload).
  internal void FillAuto() {
    for (Type? x = GetType(); x != null && x != typeof(Behaviour); x = x.BaseType)
      foreach (FieldInfo f in x.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)) {
        if (f.GetCustomAttribute<AutoAttribute>() is not { } auto) continue;
        if (!typeof(Component).IsAssignableFrom(f.FieldType) && !typeof(Behaviour).IsAssignableFrom(f.FieldType)) {
          Debug.LogWarning($"C#: {x.Name}.{f.Name}: [Auto] needs a component type, not {f.FieldType.Name}");
          continue;
        }
        MethodInfo get = typeof(Game).GetMethod(auto.InChildren ? nameof(Game.GetInChildren) : nameof(Game.Get))!.MakeGenericMethod(f.FieldType);
        object? value = get.Invoke(null, new object[] { gameObject });
        if (value == null) Debug.LogWarning($"C#: {x.Name}.{f.Name}: [Auto] found no {f.FieldType.Name} on {gameObject.Id}{(auto.InChildren ? " or its children" : "")}");
        f.SetValue(this, value);
      }
  }
}
