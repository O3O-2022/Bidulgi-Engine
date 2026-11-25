// A target: when a bullet hits it, it is knocked back, throws sparks (Assets/Effects/Spark.vfx), plays a sound, scores,
// and disappears a moment later. 표적: 총알에 맞으면 밀려나고, 불꽃이 튀고, 소리를 내고, 점수를 올리고, 잠시 뒤 사라집니다.
using System.Numerics;
using Bidulgi;

namespace FpsGame;

public class Target : Behaviour {
  [Edit, Range(1, 100), Tooltip("Points for hitting it 점수")] int points = 1;
  [Edit, Range(0, 10), Tooltip("Seconds it stays after the hit 맞은 뒤 남는 시간")] float removeAfter = 1.5f;

  [Auto] Rigidbody? body = null;
  bool hit;

  public void Hit(Projectile bullet, Collision c) {
    if (hit) return;
    hit = true;
    // c.normal points away from the bullet: the push direction. normal = 총알 반대쪽 = 밀리는 방향
    body?.AddImpulse((c.normal + new Vector3(0, 0.3f, 0)) * bullet.Impulse);
    Vfx.Play("Spark", c.point); // particles (Assets/Effects/Spark.vfx) 파티클
    Audio.Play("Audio/Hit.wav", at: c.point);
    ScoreKeeper.Add(points, gameObject.Name);
    Destroy(gameObject, after: removeAfter);
  }
}
