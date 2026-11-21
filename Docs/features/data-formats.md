# 데이터 형식 (씬, 프리팹, 에셋)

## 용도별 형식

| 데이터 | 형식 | 이유 |
|---|---|---|
| 씬, 프리팹, 위젯, 애니메이션 | JSON | 에디터가 읽고 쓰는 왕복에 가장 안정적 |
| 아이템·몬스터·밸런스 표 | TSV | 키를 반복하지 않아 토큰이 크게 줄어듦, 엑셀로 열림 |
| 대사, 퀘스트, 설정 문서 | Markdown + 프런트매터 | 글은 글답게 |
| 이미지, 사운드, 모델 원본 | 원본 파일 + `.meta` | 바이너리는 텍스트로 만들지 않음 |
| 아주 큰 월드 데이터 | SQLite (검토) | 필요한 부분만 쿼리 |

**피하는 형식:** YAML(들여쓰기 실수, `no`→false 같은 함정), CUE·Pkl·KDL·Dhall(AI 학습 데이터 부족)

## 프로젝트 폴더

유니티처럼 **폴더 자체가 프로젝트**이고, 언리얼 `.uproject`처럼 **폴더 맨 위에 프로젝트 파일 하나**를 둡니다 (2026-11-21 결정).

| 방식 | 가져오는 것 |
|---|---|
| 유니티식 폴더 | 에셋은 폴더 안 아무 데나 두면 자동 인식. 폴더를 열면 프로젝트가 열림 |
| 언리얼식 프로젝트 파일 | 더블클릭으로 열기, 엔진 버전 고정, 켜진 언어·모듈·플러그인 목록 |

```jsonc
// MyGame.bdgproject   (확장자는 엔진 이름이 정해지면 바뀜, 지금은 임시)
{"v":1, "name":"MyGame", "engine":"0.1.0",
 "languages":["csharp", "graph", "lua"],
 "modules":[{"name":"MyGame.Native", "lang":"cpp"}],
 "plugins":["physics3d", "ui"],
 "startScene":"scenes/forest_01.scene"}
```

- 짧게 유지합니다. 세부 설정은 `settings/` 아래 파일에 둡니다 (프로젝트 파일이 거대해지는 언리얼 `.ini` 문제를 피함).
- 엔진 버전이 다르면 열 때 "이 프로젝트는 0.1.0용입니다. 마이그레이션할까요?"를 묻고, 변경 범위를 미리 보여 줍니다.
- 폴더를 바로 열어도 됩니다. 프로젝트 파일이 없으면 기본값으로 새로 만들 것을 제안합니다.
- `bdg` 명령은 현재 폴더에서 위로 올라가며 프로젝트 파일을 찾습니다 (git처럼).

```
MyGame/
  MyGame.bdgproject          ← 프로젝트 파일 (더블클릭으로 열기)
  AGENTS.md                  ← AI 안내 파일 (엔진이 갱신)
  settings/                  ← 프로젝트 설정 (계층형)
  assets/
    textures/grass.png
    textures/grass.png.meta  ← GUID, 임포트 설정
    models/goblin.glb (+ .meta)
  scenes/forest_01.scene
  prefabs/goblin.prefab
  ui/hud.ui
  anim/menu_open.anim
  scripts/PlayerController.cs
  source/MyGame.Native/      ← C++ 모듈 (헤더 툴이 .generated 파일 생성)
  tools/                     ← Python 도구 (에디터 메뉴에 자동 등록)
  data/items.tsv
  schemas/                   ← 엔진이 자동 생성
  .cache/                    ← 베이크·쿠킹 결과 (git 제외)
  .user/                     ← 에디터 상태 (git 제외)
```

## 씬 형식 (`.scene`, JSONL)

**한 줄 = JSON 하나.** 첫 줄은 씬 헤더, 그다음 줄부터 오브젝트 하나씩입니다. 배열 괄호와 줄 끝 쉼표가 없어서, 두 사람(또는 에이전트 둘)이 각각 오브젝트를 추가해도 서로 다른 줄만 바뀌고 git 충돌이 나지 않습니다. (2026-11-20 검토 반영: 이전의 `"objects":[...]` 배열 형식은 폐기)

```jsonl
{"v":1, "defaults":"1.0", "scene":"forest_01", "mode":"3d", "settings":{"gravity":[0,-9.81,0], "skybox":"sky/day.hdr"}}
{"id":"cam", "name":"Main Camera", "type":"Camera", "pos":[0,5,-10], "rot":[20,0,0]}
{"id":"sun", "type":"Light", "rot":[50,-30,0], "Light":{"kind":"Directional", "intensity":1.2}}
{"id":"player", "name":"Player", "type":"Mesh", "tag":"Player", "Mesh":{"model":"models/knight.glb"}, "Rigidbody":{"mass":70}, "PlayerController":{"speed":6.5, "cam":"@cam"}, "Inventory":{"slots":20}}
{"id":"Enemies"}
{"id":"g1", "name":"Goblin", "parent":"@Enemies", "prefab":"prefabs/goblin.prefab", "pos":[6,0,1], "Health":{"hp":12}, "/hpbar":{"pos":[0,2,0]}}
{"id":"g2", "name":"Goblin", "parent":"@Enemies", "prefab":"prefabs/goblin.prefab", "pos":[8,0,3]}
```

- 헤더 줄: `v`(파일 형식 버전), `defaults`(기본값 세트 버전, [아래](#기본값-버전과-마이그레이션)), 씬 이름, 모드, 씬 설정.
- 한 줄이 길어져도 줄을 나누지 않습니다. 대신 에디터와 `bdg view`가 읽기 좋게 펼쳐 보여 줍니다. (한 줄 = 한 오브젝트를 지켜야 줄 단위 diff·병합이 유지됨)

### git 병합 드라이버

`bdg init`이 `.gitattributes`에 `*.scene merge=bidulgi`을 등록합니다. 병합은 **줄 순서가 아니라 `id` 기준**입니다.

| 경우 | 결과 |
|---|---|
| 서로 다른 오브젝트를 추가·수정·삭제 | 자동 병합 |
| 같은 오브젝트의 다른 컴포넌트를 수정 | 컴포넌트 단위로 자동 병합 |
| 같은 오브젝트의 같은 값을 다르게 수정 | 충돌. 에디터의 비교 화면에서 선택 |
| 한쪽은 삭제, 한쪽은 수정 | 충돌로 표시 |

### 필드 규칙

| 필드 | 의미 | 생략 시 |
|---|---|---|
| `id` | 씬 안에서 고유한 참조 키. 바뀌지 않음 | 필수 |
| `name` | Hierarchy 표시 이름. 중복 허용 | id와 같음 |
| `type` | 기본 컴포넌트 묶음: `Empty`, `Mesh`, `Sprite`, `Camera`, `Light`, `Text`, `Tilemap` | `Empty` |
| `parent` | 부모 참조 (`@id`) | 최상위 |
| `pos` / `rot` / `scale` | 트랜스폼. 3D `[x,y,z]`, 2D `pos:[x,y]`와 `rot` 숫자 하나 | 0 / 0 / 1 |
| `tag`, `layer`, `active` | 유니티와 동일 | `Untagged`, `Default`, `true` |
| 대문자로 시작하는 키 | 컴포넌트와 설정값. **내장 컴포넌트와 사용자 스크립트가 같은 문법** | 없음 |
| `prefab` | 프리팹 인스턴스. 적힌 컴포넌트 값은 곧 오버라이드 | 없음 |
| `"/자식id"` | 프리팹 **안쪽** 오브젝트의 오버라이드 (예: `"/hpbar":{"pos":[0,2,0]}`) | 없음 |

### 저장 원칙

1. **기본값과 같으면 저장하지 않음.** `type`이 의미하는 기본 컴포넌트가 전부 기본값이면 컴포넌트 키도 생략
2. **오브젝트 하나 = 한 줄.** 유효한 JSON이면서 줄 단위로 읽기·수정·diff·병합 가능
3. **참조:** 오브젝트는 `"@id"`, 프리팹 안쪽은 `"@인스턴스id/자식id"`, 에셋은 경로 문자열. 파일을 옮기면 엔진이 `.meta` GUID로 추적해 경로를 자동 갱신
4. **순서:** 줄 순서 = 형제 순서
5. **버전:** 헤더의 `v`(파일 형식)와 `defaults`(기본값 세트). 둘 다 바뀌면 자동 마이그레이션

### 프리팹 안쪽 오브젝트의 id

프리팹 안의 id(`hpbar`)는 **프리팹 안에서만** 고유합니다. 씬에서는 인스턴스 id를 앞에 붙인 경로로 부릅니다. 그래서 같은 프리팹을 여러 번 놓아도 id가 겹치지 않습니다.

| 대상 | 쓰는 법 |
|---|---|
| 고블린 g1의 체력바 참조 | `"@g1/hpbar"` |
| 그 체력바의 값 오버라이드 (g1 줄 안에) | `"/hpbar":{"pos":[0,2,0]}` |
| 중첩 프리팹 (g1 안의 무기 안의 이펙트) | `"@g1/weapon/trail"`, 오버라이드는 `"/weapon/trail":{...}` |
| 명령 | `bdg set g1/hpbar pos.y=2` |

- 씬에서 프리팹 인스턴스에 자식을 **새로 추가**하면, 그 자식은 일반 오브젝트처럼 씬 전체에서 고유한 id를 받고 `"parent":"@g1"`로 연결됩니다.
- 프리팹에서 자식이 삭제되면, 그 자식을 가리키던 오버라이드는 `bdg validate`가 "쓰이지 않는 오버라이드"로 알려 주고 정리를 제안합니다.

### 기본값 버전과 마이그레이션

기본값을 저장하지 않으므로, 엔진이 기본값을 바꾸면 기존 씬이 말없이 달라질 수 있습니다. 이를 막기 위한 규칙입니다.

1. 엔진의 모든 기본값은 **기본값 세트**(`defaults/1.0.json` 등)로 묶여 버전을 가집니다. 파일 헤더의 `"defaults":"1.0"`이 어떤 세트를 썼는지 기록합니다.
2. 기본값 하나라도 바뀌면 **새 세트 버전**을 만들고, 반드시 **마이그레이션**을 함께 넣습니다. 옛 버전 파일을 열면 바뀐 필드의 예전 값을 명시적으로 써 넣은 뒤 헤더를 새 버전으로 올립니다. 그래서 겉보기 결과는 그대로입니다.
3. 엔진 CI가 "기본값이 바뀌었는데 마이그레이션이 없음"을 검사해 빌드를 막습니다.
4. 마이그레이션은 바로 저장하지 않고 변경 범위 미리보기를 보여 줍니다 (`bdg migrate --dry-run`).

### 큰 씬은 레이어 파일로 분할

```
scenes/forest_01/
  scene.jsonl       ← 헤더, 카메라, 조명
  terrain.jsonl
  enemies.jsonl     ← "적 배치 고쳐 줘" → 이 파일만 읽음
  props.jsonl
```

작은 씬은 파일 하나, 큰 씬만 폴더로. 에이전트 여러 개가 서로 다른 레이어를 동시에 편집 가능.

### 섞지 않는 데이터

| 데이터 | 위치 |
|---|---|
| 직접 만든 씬 데이터 | `.scene` |
| 베이크 결과 (라이트맵, 내비메시) | `.cache/` 바이너리 |
| 에디터 상태 (뷰 카메라, 선택, 숨김, 펼친 폴더) | `.user/` |
| 게임 빌드용 바이너리 | `.cache/cooked/<플랫폼>/` ([아래](#빌드에서는-바이너리)) |

### 빌드에서는 바이너리

텍스트는 **에디터와 저장소 전용**입니다. 게임 실행 중에 JSON을 파싱하면 큰 씬의 로딩이 느리고 모바일에서 특히 불리합니다.

| 단계 | 데이터 |
|---|---|
| 에디터 | 텍스트 파일을 읽어 메모리에 올림. Play 모드도 메모리의 데이터를 그대로 사용 |
| 빌드(쿠킹) | 씬, 프리팹, 위젯, 애니메이션, 그래프, 설정을 플랫폼별 **바이너리**로 변환. 기본값을 채우고, id 문자열을 숫자로 바꾸고, 참조를 미리 연결 |
| 게임 실행 | 바이너리를 메모리에 바로 매핑해 로드. JSON 파서는 게임 빌드에 들어가지 않음 |

- 개발 빌드에서는 텍스트 로딩도 켤 수 있습니다 (모드 지원, 빠른 반복용).
- 쿠킹 결과는 원본이 바뀐 파일만 다시 만들고, `.cache/`에 두어 git에는 올리지 않습니다.

## 프리팹 형식 (`.prefab`, JSONL)

씬과 같은 규칙입니다. 첫 줄은 헤더, 둘째 줄이 루트 오브젝트입니다.

```jsonl
{"v":1, "defaults":"1.0", "prefab":"goblin"}
{"id":"root", "name":"Goblin", "type":"Mesh", "Mesh":{"model":"models/goblin.glb", "material":"materials/goblin.mat"}, "CapsuleCollider":{"height":1.2, "radius":0.4}, "Health":{}}
{"id":"hpbar", "parent":"@root", "pos":[0,1.6,0], "Widget":{"ui":"ui/hpbar.ui", "space":"World", "billboard":true}}
```

프리팹 변형(Variant)은 `"base":"prefabs/goblin.prefab"`을 두고 바뀐 값만 적습니다.

## 에셋 메타 (`.meta`)

```jsonc
{"guid":"7f3a9c21e4b84d0f", "importer":"texture", "srgb":true, "mipmaps":true,
 "platforms":{"Android":{"format":"ASTC_6x6", "maxSize":1024}}}
```

## 에셋 출처와 라이선스

`.meta`에 출처와 라이선스를 적습니다. 엔진이 빌드 때 크레디트 화면과 라이선스 목록을 자동으로 만들고, 문제가 있으면 경고합니다.

```jsonc
// textures/grass.png.meta
{"guid":"7f3a9c21e4b84d0f", "importer":"texture",
 "source":{"author":"Kenney", "url":"https://kenney.nl/assets", "license":"CC0-1.0"}}

// audio/boss_theme.ogg.meta
{"guid":"a01c55e2b9f04c12", "importer":"audio",
 "source":{"author":"OO Studio", "license":"Custom", "commercial":false, "credit":"Music by OO Studio"}}
```

| 기능 | 내용 |
|---|---|
| 라이선스 표준 이름 | SPDX 식별자 사용 (`CC0-1.0`, `CC-BY-4.0`, `MIT` …), 그 밖에는 `Custom` + 조건 필드 |
| 출처 없는 에셋 표시 | Project 패널에 "출처 없음" 배지. 임포트할 때 출처를 묻는 칸 제공 (건너뛸 수 있음) |
| 크레디트 자동 생성 | `credit` 또는 CC-BY 계열 에셋을 모아 `credits.md`와 크레디트 화면용 위젯 데이터 생성 |
| 빌드 검사 | `commercial:false` 에셋이 들어간 상업 빌드는 경고 (프로젝트 설정에서 실패로 바꿀 수 있음) |
| 목록 출력 | `bdg assets licenses` → 에셋별 출처·라이선스 표 |

- 이 기능은 법률 판단을 대신하지 않습니다. 사용자가 적은 정보를 모으고 누락을 알려 주는 도구입니다.

## 2D 씬 예시

```jsonl
{"v":1, "defaults":"1.0", "scene":"stage_1", "mode":"2d"}
{"id":"hero", "type":"Sprite", "pos":[2,1], "Sprite":{"img":"sprites/hero.png", "order":10}, "BoxCollider2D":{"size":[0.8,1.6]}, "Rigidbody2D":{}, "Hero":{"speed":4}}
{"id":"coin_01", "prefab":"prefabs/coin.prefab", "pos":[5,2]}
{"id":"ground", "type":"Tilemap", "Tilemap":{"palette":{"#":"tiles/stone"}, "chunks":{"0,0":["","","########"]}}}
```

## AI가 읽을 때: 압축 표기 (`bdg view`)

저장은 JSON, 보기는 압축 표기. 같은 정보를 대략 40~50% 적은 토큰으로 읽습니다(추정치).

```
cam     Camera  pos 0,5,-10  rot 20,0,0
sun     Light   rot 50,-30,0             | Light kind=Directional intensity=1.2
player  Mesh    #Player                  | Mesh model=models/knight.glb | Rigidbody mass=70 | PlayerController speed=6.5 cam=@cam
Enemies Empty
  g1    goblin.prefab  pos 6,0,1         | Health hp=12
  g2    goblin.prefab  pos 8,0,3
```

전용 문법(DSL)으로 저장하는 방안은 AI 문법 실수와 파서 유지보수 비용 때문에 채택하지 않았습니다.


## 분할 저장 (2026-11-21)

모든 것을 파일 하나의 텍스트나 JSON에 넣지 않습니다. **크기와 성격에 따라 나눠** 저장합니다.

| 데이터 | 저장 방식 | 이유 |
|---|---|---|
| 작은 씬 (오브젝트 수천 개 이하) | 씬 파일 하나 (JSONL, 한 줄에 오브젝트 하나) | 한눈에 보이고 AI가 한 번에 읽음 |
| 큰 씬, 오픈월드 | **오브젝트별 파일** 또는 **월드 파티션 셀별 파일** (`Scenes/forest_01/cells/x3_z5.jsonl`, 언리얼 OFPA와 같은 방식) | 여러 사람이 동시에 편집해도 충돌이 거의 없음, 필요한 셀만 불러옴, AI도 필요한 부분만 읽음 |
| 프리팹 | 프리팹마다 파일 하나 | 재사용과 오버라이드 단위 |
| 큰 숫자 데이터 (메시, 지형 높이맵, 복셀, 라이트맵, 내비메시, 애니메이션 키) | **바이너리 옆 파일** (`.bin`, `.vox` 등). 텍스트 파일에는 GUID 참조만 | 텍스트로 쓰면 크고 느리며 AI에게도 쓸모없는 토큰 |
| 원본 에셋 (PNG, glTF, WAV) | 원본 그대로 + `.meta` | 외부 도구와 호환 |
| 캐시 (바이너리 씬 사본, 쿠킹 결과, 썸네일) | `.cache/` (git 제외) | 언제든 다시 만들 수 있음 |

- 씬을 하나의 파일로 둘지 나눌지는 씬 설정에서 고르고, `bdg scene split` / `bdg scene merge`로 언제든 바꿀 수 있습니다. 내용은 같고 저장 위치만 달라집니다.
- 나눠 저장해도 `bdg view`와 `bdg query`는 씬 전체를 하나처럼 보여 줍니다.
- 바이너리 옆 파일은 형식 버전과 해시를 머리에 넣어, 텍스트 쪽 참조와 맞지 않으면 `bdg validate`가 알려 줍니다.
