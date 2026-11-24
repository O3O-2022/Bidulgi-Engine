// Engine side of C# scripting (queue 4-1). The engine hosts .NET with hostfxr and calls Host.Initialize with one
// native function: Request(json in, buffer out) -> length. Every engine call from C# is a small JSON request
// ({"op":"get","target":"g6","path":"Health.current"}), answered with {"ok":true,"value":...} or
// {"ok":false,"error":"..."}. Edits (set, call) go through the engine's command bus, so they undo and log like
// edits made in the editor. Objects are addressed by their persistent scene id (handles come with 4-2).
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Bidulgi;

public sealed class EngineException : Exception {
  public EngineException(string message) : base(message) {}
}

public static unsafe class Engine {
  static delegate* unmanaged<byte*, byte*, int, int> s_request;

  internal static void Attach(IntPtr request) => s_request = (delegate* unmanaged<byte*, byte*, int, int>)request;
  public static bool Attached => s_request != null;

  // One request; returns the "value" of the answer (or null). Throws EngineException with the engine's message.
  public static JsonNode? Request(JsonObject request) {
    if (s_request == null) throw new EngineException("the engine is not attached");
    byte[] input = Encoding.UTF8.GetBytes(request.ToJsonString() + "\0");
    int cap = 4096;
    for (int attempt = 0; attempt < 4; ++attempt) {
      byte[] output = new byte[cap];
      int len;
      fixed (byte* inp = input)
      fixed (byte* outp = output) len = s_request(inp, outp, cap);
      if (len < 0) throw new EngineException("engine request failed");
      if (len > cap) { cap = len; continue; } // the answer did not fit: ask again with room for it
      JsonNode? answer = JsonNode.Parse(Encoding.UTF8.GetString(output, 0, len));
      if (answer is not JsonObject obj) throw new EngineException("bad answer from the engine");
      if (obj["ok"]?.GetValue<bool>() != true) throw new EngineException(obj["error"]?.GetValue<string>() ?? "engine error");
      return obj["value"]?.DeepClone();
    }
    throw new EngineException("engine answer too large");
  }

  internal static readonly JsonSerializerOptions Json = new() { IncludeFields = true };
  internal static JsonNode? ToNode(object? v) => v switch {
    null => null,
    System.Numerics.Vector2 a => new JsonArray(a.X, a.Y),
    System.Numerics.Vector3 a => new JsonArray(a.X, a.Y, a.Z),
    System.Numerics.Vector4 a => new JsonArray(a.X, a.Y, a.Z, a.W),
    System.Numerics.Quaternion q => new JsonArray(q.X, q.Y, q.Z, q.W),
    Enum e => JsonValue.Create(e.ToString()),
    _ => JsonSerializer.SerializeToNode(v, v.GetType(), Json),
  };
  internal static T FromNode<T>(JsonNode? n) {
    object? r;
    Type t = typeof(T);
    if (n == null) return default!;
    if (t == typeof(System.Numerics.Vector3)) { var a = n.AsArray(); r = new System.Numerics.Vector3((float)a[0]!, (float)a[1]!, (float)a[2]!); }
    else if (t == typeof(System.Numerics.Vector2)) { var a = n.AsArray(); r = new System.Numerics.Vector2((float)a[0]!, (float)a[1]!); }
    else if (t == typeof(System.Numerics.Vector4)) { var a = n.AsArray(); r = new System.Numerics.Vector4((float)a[0]!, (float)a[1]!, (float)a[2]!, (float)a[3]!); }
    else if (t == typeof(System.Numerics.Quaternion)) { var a = n.AsArray(); r = new System.Numerics.Quaternion((float)a[0]!, (float)a[1]!, (float)a[2]!, (float)a[3]!); }
    else if (t.IsEnum) r = Enum.Parse(t, n.GetValue<string>());
    else r = n.Deserialize<T>(Json);
    return (T)r!;
  }
}

public static class Debug {
  public static void Log(string message) => Engine.Request(new JsonObject { ["op"] = "log", ["level"] = "info", ["text"] = message });
  public static void LogWarning(string message) => Engine.Request(new JsonObject { ["op"] = "log", ["level"] = "warn", ["text"] = message });
  public static void LogError(string message) => Engine.Request(new JsonObject { ["op"] = "log", ["level"] = "error", ["text"] = message });
}

// An object of the open scene, by its persistent id.
public sealed class GameObject {
  public string Id { get; }
  internal GameObject(string id) => Id = id;

  public static GameObject? Find(string idOrName) {
    JsonNode? v = Engine.Request(new JsonObject { ["op"] = "find", ["name"] = idOrName });
    return v == null ? null : new GameObject(v.GetValue<string>());
  }
  public string Name {
    get => Engine.Request(new JsonObject { ["op"] = "name", ["target"] = Id })!.GetValue<string>();
    set => Engine.Request(new JsonObject { ["op"] = "rename", ["target"] = Id, ["name"] = value });
  }
  public bool HasComponent(string type) => Engine.Request(new JsonObject { ["op"] = "has", ["target"] = Id, ["type"] = type })!.GetValue<bool>();
  public T? GetComponent<T>() where T : Component {
    T c = (T)Activator.CreateInstance(typeof(T), this)!;
    return HasComponent(c.TypeName) ? c : null;
  }
  public override string ToString() => Id;
}

// Base of the generated component classes (Bindings.g.cs): fields and functions by reflection name.
public abstract class Component {
  public GameObject GameObject { get; }
  public string TypeName { get; }
  protected Component(GameObject owner, string typeName) { GameObject = owner; TypeName = typeName; }

  protected T Get<T>(string field) =>
      Engine.FromNode<T>(Engine.Request(new JsonObject { ["op"] = "get", ["target"] = GameObject.Id, ["path"] = TypeName + "." + field }));
  protected void Set<T>(string field, T value) =>
      Engine.Request(new JsonObject { ["op"] = "set", ["target"] = GameObject.Id, ["path"] = TypeName + "." + field, ["value"] = Engine.ToNode(value) });
  protected JsonNode? Invoke(string function, params object?[] args) {
    var list = new JsonArray();
    foreach (object? a in args) list.Add(Engine.ToNode(a));
    return Engine.Request(new JsonObject { ["op"] = "call", ["target"] = GameObject.Id, ["type"] = TypeName, ["function"] = function, ["args"] = list });
  }
  protected T Call<T>(string function, params object?[] args) => Engine.FromNode<T>(Invoke(function, args));
  protected void CallVoid(string function, params object?[] args) => Invoke(function, args);
}

// Entry points the engine calls (UnmanagedCallersOnly: plain C function pointers).
public static unsafe class Host {
  [UnmanagedCallersOnly]
  public static int Initialize(IntPtr request) {
    Engine.Attach(request);
    return 1; // scripting API version
  }

  // Test hook (4-1 exit criterion): reads, calls and writes a Health component of `target` from C#.
  // Returns 1 when every step gave the expected value, 0 otherwise; -1 on an exception (logged).
  [UnmanagedCallersOnly]
  public static int SelfTest(byte* targetUtf8) {
    try {
      string target = Marshal.PtrToStringUTF8((IntPtr)targetUtf8) ?? "";
      Debug.Log("C#: hello from .NET " + Environment.Version);
      GameObject? o = GameObject.Find(target);
      if (o == null) { Debug.LogError("C#: no object " + target); return 0; }
      Health? h = o.GetComponent<Health>();
      if (h == null) { Debug.LogError("C#: no Health on " + target); return 0; }
      int before = h.Current, max = h.MaxHealth;
      int after = h.Damage(10);
      float ratio = h.Ratio();
      h.MaxHealth = 250;
      var t = o.GetComponent<Transform>()!;
      t.Position = new System.Numerics.Vector3(1, 2, 3);
      bool ok = after == Math.Max(before - 10, 0) && h.Current == after && Math.Abs(ratio - after / (float)max) < 1e-4f && h.MaxHealth == 250 &&
                t.Position == new System.Numerics.Vector3(1, 2, 3);
      Debug.Log($"C#: {o.Name} health {before} -> {after}, ratio {ratio}, max {h.MaxHealth}, position {t.Position}");
      try { // a wrong call is an error, not a crash
        Engine.Request(new JsonObject { ["op"] = "call", ["target"] = o.Id, ["type"] = "Health", ["function"] = "Damage", ["args"] = new JsonArray() });
        ok = false;
      } catch (EngineException) { }
      return ok ? 1 : 0;
    } catch (Exception e) {
      try { Debug.LogError("C#: " + e); } catch { }
      return -1;
    }
  }
}
