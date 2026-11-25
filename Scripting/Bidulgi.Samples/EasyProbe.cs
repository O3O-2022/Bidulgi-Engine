// Exercises the easy API (queue 4-4c) in play mode and reports one line (BDG_EASYAPI_TEST). The scene has this
// object at the origin with a child that has a Health, and a "Coin" template at (10, 0, 0).
using System.Numerics;
using Bidulgi;

namespace Bidulgi.Samples;

public class EasyProbe : Behaviour {
  [Auto] Transform? tf = null; // filled before Awake
  [Auto(InChildren = true)] Health? childHealth = null;
  bool _get, _find, _spawn, _nearest, _destroy, _after, _every, _tween, _random, _wait;
  int _everyCount;
  Timer? _ticker;

  void Start() {
    _get = tf != null && childHealth != null && Get<Transform>() != null && Get<Health>() == null && GetInChildren<Health>() != null;
    _find = Find("Coin") && !Find("Nobody here") && FindAll("Coin").Count == 1;

    GameObject coin = Find("Coin");
    GameObject a = Spawn(coin, new Vector3(1, 0, 0));
    GameObject b = Spawn(coin, new Vector3(2, 0, 0));
    GameObject c = Spawn(coin, new Vector3(5, 0, 0));
    GameObject empty = Spawn("Marker", new Vector3(0, 7, 0));
    _spawn = a && b && c && empty && FindAll("Coin").Count == 4 && empty.position.Y == 7 && a.position.X == 1 && a.Name == "Coin";
    _nearest = FindNearest("Coin") == a && !FindNearest("Coin", radius: 0.5f) && FindNearest("Marker") == empty;

    Destroy(b);
    Destroy(c, after: 0.05f);
    _destroy = FindAll("Coin").Count == 3 && !b; // c goes later

    After(0.05f, () => _after = true);
    _ticker = Every(0.02f, () => ++_everyCount);

    Random.Seed(42);
    int r1 = Random.Range(1, 6), r2 = Random.Range(1, 6);
    float f1 = Random.Range(0f, 1f);
    Random.Seed(42);
    bool same = Random.Range(1, 6) == r1 && Random.Range(1, 6) == r2 && Random.Range(0f, 1f) == f1;
    bool inRange = true;
    for (int i = 0; i < 200; ++i) { int d = Random.Range(1, 6); if (d < 1 || d > 6) inRange = false; }
    _random = same && inRange && !Random.Chance(0) && Random.Chance(1) && new[] { 3, 4 }.PickRandom() >= 3 && Random.OnUnitSphere.Length() > 0.99f;

    RunChecks();
  }

  async void RunChecks() {
    float t0 = Time.time;
    int f0 = Time.frameCount;
    await NextFrame();
    bool nextFrame = Time.frameCount == f0 + 1;
    await Wait(0.1f);
    bool waited = Time.time - t0 >= 0.1f;
    _wait = nextFrame && waited;

    await tf!.Tween(position: new Vector3(0, 3, 0), seconds: 0.1f, ease: Ease.OutBack);
    _tween = System.MathF.Abs(tf!.Position.Y - 3) < 1e-4f;

    await Wait(0.1f);
    int counted = _everyCount;
    _ticker!.Cancel();
    await Wait(0.05f);
    _every = counted >= 3 && _everyCount == counted && !_ticker.Running;
    _destroy = _destroy && FindAll("Coin").Count == 2;
    int B(bool v) => v ? 1 : 0;
    Debug.Log($"easy probe: get={B(_get)} find={B(_find)} spawn={B(_spawn)} nearest={B(_nearest)} destroy={B(_destroy)} after={B(_after)} " +
              $"every={B(_every)} wait={B(_wait)} tween={B(_tween)} random={B(_random)}");
  }
}
