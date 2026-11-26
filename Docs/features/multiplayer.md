# 멀티플레이 (7라운드, 2026-11-26)

목표: **언리얼만큼 쉬운 멀티플레이 게임** — 서버가 정답을 갖고(서버 권한), 클라이언트는 보간·예측으로 부드럽게 보여 줍니다. 코드: `Engine/Net/`(전송·직렬화·복제·Play 옵션), C# `Scripting/Bidulgi.Scripting/Net.cs`.

## 빠르게

1. 장면에 **`GameMode`**(플레이어 프리팹 또는 복사할 오브젝트 이름)와 **`PlayerStart`** 몇 개.
2. 모두에게 보여야 할 오브젝트에 **`NetworkObject`**, 움직이는 것에는 **`NetworkTransform`**.
3. C#:

```csharp
public class Player : Behaviour {
  [Replicated] int score;                        // 서버 -> 모든 클라이언트
  void OnRep_score() => UI.Text("score").text = $"{score}";   // 클라이언트에서 새 값이 오면

  void Update() {
    if (IsOwner) transform.Position += Input.Value<Vector2>("Move").ToXZ() * 5 * Time.deltaTime; // 내 것은 바로 움직임(예측)
    if (IsOwner && Input.Pressed("Fire")) Rpc(nameof(Fire), aim);
  }
  [Server] void Fire(Vector3 aim) { score++; Rpc(nameof(Boom)); }   // 서버에서만, 주인만 부를 수 있음
  [Multicast] void Boom() => Vfx.Play("Spark", transform.Position); // 모두에게
  [Client] void YouWereHit(int damage) { }                          // 그 오브젝트 주인에게만
}
```

4. 에디터 **Play 옆 ▾**: Net Mode(Standalone / Listen Server / Client), Players(1~4), Network(Off / LAN / Internet / Bad). Play하면 나머지 플레이어가 게임 창(프로세스)으로 열리고("Client 1" …), Stop이 모두 닫습니다.

## 넷 모드

| 모드 | 어떻게 |
|---|---|
| Standalone | 네트워크 없음(Players가 2 이상이면 에디터가 Listen Server로) |
| Listen Server | 이 게임이 서버이면서 플레이(호스트 = 플레이어 0) |
| Dedicated Server | 창 없는 서버: 빌드한 게임 `"My Game.exe" --server --port 7777` (60틱, Ctrl+C로 끝) |
| Client | 서버에 접속: `--connect 192.168.0.5:7777`, C# `Net.Join("192.168.0.5")` |

플레이어 exe 옵션: `--server`, `--listen`, `--connect host[:port]`, `--port`, `--name`, `--title`(창 제목), `--log`(로그 파일), `--scene`(시작 장면 대신), `--sim-latency` / `--sim-jitter`(ms), `--sim-loss`(%), `--quit-after`(초).

## 복제 (어떻게 맞추나)

- **접속**: 클라이언트가 인사(프로토콜 버전, 이름, **콘텐츠 목록**: 팩·모드 이름 + 해시) → 서버가 다르면 이유와 함께 거절, 같으면 플레이어 번호와 **지금 세계 전체**(장면 텍스트)를 보냄 — 늦게 들어와도 지금까지 생긴 것을 모두 받음. GameMode가 그 플레이어의 오브젝트를 PlayerStart에 만들고 주인으로 정함.
- **생성·삭제**(신뢰 채널): 서버에서 NetworkObject가 생기면(`Spawn`, 프리팹) 그 오브젝트의 스냅샷을 보냄 — 에셋·프리팹은 **경로·GUID**로(모드가 에셋을 더해도 밀리는 번호를 쓰지 않음). 지워지면 지움.
- **복제 필드**(신뢰): `BPROPERTY(Replicated)` / C# `[Replicated]` 값이 바뀌면. 클라이언트에서 `OnRep_이름()`(또는 `OnRep_이름(이전 값)`).
- **스냅샷**(비신뢰, 초당 30): NetworkTransform 위치·회전·크기, 클라이언트가 받았다고 알린 스냅샷 대비 **변경분**(1 mm, 회전 32비트). `relevancy`가 있으면 그 플레이어 근처 것만.
- **보간**: 다른 사람 것은 0.1 초 과거를 두 스냅샷 사이로 그림(늦으면 0.25 초까지 이어 가고 멈춤).
- **예측과 재조정**: 내 것(주인 + `ownerPredicted`)은 스크립트가 바로 움직이고, 그 이동을 번호와 함께 서버로(초당 `sendRate`). 서버는 `maxSpeed` 안에서만 받아들이고(순간 이동 치트는 잘림), 스냅샷에 어디까지 반영했는지 알려 줌 → 클라이언트는 서버 위치 + 아직 반영 안 된 이동으로 맞춤.
- **RPC**: `[Server]`(주인만, 서버에서 확인), `[Client]`(서버 → 주인), `[Multicast]`(서버 → 모두 + 서버), `Reliable = false`면 비신뢰. Standalone에서는 그냥 호출.
- **떠남**: 주인이 나가면 그의 오브젝트(`destroyWithOwner`)가 서버·모든 클라이언트에서 사라짐. 서버 이벤트 `OnPlayerJoined(int)` / `OnPlayerLeft(int)`.
- **전송**: ENet(UDP) 신뢰·순서 / 비신뢰 채널, 시간 초과, 흉내(지연·흔들림·손실 — 손실은 ENet 아래에서 버려서 신뢰 채널이 실제로 재전송), 통계(핑·손실·대역폭: C# `Net.Stats`).

## 언리얼 이름 대응

| 언리얼 | 비둘기 엔진 |
|---|---|
| `bReplicates`, Actor | `NetworkObject` |
| `ReplicatedMovement`, CharacterMovement 예측 | `NetworkTransform` (보간 + 주인 예측·재조정) |
| `UPROPERTY(ReplicatedUsing=OnRep_X)` | `[Replicated]` + `OnRep_X()` |
| `UFUNCTION(Server)`, `(Client)`, `(NetMulticast)`, `Reliable` | `[Server]`, `[Client]`, `[Multicast]`, `Reliable =` |
| `HasAuthority()`, `IsLocallyControlled()` | `HasAuthority`, `IsOwner` |
| GameMode `PostLogin` / `Logout`, `APlayerStart` | `OnPlayerJoined` / `OnPlayerLeft`, `PlayerStart` |
| PIE Net Mode / Number of Players / Network Emulation | Play ▾ Net Mode / Players / Network |

## 검사

- 단위 테스트(`Tests/NetTests.cpp`, `NetWorldTests.cpp`): 루프백 전송(손실 속 순서, 지연, 끊김, 시간 초과), 비트·양자화·변경분·보간·예측, 한 프로세스 안 서버 + 클라이언트 2(손실 10 %: 생성, 동기화, 복제 필드, 예측, 치트 잘림, 늦은 참가, relevancy, 생성·삭제, RPC 세 종류와 거절, 다른 콘텐츠 거절, 떠난 플레이어 정리).
- `BDG_NET_TEST`(에디터 + 플레이어 프로세스 2, CI): `net test: spawn=1 sync=1 onrep=1 rpc=1 late=1 left=1`.

## 남은 것

로비 UI·통계 표시·아레나 템플릿·빌드한 전용 서버 CI(M-7), LAN 찾기, 클라이언트 물리(지금은 서버 값이 덮음), 관심 영역 그리드, 대역폭 우선순위, 여러 서버 이전, 음성.
