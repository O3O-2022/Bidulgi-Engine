# AI 에이전트 연동

이 엔진의 정체성입니다. 목표: **AI가 유니티보다 훨씬 적은 토큰으로 엔진을 다루고, 결과를 스스로 확인할 수 있게.**

## 토큰 절약 전략

| 전략 | 내용 |
|---|---|
| 사전 지식 재활용 | 유니티와 같은 API·개념 → 문서를 컨텍스트에 넣을 필요가 없음 (가장 큰 절약) |
| 기본값 생략 | 씬·설정·UI 파일에 기본값과 다른 값만 저장 |
| 작은 파일 | 씬 레이어 분할, 기능별 파일 |
| 읽기 = 쿼리 | 파일 전체 대신 필요한 필드만 (`query`, `outline`) |
| 쓰기 = 패치 | 파일을 다시 출력하지 않고 명령 한 줄 (출력 토큰이 가장 비쌈) |
| 압축 표기 | 저장은 JSON, 보기는 `view` 압축 표기 |
| 압축 스키마 | JSON Schema 대신 한 줄 시그니처 (`schema`) |
| 간결한 에러 | `파일:줄 경로 → 원인 → 고칠 방법` 한 줄 |
| 텍스트 우선 검증 | 숫자 로그 먼저, 스크린샷은 필요할 때만 (기본 512px) |
| Python 일괄 처리 | 반복 작업은 명령 수백 개 대신 짧은 스크립트 하나 |

## CLI (`bdg`)

> 엔진 이름(비둘기 엔진)에서 따온 이름입니다. UI 시안 v0.1에는 아직 `forge`로 표시됩니다.

### 읽기
```
bdg outline forest_01                    → 트리 요약 (이름, 컴포넌트 이니셜)
bdg view forest_01                       → 압축 표기
bdg query "Enemies/*" -f pos,Health.hp   → 필요한 필드만 표로
bdg schema [Health]                      → 컴포넌트 시그니처
bdg config get rendering                 → 설정 값과 출처
```

### 쓰기
```
bdg set g1 Health.hp=20 pos=6,0,4
bdg add g3 --prefab goblin --parent Enemies --pos 4,0,5
bdg dup g1 x5 --spread 2
bdg del g2
bdg addcomp crate Rigidbody
bdg config set rendering.shadowCascades=2 --platform Android
bdg undo
```

### 검증과 실행
```
bdg validate                             → 스키마·참조 검사, 사람이 읽을 수 있는 에러
bdg render forest_01 --cam main -o shot.png --size 512
bdg sim forest_01 --frames 300 --watch g1.pos --log
bdg test                                 → 헤드리스 자동 테스트
bdg build --platform Android
```

## 라이브 에디터 연동 (MCP 서버)

에디터를 켜 둔 채로 AI가 조작합니다. "숲에 고블린 5마리 배치해 줘" → Hierarchy에 실시간으로 나타나고 Ctrl+Z 한 번으로 되돌림.

| 도구 | 기능 |
|---|---|
| `scene_view`, `scene_query` | 씬 읽기 |
| `run_command` | CLI와 같은 명령 실행 |
| `screenshot_viewport` | 현재 뷰포트 캡처 |
| `play`, `stop`, `read_console` | Play 모드와 로그 |
| `get_selection` | 사용자가 선택한 오브젝트 (사람과 AI의 협업 기준점) |

## 안전장치

- 에이전트 명령은 하나의 Undo 그룹으로 묶임
- 승인 정책 (프로젝트 설정 → Agent): `Never` / `Destructive Only` / `Always`
- 명령당 최대 변경 수 제한 (기본 500)
- Agent 탭에서 출처(UI / 터미널 / 에이전트)별 기록 확인

## 에러 메시지 형식

```
scenes/forest_01.scene:12 g1.Health.hpp → Health에 hpp 필드가 없습니다. 가능한 필드: hp, max
```

## 이후 추가

- 씬 diff·병합 도구 (여러 에이전트 동시 작업)
- 스크린샷 기반 시각 회귀 테스트
- `bdg explain g1` — 이 오브젝트가 왜 이렇게 동작하는지 (붙은 스크립트, 영향 주는 설정)

## 추가 기능 (2026-11-20 채택)

### 진단과 자연어 검색
```
bdg diagnose visibility g1           → 화면에 안 보이는 이유를 순서대로 (에디터 진단 버튼과 같은 검사)
bdg diagnose motion g1               → 안 움직이는 이유
bdg query --nl "체력 50 이하 고블린"   → 자연어를 쿼리로 바꾸고, 바꾼 쿼리와 결과를 함께 출력
```
상세: [editor-ui](editor-ui.md#편의-기능-2026-11-20-채택)

### 플레이 녹화 → 자동 테스트
Play 중 입력과 주요 결과를 녹화해 테스트로 저장합니다. 고친 뒤 같은 플레이를 헤드리스로 다시 돌려 결과를 비교합니다.

```
bdg record start --name boss_fight      (또는 에디터 Play 툴바의 ● 녹화)
bdg record stop                         → tests/play/boss_fight.playtest 저장
bdg test tests/play/boss_fight.playtest → 재생 후 비교 결과를 텍스트로
```

```jsonc
// tests/play/boss_fight.playtest
{"v":1, "scene":"scenes/boss.scene", "seed":4211, "fixedStep":0.02,
 "inputs":"boss_fight.inputs",                      // 프레임별 입력 (바이너리, 자동 생성)
 "checks":[
   {"at":"end",   "expect":"@boss.Health.hp == 0"},
   {"at":"end",   "expect":"@player.Health.hp >= 30", "note":"녹화 당시 42"},
   {"at":"12.5s", "expect":"@player.pos.y > 2", "tolerance":0.1},
   {"at":"any",   "expect":"no errors in console"}]}
```

- 녹화 때 체크 항목을 자동으로 제안합니다 (끝난 시점의 체력, 위치, 콘솔 에러 없음). 사람이나 AI가 고쳐서 확정합니다.
- 결정론 조건: 고정 타임스텝, 시드 고정, 같은 플랫폼. 조건이 깨지면 테스트가 "재현 불가"로 표시됩니다.
- AI가 코드를 고친 뒤 관련 플레이 테스트를 자동으로 돌려 확인할 수 있습니다 (AGENTS.md에 규칙으로 기록).

### 변경 범위 미리보기
`--dry-run`으로 실행 전에 범위만 볼 수 있고, 에이전트 명령이 승인 정책에 걸리면 승인 화면에 같은 내용이 나옵니다.
```
bdg set "Enemies/*" Health.max=150 --dry-run
→ 오브젝트 37개 · 프리팹 오버라이드 2개 변경 · 프리팹 원본 변경 없음
```
상세: [command-bus-and-reflection](../architecture/command-bus-and-reflection.md#변경-범위-미리보기)

### 에셋 라이선스 검사
```
bdg assets licenses             → 에셋별 출처와 라이선스 목록
bdg build --platform Android    → 상업 사용 불가 에셋이 있으면 빌드 경고(또는 설정에 따라 실패)
```
상세: [data-formats](data-formats.md#에셋-출처와-라이선스)

## 에디터 밖에서 파일을 고칠 때 (2026-11-20 검토 반영)

AI는 명령 대신 파일을 직접 고칠 수도 있습니다. 에디터가 켜져 있을 때 메모리, Undo 기록, 디스크가 어긋나지 않게 하는 규칙입니다.

| 상황 | 동작 |
|---|---|
| 에디터가 꺼져 있음 | 파일 직접 수정 가능. 다음에 열 때 `bdg validate`가 자동 실행 |
| 에디터가 켜져 있고, 저장 안 된 변경이 없음 | **파일 감시자**가 변경을 감지해 다시 불러옴. Undo 기록에 "외부 편집" 항목 하나로 남아 되돌릴 수 있음 |
| 에디터에 저장 안 된 변경이 있음 | 자동으로 덮어쓰지 않음. id 기준 비교 화면에서 오브젝트별로 고름 (git 병합 드라이버와 같은 규칙) |
| 에셋 파일을 옮기거나 지움 | `bdg mv`, `bdg rm`을 쓰면 `.meta`가 함께 움직이고 참조가 갱신됨. 셸로 직접 옮겨서 `.meta`가 떨어지면 `bdg validate`가 짝 잃은 `.meta`를 찾아 다시 연결을 제안 |

```
bdg mv textures/grass.png textures/terrain/grass.png   → .meta 함께 이동, 참조 12곳 갱신
bdg rm models/old_goblin.glb                           → 참조하는 곳이 있으면 목록을 보여 주고 중단 (--force로 진행)
bdg validate                                           → 깨진 참조, 짝 잃은 .meta, 쓰이지 않는 오버라이드
```

- 기본 원칙은 AGENTS.md에도 적혀 있습니다: **에디터가 켜져 있으면 파일 대신 명령을 쓸 것.**

## AGENTS.md 자동 생성 (2026-11-20 검토 반영)

엔진이 프로젝트 루트에 `AGENTS.md`를 만들고, 프로젝트가 바뀔 때마다 갱신합니다. AI가 매번 엔진 사용법을 다시 알아낼 필요가 없어서 토큰이 크게 줄어듭니다.

```markdown
<!-- bidulgi:generated 이 줄 아래는 엔진이 관리합니다. 직접 쓴 내용은 위에 두세요. -->
# MyGame — 비둘기 엔진 프로젝트
- 기본 스크립트 언어: C# (scripting.primary). 새 스크립트는 scripts/<기능>/ 아래에
- 에디터가 켜져 있으면 파일을 직접 고치지 말고 `bdg` 명령을 쓸 것
- 씬 읽기: `bdg view <scene>` / 쓰기: `bdg set`, `bdg add` / 확인: `bdg validate`, `bdg test`
- 컴포넌트 시그니처: `bdg schema` (아래는 이 프로젝트에서 자주 쓰는 것)
  Health { hp:int=100, max:int=100 } · PlayerController { speed:float=5, jumpForce:float=8, cam:ref }
- 그래프는 텍스트(.graph). 읽기는 `bdg graph view`, 만들기는 `bdg graph from-text`
- 플레이 테스트: tests/play/*.playtest — 코드를 고친 뒤 관련 테스트를 돌릴 것
```

- 사람이 쓴 부분은 표시 줄 **위쪽**에 두면 엔진이 건드리지 않습니다.
- 같은 내용을 `CLAUDE.md`로도 낼 수 있습니다 (프로젝트 설정 → Agent → 안내 파일 이름).
- 길이는 짧게 유지합니다 (기본 150줄 이하). 컴포넌트가 많으면 자주 쓰는 것만 넣고 나머지는 `bdg schema`로 안내합니다.
