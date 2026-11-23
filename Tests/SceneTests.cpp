// Scene model tests (queue 2-1): every object has a Transform, world matrices follow the parent chain,
// and moving under a new parent can keep the object where it is.
#include "Core/Command.h"
#include "Scene/Components.h"
#include "bdg_test.h"
#include <cmath>
#include <filesystem>

using namespace bdg;

namespace {
bool Near(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
  return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}
Command Cmd(const char* name, const char* target, std::string args) {
  Command c;
  c.name = name;
  c.target = target;
  c.args = std::move(args);
  return c;
}
std::string TransformJson(Scene& s, const char* id) {
  Component* c = s.Find(id)->Find("Transform");
  ValueType vt;
  vt.type = FieldType::Struct;
  vt.structType = c->type;
  return Registry::ToJson(c->data, vt);
}
// Parent g1 at (10,0,0) turned yaw +90 and scaled 2; child g2 at local (1,0,0).
void Build(Scene& s, CommandBus& bus) {
  bus.Execute(Cmd("add", "", R"({"name":"Parent","id":"g1"})"));
  bus.Execute(Cmd("add", "", R"({"name":"Child","id":"g2","parent":"g1"})"));
  bus.Execute(Cmd("set", "g1", R"({"path":"Transform.position","value":[10,0,0]})"));
  bus.Execute(Cmd("set", "g1", R"({"path":"Transform.rotation","value":[0,90,0]})"));
  bus.Execute(Cmd("set", "g1", R"({"path":"Transform.scale","value":[2,2,2]})"));
  bus.Execute(Cmd("set", "g2", R"({"path":"Transform.position","value":[1,0,0]})"));
  (void)s;
}
}

TEST_CASE("scene: new objects get a Transform that cannot be removed") {
  Scene s;
  CommandBus bus(s);
  CHECK(bus.Execute(Cmd("add", "", R"({"name":"Box"})")));
  SceneObject* o = s.Find("g1");
  CHECK(o && o->Find("Transform") != nullptr);
  std::string err;
  CHECK(!bus.Execute(Cmd("rmcomp", "g1", R"({"type":"Transform"})"), &err));
  CHECK(err.find("keeps its Transform") != std::string::npos);
}

TEST_CASE("scene: world matrices follow the parent chain") {
  Scene s;
  CommandBus bus(s);
  Build(s, bus);
  CHECK(Near(s.WorldPosition("g1"), {10, 0, 0}));
  CHECK(Near(s.WorldPosition("g2"), {10, 0, -2})); // local (1,0,0) * scale 2, turned left 90 -> (0,0,-2), + parent
  bus.Execute(Cmd("add", "", R"({"name":"Grandchild","id":"g3","parent":"g2"})"));
  bus.Execute(Cmd("set", "g3", R"({"path":"Transform.position","value":[0,1,0]})"));
  CHECK(Near(s.WorldPosition("g3"), {10, 2, -2}));
  CHECK(Near(s.WorldPosition("g9"), {0, 0, 0})); // unknown id: identity
}

TEST_CASE("scene: moving with keepWorld leaves the object in place") {
  Scene s;
  CommandBus bus(s);
  Build(s, bus);
  Vec3 before = s.WorldPosition("g2");
  std::string local = TransformJson(s, "g2");
  CHECK(bus.Execute(Cmd("mv", "g2", R"({"parent":"","keepWorld":true})")));
  CHECK(s.Find("g2")->parent.empty());
  CHECK(Near(s.WorldPosition("g2"), before));                   // same place on screen
  CHECK(Near(static_cast<Transform*>(s.Find("g2")->Find("Transform")->data)->scale, {2, 2, 2})); // parent's scale moved in
  CHECK(bus.Undo());
  CHECK_EQ(s.Find("g2")->parent, std::string("g1"));
  CHECK_EQ(TransformJson(s, "g2"), local);                       // exact old local values
  CHECK(bus.Redo());
  CHECK(Near(s.WorldPosition("g2"), before));
  // Without keepWorld the local values stay and the object jumps
  CHECK(bus.Execute(Cmd("mv", "g2", R"({"parent":"g1"})")));
  CHECK(!Near(s.WorldPosition("g2"), before)); // its root-space values are now read under the parent
}

TEST_CASE("scene: keep-world moves replay exactly") {
  Scene a;
  CommandBus busA(a);
  Build(a, busA);
  busA.Execute(Cmd("mv", "g2", R"({"parent":"","keepWorld":true})"));
  busA.Execute(Cmd("set", "g1", R"({"path":"Transform.rotation.y","value":33})"));
  busA.Execute(Cmd("mv", "g2", R"({"parent":"g1","keepWorld":true})"));
  busA.Undo();
  busA.Redo();
  auto path = (std::filesystem::temp_directory_path() / "bdg_scene_mv.jsonl").string();
  CHECK(busA.SaveLog(path));
  Scene b;
  CommandBus busB(b);
  std::string err;
  CHECK(busB.Replay(path, &err));
  CHECK_EQ(b.Dump(), a.Dump());
  std::filesystem::remove(path);
}
