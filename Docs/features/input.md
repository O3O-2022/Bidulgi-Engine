# 입력

## 결정: 언리얼식 입력 액션 하나로 통일 (2026-11-21)

입력 방식을 둘 중 하나로 고르면서 **입력 액션**(언리얼 Enhanced Input 방식)을 택했습니다. 유니티식 "매 프레임 키 직접 확인" API(`Input.GetKey(KeyCode.Space)` 같은 것)는 게임 코드에 열지 않습니다.

단, 매 프레임 확인하는 쓰는 법 자체는 됩니다. **키가 아니라 액션 값을 읽습니다.**

```csharp
// 이벤트로 받기 (언리얼식)
jump.Started += OnJump;

// 매 프레임 읽기 (유니티식으로 쓰고 싶을 때) - 키가 아니라 액션
void Update() {
    Vector2 move = Input.Value<Vector2>("Move");
    if (Input.Pressed("Jump")) Jump();
}
```

### 왜 입력 액션인가

| 이유 | 설명 |
|---|---|
| 여러 기기 | 키보드, 패드, 터치, 웹을 코드 수정 없이 지원. 1.0 플랫폼(Windows, Android, Web)에 필수 |
| 키 재설정 | 사용자 키 변경이 매핑 파일만 바꾸면 끝남. 접근성 요구 사항 |
| AI 친화 | 조작이 코드에 흩어지지 않고 텍스트 매핑 파일 하나에 모임. AI가 "점프를 B 버튼에도"를 파일 한 줄로 고침 |
| 테스트 | `bdg record` / `bdg test`가 액션 단위로 입력을 기록·재생. 기기와 무관 |
| 언어 공통 | Graph, C#, C++, Lua 모두 같은 액션 이름으로 접근 |

## 구성 요소

| 요소 | 파일 | 내용 |
|---|---|---|
| 입력 액션 | `Input/Actions.jsonl` | 이름과 값 종류: 버튼(bool), 축 1D(float), 축 2D(Vector2), 3D |
| 매핑 컨텍스트 | `Input/Contexts/*.jsonl` | 액션 ↔ 키·버튼·터치 연결. 우선순위가 있고 겹쳐 켤 수 있음 (예: 걷기, 차량, UI) |
| 수정자 | 매핑 안 | 데드존, 반전, 축 바꾸기, 감도, 스무딩 |
| 트리거 | 매핑 안 | 누름, 뗌, 길게 누름, 탭, 연타, 조합키(코드) |

```jsonl
{"action":"Move","type":"vec2"}
{"action":"Jump","type":"button"}
```
```jsonl
{"context":"OnFoot","priority":0}
{"action":"Move","key":"WASD"}
{"action":"Move","key":"Gamepad.LeftStick","mods":[{"deadzone":0.15}]}
{"action":"Jump","key":"Space"}
{"action":"Jump","key":"Gamepad.South"}
{"action":"Jump","key":"Touch.Button:jump"}
```

## 이벤트 시점

- 입력은 프레임 순서의 첫 단계에서 한 번 모아 액션 값으로 바꿉니다 ([interop 프레임 순서](../architecture/interop.md)).
- 이벤트(Started, Triggered, Completed, Canceled)는 그 직후 게임 스레드에서 일괄 전달하고, 같은 프레임의 Update에서 읽는 값과 항상 일치합니다.
- Fixed Update에서는 이전 고정 스텝 이후 눌렸다 뗀 입력도 놓치지 않도록 "이번 스텝 중 눌림" 값을 따로 둡니다.

## 예외: 키 직접 읽기가 허용되는 곳

- 키 설정 화면 (다음 누른 키 받기): `InputRebind` API
- 에디터 도구와 엔진 디버그 창 (에디터 전용 API)
- 텍스트 입력 (IME 포함)은 입력 액션이 아니라 UI 프레임워크의 텍스트 입력으로 받습니다.

## 일정

- M6: 입력 액션, 매핑 컨텍스트, 키보드·마우스·패드
- 1.0: 터치 가상 버튼, 웹, 키 재설정 UI, 입력 기록·재생
