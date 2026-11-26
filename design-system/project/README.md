비둘기 엔진 에디터의 모든 화면(Level, Graph, Animation, UI Designer, Scripting, 설정 창, 앞으로 추가될 창)이 따르는 규칙입니다. 새 화면을 만들 때는 이 문서의 토큰과 부품만 써서 만듭니다. 기준이 된 화면은 UI 시안 v0.2입니다.

## 원칙

- **도구는 조용하게, 작업물이 주인공.** 에디터 바탕은 어두운 중립색이고, 색은 뜻이 있을 때만 씁니다 (선택, 축, 타입, 상태).
- **강조색은 하나.** `accent`(호박색)는 지금 활성인 것 하나에만: 활성 탭 윗선, 선택 테두리, 기본 버튼, 포커스 링, 재생 헤드. 장식으로 쓰지 않습니다.
- **평평하게.** 그라데이션, 광택, 두꺼운 그림자를 쓰지 않습니다. 그림자는 떠 있는 것(그래프 노드 `shadow-node`, 모달 `shadow-window`)에만.
- **촘촘하게.** 게임 엔진 에디터는 정보 밀도가 높습니다. 행 높이 `size-row`(22px), 기본 글자 `body`(13px).
- **모든 편집 화면에는 Text 탭.** 사람이 고친 값이 파일에 어떻게 저장되는지 같은 화면에서 보여 줍니다 (비둘기 엔진 고유 원칙).

## 레이아웃

- 에디터 창은 `ground` 위에 놓이고 모서리는 `radius-window`.
- 패널 사이 구분선은 선을 그리지 않고, 창 바탕을 `line`으로 칠한 뒤 패널 사이에 1px 간격을 둡니다. 모든 구분선이 정확히 1px로 맞습니다.
- 위에서 아래로: 메뉴바(`size-panel-header`, `win`) → 워크스페이스 탭(`size-workspace-bar`, `win`) → 패널 격자 → 상태 표시줄(`size-status-bar`, `win`).
- 패널 = 헤더(`size-panel-header`, `win`, 아래 1px `line`) + 몸체(`panel`). 헤더에는 탭, 출처 배지, 오른쪽 끝 컨트롤.
- 기본 배치는 유니티: 왼쪽 Hierarchy, 가운데 뷰포트, 오른쪽 전체 높이 Inspector, 아래 Project/Console/Agent. 그래프 편집기는 언리얼 배치: 왼쪽 My Graph, 가운데 캔버스, 오른쪽 Details, 아래 Text/결과.
- 좁은 화면(1000px 미만)에서는 패널을 세로로 쌓습니다. 뷰포트·캔버스는 최소 380px 높이를 지킵니다.
- 간격은 `space-1`~`space-8`만 씁니다. 패널 안 행의 좌우 여백은 `space-5`.

## 색

- 표면 단계: `ground`(가장 바깥) < `sunk`(입력칸) < `win`(창 테두리·헤더) < `panel`(내용) < `raise`(버튼·컴포넌트 머리). 한 단계씩만 올라가거나 내려갑니다.
- 글자: 본문은 `fg`, 보조는 `fg-dim`, 소제목·종류 열·비활성은 `fg-faint`. 세 글자 토큰 모두 `panel`·`raise`·`win` 위에서 4.5:1 이상입니다(UI 검토 2026-11-26에서 `fg-faint`를 올림). 선택한 행(`selection`) 위에서는 `fg-faint` 대신 `fg-dim`.
- 입력칸·선택칸·토글·체크박스의 테두리는 `line-input`(바탕과 3:1, 빈 칸도 보임). `line`은 패널 사이 이음선에만, `line-strong`은 버튼·노드·모달 테두리.
- 호버는 `raise-hover`, 누름은 `raise-press`. 기즈모 강조는 `gizmo-hot`, 스냅 안내선은 `guide`(다른 데 쓰지 않음), 폴더 아이콘은 `icon-folder` / `icon-folder-tab`.
- 대비는 단위 테스트가 지킵니다(`Tests/ThemeContrastTests.cpp`): 토큰을 바꾸면 그 표를 통과해야 합니다.
- 선택한 행은 `selection` 바탕. 선택한 오브젝트·노드는 `accent` 2px 테두리.
- 상태색 `ok`·`warn`·`err`는 반드시 글자(`OK`, `WARN`, `ERROR`)나 아이콘과 함께 씁니다. 색만으로 상태를 알리지 않습니다.
- 축은 X `axis-x`, Y `axis-y`, Z `axis-z`. 기즈모와 벡터 입력칸 라벨에 같은 색.
- 그래프 핀과 와이어는 타입 색: `pin-exec`, `pin-float`, `pin-vector`, `pin-bool`, `pin-object`. 언어 사이 타입 매핑 표와 같은 색입니다.
- 출처 배지(`source-unity`, `source-unreal`, `source-blender`, 그리고 고유 기능은 `accent`)는 기획 문서와 시안에만 씁니다. 실제 엔진 화면에는 넣지 않습니다.

## 글자

- 글꼴은 `sans`(Inter, 한글은 시스템 글꼴로 대체)와 `mono`(JetBrains Mono). 둘 다 Google Fonts에서 불러오고 파일은 두지 않습니다.
- UI 글자는 `body`, `control`, `label`. 노드 머리와 Inspector 컴포넌트 머리는 `node-title`.
- 패널 안 소제목은 `caption`(대문자, 자간 0.07em, `fg-faint`).
- id, 설정 키, 파일 경로, 코드, JSON은 항상 `mono` (`id`, `code`). 사용자가 그대로 입력할 수 있는 글자라는 표시입니다.
- 메뉴바의 엔진 이름은 `brand` 스타일로 `BIDULGI`.

## 부품

| 부품 | 쓰는 곳 |
|---|---|
| Panel | 모든 도킹 패널. 헤더 탭 + 몸체 |
| Tabs | 패널 탭, 워크스페이스 탭. 활성 탭은 `panel` 바탕 + 위 2px `accent` |
| Button | 일반 / 기본(primary, `accent` 바탕 `on-accent` 글자, 화면당 하나) / 아이콘 버튼 / 버튼 그룹 |
| Field | 라벨 + 컨트롤 2열 행, 입력칸, 선택칸, 체크박스, 축 색 라벨이 붙은 벡터 입력 |
| ListRow | Hierarchy·Project·목록 행. 아이콘 + 이름 + 오른쪽 타입 |
| Badge | 언어 알약(pill), 상태 알약, 출처 배지 |
| Node | 그래프·애니메이션 그래프·머티리얼 그래프 노드. `panel` 몸체, `win` 머리, 위 3px 종류 띠 |

새 부품이 필요하면 먼저 위 부품을 조합해 만들 수 있는지 봅니다. 새로 만들 때는 이 시스템에 추가한 뒤 씁니다.

## 글쓰기

- 화면 글은 한국어. 엔진 고유 명사(Hierarchy, Inspector, Graph, Prefab 등)와 메뉴 이름은 영어 그대로 둡니다. 유니티·언리얼 사용자가 바로 알아보게 하기 위해서입니다.
- 버튼은 동작을 말합니다: "Compile", "재생", "되돌리기". 결과는 짧게: "컴파일됨", "저장됨".
- 에러는 한 줄로 `위치 → 원인 → 고칠 방법`: `scenes/forest_01.scene:12 g1.Health.hpp → Health에 hpp 필드가 없습니다. 가능한 필드: hp, max`.
- 이모지는 쓰지 않습니다.

## 아이콘

- 엔진 로고는 Logos 그룹의 흰색 실루엣(`icon-d-white.svg`). 밝은 바탕에서는 `icon-d-black.svg`, 앱 아이콘은 `icon-d-tile.svg`.
- 메뉴바 로고는 18px, 옆에 `brand` 스타일 `BIDULGI`.
- UI 아이콘은 인라인 SVG, 14~16px, 선 두께 1.4~1.5, `currentColor`. 채운 아이콘은 재생·일시정지 같은 미디어 컨트롤에만.
- 오브젝트 종류 표시는 11px 작은 도형: 메시 = 사각, 라이트 = 원(`warn` 테두리), 카메라 = 한쪽이 둥근 사각, 프리팹 = `prefab` 채움, 위젯 = `pin-ui` 채움, 빈 오브젝트 = 점선.

## 상태와 움직임

- 호버: 행은 `raise` 바탕, 버튼은 테두리를 `fg-faint`로.
- 포커스: 2px `accent` 윤곽선, 1px 띄움. 모든 조작 요소에 보여야 합니다.
- Play 중: 뷰포트 안쪽에 2px `accent` 테두리.
- 움직임은 상태 변화를 알릴 때만 짧게 (0.12~0.25초). 사용자가 동작 줄이기를 켜면 끕니다.
