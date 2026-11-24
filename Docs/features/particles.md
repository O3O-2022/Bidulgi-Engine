# 파티클(VFX) 구조 설계 (2026-11-25, E-10)

> 상태: **설계안 — 사용자 검토 대기.** 아래 "결정이 필요한 것"에 답을 받은 뒤 P-1부터 구현합니다.
> 참고한 공식 문서: Unreal Niagara(시스템·에미터·모듈 스택, Spawn/Update 단계, 파라미터), Unity(Shuriken 모듈, VFX Graph), Godot(GPUParticles3D·ParticleProcessMaterial).

## 목표

- **언리얼 Niagara처럼 쌓아 만드는 구조**(모듈 스택): 처음 쓰는 사람도 위에서 아래로 읽으면 이해되고, AI도 텍스트로 정확히 고칠 수 있게.
- **CPU와 GPU 경로가 같은 에셋을 씀**: 작은 이펙트(불꽃, 먼지, 피격)는 CPU, 대량(비, 눈, 군집 불티)은 GPU. 에셋은 경로를 고르는 칸 하나만 다름.
- **품질 단계·플랫폼 예산과 연결**(3-5, 3-7): 감자 단계에서도 게임이 깨지지 않고, 예산 막대에 파티클 수·비용이 보임.
- **결정성**: 시드가 같으면 같은 결과(리플레이·네트워크·테스트 이미지 비교).

## 구조

```
VfxSystem (.vfx 에셋)          ← 장면에 놓는 단위 (ParticleSystem 컴포넌트가 가리킴)
 ├─ 시스템 파라미터              (사용자 변수: Color, Intensity, Radius … — 코드·Graph에서 바꿈)
 └─ Emitter × N                  (불꽃, 연기, 불티 …)
     ├─ 설정: sim = cpu | gpu, maxParticles, local/world 좌표, bounds, 시드
     ├─ Emitter Spawn  (시작할 때 1번)      : 
     ├─ Emitter Update (프레임마다)          : SpawnRate, SpawnBurst, 루프·지속 시간
     ├─ Particle Spawn (태어날 때 1번)       : Lifetime, Position(점·구·원뿔·메시 표면), Velocity, Size, Color, Rotation
     ├─ Particle Update (프레임마다)         : Gravity, Force, Drag, CurlNoise, ColorOverLife, SizeOverLife,
     │                                         Collision(평면·깊이 버퍼·물리), KillVolume, Attractor
     ├─ Event Handlers                        : OnDeath / OnCollision → 다른 에미터에서 생성 (서브 에미터)
     └─ Renderers                             : Sprite(빌보드·속도 방향·고정 축), Mesh, Ribbon(트레일), Light
```

- **모듈 = 작은 함수 하나**(입력 값 몇 개 → 파티클 속성 변경). 내장 모듈은 C++(CPU) + GLSL(GPU) 두 구현을 같은 이름·같은 입력으로 둡니다. 순서는 스택 순서 그대로.
- **값 종류**(Niagara의 dynamic input과 같음): 상수, 범위 랜덤(min..max), **커브**(수명 0..1에 따른 값), **그라디언트**(색), 시스템 파라미터 참조(`@Intensity`), 간단한 식(`@Radius * 2`, E-4의 `Core/Expr` 확장).
- **파티클 속성**은 고정 세트(위치·속도·색·크기·회전·수명·나이·시드) + 모듈이 필요할 때 추가(`Mass`, `Custom1`). 메모리는 속성별 배열(SoA) — CPU는 SIMD, GPU는 구조화 버퍼에 그대로.

## 파일 형식 (.vfx, JSONL — 장면과 같은 규칙)

```jsonl
{"vfx":"Explosion","params":{"Color":[1,0.6,0.2,1],"Intensity":1}}
{"emitter":"Fire","sim":"cpu","max":200,"space":"world"}
{"stage":"emitter-update","module":"SpawnBurst","count":80}
{"stage":"particle-spawn","module":"Lifetime","value":[0.4,0.9]}
{"stage":"particle-spawn","module":"VelocityCone","angle":35,"speed":[3,7]}
{"stage":"particle-update","module":"Gravity","value":[0,-4,0]}
{"stage":"particle-update","module":"ColorOverLife","gradient":[[0,"@Color"],[0.6,[0.3,0.1,0.05,1]],[1,[0,0,0,0]]]}
{"stage":"render","module":"Sprite","material":"M_Flame","facing":"camera","flipbook":[4,4]}
{"emitter":"Sparks","sim":"gpu","max":2000}
…
```

- 한 줄 = 한 모듈: 순서 바꾸기·끄기(`"enabled":false`)가 한 줄 수정이고 diff가 깔끔.
- `bdg vfx view Explosion`은 스택을 들여쓰기로 보여 줌, `bdg validate`가 모르는 모듈·잘못된 단계·예산 초과를 알려 줌.

## 시뮬레이션 경로

| | CPU | GPU (컴퓨트) |
|---|---|---|
| 개수 | 에미터당 수백~수천 | 수만~수십만 |
| 충돌 | 물리(Jolt) 레이캐스트, 평면 | 깊이 버퍼(화면에 보이는 것), 평면, SDF(나중) |
| 이벤트·서브 에미터 | 지원 | 생성 수 카운터로 제한적 지원 |
| 결정성 | 완전 | 같은 GPU에서만 |
| 플랫폼 | 전부 | 컴퓨트 셰이더가 있는 곳(데스크톱·최근 모바일), 없으면 CPU로 자동 대체 + 경고 |

- 고정 타임스텝(기본 60 Hz, 느려지면 최대 4스텝) + 렌더 보간. 화면 밖·멀리 있으면 업데이트 간격을 늘림(LOD), bounds가 절두체 밖이면 그리기 생략(3-10 컬링 재사용).
- 워밍업(`warmup: 2s`)으로 시작부터 꽉 찬 연기.

## 렌더링

- 알파 블렌드는 에미터 단위 정렬 + (옵션) 파티클 단위 정렬, 가산·곱셈 블렌드, **소프트 파티클**(깊이 사전 패스로 벽과 닿는 곳을 부드럽게 — 3-5b·E-6 재사용), 플립북 아틀라스, 조명 받는 파티클(구형 법선 근사, Forward+ 라이트 목록 재사용), 그림자는 고사양 옵션.
- 반 해상도 투명 패스(초고사양 끔): 연기가 화면을 덮을 때 비용 절반.

## 품질 단계·예산

| 단계 | 최대 파티클(장면 전체) | GPU 경로 | 소프트 파티클 | 조명 |
|---|---|---|---|---|
| 감자 | 500 | 끔(CPU 대체) | 끔 | 끔 |
| 저사양 | 2,000 | 끔 | 끔 | 끔 |
| 중사양 | 10,000 | 켬 | 켬 | 정점 |
| 고사양 | 100,000 | 켬 | 켬 | 픽셀 |
| 초고사양 | 500,000 | 켬 | 켬 | 픽셀 + 그림자 |

- 에미터마다 `scalability`: 단계별 개수 배율·끄기. 예산 칩(3-7)에 "파티클 수·시뮬 ms" 막대 추가, 초과하면 무거운 에미터를 골라 줌.

## 에디터

- **VFX 편집기**(에셋을 열면): 왼쪽 모듈 스택(Niagara처럼 단계별 접기, 끌어서 순서 바꾸기, 체크로 끄기, 모듈 추가 검색), 가운데 미리보기 뷰포트(재생·일시정지·한 프레임·처음부터·시간 배율 — 3-6b 도구 재사용), 오른쪽 선택 모듈 인스펙터(E-4 칸 편의 그대로 + **커브·그라디언트 편집기** 새로).
- 장면에서 `ParticleSystem` 컴포넌트를 고르면 씬 뷰에서 바로 재생(플레이 없이), bounds 상자·에미터 이름 표시.
- 통계: 에미터별 살아 있는 개수, CPU/GPU ms, 그리기 수.

## API (모든 언어 같은 이름 — easy-api.md)

- `Vfx.Play("Explosion", pos, {Color = red})` → 핸들, `handle.Stop()`, `handle.Set("Intensity", 2)`.
- `ParticleSystem` 컴포넌트: asset, autoplay, loop, seed, 파라미터 덮어쓰기. Graph 노드는 E-8 리플렉션으로 자동(`ParticleSystem.Play`, `ParticleSystem.SetParameter`).

## 단계 (사용자 검토 후)

| 단계 | 내용 | 완료 조건 |
|---|---|---|
| **P-1** | CPU 시뮬 + Sprite 렌더 + 기본 모듈 12개(Spawn Rate/Burst, Lifetime, Position 점·구·원뿔, Velocity, Size, Color, Gravity, Drag, Color/Size over life) + `.vfx` 형식 + `ParticleSystem` 컴포넌트 + 씬 뷰 재생 | 불꽃·연기·불티 예제 3개가 모든 품질 단계에서 보이고, 같은 시드 = 같은 이미지 |
| **P-2** | VFX 편집기(모듈 스택·미리보기·커브/그라디언트) + `bdg vfx view` + 예산 막대 | 편집기에서 예제를 처음부터 만들 수 있음 |
| **P-3** | GPU 경로(같은 모듈의 컴퓨트 버전), 깊이 충돌, 소프트 파티클, 반 해상도 | 10만 파티클 고사양 60 fps(기준 GPU), CPU와 GPU 결과 모양이 같음 |
| **P-4** | Mesh·Ribbon·Light 렌더러, 물리 충돌, 이벤트·서브 에미터, 커얼 노이즈 | 총알 궤적(리본)·파편(메시)·폭발 섬광(라이트) 예제 |
| **P-5** | Graph로 커스텀 모듈(Niagara Scratch Pad처럼), 시스템 파라미터 Graph 연결 | 사용자 모듈 하나가 CPU·GPU 양쪽에서 동작 |

## 결정이 필요한 것 (사용자)

1. **편집 방식**: 위 **모듈 스택**(Niagara·Shuriken식, 추천 — 배우기 쉽고 AI가 텍스트로 고치기 쉬움)으로 먼저 가고 노드 그래프는 P-5의 커스텀 모듈에서만 쓸지, 처음부터 Unity VFX Graph처럼 노드 그래프로 갈지.
2. **GPU 경로 시기**: P-3(편집기 다음, 추천)로 둘지, 대량 파티클이 급하면 P-1 직후로 당길지.
3. **첫 예제 세트**: 불꽃·연기·불티(추천) 외에 꼭 먼저 필요한 이펙트(비·눈, 총구 화염, 마법 등)가 있는지.
