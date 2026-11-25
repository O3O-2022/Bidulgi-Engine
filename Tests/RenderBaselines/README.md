# 렌더 회귀 기준 이미지 (3-9b)

`BDG_RENDER_COMPARE=repo`로 실행하면 열린 장면의 카메라 북마크 1~9를 차례로 **480×270 고정 크기**로 렌더해(패널 크기·화면 배율과 상관없이 같은 그림) 이 폴더의 기준과 FLIP 오차로 비교합니다.

```
Tests/RenderBaselines/
  <렌더러>/meta.json                 {"renderer":"software","threshold":0.02}  — 평균 오차 허용치
  <렌더러>/<장면 파일 이름>/bookmark_<n>.png
```

| 렌더러 폴더 | 어디서 | 비고 |
|---|---|---|
| `software` | CI(Mesa llvmpipe, GPU 없음) | 매 커밋 비교. 처음에는 비어 있음 → 아래 '받아들이기' |
| `gpu-nvidia` | 기준 GPU(RTX 5070 Ti 노트북) | 로컬 확인용. 드라이버가 바뀌면 다시 만듦 |
| `gpu-amd`, `gpu-intel` | 그 GPU가 있는 사람 | 필요할 때 만듦 |

렌더러마다 래스터 결과가 조금씩 달라서 세트를 섞지 않습니다(`GL_RENDERER` 문자열로 자동 선택).

## 실행

```powershell
$env:BDG_ASSETS = "$PWD/Tests/IndoorScene/Assets"; $env:BDG_SCENE_FILE = "$env:BDG_ASSETS/Scenes/Cloister.scene"
$env:BDG_RENDER_COMPARE = 'repo'; $env:BDG_COMPARE_OUT = "$PWD/ci-regress/Cloister"
.\build\Release\BidulgiEditor.exe --frames 42
# Bidulgi.log: compare: bookmark 1 1 Hall  flip=0.0000 ... ok   /   compare summary: ... failed=0 ... missing=0 renderer=gpu-nvidia
```

- `<out>/<n>.png`(이번 그림)와 `<n>_diff.png`(오차 열지도)가 남습니다.
- 기준이 없는 북마크는 **실패가 아니라 missing**으로 세고, 그림을 `<out>/bookmark_<n>.png`로 내보냅니다.

## 받아들이기 (기준 만들기·바꾸기)

- 로컬: `BDG_COMPARE_BLESS=1`을 더하면 이번 그림을 이 폴더에 씁니다(없으면 `meta.json`도). 바뀐 그림을 눈으로 확인한 뒤 커밋하세요.
- CI(software): 기준이 없거나 그림이 의도대로 바뀌었으면, 실행 결과의 **render-regression** 아티팩트에서 `<장면>/bookmark_<n>.png`를 받아 `Tests/RenderBaselines/software/<장면>/`에 넣고 커밋합니다(`meta.json`은 `{"renderer":"software","threshold":0.02}`).
- 허용치는 세트의 `meta.json`(`threshold`)이나 `BDG_COMPARE_THRESHOLD`로 바꿉니다. Mesa 버전이 바뀌어 전체가 조금씩 다르면 세트를 다시 받습니다.
