# 기능 백로그

우선순위별 기능 목록입니다. 상세 기획이 있는 항목은 링크를 따라가세요.

## MVP — 없으면 엔진이 아닌 것

- [ ] GameObject/Component, 프리팹과 프리팹 변형 → [data-formats](../features/data-formats.md)
- [ ] 텍스트 씬 저장, 에셋 DB(GUID) → [data-formats](../features/data-formats.md)
- [ ] 명령 버스, Undo/Redo → [command-bus](../architecture/command-bus-and-reflection.md)
- [ ] 에이전트 CLI → [agent-integration](../features/agent-integration.md)
- [ ] 에디터 기본 패널 → [editor-ui](../features/editor-ui.md)
- [ ] 에셋 임포트와 플랫폼별 쿠킹 → [rendering-and-platforms](../architecture/rendering-and-platforms.md)
- [ ] 2D/3D 렌더링, 카메라 → [2d-3d](../features/2d-3d.md)
- [ ] C# 스크립팅, Play 모드 → [scripting](../architecture/scripting.md)
- [ ] 머티리얼 에디터 (노드 그래프, 텍스트 저장)
- [ ] 애니메이션 그래프 (State Machine, Blend Space, Montage, IK, 리타게팅) → [animation](../features/animation.md)
- [ ] 입력 액션 매핑 (언리얼 Enhanced Input 방식) → [input](../features/input.md)
- [ ] 오디오 믹서 (버스, 3D 사운드)
- [ ] 프로파일러와 프레임 디버거
- [ ] **헤드리스 자동 테스트** — AI가 고친 뒤 스스로 검증하는 데 필수
- [ ] 플레이 녹화 → 자동 테스트 (`.playtest`) → [agent-integration](../features/agent-integration.md#플레이-녹화--자동-테스트)
- [ ] 명령 실행 전 변경 범위 미리보기 (`--dry-run`) → [command-bus](../architecture/command-bus-and-reflection.md#변경-범위-미리보기)

## 1.0 — 상용 수준

- [ ] 프로젝트 설정 (계층형) → [project-settings](../features/project-settings.md)
- [ ] UI 위젯 시스템 → [ui-widgets](../features/ui-widgets.md)
- [ ] 타임라인 애니메이션 → [animation](../features/animation.md)
- [ ] 그래프 비주얼 스크립팅 (VM → AOT 네이티브) → [visual-scripting](../features/visual-scripting.md)
- [ ] VFX (언리얼 Niagara식 모듈 스택)
- [ ] 내비메시와 길찾기, 비헤이비어 트리
- [ ] 터레인과 폴리지
- [ ] 라이트 베이크, 포스트 프로세스 볼륨
- [ ] 현지화 (`@loc.key`)
- [ ] 세이브 시스템
- [ ] 패키지 매니저, git + LFS 연동
- [ ] Lua, Python(AI·도구), C++ 모듈 스크립팅
- [ ] "왜 안 보이지?" 진단 (`bdg diagnose`) → [editor-ui](../features/editor-ui.md#편의-기능-2026-11-20-채택)
- [ ] Hierarchy·Project 자연어 검색 → [editor-ui](../features/editor-ui.md#편의-기능-2026-11-20-채택)
- [ ] 편집 중 플랫폼 예산 표시 → [rendering-and-platforms](../architecture/rendering-and-platforms.md#편집-중-플랫폼-예산-표시)
- [ ] UI 자동 점검 (대비, 글자 크기, 터치 영역, 번역문 넘침) → [ui-widgets](../features/ui-widgets.md#ui-자동-점검-2026-11-20-채택)
- [ ] 에셋 출처·라이선스 추적, 크레디트 자동 생성 → [data-formats](../features/data-formats.md#에셋-출처와-라이선스)

## 나중에

- [ ] **대형 게임 트랙 L1–L7** → [large-game-track](large-game-track.md)
  - [ ] L1 SDL_GPU 렌더러, 멀티스레드, GPU 컬링, 오클루전
  - [ ] L2 월드 스트리밍, 64비트 좌표, LOD·HLOD, 터레인·식생, 비동기 스트리밍
  - [ ] L3 실시간 GI (Halo GI Tier 1~2, 기준 이미지 자동 비교 CI), 고급 그림자, 볼류메트릭 → [global-illumination](../features/global-illumination.md)
  - [ ] L4 멀티플레이어: 리플리케이션, 예측·보정, 전용 서버, 매치메이킹 연동, 리플레이
  - [ ] L5 정밀 전투: 탄도, 관통, 부위 판정·방어구, 1인칭 무기, 부착물
  - [ ] L6 모션 매칭, 전신 IK, 1·3인칭 동기화
  - [ ] L7 인벤토리 프레임워크, 서버 영속화, 안티치트 연동, 크래시 리포터, 패치
- [ ] 모드 지원 (Lua 샌드박스)
- [ ] (1.0으로 이동) 비주얼 스크립팅 → 그래프 항목 참고

## AI 전용 (차별점)

- [ ] 씬 diff와 병합 도구 (여러 에이전트 동시 작업)
- [ ] 에이전트 권한 설정 (삭제, 빌드 등은 승인 필요)
- [ ] 스크린샷 기반 시각 회귀 테스트
- [ ] `explain` 명령 — 오브젝트나 시스템이 왜 이렇게 동작하는지 텍스트로 설명
- [ ] 스키마와 API 문서 자동 생성
