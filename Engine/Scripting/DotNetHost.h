#pragma once
// .NET 8+ hosting (queue 4-1): loads the CoreCLR runtime into the engine process with hostfxr (the official native
// hosting API, MIT), then Bidulgi.Scripting.dll, and gets its [UnmanagedCallersOnly] entry points as C function
// pointers. No SDK headers are needed: hostfxr is found in DOTNET_ROOT or the default install folder and its few
// functions are declared by hand.
//
// C# talks to the engine through one native function (JSON request -> JSON answer, ScriptApi.h). The runtime can be
// loaded once per process and is never unloaded; scripts reload by unloading their own assembly context (4-4).
// Object references are scene ids; a fast handle path for per-frame bulk access comes later (decisions 4-1).
#include "Core/Reflection.h"
#include <deque>
#include <array>
#include <set>
#include <string>

struct yyjson_val; // yyjson.h

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

  // Script assemblies (4-2): loads a game's .dll; every Behaviour class in it becomes a component type of the same
  // name (tags Component, Script, CSharp; one field "enabled"), so scenes attach it by class name. Returns the
  // number of classes, or -1. Load before opening scenes that use them.
  int LoadScripts(const std::string& dllPath, std::string* error = nullptr);
  bool IsBehaviourType(const TypeInfo* type) const { return type && behaviourTypes_.count(type) != 0; }
  size_t BehaviourTypeCount() const { return behaviourTypes_.size(); }

  // Lifecycle (driven by BehaviourRunner): create = new instance + Awake (+ OnEnable when enabled); 0 = failed.
  // fieldsJson: the component's values ({"speed":5,...}), set on the C# fields before Awake (Unity order).
  int CreateBehaviour(const std::string& objectId, const std::string& type, bool enabled, const std::string& fieldsJson = "{}");
  // [Edit] fields (4-3): engine -> C# (an Inspector edit while playing) and C# -> engine (what the script changed).
  void SetBehaviourFields(int handle, const std::string& json);
  std::string GetBehaviourFields(int handle); // "" when the instance is gone
  static std::string FieldsJson(const void* data, const TypeInfo& type); // {"enabled":true,"speed":5}
  void SetBehaviourEnabled(int handle, bool enabled); // OnEnable / OnDisable on a change
  void DestroyBehaviour(int handle);                  // OnDisable (if enabled) + OnDestroy
  enum class Phase { Update = 0, FixedUpdate = 1, LateUpdate = 2 }; // Update runs pending Starts first
  void TickBehaviours(Phase phase, double dt, double time, int frame);

  // Engine side of the request channel (C# calls it through a function pointer).
  static int Request(const char* json, char* out, int capacity);

private:
  DotNetHost() = default;
  Scene* scene_ = nullptr;
  CommandBus* bus_ = nullptr;
  int (*selfTest_)(const char*) = nullptr;
  int (*loadAssembly_)(const char*) = nullptr;
  int (*create_)(const char*, const char*, int, const char*) = nullptr;
  void (*setFields_)(int, const char*) = nullptr;
  int (*getFields_)(int, char*, int) = nullptr;
  void (*setEnabled_)(int, int) = nullptr;
  void (*destroy_)(int) = nullptr;
  void (*tick_)(int, double, double, int) = nullptr;
  std::string RegisterBehaviourType(const std::string& name, yyjson_val* fields); // "" = ok, else why not
  std::deque<EnumInfo> enums_;                                // C# enum fields (stable addresses)
  std::deque<TypeInfo> types_;                                // stable addresses for the Registry
  std::set<const TypeInfo*> behaviourTypes_;
  std::string version_;
  std::string lastRequest_, lastAnswer_; // an answer too long for the buffer waits for the same request again
};
} // namespace bdg
