# {{GAME_NAME}}

비둘기 엔진 3인칭 템플릿으로 만든 게임입니다. A third-person playground made from the Bidulgi Engine third-person template.

## 조작 / Controls

| | 키보드·마우스 | 게임패드 |
|---|---|---|
| 이동 Move | WASD, 화살표 | 왼쪽 스틱 |
| 시점 Look | 마우스 | 오른쪽 스틱 |
| 점프 Jump | Space | A (South) |
| 달리기 Sprint | Shift | LB |
| 손 흔들기 Wave | E | Y (North) |

## 실행 / Run

1. 스크립트 빌드 (C# scripts): `dotnet build Game.csproj` — `bdg new`가 한 번 해 둡니다.
2. 에디터: 이 폴더에서 비둘기 에디터를 열고 ▶ Play. Hero를 고르면 Inspector에 애니메이터 상태·블렌드 가중치·변수가 보입니다.
3. 게임 만들기 (build the game): `bdg build`, 그다음 `Builds/Windows-Development/{{GAME_NAME}}.exe`.

구조와 고치는 법은 [AGENTS.md](AGENTS.md)에 있습니다. Structure and how to change things: AGENTS.md.
