# 쉬운 엔진 API — 언어별 문법과 자주 쓰는 함수

작성 2026-11-23. 목표: **처음 쓰는 사람도 자주 하는 일을 한 줄로**. 다섯 언어가 **같은 이름**을 쓰고, 언어 습관에 맞는 모양만 다릅니다.
기본 문법 예제는 [code-syntax](code-syntax.md), Inspector 노출 규칙은 그 문서의 공통 규칙을 따릅니다.

## 설계 원칙

1. **같은 이름, 같은 의미.** `Spawn`, `Find`, `After`, `Tween`, `Raycast` … C#·C++·Lua·Graph 노드 이름이 같습니다. 대소문자만 언어 관례를 따릅니다(Lua 메서드는 `:` 호출).
2. **자주 하는 일은 한 줄.** 오브젝트 찾기, 생성, 타이머, 트윈, 소리, 씬 이동이 모두 한 줄입니다.
3. **안전한 기본값.** 없는 오브젝트에 접근하면 예외로 튕기지 않고 경고 로그 + 빈 결과(`IsValid() == false`). 배포 빌드에서는 경고만.
4. **기다리기는 `await`/코루틴.** 콜백 지옥 대신 `await Wait(2)`, Lua는 `wait(2)`.
5. **문자열 대신 타입·자동완성.** 태그·레이어·입력 액션·씬 이름은 프로젝트에서 상수 코드를 자동 생성합니다(`Tags.Enemy`, `Actions.Jump`, `Scenes.Forest`). 오타가 컴파일 오류가 됩니다.
6. **AI가 읽기 쉬움.** 함수 이름이 하는 일을 그대로 말하고, 매개변수는 이름 붙여 호출 가능(C# named args, Lua 테이블).

## 자주 쓰는 일 한눈에

| 하고 싶은 일 | C# | C++ | Lua | Graph 노드 |
|---|---|---|---|---|
| 내 컴포넌트 가져오기 | `Get<Rigidbody>()` | `Get<BRigidbody>()` | `self:Get(Rigidbody)` | Get Component |
| 자식에서 찾기 | `GetInChildren<Light>()` | `GetInChildren<BLight>()` | `self:GetInChildren(Light)` | Get In Children |
| 이름·태그로 찾기 | `Find("Player")`, `FindAll(Tags.Enemy)` | `Find("Player")` | `Find("Player")` | Find |
| 가장 가까운 대상 | `FindNearest(Tags.Enemy, radius: 10)` | `FindNearest(...)` | `FindNearest(Tags.Enemy, {radius=10})` | Find Nearest |
| 생성 | `Spawn(prefab, pos, rot)` | `Spawn(Prefab, Pos)` | `Spawn(prefab, pos)` | Spawn |
| 몇 초 뒤 삭제 | `Destroy(go, after: 3)` | `Destroy(Go, 3.f)` | `Destroy(go, 3)` | Destroy (Delay) |
| 몇 초 뒤 실행 | `After(2, () => Explode())` | `After(2.f, [&]{ Explode(); })` | `After(2, function() self:Explode() end)` | After |
| 반복 실행 | `Every(0.5f, Shoot)` → 취소 핸들 | `Every(0.5f, ...)` | `Every(0.5, shoot)` | Every |
| 기다리기 | `await Wait(2);` | `co_await Wait(2.f);` | `wait(2)` | Wait |
| 다음 프레임까지 | `await NextFrame();` | `co_await NextFrame();` | `wait()` | Next Frame |
| 부드럽게 이동(트윈) | `transform.Tween(position: target, seconds: 0.5f, ease: Ease.OutBack)` | `Tween(GetTransform(), ...)` | `self.transform:Tween{position=target, seconds=0.5, ease="OutBack"}` | Tween |
| 입력 | `Input.Pressed(Actions.Jump)`, `Input.Value<Vector2>(Actions.Move)` | 같음 | `Input.Pressed(Actions.Jump)` | Input Action |
| 레이캐스트 | `if (Raycast(origin, dir, out var hit, 100)) …` | `if (auto Hit = Raycast(...))` | `local hit = Raycast(origin, dir, 100)` | Raycast |
| 범위 안 대상 | `OverlapSphere(pos, 5, Tags.Enemy)` | 같음 | `OverlapSphere(pos, 5, Tags.Enemy)` | Overlap Sphere |
| 소리 | `Audio.Play(clip, at: pos, volume: 0.8f)` | `Audio::Play(Clip, Pos)` | `Audio.Play(clip, {at=pos})` | Play Sound |
| 이펙트 | `Vfx.Play(effect, pos)` | 같음 | `Vfx.Play(effect, pos)` | Play Effect |
| 씬 이동 | `await Scene.Load(Scenes.Forest, fade: 0.5f);` | `Scene::Load(...)` | `Scene.Load(Scenes.Forest)` | Load Scene |
| 이벤트 보내기·받기 | `Events.Send(new PlayerDied(this))`, `Events.On<PlayerDied>(OnDied)` | 같음 | `Events.send("PlayerDied", self)`, `Events.on("PlayerDied", f)` | Send / On Event |
| 저장·불러오기 | `SaveGame.Save("slot1")`, `SaveGame.Load("slot1")` | 같음 | 같음 | Save / Load Game |
| 데이터 테이블 | `Data.Get<Weapon>("rifle").damage` | `Data::Get<FWeapon>("rifle")` | `Data.get("Weapons", "rifle").damage` | Get Data Row |
| 무작위 | `Random.Range(1, 6)`, `Random.Chance(0.2f)`, `list.PickRandom()` | 같음 | `random.range(1,6)`, `random.chance(0.2)` | Random |
| 화면 글자·디버그 | `Debug.Log("hp", hp)`, `Debug.DrawSphere(pos, 1, Color.red, 2)` | 같음 | `print("hp", hp)`, `Debug.DrawSphere(...)` | Print / Draw Debug |
| 시간 | `Time.delta`, `Time.scale = 0.2f`(슬로모션) | 같음 | `Time.delta` | Get Delta Time |

## 컴포넌트를 짧게 쓰는 장치

```csharp
public class Turret : Behaviour
{
    [Auto] Rigidbody body;                  // Awake 전에 자동으로 Get<Rigidbody>() — 없으면 경고
    [Auto(InChildren = true)] Light muzzle;  // 자식에서 찾기
    [Edit] Prefab bullet;
    [Edit, Range(0.1f, 2)] float fireRate = 0.5f;

    void Start() => Every(fireRate, Fire);   // 반복 발사

    void Fire()
    {
        var target = FindNearest(Tags.Enemy, radius: 20);
        if (!target) return;                 // 핸들은 bool로 바로 검사 (= IsValid())
        transform.LookAt(target);
        Spawn(bullet, muzzle.position, transform.rotation);
        Audio.Play(Sounds.Shot, at: transform.position);
    }

    async void OnDamaged(int amount)         // 이벤트 함수도 async 가능
    {
        Vfx.Play(Effects.Spark, transform.position);
        await transform.Tween(scale: 1.2f, seconds: 0.08f, pingPong: true);
    }
}
```

```lua
--!strict
local Turret = Behaviour {
    body     = Auto(Rigidbody),
    muzzle   = Auto(Light, {inChildren = true}),
    bullet   = Edit(Prefab),
    fireRate = Edit(0.5, {range = {0.1, 2}}),
}

function Turret:Start()
    Every(self.fireRate, function() self:Fire() end)
end

function Turret:Fire()
    local target = FindNearest(Tags.Enemy, {radius = 20})
    if not target then return end
    self.transform:LookAt(target)
    Spawn(self.bullet, self.muzzle.position, self.transform.rotation)
    Audio.Play(Sounds.Shot, {at = self.transform.position})
end

function Turret:OnDamaged(amount: number)    -- 코루틴: wait는 그 자리에서 멈췄다 이어짐
    Vfx.Play(Effects.Spark, self.transform.position)
    self.transform:Tween{scale = 1.2, seconds = 0.08, pingPong = true}
    wait(0.08)
end

return Turret
```

```cpp
BCLASS()
class BTurret : public BBehaviour
{
    GENERATED_BODY()
public:
    BPROPERTY(Auto) BRigidbody* Body = nullptr;
    BPROPERTY(Edit) TPrefab Bullet;
    BPROPERTY(Edit, Range=(0.1,2)) float FireRate = 0.5f;

    void Start() override { Every(FireRate, [this] { Fire(); }); }

    void Fire()
    {
        auto Target = FindNearest(Tags::Enemy, 20.f);
        if (!Target) return;
        GetTransform().LookAt(Target);
        Spawn(Bullet, GetTransform().GetPosition(), GetTransform().GetRotation());
    }

    BTask OnDamaged(int32 Amount)              // C++20 코루틴
    {
        Vfx::Play(Effects::Spark, GetTransform().GetPosition());
        co_await Tween(GetTransform(), {.Scale = 1.2f, .Seconds = 0.08f, .PingPong = true});
    }
};
```

## 자동 생성되는 상수 (오타 방지)

프로젝트 설정의 태그·레이어·입력 액션·씬·사운드·이펙트 목록에서 언어별 상수 파일을 만듭니다 (`.cache/generated/`, git 제외).

```csharp
Tags.Enemy   Layers.Ground   Actions.Jump   Scenes.Forest   Sounds.Shot   Effects.Spark
```
- 이름을 바꾸면 참조하는 코드를 자동으로 고쳐 주는 리팩터링 명령 (`bdg rename tag Enemy Foe`).
- Graph에서는 드롭다운으로 선택, Lua는 타입 검사로 오타를 잡습니다.

## Python (에디터·도구용 쉬운 함수)

```python
import bdg

sel = bdg.selection()                         # 선택한 오브젝트들
for go in bdg.find_all(tag="Enemy"):          # 씬 검색
    go.set("Health.max", 200)                 # 리플렉션 경로로 값 바꾸기 (명령 버스, Undo 가능)

with bdg.undo("나무 심기"):
    for p in bdg.scatter(area="Forest", count=200, min_dist=2.0):   # 영역에 흩뿌리기
        bdg.spawn("Prefabs/Tree.prefab", position=p, random_yaw=True)

bdg.assets.find(type="texture", larger_than=2048)                    # 에셋 질의
bdg.table("Weapons").update(lambda row: row.damage * 1.1)            # 데이터 테이블 일괄 수정
```

## 이름 대응표 (다른 엔진에서 온 사람용)

| 유니티 | 언리얼 | 비둘기 엔진 |
|---|---|---|
| `GetComponent<T>()` | `FindComponentByClass<T>()` | `Get<T>()` (긴 이름 `GetComponent`도 별칭으로 제공) |
| `Instantiate` | `SpawnActor` | `Spawn` |
| `Invoke("F", 2)` / 코루틴 | `SetTimer` | `After(2, F)` / `await Wait(2)` |
| `InvokeRepeating` | 반복 `SetTimer` | `Every` |
| DOTween(외부) | Timeline/커브 | `Tween` (기본 내장) |
| `Physics.Raycast` | `LineTraceSingle` | `Raycast` |
| `SceneManager.LoadScene` | `OpenLevel` | `Scene.Load` |
| `PlayerPrefs` | `SaveGame` | `SaveGame` / `Settings` |
| ScriptableObject 표 | DataTable | `Data` 테이블 |

에디터 검색과 AGENTS.md에 이 표를 넣어, 다른 엔진 이름으로 검색해도 비둘기 엔진 함수로 안내합니다.

## 일정

| 시기 | 내용 |
|---|---|
| M4 (C#) | `Get`·`Find`·`Spawn`·`Destroy`·`After/Every`·`await Wait`·`Tween`·`Random`·`Debug.Draw*`, `[Auto]`, 상수 자동 생성 |
| M5 (Lua·Graph·Python) | 같은 함수 세트를 Lua·Graph 노드·Python에 노출 (생성 코드, 이름 일치 테스트) |
| M6 | `Raycast`·`Overlap`·`Audio`·`Vfx`·`Events`·`SaveGame`·`Data` |
| 지속 | 언어 간 이름 일치 검사 CI: 한 언어에만 있는 함수가 생기면 실패 |
