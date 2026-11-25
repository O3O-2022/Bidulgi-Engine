#include "bdg_test.h"
#include "Core/Command.h"
#include "Scene/Components.h"
#include "Scene/Prefab.h"
#include "Scene/SampleComponents.h"
#include "Scene/SceneFile.h"
#include <map>

using namespace bdg;

namespace {
// In-memory prefab files: path -> text; canonical reference "guid:<path>".
std::map<std::string, std::string> g_files;
void UseFiles() {
  SetPrefabLoader([](const std::string& ref, std::string& canonical, std::string& text, std::string& path) {
    const std::string p = ref.starts_with("guid:") ? ref.substr(5) : ref;
    auto it = g_files.find(p);
    if (it == g_files.end()) return false;
    canonical = "guid:" + p;
    text = it->second;
    path = p;
    return true;
  });
}
const char* kEnemy = "{\"v\":1,\"defaults\":\"1.0\",\"prefab\":\"Enemy\"}\n"
                     "{\"id\":\"root\",\"name\":\"Enemy\",\"MeshRenderer\":{\"mesh\":\"Cube\"},\"Health\":{\"maxHealth\":50}}\n"
                     "{\"id\":\"hpbar\",\"parent\":\"@root\",\"pos\":[0,2,0]}\n"
                     "{\"id\":\"weapon\",\"parent\":\"@root\",\"prefab\":\"Sword.prefab\",\"pos\":[0.5,1,0]}\n";
const char* kSword = "{\"v\":1,\"defaults\":\"1.0\",\"prefab\":\"Sword\"}\n"
                     "{\"id\":\"root\",\"name\":\"Sword\",\"MeshRenderer\":{\"mesh\":\"Cylinder\"}}\n"
                     "{\"id\":\"trail\",\"parent\":\"@root\",\"scale\":[1,2,1]}\n";
int Max(Scene& s, const std::string& id) {
  Component* c = s.Find(id) ? s.Find(id)->Find("Health") : nullptr;
  return c ? static_cast<Health*>(c->data)->maxHealth : -1;
}
}

TEST_CASE("prefabs: instances, child paths, nested, overrides saved as differences, edits propagate (queue 6-1)") {
  g_files = {{"Enemy.prefab", kEnemy}, {"Sword.prefab", kSword}};
  UseFiles();
  Scene s;
  CommandBus bus(s);
  std::string err;
  auto exec = [&](const char* name, const std::string& target, const std::string& args) {
    Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    const bool ok = bus.Execute(c, &err);
    if (!ok) std::printf("    %s %s: %s\n", name, target.c_str(), err.c_str());
    return ok;
  };
  CHECK(exec("addprefab", "", R"({"prefab":"Enemy.prefab","id":"a","pos":[1,0,0]})"));
  CHECK(exec("addprefab", "", R"({"prefab":"Enemy.prefab","id":"b","pos":[5,0,0]})"));
  // expanded with paths, nested prefab included
  CHECK(s.Find("a") && s.Find("a/hpbar") && s.Find("a/weapon") && s.Find("a/weapon/trail") && s.Find("b/weapon/trail"));
  CHECK(IsInstanceRoot(*s.Find("a")) && s.Find("a")->prefabRef == "guid:Enemy.prefab" && s.Find("a/weapon")->prefabRef == "guid:Sword.prefab");
  CHECK(InstanceRootOf(s, "a/weapon/trail") == "a" && Max(s, "a") == 50);
  CHECK(PrefabOverrides(s, "a").empty()); // the position is where it stands, not an override
  // override one instance (a value, a child, a component added)
  CHECK(exec("set", "a", R"({"path":"Health.maxHealth","value":5})"));
  CHECK(exec("set", "a/hpbar", R"({"path":"Transform.position","value":[0,3,0]})"));
  CHECK(exec("addcomp", "a/weapon/trail", R"({"type":"Light"})"));
  auto ov = PrefabOverrides(s, "a");
  CHECK(ov.size() == 3);
  // saved as one line per instance with only the differences
  const std::string text = WriteSceneText(s, "T");
  CHECK(text.find(R"("prefab":"guid:Enemy.prefab","pos":[1,0,0],"Health":{"maxHealth":5},"/hpbar":{"pos":[0,3,0]},"/weapon/trail":{"Light":{}})") != std::string::npos);
  CHECK(text.find("\"id\":\"a/hpbar\"") == std::string::npos);
  Scene back;
  std::vector<std::string> warnings;
  CHECK(ReadSceneText(back, text, nullptr, &warnings, &err));
  CHECK(WriteSceneText(back, "T") == text && Max(back, "a") == 5 && back.Find("a/weapon/trail")->Find("Light"));
  // edit the prefab: maxHealth 50 -> 80; the overridden instance keeps 5, the other gets 80
  std::string edited = kEnemy;
  edited.replace(edited.find("\"maxHealth\":50"), 14, "\"maxHealth\":80");
  CHECK(ReloadPrefab(s, "Enemy.prefab", &edited) == 2);
  CHECK(Max(s, "a") == 5 && Max(s, "b") == 80 && s.Find("a/weapon/trail")->Find("Light"));
  // a nested prefab change reaches instances of the outer prefab
  std::string sword = kSword;
  sword.replace(sword.find("[1,2,1]"), 7, "[1,4,1]");
  CHECK(ReloadPrefab(s, "Sword.prefab", &sword) == 2);
  auto scaleY = [&](const char* id) { return static_cast<Transform*>(s.Find(id)->Find("Transform")->data)->scale.y; };
  CHECK(scaleY("b/weapon/trail") == 4.f);
  // revert one override, then everything
  for (const Command& c : RevertPrefabCommands(s, "a", "Health.maxHealth")) CHECK(bus.Execute(c, &err));
  CHECK(Max(s, "a") == 80 && PrefabOverrides(s, "a").size() == 2);
  bus.BeginGroup("Revert all", "ui");
  for (const Command& c : RevertPrefabCommands(s, "a", "")) CHECK(bus.Execute(c, &err));
  bus.EndGroup();
  CHECK(PrefabOverrides(s, "a").empty() && !s.Find("a/weapon/trail")->Find("Light"));
  CHECK(bus.Undo(&err) && PrefabOverrides(s, "a").size() == 2); // one undo step brings both back
  // apply: the instance becomes the prefab; the other instance follows, nothing is an override any more
  const std::string applied = WritePrefabText(s, "a");
  CHECK(applied.find("\"pos\":[1,0,0]") == std::string::npos); // the root's place stays in the scene
  CHECK(ReloadPrefab(s, "Enemy.prefab", &applied) == 2);
  CHECK(PrefabOverrides(s, "a").empty() && s.Find("b/weapon/trail")->Find("Light") && s.Find("b/hpbar"));
  // undo of addprefab removes the instance; redo brings it back with the same ids
  Scene s2;
  CommandBus bus2(s2);
  Command add;
  add.name = "addprefab";
  add.args = R"({"prefab":"guid:Sword.prefab"})";
  CHECK(bus2.Execute(add, &err) && s2.ObjectCount() == 2);
  CHECK(bus2.Undo(&err) && s2.ObjectCount() == 0 && bus2.Redo(&err) && s2.ObjectCount() == 2);
  // a missing prefab: an empty stand-in and a warning, not a failed load
  Scene s3;
  warnings.clear();
  CHECK(ReadSceneText(s3, "{\"v\":1,\"defaults\":\"1.0\",\"scene\":\"T\"}\n{\"id\":\"x\",\"prefab\":\"guid:Gone.prefab\"}\n", nullptr, &warnings, &err));
  CHECK(s3.Find("x") && !warnings.empty());
  CHECK(WriteSceneText(s3, "T").find(R"({"id":"x","prefab":"guid:Gone.prefab"})") != std::string::npos); // the link survives a save
  SetPrefabLoader(nullptr);
}
