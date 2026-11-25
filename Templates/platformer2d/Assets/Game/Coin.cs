// A coin: the hero touches it (a trigger collider), it plays a sound, counts, and disappears.
// 동전: 주인공이 닿으면(트리거) 소리, 점수, 사라짐.
using Bidulgi;

namespace Platformer;

public class Coin : Behaviour {
  [Edit, Range(1, 100), Tooltip("Points 점수")] int value = 1;
  bool taken;

  void OnTriggerEnter(Collision c) {
    if (taken || c.gameObject.GetComponent<PlatformerController>() == null) return;
    taken = true;
    Audio.Play("Audio/Coin.wav", volume: 0.6f);
    GameRules.AddCoins(value);
    Destroy(gameObject);
  }
}
