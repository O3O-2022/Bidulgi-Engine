# 전체 구조

## 기술 스택

| 영역 | 선택 | 이유 |
|---|---|---|
| 코어 언어 | C++20 | 성능, 그래픽 생태계 |
| 빌드 | CMake + vcpkg | 표준, 의존성 관리 |
| 그래픽 | RHI 추상화 + OpenGL 4.5 (첫 백엔드) | [rendering-and-platforms](rendering-and-platforms.md) |
| 창/입력 | **SDL3** (GLFW에서 변경) | 한글 IME, 게임패드, 모바일 터치, 웹을 한 계층으로. 이후 SDL_GPU 백엔드로 연결 |
| 에디터 UI | **자체 UI 프레임워크** (Slate형, 게임 UI와 공용) | 디자인 시스템과 1:1, 접근성·IME·UI 애니메이션을 처음부터 |
| 접근성 | AccessKit (C 바인딩) | 스크린 리더, 키보드 탐색 |
| 디버거 연결 | DAP (Debug Adapter Protocol) | C#·Lua·그래프를 VS Code 하나에서 디버깅 |
| 그래프 에디터 | 자체 노드 그래프 위젯 (같은 UI 프레임워크 위) | 그래프·애니메이션·머티리얼 그래프 공통, 에디터와 같은 스타일 |
| ECS (내부) | EnTT | 겉은 GameObject, 속은 ECS |
| 수학 | glm | |
| 모델/텍스처 | cgltf, ufbx, stb_image | |
| 물리 | Jolt (3D), Box2D (2D) | |
| 오디오 | miniaudio | |
| UI 레이아웃 | Yoga (Flexbox) | [ui-widgets](../features/ui-widgets.md) |
| 데이터 | nlohmann/json + JSON Schema 검증기 | |
| 스크립팅 | 1.0: .NET 8 (hostfxr), 그래프 VM, Lua(Luau), CPython(AI·에디터·도구), C++ 모듈 · 이후: C++ 핫 리로드, 그래프 AOT | [scripting](scripting.md) |

## 계층 구조

```
┌──────────── Editor (자체 UI 패널, 워크스페이스) ────────────┐   ┌─ AI 에이전트 ─┐
│                                                         │   │ CLI / MCP     │
└──────────────────────────┬──────────────────────────────┘   └──────┬───────┘
                           ▼                                         ▼
                ┌──────────────────── Command Bus ────────────────────┐
                │ 모든 변경 = 명령 → Undo/Redo, 기록, 검증, 권한        │
                └──────────────────────────┬─────────────────────────┘
                                           ▼
┌──────────────────────────────── Engine Core (C++) ─────────────────────────────────┐
│ Scene/ECS │ Asset DB(GUID) │ Serializer │ Reflection │ Config │ Script Hosts       │
├───────────┴────────────────┴────────────┴────────────┴────────┴────────────────────┤
│ Renderer (Render Graph → RHI) │ Physics │ Audio │ Input │ UI │ Anim │ Job System   │
├────────────────────────────────────────────────────────────────────────────────────┤
│ Platform Layer (Windows / Linux / macOS / Android / iOS / Web / Console)          │
└────────────────────────────────────────────────────────────────────────────────────┘
                                           ▲
                        Player 빌드 (에디터 모듈 제외, 런타임만)
```

## 모듈 구조 (언리얼식)

- 엔진은 모듈 단위로 나뉩니다: `Core`, `RHI`, `RHI.GL`, `Renderer`, `Physics2D`, `Physics3D`, `Audio`, `UI`, `Anim`, `Script.CSharp`, `Script.Lua` …
- 각 모듈은 `module.json`에 의존성과 지원 플랫폼을 적습니다.
- 빌드 때 쓰지 않는 모듈은 빠집니다. 모바일과 웹 빌드를 작게 만드는 핵심입니다.
- 에디터 전용 모듈(`Editor.*`)은 Player 빌드에 절대 포함되지 않습니다.

## 사용자 모델 (유니티식)

- 사용자에게는 GameObject + Component로 보입니다.
- 내부적으로는 EnTT 엔티티와 컴포넌트 배열이라 캐시 효율이 좋습니다.
- 대량 처리가 필요한 C++ 시스템은 ECS 쿼리에 직접 접근할 수 있습니다.

## 관련 문서

- [명령 버스와 리플렉션](command-bus-and-reflection.md)
- [렌더링과 플랫폼](rendering-and-platforms.md)
- [스크립팅](scripting.md)
