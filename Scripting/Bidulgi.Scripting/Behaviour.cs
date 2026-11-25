// C# Behaviours and their lifecycle (queue 4-2), Unity style: a class derived from Behaviour is attached to an
// object by its class name ("Spinner":{} in the scene file) and the engine calls its event methods. Like Unity,
// the methods are found by name and may be private: void Awake(), OnEnable(), Start(), Update(), FixedUpdate(),
// LateUpdate(), OnDisable(), OnDestroy(). Frame order (Docs/architecture/interop.md §5): Start (first frame of
// an enabled behaviour) -> Update -> FixedUpdate (0..n fixed steps, with physics) -> LateUpdate.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json.Nodes;

namespace Bidulgi;

// Inspector attributes (4-3), Unity style. [Edit] shows a field in the Inspector and saves it in the scene; it may be
// private. Types: bool, int, float, double, string, Vector2/3/4, enums. [Range] makes a number a slider, [Tooltip]
// is the hover help, [Header] a caption above the field.
[AttributeUsage(AttributeTargets.Field)] public sealed class EditAttribute : Attribute {}
[AttributeUsage(AttributeTargets.Field)] public sealed class RangeAttribute : Attribute {
  public readonly double Min, Max;
  public RangeAttribute(double min, double max) { Min = min; Max = max; }
}
[AttributeUsage(AttributeTargets.Field)] public sealed class TooltipAttribute : Attribute {
  public readonly string Text;
  public TooltipAttribute(string text) => Text = text;
}
[AttributeUsage(AttributeTargets.Field)] public sealed class HeaderAttribute : Attribute {
  public readonly string Text;
  public HeaderAttribute(string text) => Text = text;
}

public abstract class Behaviour {
  public GameObject gameObject { get; internal set; } = null!;
  public string name { get => gameObject.Name; set => gameObject.Name = value; }
  public Transform transform => gameObject.GetComponent<Transform>() ?? throw new EngineException(gameObject.Name + " has no Transform");
  // The component's "enabled" field (an undoable edit like any other); OnEnable / OnDisable follow on the next frame.
  public bool enabled {
    get => Engine.Request(new JsonObject { ["op"] = "get", ["target"] = gameObject.Id, ["path"] = GetType().Name + ".enabled" })?.GetValue<bool>() ?? false;
    set => Engine.Request(new JsonObject { ["op"] = "set", ["target"] = gameObject.Id, ["path"] = GetType().Name + ".enabled", ["value"] = value });
  }
  public T? GetComponent<T>() where T : class => gameObject.GetComponent<T>();
}

public static class Time {
  public static float deltaTime { get; internal set; }      // game seconds of this frame (time scale applied)
  public static float fixedDeltaTime { get; internal set; } // inside FixedUpdate: the fixed step
  public static float time { get; internal set; }           // game seconds since Play
  public static int frameCount { get; internal set; }
}

// Instances and event dispatch; driven by the engine (BehaviourRunner) through Host's entry points.
internal static class Behaviours {
  sealed class Events {
    public Action<Behaviour>? Awake, OnEnable, Start, Update, FixedUpdate, LateUpdate, OnDisable, OnDestroy;
  }
  sealed class Instance {
    public required Behaviour B;
    public required Events E;
    public bool Enabled, Started;
    public int Errors;
  }
  static readonly Dictionary<string, Type> s_types = new();
  static readonly Dictionary<Type, Events> s_events = new();
  static readonly Dictionary<int, Instance> s_instances = new();
  static readonly Dictionary<Type, FieldInfo[]> s_fields = new();

  // The [Edit] fields of a class (base classes first), in declaration order.
  static FieldInfo[] EditFields(Type t) {
    if (s_fields.TryGetValue(t, out var f)) return f;
    var chain = new List<Type>();
    for (Type? x = t; x != null && x != typeof(Behaviour); x = x.BaseType) chain.Insert(0, x);
    f = chain.SelectMany(x => x.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
                                .Where(fi => fi.GetCustomAttribute<EditAttribute>() != null).OrderBy(fi => fi.MetadataToken)).ToArray();
    s_fields[t] = f;
    return f;
  }

  static string? KindOf(Type t) =>
    t == typeof(bool) ? "bool" : t == typeof(int) ? "int" : t == typeof(float) ? "float" : t == typeof(double) ? "double" :
    t == typeof(string) ? "string" : t == typeof(System.Numerics.Vector2) ? "vec2" : t == typeof(System.Numerics.Vector3) ? "vec3" :
    t == typeof(System.Numerics.Vector4) ? "vec4" : t.IsEnum ? "enum" : null;

  // What the engine needs to show and save a class's fields: name, type, default (from a fresh instance), range, help.
  static JsonArray Describe(Type t) {
    var list = new JsonArray();
    object? fresh = null;
    try { fresh = Activator.CreateInstance(t); } catch { }
    foreach (FieldInfo f in EditFields(t)) {
      string? kind = KindOf(f.FieldType);
      if (kind == null) { Debug.LogWarning($"C#: {t.Name}.{f.Name}: [Edit] on a {f.FieldType.Name} is not supported yet"); continue; }
      var o = new JsonObject { ["name"] = f.Name, ["type"] = kind };
      if (fresh != null) o["default"] = Engine.ToNode(f.GetValue(fresh)) ?? (kind == "string" ? "" : null);
      if (f.GetCustomAttribute<RangeAttribute>() is { } r) { o["min"] = r.Min; o["max"] = r.Max; }
      if (f.GetCustomAttribute<TooltipAttribute>() is { } tip) o["tooltip"] = tip.Text;
      if (f.GetCustomAttribute<HeaderAttribute>() is { } head) o["header"] = head.Text;
      if (f.FieldType.IsEnum) {
        var values = new JsonArray();
        foreach (object v in Enum.GetValues(f.FieldType)) values.Add(new JsonArray(v.ToString(), Convert.ToInt64(v)));
        o["values"] = values;
      }
      list.Add(o);
    }
    return list;
  }

  // Engine values -> the instance's [Edit] fields (keys it does not know are ignored).
  public static void ApplyFields(Behaviour b, string json) {
    if (string.IsNullOrEmpty(json)) return;
    if (JsonNode.Parse(json) is not JsonObject obj) return;
    foreach (FieldInfo f in EditFields(b.GetType())) {
      if (!obj.TryGetPropertyValue(f.Name, out JsonNode? n) || n == null) continue;
      try { f.SetValue(b, Engine.FromNode(f.FieldType, n)); }
      catch (Exception e) { Debug.LogWarning($"C#: {b.GetType().Name}.{f.Name}: {e.Message}"); }
    }
  }

  public static string ReadFields(int h) {
    if (!s_instances.TryGetValue(h, out var i)) return "";
    var o = new JsonObject();
    foreach (FieldInfo f in EditFields(i.B.GetType())) o[f.Name] = Engine.ToNode(f.GetValue(i.B));
    return o.ToJsonString();
  }

  public static void SetFields(int h, string json) {
    if (s_instances.TryGetValue(h, out var i)) ApplyFields(i.B, json);
  }
  static readonly List<int> s_order = new(); // creation order = call order
  static int s_next = 1;

  sealed class ScriptContext : AssemblyLoadContext {
    readonly AssemblyDependencyResolver _resolver;
    public ScriptContext(string path) : base("Bidulgi scripts: " + Path.GetFileName(path), isCollectible: true) => _resolver = new AssemblyDependencyResolver(path);
    protected override Assembly? Load(AssemblyName name) {
      if (name.Name == typeof(Behaviour).Assembly.GetName().Name) return typeof(Behaviour).Assembly; // share the engine API
      string? p = _resolver.ResolveAssemblyToPath(name);
      return p != null ? LoadFromAssemblyPath(p) : null;
    }
  }

  static Events EventsOf(Type t) {
    if (s_events.TryGetValue(t, out var e)) return e;
    e = new Events();
    Action<Behaviour>? Find(string n) {
      for (Type? x = t; x != null && x != typeof(Behaviour); x = x.BaseType) {
        MethodInfo? m = x.GetMethod(n, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, Type.EmptyTypes);
        if (m != null && m.ReturnType == typeof(void)) return b => m.Invoke(b, null);
      }
      return null;
    }
    e.Awake = Find("Awake"); e.OnEnable = Find("OnEnable"); e.Start = Find("Start"); e.Update = Find("Update");
    e.FixedUpdate = Find("FixedUpdate"); e.LateUpdate = Find("LateUpdate"); e.OnDisable = Find("OnDisable"); e.OnDestroy = Find("OnDestroy");
    s_events[t] = e;
    return e;
  }

  // Loads a script assembly and registers each Behaviour class with the engine (a component type of that name).
  public static int Load(string path) {
    var ctx = new ScriptContext(Path.GetFullPath(path));
    Assembly asm = ctx.LoadFromAssemblyPath(Path.GetFullPath(path));
    int n = 0;
    foreach (Type t in asm.GetTypes().Where(t => t.IsClass && !t.IsAbstract && t.IsSubclassOf(typeof(Behaviour))).OrderBy(t => t.Name, StringComparer.Ordinal)) {
      if (t.GetConstructor(Type.EmptyTypes) == null) { Debug.LogWarning($"C#: {t.FullName} has no parameterless constructor (skipped)"); continue; }
      if (s_types.TryGetValue(t.Name, out Type? old) && old.Assembly.GetName().Name != asm.GetName().Name) Debug.LogWarning($"C#: two Behaviours named {t.Name} ({old.FullName}, {t.FullName}); the last one wins");
      s_types[t.Name] = t;
      Engine.Request(new JsonObject { ["op"] = "register", ["name"] = t.Name, ["assembly"] = asm.GetName().Name, ["fields"] = Describe(t) });
      ++n;
    }
    return n;
  }

  static void Run(Instance i, Action<Behaviour>? f, string what) {
    if (f == null) return;
    try { f(i.B); }
    catch (Exception e) {
      Exception inner = e is TargetInvocationException { InnerException: { } x } ? x : e;
      if (++i.Errors <= 10)
        Debug.LogError($"C#: {i.B.GetType().Name}.{what} on {i.B.gameObject.Id}: {inner.GetType().Name}: {inner.Message}{(i.Errors == 10 ? " (further errors of this behaviour are not shown)" : "")}");
    }
  }

  public static int Create(string objectId, string typeName, bool enabled, string fieldsJson = "") {
    if (!s_types.TryGetValue(typeName, out Type? t)) { Debug.LogError($"C#: no Behaviour class {typeName}"); return 0; }
    Behaviour b;
    try { b = (Behaviour)Activator.CreateInstance(t)!; }
    catch (Exception e) { Debug.LogError($"C#: {typeName} constructor: {(e.InnerException ?? e).Message}"); return 0; }
    b.gameObject = new GameObject(objectId);
    ApplyFields(b, fieldsJson); // the saved values, before Awake
    var inst = new Instance { B = b, E = EventsOf(t) };
    int h = s_next++;
    s_instances[h] = inst;
    s_order.Add(h);
    Run(inst, inst.E.Awake, "Awake");
    if (enabled) { inst.Enabled = true; Run(inst, inst.E.OnEnable, "OnEnable"); }
    return h;
  }

  public static void SetEnabled(int h, bool on) {
    if (!s_instances.TryGetValue(h, out var i) || i.Enabled == on) return;
    i.Enabled = on;
    Run(i, on ? i.E.OnEnable : i.E.OnDisable, on ? "OnEnable" : "OnDisable");
  }

  public static void Destroy(int h) {
    if (!s_instances.TryGetValue(h, out var i)) return;
    if (i.Enabled) { i.Enabled = false; Run(i, i.E.OnDisable, "OnDisable"); }
    Run(i, i.E.OnDestroy, "OnDestroy");
    s_instances.Remove(h);
    s_order.Remove(h);
  }

  public static Behaviour? Find(string objectId, Type t) {
    foreach (int h in s_order)
      if (s_instances.TryGetValue(h, out var i) && i.B.gameObject.Id == objectId && t.IsInstanceOfType(i.B)) return i.B;
    return null;
  }

  // phase 0 = Start + Update, 1 = FixedUpdate, 2 = LateUpdate
  public static void Tick(int phase, double dt, double time, int frame) {
    Time.time = (float)time;
    Time.frameCount = frame;
    if (phase == 1) Time.fixedDeltaTime = (float)dt; else Time.deltaTime = (float)dt;
    foreach (int h in s_order.ToArray()) { // a snapshot: behaviours may disable each other while running
      if (!s_instances.TryGetValue(h, out var i) || !i.Enabled) continue;
      if (phase == 0) {
        if (!i.Started) { i.Started = true; Run(i, i.E.Start, "Start"); }
        if (i.Enabled) Run(i, i.E.Update, "Update");
      } else if (phase == 1) {
        if (i.Started) Run(i, i.E.FixedUpdate, "FixedUpdate");
      } else if (i.Started) Run(i, i.E.LateUpdate, "LateUpdate");
    }
  }

  public static int Count => s_instances.Count;
}

public static unsafe partial class Host {
  static string Str(byte* p) => Marshal.PtrToStringUTF8((IntPtr)p) ?? "";

  // Returns the number of Behaviour classes registered, or -1 (error logged).
  [UnmanagedCallersOnly]
  public static int LoadAssembly(byte* pathUtf8) {
    try { return Behaviours.Load(Str(pathUtf8)); }
    catch (Exception e) {
      try { Debug.LogError($"C#: cannot load {Str(pathUtf8)}: {e.Message}"); } catch { }
      return -1;
    }
  }

  [UnmanagedCallersOnly]
  public static int CreateBehaviour(byte* objectId, byte* typeName, int enabled, byte* fieldsJson) {
    try { return Behaviours.Create(Str(objectId), Str(typeName), enabled != 0, Str(fieldsJson)); } catch { return 0; }
  }

  [UnmanagedCallersOnly]
  public static void SetBehaviourFields(int handle, byte* json) {
    try { Behaviours.SetFields(handle, Str(json)); } catch { }
  }

  // Writes the [Edit] field values as JSON into out; returns the length (more than capacity: call again, larger).
  [UnmanagedCallersOnly]
  public static int GetBehaviourFields(int handle, byte* output, int capacity) {
    try {
      byte[] bytes = System.Text.Encoding.UTF8.GetBytes(Behaviours.ReadFields(handle));
      if (bytes.Length <= capacity) Marshal.Copy(bytes, 0, (IntPtr)output, bytes.Length);
      return bytes.Length;
    } catch { return 0; }
  }

  [UnmanagedCallersOnly]
  public static void SetBehaviourEnabled(int handle, int enabled) {
    try { Behaviours.SetEnabled(handle, enabled != 0); } catch { }
  }

  [UnmanagedCallersOnly]
  public static void DestroyBehaviour(int handle) {
    try { Behaviours.Destroy(handle); } catch { }
  }

  [UnmanagedCallersOnly]
  public static void TickBehaviours(int phase, double dt, double time, int frame) {
    try { Behaviours.Tick(phase, dt, time, frame); } catch { }
  }
}
