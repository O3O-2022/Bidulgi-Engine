// The rules (Unreal's GameMode): coins collected, time, the goal — shown on the HUD (the "HUD" canvas in the scene)
// and in the log. 게임 규칙: 모은 동전, 시간, 골 — 화면 HUD와 로그에 표시.
using Bidulgi;

namespace Platformer;

public class GameRules : Behaviour {
  [Edit, Tooltip("Coins collected (set by the game) 모은 동전")] int coins;
  [Edit, Tooltip("Coins in the level (set by the game) 레벨의 동전")] int coinsInLevel;

  static GameRules? current;
  public static int Coins => current?.coins ?? 0;
  bool finished;

  void Awake() => current = this;
  void OnDestroy() { if (current == this) current = null; }

  void Start() {
    coinsInLevel = FindAll("Coin").Count;
    ShowCoins();
    Debug.Log($"coins: 0 / {coinsInLevel} — run right to the flag! (A/D or arrows, Space, Shift) / 오른쪽 깃발까지 달리세요");
    // the clock, ten times a second 시계 (0.1초마다)
    Every(0.1f, () => { if (!finished) UI.Text("time").text = $"{Time.time:0.0} 초"; });
    // the help button toggles the controls panel 도움말 버튼: 조작 안내를 켜고 끔
    UI.Button("help").OnClick(() => {
      UIPanel panel = UI.Panel("helpPanel");
      panel.visible = !panel.visible;
      Debug.Log($"platformer: help {(panel.visible ? "shown" : "hidden")}");
    });
  }

  void ShowCoins() => UI.Text("coins").text = $"동전 {coins} / {coinsInLevel}";

  public static void AddCoins(int n) {
    if (current == null) return;
    current.coins += n;
    current.ShowCoins();
    Debug.Log($"coins: {current.coins} / {current.coinsInLevel}");
  }

  public static void ReachGoal() {
    if (current == null || current.finished) return;
    current.finished = true;
    Debug.Log($"platformer: goal! {current.coins} / {current.coinsInLevel} coins in {Time.time:0.0} s / 골인!");
    UIText message = UI.Text("message");
    message.text = $"골인!  동전 {current.coins} / {current.coinsInLevel}  ·  {Time.time:0.0} 초";
    message.visible = true;
  }
}
