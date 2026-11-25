# 스크립팅 (다국어)

지원 언어는 **그래프, C++, C#, Python, Lua** 다섯 가지입니다 (2026-11-21 확정).
언어마다 손으로 바인딩을 붙이지 않고 **C API 허브 + 자동 생성** 구조로 만듭니다.

> 변경 기록: Rust는 지원 언어에서 뺐습니다. "블루프린트"는 언리얼 용어와 겹치지 않게 **그래프(Graph)**로 이름을 바꿨습니다.

## 구조

```
                 ┌──────── 리플렉션 레지스트리 (단일 진실 공급원) ────────┐
                 │  컴포넌트·API 정의 → 바인딩 코드 자동 생성              │
                 └───────────────────────┬──────────────────────────────┘
                                         ▼
 ┌─────────────────────────── C++ 코어 ───────────────────────────┐
 │                  안정적인 C API (bdg_api.h)                    │
 └──────┬──────────┬──────────┬──────────┬──────────┬─────────────┘
        ▼          ▼          ▼          ▼          ▼
     C++ 모듈     C# (.NET 8)  Lua (Luau)  Python      그래프
     헤더 툴      hostfxr      내장 VM      CPython      레지스터 VM
```

- 코어는 바깥으로 **순수 C 함수만** 노출합니다. 모든 언어가 호출할 수 있는 공통어입니다.
- 언어끼리는 직접 호출하지 않고 **리플렉션 레지스트리와 이벤트 버스**로 소통합니다 → [interop](interop.md)
- 씬에서는 클래스 이름으로 붙이고, 엔진이 파일과 언어를 찾습니다: `"PlayerController":{"speed":6.5}`
- 어느 언어든 **공개 필드 = Inspector 항목 = 씬 JSON 키**입니다.

## 언어별 역할

| 언어 | 주 용도 | 문법 기준 | 게임 빌드에 포함 |
|---|---|---|---|
| **C#** | 메인 게임플레이 (기본 언어) | 유니티 | 모든 플랫폼 |
| **그래프** | 비주얼 스크립팅, 기획자·디자이너용 로직, 애니메이션 그래프 | 언리얼 블루프린트 구조 → [visual-scripting](../features/visual-scripting.md) | 모든 플랫폼 |
| **C++** | 엔진 확장, 고성능 시스템, 네이티브 라이브러리 연결 | 언리얼 매크로 | 모든 플랫폼 |
| **Lua** | 가벼운 로직, 대사·이벤트, 모딩 | 유니티식 메서드 이름 | 모든 플랫폼 |
| **Python** | **AI 에이전트 스크립트**, 에디터 자동화, 에셋 파이프라인, 테스트, 데이터·밸런싱 도구, 빌드 스크립트 | 블렌더 `bpy`식 | 데스크톱 개발 빌드·전용 서버만 (선택) |

### C# — 유니티 방식

AI가 가장 잘 아는 형태라서 기본 언어로 둡니다.

```csharp
public class Goblin : MonoBehaviour
{
    [Range(0, 500)] public float hp = 100;
    public Transform target;
    [HideInInspector] public int combo;

    void Update()
    {
        transform.position = Vector3.MoveTowards(transform.position, target.position, 2 * Time.deltaTime);
    }
    void OnCollisionEnter(Collision c) { }
}
```

- .NET 8을 hostfxr로 호스팅, `AssemblyLoadContext`로 핫 리로드
- `MonoBehaviour`, `transform`, `GetComponent<T>()`, `Time.deltaTime` 등 같은 형태의 API

### 그래프 — 언리얼 블루프린트 구조, 새 이름

언리얼 블루프린트와 같은 구조(이벤트 그래프, 함수, 변수, 디스패처, 인터페이스)를 쓰되, 텍스트로 저장하고 빠른 VM으로 실행합니다.

| 종류 | 파일 | 대응하는 언리얼 개념 |
|---|---|---|
| 그래프 클래스 | `.graph` | Blueprint Class |
| 애니메이션 그래프 | `.animgraph` | Animation Blueprint |
| 씬 그래프 | 씬마다 하나 | Level Blueprint |
| 함수·매크로 라이브러리 | `.graphlib` | Function / Macro Library |

상세: [visual-scripting](../features/visual-scripting.md), 애니메이션: [animation](../features/animation.md)

### C++ — 언리얼 방식

매크로 + 헤더 툴 코드 생성. 접두어는 엔진 이름이 정해지면 확정합니다 (지금 문서의 `B`는 임시).

```cpp
#include "Goblin.generated.h"

BCLASS()
class Goblin : public Behaviour {
    GENERATED_BODY()
public:
    BPROPERTY(Edit, Range = "0,500") float hp = 100;
    BPROPERTY(Edit) Ref<GameObject> target;

    BFUNCTION(Callable) void TakeDamage(float amount);
    void Update(float dt) override;
};
```

- **1.0:** 프로젝트의 C++ 모듈을 엔진과 함께 빌드 (에디터 재시작으로 반영). 성능이 중요한 시스템과 외부 C/C++ 라이브러리 연결에 사용
- **1.0 이후:** DLL 핫 리로드 (언리얼 Live Coding과 비슷한 방식), 플러그인 크래시 격리
- 대량 처리(군집, 파티클 로직, 길찾기)는 C++에서 ECS 쿼리 API로 직접 처리합니다 (Rust가 맡던 역할)

### Lua — Luau 구현

Lua 문법을 그대로 쓰되, 구현은 **Luau**(Lua 5.1 호환, MIT 라이선스)를 씁니다.

- **타입 표기**(선택)가 있어서 AI가 실수를 덜 하고 에러를 미리 잡을 수 있음
- 일반 Lua보다 빠르고, **샌드박스**가 잘 되어 있어 모드 지원에 적합
- 타입을 쓰지 않으면 일반 Lua 코드와 거의 같음
- 메서드 이름은 유니티와 맞춤 (`Update`, `OnTriggerEnter`)

```lua
local Coin = Script {
    value = Export(10),     -- Inspector에 노출
    spin  = Export(90.0),
}

function Coin:Update(dt)
    self.transform:Rotate(0, self.spin * dt, 0)
end

function Coin:OnTriggerEnter(other)
    if other.tag == "Player" then
        Score.Add(self.value)
        self.gameObject:Destroy()
    end
end

return Coin
```

### Python — AI용, 그리고 여러 용도로

Python은 게임 로직의 주 언어가 아니라 **엔진을 다루는 언어**입니다. AI 에이전트가 가장 잘 쓰고, 도구를 만들기 쉬워서 용도를 넓게 열어 둡니다.

| 용도 | 예 |
|---|---|
| **AI 에이전트 스크립트** | 에이전트가 명령 수백 개 대신 짧은 스크립트 하나로 씬을 고침. 실행 전 변경 범위 미리보기, 하나의 Undo 그룹 |
| 에디터 자동화 | 메뉴·버튼 추가, 일괄 작업 (블렌더 `bpy`처럼) |
| 에셋 파이프라인 | 일괄 임포트, 텍스처 변환, 이름 규칙 검사 |
| 테스트 | 플레이 테스트 작성, 결과 분석 |
| 데이터·밸런싱 | TSV 밸런스 표 계산, 시뮬레이션 결과 그래프 |
| 빌드·배포 | 빌드 스크립트, 패치 생성 |
| 게임 실행 (선택) | 데스크톱 개발 빌드에서 프로토타이핑, 전용 서버의 운영 도구. **모바일·웹 빌드에는 넣지 않음** (크기·성능) |

```python
import bidulgi as bd

# AI 에이전트가 쓰는 예: 고블린 10마리를 원형으로 배치하고 결과 확인
with bd.batch("고블린 원형 배치"):            # 하나의 Undo 그룹
    for i in range(10):
        bd.scene.add(f"g{i+4}", prefab="goblin", parent="Enemies",
                     pos=bd.math.circle(i, 10, radius=6))

print(bd.diagnose.budget("forest_01", platform="Android"))
```

- 에디터에 Python 콘솔과 스크립트 패널 내장
- 프로젝트의 `tools/` 폴더 스크립트는 메뉴에 자동 등록
- 외부 패키지(pip)는 프로젝트별 가상 환경에 설치

## 기다리기 — C# `async` (4-6b, 2026-11-25)

Behaviour의 메서드는 `async`로 쓸 수 있습니다. 이벤트 메서드도 됩니다(`async void Start()` 또는 `async Task Start()` — `Task`를 돌려주면 실패가 로그에 남음).

```csharp
public class Door : Behaviour {
  async Task Start() {
    await WaitUntil(() => Find("Player").position.Z > 3); // 매 프레임 Update 뒤에 검사
    await transform.Tween(position: new Vector3(0, 3, 0), seconds: 1);
    await Wait(2);                                       // 게임 시간 2초
    await transform.Tween(position: Vector3.Zero, seconds: 1);
  }
}
```

규칙 (코드: `Scripting/Bidulgi.Scripting/Scheduler.cs`):
- **게임 시간**: `Wait(초)`는 `Time.time`으로 잽니다 — 일시정지 동안 멈추고, 시간 배율이 반영되고, 한 프레임씩 진행도 그대로 따릅니다. `NextFrame()`은 다음 프레임의 Update 뒤, `WaitUntil(조건)`은 매 프레임 Update 뒤에 검사. 어떤 기다림도 시작한 그 프레임 안에서는 끝나지 않습니다.
- **주인 = 시작한 Behaviour**: 이벤트 메서드나 그 이어하기 안에서 시작한 기다림·타이머·트윈은 그 Behaviour에 묶입니다(AsyncLocal로 `await` 너머까지 따라감). 그 Behaviour가 **꺼지거나(disable) 지워지면**(에디터에서 오브젝트 삭제, `Destroy`, 핫 리로드 전 OnDisable) 즉시 취소되고, `await`는 그 자리에서 `OperationCanceledException`을 던집니다 → `catch`·`finally`가 실행되고, 그 예외는 오류로 보고하지 않습니다. 꺼진 Behaviour가 새로 기다리면 바로 취소(무한 반복 방지). 플레이를 멈추면 남은 것은 모두 취소.
- **게임 스레드로 돌아옴**: 스크립팅 `SynchronizationContext`가 이어하기를 모았다가 다음 프레임(Update 직후)에 실행합니다. `await Task.Run(...)` 뒤의 코드도 게임 스레드에서 돌아 엔진 API를 그대로 쓸 수 있습니다. `async void`에서 빠져나온 예외는 프로세스를 끝내지 않고 로그에 남습니다.
- 프레임 순서: 동기화(Awake·OnEnable / OnDisable·OnDestroy) → Start·Update → 이어하기 → 타이머·기다리기·트윈(시작한 순서) → FixedUpdate → LateUpdate.
- 자동 검사 `BDG_AWAIT_TEST=1`(샘플 `AwaitProbe`) → `await test: pause=1 deleted=1 disabled=1 timed=1 self=1 errors=0`.
- 남은 것: C++20 `BTask`·Lua `wait`(같은 규칙), `WaitForEvent`(이벤트 시스템과 함께), `RunOnWorker`(4-6c).

## 언어 사이 상호작용

그래프·애니메이션 그래프를 포함해 언어끼리 부르는 규칙(노출 표시, 타입 매핑, 핸들, 프레임 순서)은 [interop](interop.md)에 정리했습니다.

## 주의할 점

- **디버깅:** 모든 언어를 **DAP**로 연결해 VS Code 하나에서 함께 디버깅. 에디터 안 그래프 디버거도 같은 프로토콜 — C#은 [csharp-debugging](../features/csharp-debugging.md)(4-5, 실행 중인 에디터에 attach)
- **문서:** 리플렉션 정의에서 언어별 API 문서와 예제를 자동 생성
- **AI 혼란 방지:** `scripting.primary`(기본 C#)를 두고, 에이전트는 그 언어로 새 스크립트를 만듦. 새 프로젝트는 C#과 그래프만 켜진 상태로 시작

## 도입 순서

| 단계 | 언어 |
|---|---|
| M1 | C API 설계, 리플렉션, 바인딩 생성기 뼈대, C++ 모듈 빌드 |
| M3 | Python (에이전트 CLI와 함께, 에디터 자동화) |
| M5 | C# (기본 언어) |
| M5.5 | Lua |
| M7.5 | 그래프 VM |
| 1.0 이후 | C++ 핫 리로드, 그래프 AOT, Python 게임 실행(데스크톱) |

---

## 멀티스레드 (2026-11-23)

원칙: **기본은 게임 스레드 하나로 안전하게, 필요할 때 쉽게 병렬로.** 엔진 내부(물리·애니메이션 평가·렌더 준비·에셋 로딩·셰이더 컴파일)는 잡 시스템(enkiTS) 위에서 이미 모든 코어를 씁니다.

### 기본: 게임 스레드
`Update`·이벤트·코루틴(`await Wait`)은 항상 게임 스레드. 잠금 없이 오브젝트를 다뤄도 안전합니다.

### 병렬 API (모든 언어 같은 이름)

| 방법 | C# | C++ | 용도 |
|---|---|---|---|
| 무거운 일 하나 넘기기 | `var p = await RunOnWorker(() => FindPath(a, b));` | `co_await RunOnWorker(...)` | 길찾기, 파일, 절차적 생성. 끝나면 게임 스레드로 복귀 |
| 반복 나누기 | `Jobs.ParallelFor(n, i => pos[i] += vel[i] * dt);` | `bdg::ParallelFor(n, ...)` | 수천 개 계산 |
| 컴포넌트 병렬 Update | `[Parallel] void Update(float dt)` | `BFUNCTION(Parallel)` | 같은 컴포넌트 수천 개 (총알, 군중) |
| 잡 의존성 | `var h = Jobs.Run(A); Jobs.Run(B, after: h);` | 같음 | 순서 있는 파이프라인 |

- **Graph**: 같은 그래프 인스턴스를 자동으로 묶어 병렬 실행(배치 실행), Run On Worker 노드.
- **Lua**: 워커마다 독립 Lua 상태, 데이터는 메시지로 전달.
- **Python**: 에디터 도구용 단일 스레드, 무거운 작업은 백그라운드 작업으로.

### 안전장치

| 장치 | 내용 |
|---|---|
| **스레드 검사** | 워커에서 게임 스레드 전용 API(오브젝트 생성·삭제, Transform 쓰기 등)를 호출하면 크래시 대신 "게임 스레드 전용 API를 워커에서 호출 — 파일·줄" 오류 |
| **읽기 전용 스냅숏** | 워커는 프레임 시작 때 복사된 데이터와 `[ThreadSafe]` 함수만 사용 (애니메이션 그래프와 같은 규칙) |
| **`[Parallel]` 검사** | 병렬 Update 안에서 자기 컴포넌트 밖에 쓰기 → C#·C++ 분석기 경고 + 실행 중 검사(Debug) |
| **순서 섞기 모드** | 디버그 옵션으로 병렬 실행 순서를 무작위로 바꿔 경쟁 조건을 드러냄. CI에서도 실행 |
| **프로파일러** | 스레드 타임라인(코어별), `bdg profile`이 "`[Parallel]`로 바꾸면 3.2 ms 절약" 같은 제안 |