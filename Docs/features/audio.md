# 오디오 (6-3)

작성 2026-11-25. [miniaudio](https://miniaud.io) 0.11.26(퍼블릭 도메인 / MIT-0, `ThirdParty/miniaudio`)로 소리를 냅니다. 엔진 API에는 miniaudio 형식이 드러나지 않습니다(`Engine/Audio/Audio.h`).

## 컴포넌트 (유니티식)

| 컴포넌트 | 필드 |
|---|---|
| **AudioSource** | `clip`(Assets 기준 경로 또는 파일 이름, .wav·.mp3·.flac), `group`(믹서 그룹, 기본 Effects), `volume`, `pitch`, `loop`, `playOnStart`(기본 켬), **3D**: `spatial`(기본 켬), `minDistance`(이 거리 안은 최대 음량), `maxDistance`(이 거리 밖은 더 작아지지 않음), `rolloff`(Inverse·Linear·Exponential), `rolloffFactor`. 함수 `Play()`·`Stop()`(Callable: 스크립트·Graph에서) |
| **AudioListener** | `volume`. 보통 카메라에 붙임. 없으면 **첫 Camera**가 듣는 사람 |

플레이 모드(에디터의 Play, 빌드한 게임)에서:
- `playOnStart` 소스는 시작하자마자, 플레이 중에 생긴 오브젝트(Spawn)의 소스도 생기자마자 재생.
- 3D 소스는 매 프레임 오브젝트 위치를 따라가고, 듣는 사람의 위치·방향으로 **거리 감쇠와 좌우 패닝**(도플러는 끔).
- Inspector에서 바꾼 `volume`·`pitch`는 재생 중인 소리에 바로 반영. 오브젝트를 지우면 그 소리가 멈춤.
- **일시정지**하면 소리도 멈췄다가 이어지고, **정지(Stop)**하면 모든 소리가 끝남.

## 믹서 그룹

`Master` 아래 기본 그룹 `Music`·`Effects`·`Voice`·`UI`·`Ambient`. 더 필요하면 `ProjectSettings/AudioMixer.json`:

```json
{"groups":[{"name":"Footsteps","parent":"Effects","volume":0.7},{"name":"Music","volume":0.8}]}
```

그룹 음량은 부모 그룹 음량과 곱해집니다(Footsteps = Master × Effects × Footsteps). 모르는 그룹 이름의 소리는 Effects로.

## 프로젝트 설정 (`audio.*`)

| 설정 | 동작 |
|---|---|
| `sampleRate` | 출력 샘플 레이트(기본 48000) |
| `masterVolume` | 전체 음량 |
| `playInBackground` | 끄면(기본) **빌드한 게임** 창이 포커스를 잃었을 때 소리를 끔. 에디터는 항상 들림 |

## C# (쉬운 API)

```csharp
Audio.Play("Shot.wav");                                           // 2D
Audio.Play("Boom.wav", at: transform.Position, volume: 0.8f);     // 3D: 그 자리에서 (minDistance·maxDistance도 줄 수 있음)
Sound music = Audio.Play("Theme.mp3", group: "Music", loop: true);
music.Stop();  bool on = music.Playing;
Audio.SetGroupVolume("Music", 0.3f);  float v = Audio.GroupVolume("Music");
Audio.StopAll();
Get<AudioSource>().Play();                                        // 컴포넌트도 그대로 (생성된 바인딩)
```

- 없는 소리 파일은 예외가 아니라 경고 + `Sound.None`(`Playing == false`) — [easy-api](easy-api.md) 원칙 3.
- 스크립트 요청: `audio.play`·`audio.stop`·`audio.playing`·`audio.stopall`·`audio.volume`(Undo·로그 없음, 게임 상태).

## 장치가 없을 때 (CI, 자동 검사)

출력 장치가 없거나 `BDG_AUDIO_NULL=1`이면 장치 없이 돌고, 매 프레임 게임 시간만큼 **섞어서 소리 크기(RMS·피크)를 잽니다**. 자동 검사는 이것으로 "들립니다": 스피커 없이도 소리가 나는지, 멀어지면 작아지는지 확인.

## 빌드

`bdg build`는 장면의 `AudioSource.clip`이 가리키는 파일을 다른 에셋처럼 묶고(참조를 따라감), 사전 검사(7-4b)는 없는 소리 파일을 `[오류] 에셋 · … AudioSource.clip — 소리 파일 '…'가 프로젝트에 없음`으로 알려 줍니다.

## 자동 검사

- 단위 테스트(`Tests/AudioTests.cpp`): 사인파 WAV를 만들어 장치 없이 재생 — 2D 음량, 같은 소리 1 m vs 30 m(30 m가 1/5 미만), 그룹 음량 0이면 무음, 그룹 추가, 일시정지·재개, 짧은 소리는 끝나면 사라짐, 없는 파일 오류.
- `BDG_AUDIO_TEST=1` + `BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll`: 카메라(AudioListener)와 2 m 앞 3D 소스 → 플레이하면 바로 들림, 40 m로 옮기면 작아짐, 일시정지 무음·재개, C# `AudioProbe`(Audio.Play 2D·3D, 그룹 음량, 없는 파일, AudioSource.Play), 정지하면 0개 → `audio test: plays=1 distance=1 pause=1 csharp=1 stopped=1`.

## 남은 것

- 사운드 큐(언리얼식 노드: 랜덤·믹서·모듈레이터·감쇠), 덕킹·스냅샷, 리버브 존, .ogg(stb_vorbis), 스트리밍 음악의 크로스페이드.
- 에디터 안에서 미리 듣기(Project 패널·Inspector), 씬 뷰에 소리 범위(min/max) 원 표시.
- 상수 자동 생성(`Sounds.Shot`) — 4-4c의 남은 항목과 함께.
