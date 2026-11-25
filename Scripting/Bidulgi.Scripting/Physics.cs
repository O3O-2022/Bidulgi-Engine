// Physics events for Behaviours (queue 6-2), Unity style: write any of these methods (they may be private) and the
// engine calls them in the fixed step, right after physics and before FixedUpdate:
//
//   void OnCollisionEnter(Collision c)   void OnCollisionExit(Collision c)
//   void OnTriggerEnter(Collision c)     void OnTriggerExit(Collision c)
//
// (a version without the parameter works too). Bodies are pushed and read through the Rigidbody / Rigidbody2D
// components: GetComponent<Rigidbody>().AddForce(new Vector3(0, 10, 0)), .Velocity, .AddImpulse, .AddTorque.
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

// What a ray hit (7-1b): Physics.Raycast(origin, direction, out RaycastHit hit, maxDistance, ignore...).
public struct RaycastHit {
  public GameObject gameObject; // the object hit (its collider, or a ragdoll's bone)
  public Vector3 point, normal;
  public float distance;
}

public static class Physics {
  // The nearest solid surface along the ray (triggers are skipped), up to maxDistance metres, ignoring the given
  // objects (your own character). false while the game is not running or nothing is there.
  public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000f, params GameObject[] ignore) {
    hit = default;
    var skip = new JsonArray();
    foreach (GameObject g in ignore) skip.Add(g.Id);
    JsonNode? n = Engine.Request(new JsonObject {
      ["op"] = "raycast", ["origin"] = new JsonArray(origin.X, origin.Y, origin.Z), ["dir"] = new JsonArray(direction.X, direction.Y, direction.Z),
      ["max"] = maxDistance, ["ignore"] = skip });
    if (n is not JsonObject o) return false;
    static Vector3 Vec(JsonNode? a) => a is JsonArray v && v.Count == 3 ? new Vector3((float)v[0]!, (float)v[1]!, (float)v[2]!) : Vector3.Zero;
    hit = new RaycastHit { gameObject = new GameObject(o["id"]?.GetValue<string>() ?? ""), point = Vec(o["point"]), normal = Vec(o["normal"]),
                           distance = (float)(o["distance"] ?? 0) };
    return true;
  }
}

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
