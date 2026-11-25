// The rules (Unreal's GameMode): coins collected, time, the goal. Shown in the log / Console until game UI (7-3).
// 게임 규칙: 모은 동전, 시간, 골. 게임 UI(7-3) 전까지 로그·콘솔에 표시.
using Bidulgi;

namespace Platformer;

public class GameRules : Behaviour {
  [Edit, Tooltip("Coins collected (set by the game) 모은 동전")] int coins;
  [Edit, Tooltip("Coins in the level (set by the game) 레벨의 동전")] int coinsInLevel;

  static GameRules? current;
  public static int Coins => current?.coins ?? 0;

  void Awake() => current = this;
  void OnDestroy() { if (current == this) current = null; }

  void Start() {
    coinsInLevel = FindAll("Coin").Count;
    Debug.Log($"coins: 0 / {coinsInLevel} — run right to the flag! (A/D or arrows, Space, Shift) / 오른쪽 깃발까지 달리세요");
  }

  public static void AddCoins(int n) {
    if (current == null) return;
    current.coins += n;
    Debug.Log($"coins: {current.coins} / {current.coinsInLevel}");
  }

  public static void ReachGoal() {
    if (current == null) return;
    Debug.Log($"platformer: goal! {current.coins} / {current.coinsInLevel} coins in {Time.time:0.0} s / 골인!");
  }
}
