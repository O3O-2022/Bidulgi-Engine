// The game rules (Unreal's GameMode): the score and the end of the round, shown on the HUD (the "HUD" canvas in
// the scene: score, message) and in the log. 게임 규칙: 점수와 라운드 끝, 화면 HUD와 로그에 표시.
using Bidulgi;

namespace FpsGame;

public class ScoreKeeper : Behaviour {
  [Edit, Tooltip("Current score (set by the game) 현재 점수")] int score;
  [Edit, Tooltip("Targets still standing (set by the game) 남은 표적")] int targetsLeft;

  static ScoreKeeper? current;
  public static int Score => current?.score ?? 0;

  void Awake() => current = this;
  void OnDestroy() { if (current == this) current = null; }

  void Start() {
    targetsLeft = FindAll("Target").Count;
    UI.Text("score").text = "점수 0";
    Debug.Log($"score: 0 — shoot the {targetsLeft} gold targets! (WASD, mouse, Space, Shift, left click) / 금색 표적 {targetsLeft}개를 맞히세요");
  }

  public static void Add(int points, string what) {
    if (current == null) return;
    current.score += points;
    current.targetsLeft--;
    Debug.Log($"score: {current.score} (+{points} {what})");
    UI.Text("score").text = $"점수 {current.score}";
    if (current.targetsLeft <= 0) {
      Debug.Log($"score: all targets down! final score {current.score} / 모든 표적 명중, 최종 점수 {current.score}");
      UIText message = UI.Text("message");
      message.text = $"모든 표적 명중!  점수 {current.score}";
      message.visible = true;
    }
  }
}
