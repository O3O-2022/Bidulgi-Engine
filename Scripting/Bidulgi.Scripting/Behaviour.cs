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

public abstract partial class Behaviour {
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
        if (m != null && m.ReturnType == typeof(void)) return b => m.Invoke(b, null); // also `async void Start()`
        if (m != null && m.ReturnType == typeof(System.Threading.Tasks.Task)) // `async Task Start()`: its failure is logged
          return b => { if (m.Invoke(b, null) is System.Threading.Tasks.Task t) Observe(t, b, n); };
      }
      return null;
    }
    e.Awake = Find("Awake"); e.OnEnable = Find("OnEnable"); e.Start = Find("Start"); e.Update = Find("Update");
    e.FixedUpdate = Find("FixedUpdate"); e.LateUpdate = Find("LateUpdate"); e.OnDisable = Find("OnDisable"); e.OnDestroy = Find("OnDestroy");
    s_events[t] = e;
    return e;
  }

  static readonly Dictionary<string, ScriptContext> s_contexts = new(StringComparer.OrdinalIgnoreCase); // full path -> context

  // Loads a script assembly and registers each Behaviour class with the engine (a component type of that name).
  // The file is read into memory (with its .pdb when there is one), so a build can overwrite it while it is loaded.
  public static int Load(string path) {
    string full = Path.GetFullPath(path);
    var ctx = new ScriptContext(full);
    Assembly asm;
    using (var dll = new MemoryStream(File.ReadAllBytes(full))) {
      string pdb = Path.ChangeExtension(full, ".pdb");
      if (File.Exists(pdb)) { using var sym = new MemoryStream(File.ReadAllBytes(pdb)); asm = ctx.LoadFromStream(dll, sym); }
      else asm = ctx.LoadFromStream(dll);
    }
    s_contexts[full] = ctx;
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

  // Hot reload (4-4). Unity-like: state out, OnDisable, unload, load, state in, OnEnable; Awake / Start do not run again.
  public static int Reload(string path) {
    string full = Path.GetFullPath(path);
    if (!s_contexts.TryGetValue(full, out ScriptContext? old)) return Load(full); // not loaded yet: a first load
    Data.Reload(); // typed rows hold the old classes
    var saved = new List<(int h, string obj, string type, bool enabled, bool started, JsonObject state)>();
    foreach (int h in s_order.ToArray()) {
      if (!s_instances.TryGetValue(h, out var i) || AssemblyLoadContext.GetLoadContext(i.B.GetType().Assembly) != old) continue;
      saved.Add((h, i.B.gameObject.Id, i.B.GetType().Name, i.Enabled, i.Started, SaveState(i.B)));
      Stop(i); // its timers and waits hold the old code: they end like on a disable
      if (i.Enabled) Run(i, i.E.OnDisable, "OnDisable");
      s_instances.Remove(h);
    }
    foreach (var kv in s_types.Where(kv => AssemblyLoadContext.GetLoadContext(kv.Value.Assembly) == old).ToList()) {
      s_types.Remove(kv.Key);
      s_events.Remove(kv.Value);
      s_fields.Remove(kv.Value);
    }
    s_contexts.Remove(full);
    var weak = new WeakReference(old);
    old.Unload();
    old = null;
    int n = Load(full);
    int lost = 0;
    foreach (var s in saved) {
      if (!s_types.TryGetValue(s.type, out Type? t)) { s_order.Remove(s.h); ++lost; continue; }
      Behaviour b;
      try { b = (Behaviour)Activator.CreateInstance(t)!; }
      catch (Exception e) { Debug.LogError($"C#: {s.type} constructor: {(e.InnerException ?? e).Message}"); s_order.Remove(s.h); ++lost; continue; }
      b.gameObject = new GameObject(s.obj);
      LoadState(b, s.state);
      try { b.FillAuto(); } catch (Exception e) { ReportException(b, "[Auto]", e); }
      b.Running = s.enabled;
      var inst = new Instance { B = b, E = EventsOf(t), Enabled = s.enabled, Started = s.started };
      s_instances[s.h] = inst;
      if (s.enabled) Run(inst, inst.E.OnEnable, "OnEnable");
    }
    if (lost > 0) Debug.LogWarning($"C# hot reload: {lost} behaviour(s) whose class is gone were removed");
    for (int k = 0; k < 3 && weak.IsAlive; ++k) { GC.Collect(); GC.WaitForPendingFinalizers(); }
    s_lastReloadUnloaded = !weak.IsAlive;
    return n;
  }
  static bool s_lastReloadUnloaded;
  public static bool LastReloadUnloaded => s_lastReloadUnloaded; // the old assembly really left memory (test hook)

  // Every instance field of a simple type (private too), by name: what survives a reload.
  static IEnumerable<System.Reflection.FieldInfo> StateFields(Type t) {
    for (Type? x = t; x != null && x != typeof(Behaviour); x = x.BaseType)
      foreach (var f in x.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
        if (!f.IsInitOnly && !f.Name.Contains('<') && Simple(f.FieldType)) yield return f;
  }
  static bool Simple(Type t) =>
    t.IsPrimitive || t == typeof(string) || t.IsEnum || t == typeof(decimal) || t == typeof(System.Numerics.Vector2) ||
    t == typeof(System.Numerics.Vector3) || t == typeof(System.Numerics.Vector4) || t == typeof(System.Numerics.Quaternion) ||
    (t.IsArray && Simple(t.GetElementType()!)) || (t.IsGenericType && t.GetGenericTypeDefinition() == typeof(List<>) && Simple(t.GetGenericArguments()[0]));

  static JsonObject SaveState(Behaviour b) {
    var o = new JsonObject();
    foreach (var f in StateFields(b.GetType())) {
      try { o[f.Name] = Engine.ToNode(f.GetValue(b)); } catch { }
    }
    return o;
  }

  static void LoadState(Behaviour b, JsonObject state) {
    foreach (var f in StateFields(b.GetType())) {
      if (!state.TryGetPropertyValue(f.Name, out JsonNode? n) || n == null) continue;
      try { f.SetValue(b, Engine.FromNode(f.FieldType, n)); } catch { } // the field changed type: keep the new default
    }
  }

  static void Run(Instance i, Action<Behaviour>? f, string what) {
    if (f == null) return;
    ScriptSyncContext.EnsureInstalled();
    Scheduler.Call(i.B, () => {
      try { f(i.B); }
      catch (Exception e) {
        Exception inner = e is TargetInvocationException { InnerException: { } x } ? x : e;
        if (++i.Errors <= 10)
          Debug.LogError($"C#: {i.B.GetType().Name}.{what} on {i.B.gameObject.Id}: {inner.GetType().Name}: {inner.Message}{(i.Errors == 10 ? " (further errors of this behaviour are not shown)" : "")}");
      }
    });
  }

  // An exception from a timer, tween or async continuation. Cancellation (a disabled / destroyed behaviour) is normal
  // and not reported.
  internal static void ReportException(Behaviour? owner, string what, Exception e) {
    Exception inner = e is TargetInvocationException { InnerException: { } x } ? x : e;
    if (inner is OperationCanceledException) return;
    Instance? inst = null;
    if (owner != null) foreach (var kv in s_instances) if (kv.Value.B == owner) { inst = kv.Value; break; }
    if (inst != null && ++inst.Errors > 10) return;
    string who = owner != null ? $"{owner.GetType().Name} ({what}) on {owner.gameObject.Id}" : what;
    try { Debug.LogError($"C#: {who}: {inner.GetType().Name}: {inner.Message}"); } catch { }
  }

  // Objects removed by a script (Destroy): their behaviours end now, not at the next frame's sync.
  public static void ObjectsGone(HashSet<string> ids) {
    foreach (int h in s_order.ToArray())
      if (s_instances.TryGetValue(h, out var i) && ids.Contains(i.B.gameObject.Id)) Destroy(h);
  }

  // A Task-returning event method: when it fails (not a cancellation) the error is logged on the game thread.
  static void Observe(System.Threading.Tasks.Task t, Behaviour b, string what) {
    if (t.IsCompleted) { if (t.Exception is { } done) ReportException(b, what, done.InnerException ?? done); return; }
    t.ContinueWith(x => { if (x.Exception is { } e) ReportException(b, what, e.InnerException ?? e); },
                   System.Threading.CancellationToken.None, System.Threading.Tasks.TaskContinuationOptions.OnlyOnFaulted,
                   System.Threading.Tasks.TaskScheduler.FromCurrentSynchronizationContext());
  }

  static void Stop(Instance i) { // no more timers, tweens or waits for this behaviour
    i.B.Running = false;
    Scheduler.CancelOwner(i.B);
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
    b.Running = enabled; // Awake of an enabled behaviour may start timers and waits
    try { b.FillAuto(); } catch (Exception e) { ReportException(b, "[Auto]", e); }
    Run(inst, inst.E.Awake, "Awake");
    if (!s_instances.ContainsKey(h)) return h; // destroyed itself in Awake
    if (enabled) { inst.Enabled = true; Run(inst, inst.E.OnEnable, "OnEnable"); }
    return h;
  }

  public static void SetEnabled(int h, bool on) {
    if (!s_instances.TryGetValue(h, out var i) || i.Enabled == on) return;
    i.Enabled = on;
    if (on) i.B.Running = true;
    else Stop(i);
    Run(i, on ? i.E.OnEnable : i.E.OnDisable, on ? "OnEnable" : "OnDisable");
  }

  public static void Destroy(int h) {
    if (!s_instances.TryGetValue(h, out var i)) return;
    s_instances.Remove(h); // first: a Destroy from inside OnDisable / OnDestroy does not come back here
    s_order.Remove(h);
    Stop(i);
    if (i.Enabled) { i.Enabled = false; Run(i, i.E.OnDisable, "OnDisable"); }
    Run(i, i.E.OnDestroy, "OnDestroy");
    Stop(i); // anything OnDisable / OnDestroy started
  }

  public static Behaviour? Find(string objectId, Type t) {
    foreach (int h in s_order)
      if (s_instances.TryGetValue(h, out var i) && i.B.gameObject.Id == objectId && t.IsInstanceOfType(i.B)) return i.B;
    return null;
  }

  // phase 0 = Start + Update (then timers, tweens, waits), 1 = FixedUpdate, 2 = LateUpdate, 3 = end of play
  public static void Tick(int phase, double dt, double time, int frame) {
    ScriptSyncContext.EnsureInstalled();
    if (phase == 3) { Scheduler.Reset(); Data.Reload(); Input.Reset(); UI.Reset(); return; }
    Time.time = (float)time;
    Time.frameCount = frame;
    if (phase == 1) Time.fixedDeltaTime = (float)dt; else Time.deltaTime = (float)dt;
    if (phase == 1) Input.BeginFixed(); // input presses count per fixed step there (6-4)
    foreach (int h in s_order.ToArray()) { // a snapshot: behaviours may disable each other while running
      if (!s_instances.TryGetValue(h, out var i) || !i.Enabled) continue;
      if (phase == 0) {
        if (!i.Started) { i.Started = true; Run(i, i.E.Start, "Start"); }
        if (i.Enabled) Run(i, i.E.Update, "Update");
      } else if (phase == 1) {
        if (i.Started) Run(i, i.E.FixedUpdate, "FixedUpdate");
      } else if (i.Started) Run(i, i.E.LateUpdate, "LateUpdate");
    }
    if (phase == 0) Scheduler.Update();
    if (phase == 1) Input.EndFixed();
  }

  public static int Count => s_instances.Count;

  // Physics events (6-2) and other named events: void OnCollisionEnter(Collision c) or a method without parameters,
  // found by name like the lifecycle methods (may be private). Run in the fixed step, right after physics.
  static readonly Dictionary<(Type, string), MethodInfo?> s_named = new();
  public static void SendEvent(int h, string method, string json) {
    if (!s_instances.TryGetValue(h, out var i) || !i.Enabled || !i.Started) return;
    Type t = i.B.GetType();
    if (!s_named.TryGetValue((t, method), out MethodInfo? m)) {
      m = null;
      for (Type? x = t; x != null && x != typeof(Behaviour) && m == null; x = x.BaseType)
        m = x.GetMethod(method, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, new[] { typeof(Collision) })
            ?? x.GetMethod(method, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, new[] { typeof(string) })
            ?? x.GetMethod(method, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, Type.EmptyTypes);
      s_named[(t, method)] = m;
    }
    if (m == null) return;
    // OnCollision* / OnTrigger*(Collision), OnAnimationEvent(string name) (7-1), or no argument
    object?[]? args = null;
    if (m.GetParameters().Length == 1)
      args = m.GetParameters()[0].ParameterType == typeof(string)
          ? new object?[] { (System.Text.Json.Nodes.JsonNode.Parse(json)?["name"]?.GetValue<string>()) ?? "" }
          : new object?[] { Collision.FromJson(json) };
    Run(i, b => m.Invoke(b, args), method);
  }
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
  public static int ReloadAssembly(byte* pathUtf8) {
    try { return Behaviours.Reload(Str(pathUtf8)); }
    catch (Exception e) {
      try { Debug.LogError($"C#: cannot reload {Str(pathUtf8)}: {e.Message}"); } catch { }
      return -1;
    }
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

  [UnmanagedCallersOnly]
  public static void SendBehaviourEvent(int handle, byte* method, byte* json) {
    try { Behaviours.SendEvent(handle, Str(method), Str(json)); } catch { }
  }
}
