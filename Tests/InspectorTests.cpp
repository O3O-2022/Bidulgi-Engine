// Inspector metadata tests (queue 2-4): Header, Button functions as undoable "call" commands, ShowIf.
#include "Core/Command.h"
#include "Runtime/EditorModel.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Tests/BTestComponent.h"
#include "bdg_test.h"
#include <filesystem>

using namespace bdg;

namespace {
Command Cmd(const char* name, const char* target, const char* args) {
  Command c;
  c.name = name; c.target = target; c.args = args;
  return c;
}
std::string Get(Scene& s, const char* id, const char* comp, const char* field) {
  std::string v;
  Component* c = s.Find(id)->Find(comp);
  Registry::Get(c->data, *c->type, field, v);
  return v;
}
}

TEST_CASE("inspector: Header and Button metadata are generated") {
  CHECK_EQ(PlayerMove::StaticType().FindField("speed")->meta.header, std::string("Movement"));
  CHECK(PlayerMove::StaticType().FindField("jumpHeight")->meta.header.empty());
  const FunctionInfo* f = Health::StaticType().FindFunction("Revive");
  CHECK(f && (f->flags & kButton) && f->params.empty());
  CHECK(!(bdgtest::BTestComponent::StaticType().FindFunction("Heal")->flags & kButton));
}

TEST_CASE("inspector: a button runs as a call command that undoes") {
  Scene s;
  CommandBus bus(s);
  CHECK(bus.Execute(Cmd("add", "", R"({"name":"Hero","id":"g1"})")));
  CHECK(bus.Execute(Cmd("addcomp", "g1", R"({"type":"Health"})")));
  CHECK(bus.Execute(Cmd("set", "g1", R"({"path":"Health.current","value":10})")));
  CHECK(bus.Execute(Cmd("call", "g1", R"({"type":"Health","function":"Revive"})")));
  CHECK_EQ(Get(s, "g1", "Health", "current"), std::string("100"));
  CHECK(bus.Undo());
  CHECK_EQ(Get(s, "g1", "Health", "current"), std::string("10"));
  CHECK(bus.Redo());
  CHECK_EQ(Get(s, "g1", "Health", "current"), std::string("100"));
  std::string err;
  CHECK(!bus.Execute(Cmd("call", "g1", R"({"type":"Health","function":"Explode"})"), &err));
  CHECK(err.find("no function") != std::string::npos);
  CHECK(!bus.Execute(Cmd("call", "g1", R"({"type":"Camera","function":"Revive"})"), &err));
  CHECK(bus.Execute(Cmd("setcomp", "g1", R"({"type":"Health","value":{"maxHealth":7,"current":3}})")));
  CHECK(Get(s, "g1", "Health", "maxHealth") == "7" && Get(s, "g1", "Health", "current") == "3");
  CHECK(bus.Undo());
  CHECK_EQ(Get(s, "g1", "Health", "maxHealth"), std::string("100"));
  auto path = (std::filesystem::temp_directory_path() / "bdg_call.jsonl").string(); // calls replay too
  CHECK(bus.SaveLog(path));
  Scene b;
  CommandBus busB(b);
  CHECK(busB.Replay(path, &err));
  CHECK_EQ(b.Dump(), s.Dump());
  std::filesystem::remove(path);
}

TEST_CASE("inspector: ShowIf conditions") {
  Scene s;
  s.Create("g1", "Lamp");
  auto* light = static_cast<Light*>(s.AddComponent("g1", Light::StaticType()));
  Component& lc = *s.Find("g1")->Find("Light");
  light->type = LightType::Directional;
  CHECK(!EditorModel::ShowIfPasses(lc, "type != Directional"));
  CHECK(EditorModel::ShowIfPasses(lc, "type == Directional"));
  light->type = LightType::Spot;
  CHECK(EditorModel::ShowIfPasses(lc, "type != Directional"));
  CHECK(EditorModel::ShowIfPasses(lc, "type == \"Spot\""));
  CHECK(EditorModel::ShowIfPasses(lc, ""));
  CHECK(EditorModel::ShowIfPasses(lc, "nosuchfield")); // unknown: shown, never hidden by a typo
  s.Create("g2", "Test");
  auto* t = static_cast<bdgtest::BTestComponent*>(s.AddComponent("g2", bdgtest::BTestComponent::StaticType()));
  Component& tc = *s.Find("g2")->Find("BTestComponent");
  CHECK(!EditorModel::ShowIfPasses(tc, "invincible"));
  CHECK(EditorModel::ShowIfPasses(tc, "!invincible"));
  t->invincible = true;
  CHECK(EditorModel::ShowIfPasses(tc, "invincible"));
  CHECK(EditorModel::ShowIfPasses(tc, "hp"));       // non-zero number
  t->hp = 0;
  CHECK(!EditorModel::ShowIfPasses(tc, "hp"));
}
