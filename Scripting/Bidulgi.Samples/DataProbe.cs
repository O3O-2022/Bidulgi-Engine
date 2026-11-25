// Reads data tables from C# (queue 4-4d) and reports one line (BDG_DATATABLE_TEST). The check writes
// Weapons.table.jsonl (with a header) and Enemies.csv into its data folder before Play.
using System.Collections.Generic;
using Bidulgi;

namespace Bidulgi.Samples;

public class Weapon { // table "Weapons" (the class name + s)
  public string id = "";
  public int damage;
  public float range = 1;
  public string name = "";
  public bool automatic;
}

[Table("Enemies")]
public class EnemyRow {
  public string id = "";
  public int hp = 10;
  public float speed;
  public string title = "";
}

public class DataProbe : Behaviour {
  void Start() {
    Weapon rifle = Data.Get<Weapon>("rifle");
    bool get = rifle.damage == 12 && rifle.range == 50.5f && rifle.name == "Rifle, long" && rifle.automatic;
    bool defaults = Data.Get<Weapon>("pistol").range == 1; // not in the row: the class default
    IReadOnlyList<Weapon> all = Data.All<Weapon>();
    bool allRows = all.Count == 2 && all[0].id == "rifle" && all[1].id == "pistol";
    EnemyRow goblin = Data.Get<EnemyRow>("goblin"), ghost = Data.Get<EnemyRow>("ghost");
    bool csv = goblin.hp == 30 && goblin.speed == 1.5f && goblin.title == "Goblin \"Boss\"" && ghost.hp == 10; // ghost: empty hp cell
    bool missing = Data.Get<Weapon>("laser").damage == 0 && !Data.TryGet<Weapon>("laser", out _) && !Data.Has("Nothing");
    bool untyped = Data.Row("Weapons", "pistol")?["damage"]?.GetValue<int>() == 5 && Data.Keys("Enemies").Count == 2;
    int B(bool v) => v ? 1 : 0;
    Debug.Log($"data probe: get={B(get)} defaults={B(defaults)} all={B(allRows)} csv={B(csv)} missing={B(missing)} untyped={B(untyped)}");
  }
}
