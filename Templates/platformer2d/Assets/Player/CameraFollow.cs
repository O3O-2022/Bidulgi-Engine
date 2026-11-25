// The camera follows the hero sideways with a dead zone (it moves only when the hero leaves the middle part).
// 카메라가 주인공을 따라감 (가운데 데드존 안에서는 가만히).
using System;
using System.Numerics;
using Bidulgi;

namespace Platformer;

public class CameraFollow : Behaviour {
  [Edit, Tooltip("Name of the object to follow 따라갈 오브젝트 이름")] string target = "Hero";
  [Edit, Range(0, 10), Tooltip("Half width of the dead zone (m) 데드존 반 너비")] float deadZoneX = 1.5f;
  [Edit, Range(0, 10), Tooltip("Half height of the dead zone (m) 데드존 반 높이")] float deadZoneY = 1.5f;
  [Edit, Range(0.5f, 20), Tooltip("How quickly it catches up 따라잡는 빠르기")] float smoothing = 6f;

  GameObject hero = GameObject.None;
  Vector3 offset;

  void Start() {
    hero = Find(target);
    if (hero) offset = transform.Position - hero.position;
  }

  void LateUpdate() {
    if (!hero) return;
    Vector3 cam = transform.Position, want = hero.position + offset;
    float dx = want.X - cam.X, dy = want.Y - cam.Y;
    // only the part outside the dead zone pulls the camera 데드존 밖으로 나간 만큼만
    dx = MathF.Sign(dx) * MathF.Max(MathF.Abs(dx) - deadZoneX, 0);
    dy = MathF.Sign(dy) * MathF.Max(MathF.Abs(dy) - deadZoneY, 0);
    if (dx == 0 && dy == 0) return;
    float k = 1 - MathF.Exp(-smoothing * Time.deltaTime);
    transform.Position = new Vector3(cam.X + dx * k, cam.Y + dy * k, cam.Z);
  }
}
