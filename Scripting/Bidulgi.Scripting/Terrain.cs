// Terrain queries from C# (round 6), the easy-API way: the ground height and slope anywhere on a terrain.
//
//   float y = Terrain.HeightAt(x, z);                 // NaN where there is no terrain
//   Vector3 up = Terrain.NormalAt(transform.Position);
//   if (Terrain.TryHeightAt(p.X, p.Z, out float h)) spawnPoint.Y = h;
//
// The Terrain component itself (asset, collider, brush settings) is the generated class of the same name.
using System.Numerics;
using System.Text.Json.Nodes;

namespace Bidulgi;

public sealed partial class Terrain {
  static JsonObject? Ask(float x, float z) => Engine.Request(new JsonObject { ["op"] = "terrain.height", ["x"] = x, ["z"] = z }) as JsonObject;

  public static bool TryHeightAt(float x, float z, out float height) {
    JsonObject? o = Ask(x, z);
    height = o == null ? float.NaN : o["height"]!.GetValue<float>();
    return o != null;
  }
  public static float HeightAt(float x, float z) => TryHeightAt(x, z, out float h) ? h : float.NaN;
  public static float HeightAt(Vector3 position) => HeightAt(position.X, position.Z);
  public static Vector3 NormalAt(float x, float z) {
    JsonObject? o = Ask(x, z);
    return o?["normal"] is JsonArray n ? new Vector3((float)n[0]!, (float)n[1]!, (float)n[2]!) : Vector3.UnitY;
  }
  public static Vector3 NormalAt(Vector3 position) => NormalAt(position.X, position.Z);
}
