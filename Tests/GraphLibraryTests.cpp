// Graph nodes from reflection (queue E-8).
#include "Scene/Components.h"
#include "Scene/GraphLibrary.h"
#include "Scene/SampleComponents.h"
#include "bdg_test.h"

using namespace bdg;

TEST_CASE("graph nodes: functions and fields of components become nodes (queue E-8)") {
  const auto lib = BuildGraphNodeLibrary();
  const GraphNodeDesc* damage = FindGraphNode(lib, "Health.Damage");
  CHECK(damage && damage->kind == GraphNodeDesc::Kind::Call);
  CHECK(damage && damage->Signature() == "Health.Damage(exec, target: object, amount: int) -> exec, result: int");
  const GraphNodeDesc* ratio = FindGraphNode(lib, "health.ratio"); // ids match without case
  CHECK(ratio && ratio->kind == GraphNodeDesc::Kind::Pure && ratio->Signature() == "Health.Ratio(target: object) -> result: float");
  const GraphNodeDesc* revive = FindGraphNode(lib, "Health.Revive"); // a Button is callable too, no result
  CHECK(revive && revive->Signature() == "Health.Revive(exec, target: object) -> exec");
  const GraphNodeDesc* get = FindGraphNode(lib, "Health.Get current");
  const GraphNodeDesc* set = FindGraphNode(lib, "Health.Set current");
  CHECK(get && get->kind == GraphNodeDesc::Kind::Get && set && set->kind == GraphNodeDesc::Kind::Set);
  CHECK(FindGraphNode(lib, "Light.Get type") && FindGraphNode(lib, "Light.Get type")->outputs[0].type == "LightType");
  CHECK(FindGraphNode(lib, "Transform.Set position") && FindGraphNode(lib, "Transform.Set position")->inputs[2].type == "vec3");
  const auto onlyHealth = BuildGraphNodeLibrary("health.");
  CHECK(!onlyHealth.empty());
  for (const GraphNodeDesc& n : onlyHealth) CHECK(n.component == "Health");
  for (size_t i = 1; i < lib.size(); ++i) CHECK(lib[i - 1].id <= lib[i].id); // sorted
}

TEST_CASE("graph nodes: running calls, values and sets; calls undo (queue E-8)") {
  Scene s;
  s.Create("hero", "Hero", "");
  Health& h = *static_cast<Health*>(s.AddComponent("hero", Health::StaticType()));
  h.maxHealth = 100;
  h.current = 80;
  CommandBus bus(s);
  const auto lib = BuildGraphNodeLibrary();
  std::string out, err;
  CHECK(RunGraphNode(bus, s, *FindGraphNode(lib, "Health.Damage"), "hero", {"30"}, &out, &err));
  CHECK(out == "50" && h.current == 50 && bus.UndoDepth() == 1);
  CHECK(RunGraphNode(bus, s, *FindGraphNode(lib, "Health.Ratio"), "hero", {}, &out, &err));
  CHECK(out == "0.5" && bus.UndoDepth() == 1); // pure: no command
  CHECK(RunGraphNode(bus, s, *FindGraphNode(lib, "Health.Get current"), "hero", {}, &out, &err) && out == "50");
  CHECK(RunGraphNode(bus, s, *FindGraphNode(lib, "Health.Set current"), "hero", {"7"}, nullptr, &err) && h.current == 7);
  CHECK(bus.Undo() && h.current == 50 && bus.Undo() && h.current == 80); // the set, then the call
  CHECK(!RunGraphNode(bus, s, *FindGraphNode(lib, "Health.Damage"), "hero", {}, &out, &err) && err.find("takes 1") != std::string::npos);
  CHECK(!RunGraphNode(bus, s, *FindGraphNode(lib, "Health.Damage"), "nobody", {"1"}, &out, &err));
  CHECK(!RunGraphNode(bus, s, *FindGraphNode(lib, "Light.Get range"), "hero", {}, &out, &err) && err.find("no component") != std::string::npos);
}
