// A bullet of the pool: flies straight (gravity scale 0), hits things, goes back to the pool.
// 총알 하나: 곧게 날아가 맞히고 풀로 돌아갑니다.
using System.Numerics;
using Bidulgi;

namespace FpsGame;

public class Projectile : Behaviour {
  [Edit, Tooltip("Seconds before an unused bullet returns to the pool 빗나간 총알이 돌아오는 시간")] float lifetime = 2f;

  [Auto] Rigidbody? body = null;
  public float Impulse { get; private set; } = 4;
  Timer? expire;
  bool flying;
  Vector3 home; // its place in the pool, below the level 풀 자리 (맵 아래)

  void Start() => home = transform.Position;

  public void Launch(Vector3 from, Vector3 velocity, float impulse) {
    if (body == null) return;
    Impulse = impulse;
    transform.Position = from;
    body.Velocity = velocity;
    flying = true;
    expire?.Cancel();
    expire = After(lifetime, Park);
  }

  void OnCollisionEnter(Collision c) {
    if (!flying) return;
    c.gameObject.GetComponent<Target>()?.Hit(this, c);
    Park();
  }

  void Park() {
    flying = false;
    expire?.Cancel();
    if (body != null) body.Velocity = Vector3.Zero;
    transform.Position = home;
  }
}
