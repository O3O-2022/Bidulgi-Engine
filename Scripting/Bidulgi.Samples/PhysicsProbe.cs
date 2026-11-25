// Physics sample (queue 6-2): counts the collision and trigger events of its object, and can push its body up
// once (AddImpulse) and read the speed back from the Rigidbody. The BDG_PHYSICS_TEST check uses it.
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class PhysicsProbe : Behaviour {
  [Edit, Tooltip("Upward impulse (N s) given once, in the first FixedUpdate; 0 = none")] public float pushUp;
  [Edit] public int collisions;
  [Edit] public int collisionExits;
  [Edit] public int triggers;
  [Edit] public string lastOther = "";
  [Edit, Tooltip("Speed read from the Rigidbody in the last FixedUpdate (m/s)")] public float speed;
  bool pushed;

  void FixedUpdate() {
    Rigidbody? rb = GetComponent<Rigidbody>();
    if (rb == null) return;
    if (pushUp > 0 && !pushed) {
      rb.AddImpulse(new Vector3(0, pushUp, 0));
      pushed = true;
    }
    speed = rb.Velocity.Length();
  }

  void OnCollisionEnter(Collision c) {
    ++collisions;
    lastOther = c.gameObject.Name;
  }
  void OnCollisionExit(Collision c) => ++collisionExits;
  void OnTriggerEnter(Collision c) {
    ++triggers;
    lastOther = c.gameObject.Name;
  }
}
