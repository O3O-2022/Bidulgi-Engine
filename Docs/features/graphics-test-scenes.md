# 그래픽 테스트 씬 (렌더링 기준점)

작성 2026-11-22. 테스트 씬은 화려하게 꾸미는 것이 아니라, **렌더링 파이프라인의 각 기능이 의도대로 동작하는지 한눈에 확인하는 기준점**입니다.
같은 장면·같은 카메라로 매번 찍어 이전 결과와 픽셀 단위로 비교하는 것이 목적입니다 ([Halo GI 품질 보증](global-illumination.md#품질-보증-버그를-줄이는-방법)과 같은 방식).

## 1. 검증된 표준 에셋 사용

직접 만든 모델은 토폴로지·UV 오류와 엔진 버그를 구분하기 어렵습니다. 처음에는 검증된 공개 에셋만 씁니다.

| 에셋 | 검증할 것 | 라이선스 |
|---|---|---|
| **Khronos glTF Sample Assets**: MetalRoughSpheres, DamagedHelmet, FlightHelmet, NormalTangentTest, AlphaBlendModeTest, TextureCoordinateTest | PBR 정확도, 노멀·탄젠트, 알파 모드, UV 방향 | 모델마다 다름 (CC0, CC-BY 4.0 등) |
| ~~Sponza~~ (Khronos판은 CRYENGINE 라이선스라 쓰지 않음, 2026-11-24) → 라이선스가 분명한 실내 장면으로 대체(3-8b) | 실내외 조명, 그림자, AO, GI | 대체 장면 확인 필요 |
| **Utah Teapot** | 곡면 노멀, 스페큘러, 스무딩 | 퍼블릭 도메인 |
| **Stanford Dragon / Bunny** | 고밀도 메시, 노멀, LOD | Stanford 3D Scanning Repository 이용 조건 (연구·테스트용) |

- **저장소에 넣지 않습니다.** 라이선스가 제각각이라 `bdg testassets fetch`로 받아 `.cache/testassets/`에 두고, 출처·라이선스를 `testassets.lock.json`에 기록합니다 (해시로 같은 파일인지 확인).
- PBR 결과는 같은 에셋을 **Blender Cycles / glTF Sample Viewer**로 렌더링한 기준 이미지와 대조합니다.

## 2. 목적별 테스트 구역 (한 장면, 격자로 나눔)

`Tests/Scenes/RenderReference.scene` 하나에 구역을 나눠 배치합니다. 구역마다 카메라 북마크가 있습니다.

| 구역 | 배치 | 검증 |
|---|---|---|
| **① 재질 매트릭스** | 구 7×7 (X: Roughness 0→1, Y: Metallic 0→1), 흰색·금·구리 3세트 + MetalRoughSpheres | BRDF 정확도, IBL 반사, 에너지 보존 |
| **② 조명·그림자** | 얇은 기둥, 공중에 뜬 구, 계단, 방향광·점광·스폿광 혼합 | 그림자 여드름(acne), 피터 패닝, CSM 경계, 점광 큐브 그림자 |
| **③ 투명·알파** | 반투명 유리판 여러 겹(색 다름), 철망(알파 컷아웃), 파티클 | 정렬 오류, 알파 테스트·블렌드, MSAA |
| **④ 지오메트리·컬링** | 작은 큐브 수만 개(인스턴싱), 화면 밖으로 걸친 거대 메시, 아주 얇은 삼각형, 겹친 평면 | 프러스텀·오클루전 컬링, 인스턴싱 성능, Z-fighting, LOD 전환 |
| **⑤ 포스트 프로세스** | 발광 강도 10 이상 Emissive, 어두운 동굴 입구 | Bloom, 톤매핑(ACES), 자동 노출 적응 |
| **⑥ 좌표·색 기준** | 축 화살표(X 빨강·Y 초록·Z 파랑), 글자 텍스처 판, 18% 회색 카드, 컬러 체커 | 좌표계·손잡이 방향, 와인딩(CW/CCW), sRGB↔Linear 감마, UV 뒤집힘 |
| **⑦ 품질 단계 비교** | 위 구역을 감자~초고사양으로 분할 화면 | 단계별로 빠지는 기능이 의도대로 대체되는지 (예: 그림자 → 블롭) |

## 3. 디버그 기능 (씬만큼 중요)

| 기능 | 내용 | 텍스트 출력 (AI용) |
|---|---|---|
| **렌더 패스 뷰어** | 단축키로 Albedo, World Normal, Roughness·Metallic, Depth, Shadow Map, AO, Bloom 버퍼를 전체 화면 또는 분할로 표시 | `bdg render passes` (패스 목록·해상도·시간) |
| **오버레이** | 와이어프레임, 바운딩 박스, LOD 단계 색, 컬링된 오브젝트 표시, 오버드로우 히트맵 | `bdg render stats` |
| **카메라 북마크** | 숫자 키 1~9로 각 구역의 고정 카메라로 즉시 이동 (씬 파일에 저장) | — |
| **이미지 비교** | 북마크별 스크린샷을 기준 이미지와 비교 (FLIP 오차), 차이 영역을 강조한 이미지 생성 | `bdg render compare` → 구역별 오차 표 |
| **GPU 타이머** | 패스별 GPU 시간, 드로우콜, 삼각형, VRAM 사용량 | 성능 회귀 CI의 입력 |

```
$ bdg render compare Tests/Scenes/RenderReference.scene --tier medium
bookmark  zone                 FLIP    base     result
1         materials            0.011   ✓ 0.03
2         lights-shadows       0.024   ✓ 0.03
3         transparency         0.051   ✗ 0.03   diff: .cache/compare/3_diff.png (유리판 정렬)
...
```

## 4. 구축 순서

| 단계 | 내용 | 대기열 |
|---|---|---|
| **1단계 기본 파이프라인** | 단색 평면 + 방향광 1개 + MetalRoughSpheres + ⑥ 좌표·색 기준판. 좌표계, 와인딩, 감마부터 확정 | M3 3-0 |
| **2단계 복합 씬** | `Tests/RenderReference` 구역 ①~⑦ + 북마크·렌더 패스 뷰어(3-8 완료), 실내 장면으로 텍스처 로딩·다중 라이트·그림자 품질 | M3 3-8 / 3-8b |
| **3단계 스트레스** | 같은 메시 1만 개 인스턴싱, 4K 텍스처 다수 → 드로우콜·VRAM 측정 (3-9 완료: `render-stress`, `BDG_RENDER_COMPARE`) | M3 3-9 |
| **회귀 CI** | 북마크 스크린샷 자동 비교를 매 커밋마다 (기준 GPU 1대 + 소프트웨어 렌더러로 보조) | 1.0 |

## 참고

- 이전에 만든 HTML 시안 `prototype/quality-tiers.html`의 구역(유리판, 발광 수정, 어두운 동굴)은 이 문서의 ③⑤⑦ 구역으로 이어집니다.
- 테스트 씬 파일과 북마크는 텍스트(JSONL)라서, AI가 새 테스트 구역을 추가하거나 카메라 각도를 고칠 수 있습니다.
