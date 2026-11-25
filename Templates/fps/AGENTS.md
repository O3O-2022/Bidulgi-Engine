# {{GAME_NAME}} — guide for AI agents (and people)

Made from the Bidulgi Engine **first-person shooter template** (`bdg new <dir> --template fps`).
비둘기 엔진 **1인칭 슈팅 템플릿**으로 만든 프로젝트입니다.

## 이 템플릿 구조와 고치는 법 (Template structure and how to change it)

```
Scenes/Main.scene                 the range: greybox level, 5 targets, the player, the bullet pool
                                  연습장: 그레이박스 맵, 표적 5개, 플레이어, 총알 풀 (한 줄 = 오브젝트 하나)
Assets/Player/FPSController.cs    walk / sprint / jump / mouse look        이동·달리기·점프·시점
Assets/Weapons/Weapon.cs          fire, magazine, reload, spread, recoil   발사·탄창·재장전·퍼짐·반동
Assets/Weapons/Projectile.cs      one pooled bullet (physics)              총알 하나 (물리)
Assets/Game/Target.cs             reacts to hits: impulse, sound, score, disappears
Assets/Game/ScoreKeeper.cs        the rules (GameMode): score, end of round (HUD + log)
HUD (Scenes/Main.scene)           UICanvas "HUD" with elements score, ammo, weapon, crosshair, message (UI.Text("score") in C#)
Assets/Audio/*.wav                generated sounds (CC0)                   생성한 소리
Assets/Materials/*.material       Gold (targets), Steel, Concrete, Wood, Glow (bullets): colour, metal, roughness
Data/Weapons.table.jsonl          weapon numbers (one row per weapon)      무기 수치 (한 줄에 무기 하나)
Input/Actions.jsonl               actions: Move, Look, Jump, Sprint, Fire, Reload
Input/Contexts/OnFoot.jsonl       keys / mouse / gamepad for each action   키·마우스·패드
ProjectSettings/Project.json      name, version, start scene, window       이름·버전·시작 장면·창
Game.csproj                       builds Assets/**/*.cs into .cache/Scripts/ (the editor reloads it while running)
```

The player is `Player` (Rigidbody **Character** + capsule Collider + `FPSController` + `Weapon`) → child `Head`
(turns left / right) → child `Eyes` (Camera + AudioListener, looks up / down). Bullets are 12 objects named
`Bullet` parked at y = -60 with gravity scale 0; `Weapon` moves one to the muzzle and sets its velocity.
Targets are objects named `Target` (Rigidbody Dynamic + `Target`).

| Request (요청) | Change (고칠 곳) |
|---|---|
| "Make the gun a shotgun" 총을 샷건으로 | `Weapon.weapon = "shotgun"` on Player in the scene (`"Weapon":{"weapon":"shotgun"}`), or edit the `rifle` row in `Data/Weapons.table.jsonl` (`pellets`, `spreadDegrees`) |
| Faster fire / bigger magazine 연사·탄창 | `fireRate`, `magazine` in `Data/Weapons.table.jsonl` |
| New weapon 새 무기 | add a row to `Data/Weapons.table.jsonl`, set `Weapon.weapon` to its id |
| Run faster / jump higher 달리기·점프 | `FPSController` fields `sprintSpeed`, `jumpSpeed` (Inspector or the scene line) |
| Other keys 키 바꾸기 | `Input/Contexts/OnFoot.jsonl` (`{"action":"Jump","key":"F"}`); never key codes in scripts |
| More targets 표적 추가 | copy a `Target` line in `Scenes/Main.scene` with a new `id` and `pos`; `points` sets its score |
| More bullets in the air 총알 더 | copy a `Bullet` line (new id, another x at y = -60) |
| Change the map 맵 바꾸기 | greybox objects in `Scenes/Main.scene` (`Wall`, `Ramp`, `Platform`, `Crate`): `pos`, `rot`, `scale`; a `Collider` makes them solid |
| HUD text / place 화면 글자·위치 | the `UIElement` lines under `HUD` in the scene (`anchor`, `position`, `size`, `fontSize`); from C# `UI.Text("score").text = ...` |

Rules: game code reads **actions**, not keys; numbers that designers tune live in **data files**; one behaviour per
job (player, weapon, target, rules). After changing a script: `dotnet build Game.csproj` (the editor reloads it).
Check a change: `bdg build --check`, then `bdg build` and run `Builds/Windows-Development/<name>.exe`.

## Licence

Template code: 0BSD (use it any way, no notice needed). Template assets (the scene, sounds, data): CC0.
See LICENSE-TEMPLATE.txt. The Bidulgi Engine itself is MIT (THIRD-PARTY-NOTICES.txt comes with every build).

## Left for later (다음 단계)

Prefabs (queue 6-1): the player, targets and bullets become prefabs. Crouch, aim down sights, health and damage,
hit effects (VFX), hitscan with physics raycasts, arms mesh.
