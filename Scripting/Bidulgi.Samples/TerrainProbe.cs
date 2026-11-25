// Reads the terrain under it from C# (round 6, BDG_TERRAIN_TEST): once it has fallen onto the terrain (a Character
// rigidbody with a 2 m capsule), Terrain.HeightAt / NormalAt there must match where it stands.
using System;
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class TerrainProbe : Behaviour {
  void Start() => After(3.0f, Report); // fallen and settled by then

  void Report() {
    Vector3 p = transform.Position;
    float h = Terrain.HeightAt(p.X, p.Z);
    Vector3 n = Terrain.NormalAt(p);
    bool stands = MathF.Abs(p.Y - (h + 1f)) < 0.2f; // the capsule's centre is 1 m above its feet
    bool missing = float.IsNaN(Terrain.HeightAt(99999, 99999)) && !Terrain.TryHeightAt(99999, 99999, out _);
    Debug.Log($"terrain probe: stand={(stands ? 1 : 0)} normal={(n.Y > 0.5f ? 1 : 0)} missing={(missing ? 1 : 0)} (height {h:0.00}, y {p.Y:0.00})");
  }
}
