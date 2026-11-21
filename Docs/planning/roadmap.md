# 로드맵

엔진 본체 개발 순서입니다. 에이전트 인터페이스(M3)를 렌더링보다 앞에 둔 이유는, 그것이 이 엔진의 정체성이고 일찍 만들어 둬야 이후 개발도 AI에게 맡길 수 있기 때문입니다.

| 단계 | 내용 | 완료 기준 |
|---|---|---|
| **M0** 기반 | CMake + vcpkg, **SDL3** 창·입력, OpenGL 4.5 컨텍스트, RHI 뼈대, 자체 UI 프레임워크 기초(부품 8개, 고정 레이아웃 — 도킹은 M1), 공통 텍스트 입력 위젯 | 빈 패널 5개가 도킹된 에디터 창 + **한글 입력(IME) 테스트 통과** |
| **M1** 씬 | GameObject/Component, 리플렉션, C API와 바인딩 생성기 뼈대, Hierarchy·Inspector, 명령 버스, Undo | 엔티티를 만들고 편집하고 되돌릴 수 있음 |
| **M2** 저장 | JSON 씬 직렬화(기본값 생략), 에셋 DB와 GUID(.meta), Project 패널 | 저장 후 다시 열면 그대로 복원 |
| **M3** 에이전트 | CLI: `outline`, `view`, `query`, `set`, `add`, `schema`, `validate` | AI가 CLI만으로 씬을 편집하고 검증 |
| **M4** 렌더링 | 2D(스프라이트, 배칭) → 3D(메시, 머티리얼), 카메라, 기즈모, `bdg render` | glTF 모델과 스프라이트를 배치하고 스크린샷 |
| **M5** 스크립팅 | .NET 8 호스팅(C#), MonoBehaviour 생명주기, 핫 리로드, Play 모드 | C# 스크립트로 캐릭터 이동 |
| **M5.5** 스크립팅 확장 | Lua, Python 임베딩 | Lua 스크립트, Python 에디터 자동화 |
| **M6** 게임 기능 | 프리팹/오버라이드, Box2D + Jolt 물리, 오디오, 입력 액션, MCP 서버 | 라이브 에디터를 AI가 조작 |
| **M7** UI·애니메이션 | 위젯 시스템(Screen/Camera/World), UI Designer, 타임라인, 애니메이션 그래프(State Machine, Blend Space, Montage) | 애니메이션되는 HUD, 걷기·뛰기·점프가 섞이는 캐릭터 |
| **M7.5** 그래프 | 그래프 텍스트 형식, 레지스터 VM, 그래프 에디터, 디버거 → 이후 AOT C++ 생성 | 그래프만으로 적 AI 동작 |
| **M8** 1.0 마무리 | 라이트맵·라이트 프로브 베이크, 그림자, 플레이어 빌드(Windows·Android·Web), git 패키지, 템플릿, 유니티 에셋 임포터 | 작은 데모 게임을 Windows·Android·Web으로 빌드 |

## 1.0 범위 (2026-11-20 확정)

설계는 전체 범위로 하되, 1.0에 구현하는 것은 아래로 제한합니다 → 근거: [risk-response](risk-response.md)

| 항목 | 1.0 | 1.0 이후 |
|---|---|---|
| 스크립팅 | C#, 그래프(VM), Lua, C++ 모듈, Python(AI·에디터·도구) | C++ 핫 리로드, 그래프 AOT, Python 게임 실행(데스크톱) |
| 그래픽 | OpenGL 4.5, GLES 3.0 / WebGL2 | SDL_GPU(Vulkan·Metal·D3D12), 콘솔 |
| 플랫폼 | Windows, Android, Web | Linux, macOS·iOS, 콘솔 |
| 애니메이션 | 클립, State Machine, Blend Space, Montage, Notify, 타임라인 | IK 전체, 리타게팅, Graph Editor, 컷신 Sequencer |


## 이후 (1.0 이후 후보)

- **대형 게임 트랙** (오픈월드, 대형 멀티플레이어 FPS) → [large-game-track](large-game-track.md)

  | 단계 | 내용 |
  |---|---|
  | L1 | SDL_GPU 렌더러(Vulkan·D3D12·Metal), 멀티스레드, GPU 컬링 |
  | L2 | 월드 스트리밍, 64비트 좌표, LOD·HLOD, 터레인·식생 |
  | L3 | 실시간 GI, 고급 그림자, 볼류메트릭 |
  | L4 | 멀티플레이어 (서버 권한, 예측·보정, 전용 서버) |
  | L5 | 정밀 전투 (탄도, 관통, 부위 판정, 1인칭 무기) |
  | L6 | 모션 매칭, 전신 IK |
  | L7 | 인벤토리 프레임워크, 서버 영속화, 안티치트 연동, 패치 |

- 그 밖에: VFX, 내비메시, 모드 지원

## UI 시안 진행

- v0.1 (완료): Level 에디터 시안 — `prototype/forge-editor.html`
- v0.2 (계획): 워크스페이스 탭, 프로젝트 설정, UI Designer, 타임라인 — [mockup-v0.2.md](mockup-v0.2.md)
