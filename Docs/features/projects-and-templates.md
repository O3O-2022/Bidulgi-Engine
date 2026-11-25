# 프로젝트 생성 · 열기 · 엔진 버전 · 템플릿

작성 2026-11-21.

## 1. 허브와 시작 화면

기본은 **유니티식 허브**이고, 고도식 시작 화면을 덤으로 둡니다.

| 실행 방법 | 동작 |
|---|---|
| 허브 실행 | 최근 프로젝트, 설치된 엔진, 새 프로젝트, 템플릿. 프로젝트를 고르면 **그 프로젝트의 엔진 버전**으로 엽니다 |
| 엔진을 직접 실행 | 고도식 "만들기 / 열기" 화면. 허브와 같은 UI 코드를 씁니다. 버전이 다른 프로젝트를 고르면 "1.3 엔진으로 다시 열기" |
| `.bdgproject` 더블클릭 | `bdg-launcher`가 버전을 읽고 맞는 엔진을 실행. 없으면 "설치 / 다른 버전으로 열기" |
| 터미널 | `bdg new`, `bdg open`. 프로젝트 폴더 안에서는 그 프로젝트의 엔진 버전으로 자동 연결 (rustup 방식) |

유니티 허브의 불만은 피합니다: 로그인이 없고, 엔진과 같은 UI 프레임워크로 만든 작은 프로그램이며(1초 안에 실행), 압축 파일이나 소스로 빌드한 엔진도 등록할 수 있습니다. 허브는 선택 사항이고, 허브가 하는 일은 전부 `bdg` 명령으로도 됩니다.

## 2. 엔진 버전

- 여러 버전을 나란히 설치합니다: `%LOCALAPPDATA%\Bidulgi\Engines\<버전>\` (관리자 권한 불필요).
- `<Name>.bdgproject`에 `"engine": "1.2.3"`처럼 정확한 버전을 적습니다. 이 파일을 git에 커밋하므로 팀 전원이 같은 버전을 씁니다.

| 바뀌는 버전 | 처리 |
|---|---|
| 패치 | 그냥 엶. 허브가 업데이트를 권함 |
| 마이너 | 변환 목록을 보여 주고, 백업(git 커밋)한 뒤 변환 |
| 메이저 | 전체 변환 도구, 고칠 코드 목록과 자동 수정 |
| 낮은 버전으로 열기 | 기본은 막음. 파일 형식이 같을 때만 경고 후 허용 |

- `bdg upgrade --to 1.3 --dry-run`으로 무엇이 바뀌는지 텍스트로 먼저 봅니다.
- 씬 형식 버전(`"defaults":"1.0"`)은 엔진 버전과 따로 관리합니다. 형식이 그대로면 엔진을 업데이트해도 파일이 바뀌지 않습니다.
- CI: `bdg engine install --from-project`

## 3. 새 프로젝트 만들기

```
bdg new MyShooter --template fps --lang csharp --tier medium..high --platforms windows,android
```

| 선택 항목 | 내용 |
|---|---|
| 템플릿 | 아래 목록 |
| 주 언어 | C# 또는 Graph. 템플릿 코드가 그 언어로 들어옴 (C++ 버전은 고급 옵션) |
| 품질 단계 | 최소·최대 단계 |
| 플랫폼 | Windows, Android, Web |
| 시작 콘텐츠 | 켜면 예제 에셋(무기, 소품, 머티리얼)을 함께 넣음 |

자동으로 생기는 것: `.bdgproject`, git 초기화와 `.gitignore`, `AGENTS.md`, `Packages/manifest.json`, 입력 액션 파일, 기본 씬.

## 4. 템플릿 (언리얼식)

| 템플릿 | 들어 있는 것 |
|---|---|
| **빈 프로젝트** | Main Camera만 |
| **기본 3D** | Main Camera, Sun, Sky, World Settings, Ground (아래 기본 씬) |
| **기본 2D** | 직교 카메라, 배경색. 조명 없음 (최소 단계 기준) |
| **1인칭 슈팅** | 1인칭 캐릭터(팔 메시), 이동·달리기·앉기·점프, 마우스·패드 시점, **총기 시스템**(히트스캔과 투사체, 탄창, 재장전, 반동, 탄 퍼짐), 조준(ADS), 피격·체력, 표적과 연습장 맵, HUD(조준점, 탄약, 체력), 피격 이펙트와 소리 |
| **3인칭** | 3인칭 캐릭터, 스프링암 카메라(벽 충돌), 이동·달리기·점프, **애니메이션 그래프**(이동 블렌드 스페이스, 점프 상태 머신, 발 IK), 상호작용(문, 줍기), 예제 맵 |
| **탑다운** | 클릭 또는 WASD 이동, 길찾기(Recast), 카메라 추적, 마우스 위치 조준 |
| **차량** | 물리 차량(Jolt): 엔진·기어·서스펜션, 추적 카메라, 트랙 맵 |
| **2D 플랫포머** | 2D 캐릭터(Box2D), 코요테 타임과 점프 버퍼, 타일맵, 스프라이트 애니메이션, 카메라 데드존 |
| **오픈월드** (L 트랙) | 3D 기본 + 지형, 월드 파티션, 시간대(낮과 밤) 하늘, 3인칭 캐릭터 |
| **멀티플레이** (L 트랙) | 3인칭 또는 1인칭 + Network Manager, 리슨 서버, 이동 동기화, Player Start 2개 |

### 템플릿 공통 규칙

- **게임 구조:** 컴포넌트 방식으로 만듭니다. 플레이어 컨트롤, 게임 규칙(GameMode에 해당), HUD를 각각 컴포넌트로 나눠, 무엇이 어디에 있는지 바로 보이게 합니다.
- **입력:** 입력 액션으로만 받습니다(Move, Look, Jump, Fire, Aim, Reload, Interact). 키보드, 패드, 터치 매핑이 모두 들어 있습니다.
- **품질 단계:** 템플릿마다 모든 단계에서 동작하는지 CI로 확인합니다.
- **설명:** 템플릿 코드에는 짧은 한국어·영어 주석을 달고, `AGENTS.md`에 "이 템플릿 구조와 고치는 법" 절을 넣습니다. AI가 "총을 샷건으로 바꿔 줘" 같은 요청을 바로 처리할 수 있게 하기 위해서입니다.
- **라이선스:** 템플릿 코드는 0BSD, 에셋은 CC0으로 배포합니다. 게임에 그대로 써도 아무 의무가 없습니다.
- **패키지:** 템플릿은 패키지(`com.bdg.template.fps`)로 배포합니다. 엔진 업데이트와 따로 고칠 수 있고, 누구나 `bdg template create`로 자신만의 템플릿을 만들어 공유할 수 있습니다.

### 1인칭 슈팅 템플릿 구조 (예)

```
Assets/
├─ Scenes/Range.scene              연습장 맵
├─ Player/
│  ├─ FPSPlayer.prefab             캐릭터 + 카메라 + 팔
│  ├─ FPSController.cs             이동, 시점, 앉기
│  └─ Health.cs
├─ Weapons/
│  ├─ Weapon.cs                    발사, 탄창, 재장전, 반동 (데이터는 아래 파일)
│  ├─ Rifle.weapon.json            연사 속도, 데미지, 반동 곡선
│  ├─ Pistol.weapon.json
│  └─ Projectile.prefab
├─ UI/HUD.widget
├─ Input/Actions.jsonl, Contexts/OnFoot.jsonl
└─ Audio/, VFX/
```

- 무기 수치는 코드가 아니라 데이터 파일에 둡니다. 밸런스 조정은 파일 한 줄 수정이고, AI도 그대로 고칠 수 있습니다.

### 구현 현황 — 1인칭 슈팅 (2026-11-25, 첫 게임)

`bdg new <폴더> --template fps [--name "게임 이름"]` — 템플릿은 저장소의 `Templates/fps/`(빌드 때 `bdg.exe` 옆 `Templates/`로 복사)이고, 만들 때 이름·엔진 C# 경로를 채우고, 소리 3개(`Shoot`·`Hit`·`Jump`, 합성 = CC0)를 만들고, `dotnet build`로 C#을 한 번 빌드합니다(`.cache/Scripts`).

| 파일 | 내용 |
|---|---|
| `Scenes/Main.scene` | 연습장: 바닥·벽·경사로·발판·상자(그레이박스, 정적 Collider), 받침대 위 금색 표적 5개(Rigidbody), 플레이어, 총알 12개 풀 |
| `Assets/Player/FPSController.cs` | 걷기·달리기(Sprint)·점프(땅에 있을 때)·마우스/스틱 시점. Player(Rigidbody Character + 캡슐) → Head(좌우) → Eyes(Camera + AudioListener, 위아래) |
| `Assets/Weapons/Weapon.cs`, `Projectile.cs` | 연사·탄창·재장전(`await Wait`)·퍼짐·반동, 수치는 `Data/Weapons.table.jsonl`(rifle·pistol·shotgun — "샷건으로" = 한 칸). 총알은 **물리 투사체**: 미리 놓아 둔 `Bullet` 풀(중력 0)을 총구로 옮기고 속도를 줌(플레이 중 새로 만든 오브젝트는 아직 물리 몸체가 없어서) |
| `Assets/Game/Target.cs`, `ScoreKeeper.cs` | 맞으면 충격으로 밀리고 소리, 점수(+1·+2·+3), 1.5초 뒤 사라짐. 규칙(GameMode)이 점수를 로그·Console에(게임 UI 7-3 전) |
| `Input/Actions.jsonl`, `Input/Contexts/OnFoot.jsonl` | Move·Look·Jump·Sprint·Fire·Reload — 키보드·마우스·게임패드 |
| `Assets/Materials/*.material` | Gold·Steel·Concrete·Wood·Glow |
| `AGENTS.md`, `README.md`, `LICENSE-TEMPLATE.txt` | 구조와 고치는 법(요청 → 고칠 곳 표), 조작, 0BSD(코드)·CC0(에셋) |

함께 바뀐 것: 에디터를 **게임 프로젝트 폴더에서 열면** 그 폴더를 프로젝트로 씀(엔진 폴더로 옮기지 않고, 글꼴·토큰·아이콘은 엔진 폴더에서), 빌드한 게임이 **입력 액션**(창 전체가 Game 뷰)·**물리 설정**(중력)을 쓰고 `display.showCursor`가 꺼져 있으면 마우스를 붙잡음(Esc로 놓고 클릭으로 다시), `bdg build`가 `Input/`을 묶음, 빌드 사전 검사가 머티리얼 파일을 줄 단위로 읽음.

검사: `BDG_FPS_TEST=1`(템플릿 폴더에서 에디터 실행) — 주입한 W·Space·클릭·마우스로 이동·점프·첫 표적 명중(밀림, 점수 1)·시점 → `fps test: play=1 moved=1 jumped=1 hit=1 pushed=1 look=1`, 이어서 `bdg build`와 빌드한 게임 120프레임(CI). **남은 것**: 앉기, 조준(ADS), 체력·피해, HUD(7-3), 피격 이펙트, 팔 메시, 레이캐스트 히트스캔(물리 레이캐스트 다음), 프리팹(6-1 뒤 플레이어·표적·총알을 프리팹으로), 3인칭 등 다른 템플릿.

### 구현 현황 — 3인칭 (2026-11-25, 7-1b)

`bdg new <폴더> --template thirdperson` — `Templates/thirdperson/`. 캐릭터는 `bdg new`가 만드는 **생성 휴머노이드**(`Assets/Models/Hero.gltf`, CC0; `template.json`의 `"models"`)와 그래프 `Assets/Anim/Hero.animgraph`(Move = Speed 1D 블렌드 스페이스 Idle·Walk·Run, Strafe = 2D, Jump, UpperBody Wave 레이어, Lean 가산 레이어). 놀이터: 땅·벽, 계단 4단 + 단상, 경사로 + 데크, 상자, 금색 표지. C#(0BSD): `ThirdPersonController`(카메라 기준 걷기·달리기, 가는 쪽으로 돌기, 낮은 턱 오르기, 점프, E로 손 흔들기, Animator `Speed`·`Jump`·`Wave`, 가까우면 머리가 표지를 봄, HUD 속도·상태), `FollowCamera`(시점 입력으로 공전, 벽이 사이에 있으면 앞으로 당김 — `Physics.Raycast`). 캐릭터의 `AnimatorIK`가 계단·경사로에서 발을 땅에 맞춥니다. 입력 Move·Look·Jump·Sprint·Wave(키보드·패드), 소리 Jump(합성), HUD는 레인 B의 UICanvas. 검사 `BDG_THIRDPERSON_TEST=1`: W로 걷기(블렌드 대부분 Walk), 점프, Shift로 달리기(Run 가중치), 계단 발 IK, HUD, 남쪽 벽 앞에서 카메라가 벽 안쪽으로, 마우스 시점 → `third person test: moved=1 walk=1 run=1 jump=1 footik=1 hud=1 collide=1 look=1`, 빌드한 게임 120프레임(CI).

### 구현 현황 — 2D 플랫포머 (2026-11-25)

`bdg new <폴더> --template platformer2d` — `Templates/platformer2d/`. **Box2D**(XY 평면) 위 옆으로 가는 레벨: 땅 3덩어리와 구덩이, 발판 4개, 동전 8개(트리거), 체크포인트, 골 깃발. C#: `PlatformerController`(걷기·달리기·가속, **코요테 타임·점프 버퍼·짧은 점프**, 떨어지면 체크포인트로; Rigidbody2D + Collider2D + 마찰 0), `CameraFollow`(데드존 + 부드럽게), `Coin`(트리거 → 소리·점수·사라짐), `Goal`(골 또는 `isCheckpoint`), `GameRules`(동전 수·골 → 로그). 입력 Move·Jump·Run(키보드·패드), 소리 Jump·Coin·Goal(합성), 2D 중력 -25. 그림은 지금 원근 카메라로 본 3D 상자(직교 2D 카메라·스프라이트·타일맵은 다음). 검사 `BDG_PLATFORMER_TEST=1`: D로 달려 동전 2개, 점프, 카메라 따라옴 → `platformer test: ran=1 jumped=1 coins=1 camera=1`, 빌드한 게임 90프레임(CI).

## 5. 기본 3D 씬

| 오브젝트 | 내용 |
|---|---|
| Main Camera | 원근 카메라, (0, 1.5, -6)에서 원점을 봄 |
| Sun | 방향광, 그림자 켬, 45° |
| Sky | 절차적 하늘, 태양 방향 연동, 하늘색 주변광 |
| World Settings | GI·안개·포스트 프로세스·품질 단계 기본값을 한 오브젝트에 |
| Ground | 20×20m 평면, 격자 머티리얼 |

```jsonl
{"v":1,"defaults":"1.0","scene":"Main","mode":"3d"}
{"id":"cam","name":"Main Camera","pos":[0,1.5,-6],"rot":[-14.036243,180,0],"Camera":{}}
{"id":"sun","name":"Sun","rot":[-45,-30,0],"Light":{"type":"Directional","intensity":3}}
{"id":"sky","name":"Sky","Sky":{}}
{"id":"world","name":"World Settings","WorldSettings":{}}
{"id":"ground","name":"Ground","scale":[20,1,20],"MeshRenderer":{"mesh":"Plane","material":"bdg:Grid"}}
```

(2-6에서 구현: `Scene/DefaultScene.*`가 만드는 실제 파일. 에디터 Ctrl+N과 `bdg new`가 같은 함수를 씀. World Settings는 지금 포그와 노출을 가짐 — GI·품질 단계 기본값은 해당 기능이 장면 단위 설정을 가질 때 여기로 모음.)

## 일정

| 시기 | 내용 |
|---|---|
| M7~1.0 | 허브, 시작 화면, 버전 관리, `bdg new`; 템플릿은 빈 프로젝트, 기본 3D·2D, 1인칭 슈팅, 3인칭, 2D 플랫포머 |
| 1.0 이후 | 탑다운, 차량 |
| L 트랙 | 오픈월드, 멀티플레이 |
