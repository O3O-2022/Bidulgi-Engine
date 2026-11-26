# {{GAME_NAME}} — guide for AI agents (and people)

Made from the Bidulgi Engine **multiplayer arena template** (`bdg new <dir> --template arena`).
비둘기 엔진 **멀티플레이 아레나 템플릿**으로 만든 프로젝트입니다. 2~4명이 링 위에서 서로 밀어 떨어뜨립니다.

## 구조 (Structure)

```
Scenes/Main.scene            the ring, 4 PlayerStarts, "Game Rules" (GameMode + ArenaGame), "PlayerProto", HUD (scores, net, lobby)
Assets/Game/ArenaPlayer.cs   one player: owner moves (prediction), [Server] Push, [Multicast] PushFx, [Replicated] score, falling off
Assets/Game/ArenaGame.cs     lobby (Host / Join buttons), scoreboard, network stats, [Replicated] round clock
Input/Actions.jsonl          Move, Push;  Input/Contexts/Play.jsonl: WASD / arrows / stick, Space / mouse / pad
```

Server-authoritative (Unreal style, Docs/features/multiplayer.md): the server decides pushes, falls and scores; each
player's own client moves its player at once and the server checks the moves. 서버가 정답, 내 캐릭터만 바로 움직임.

## 실행 (Running)

- Editor: Play ▾ → Net Mode **Listen Server**, Players **2~4**, Play. 나머지 플레이어가 창으로 열림, Stop이 모두 닫음.
- Built game: run it and press **방 만들기 (Host)** on one, **참가 (Join)** on others (same PC, port 7777).
  Other PCs: `"{{GAME_NAME}}.exe" --connect 192.168.0.5:7777`.
- Dedicated server (no window): `"{{GAME_NAME}}.exe" --server --port 7777`, then clients `--connect`.

| Request (요청) | Change (고칠 곳) |
|---|---|
| Faster players 더 빠르게 | `ArenaPlayer.speed` on PlayerProto (and `NetworkTransform.maxSpeed`, the server's limit) |
| Stronger push 더 세게 | `ArenaPlayer.pushStrength`, `pushRange` |
| Bigger ring 링 크게 | `Ring` scale in the scene and the 10.6 bounds in `ArenaPlayer.Update` |
| More players 더 많이 | `GameMode.maxPlayers`, more `PlayerStart` objects |
| Another port 다른 포트 | `ArenaGame.port`, `--port` |

## Licence

Template code: 0BSD. Template assets: CC0. See LICENSE-TEMPLATE.txt. The engine is MIT.
