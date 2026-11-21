# UI 위젯 시스템

**언리얼 UMG 방식**의 위젯 에셋을 만들고, 어디에 띄울지는 컴포넌트에서 고릅니다.

## 위젯 에셋 (`.ui`)

```jsonc
// ui/hud.ui
{"v":1, "size":[1920,1080], "root":{"type":"Canvas", "children":[
  {"id":"hp", "type":"ProgressBar", "anchor":"top-left", "pos":[24,24], "size":[240,18],
   "style":"styles/bar_red", "bind":{"value":"@player.Health.hp", "max":"@player.Health.max"}},
  {"id":"ammo", "type":"Text", "anchor":"bottom-right", "pos":[-24,-24], "text":"{0} / 30", "bind":{"0":"@player.Gun.ammo"}},
  {"id":"PlayBtn", "type":"Button", "anchor":"center", "size":[240,64], "text":"@loc.play", "onClick":"Menu.Play"}
]}}
```

### 위젯 종류

| 분류 | 위젯 |
|---|---|
| 컨테이너 | Canvas(자유 배치), Panel, Flex(가로/세로), Grid, Scroll, Overlay |
| 표시 | Text, Image, ProgressBar, Spinner |
| 입력 | Button, Toggle, Slider, InputField, Dropdown |
| 재사용 | 다른 `.ui`를 자식으로 포함 (`"type":"ui/item_slot.ui"`) |

### 규칙

- 기본값은 저장하지 않음 (씬 형식과 같은 원칙)
- `anchor`: `top-left`, `center`, `stretch` 등 9+α 프리셋. 언리얼처럼 앵커 프리셋 격자로 선택
- 레이아웃은 앵커(자유 배치)와 Flexbox(Yoga) 둘 다
- 텍스트의 `@loc.키`는 현지화 테이블에서 찾음
- `bind`는 값이 바뀔 때만 갱신 (매 프레임 폴링 안 함)
- `onClick` 등 이벤트는 `"스크립트.메서드"`로 연결

## 띄우는 공간: Widget 컴포넌트

```jsonc
{"id":"hud",  "Widget":{"ui":"ui/hud.ui", "space":"Screen"}}
{"id":"sign", "pos":[3,2,5], "Widget":{"ui":"ui/shop.ui", "space":"World", "size":[1.6,0.9], "interactive":true}}
{"id":"ammo", "parent":"@player", "Widget":{"ui":"ui/ammo.ui", "space":"Camera", "distance":1.2}}
{"id":"tag",  "parent":"@g1", "pos":[0,1.6,0], "Widget":{"ui":"ui/hpbar.ui", "space":"World", "billboard":true}}
```

| space | 용도 | 동작 |
|---|---|---|
| `Screen` | HUD, 메뉴 | 화면 위 오버레이. 해상도에 맞게 스케일 |
| `Camera` | VR HUD, 깊이감 있는 UI | 카메라 앞 일정 거리에 고정, 3D 렌더링 |
| `World` | 간판, 키패드, 게임 속 모니터 | 월드에 놓인 판. `interactive`면 레이캐스트로 클릭 |
| `World` + `billboard` | 이름표, 체력바 | 항상 카메라를 향함. 거리에 따라 크기 유지 옵션 |

## 렌더링

- SDF 폰트 (어떤 크기에서도 선명), 한글 포함 동적 글리프 아틀라스
- 9-slice 이미지, 둥근 모서리, 그림자
- 같은 텍스처를 쓰는 위젯은 한 번에 배칭
- World/Camera 위젯은 렌더 텍스처 또는 직접 메시로 그림 (위젯 수에 따라 자동 선택)

## UI Designer 워크스페이스

```
┌ Palette ──┬──────────── Canvas (1920×1080 ▾) ────────────┬ Details ─────┐
│ Canvas    │                                              │ Anchor [격자] │
│ Panel     │   ▭ 체력바                                    │ Pos  24  24  │
│ Flex      │                                              │ Size 240 18  │
│ Text      │            [ PLAY ]                          │ Style        │
│ Button    │                                              │ Bind         │
│ Image     │                                ammo 12 / 30  │              │
├ Tree ─────┤                                              │ [Text 탭]    │
│ Canvas    │                                              │              │
│  hp       ├──────────────────────────────────────────────┤              │
│  ammo     │ Timeline: menu_open.anim  ◆────◆──◆          │              │
└───────────┴──────────────────────────────────────────────┴──────────────┘
```

- 팔레트에서 끌어 놓기, 캔버스에서 드래그로 이동·크기 조절
- 해상도 프리셋: 1920×1080, 폰 세로, 태블릿, 사용자 지정
- 아래 타임라인으로 위젯 애니메이션 → [animation](animation.md)
- Text 탭에서 `.ui` 파일 실시간 확인

## 명령 예시

```
bdg ui add ui/hud.ui score --type Text --anchor top-right --pos -24,24 --text "0"
bdg ui set ui/hud.ui hp.size=300,20
```

## UI 자동 점검 (2026-11-20 채택)

UI Designer에서 저장할 때(그리고 `bdg validate`에서) 위젯을 검사하고, 문제를 캔버스 위에 표시합니다.

| 검사 | 기준 (프로젝트 설정에서 조정) | 표시 |
|---|---|---|
| 글자 대비 | 배경 대비 4.5:1 이상 (큰 글자 3:1) | 대비 부족 글자에 노란 테두리 |
| 최소 글자 크기 | 기준 해상도에서 플랫폼별 최소 크기 (모바일은 더 크게) | 크기와 권장값 |
| 터치 영역 | 모바일 대상이면 버튼 등 입력 위젯 최소 크기 확보 | 영역이 작은 위젯 강조 |
| 번역문 넘침 | 등록된 언어 중 **가장 긴 번역문**으로 미리 채워 넘치거나 잘리는지 | 잘리는 위젯과 해당 언어 |
| 겹침 | 입력 위젯끼리 겹쳐 클릭이 가로막히는지 | 겹친 영역 |
| 안전 영역 | 노치·둥근 모서리 영역에 중요한 요소가 있는지 | 안전 영역 밖 요소 |

- 결과는 경고이고 저장을 막지 않습니다. 빌드에서 실패로 다룰지는 프로젝트 설정에서 고릅니다.
- 텍스트 결과: `bdg validate ui/hud.ui` → `ui/hud.ui ammo: 글자 대비 2.9:1 (기준 4.5:1) → 색을 #FFFFFF로 바꾸거나 그림자 추가`
- 미리보기 언어 전환: Designer 상단에서 언어를 바꾸거나 "가장 긴 번역" 모드로 볼 수 있습니다.
