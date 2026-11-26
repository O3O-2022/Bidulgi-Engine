# MCP 서버 — `bdg mcp` (AI-4, 2026-11-26)

AI 에이전트(Claude Code, Claude Desktop, 그 밖의 MCP 클라이언트)가 비둘기 엔진 프로젝트를 **직접** 다루게 하는 서버입니다. 장면을 보고, 명령 버스로 고치고(되돌리기 가능, dry run), 검사하고, 사진을 찍고, 자동 검사를 돌리고, 빌드하고, 로그와 엔진 문서를 읽습니다. 코드: `Engine/Tools/Mcp.*`.

## 연결

Claude Code(프로젝트의 `.mcp.json`) 또는 Claude Desktop(`claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "bidulgi": {
      "command": "bdg",
      "args": ["mcp", "--project", "C:/Games/MyGame"]
    }
  }
}
```

`bdg`가 PATH에 없으면 `"command"`에 `bdg.exe`의 전체 경로를 씁니다(엔진 빌드의 `build/Release/bdg.exe`). Claude Code에서는 `claude mcp add bidulgi -- bdg mcp --project C:/Games/MyGame`도 됩니다.

## 도구

모든 도구는 같은 이름의 `bdg` 명령을 `--json`으로 실행한 결과를 돌려줍니다(`structuredContent` = `{"ok","exit","command","output":[줄],"error"}`, 실패면 `isError: true`). 그래서 AI가 보는 것과 터미널의 `bdg`가 보여 주는 것이 같습니다.

| 도구 | 하는 일 | `bdg` |
|---|---|---|
| `scene_view` | 장면 전체, 한 줄에 한 물체(처음엔 이것) | `view` |
| `scene_outline` | 이름 트리 | `outline` |
| `query` | 패턴에 맞는 물체들의 필드 표 (`pattern`, `fields`) | `query` |
| `schema` | 컴포넌트 필드·형식·기본값 (`type`) | `schema` |
| `set` | 필드 바꾸기 `{"target":"Player","values":{"Health.hp":20,"pos":[6,0,4]}}` | `set` |
| `add` | 물체 추가 (`id`, `name`, `parent`, `pos`, `components`) | `add` |
| `del` | 물체 삭제 (`ids`) | `del` |
| `addcomp` / `rmcomp` | 컴포넌트 추가·제거 (`id`, `type`) | `addcomp` / `rmcomp` |
| `validate` | 장면·참조·에셋 규칙 검사 | `validate` |
| `shot` | 장면 사진 — **PNG를 이미지 블록으로** + 경로 (`camera`, `quality`, `bookmarks`) | `shot` |
| `run_check` | 에디터 자동 검사 하나(`BDG_*_TEST`)의 결과 줄 (`test`, `frames`, `env`) | `check` |
| `build` | 게임 빌드 / 검사만 (`check`, `config`, `zip`) | `build` |
| `read_log` | 프로젝트 `Bidulgi.log`의 끝 (`lines`) | — |
| `list_docs` / `read_doc` | 엔진 문서: `llms.txt`, `api/*`, `features/*` | — |

- 고치는 도구(`set` `add` `del` `addcomp` `rmcomp`)는 모두 **`dry_run`**: 바뀔 줄만 보여 주고 저장하지 않음. 한 번의 호출 = 명령 버스의 한 단계(에디터에서 Ctrl+Z 한 번).
- 모든 장면 도구에 `scene`(`Scenes/`의 이름 또는 .scene 경로, 기본 Main).
- 읽기 도구에는 `readOnlyHint` 표시 — 클라이언트가 허락 없이 부를 수 있음.

## 리소스

| URI | 내용 |
|---|---|
| `bidulgi://docs/llms.txt` | 엔진 AI 참고서(컴포넌트, `bdg` 명령, C# API 한 파일) |
| `bidulgi://schema/components` | 모든 컴포넌트 스키마(`bdg schema --json`) |
| `bidulgi://project/scene` | 프로젝트 Main 장면(`bdg view --json`) |

## 프로토콜

- stdio, 줄 단위 JSON-RPC 2.0(한 줄에 한 메시지, UTF-8). stdout에는 프로토콜만 — 로그는 쓰지 않음.
- MCP **2025-06-18**(클라이언트가 2025-03-26 / 2024-11-05를 말하면 그 버전으로 답함). `initialize`, `ping`, `tools/list`, `tools/call`, `resources/list`, `resources/read`, `prompts/list`(빈 목록), 배치 배열.
- 오류: 파싱 -32700, 없는 메서드 -32601, 잘못된 인자 -32602, 없는 리소스 -32002. 도구 실행 실패는 오류가 아니라 `isError: true` 결과(모델이 읽고 고칠 수 있게).

## 라이브 에디터

에디터가 그 프로젝트에서 열려 있으면 `127.0.0.1`의 빈 포트에서 기다리고 `.cache/editor.json`(`port`, `pid`, `token`)을 씁니다. 그러면 **열린 장면의 도구**(`scene_view` `scene_outline` `query` `set` `add` `del` `addcomp` `rmcomp`)는 파일 대신 에디터로 갑니다:

- 고치기는 에디터의 **명령 버스**로 — 화면에 바로 보이고, 한 번의 호출이 Ctrl+Z 한 번. 저장은 하지 않음(제목에 ●, 저장은 사람이).
- 읽기는 저장 안 된 지금 장면을 봄. `dry_run`은 열린 장면의 복사본에서.
- `scene`을 주면(다른 장면), 그리고 `validate` `shot` `build` `run_check`는 파일로.
- Play 중에는 거절("Stop한 뒤 다시"). 에디터가 없거나 답하지 않으면(파일이 남았는데 에디터가 꺼짐) 조용히 파일로.
- 자동 실행(`--frames`, `BDG_HIDDEN` — `bdg shot`·`bdg check`가 띄우는 에디터)은 기다리지 않음: 사람이 연 에디터의 `editor.json`을 덮지 않게. 에디터가 끝나면 파일을 지움.

## 검사

- 단위 테스트 `Tests/McpTests.cpp`: 줄 나누기(CRLF, 마지막 줄), initialize·버전 협상, 알림 무응답, 오류 코드, 배치, Base64, 모든 도구의 `bdg` 인자(가짜 `bdg`), dry_run, 라이브 전달과 되돌아감, 리소스.
- `BDG_MCP_LIVE_TEST`(CI): 에디터 안의 클라이언트 스레드가 `bdg mcp`처럼 `editor.json`을 읽고 이름 바꾸기·dry run·view를 보냄 → `mcp live test: file=1 set=1 dry=1 view=1 undo=1`, 끝나면 `editor.json` 없음.
- CI: 한글 폴더의 새 프로젝트에 `initialize` → `tools/list` → `scene_view` → `set`(dry_run) → `shot`을 파이프로 넣고 답 5줄·장면 불변·PNG 이미지 블록 확인.
