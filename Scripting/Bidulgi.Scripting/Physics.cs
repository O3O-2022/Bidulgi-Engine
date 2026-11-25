// Physics events for Behaviours (queue 6-2), Unity style: write any of these methods (they may be private) and the
// engine calls them in the fixed step, right after physics and before FixedUpdate:
//
//   void OnCollisionEnter(Collision c)   void OnCollisionExit(Collision c)
//   void OnTriggerEnter(Collision c)     void OnTriggerExit(Collision c)
//
// (a version without the parameter works too). Bodies are pushed and read through the Rigidbody / Rigidbody2D
// components: GetComponent<Rigidbody>().AddForce(new Vector3(0, 10, 0)), .Velocity, .AddImpulse, .AddTorque.
using System.Collections.Generic;
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

public sealed class Collision {
  public GameObject gameObject { get; init; } = null!; // the other object
  public Vector3 point { get; init; }                  // where they touched (world; enter events)
  public Vector3 normal { get; init; }                 // pointing away from the other object (enter events)

  internal static Collision FromJson(string json) {
    JsonNode? n = string.IsNullOrEmpty(json) ? null : JsonNode.Parse(json);
    static Vector3 Vec(JsonNode? a) => a is JsonArray v && v.Count == 3 ? new Vector3((float)v[0]!, (float)v[1]!, (float)v[2]!) : Vector3.Zero;
    return new Collision {
      gameObject = new GameObject(n?["other"]?.GetValue<string>() ?? ""),
      point = Vec(n?["point"]),
      normal = Vec(n?["normal"]),
    };
  }
}

// What a ray met (queue 6-5b): Physics.Raycast / Raycast in a Behaviour.
public struct RaycastHit {
  public GameObject gameObject; // the object with the collider
  public Vector3 point;         // where the ray met it (world)
  public Vector3 normal;        // the surface's direction there (toward the ray's start)
  public float distance;        // from the ray's start
}

// Scene queries on the running game's physics (queue 6-5b), the easy-API way (Docs/features/easy-api.md):
//
//   if (Physics.Raycast(eye, forward, out RaycastHit hit, 100)) Debug.Log($"looking at {hit.gameObject.Name}");
//   foreach (GameObject g in Physics.OverlapSphere(transform.Position, 5, "Enemy")) ...
//
// 3D (Jolt) and 2D (Box2D, the XY plane) bodies are both asked; the nearest hit wins. Triggers are skipped unless
// triggers: true. Bodies are where the last physics step left them. Outside Play: a warning, nothing hit.
public static class Physics {
  static bool s_warned;
  static JsonNode? Ask(JsonObject r) {
    try { return Engine.Request(r); }
    catch (EngineException e) {
      if (!s_warned) Debug.LogWarning("Physics: " + e.Message);
      s_warned = true;
      return null;
    }
  }
  static Vector3 Vec(JsonNode? a) => a is JsonArray v && v.Count == 3 ? new Vector3((float)v[0]!, (float)v[1]!, (float)v[2]!) : Vector3.Zero;

  public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000, bool triggers = false) {
    hit = default;
    JsonNode? n = Ask(new JsonObject { ["op"] = "physics.raycast", ["origin"] = Engine.ToNode(origin), ["direction"] = Engine.ToNode(direction),
                                       ["maxDistance"] = maxDistance, ["triggers"] = triggers });
    if (n is not JsonObject o) return false;
    hit = new RaycastHit { gameObject = new GameObject(o["id"]!.GetValue<string>()), point = Vec(o["point"]), normal = Vec(o["normal"]),
                           distance = o["distance"]!.GetValue<float>() };
    return true;
  }
  public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = 1000, bool triggers = false) =>
    Raycast(origin, direction, out _, maxDistance, triggers);

  // Objects whose colliders are inside or touch the sphere; name: only objects with that name.
  public static List<GameObject> OverlapSphere(Vector3 center, float radius, string? name = null, bool triggers = false) {
    var list = new List<GameObject>();
    if (Ask(new JsonObject { ["op"] = "physics.overlap", ["center"] = Engine.ToNode(center), ["radius"] = radius, ["triggers"] = triggers }) is JsonArray a)
      foreach (JsonNode? id in a) {
        var g = new GameObject(id!.GetValue<string>());
        if (name == null || g.Name == name) list.Add(g);
      }
    return list;
  }
  internal static void Reset() => s_warned = false;
}
