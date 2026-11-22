# 에디터 UI

배치와 조작은 유니티, 도구의 밀도는 언리얼, 작업 공간 전환은 블렌더 방식입니다.
현재 시안: `prototype/forge-editor.html`

## 워크스페이스 탭 (블렌더식)

```
[ Level ] [ Blueprint ] [ Animation ] [ UI Designer ] [ Scripting ]
```

탭을 누르면 패널 배치 전체가 그 작업에 맞게 바뀝니다. 사용자가 배치를 바꾸면 워크스페이스별로 `.user/`에 저장됩니다.

## Level 워크스페이스 레이아웃 (유니티 기본 배치)

```
┌───────────────────────────────────────────────────────────────┐
│ File Edit Assets GameObject Component Build Window Agent Help │
│ [Q W E R] [Pivot|Global]   [▶ ⏸ ⏭]   Platform Scalability RHI │
├────────────┬──────────────────────────────────┬───────────────┤
│ Hierarchy  │  [Scene] [Game]                  │ Inspector     │
│            │                                  │ (전체 높이)    │
│            │      뷰포트 + 기즈모              │               │
├────────────┴──────────────────────────────────┤               │
│ [Project] [Console] [Agent]                   │               │
├───────────────────────────────────────────────┴───────────────┤
│ 상태 표시줄: 저장 상태 · 오브젝트 수 · 씬 파일 크기와 토큰 수 · RHI │
└───────────────────────────────────────────────────────────────┘
```

## 패널별 기능과 출처

| 패널 | 기능 | 출처 |
|---|---|---|
| 툴바 도구 | Q/W/E/R (손, 이동, 회전, 크기), Pivot/Global | 유니티 |
| Play 컨트롤 | 재생, 일시정지, 한 프레임 진행. Play 중 변경은 정지 시 복원 | 유니티 |
| Platform / Scalability / RHI 선택 | 대상 플랫폼, 품질 단계, 그래픽 백엔드 미리보기 | 언리얼 |
| Hierarchy | 씬 트리, 드래그로 부모 변경, 검색 | 유니티 |
| Hierarchy 열·눈 아이콘 | 타입 열, 뷰포트 숨김 (에디터 상태, 씬 파일에 저장 안 됨) | 언리얼 Outliner |
| Scene 뷰 | 회전·이동·줌, 기즈모, F로 포커스, 2D 토글 | 유니티 |
| 셰이딩 모드 | Lit / Unlit / Wireframe | 언리얼 |
| 뷰포트 통계 | fps, Draw call, 삼각형 수 | 언리얼 |
| Inspector | 컴포넌트 편집, 프리팹 오버라이드 표시, Add Component | 유니티 |
| Inspector 검색 | 속성 이름으로 필터 | 언리얼 Details |
| **Inspector Text 탭** | 선택한 오브젝트의 JSON 한 줄, 씬 파일 전체, 압축 표기, 토큰 수 | 고유 |
| Project | 폴더 트리와 에셋 그리드, 프리팹 더블클릭 배치 | 유니티 |
| Project 필터 칩 | 종류별 필터 | 언리얼 Content Browser |
| Console | 로그, 경고, 에러 | 유니티 |
| **Agent 탭** | 명령 기록(Undo 스택), `bdg` CLI 터미널 | 고유 |

## 단축키 (유니티와 동일)

| 키 | 동작 |
|---|---|
| Q / W / E / R | 손 / 이동 / 회전 / 크기 |
| F | 선택한 오브젝트로 포커스 |
| Ctrl+D | 복제 |
| Delete | 삭제 |
| Ctrl+Z / Ctrl+Y | 되돌리기 / 다시 실행 |
| Ctrl+P | Play |

## 다른 워크스페이스

- UI Designer → [ui-widgets](ui-widgets.md)
- Animation → [animation](animation.md)
- Blueprint / 애니메이션 그래프 그래프 → [visual-scripting](visual-scripting.md), [animation](animation.md)
- Scripting: 코드 뷰어, 언어별 예시, 공개 필드 ↔ Inspector 연결 표시 → [scripting](../architecture/scripting.md)
- 프로젝트 설정 창 → [project-settings](project-settings.md)

## 구현
- 색·글꼴·간격·부품 규칙은 [디자인 시스템](../../design-system/project/README.md)을 따릅니다. 새 패널이나 창도 같은 토큰과 부품으로 만듭니다.

- 자체 UI 프레임워크(게임 UI와 공용, 도킹·기즈모 포함) → [libraries-and-packaging](../architecture/libraries-and-packaging.md#1-1-ui-프레임워크-자체-구현)
- Inspector는 리플렉션에서 자동 생성, 컴포넌트별 커스텀 에디터는 `[CustomEditor]`(C#) 방식으로 확장
- 모든 편집은 명령 버스를 거침 → [command-bus-and-reflection](../architecture/command-bus-and-reflection.md)

## 편의 기능 (2026-11-20 채택)

### "왜 안 보이지?" 진단
오브젝트를 고르고 Inspector 위쪽의 진단 버튼(또는 `bdg diagnose visibility <id>`)을 누르면, 화면에 안 나오는 이유를 순서대로 검사해 결과를 목록으로 보여 줍니다.

| 순서 | 검사 | 결과 예시 |
|---|---|---|
| 1 | 오브젝트나 부모가 비활성 | `Enemies`(부모)가 꺼져 있음 → [켜기] |
| 2 | 렌더러·메시·스프라이트가 없음 | MeshRenderer 없음 → [추가] |
| 3 | 머티리얼·텍스처 누락, 셰이더 컴파일 실패 | `materials/goblin.mat` 없음 |
| 4 | 크기가 0이거나 너무 작음 | scale 0,0,0 |
| 5 | 카메라 컬링 마스크·레이어에서 빠짐 | 레이어 `Hidden`이 Main Camera 마스크에 없음 |
| 6 | 카메라 시야 밖, near/far 범위 밖 | 카메라 뒤쪽 12m |
| 7 | 다른 물체에 완전히 가려짐 | `Wall_03` 뒤 |
| 8 | 알파 0, 2D 정렬 순서 | Sorting Order가 배경보다 낮음 |

- 각 항목에 바로 고치는 버튼이 있고, 누르면 일반 명령으로 실행되어 Undo가 됩니다.
- 같은 방식으로 "왜 안 움직이지?"(Rigidbody kinematic, 스크립트 비활성, 애니메이션 루트 모션 충돌), "왜 소리가 안 나지?"(볼륨, 리스너 거리, 믹서 음소거)도 순차로 추가합니다.

### 자연어 검색
Hierarchy와 Project 검색창에 문장을 쓰면 `bdg query`로 바꿔 실행하고, **만들어진 쿼리를 함께 보여 줍니다** (결과를 믿을 수 있고, 쿼리 문법도 자연스럽게 익힘).

```
검색: 체력 50 이하 고블린
쿼리: bdg query "prefab=goblin.prefab" --where "Health.hp<=50"
결과: g1 (hp 12), g7 (hp 40)
```

- 일반 단어(이름·타입)는 기존처럼 바로 검색하고, 문장일 때만 변환합니다.
- 변환은 로컬 규칙(컴포넌트·필드 이름 사전)을 먼저 쓰고, 애매하면 연결된 AI에게 묻습니다. AI가 없으면 규칙 기반만 동작합니다.

### 편집 중 플랫폼 예산 표시
뷰포트 구석에 대상 플랫폼의 예산 막대를 띄웁니다 → 상세: [rendering-and-platforms](../architecture/rendering-and-platforms.md#편집-중-플랫폼-예산-표시)

### 명령 실행 전 변경 범위 미리보기
여러 오브젝트를 바꾸는 명령은 실행 전에 범위를 보여 줍니다 → 상세: [command-bus-and-reflection](../architecture/command-bus-and-reflection.md#변경-범위-미리보기)


## 에셋 아이콘과 썸네일

Project 창과 선택 창에서 에셋을 두 가지로 보여 줍니다. 시안: [prototype/asset-icons.html](../../prototype/asset-icons.html)

| 종류 | 표시 |
|---|---|
| C++, C#, Python, Lua | **파일 종류 아이콘** (언어별 색 띠 + 약자). 썸네일 없음 |
| Graph, 애니메이션 그래프 | 종류 아이콘. 큰 보기에서는 노드 배치를 축소한 미리보기 |
| 폴더 | 폴더 아이콘. 패키지 폴더는 상자 표시, 읽기 전용은 자물쇠 |
| 이미지, 스프라이트 | **실제 이미지 썸네일** (투명은 체크무늬 배경) |
| 프리팹, 모델 | **3D 썸네일** (자동 카메라로 한 장 렌더). 2D 프리팹은 스프라이트 합성 |
| 머티리얼 | 구 또는 평면에 적용한 썸네일 |
| 씬 | 마지막 저장 때 씬 카메라 화면 |
| 오디오 | 파형 썸네일, 마우스를 올리면 재생 버튼 |
| 애니메이션 클립 | 대표 포즈 썸네일, 마우스를 올리면 재생 |

- 썸네일은 백그라운드에서 만들고 `.cache/thumbnails/`에 해시로 저장합니다. 원본이 바뀐 것만 다시 만듭니다.
- 작은 보기에서는 썸네일 위에 종류 배지(작은 아이콘)를 겹쳐 종류를 구분합니다.
- AI용: `bdg thumb <에셋>`으로 썸네일 PNG 경로를 받아 이미지로 확인할 수 있습니다.

### C++ 헤더·소스 묶음 표시 (2026-11-22)

같은 폴더에 같은 이름의 `.h`와 `.cpp`가 있으면 **하나의 "C++ class" 묶음**으로 보여 줍니다 (Rider·Visual Studio의 파일 묶기와 같은 방식). 파일이 많은 폴더에서 짝이 헷갈리지 않게 하기 위해서입니다.

| 보기 | 표시 |
|---|---|
| 큰 보기 (타일) | 두 파일 아이콘을 C++ 색 테두리 하나로 감싸고, 사이를 연결선으로 이음. 이름은 `Weapon · C++ class` |
| 목록 보기 | `Weapon.cpp` 아래에 `Weapon.h`가 들여쓰기된 자식으로 붙음 (접기 가능) |
| 짝이 없는 파일 | `Utils.h`처럼 혼자 있으면 보통 파일로 표시 |
| 생성 파일 | `Weapon.gen.h` / `.gen.cpp`(헤더 도구가 생성)는 기본으로 숨기고, 묶음에 작은 표시만 |

- **열기:** 묶음을 더블클릭하면 Scripting 작업 공간에서 `.h`와 `.cpp`를 나란히 엽니다. `Ctrl+K, O`로 짝 파일 전환.
- **이름 바꾸기·이동·삭제:** 두 파일에 함께 적용 (명령 하나라 Undo도 한 번). 클래스 이름 변경도 제안.
- **새로 만들기:** `Create > C++ Class`는 두 파일을 함께 만들고 `BCLASS` 템플릿을 채움.
- 같은 규칙을 다른 짝에도 쓸 수 있게 설정으로 둡니다 (예: `.shader` + `.shader.meta`는 숨김, `.animgraph` + 미리보기 캐시).
- 구현 상태: Project 패널 큰 보기에서 묶음 타일 표시 완료 (`Engine/Runtime/Screens.cpp`). 목록 보기·열기·이름 바꾸기 연동은 M2 Project 패널(2-5)에서.