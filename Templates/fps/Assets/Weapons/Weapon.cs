// The gun: fire rate, magazine, reload, spread, recoil. Numbers live in Data/Weapons.table.jsonl, not here.
// 총: 연사, 탄창, 재장전, 퍼짐, 반동. 수치는 코드가 아니라 Data/Weapons.table.jsonl에 있습니다.
//
// Shots are real physics projectiles: the scene has a pool of "Bullet" objects (Rigidbody, no gravity) waiting far
// below the level; firing moves one to the muzzle and launches it. (Objects spawned while playing get physics bodies
// after queue 6-1/6-2 follow-ups; until then the pool is the way.) 총알은 미리 만들어 둔 "Bullet" 오브젝트를 재사용합니다.
//
// Change the gun: set `weapon` to "pistol" or "shotgun" in the Inspector, or add a row to the table.
// 총 바꾸기: Inspector에서 weapon을 pistol·shotgun으로, 또는 표에 행을 추가.
using System;
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;
using Random = Bidulgi.Random;

namespace FpsGame;

[Table("Weapons")]
public class WeaponData {
  public string id = "", name = "";
  public float fireRate = 5, reloadSeconds = 1.5f, spreadDegrees = 1, bulletSpeed = 40, impulse = 4, recoilDegrees = 1;
  public bool automatic = true;
  public int magazine = 20, pellets = 1;
}

public class Weapon : Behaviour {
  [Edit, Tooltip("Row of Data/Weapons.table.jsonl: rifle, pistol, shotgun 무기 표의 행")] string weapon = "rifle";
  [Edit, Tooltip("Rounds left in the magazine (set by the script) 남은 탄")] int ammo;

  [Auto] FPSController? player = null;
  WeaponData data = new();
  readonly List<GameObject> pool = new();
  int next;
  float cooldown;
  bool reloading;

  public WeaponData Data => data;

  void Start() {
    data = Bidulgi.Data.Get<WeaponData>(weapon);
    ammo = data.magazine;
    pool.AddRange(FindAll("Bullet"));
    if (pool.Count == 0) Debug.LogWarning("Weapon: no \"Bullet\" objects in the scene (the projectile pool)");
    Debug.Log($"weapon: {data.name} — {data.magazine} rounds, {data.fireRate}/s (R = reload)");
  }

  void Update() {
    cooldown -= Time.deltaTime;
    if (Input.Pressed("Reload")) Reload();
    bool trigger = data.automatic ? Input.Down("Fire") : Input.Pressed("Fire");
    if (trigger && cooldown <= 0 && !reloading) Fire();
  }

  void Fire() {
    if (player == null || pool.Count == 0) return;
    if (ammo <= 0) { Reload(); return; }
    --ammo;
    cooldown = 1f / Math.Max(data.fireRate, 0.1f);
    Vector3 eye = player.EyePosition, aim = player.AimDirection;
    for (int i = 0; i < Math.Max(data.pellets, 1); ++i) {
      Vector3 dir = Spread(aim, data.spreadDegrees);
      GameObject b = pool[next++ % pool.Count];
      b.GetComponent<Projectile>()?.Launch(eye + aim * 0.6f, dir * data.bulletSpeed, data.impulse);
    }
    player.Kick(data.recoilDegrees);
    Audio.Play("Audio/Shoot.wav", volume: 0.5f);
  }

  async void Reload() {
    if (reloading || ammo == data.magazine) return;
    reloading = true;
    Debug.Log("weapon: reloading");
    await Wait(data.reloadSeconds); // cancelled if the player is destroyed / disabled 플레이어가 사라지면 취소
    ammo = data.magazine;
    reloading = false;
  }

  // A direction inside a cone around `dir`. dir 둘레 원뿔 안의 방향.
  static Vector3 Spread(Vector3 dir, float degrees) {
    if (degrees <= 0) return dir;
    Vector3 side = Vector3.Normalize(Vector3.Cross(dir, MathF.Abs(dir.Y) > 0.99f ? Vector3.UnitX : Vector3.UnitY));
    Vector3 up = Vector3.Cross(side, dir);
    Vector2 r = Random.InsideUnitCircle * MathF.Tan(degrees * MathF.PI / 180);
    return Vector3.Normalize(dir + side * r.X + up * r.Y);
  }
}
