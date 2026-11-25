# {{GAME_NAME}}

비둘기 엔진 2D 플랫포머 템플릿으로 만든 게임입니다. A side-scrolling platformer made from the Bidulgi Engine 2D platformer template.

## 조작 / Controls

| | 키보드 | 게임패드 |
|---|---|---|
| 이동 Move | A / D, 화살표 | 왼쪽 스틱, 방향 패드 |
| 점프 Jump (길게 = 높이) | Space | A (South) |
| 달리기 Run | Shift | X (West) |

동전을 모으며 오른쪽 끝의 빛나는 깃발까지 가세요. Collect coins and reach the glowing flag on the right.

## 실행 / Run

1. 스크립트 빌드 (C# scripts): `dotnet build Game.csproj` — `bdg new`가 한 번 해 둡니다.
2. 에디터: 이 폴더에서 비둘기 에디터를 열고 ▶ Play. 동전 수는 Console(로그)에 나옵니다.
3. 게임 만들기 (build the game): `bdg build`, 그다음 `Builds/Windows-Development/{{GAME_NAME}}.exe`.

구조와 고치는 법: [AGENTS.md](AGENTS.md).
