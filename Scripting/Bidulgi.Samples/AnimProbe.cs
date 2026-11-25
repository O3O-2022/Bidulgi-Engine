// Animation sample (queue 7-1): drives an Animator with the easy API. After `runAfter` seconds it sets the graph's
// Speed variable (the Idle -> Run transition cross-fades), optionally fires the Jump trigger later, and counts the
// events the graph sends (OnAnimationEvent). The BDG_ANIM_TEST check and the built player read what it logs.
using Bidulgi;

namespace Bidulgi.Samples;

public class AnimProbe : Behaviour {
  [Edit, Tooltip("Seconds before SetFloat(\"Speed\", speed)")] public float runAfter = 0.5f;
  [Edit] public float speed = 3f;
  [Edit, Tooltip("Seconds after that before SetTrigger(\"Jump\") (0 = never)")] public float jumpAfter = 0f;
  [Edit, Tooltip("Animation events received (set by the script)")] public int events;
  [Edit, Tooltip("The last event's name (set by the script)")] public string lastEvent = "";

  Animator? animator;
  float time;
  bool ran, jumped, sawRun;

  void Start() {
    animator = Get<Animator>();
    if (animator == null) Debug.LogWarning($"anim probe: {gameObject.Id} has no Animator");
  }

  void Update() {
    if (animator == null) return;
    time += Time.deltaTime;
    if (!ran && time >= runAfter) {
      animator.SetFloat("Speed", speed);
      ran = true;
      Debug.Log($"anim probe: Speed={speed} at {time:0.00} s");
    }
    if (ran && jumpAfter > 0 && !jumped && time >= runAfter + jumpAfter) {
      animator.SetTrigger("Jump");
      jumped = true;
    }
    if (animator.CurrentState() == "Run") sawRun = true;
  }

  void OnAnimationEvent(string name) {
    ++events;
    lastEvent = name;
    if (events <= 3) Debug.Log($"anim probe: event {name} at {time:0.00} s");
  }

  void OnDestroy() => Debug.Log($"anim probe: run={(sawRun ? 1 : 0)} events={events} last={lastEvent}");
}
