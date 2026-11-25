// Saves from C# (queue 6-5b), the easy-API way (Docs/features/easy-api.md): values by key in the current slot,
// written to the game's save folder (Project Settings saves.saveFolder, "%APPDATA%/{company}/{name}/Saves").
//
//   int best = Save.Get("best", 0);          // what the last run saved (0 the first time)
//   Save.Set("best", 12);                    // written within a second, and when the game stops
//   Save.Write();                            // ... or right now
//   Save.Load("slot2");                      // another slot (files: main.save.json, slot2.save.json)
//   Save.Delete("slot2");  Save.Slots;  Save.Exists("main");
//
// Values are anything JSON can hold: numbers, strings, bools, Vector3, lists, your own classes with public fields.
// The file is versioned JSON written safely (the previous save kept as .bak). The same folder in the editor and in the
// built game. `SaveGame` is the same class under the name from other engines.
using System;
using System.Collections.Generic;
using System.Text.Json.Nodes;

namespace Bidulgi;

public static class Save {
  static JsonObject? s_data;   // the current slot, read on first use
  static string s_slot = "main";
  static bool s_dirty;
  static float s_lastWrite = -1e9f;
  static bool s_warned;

  public static string Slot => s_slot;

  static JsonObject Data {
    get {
      if (s_data != null) return s_data;
      s_data = new JsonObject();
      try {
        if (Engine.Request(new JsonObject { ["op"] = "save.read", ["slot"] = s_slot }) is JsonObject read) s_data = read;
      } catch (EngineException e) { Debug.LogWarning("Save: " + e.Message + " (starting with an empty save)"); }
      return s_data;
    }
  }

  public static T Get<T>(string key, T fallback = default!) {
    if (!Data.TryGetPropertyValue(key, out JsonNode? n) || n == null) return fallback;
    try { return Engine.FromNode<T>(n); }
    catch (Exception) {
      Debug.LogWarning($"Save.Get(\"{key}\"): the saved value is not a {typeof(T).Name}; using the default");
      return fallback;
    }
  }
  public static bool Has(string key) => Data.ContainsKey(key);
  public static void Set<T>(string key, T value) {
    JsonNode? n = Engine.ToNode(value);
    if (Data.TryGetPropertyValue(key, out JsonNode? old) && JsonNode.DeepEquals(old, n)) return;
    Data[key] = n;
    s_dirty = true;
  }
  public static void Remove(string key) { if (Data.Remove(key)) s_dirty = true; }
  public static void Clear() { Data.Clear(); s_dirty = true; }

  // Writes the current slot now (only when something changed, unless force).
  public static bool Write(bool force = false) {
    if (!s_dirty && !force) return true;
    try {
      Engine.Request(new JsonObject { ["op"] = "save.write", ["slot"] = s_slot, ["data"] = Data.DeepClone() });
      s_dirty = false;
      s_lastWrite = Time.time;
      return true;
    } catch (EngineException e) {
      if (!s_warned) Debug.LogWarning("Save: " + e.Message);
      s_warned = true;
      return false;
    }
  }

  // Writes the current values into another slot, which becomes the current one ("save as").
  public static bool WriteAs(string slot) {
    _ = Data;
    s_slot = slot;
    s_dirty = true;
    return Write();
  }

  // Switches to another slot (writing the current one first) and reads it; true when that slot had a save.
  public static bool Load(string slot = "main") {
    Write();
    s_slot = slot;
    s_data = null;
    s_dirty = false;
    try {
      if (Engine.Request(new JsonObject { ["op"] = "save.read", ["slot"] = slot }) is JsonObject read) { s_data = read; return true; }
    } catch (EngineException e) { Debug.LogWarning("Save.Load: " + e.Message); }
    s_data = new JsonObject();
    return false;
  }
  public static bool Exists(string slot) => Slots.Contains(slot);
  public static void Delete(string slot) {
    Engine.Request(new JsonObject { ["op"] = "save.delete", ["slot"] = slot });
    if (slot == s_slot) { s_data = new JsonObject(); s_dirty = false; }
  }
  public static List<string> Slots {
    get {
      var list = new List<string>();
      if (Engine.Request(new JsonObject { ["op"] = "save.list" }) is JsonArray a)
        foreach (JsonNode? n in a) if (n != null) list.Add(n.GetValue<string>());
      return list;
    }
  }
  public static string Folder => Engine.Request(new JsonObject { ["op"] = "save.folder" })?.GetValue<string>() ?? "";

  // Engine side: changed values reach the file at most once a second, and when the game stops.
  internal static void EndOfFrame() { if (s_dirty && Time.time - s_lastWrite >= 1f) Write(); }
  internal static void EndOfPlay() {
    Write();
    s_data = null;
    s_slot = "main";
    s_dirty = false;
    s_lastWrite = -1e9f;
    s_warned = false;
  }
}

// The same, under the name other engines use (Unreal's SaveGame, easy-api.md's table).
public static class SaveGame {
  public static T Get<T>(string key, T fallback = default!) => Bidulgi.Save.Get(key, fallback);
  public static void Set<T>(string key, T value) => Bidulgi.Save.Set(key, value);
  public static bool Save(string slot = "main") => slot == Bidulgi.Save.Slot ? Bidulgi.Save.Write(true) : Bidulgi.Save.WriteAs(slot);
  public static bool Load(string slot = "main") => Bidulgi.Save.Load(slot);
}
