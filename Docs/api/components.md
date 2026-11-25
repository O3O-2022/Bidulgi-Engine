# 컴포넌트 레퍼런스

장면 파일(`.scene`, 한 줄에 물체 하나)에 쓰는 컴포넌트와 필드입니다. `bdg schema <이름>`과 같은 내용이고,
`bdg set <id> 컴포넌트.필드=값`, `bdg addcomp <id> <컴포넌트>`로 바꿉니다. 이 파일은 `Tools/gen_llms.py`가 만듭니다.

## Animator

| 필드 | 타입 | 기본값 |
|---|---|---|
| `graph` | string | — |
| `clip` | string | — |
| `speed` | float | `1` |
| `crossFade` | float | `0.2` |
| `rootMotion` | bool | `false` |

## AnimatorIK

| 필드 | 타입 | 기본값 |
|---|---|---|
| `footIK` | bool | `true` |
| `leftFoot` | string | `LeftFoot` |
| `rightFoot` | string | `RightFoot` |
| `maxStep` | float | `0.45` |
| `footHeight` | float | `0` |
| `lookAt` | string | — |
| `head` | string | `Head` |
| `lookWeight` | float | `1` |
| `aimSpine` | float | `0.4` |
| `maxLookAngle` | float | `75` |
| `reachTarget` | string | — |
| `hand` | string | `RightHand` |
| `reachWeight` | float | `1` |
| `chainEnd` | string | — |
| `chainLength` | int | `3` |
| `chainTarget` | string | — |
| `smoothing` | float | `14` |

## AudioListener

| 필드 | 타입 | 기본값 |
|---|---|---|
| `volume` | float | `1` |

## AudioSource

| 필드 | 타입 | 기본값 |
|---|---|---|
| `clip` | string | — |
| `group` | string | `Effects` |
| `volume` | float | `1` |
| `pitch` | float | `1` |
| `loop` | bool | `false` |
| `playOnStart` | bool | `true` |
| `spatial` | bool | `true` |
| `minDistance` | float | `1` |
| `maxDistance` | float | `50` |
| `rolloff` | AudioRolloff | `Inverse` |
| `rolloffFactor` | float | `1` |

## Blockout

| 필드 | 타입 | 기본값 |
|---|---|---|
| `shape` | BlockoutShape | `Box` |
| `size` | vec3 | `1,1,1` |
| `steps` | int | `6` |
| `material` | string | — |
| `collider` | bool | `true` |

## Camera

| 필드 | 타입 | 기본값 |
|---|---|---|
| `fov` | float | `60` |
| `nearClip` | float | `0.1` |
| `farClip` | float | `1000` |

## CameraBookmark

| 필드 | 타입 | 기본값 |
|---|---|---|
| `slot` | int | `1` |
| `pivot` | vec3 | `0,0,0` |
| `yaw` | float | `35` |
| `pitch` | float | `-24` |
| `distance` | float | `11` |
| `tier` | string | — |

## CameraShakeSource

| 필드 | 타입 | 기본값 |
|---|---|---|
| `shake` | string | `bdg:Medium` |
| `scale` | float | `1` |
| `innerRadius` | float | `2` |
| `outerRadius` | float | `20` |
| `falloff` | float | `1` |
| `playOnStart` | bool | `false` |

## Collider

| 필드 | 타입 | 기본값 |
|---|---|---|
| `shape` | ColliderShape | `Auto` |
| `center` | vec3 | `0,0,0` |
| `size` | vec3 | `1,1,1` |
| `radius` | float | `0.5` |
| `height` | float | `2` |
| `isTrigger` | bool | `false` |

## Collider2D

| 필드 | 타입 | 기본값 |
|---|---|---|
| `shape` | Collider2DShape | `Auto` |
| `center` | vec2 | `0,0` |
| `size` | vec2 | `1,1` |
| `radius` | float | `0.5` |
| `height` | float | `2` |
| `isTrigger` | bool | `false` |

## ColorGrading

| 필드 | 타입 | 기본값 |
|---|---|---|
| `lut` | string | — |
| `intensity` | float | `1` |
| `contrast` | float | `1` |
| `saturation` | float | `1` |
| `temperature` | float | `0` |
| `tint` | float | `0` |

## Health

| 필드 | 타입 | 기본값 |
|---|---|---|
| `maxHealth` | int | `100` |
| `current` | int | `100` |

## Light

| 필드 | 타입 | 기본값 |
|---|---|---|
| `type` | LightType | `Point` |
| `color` | color | `1,1,1,1` |
| `intensity` | float | `1` |
| `range` | float | `10` |
| `spotAngle` | float | `45` |
| `innerSpotAngle` | float | `30` |
| `castShadows` | bool | `true` |
| `lightShafts` | bool | `false` |
| `lightShaftIntensity` | float | `1` |

## LightProbeVolume

| 필드 | 타입 | 기본값 |
|---|---|---|
| `size` | vec3 | `10,4,10` |
| `countX` | int | `4` |
| `countY` | int | `2` |
| `countZ` | int | `4` |
| `intensity` | float | `1` |

## MeshRenderer

| 필드 | 타입 | 기본값 |
|---|---|---|
| `mesh` | string | — |
| `material` | string | — |
| `bakeLightmap` | bool | `false` |

## ParticleSystem

| 필드 | 타입 | 기본값 |
|---|---|---|
| `effect` | string | — |
| `playOnStart` | bool | `true` |
| `loop` | ParticleLoop | `Effect` |
| `seed` | int | `0` |

## PhysicsMaterial

| 필드 | 타입 | 기본값 |
|---|---|---|
| `friction` | float | `0.6` |
| `bounciness` | float | `0` |

## PlayerMove

| 필드 | 타입 | 기본값 |
|---|---|---|
| `speed` | float | `5` |
| `jumpHeight` | float | `4.5` |

## PostProcessVolume

| 필드 | 타입 | 기본값 |
|---|---|---|
| `unbound` | bool | `true` |
| `size` | vec3 | `10,10,10` |
| `blendRadius` | float | `2` |
| `blendWeight` | float | `1` |
| `priority` | int | `0` |
| `exposureCompensation` | float | `0` |
| `autoExposure` | bool | `false` |
| `autoMinEV` | float | `-2` |
| `autoMaxEV` | float | `2` |
| `adaptationSpeed` | float | `2` |
| `bloomIntensity` | float | `1` |
| `vignette` | float | `0` |
| `vignetteSmoothness` | float | `0.5` |
| `chromaticAberration` | float | `0` |
| `filmGrain` | float | `0` |
| `lensFlare` | float | `0` |
| `dofBlur` | float | `0` |
| `dofFocusDistance` | float | `10` |
| `dofFocusRange` | float | `2` |
| `fogDensity` | float | `0` |
| `fogHeight` | float | `0` |
| `fogHeightFalloff` | float | `0.2` |
| `fogAlbedo` | color | `1,1,1,1` |
| `fogAnisotropy` | float | `0.6` |
| `fogDistance` | float | `64` |
| `fogLightScattering` | float | `1` |

## QuestLogic

| 필드 | 타입 | 기본값 |
|---|---|---|
| `questId` | string | `q_intro` |

## Ragdoll

| 필드 | 타입 | 기본값 |
|---|---|---|
| `active` | bool | `false` |
| `mass` | float | `60` |
| `swingLimit` | float | `50` |
| `twistLimit` | float | `25` |
| `thickness` | float | `1` |

## ReflectionProbe

| 필드 | 타입 | 기본값 |
|---|---|---|
| `size` | vec3 | `10,4,10` |
| `boxProjection` | bool | `true` |
| `intensity` | float | `1` |

## Rigidbody

| 필드 | 타입 | 기본값 |
|---|---|---|
| `type` | RigidbodyType | `Dynamic` |
| `simulation` | PhysicsSimulation | `Auto` |
| `mass` | float | `1` |
| `linearDamping` | float | `0.05` |
| `angularDamping` | float | `0.05` |
| `gravityScale` | float | `1` |
| `freezeRotation` | bool | `false` |
| `velocity` | vec3 | `0,0,0` |
| `angularVelocity` | vec3 | `0,0,0` |

## Rigidbody2D

| 필드 | 타입 | 기본값 |
|---|---|---|
| `type` | RigidbodyType | `Dynamic` |
| `mass` | float | `1` |
| `linearDamping` | float | `0` |
| `angularDamping` | float | `0.05` |
| `gravityScale` | float | `1` |
| `freezeRotation` | bool | `false` |
| `velocity` | vec2 | `0,0` |
| `angularVelocity` | float | `0` |

## Rotator

| 필드 | 타입 | 기본값 |
|---|---|---|
| `speed` | vec3 | `0,45,0` |

## Sky

| 필드 | 타입 | 기본값 |
|---|---|---|
| `zenith` | color | `0.16,0.32,0.62,1` |
| `horizon` | color | `0.62,0.7,0.8,1` |
| `ground` | color | `0.2,0.18,0.16,1` |
| `intensity` | float | `1` |
| `sunSize` | float | `1.5` |

## Terrain

| 필드 | 타입 | 기본값 |
|---|---|---|
| `asset` | string | — |
| `collider` | bool | `true` |
| `lodDistance` | float | `40` |
| `foliage` | bool | `true` |
| `wind` | float | `1` |
| `brush` | TerrainBrush | `Raise` |
| `brushRadius` | float | `6` |
| `brushStrength` | float | `0.5` |
| `brushLayer` | int | `0` |

## Transform

| 필드 | 타입 | 기본값 |
|---|---|---|
| `position` | vec3 | `0,0,0` |
| `rotation` | vec3 | `0,0,0` |
| `scale` | vec3 | `1,1,1` |

## UICanvas

| 필드 | 타입 | 기본값 |
|---|---|---|
| `referenceSize` | vec2 | `1920,1080` |
| `matchHeight` | float | `1` |
| `order` | int | `0` |
| `visible` | bool | `true` |

## UIElement

| 필드 | 타입 | 기본값 |
|---|---|---|
| `kind` | UIKind | `Text` |
| `anchor` | UIAnchor | `TopLeft` |
| `position` | vec2 | `0,0` |
| `size` | vec2 | `300,60` |
| `text` | string | — |
| `fontSize` | float | `36` |
| `align` | UITextAlign | `Center` |
| `textColor` | color | `1,1,1,1` |
| `color` | color | `0,0,0,0` |
| `fillColor` | color | `0.35,0.8,0.4,1` |
| `value` | float | `1` |
| `image` | string | — |
| `cornerRadius` | float | `8` |
| `visible` | bool | `true` |
| `interactable` | bool | `true` |

## WorldSettings

| 필드 | 타입 | 기본값 |
|---|---|---|
| `fog` | bool | `false` |
| `fogColor` | color | `0.62,0.7,0.8,1` |
| `fogDensity` | float | `0.02` |
| `fogStart` | float | `0` |
| `exposure` | float | `1` |
