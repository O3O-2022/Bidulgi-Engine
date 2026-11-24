#pragma once
// .NET 8+ hosting (queue 4-1): loads the CoreCLR runtime into the engine process with hostfxr (the official native
// hosting API, MIT), then Bidulgi.Scripting.dll, and gets its [UnmanagedCallersOnly] entry points as C function
// pointers. No SDK headers are needed: hostfxr is found in DOTNET_ROOT or the default install folder and its few
// functions are declared by hand.
//
// C# talks to the engine through one native function (JSON request -> JSON answer, ScriptApi.h). The runtime can be
// loaded once per process and is never unloaded; scripts reload by unloading their own assembly context (4-4).
// Object references are scene ids for now; runtime handles replace them when Behaviours get their lifecycle (4-2).
#include <string>

namespace bdg {
class Scene;
class CommandBus;

class DotNetHost {
public:
  static DotNetHost& Instance();

  // hostfxr.dll path: DOTNET_ROOT, then %ProgramFiles%\dotnet; the highest host/fxr/<version>. "" = none.
  static std::string FindHostFxr(std::string* error = nullptr);
  static std::string DefaultManagedDir(); // <folder of the running exe>/Managed

  // Loads the runtime and Bidulgi.Scripting.dll from managedDir ("" = DefaultManagedDir). Safe to call again.
  bool Start(const std::string& managedDir = {}, std::string* error = nullptr);
  bool Running() const { return selfTest_ != nullptr; }
  const std::string& RuntimeVersion() const { return version_; } // from the runtimeconfig the runtime resolved

  // Requests from C# go to this scene and command bus (nullptr: "no scene" errors). Not thread-safe: scripts run
  // on the main thread.
  void Attach(Scene* scene, CommandBus* bus);

  // The 4-1 check: C# reads, calls and writes a Health component on `target`. 1 = passed, 0 = wrong values,
  // -1 = exception in C# (logged), -2 = not running.
  int SelfTest(const std::string& target);

  // Engine side of the request channel (C# calls it through a function pointer).
  static int Request(const char* json, char* out, int capacity);

private:
  DotNetHost() = default;
  Scene* scene_ = nullptr;
  CommandBus* bus_ = nullptr;
  int (*selfTest_)(const char*) = nullptr;
  std::string version_;
  std::string lastRequest_, lastAnswer_; // an answer too long for the buffer waits for the same request again
};
} // namespace bdg
