# 머티리얼과 텍스처 (언리얼식)

작성 2026-11-22. 사용 방식은 언리얼의 **머티리얼 → 머티리얼 인스턴스** 구조를 따르고, 언리얼의 불편한 점(셰이더 컴파일 대기, 순열 폭발, 바이너리 에셋)은 고칩니다.

## 1. 구조

```
Master Material (.material)          노드 그래프로 셰이더를 만듦 (아티스트·테크니컬 아티스트)
   └─ Material Instance (.matinst)   파라미터 값만 바꿈, 컴파일 없음 (대부분의 사용자)
        └─ Material Instance         인스턴스의 인스턴스도 가능 (상속)
Dynamic Instance (실행 중)           스크립트에서 값 변경: SetFloat("Wetness", 0.7)
```

| 언리얼 | 비둘기 엔진 |
|---|---|
| Material / Material Instance Constant / MID | `.material` / `.matinst` / 실행 중 인스턴스 |
| Material Editor (노드) | **그래프 에디터 공통 화면** (Graph·애니메이션 그래프와 같은 조작) |
| Material Function | 머티리얼 함수 (`.matfunc`), 패키지로 공유 |
| Material Parameter Collection | 전역 파라미터 묶음 (날씨, 시간대 등) |
| Static Switch | **최소화** (아래 "순열 관리") |

## 2. 머티리얼 그래프

- **출력 노드:** Base Color, Metallic, Roughness, Normal, Emissive, Opacity, AO, World Offset(정점 이동), Subsurface.
- **셰이딩 모델:** Default Lit(PBR), Unlit, Subsurface, Clear Coat, Two-Sided Foliage, Hair·Cloth는 이후.
- **블렌드 모드:** Opaque, Masked, Translucent, Additive.
- **노드 종류:** 텍스처 샘플, 수학, UV(패닝·회전·트리플래너), 시간, 정점 색, 월드 좌표, 프레넬, 노멀 합성, 파라미터(Scalar·Vector·Texture·Switch), 커스텀 HLSL/GLSL 노드.
- **실시간 미리보기:** 구·큐브·평면·씬 안 오브젝트에서 바로 확인. 노드마다 작은 미리보기 썸네일.
- **식 노드:** Graph처럼 짧은 식을 노드 하나에 쓸 수 있음 (`"in":{"uv":"uv * tiling + time * pan"}`).

### 텍스트 저장 (AI가 읽고 고침)
```jsonl
{"material":"M_Rock","domain":"surface","shading":"lit","blend":"opaque","defaults":"1.0"}
{"param":"BaseTint","type":"color","value":"#b8b2a6","group":"Color"}
{"param":"Roughness","type":"float","value":0.8,"range":[0,1],"group":"Surface"}
{"param":"Albedo","type":"texture","value":"guid:7f3a…","group":"Textures"}
{"id":"t1","node":"TextureSample","in":{"tex":"Albedo","uv":"uv * 2"}}
{"id":"c1","node":"Multiply","in":{"a":"t1.rgb","b":"BaseTint"}}
{"out":"BaseColor","from":"c1"}
{"out":"Roughness","from":"Roughness"}
```
```jsonl
{"instance":"MI_Rock_Wet","parent":"M_Rock"}
{"set":"Roughness","value":0.25}
{"set":"BaseTint","value":"#8d8a84"}
```

## 3. 언리얼의 문제를 고치는 부분

| 언리얼에서 불편한 점 | 비둘기 엔진 |
|---|---|
| **셰이더 컴파일 대기** (수천 개, 에디터 시작이 느림) | 머티리얼 인스턴스는 컴파일 없음. 마스터 머티리얼만 컴파일하고, 결과는 해시로 캐시(`.cache/shaders/`) + 팀 공유 캐시. 백그라운드 컴파일 중에는 기본 셰이더로 표시 |
| **순열 폭발** (Static Switch × 품질 × 플랫폼) | Static Switch는 머티리얼당 최대 개수 제한 + 순열 수를 에디터에 표시(`이 머티리얼: 순열 12개`). 가능한 것은 동적 분기·uniform으로 |
| 바이너리 `.uasset`이라 diff·병합 불가 | JSONL 텍스트. git diff로 "Roughness 0.8 → 0.6"이 보임 |
| 저사양 대응이 수동 | 품질 단계별 자동 단순화: 감자는 Unlit 대체, 저사양은 노멀·AO 생략 등 (노드에 `"minTier"` 표시 가능) |
| 원인을 찾기 어려운 성능 문제 | `bdg mat stats M_Rock` → 명령어 수, 텍스처 샘플 수, 순열 수, 단계별 비용을 텍스트로 |

## 4. 텍스처 (언리얼식 임포트 설정)

텍스처를 넣으면 **용도별 프리셋**을 자동으로 고르고, `.meta`에 텍스트로 저장합니다.

| 프리셋 (언리얼 Compression Settings) | sRGB | 압축 (PC / 모바일 / 웹) |
|---|---|---|
| Default (색) | 켬 | BC7 / ASTC 6x6 / KTX2 |
| Normal Map | 끔 | BC5 / ASTC 5x5 / KTX2 |
| Masks (R=AO, G=Roughness, B=Metallic 패킹) | 끔 | BC7 / ASTC 6x6 / KTX2 |
| Grayscale | 끔 | BC4 / ASTC 8x8 / KTX2 |
| HDR (하늘·라이트맵) | — | BC6H / ASTC HDR / KTX2 |
| UI | 켬 | 무압축 또는 BC7, 밉맵 없음 |

- **이름으로 자동 판별:** `_N`, `_Normal` → Normal Map, `_ORM`/`_MRA` → Masks, `_D`, `_BaseColor` → Default.
- **텍스처 그룹 (언리얼 LOD Group):** World, Character, UI, Effects… 그룹별 최대 해상도를 **품질 단계마다** 지정(예: 저사양에서 World 최대 1024).
- **밉맵, 필터, 래핑, 최대 크기, 스트리밍 여부**를 `.meta`에서 설정.
- **텍스처 에디터:** 채널별 보기(R/G/B/A), 밉 단계 보기, 압축 전후 비교, 메모리 표시.
- **텍스처 스트리밍** (대형 게임 트랙): 화면에 필요한 밉만 올림.
- **가상 텍스처**는 오픈월드용으로 L 트랙에서.

```json
{"guid":"7f3a…","importer":"texture","preset":"normal","group":"world","srgb":false,
 "maxSize":{"potato":256,"low":512,"medium":1024,"high":2048,"ultra":4096},"mips":true,"wrap":"repeat",
 "source":{"file":"Rock_N.png","license":"CC0","author":"…"}}
```

## 5. 스크립트에서

```csharp
var mat = GetComponent<MeshRenderer>().material;   // 처음 접근 시 실행 중 인스턴스 생성
mat.SetFloat("Wetness", 0.7f);
mat.SetColor("BaseTint", Color.red);
Material.SetGlobal("RainAmount", 1.0f);             // 파라미터 묶음(전역)
```
C++ `SetScalarParameter`, Lua `mat:SetFloat`, Graph `Set Material Float` 노드도 같은 이름을 씁니다.

## 6. 일정

| 시기 | 내용 |
|---|---|
| M3 | PBR 기본 셰이더, `.material`(고정 파라미터) + `.matinst`, 텍스처 임포트 프리셋·`.meta`, BC7/BC5 쿠킹 |
| M5 | 머티리얼 그래프 에디터(그래프 에디터 공통), 머티리얼 함수, 노드 미리보기 |
| 1.0 | 품질 단계별 자동 단순화, ASTC·KTX2, 셰이더 캐시 공유, `bdg mat stats` |
| L 트랙 | 텍스처 스트리밍, 가상 텍스처, 지형 레이어 머티리얼 |
