// One player on the ring (the GameMode spawns a copy of "PlayerProto" for every player, owned by them).
// 링 위의 플레이어 하나 (GameMode가 접속한 사람마다 PlayerProto를 복사해 주인으로 정함).
//
// Server-authoritative: the owner moves it at once (client-side prediction, NetworkTransform), the server checks the
// moves (maxSpeed) and owns everything that decides the game: pushes, falling off, scores.
// 서버 권한: 주인은 바로 움직이고(예측), 밀기·떨어짐·점수는 서버가 정합니다.
using System;
using System.Collections.Generic;
using System.Numerics;
using Bidulgi;

namespace Arena;

public class ArenaPlayer : Behaviour {
  [Edit, Tooltip("Metres per second 이동 속도")] float speed = 6;
  [Edit, Tooltip("Push reach (m) 밀기 거리")] float pushRange = 2.4f;
  [Edit, Tooltip("Push strength (m/s) 미는 힘")] float pushStrength = 9;
  [Replicated] int score; // the server counts, everyone sees it 서버가 세고 모두에게 보임

  public static readonly List<ArenaPlayer> All = new(); // the players in this game (each process has its own list)
  public int Score => score;
  public string Label { get; private set; } = "";

  Vector3 knock;          // server: sliding from a push 서버: 밀려 미끄러지는 속도
  ArenaPlayer? lastPusher;
  float cooldown;

  void Start() {
    Label = gameObject.Name;
    if (Label != "PlayerProto") All.Add(this);
  }
  void OnDestroy() => All.Remove(this);

  void Update() {
    if (!Net.IsNetworked || Label == "PlayerProto") return;
    cooldown -= Time.deltaTime;
    if (IsOwner) { // our player: move now, ask the server to push
      Vector2 m = Input.Value<Vector2>("Move");
      transform.Position += new Vector3(m.X, 0, -m.Y) * speed * Time.deltaTime;
      if (Input.Pressed("Push") && cooldown <= 0) {
        cooldown = 0.4f;
        Rpc(nameof(Push));
      }
    }
    if (IsServer) { // the rules
      if (knock.LengthSquared() > 0.01f) {
        transform.Position += knock * Time.deltaTime;
        knock *= MathF.Exp(-3f * Time.deltaTime);
      }
      Vector3 p = transform.Position;
      if (MathF.Abs(p.X) > 10.6f || MathF.Abs(p.Z) > 10.6f) Fall();
    }
  }

  // Asked by the owner, run by the server: everyone near is pushed away. 주인이 요청, 서버가 실행.
  [Server] void Push() {
    Vector3 me = transform.Position;
    foreach (ArenaPlayer other in All) {
      if (other == this) continue;
      Vector3 d = other.transform.Position - me;
      d.Y = 0;
      float dist = d.Length();
      if (dist > pushRange || dist < 0.001f) continue;
      other.knock = Vector3.Normalize(d) * pushStrength;
      other.lastPusher = this;
    }
    Rpc(nameof(PushFx));
  }

  // Everyone sees the push. 밀기를 모두가 봄.
  [Multicast(Reliable = false)] void PushFx() => transform.Tween(scale: 1.35f, seconds: 0.08f, pingPong: true);

  // Server: off the ring -> back to a start, a point for whoever pushed (or one off for falling alone).
  void Fall() {
    if (lastPusher != null && lastPusher != this) lastPusher.score++;
    else score = Math.Max(0, score - 1);
    lastPusher = null;
    knock = Vector3.Zero;
    var starts = FindAll("PlayerStart 1");
    transform.Position = starts.Count > 0 ? starts[0].position : new Vector3(0, 0.5f, 0);
  }
}
