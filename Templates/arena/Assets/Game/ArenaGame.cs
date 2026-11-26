// The arena's rules and HUD (Unreal's GameMode + HUD): the lobby when nothing is running yet (Host / Join), the
// scoreboard from every player's replicated score, the network stats, and a round clock the server owns.
// 아레나 규칙과 화면: 로비(방 만들기 / 참가), 복제된 점수판, 네트워크 통계, 서버가 세는 라운드 시간.
//
// Host / join from the game: the lobby's buttons (this machine, port 7777). Other machines: run the built game with
// `--connect 192.168.0.5:7777`; a dedicated server: `--server --port 7777`. 다른 PC는 --connect, 전용 서버는 --server.
using System.Linq;
using Bidulgi;

namespace Arena;

public class ArenaGame : Behaviour {
  [Edit, Tooltip("Port to host and join on 포트")] int port = 7777;
  [Replicated] int round; // seconds since the server started 서버 시작 후 초
  float tick, hud;

  void Start() {
    bool lobby = !Net.IsNetworked;
    UI.Panel("lobby").visible = lobby;
    UI.Button("host").OnClick(() => { if (Net.Host(port)) UI.Panel("lobby").visible = false; });
    UI.Button("join").OnClick(() => { if (Net.Join("127.0.0.1", port)) UI.Panel("lobby").visible = false; });
  }

  bool told;
  void OnRep_round() { // clients: a new value from the server 클라이언트: 서버가 보낸 새 값
    if (told || round < 3) return;
    told = true;
    Debug.Log($"arena: round clock {round} s from the server");
  }

  void OnPlayerJoined(int player) => Debug.Log($"arena: player {player} joined ({Net.Players.Count} now)");
  void OnPlayerLeft(int player) => Debug.Log($"arena: player {player} left");

  void Update() {
    if (IsServer && Net.IsNetworked) {
      tick += Time.deltaTime;
      if (tick >= 1) { tick -= 1; round++; }
    }
    hud -= Time.deltaTime;
    if (hud > 0) return;
    hud = 0.25f;
    var board = ArenaPlayer.All.OrderByDescending(p => p.Score).Select(p => $"{p.Label}  {p.Score}");
    UI.Text("scores").text = Net.IsNetworked ? "점수\n" + string.Join("\n", board) : "";
    if (Net.IsNetworked) {
      NetStats s = Net.Stats;
      UI.Text("net").text = Net.IsServer && Net.Mode != "Client"
        ? $"{Net.Mode} · {Net.Players.Count}명 · {s.SendKbps:0} kbit/s ↑"
        : $"ping {s.Ping:0} ms · loss {s.Loss:0.0}% · {s.SendKbps:0}/{s.ReceiveKbps:0} kbit/s";
    } else {
      UI.Text("net").text = "오프라인";
    }
  }
}
