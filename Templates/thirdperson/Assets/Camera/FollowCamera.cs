// Follow camera: orbits the player with the Look action and pulls in when something is between them (a raycast), so
// walls never hide the character. 따라가는 카메라: 시점 입력으로 돌고, 사이에 벽이 있으면 앞으로 당겨 옴.
using System;
using System.Numerics;
using Bidulgi;

namespace ThirdPersonGame;

public class FollowCamera : Behaviour {
  // Where the camera looks left / right (degrees): the player moves relative to it. 플레이어 이동 기준.
  public static float Yaw { get; private set; }

  [Edit, Tooltip("Object to follow 따라갈 물체")] string target = "Player";
  [Edit, Range(1, 12), Tooltip("Distance behind (m) 뒤 거리")] float distance = 4.5f;
  [Edit, Range(0, 2), Tooltip("Look point above the target's centre (m) 보는 높이")] float height = 0.6f;
  [Edit, Range(0.1f, 5), Tooltip("Look multiplier 감도")] float sensitivity = 1f;
  [Edit, Range(-85, 0)] float minPitch = -60f;
  [Edit, Range(0, 85)] float maxPitch = 45f;
  [Edit, Range(1, 30), Tooltip("How fast it moves back out after a wall (per second)")] float ease = 6f;

  GameObject? goal;
  float yaw, pitch, current;
  public float CurrentDistance => current; // after collision 충돌 뒤 실제 거리

  void Start() {
    goal = Game.Find(target);
    Vector3 r = transform.Rotation;
    yaw = r.Y;
    pitch = r.X;
    current = distance;
    Yaw = yaw;
  }

  void LateUpdate() {
    if (!goal) return;
    Vector2 look = Input.Value<Vector2>("Look") * sensitivity;
    yaw -= look.X;
    pitch = Math.Clamp(pitch + look.Y, minPitch, maxPitch);
    Yaw = yaw;
    Vector3 pivot = goal!.position + new Vector3(0, height, 0);
    float y = yaw * MathF.PI / 180, p = pitch * MathF.PI / 180;
    var forward = new Vector3(-MathF.Sin(y) * MathF.Cos(p), MathF.Sin(p), -MathF.Cos(y) * MathF.Cos(p));
    // Something between the player and the camera: stop in front of it. 사이에 무언가 있으면 그 앞에.
    float want = distance;
    if (Physics.Raycast(pivot, -forward, out RaycastHit hit, distance + 0.3f, goal, gameObject)) want = Math.Max(hit.distance - 0.3f, 0.5f);
    current = want < current ? want : current + (want - current) * Math.Min(1f, ease * Time.deltaTime);
    transform.Position = pivot - forward * current;
    transform.Rotation = new Vector3(pitch, yaw, 0);
  }
}
