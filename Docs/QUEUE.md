# 작업 대기열

**만들 순서대로** 적은 목록입니다. 새 대화(사람이든 AI든, 다른 계정이든)는 **이 파일 하나만 읽고** 작업을 이어 갈 수 있게 썼습니다.
맨 위의 `[ ]` 항목 하나를 집어 진행하세요.

---

## 0. 새 작업자 시작 안내 (먼저 읽기)

### 프로젝트 한 줄 요약
AI 친화적이고 텍스트 기반인 범용 게임 엔진. 겉은 유니티, 속은 언리얼, 도구는 블렌더식. C++20, MIT 라이선스.
기획 전체는 [README](README.md)에서 시작합니다. 확정된 결정은 [decisions](planning/decisions.md)에 있습니다.

### 빌드 (Windows)
필요한 것: Visual Studio 2022 (C++ 데스크톱 워크로드), CMake 3.24 이상, Python 3, git, 인터넷(첫 구성 때 SDL3·FreeType·yyjson을 받음).

```powershell
# CMake가 없으면 둘 중 하나: VS Installer에서 "C++용 CMake 도구" 추가, 또는
python -m pip install --user cmake
# 구성 + 빌드 (저장소 루트에서)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target BidulgiEditor
# 실행: 빌드가 끝나면 Bin/BidulgiEditor.exe로 자동 복사됨
.\Bin\BidulgiEditor.exe
```
- 경고 0개를 유지합니다 (`/W4`). 외부 라이브러리 경고는 해당 타깃에서만 끕니다.
- glad 로더를 다시 만들 때: `python -m pip install --user glad2` 후 `ThirdParty/glad/README.md`의 명령.

### 검증 방법 (자동 테스트용 환경 변수)
에디터는 창 앱이라 콘솔이 없습니다. 로그는 **실행 폴더의 `Bidulgi.log`** 에 남습니다.

| 변수 / 인자 | 용도 |
|---|---|
| `--frames N` | N프레임 뒤 자동 종료 (모든 자동 테스트의 기본) |
| `BDG_SCREENSHOT=경로.png` | 마지막 프레임을 PNG로 저장 → 이미지로 직접 확인 |
| `BDG_DUMP_WIDGETS=경로.jsonl` | 위젯 상자(id, kind, x, y, w, h) 덤프 |
| `BDG_SCREEN=gallery` | 부품 갤러리 화면으로 시작 (실행 중에는 F1) |
| `BDG_UI_SELFTEST=1` | 갤러리에서 클릭·Tab·한글 입력을 자동 주입하고 결과를 `ui:` 줄로 기록 |
| `BDG_BATCH_STRESS=1` | 사각형 1만 개 부하 테스트 |
| `BDG_GL_ERROR_TEST=1` | 일부러 GL 오류를 내서 디버그 출력 경로 확인 (Debug 빌드) |
| `BDG_UI_SCALE=1.5` | 화면 배율 강제 |
| `BDG_FONT`, `BDG_TOKENS` | 글꼴·토큰 파일 경로 강제 |

PowerShell 예:
```powershell
$env:BDG_SCREEN='gallery'; $env:BDG_UI_SELFTEST='1'
Start-Process .\build\Debug\BidulgiEditor.exe -ArgumentList '--frames','20' -Wait
Get-Content .\Bidulgi.log | Select-String "ui:|error|warn"
# 기대: ui: clicks(Apply)=1 focused=field.speed ... field.speed="한글 입력" composing="ㅋ"
```

### 코드 지도

| 경로 | 내용 |
|---|---|
| `Engine/include/bdg/bdg_api.h` | **C API 허브.** 모든 언어 바인딩은 이것만 봄 |
| `Engine/Runtime/Engine.cpp` | C API 구현, 메인 루프, 자동 테스트 주입 |
| `Engine/Runtime/Screens.cpp` | 에디터 화면·갤러리 화면 구성 (에디터 모듈이 생기면 옮김) |
| `Engine/Platform/Window.*` | SDL3 창, GL 컨텍스트, 입력 이벤트, IME, 클립보드, 화면 배율 |
| `Engine/RHI/RHI.h`, `RHI_GL.cpp` | 그래픽 추상화. 버퍼·텍스처·파이프라인·그리기 (GL 4.5 DSA) |
| `Engine/Render2D/Batch2D.*` | 2D 일괄 그리기 (SDF 둥근 사각형, 선, SDF 글자) |
| `Engine/Text/Font.*` | FreeType SDF 글자 아틀라스 |
| `Engine/UI/Theme.*` | `design-system/project/tokens.json` 읽기, 실시간 다시 읽기 |
| `Engine/UI/UI.*` | 유지형 위젯 트리, 플렉스 레이아웃, 입력·포커스, 부품 8개, 텍스트 입력 |
| `Engine/Core/Log.*`, `Image.*` | 텍스트 로그, PNG 저장 |
| `Editor/main.cpp` | 에디터 실행 파일 진입점 (C API만 사용) |
| `ThirdParty/glad` | 생성된 GL 로더 (커밋됨) |

### 작업 규칙
- 상태: `[ ]` 대기 · `[~]` 진행 중 · `[x]` 완료 · `[-]` 보류. 한 번에 **하나만** `[~]`.
- **완료 조건을 실제로 확인한 뒤에만** `[x]`. 확인 방법(로그 줄, 스크린샷)을 결과에 적습니다.
- 항목 하나 = 커밋 하나 이상. 커밋 메시지는 영어 요약 + 본문. 커밋 후 이 파일의 해당 줄을 갱신합니다.
- 순서를 바꾸거나 항목을 추가하면 [decisions](planning/decisions.md)에 이유를 한 줄 남깁니다.
- 외부 라이브러리는 MIT 호환 라이선스만 (MIT, BSD, zlib, Apache-2.0, ISC, FTL, 글꼴은 OFL). GPL·LGPL 금지.
- 외부 라이브러리 타입을 `bdg_api.h`나 엔진 공개 헤더에 노출하지 않습니다.
- 확정된 규칙: 생명주기 이름은 유니티식(`Awake` `Start` `Update(dt)` …), 좌표는 Y-up·1단위=1m(제안, M2 전 확정), 입력은 입력 액션만, 게임 구조는 컴포넌트 방식.
- 사용자는 한국어로 소통합니다. 문서는 한국어, 코드 주석과 커밋은 영어.

---

## 완료된 것 (요약)

| 항목 | 결과 | 커밋 |
|---|---|---|
| 기획 문서 | `Docs/` 전체, 디자인 시스템, HTML 시안 | `45f69ad` |
| 최소 베이스 | CMake + SDL3 + OpenGL 4.5 + C API + 로그 | `45f69ad` |
| 0-1 git | 저장소, `.gitattributes` | `45f69ad` |
| 0-2 glad | GL 4.5 로더 + KHR_debug 오류 로그 | `4227e99` |
| 0-3 RHI | 버퍼·파이프라인·Draw/DrawIndexed, 삼각형 | `2378497` |
| 0-4 Batch2D | SDF 둥근 사각형·테두리·선, 1만 개 = 드로우콜 1 | `086a924` |
| 0-5 글자 | FreeType SDF 아틀라스, 13~96px 선명 | `86ba51e` |
| 0-6 토큰 | `tokens.json` 읽기 + 실시간 반영 | `6ca57bb` |
| 0-7 UI 기초 | 위젯 트리·플렉스·포커스·부품 8개·덤프 | `9e28e27` |
| 0-8 텍스트 입력 | 커서·선택·클립보드·Undo·한글 IME (사용자 수동 확인) | `3b15f46` |
| 0-9 에디터 화면 | 위젯으로 만든 고정 레이아웃 에디터 | `79f9292` |
| 0-11 성능 기준 | 시작 585ms, 프레임 1.46ms (Release, RTX 5070 Ti) | `fb35682` |
| 고해상도 | Windows 화면 배율 자동 적용 | `2cc724f` |
| 에디터 2차 디자인 | 로고·Obsidian Dark 테마·컴포넌트 카드(접기)·타입 아이콘·하단 도크(Project/Console/Build Log/Profiler)·Consolas·상태 줄 FPS/메모리·exe 아이콘 | (이 커밋) |

---

## M0 — 남은 것

- [ ] **0-10. 디자인 일치 검사 1차** ([design-parity-test](planning/design-parity-test.md) 1~2단계)
  - 구현:
    1. `tools/ui_compare/`에 Python 스크립트 `compare.py`를 만든다. 입력: 브라우저 쪽 JSON, 엔진 쪽 JSONL(`BDG_DUMP_WIDGETS`).
    2. 브라우저 쪽: `design-system/project/components/*/preview.html`을 헤드리스 Edge(`msedge --headless --dump-dom`은 상자를 못 주므로) 대신 **Playwright(Apache-2.0)** 로 열어 `getBoundingClientRect()`로 `[data-bdg-id]` 요소 상자를 JSON으로 저장한다. preview.html 요소에 `data-bdg-id` 속성을 붙인다.
    3. 엔진 쪽: 같은 id를 가진 갤러리 위젯을 만든다 (`Screens.cpp`의 `BuildGalleryScreen`에 부품별 미리보기와 같은 크기의 고정 레이아웃 추가). 창 크기를 브라우저와 같게 맞추는 `--size 800x600` 인자를 `Editor/main.cpp`에 추가.
    4. 토큰 비교: 엔진에 `--dump-tokens 경로` 추가 (Theme의 모든 값을 JSON으로) → `tokens.json`과 값 비교.
    5. 보고서는 텍스트: `tokens ✓ 64/64`, `layout max error 0.6px` 형식. 기준 초과 시 종료 코드 1.
  - 완료 조건: 부품 8개에 대해 토큰 전부 일치, 레이아웃 오차 1px 이하 보고서가 나옴.
- [ ] **0-13. 대기 중 다시 그리기 멈추기 (최우선 최적화)** — 지금 수직 동기화가 안 걸려 대기 중에도 초당 수천 프레임을 그림(상태 줄 4000+ fps). `SDL_GL_SetSwapInterval` 결과 확인(실패 시 적응형 -1 → 1 → 프레임 제한), 입력·애니메이션·파일 변경이 없으면 `SDL_WaitEventTimeout`으로 잠들기. 완료 조건: 대기 중 CPU·GPU 사용률이 거의 0, 입력 반응 지연 없음
- [ ] **0-12. M0 마무리 손질**
  - Pretendard(OFL) `Assets/Fonts/Pretendard-Regular.otf`, JetBrains Mono(OFL) 추가 → `FindFont`가 우선 사용. 라이선스 파일 함께 커밋. (파일 내려받기는 사용자 허락 후)
  - Inspector 컴포넌트 헤더(`ComponentHeader`)에 `raise` 배경 + 위쪽 `line` 1px (`bundle.css`의 `.bdg-comp-head`).
  - Node 실행 핀(`exec`)을 원 대신 삼각형으로 (Batch2D에 `Triangle` 추가: 정점 3개짜리 쿼드 대신 SDF 삼각형 모드 추가).
  - `shadow-node` 토큰 문자열을 파싱해 그림자 오프셋·흐림을 반영 (Batch2D 사각형에 흐림 반경 인자 추가: SDF 거리로 부드러운 가장자리).
  - 완료 조건: 갤러리 스크린샷에서 세 가지가 보임, 경고 0개.

---

## M1 — 에디터 골격 (실제로 편집되는 에디터)

> 순서가 중요합니다: 1-2(리플렉션) → 1-3(명령 버스) → 1-4(핸들)는 이후 모든 기능(씬, 스크립팅, 네트워크, 공동 편집)의 바탕입니다.

- [ ] **1-1. 도킹·탭·패널 크기 조절**
  - 구현: `Engine/UI/Dock.*`. 도킹 트리 = 노드(가로/세로 분할 + 비율) 또는 잎(탭 묶음: 패널 id 목록 + 선택 탭). 분할 경계선 드래그로 비율 변경. 탭을 끌면 미리보기 영역(위·아래·좌·우·가운데) 강조 → 놓으면 트리 수정. 패널 내용은 기존 Widget 트리를 id로 연결.
  - 레이아웃 저장: `ProjectSettings/EditorLayout.jsonl`(사용자별은 `.cache/`), 형식은 한 줄에 노드 하나 `{"id":"n1","split":"row","ratio":0.2,"a":"n2","b":"n3"}`, `{"id":"n2","tabs":["hierarchy"],"sel":0}`.
  - 입력: 마우스 캡처(드래그 중 창 밖으로 나가도 추적) → `Window`에 `SDL_CaptureMouse` 추가.
  - 완료 조건: 패널 크기 조절·탭 이동·재시작 후 레이아웃 유지. 자동 테스트: 드래그 이벤트 주입 후 덤프로 크기 변화 확인.
- [ ] **1-2. 리플렉션 레지스트리 + 헤더 도구**
  - 문서: [command-bus-and-reflection](architecture/command-bus-and-reflection.md), [code-syntax](features/code-syntax.md).
  - 구현:
    1. `Engine/Core/Reflection.h`: `TypeInfo{name, size, fields[], functions[], base}`, `FieldInfo{name, type, offset, flags(Edit/Save/ReadOnly/Replicated), meta(range, tooltip, category, showIf)}`. 전역 `Registry`에 이름으로 등록.
    2. 매크로: `BCLASS(...)`, `BSTRUCT`, `BPROPERTY(...)`, `BFUNCTION(...)`, `GENERATED_BODY()` — 컴파일러에는 빈 매크로(또는 friend 선언).
    3. 헤더 도구 `Tools/bht/`(C++ 또는 Python, libclang 쓰지 않음): 헤더를 토큰 단위로 읽어 매크로 뒤 선언을 파싱 → `<File>.gen.h/.gen.cpp`에 등록 코드 생성. CMake `add_custom_command`로 빌드 전 실행, 바뀐 헤더만.
    4. 기본 타입 지원: bool, int32, float, double, Vec2/3/4, Quat, Color, string, enum, 핸들, `std::vector<T>`.
    5. 값 읽기·쓰기를 문자열/JSON으로: `Registry::Get(obj, "path.to.field")`, `Set(...)` — 명령 버스와 `bdg set`의 바탕.
    6. **변경 추적 훅**: 필드 쓰기는 반드시 `Set`을 거쳐 "바뀐 필드" 표시를 남긴다 → 나중에 네트워크 복제·공동 편집이 같은 정보를 씀.
  - 완료 조건: 테스트 클래스(`BTestComponent`: 필드 6종 + 함수 1개)의 타입 정보가 자동 생성되고, 이름으로 값 읽기·쓰기가 되는 단위 테스트 통과 (doctest 추가).
- [ ] **1-3. 명령 버스 + Undo/Redo**
  - 구현: `Engine/Core/Command.*`. 명령 = `{name, target(오브젝트 id), args(JSON), inverse(JSON)}`. `CommandBus::Execute(cmd)` → 실행 + Undo 스택에 push. 기본 명령: `set`(필드 값), `add`/`del`(오브젝트·컴포넌트), `mv`(부모 변경), `rename`.
  - 모든 편집(UI·터미널·AI)은 명령만 사용. 연속 드래그는 하나로 합침(merge window 300ms).
  - **직렬화 가능**: 명령을 JSON 한 줄로 저장·재생 (`--replay 파일.jsonl`) → 테스트·네트워크·공동 편집에 그대로 사용.
  - Field 위젯의 `onCommit`을 `set` 명령으로 연결. Ctrl+Z/Y가 전역 Undo(필드 내부 Undo는 편집 중에만).
  - 완료 조건: Inspector에서 값 변경 → Ctrl+Z로 되돌아감 → Ctrl+Y로 다시. 명령 로그를 재생하면 같은 결과(자동 테스트).
- [ ] **1-4. 핸들 기반 오브젝트 수명**
  - 구현: `Handle<T>{index:24, generation:8}` 또는 64비트(`index32 + gen32`). 오브젝트 풀은 세대 번호 비교로 `IsValid()`. 파괴된 대상 접근은 언어와 무관하게 같은 오류 메시지.
  - **안정 ID**: 씬 파일·명령·네트워크용으로 핸들과 별개인 영구 ID(문자열 `"g1"` 또는 64비트 UUID)를 둔다. 핸들은 실행 중 전용.
  - 완료 조건: 생성·파괴·재사용 시 이전 핸들이 무효가 되는 단위 테스트.
- [ ] **1-5. 단축키 시스템 + 프리셋**
  - 구현: `Engine/UI/Shortcuts.*`. 동작 이름 ↔ 키 조합 매핑 파일 `ProjectSettings/Shortcuts.jsonl`, 프리셋 `unity`(기본)·`unreal`. 첫 실행 때 선택 화면. 충돌 검사.
  - 완료 조건: 프리셋 전환 시 같은 동작(예: 이동 기즈모)이 다른 키로 실행.
- [ ] **1-6. 접근성 기초**
  - 구현: AccessKit C 바인딩(MIT/Apache) FetchContent. 위젯 트리 → 접근성 트리(역할: button, tab, textbox, list item; 이름; 상태). 포커스 변경을 알림. Ctrl+F6 등으로 패널 간 이동.
  - 완료 조건: Windows 내레이터가 포커스된 버튼·탭·입력칸 이름을 읽음 (사용자 수동 확인).
- [ ] **1-7. 네트워크·공동 편집 대비 점검** (구현 기능은 나중, 구조만 지금)
  - 확인 목록: 영구 ID가 모든 오브젝트에 있음 / 모든 변경이 명령으로 직렬화됨 / 리플렉션이 바뀐 필드를 추적함 / 명령 적용이 결정적(같은 입력 → 같은 결과).
  - 완료 조건: 명령 로그를 다른 프로세스에서 재생해 씬 덤프가 같음을 비교하는 테스트.

---

## M2 — 씬과 에셋 (실제 데이터를 다룸)

- [ ] **2-0. 좌표·단위 확정** — Y-up, 1단위=1m, 오른손/왼손 결정 후 [decisions](planning/decisions.md)에 기록. 수학 라이브러리(GLM, MIT) 추가.
- [ ] **2-1. 씬 모델** — 오브젝트(영구 ID, 이름, 부모, Transform) + 컴포넌트(리플렉션 타입). 계층 변경은 명령.
- [ ] **2-2. JSONL 씬 저장/열기** — 형식은 [data-formats](features/data-formats.md): 머리 줄 `{"scene":..,"defaults":"1.0"}` + 한 줄에 오브젝트 하나, 기본값 생략, id 순 정렬(git diff 안정). yyjson으로 쓰기/읽기. 분할 저장(셀·오브젝트별)은 형식만 지원하고 UI는 나중.
  - 완료 조건: 저장 → 다시 열기 → 다시 저장 시 파일이 바이트 단위로 같음.
- [ ] **2-3. 에셋 데이터베이스** — 파일 감시, `.meta`(GUID, 임포트 설정, 출처·라이선스), GUID ↔ 경로 표. 이름 바꾸기·이동해도 참조 유지.
- [ ] **2-4. Hierarchy·Inspector 실제 동작** — Hierarchy는 씬 트리 표시·선택·드래그로 부모 변경·이름 바꾸기. Inspector는 리플렉션으로 필드 자동 생성(`[Edit]` 메타: Range는 슬라이더, ShowIf, Header, Button). 편집은 전부 명령.
- [ ] **2-5. Project 패널** — 실제 폴더 목록, 파일 종류 아이콘(시안: `prototype/asset-icons.html`), 썸네일 캐시 `.cache/thumbnails/`. **C++ `.h`+`.cpp` 묶음 표시**(타일은 시안 완료, 목록 보기 중첩·짝 열기·함께 이름 바꾸기는 여기서) — [editor-ui](features/editor-ui.md#c-헤더소스-묶음-표시-2026-11-22)
- [ ] **2-6. 새 프로젝트 기본 3D 씬** — Main Camera, Sun, Sky, World Settings, Ground ([projects-and-templates](features/projects-and-templates.md)).
- [ ] **2-7. CLI `bdg` 1차** — `bdg view/query/set/add/del/validate` 를 같은 명령 버스로 (에디터 없이 헤드리스 실행). AI가 쓰는 첫 경로.

## M3 — 3D 렌더링과 플레이 모드

- [ ] **3-0. 렌더링 기준판 1단계** — 단색 평면 + 방향광 + MetalRoughSpheres + 좌표·색 기준판(축, 글자 판, 18% 회색, 컬러 체커)으로 좌표계·와인딩(CW/CCW)·sRGB↔Linear 감마를 먼저 확정. `bdg testassets fetch`(라이선스 기록, `.cache/testassets/`) ([graphics-test-scenes](features/graphics-test-scenes.md))
- [ ] **3-1. 메시·머티리얼·카메라** — glTF 임포트(cgltf, MIT), meshoptimizer, PBR 기본 셰이더. 머티리얼은 언리얼식 `.material` + `.matinst`(파라미터만), 텍스처는 용도별 프리셋·`.meta`·BC7/BC5 쿠킹 ([materials-and-textures](features/materials-and-textures.md))
- [ ] **3-2. 씬 뷰 실제 3D** — Custom 위젯 안에 오프스크린 렌더 타깃 → 텍스처로 표시. 궤도·이동 카메라, 그리드, 선택 외곽선.
- [ ] **3-3. 기즈모** — 이동·회전·크기 (자체 구현), 명령으로 기록.
- [ ] **3-4. 조명·그림자** — 방향광 그림자 맵(CSM은 고사양), 점·스폿광, 하늘·환경광.
- [ ] **3-5. 품질 단계 감자~중사양** — [rendering-and-platforms](architecture/rendering-and-platforms.md)의 표대로 설정 묶음, 프로젝트 최소·최대 단계.
- [ ] **3-6. 플레이 모드** — 복사본 월드에서 실행, 정지 시 원래대로, "이 변경 유지" 선택 반영(명령으로).
- [ ] **3-7. 편집 중 플랫폼 예산 표시** — Draw·삼각형·텍스처·라이트 막대.
- [ ] **3-8. 렌더 기준 씬 + 디버그 도구** — `Tests/Scenes/RenderReference.scene`(재질 매트릭스·조명/그림자·투명·컬링·포스트·좌표·품질 비교 구역), Sponza, 카메라 북마크 1~9, 렌더 패스 뷰어(Albedo/Normal/Depth/Shadow 등), 와이어프레임·바운딩·LOD 오버레이, `bdg render passes/stats`
- [ ] **3-9. 스트레스·이미지 비교** — 메시 1만 개 인스턴싱, 4K 텍스처 다수로 드로우콜·VRAM 측정, `bdg render compare`(북마크별 FLIP 오차·차이 이미지). 1.0에서 회귀 CI로

## M4 — C# 스크립팅

- [ ] **4-1. .NET 8 호스팅** — hostfxr로 런타임 로드, C API(`bdg_api.h`)를 P/Invoke로 노출하는 생성 코드(리플렉션에서).
- [ ] **4-2. `Behaviour` 기본 클래스 + 생명주기** — `Awake/Start/Update/FixedUpdate/LateUpdate/OnEnable/OnDisable/OnDestroy`, 프레임 순서는 [interop](architecture/interop.md).
- [ ] **4-3. `[Edit]` 노출** — C# 리플렉션 → 엔진 리플렉션 등록 → Inspector 자동 표시 ([code-syntax](features/code-syntax.md) 속성 표).
- [ ] **4-4. 핫 리로드** — 수집 가능한 AssemblyLoadContext, 상태 직렬화 후 재적용. 목표: 변경 → 반영 2초.
- [ ] **4-5. 디버깅** — DAP 연결 문서화 (VS Code / Rider).

## M5 — Graph·Lua·Python

- [ ] **5-1. Graph 텍스트 형식 + VM** — [visual-scripting](features/visual-scripting.md), JSONL 저장, 식 노드(`"in":{"y":"speed*dt"}`).
- [ ] **5-2b. 머티리얼 그래프 에디터** — 그래프 에디터 공통 화면 재사용, 출력 노드·파라미터 노드·미리보기, 셰이더 캐시, 순열 수 표시
- [ ] **5-2. 그래프 에디터** — 노드 그래프 위젯(줌·팬·박스 선택·와이어·미니맵), 목표: 노드 5천 개 60fps.
- [ ] **5-3. Lua(Luau)** — 샌드박스, `Behaviour{}` 선언, 핫 리로드, 타입 정의 자동 생성.
- [ ] **5-4. Python 에디터 도구** — CPython 임베드, `@bdg.menu`, `@bdg.validator`, `bdg.undo()`가 명령 버스와 연결.

## M6 — 게임 기능

- [ ] **6-1. 프리팹·오버라이드** — `@g1/hpbar` 경로, 오버라이드 표시(파란 막대), 되돌리기.
- [ ] **6-2. 물리** — PHI 추상 계층 + Jolt(3D) + Box2D(2D) ([physics](architecture/physics.md)).
- [ ] **6-3. 오디오** — miniaudio, 3D 소리, 믹서.
- [ ] **6-4. 입력 액션** — [input](features/input.md): `Input/Actions.jsonl`, 매핑 컨텍스트, 패드.
- [ ] **6-5. MCP 서버 + AGENTS.md 자동 생성** — [agent-integration](features/agent-integration.md).

## M7 — 애니메이션·UI 디자이너·빌드

- [ ] **7-1. 애니메이션 그래프** — Graph 상위호환 ([animation](features/animation.md)): 포즈 그래프, 상태 머신, 노티파이, 워커 스레드 평가.
- [ ] **7-2. 타임라인 (블렌더식)** — 키프레임·커브 편집.
- [ ] **7-3. UI 위젯 디자이너** — 런타임 게임 UI(월드·카메라 공간) ([ui-widgets](features/ui-widgets.md)).
- [ ] **7-4. 빌드·패키징** — `bdg build`, 쿠킹, `.bpak`, `build-report.txt` ([libraries-and-packaging](architecture/libraries-and-packaging.md)).

## 1.0 준비

- [ ] **허브·시작 화면·엔진 버전 관리** ([projects-and-templates](features/projects-and-templates.md))
- [ ] **템플릿**: 빈, 기본 3D·2D, 1인칭 슈팅, 3인칭, 2D 플랫포머
- [ ] **Android·Web** (GLES 3.0 / WebGL2 백엔드, Emscripten)
- [ ] **Halo GI Tier 0** — 라이트맵 + 프로브 + 반사 프로브 ([global-illumination](features/global-illumination.md))
- [ ] **패키지 시스템** — `bdgpkg.json`, `Packages/manifest.json`·`lock.json`, git 출처
- [ ] **디자인 일치 검사 CI**, 성능 회귀 CI

## 1.0 이후 — 멀티플레이와 공동 편집 (로블록스 스튜디오식)

- [ ] **N-1. 전송 계층** — GameNetworkingSockets(BSD). 리슨 서버·전용 서버(`bdg build --server`, 렌더링 없음).
- [ ] **N-2. 복제·RPC** — `[Replicated]` 필드(리플렉션 변경 추적 사용), `[Server]`/`[Client]`/`[Multicast]` RPC, 관심 영역.
- [ ] **N-3. 캐릭터 이동 예측 + 서버 보정**, 슈팅용 지연 보상.
- [ ] **N-4. 멀티 클라이언트 Play** — 에디터에서 클라이언트 2~4개 동시 실행, `bdg net simulate --lag --loss`, `bdg net stats`.
- [ ] **N-5. "멀티플레이 기본" 프로젝트 옵션** — 스크립트 실행 위치 `[ServerOnly]`/`[ClientOnly]`.
- [ ] **C-1. 공동 편집 세션 서버** — `bdg collab host`. 명령 버스의 명령을 세션 서버로 중계해 모든 에디터에 같은 순서로 적용.
- [ ] **C-2. 충돌 처리** — 오브젝트·속성 단위 잠금 또는 나중 값 우선, 스크립트 파일은 편집 중 잠금.
- [ ] **C-3. 다른 사람 커서·선택·이름표 표시**, AI 에이전트도 참여자로 접속.
- [ ] **N-6. 롤백 넷코드**(결정론 Jolt), 대규모 동기화 — L 트랙.

## 보류

- [-] Graph AOT (1.0 이후, 차분 테스트가 생긴 뒤)
- [-] 대형 게임 트랙 L1~L7 ([large-game-track](planning/large-game-track.md)): 오픈월드, GPU 물리·복셀 파괴, Halo GI Tier 1~3
- [-] 원클릭 클라우드 호스팅 (엔진 범위 밖, 선택형 서비스로 나중에 검토)
