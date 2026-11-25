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
