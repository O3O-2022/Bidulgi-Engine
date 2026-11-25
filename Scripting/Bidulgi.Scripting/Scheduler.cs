// Timers, waits and tweens of C# scripts (queues 4-4c, 4-6b). Everything here runs on the game thread, in game time
// (Time.time: paused frames do not advance it and the time scale applies), once per frame right after Update.
//
// Each job belongs to the behaviour that started it (the "current" behaviour: the one whose event method or async
// continuation is running). Disabling or destroying that behaviour cancels its jobs: timers stop, tweens stop where
// they are, and an `await` on a cancelled wait throws OperationCanceledException inside the async method (finally
// blocks run; the exception itself is not reported). Stop (end of play) cancels everything that is left.
//
// Async methods resume on the game thread: the scripting SynchronizationContext queues continuations (also those
// of ordinary Tasks such as Task.Run) and runs them in the next frame, and an exception that escapes an async void
// method is logged instead of ending the process.
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Threading;

namespace Bidulgi;

// What `await Wait(2)`, `await NextFrame()`, `await WaitUntil(...)` and `await tween` wait on. Await it once.
public sealed class Waiting : INotifyCompletion {
  Action? _continuation;
  bool _done, _cancelled;
  internal Behaviour? Owner;

  public Waiting GetAwaiter() => this;
  public bool IsCompleted => _done;
  public bool IsCancelled => _cancelled;
  public void OnCompleted(Action continuation) {
    if (_done) Scheduler.Resume(Owner, continuation);
    else _continuation += continuation;
  }
  public void GetResult() {
    if (_cancelled) throw new OperationCanceledException(Owner != null ? $"{Owner.GetType().Name} on {Owner.gameObject.Id} was disabled or destroyed" : "play mode ended");
  }

  internal void Finish(bool cancelled) {
    if (_done) return;
    _done = true;
    _cancelled = cancelled;
    Action? c = _continuation;
    _continuation = null;
    if (c != null) Scheduler.Resume(Owner, c);
  }
  internal static Waiting Cancelled(Behaviour? owner) {
    var w = new Waiting { Owner = owner };
    w.Finish(true);
    return w;
  }
}

// A running After / Every: Cancel() stops it. Cancelled by itself when its behaviour is disabled or destroyed.
public sealed class Timer {
  internal Scheduler.Job? Job;
  public bool Running => Job is { Done: false };
  public void Cancel() => Job?.Cancel();
}

internal static class Scheduler {
  internal abstract class Job {
    public Behaviour? Owner;
    public int StartFrame;
    public float StartTime;
    public bool Done;
    public abstract bool Step(); // true when finished
    public virtual void Cancel() => Done = true;
  }

  sealed class WaitJob : Job {
    public required Waiting W;
    public float Seconds;
    public Func<bool>? Until;
    public override bool Step() {
      if (Time.frameCount <= StartFrame) return false; // never in the frame that started it
      bool ready = Until != null ? Until() : Time.time - StartTime >= Seconds;
      if (ready) { Done = true; W.Finish(false); }
      return ready;
    }
    public override void Cancel() { if (Done) return; Done = true; W.Finish(true); }
  }

  sealed class TimerJob : Job {
    public required Action Action;
    public float Next, Interval; // Interval 0 = once
    public override bool Step() {
      if (Time.frameCount <= StartFrame) return false;
      for (int runs = 0; !Done && Time.time - StartTime >= Next && runs < 4; ++runs) { // a slow frame catches up a little
        if (Interval <= 0) Done = true;
        else Next += Interval;
        Call(Owner, Action);
      }
      if (!Done && Interval > 0 && Time.time - StartTime >= Next) Next = Time.time - StartTime + Interval; // too slow: skip ahead
      return Done;
    }
  }

  internal sealed class TweenJob : Job {
    public required Action<float> Apply; // 0..1 progress, eased
    public required Waiting W;
    public float Seconds;
    public Ease Ease;
    public bool PingPong;
    public override bool Step() {
      float total = Seconds * (PingPong ? 2 : 1);
      float t = total <= 0 ? 1 : Math.Clamp((Time.time - StartTime) / total, 0, 1);
      if (Time.frameCount <= StartFrame && t < 1) return false;
      float p = PingPong ? (t < 0.5f ? t * 2 : (1 - t) * 2) : t;
      try { Apply(Easing.Evaluate(Ease, p)); }
      catch (Exception e) { Done = true; Behaviours.ReportException(Owner, "tween", e); W.Finish(true); return true; }
      if (t >= 1) { Done = true; W.Finish(false); }
      return Done;
    }
    public override void Cancel() { if (Done) return; Done = true; W.Finish(true); }
  }

  static readonly List<Job> s_jobs = new();
  static readonly List<Job> s_added = new(); // started while jobs run: join after this pass
  static bool s_stepping;

  // The behaviour whose code is running (event method or async continuation): new jobs belong to it. An AsyncLocal,
  // so it flows into async methods and comes back with their continuations (also after awaiting a Task.Run).
  static readonly AsyncLocal<Behaviour?> s_current = new();
  static Behaviour? CurrentOwner { get => s_current.Value; set => s_current.Value = value; }
  internal static Behaviour? Current => CurrentOwner;

  internal static void Call(Behaviour? owner, Action action) {
    Behaviour? before = CurrentOwner;
    CurrentOwner = owner;
    try { action(); }
    catch (Exception e) { Behaviours.ReportException(owner, "timer", e); }
    finally { CurrentOwner = before; }
  }

  internal static void Resume(Behaviour? owner, Action continuation) {
    Behaviour? before = CurrentOwner;
    CurrentOwner = owner;
    try { continuation(); }
    catch (Exception e) { Behaviours.ReportException(owner, "async", e); }
    finally { CurrentOwner = before; }
  }

  static T Add<T>(T job) where T : Job {
    job.Owner ??= CurrentOwner;
    job.StartFrame = Time.frameCount;
    job.StartTime = Time.time;
    (s_stepping ? s_added : s_jobs).Add(job);
    return job;
  }

  static bool OwnerStopped(Behaviour? owner) => owner != null && !owner.Running;

  internal static Waiting Wait(float seconds, Func<bool>? until = null) {
    Behaviour? owner = CurrentOwner;
    if (OwnerStopped(owner)) return Waiting.Cancelled(owner); // a disabled behaviour cannot wait (no endless loops)
    var w = new Waiting { Owner = owner };
    Add(new WaitJob { W = w, Seconds = seconds, Until = until });
    return w;
  }

  internal static Timer Every(float first, float interval, Action action) {
    var timer = new Timer();
    if (OwnerStopped(CurrentOwner)) return timer;
    timer.Job = Add(new TimerJob { Action = action, Next = Math.Max(first, 0), Interval = interval });
    return timer;
  }

  internal static Timer AfterUnowned(float seconds, Action action) { // Destroy(go, after): not tied to the caller
    var timer = new Timer();
    var job = new TimerJob { Action = action, Next = Math.Max(seconds, 0), Interval = 0 };
    job.Owner = null;
    Behaviour? before = CurrentOwner;
    CurrentOwner = null;
    try { timer.Job = Add(job); } finally { CurrentOwner = before; }
    return timer;
  }

  internal static TweenJob Tween(float seconds, Ease ease, bool pingPong, Action<float> apply) {
    Behaviour? owner = CurrentOwner;
    var w = new Waiting { Owner = owner };
    var job = new TweenJob { Apply = apply, W = w, Seconds = seconds, Ease = ease, PingPong = pingPong };
    if (OwnerStopped(owner)) { job.Done = true; w.Finish(true); return job; }
    return Add(job);
  }

  // Once per frame, after Update: queued continuations first, then every job in the order it started.
  internal static void Update() {
    ScriptSyncContext.Pump();
    s_stepping = true;
    try {
      for (int i = 0; i < s_jobs.Count; ++i) {
        Job j = s_jobs[i];
        if (j.Done) continue;
        if (OwnerStopped(j.Owner)) { j.Cancel(); continue; }
        j.Step();
      }
    } finally { s_stepping = false; }
    s_jobs.RemoveAll(j => j.Done);
    s_jobs.AddRange(s_added);
    s_added.Clear();
  }

  // The behaviour was disabled or destroyed: its jobs end now (awaits resume with OperationCanceledException).
  internal static void CancelOwner(Behaviour b) {
    Events.RemoveOwner(b); // its event handlers stop with it (6-5b)
    foreach (Job j in s_jobs.ToArray()) if (j.Owner == b) j.Cancel();
    foreach (Job j in s_added.ToArray()) if (j.Owner == b) j.Cancel();
  }

  // End of play: everything left is cancelled, then forgotten.
  internal static void Reset() {
    for (int pass = 0; pass < 4 && (s_jobs.Count > 0 || s_added.Count > 0); ++pass) {
      var all = new List<Job>(s_jobs);
      all.AddRange(s_added);
      s_jobs.Clear();
      s_added.Clear();
      foreach (Job j in all) j.Cancel(); // cancelled continuations may start new jobs: those go in the next pass
    }
    s_jobs.Clear();
    s_added.Clear();
    ScriptSyncContext.Pump();
    ScriptSyncContext.Clear();
  }

  internal static int Count => s_jobs.Count + s_added.Count;
}

// Continuations posted from anywhere (another thread, a finished Task) run on the game thread in the next frame.
internal sealed class ScriptSyncContext : SynchronizationContext {
  static readonly ScriptSyncContext s_instance = new();
  static readonly Queue<(SendOrPostCallback, object?)> s_queue = new();
  static readonly object s_lock = new();
  static int s_mainThread;

  internal static void Install() {
    s_mainThread = Environment.CurrentManagedThreadId;
    SetSynchronizationContext(s_instance);
  }
  internal static void EnsureInstalled() {
    if (Current != s_instance && Environment.CurrentManagedThreadId == s_mainThread) SetSynchronizationContext(s_instance);
  }

  public override void Post(SendOrPostCallback d, object? state) {
    lock (s_lock) s_queue.Enqueue((d, state));
  }
  public override void Send(SendOrPostCallback d, object? state) {
    if (Environment.CurrentManagedThreadId == s_mainThread) d(state);
    else throw new InvalidOperationException("scripts cannot Send to the game thread from another thread; use Post");
  }
  public override SynchronizationContext CreateCopy() => this;

  internal static void Pump() {
    for (int n = 0; n < 10000; ++n) {
      (SendOrPostCallback d, object? state) item;
      lock (s_lock) {
        if (s_queue.Count == 0) return;
        item = s_queue.Dequeue();
      }
      try { item.d(item.state); }
      catch (Exception e) { Behaviours.ReportException(null, "async", e); } // e.g. what escaped an async void method
    }
  }
  internal static void Clear() { lock (s_lock) s_queue.Clear(); }
}
