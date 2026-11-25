# 게임 빌드·패키징 (7-4, 7-4b)

작성 2026-11-25. 에디터에서 만든 게임을 **남에게 줄 수 있는 폴더(와 zip)**로 만듭니다. 설계 배경은 [libraries-and-packaging §4](../architecture/libraries-and-packaging.md#4-게임-빌드-패키징-배포-파일-만들기).

## 쓰는 법

```powershell
cd 내프로젝트                    # ProjectSettings/, Scenes/, Assets/, Data/ 가 있는 폴더
bdg build --check                 # 사전 검사만: 문제를 모두 모아 한 줄씩 (아무것도 쓰지 않음)
bdg build                         # 검사 → 빌드
bdg build --config Shipping --zip --out D:\Release\MyGame
bdg build --scripts Scripts\bin\Debug\net8.0\MyGame.dll   # 스크립트 dll을 직접 줄 때 (기본: .cache/Scripts/*.dll)
```

결과(기본 `Builds/Windows-<구성>/`):

| 파일 | 내용 |
|---|---|
| `<게임 이름>.exe` | 게임 실행 파일(플레이어). `packaging.executableName`(비우면 `description.name`), 아이콘은 `description.icon`(PNG → 256 px로 exe에 넣음) |
| `Content/game.bpak` | 묶은 게임 데이터: 실제로 쓰는 설정(`ProjectSettings/Project.json`), 빌드에 넣는 장면, 그 장면이 쓰는 에셋과 `.meta`, 쿠킹한 텍스처(`.cache/cooked`), `Data/` 표. zstd 압축(`packaging.compressAssets`) |
| `Managed/`, `Scripts/*.dll` | C# 스크립트가 있을 때만: 엔진의 C# 연결 어셈블리와 게임 스크립트(.pdb는 Shipping + `debugSymbols` 끔이면 뺌) |
| `THIRD-PARTY-NOTICES.txt` | 엔진과 외부 라이브러리 고지문 (배포 의무) |
| `build-report.txt` | 장면·스크립트·파일별 크기(묶은 크기 순)·단계별 시간·빼 놓은 에셋·경고 |
| `../<이름>-<버전>-<구성>.zip` | `packaging.zipBuild` 또는 `--zip` |

같은 출력 폴더에 다시 빌드하면 이전 빌드를 통째로 바꿉니다. **이전 빌드가 아닌 비어 있지 않은 폴더는 거부**합니다(다른 파일을 지우지 않게).

## 단계

1. **검사**(7-4b) — 아래 표. 오류가 하나라도 있으면 아무것도 쓰지 않고 멈춤.
2. **수집** — 시작 장면(`maps.gameStartScene`) + `maps.scenesInBuild`(`;`로 구분, 비우면 `Scenes/`의 모든 장면), 장면이 쓰는 에셋(참조를 따라감: 장면 → 머티리얼 → 텍스처). `packaging.stripUnusedAssets`면 아무도 쓰지 않는 에셋은 빼고 보고서에 적음, `excludeEditorContent`면 `.cs`·`.py`·`.cpp` 같은 에디터 전용 파일을 뺌.
3. **쿠킹** — 쓰는 텍스처를 BC7/BC5 등으로 변환(해시 캐시라 바뀐 것만), 결과 DDS를 같이 묶음.
4. **스크립트** — C# 어셈블리 복사. 컴파일은 IDE/`dotnet build`가 하고(4-4), 빌드는 그 결과를 씀.
5. **묶기** — `.bpak`: 헤더 + 파일마다 zstd(작아질 때만) + 색인(경로·크기·FNV-1a 해시). 암호화가 아니라 묶음입니다(누구나 열 수 있음).
6. **출력·보고서** — 플레이어 복사·아이콘, 고지문, 보고서, zip.

## 사전 검사가 알려 주는 것 (7-4b)

한 줄 형식: `[오류|경고] 단계 · 대상 — 원인 → 고치는 법`. 오류를 끝까지 모아 한 번에 보여 줍니다.

| 단계 | 문제 |
|---|---|
| 설정 | `Project.json`을 읽을 수 없음, 없음(경고: 기본값), `ValidateProjectSettings`의 모든 문제(버전 형식, 식별자, 회사, 플랫폼, 시작 장면, 모바일 아이콘, 기본 언어), Windows가 대상이 아님, 알 수 없는 `--config`, 실행 파일 이름에 쓸 수 없는 글자·예약 이름 |
| 아이콘 | 파일 없음, 이미지가 아님, 정사각형이 아님(경고), 256 px 미만(경고), 아이콘 없음(경고) |
| 출력 | 출력 폴더가 프로젝트 자체·`Assets`/`Scenes`/`ProjectSettings`/`Data` 안, 이전 빌드가 아닌 비어 있지 않은 폴더, 플레이어 실행 파일 없음 |
| 스크립트 | `--scripts`의 파일 없음, C# 런타임을 시작할 수 없음, 어셈블리를 불러올 수 없음, C#을 쓰는데 `Bidulgi.Scripting.dll` 없음, 플레이어 PC에 .NET 런타임 필요(경고) |
| 장면 | 시작 장면·빌드 목록의 장면 파일 없음, 읽을 수 없는 장면(줄 번호), 아무 코드도 모르는 컴포넌트(C# 클래스 철자·빌드 누락), 시작 장면에 Camera 없음 |
| 에셋 | `MeshRenderer.mesh`가 프로젝트에 없음, 머티리얼이 없음(경고), 머티리얼이 가리키는 텍스처가 없음, 짝 잃은 `.meta`(경고), 쿠킹 실패 |
| 데이터 | `Data/` 표를 읽을 수 없음, 표의 문제 줄(경고) |

단위 테스트가 일부러 깨뜨린 프로젝트(버전·식별자·exe 이름·아이콘·출력 폴더·없는 장면·깨진 장면·모르는 컴포넌트·없는 메시·카메라 없음·없는 텍스처·없는 스크립트 dll + 데이터 표 경고)로 **12가지 오류가 모두 원인·고치는 법과 함께** 나오는지 확인합니다.

## 플레이어 (게임 실행 파일)

`BidulgiPlayer.exe`(엔진과 함께 빌드, `bdg.exe` 옆)를 빌드가 게임 이름으로 복사합니다. 에디터 UI 없이:

- `Content/game.bpak`을 `%LOCALAPPDATA%/<회사>/<게임>/Content`에 **한 번 풀고**(바뀐 파일만 다시 씀, 묶음이 그대로면 바로 시작) 그 폴더에서 읽습니다. 지금 엔진의 로더들이 파일을 직접 읽기 때문에 고른 방법이고, 묶음에서 바로 읽기(가상 파일 시스템)는 다음 단계입니다.
- 창: `display.width/height`, `windowMode`(창·테두리 없는 전체 화면·전체 화면), `resizable`, `vsync`, `frameLimit`, `showCursor`, 제목 = 게임 이름, 아이콘 = exe 아이콘. 품질 단계 = `display.startQuality`(Auto는 지금 Medium).
- C# 스크립트: `Managed/`와 `Scripts/*.dll`을 장면보다 먼저 불러옴. 고정 스텝 = `physics.fixedTimestep`/`maxFixedSteps`.
- 시작 장면을 열고 바로 플레이 모드(에디터의 Play와 같은 코드: C# Behaviour, 기본 동작, 카메라 흔들림), 첫 Camera로 화면 전체에 그림.
- 로그: exe 옆 `Bidulgi.log`. 문제가 있으면 메시지 상자로 알려 줌(데이터 없음, OpenGL 4.5 안 됨, .NET 없음, 시작 장면 없음).
- 개발용: `--frames N`(N프레임 뒤 종료, 숨은 창, 게임 시간 1/60초 고정), `--project <폴더>`(묶지 않고 프로젝트 폴더를 바로 실행), `BDG_SCREENSHOT=파일.png`(마지막 화면), `BDG_PLAYER_CACHE`(푸는 곳), `BDG_PLAYER_QUIET=1`(메시지 상자 없이 로그만).

## 자동 검사

- 단위 테스트(`Tests/BuildTests.cpp`): `.bpak` 왕복·압축·해시·손상 감지·경로 검사, 작은 프로젝트 빌드(폴더·묶음·보고서·zip·안 쓰는 에셋 빼기·다시 빌드·남의 폴더 거부), 깨진 프로젝트 사전 검사.
- CI: `bdg new`로 만든 프로젝트에 C# `Spinner`·`LifecycleProbe`를 붙여 `bdg build --zip` → 만든 게임을 `--frames 90`으로 실행 → `player: 90 frames, 1.50 s of game time, 2 C# behaviours, camera yes`와 `probe: box ... update=90`.

## 남은 것

- 묶음에서 바로 읽기(가상 파일 시스템), 바이너리 장면, DLC 청크·패치.
- exe의 버전 정보(파일 속성의 회사·버전·저작권) 리소스, 시작 화면(`description.splash`).
- C# 런타임 자체 포함(지금은 플레이어 PC에 .NET 8 이상 필요), 스크립트 컴파일을 빌드가 직접.
- 에디터 메뉴 "빌드"(지금은 `bdg build`), Linux·Android·Web 출력.
