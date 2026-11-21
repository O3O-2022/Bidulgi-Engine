# 그래프 · 애니메이션 · 코드 상호작용

그래프, 애니메이션 그래프, 코드(C#, C++, Lua, Python)가 **서로 부르고, 데이터를 읽고 쓰고, 이벤트를 주고받는 규칙**입니다.

관련 문서: [scripting](scripting.md) · [command-bus-and-reflection](command-bus-and-reflection.md) · [visual-scripting](../features/visual-scripting.md) · [animation](../features/animation.md)

---

## 1. 원칙

1. **그래프도 하나의 언어입니다.** 그래프, 애니메이션 그래프, C#, C++, Lua, Python은 모두 같은 **리플렉션 레지스트리**에 클래스·함수·변수·이벤트를 등록하고, 서로는 레지스트리를 통해서만 만납니다.
2. **언어끼리 직접 링크하지 않습니다.** 모든 호출은 레지스트리의 함수 표(thunk 테이블)를 거칩니다. 그래서 어느 쪽을 다시 불러와도(핫 리로드) 다른 쪽이 깨지지 않습니다.
3. **타입은 하나.** 모든 언어가 같은 타입 표([타입 매핑](#6-타입-매핑))를 씁니다.
4. **객체는 핸들로.** 언어 경계에서는 포인터 대신 핸들(인덱스 + 세대 번호)만 넘깁니다. 삭제된 객체에 접근하면 크래시 대신 명확한 에러가 납니다.
5. **스레드 규칙은 엔진이 강제.** 애니메이션 계산은 워커 스레드에서 돌고, 그동안 임의의 코드를 부를 수 없습니다. 결과(Notify 등)는 게임 스레드로 모아서 전달합니다.
6. **타입이 있는 프록시 자동 생성.** 그래프를 컴파일하면 각 언어용 프록시 코드가 생성됩니다. 문자열로 함수를 찾지 않고, 컴파일 단계에서 오타와 타입 오류가 잡힙니다.

```
            ┌────────────────── 리플렉션 레지스트리 ──────────────────┐
            │ 클래스 · 함수 · 변수 · 이벤트 · 인터페이스 · 타입 표       │
            └──▲──────▲──────▲──────▲──────▲──────▲──────────▲─────┘
               │      │      │      │      │      │          │
             C#     C++    Lua    Python  그래프  애니메이션 그래프
                                         (에디터)   (VM/AOT)   (VM/AOT, 워커 스레드)
               └──────────── 공통: 핸들 · 이벤트 버스 · 프레임 순서 ────────────┘
```

---

## 2. 노출 규칙: 무엇이 다른 쪽에 보이는가

각 언어에서 표시를 붙인 것만 다른 언어와 그래프에 보입니다.

| 노출 대상 | C# | C++ | Lua | 그래프 |
|---|---|---|---|---|
| 클래스 (상속 가능) | `[Extendable]` | `BCLASS(Extendable)` | `Script { ... }` | 항상 |
| 함수 (호출 가능) | `[Callable]` | `BFUNCTION(Callable)` | `Export(function)` | 함수는 기본 공개 (`private` 표시 가능) |
| 순수 함수 (부작용 없음) | `[Callable(Pure)]` | `BFUNCTION(Pure)` | `Pure(function)` | 순수 함수 그래프 |
| 스레드 안전 (애니메이션에서 사용 가능) | `[ThreadSafe]` | `BFUNCTION(ThreadSafe)` | 불가 | 순수 + 스레드 안전 표시 |
| 변수 읽기/쓰기 | `[Expose]` / `[Expose(ReadOnly)]` | `BPROPERTY(Expose)` | `Export(값)` | `"expose":true` |
| Inspector 노출 | `public` 필드 | `BPROPERTY(Edit)` | `Export(값)` | `"edit":true` |
| 이벤트 (구독 가능) | `[Event] public event Action<float> OnHit` | `BEVENT(OnHit, float)` | `Event()` | Event Dispatcher |
| 구현을 그래프에 맡기는 함수 | `[Implementable] partial void OnSpawned();` | `BFUNCTION(Implementable)` | — | Override |
| 기본 구현 + 그래프 덮어쓰기 | `[Overridable] virtual` | `BFUNCTION(Overridable)` | — | Override (부모 호출 노드 제공) |

- 표시가 없는 것은 해당 언어 안에서만 쓰입니다.
- `bdg schema --api <클래스>`로 한 클래스가 다른 쪽에 노출한 모든 것을 언어 상관없이 한눈에 볼 수 있습니다 (AI가 읽는 용도).

---

## 3. 코드 ↔ 그래프

### 3-1. 그래프가 코드 클래스를 상속

가장 흔한 패턴입니다. 프로그래머가 코드로 뼈대와 성능이 중요한 부분을 만들고, 기획자가 그래프로 상속해 값과 흐름을 짭니다 (언리얼과 같은 방식).

```csharp
// scripts/EnemyBase.cs
[Extendable]
public partial class EnemyBase : MonoBehaviour, IDamageable
{
    [Expose] public float hp = 100;
    [Event]  public event Action<EnemyBase> OnDied;

    [Callable] public void MoveTowards(Vector3 target, float speed) { /* 네이티브 이동 */ }

    [Implementable] partial void OnAggro(GameObject target);   // 그래프가 구현
    [Overridable] public virtual float CalcDamage(float raw) => raw;  // 기본 구현, 그래프가 덮어쓰기 가능

    public void TakeDamage(float amount)
    {
        hp -= CalcDamage(amount);
        if (hp <= 0) OnDied?.Invoke(this);
    }
}
```

```jsonc
// scripts/BP_Goblin.graph  — EnemyBase를 상속
{"v":1, "class":"BP_Goblin", "parent":"EnemyBase",
 "graphs":{
   "OnAggro":[                                   // [Implementable] 구현
     {"id":"in",  "op":"Event.OnAggro"},
     {"id":"mv",  "op":"Self.MoveTowards", "after":"in", "in":{"target":"in.target.pos", "speed":3.5}}],
   "CalcDamage":[                                // [Overridable] 덮어쓰기
     {"id":"in",  "op":"Event.CalcDamage"},
     {"id":"par", "op":"Parent.CalcDamage", "in":{"raw":"in.raw"}},
     {"id":"half","op":"Math.Mul", "in":{"a":"par", "b":0.5}},
     {"id":"ret", "op":"Return", "after":"in", "in":{"value":"half"}}]
 }}
```

### 3-2. 그래프에서 코드 함수 호출

노출된 함수는 노드 검색에 자동으로 나타납니다. 텍스트에서는 `"op":"클래스.함수"`로 씁니다.

| 대상 | `op` 예시 |
|---|---|
| 자기 자신(상속한 코드 포함) | `Self.MoveTowards` |
| 다른 오브젝트의 컴포넌트 | `PlayerController.Jump` + `"in":{"target":"@player"}` |
| 정적 함수 / 라이브러리 | `MathEx.RandomInCircle`, `Score.Add` |
| Lua·Python 함수 | 같은 방식 (`Coin.Collect`, `Tools.Spawn`) |

### 3-3. 코드에서 그래프 사용

그래프를 컴파일하면 언어별 **타입 있는 프록시**가 `.gen/`에 생성됩니다.

```csharp
// 생성됨: .gen/csharp/BP_Goblin.g.cs (직접 수정하지 않음)
public sealed partial class BP_Goblin : EnemyBase
{
    public float Speed { get => GetVar<float>("speed"); set => SetVar("speed", value); }
    public void Attack() => CallBp("Attack");
    public event Action<BP_Goblin> OnEnraged { add => Bind("OnEnraged", value); remove => Unbind("OnEnraged", value); }
}
```

```csharp
// 사용하는 코드
var goblin = GetComponent<BP_Goblin>();
goblin.Speed = 4;          // 그래프 변수
goblin.Attack();           // 그래프 함수
goblin.OnEnraged += g => Debug.Log($"{g.name} 분노!");   // 그래프 Event Dispatcher 구독
```

```lua
-- Lua: 생성된 타입 정의(.gen/luau/BP_Goblin.d.luau) 덕분에 자동완성과 타입 검사
local goblin = self.gameObject:GetComponent(BP_Goblin)
goblin.speed = 4
goblin:Attack()
goblin.OnEnraged:Connect(function(g) print(g.name .. " 분노!") end)
```

```cpp
// C++: 생성된 헤더
#include "BP_Goblin.gen.h"
if (auto g = owner->Get<BP_Goblin>()) { g->SetSpeed(4); g->Attack(); }
```

- 그래프에서 변수나 함수 이름을 바꾸면 프록시가 다시 생성되고, 그것을 쓰던 코드는 **컴파일 에러**로 바로 드러납니다.
- 프록시 없이 이름으로 부르는 동적 호출(`CallBp("Attack")`)도 있지만, 에디터 경고가 붙고 AI 안내 파일에서도 권장하지 않습니다.

### 3-4. 인터페이스 (언어를 모르고 호출)

인터페이스는 어느 언어로든 정의하고, 어느 언어로든 구현할 수 있습니다. 호출하는 쪽은 상대가 C#인지 그래프인지 몰라도 됩니다.

```csharp
[Interface] public interface IDamageable { void TakeDamage(float amount); }
```

```jsonc
// 그래프에서 구현
{"class":"BP_Barrel", "parent":"Behaviour", "implements":["IDamageable"],
 "graphs":{"TakeDamage":[ {"id":"in","op":"Event.TakeDamage"}, {"id":"boom","op":"Self.Explode","after":"in"} ]}}
```

```csharp
// 호출: 상대가 BP_Barrel(그래프)이든 EnemyBase(C#)든 같은 코드
if (hit.gameObject.TryGet<IDamageable>(out var d)) d.TakeDamage(25);
```

그래프에서는 `Message: TakeDamage` 노드로 부릅니다 (구현하지 않은 대상이면 조용히 무시, 언리얼과 동일).

### 3-5. 이벤트 (Event Dispatcher / event)

- 모든 언어의 이벤트는 같은 **멀티캐스트 이벤트**입니다. C# `event`, C++ `BEVENT`, Lua `Event()`, 그래프 Event Dispatcher가 서로 구독할 수 있습니다.
- 구독은 핸들로 기록되어, 구독한 쪽 오브젝트가 삭제되면 자동으로 해제됩니다.
- 호출 순서는 구독 순서. 예외가 나도 다른 구독자는 계속 호출되고, 에러는 Console에 언어와 위치를 함께 표시합니다.

---

## 4. 코드·그래프 ↔ 애니메이션 그래프

### 4-1. 데이터가 애니메이션으로 가는 두 가지 방법

| 방식 | 내용 | 언제 |
|---|---|---|
| **당겨 오기 (기본)** | `.animgraph`의 `update`에 소유자 속성 경로를 적어 두면, 엔진이 게임 스레드에서 한 번에 읽어 스냅샷으로 넘김 | 속도, 공중 여부처럼 매 프레임 바뀌는 상태 |
| **밀어 넣기** | 코드나 그래프가 애니메이션 변수를 직접 설정 | 조준 각도, 감정 상태처럼 게임 로직이 정하는 값 |

```jsonc
// anim/ABP_Knight.animgraph
{"vars":{"speed":"float", "inAir":"bool", "aimPitch":"float", "mood":"enum<Mood>"},
 "update":{
   "speed": "owner.CharacterController.velocity.xz.length",
   "inAir": "owner.CharacterController.airborne"}}
```

```csharp
var anim = GetComponent<ABP_Knight>();   // 생성된 타입 프록시 (그래프와 같은 방식)
anim.AimPitch = cam.pitch;                // 밀어 넣기
anim.Mood = Mood.Angry;
```

- `update` 경로는 컴파일 때 검사합니다. 없는 속성이면 `.animgraph` 파일의 해당 줄을 가리키는 에러가 납니다.
- 밀어 넣은 값은 다음 애니메이션 업데이트 때 반영됩니다 (같은 프레임 안에서 순서가 보장됨, [프레임 순서](#5-프레임-순서와-스레드-규칙) 참고).

### 4-2. 코드·그래프가 애니메이션에게 시키는 것

| 동작 | C# | 그래프 노드 |
|---|---|---|
| Montage 재생 / 섹션 이동 / 정지 | `anim.PlayMontage(AM_Attack, section:"Combo2")` | `Play Montage` |
| 애니메이션 레이어 교체 | `anim.LinkLayer("Weapon", ALI_Sword)` | `Link Anim Layer` |
| IK 목표 설정 | `anim.SetIKTarget("LeftHand", ladderRung.position)` | `Set IK Target` |
| 현재 상태 읽기 | `anim.CurrentState("Locomotion")` | `Get Current State` |
| 커브 값 읽기 | `anim.GetCurve("WeaponTrail")` | `Get Curve Value` |
| 루트 모션 사용 여부 | `anim.RootMotion = RootMotionMode.MontageOnly` | `Set Root Motion Mode` |

`PlayMontage`는 핸들을 돌려주고, 그 핸들로 완료·중단·섹션 진입을 기다릴 수 있습니다.

```csharp
var m = anim.PlayMontage(AM_Attack);
m.OnNotify("HitStart", () => weapon.EnableHitbox());
m.OnBlendOut(() => state = State.Idle);
await m.Ended;                       // C# async (게임 스레드에서 재개)
```

그래프에서는 `Play Montage` 노드가 `On Completed`, `On Blend Out`, `On Interrupted`, `On Notify Begin` 실행 핀을 가집니다 (언리얼과 동일).

### 4-3. 애니메이션이 코드·그래프에게 알리는 것

| 이벤트 | 내용 | 받는 쪽 |
|---|---|---|
| **Anim Notify** | 클립의 특정 시점 (발소리, 타격 판정 시작) | 소유자의 `OnAnimNotify_<이름>` 함수, 또는 `.animgraph`에서 지정한 함수 |
| **Notify State** | 구간 (Begin / Tick / End) | `OnNotifyBegin_<이름>`, `OnNotifyEnd_<이름>` |
| **상태 진입·이탈** | State Machine 상태가 바뀜 | `OnStateEnter_<머신>_<상태>`, `OnStateExit_...` |
| **Montage 콜백** | 시작, 섹션 진입, Blend Out, 끝, 중단 | `PlayMontage` 핸들 또는 노드의 실행 핀 |

받는 함수는 어떤 언어로 써도 됩니다. 이름 규칙 대신 명시적으로 연결할 수도 있습니다.

```jsonc
// anim/ABP_Knight.animgraph
{"notifies":{
   "Footstep": "FootstepAudio.Play",       // 다른 컴포넌트(C#)의 함수
   "HitStart": "Self.OnHitStart",          // 소유자의 함수 (그래프든 코드든)
   "HitEnd":   "Self.OnHitEnd"},
 "stateEvents":{"Locomotion.Jump":{"enter":"Self.OnJumpStarted"}}}
```

```lua
-- Lua에서 받기 (이름 규칙)
function Knight:OnAnimNotify_Footstep(info)
    Audio.PlayAt("sfx/step_" .. info.surface, self.transform.position)
end
```

### 4-4. 애니메이션 그래프 안에서 코드 부르기

- **Event Graph (게임 스레드 단계):** 노출된 함수라면 무엇이든 호출할 수 있습니다.
- **Anim Graph와 전환 조건 (워커 스레드):** `[ThreadSafe]`(+ 순수) 표시가 있는 함수만 호출할 수 있습니다. 표시가 없는 함수를 연결하면 **컴파일 에러**가 납니다. 언리얼에서 런타임 경고로만 드러나던 문제를 컴파일 단계에서 막습니다.

```csharp
[Callable(Pure), ThreadSafe]
public static bool CanLandCleanly(float verticalSpeed, float groundAngle) => verticalSpeed > -12 && groundAngle < 35;
```

```jsonc
{"from":"Jump", "to":"Idle", "when":{"fn":"MoveRules.CanLandCleanly", "args":["vSpeed","groundAngle"]}, "blend":0.15}
```

---

## 5. 프레임 순서와 스레드 규칙

한 프레임 안에서 언어와 시스템이 실행되는 순서입니다. 모든 언어가 같은 순서를 따릅니다.

```
① 입력 처리
② Update — C#, C++, Lua, 그래프 (Python은 데스크톱 개발 빌드에서 켰을 때만) (게임 스레드, 우선순위 그룹 순)
③ 애니메이션 업데이트 단계 (게임 스레드)
     · update 경로 읽기 → 스냅샷
     · ②에서 밀어 넣은 값, Montage 명령 반영
     · Event Graph 실행
④ 애니메이션 평가 (워커 스레드, 캐릭터 여러 명을 묶어서 병렬)
     · Anim Graph, State Machine, 블렌딩, IK
     · 이때 생긴 Notify·상태 이벤트는 큐에 쌓기만 함
⑤ 애니메이션 이벤트 전달 (게임 스레드) — Notify, 상태 진입·이탈, Montage 콜백
⑥ 물리 (고정 타임스텝)
⑦ LateUpdate — 모든 언어
⑧ 렌더링 준비
```

| 규칙 | 이유 |
|---|---|
| ④ 동안 게임 상태는 읽기만, 그것도 스냅샷으로만 | 워커 스레드와 게임 스레드가 같은 데이터를 동시에 건드리지 않게 |
| Notify는 ⑤에서 한꺼번에 전달 | 받는 코드는 언제나 게임 스레드에서 실행됨. 어느 언어든 스레드 걱정 없음 |
| ⑤에서 Montage를 새로 틀면 다음 프레임 ③에서 반영 | 같은 프레임 재진입으로 생기는 꼬임 방지 |
| Python은 기본적으로 이 루프에 참여하지 않음 | AI·에디터·도구용. 데스크톱 개발 빌드나 전용 서버에서 켜면 ② 단계에 참여 |

---

## 6. 타입 매핑

| 엔진 타입 | 그래프 핀 | C# | C++ | Lua |
|---|---|---|---|---|
| bool | 빨강 | `bool` | `bool` | `boolean` |
| int | 청록 | `int` | `int32` | `number` (정수 검사) |
| float | 초록 | `float` | `float` | `number` |
| string | 분홍 | `string` | `BString` | `string` |
| Vector2/3, Quat, Color | 노랑 | `Vector3` 등 | `BVec3` 등 | `Vector3` 등 |
| enum | 진초록 | `enum` | `BENUM()` | 문자열 리터럴 유니온 |
| struct | 파랑(진) | `[Struct] struct` | `BSTRUCT()` | 테이블 타입 |
| GameObject / 컴포넌트 참조 | 하늘색 | `GameObject`, `T : Component` | `Ref<T>` | 객체 |
| 에셋 참조 | 보라 | `Asset<T>` | `AssetRef<T>` | 객체 |
| 배열 / 맵 | 격자 아이콘 | `List<T>` / `Dictionary` | `BArray` / `BMap` | 테이블 |
| 이벤트 / 콜백 | 흰색 테두리 | `Action<...>` | `BDelegate<...>` | `function` |

- 경계를 넘을 때 문자열, 배열은 복사됩니다. 큰 데이터는 핸들이나 에셋 참조로 넘기는 것을 권장합니다.
- 숫자 범위를 넘는 변환(예: Lua number → int)은 런타임에 검사하고, 실패하면 호출 위치와 함께 에러를 냅니다.

---

## 7. 성능

| 호출 경로 | 비용 (상대값, 목표치) | 설명 |
|---|---|---|
| 같은 언어 안 | 1 | 일반 함수 호출 |
| AOT 그래프 ↔ C++ | ≈1 | 빌드에서는 그래프가 C++로 바뀌어 직접 호출 |
| C# ↔ C++ | 낮음 | 함수 포인터 표를 통한 호출, 마샬링 없는 타입 위주 |
| VM 그래프 ↔ 코드 | 중간 | thunk 테이블 경유 (에디터) |
| Lua ↔ 코드 | 중간 | 인자 변환 비용 |
| 이름으로 찾는 동적 호출 | 높음 | 첫 호출에 캐시, 그래도 권장하지 않음 |

- 매 프레임 많은 오브젝트에 대해 언어 경계를 넘는 호출은 **배치 API**로 묶습니다 (`ctx.query`, `ForEachComponent`).
- 프로파일러는 언어 경계 호출을 별도 항목으로 보여 줍니다 (`bdg profile`의 "interop" 줄).

---

## 8. 끝까지 따라가 보는 예시

플레이어가 공격 → 몽타주 재생 → 타격 Notify → 고블린(그래프)이 피해를 받음 → 죽으면 점수 증가와 UI 갱신.

```
[C#  PlayerCombat]          공격 입력 → anim.PlayMontage(AM_Attack)
        │
[AnimGraph AG_Knight]         AM_Attack 재생 → 0.38초에 Notify "HitStart"
        │  (⑤ 게임 스레드로 전달)
[C#  PlayerCombat]          OnAnimNotify_HitStart → 범위 검사 → IDamageable.TakeDamage(25)
        │
[그래프 BP_Goblin]       EnemyBase.TakeDamage → CalcDamage(그래프 덮어쓰기, 절반) → hp 0 이하
        │                    → OnDied 이벤트 발생
        ├─▶ [C#   GameManager]   OnDied 구독 → Score.Add(10)
        └─▶ [Lua HudScore]      Score.OnChanged 구독 → 위젯 텍스트 갱신 + menu_pop.anim 재생
```

```csharp
public class PlayerCombat : MonoBehaviour
{
    ABP_Knight anim;
    void Start() => anim = GetComponent<ABP_Knight>();
    void Update() { if (Input.Pressed("Fire")) anim.PlayMontage(AM_Attack); }

    void OnAnimNotify_HitStart(AnimNotifyInfo info)
    {
        foreach (var hit in Physics.OverlapSphere(weapon.tip, 1.2f))
            if (hit.TryGet<IDamageable>(out var d)) d.TakeDamage(25);
    }
}

public class GameManager : MonoBehaviour
{
    void Start() { foreach (var e in FindAll<EnemyBase>()) e.OnDied += _ => Score.Add(10); }
}
```

```lua
--!strict
local HudScore = Script { label = Export(Widget.Ref("score")) }
function HudScore:Start()
    Score.OnChanged:Connect(function(v: number)
        self.label.text = tostring(v)
        self.label:PlayAnim("anim/menu_pop.anim")
    end)
end
return HudScore
```

---

## 9. 디버깅

- **언어를 넘는 호출 스택:** 브레이크포인트에서 `C# PlayerCombat.OnAnimNotify_HitStart ← AnimGraph Notify(HitStart, 0.38s) ← C# PlayerCombat.Update`처럼 언어와 시스템이 섞인 스택을 한 줄로 보여 줍니다.
- **그래프 와이어 값:** 코드가 넘긴 값도 와이어 위에 표시됩니다.
- **애니메이션 디버거:** 현재 상태, 진행 중 Montage, 이번 프레임에 나간 Notify 목록과 받은 함수.
- **텍스트 로그 (AI용):** `bdg sim --trace interop`으로 언어 경계 호출과 이벤트를 시간순 텍스트로 출력합니다.

---

## 10. 개발 순서

| 단계 | 내용 |
|---|---|
| 1 | 레지스터리 타입 표, 핸들, 이벤트 버스 (M1, 모든 언어 공통 기반) |
| 2 | C# ↔ C++ 호출, `[Callable]`·`[Expose]`·`[Event]` (M5) |
| 3 | 그래프 → 코드 호출, 코드 클래스 상속, `[Implementable]`·`[Overridable]` (M7.5) |
| 4 | 그래프 컴파일 시 C#·Lua·C++ 프록시 생성 |
| 5 | 애니메이션: `update` 경로, 밀어 넣기, Montage 핸들, Notify 전달, `[ThreadSafe]` 컴파일 검사 (M7) |
| 6 | 인터페이스, 언어를 넘는 디버거 호출 스택, `--trace interop` |
| 7 | Python 프록시(에디터·AI용), 배치 API 최적화 |
