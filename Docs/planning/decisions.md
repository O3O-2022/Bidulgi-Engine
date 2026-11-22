# 결정 기록과 미결 사항

## 결정된 것

| 날짜 | 결정 | 이유 |
|---|---|---|
| 2026-11-19 | 데이터는 텍스트(JSON 중심)로 저장, JSON Schema로 검증 | AI가 읽고 쓰기 쉽고, 문법이 엄격해 실수가 바로 잡힘 |
| 2026-11-19 | 용도별 형식: 씬·프리팹은 JSON, 표 데이터는 TSV, 대사·문서는 Markdown | 토큰 효율과 편집 편의 |
| 2026-11-19 | 사용법과 개념은 유니티와 같게 | 유니티 사용자의 진입 장벽 제거, AI의 사전 지식 재활용 |
| 2026-11-19 | 2D와 3D 모두 지원 | 범용성 |
| 2026-11-19 | 엔진 코어는 C++ | 성능과 그래픽 생태계 |
| 2026-11-19 | 스크립팅은 C#, C++, Rust, Lua(Luau), Python 동시 지원. C API 허브와 바인딩 자동 생성 | 언어 수가 늘어도 유지보수 비용이 곱절로 늘지 않음 |
| 2026-11-19 | 겉은 유니티, 엔진 내부 구조는 언리얼 | 사용 편의(유니티)와 멀티플랫폼 설계(언리얼)의 장점만 |
| 2026-11-19 | 그래픽은 RHI 추상화, 첫 백엔드는 OpenGL 4.5 | 콘솔·Apple은 OpenGL로 불가능. 나중에 Vulkan, Metal, D3D12 추가 |
| 2026-11-19 | 씬 형식: 오브젝트당 한 줄, 기본값 생략, `id`와 `name` 분리, 스크립트도 일반 컴포넌트 | [data-formats](../features/data-formats.md) 참고 |
| 2026-11-19 | 저장은 JSON, AI가 읽을 때는 압축 표기(`view`) | 파싱 안정성과 토큰 효율을 둘 다 |
| 2026-11-20 | 프로젝트 설정은 언리얼 방식 | 카테고리 트리, 검색, 플랫폼별 덮어쓰기 |
| 2026-11-20 | UI는 언리얼 위젯 방식, Screen/Camera/World 공간 지원 | [ui-widgets](../features/ui-widgets.md) |
| 2026-11-20 | 블렌더식 애니메이션 도구 내장 (모델링은 블렌더 연동) | 블렌더 전체를 넣으면 범위가 지나치게 커짐 |
| 2026-11-20 | C#은 유니티 문법, C++은 언리얼 매크로 문법 | 각 엔진 사용자의 습관을 그대로 |
| 2026-11-20 | Lua는 Luau 사용, Rust는 속성 매크로 + 쿼리 방식, Python은 에디터 자동화 전용 | [scripting](../architecture/scripting.md) |
| 2026-11-20 | 문서는 `Docs/` 아래 기능별 Markdown으로 관리 | 이 폴더 |
| 2026-11-20 | 엔진 이름 **비둘기 엔진** (Bidulgi Engine). CLI `bdg`, C++ 접두어 `B`, Python 모듈 `bidulgi` | 사용자 결정. 영문은 Pigeon과의 중복을 피해 Bidulgi |
| 2026-11-20 | 애니메이션 로직은 언리얼 **애니메이션 블루프린트** 구조, 비주얼 스크립팅은 언리얼 **블루프린트** 구조. 둘 다 텍스트 저장, 컴파일러(VM + AOT)로 속도 업그레이드 | [visual-scripting](../features/visual-scripting.md), [animation](../features/animation.md). 정식 이름은 Epic 용어와 겹치지 않게 별도로 (후보: 둥지 그래프) |
| 2026-11-20 | 블루프린트·애니메이션 블루프린트·코드는 리플렉션 레지스트리로만 만남. 노출 표시, 핸들, 생성되는 타입 프록시, 프레임 순서(애니메이션 평가는 워커, Notify는 게임 스레드로 일괄 전달), `[ThreadSafe]` 컴파일 검사 | [interop](../architecture/interop.md) |
| 2026-11-20 | 편의 기능 7개 채택: "왜 안 보이지?" 진단, 자연어 검색, 플레이 녹화 → 자동 테스트, 편집 중 플랫폼 예산, UI 자동 점검, 에셋 출처·라이선스 추적, 변경 범위 미리보기 | [unique-features](unique-features.md) |
| 2026-11-20 | 블루프린트는 언리얼 블루프린트의 문제 13가지를 고친 차세대 버전으로. 그래프 에디터(블루프린트·애니메이션·머티리얼 공통)는 ~~Dear ImGui + imgui-node-editor~~ (11-21 변경: 자체 UI 프레임워크), 스타일은 엔진 에디터와 통일 | [visual-scripting](../features/visual-scripting.md) |
| 2026-11-20 | 에디터 UI 디자인 시스템 확정: v0.2 시안의 평평한 어두운 스타일이 기준. 새 화면은 `design-system/`의 토큰과 부품만 사용 | [design-system](../../design-system/project/README.md) |
| 2026-11-20 | 씬·프리팹을 JSONL(헤더 줄 + 오브젝트당 한 줄)로, id 기준 git 병합 드라이버 | git 충돌 감소 → [data-formats](../features/data-formats.md) |
| 2026-11-20 | 프리팹 안쪽 id는 `@인스턴스/자식` 경로, 오버라이드는 `"/자식":{...}` | 같은 프리팹을 여러 번 놓아도 id가 겹치지 않음 |
| 2026-11-20 | 기본값 세트에 버전(`defaults`)을 두고, 기본값 변경 시 마이그레이션 필수 (CI 검사) | 기본값 생략 방식의 함정 해결 |
| 2026-11-20 | 에디터 밖 파일 수정: 파일 감시자, 외부 편집 Undo, 비교 화면, `bdg mv`·`bdg rm` | [agent-integration](../features/agent-integration.md) |
| 2026-11-20 | 게임 빌드는 바이너리로 쿠킹, JSON 파서는 게임 빌드에서 제외 | 로딩 속도 |
| 2026-11-20 | AGENTS.md 자동 생성·갱신 | 토큰 절약 |
| 2026-11-20 | **1.0 범위 축소**: 언어 C#·블루프린트(VM)·Luau, 그래픽 OpenGL·GLES·WebGL, 플랫폼 Windows·Android·Web. 블루프린트 AOT는 1.0 이후 + 차등 테스트 | [risk-response](risk-response.md) |
| 2026-11-20 | 창·입력 SDL3(한글 IME), 접근성 AccessKit, 디버거 DAP, 두 번째 그래픽 백엔드는 SDL_GPU | [risk-response](risk-response.md) |
| 2026-11-20 | 생태계 대응: 열린 표준, 유니티 에셋 임포터, git 패키지, 템플릿, AI 안내 | [risk-response](risk-response.md) |
| 2026-11-20 | 1.0 이후 **대형 게임 트랙** L1–L7 (렌더러 → 큰 월드 → 고급 그래픽 → 멀티플레이어 → 정밀 전투 → 애니메이션 → 운영). 렌더러와 월드 스트리밍이 먼저 | 오픈월드·대형 FPS까지 목표 → [large-game-track](large-game-track.md) |
| 2026-11-21 | 지원 언어 확정: **그래프, C++, C#, Python, Lua**. Rust 제외. C++은 1.0부터 모듈로 지원(핫 리로드는 이후). Lua는 Luau로 구현 | [scripting](../architecture/scripting.md) |
| 2026-11-21 | 비주얼 스크립팅 이름을 "블루프린트"에서 **그래프(Graph)**로 변경. 파일 `.graph`, 애니메이션 `.animgraph`, 노출 표시 `[Extendable]` | 언리얼 용어와 겹치지 않게 |
| 2026-11-21 | Python 용도 확장: AI 에이전트 스크립트, 에디터 자동화, 에셋 파이프라인, 테스트, 데이터 도구, 빌드. 게임 실행은 데스크톱 개발 빌드·전용 서버에서만 선택 | 모바일·웹 빌드 크기와 성능 |
| 2026-11-21 | 프로젝트 형식: 유니티식 폴더 프로젝트 + 언리얼식 프로젝트 파일(`<이름>.bdgproject`, 엔진 버전·언어·모듈·플러그인만) | [data-formats](../features/data-formats.md) |
| 2026-11-21 | 전역 조명은 단계별 하이브리드(가칭 Halo GI): Tier 0 베이크(1.0) → Tier 1 가시성 인식 프로브 → Tier 2 래디언스 캐스케이드 → Tier 3 HW 레이 트레이싱. 루멘의 약점을 측정 목표로, 기준 이미지 자동 비교로 품질 보증 | [global-illumination](../features/global-illumination.md) |
| 2026-11-21 | 품질 단계를 **감자·저사양·중사양·고사양·초고사양** 5단계로 (키 potato/low/medium/high/ultra). 중사양이 기본, 게임마다 최소·최대 단계. GI는 저사양 라이트맵 → 중사양 Tier 0 → 고사양 Tier 1 → 초고사양 Tier 2~3 | [rendering-and-platforms](../architecture/rendering-and-platforms.md) |
| 2026-11-21 | Dear ImGui를 쓰지 않음. 에디터와 게임 UI가 같은 자체 UI 프레임워크(Slate형, 유지형 위젯 트리)를 사용. 도킹은 M1 | [libraries-and-packaging](../architecture/libraries-and-packaging.md#1-1-ui-프레임워크-자체-구현) |
| 2026-11-21 | 애니메이션 블루프린트 → **애니메이션 그래프**(`.animgraph`). Graph의 상위호환: Graph 기능 전부 + 포즈 그래프·상태 머신·노티파이 | [animation](../features/animation.md) |
| 2026-11-21 | 입력은 **언리얼식 입력 액션 하나로** 통일. 매 프레임 확인도 액션 값으로 읽음. 키 직접 읽기는 게임 코드에 열지 않음 | [input](../features/input.md) |
| 2026-11-21 | Dear ImGui는 필요한 곳에 허용 (엔진 개발용 디버그 창, 실행 중 디버그 오버레이). 정식 에디터와 게임 UI는 자체 UI 프레임워크 | [libraries-and-packaging](../architecture/libraries-and-packaging.md#1-1-ui-프레임워크-자체-구현) |
| 2026-11-21 | 에셋 아이콘은 파일 종류별 아이콘 + 미리보기 썸네일(이미지·프리팹·머티리얼·모델·씬) | [editor-ui](../features/editor-ui.md#에셋-아이콘과-썸네일) |
| 2026-11-21 | 물리는 추상 계층(PHI) 뒤에서 백엔드를 바꿔 끼움. Jolt·Box2D(CPU, 1.0) + 자체 GPU 물리(복셀 파괴·파편·천·유체, L 트랙). 월드·레이어 단위로 섞어 씀 | [physics](../architecture/physics.md) |
| 2026-11-21 | 유니티·언리얼 혼합 원칙: 개념마다 정답 하나, 다른 엔진 방식은 대응표·프리셋으로만. 충돌 12개와 제안 정리 (게임 구조·생명주기 이름·좌표 단위는 M0 전 확정) | [unity-unreal-merge](unity-unreal-merge.md) |
| 2026-11-21 | 생명주기 이름은 **유니티식**으로 확정 (`Awake` `Start` `Update(dt)` `FixedUpdate(dt)` `LateUpdate(dt)` `OnEnable` `OnDisable` `OnDestroy`). C++ 포함 모든 언어 공통 | [unity-unreal-merge](unity-unreal-merge.md), [code-syntax](../features/code-syntax.md) |
| 2026-11-21 | 엔진 라이선스는 **MIT** (고도와 같음). 로열티 없음. 외부 라이브러리는 MIT 호환(MIT·BSD·zlib·Apache-2.0 등)만. 이름·로고는 별도 상표 정책. 기여는 DCO 서명. 콘솔 포트는 비공개 저장소 | [libraries-and-packaging](../architecture/libraries-and-packaging.md#라이선스-mit) |
| 2026-11-21 | 프로젝트는 유니티식 허브로 만들고 열며, 엔진 단독 실행 시 고도식 시작 화면. 엔진 버전은 `.bdgproject`에 고정. 언리얼식 템플릿(1인칭 슈팅, 3인칭, 2D 플랫포머 등)을 패키지로 제공 | [projects-and-templates](../features/projects-and-templates.md) |
| 2026-11-21 | 분할 저장: 작은 씬은 파일 하나, 큰 씬·오픈월드는 오브젝트별 또는 셀별 파일, 큰 숫자 데이터는 바이너리 옆 파일(텍스트에는 GUID만) | [data-formats](../features/data-formats.md#분할-저장-2026-11-21) |
| 2026-11-21 | 작업 순서는 `Docs/QUEUE.md` 대기열로 관리. 새 대화는 대기열 맨 위 항목부터 | [QUEUE](../QUEUE.md) |
| 2026-11-22 | 공식 로고 확정 (`branding/bidulgi-logo.png`, 비둘기 + 궤도 링). 색상 테마 **Obsidian Dark** (`#11151B` 배경, `#1C222B` 패널, `#AAB3C0` 보조 글자), **Bidulgi Amber `#F59E32`를 브랜드 색으로 고정**(로고 부리 색), Engine Blue `#3996B9`는 오브젝트·컴포넌트·링크. 언어 색 토큰 `lang-*` 추가 | [tokens.json](../../design-system/project/tokens.json) |
| 2026-11-22 | 코드·로그용 글꼴은 Consolas(시스템 글꼴, 배포 안 함; 없으면 Courier). 배포용은 JetBrains Mono(OFL)로 교체 예정 | [QUEUE](../QUEUE.md) |
| 2026-11-22 | 머티리얼·텍스처는 언리얼식: 마스터 머티리얼(노드 그래프) → 머티리얼 인스턴스(파라미터만, 컴파일 없음), 텍스처 용도별 프리셋·텍스처 그룹. 셰이더 캐시·순열 제한·텍스트 저장으로 언리얼의 불편함 해결 | [materials-and-textures](../features/materials-and-textures.md) |
| 2026-11-22 | 엔진 이름 **비둘기 엔진 (Bidulgi Engine)으로 최종 확정**. U로 시작하는 새 이름 검토는 종료. CLI `bdg`, C++ 접두어 `B`, Python 모듈 `bidulgi` 유지 | 사용자 결정 |

## 미결 사항

- [ ] **상표·도메인 확인** — 비둘기 엔진 / Bidulgi Engine 공개 전 KIPRIS, USPTO, 도메인, GitHub 이름 확인
- [x] **프로젝트 폴더 이름** — `C:\GameProjects\BidulgiEngine`으로 결정 (2026-11-20). 이전 폴더 `C:\GameProjects\Tessera`는 삭제 대상
- [ ] **시안 v0.2 범위** — 다섯 항목 전부인지, 1~4까지인지 → [mockup-v0.2](mockup-v0.2.md)
- [ ] **시안 파일 구성** — HTML 하나 유지 vs 화면별 스크립트 파일 분리 (분리 추천)
- [x] **라이선스** — **MIT** (2026-11-21 확정, 고도와 같음)
- [ ] **콘솔 지원 방식** — 제조사 계약 후 비공개 포트로 붙이는 방식 검토
