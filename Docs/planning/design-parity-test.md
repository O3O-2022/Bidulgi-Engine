# 디자인 일치 검사 (HTML 시안 ↔ 엔진 화면)

작성 2026-11-21. 시점: 자체 UI 프레임워크 기초(M0)가 나온 뒤.

HTML로 만든 디자인 시안과 디자인 시스템이 실제 엔진 화면과 같게 나오는지 자동으로 비교합니다.

## 무엇을 비교하나

| 단계 | 비교 대상 | 방법 | 통과 기준 |
|---|---|---|---|
| 1. 토큰 | 색, 글꼴 크기, 간격, 모서리 반경 | `tokens.json` 값 ↔ 엔진이 실제로 쓰는 값 (텍스트 덤프) | **완전히 같음** |
| 2. 레이아웃 | 부품마다 위치와 크기 | 브라우저 DOM 상자(`getBoundingClientRect`) ↔ 엔진 위젯 트리 상자 덤프 (JSON) | 오차 1px 이하 |
| 3. 부품 이미지 | 디자인 시스템 부품 8개 (Panel, Tabs, Button, Field, ListRow, Badge, Node, Cover) | 같은 크기·배율로 각각 스크린샷 → 이미지 차이(FLIP/SSIM) | 차이 점수가 기준 이하 |
| 4. 전체 화면 | 에디터 시안 v0.2의 워크스페이스 화면 | 전체 스크린샷 비교 + 차이 영역 강조 이미지 | 차이 점수가 기준 이하, 차이 영역 목록 |

- 픽셀 비교만 하면 글자 렌더링 차이(브라우저와 FreeType·MSDF의 안티앨리어싱)로 항상 조금씩 다릅니다. 그래서 **토큰과 레이아웃은 숫자로 정확히** 비교하고, 이미지는 허용 오차를 둡니다.
- 시안에 나오는 아이콘과 글꼴(Pretendard, JetBrains Mono, Lucide)은 엔진과 같은 파일을 씁니다.

## 필요한 기능

- 엔진: `BidulgiEditor --screenshot out.png --layout <시안 이름> --scale 1`, `--dump-widgets out.json`
- 브라우저 쪽: 헤드리스 브라우저로 시안을 같은 창 크기·배율로 열어 스크린샷과 DOM 상자 JSON을 저장하는 스크립트
- 비교 도구: `bdg ui compare` → 텍스트 보고서 + 차이 이미지

```
$ bdg ui compare design-system/components/Button
tokens   ✓ 24/24
layout   ✓ 6 boxes, max error 0.5px
image    ✓ FLIP 0.012 (기준 0.03)
```

## 1차 구현 (2026-11-23, QUEUE 0-10) — 1~2단계

```
python tools/ui_compare/compare.py          # 빌드된 build/Release/BidulgiEditor.exe 사용
tokens   ✓ 72/72
layout   ✓ Panel    8 boxes, max error 0.02px
...
result   PASS (layout max error 0.64px, tolerance 1.0px)
```

- 브라우저 쪽 `tools/ui_compare/browser_boxes.py`: 추가 패키지 없이 **헤드리스 Edge `--dump-dom`**. 토큰 CSS 변수 + `bundle.css` + 저장소 글꼴(Pretendard, JetBrains Mono) + `preview.html`로 하네스 페이지를 만들고, 글꼴 로드 후 `[data-bdg-id]` 요소의 `getBoundingClientRect()`를 DOM에 써 넣는 스크립트를 넣어 덤프에서 읽습니다. 엔진처럼 커닝·합자를 끄고, 페이지 폭을 800px로 고정합니다.
- 엔진 쪽: `BDG_SCREEN=parity:<부품>`(Screens.cpp `BuildParityScreen`) + `--size 800x600` + `BDG_DUMP_WIDGETS`, 토큰은 `--dump-tokens`.
- 맞추면서 고친 엔진 쪽: 글자 진행폭을 힌팅 없는 값(`linearHoriAdvance`)으로(크기와 무관하게 정확), `line-height: normal`을 브라우저처럼 올림, 글자 간격(letter-spacing), 여러 줄 Label, Label 변형(컴포넌트 머리글·캡션), Button 상태 점, Badge 색·출처 배지, 작업공간 탭 막대(`size-workspace-bar`), Node 치수(CSS 그대로), Cover를 디자인(960×300)대로, 고정 크기 위젯은 부모를 넘어갈 수 있게.
- 아직 엔진이 그리지 않는 조각(아이콘 버튼 묶음)은 토큰 크기의 상자로 자리만 잡습니다. 3단계(부품 이미지 비교)부터는 다음 차례.
- CI의 `Design parity` 단계가 매 푸시마다 돌립니다.

## 운영

- 디자인 시스템이나 UI 프레임워크가 바뀔 때마다 CI에서 자동으로 돌립니다.
- 시안을 바꿀 때는 시안을 먼저 고치고 → 엔진이 따라오게 합니다 (시안이 정답).
- 결과 보고서가 텍스트라서 AI가 읽고 어긋난 곳을 바로 고칠 수 있습니다.
