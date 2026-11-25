// Particle effects from C# (queue 6-VFX P-1), the easy-API way (Docs/features/easy-api.md):
//
//   Vfx.Play("Spark", hit.point);                       // a .vfx under Assets, by name or Assets-relative path
//   Effect smoke = Vfx.Play("Effects/Smoke.vfx", transform.Position);  smoke.Stop();
//
// One-shot effects remove themselves when their particles are gone. A ParticleSystem component plays an effect at its
// object instead (Get<ParticleSystem>().Play()). A missing effect is a warning and an empty Effect, not an exception.
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

// A playing effect. Stop() ends emitting (the live particles finish); Stop(clear: true) removes them at once.
public sealed class Effect {
  public static readonly Effect None = new(0);
  public int Id { get; }
  internal Effect(int id) => Id = id;
  public bool Playing => Id != 0 && Engine.Request(new JsonObject { ["op"] = "vfx.playing", ["id"] = Id })!.GetValue<bool>();
  public int Particles => Id == 0 ? 0 : Engine.Request(new JsonObject { ["op"] = "vfx.alive", ["id"] = Id })!.GetValue<int>();
  public void Stop(bool clear = false) { if (Id != 0) Engine.Request(new JsonObject { ["op"] = "vfx.stop", ["id"] = Id, ["clear"] = clear }); }
}

public static class Vfx {
  // rotation: degrees (the effect's up / cone axis turned by it); seed 0 = a new one each time.
  public static Effect Play(string effect, Vector3 position, Vector3? rotation = null, int seed = 0) {
    var r = new JsonObject { ["op"] = "vfx.play", ["effect"] = effect, ["at"] = Engine.ToNode(position), ["seed"] = seed };
    if (rotation is Vector3 rot) r["rotation"] = Engine.ToNode(rot);
    try { return new Effect(Engine.Request(r)!.GetValue<int>()); }
    catch (EngineException e) { Debug.LogWarning("Vfx.Play: " + e.Message); return Effect.None; }
  }
  // Particles alive in the whole game now.
  public static int Particles => Engine.Request(new JsonObject { ["op"] = "vfx.alive" })!.GetValue<int>();
}
