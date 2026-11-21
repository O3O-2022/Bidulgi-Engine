# 프로젝트 설정

화면과 구조는 **언리얼 Project Settings 방식**, 저장은 텍스트입니다.

## 화면 구성

```
┌ Project Settings ─────────────────────────────────────────────────────┐
│ [검색: shadow, gravity, android ...]   편집 대상: [기본 ▾]             │
├──────────────┬────────────────────────────────────┬───────────────────┤
│ PROJECT      │ Rendering                          │ 저장되는 파일       │
│  Description │ RHI 백엔드와 렌더링 경로입니다.      │ settings/         │
│  Maps & Modes│ ─────────────────────────────────  │   project.json    │
│  Packaging   │ RHI Backend      [OpenGL 4.5 ▾]    │ settings/platforms│
│  Platforms   │   rendering.rhi                    │   /android.json   │
│ ENGINE       │ Render Path      [Forward+ ▾]  ↺   │                   │
│  Rendering ● │ Shadow Cascades  [4]               │ (기본값은 저장     │
│  Scalability │ ...                                │  안 함)           │
│  Physics     │                                    │                   │
│  Input       │                                    │                   │
│  Scripting   │                                    │                   │
│  Agent  ★    │                                    │                   │
│ PLATFORMS    │                                    │                   │
│  Windows     │                                    │                   │
│  Android     │                                    │                   │
│  iOS / Web   │                                    │                   │
└──────────────┴────────────────────────────────────┴───────────────────┘
```

- **왼쪽:** 카테고리 트리. 값을 바꾼 카테고리에 ● 표시. ★ = 이 엔진 고유 항목
- **위:** 전체 검색. 검색하면 모든 카테고리에서 일치하는 항목만 모아 보여 줌
- **가운데:** 항목마다 이름과 키(`rendering.rhi`). 기본값과 다르면 ↺ 되돌리기 버튼
- **편집 대상:** 기본(모든 플랫폼) / Windows / Android / iOS / Web / Console. 플랫폼을 고르면 그 플랫폼 값만 덮어씀
- **오른쪽:** 실제로 저장되는 파일 미리보기

## 카테고리와 주요 항목

| 그룹 | 카테고리 | 항목 예시 |
|---|---|---|
| Project | Description | 이름, 회사, 버전, 아이콘 |
| | Maps & Modes | 게임 시작 씬, 에디터 시작 씬 |
| | Packaging | 빌드 구성(Debug/Development/Shipping), 에셋 압축, 에디터 모듈 제외, 미사용 모듈 제외 |
| | Supported Platforms | Windows, Linux, macOS, Android, iOS, Web, Console |
| Engine | Rendering | RHI, 렌더 경로, 안티에일리어싱, HDR, VSync, 그림자 단계, 텍스처 압축 |
| | Scalability (품질 단계) | 기본 단계(감자·저사양·중사양·고사양·초고사양, 기본 중사양), 게임의 최소·최대 단계, 자동 감지, 동적 조정, 해상도 배율, 프레임 제한 |
| | Physics | 3D/2D 중력, 고정 타임스텝 |
| | Input | 액션 이름 ↔ 키 매핑 (`Jump = Space`) |
| | Scripting | 기본 언어, 언어별 사용 여부, 핫 리로드 |
| | **Agent** ★ | CLI, MCP 서버와 포트, 승인 정책(Never / Destructive Only / Always), 명령당 최대 변경 수, 압축 출력 |
| Platforms | Android | 패키지 이름, 최소/대상 SDK, 아키텍처 |
| | iOS | 번들 ID, 최소 버전 |
| | Web | 초기 메모리, 스레드 사용 |
| | Windows | DPI 인식, 대체 RHI |

## 저장: 계층형 텍스트 (언리얼 설정 계층과 같은 구조)

```
엔진 기본값  →  settings/project.json  →  settings/platforms/<플랫폼>.json
 (코드 안)        (모든 플랫폼 공통)          (해당 플랫폼만 덮어씀)
```

언리얼의 Base → Default → Platform `.ini` 계층과 같은 구조이고, 파일 형식만 JSON입니다. 기본값과 같은 값은 저장하지 않습니다.

```jsonc
// settings/project.json
{
  "project": {"name":"MyGame", "company":"MyStudio"},
  "platforms": {"android":true, "web":true},
  "scripting": {"rust":true}
}
```

```jsonc
// settings/platforms/android.json
{
  "android": {"minSdk":28},
  "rendering": {"rhi":"GLES 3.0", "path":"Forward", "textureFormat":"ASTC", "shadowCascades":2},
  "scalability": {"default":"medium", "min":"low"}
}
```

## 명령

UI에서 바꾼 값도 아래 명령으로 기록되어 Undo와 Agent 기록에 남습니다.

```
bdg config get rendering                       → rendering.* 의 현재 값과 출처
bdg config set rendering.shadowCascades=2 --platform Android
bdg config reset rendering.path --platform Android
```

## 스크립트에서 읽기

```csharp
var rhi = ProjectSettings.Get<string>("rendering.rhi");   // 현재 플랫폼 기준 최종 값
```
