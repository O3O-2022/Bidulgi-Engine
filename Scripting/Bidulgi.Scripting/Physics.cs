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

// What a ray met (7-1b, 6-5b): Physics.Raycast / Raycast in a Behaviour.
public struct RaycastHit {
  public GameObject gameObject; // the object with the collider (or the ragdoll whose bone was hit)
  public Vector3 point;         // where the ray met it (world)
  public Vector3 normal;        // the surface's direction there (toward the ray's start)
  public float distance;        // from the ray's start
}

// Whether scene queries see triggers (colliders with isTrigger). Unity's QueryTriggerInteraction.
public enum Triggers { Ignore, Hit }

// Scene queries on the running game's physics (7-1b, 6-5b), the easy-API way (Docs/features/easy-api.md):
//
//   if (Physics.Raycast(eye, forward, out RaycastHit hit, 100)) Debug.Log($"looking at {hit.gameObject.Name}");
//   bool grounded = Physics.Raycast(feet, -Vector3.UnitY, out _, 0.2f, gameObject);   // skip your own collider
//   Physics.Raycast(eye, forward, out hit, 50, Triggers.Hit);                           // triggers too
//   foreach (GameObject g in Physics.OverlapSphere(transform.Position, 5, "Enemy")) ...
//
// 3D (Jolt) and 2D (Box2D, the XY plane) bodies are both asked; the nearest hit wins. Bodies are where the last physics
// step left them. While the game is not running: nothing is hit.
public static class Physics {
  static Vector3 Vec(JsonNode? a) => a is JsonArray v && v.Count == 3 ? new Vector3((float)v[0]!, (float)v[1]!, (float)v[2]!) : Vector3.Zero;

  // The nearest surface along the ray, up to maxDistance metres, skipping triggers and the given objects (your own
  // character). false while the game is not running or nothing is there.
  public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000f, params GameObject[] ignore) =>
    Raycast(origin, direction, out hit, maxDistance, Triggers.Ignore, ignore);

  public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance, Triggers triggers, params GameObject[] ignore) {
    hit = default;
    var skip = new JsonArray();
    foreach (GameObject g in ignore) skip.Add(g.Id);
    JsonNode? n = Engine.Request(new JsonObject {
      ["op"] = "raycast", ["origin"] = Engine.ToNode(origin), ["dir"] = Engine.ToNode(direction), ["max"] = maxDistance, ["ignore"] = skip,
      ["triggers"] = triggers == Triggers.Hit });
    if (n is not JsonObject o) return false;
    hit = new RaycastHit { gameObject = new GameObject(o["id"]?.GetValue<string>() ?? ""), point = Vec(o["point"]), normal = Vec(o["normal"]),
                           distance = (float)(o["distance"] ?? 0) };
    return true;
  }

  // Objects whose colliders are inside or touch the sphere; name: only objects with that name.
  public static List<GameObject> OverlapSphere(Vector3 center, float radius, string? name = null, Triggers triggers = Triggers.Ignore) {
    var list = new List<GameObject>();
    JsonNode? n;
    try { n = Engine.Request(new JsonObject { ["op"] = "overlap", ["center"] = Engine.ToNode(center), ["radius"] = radius, ["triggers"] = triggers == Triggers.Hit }); }
    catch (EngineException) { return list; } // not playing: nothing there
    if (n is JsonArray a)
      foreach (JsonNode? id in a) {
        var g = new GameObject(id!.GetValue<string>());
        if (name == null || g.Name == name) list.Add(g);
      }
    return list;
  }
}
