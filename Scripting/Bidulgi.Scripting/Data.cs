// Data tables from C# (queue 4-4d): rows of Data/<Table>.table.jsonl (or .csv) as your own classes.
//
//   public class Weapon { public string id = ""; public int damage; public float range; }
//   int d = Data.Get<Weapon>("rifle").damage;
//
// The table of a class is [Table("Weapons")], else the class name + "s" (Weapon -> Weapons) when that table exists,
// else the class name. Row fields fill public fields and properties of the same name (case does not matter); a
// value the row does not have keeps the class's default. Tables are read once and kept until play stops (or a hot
// reload): edit the file, press Play again. Rows are shared: treat them as read-only.
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Bidulgi;

[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class TableAttribute : Attribute {
  public readonly string Name;
  public TableAttribute(string name) => Name = name;
}

public static class Data {
  sealed class Table {
    public required string Name, Key;
    public required List<(string key, JsonObject row)> Rows;
    public readonly Dictionary<Type, (Dictionary<string, object> byKey, System.Collections.IList all)> Typed = new();
  }
  static readonly Dictionary<string, Table?> s_tables = new(StringComparer.OrdinalIgnoreCase); // null = not found
  static readonly Dictionary<Type, string> s_tableOf = new();
  static readonly JsonSerializerOptions s_json = new() { IncludeFields = true, PropertyNameCaseInsensitive = true, NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowReadingFromString };

  // The row `key` of T's table; a warning and a new T() (its defaults) when there is no such row or table.
  public static T Get<T>(string key) where T : new() {
    if (TryGet(key, out T row)) return row;
    Debug.LogWarning($"Data: no row \"{key}\" in table {TableName(typeof(T))}");
    return new T();
  }

  public static bool TryGet<T>(string key, out T row) where T : new() {
    var typed = Typed<T>();
    if (typed != null && typed.Value.byKey.TryGetValue(key, out object? r)) { row = (T)r; return true; }
    row = default!;
    return false;
  }

  // Every row, in file order (empty when the table is missing; a warning then).
  public static IReadOnlyList<T> All<T>() where T : new() {
    var typed = Typed<T>();
    if (typed == null) { Debug.LogWarning($"Data: no table {TableName(typeof(T))}"); return Array.Empty<T>(); }
    return (IReadOnlyList<T>)typed.Value.all;
  }

  // A row without a class: Data.Row("Weapons", "rifle")?["damage"]. Null when missing.
  public static JsonObject? Row(string table, string key) {
    Table? t = Load(table);
    if (t == null) return null;
    foreach (var (k, row) in t.Rows) if (k == key) return row;
    return null;
  }
  // The row keys of a table, in file order (empty when it is missing).
  public static IReadOnlyList<string> Keys(string table) {
    var list = new List<string>();
    if (Load(table) is { } t) foreach (var (k, _) in t.Rows) list.Add(k);
    return list;
  }
  public static bool Has(string table) => Load(table) != null;

  // Forget what was read (end of play, hot reload): the next use reads the files again.
  public static void Reload() {
    s_tables.Clear();
    s_tableOf.Clear();
  }

  static (Dictionary<string, object> byKey, System.Collections.IList all)? Typed<T>() where T : new() {
    Type type = typeof(T);
    Table? t = Load(TableName(type));
    if (t == null) return null;
    if (t.Typed.TryGetValue(type, out var cached)) return cached;
    var byKey = new Dictionary<string, object>();
    var all = new List<T>();
    foreach (var (k, row) in t.Rows) {
      T value;
      try { value = row.Deserialize<T>(s_json) ?? new T(); }
      catch (Exception e) { Debug.LogWarning($"Data: {t.Name} row \"{k}\" does not fit {type.Name}: {e.Message}"); continue; }
      byKey[k] = value!;
      all.Add(value);
    }
    var entry = (byKey, (System.Collections.IList)all);
    t.Typed[type] = entry;
    return entry;
  }

  static string TableName(Type type) {
    if (s_tableOf.TryGetValue(type, out string? n)) return n;
    if (type.GetCustomAttribute<TableAttribute>() is { } a) n = a.Name;
    else n = Load(type.Name + "s", quiet: true) != null ? type.Name + "s" : type.Name;
    s_tableOf[type] = n;
    return n;
  }

  static Table? Load(string name, bool quiet = false) {
    if (s_tables.TryGetValue(name, out Table? t)) return t;
    JsonNode? v;
    try { v = Engine.Request(new JsonObject { ["op"] = "table", ["name"] = name }); }
    catch (EngineException e) {
      if (!quiet) { Debug.LogWarning("Data: " + e.Message); s_tables[name] = null; }
      return null;
    }
    var rows = new List<(string, JsonObject)>();
    string key = v?["key"]?.GetValue<string>() ?? "id";
    foreach (JsonNode? r in v?["rows"]?.AsArray() ?? new JsonArray())
      if (r is JsonObject o) {
        JsonNode? k = o[key];
        rows.Add((k is JsonValue kv && kv.TryGetValue(out string? s) ? s : k?.ToJsonString() ?? "", o));
      }
    t = new Table { Name = v?["name"]?.GetValue<string>() ?? name, Key = key, Rows = rows };
    s_tables[name] = t;
    return t;
  }
}
