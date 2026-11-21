# 명령 버스와 리플렉션

이 두 가지가 엔진의 척추입니다. 에디터 UI, 텍스트 저장, 다국어 바인딩, AI 연동이 모두 여기서 나옵니다.

## 명령 버스 (Command Bus)

### 원칙

에디터에서 일어나는 **모든 변경은 명령**입니다. 직접 메모리를 고치는 코드 경로는 없습니다.

```
Inspector에서 값 수정  ─┐
Hierarchy에서 드래그    ─┤
터미널: bdg set ...    ─┼─▶ Command Bus ─▶ 검증 ─▶ 실행 ─▶ Undo 스택 / 기록 / 파일 저장
AI 에이전트 (MCP)       ─┘
```

### 명령이 하는 일

| 단계 | 내용 |
|---|---|
| 파싱 | 텍스트(`set g1 Health.hp=20`) 또는 구조화된 호출을 같은 내부 형식으로 |
| 검증 | 스키마로 경로와 값 타입 확인. 실패하면 "원인 + 고칠 방법" 한 줄 에러 |
| 권한 | 에이전트 명령이면 권한 설정 확인 (예: 삭제는 승인 필요) |
| 실행 | 씬/설정 변경 |
| 기록 | Undo 스택에 이전 상태 저장, 출처(UI / 터미널 / 에이전트) 기록 |
| 알림 | 에디터 패널과 파일 감시자에게 변경 알림 |

### 묶음과 되돌리기

- 에이전트가 한 번에 보낸 명령 여러 개는 **하나의 Undo 그룹**으로 묶입니다. Ctrl+Z 한 번에 전부 되돌아갑니다.
- 기즈모 드래그처럼 연속 변경은 드래그가 끝날 때 명령 하나로 기록됩니다.
- Play 모드 중 변경은 정지할 때 되돌아갑니다 (유니티와 동일).

### 기본 명령 목록

`set`, `add`, `dup`, `del`, `addcomp`, `rmcomp`, `sel`, `view`, `query`, `schema`, `config get|set|reset`, `undo`, `redo`
→ 상세: [agent-integration](../features/agent-integration.md)

## 리플렉션

### 원칙

컴포넌트를 **한 번 정의하면** 아래가 모두 자동으로 생깁니다.

| 자동 생성물 | 쓰는 곳 |
|---|---|
| Inspector UI | 에디터 |
| JSON 직렬화 (기본값 생략 포함) | 씬/프리팹 저장 |
| JSON Schema + 압축 시그니처 | 검증, AI 문서 (`bdg schema`) |
| C API 함수 | 다국어 바인딩의 기반 |
| C# / Lua / Python / 그래프 바인딩 | 스크립팅 |
| 애니메이션 가능 속성 목록 | 타임라인 |
| API 문서 | Docs |

### 정의 방식 (C++, 언리얼식 코드 생성)

```cpp
#include "Health.generated.h"

BCLASS(Component)
class Health : public Component {
    GENERATED_BODY()
public:
    BPROPERTY(Edit, Range = "0,10000") int hp = 100;
    BPROPERTY(Edit) int max = 100;
    BPROPERTY(Edit, Animatable) bool invincible = false;
};
```

- 헤더 툴(`bdg-header-tool`)이 빌드 전에 매크로를 읽어 `*.generated.h/.cpp`를 만듭니다.
- 다른 언어로 만든 스크립트 컴포넌트는 각 언어 호스트가 런타임에 같은 레지스트리로 등록합니다.

### 압축 시그니처 예시 (`bdg schema`)

```
Health     { hp:int=100, max:int=100, invincible:bool=false }
Light      { kind:Directional|Point|Spot=Point, color:rgb=#fff, intensity:float=1, range:float=10 }
Rigidbody  { mass:float=1, kinematic:bool=false, gravity:bool=true }
```

JSON Schema 원문보다 훨씬 짧아서 AI 컨텍스트에 통째로 넣을 수 있습니다.

## 변경 범위 미리보기

명령은 실행 전에 **무엇이 바뀌는지 계산**할 수 있습니다 (명령 버스가 모든 변경을 알고 있기 때문).

```
bdg set "Enemies/*" Health.max=150 --dry-run
→ 오브젝트 37개 변경 (scenes/forest_01/enemies.jsonl)
→ 프리팹 오버라이드 2개 새로 생김 (g1, g7은 프리팹 값과 달라짐)
→ 프리팹 원본·다른 씬 변경 없음
```

| 언제 보이나 | 동작 |
|---|---|
| 사람이 여러 오브젝트를 한 번에 바꿀 때 (예: 10개 이상, 설정으로 조정) | 실행 전 확인 창 |
| 에이전트 명령이 승인 정책에 걸릴 때 | 승인 화면에 같은 요약 + 오브젝트 목록 |
| 에셋 삭제·이동 | 이 에셋을 참조하는 씬·프리팹·그래프 목록 |
| `--dry-run` | 실행하지 않고 텍스트로만 출력 (AI 확인용) |

- 미리보기와 실제 실행은 같은 코드 경로를 써서, 미리보기 결과와 실행 결과가 어긋나지 않습니다.
