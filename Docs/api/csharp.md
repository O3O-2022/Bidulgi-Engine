# C# 게임 API

게임 스크립트(`Behaviour`를 상속한 클래스)에서 쓰는 공개 API입니다. 이름은 유니티와 비슷하게 맞췄습니다.
이 파일은 `tools/gen_llms.py`가 `Scripting/Bidulgi.Scripting/*.cs`에서 만듭니다(주석 = 설명).

## Audio.cs


### `public sealed class Sound`

A playing sound. Stop() any time; Playing turns false when it ends.

- `public static readonly Sound None = new(0)`
- `public int Id`
- `public bool Playing`
- `public void Stop()`

### `public static class Audio`

- `public static Sound Play(string clip, Vector3? at = null, float volume = 1f, float pitch = 1f, string group = "Effects", bool loop = false, float minDistance = 1f, float maxDistance = 50f)`
- `public static void SetGroupVolume(string group, float volume)`
- `public static float GroupVolume(string group)`
- `public static void StopAll()`

## Behaviour.cs


### `public sealed class EditAttribute : Attribute`

Inspector attributes (4-3), Unity style. [Edit] shows a field in the Inspector and saves it in the scene; it may be private. Types: bool, int, float, double, string, Vector2/3/4, enums. [Range] makes a number a slider, [Tooltip] is the hover help, [Header] a caption above the field.


### `public sealed class RangeAttribute : Attribute`

- `public readonly double Min, Max`
- `public RangeAttribute(double min, double max)`

### `public sealed class TooltipAttribute : Attribute`

- `public readonly string Text`
- `public TooltipAttribute(string text)`

### `public sealed class HeaderAttribute : Attribute`

- `public readonly string Text`
- `public HeaderAttribute(string text)`

### `public abstract partial class Behaviour`

- `public GameObject gameObject`
- `public string name`
- `public Transform transform`
- `public bool enabled` — The component's "enabled" field (an undoable edit like any other); OnEnable / OnDisable follow on the next frame.

### `public T? GetComponent<T>() where T : class`


### `public static class Time`

- `public static float deltaTime` — game seconds of this frame (time scale applied)
- `public static float fixedDeltaTime` — inside FixedUpdate: the fixed step
- `public static float time` — game seconds since Play
- `public static int frameCount`
- `public Action<Behaviour>? Awake, OnEnable, Start, Update, FixedUpdate, LateUpdate, OnDisable, OnDestroy`
- `public required Behaviour B`
- `public required Events E`
- `public bool Enabled, Started`
- `public int Errors`
- `public static void ApplyFields(Behaviour b, string json)` — Engine values -> the instance's [Edit] fields (keys it does not know are ignored).
- `public static string ReadFields(int h)`
- `public static void SetFields(int h, string json)`
- `public ScriptContext(string path) : base("Bidulgi scripts: " + Path.GetFileName(path), isCollectible: true)`
- `public static int Load(string path)` — Loads a script assembly and registers each Behaviour class with the engine (a component type of that name). The file is read into memory (with its .pdb when there is one), so a build can overwrite it while it is loaded.
- `public static int Reload(string path)` — Hot reload (4-4). Unity-like: state out, OnDisable, unload, load, state in, OnEnable; Awake / Start do not run again.
- `public static bool LastReloadUnloaded` — the old assembly really left memory (test hook)
- `public static void ObjectsGone(HashSet<string> ids)` — Objects removed by a script (Destroy): their behaviours end now, not at the next frame's sync.
- `public static int Create(string objectId, string typeName, bool enabled, string fieldsJson = "")`
- `public static void SetEnabled(int h, bool on)`
- `public static void Destroy(int h)`
- `public static Behaviour? Find(string objectId, Type t)`
- `public static void Tick(int phase, double dt, double time, int frame)` — phase 0 = Start + Update (then timers, tweens, waits), 1 = FixedUpdate, 2 = LateUpdate, 3 = end of play
- `public static int Count`
- `public static void SendEvent(int h, string method, string json)`

### `public static unsafe partial class Host`

- `public static int LoadAssembly(byte* pathUtf8)`
- `public static int CreateBehaviour(byte* objectId, byte* typeName, int enabled, byte* fieldsJson)`
- `public static int ReloadAssembly(byte* pathUtf8)`
- `public static void SetBehaviourFields(int handle, byte* json)`
- `public static int GetBehaviourFields(int handle, byte* output, int capacity)`
- `public static void SetBehaviourEnabled(int handle, int enabled)`
- `public static void DestroyBehaviour(int handle)`
- `public static void TickBehaviours(int phase, double dt, double time, int frame)`
- `public static void SendBehaviourEvent(int handle, byte* method, byte* json)`

## Data.cs


### `public sealed class TableAttribute : Attribute`

- `public readonly string Name`
- `public TableAttribute(string name)`

### `public static class Data`

- `public required string Name, Key`
- `public required List<(string key, JsonObject row)> Rows`
- `public readonly Dictionary<Type, (Dictionary<string, object> byKey, System.Collections.IList all)> Typed = new()`
- `public static T Get<T>(string key) where T : new()` — The row `key` of T's table; a warning and a new T() (its defaults) when there is no such row or table.
- `public static bool TryGet<T>(string key, out T row) where T : new()`
- `public static IReadOnlyList<T> All<T>() where T : new()` — Every row, in file order (empty when the table is missing; a warning then).
- `public static JsonObject? Row(string table, string key)` — A row without a class: Data.Row("Weapons", "rifle")?["damage"]. Null when missing.
- `public static IReadOnlyList<string> Keys(string table)` — The row keys of a table, in file order (empty when it is missing).
- `public static bool Has(string table)`
- `public static void Reload()` — Forget what was read (end of play, hot reload): the next use reads the files again.

## Easy.cs


### `public sealed class AutoAttribute : Attribute`

- `public bool InChildren`

### `public sealed partial class GameObject`

- `public static readonly GameObject None = new("")` — An invalid handle (what Find gives when nothing matches).
- `public bool IsValid`
- `public static implicit operator bool(GameObject? g)`
- `public static bool operator ==(GameObject? a, GameObject? b)`
- `public static bool operator !=(GameObject? a, GameObject? b)`
- `public Transform transform`
- `public Vector3 position` — World position (parents applied).
- `public GameObject[] Children(bool deep = false)`

### `public static class Game`

The easy functions, usable from any code (Behaviour has the same ones as members).


### `public static T? Get<T>(GameObject go) where T : class`

---- finding ----


### `public static T? GetInChildren<T>(GameObject go) where T : class`

- `public static GameObject Find(string nameOrId)` — By name or id; a warning and GameObject.None when there is none.
- `public static List<GameObject> FindAll(string name)` — Every object with this name ("" = every object), in hierarchy order.
- `public static GameObject FindNearest(string? name, Vector3 from, float radius = float.PositiveInfinity, GameObject? except = null)` — The nearest object named `name` ("" or null = any) within `radius` of `from`; GameObject.None when none.
- `public static GameObject Spawn(GameObject original, Vector3? position = null, Vector3? rotation = null, GameObject? parent = null, string? name = null)` — ---- making and removing ---- A copy of `original` (with its children and C# behaviours, which wake up at the start of the next frame). Any object of the scene can be the template; for a prefab asset use Spawn("Prefabs/Enemy.prefab") below (6-1).
- `public static GameObject Spawn(string name, Vector3? position = null, Vector3? rotation = null, GameObject? parent = null)` — A new empty object with a Transform, or with a .prefab path ("Prefabs/Enemy.prefab", "guid:...") an instance of it.
- `public static void Destroy(GameObject go, float after = 0)` — Removes the object and its children (after: seconds of game time later). Their behaviours get OnDisable and OnDestroy right away, and their timers, tweens and waits are cancelled.
- `public static Timer After(float seconds, Action action)` — ---- time ---- Runs `action` once, `seconds` of game time from now. Cancelled when the calling behaviour is disabled or destroyed.
- `public static Timer Every(float seconds, Action action, float? first = null)` — Runs `action` every `seconds` (the first time after `first` seconds; default: one interval). Cancel() stops it.
- `public static Waiting Wait(float seconds)` — await Wait(2): resumes 2 seconds of game time later (pause and time scale apply), never in the same frame.
- `public static Waiting NextFrame()` — await NextFrame(): resumes after Update of the next frame.
- `public static Waiting WaitUntil(Func<bool> condition)` — await WaitUntil(() => door.Open): checked once per frame after Update.
- `public static Tween Tween(float from, float to, float seconds, Action<float> set, Ease ease = Ease.OutQuad, bool pingPong = false)` — Tweens any number: set(value) every frame from `from` to `to`.

### `public readonly struct TweenValue`

A Vector3 tween target that also takes one number (scale: 1.2f = uniform).

- `public readonly Vector3 Value`
- `public TweenValue(Vector3 v)`
- `public static implicit operator TweenValue(Vector3 v)`
- `public static implicit operator TweenValue(float f)`

### `public sealed class Tween`

A running tween: await it (completes when it ends; OperationCanceledException when cancelled), or Cancel() it.

- `public bool Playing`
- `public void Cancel()`
- `public Waiting GetAwaiter()`

### `public static class TweenExtensions`

- `public static Tween Tween(this Transform t, TweenValue? position = null, TweenValue? rotation = null, TweenValue? scale = null, float seconds = 0.5f, Ease ease = Ease.OutQuad, bool pingPong = false)` — transform.Tween(position: target, seconds: 0.5f, ease: Ease.OutBack): moves the Transform's local values from where they are now to the targets given (others untouched). pingPong: there and back (twice the seconds).

### `public enum Ease`


### `public static class Easing`

- `public static float Evaluate(Ease e, float t)` — Eased progress for t in 0..1 (Back and Elastic overshoot a little past 1 on the way).

### `public static class Random`

Random numbers for gameplay. One generator for all scripts; Seed(n) makes a run repeatable (tests, replays). Note: with `using System;` write Bidulgi.Random or add `using Random = Bidulgi.Random;` (System.Random has the same name).

- `public static void Seed(int seed)`
- `public static float Value` — 0 (inclusive) .. 1 (exclusive)
- `public static int Range(int min, int max)` — min..max, both included: Range(1, 6) is a die.
- `public static float Range(float min, float max)` — min..max (floats)
- `public static bool Chance(float p)` — true with probability p (0..1): Chance(0.2f) is true one time in five.
- `public static int Sign`
- `public static Vector2 InsideUnitCircle`
- `public static Vector3 InsideUnitSphere`
- `public static Vector3 OnUnitSphere`

### `public static class RandomExtensions`

- `public static T? PickRandom<T>(this IReadOnlyList<T> list)` — A random element of the list (default when it is empty).
- `public static T? PickRandom<T>(this List<T> list)`
- `public static T? PickRandom<T>(this T[] list)`
- `public static void Shuffle<T>(this IList<T> list)` — Shuffles the list in place (Fisher–Yates).

### `public abstract partial class Behaviour`


## Engine.cs


### `public sealed class EngineException : Exception`

- `public EngineException(string message) : base(message)`

### `public static unsafe class Engine`

- `public static bool Attached`
- `public static JsonNode? Request(JsonObject request)` — One request; returns the "value" of the answer (or null). Throws EngineException with the engine's message.

### `public static class Debug`

- `public static void Log(string message)`
- `public static void LogWarning(string message)`
- `public static void LogError(string message)`

### `public sealed partial class GameObject`

An object of the open scene, by its persistent id.

- `public string Id`
- `public static GameObject? Find(string idOrName)`
- `public string Name`
- `public bool HasComponent(string type)`

### `public T? GetComponent<T>() where T : class`

An engine component (generated class) or a C# Behaviour on this object; null when it has none.


### `public abstract class Component`

Base of the generated component classes (Bindings.g.cs): fields and functions by reflection name.

- `public GameObject GameObject`
- `public string TypeName`

### `public static unsafe partial class Host`

Entry points the engine calls (UnmanagedCallersOnly: plain C function pointers).

- `public static int Initialize(IntPtr request)`
- `public static int SelfTest(byte* targetUtf8)`

## Events.cs


### `public static class Events`

- `public required Delegate Fn`
- `public required Action<object?> Call`
- `public Behaviour? Owner`
- `public static void On(string name, Action handler)`
- `public static void On<T>(string name, Action<T> handler)`

### `public static void On<T>(Action<T> handler) where T : class`

- `public static void Off(string name, Delegate handler)`
- `public static void Off(string name, Action handler)`
- `public static void Off<T>(string name, Action<T> handler)`

### `public static void Off<T>(Action<T> handler) where T : class`

- `public static int Emit(string name)` — Returns how many handlers ran.
- `public static int Emit<T>(string name, T value)`

### `public static int Emit<T>(T value) where T : class`

- `public static int Send(string name)`
- `public static int Send<T>(string name, T value)`

### `public static int Send<T>(T value) where T : class`

- `public static int Count(string name)`

## Input.cs


### `public readonly struct InputActionState`

- `public string name`
- `public bool down` — held now
- `public bool pressed` — went down this frame (FixedUpdate: since the previous fixed step)
- `public bool released` — went up this frame (FixedUpdate: since the previous fixed step)
- `public float value` — button 0 / 1, axis, or the x of a vector
- `public Vector2 vector`
- `public Vector3 vector3`
- `public bool exists` — false: no such action in Actions.jsonl

### `public static class Input`

- `public static InputActionState Action(string name)`
- `public static bool Pressed(string action)`
- `public static bool Released(string action)`
- `public static bool Down(string action)`
- `public static T Value<T>(string action)`
- `public static void EnableContext(string name, bool on = true)` — Turns a mapping context (Input/Contexts/<file>.jsonl, by its "context" name) on or off.

## Physics.cs


### `public sealed class Collision`

- `public GameObject gameObject` — the other object
- `public Vector3 point` — where they touched (world; enter events)
- `public Vector3 normal` — pointing away from the other object (enter events)

### `public struct RaycastHit`

What a ray met (7-1b, 6-5b): Physics.Raycast / Raycast in a Behaviour.

- `public GameObject gameObject` — the object with the collider (or the ragdoll whose bone was hit)
- `public Vector3 point` — where the ray met it (world)
- `public Vector3 normal` — the surface's direction there (toward the ray's start)
- `public float distance` — from the ray's start

### `public enum Triggers`

Whether scene queries see triggers (colliders with isTrigger). Unity's QueryTriggerInteraction.


### `public static class Physics`

Scene queries on the running game's physics (7-1b, 6-5b), the easy-API way (Docs/features/easy-api.md):  if (Physics.Raycast(eye, forward, out RaycastHit hit, 100)) Debug.Log($"looking at {hit.gameObject.Name}"); bool grounded = Physics.Raycast(feet, -Vector3.UnitY, out _, 0.2f, gameObject);   // skip your own collider Physics.Raycast(eye, forward, out hit, 50, Triggers.Hit);                           // triggers too foreach (GameObject g in Physics.OverlapSphere(transform.Position, 5, "Enemy")) ...  3D (Jolt) and 2D (Box2D, the XY plane) bodies are both asked; the nearest hit wins. Bodies are where the last physics step left them. While the game is not running: nothing is hit.

- `public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000f, params GameObject[] ignore)` — The nearest surface along the ray, up to maxDistance metres, skipping triggers and the given objects (your own character). false while the game is not running or nothing is there.
- `public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance, Triggers triggers, params GameObject[] ignore)`
- `public static List<GameObject> OverlapSphere(Vector3 center, float radius, string? name = null, Triggers triggers = Triggers.Ignore)` — Objects whose colliders are inside or touch the sphere; name: only objects with that name.

## Save.cs


### `public static class Save`

- `public static string Slot`
- `public static T Get<T>(string key, T fallback = default!)`
- `public static bool Has(string key)`
- `public static void Set<T>(string key, T value)`
- `public static void Remove(string key)`
- `public static void Clear()`
- `public static bool Write(bool force = false)` — Writes the current slot now (only when something changed, unless force).
- `public static bool WriteAs(string slot)` — Writes the current values into another slot, which becomes the current one ("save as").
- `public static bool Load(string slot = "main")` — Switches to another slot (writing the current one first) and reads it; true when that slot had a save.
- `public static bool Exists(string slot)`
- `public static void Delete(string slot)`
- `public static List<string> Slots`
- `public static string Folder`

### `public static class SaveGame`

The same, under the name other engines use (Unreal's SaveGame, easy-api.md's table).

- `public static T Get<T>(string key, T fallback = default!)`
- `public static void Set<T>(string key, T value)`
- `public static bool Save(string slot = "main")`
- `public static bool Load(string slot = "main")`

## Scheduler.cs


### `public sealed class Waiting : INotifyCompletion`

What `await Wait(2)`, `await NextFrame()`, `await WaitUntil(...)` and `await tween` wait on. Await it once.

- `public Waiting GetAwaiter()`
- `public bool IsCompleted`
- `public bool IsCancelled`
- `public void OnCompleted(Action continuation)`
- `public void GetResult()`

### `public sealed class Timer`

A running After / Every: Cancel() stops it. Cancelled by itself when its behaviour is disabled or destroyed.

- `public bool Running`
- `public void Cancel()`
- `public Behaviour? Owner`
- `public int StartFrame`
- `public float StartTime`
- `public bool Done`
- `public abstract bool Step()` — true when finished
- `public virtual void Cancel()`
- `public required Waiting W`
- `public float Seconds`
- `public Func<bool>? Until`
- `public required Action Action`
- `public float Next, Interval` — Interval 0 = once
- `public required Action<float> Apply` — 0..1 progress, eased
- `public required Waiting W`
- `public float Seconds`
- `public Ease Ease`
- `public bool PingPong`

## Terrain.cs


### `public sealed partial class Terrain`

- `public static bool TryHeightAt(float x, float z, out float height)`
- `public static float HeightAt(float x, float z)`
- `public static float HeightAt(Vector3 position)`
- `public static Vector3 NormalAt(float x, float z)`
- `public static Vector3 NormalAt(Vector3 position)`

## UI.cs


### `public class UIWidget`

- `public bool exists`
- `public GameObject gameObject`
- `public bool visible`
- `public string text`
- `public Vector4 color`
- `public Vector4 textColor`
- `public float fontSize`
- `public Vector2 position`
- `public Vector2 size`

### `public sealed class UIText : UIWidget`


### `public sealed class UIPanel : UIWidget`


### `public sealed class UIImage : UIWidget`

- `public string image`

### `public sealed class UIBar : UIWidget`

- `public float value` — 0..1

### `public sealed class UIButton : UIWidget`

- `public int clicks` — Clicks since the game started.
- `public bool clicked` — True once for each click (reading it takes the click).
- `public bool interactable`
- `public Timer OnClick(Action action)` — Calls `action` on every click while the calling behaviour runs (cancelled with it, like After / Every).

### `public static class UI`

- `public static UIText Text(string name)`
- `public static UIButton Button(string name)`
- `public static UIImage Image(string name)`
- `public static UIPanel Panel(string name)`
- `public static UIBar Bar(string name)`

## Vfx.cs


### `public sealed class Effect`

A playing effect. Stop() ends emitting (the live particles finish); Stop(clear: true) removes them at once.

- `public static readonly Effect None = new(0)`
- `public int Id`
- `public bool Playing`
- `public int Particles`
- `public void Stop(bool clear = false)`

### `public static class Vfx`

- `public static Effect Play(string effect, Vector3 position, Vector3? rotation = null, int seed = 0)` — rotation: degrees (the effect's up / cone axis turned by it); seed 0 = a new one each time.
- `public static int Particles` — Particles alive in the whole game now.
