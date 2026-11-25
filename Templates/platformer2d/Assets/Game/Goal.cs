// The goal flag: reaching it ends the level. Checkpoints use the same script with isCheckpoint on.
// 골 깃발: 닿으면 레벨 끝. 체크포인트는 isCheckpoint를 켠 같은 스크립트.
using System.Numerics;
using Bidulgi;

namespace Platformer;

public class Goal : Behaviour {
  [Edit, Tooltip("A checkpoint instead of the goal 골 대신 체크포인트")] bool isCheckpoint;
  bool reached;

  void OnTriggerEnter(Collision c) {
    var hero = c.gameObject.GetComponent<PlatformerController>();
    if (hero == null || reached) return;
    reached = true;
    if (isCheckpoint) {
      hero.SetCheckpoint(gameObject.position + new Vector3(0, 1, 0));
      Debug.Log("platformer: checkpoint / 체크포인트");
      return;
    }
    Audio.Play("Audio/Goal.wav", volume: 0.7f);
    GameRules.ReachGoal();
  }
}
