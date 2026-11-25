// An event bus for C# (queue 6-5b), the easy-API way (Docs/features/easy-api.md): one part of the game says that
// something happened, any number of others react, without knowing each other.
//
//   Events.On("PlayerDied", OnPlayerDied);                  // by name
//   Events.On<int>("ScoreChanged", score => ...);           // with a value
//   Events.Emit("PlayerDied");   Events.Emit("ScoreChanged", 12);
//
//   public record CoinTaken(GameObject coin, int value);    // or by type: the class is the event
//   Events.On<CoinTaken>(e => total += e.value);
//   Events.Emit(new CoinTaken(gameObject, 5));
//
// A handler added inside a Behaviour belongs to it: it stops when that behaviour is disabled or destroyed, like its
// timers. Everything is cleared when the game stops. Handlers run right away, in the order they were added; an error
// in one is logged and the others still run. `Events.Send` is the same as Emit.
using System;
using System.Collections.Generic;

namespace Bidulgi;

public static class Events {
  sealed class Handler {
    public required Delegate Fn;
    public required Action<object?> Call;
    public Behaviour? Owner;
  }
  static readonly Dictionary<string, List<Handler>> s_handlers = new();
  static int s_depth;

  static void Add(string name, Delegate fn, Action<object?> call) {
    if (!s_handlers.TryGetValue(name, out var list)) s_handlers[name] = list = new List<Handler>();
    list.Add(new Handler { Fn = fn, Call = call, Owner = Scheduler.Current });
  }

  public static void On(string name, Action handler) => Add(name, handler, _ => handler());
  public static void On<T>(string name, Action<T> handler) => Add(name, handler, v => handler(v is T t ? t : default!));
  public static void On<T>(Action<T> handler) where T : class => On(typeof(T).Name, handler);

  public static void Off(string name, Delegate handler) {
    if (s_handlers.TryGetValue(name, out var list)) list.RemoveAll(h => h.Fn.Equals(handler));
  }
  public static void Off(string name, Action handler) => Off(name, (Delegate)handler);
  public static void Off<T>(string name, Action<T> handler) => Off(name, (Delegate)handler);
  public static void Off<T>(Action<T> handler) where T : class => Off(typeof(T).Name, (Delegate)handler);

  // Returns how many handlers ran.
  public static int Emit(string name) => Raise(name, null);
  public static int Emit<T>(string name, T value) => Raise(name, value);
  public static int Emit<T>(T value) where T : class => Raise(typeof(T).Name, value);
  public static int Send(string name) => Emit(name);
  public static int Send<T>(string name, T value) => Emit(name, value);
  public static int Send<T>(T value) where T : class => Emit(value);

  public static int Count(string name) => s_handlers.TryGetValue(name, out var list) ? list.Count : 0;

  static int Raise(string name, object? value) {
    if (!s_handlers.TryGetValue(name, out var list) || list.Count == 0) return 0;
    if (++s_depth > 32) { // an event that keeps emitting itself
      --s_depth;
      Debug.LogError($"Events.Emit(\"{name}\"): events nested more than 32 deep; stopped (does a handler emit its own event?)");
      return 0;
    }
    int ran = 0;
    try {
      foreach (Handler h in list.ToArray()) { // a snapshot: handlers may add or remove handlers
        if (h.Owner != null && !h.Owner.Running) continue;
        ++ran;
        Scheduler.Call(h.Owner, () => {
          try { h.Call(value); }
          catch (Exception e) { Behaviours.ReportException(h.Owner, $"event {name}", e); }
        });
      }
    } finally { --s_depth; }
    return ran;
  }

  // A behaviour was disabled or destroyed: its handlers go (Scheduler.CancelOwner).
  internal static void RemoveOwner(Behaviour b) {
    foreach (var list in s_handlers.Values) list.RemoveAll(h => h.Owner == b);
  }
  internal static void Reset() { s_handlers.Clear(); s_depth = 0; }
}
