# 애니메이션

애니메이션은 두 축으로 나눕니다.

| 축 | 하는 일 | 방식 |
|---|---|---|
| **애니메이션 그래프** | 캐릭터가 실행 중에 어떤 동작을 어떻게 섞어 재생할지 (로직) | **Graph의 상위호환** (언리얼 Animation Blueprint에 해당) + 속도 업그레이드 |
| **타임라인 도구** | 클립 자체를 만들고 다듬기, UI 애니메이션, 컷신 | 블렌더 애니메이션 도구 + 언리얼 Sequencer |

---

## 1. 애니메이션 그래프

### 언리얼과 같은 구조

| 요소 | 내용 |
|---|---|
| **Event Graph** | 매 프레임 캐릭터 상태를 읽어 변수(속도, 공중 여부 등)를 갱신 |
| **Anim Graph** | 최종 포즈를 만드는 그래프. 결과는 Output Pose 노드로 |
| **State Machine** | 상태(Idle, Move, Jump…)와 전환 규칙, Conduit(분기용 빈 상태) |
| **Blend Space 1D / 2D** | 속도·방향 같은 값으로 여러 클립을 연속으로 섞기 |
| **Anim Montage** | 공격·상호작용처럼 코드에서 직접 재생하는 클립. 슬롯, 섹션, 분기 |
| **Slot / Layered Blend** | 상체만 다른 동작 (뼈 기준 레이어 섞기) |
| **Anim Notify / Notify State** | 클립의 특정 시점에 이벤트 (발소리, 타격 판정 시작·끝) |
| **Sync Group** | 걷기·뛰기 클립의 발 위치를 맞춰서 섞기 |
| **Linked Anim Layers** | 무기별로 애니메이션 묶음을 갈아 끼우기 |
| **IK 노드** | Two-Bone IK, FABRIK, 발 IK(지면 맞추기), Look-At |
| **Anim Curve** | 클립에 담긴 값 곡선 (표정, 머티리얼 파라미터 구동) |
| **Root Motion** | 클립의 이동량으로 캐릭터를 움직이기 |
| **리타게팅** | 뼈 구조가 다른 캐릭터에 같은 애니메이션 적용 |

### Graph의 상위호환 (2026-11-21 확정)

정식 이름은 **애니메이션 그래프**(`.animgraph`, 컴포넌트 `AnimGraph`)입니다. "애니메이션 블루프린트"라는 이름은 쓰지 않습니다.

애니메이션 그래프는 별도 시스템이 아니라 **Graph에 애니메이션 전용 기능을 더한 것**입니다. Graph에서 되는 것은 전부 그대로 됩니다.

| | Graph (`.graph`) | 애니메이션 그래프 (`.animgraph`) |
|---|---|---|
| 노드 문법, 변수, 함수, 매크로, 이벤트 | ✓ | ✓ 같음 |
| 에디터 화면, 단축키, 디버거, 텍스트 저장 형식 | ✓ | ✓ 같음 |
| 코드 호출 (C#, C++, Lua, Python) | ✓ | ✓ 같음 |
| 컴파일러 (VM, 이후 AOT) | ✓ | ✓ 같음 + 포즈 전용 최적화 |
| **이벤트 그래프** (매 프레임 변수 계산) | — | ✓ 일반 Graph와 똑같음 |
| **포즈 그래프** (포즈 노드: 클립 재생, 블렌드, 블렌드 스페이스, IK, 레이어) | — | ✓ 추가 |
| **상태 머신** (상태, 전이 규칙, 전이 블렌드) | — | ✓ 추가 |
| 노티파이, 몽타주 슬롯, 싱크 그룹, 커브 | — | ✓ 추가 |
| 워커 스레드 평가 (`[ThreadSafe]` 검사) | — | ✓ 추가 |

- 파일 형식도 Graph 형식에 섹션만 더한 구조라, Graph를 다루는 도구(AI 포함)가 애니메이션 그래프도 그대로 읽습니다.
- 일반 Graph를 애니메이션 그래프로 바꾸면 기존 노드는 이벤트 그래프로 들어갑니다. 반대는 포즈·상태 머신 섹션이 없을 때만 됩니다.
- 추가 기능은 Graph의 확장 표시(`[Extendable]`)로 붙이므로, 나중에 다른 전용 그래프(예: AI 행동 그래프)도 같은 방식으로 만듭니다.

### 업그레이드: 속도

| 언리얼의 불편 | 이 엔진 |
|---|---|
| 스레드 안전 업데이트를 사람이 직접 켜고 지켜야 함 | **기본이 스레드 안전.** Event Graph를 "변수 읽기(게임 스레드)"와 "포즈 계산(워커 스레드)"으로 엔진이 강제로 나눔. 변수 읽기는 속성 경로로 자동 처리 |
| 캐릭터가 많으면 직접 예산 관리 | **업데이트 예산 관리 내장**: 거리·화면 크기에 따라 업데이트 빈도를 자동으로 낮추고, 화면 밖에서는 생략 |
| 그래프 해석 비용 | 그래프와 같은 컴파일러 사용 → 에디터는 VM, 빌드는 네이티브 ([visual-scripting](visual-scripting.md)) |
| 인스턴스마다 따로 평가 | 같은 애니메이션 그래프를 쓰는 캐릭터들을 **묶어서 평가** |
| 포즈 섞기 | SIMD 블렌딩 |

### 업그레이드: 텍스트 저장

언리얼은 바이너리라 AI가 읽고 고칠 수 없습니다. 이 엔진은 텍스트입니다.

```jsonc
// anim/ABP_Knight.animgraph
{"v":1, "skeleton":"models/knight.glb",
 "vars":{"speed":"float", "inAir":"bool", "aim":"float"},
 "update":{
   "speed": "owner.CharacterController.velocity.xz.length",
   "inAir": "owner.CharacterController.airborne"},
 "stateMachines":{"Locomotion":{
   "entry":"Idle",
   "states":{
     "Idle": {"play":"anim/knight_idle.anim"},
     "Move": {"blendSpace":"anim/BS_KnightMove.blend1d", "x":"speed"},
     "Jump": {"play":"anim/knight_jump.anim", "loop":false}},
   "transitions":[
     {"from":"Idle", "to":"Move", "when":"speed > 0.1",  "blend":0.2},
     {"from":"Move", "to":"Idle", "when":"speed < 0.1",  "blend":0.25},
     {"from":"*",    "to":"Jump", "when":"inAir",        "blend":0.1},
     {"from":"Jump", "to":"Idle", "when":{"fn":"LandedCleanly"}, "blend":0.15}]}},
 "pipeline":["Locomotion", {"slot":"UpperBody", "fromBone":"spine_01"}, {"ik":"FootIK"}, "Output"]}
```

- `update`는 **속성 경로만** 씁니다 (게임 스레드에서 안전하게 읽기 위해).
- 전환 조건 `when`은 단순 비교만 허용합니다: `변수 비교연산 상수`, `&&`, `||`, `!`. 복잡한 조건은 `{"fn":"함수이름"}`으로 그래프 함수를 부릅니다. 식 언어가 커지는 것을 막기 위한 제한입니다.
- `pipeline`은 Anim Graph를 일직선으로 쓴 것. 복잡한 그래프는 그래프와 같은 노드 형식(`"graph":[...]`)도 지원합니다.

### 압축 보기 (`bdg anim view`)

```
ABP_Knight  skeleton=knight.glb
vars      speed:float  inAir:bool  aim:float
Locomotion  Idle ⇄ Move (speed>0.1 / speed<0.1)   * → Jump (inAir)   Jump → Idle (LandedCleanly)
pipeline  Locomotion → Slot UpperBody(spine_01) → FootIK → Output
```

### 2D에서도 같은 구조

스프라이트 애니메이션도 같은 상태 머신을 씁니다 (상태의 `play`가 스프라이트 시트 클립). 2D와 3D 캐릭터 로직을 같은 방식으로 만듭니다.

### 에디터 화면

```
┌ 미리보기 (캐릭터) ───────────┬ Anim Graph / State Machine ──────────────────┐
│                             │   [Idle] ──speed>0.1──▶ [Move]                │
│        (캐릭터 3D 뷰)         │      ▲ ◀──speed<0.1───                        │
│                             │      └──── [Jump] ◀── * (inAir)               │
├ 변수 (실행 중 값 표시) ───────┤                                              │
│ speed 3.2   inAir false     │  Locomotion → Slot UpperBody → FootIK → Out  │
├ 에셋 ────────────────────────┴──────────────────────────────────────────────┤
│ knight_idle  knight_run  BS_KnightMove  knight_jump  AM_Attack (Montage)    │
└─────────────────────────────────────────────────────────────────────────────┘
```

- Play 중에 현재 상태와 전환이 강조되고, 변수 값이 실시간으로 보입니다 (언리얼과 동일).
- 미리보기에서 변수를 슬라이더로 바꿔 보며 전환을 시험할 수 있습니다.

### 스크립트에서 사용

> 데이터를 넘기는 방법(당겨 오기·밀어 넣기), Notify·상태 이벤트가 코드로 전달되는 방식, 프레임 순서와 스레드 규칙은 [interop](../architecture/interop.md)에 정리했습니다.

```csharp
var anim = GetComponent<AnimGraph>();
anim.PlayMontage("anim/AM_Attack.montage", section: "Combo2");
anim.SetLayer("Weapon", "anim/ALI_Sword.animlayer");
```

---

## 2. 타임라인 도구 (블렌더식)

블렌더 전체(모델링, 스컬프팅)를 넣지 않고 **블렌더의 애니메이션 도구만** 내장합니다. 모델링은 블렌더와 연동합니다.

| 도구 | 역할 | 출처 |
|---|---|---|
| Timeline / Sequencer | 트랙 기반 편집. UI, 카메라, 오브젝트, 오디오, 이벤트를 한 타임라인에서 (컷신 포함) | 언리얼 Sequencer |
| Dope Sheet | 키프레임을 한눈에 보고 끌어서 옮기기, 복사/붙여넣기 | 블렌더 |
| Graph Editor | 베지어 커브로 이징 편집, 핸들 조작 | 블렌더 |
| 자동 키 (녹화) | 녹화 중에 Inspector/Details 값을 바꾸면 키프레임 자동 생성 | 블렌더 |
| Notify 트랙 | 클립에 Anim Notify를 찍는 트랙 (애니메이션 그래프와 연결) | 언리얼 |

- 리플렉션 덕분에 **Inspector에 보이는 값이면 무엇이든** 키프레임을 넣을 수 있습니다.
- UI Designer 아래에도 같은 타임라인이 붙습니다.

### 애니메이션 클립 (`.anim`)

```jsonc
// anim/menu_open.anim
{"v":1, "length":0.4, "loop":false, "tracks":[
  {"target":"Panel",   "prop":"opacity", "keys":[[0,0], [0.25,1,"easeOut"]]},
  {"target":"Panel",   "prop":"pos.y",   "keys":[[0,-40], [0.4,0,"spring"]]},
  {"target":"PlayBtn", "prop":"scale",   "keys":[[0.15,0.8], [0.4,1,"backOut"]]},
  {"target":"@self",   "event":"Menu.OnOpened", "at":0.4}
]}
```

- 키: `[시간, 값]` 또는 `[시간, 값, 이징]`
- 이징 프리셋: `linear`, `step`, `easeIn`, `easeOut`, `easeInOut`, `backOut`, `spring`
- 직접 커브: `[0.25, 1, {"in":[-0.1,0], "out":[0.1,0.2]}]` (베지어 핸들)
- `target`은 위젯·오브젝트의 **id**(이름이 아님), `prop`은 속성 경로
- 스켈레탈 클립은 glTF에서 가져온 데이터를 참조하고, Notify와 Curve만 텍스트로 덧붙입니다.

### Animation 워크스페이스

```
┌ 미리보기 (대상 위젯 또는 씬) ─────────────────────────────────────────┐
├ 트랙 ────────────┬ [Dope Sheet | Graph] ─────────────────────────────┤
│ ▾ Panel          │ 0.0   0.1   0.2   0.3   0.4                      │
│     opacity      │ ◆─────────────◆                                  │
│     pos.y        │ ◆───────────────────────◆                        │
│ ▾ PlayBtn        │         │                                        │
│     scale        │       ◆─┼───────────────◆                        │
│ [● 녹화] [▶] [⏮]  │         ▲ 재생 헤드                                │
└──────────────────┴──────────────────────────────────────────────────┘
```

### 블렌더 연동

- 블렌더에서 저장하면 glTF로 자동 내보내고 엔진이 핫 리로드 (블렌더 애드온 제공)
- 장기적으로 간단한 메시 편집(ProBuilder 수준)은 검토하되, 본격 모델링은 블렌더에 맡김

### 스크립트에서 재생

```csharp
GetComponent<Animation>().Play("anim/menu_open.anim");
```

```python
bidulgi.anim.key("Panel", "opacity", t=0.25, value=1, ease="easeOut")
```

---

## 개발 순서

| 단계 | 내용 |
|---|---|
| 1 | 클립 재생, 타임라인과 Dope Sheet (UI 애니메이션) |
| 2 | 스켈레탈 클립 임포트, State Machine, Blend Space |
| 3 | Montage, Slot, Notify, Sync Group |
| 4 | 스레드 분리 강제, 업데이트 예산, 묶음 평가 |
| 5 | IK, Linked Layers, 리타게팅, Root Motion |
| 6 | Graph Editor, 컷신용 Sequencer |
