# Indoor composite test scene — licence

**CC0 1.0 Universal** (public domain dedication): https://creativecommons.org/publicdomain/zero/1.0/

| 항목 | 출처 | 라이선스 |
|---|---|---|
| 텍스처 11장 (`Assets/Textures/*.png`: 벽돌·타일·회벽·나무 색/노멀/ORM, 그림, 철제 난간) | 이 저장소의 `tools/make_indoor_scene.py`가 수식(값 잡음·사인파)으로 생성. 외부 사진·스캔·에셋을 쓰지 않음 | CC0 1.0 |
| 머티리얼 10개 (`Assets/Materials/*.material`) | 같은 스크립트 | CC0 1.0 |
| 장면 (`Assets/Scenes/Cloister.scene`: 회랑·별실·조명·북마크) | 같은 스크립트. 메시는 엔진 내장 도형(Blockout 상자·원기둥, Plane, Sphere) | CC0 1.0 |

- 외부에서 받은 파일이 없으므로 출처·라이선스 확인이 필요한 항목이 없습니다. Sponza(Khronos 사본은 CRYENGINE 라이선스)는 쓰지 않습니다 — [decisions](../../Docs/planning/decisions.md) 3-8 참고.
- 파일을 손으로 고치지 말고 생성기를 고친 뒤 `python tools/make_indoor_scene.py`로 다시 만드세요. CI가 다시 만들어 저장소와 같은지 비교합니다.
- 엔진이 열 때 만드는 `.meta`·`.cache`는 git에서 제외됩니다.
