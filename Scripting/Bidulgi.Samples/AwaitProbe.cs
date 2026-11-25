// Async waits in behaviours (queue 4-6b), checked by BDG_AWAIT_TEST. `mode` picks the scenario:
//   loop    counts `ticks` every 0.05 s of game time until it is cancelled (the object deleted or the behaviour
//           disabled), then reports from catch / finally
//   timed   async Task Start: Wait(0.5) in game time, WaitUntil, NextFrame, Task.Run coming back to the game thread
//   self    destroys its own object while waiting: the next wait is cancelled at once
using System;
using System.Threading.Tasks;
using Bidulgi;

namespace Bidulgi.Samples;

public class AwaitProbe : Behaviour {
  [Edit] string mode = "loop";
  [Edit, Tooltip("Waits finished so far (set by the script)")] int ticks;

  async Task Start() { // a Task-returning event method (async void works too)
    if (mode == "timed") { await Timed(); return; }
    if (mode == "self") { await Self(); return; }
    bool cancelled = false;
    try {
      while (true) {
        await Wait(0.05f);
        ++ticks;
      }
    } catch (OperationCanceledException) {
      cancelled = true;
    } finally {
      Debug.Log($"await probe: {gameObject.Id} mode=loop cancelled={(cancelled ? 1 : 0)} finally=1 ticks={ticks}");
    }
  }

  async Task Timed() {
    int mainThread = Environment.CurrentManagedThreadId;
    float t0 = Time.time;
    await Wait(0.5f);
    float waited = Time.time - t0;
    int f0 = Time.frameCount;
    await WaitUntil(() => Time.frameCount >= f0 + 3);
    bool until = Time.frameCount == f0 + 3;
    int worker = await Task.Run(() => Environment.CurrentManagedThreadId);
    bool back = Environment.CurrentManagedThreadId == mainThread && worker != mainThread;
    await NextFrame();
    Debug.Log($"await probe: timed waited={(waited >= 0.5f ? 1 : 0)} until={(until ? 1 : 0)} thread={(back ? 1 : 0)} game={waited:0.00}");
  }

  async Task Self() {
    await NextFrame();
    Destroy(gameObject);
    try {
      await Wait(1);
      Debug.LogError("await probe: self resumed after its object was destroyed");
    } catch (OperationCanceledException) {
      Debug.Log("await probe: self cancelled=1");
    }
  }
}
