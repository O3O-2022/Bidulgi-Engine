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
| 소리 ([6-3](audio.md)) | `Audio.Play(clip, at: pos, volume: 0.8f)` | `Audio::Play(Clip, Pos)` | `Audio.Play(clip, {at=pos})` | Play Sound |
| 이펙트 | `Vfx.Play(effect, pos)` | 같음 | `Vfx.Play(effect, pos)` | Play Effect |
| 씬 이동 | `await Scene.Load(Scenes.Forest, fade: 0.5f);` | `Scene::Load(...)` | `Scene.Load(Scenes.Forest)` | Load Scene |
| 이벤트 보내기·받기 ([6-5b](#c-구현-현황--세이브이벤트레이캐스트이펙트-6-5b-2026-11-25)) | `Events.Emit(new PlayerDied(this))`, `Events.On<PlayerDied>(OnDied)` (`Send` = `Emit`) | 같음 | `Events.send("PlayerDied", self)`, `Events.on("PlayerDied", f)` | Send / On Event |
| 저장·불러오기 ([6-5b](#c-구현-현황--세이브이벤트레이캐스트이펙트-6-5b-2026-11-25)) | `Save.Set("best", 12)`, `Save.Get("best", 0)`, `Save.Load("slot2")` (`SaveGame.Save/Load`도) | 같음 | 같음 | Save / Load Game |
| 화면 UI ([7-3](ui-widgets.md#구현-현황--런타임-게임-ui-7-3-런타임-2026-11-25)) | `UI.Text("score").text = "점수 1"`, `UI.Button("restart").clicked` | — | — | Set Text / On Click |
| 데이터 테이블 ([4-4d](data-tables.md)) | `Data.Get<Weapon>("rifle").damage` | `Data::Get<FWeapon>("rifle")` | `Data.get("Weapons", "rifle").damage` | Get Data Row |
| 무작위 | `Random.Range(1, 6)`, `Random.Chance(0.2f)`, `list.PickRandom()` | 같음 | `random.range(1,6)`, `random.chance(0.2)` | Random |
| 화면 글자·디버그 | `Debug.Log("hp", hp)`, `Debug.DrawSphere(pos, 1, Color.red, 2)` | 같음 | `print("hp", hp)`, `Debug.DrawSphere(...)` | Print / Draw Debug |
| 시간 | `Time.delta`, `Time.scale = 0.2f`(슬로모션) | 같음 | `Time.delta` | Get Delta Time |

## C# 구현 현황 (4-4c, 2026-11-25)

`Behaviour` 안에서는 그냥 부르고(`Get<Health>()`, `Find("Player")`), Behaviour 밖의 코드는 `Game.Find`·`Game.Spawn`처럼 `Game` 클래스로 부릅니다. 코드: `Scripting/Bidulgi.Scripting/Easy.cs`(함수), `Scheduler.cs`(타이머·트윈·기다리기). 스크립팅 API 버전 4.

| 함수 | 동작 |
|---|---|
| `Get<T>()`, `GetInChildren<T>()` | 엔진 컴포넌트(생성된 클래스)와 C# Behaviour 모두. 자식 찾기는 자기 자신 → 자식(깊이 우선) 순서 |
| `[Auto]`, `[Auto(InChildren = true)]` | Awake **전에** 필드를 채움(핫 리로드 뒤에도 다시). 없으면 경고 로그. 같은 오브젝트의 C# Behaviour는 컴포넌트 목록에서 **앞에 있는 것만** 보임(생성 순서) |
| `Find(이름 또는 id)` | 없으면 경고 + `GameObject.None`(`if (!target)`이 참). `GameObject`는 `bool`로 바로 검사(`IsValid` = 아직 장면에 있음), `==`는 id 비교 |
| `FindAll(이름)`, `FindNearest(이름, radius)` | 계층 순서. `FindNearest`는 월드 위치 기준, 자기 오브젝트 제외, 없으면 `GameObject.None` |
| `Spawn(원본, 위치, 회전, 부모)`, `Spawn("이름", 위치)` | 원본(자식·C# Behaviour 포함)을 **새 id로 복사**하거나 빈 오브젝트를 만듦. 명령 버스의 `add` 한 번(플레이 중엔 Undo 없음). 복사본의 Behaviour는 **다음 프레임 시작**에 Awake. 프리팹 에셋은 6-1에서 — 지금은 장면의 아무 오브젝트나 원본으로 |
| `Destroy(go)`, `Destroy(go, after: 3)` | 오브젝트와 자식을 지움(`del`). 그 오브젝트들의 Behaviour는 **바로** OnDisable·OnDestroy, 타이머·트윈·기다리기 취소. `after`는 게임 시간 뒤(부른 Behaviour가 먼저 사라져도 실행) |
| `After(초, 함수)`, `Every(초, 함수, first:)` → `Timer` | 게임 시간. `Every`의 첫 실행은 한 간격 뒤(`first`로 바꿈), 느린 프레임은 최대 4번 따라잡음. `timer.Cancel()` |
| `await Wait(초)`, `await NextFrame()`, `await WaitUntil(조건)` | 게임 시간(일시정지·시간 배율 반영), 같은 프레임 안에서는 끝나지 않음. 자세한 규칙은 4-6b |
| `transform.Tween(position:, rotation:, scale:, seconds:, ease:, pingPong:)` → `Tween` | 지금 값에서 목표로(로컬 값). `scale: 1.2f`처럼 숫자 하나 = 균일. `await`하면 끝날 때까지, `Cancel()`. 숫자 트윈 `Game.Tween(from, to, seconds, v => ...)`. `Ease` 15가지(Linear, In/Out/InOut Quad·Cubic·Sine·Back, OutElastic, OutBounce) |
| `Random.Range(1, 6)`(정수는 **양 끝 포함**), `Range(0f, 1f)`, `Value`, `Chance(p)`, `Sign`, `InsideUnitCircle/Sphere`, `OnUnitSphere`, `Seed(n)`, `list.PickRandom()`, `list.Shuffle()` | 스크립트 전체가 한 생성기(xorshift). `Seed`로 같은 결과 재현. `using System;`과 함께 쓰면 `System.Random`과 이름이 겹치므로 `using Random = Bidulgi.Random;` |

타이머·트윈·기다리기는 **시작한 Behaviour에 묶입니다**: 그 Behaviour가 꺼지거나(disable) 지워지면 취소되고, 플레이를 멈추면 남은 것이 모두 취소됩니다. 매 프레임 순서: Start·Update → (스크립트가 보낸 비동기 이어하기) → 타이머·기다리기·트윈(시작한 순서) → FixedUpdate → LateUpdate.

엔진 쪽 새 요청(`ScriptApi`): `children`, `findall`, `spawn`, `destroy`. 자동 검사: `BDG_EASYAPI_TEST=1` + `BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll`(샘플 `EasyProbe`) → `easy probe: get=1 find=1 spawn=1 nearest=1 destroy=1 after=1 every=1 wait=1 tween=1 random=1`, `easy api test: restored=1 instances=0`.

**아직 남은 것**: 태그·레이어·입력 액션·씬 상수 자동 생성(`Tags.Enemy` …) — 엔진에 태그·입력 액션 목록이 아직 없음(태그가 생기면 `FindAll(Tags.Enemy)`도), `Time.scale` 쓰기(지금은 Play 도구 막대의 배율), `Debug.Draw*`는 4-4b.

## C# 구현 현황 — 세이브·이벤트·레이캐스트·이펙트 (6-5b, 2026-11-25)

코드: `Scripting/Bidulgi.Scripting/Save.cs`, `Events.cs`, `Physics.cs`(Raycast·OverlapSphere), `Vfx.cs`, 엔진 쪽 `Engine/Scripting/SaveGame.*`, `ScriptApi`의 `save.*`·`physics.*`·`vfx.*` 요청.

```csharp
int best = Save.Get("best", 0);                  // 지난 실행이 저장한 값 (처음엔 0)
Save.Set("best", score);                          // 1초 안에, 그리고 게임이 멈출 때 파일로
Events.On<int>("ScoreChanged", s => UI.Text("score").text = $"점수 {s}");
Events.Emit("ScoreChanged", score);
if (Raycast(eye, forward, out RaycastHit hit, 100)) Vfx.Play("Spark", hit.point);
foreach (GameObject e in OverlapSphere(transform.Position, 5, "Enemy")) ...
PlaySound("Audio/Hit.wav");  PlayEffect("Spark");  // 이 오브젝트 위치에서 (Audio.Play·Vfx.Play 줄임)
```

| 기능 | 동작 |
|---|---|
| **세이브** `Save.Get/Set/Has/Remove/Clear`, `Write()`, `WriteAs(slot)`, `Load(slot)`, `Delete`, `Exists`, `Slots`, `Folder` | 값은 JSON으로 되는 것 무엇이든(숫자·글자·bool·`Vector3`·목록·public 필드 클래스). 처음 쓸 때 현재 슬롯(기본 `main`)을 읽고, 바뀐 값은 **1초에 한 번까지 + 플레이가 끝날 때** 파일에 씀. 플레이가 끝나면 메모리를 비워서 다음 실행은 파일에서 다시 읽음(= 재시작 후 불러오기). `SaveGame.Save/Load`는 다른 엔진 이름 |
| 저장 폴더 | 프로젝트 설정 `saves.saveFolder`(기본 `%APPDATA%/{company}/{name}/Saves`)에 `{company}`·`{name}`(폴더에 못 쓰는 글자는 뺌)·`%환경변수%`(한글 사용자 폴더도 그대로)를 채움. `BDG_SAVE_DIR`이 있으면 그 폴더(테스트·포터블). **에디터 플레이와 빌드한 게임이 같은 폴더** — FPS 템플릿 CI가 에디터에서 저장한 최고 점수를 빌드한 게임이 읽는지 확인 |
| 파일 형식 | 슬롯 하나 = `<슬롯>.save.json`: `{"format":"bidulgi-save","version":1,"game","gameVersion","slot","saved"(UTC),"data":{...}}`. 임시 파일에 쓰고 바꿔 끼움, 직전 저장은 `.bak`으로 남기고 새 파일이 깨졌으면 `.bak`을 읽음(경고). 더 새 형식 버전은 읽지 않고 이유를 알려 줌 |
| **이벤트** `Events.On/Off/Emit/Send/Count` | 이름으로(`On("Ping", f)`), 값과 함께(`On<int>("Score", f)`), **타입으로**(`On<CoinTaken>(f)` + `Emit(new CoinTaken(...))`). 바로, 더한 순서대로 실행; 한 핸들러의 예외는 로그만 남기고 나머지는 계속. Behaviour 안에서 더한 핸들러는 그 Behaviour가 꺼지거나 지워지면 같이 빠짐(타이머와 같은 규칙), 플레이가 끝나면 모두 지움. 32단계 넘게 중첩되면 멈추고 오류 |
| **레이캐스트** `Raycast(origin, dir, out hit, maxDistance, triggers)`, `OverlapSphere(center, r, name, triggers)` | 실행 중인 물리(PHI)에 묻기: Jolt 3D와 Box2D 2D(XY 평면) 모두, 가장 가까운 것. `RaycastHit`: `gameObject`·`point`·`normal`(광선 쪽)·`distance`. 트리거는 `triggers: true`일 때만. 물체는 마지막 물리 스텝 위치. 플레이 밖에서는 경고 한 번 + 안 맞음 |
| **이펙트** `Vfx.Play(effect, position, rotation, seed)` → `Effect`(`.Stop(clear)`, `.Playing`, `.Particles`), `Vfx.Particles` | [6-VFX P-1](particles.md#구현-현황--p-1-2026-11-25) `.vfx`를 이름(`"Spark"`)이나 Assets 기준 경로로. 한 번 터지는 효과는 파티클이 다 사라지면 스스로 없어짐. 없는 이펙트는 경고 + `Effect.None` |
| 소리·이펙트 줄임 | Behaviour의 `PlaySound(clip, volume, group)`, `PlayEffect(effect, offset)` = 이 오브젝트 위치에서 `Audio.Play`·`Vfx.Play` |

엔진 요청(`ScriptApi.h`): `save.write/read/list/delete/folder`, `physics.raycast/overlap`, `vfx.play/stop/playing/alive`. PHI(`Physics/Physics.h`)에 `Raycast`·`OverlapSphere` 두 함수 추가(Jolt: NarrowPhaseQuery, Box2D: `b2World_CastRay`·`OverlapShape`), `PhysicsWorld::Raycast/OverlapSphere/Current()`가 몸체 번호를 오브젝트 id로 바꿈.

검사: 단위 테스트(폴더 패턴·한글 경로, 왕복, 덮어쓰기 `.bak`, 깨진 파일 → `.bak`, 새 버전 거부, 목록·삭제; Jolt·Box2D 레이캐스트 거리·법선·트리거·짧은 광선, 겹침), `BDG_GAMEAPI_TEST`(C# `GameApiProbe`: 저장 → 정지 → 다시 플레이에서 읽힘, 이벤트 5가지, 레이캐스트·겹침, 이펙트 12개 생성 후 사라짐, 슬롯 파일 형식) → `game api test: save=1 events=1 raycast=1 overlap=1 vfx=1 restored=1 file=1`, FPS 템플릿 `sparks=1 best=1` + 빌드한 게임 `best score: 1`(CI).

**아직 남은 것**: 클라우드 세이브(`saves.cloudSaves`), 암호화·변조 검사, 게임 데이터 버전 올리기(마이그레이션 함수), 다른 스레드에서 쓰기, `BoxCast`·`SphereCast`·레이어 마스크(레이어가 생기면), 이벤트의 Graph 노드·Lua 이름, `Tags`·`Effects` 상수 자동 생성.

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
| M6 | `Raycast`·`Overlap`·`Audio`·`Vfx`·`Events`·`SaveGame`·`Data` — C# 완료(6-3, 4-4d, 6-5b) |
| 지속 | 언어 간 이름 일치 검사 CI: 한 언어에만 있는 함수가 생기면 실패 |
