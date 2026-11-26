// Exercises the C# multiplayer API (round 7, BDG_NET_TEST): a "board" object whose [Replicated] score the server raises
// (clients log OnRep_score), and the GameMode's player (role "player"): its owner client moves it (prediction) and calls
// the server ([Server] Fire), which answers the owner ([Client] Hit) and everyone ([Multicast] Boom). The server logs
// players joining and leaving; a player object that goes away logs it.
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class NetProbe : Behaviour {
  [Edit] string role = "board";
  [Replicated] int score;
  float clock;
  bool fired;
  string myName = "";

  void OnRep_score() => Debug.Log($"net probe: onrep score={score} on {Net.Mode}");

  void Start() {
    myName = gameObject.Name; // OnDestroy may come after the object is gone
    Debug.Log($"net probe: start {role} {myName} server={IsServer} client={IsClient} owner={IsOwner} authority={HasAuthority} local={Net.LocalPlayer}");
  }

  void Update() {
    if (role == "board" && IsServer && Net.IsNetworked) {
      clock += Time.deltaTime;
      if (clock > 0.25f) { clock = 0; score++; }
    }
    if (role == "player" && IsClient && IsOwner) {
      transform.Position += new Vector3(1.5f * Time.deltaTime, 0, 0); // the owner moves it at once (prediction)
      if (!fired) { fired = true; Rpc(nameof(Fire), 7); }
    }
  }

  [Server] void Fire(int power) {
    Debug.Log($"net probe: fire {power} on {gameObject.Name}");
    Rpc(nameof(Hit), power * 2);
    Rpc(nameof(Boom));
  }
  [Client] void Hit(int damage) => Debug.Log($"net probe: hit {damage} on {gameObject.Name} owner={IsOwner}");
  [Multicast(Reliable = true)] void Boom() => Debug.Log($"net probe: boom on {gameObject.Name}");

  void OnPlayerJoined(int player) { if (role == "board") Debug.Log($"net probe: joined {player}"); }
  void OnPlayerLeft(int player) { if (role == "board") Debug.Log($"net probe: left {player}"); }
  void OnDestroy() { if (role == "player") Debug.Log($"net probe: gone {myName}"); }
}
