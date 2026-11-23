# 언어별 코드 문법 예제

작성 2026-11-21. 문법 시안입니다. API 이름은 확정 전입니다.

## 공통 규칙

| 규칙 | 내용 |
|---|---|
| 생명주기 이름 | 모든 언어가 같은 이름을 씁니다: `Awake` `Start` `Update(dt)` `FixedUpdate(dt)` `LateUpdate(dt)` `OnDestroy` (**유니티식으로 확정**, 2026-11-21, [혼합 문제 3번](../planning/unity-unreal-merge.md)) |
| 이벤트 | `OnTriggerEnter` `OnCollisionEnter` `OnAnimNotify` 등 이름이 언어와 상관없이 같음 |
| 에디터 노출 | C# `[Edit]`, C++ `BPROPERTY(Edit)`, Lua `Edit(값)`, Graph 변수의 `"edit":true` |
| 다른 언어에서 부르기 | C++ `BFUNCTION(Callable)` / C# `[Callable]`로 표시하면 다른 언어 전부에서 같은 이름으로 호출 |
| 참조 | 오브젝트는 모두 핸들. 파괴됐는지는 `IsValid()` 하나로 확인 |
| 입력 | 키가 아니라 입력 액션 이름으로 읽음 ([input](input.md)) |
| 좌표 | 오른손, Y-up, 앞쪽 -Z, 1 = 1m, 각도는 도 (`Core/Space.h`) |

## 예제: 코인 줍기

같은 기능을 언어마다 만듭니다.

- 코인은 제자리에서 돕니다.
- 플레이어가 닿으면 점수를 올리고, 소리를 내고, 사라집니다.
- 점수 시스템은 C++로 만들고, 다른 언어에서 그대로 부릅니다. 언어 간 호출이 어떻게 보이는지 확인하는 용도입니다.

---

### C++ — 언리얼식 매크로 (성능, 엔진 확장)

```cpp
// ScoreSystem.h
#pragma once
#include "Bidulgi/GameSystem.h"
#include "ScoreSystem.gen.h"          // 헤더 도구가 생성

BCLASS()
class BScoreSystem : public BGameSystem
{
    GENERATED_BODY()
public:
    BPROPERTY(Edit, Category="Score")
    int32 WinScore = 100;

    BEVENT()                           // 다른 언어에서 구독 가능
    TEvent<int32> OnScoreChanged;

    BFUNCTION(Callable)                // C#, Lua, Graph, Python에서 호출 가능
    void Add(int32 Amount)
    {
        Score += Amount;
        OnScoreChanged.Broadcast(Score);
        if (Score >= WinScore) BGame::Get()->LoadScene("Scenes/Win.scene");
    }

    BFUNCTION(Callable, Pure)
    int32 GetScore() const { return Score; }

private:
    int32 Score = 0;
};
```

```cpp
// Coin.h — 컴포넌트도 C++로 만들 수 있음
BCLASS()
class BCoin : public BBehaviour
{
    GENERATED_BODY()
public:
    BPROPERTY(Edit) float SpinSpeed = 90.f;
    BPROPERTY(Edit) int32 Value = 1;
    BPROPERTY(Edit) TSoftRef<BAudioClip> PickupSound;

    void Update(float Dt) override
    {
        GetTransform().Rotate(0.f, SpinSpeed * Dt, 0.f);
    }

    void OnTriggerEnter(BCollider& Other) override
    {
        if (!Other.GetEntity().HasTag("Player")) return;
        BGame::GetSystem<BScoreSystem>()->Add(Value);
        BAudio::PlayAt(PickupSound, GetTransform().GetPosition());
        Destroy(GetEntity());
    }
};
```

---

### C# — 유니티식 (권장 언어)

```csharp
using Bidulgi;

public class Coin : Behaviour
{
    [Edit] public float spinSpeed = 90f;
    [Edit] public int value = 1;
    [Edit] public SoftRef<AudioClip> pickupSound;

    void Update(float dt)
    {
        transform.Rotate(0, spinSpeed * dt, 0);
    }

    void OnTriggerEnter(Collider other)
    {
        if (!other.gameObject.HasTag("Player")) return;
        Game.GetSystem<ScoreSystem>().Add(value);      // C++ 클래스를 그대로 호출 (생성된 프록시)
        Audio.PlayAt(pickupSound, transform.position);
        Destroy(gameObject);
    }
}
```

```csharp
// 입력 액션 + 이벤트 구독 예
public class PlayerMove : Behaviour
{
    [Edit] public float speed = 5f;
    CharacterBody body;

    void Start()
    {
        body = GetComponent<CharacterBody>();
        Game.GetSystem<ScoreSystem>().OnScoreChanged += s => Debug.Log($"점수 {s}");
    }

    void Update(float dt)
    {
        Vector2 move = Input.Value<Vector2>("Move");
        body.Move(new Vector3(move.x, 0, move.y) * speed);
        if (Input.Pressed("Jump")) body.Jump(4.5f);
    }
}
```

---

### Lua (Luau) — 모드, 핫픽스, 가벼운 게임 로직

```lua
--!strict
local Coin = Behaviour {
    spinSpeed   = Edit(90),
    value       = Edit(1),
    pickupSound = Edit(SoftRef(AudioClip)),
}

function Coin:Update(dt: number)
    self.transform:Rotate(0, self.spinSpeed * dt, 0)
end

function Coin:OnTriggerEnter(other: Collider)
    if not other.gameObject:HasTag("Player") then return end
    Game.GetSystem(ScoreSystem):Add(self.value)
    Audio.PlayAt(self.pickupSound, self.transform.position)
    Destroy(self.gameObject)
end

return Coin
```

- 파일을 저장하면 실행 중에 바로 바뀝니다 (핫 리로드).
- `--!strict`면 Luau 타입 검사가 켜집니다. 엔진 타입 정의 파일은 리플렉션에서 자동 생성됩니다.

---

### Python — 에디터 도구, 자동화, AI

```python
import bdg

@bdg.menu("Tools/코인 줄 세우기")
def place_coins(count: int = 10, spacing: float = 2.0):
    """선택한 오브젝트 앞에 코인을 일렬로 배치합니다."""
    origin = bdg.selection.active().transform.position
    with bdg.undo("코인 배치"):                       # 한 번의 Ctrl+Z로 전부 취소
        for i in range(count):
            bdg.scene.instantiate("Prefabs/Coin.prefab",
                                  position=origin + bdg.Vec3(i * spacing, 1, 0))

@bdg.validator("Coin")
def coin_value_positive(coin):
    if coin.value <= 0:
        return bdg.error("value는 1 이상이어야 합니다", fix=lambda: setattr(coin, "value", 1))
```

- 함수 인자가 그대로 도구 창의 입력칸이 됩니다.
- `bdg validate`가 검사기를 함께 돌립니다. AI는 `fix`를 그대로 적용할 수 있습니다.
- 런타임용 Python은 데스크톱 개발 빌드에서만 선택적으로 켭니다.

---

### Graph — 비주얼 스크립팅

에디터에서는 노드로 보입니다:

```
[Event Update] ──▶ [Rotate] y = spinSpeed × dt

[Event OnTriggerEnter (other)] ──▶ [Branch: other HasTag "Player"]
        True ──▶ [ScoreSystem.Add (value)] ──▶ [Audio.PlayAt (pickupSound, position)] ──▶ [Destroy (self)]
```

저장은 텍스트(JSONL)입니다. AI가 읽고 고칠 수 있습니다:

```jsonl
{"graph":"Coin","kind":"behaviour","defaults":"1.0"}
{"var":"spinSpeed","type":"float","value":90,"edit":true}
{"var":"value","type":"int","value":1,"edit":true}
{"var":"pickupSound","type":"SoftRef<AudioClip>","edit":true}
{"id":"u","node":"Event.Update"}
{"id":"r","node":"Transform.Rotate","after":"u","in":{"y":"spinSpeed * dt"}}
{"id":"t","node":"Event.OnTriggerEnter"}
{"id":"b","node":"Branch","after":"t","in":{"cond":"other.HasTag(\"Player\")"}}
{"id":"a","node":"ScoreSystem.Add","after":"b.true","in":{"Amount":"value"}}
{"id":"s","node":"Audio.PlayAt","after":"a","in":{"clip":"pickupSound","pos":"self.position"}}
{"id":"d","node":"Destroy","after":"s","in":{"target":"self"}}
```

- `"in"`에 짧은 식(`spinSpeed * dt`)을 쓸 수 있습니다. 에디터에서는 식 노드로 펼쳐 보입니다. 노드 수가 줄어 텍스트가 짧아집니다.
- 노드 위치는 별도 섹션에 저장되므로, 위치를 옮겨도 로직 줄은 바뀌지 않습니다 (git 변경 내용이 깔끔함).

---

## 한눈에 비교

| | C++ | C# | Lua | Python | Graph |
|---|---|---|---|---|---|
| 컴포넌트 선언 | `BCLASS() class BCoin : public BBehaviour` | `class Coin : Behaviour` | `Behaviour { ... }` | (컴포넌트는 안 만듦) | `"kind":"behaviour"` |
| 에디터 노출 | `BPROPERTY(Edit)` | `[Edit]` | `Edit(90)` | 함수 인자 | `"edit":true` |
| 다른 언어에 공개 | `BFUNCTION(Callable)` | `[Callable]` | 자동 | `@bdg.command` | 자동 |
| 시스템 가져오기 | `BGame::GetSystem<T>()` | `Game.GetSystem<T>()` | `Game.GetSystem(T)` | `bdg.game.system(T)` | `Get System` 노드 |
| 바뀌면 | 모듈 라이브 리로드 | 핫 리로드 | 즉시 | 즉시 | 즉시 |
