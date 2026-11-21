# UI 시안 v0.2 계획

현재 시안: `prototype/forge-editor.html` (v0.1, Level 에디터)
상태: **v0.2 시안 제작 (2026-11-20)** — `prototype/v0.2/editor.html`. 아이콘 시안은 `branding/icons.html`

## 핵심 변경: 블렌더식 워크스페이스 탭

메뉴바 아래에 작업 공간 탭을 두고, 탭마다 패널 배치 전체가 바뀝니다.

```
[ Level ] [ UI Designer ] [ Animation ] [ Scripting ]
```

| 워크스페이스 | 화면 구성 | 출처 |
|---|---|---|
| Level | v0.1 그대로 (Hierarchy, Scene, Inspector, Project) | 유니티 |
| UI Designer | 팔레트, 위젯 트리, 캔버스, Details, 아래 타임라인 | 언리얼 UMG |
| Animation | Dope Sheet / Graph Editor, 대상 미리보기 | 블렌더 |
| Scripting | 코드 뷰어와 언어별 예시, 콘솔 | 공통 |

## 작업 항목

### ① 프로젝트 설정 창
- 여는 곳: Edit 메뉴 → Project Settings, 툴바 설정 버튼
- 왼쪽 카테고리 트리, 위쪽 전체 검색, 가운데 항목(이름 + 키), 바뀐 값에 ↺ 되돌리기
- "편집 대상" 선택: 기본 / Android / iOS / Web / Console
- 오른쪽에 실제 저장 파일 미리보기
- 모든 변경은 `config set` 명령 → Undo와 Agent 기록에 남음
- 상세: [project-settings](../features/project-settings.md)

### ② UI Designer
- 팔레트: Canvas, Panel, Flex, Text, Button, Image, ProgressBar, Slider
- 위젯 트리, 해상도 프리셋 캔버스(1920×1080, 폰, 태블릿), 앵커 표시, 드래그 이동·크기 조절
- Details: 앵커 프리셋 격자, 위치·크기, 스타일, 바인딩
- Text 탭: `ui/hud.ui` 실시간 표시
- 예시 위젯: HUD(체력바, 탄약), 상점 간판(월드 공간)
- 상세: [ui-widgets](../features/ui-widgets.md)

### ③ Animation 워크스페이스와 타임라인
- Dope Sheet (키프레임 끌어서 이동), Graph Editor (베지어 이징)
- 재생 헤드, 재생/정지, 자동 키(녹화 버튼)
- UI Designer 아래에도 같은 타임라인
- Text 탭: `.anim` 파일
- 상세: [animation](../features/animation.md)

### ④ Level에서 위젯 공간 시연
- Inspector에 Widget 컴포넌트 (`space`: Screen / Camera / World, `billboard`)
- 씬 뷰: 월드 간판, 고블린 머리 위 체력바
- Game 뷰: HUD 오버레이

### ⑤ Scripting 워크스페이스
- Project 패널에서 스크립트를 열면 코드 뷰어에 표시
- C#, C++, Rust, Lua, Python 예시
- 공개 필드 ↔ Inspector 연결을 나란히 표시

## 진행 순서

1. 워크스페이스 탭 뼈대
2. 프로젝트 설정 창
3. UI Designer
4. 타임라인 / Animation
5. Level의 Widget 컴포넌트
6. Scripting

## 정할 것

- 시안의 로고와 명령어(`forge`)를 비둘기 엔진(`bdg`)으로 변경
- 범위: 전부 vs 1~4만 먼저
- 파일 구성: HTML 하나 vs 화면별 스크립트 분리 (분리 추천)
