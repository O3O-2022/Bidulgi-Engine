# 컴포넌트 레퍼런스

장면 파일(`.scene`, 한 줄에 물체 하나)에 쓰는 컴포넌트와 필드입니다. `bdg schema <이름>`과 같은 내용이고,
`bdg set <id> 컴포넌트.필드=값`, `bdg addcomp <id> <컴포넌트>`로 바꿉니다. 이 파일은 `tools/gen_llms.py`가 만듭니다.
이름은 엔진 용어라 영어 그대로 쓰고, 한국어 이름은 에디터 툴팁과 같은 `design-system/project/editor.ko.json`에서 옵니다.

## Animator (애니메이터)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `graph` | 애니메이션 그래프 | string | — |
| `clip` | 클립 | string | — |
| `speed` | 속도 | float | `1` |
| `crossFade` | 전환 시간 | float | `0.2` |
| `rootMotion` | 루트 모션 | bool | `false` |

## AnimatorIK (애니메이터 IK)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `footIK` | 발 IK | bool | `true` |
| `leftFoot` | 왼발 뼈 | string | `LeftFoot` |
| `rightFoot` | 오른발 뼈 | string | `RightFoot` |
| `maxStep` | 최대 단차 | float | `0.45` |
| `footHeight` | 발 높이 | float | `0` |
| `lookAt` | 바라볼 대상 | string | — |
| `head` | 머리 뼈 | string | `Head` |
| `lookWeight` | 바라보기 비율 | float | `1` |
| `aimSpine` | 척추로 조준 | float | `0.4` |
| `maxLookAngle` | 최대 바라보기 각도 | float | `75` |
| `reachTarget` | 손 뻗을 대상 | string | — |
| `hand` | 손 뼈 | string | `RightHand` |
| `reachWeight` | 손 뻗기 비율 | float | `1` |
| `chainEnd` | 체인 끝 뼈 | string | — |
| `chainLength` | 체인 길이 | int | `3` |
| `chainTarget` | 체인 대상 | string | — |
| `smoothing` | 부드러움 | float | `14` |

## AudioListener (오디오 리스너(귀))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `volume` | 전체 음량 | float | `1` |

## AudioSource (오디오 소스)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `clip` | 클립 | string | — |
| `group` | 믹서 그룹 | string | `Effects` |
| `volume` | 음량 | float | `1` |
| `pitch` | 음높이 | float | `1` |
| `loop` | 반복 | bool | `false` |
| `playOnStart` | 시작할 때 재생 | bool | `true` |
| `spatial` | 3D 소리 | bool | `true` |
| `minDistance` | 최소 거리 | float | `1` |
| `maxDistance` | 최대 거리 | float | `50` |
| `rolloff` | 거리 감쇠 방식 | AudioRolloff | `Inverse` |
| `rolloffFactor` | 거리 감쇠 배율 | float | `1` |

## Blockout (블록아웃(회색 상자))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `shape` | 모양 | BlockoutShape | `Box` |
| `size` | 크기 | vec3 | `1,1,1` |
| `steps` | 계단 수 | int | `6` |
| `material` | 머티리얼 | string | — |
| `collider` | 충돌체 | bool | `true` |

## Camera (카메라)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `fov` | 시야각 | float | `60` |
| `nearClip` | 가까운 잘림 거리 | float | `0.1` |
| `farClip` | 먼 잘림 거리 | float | `1000` |

## CameraBookmark (카메라 북마크)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `slot` | 번호 | int | `1` |
| `pivot` | 중심점 | vec3 | `0,0,0` |
| `yaw` | 좌우 각도 | float | `35` |
| `pitch` | 위아래 각도 | float | `-24` |
| `distance` | 거리 | float | `11` |
| `tier` | 품질 단계 | string | — |

## CameraShakeSource (카메라 흔들림 원천)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `shake` | 흔들림 | string | `bdg:Medium` |
| `scale` | 세기 배율 | float | `1` |
| `innerRadius` | 안쪽 반경 | float | `2` |
| `outerRadius` | 바깥 반경 | float | `20` |
| `falloff` | 감쇠 | float | `1` |
| `playOnStart` | 시작할 때 재생 | bool | `false` |

## Collider (충돌체(3D))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `shape` | 모양 | ColliderShape | `Auto` |
| `center` | 중심 | vec3 | `0,0,0` |
| `size` | 크기 | vec3 | `1,1,1` |
| `radius` | 반지름 | float | `0.5` |
| `height` | 높이 | float | `2` |
| `isTrigger` | 트리거 | bool | `false` |

## Collider2D (충돌체(2D))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `shape` | 모양 | Collider2DShape | `Auto` |
| `center` | 중심 | vec2 | `0,0` |
| `size` | 크기 | vec2 | `1,1` |
| `radius` | 반지름 | float | `0.5` |
| `height` | 높이 | float | `2` |
| `isTrigger` | 트리거 | bool | `false` |

## ColorGrading (색 보정)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `lut` | LUT | string | — |
| `intensity` | 강도 | float | `1` |
| `contrast` | 대비 | float | `1` |
| `saturation` | 채도 | float | `1` |
| `temperature` | 색온도 | float | `0` |
| `tint` | 색조 | float | `0` |

## GameMode (게임 모드)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `playerPrefab` | 플레이어 프리팹 | string | — |
| `hostHasPlayer` | 호스트도 플레이 | bool | `true` |
| `maxPlayers` | 최대 인원 | int | `8` |

## Health (체력)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `maxHealth` | 최대 체력 | int | `100` |
| `current` | 현재 체력 | int | `100` |

## Light (라이트)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `type` | 종류 | LightType | `Point` |
| `color` | 색 | color | `1,1,1,1` |
| `intensity` | 강도 | float | `1` |
| `range` | 범위 | float | `10` |
| `spotAngle` | 스포트 각도 | float | `45` |
| `innerSpotAngle` | 스포트 안쪽 각도 | float | `30` |
| `castShadows` | 그림자 드리우기 | bool | `true` |
| `lightShafts` | 빛줄기 | bool | `false` |
| `lightShaftIntensity` | 빛줄기 강도 | float | `1` |

## LightProbeVolume (라이트 프로브 볼륨)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `size` | 크기 | vec3 | `10,4,10` |
| `countX` | X 개수 | int | `4` |
| `countY` | Y 개수 | int | `2` |
| `countZ` | Z 개수 | int | `4` |
| `intensity` | 강도 | float | `1` |

## MeshRenderer (메시 렌더러)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `mesh` | 메시 | string | — |
| `material` | 머티리얼 | string | — |
| `bakeLightmap` | 라이트맵 굽기 | bool | `false` |

## NetworkObject (네트워크 오브젝트)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `relevancy` | 관심 거리 | float | `0` |
| `destroyWithOwner` | 주인이 나가면 삭제 | bool | `true` |

## NetworkTransform (네트워크 트랜스폼)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `sendRate` | 초당 전송 횟수 | float | `30` |
| `ownerPredicted` | 주인 예측 | bool | `true` |
| `maxSpeed` | 최대 속도(치트 방지) | float | `20` |

## ParticleSystem (파티클 시스템)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `effect` | 효과 | string | — |
| `playOnStart` | 시작할 때 재생 | bool | `true` |
| `loop` | 반복 | ParticleLoop | `Effect` |
| `seed` | 난수 시드 | int | `0` |

## PhysicsMaterial (물리 재질)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `friction` | 마찰 | float | `0.6` |
| `bounciness` | 튀는 정도 | float | `0` |

## PlayerMove (플레이어 이동)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `speed` | 속도 | float | `5` |
| `jumpHeight` | 점프 높이 | float | `4.5` |

## PlayerStart (플레이어 시작 위치)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `order` | 순서 | int | `0` |

## PostProcessVolume (후처리 볼륨)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `unbound` | 범위 없음(장면 전체) | bool | `true` |
| `size` | 크기 | vec3 | `10,10,10` |
| `blendRadius` | 섞임 반경 | float | `2` |
| `blendWeight` | 섞임 비율 | float | `1` |
| `priority` | 우선순위 | int | `0` |
| `exposureCompensation` | 노출 보정 | float | `0` |
| `autoExposure` | 자동 노출 | bool | `false` |
| `autoMinEV` | 자동 노출 최소 EV | float | `-2` |
| `autoMaxEV` | 자동 노출 최대 EV | float | `2` |
| `adaptationSpeed` | 적응 속도 | float | `2` |
| `bloomIntensity` | 블룸 강도 | float | `1` |
| `vignette` | 비네트 | float | `0` |
| `vignetteSmoothness` | 비네트 부드러움 | float | `0.5` |
| `chromaticAberration` | 색수차 | float | `0` |
| `filmGrain` | 필름 그레인 | float | `0` |
| `lensFlare` | 렌즈 플레어 | float | `0` |
| `dofBlur` | 피사계 심도 흐림 | float | `0` |
| `dofFocusDistance` | 초점 거리 | float | `10` |
| `dofFocusRange` | 초점 범위 | float | `2` |
| `fogDensity` | 볼류메트릭 안개 농도 | float | `0` |
| `fogHeight` | 안개 높이 | float | `0` |
| `fogHeightFalloff` | 안개 높이 감쇠 | float | `0.2` |
| `fogAlbedo` | 안개 반사율 | color | `1,1,1,1` |
| `fogAnisotropy` | 안개 산란 방향성 | float | `0.6` |
| `fogDistance` | 안개 거리 | float | `64` |
| `fogLightScattering` | 안개 빛 산란 | float | `1` |

## QuestLogic (퀘스트 로직)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `questId` | 퀘스트 id | string | `q_intro` |

## Ragdoll (래그돌)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `active` | 켜기 | bool | `false` |
| `mass` | 질량 | float | `60` |
| `swingLimit` | 흔들림 한계 | float | `50` |
| `twistLimit` | 비틀림 한계 | float | `25` |
| `thickness` | 두께 | float | `1` |

## ReflectionProbe (반사 프로브)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `size` | 크기 | vec3 | `10,4,10` |
| `boxProjection` | 상자 투영 | bool | `true` |
| `intensity` | 강도 | float | `1` |

## Rigidbody (강체(3D 물리))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `type` | 종류 | RigidbodyType | `Dynamic` |
| `simulation` | 시뮬레이션 | PhysicsSimulation | `Auto` |
| `mass` | 질량 | float | `1` |
| `linearDamping` | 이동 감쇠 | float | `0.05` |
| `angularDamping` | 회전 감쇠 | float | `0.05` |
| `gravityScale` | 중력 배율 | float | `1` |
| `freezeRotation` | 회전 고정 | bool | `false` |
| `velocity` | 속도 | vec3 | `0,0,0` |
| `angularVelocity` | 회전 속도 | vec3 | `0,0,0` |

## Rigidbody2D (강체(2D 물리))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `type` | 종류 | RigidbodyType | `Dynamic` |
| `mass` | 질량 | float | `1` |
| `linearDamping` | 이동 감쇠 | float | `0` |
| `angularDamping` | 회전 감쇠 | float | `0.05` |
| `gravityScale` | 중력 배율 | float | `1` |
| `freezeRotation` | 회전 고정 | bool | `false` |
| `velocity` | 속도 | vec2 | `0,0` |
| `angularVelocity` | 회전 속도 | float | `0` |

## Rotator (회전기)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `speed` | 속도 | vec3 | `0,45,0` |

## Sky (하늘)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `zenith` | 천정 색 | color | `0.16,0.32,0.62,1` |
| `horizon` | 지평선 색 | color | `0.62,0.7,0.8,1` |
| `ground` | 땅 색 | color | `0.2,0.18,0.16,1` |
| `intensity` | 강도 | float | `1` |
| `sunSize` | 해 크기 | float | `1.5` |

## Terrain (지형)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `asset` | 지형 에셋 | string | — |
| `collider` | 충돌체 | bool | `true` |
| `lodDistance` | LOD 거리 | float | `40` |
| `foliage` | 풀·나무 | bool | `true` |
| `wind` | 바람 | float | `1` |
| `brush` | 브러시 | TerrainBrush | `Raise` |
| `brushRadius` | 브러시 반지름 | float | `6` |
| `brushStrength` | 브러시 세기 | float | `0.5` |
| `brushLayer` | 브러시 레이어 | int | `0` |

## Transform (트랜스폼(위치·회전·크기))

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `position` | 위치 | vec3 | `0,0,0` |
| `rotation` | 회전 | vec3 | `0,0,0` |
| `scale` | 크기 | vec3 | `1,1,1` |

## UICanvas (UI 캔버스)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `referenceSize` | 기준 해상도 | vec2 | `1920,1080` |
| `matchHeight` | 높이 맞춤 비율 | float | `1` |
| `order` | 그리는 순서 | int | `0` |
| `visible` | 보이기 | bool | `true` |

## UIElement (UI 요소)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `kind` | 종류 | UIKind | `Text` |
| `anchor` | 앵커 | UIAnchor | `TopLeft` |
| `position` | 위치 | vec2 | `0,0` |
| `size` | 크기 | vec2 | `300,60` |
| `text` | 글자 | string | — |
| `fontSize` | 글자 크기 | float | `36` |
| `align` | 정렬 | UITextAlign | `Center` |
| `textColor` | 글자 색 | color | `1,1,1,1` |
| `color` | 바탕 색 | color | `0,0,0,0` |
| `fillColor` | 채움 색 | color | `0.35,0.8,0.4,1` |
| `value` | 값 | float | `1` |
| `placeholder` | 안내 글자 | string | — |
| `options` | 선택지 | string | — |
| `scroll` | 스크롤 | vec2 | `0,0` |
| `image` | 이미지 | string | — |
| `cornerRadius` | 모서리 둥글기 | float | `8` |
| `slice` | 9-slice 테두리 | float | `0` |
| `layout` | 자식 배치 | UILayout | `None` |
| `spacing` | 간격 | float | `8` |
| `padding` | 안쪽 여백 | float | `0` |
| `cellSize` | 칸 크기 | vec2 | `100,100` |
| `visible` | 보이기 | bool | `true` |
| `interactable` | 누를 수 있음 | bool | `true` |

## WorldSettings (월드 설정)

| 필드 | 한국어 | 타입 | 기본값 |
|---|---|---|---|
| `fog` | 안개 | bool | `false` |
| `fogColor` | 안개 색 | color | `0.62,0.7,0.8,1` |
| `fogDensity` | 안개 농도 | float | `0.02` |
| `fogStart` | 안개 시작 거리 | float | `0` |
| `exposure` | 노출 | float | `1` |
