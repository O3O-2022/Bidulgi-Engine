# {{GAME_NAME}} — guide for AI agents (and people)

Made from the Bidulgi Engine **2D platformer template** (`bdg new <dir> --template platformer2d`).
비둘기 엔진 **2D 플랫포머 템플릿**으로 만든 프로젝트입니다.

## 이 템플릿 구조와 고치는 법 (Template structure and how to change it)

```
Scenes/Main.scene                       the level: ground, platforms, coins, a checkpoint, the goal flag, the hero, the camera
                                        레벨 (한 줄 = 오브젝트 하나; 2D 물리는 XY 평면, z = 0)
Assets/Player/PlatformerController.cs   run, jump (coyote time, jump buffer, short hop), fall -> checkpoint
Assets/Player/CameraFollow.cs           side-scrolling camera with a dead zone              따라가는 카메라
Assets/Game/Coin.cs                     trigger: sound, +coins, disappears                  동전
Assets/Game/Goal.cs                     the goal flag, or a checkpoint (isCheckpoint)       골·체크포인트
Assets/Game/GameRules.cs                the rules (GameMode): coins, the goal (log / Console)
Assets/Audio/*.wav                      generated sounds (CC0)
Assets/Materials/*.material             Hero, Grass, Wood, Concrete, Gold, Glow
Input/Actions.jsonl, Input/Contexts/Play.jsonl   Move, Jump, Run (keyboard, gamepad)
ProjectSettings/Project.json            name, start scene, window, 2D gravity (-25: snappier than real life)
Game.csproj                             builds Assets/**/*.cs into .cache/Scripts/
```

The hero is `Hero` (Rigidbody2D Dynamic with rotation frozen + Collider2D + PhysicsMaterial friction 0 +
`PlatformerController`). Ground and platforms are objects with a `Collider2D` and no Rigidbody2D (static). Coins
and flags have `"isTrigger":true` colliders. The camera is a perspective camera looking at the XY plane.

| Request (요청) | Change (고칠 곳) |
|---|---|
| Jump higher / floatier 점프 | `PlatformerController.jumpSpeed`, or `physics.gravity2D` in ProjectSettings/Project.json |
| Faster hero 빠르게 | `walkSpeed`, `runSpeed`, `acceleration` |
| More forgiving jumps 점프 판정 | `coyoteTime`, `jumpBuffer` |
| New platform 발판 | copy a `Platform` line (new `id`, `pos`, `scale`) |
| New coin 동전 | copy a `Coin` line; `"Coin":{"value":5}` for a big one |
| Move the goal 골 위치 | the `goal` line's `pos` |
| Other keys 키 | `Input/Contexts/Play.jsonl` |

After changing a script: `dotnet build Game.csproj` (the editor reloads it). Check: `bdg build --check`, then
`bdg build` and run `Builds/Windows-Development/<name>.exe`.

## Licence

Template code: 0BSD. Template assets (scene, sounds, data): CC0. See LICENSE-TEMPLATE.txt.

## Left for later (다음 단계)

Orthographic 2D camera and sprites (the level is drawn with 3D boxes for now), tilemaps, sprite animation, enemies,
moving platforms, HUD (7-3), prefabs (6-1: hero, coin, flag).
