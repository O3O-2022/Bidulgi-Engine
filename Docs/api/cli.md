# bdg 명령줄

에디터와 같은 명령 버스로 장면을 읽고 바꿉니다(실행 취소 기록에 남음). 모든 명령에 `--json`을 붙이면
결과와 오류가 JSON 한 개로 나옵니다: `{"ok", "exit", "command", "output": [...], "error": {"code", "message", "hint"}}`.

```
bdg — 비둘기 엔진 명령줄 (에디터와 같은 명령 버스)
  new <dir> [--template basic3d|empty|fps|platformer2d|thirdperson] [--name 게임 이름]   (fps, platformer2d, thirdperson = 바로 할 수 있는 게임 템플릿)
  view [scene] | outline [scene] | query <pattern> [-f pos,Health.hp] | schema [Type]
  set <target> path=value ... [--dry-run] | add <id> [--name N] [--parent P] [--pos x,y,z] [--comp Type]
  del <id>... | addcomp <id> <Type> | rmcomp <id> <Type>
  validate [scene] | refs <asset> | unused
  csharp-bindings [file]  (C# 클래스 생성)
  table import <csv> [out] | export <table.jsonl> [out] | check <file>  (데이터 테이블)
  build [--check] [--out <dir>] [--config Debug|Development|Shipping] [--zip] [--scripts a.dll;b.dll]  (게임 빌드)
  nodes [filter] | call <target> <Component.Function | Component.Get x | Component.Set x> [인자...]
  shot [scene] [--bookmarks] [--camera yaw,pitch,dist[,x,y,z]] [--quality 최소|낮음|보통|높음|최고] [--out <dir>] [--frames N]  (장면 사진, 파일 경로 출력)
공통: --project <dir> --scene <이름 또는 경로> --json (결과·오류를 JSON 한 개로)
```
