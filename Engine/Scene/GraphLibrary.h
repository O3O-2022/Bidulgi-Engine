#pragma once
// Graph node library from reflection (queue E-8), like Unreal's BlueprintCallable / BlueprintPure /
// BlueprintReadWrite: what a component written in C++ (and later C# and Lua) exposes becomes Graph nodes, so heavy
// work lives in code and the Graph wires it together.
//
//   C++   BFUNCTION(Callable) int Damage(int amount);        -> "Health.Damage" node: exec in/out, target, amount -> int
//         BFUNCTION(Pure) float Ratio() const;               -> "Health.Ratio": no exec pins (a value), no undo step
//         BPROPERTY(Edit) int current;                       -> "Health.Get current" and "Health.Set current"
//         BPROPERTY(ReadOnly) ...                            -> Get only
//   C#    [Callable] / [Pure] on Behaviour methods, [Edit] fields      (registered the same way: M4 4-3)
//   Lua   bdg.export{ Damage = {args = {"int"}, ret = "int"} }       (M5 5-3)
// Every language registers into the same reflection Registry, so the library below lists them all.
//
// Running a node: Call and Set go through the command bus (one undoable step, logged like any edit); Pure and Get
// only read. The Graph VM (M5 5-1) and the graph editor use exactly these.
#include "Core/Command.h"
#include <string>
#include <vector>

namespace bdg {

struct GraphPin {
  std::string name;
  std::string type;  // "exec", "object" (the target), or a reflection type name: int, float, vec3, Health, LightType...
};

struct GraphNodeDesc {
  enum class Kind { Call, Pure, Get, Set };
  Kind kind = Kind::Call;
  std::string id;        // "Health.Damage", "Health.Get current", "Health.Set current"
  std::string component; // reflected type
  std::string member;    // function or field name
  std::vector<GraphPin> inputs, outputs;
  std::string Signature() const; // "Health.Damage(exec, target: object, amount: int) -> exec, result: int"
};

// Every node the reflected components offer, sorted by id. filter: case-insensitive part of the id ("" = all).
std::vector<GraphNodeDesc> BuildGraphNodeLibrary(const std::string& filter = {});
const GraphNodeDesc* FindGraphNode(const std::vector<GraphNodeDesc>& lib, const std::string& id);

// Runs a node on the object `target` with JSON arguments (one per input after exec and target). resultJson gets
// the return value (Call / Pure) or the field value (Get).
bool RunGraphNode(CommandBus& bus, Scene& scene, const GraphNodeDesc& node, const std::string& target,
                  const std::vector<std::string>& args, std::string* resultJson = nullptr, std::string* error = nullptr);

} // namespace bdg
