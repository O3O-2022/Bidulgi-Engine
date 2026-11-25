# C# 스크립트 디버깅 (4-5)

작성 2026-11-25. 에디터(`BidulgiEditor.exe`)는 .NET 런타임을 자기 프로세스 안에 띄웁니다(hostfxr, [4-1](../QUEUE.md)). 그래서 C# 스크립트를 디버깅하려면 **실행 중인 에디터 프로세스에 .NET 디버거를 붙입니다**(attach). 별도 게임 프로세스를 실행(launch)하는 방식이 아닙니다. 디버거와는 DAP(Debug Adapter Protocol, VS Code가 쓰는 프로토콜)로 이야기합니다.

## 확인한 것 / 확인하지 못한 것

| 방법 | 상태 (2026-11-25, Windows 11, .NET 9 런타임) |
|---|---|
| **netcoredbg**(Samsung, MIT) + DAP — `Scripting/Tools/dap_attach.py` | **확인함**: 에디터에 붙기 → 중단점 확인(verified) → `Spinner.Update`에서 멈춤 → 호출 스택 → `this.speed = 90` 읽기 → 계속 → 떼기, 에디터는 그대로 계속 돌고 자동 검사도 통과 |
| VS Code + C# 확장(vsdbg) | 확인 못 함 — 이 PC에 확장이 없음. 같은 DAP·같은 attach 방식이라 아래 설정으로 됩니다(일반적인 .NET attach 설정) |
| VS Code + netcoredbg | 확인 못 함(VS Code에서의 연결은). 디버거 자체는 위에서 확인 |
| JetBrains Rider | 확인 못 함 — 설치되어 있지 않음 |
| Visual Studio 2022 | 확인 못 함(GUI 조작). 관리 코드 attach는 VS의 기본 기능 |

## 꼭 알아야 할 것

1. **스크립트는 Debug로 빌드**합니다. Release(최적화) 빌드에서도 중단점이 "확인됨"으로 표시되지만 **실제로는 멈추지 않았습니다**(확인함). IDE에서 기본 구성(Debug)으로 빌드해 `.cache/Scripts`에 두거나, `BDG_SCRIPT_ASSEMBLIES`에 Debug 빌드를 가리키세요. CMake가 만드는 엔진 샘플(`build/Release/Managed/Bidulgi.Samples.dll`)은 Release이므로, 샘플을 디버깅할 때는:
   ```powershell
   dotnet build Scripting/Bidulgi.Samples/Bidulgi.Samples.csproj -c Debug -o .cache/samples-debug -p:BdgManaged=$PWD/build/Release/Managed
   $env:BDG_SCRIPT_ASSEMBLIES = "$PWD/.cache/samples-debug/Bidulgi.Samples.dll"
   ```
2. **.pdb가 .dll 옆에** 있어야 합니다. 엔진은 스크립트를 메모리로 읽어 로드하지만(빌드가 덮어쓸 수 있게, 4-4) .pdb도 같이 메모리로 넘기므로 디버거가 기호를 찾습니다("Symbols loaded" 확인함).
3. **Awake·Start를 디버깅하려면** 디버거가 붙을 때까지 기다리게 합니다:
   ```powershell
   $env:BDG_CSHARP_DEBUG_WAIT = "60"   # 초. 스크립팅 시작 때 디버거를 기다림 (붙으면 바로 진행)
   ```
   로그(`Bidulgi.log`)에 `C#: waiting up to 60 s for a .NET debugger to attach (process id 1234)`가 찍힙니다 — 이 **프로세스 id**로 붙이면 됩니다. 기다리지 않을 때는 작업 관리자나 `Get-Process BidulgiEditor`로 id를 찾습니다.
4. netcoredbg로 붙는 데 **20~30초** 걸렸습니다(모듈·기호를 읽는 동안). 그동안 에디터는 계속 돕니다.
5. 중단점에 멈추면 **에디터 전체가 멈춥니다**(스크립트가 게임 스레드에서 돌기 때문). 계속(F5)하면 이어집니다.

## netcoredbg (DAP, 확인한 방법)

1. https://github.com/Samsung/netcoredbg/releases 에서 `netcoredbg-win64.zip`을 받아 원하는 폴더에 풉니다(예: `C:\tools\netcoredbg`).
2. 에디터를 실행합니다(필요하면 `BDG_CSHARP_DEBUG_WAIT`).
3. 붙여서 중단점을 확인합니다(AI 세션처럼 IDE가 없을 때도 이 스크립트로 됩니다):
   ```powershell
   python Scripting/Tools/dap_attach.py --debugger C:\tools\netcoredbg\netcoredbg.exe --pid 1234 --source Scripting/Bidulgi.Samples/Samples.cs --line 25 --variable speed
   ```
   출력 예: `frame: Bidulgi.Samples.Spinner.Update() ...Samples.cs 25`, `variable: this.speed = 90`, `dap check: hit line 25`(종료 코드 0). 스크립트는 떼고 나가며 에디터는 계속 돕니다.

## VS Code

C# 확장(Microsoft `ms-dotnettools.csharp`, 디버거 vsdbg)을 설치하고 `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "비둘기 에디터에 C# 붙이기",
      "type": "coreclr",
      "request": "attach",
      "processId": "${command:pickProcess}",
      "justMyCode": false
    }
  ]
}
```

F5 → 목록에서 `BidulgiEditor.exe`를 고릅니다. 위 표처럼 이 설정은 이 PC에서 직접 확인하지 못했습니다. vsdbg의 라이선스는 Microsoft 제품(VS Code·Visual Studio) 안에서만 쓰도록 되어 있어, VSCodium 같은 곳에서는 netcoredbg를 씁니다.

## Rider

**Run → Attach to Process…** → `BidulgiEditor.exe`를 고르고, 디버거 종류는 **.NET Core**(CoreCLR)를 고릅니다. 스크립트 프로젝트(`.csproj`)를 Rider에서 열어 두면 소스가 바로 연결됩니다. (확인 못 함)

## Visual Studio 2022

**디버그 → 프로세스에 연결** → 코드 형식 **Managed (.NET Core, .NET 5+)** → `BidulgiEditor.exe`. **Native**도 함께 고르면 C++ 엔진 코드와 섞어서(mixed mode) 디버깅할 수 있습니다. (확인 못 함)

## 남은 것

- 핫 리로드(4-4) 뒤 새 어셈블리에 중단점이 다시 붙는지 확인(새 모듈이 로드되면 DAP 디버거가 다시 해석하는 것이 정상).
- 에디터 메뉴에서 "디버거 기다리기" 켜기, Lua·Graph 디버깅도 같은 DAP로([scripting](../architecture/scripting.md#주의할-점)).
