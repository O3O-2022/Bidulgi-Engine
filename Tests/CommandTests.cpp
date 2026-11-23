// Command bus tests (queue 1-3).
#include "Core/Command.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "bdg_test.h"
#include <cstdio>
#include <filesystem>

using namespace bdg;

namespace {
// A small scene: g1 "Root" (Transform, Health) with child g2 "Child" (Transform).
void Fill(Scene& s) {
  s.Create("g1", "Root");
  s.AddComponent("g1", Transform::StaticType());
  s.AddComponent("g1", Health::StaticType());
  s.Create("g2", "Child", "g1");
  s.AddComponent("g2", Transform::StaticType(), "{\"position\":[1,1,1]}");
}
std::string Get(Scene& s, const char* id, const char* comp, const char* field) {
  std::string v;
  Component* c = s.Find(id)->Find(comp);
  Registry::Get(c->data, *c->type, field, v);
  return v;
}
Command Set(const char* target, const char* path, const char* value, double t = -1, const char* source = "ui") {
  Command c;
  c.name = "set";
  c.target = target;
  c.args = std::string("{\"path\":\"") + path + "\",\"value\":" + value + "}";
  c.time = t;
  c.source = source;
  return c;
}
Command Cmd(const char* name, const char* target, const char* args) {
  Command c;
  c.name = name;
  c.target = target;
  c.args = args;
  return c;
}
}

TEST_CASE("commands: set, undo, redo") {
  Scene s; Fill(s);
  CommandBus bus(s);
  CHECK(bus.Execute(Set("g1", "Transform.position", "[3,4,5]")));
  CHECK_EQ(Get(s, "g1", "Transform", "position"), std::string("[3,4,5]"));
  CHECK(bus.Undo()); CHECK_EQ(Get(s, "g1", "Transform", "position"), std::string("[0,0,0]"));
  CHECK(bus.Redo()); CHECK_EQ(Get(s, "g1", "Transform", "position"), std::string("[3,4,5]"));
  CHECK(bus.Execute(Set("g1", "Transform.position.y", "9")));
  CHECK_EQ(Get(s, "g1", "Transform", "position"), std::string("[3,9,5]"));
  CHECK(!bus.Redo()); // a new edit clears redo
  CHECK(bus.Undo() && bus.Undo());
  CHECK(!bus.Undo());
  CHECK_EQ(Get(s, "g1", "Transform", "position"), std::string("[0,0,0]"));
}

TEST_CASE("commands: continuous edits merge") {
  Scene s; Fill(s);
  CommandBus bus(s);
  CHECK(bus.Execute(Set("g1", "Health.maxHealth", "110", 1.0)));
  CHECK(bus.Execute(Set("g1", "Health.maxHealth", "120", 1.1)));
  CHECK(bus.Execute(Set("g1", "Health.maxHealth", "130", 1.35)));   // still within 0.3 s of the last one
  CHECK_EQ(bus.UndoDepth(), size_t(1));
  CHECK(bus.Execute(Set("g1", "Health.maxHealth", "140", 2.0)));    // a pause: new step
  CHECK(bus.Execute(Set("g1", "Transform.scale.x", "2", 2.1)));     // other field: new step
  CHECK(bus.Execute(Set("g1", "Transform.scale.x", "3", 2.2, "ai"))); // not from the UI: never merged
  CHECK_EQ(bus.UndoDepth(), size_t(4));
  bus.Undo(); bus.Undo(); bus.Undo();
  CHECK_EQ(Get(s, "g1", "Health", "maxHealth"), std::string("130"));
  bus.Undo();
  CHECK_EQ(Get(s, "g1", "Health", "maxHealth"), std::string("100"));
}

TEST_CASE("commands: add, delete and undo restore the subtree") {
  Scene s; Fill(s);
  CommandBus bus(s);
  Command add = Cmd("add", "", "{\"name\":\"Cube\",\"parent\":\"g2\"}");
  CHECK(bus.Execute(add));
  CHECK_EQ(s.ObjectCount(), size_t(3));
  CHECK(s.Find("g3") && s.Find("g3")->parent == "g2");
  CHECK(bus.Execute(Cmd("addcomp", "g3", "{\"type\":\"MeshRenderer\",\"value\":{\"mesh\":\"Cube.glb\"}}")));
  std::string before = s.Dump();
  CHECK(bus.Execute(Cmd("del", "g2", "{}")));        // deletes g2 and its child g3
  CHECK_EQ(s.ObjectCount(), size_t(1));
  CHECK(bus.Undo());
  CHECK_EQ(s.Dump(), before);                          // ids, order, components and values come back
  CHECK(bus.Undo() && bus.Undo());                     // addcomp, add
  CHECK(s.Find("g3") == nullptr);
  CHECK(bus.Redo());
  CHECK(s.Find("g3") != nullptr);                      // same id again
}

TEST_CASE("commands: move, rename, components") {
  Scene s; Fill(s);
  CommandBus bus(s);
  CHECK(bus.Execute(Cmd("mv", "g2", "{\"parent\":\"\"}")));
  CHECK(s.Find("g2")->parent.empty() && s.Roots().size() == 2);
  std::string err;
  CHECK(!bus.Execute(Cmd("mv", "g1", "{\"parent\":\"g1\"}"), &err));
  CHECK(err.find("under itself") != std::string::npos);
  CHECK(bus.Undo()); CHECK_EQ(s.Find("g2")->parent, std::string("g1"));
  CHECK(bus.Execute(Cmd("rename", "g1", "{\"name\":\"Hero\"}"))); CHECK_EQ(s.Find("g1")->name, std::string("Hero"));
  CHECK(bus.Undo()); CHECK_EQ(s.Find("g1")->name, std::string("Root"));
  CHECK(bus.Execute(Set("g1", "Health.current", "42")));
  CHECK(bus.Execute(Cmd("rmcomp", "g1", "{\"type\":\"Transform\"}")));
  CHECK(s.Find("g1")->components.size() == 1 && s.Find("g1")->Find("Transform") == nullptr);
  CHECK(bus.Undo());
  CHECK(s.Find("g1")->components[0].type->name == "Transform"); // back in its old slot
  CHECK(bus.Execute(Cmd("rmcomp", "g1", "{\"type\":\"Health\"}")));
  CHECK(bus.Undo());
  CHECK_EQ(Get(s, "g1", "Health", "current"), std::string("42")); // value restored
  CHECK(!bus.Execute(Cmd("addcomp", "g1", "{\"type\":\"Health\"}"), &err)); // already there
}

TEST_CASE("commands: groups undo as one step") {
  Scene s; Fill(s);
  CommandBus bus(s);
  bus.BeginGroup("ai request");
  CHECK(bus.Execute(Set("g1", "Health.maxHealth", "500", -1, "ai")));
  CHECK(bus.Execute(Cmd("rename", "g2", "{\"name\":\"Sidekick\"}")));
  CHECK(bus.Execute(Cmd("add", "", "{\"name\":\"Pet\"}")));
  bus.EndGroup();
  CHECK_EQ(bus.UndoDepth(), size_t(1));
  CHECK(bus.Undo());
  CHECK(s.ObjectCount() == 2 && s.Find("g2")->name == "Child" && Get(s, "g1", "Health", "maxHealth") == "100");
  CHECK(bus.Redo());
  CHECK(s.ObjectCount() == 3 && s.Find("g2")->name == "Sidekick" && Get(s, "g1", "Health", "maxHealth") == "500");
}

TEST_CASE("commands: errors explain and change nothing") {
  Scene s; Fill(s);
  CommandBus bus(s);
  std::string err;
  CHECK(!bus.Execute(Set("g1", "Helth.maxHealth", "1"), &err));
  CHECK(err.find("no component \"Helth\"") != std::string::npos && err.find("Transform, Health") != std::string::npos);
  CHECK(!bus.Execute(Set("g9", "Transform.position", "[1,2,3]"), &err));
  CHECK(err.find("no object \"g9\"") != std::string::npos);
  CHECK(!bus.Execute(Set("g1", "Health.maxHealth", "\"many\""), &err));
  CHECK(!bus.Execute(Cmd("fly", "g1", "{}"), &err));
  CHECK(err.find("unknown command") != std::string::npos);
  CHECK_EQ(bus.UndoDepth(), size_t(0));
  CHECK(bus.Log().empty());
  CHECK_EQ(Get(s, "g1", "Health", "maxHealth"), std::string("100"));
}

TEST_CASE("commands: a saved log replays to the same scene") {
  Scene a; Fill(a);
  CommandBus busA(a);
  busA.Execute(Set("g1", "Transform.position", "[1,2,3]", 0.0));
  busA.Execute(Set("g1", "Transform.position", "[1,2,4]", 0.1)); // merged with the previous one
  busA.Execute(Cmd("add", "", "{\"name\":\"Lamp\",\"parent\":\"g1\"}"));
  busA.Execute(Cmd("addcomp", "g3", "{\"type\":\"Light\"}"));
  busA.Execute(Set("g3", "Light.type", "\"Spot\""));
  busA.Undo();
  busA.Execute(Cmd("rename", "g2", "{\"name\":\"Arm\"}"));
  busA.Undo();
  busA.Redo();
  busA.Execute(Cmd("del", "g2", "{}"));
  busA.Undo();
  busA.Undo();      // rename
  busA.Undo();      // addcomp
  busA.Redo();
  std::string path = (std::filesystem::temp_directory_path() / "bdg_replay_test.jsonl").string();
  CHECK(busA.SaveLog(path));

  Scene b; Fill(b);
  CommandBus busB(b);
  std::string err;
  size_t n = 0;
  CHECK(busB.Replay(path, &err, &n));
  CHECK_EQ(err, std::string(""));
  CHECK_EQ(n, busA.Log().size());
  CHECK_EQ(b.Dump(), a.Dump());
  CHECK_EQ(busB.UndoDepth(), busA.UndoDepth());
  std::remove(path.c_str());
}
