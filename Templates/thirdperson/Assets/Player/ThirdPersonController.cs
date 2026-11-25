// Third-person player: walks and runs relative to the camera, turns to face where it goes, climbs low steps, jumps,
// waves; drives the character's Animator (Speed, Jump, Wave) and its head look-at, and shows speed / state on the HUD.
// 3인칭 플레이어: 카메라 기준 이동·달리기, 가는 쪽으로 몸 돌리기, 낮은 계단 오르기, 점프, 손 흔들기.
//
// Objects (Scenes/Main.scene): Player (Rigidbody Character + capsule Collider, this script)
//   └ Hero (MeshRenderer Models/Hero.gltf + Animator Anim/Hero.animgraph + AnimatorIK: feet on the ground, look-at)
// Input actions (Input/Contexts/OnFoot.jsonl): Move, Jump, Sprint, Wave. 키는 액션 파일에서 바꿉니다.
using System;
using System.Numerics;
using Bidulgi;

namespace ThirdPersonGame;

public class ThirdPersonController : Behaviour {
  [Header("Movement 이동")]
  [Edit, Range(0.5f, 6), Tooltip("Walking speed (m/s) 걷기 속도")] float walkSpeed = 2.2f;
  [Edit, Range(1, 12), Tooltip("Running speed (m/s) 달리기 속도")] float runSpeed = 5.5f;
  [Edit, Range(1, 12), Tooltip("Jump take-off speed (m/s) 점프 속도")] float jumpSpeed = 5f;
  [Edit, Range(90, 1440), Tooltip("Turning speed (degrees per second) 도는 속도")] float turnSpeed = 720f;
  [Edit, Range(0.05f, 0.6f), Tooltip("Highest step climbed by walking (m) 걸어서 오르는 높이")] float stepHeight = 0.35f;
  [Header("Look 바라보기")]
  [Edit, Tooltip("Object the head turns to when close 가까우면 머리가 보는 물체")] string lookTarget = "Beacon";
  [Edit, Range(1, 20)] float lookDistance = 6f;

  [Auto] Rigidbody? body = null;
  Transform? model;
  Animator? animator;
  AnimatorIK? ik;
  GameObject? beacon;
  float facing = 180, shownFacing = float.NaN, wave, speedShown = -1;
  string stateShown = "", lookShown = "";
  bool grounded, jumpQueued;
  public bool Grounded => grounded;
  public float Speed { get; private set; }

  void Start() {
    foreach (GameObject c in gameObject.Children()) {
      if (c.Name != "Hero") continue;
      model = c.transform;
      animator = Game.Get<Animator>(c);
      ik = Game.Get<AnimatorIK>(c);
    }
    if (animator == null) Debug.LogWarning("ThirdPersonController: Player needs a child Hero with an Animator");
    facing = model?.Rotation.Y ?? 180;
    beacon = Game.Find(lookTarget);
  }

  void FixedUpdate() {
    if (body == null) return;
    Vector3 pos = gameObject.position; // the capsule's centre, 0.9 m above the feet
    grounded = Physics.Raycast(pos, -Vector3.UnitY, out _, 1.0f, gameObject);
    // Move relative to where the camera looks. 카메라가 보는 방향 기준으로 이동.
    Vector2 move = Input.Value<Vector2>("Move");
    float yaw = FollowCamera.Yaw * MathF.PI / 180;
    var forward = new Vector3(-MathF.Sin(yaw), 0, -MathF.Cos(yaw));
    var right = new Vector3(MathF.Cos(yaw), 0, -MathF.Sin(yaw));
    Vector3 wish = right * move.X + forward * move.Y;
    if (wish.LengthSquared() > 1) wish = Vector3.Normalize(wish);
    float speed = Input.Down("Sprint") ? runSpeed : walkSpeed;
    Vector3 v = body.Velocity;
    var next = new Vector3(wish.X * speed, v.Y, wish.Z * speed);
    if (grounded && wish.LengthSquared() > 0.01f) {
      // A low step just ahead: hop up onto it. 바로 앞 낮은 턱: 올라섬.
      Vector3 dir = Vector3.Normalize(wish);
      float feet = pos.Y - 0.9f;
      var from = new Vector3(pos.X + dir.X * 0.42f, feet + stepHeight + 0.05f, pos.Z + dir.Z * 0.42f);
      if (Physics.Raycast(from, -Vector3.UnitY, out RaycastHit step, stepHeight + 0.05f, gameObject) && step.point.Y > feet + 0.04f && step.normal.Y > 0.7f)
        next.Y = Math.Max(next.Y, 2.6f);
      // Face where we go, turning at turnSpeed. 가는 쪽을 보도록 돌기.
      float want = MathF.Atan2(wish.X, wish.Z) * 180 / MathF.PI;
      float diff = ((want - facing) % 360 + 540) % 360 - 180;
      float stepDeg = turnSpeed * Time.fixedDeltaTime;
      facing += Math.Clamp(diff, -stepDeg, stepDeg);
    }
    if (jumpQueued && grounded) {
      next.Y = jumpSpeed;
      animator?.SetTrigger("Jump");
      Audio.Play("Audio/Jump.wav", volume: 0.6f, group: "Effects");
    }
    jumpQueued = false;
    if (next != v) body.Velocity = next;
  }

  void Update() {
    if (body == null) return;
    if (Input.Pressed("Jump")) jumpQueued = true; // used by the next fixed step (a frame may have none) 다음 고정 단계에서
    if (model != null && facing != shownFacing) { model.Rotation = new Vector3(0, facing, 0); shownFacing = facing; }
    Vector3 v = body.Velocity;
    Speed = MathF.Sqrt(v.X * v.X + v.Z * v.Z);
    // The blend space picks Idle / Walk / Run from Speed. 블렌드 스페이스가 Speed로 대기·걷기·달리기를 섞음.
    animator?.SetFloat("Speed", grounded ? Speed : Math.Min(Speed, 1.6f));
    float waveTo = Input.Down("Wave") ? 1 : 0;
    if (wave != waveTo) {
      wave = Math.Clamp(wave + Math.Sign(waveTo - wave) * 4 * Time.deltaTime, 0, 1);
      animator?.SetFloat("Wave", wave);
    }
    // The head looks at the beacon when it is near. 가까우면 머리가 표지를 봄.
    string look = beacon && Vector3.Distance(beacon!.position, gameObject.position) < lookDistance ? beacon.Id : "";
    if (ik != null && look != lookShown) { ik.LookAt = look; lookShown = look; }
    // HUD
    if (MathF.Abs(Speed - speedShown) > 0.05f) { UI.Text("speed").text = $"속도 {Speed:0.0} m/s"; speedShown = Speed; }
    string state = animator?.CurrentState() ?? "-";
    if (state != stateShown) { UI.Text("state").text = $"상태 {state}"; stateShown = state; }
  }
}
