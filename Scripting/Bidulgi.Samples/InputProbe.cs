// Input actions sample (queue 6-4): reads actions, never keys. The BDG_INPUT_TEST check drives it with injected keys
// and mouse moves and reads its counters back.
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class InputProbe : Behaviour {
  [Edit, Tooltip("Times Jump was pressed (Update)")] public int jumps;
  [Edit, Tooltip("Times Jump was pressed, seen in FixedUpdate")] public int fixedJumps;
  [Edit, Tooltip("Times Brake was pressed (the Vehicle context)")] public int brakes;
  [Edit] public float moveX;
  [Edit] public float moveY;
  [Edit, Tooltip("Look (mouse) added up")] public float lookX;

  void Update() {
    if (Input.Pressed("Jump")) ++jumps;
    if (Input.Pressed("Brake")) ++brakes;
    Vector2 move = Input.Value<Vector2>("Move");
    moveX = move.X;
    moveY = move.Y;
    lookX += Input.Action("Look").vector.X;
  }

  void FixedUpdate() {
    if (Input.Action("Jump").pressed) ++fixedJumps;
  }
}
