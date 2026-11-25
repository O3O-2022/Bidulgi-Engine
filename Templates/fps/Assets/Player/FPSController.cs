// First-person player: walk, sprint, jump, look. 1인칭 플레이어: 걷기, 달리기, 점프, 둘러보기.
//
// Objects (Scenes/Main.scene): Player (Rigidbody Character + capsule Collider, this script, Weapon)
//   └ Head (turns left / right: yaw)        머리: 좌우 회전
//      └ Eyes (Camera + AudioListener, turns up / down: pitch)   눈: 카메라, 위아래 회전
// Input actions (Input/Contexts/OnFoot.jsonl): Move, Look, Jump, Sprint. 키는 액션 파일에서 바꿉니다.
using System;
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;

namespace FpsGame;

public class FPSController : Behaviour {
  [Header("Movement 이동")]
  [Edit, Range(1, 10), Tooltip("Walking speed (m/s) 걷기 속도")] float walkSpeed = 4.5f;
  [Edit, Range(1, 15), Tooltip("Sprinting speed (m/s) 달리기 속도")] float sprintSpeed = 8f;
  [Edit, Range(1, 12), Tooltip("Jump take-off speed (m/s) 점프 속도")] float jumpSpeed = 5.5f;
  [Header("Look 시점")]
  [Edit, Range(0.1f, 5), Tooltip("Mouse / stick look multiplier 시점 감도")] float lookSensitivity = 1f;
  [Edit, Range(10, 89), Tooltip("How far up / down you can look (degrees) 위아래 한계")] float pitchLimit = 85f;

  [Auto] Rigidbody? body = null;
  Transform? head, eyes;
  float yaw, pitch;
  bool turned = true; // the view changed: write Head / Eyes rotations
  readonly HashSet<string> ground = new(); // what we stand on 발밑에 있는 것들

  // Where shots come from and where they go (used by Weapon). 총알이 나가는 곳과 방향.
  public Vector3 EyePosition => gameObject.position + new Vector3(0, 0.7f, 0);
  public Vector3 AimDirection {
    get {
      float y = yaw * MathF.PI / 180, p = pitch * MathF.PI / 180;
      return Vector3.Normalize(new Vector3(-MathF.Sin(y) * MathF.Cos(p), MathF.Sin(p), -MathF.Cos(y) * MathF.Cos(p)));
    }
  }
  public bool Grounded => ground.Count > 0;
  public void Kick(float degrees) { pitch = Math.Clamp(pitch + degrees, -pitchLimit, pitchLimit); turned = true; } // recoil 반동

  void Start() {
    foreach (GameObject c in gameObject.Children(deep: true)) {
      if (c.Name == "Head") head = c.transform;
      if (c.Name == "Eyes") eyes = c.transform;
    }
    if (head == null || eyes == null) Debug.LogWarning("FPSController: Player needs children Head and Eyes (Eyes has the Camera)");
    yaw = head?.Rotation.Y ?? 0;
  }

  void Update() {
    // Look: mouse right = turn right, mouse up = look up. 마우스 오른쪽 = 오른쪽으로, 위 = 위를 봄.
    Vector2 look = Input.Value<Vector2>("Look") * lookSensitivity;
    if (look != Vector2.Zero) {
      yaw -= look.X;
      pitch = Math.Clamp(pitch + look.Y, -pitchLimit, pitchLimit);
      turned = true;
    }
    if (!turned) return;
    turned = false;
    if (head != null) head.Rotation = new Vector3(0, yaw, 0);
    if (eyes != null) eyes.Rotation = new Vector3(pitch, 0, 0);
  }

  void FixedUpdate() {
    if (body == null) return;
    // Move along the ground, relative to where we look. 보는 방향 기준으로 이동.
    Vector2 move = Input.Value<Vector2>("Move");
    float speed = Input.Down("Sprint") ? sprintSpeed : walkSpeed;
    float y = yaw * MathF.PI / 180;
    var forward = new Vector3(-MathF.Sin(y), 0, -MathF.Cos(y));
    var right = new Vector3(MathF.Cos(y), 0, -MathF.Sin(y));
    Vector3 wish = right * move.X + forward * move.Y;
    if (wish.LengthSquared() > 1) wish = Vector3.Normalize(wish);
    Vector3 v = body.Velocity;
    var next = new Vector3(wish.X * speed, v.Y, wish.Z * speed);
    // Jump only from the ground. 땅에 있을 때만 점프.
    if (Input.Pressed("Jump") && Grounded) {
      next.Y = jumpSpeed;
      ground.Clear();
      Audio.Play("Audio/Jump.wav", volume: 0.6f, group: "Effects");
    }
    if (next != v) body.Velocity = next;
  }

  // Standing on something = touching it from above. 위에서 닿으면 서 있는 것.
  void OnCollisionEnter(Collision c) {
    if (c.normal.Y > 0.5f) ground.Add(c.gameObject.Id);
  }
  void OnCollisionExit(Collision c) => ground.Remove(c.gameObject.Id);
}
