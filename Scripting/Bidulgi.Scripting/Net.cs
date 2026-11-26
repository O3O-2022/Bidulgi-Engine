// Multiplayer from C# (round 7, M-4, Docs/features/multiplayer.md), Unreal-style and server-authoritative:
//
//   [Replicated] int score;                 // server -> every client when it changes
//   void OnRep_score() { UI.Text("score").text = $"{score}"; }   // on clients, after a new value arrived
//
//   [Server] void Fire(Vector3 dir) { ... }  // runs on the server; a client asks with Rpc(nameof(Fire), dir)
//   [Client] void Hit(int damage) { ... }    // the server -> the object's owner only
//   [Multicast(Reliable = false)] void Boom() { ... } // the server -> everyone (here too)
//   Rpc(nameof(Fire), aim);                  // routes by the attribute; standalone: just calls it
//
//   if (IsServer) ...   if (IsOwner) ...   if (HasAuthority) ...
//   void OnPlayerJoined(int player) { }     // server: a player joined (its GameMode player already spawned)
//   void OnPlayerLeft(int player) { }
//   Net.Host(7777);  Net.Join("192.168.0.5", 7777);  Net.Leave();  Net.Players;  Net.Stats
//
// Only the owner of an object may call its [Server] methods; [Client] and [Multicast] calls belong to the server.
// Objects spawned on the server (Spawn) appear on every client when they have a NetworkObject.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json.Nodes;

namespace Bidulgi;

[AttributeUsage(AttributeTargets.Field)] public sealed class ReplicatedAttribute : Attribute {}
[AttributeUsage(AttributeTargets.Method)] public sealed class ServerAttribute : Attribute { public bool Reliable { get; set; } = true; }
[AttributeUsage(AttributeTargets.Method)] public sealed class ClientAttribute : Attribute { public bool Reliable { get; set; } = true; }
[AttributeUsage(AttributeTargets.Method)] public sealed class MulticastAttribute : Attribute { public bool Reliable { get; set; } = true; }

public sealed record NetPlayerInfo(int Id, string Name);
public sealed record NetStats(float Ping, float Loss, float SendKbps, float ReceiveKbps);

public static class Net {
  static JsonObject State(string? target = null) {
    var r = new JsonObject { ["op"] = "net.state" };
    if (target != null) r["target"] = target;
    return Engine.Request(r) as JsonObject ?? new JsonObject();
  }
  public static string Mode => State()["mode"]?.GetValue<string>() ?? "Standalone";
  public static bool IsServer => State()["server"]?.GetValue<bool>() ?? true;
  public static bool IsClient => State()["client"]?.GetValue<bool>() ?? false;
  public static bool IsNetworked => Mode != "Standalone";
  public static int LocalPlayer => State()["local"]?.GetValue<int>() ?? 0;
  public static List<NetPlayerInfo> Players =>
    (Engine.Request(new JsonObject { ["op"] = "net.players" }) as JsonArray ?? new JsonArray())
      .Select(n => new NetPlayerInfo(n!["id"]!.GetValue<int>(), n["name"]!.GetValue<string>())).ToList();
  public static NetStats Stats {
    get {
      var o = Engine.Request(new JsonObject { ["op"] = "net.stats" }) as JsonObject ?? new JsonObject();
      float F(string k) => o[k]?.GetValue<float>() ?? 0;
      return new NetStats(F("ping"), F("loss"), F("send"), F("receive"));
    }
  }
  // Becomes a listen server (this game hosts and plays) / joins a server (the world is replaced by the server's).
  public static bool Host(int port = 7777, string name = "Host") => Start("listen", "", port, name);
  public static bool Join(string address, int port = 7777, string name = "Player") => Start("client", address, port, name);
  public static void Leave() => Engine.Request(new JsonObject { ["op"] = "net.leave" });
  static bool Start(string mode, string address, int port, string name) {
    try { Engine.Request(new JsonObject { ["op"] = "net.start", ["mode"] = mode, ["address"] = address, ["port"] = port, ["name"] = name }); return true; }
    catch (EngineException e) { Debug.LogWarning("Net: " + e.Message); return false; }
  }
  internal static JsonObject ObjectState(string id) => State(id);
}

public abstract partial class Behaviour {
  protected bool IsServer => Net.ObjectState(gameObject.Id)["server"]?.GetValue<bool>() ?? true;
  protected bool IsClient => Net.ObjectState(gameObject.Id)["client"]?.GetValue<bool>() ?? false;
  protected bool IsOwner => Net.ObjectState(gameObject.Id)["owner"]?.GetValue<bool>() ?? true;
  protected bool HasAuthority => Net.ObjectState(gameObject.Id)["authority"]?.GetValue<bool>() ?? true;

  // Calls a [Server], [Client] or [Multicast] method of this behaviour where its attribute says.
  protected void Rpc(string method, params object?[] args) {
    MethodInfo? m = GetType().GetMethod(method, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
    if (m == null) { Debug.LogWarning($"Rpc: {GetType().Name} has no method {method}"); return; }
    string? kind = null;
    bool reliable = true;
    if (m.GetCustomAttribute<ServerAttribute>() is { } s) { kind = "server"; reliable = s.Reliable; }
    else if (m.GetCustomAttribute<ClientAttribute>() is { } c) { kind = "client"; reliable = c.Reliable; }
    else if (m.GetCustomAttribute<MulticastAttribute>() is { } mc) { kind = "multicast"; reliable = mc.Reliable; }
    if (kind == null) { Debug.LogWarning($"Rpc: {GetType().Name}.{method} needs [Server], [Client] or [Multicast]"); return; }
    var list = new JsonArray();
    foreach (object? a in args) list.Add(Engine.ToNode(a));
    Engine.Request(new JsonObject { ["op"] = "net.rpc", ["target"] = gameObject.Id, ["type"] = GetType().Name, ["method"] = method, ["args"] = list,
                                    ["kind"] = kind, ["reliable"] = reliable });
  }
}
