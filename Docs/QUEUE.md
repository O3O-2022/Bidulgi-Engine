# 작업 대기열

**만들 순서대로** 적은 목록입니다. 새 대화(사람이든 AI든, 다른 계정이든)는 **이 파일 하나만 읽고** 작업을 이어 갈 수 있게 썼습니다.
맨 위의 `[ ]` 항목 하나를 집어 진행하세요.

---

## 0. 새 작업자 시작 안내 (먼저 읽기)

### 프로젝트 한 줄 요약
AI 친화적이고 텍스트 기반인 범용 게임 엔진. 겉은 유니티, 속은 언리얼, 도구는 블렌더식. C++20, MIT 라이선스.
기획 전체는 [README](README.md)에서 시작합니다. 확정된 결정은 [decisions](planning/decisions.md)에 있습니다.

### 빌드 (Windows)
필요한 것: Visual Studio 2022 (C++ 데스크톱 워크로드), CMake 3.24 이상, Python 3, git, 인터넷(첫 구성 때 SDL3·FreeType·yyjson을 받음). C# 스크립팅(M4)에는 .NET 8 이상 SDK(`dotnet`) — 없으면 엔진은 빌드되고 C#만 빠짐.

```powershell
# CMake가 없으면 둘 중 하나: VS Installer에서 "C++용 CMake 도구" 추가, 또는
python -m pip install --user cmake
# 구성 + 빌드 (저장소 루트에서)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target BidulgiEditor
# 실행: 빌드가 끝나면 Bin/BidulgiEditor.exe로 자동 복사됨
.\Bin\BidulgiEditor.exe
```
- 경고 0개를 유지합니다 (`/W4`). 외부 라이브러리 경고는 해당 타깃에서만 끕니다. **CI(`.github/workflows/ci.yml`)가 매 푸시·PR마다 빌드와 자동 테스트를 돌립니다** — 새 자동 테스트를 만들면 CI의 Smoke tests 단계에도 추가하세요.
- glad 로더를 다시 만들 때: `python -m pip install --user glad2` 후 `ThirdParty/glad/README.md`의 명령.

### 검증 방법 (자동 테스트용 환경 변수)
에디터는 창 앱이라 콘솔이 없습니다. 로그는 **실행 폴더의 `Bidulgi.log`** 에 남습니다.

| 변수 / 인자 | 용도 |
|---|---|
| `--frames N` | N프레임 뒤 자동 종료 (모든 자동 테스트의 기본) |
| `BDG_SCREENSHOT=경로.png` | 마지막 프레임을 PNG로 저장 → 이미지로 직접 확인 |
| `BDG_DUMP_WIDGETS=경로.jsonl` | 위젯 상자(id, kind, x, y, w, h) 덤프 |
| `BDG_SCREEN=gallery` | 부품 갤러리 화면으로 시작 (실행 중에는 F1) |
| `BDG_DOCK_TEST=1`, `BDG_LAYOUT=경로` | 도킹 자동 테스트(경계선 +100, Console 탭 이동), 레이아웃 파일 경로 강제 |
| `BDG_CMD_TEST=1`, `BDG_CMD_LOG=경로`, `BDG_SCENE_DUMP=경로`, `--replay 경로` | 명령 버스 자동 테스트(Inspector 입력 → Ctrl+Z → Ctrl+Y → 선택), 명령 로그·씬 덤프 저장, 로그 재생 |
| `BDG_CRASH_TEST=1` | 3번째 프레임에서 일부러 크래시 (보고서 확인용) |
| `BDG_FAULT_TEST=1` | 오류 주입: Scene 패널 그리기 크래시 + 일부러 실패하는 검사 → 에디터가 살아 있고 자동 저장 |
| `BDG_A11Y_TEST=1`, `BDG_A11Y=0` | 접근성 자동 테스트(UIA 클라이언트로 포커스 이름·역할 읽기 + Ctrl+F6), 접근성 연결 끄기 |
| `BDG_SHORTCUT_TEST=1`, `BDG_SHORTCUTS=경로` | 단축키 자동 테스트(첫 실행 선택 → Unity 키 → Unreal 프리셋 → 다른 키), 단축키 파일 경로 강제 |
| `BDG_DRAG_TEST=1` | Hierarchy 끌어다 놓기 자동 테스트(Weapon → Main, 월드 위치 유지) |
| `BDG_SCENE_FILE=경로`, `BDG_SAVE_TEST=1` | 씬 파일 경로 강제(없으면 `Scenes/Main.scene`, 그것도 없으면 샘플 씬), 2번째 프레임에 Ctrl+S |
| `BDG_ASSETS=폴더`, `BDG_ASSET_TEST=1` | 에셋 폴더(프로젝트 허브 전까지), 에셋 자동 테스트(에디터 안 이동 + 바깥 이동 → GUID·참조 유지) |
| `BDG_INSPECTOR_TEST=1` | Inspector 자동 테스트(슬라이더 끌기·Undo, F2 이름 바꾸기, 버튼·Undo, ShowIf) |
| `BDG_MULTI_TEST=1`, `BDG_PRESETS=폴더` | 여러 오브젝트 편집·값 복사/붙여넣기·프리셋 자동 테스트, 프리셋 폴더 강제 |
| `BDG_SCREEN=render-ref` | 렌더링 기준판(3D) 전체 화면 |
| `BDG_RENDER_TEST=1` | 기준판 렌더 결과 자동 판정(`render-ref`와 함께) |
| `BDG_QUALITY=potato\|low\|medium\|high\|ultra` | 시작 품질 단계(프로젝트 범위 안으로) |
| `BDG_QUALITY_SET=msaa=1,bloom=0,...` | 단계 위에 설정 덮어쓰기(msaa·bloom·fxaa·shadows·tonemap·lights·bias·exposure·shadowmap·view) |
| `BDG_SCREEN=render-stress` | 스트레스 화면(상자 `BDG_STRESS_INSTANCES`=10000, 4K 텍스처 `BDG_STRESS_TEXTURES`=16), 마지막에 `stress:` 로그 |
| `BDG_STRESS_TEST=1` | 인스턴싱 켬/끔, 컬링 켬/끔 비교(드로우·그린 수·같은 그림·CPU/GPU 시간) |
| `BDG_RENDER_COMPARE=<폴더>` | 북마크별 이미지 비교(`BDG_COMPARE_OUT`, `BDG_COMPARE_THRESHOLD`=0.03), `--frames 42` |
| `BDG_GI_READONLY=1` | 실행 빌드처럼 `.cache/gi`의 굽기 결과를 읽기만 하고 굽지 않음 |
| `BDG_GI_CACHE=0` | 보드 화면에서 GI 캐시를 쓰지 않음(항상 굽기) |
| `BDG_GI_DIAGNOSE=1` | `bdg gi diagnose`: 굽기·읽기 뒤 `gi diagnose:` 줄로 진단 |
| `BDG_RENDER_VIEW=0..7` | 보드 화면(render-ref/assets/stress/gi)의 디버그 보기, 7 = 간접광만 |
| `BDG_BLOCKOUT_TEST=1` | 3-3c Blockout 도구: 칩으로 만들기, 면 밀고 당기기(그리드·Esc·되돌리기), 모양 3개, 피킹 자동 테스트 |
| `BDG_ICONS=<폴더>` | 에디터 아이콘 PNG 폴더(기본 `Assets/Icons`; 빈 폴더면 예전 도형) |
| `BDG_SHOW_WINDOW=1` | 자동 실행(`--frames N`, 유휴 테스트)도 창을 보이게 함 — 기본은 숨긴 창 + 화면 밖 버퍼(테스트가 창을 띄우거나 포커스를 빼앗지 않음) |
| `BDG_KEEP_CWD=1` | 작업 폴더를 그대로 둠(기본: 엔진 데이터가 없는 폴더에서 시작하면 실행 파일 위의 엔진 폴더로 옮김) |
| `BDG_HIDDEN=1` | 어떤 실행이든 창을 숨기고 화면 밖 버퍼에 그림 |
| `BDG_BUDGET_TEST=1` | 3-7 예산 칩·플랫폼 배지·막대 값·예산 초과 경고·무거운 물체 선택 자동 테스트 |
| `BDG_REFS_TEST=1` | 2-5b 참조 보기·행 클릭 선택·안 쓰는 것·지우기 경고·지우기·참조 바꾸기(Undo)·일괄 이름 바꾸기 자동 테스트(`BDG_ASSETS` = CI가 만드는 작은 프로젝트) |
| `BDG_THEME_TEST=1` | 2-5c 테마 편집기 자동 테스트(`BDG_THEME_FILE` = 테마 파일 경로, 기본 `ProjectSettings/Theme.json`) |
| `BDG_IMPORT_TEST=1` | 2-5d 끌어다 놓기 임포트·이름 겹침·규칙·규칙 보기·씬 뷰에 놓기·.bin 변경 시 다시 가져오기·저장 경고 자동 테스트(`BDG_IMPORT_SRC` = 프로젝트 밖 원본 폴더) |
| `BDG_NEWSCENE_TEST=1` | 2-6 Ctrl+N 기본 3D 장면·카메라 방향·격자 바닥·포그·노출·저장 자동 테스트(`BDG_SCENE_FILE` 옆에 `New Scene.scene`을 씀) |
| `BDG_PREVIEW_TEST=1` | E-1 타일·썸네일(텍스처·모델·머티리얼)·렌더 캐시·Inspector 에셋 보기·미리보기 돌리기·물체로 돌아가기·목록 전환 자동 테스트 |
| `BDG_TREE_TEST=1` | E-2 계층 가이드 선·화살표 접기·고르면 펴기·Alt 가지 전체·저장 자동 테스트(`BDG_FOLDS` = 접힘 파일) |
| `BDG_SCENEICON_TEST=1` | E-3 컴포넌트 아이콘·그려짐·카메라 절두체·점광 범위·Icons 칩·아이콘 클릭 선택 자동 테스트 |
| `BDG_UIEDIT_TEST=1` | E-4a 숫자·벡터 수식, 오른쪽 클릭 메뉴(기본값·복사·붙여넣기·바깥 클릭 닫힘), UI 배율 Ctrl+휠·Ctrl+0·저장 자동 테스트(`BDG_PREFS` = 설정 파일) |
| `BDG_INSPECT2_TEST=1` | E-4b 라벨 끌기·벡터 축 끌기·축 상자 입력·색 선택기·에셋 놓기·계층 방향키 자동 테스트 |
| `BDG_FBX_TEST=1` | E-5 OBJ·FBX 썸네일·장면 배치·인스펙터 자동 테스트(`BDG_ASSETS` = CI가 만드는 OBJ+mtl+텍스처, cm 단위 ASCII FBX) |
| `BDG_POSTFX_TEST=1` | E-6a 포스트 프로세스 볼륨 비네트·그레인·색수차·노출 보정·자동 노출·상자 볼륨·삭제 자동 테스트(`BDG_QUALITY=high` 권장) |
| `BDG_POSTFX2_TEST=1` | E-6b 피사계 심도(전체 흐림·초점 선명)·렌즈 플레어·삭제 자동 테스트(`BDG_QUALITY=high`) |
| `BDG_SHAFTS_TEST=1` | E-7 빛줄기: 해 쪽 카메라·빛 더해짐·판자로 가림·등지면 꺼짐 자동 테스트(`BDG_QUALITY=high`) |
| `bdg.exe` (CI 스모크) | 2-7 한글 폴더에서 `new`·`view`·`set`·`add`·`validate` 종료 코드 확인, 프로젝트에 Bidulgi.log가 생기지 않음 |
| `BDG_SHAKE_TEST=1` | E-9 카메라 흔들림: 가까운·먼 소스, 게임 뷰가 흔들림, 끝남, Camera.Shake 노드, 정지 자동 테스트 |
| `BDG_CSHARP_TEST=1` | 4-1 C# 스크립팅: 에디터가 .NET을 띄우고 C#이 열린 장면의 Health.Damage 호출·Ratio 읽기·필드 쓰기, 명령 버스 Undo 3단계(source script)·되돌림 자동 테스트 |
| `BDG_BEHAVIOUR_TEST=1` + `BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll` | 4-2 C# Behaviour: 플레이 모드에서 Spinner가 물체를 돌림, 생명주기 순서, 정지 시 편집 월드 복원 → `behaviour test: types=4 instances=2 spun=1 order=1 restored=1 ended=1` |
| `BDG_EDITFIELD_TEST=1` + `BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll` | 4-3 C# `[Edit]` 필드: 기본값, Inspector 슬라이더·선택, 저장 값이 Awake 전에 들어감, 플레이 중 수정 전달, 스크립트가 바꾼 값이 돌아옴 → `edit field test: defaults=1 inspector=1 saved=1 push=1 pull=1 restored=1` |
| `BDG_HOTRELOAD_TEST=1` | 4-4 C# 핫 리로드: 플레이 중 샘플 사본 위에 v2 빌드를 덮어쓰면 감시가 다시 불러옴 — 값 유지, 새 필드, private 상태 유지, 새 코드 실행 → `hot reload test: reloaded=1 kept=1 newcode=1 state=1 stopped=1 ms=…` |
| `BDG_CHOICE_TEST=1` | UI 선택 위젯: Sun의 Light Shafts 스위치 클릭, Type 목록에서 Spot 고르기, Undo 한 번, 툴팁 예약 → `choice test: toggle=1 pick=1 undo=1 tooltip=1` |
| `BDG_QUIT_TEST=1` | 종료·자동 저장: 수정 → 제목 ● → 종료 요청 시 저장 목록(체크박스) → 취소 → 자동 저장 백업·복구 제안 → 저장 안 함으로 종료, 백업 삭제 → `quit test: dirty=1 ask=1 cancel=1 autosave=1 quit=1 cleaned=1` |
| `BDG_SETTINGS_TEST=1` + `BDG_PROJECT_SETTINGS=<새 파일>` | 프로젝트 설정: 기본값, 패널 필드 전부, 이름·버전 수정, 문제 목록, 물리 스텝 → 플레이, 모두 저장(바뀐 값만 파일에) → `project test: defaults=1 name=1 problems=1 physics=1 saved=1` |
| `BDG_SELECT=<id>` | 시작할 때 그 물체를 선택(Inspector 스크린샷용, `BDG_SCREENSHOT`과 함께) |
| `BDG_PLAY_TEST=1` | 3-6 플레이 모드: 실행·실행 중 되돌리기·정지 복원·유지 배너·게임 뷰 자동 테스트 |
| 3-6b 플레이 모드 도구 | 실행 중 **편집기 테두리**(실행 주황, 일시정지 노랑 — 지금 고친 값은 사라질 수 있다는 표시), **일시정지**(‖ 버튼·Ctrl+Shift+P, 실행 전에 누르면 멈춘 채로 시작, 버튼에 불), **한 프레임 진행**(▶| ·Ctrl+Alt+P: 정확히 1/60초 진행 후 멈춤), 툴바 **속도** 슬라이더(게임 시간 0.1x~4x, 범위 밖은 잘림). 인스펙터는 **실행 중에 바꾼 값**(되돌리지 않은 것)에 주황 점·글자로 표시 — 정지 때 유지 배너에 나오는 것과 같음. `BDG_PLAY_TEST` 판정 13개로 확장(일시정지 중 진행 없음·노란 테두리 픽셀·한 프레임 1/60초·배율 4x로 잘림·표시) | `13f5fad` |
| 3-7 플랫폼 예산 표시 | 새 `Runtime/Budget.*`(계산만, 단위 테스트): 플랫폼 4개(Windows·Android·iOS·Web)의 중사양 기준 예산(Draw·삼각형·텍스처 MB·라이트·그림자 라이트), 품질 단계에 따라 배율(감자 0.25x … 초고사양 3x, 라이트는 따로), `ProjectSettings/Budgets.json`으로 덮어쓰기(잘못된 플랫폼·키·값은 경고). 씬 뷰 **Budget 칩** → 왼쪽 아래 막대 4개(마지막 렌더의 실제 수치: 장면 드로우, 삼각형, 텍스처 메모리, 라이트·그림자), 80% 이상 노랑, 넘으면 경고색·⚠, **넘으면 칩도 경고색**(패널을 닫아도). **무거운 것** 상위 3개(삼각형 기준)를 눌러 바로 선택. 툴바 **플랫폼 배지**(Windows → Android → iOS → Web). 로그 `budget:` 한 줄(나중의 `bdg budget`). 씬 뷰 칩이 넓이를 넘으면 다음 줄로(Align 칩이 밖으로 밀리던 문제). 단위 테스트 1개(총 74개·865개), `BDG_BUDGET_TEST=1` 판정 7개, CI 추가 | `3b11fae` |
| 2-5b 참조·안 쓰는 에셋·찾아 바꾸기 | 새 `Assets/AssetRefs.*`(디스크 파일 + 열린 장면을 같은 방식으로, `bdg refs`·`bdg unused`(2-7)가 같은 답을 내도록): 에셋을 부르는 이름 4가지(프로젝트 경로·파일 이름·`guid:`/GUID·머티리얼은 파일 이름 앞부분)를 모두 참조로 셈, JSON 문서(.scene·.material·.matinst·.prefab·.json)는 문자열 값 전체 일치 + 위치(`g6 Player · MeshRenderer.mesh`, `BaseColorMap`), 스크립트는 따옴표 문자열 + 줄 번호. **안 쓰는 것** = 장면·스크립트·열린 문서에서 닿지 않는 에셋(장면 → 머티리얼 → 텍스처까지 따라감). Project 패널: **참조** 보기(행 클릭 → 열린 장면의 물체 선택), **안 쓰는 것** 보기, **지우기**(쓰이는 에셋은 먼저 경고, 한 번 더 누르면 '그래도 지우기', 휴지통으로 `.meta`와 함께), **다른 에셋으로 바꾸기**(열린 장면은 명령 묶음 = Undo 한 번, 다른 파일은 따옴표 안 정확한 문자열만 바꾸고 나머지 바이트 그대로). Inspector 다중 선택 이름 칸 = **일괄 이름 바꾸기** 패턴(`#`은 0을 채운 번호, `*`는 원래 이름, 명령 묶음). 단위 테스트 2개, `BDG_REFS_TEST=1` 판정 8개, CI 추가 | `65aa7a8` |
| 2-5c 테마 편집기 | `Theme`에 **프로젝트 테마**: `ProjectSettings/Theme.json`(`{"theme":..,"colors":{"accent":"#3b82f6"}}`)의 색이 tokens.json 위에 덮어씀(`C()`가 덮어쓴 값 우선, 기본값과 같으면 덮어쓰기 아님, 모두 지우면 파일 삭제, tokens.json 다시 읽어도 유지, 손으로 고쳐도 0.5초 안에 반영, 잘못된 항목은 건너뛰고 경고). Project Settings > **Theme**: 색 토큰 40개를 tokens.json 순서로(견본·이름·값, 바꾼 색은 점 + 밝은 글자), 클릭하면 아래에 **선택기**(값 칸 `#rgb/#rrggbb/#rrggbbaa/rgba()`, 색상·채도·밝기·불투명도 슬라이더 — 끄는 동안 매 단계 반영·저장), 바꾼 색만 **기본값** 버튼, 용도 메모, 위에 '테마 이름 · 바꾼 색 N개'와 **기본 테마로**. HSV 변환·색 표기 함수(`ParseColor`/`FormatColor`/`ToHSV`/`FromHSV`). 단위 테스트 2개, `BDG_THEME_TEST=1` 판정 7개(시작 적용·선택기·그려진 견본 픽셀·슬라이더·기본값·손 편집·초기화), CI 추가 | `5c014c0` |
| 2-5d 끌어다 놓기 임포트·자동 다시 가져오기·에셋 규칙 | **끌어다 놓기**: 탐색기에서 끌어 온 파일·폴더(SDL 드롭 이벤트 → `InputEvent::DropFile`)를 Project 패널의 현재 폴더로 복사(새 `Assets/AssetImport.*`): 이름이 겹치면 `rock 1.png`, **.gltf는 URI로 부르는 .bin·이미지를 하위 폴더째 함께**(같은 내용이면 공유, 다르면 덮어쓰지 않고 경고), 폴더는 통째로(`.meta`는 새로), 이미 프로젝트 안의 파일은 거부. 들여온 파일은 바로 선택, 규칙 검사 경고. **씬 뷰에 모델을 놓으면** 그 자리(표면, 없으면 바닥)에 MeshRenderer 물체를 만듦(Undo 한 번). **자동 다시 가져오기**: 감시자가 알린 변경(수정·추가·삭제)에 해당하는 모델, 또는 그 모델의 .bin·이미지가 바뀐 .gltf를 씬 뷰가 다시 가져옴(로그 '다시 가져오기: … (Box.bin changed)'; 텍스처·머티리얼·썸네일은 이미 내용 해시로 갱신). **에셋 규칙**: `<프로젝트>/ProjectSettings/AssetRules.json`(접두사 `{"texture":"T_",…}`, `maxTextureSize`(기본 8192, 헤더만 읽고 해시별 캐시), `requireLicense`(.meta의 `source.license`)), 검사할 때마다 다시 읽음 → 임포트·**장면 저장 때 경고**(저장은 막지 않음), Project 패널 **규칙** 보기(행 클릭 = 선택). 단위 테스트 1개, `BDG_IMPORT_TEST=1` 판정 7개, CI 추가. 함께 고침: 테마 파일이 다른 프로그램에 잠시 잡혀 읽기 실패하면 다음 확인 때 다시 읽음(전에는 수정 시각만 기억해 영영 놓침) | `00b4013` |
| 2-6 새 장면·새 프로젝트 기본 3D 씬 | 새 `Scene/DefaultScene.*`: 템플릿 **기본 3D**(Main Camera (0,1.5,-6)에서 원점을 봄 · Sun 방향광 45° 그림자 · Sky · World Settings · Ground 20×20 m 평면 + 내장 격자 머티리얼 `bdg:Grid`)과 **빈 장면**(Main Camera만), `WriteDefaultScene`(2-7 `bdg new`가 같은 함수로 `Scenes/Main.scene`을 씀). 새 **`WorldSettings` 컴포넌트**: 거리 **포그**(색·밀도·시작 거리, `mix(포그색, 표면, exp(-밀도×거리))`, 모든 조명 단계·안 비추는 재질에 적용, 프로브·라이트맵 굽기에는 안 씀)와 장면 **노출**(품질 단계 노출에 곱함). 에디터 **File > New Scene = Ctrl+N**(두 프리셋 모두): 열린 장면을 기본 3D로 바꾸고 기록을 새로 시작, 저장 경로는 지금 장면 옆 `New Scene.scene`(있으면 번호). 단위 테스트 1개, `BDG_NEWSCENE_TEST=1` 판정 6개(장면·카메라 방향·격자·포그 픽셀·노출 픽셀·저장), CI 추가. 허브·`bdg new`의 나머지(.bdgproject, git, AGENTS.md, 템플릿 선택 화면)는 기존 일정(M7~1.0) | `5a1ee5e` |
| E-1 미리보기·썸네일 | 새 `Runtime/AssetPreviews.*`: **Project 패널 타일 보기**(기본, '목록' 버튼으로 전환) — 64 px 썸네일 + 이름(…로 줄임), 패널 너비만큼 한 줄에, 썸네일이 없는 종류는 PhotoCraft 아이콘(폴더·C++·C#·장면·프리팹·오디오…). 썸네일: 이미지는 2-5t 캐시, **모델(glTF)·머티리얼은 오프스크린 렌더**(모델은 경계에 맞춰 위-앞에서, 머티리얼은 구) 128 px → 64 px로 줄여 `<프로젝트>/.cache/thumbnails/<GUID>-<해시>-r1.png`에 저장(다음 시작은 파일만 읽음), 프레임당 2개씩 만들어 큰 폴더도 멈추지 않음, 내용이 바뀌면 새로. **Inspector 에셋 보기**: Project 항목을 누르면 Inspector가 그 에셋(종류·경로·크기·GUID) + **미리보기 창**(모델·머티리얼은 패널 크기로 실시간 렌더, 끌어서 돌리기·휠 확대 / 이미지는 비율 유지 최대 512 px), Hierarchy·씬에서 물체를 고르면 물체로 돌아감. `BDG_PREVIEW_TEST=1` 판정 8개, CI 추가. 프리팹·장면 썸네일은 6-1(프리팹) 때 | `6cc76d8` |
| E-2 계층 트리 | Hierarchy 행이 트리로: 단계마다 **가이드 선**(위 단계에 형제가 더 있으면 세로선, 자기 가지는 ├ 또는 마지막이면 └), 자식이 있는 물체는 **▼/▶ 화살표** — 화살표를 누르면 접기·펴기(선택은 그대로), **Alt+클릭은 아래 전부** 함께(유니티), 씬 뷰 등에서 접힌 물체를 고르면 부모가 자동으로 펴짐. 접힘은 장면별로 `.cache/HierarchyFolds.jsonl`에 저장(자동 테스트 실행은 `BDG_FOLDS`를 줄 때만 읽고 씀 — 사용자 상태가 테스트를 바꾸지 않게). UI: `Widget::tree`·`fold`, `UIContext::ClickX/ClickAlt/OnTreeArrow`. `BDG_TREE_TEST=1` 판정 7개(그려진 선 픽셀 포함), CI 추가. 방향키로 접기·펴기는 E-4(키보드 탐색)와 함께 | `5425737` |
| E-3 씬 아이콘 | 표면이 없는 물체(빛·카메라·반사 프로브·라이트 프로브 볼륨·카메라 북마크)에 **컴포넌트 아이콘**(PhotoCraft 세트: comp-sun·comp-light·comp-camera·…, 빛 아이콘은 빛 색으로 물듦), 거리와 상관없이 같은 크기, 카메라가 빛보다 우선. **Icons 칩**: 끔 → S(16) → M(22, 기본) → L(30), 클릭 판정 반지름도 크기를 따름. **선택한 물체의 모양**(유니티 기즈모식): 카메라 = 시야 절두체(16:9, 먼 면은 25 m까지), 점광 = 범위 원 3개, 스폿광 = 범위까지 원뿔, 방향광 = 방향 화살. 에디터 카메라 뒤로 넘어가는 선은 근평면에서 잘라 그림. `BDG_SCENEICON_TEST=1` 판정 6개(아이콘을 끄면 픽셀이 바뀜 포함), CI 추가 | `7142a64` |
| E-4a 인스펙터·UI 편의 1 | **숫자 칸 수식**(새 `Core/Expr.*`: `+ - * / %`, 괄호, 음수, 지수 표기 — 숫자 칸과 벡터의 각 칸 `1+1, 10/4, -(1+2)`, 정수는 반올림, 틀린 식은 그대로 둬 경고). **오른쪽 클릭 메뉴**(UI 공통 팝업: `UIContext::OpenMenu`, 바깥 클릭·Esc로 닫힘, 위젯 `onContext`): 인스펙터 칸 = **기본값으로**(타입 기본값을 미리 보여 줌)·값 복사·값 붙여넣기. **UI 배율**: 70~200 %, Ctrl+휠(씬 뷰 위는 카메라 확대 그대로)·Ctrl+= / Ctrl+- / Ctrl+0(두 프리셋), Project Settings > Editor 'UI 배율 %' 슬라이더, 사용자별 `.cache/EditorPrefs.json`에 저장(자동 실행은 `BDG_PREFS`를 줄 때만). 키 `-`·`=` 추가(키패드 포함), 휠 이벤트에 Ctrl/Shift/Alt. 자동 실행(숨긴 창)은 **자체 클립보드**를 써서 테스트가 사용자 클립보드를 덮지 않음. 단위 테스트 1개, `BDG_UIEDIT_TEST=1` 판정 7개, CI 추가 | `e5fe621` |
| E-4b 인스펙터·UI 편의 2 | **라벨 끌기**(유니티): 숫자 칸 라벨을 좌우로 끌면 값이 바뀜(크기의 약 1 %/단위, 최소 0.01, 정수는 0.25/단위, Shift = 10배 세밀, 끄는 동안 라벨 강조, Undo 한 번). **벡터 X/Y/Z 상자**(언리얼·유니티식): Vec2/3/4·Quat 칸을 축 색 태그가 붙은 상자로 나눠 그림, 태그를 끌면 그 축만 바뀜, 상자를 누르면 그 숫자가 선택돼 바로 입력(칸 id·명령은 그대로라 기존 경로와 호환). **색 칸**: 끝에 견본 상자, 누르면 아래에 색상·채도·밝기·불투명도 선택기(무채색에서도 색상 유지). **에셋 끌어다 놓기**: Project 타일을 글자 칸(MeshRenderer.mesh 등)에 놓으면 그 경로가 들어감. **계층 방향키**(유니티): 마지막으로 Hierarchy를 눌렀을 때 ↑↓ 이전·다음 행, ← 접기(접혀 있으면 부모로), → 펴기(펴져 있으면 첫 자식으로). `BDG_INSPECT2_TEST=1` 판정 6개, CI 추가 | `f0bc88b` |
| E-5 FBX·OBJ 임포트 | **ufbx 0.23.1**(MIT/퍼블릭 도메인, `ThirdParty/ufbx`, 사용자 지시로 묻지 않고 설치) + 새 `Assets/ModelImport.*`: `ImportModel`이 확장자로 glTF(cgltf)·**FBX·OBJ(ufbx)**를 골라 같은 `ImportedModel`로. ufbx가 파일의 축·단위를 엔진 공간(오른손·Y 위·미터)으로 지오메트리에 구워 줌(cm FBX → m), 재질 파트마다 프리미티브 + 노드마다 인스턴스, 면은 삼각형으로, UV는 위아래 뒤집어 엔진 규칙에, 노멀 없으면 생성, meshoptimizer 공통 패스(`OptimizeMesh`). 재질: PBR 값 또는 FBX Phong(→ 금속 0, 거칠기 0.6)·불투명도·발광, 텍스처는 내장 바이트 → 모델 옆 파일 → 파일 이름만으로 찾음. 씬 뷰·썸네일·미리보기·인스펙터가 모두 FBX·OBJ를 받음, **OBJ는 .mtl과 map_* 텍스처**가 끌어다 놓을 때 함께 복사되고 바뀌면 다시 가져옴(`ModelDependencies`). 골격·애니메이션은 M7. **함께 고침**: 패널을 다시 만들 때 해제된 위젯 주소를 새 위젯이 재사용하면 키보드 포커스가 엉뚱한 칸으로 옮겨 가던 문제(포커스를 id로도 기억, 같은 id 위젯을 다시 찾음) — E-4b 방향키 테스트가 스모크에서 가끔 실패한 원인. 단위 테스트 1개(OBJ+mtl+텍스처, cm 단위 ASCII FBX, 의존 파일 복사), `BDG_FBX_TEST=1` 판정 4개, CI 추가 | `12487dd` |
| E-6a 포스트 프로세스 볼륨 | 새 **`PostProcessVolume` 컴포넌트**(언리얼식): 무한(장면 전체) 또는 물체 둘레 상자 + 바깥 섞임 거리, 섞임 가중치, 우선순위 — 새 `Render3D/PostVolume.*`(계산만, 단위 테스트)이 카메라 위치에서 낮은 우선순위부터 가중치×안쪽 정도로 섞음(씬 뷰·게임 뷰가 각자 카메라로). 효과(톤매핑 패스 안): **노출 보정**(스톱), **자동 노출(눈 적응)** — HDR 그림의 로그 휘도를 32×32로, 출력 타깃마다 1×1 두 장을 번갈아 써서 적응 속도대로 따라감, 평균을 중간 회색으로(최소·최대 EV 안), 보정은 그 위에 더함(언리얼과 같음); **블룸 세기**(품질 단계 값에 곱함), **비네트**(화면 비율과 상관없이 둥글게, 부드러움), **색수차**(가장자리로 갈수록 빨강·파랑을 벌림, 초록은 그대로), **필름 그레인**(프레임마다 바뀌는 잡음). 프로브·라이트맵 굽기와 디버그 보기에는 적용 안 함. 단위 테스트 1개, `BDG_POSTFX_TEST=1` 판정 7개(씬 뷰 픽셀: 모서리 어두워짐·잡음·초록 불변 색수차·밝아짐·포화에서 자동 노출로 내려옴·먼 상자 볼륨 무효·지우면 원래대로), CI 추가. 피사계 심도·렌즈 플레어는 E-6b | `a99aca7` |
| E-6b 피사계 심도·렌즈 플레어 | Post Process Volume에 **피사계 심도**(흐림 0~1, 초점 거리, 선명한 깊이 범위): 깊이·법선 사전 패스(SSAO와 같은 것, DOF가 켜지면 SSAO 없이도 돌림)의 시야 깊이로 착란원을 구해 황금각 원판 48샘플로 모음(표본의 착란원이 이 픽셀에 닿을 때만 섞어 선명한 물체가 흐린 배경에 번지지 않음), 해상도에 비례한 최대 반지름, 하늘은 먼 땅만큼 흐림. **렌즈 플레어**(언리얼식 이미지 기반): 블룸 체인에서 노출 후 흰색을 넘는 부분만, 중심을 지나 비친 고스트 4개(색을 조금씩 달리) + 후광 고리, 블룸이 있는 고사양 이상. 프로브·굽기·디버그 보기 제외. 씬 뷰에 `showGrid`(격자는 후처리 뒤에 그려짐). `BDG_POSTFX2_TEST=1` 판정 4개(라플라시안 선명도: 가까운 초점이면 전체가 흐려짐·플레이어에 초점을 맞추면 그 주변이 선명, 플레어가 밝힘, 지우면 원래대로), CI 추가 | `f60ef4d` |
| E-7 갓레이(빛줄기) | 방향광(해) Light 컴포넌트에 **Light Shafts** 켜기·세기(언리얼 Light Shaft Occlusion식): 해 방향을 화면에 투영(카메라 뒤면 끔, 화면 밖으로 나가면 서서히 사라짐), 절반 크기 패스가 각 픽셀에서 해 쪽으로 64걸음 행진하며 **깊이 사전 패스로 본 하늘**(물체가 없는 곳)이 해 가까이 있을수록 빛을 모아 감쇠 — 물체가 가리면 그 뒤로 빛줄기가 끊김. 톤매핑 전에 해 색 × 세기로 더함, 깊이 사전 패스는 SSAO·피사계 심도·빛줄기 중 하나만 켜져도 돌림, 렌더 통계에 `shafts`·해의 화면 위치. 픽셀 조명 단계(중사양 이상)만, 캡처·디버그 보기 제외. `BDG_SHAFTS_TEST=1` 판정 4개(해를 향하면 해가 화면에 있고 빛이 더해짐, 해를 판자로 가리면 1/4 아래로, 등지면 꺼짐), CI 추가. 고사양 볼류메트릭 포그 산란은 나중 검토 | `07e4790` |
| 2-7 CLI `bdg` 1차 | 새 **`bdg.exe`**(`tools/bdg`, 콘솔) — 엔진 경계 규칙대로 본체는 엔진 라이브러리의 `Engine/Tools/Cli.*`, 밖으로는 C API `bdg_cli_main` 하나. 에디터 없이(창 없음) 같은 **명령 버스**로: `new <dir> [--template basic3d|empty]`(2-6 템플릿으로 Scenes/Main.scene·Assets·ProjectSettings·.gitignore), `view`(압축 표기: 한 줄에 물체 하나, 바뀐 필드만, `#이름`, `| 컴포넌트 필드=값`), `outline`, `query <패턴> -f pos,Health.hp`(id·이름·경로 glob, 탭 표), `schema [Type]`(필드:타입=기본값), `set <대상> 경로=값 …`(pos·rot·scale·name 별칭, 6,0,4 → 배열, 대상 glob, `--dry-run`은 바뀔 줄을 -/+로만), `add <id> [--name --parent --pos --comp …]`, `del`, `addcomp`, `rmcomp` — 한 명령 = 묶음 하나, 저장은 바이트 안정, 명령은 `.cache/bdg-commands.jsonl`에 쌓임. `validate`(장면 경고, MeshRenderer 메시·머티리얼 참조, 짝 잃은 .meta, 에셋 규칙 → `파일:줄 경로 → 원인 → 고칠 방법`, 오류 있으면 종료 코드 1), `refs`·`unused`(2-5b 색인 + 프로젝트의 장면 파일들). 공통 `--project`·`--scene`. CLI 모드 로그는 경고 이상만 stderr로(표준 출력은 결과만, 프로젝트에 Bidulgi.log 안 씀), 인자·출력 UTF-8. **함께 고침**: ① 장면·명령 로그·에셋 캐시·레이아웃·단축키·테마·GI 캐시 파일이 UTF-8 경로를 `fopen`에 그대로 넘겨 **한글 폴더에서 깨진 이름으로 저장**되던 문제(새 `Core/Utf8Path.*`: 올바른 UTF-8이면 UTF-8로, 아니면 시스템 코드페이지로 읽어 둘 다 동작) ② Assets 바로 아래 에셋의 참조가 경로·파일 이름이 같아 **두 번 세어지던** 문제(2-5b). 단위 테스트 4개(UTF-8 경로 저장·열기, 읽기 명령, 쓰기·dry-run·검증·로그, refs·unused), CI 빌드에 `bdg` + 한글 폴더 스모크. `bdg render/sim/budget/gi diagnose`(GPU 필요)와 `undo`·`dup`·`config`는 다음 차례 | `99d2236` |
| E-8 함수 리플렉션 → Graph 노드 | 새 `Scene/GraphLibrary.*`: 리플렉션 레지스트리의 컴포넌트에서 **Graph 노드 목록**을 만듦(언리얼 BlueprintCallable·Pure·ReadWrite식) — `BFUNCTION(Callable)` = exec 핀이 있는 호출 노드, 새 **`BFUNCTION(Pure)`**(const, 값만) = exec 없는 값 노드, `BPROPERTY(Edit)` = Get·Set 노드(ReadOnly는 Get만), 부모 타입 멤버 포함, 핀 타입은 스키마와 같은 이름(int·float·vec3·열거형·구조체), id 정렬·검색. `RunGraphNode`: Call·Set은 명령 버스(`call`·`set`, Undo 한 번), Pure·Get은 읽기만, 인자 개수 검사. 명령 버스에 **`LastResult()`**(call의 반환값 JSON). 헤더 도구가 모르는 BFUNCTION 플래그를 오류로. 샘플 Health에 `Damage(int) -> int`(Callable)·`Ratio()`(Pure). CLI `bdg nodes [검색어]`·`bdg call <물체> <노드> [인자…]`. C#(`[Callable]`/`[Pure]`)·Lua(`bdg.export`)는 같은 레지스트리로 등록하는 규약만 문서화(`visual-scripting.md` 새 절) — 실제 연결은 M4·M5. 단위 테스트 3개 | `947ee13` |
| E-9 카메라 흔들림 | 언리얼 Camera Shake식: 새 `Scene/CameraShake.*`(계산만, 단위 테스트) — 흔들림 정의 = **Perlin(매끄러운 잡음) 또는 Sine 패턴**, 위치(카메라 오른쪽·위·앞, m)·회전(피치·요·롤, 도)·FOV 진폭, 위치·회전 주파수, 길이(0 = 멈출 때까지), 블렌드 인·아웃; 여러 흔들림이 더해짐, 같은 시드면 같은 흔들림, Stop은 블렌드 아웃. **`.camshake` 에셋**(JSON 한 줄) + 내장 `bdg:Light/Medium/Heavy/Explosion/Handheld`. **`Camera.Shake(이름, 배율)`**(Callable → Graph 노드·스크립트), 새 **`CameraShakeSource` 컴포넌트**(언리얼 CameraShakeSourceComponent: 안쪽 반지름까지 최대, 바깥 반지름에서 0, 감쇠 지수, 시작할 때 재생, Play 버튼). 플레이 모드가 게임 시간(배율·일시정지 반영)으로 돌리고 게임 뷰 카메라에 오프셋(축 이동·작은 회전·FOV)을 더함, 정지하면 0. 에셋은 경로·파일 이름·확장자 없는 이름으로 찾고 없으면 한 번만 경고. 단위 테스트 3개, `BDG_SHAKE_TEST=1` 판정 5개(가까운 소스만 흔듦·게임 뷰 그림이 프레임마다 바뀜·길이 뒤 끝남·Camera.Shake 노드·정지), CI 추가 | `19743ec` |
| 4-1 .NET 8 호스팅 | 새 `Engine/Scripting/`: **`DotNetHost`** — hostfxr(공식 네이티브 호스팅 API)를 `DOTNET_ROOT` 또는 `%ProgramFiles%\dotnet`의 가장 높은 `host/fxr/<버전>`에서 찾아 SDK 헤더 없이 함수 몇 개를 직접 선언, `Bidulgi.Scripting.runtimeconfig.json`으로 런타임 시작 → 어셈블리 로드 → `[UnmanagedCallersOnly]` 진입점을 C 함수 포인터로(런타임은 프로세스당 한 번, 다시 부르면 그대로). **요청 채널** `ScriptApi`: C#의 엔진 호출은 전부 JSON 요청 하나(`log`·`find`(id 또는 이름)·`name`·`rename`·`has`·`get`·`set`·`call`) → `{"ok":…,"value"/"error"}`, **set·call·rename은 명령 버스(source `script`)** 라 Undo·로그·리플레이가 에디터 편집과 같음, Pure 함수는 Undo 없이 읽기만, 틀린 호출은 오류 답(크래시 아님), 버퍼보다 긴 답은 같은 요청의 재시도에 돌려줌(편집이 두 번 실행되지 않게). **관리 코드** `Scripting/Bidulgi.Scripting`(net8.0, RollForward LatestMajor): `Engine.Request`, `Debug.Log/LogWarning/LogError`, `GameObject.Find/Name/GetComponent<T>`, `Component` 기반 클래스(Get/Set/Call, Vector2·3·4·Quaternion·열거형 변환). **바인딩 생성** `bdg csharp-bindings <파일>`: 리플렉션의 모든 컴포넌트 → C# 클래스(Edit 필드 = 속성, ReadOnly = 읽기 전용, Callable·Pure·Button = 메서드, 툴팁 = 문서 주석), 쓰인 열거형 → C# enum, 결과가 같으면 파일을 다시 쓰지 않음. CMake: `dotnet`이 있으면 `BidulgiScripting` 타깃이 bdg → Bindings.g.cs → `dotnet build` → `build/<구성>/Managed`(에디터와 함께 `Bin/Managed`로 복사), API가 안 바뀌면 다시 빌드 안 함, 없으면 경고만. 단위 테스트 3개(바인딩 내용·결정성, 요청 채널·Undo·오류, 실제 .NET에서 C# 셀프 테스트 — ASan 빌드는 제외), `BDG_CSHARP_TEST=1`, CI에 .NET SDK 설치 | (다음 커밋에 기록) |
| 4-2 Behaviour 생명주기 | C# `Behaviour` 기본 클래스(유니티식, 장면 파일에서 클래스 이름 `"Spinner":{}`로 붙임) + `Time`. 새 `Scripting/BehaviourRunner.*`가 플레이 모드 동안 인스턴스를 들고 [interop](architecture/interop.md) §5 순서로 호출: 동기화(Awake·OnEnable / OnDisable·OnDestroy·enabled 변화) → Start(첫 프레임) → Update → FixedUpdate(고정 1/60초, 프레임당 최대 4번) → LateUpdate. 스크립트 어셈블리는 `BDG_SCRIPT_ASSEMBLIES`로 수집 가능한 AssemblyLoadContext에 로드. 플레이 중 스크립트 변경은 Undo·로그를 남기지 않음(정지 때 버려지는 월드). 예외는 그 인스턴스만 로그 후 계속. 예제 `Scripting/Bidulgi.Samples`(Spinner·LifecycleProbe·Faulty·Medic). 단위 테스트 + CI 스모크 | `2e396a4` |
| UI 선택 위젯·툴팁 | 글자로 치던 값을 **고르기**로: 새 `WidgetKind::Choice` — **콤보박스**(▼, 현재 값 강조, ↑↓·Enter, 8개 넘으면 검색 칸), **세그먼트 버튼**(짧은 2~4개, 이름이 안 들어가면 자동으로 콤보), **토글 스위치**(bool, 여러 물체 값이 다르면 가운데). Inspector: enum → `MakeChoice`, bool → 스위치, `BPROPERTY Tooltip` → 마우스를 0.55초 올리면 **툴팁**. 씬 뷰 칩: View·Icons·도구·품질 단계는 클릭하면 목록(▼ 표시), Snap은 클릭 = 다음 단계, 오른쪽 클릭 = 목록. 툴바 품질 단계도 목록. 팝업 메뉴: 체크 표시, 키보드, 검색. 접근성: combobox/checkbox. `BDG_CHOICE_TEST` | `c5f8740` |
| 4-3 `[Edit]` 노출 | C# `[Edit]`(private 가능)·`[Range(min,max)]`·`[Tooltip]`·`[Header]`. 타입: bool·int·float·double·string·Vector2/3/4·enum. 등록 때 C#이 필드 목록·기본값(새 인스턴스 값)·범위·enum 이름을 보내고, 엔진은 그 클래스의 컴포넌트 타입에 실제 필드를 만듦(`ScriptComponentData`: 1KB 숫자 영역 + 문자열 16개, 클래스마다 기본값 템플릿 슬롯 256개) → Inspector·장면 저장·명령 버스·`bdg` CLI가 그대로 동작. 플레이: 저장 값을 **Awake 전에** C# 필드에 넣음(유니티 순서), Inspector 수정은 다음 프레임에 전달, LateUpdate 뒤 스크립트가 바꾼 값을 컴포넌트로 돌려받아 Inspector에 실시간 표시(Undo 없음). 스크립팅 API 버전 2. 예제 Spinner(speed·axis·label·frames). 클래스를 다시 불러올 때 필드 변경은 4-4 핫 리로드에서 | `521dcfa` |
| 4-4 핫 리로드 | 불러온 스크립트 `.dll`을 0.5초마다 확인(시간이 바뀌고 한 번 더 그대로면 = 빌드가 다 씀) → 다시 불러오기. C#: 그 어셈블리의 인스턴스가 **상태 저장**(private 포함, 단순 타입 필드 전부: 숫자·문자열·enum·벡터·배열/List) → OnDisable → 옛 `AssemblyLoadContext` 언로드 → 새로 로드 → 상태 복원 → OnEnable (Awake·Start는 다시 안 함, 유니티와 같음). 어셈블리는 **메모리로 읽어서** 로드(.pdb 포함) → 빌드가 실행 중에도 덮어쓸 수 있음. 엔진: 같은 이름 클래스가 다시 등록되면 `[Edit]` 필드를 제자리에서 다시 만들고 장면의 값을 이름으로 옮김(지운 필드는 버림, 새 필드는 C# 기본값). 다시 불러오기 30ms(RTX 5070 Ti 노트북). 사라진 클래스의 인스턴스는 경고 후 제거. 스크립팅 API 버전 3. 테스트용 v2 샘플(`Managed/HotReloadV2`). **남은 것**: 프로젝트 `.cs`를 엔진이 직접 컴파일(지금은 IDE·`dotnet build`가 `.cache/Scripts`에 쓰면 반영) | `fde25c3` |
| 4-4c 쉬운 API (C#) | `Behaviour` 안에서 한 줄: `Get`/`GetInChildren`, `[Auto]`(Awake 전), `Find`(없으면 경고 + `GameObject.None`, `if (target)` 검사)·`FindAll`·`FindNearest`, `Spawn`(오브젝트 복사 또는 빈 오브젝트, 명령 버스 `add` 한 번)·`Destroy`(바로/`after:`, 지운 오브젝트의 Behaviour는 즉시 OnDisable·OnDestroy), `After`/`Every`(→ `Timer.Cancel`), `await Wait`/`NextFrame`/`WaitUntil`, `transform.Tween(position/rotation/scale, ease, pingPong)`(await 가능, Ease 15종), `Random`(정수 범위 양 끝 포함, `Seed`, `PickRandom`, `Shuffle`). 밖에서는 `Game.*`. 타이머·트윈·기다리기는 시작한 Behaviour에 묶여 disable·destroy·Stop 때 취소, 게임 시간(일시정지·배율 반영). 비동기 이어하기는 게임 스레드로 돌아옴(스크립팅 SynchronizationContext, async void 예외는 로그). 새 요청 `children`·`findall`·`spawn`·`destroy`, 스크립팅 API 버전 4, 샘플 `EasyProbe`, 단위 테스트 + `BDG_EASYAPI_TEST`. **남은 것**: 태그·입력 액션·씬 상수 생성(엔진에 목록이 아직 없음), `Time.scale` 쓰기 [easy-api](features/easy-api.md#c-구현-현황-4-4c-2026-11-25) | (다음 커밋에 기록) |
| 4-6b 기다리기 (C# 부분) | Behaviour 메서드를 `async`로(이벤트도: `async void`/`async Task Start()`, Task 실패는 로그). `await Wait(초)`·`NextFrame()`·`WaitUntil(조건)`은 **게임 시간**(일시정지·배율·한 프레임 진행 반영, 시작 프레임 안에서는 안 끝남). 기다림·타이머·트윈은 시작한 Behaviour에 묶임(AsyncLocal로 await 너머까지) → **disable·삭제·`Destroy`·핫 리로드·Stop 때 즉시 취소**, await 자리에서 `OperationCanceledException`(catch·finally 실행, 오류로 보고 안 함), 꺼진 Behaviour의 새 기다림은 바로 취소. 이어하기는 스크립팅 SynchronizationContext로 게임 스레드(Update 직후)에서, `Task.Run` 뒤도 게임 스레드. 샘플 `AwaitProbe`, `BDG_AWAIT_TEST`(일시정지 중 진행 없음, 오브젝트 삭제·disable·자기 삭제 취소, 크래시·오류 0). **남은 것**: C++20 `BTask`·Lua `wait`, `WaitForEvent`, `RunOnWorker` — 그래서 4-6b 체크는 열어 둠 [scripting](architecture/scripting.md#기다리기--c-async-4-6b-2026-11-25) | (다음 커밋에 기록) |
| 4-4d 데이터 테이블 | `Data/<이름>.table.jsonl`(한 줄 한 행, 첫 줄에 선택적 설명 `{"$table":{"key","columns":[{name,type}]}}` — int·float·bool·string·json) 또는 `Data/<이름>.csv`(RFC 4180, 열 타입 짐작, 빈 셀 = 값 없음). 문제(키 없음·중복 키·타입 틀림·JSON 아님)는 줄 번호와 경고 후 그 행만 건너뜀. 새 `Engine/Scripting/DataTable.*`, 스크립트 요청 `table`(장면 없이도), **`bdg table import/export/check`**. C# `Data.Get<T>(key)`(없으면 경고 + 기본값)·`TryGet`·`All`·`Row`·`Keys`·`[Table("이름")]`(기본: 클래스 이름 + s), 플레이가 끝날 때·핫 리로드 때 다시 읽음. 샘플 `DataProbe`, 단위 테스트 + `BDG_DATATABLE_TEST`. **남은 것**: 에디터 스프레드시트 보기(공용 UI를 크게 고쳐야 해서 보류), 리플렉션 구조체로 행 구조 정하기 [data-tables](features/data-tables.md) | (다음 커밋에 기록) |
| 4-5 디버깅 (C#) | 실행 중인 에디터 프로세스에 .NET 디버거를 **붙이는(attach)** 방법 문서 — netcoredbg(DAP)·VS Code(coreclr attach)·Rider·Visual Studio. **확인함**: netcoredbg + DAP로 붙기 → 메모리로 로드한 스크립트의 .pdb 기호 → 중단점이 `Spinner.Update`에서 멈춤 → 스택·`this.speed` 읽기 → 계속·떼기(에디터는 계속). **Release(최적화) 스크립트는 중단점이 "확인됨"이어도 안 멈춤**(확인함) → 스크립트는 Debug로. `BDG_CSHARP_DEBUG_WAIT=<초>`: 스크립팅 시작 때 디버거를 기다리고 프로세스 id를 로그에(Awake·Start 디버깅). IDE 없는 AI 세션용 `Scripting/Tools/dap_attach.py`(DAP로 붙어 중단점·변수 확인, 종료 코드). VS Code·Rider·VS는 이 PC에 없어 확인 못 함 [csharp-debugging](features/csharp-debugging.md) | (다음 커밋에 기록) |
| 종료 확인·자동 저장·저장 목록 | **종료**(창 닫기·Alt+F4·Ctrl+Q): 저장 안 된 항목이 있으면 언리얼 "Save Content"식 **체크박스 목록**(장면, 프로젝트 설정 — 항목별로 저장/버림) + [선택 항목 저장 후 종료] [저장 안 함] [취소]. 없으면 "에디터를 끌까요?"(다시 묻지 않기 체크 = 설정 '종료할 때 확인' 끔). 플레이 중이면 먼저 정지. **모두 저장** Ctrl+Shift+S = 같은 목록. **변경 추적**: 명령 버스 `Revision()`(플레이 중 변경 제외) vs 저장 시점 → 제목 옆 ●. **자동 저장**: 설정 간격(기본 5분, 0=끔)마다 변경이 있으면 `.cache/autosave/<장면>.scene`에 백업(원본 파일은 저장할 때만 씀), 크래시 때도 백업. 다음 시작 때 백업이 장면 파일보다 새로우면 **복구 / 버리기** 대화상자. 공용 모달 대화상자(`UIContext::OpenDialog`: 어두운 배경, Enter=기본, Esc=취소, 체크 목록). 유휴 테스트는 첫 대기부터 3초를 셈(CI의 느린 시작에 흔들리지 않게) | `04afbc4` |
| 프로젝트 설정 | 새 `Engine/Project/ProjectSettings.*`(리플렉션 구조체 10개, 58항목): **설명**(게임 이름·버전·빌드 번호·설명·회사·제작자·저작권·홈페이지·문의 메일·식별자·아이콘·시작 이미지), **맵 & 모드**(게임/에디터 시작 장면, 빌드에 넣을 장면), **패키징**(Debug/Development/Shipping, 출력 폴더, 실행 파일 이름, 압축·안 쓰는 에셋 빼기·에디터 전용 제외·디버그 심볼·출시 콘솔·zip·실패 설명), **지원 플랫폼**, **화면·창**(창 모드·해상도·크기 조절·vsync·프레임 제한·백그라운드·커서·시작 품질), **물리**(중력 3D/2D, 고정 스텝 → 플레이 모드에 바로 적용, 최대 스텝), **언어**, **오디오**, **스크립팅**, **저장·개인정보**(세이브 폴더·클라우드·크래시 보고·통계·연령 등급). `ProjectSettings/Project.json`에 **기본값과 다른 값만** 저장. Project Settings 탭 맨 위에 카테고리 카드, 필드는 리플렉션으로(텍스트·슬라이더·스위치·선택), 툴팁에 설명 + 키, **문제 목록**(빈 이름, 버전 형식, 식별자 형식, 플랫폼 없음, 시작 장면, 모바일 아이콘, 기본 언어), ● 저장 안 됨 배지, 저장 버튼. 제목 줄에 게임 이름. **남은 것**: 검색, 플랫폼별 덮어쓰기, 기본값 되돌리기(↺), `bdg config`, Undo, 빌드(7-4)가 읽기 | `04afbc4` |
| `BDG_SNAP_TEST=1` | 3-3b 그리드·꼭짓점·표면 스냅, 정렬·간격 칩, 측정 도구 자동 테스트 |
| `BDG_SCREEN=render-lights` | 3-5b Forward+ 보드(점광원 256개), `BDG_RENDER_TEST=1`이면 `lights test:` 판정 |
| `BDG_SCREEN=render-ao` | 3-5b SSAO 보드, `BDG_RENDER_TEST=1`이면 `ao test:` 판정(중사양·고사양 비교) |
| `BDG_SCREEN=render-shadows` | 3-5b 캐스케이드 그림자 보드, `BDG_RENDER_TEST=1`이면 `shadow test:` 판정(중사양·고사양 비교) |
| `BDG_SCREEN=render-gi` | GI Tier 0 코넬 박스(반사 프로브·IBL·SH 프로브 볼륨·라이트맵), `BDG_RENDER_TEST=1`이면 `gi test:` 판정 |
| `BDG_GI_TEST=1` | 편집기에서 반사 프로브·프로브 볼륨·라이트맵(g6) 추가·굽기·Bake GI 칩·되돌리기 자동 테스트 |
| `BDG_REFSCENE_TEST=1` | 렌더 기준 씬 자동 테스트(`BDG_ASSETS`=Tests/RenderReference/Assets + `BDG_SCENE_FILE`) |
| `BDG_RENDER_STATS=1` | 마지막 프레임의 패스별 GPU 시간·렌더 통계를 로그로 |
| `BDG_BOOKMARK=N` | 씬 뷰를 카메라 북마크 N에서 시작 |
| `BDG_QUALITY_TEST=1` | 품질 단계 자동 테스트(감자 평면·저사양 정점 조명·중사양부터 그림자·FXAA·Bloom·배지) |
| `BDG_SCENE_CAMERA=yaw,pitch,거리[,x,y,z]` | 씬 뷰 카메라 시작 위치(스크린샷, 중심점은 선택) |
| `BDG_MATERIAL_TEST=1` | 머티리얼 자동 테스트(빈 `BDG_ASSETS`에 머티리얼·인스턴스·텍스처를 만들어 색·쿠킹·재사용 확인) |
| `BDG_SCREEN=render-assets` | Khronos 표준 모델 4개(먼저 `python tools/testassets.py fetch`, 다른 폴더는 `BDG_TESTASSETS`) |
| `BDG_GIZMO_TEST=1` | 기즈모 자동 테스트(축·평면 이동, 되돌리기 1번, Esc 취소, 회전 고리, 크기, Ctrl 스냅, 로컬 축) |
| `BDG_SCENEVIEW_TEST=1` | 씬 뷰 자동 테스트(그리기·클릭 선택·외곽선·궤도·확대·맞추기·이동·비행·선택 해제·아이콘) |
| `BDG_MODEL=<.gltf/.glb>` | `render-ref` 기준판 왼쪽에 모델 표시(2.2 m로 맞춤) |
| `BDG_PROJECT_TEST=1` | Project 패널 자동 테스트(Scripts 폴더 → C++ 클래스 항목 → 이름 바꾸기) |
| `BDG_SCREEN=parity:Button` | 디자인 일치 검사용 부품 화면 (Panel·Tabs·Button·Field·ListRow·Badge·Node·Cover) |
| `--size 800x600`, `--dump-tokens 경로` | 창 크기(UI 단위), 엔진이 쓰는 토큰 값 JSON 저장 |
| `BDG_UI_SELFTEST=1` | 갤러리에서 클릭·Tab·한글 입력을 자동 주입하고 결과를 `ui:` 줄로 기록 |
| `BDG_BATCH_STRESS=1` | 사각형 1만 개 부하 테스트 |
| `BDG_GL_ERROR_TEST=1` | 일부러 GL 오류를 내서 디버그 출력 경로 확인 (Debug 빌드) |
| `BDG_IDLE_TEST=초` | 대기 상태로 N초 실행 후 `idle: frames= cpu_ms= wake_latency_ms=` 기록 (중간에 Wake 1회) |
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
| `Engine/Platform/Accessibility*` | 화면 낭독기 연결: Windows UI Automation 제공자(위젯 → 접근성 요소, 포커스 알림, 누르기·값) |
| `Engine/RHI/RHI.h`, `RHI_GL.cpp` | 그래픽 추상화. 버퍼·텍스처·파이프라인·그리기 (GL 4.5 DSA) |
| `Engine/Render2D/Batch2D.*` | 2D 일괄 그리기 (SDF 둥근 사각형, 선, SDF 글자, RGBA 이미지) |
| `Engine/Assets/GltfImport.*` | glTF 2.0 임포트(cgltf) + meshoptimizer 최적화 → `ImportedModel` |
| `Engine/Assets/MaterialFile.*` | `.material`/`.matinst` 읽기·쓰기·부모 체인 해석 |
| `Engine/Assets/TextureCooker.*` | 텍스처 프리셋·`.meta` 설정, 밉, BC7/BC5/BC4, DDS, 쿠킹 캐시 |
| `Engine/Runtime/MaterialLibrary.*` | 프로젝트 머티리얼 → GPU(텍스처 쿠킹·업로드 캐시) |
| `tools/testassets.py` | 표준 테스트 에셋 받기·확인(`Tests/testassets.lock.json`) |
| `Engine/Runtime/Gizmo.*` | 이동·회전·크기 기즈모: 핸들 히트 테스트, 드래그 계산, 그리기 (씬과 명령은 모름) |
| `Engine/Runtime/SceneView.*` | Scene 패널 3D: 씬 → 그리기 항목, 메시 캐시, 에디터 카메라, 피킹, 오버레이 |
| `Engine/Render3D/Culling.*` | 절두체 평면·상자 판정·상자 변환 |
| `Engine/Core/ImageCompare.*` | FLIP(LDR) 이미지 차이 + 열지도 |
| `tools/make_render_reference.py` → `Tests/RenderReference/` | 렌더 기준 프로젝트(구역·북마크·머티리얼) 생성기와 결과 |
| `Engine/Render3D/Quality.*` | 품질 단계 설정 묶음, 프로젝트 범위, 덮어쓰기, 톤 곡선 |
| `Engine/Render3D/*` | 3D: 절차 메시(`MeshGen`), 전방 렌더러(`Renderer3D`, GGX + 방향광 + 반구 환경), 렌더링 기준판(`ReferenceBoard`) |
| `Engine/Text/Font.*` | FreeType SDF 글자 아틀라스 |
| `Engine/UI/Theme.*` | `design-system/project/tokens.json` 읽기, 실시간 다시 읽기 |
| `Engine/UI/UI.*` | 유지형 위젯 트리, 플렉스 레이아웃, 입력·포커스, 부품 8개, 텍스트 입력 |
| `Engine/UI/Shortcuts.*` | 단축키: 동작 이름 ↔ 키 조합, 프리셋 unity·unreal, 충돌 검사, `ProjectSettings/Shortcuts.jsonl` |
| `Engine/UI/Dock.*` | 도킹 트리(분할·탭 묶음), 경계선 드래그, 탭 끌어 옮기기, 레이아웃 JSONL 저장 |
| `tools/ui_compare/` | 디자인 일치 검사 (`compare.py`) |
| `Engine/Core/Log.*`, `Image.*` | 텍스트 로그, PNG 저장 |
| `Engine/Core/Reflection.*`, `Math.h` | 리플렉션 레지스트리(타입·필드·함수, 경로로 JSON 값 읽기/쓰기, 변경 추적), 값 타입 |
| `Engine/Core/Space.*` | 좌표계·단위 규칙(오른손 Y-up, 앞 -Z, 1=1m, 각도 도), 오일러↔쿼터니언, TRS (GLM) |
| `Engine/Core/Handle.h` | `Handle<T>{index, generation}` + `Pool<T>`(세대 비교, 주소 고정), 공통 오류 문구 |
| `Engine/Core/Crash.*` | 크래시 처리기: 미니덤프·`crash.txt`·로그 → `BugReports/crash-<시간>/`, 다음 실행 때 제보 버튼 |
| `Engine/Core/Guard.*` | `BDG_CHECK`(복구 가능한 검사), `GuardedCall`(SEH 가드) |
| `Engine/Core/Command.*` | 명령 버스: set·rename·add·del·mv·addcomp·rmcomp, Undo/Redo, 묶음, 연속 편집 합치기, JSONL 로그·재생 |
| `Engine/Assets/AssetDatabase.*` | 에셋 DB: `.meta`(GUID·가져오기 설정·출처), GUID↔경로 표, 이동 추적, 파일 감시 |
| `Engine/Scene/SceneFile.*` | `.scene` JSONL 저장·열기(기본값 생략, 계층 순서, 같은 씬 = 같은 바이트) |
| `Engine/Scene/*` | 최소 씬 모델(영구 ID·이름·부모·컴포넌트, 스냅샷), 기본 컴포넌트(Transform·MeshRenderer·Light·Camera)·샘플 컴포넌트 |
| `Engine/Runtime/EditorModel.*` | 씬 + 명령 버스 + 선택, Hierarchy·Inspector를 씬·리플렉션에서 생성 |
| `Engine/Scripting/*` · `Scripting/Bidulgi.Scripting/` | C# 스크립팅(4-1): .NET 호스트(hostfxr), JSON 요청 채널(명령 버스), 리플렉션 → C# 바인딩 생성; 관리 코드 API |
| `tools/bht/bht.py` | 헤더 도구: `BCLASS`/`BPROPERTY`… → `<File>.gen.cpp` (CMake `bdg_reflect()`) |
| `Tests/` | 단위 테스트 (`BidulgiTests.exe`, 자체 최소 매크로 `bdg_test.h`) |
| `Editor/main.cpp` | 에디터 실행 파일 진입점 (C API만 사용) |
| `Engine/Runtime/EditorTests/*.inl` | 자동 검사(`BDG_*_TEST`)를 분야별로: scene·editor_ui·assets·render·scripting. Engine.cpp가 C API 바로 앞에서 include(같은 번역 단위). **새 검사는 `BDG_EDITOR_TEST("BDG_X_TEST", Inject, Report)`로 등록** (`EditorTests/Registry.h`) — Engine.cpp를 고치지 않음 |
| `ThirdParty/glad` | 생성된 GL 로더 (커밋됨) |

### 작업 규칙
- 상태: `[ ]` 대기 · `[~]` 진행 중 · `[x]` 완료 · `[-]` 보류. 한 번에 **하나만** `[~]`.
- **완료 조건을 실제로 확인한 뒤에만** `[x]`. 확인 방법(로그 줄, 스크린샷)을 결과에 적습니다.
- 항목 하나 = 커밋 하나 이상. 커밋 메시지는 영어 요약 + 본문. 커밋 후 이 파일의 해당 줄을 갱신합니다.
- 순서를 바꾸거나 항목을 추가하면 [decisions](planning/decisions.md)에 이유를 한 줄 남깁니다.
- 외부 라이브러리는 MIT 호환 라이선스만 (MIT, BSD, zlib, Apache-2.0, ISC, FTL, 글꼴은 OFL). GPL·LGPL 금지.
- 외부 라이브러리 타입을 `bdg_api.h`나 엔진 공개 헤더에 노출하지 않습니다.
- 확정된 규칙: 생명주기 이름은 유니티식(`Awake` `Start` `Update(dt)` …), 좌표는 오른손·Y-up·앞쪽 -Z·1단위=1m(2-0 확정, `Core/Space.h`), 입력은 입력 액션만, 게임 구조는 컴포넌트 방식.
- 사용자는 한국어로 소통합니다. 문서는 한국어, 코드 주석과 커밋은 영어.

---

## 병렬 세션 규칙 (2026-11-25)

여러 세션이 동시에 작업할 때 서로의 파일을 건드리지 않게 **분야(레인)**를 나눕니다. 각 세션은 자기 worktree·브랜치에서 작업하고, 합치기는 허브 세션이 합니다.

| 레인 | 대기열 항목 | 주로 고치는 파일 | 검사 파일 |
|---|---|---|---|
| A. 3D 그래픽 → **물리·입력** (2라운드) | ~~3-4b, 3-5c, 3-8b, 3-9b~~ 완료 → **6-2 물리, 6-4 입력 액션** | 새 `Engine/Physics/*`, 새 `Engine/Input/*`, `Engine/Scene/Components.h`(물리·입력 컴포넌트), 플레이 모드 고정 스텝 연결 | `EditorTests/scene.inl` (새 검사) |
| B. C# 스크립팅 → **빌드·오디오** (2라운드) | ~~4-4c, 4-4d, 4-6b(C#), 4-5~~ 완료 → **7-4 빌드·패키징(+7-4b), 6-3 오디오** | 새 `Engine/Build/*`, `tools/bdg/*`, 게임 실행 파일(플레이어), 새 `Engine/Audio/*` | `EditorTests/assets.inl` (새 검사) |
| 허브 | 합치기, CI, 큰 결정, 그 밖의 항목 | 나머지 | 나머지 |

- **새 자동 검사는 등록식**(`BDG_EDITOR_TEST`)으로 자기 레인의 `.inl`에 — `Engine.cpp`의 `bdg_engine` 구조체·검사 분기에 줄을 더하지 않습니다.
- `Engine.cpp`·`EditorModel.cpp`·`UI.*`처럼 공용 파일은 꼭 필요할 때만, 작게 고치고 커밋 메시지에 적습니다.
- CI(`ci.yml`)에는 자기 검사 줄을 **같은 분야 검사들 옆**에 넣습니다(서로 다른 곳에 넣어야 합칠 때 충돌이 적음).
- 이 파일(`QUEUE.md`)은 **자기 항목의 `[ ]`→`[x]`와, 완료 표의 자기 행**만 고칩니다. 완료 표 행은 표 맨 끝이 아니라 **같은 분야 행 바로 아래**에 넣습니다.
- 커밋은 자기 브랜치에. 푸시·main 합치기는 허브가 합니다.

## ★ 지금 우선순위 (2026-11-25 오후, 허브 결정) — "첫 게임"을 만들 수 있게

목표: 간단한 게임을 에디터에서 만들고 **exe로 내보내** 남에게 줄 수 있게(약 1주). 순서: 물리 → 입력 → 빌드·패키징 → 오디오 → 프리팹 → 게임 템플릿 하나. 레인 A: 6-2 물리 → 6-4 입력. 레인 B: 7-4 빌드·패키징(+7-4b) → 6-3 오디오. 허브: 합치기·CI, 그 다음 6-1 프리팹과 템플릿. M5(Graph·Lua·Python)는 이 뒤로.

## (이전) 우선순위 (2026-11-25, 사용자 요청) — 에디터 편의·미리보기·임포트·포스트 프로세스

아래 순서가 목록 위치보다 우선입니다. 이전 우선순위(2026-11-23 3D 그래픽 먼저)는 2-6까지 모두 끝남. 원칙: **유니티·언리얼·Godot 공식 문서를 참고해 오래 다듬어진 편의 기능을 따름**, 애니메이션 블렌드 스페이스·카메라 흔들림·사운드 큐는 **언리얼식**. 필요한 MIT 호환 라이브러리는 묻지 않고 설치(2026-11-25 사용자 지시).

1. ~~**E-1 미리보기·썸네일**~~ (2026-11-25 완료, 3-2b 포함) — Project 패널 타일 썸네일(텍스처·모델·머티리얼·프리팹·장면), 모델·머티리얼은 오프스크린 렌더로 캐시, Inspector 아래 **미리보기 창**(돌려 보기)
2. ~~**E-2 계층 트리**~~ (2026-11-25 완료) — 접기·펴기(▶/▼, Alt+클릭 = 하위 전부), **트리 가이드 선(ㄴ자)**, 접힘 상태 저장
3. ~~**E-3 씬 아이콘**~~ (2026-11-25 완료) — 빛·카메라·프로브·사운드 등 컴포넌트별 아이콘(유니티식 2D 빌보드, 거리 무관 크기), 선택한 카메라는 **절두체 선**, 아이콘 크기·표시 토글
4. ~~**E-4 인스펙터·UI 편의**~~ (2026-11-25 완료: E-4a 수식·오른쪽 클릭 메뉴·UI 배율, E-4b 라벨 끌기·벡터 축 상자·색 선택기·에셋 놓기·계층 방향키) — 라벨 끌어서 값 조절(유니티), 숫자 칸 수식(`2*3`), 오른쪽 클릭 '기본값으로'·복사·붙여넣기, 벡터 칸 축별 끌기, 색 칸 선택기(2-5c 재사용), 에셋 칸 끌어다 놓기·찾기, **UI 배율**(70~200%, Ctrl+휠)·패널 비율 저장
5. ~~**E-5 FBX·OBJ 임포트**~~ (2026-11-25 완료) — ufbx(MIT, FBX·OBJ·MTL), 단위·축 변환(2-0 규칙), 메시·머티리얼·계층(스켈레톤·애니메이션은 M7)
6. ~~**E-6 포스트 프로세스 볼륨**~~ (언리얼식, 2026-11-25 완료: E-6a 노출·자동 노출·블룸·비네트·색수차·그레인, E-6b 피사계 심도·렌즈 플레어) — 장면 전체/상자 범위·섞임 거리·우선순위, 비네트·색수차·필름 그레인·자동 노출(눈 적응)·피사계 심도·렌즈 플레어, 기존 Bloom·톤매핑·그레이딩을 볼륨 값으로
7. ~~**E-7 갓레이(빛줄기)**~~ (2026-11-25 완료, 볼류메트릭 산란은 나중) — 언리얼 Light Shafts식 화면 공간 방사 블러(태양 가림 마스크) + 고사양은 볼류메트릭 포그 산란 검토
8. ~~**2-7 CLI `bdg` 1차**~~ (2026-11-25 완료, GPU가 필요한 render·budget·gi diagnose는 다음)
9. ~~**E-8 함수 리플렉션 → Graph 호출**~~ (2026-11-25 바탕 완료: 노드 목록·실행·CLI, C#·Lua 연결은 M4·M5) — `BFUNCTION(Callable)`(언리얼 BlueprintCallable식): C++ 클래스를 상속한 컴포넌트의 함수·변수를 Graph 노드로, C#은 `[Callable]`, Lua는 `bdg.export` — 무거운 계산은 C++, 조립은 Graph (M5 5-1 앞에 바탕만)
10. ~~**E-9 카메라 흔들림**~~ (2026-11-25 완료) (언리얼 Camera Shake식) — 흔들림 에셋(펄린·사인파 위치·회전, 감쇠), `Camera.Shake(asset, scale)` 한 줄, 거리 감쇠
11. ~~**E-10 파티클 구조 설계**~~ (2026-11-25 설계안 [particles](features/particles.md) — **사용자 검토 대기**: 편집 방식·GPU 시기·첫 예제. 답을 받으면 M6의 VFX P-1부터) — 문서 먼저(에미터·모듈 스택, CPU/GPU 경로, 품질 단계별 예산), 사용자와 검토 후 구현
12. 그 다음: M4 이후 순서. **사운드 큐**(언리얼식 노드: 랜덤·믹서·모듈레이터·감쇠)는 6-3 오디오에, **블렌드 스페이스**(언리얼식 1D/2D 격자·샘플 점 편집)는 7-1에 포함

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
| 엔진 CI | GitHub Actions: Windows Debug·Release 빌드, 엔진 코드 경고 0개 검사, Mesa 소프트웨어 GL 4.5로 자동 테스트 4종(에디터 화면·UI/한글 입력·1만 개 배칭·버그 제보), 스크린샷·로그 아티팩트. README에 배지 | (이 커밋) |
| 버그 제보 | GitHub 이슈 양식(버그·기능 제안) + 에디터 F12·버튼: 스크린샷·로그·시스템 정보 폴더 + 미리 채운 이슈 양식 | (이 커밋) |
| 에디터 2차 디자인 | 로고·Obsidian Dark 테마·컴포넌트 카드(접기)·타입 아이콘·하단 도크(Project/Console/Build Log/Profiler)·Consolas·상태 줄 FPS/메모리·exe 아이콘 | (이 커밋) |
| 0-13 대기 중 다시 그리기 멈추기 | 바뀔 때만 그림(OS 이벤트 대기), vsync 확인 + 무시 감지 시 주사율 타이머 제한, `Window::Wake()`. 대기 CPU 96% → 0ms/5초(스레드별 측정), `BDG_IDLE_TEST=3`: frames=3, wake_latency 0.1~0.6ms. CI 테스트 5번 추가. 셀프테스트 클릭이 화면 배율 1.5에서 빗나가던 버그도 수정 | `1a657b9` |
| 0-14 버그 제보 보완 | 단축키 `Ctrl+Shift+B`, 보고서의 사용자 폴더·이름 → `%USERPROFILE%`·`%USERNAME%`, 폴더를 먼저 열고 `README.txt`로 파일 설명(첨부 전 확인), 30일 지난 보고서 삭제, stb_image_write 압축 PNG(에디터 1920×1080 = 324KB). CI 4번에 사용자 이름·500KB 검사 추가 | `5b89dbc` |
| 0-12 M0 손질 | Inspector 카드 헤더에 위쪽 `line` 1px(안 쓰던 `ComponentHeader` 삭제), Batch2D SDF 삼각형 모드 → Node 실행 핀 삼각형, `shadow` 토큰 파싱(`Theme::S`) + `Batch2D::Shadow`(흐림 반경 SDF) → 노드 그림자. 갤러리 스크린샷에서 세 가지 확인, 경고 0개 | `8abd5a9` |
| 0-12b 글꼴 | `Assets/Fonts/` Pretendard Regular 1.3.9 + JetBrains Mono Regular 2.304 (OFL, 라이선스 파일 포함). 로그 `font: ...Pretendard` / `font (mono): ...JetBrains Mono`, 시작 689ms | `a397455` |
| 0-10 디자인 일치 검사 1차 | `tools/ui_compare/compare.py`: 토큰 ✓ 72/72, 부품 8개 레이아웃 최대 오차 0.64px (기준 1px). 브라우저는 Playwright 대신 헤드리스 Edge `--dump-dom` + 측정 스크립트 주입(새 의존성 없음). 엔진 `BDG_SCREEN=parity:<부품>`, `--size`, `--dump-tokens`. 글자 폭을 힌팅 없는 진행폭으로 바꾸는 등 위젯 치수를 CSS에 맞춤. CI `Design parity` 단계 | `5fedfeb` |
| 1-1 도킹·탭·패널 크기 조절 | `Engine/UI/Dock.*`: 분할 노드(축·비율)/잎(탭 묶음) 트리, 경계선 드래그(마우스 캡처·크기 조절 커서), 탭 끌기 → 위·아래·좌·우·가운데 미리보기 → 트리 수정, 사용자별 `.cache/EditorLayout.jsonl` 저장·불러오기(형식은 계획대로 한 줄에 노드 하나). Batch2D 정점별 잘라내기로 패널 밖 그리기 차단(드로우콜 1 유지). 자동 테스트: 폭 256→356, Console 이동, 재시작 후 같은 트리. CI 6번 | `36b1e5c` |
| 1-2 리플렉션 + 헤더 도구 | `Core/Reflection.*`: TypeInfo/FieldInfo(플래그 Edit·Save·ReadOnly·Replicated·Animatable, 메타 Range·Tooltip·Category·ShowIf)/FunctionInfo, `Registry::Get/Set/Call`(경로 `a.b`, `items[2]`, `pos.x`, 값은 JSON 텍스트), 바뀐 필드 기록(`TakeChanges`). 지원 타입: bool·int32·float·double·Vec2/3/4·Quat·Color·string·enum·핸들·구조체·`std::vector<T>`. `tools/bht/bht.py`(Python, libclang 없음: 이름만 파싱하고 타입은 템플릿 추론), CMake `bdg_reflect()`가 바뀐 헤더만 다시 생성. 단위 테스트 7개·검사 137개 통과(`BTestComponent`: 필드 6종 + 함수 1개). CI `Unit tests` 단계 | `fb72d1a` |
| 1-3 명령 버스 + Undo/Redo | `Core/Command.*`: 명령 `{cmd, target, args(JSON), source, t}` → 실행 + 역명령 기록, Undo/Redo, 묶음(AI 요청 하나 = Undo 한 번), UI의 같은 필드 연속 편집은 300ms 안이면 합침, JSONL 로그 저장·`--replay`. 대상이 될 최소 씬 모델(`Scene/*`, 2-1의 바탕)과 기본·샘플 컴포넌트. Inspector·Hierarchy를 씬·리플렉션에서 생성, 필드 확정 → `set` 명령, Ctrl+Z/Y 전역(필드 내부 Undo는 미확정 편집 중에만). 자동 테스트: `edit=[3,4,5] undo=[0,1,0] redo=[3,4,5]`, 재생한 씬 덤프 동일. 단위 테스트 14개·검사 214개. CI 7번 | `7c761ba` |
| 1-3b Undo 기록 패널 | 아래 도크에 History 탭: 단계마다 한 줄(`Player: Transform.position = [3,4,5]`), 현재 단계 강조, 되돌린 단계 흐리게, 클릭하면 그 시점으로 이동(`CommandBus::JumpTo`, 로그에는 undo/redo로 남아 재생도 같음), 출처 열(AI·Script·Terminal), 묶음은 한 줄 `(N edits)`로 접힘. float 값은 가장 짧은 표기(0.45), 긴 입력값은 칸 안에서 잘림. 자동 테스트: 기록 클릭 후 `maxHealth=100 undo_depth=1` | `7a53daf` |
| 1-4 핸들 기반 오브젝트 수명 | `Core/Handle.h`: `Handle<T>{index:32, generation:32}`(0 = 널), `Pool<T>`(파괴 시 세대 증가 → 재사용돼도 옛 핸들 무효, 객체 주소 고정), `DeadHandleMessage` 모든 언어 공통 문구. 씬이 풀 위로(영구 ID ↔ 현재 핸들 표): 삭제 후 Undo로 같은 `g1`이 돌아와도 옛 핸들은 무효. 단위 테스트 3개 추가(총 17개·242개 검사) | `cfadf8f` |
| 1-4b 크래시 보고 | crashpad 대신 Windows `dbghelp`(새 의존성 없음): 처리되지 않은 예외 → `BugReports/crash-<시간>/`에 `crash.dmp`(미니덤프)·`crash.txt`(예외 코드·모듈+오프셋)·로그, `pending-crash.txt` 표시 → 다음 실행 때 경고 + 상태 줄 "지난 실행이 비정상 종료됨 — 제보하기"(폴더 + 미리 채운 `[Crash]` 이슈, 로그는 사용자 경로 치환). 디버거가 붙어 있으면 설치 안 함, 스택 넘침용 여유 스택. 상자 레이아웃은 숨긴 자식을 건너뜀. CI 8번 | `ceba06b` |
| 1-4c 잘 죽지 않는 엔진 기초 | C API 널 인자 경고+오류 코드(크래시 없음), `BDG_CHECK`(실패 시 로그·횟수·false 반환으로 복구), `GuardedCall` SEH 가드(접근 위반·0 나누기·스택 넘침·C++ 예외 → 오류 문구), Custom 패널 그리기를 가드 → 크래시한 패널은 그 자리에 안내문, 실패 시 `.cache/autosave/`에 씬·명령 로그 자동 저장. 테스트: 오류 주입 5개 + 로더 퍼징 3개(반사 값·씬 스냅샷·명령 줄·도킹 레이아웃, 각 1500회 변형) → 25개·330개 검사. CMake `BDG_ASAN` + CI `asan` 작업(ASan으로 전체 단위 테스트·퍼징), CI 9번 | `e676464` |
| 1-5 단축키 시스템 + 프리셋 | `UI/Shortcuts.*`: 동작 15개(undo·redo·duplicate·delete·rename·save·play·도구 5개·focus·gallery·bug), 프리셋 `unity`(기본)·`unreal`(예: Play Ctrl+P ↔ Alt+P, Duplicate Ctrl+D ↔ Ctrl+W), 사용자 바인딩은 프리셋 위에 유지, 충돌 검사(로그 + 설정 목록), `ProjectSettings/Shortcuts.jsonl`(첫 줄 프리셋 + 바꾼 것만). 첫 실행 때 선택 줄, Project Settings에 프리셋 버튼과 동작별 키 목록. 키 입력 칸에서 글자를 칠 때는 Ctrl/Alt 조합과 F키만 단축키로. 키 전체(A~Z, 0~9, F1~F12, Alt). Duplicate는 명령 묶음(add + addcomp). 자동 테스트: `unity_play=1 unreal_play=0 objects=10`, 재시작 후 프리셋 유지. 단위 테스트 3개(총 28개·362개). CI 10번 | `c610361` |
| 1-6 접근성 기초 | AccessKit 대신 Windows UI Automation 제공자 직접 구현(`Platform/Accessibility*`, 새 의존성·Rust 빌드 없음): 창 = 루트, 포커스 가능한 위젯 = 자식 요소(Tab 순서, 위젯 id로 찾아 패널이 다시 만들어져도 안전), 역할(Button·TabItem·Edit·ListItem·Group)·이름(장식 기호 ▶·+ 제외)·AutomationId·포커스 상태·화면 위치, 누르기(Invoke)·값(Value, 입력은 명령으로 확정) 패턴, 포커스 변경 알림(`UiaRaiseAutomationEvent`). Ctrl+F6 = 다음 도킹 패널의 첫 컨트롤로 이동(두 프리셋 공통, 단축키 동작 `view.nextPanel`). `BDG_A11Y=0`이면 끔. 자동 테스트: 다른 스레드의 UIA 클라이언트가 `focused_name=Add Component control=Button children=30`, Ctrl+F6 후 `next_panel=h.scene`. CI 11번. **내레이터가 실제로 읽는지는 사용자 수동 확인 필요** | (이 커밋) |
| 1-7 네트워크·공동 편집 대비 점검 | 영구 ID·명령 직렬화·바뀐 필드 추적·결정성 4가지 확인 → [network-readiness](architecture/network-readiness.md). 발견해서 고침: 바뀐 필드 기록이 비워지지 않고 쌓이던 문제(`EditorModel::Update`가 매 프레임 비움). 결정성 테스트 3개: 무작위 편집 세션을 다른 버스에 줄마다 전달(2010줄), JSONL 재생, **다른 프로세스**(`BidulgiTests --replay-dump`) 재생(1734줄) → 씬 덤프·Undo 깊이 동일. 단위 테스트 31개·376개 검사 | `27d06d1` |
| 2-0 좌표·단위 확정 | 오른손 좌표계, Y-up, 앞쪽 -Z(glTF·OpenGL·Godot·Maya와 같음), 1단위=1m·kg·초, 각도는 편집·파일에서 도(수학 코드는 라디안), 양의 회전은 오른손 법칙, 오일러 순서 Ry·Rx·Rz(유니티와 같은 순서), 앞면은 반시계 감김. GLM 1.0.1(MIT, 헤더만) 추가, `Core/Space.*`: 오일러↔쿼터니언(짐벌 처리), TRS, 점·방향 변환. 규칙을 고정하는 단위 테스트 4개(총 35개·397개) | `0a6971d` |
| 2-1 씬 모델 | 1-3의 최소 씬 모델을 확장: 새 오브젝트에는 Transform 자동 추가, Transform 제거 거부, `LocalMatrix`/`WorldMatrix`/`WorldPosition`(부모 사슬, 2-0 규칙), `mv {"keepWorld":true}` = 화면 위치를 유지하며 부모 변경(계산한 로컬 값을 로그에 적어 재생·Undo가 정확). UI 끌어다 놓기(`draggable`/`onDrop`, 6단위 넘게 움직여야 끌기, 대상 강조 + 따라오는 라벨) → Hierarchy 행을 다른 행이나 Main에 놓으면 위치 유지 이동. 복제는 Transform 값을 `set`으로 복사. 단위 테스트 4개 추가(총 39개·420개), CI 12번 | `82e17a8`, `bfc0945` |
| 2-2 JSONL 씬 저장/열기 | `Scene/SceneFile.*`: 머리 줄 `{"v":1,"defaults":"1.0","scene":..,"mode":"3d"}` + 한 줄에 오브젝트 하나, 기본값 생략(Transform은 0/0/1이 아닐 때만 `pos`/`rot`/`scale`, 컴포넌트는 바뀐 필드만, 모두 기본이면 `{}`), 이름이 id와 같으면 생략, 부모는 `"@id"`, 줄 순서 = 계층 전위 순서(형제 순서 보존). 임시 파일에 쓰고 바꿔치기(저장 중 크래시에도 반쪽 파일 없음). 읽기: 모르는 컴포넌트·키·옛 기본값 세트는 경고, 깨진 줄은 줄 번호와 함께 오류. 에디터: `Scenes/Main.scene`(또는 `BDG_SCENE_FILE`)이 있으면 열고 Ctrl+S로 저장, 열기는 편집이 아니라 Undo 기록을 새로 시작. 분할 저장(셀·레이어 파일)은 형식 문서에만, 구현은 나중. 저장→열기→저장 바이트 동일(단위 테스트 + CI 13번), 로더 퍼징 1500회. 단위 테스트 4개 추가(총 43개·449개) | `3188232` |
| 2-3 에셋 데이터베이스 | `Assets/AssetDatabase.*`: 첫 스캔 때 파일마다 `<파일>.meta` 한 줄(`{"guid":"16자리","importer":"model"}`, 확장자로 importer), 스캐너는 기존 `.meta`를 다시 쓰지 않아 사용자 설정·출처가 보존됨. GUID↔경로 표. `Move()`는 파일과 `.meta`를 함께 옮김. 탐색기처럼 `.meta` 없이 옮겨진 파일은 다음 새로 고침에서 내용(크기+FNV 해시)으로 짝 잃은 `.meta`를 찾아 GUID 재연결, 못 찾은 `.meta`는 지우지 않고 보고. `.meta`째 복사된 파일은 새 GUID. 숨김 폴더(`.cache` 등) 제외. `FileWatcher`(감시 스레드 → `Window::Wake`)로 잠든 에디터도 바로 새로 고침, 옮겨진 에셋을 가리키는 씬의 문자열 필드는 명령 묶음 하나로 새 경로로 갱신. 에셋 폴더는 프로젝트 허브 전까지 `BDG_ASSETS`. 단위 테스트 4개(총 47개·483개), CI 14번 | `9229b14` |
| 2-3b 빨리 열리는 에디터 | 에셋 DB 색인 캐시 `<프로젝트>/.cache/assetdb.jsonl`(GUID·경로·크기·해시·수정 시각): 다음 시작 때 크기·시각이 같은 파일은 `.meta`도 내용도 읽지 않음. 스캔은 `FindFirstFileEx` 한 번(목록에 크기·시각이 함께 와서 파일마다 시스템 호출 없음, 경로는 UTF-8). 측정: **에셋 1만 개 첫 스캔 4.9초(`.meta` 1만 개 쓰기 포함), 다음 시작 41ms**(목표 10초), 3천 개 16ms. 시작 단계별 시간 한 줄(`startup: window=260 gl=32 fonts=1 ... total 294 ms`) + 첫 프레임 작업·만든 글자 수(`first frame 503 ms, 122 glyphs`) → 남은 큰 비용은 첫 프레임 글자 SDF 생성(글자당 약 4ms, 아틀라스 캐시는 후속). CI 첫 단계가 시작 시간을 실행 페이지에 표시. 패키지 지연 로드·씬 점진 로드는 패키지(1.0)·큰 씬이 생길 때로 미룸 | `bcf39e1`, `15d7638` |
| 2-4 Hierarchy·Inspector 실제 동작 | Hierarchy: 씬 트리·선택·끌어다 놓기로 부모 변경(2-1)에 더해 이름 바꾸기 — Inspector 맨 위 Name 칸, F2가 그 칸에 포커스·전체 선택, Enter = `rename` 명령. Inspector는 리플렉션 메타를 따름: `Range` → 슬라이더(칸 안 끌기, 끄는 동안의 `set`은 300ms 합치기로 Undo 한 번, 글자 입력도 그대로), `ShowIf`(`flag`·`!flag`·`field == 값`·`!=`, 조건이 바뀌면 칸이 나타나고 사라짐, 모르는 필드는 숨기지 않음), `Header`(섹션 캡션, bht가 생성), `BFUNCTION(Button)` → 버튼 = 새 명령 `call`(함수 실행, 역명령은 새 명령 `setcomp`로 컴포넌트 값 복원 → Undo·재생 가능). 샘플: PlayerMove `Movement` 머리글, Health `Revive` 버튼. 단위 테스트 3개(총 51개·529개), 에디터 자동 테스트·CI 15번 | `64a0028` |
| 2-4b 여러 오브젝트 편집·값 복사·프리셋 | Hierarchy Ctrl+클릭(추가/빼기)·Shift+클릭(범위) 다중 선택(마지막이 기준). Inspector는 모두가 가진 컴포넌트만, 값이 다르면 "—", 수정하면 선택 전체에 명령 묶음 하나(Undo 한 번); 다중일 때 슬라이더 끌기는 끄고 입력만. 버튼도 전체에. 카드마다 값 복사·값 붙여넣기(`setcomp`)·프리셋 저장·프리셋 적용, 복사한 컴포넌트가 없는 오브젝트에는 "+ 붙여넣기: 타입"(새 컴포넌트, `addcomp`). 복사는 시스템 클립보드에도 `{"bdgComponent":..,"value":..}`로. 프리셋은 `<Presets>/<타입>.preset` 한 줄 JSON (`BDG_PRESETS` 또는 에셋 폴더/Presets). 함께 고침: Inspector가 패널보다 길면 아래가 보이지도 눌리지도 않던 문제 → 도킹 패널 내용 휠 스크롤(본문 밖은 그리지도 맞히지도 않음, 스크롤 막대), 포커스가 옮겨가면 보이게 스크롤. 단위 테스트 1개(총 52개·540개), CI 16번. 메뉴 위젯이 생기면 카드 버튼 줄을 컴포넌트 메뉴로 옮김 | `3ce90bc` |
| 2-5 Project 패널 | 에셋 폴더가 열려 있으면(`BDG_ASSETS`) Project 탭이 실제 내용: 왼쪽 폴더 트리, 오른쪽 현재 폴더(하위 폴더 → 파일, 종류 표시: Texture·Model·Audio·Scene·C# script·Lua script·Python script·C++…). **C++ `.h`+`.cpp` 묶음**: 이름이 같은 헤더·소스는 한 항목 `Weapon · C++ class` + 아래에 두 파일 중첩, 이름 바꾸기 칸에서 바꾸면 두 파일이 함께 바뀜(확장자 유지, GUID 유지, 씬 참조 갱신), 열기는 두 파일을 함께 기본 앱으로. 파일 감시로 바깥 변경도 반영. CI 17번 | `b04a1cd` |
| 2-5t 썸네일 캐시(핵심) | stb_image 2.34(퍼블릭 도메인, 같은 stb 커밋, 사용자 승인) 추가 → `LoadImageRGBA`(PNG·JPEG·TGA·BMP·PSD·HDR, 유니코드 경로). `Assets/Thumbnails.*`: 이미지 에셋을 64×64(비율 유지, 평균 축소)로 만들어 `<프로젝트>/.cache/thumbnails/<GUID>-<해시>.png`에 저장, 다음 시작은 작은 캐시 PNG만 읽음, 내용이 바뀌면 새로 만들고 옛 파일 삭제, 깨진 이미지는 실패로 셈. 단위 테스트 1개(총 53개·551개). **Project 패널에 그리기는 3-2와 함께**(UI에 RGBA 텍스처를 그리는 기능이 씬 뷰 표시와 같음) | `4d6bdbf` |
| 3-0 렌더링 기준판 1단계 | **첫 3D 렌더링.** RHI: 오프스크린 렌더 타깃(RGBA8 + 깊이24, MSAA 4× → 해석), 깊이 테스트·후면 컬링(CCW 앞면), sRGB 텍스처 형식, UBO 구간 바인딩. Batch2D: RGBA 이미지 사각형(모드 3) — UI 안에 3D 화면·썸네일을 그리는 길. `Render3D/`: 절차 메시(사각형·평면·상자·구·원기둥·원뿔, 모두 바깥에서 CCW), 전방 렌더러(GGX 금속/거칠기, 방향광 1 + 하늘/땅 반구 환경, 선형 계산 → sRGB 출력, 톤매핑 없음), 기준판(바닥, 구 7×7 거칠기×금속, 축 화살표, 18% 회색 카드, ColorChecker 24칸, 글자 텍스처 판, 와인딩·깊이 탐침). `BDG_SCREEN=render-ref` 화면, `BDG_RENDER_TEST=1`이 렌더 결과를 읽어 8가지 판정 → 회색 118, 체커 오차 0, 축·오른손·CCW·깊이·위쪽 조명·UV 모두 통과. 단위 테스트 2개(총 55개·587개), CI 18번. **MetalRoughSpheres glTF와 `bdg testassets fetch`는 3-1로**(glTF 로더와 내려받기 승인이 필요) | `153d3b4` |
| 3-1 glTF 임포트·PBR 맵 | cgltf 1.15·meshoptimizer 1.3(둘 다 MIT, 사용자 승인) 추가. `Assets/GltfImport.*`: .gltf(외부·data URI 버퍼)·.glb, 유니코드 경로(cgltf 파일 콜백), 노드 계층 → 월드 행렬, 삼각형 프리미티브(노멀 없으면 생성, 인덱스 없으면 생성), 머티리얼(금속/거칠기·노멀·AO·발광·발광 세기·알파 마스크/블렌드·양면·KHR_materials_unlit), 이미지(내장·data URI·파일, 용도별 sRGB/선형, 샘플러 NEAREST·CLAMP), meshoptimizer(중복 제거·버텍스 캐시·오버드로·페치 순서). 렌더러: 맵 5종, 탄젠트 없으면 화면 미분으로 탄젠트(glTF 샘플 뷰어 방식), 불투명 → 반투명(뒤에서 앞) 순서, 양면·블렌드 파이프라인, 밉맵. 기준판에 **glTF 탐침**(직접 만든 glTF가 실제 임포터를 거쳐 그려짐) → 렌더 검사 9번째 `gltf=ok`. `BDG_MODEL=<파일>`로 아무 glTF나 기준판 옆에 표시(사용자 로컬 파일로 4만 삼각형 모델 확인, 저장소·CI에는 넣지 않음). 단위 테스트 2개(총 57개·612개). 머티리얼 파일·텍스처 쿠킹·표준 에셋 검증은 3-1b로 | `6601ed3` |
| 3-2 씬 뷰 실제 3D | Scene 패널이 열린 씬을 실제 3D로 그림(`Runtime/SceneView.*`): 오프스크린 MSAA 타깃 → 패널 이미지, 오버레이(칩, Draw·Tris·ms, 카메라 방향을 따르는 축 위젯, 조명·카메라 아이콘, 조작 안내). **에디터 카메라**: Alt+왼쪽 드래그 궤도, 가운데 드래그 이동, 휠 확대, 오른쪽 드래그 둘러보기 + WASDQE 비행(휠 = 속도), F 선택에 맞추기. **클릭 선택**(삼각형 레이 피킹, 아이콘 클릭, Ctrl/Shift 추가·제거, 빈 곳 클릭 = 선택 해제). **선택 외곽선**(마스크 타깃 + 2px 외곽선, 가려져도 보임, 자식 포함). **그리드**(1 m/10 m, X 빨강·Z 파랑 축선, 거리 페이드, 바닥 위로 살짝). 메시: 프로젝트 에셋 glTF(해시가 바뀌면 다시 임포트) 또는 내장 도형(Plane·Cube·Sphere·Cylinder·Cone), 없으면 자리표시 상자 + 경고. 머티리얼은 3-1b 전까지 이름으로(M_Ground·M_Gold·M_Steel·M_Copper·M_Glass, 그 외 이름 해시 색). 첫 방향광이 태양. 샘플 씬 Sun 회전을 이 엔진 규칙(양의 pitch = 위)에 맞게 (-50, -30, 0)으로. 키 뗌 이벤트(`KeyUp`) 추가. 프레임 통계에 `ui_draws`·`draws3d` 분리. `BDG_SCENEVIEW_TEST=1` 10개 판정 통과, CI 19번, 로컬에서 CI 스모크 전체 통과. **Project 패널 썸네일은 3-2b로** | `3958640` |
| 3-3 기즈모 | 사용자 요청으로 우선순위 절보다 먼저. `Runtime/Gizmo.*`(계산만, 단위 테스트 가능) + 씬 뷰 연결. **이동**: X·Y·Z 화살표(광선-축 최근접점), XY·YZ·XZ 평면 사각형(광선-평면), 가운데(화면 평면). **회전**: X·Y·Z 고리 + 바깥 화면 고리(화면 각도, 오른손 규칙 부호). **크기**: 축 + 상자, 가운데 균일. 화면에서 늘 같은 크기(110px), 마우스 아래 핸들 노란색, 뒤쪽 고리 반 흐리게. W/E/R 도구(기존 단축키 프리셋), **X 월드/로컬 축**, **Ctrl 스냅**(1 m·15°·0.1), **Esc 취소**. 드래그 하나 = **명령 그룹 하나 = 되돌리기 한 번**(`set Transform.*`), 여러 선택은 맨 위 오브젝트만 (부모가 선택된 자식 제외), 회전은 주 선택을 중심으로. 명령 버스: `CancelGroup()`(되돌리기·다시 하기 모두 남기지 않음), 그룹 안 명령은 커밋 때만 다시 하기 기록을 지움. Batch2D `Quad`, 마우스 이동 이벤트에 Ctrl/Shift/Alt. `view.focus`(F) 단축키가 씬 뷰 맞추기에 연결. 단위 테스트 2개(총 59개·649개), `BDG_GIZMO_TEST=1` 8개 판정 통과, CI 20번 | `393c0ac` |
| 3-1b 머티리얼 파일·텍스처 쿠킹·표준 에셋 | **표준 에셋**: `tools/testassets.py fetch`가 Khronos glTF-Sample-Assets(커밋 고정 e124e8d)의 MetalRoughSpheres·NormalTangentMirrorTest·TextureCoordinateTest·AlphaBlendModeTest + 기준 스크린샷을 `.cache/testassets/`로 받고 SHA-256 확인(`Tests/testassets.lock.json`에 URL·크기·해시·라이선스 CC-BY-4.0/CC0·저작자, CREDITS.txt). `BDG_SCREEN=render-assets` 4개 모델 나란히: 알파(불투명·블렌드·컷오프 0.25/0.5/0.75)와 노멀맵 미러 UV가 Khronos 기준 스크린샷과 일치. **머티리얼**: `Assets/MaterialFile.*` `.material`(고정 파라미터: BaseColor·BaseColorMap·Metallic·Roughness·ORMMap·NormalMap·NormalStrength·AOStrength·Emissive·EmissiveStrength·EmissiveMap·Opacity, shading lit/unlit, blend opaque/masked/translucent, twoSided, cutoff) + `.matinst`(부모 체인, 덮어쓰기, 없는 부모·순환 오류). `Runtime/MaterialLibrary.*`: 경로·guid:·파일 이름으로 찾고 MeshRenderer.material에 연결(없으면 이전 이름 규칙). **텍스처**: `Assets/TextureCooker.*` 이름으로 프리셋(default·normal·masks·grayscale·ui) → `.meta`에 기록, 최대 크기 축소, 밉(sRGB는 선형 평균, 노멀은 재정규화), **BC7(sRGB)/BC5/BC4** 압축(bc7enc·rgbcx, MIT) → DDS(DX10)로 `<프로젝트>/.cache/cooked/`, 다음부터 DDS만 읽음. RHI 압축 텍스처·밉 업로드, 셰이더 노멀 Z 복원(BC5). 단위 테스트 3개(BC7 PSNR>38dB, BC5 오차≤6, sRGB 밉 188, DDS 왕복 등, 총 62개·693개), `BDG_MATERIAL_TEST=1`, CI 21·22번 | `9da9bda` |
| 3-4 조명·그림자 | **방향광 그림자**: 2048 깊이 맵(32비트 float, 하드웨어 비교 = 2×2 PCF) × 3×3 탭, 카메라 시야 조각(기본 40 m)의 경계 구에 맞춘 정사영 + 텍셀 단위 스냅(흔들림 없음), 노멀 오프셋 + 폴리곤 오프셋 바이어스, 맵 가장자리에서 서서히 사라짐, 마스크 머티리얼은 구멍까지. **점·스폿광** 최대 16개(카메라 중심에 가까운 순): 창 함수를 곱한 역제곱 감쇠(세기 1 = 1 m에서 태양 1), 스폿 안쪽·바깥 각 부드러운 가장자리. **하늘**: 절차 하늘(천정·지평선·땅 색, 세기, 해 원반) 배경 패스 + 같은 하늘로 환경광(반구)·반사(거칠기만큼 흐린 지평선). `Light`에 spotAngle·innerSpotAngle(스폿일 때만 보임)·castShadows, 새 `Sky` 컴포넌트(샘플 씬 Sky 오브젝트). Lantern 세기 6·범위 8. 공통 프레임 상수 블록(모든 3D 셰이더), RHI 깊이 타깃·폴리곤 오프셋. 기준판에 떠 있는 상자·빨간 점광·흰 스폿광 → 검사 3개 추가(`shadow`·`point`·`spot`, 총 12개 통과). `BDG_SCENE_CAMERA=yaw,pitch,거리[,x,y,z]`(스크린샷용). 유휴 로그에 깨운 원인(`wakes_mouse/input/other`). 점·스폿 그림자는 3-4b로 | `54f4586` |
| 3-5 품질 단계·포스트 프로세스 | `Render3D/Quality.*`: 감자·저사양·중사양·고사양·초고사양 설정 묶음(조명 방식 없음/정점/픽셀, 라이트 수 0/4/8/16, 그림자 없음/없음/1024/2048, 텍스처 밉 바이어스 2/2/1/0, 포스트 없음/없음/ACES+FXAA/+Bloom·MSAA4, 뷰 거리 0.5~1.25). 렌더러: 장면을 **HDR(RGBA16F, MSAA는 단계별)** 에 선형으로 그린 뒤 Bloom(13탭 다운 → 텐트 업, 5단) → ACES 톤매핑(Narkowicz) → FXAA → 편집 오버레이(그리드·선택 외곽선, 장면 깊이 복사). 기준판은 기준 설정(톤매핑 끔)이라 색 판정 그대로. 프로젝트 범위 `ProjectSettings/Quality.json`(min·max·default), 위 막대 단계 배지 클릭으로 순환, `BDG_QUALITY`·`BDG_QUALITY_SET`(키=값 덮어쓰기). 고친 문제: MSAA 깊이 복사 후 그리드가 바닥에 가려짐(→ 기울기 깊이 바이어스), FXAA 없는 경로에서 알파=휘도로 화면이 비침(→ 알파 1). 단위 테스트 1개(총 63개·737개), `BDG_QUALITY_TEST=1` 7개 판정, CI 23번. 고사양 고유 기능(CSM·SSAO·Forward+)은 3-5b | `5774307` |
| 3-8 렌더 기준 씬 + 디버그 도구 | `tools/make_render_reference.py`가 `Tests/RenderReference/` 프로젝트를 생성(머티리얼 110개·텍스처 2개·`Scenes/RenderReference.scene` 290개 오브젝트, CI가 다시 만들어 차이 없음 확인): ① 재질 매트릭스(흰 7×7 + 금·구리 거칠기 줄) ② 조명·그림자(얇은 기둥·떠 있는 구·계단·빨간 점광·스폿광) ③ 투명(색 유리 겹침·잘린 철망) ④ 지오메트리(작은 상자 144개·아주 얇은 막대·겹친 평면) ⑤ 포스트(발광 12·20 구·어두운 터널) ⑥ 좌표·색(축·18% 회색·ColorChecker·글자 판) ⑦ 품질 비교(북마크 7·8 = 구역 1을 저사양·감자로) + 9 전체. 새 `CameraBookmark` 컴포넌트(씬에 저장): 숫자 1~9 이동, Ctrl+숫자 현재 시점 저장(명령, 되돌리기 한 번), 단계 지정 가능. **렌더 패스 뷰어**: Alt+1~7(GI-B부터 Alt+8 Indirect) 또는 칩으로 Lit·Albedo·Normal·Roughness·Metallic·Depth·Shadow(데이터 보기는 톤매핑·포스트 끔). 칩: 와이어프레임(선 오버레이), 바운딩 상자, 통계 패널(해상도·MSAA·드로우·삼각형·라이트·그림자 캐스터·포스트 + **패스별 GPU 시간**, 타임스탬프 쿼리를 기다리지 않고 읽음). `BDG_RENDER_STATS=1` 로그(`render passes`·`render stats`, CLI는 2-7), `BDG_BOOKMARK=N`. **Sponza는 쓰지 않음**: Khronos판은 CRYENGINE 라이선스(3-8b에서 대체). 단위 테스트 1개(씬 경고 0·290개·북마크 9개, 총 64개·741개), `BDG_REFSCENE_TEST=1` 11개 판정, CI 24번 | `6a3b06f` |
| 3-9 스트레스·이미지 비교 | **인스턴싱**: 오브젝트 상수를 프레임당 스토리지 버퍼(std430) 하나에 그리기 순서대로 넣고, 메시·파이프라인·텍스처가 같은 불투명 오브젝트를 정렬해 묶은 '연속 구간'마다 인스턴스 그리기 한 번(색 등 값은 오브젝트마다 달라도 됨, 인스턴스 번호 스트림 + base instance). 그림자·외곽선·와이어프레임도 같은 구간. 기준 씬 266개 → 장면 드로우 11번. **스트레스 화면** `BDG_SCREEN=render-stress`: 상자 1만 개 + 4096² BC7 텍스처 16장(256 타일을 인코딩해 블록 반복, 밉 전부) — 인스턴싱 18번 ↔ 끔 10,017번, **같은 그림**(픽셀 일치), CPU 0.8→30 ms, GPU 0.6→30 ms. **VRAM**: RHI가 텍스처·버퍼·타깃 바이트를 직접 셈(+ NVIDIA는 드라이버 수치), 이 장면 약 505 MB. **이미지 비교** `BDG_RENDER_COMPARE=<기준 폴더>`: 북마크 1~9를 돌며 `bookmark_N.png`와 비교, 없으면 기준 생성, `<out>/N.png`·`N_diff.png`(열지도), 표 로그. 오차는 `Core/ImageCompare.*`의 **FLIP(LDR)를 논문대로 직접 구현**(색 반대 공간·CSF 필터·HyAB·특징 항, NVIDIA 코드와 비트 일치는 아님). 같은 빌드 = 0.0000, 노출 1.8 = 6곳 실패(최대 0.38). 단위 테스트 1개(총 65개·750개), `BDG_STRESS_TEST=1`, CI 25·26번. 회귀 CI는 3-9b | `a731af5` |
| 3-10 절두체 컬링 | 그래픽·최적화 점검 뒤 사용자와 GI보다 먼저 넣음. `Render3D/Culling.*`: 뷰·투영 행렬에서 평면 6개(Gribb-Hartmann), 변환한 메시 상자(Arvo)로 보수적 판정. 메시는 만들 때 로컬 상자를 가짐. 패스마다 자기 절두체로: 카메라 패스는 카메라, 그림자 맵은 빛의 정사영 행렬(화면 밖이라도 그림자를 드리우는 물체는 남음). 인스턴싱과 함께: 오브젝트 상수는 항목 순서 그대로, 패스마다 보이는 오브젝트 번호 목록을 인스턴스 스트림에 써서 구간을 만듦. 스트레스 근접 시점: 10,017개 중 333개만 그림(9,684개·그림자 840개 컬링), **컬링 끔과 같은 그림**. 품질 키 `culling`. 유휴 테스트는 입력 때문에 그린 프레임을 빼고 판정(`frames_without_input`, 데스크톱 마우스 움직임으로 흔들리던 문제). 단위 테스트 1개(총 66개·757개), CI 25번에 컬링 판정 | `3186992` |
| GI Tier 0-A 반사 프로브 + IBL | **IBL**: 하늘(또는 기준판의 반구색)을 큐브맵(128, RGBA16F)으로 그린 뒤 **GGX 중요도 샘플링으로 거칠기 5단계** 미리 흐림(필터드 중요도 샘플링, Karis) + 작은 밉을 CPU로 읽어 **SH9 조도**(코사인 로브 / π)로 — 하늘이 바뀌면 다시 굽고, 모든 화면의 반사·환경광이 이것을 씀(이전의 해석적 두 색 하늘 대체). **반사 프로브**: 큐브맵 배열(하늘 + 7개), 프로브 위치에서 씬을 6면 촬영(선형 출력 모드) → 같은 흐림·SH, **상자 투영**(방 벽에 맞는 반사), 오브젝트 중심이 상자 안이면 그 프로브, 아니면 하늘. 굽기마다 한 번 더 튕긴 빛(처음 2번). 새 `ReflectionProbe` 컴포넌트(크기·상자 투영·세기), 씬 뷰가 바뀌면 자동으로 굽고 **Bake GI** 칩으로 한 번 더. `BDG_SCREEN=render-gi` **코넬 박스**: 프로브 면 방향(+X 초록·-X 빨강), 거울 구에 비친 양쪽 벽, 거친 흰 구의 색 번짐 자동 판정, 2번 튕김 굽기 10 ms. RHI: 큐브맵·배열·면 렌더·면 복사·읽기, 카메라 up. `BDG_GI_TEST=1`(명령으로 추가 → 굽기 → 칩 → 되돌리기). CI 27번 | `e9d6a9e` |
| GI Tier 0-B SH 프로브 볼륨 | 상자를 채우는 **프로브 격자**(칸 중앙마다 하나, 축당 1~16개): 각 점에서 32² 큐브로 씬을 촬영 → 16² 밉에서 SH9 조도 → 저장 버퍼(프로브당 vec4 9개). 셰이더는 상자 안의 픽셀마다 **가까운 8개를 삼선형으로 섞고**(표면 법선 방향으로 칸 크기 1/4만큼 띄움) 확산 환경광을 대체 — 오브젝트당 하나였던 값이 바닥·벽 위에서 변함(반사는 그대로 반사 프로브). 굽기마다 한 번 더 튕김(반사 프로브와 같은 규칙). 새 `LightProbeVolume` 컴포넌트(크기·축별 개수·세기), 씬 뷰가 바뀌면 자동 굽기·**Bake GI** 칩. **Indirect 디버그 보기**(Alt+8, 칩 순환, 간접 확산광만, 압축 표시). 보드 화면용 `BDG_RENDER_VIEW=0..7`. 코넬 박스 4×3×4(48개) 2번 튕김 약 90 ms, 판정: 볼륨 숫자(빨간 벽 쪽 R>G·초록 벽 쪽 G>R) + 화면의 바닥 색 번짐. 고친 것: 점광원 하이라이트가 RGBA16F 최대값을 넘어 **inf → 밉·SH가 NaN**(장면 출력을 60000으로 제한), 상자 투영 0/0, SH 계산이 유한하지 않은 텍셀을 빼고 경고. `BDG_GI_TEST`에 볼륨 추가·되돌리기. CI 27번 확장 + 27b(간접광 보기) | `d8858c3` |
| GI Tier 0-C 라이트맵 | **xatlas**(MIT, `ThirdParty/xatlas`, 승인 후 내려받음)로 `lightmapped` 항목들을 **월드 크기 그대로 한 아틀라스에** 펼치고 묶음(텍셀 밀도 고르게, 기본 12 텍셀/m, 2048 넘으면 밀도를 낮춤). 항목마다 아틀라스 좌표(`Vertex3D::lu, lv`, 정점 속성 5)를 가진 메시 사본, 원래 메시·배치가 같을 때만 사용(옮기면 다음 굽기까지 프로브로). **CPU 레이 트레이싱**: 새 `RayScene`(BVH, 앞/뒷면, 그림자 광선), 텍셀마다 코사인 분포 층화 광선 64개 → 닿은 면의 직접광(해·로컬 라이트, 그림자 포함) × 색 + 이전 바운스(라이트맵이 없는 면은 볼륨·프로브 SH), 나간 광선은 하늘 큐브. 결과는 프로브와 같은 '환경광'(E/π): **직접광은 실시간 그대로, 확산 환경광만 대체**. 노이즈는 같은 물체·같은 방향 이웃끼리 부드럽게, 차트 밖과 물체 안 텍셀은 이웃으로 채움(이중 선형 번짐 방지). RGBA16F 텍스처(RHI 새 형식). 결정적(텍셀별 난수 씨앗). 프로브보다 먼저 굽기(프로브가 라이트맵 면을 봄). 코넬 박스: 7개, 284×280, 54k 텍셀, **약 0.8초**(16스레드), 구 아래 **접촉 그림자**(열린 바닥의 5%)·벽 색 번짐 판정. 편집기: `MeshRenderer.bakeLightmap`, **Bake GI** 칩으로만 굽고 오래되면 칩 강조. 단위 테스트 2개(광선 장면, GPU 없는 굽기 — 열린 하늘 = 1, 상자 옆이 더 어두움, 총 68개·776개), `BDG_GI_TEST`에 라이트맵 켜기·되돌리기, CI 27번에 `lightmaps=ok` | `64c4609` |
| GI Tier 0-D 굽기 저장·진단 | **`.cache/gi/<장면>-<입력 해시>.bdggi`**: 반사 프로브(SH + 미리 흐린 큐브 5단계, half), 볼륨 SH, 라이트맵 아틀라스(half)·물체별 메시 사본·텍셀 목록. 해시는 굽기에 영향을 주는 모든 것(메시 내용 해시·배치·재질·라이트맵 표시, 조명·하늘, 프로브·볼륨, 라이트맵 설정, 형식 버전) — 같으면 굽지 않고 읽음(코넬 박스: 굽기 1.7초 → 읽기 16 ms, 같은 화면). 라이트맵은 주소가 아니라 **메시 내용 + 배치**로 다시 짝지음. 임시 파일에 쓰고 바꿔치기(도중에 꺼져도 이전 캐시 유지). **`BDG_GI_READONLY=1`** = 실행 빌드처럼 읽기만(없으면 경고, 굽지 않음). 편집기: 굽고 나면 저장, 프로브·라이트맵 대상이 바뀌면 먼저 캐시를 찾음(되돌리기로 돌아가면 즉시). **`bdg gi diagnose`**(CLI 전까지 `BDG_GI_DIAGNOSE=1`): 물체 속 반사·볼륨 프로브(위치), 볼륨 프로브 사이 벽·0.2 m보다 얇은 벽(빛 샘 의심), 라이트맵 커버리지·겹침·옮겨진 물체, 메모리, 굽기 시간, 캐시, 고칠 방법. **물체 속 볼륨 프로브는 끔**(64개 광선 중 절반 넘게 뒷면 → 가중치 0, 이웃이 채움; 코넬 박스 구 속 2개). 간접광만 보기는 B단계의 Indirect. RHI 큐브 면 업로드. 단위 테스트 1개(캐시 키, 총 69개·785개). CI 27번 저장 확인 + 27c(읽기 전용으로 읽기·같은 판정·진단 줄) + 27d(캐시 없이 읽기 전용 → 굽지 않음) | `d8ced15` |
| 3-5b-A 캐스케이드 그림자 | 그림자 맵을 **카메라 거리별 3단**(고사양·초고사양; 중사양 이하·기준판은 1단 그대로)으로: 시야 깊이를 균등·로그 혼합(λ 0.8)으로 나누고 단마다 자기 구간에 맞춤(텍셀 스냅 유지), 한 깊이 아틀라스에 나란히(RHI 직사각형 깊이 타깃·뷰포트), 경계 10%에서 다음 단과 섞어 이음매 없음, 그림자 도달 거리 2.5배(고사양 100 m). 캐스터 컬링도 단별. **부드러운 그림자**: 회전 포아송 12탭, 반지름을 미터로(모든 단에서 같은 폭, 고사양 2.5 cm). 새 디버그 보기 **Cascades**(Alt+9, 단별 색 + 그림자). 품질 설정 `cascades`·`softness`(BDG_QUALITY_SET). `BDG_SCREEN=render-shadows`: 200 m 바닥에 6.5/18/38/73 m 벽, 중사양·고사양을 직접 렌더해 비교 — 가까운 그림자 경계 15.1 cm → **4.0 cm**, 73 m 그림자(중사양 없음 → 고사양 있음), 단 색 0/1/2. 고친 것: 셰이더에서 vec4 성분을 변수 인덱스로 읽으면(NVIDIA) 잘못된 단이 골라짐 → 분기로. CI 28번 | `4b0ca5e` |
| 3-5b-B SSAO | 고사양·초고사양: **깊이·법선 사전 패스**(불투명 물체의 월드 법선 + 시야 깊이, RGBA16F, 알파 마스크 구멍 반영) → **절반 크기 SSAO**(법선 반구 코사인 분포 12샘플, 반지름 0.5 m, 픽셀마다 회전, 범위 검사로 먼 물체가 가리지 않게, 60~100 m에서 사라짐) → **깊이 인식 블러** 가로·세로(다른 깊이는 안 섞음). 결과는 **간접광(환경광·환경 반사)에만** 곱함 — 직접광·그림자는 그대로(오클루전 맵과 같은 규칙). 새 디버그 보기 **AO**(칩 순환·`BDG_RENDER_VIEW=9`). 품질 키 `ssao`·`aoradius`·`aointensity`, 통계 `ssao=`, GPU 패스 `ssao`. `BDG_SCREEN=render-ao`: 상자와 안쪽 모서리를 중사양·고사양으로 직접 렌더 — 열린 바닥·상자 윗면 1.00(평면에 가짜 그늘 없음), 상자 밑동 0.62, 모서리 0.48, 중사양은 꺼짐, 밝은 화면에서 밑동/열린 바닥 1.00 → 0.83. 단위 테스트 확장(3-5b 설정, 총 69개·790개). CI 29번 | `e62c598` |
| 3-5b-C Forward+ | 픽셀 조명을 **저장 버퍼의 조명 목록**(조명당 vec4 4개)으로 — 고사양 **256개**, 초고사양 **1024개**(전에는 16개), 정점 조명(저사양)은 앞 16개 그대로. 화면 **16×9칸 × 깊이 24층(로그)** 클러스터, 매 프레임 CPU가 조명 구의 화면 사각형·깊이 범위로 클러스터 목록 작성(세고 채우는 두 번, 결정적) → 픽셀은 자기 클러스터의 조명만. 범위 밖 조명은 계산 생략. 중사양은 8개를 모두 도는 기존 방식. 새 디버그 보기 **Lights**(픽셀이 보는 조명 수 열지도, 칩 순환·`BDG_RENDER_VIEW=10`), 품질 키 `clustered`, 통계 `clustered=`·`cluster_refs=`·`cluster_max=`. 라이트맵 굽기도 모든 조명 사용. `BDG_SCREEN=render-lights`: 40 m 바닥에 점광원 256개 — 클러스터 결과가 모든 조명을 도는 결과와 **픽셀까지 같음**(차이 0), 픽셀당 평균 9.4개·최대 31개, 밝은 면적 중사양 0.3% → 고사양 53%. 단위 테스트 확장(총 69개·792개). CI 30번 | `712bc39` |
| 3-5b-D 이방성·컬러 그레이딩·블롭 그림자 | **이방성 필터**: 중사양 4x·고사양 8x·**초고사양 16x**(드라이버 한도까지, 재질 텍스처를 묶을 때 설정·캐시; RHI가 ARB/EXT 확장 확인). **컬러 그레이딩**: 새 `Render3D/ColorGrading`(.cube 파일 읽기 — Resolve·Photoshop에서 내보낸 3D LUT, 0..1 영역 — + 대비·채도·색온도·틴트·세기) → CPU에서 32³ 표를 만들어 3D 텍스처로, 톤매핑·sRGB 뒤에 적용(모든 단계, 디버그 보기·프로브 촬영에는 안 씀). 새 `ColorGrading` 컴포넌트(.cube 경로는 프로젝트 기준, 경로마다 한 번 읽음). **저사양 블롭 그림자**: 그림자 맵이 없는 단계에서 해가 그림자를 드리울 때, 서 있는 물체마다 바닥에 부드러운 검은 원(바닥 크기의 1.6배, 물체 밑은 진하게) — 반투명 물체 목록에 섞여 한 번에 그려짐, 바닥·큰 평면은 제외. 보드 화면에도 `BDG_QUALITY_SET` 적용. 판정: AO 보드에서 초고사양 바닥 텍스처 이방성 16/16, 따뜻한 색온도로 바닥 R−B −11 → 56; 그림자 보드에서 저사양 블롭 5개·상자 밑 0.77배(블롭 끈 것 대비), 중사양 블롭 0. 단위 테스트 1개(.cube 읽기·오류, 항등 표, 채도 0, 색온도, 세기, 표 만들기, 단계 설정; 총 70개·811개). CI 28·29번 판정 확장 | `f85a6df` |
| 3-4b 점·스폿광 그림자 | **그림자 아틀라스** 하나(깊이 32F, 정사각 타일): 스폿광 = 원뿔(+4° 여유) 원근 타일 1개, 점광 = **큐브 6면**(+x −x +y −y +z −z, 필터 탭이 타일 밖으로 안 나가게 90°보다 조금 넓게) 타일 6개, 픽셀은 광원 쪽 가장 큰 축으로 면을 골라 타일 안 3×3 탭 × 2×2 하드웨어 PCF, 노멀 오프셋은 거리에 비례(타일 텍셀 1.5개), 캐스터는 타일 절두체마다 컬링·인스턴싱. **라이트당 켜기/끄기** = Light `castShadows`(이제 점·스폿에도 보임). **단계별 개수**: 감자·저사양 0, 중사양 1(512), 고사양 4(512), 초고사양 8(1024) — 화면에 빛이 닿는 그림자 라이트 중 카메라에 가까운 순, 나머지는 그림자 없이 비춤, 아틀라스는 4096²를 넘지 않게 타일을 반으로 줄임. 기준판(ReferenceQuality)은 0. 품질 키 `localshadows`·`localshadowmap`, 통계 `local_shadows=`·`local_tiles=`, 예산 막대의 그림자 라이트 수에 포함. `BDG_SCREEN=render-shadows` 판정 확장: 해 없는 방에서 스폿 그림자 0.36(그림자 끔 1.07), 점광 +x면 0.20·−z면 0.20(끔 0.99), 열린 바닥 밝기 유지 1.00(여드름 없음), 고사양 2개·타일 7, 중사양 1개(1개 건너뜀), 저사양 0. 단위 테스트 확장(97개). 함께 고침: main에서 `EditorModel::ShowIfPasses`가 private이 되어 단위 테스트가 빌드되지 않던 문제. CI 28번 | (이 커밋) |
| 3-5c 품질 단계 자동화 | **텍스처 그룹**(언리얼 Texture LOD Group·유니티 mipmap limit식): 프리셋을 그룹으로(world·normal·masks·grayscale·ui), 단계별 최대 변 — 감자 512/256, 저사양 1024/512, 중사양 2048/1024, 고사양 4096, 초고사양 8192, UI는 줄이지 않음. 쿠킹된 DDS는 밉을 모두 가지므로 **올릴 때 맞는 첫 밉부터**(다시 쿠킹 없음), 단계를 바꾸면 영향받는 텍스처만 다음 사용 때 다시 올림(`MaterialLibrary::SetTextureLimits`, 씬·게임 뷰가 매 프레임 단계 값을 넘김) → 예산 막대의 텍스처 MB도 따라 줄어듦. 품질 키 `tex.world=1024` 등. **기기 측정 → 단계**: 새 `Render3D/Benchmark.*` — 상자 400개·해 그림자·점광 64개(그림자 4개)·하늘을 고사양 1920×1080으로 오프스크린 렌더, 제출~완성 시간을 3번 재서 중앙값(느린 기기는 프레임 수를 줄임). 단계별 상대 비용(감자 0.15·저 0.25·중 0.45·고 1·초고 1.6) × 측정값이 프레임 예산(60 Hz)의 60 % 안인 가장 높은 단계 → 프로젝트 범위로 자름. `ProjectSettings/Quality.json`의 `"default":"auto"` 또는 `BDG_QUALITY=auto`, 결과는 GPU·드라이버별로 `.cache/QualityBenchmark.json`에 저장해 다음부터 다시 재지 않음(RTX 5070 Ti 노트북 1.1~1.4 ms → 초고사양). **프레임 하락 시 한 단계 내림**: `QualityGovernor` — 최근 2초 평균이 예산의 1.2배를 넘으면(30프레임 이상) 한 단계 내리고 4초 쉼, 프로젝트 최소 단계 아래로는 안 감, 올리지는 않음; 자동 모드에서 씬 뷰가 GPU 시간으로 먹임, 배지 글자도 바뀜, 손으로 단계를 고르면 꺼짐. 플랫폼 예산 막대는 3-7에서 완료. 단위 테스트 확장(그룹 한도·밉 선택·덮어쓰기·측정→단계·캐시·가드·auto 파싱), `BDG_AUTOQ_TEST=1`(+ 빈 `BDG_ASSETS`): 측정·캐시·가드 2단계·그룹(감자 world 512/1024 normal 256/512 → 초고사양 전체, 메모리 4배) → `autoq test: bench=1 cached=1 tier=… governor=1 groups=1`, CI 추가. 공용 파일: Engine.cpp(BDG_QUALITY=auto·배지 콜백 3줄), MaterialLibrary | (이 커밋) |
| 3-8b 실내 복합 씬 | Sponza 대신 **직접 만든 회랑**(외부 파일 없음 → 출처·라이선스가 분명: CC0 1.0, `Tests/IndoorScene/LICENSE.md`). 생성기 `tools/make_indoor_scene.py`(기준 씬처럼 결과를 커밋, CI가 다시 만들어 비교): 수식으로 만든 텍스처 11장(벽돌·바닥 타일·회벽·나무의 색 `_D`·노멀 `_N`·ORM `_ORM`, 그림, 철제 난간 알파) — world·normal·masks 그룹을 모두 씀, 머티리얼 10개, 장면 `Cloister.scene` 74개: 10×30×6 m 회랑(Blockout 상자·원기둥으로 월드 단위 UV), 동쪽 벽 창 5개로 낮은 해, 기둥 12개, 랜턴 점광 6개(그림자), 그림을 비추는 스폿 2개, 알파 난간, 벤치, 반사 프로브·라이트 프로브 볼륨, 문 너머 별실(받침대 2개·금/구리 구·상자, 그림자 점광, 반사 프로브). 북마크 1~9(회랑·창 빛·그림·별실·난간·위에서·저사양·감자·초고사양). `BDG_INDOOR_TEST=1`: 머티리얼 10/10·텍스처 그룹 6/4/2, 조명 9개 중 고사양 그림자 4개(5개 건너뜀)·중사양 1개, 별실 뒷벽의 구·받침대 그림자 0.50(점광 그림자 끈 같은 화면 대비), 창 기둥 해 그림자 0.44(해 그림자 끈 화면 대비), 열린 곳은 1.00 그대로; 고사양 1032×552 장면 드로우 19·삼각형 1.7만·GPU 0.5 ms(RTX 5070 Ti 노트북) → CI 추가. 알게 된 한계: GI 프로브는 닫힌 방 안에서도 하늘을 환경광으로 받음(하늘 가림 없음) — 장면은 하늘 세기 0.4로 맞춤, 라이트맵/가림 보정은 GI 다음 단계. LOD 오버레이·`bdg render passes/stats`는 LOD·CLI 2차와 함께 | (이 커밋) |
| 3-9b 이미지 비교 회귀 CI | `BDG_RENDER_COMPARE=repo`(또는 `repo:<폴더>`): 북마크 1~9를 **480×270 고정 크기**로 렌더(패널 크기·화면 배율과 무관 → 어느 기기든 같은 크기) → 저장소 기준 `Tests/RenderBaselines/<렌더러>/<장면>/bookmark_N.png`와 FLIP 비교. 렌더러는 `GL_RENDERER`로 자동 구분(`software` = CI의 Mesa llvmpipe, `gpu-nvidia` = 기준 GPU, `gpu-amd`·`gpu-intel`) — 래스터 차이 때문에 세트를 섞지 않음, 허용치는 세트의 `meta.json`(기본 0.02). 기준이 없으면 실패가 아니라 `missing`으로 세고 그림을 `<out>/bookmark_N.png`로 내보냄, `BDG_COMPARE_BLESS=1`이면 저장소에 씀. 요약 줄에 `missing=… renderer=…`. 기준 GPU 세트(RTX 5070 Ti 노트북, 렌더 기준 씬 9장 + 실내 씬 9장, 2.6 MB, LFS 없이)를 커밋: 같은 빌드 오차 0.0000, 노출 1.3이면 6곳 실패(최대 0.26). CI: 두 장면을 매 커밋 software 세트와 비교(실패 = 오류), 그림·열지도를 **render-regression** 아티팩트로 올림 — software 세트는 CI에서만 만들 수 있으므로 **첫 CI 실행의 아티팩트를 `Tests/RenderBaselines/software/`에 넣어 커밋하면 비교가 시작됨**(그 전엔 missing 안내). 사용법 `Tests/RenderBaselines/README.md`. **GLES 3.0·WebGL2 인스턴싱 검토**([rendering-and-platforms](architecture/rendering-and-platforms.md) 새 절): 스토리지 버퍼 대신 데이터 텍스처(RGBA32F, 오브젝트당 11텍셀, `texelFetch`)가 기본안 — UBO 묶음은 16 KB 보장이라 구간당 93개로 쪼개짐, 인스턴스 속성은 17개로 한도 초과 | (이 커밋) |
| 3-3b 스냅·정렬·측정 | 새 `Runtime/Snapping.*`(계산만, 단위 테스트). **그리드 스냅**: 씬 뷰 **Snap 칩**이 꺼짐 → 0.1 → 0.25 → 0.5 → 1 → 2 m 순환(켜지면 늘 스냅, Ctrl은 한 드래그 동안 반대로), 월드 축 이동은 **좌표가 격자에 놓임**(이전: 이동량만 반올림), 로컬 축은 간격만큼, 회전·크기 간격도 설정값. **꼭짓점 스냅**: 이동 중 **V**를 누르면 잡은 곳에서 가장 가까운 선택 물체의 꼭짓점을 마우스 아래 다른 물체의 꼭짓점(24 UI 단위 안)에 맞춤. **표면 스냅**: 이동 중 **Ctrl+Shift**면 마우스 아래 표면에 물체가 닿게 얹음(법선 방향으로 경계 상자만큼 띄움). 스냅 대상은 노란 표시. **정렬·간격**: 둘 이상 선택하면 **Align 칩** → 축별 min·mid·max·dist 칩(선택 전체의 경계 기준, dist는 바깥 둘 고정·간격 같게), 한 번 = 되돌리기 한 번, 자식은 선택된 부모가 옮김. **측정 도구**(M, 두 프리셋 모두): 표면 두 점 사이를 끌어 자(노란 선 + `3.00 m · x y z`), V로 꼭짓점, 아무것도 없으면 바닥면, Esc로 지움, 로그 `measure:`. 레이캐스트·꼭짓점 찾기는 렌더 이후 움직인 물체도 현재 위치로 계산(한 프레임에 이벤트가 여럿 와도 맞음). 단위 테스트 1개(총 71개·822개), `BDG_SNAP_TEST=1` 6개 판정, CI 추가 | `92190eb` |
| 3-3c 그레이박스(블록아웃) | 새 `MakeBlock`(`Render3D/MeshGen.*`, 계산만): **상자·계단·경사로·원기둥**, 기준점은 **바닥 가운데**, 면마다 평평한 법선, **월드 크기 UV**(1 m = 텍스처 한 번 반복)라 크기가 달라도 격자가 고르게. 계단은 앞(+Z)에서 뒤(-Z)로 올라감(단 수 1~64), 경사로도 -Z로 올라감. 새 **`Blockout` 컴포넌트**(모양·크기·단 수·재질·충돌체 표시) — 재질이 비면 절차적 **회색 1 m 격자 텍스처**(25 cm 잔선). 씬 뷰가 모양·크기별 메시를 캐시하고 한 프레임 안 쓰인 것은 지움. **Blockout 도구(B, 두 프리셋)**: `+ Box`·`+ Stairs`·`+ Ramp`·`+ Cylinder` 칩이 화면 가운데가 보는 표면 위(격자에 맞춤)에 만들고 선택(되돌리기 한 번). 선택한 블록의 **면 6개에 손잡이**(축 색, 마우스 아래 노랑): 끌면 그 면만 밀고 당김 — 반대쪽 면은 그대로, 그리드 스냅이면 면이 격자에 놓임, Esc 취소, 한 드래그 = 되돌리기 한 번(`set Blockout.size` + `Transform.position`). 피킹·레이캐스트·스냅도 블록에 동작. 충돌체 표시는 물리(M6)가 쓸 값으로 저장만. 단위 테스트 1개(네 모양 모두 닫힌 메시·바닥 가운데 기준 경계, 계단 디딤판 높이, 월드 UV; 총 72개·837개), `BDG_BLOCKOUT_TEST=1` 7개 판정(만들기·+X 밀기·위로 당기기·Esc·되돌리기·모양 3개·피킹), CI 추가 | `30b9b06` |
| 테스트 창 숨김 | 자동 실행(`--frames N`, `BDG_IDLE_TEST`)은 **창을 숨기고 화면 밖 프레임 버퍼**(RGBA8 + 깊이, 창 크기)에 그림: 테스트가 창을 띄우거나 포커스를 빼앗지 않고, 책상의 마우스·키 입력도 섞이지 않음(유휴 테스트가 가끔 실패하던 원인). 스크린샷·픽셀 판정은 같은 그림(창 모드와 픽셀 차이 0). `BDG_SHOW_WINDOW=1`이면 보이게, `BDG_HIDDEN=1`이면 어떤 실행이든 숨김. RHI `SetOffscreenFrame`, `Window::Create(..., hidden)`. 전체 스모크 40개 실행이 숨김으로 통과, 유휴 테스트 3프레임·마우스 0 | `974aad6` |
| 에디터 아이콘 세트 | **PhotoCraft**(헤드리스 CLI)로 그린 64×64 아이콘 40개: 도구 8(선택·이동·회전·크기·보기·측정·블록아웃·스냅), 컴포넌트 13(Transform·메시·라이트·해·카메라·북마크·반사 프로브·라이트 프로브·하늘·컬러 그레이딩·블록아웃·스크립트·기본), 계층 3(씬·프리팹·빈 오브젝트), 폴더 2·파일 14(씬·머티리얼·인스턴스·텍스처·모델·C#·.H·C++·프리팹·오디오·LUT·JSON·셰이더·일반). 원본은 **`tools/make_icons.py`**(아이콘마다 벡터 도형·그라디언트·글자 명령 목록, 디자인 토큰 색, 64 격자·내용 4..60·5 px 둥근 선) → `Assets/Icons/*.png`, `--sheet`로 모음 이미지. 편집기: 시작 때 **아틀라스 한 장**(8열, 칸마다 4 px 여백, 투명 픽셀로 색을 번지게 해 밉맵 가장자리 깨끗, 576×360)으로 묶고 **Batch2D 전용 슬롯**(binding 2, 모드 4)으로 그려서 **UI 그리기 호출 1회 유지**(`ui_draws=1`). `UIContext::Icon(이름, x, y, 크기)` — 없으면 false라 예전 도형으로 그림. 쓰는 곳: 계층 행(종류별), 인스펙터 컴포넌트 머리(이름별, 언어 배지가 있으면 스크립트), 프로젝트 패널 폴더·파일(확장자별; Lua·Python·그래프는 언어 띠 유지), 씬 뷰 도구·스냅 칩. 디자인 비교 화면(`parity:`)은 아이콘 없이. `BDG_ICONS=<폴더>`로 다른 세트 | (이 커밋) |
| 3-6 플레이 모드 | **Play**(Ctrl+P)는 편집 중인 장면을 통째로 저장해 두고 **명령 기록에 새 층**을 쌓은 뒤 장면을 '플레이 월드'로 실행: 씬 뷰·계층·인스펙터·기즈모가 그대로 동작하고 Ctrl+Z는 실행 중에 한 편집만 되돌림, 실행 중 명령은 세션 로그에서 빠짐. **Stop**은 편집 중인 장면과 그 되돌리기 기록을 그대로 복원(같은 id). 실행 중 사용자가 바꾼 값(되돌리지 않은 `set`, 필드마다 마지막 값)은 정지 뒤 **배너로 '유지 / 버리기'** — 유지하면 편집 장면에 명령 그룹(되돌리기 한 번)으로 반영, 게임이 스스로 바꾼 값·실행 중 만든 물체는 남기지 않음. 첫 내장 동작 **`Rotator`** 컴포넌트(초당 회전, M4 스크립트 전까지). **Game 뷰**: 장면의 첫 Camera 시점으로 렌더(편집 표시 없음, 실행 시간 배지), 실행하면 Game 탭, 정지하면 Scene 탭. 기본 장면 Main Camera가 장면을 보도록 회전. 명령 버스 `PushLayer/PopLayer/LayerCommands` + 단위 테스트(총 73개·851개). `BDG_PLAY_TEST=1` 7개 판정, CI 추가 | (이 커밋) |
---

## M0 — 남은 것

(없음 — M0 완료)

## M1 — 에디터 골격 (실제로 편집되는 에디터) — 완료

> 순서가 중요합니다: 1-2(리플렉션) → 1-3(명령 버스) → 1-4(핸들)는 이후 모든 기능(씬, 스크립팅, 네트워크, 공동 편집)의 바탕입니다.

---

## M2 — 씬과 에셋 (실제 데이터를 다룸) — 남은 항목은 M3 3D 핵심(3-0~3-4) 다음으로 (사용자 결정 2026-11-23)

  - 완료 조건: 저장 → 다시 열기 → 다시 저장 시 파일이 바이트 단위로 같음.
- [x] **2-5b. 참조 찾기·안 쓰는 에셋 찾기·찾아 바꾸기** — `bdg refs`, `bdg unused`, 지우기 전 경고, 참조 일괄 교체, 일괄 이름 바꾸기 [convenience-features](planning/convenience-features.md)
- [x] **2-5c. 테마 편집기** — 토큰 색상 선택기, 실시간 반영, 프로젝트별 테마 [convenience-features](planning/convenience-features.md)
- [x] **2-5d. 끌어다 놓기 임포트·자동 다시 가져오기·에셋 규칙 검사** (`.blend` 직접 가져오기는 M3) [convenience-features](planning/convenience-features.md)
- [x] **2-6. 새 프로젝트 기본 3D 씬** — Main Camera, Sun, Sky, World Settings, Ground ([projects-and-templates](features/projects-and-templates.md)).
- [x] **2-7. CLI `bdg` 1차** — `bdg view/query/set/add/del/validate` 를 같은 명령 버스로 (에디터 없이 헤드리스 실행). AI가 쓰는 첫 경로.

## M3 — 3D 렌더링과 플레이 모드 — **지금 진행** (3-0 → 3-1 → 3-2 → 3-3 → 3-4)

- [x] **3-2b. Project 패널 썸네일 표시** (★ E-1에서 완료) — 2-5t 캐시(64×64 PNG)를 텍스처로 올려 Project 패널 타일에 그림(Batch2D 이미지 모드 사용). 모델 썸네일은 Renderer3D로 오프스크린 렌더 후 같은 캐시에
- [x] **3-3b. 스냅·정렬·측정** (2026-11-24) — 그리드·꼭짓점·표면 스냅, 정렬·간격 맞추기, 거리 측정 [convenience-features](planning/convenience-features.md)
- [x] **3-3c. 그레이박스(블록아웃) 도구** — 상자·계단·경사로, 면 밀고 당기기, 메시+충돌체 [convenience-features](planning/convenience-features.md) (2026-11-24)
- [x] **3-4b. 점·스폿광 그림자** (2026-11-25) — 스폿광 그림자 맵, 점광 큐브 그림자(6면), 라이트당 켜기/끄기, 그림자 아틀라스. 3-5 고사양(CSM 3)과 함께 검토
- [x] **3-5b. 고사양 단계 확장** — CSM 3단 + 소프트 그림자, SSAO, Forward+ 클러스터 라이트, 컬러 그레이딩(LUT), 저사양 블롭 그림자, 이방성 16x(초고사양), 텍스처 그룹별 최대 해상도(쿠킹 연동), 기기 성능 측정으로 단계 자동 선택·프레임 하락 시 한 단계 내림, 플랫폼 예산 막대 (2026-11-24, A~D)
- [x] **3-5c. 품질 단계 자동화** (2026-11-25) — 텍스처 그룹별 최대 해상도(쿠킹 연동), 기기 성능 측정으로 단계 자동 선택·프레임 하락 시 한 단계 내림, 플랫폼 예산 막대
- [x] **3-6. 플레이 모드** (2026-11-24) — 복사본 월드에서 실행, 정지 시 원래대로, "이 변경 유지" 선택 반영(명령으로).
- [x] **3-6b. 플레이 모드 도구** (2026-11-24) — 실행 중 테두리 색, 시간 배율, 한 프레임 진행 [convenience-features](planning/convenience-features.md)
- [x] **3-7. 편집 중 플랫폼 예산 표시** (2026-11-24) — Draw·삼각형·텍스처·라이트 막대.
- [x] **3-8b. 실내 복합 씬 (Sponza 대체)** (2026-11-25) — 라이선스가 분명한 실내 장면(CC0/CC-BY 확인, 예: 직접 만든 회랑·Intel 샘플 라이선스 검토)으로 텍스처 로딩·다중 라이트·그림자 품질. LOD 생기면 LOD 오버레이, `bdg render passes/stats`는 2-7 CLI와 함께
- [x] **3-9b. 이미지 비교 회귀 CI (1.0)** (2026-11-25) — 기준 GPU 1대 + 소프트웨어 렌더러로 북마크 기준 이미지를 저장소(LFS 등)에 두고 매 커밋 비교, GLES 3.0/WebGL2 백엔드용 인스턴싱 경로(스토리지 버퍼 대신 UBO 묶음 또는 텍스처) 검토

## M4 — C# 스크립팅

- [x] **4-1. .NET 8 호스팅** — hostfxr로 런타임 로드, C API(`bdg_api.h`)를 P/Invoke로 노출하는 생성 코드(리플렉션에서). (2026-11-25: P/Invoke 대신 JSON 요청 함수 포인터 하나 + 리플렉션에서 C# 클래스 생성 — decisions 참고)
- [x] **4-2. `Behaviour` 기본 클래스 + 생명주기** — `Awake/Start/Update/FixedUpdate/LateUpdate/OnEnable/OnDisable/OnDestroy`, 프레임 순서는 [interop](architecture/interop.md).
- [x] **4-3. `[Edit]` 노출** — C# 리플렉션 → 엔진 리플렉션 등록 → Inspector 자동 표시 ([code-syntax](features/code-syntax.md) 속성 표).
- [x] **4-4. 핫 리로드** — 수집 가능한 AssemblyLoadContext, 상태 직렬화 후 재적용. 목표: 변경 → 반영 2초.
- [ ] **4-6. 빠른 C++ 반복** — 엔진 SDK(헤더+라이브러리) 배포 형태, 얇은 게임용 헤더, 헤더 도구 증분, 작은 모듈·PCH·유니티 빌드 자동, 모듈 DLL 라이브 리로드(실패 시 이전 DLL 복귀), sccache 옵션. 목표: `.cpp` 수정 반영 5초, 클린 빌드 60초, CI 측정 [pain-points](planning/pain-points.md)
- [ ] **4-6b. 기다리기(코루틴) — C++20 `BTask`·C# `async`·Lua `wait`** — `Wait`/`WaitUntil`/`NextFrame`/`WaitForEvent`/`RunOnWorker`, 오브젝트·씬·플레이 모드에 묶여 자동 취소, 시간 배율 반영. 완료 조건: 오브젝트를 지운 뒤 대기 중 코루틴이 크래시 없이 취소되는 테스트 [pain-points](planning/pain-points.md)
- [ ] **4-6c. 멀티스레드 게임 코드** — `RunOnWorker`, `Jobs.ParallelFor`, `[Parallel] Update`, 잡 의존성, 워커에서 게임 스레드 전용 API 호출 시 오류(스레드 검사), 순서 섞기 디버그 모드, 프로파일러 스레드 타임라인. 완료 조건: 총알 1만 개 `[Parallel]` Update가 단일 스레드보다 코어 수에 비례해 빨라지고, 워커에서 Transform 쓰기를 하면 크래시 없이 오류 [scripting](architecture/scripting.md#멀티스레드-2026-11-23)
- [ ] **4-4b. 디버그 그리기 API + 콘솔 개선** — `Debug.Draw*`(모든 언어), 오류 클릭 → 코드 줄·Graph 노드로 이동, 같은 메시지 묶기, 필터·검색 [convenience-features](planning/convenience-features.md)
- [x] **4-4c. 쉬운 API (C#)** — `Get`/`Find`/`Spawn`/`Destroy`/`After`/`Every`/`await Wait`/`Tween`/`Random`, `[Auto]`, 태그·액션·씬 상수 자동 생성 [easy-api](features/easy-api.md)
- [x] **4-4d. 데이터 테이블** — `.table.jsonl`, 스프레드시트 편집, CSV, `Data.Get<T>` [convenience-features](planning/convenience-features.md)
- [x] **4-5. 디버깅** — DAP 연결 문서화 (VS Code / Rider).

## M5 — Graph·Lua·Python

- [ ] **5-1. Graph 텍스트 형식 + VM** — [visual-scripting](features/visual-scripting.md), JSONL 저장, 식 노드(`"in":{"y":"speed*dt"}`).
- [ ] **5-1b. 열다가 튕기지 않는 Graph** — 불러오기 전 구조 검사(깨진 노드는 빨갛게 표시하고 나머지는 열기), VM 명령 수·호출 깊이·메모리 제한, `bdg validate --fix`, 에셋을 열다 크래시 나면 다음 실행에서 그 에셋 격리(안전 모드). 완료 조건: 무한 루프·재귀·순환 참조·깨진 연결 그래프를 열어도 에디터가 살아 있음 [pain-points](planning/pain-points.md)
- [ ] **5-2b. 머티리얼 그래프 에디터** — 그래프 에디터 공통 화면 재사용, 출력 노드·파라미터 노드·미리보기, 셰이더 캐시, 순열 수 표시
- [ ] **5-2. 그래프 에디터** — 노드 그래프 위젯(줌·팬·박스 선택·와이어·미니맵), 목표: 노드 5천 개 60fps.
- [ ] **5-3. Lua(Luau)** — 샌드박스, `Behaviour{}` 선언, 핫 리로드, 타입 정의 자동 생성.
- [ ] **5-3b. 쉬운 API를 Lua·Graph·Python에 노출** — 생성 코드, 언어 간 이름 일치 검사 CI [easy-api](features/easy-api.md)
- [ ] **5-4. Python 에디터 도구** — CPython 임베드, `@bdg.menu`, `@bdg.validator`, `bdg.undo()`가 명령 버스와 연결.

## M6 — 게임 기능

- [ ] **6-1. 프리팹·오버라이드** — `@g1/hpbar` 경로, 오버라이드 표시(파란 막대), 되돌리기.
- [ ] **6-2. 물리** — PHI 추상 계층 + Jolt(3D) + Box2D(2D), `Rigidbody`·`Collider`·`PhysicsMaterial`, "물리 추가" 원클릭, Rigidbody별 Simulation 백엔드 선택, Collider Auto(정적 메시·볼록 분해·뼈 캡슐) + 충돌 오차 표시 ([physics](architecture/physics.md#컴포넌트와-사용법-2026-11-23-확정))
- [ ] **6-3. 오디오** — miniaudio, 3D 소리, 믹서.
- [ ] **6-4. 입력 액션** — [input](features/input.md): `Input/Actions.jsonl`, 매핑 컨텍스트, 패드.
- [ ] **6-5. MCP 서버 + AGENTS.md 자동 생성** — [agent-integration](features/agent-integration.md).
- [ ] **6-5b. 세이브·로드, 이벤트, Raycast·Overlap·Audio·Vfx 쉬운 API** [easy-api](features/easy-api.md)
- [ ] **6-VFX. 파티클 P-1~P-5** — CPU 시뮬·모듈 스택·`.vfx` → 편집기 → GPU 경로 → 메시·리본·라이트·이벤트 → Graph 커스텀 모듈 ([particles](features/particles.md), 설계 검토 후 시작)
- [ ] **6-5c. 스플라인 도구** [convenience-features](planning/convenience-features.md)
- [ ] **6-6. 에디터 안 AI 패널** — 요청 → diff 미리 보기 → 승인 시 명령 버스로 적용, 콘솔 오류 "AI에게 설명", AI 끄기·사용 기록 [convenience-features](planning/convenience-features.md)

## M7 — 애니메이션·UI 디자이너·빌드

- [ ] **7-1. 애니메이션 그래프** — Graph 상위호환 ([animation](features/animation.md)): 포즈 그래프, 상태 머신, 노티파이, 워커 스레드 평가.
- [ ] **7-1b. IK·래그돌·스프링 본** — Two-Bone IK, Look-At/Aim Offset, 발 IK(경사·계단), FABRIK/CCD, 래그돌 자동 생성·편집·죽을 때 전환, 머리카락·옷자락 스프링 본, 디버그 겹쳐 보기 [animation](features/animation.md#ik와-물리-애니메이션-2026-11-23-보강)
- [ ] **7-2. 타임라인 (블렌더식)** — 키프레임·커브 편집.
- [ ] **7-3. UI 위젯 디자이너** — 런타임 게임 UI(월드·카메라 공간) ([ui-widgets](features/ui-widgets.md)).
- [ ] **7-3b. 실행 중 상태 디버거·메모리 프로파일러** [convenience-features](planning/convenience-features.md)
- [ ] **7-4b. 빌드 실패를 설명하는 빌드** — `bdg build --check` 사전 검사, 단계·에셋·원인·고치는 법을 한 줄로, 오류를 끝까지 모아 한 번에 표시. 완료 조건: 일부러 깨뜨린 에셋 10종이 모두 원인과 함께 보고됨 [pain-points](planning/pain-points.md)
- [ ] **7-4. 빌드·패키징** — `bdg build`, 쿠킹, `.bpak`, `build-report.txt` ([libraries-and-packaging](architecture/libraries-and-packaging.md)).

## 1.0 준비

- [ ] **허브·시작 화면·엔진 버전 관리** ([projects-and-templates](features/projects-and-templates.md))
- [ ] **템플릿**: 빈, 기본 3D·2D, 1인칭 슈팅, 3인칭, 2D 플랫포머
- [ ] **Android·Web** (GLES 3.0 / WebGL2 백엔드, Emscripten)
- [ ] **Halo GI Tier 0** — 라이트맵 + 프로브 + 반사 프로브 ([global-illumination](features/global-illumination.md))
- [ ] **애니메이션 보강** — 손 IK·IK 리그 편집기, 부분 래그돌·히트 리액션 블렌드 [animation](features/animation.md#ik와-물리-애니메이션-2026-11-23-보강)
- [ ] **패키지 시스템** — `bdgpkg.json`, `Packages/manifest.json`·`lock.json`, git 출처
- [ ] **디자인 일치 검사 CI**, 성능 회귀 CI
- [ ] **게임 기본 기능** — 설정 메뉴(품질·해상도·키 재설정·음량·언어·자막), 게임 접근성(자막·색각 필터·글자 크기), 원클릭 빌드 프로필, CI 템플릿 [convenience-features](planning/convenience-features.md)
- [ ] **편의 도구 묶음** — git 상태 표시, 모바일 기기 미리보기, 녹화·캡처(필름크래프트 내보내기), `bdg doctor` 상태 점검 화면, 첫 실행 튜토리얼·"?" 문서 버튼 [convenience-features](planning/convenience-features.md)

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
- [-] 전신 IK, 물리 애니메이션(Active Ragdoll)·일어나기, 천·머리카락 시뮬레이션, 모션 매칭 (1.0 이후 / L 트랙) [animation](features/animation.md#ik와-물리-애니메이션-2026-11-23-보강)
- [-] 물리 백엔드 패키지: PhysX 5(1순위) → Rapier → Bullet, 공통 테스트 장면·`bdg physics bench` 비교 보고서 (1.0 이후) [physics](architecture/physics.md#지원할-물리-엔진-목록-2026-11-23)
- [-] 원클릭 클라우드 호스팅 (엔진 범위 밖, 선택형 서비스로 나중에 검토)
