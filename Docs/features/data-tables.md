# 데이터 테이블 (4-4d)

작성 2026-11-25. 무기·적·아이템처럼 **행이 많은 게임 데이터**를 텍스트 파일 하나에 두고 스크립트에서 한 줄로 읽습니다. 언리얼 DataTable, 유니티 ScriptableObject 표에 해당합니다.

## 파일

`Data/Weapons.table.jsonl` — 한 줄에 한 행(JSON 객체). 첫 줄에 표 설명을 둘 수 있습니다(없어도 됨).

```jsonl
{"$table":{"key":"id","columns":[{"name":"id","type":"string"},{"name":"damage","type":"int"},{"name":"range","type":"float"}]}}
{"id":"rifle","damage":12,"range":50.5}
{"id":"pistol","damage":5}
```

- **키**: 행 이름이 들어 있는 열(기본 `id`, 설명 줄의 `key`로 바꿈). 키가 없는 행, 같은 키가 두 번 나온 행(첫 행을 씀)은 줄 번호와 함께 경고하고 건너뜁니다.
- **열 타입**: `int`·`float`·`bool`·`string`·`json`. 설명 줄이 있으면 타입이 틀린 값(`"hp":"lots"`)은 경고 후 그 행을 건너뛰고, 설명에 없는 열은 경고만 하고 둡니다. 설명 줄이 없으면 행에 나온 키 순서대로 열을 만들고 타입을 짐작합니다.
- 빈 줄, `//`로 시작하는 줄은 무시. git diff·병합·AI 편집에 유리하도록 한 행 = 한 줄.
- 찾는 곳: 프로젝트(작업 폴더)의 `Data/`, 그다음 `Assets/Data/`. 각 폴더에서 `<이름>.table.jsonl`, 없으면 `<이름>.csv`.

## CSV

스프레드시트 프로그램(엑셀·구글 시트)에서 내보낸 CSV를 그대로 둬도 표로 읽힙니다(`Data/Enemies.csv`). 첫 줄은 열 이름, 쉼표·`"따옴표"`·`""` 이스케이프·셀 안 줄바꿈·CRLF·UTF-8 BOM 지원. 열 타입은 그 열의 빈칸이 아닌 셀이 모두 정수면 `int`, 숫자면 `float`, true/false면 `bool`, 아니면 `string`. **빈 셀 = 값 없음**(스크립트 클래스의 기본값). 키 열은 `id`가 있으면 그것, 없으면 첫 열.

```powershell
bdg table import Data/Enemies.csv            # → Data/Enemies.table.jsonl (설명 줄 포함)
bdg table export Data/Weapons.table.jsonl    # → Data/Weapons.csv (스프레드시트에서 고치고 다시 import)
bdg table check Data/Weapons.table.jsonl     # 행·열 수와 문제 목록 (문제가 있으면 종료 코드 1)
```

## C#에서 읽기

```csharp
public class Weapon { public string id = ""; public int damage; public float range = 1; }

[Table("Enemies")] public class EnemyRow { public string id = ""; public int hp = 10; }

int d = Data.Get<Weapon>("rifle").damage;          // 표 이름 = [Table], 없으면 클래스 이름 + s (Weapons), 없으면 클래스 이름
if (Data.TryGet<Weapon>("laser", out var w)) { }   // 없을 수도 있는 행
foreach (Weapon x in Data.All<Weapon>()) { }        // 파일 순서
int hp = Data.Row("Enemies", "goblin")?["hp"]?.GetValue<int>() ?? 0; // 클래스 없이
```

- 행의 값은 같은 이름의 public 필드·속성에 들어갑니다(대소문자 무시). 행에 없는 값은 클래스의 기본값.
- 없는 행·표: `Get`은 경고 로그 + `new T()`(기본값) — 예외로 멈추지 않음([easy-api](easy-api.md) 원칙 3). 그래서 `T`는 매개변수 없는 생성자가 필요합니다.
- 표는 처음 쓸 때 한 번 읽고 **플레이가 끝날 때까지** 보관합니다(핫 리로드 때도 다시 읽음). 파일을 고친 뒤 Play를 다시 누르면 반영. 돌려받은 행 객체는 공유되므로 읽기 전용으로 쓰세요.
- 엔진 쪽: 스크립트 요청 `{"op":"table","name":"Weapons"}` → `{"name","key","rows":[...]}`(장면이 없어도 됨), 코드 `Engine/Scripting/DataTable.*`.

## 자동 검사

- 단위 테스트: 파싱·설명 줄 타입 검사·문제 줄 번호·쓰기/읽기 왕복·CSV(따옴표·줄바꿈·BOM·빈 셀)·CSV 내보내기/가져오기 왕복·폴더에서 이름으로 읽기·스크립트 요청.
- `BDG_DATATABLE_TEST=1` + `BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll`: `.cache` 아래에 표 두 개(jsonl·csv)를 쓰고 샘플 `DataProbe`가 플레이 중에 읽음 → `data probe: get=1 defaults=1 all=1 csv=1 missing=1 untyped=1`.

## 남은 것

- **에디터 스프레드시트 보기**(셀 편집·열 정렬·명령 버스 Undo): 공용 UI(`UI/*`, `EditorModel`, Project 패널)를 크게 고쳐야 해서 이번에는 하지 않음. 지금은 텍스트 편집기나 CSV(스프레드시트 프로그램) → `bdg table import`.
- 행 구조를 C++ 리플렉션 구조체로 정하기(`BSTRUCT` → 설명 줄 자동 생성), Lua·Graph의 `Data.get`(M5), 파일이 바뀌면 플레이 중에도 다시 읽기.
