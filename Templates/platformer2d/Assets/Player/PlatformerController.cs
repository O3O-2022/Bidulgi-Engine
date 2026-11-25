// The 2D hero: run, jump with coyote time and a jump buffer, fall back to the last checkpoint.
// 2D 주인공: 달리기, 점프(코요테 타임·점프 버퍼), 떨어지면 체크포인트로.
//
// Physics is Box2D on the XY plane: Rigidbody2D (Dynamic, rotation frozen) + Collider2D (box), PhysicsMaterial with
// no friction so it does not stick to walls. 물리는 XY 평면의 Box2D.
// Input actions (Input/Contexts/Play.jsonl): Move (x), Jump, Run.
using System;
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;

namespace Platformer;

public class PlatformerController : Behaviour {
  [Header("Run 달리기")]
  [Edit, Range(1, 15), Tooltip("Walking speed (m/s) 걷기 속도")] float walkSpeed = 6f;
  [Edit, Range(1, 20), Tooltip("Speed while Run is held (m/s) 달리기 속도")] float runSpeed = 9f;
  [Edit, Range(1, 100), Tooltip("How fast it reaches the speed (m/s²) 가속")] float acceleration = 60f;
  [Header("Jump 점프")]
  [Edit, Range(1, 25), Tooltip("Take-off speed (m/s) 점프 속도")] float jumpSpeed = 11f;
  [Edit, Range(0, 0.3f), Tooltip("Jump still works this long after walking off an edge (s) 코요테 타임")] float coyoteTime = 0.1f;
  [Edit, Range(0, 0.3f), Tooltip("A jump pressed this long before landing still happens (s) 점프 버퍼")] float jumpBuffer = 0.12f;
  [Edit, Range(0, 1), Tooltip("Letting go of Jump early cuts the jump (0 = full cut) 짧은 점프")] float jumpCut = 0.5f;
  [Edit, Range(-50, 0), Tooltip("Below this height the hero falls back to the checkpoint 낙사 높이")] float fallLimit = -10f;

  [Auto] Rigidbody2D? body = null;
  readonly HashSet<string> ground = new();
  float sinceGrounded = 1, sinceJumpPressed = 1;
  bool jumping;
  Vector3 checkpoint;

  public bool Grounded => ground.Count > 0;
  public void SetCheckpoint(Vector3 p) => checkpoint = p;

  void Start() => checkpoint = transform.Position;

  void FixedUpdate() {
    if (body == null) return;
    float dt = Time.fixedDeltaTime;
    sinceGrounded = Grounded ? 0 : sinceGrounded + dt;
    sinceJumpPressed = Input.Pressed("Jump") ? 0 : sinceJumpPressed + dt;

    // Run: move the x speed toward the target speed. 달리기: x 속도를 목표로 가까이.
    float target = Input.Value<Vector2>("Move").X * (Input.Down("Run") ? runSpeed : walkSpeed);
    Vector2 v = body.Velocity;
    float x = v.X + Math.Clamp(target - v.X, -acceleration * dt, acceleration * dt);

    // Jump: pressed a moment ago (buffer) and on the ground a moment ago (coyote). 점프: 버퍼 + 코요테.
    float y = v.Y;
    if (sinceJumpPressed <= jumpBuffer && sinceGrounded <= coyoteTime) {
      y = jumpSpeed;
      jumping = true;
      sinceJumpPressed = sinceGrounded = 1;
      ground.Clear();
      Audio.Play("Audio/Jump.wav", volume: 0.5f);
    }
    if (jumping && !Input.Down("Jump") && y > 0) { y *= jumpCut; jumping = false; } // short hop 짧게 뛰기
    if (y <= 0) jumping = false;

    var next = new Vector2(x, y);
    if (next != v) body.Velocity = next;
  }

  void Update() {
    if (transform.Position.Y < fallLimit) Respawn();
  }

  void Respawn() {
    transform.Position = checkpoint;
    if (body != null) body.Velocity = Vector2.Zero;
    Debug.Log("platformer: fell — back to the checkpoint / 떨어짐, 체크포인트로");
  }

  // Standing on something = touching it from above. 위에서 닿으면 서 있는 것.
  void OnCollisionEnter(Collision c) {
    if (c.normal.Y > 0.5f) ground.Add(c.gameObject.Id);
  }
  void OnCollisionExit(Collision c) => ground.Remove(c.gameObject.Id);
}
