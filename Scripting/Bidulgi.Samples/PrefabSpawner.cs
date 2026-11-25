// Prefab sample (queue 6-1): places instances of a .prefab with Spawn, the easy API's one-liner. The BDG_PREFAB_TEST
// check plays it and looks for the spawned instance (and its children) in the scene.
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class PrefabSpawner : Behaviour {
  [Edit, Tooltip("Project path of the prefab (Prefabs/Enemy.prefab) or guid:...")] public string prefab = "";
  [Edit, Tooltip("Instances placed in Start, one metre apart along X")] public int count = 1;
  [Edit, Tooltip("Id of the last instance placed (set by the script)")] public string spawned = "";

  void Start() {
    for (int i = 0; i < count; ++i) {
      GameObject o = Game.Spawn(prefab, new Vector3(10 + i, 0, -4));
      spawned = o.Id;
    }
  }
}
