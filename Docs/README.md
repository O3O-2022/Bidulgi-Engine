# 비둘기 엔진 (Bidulgi Engine) 문서

AI 에이전트와 사람이 같은 방식으로 다루는 범용 게임 엔진의 기획 문서입니다.
겉모습과 사용법은 유니티, 엔진 내부 설계는 언리얼, 모든 데이터는 텍스트로 저장합니다.

> 엔진 이름: **비둘기 엔진** (영문 Bidulgi Engine, 2026-11-22 최종 확정). CLI는 `bdg`, C++ 매크로 접두어는 `B`(`BCLASS`), Python 모듈은 `bidulgi`입니다. UI 시안(`prototype/forge-editor.html`)은 v0.2에서 이름을 바꿀 예정이라 아직 "Forge"로 표시됩니다.

> **다음에 할 일은 [QUEUE.md](QUEUE.md)** (만들 순서대로 정리한 작업 대기열). 새 대화는 이 파일부터 읽습니다.

## 폴더 구성

| 폴더 | 내용 |
|---|---|
| `planning/` | 비전, 로드맵, 기능 백로그, 결정 기록, 시안 계획 |
| `architecture/` | 엔진 내부 구조 (코어, 렌더링·플랫폼, 스크립팅, 명령 버스·리플렉션) |
| `features/` | 기능별 상세 기획 (데이터 형식, 에디터 UI, UI 위젯, 애니메이션 등) |
| `../prototype/` | HTML UI 시안 |

## 문서 목록

### planning — 기획
- [비전과 목표](planning/vision.md)
- [로드맵](planning/roadmap.md)
- [기능 백로그](planning/backlog.md)
- [결정 기록과 미결 사항](planning/decisions.md)
- [UI 시안 v0.2 계획](planning/mockup-v0.2.md)
- [기획 검토 2026-11-20 (문제점·편의성·추가 기능)](planning/review-2026-11-20.md)
- [다른 상용 엔진에 없는 기능](planning/unique-features.md)
- [위험과 약점 대응 (1.0 범위 확정)](planning/risk-response.md)
- [유니티 + 언리얼 혼합 문제와 정답](planning/unity-unreal-merge.md)
- [디자인 일치 검사 (HTML 시안 ↔ 엔진)](planning/design-parity-test.md)
- [추가 편의 기능 (2026-11-23)](planning/convenience-features.md)
- [대형 게임 트랙 (오픈월드·대형 FPS, 1.0 이후)](planning/large-game-track.md)

### architecture — 구조
- [전체 구조](architecture/overview.md)
- [명령 버스와 리플렉션](architecture/command-bus-and-reflection.md)
- [렌더링과 플랫폼](architecture/rendering-and-platforms.md)
- [스크립팅 (다국어)](architecture/scripting.md)
- [그래프·애니메이션·코드 상호작용](architecture/interop.md)
- [물리 (CPU·GPU 백엔드 교체, 복셀 파괴)](architecture/physics.md)
- [외부 라이브러리·디자인·패키지](architecture/libraries-and-packaging.md)

### features — 기능
- [데이터 형식 (씬, 프리팹, 에셋)](features/data-formats.md)
- [2D / 3D 통합](features/2d-3d.md)
- [에디터 UI](features/editor-ui.md) — 전체 창 시안 v0.3: [prototype/v0.3/all-windows.html](../prototype/v0.3/all-windows.html) (19개 화면)
- [프로젝트 설정](features/project-settings.md)
- [UI 위젯 시스템](features/ui-widgets.md)
- [애니메이션 (애니메이션 그래프 + 타임라인)](features/animation.md)
- [비주얼 스크립팅 (그래프)](features/visual-scripting.md)
- [전역 조명 (Halo GI)](features/global-illumination.md)
- [머티리얼과 텍스처 (언리얼식)](features/materials-and-textures.md)
- [그래픽 테스트 씬 (렌더링 기준점)](features/graphics-test-scenes.md)
- [AI 에이전트 연동](features/agent-integration.md)
- [입력 (입력 액션)](features/input.md)
- [언어별 코드 문법 예제](features/code-syntax.md)
- [쉬운 엔진 API (언어별 자주 쓰는 함수)](features/easy-api.md)
- [프로젝트 생성·열기·엔진 버전·템플릿](features/projects-and-templates.md)
- [에디터 디자인 시스템 (색·글꼴·간격·부품 규칙)](../design-system/project/README.md) — 새 화면은 모두 이 규칙을 따릅니다

## 문서 작성 규칙

- 한 파일에 한 주제. 길어지면 나눕니다.
- 결정된 내용은 `planning/decisions.md`에 날짜와 함께 기록합니다.
- 파일 형식 예시는 실제로 쓸 형식 그대로 적습니다 (AI가 그대로 따라 쓸 수 있게).
