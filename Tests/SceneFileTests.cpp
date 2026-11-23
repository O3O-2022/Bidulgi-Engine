// Scene file tests (queue 2-2 exit criterion: save -> open -> save gives the same bytes).
#include "Core/Command.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Scene/SceneFile.h"
#include "bdg_test.h"
#include <cmath>
#include <filesystem>
#include <random>

using namespace bdg;

namespace {
bool Near(const Vec3& a, const Vec3& b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) < 1e-4f; }
void Build(Scene& s) {
  CommandBus bus(s);
  auto run = [&](const char* name, const char* target, const char* args) {
    Command c;
    c.name = name; c.target = target; c.args = args;
    bus.Execute(c);
  };
  run("add", "", R"({"name":"Main Camera","id":"g1"})");
  run("set", "g1", R"({"path":"Transform.position","value":[0,2,-6]})");
  run("addcomp", "g1", R"({"type":"Camera","value":{"fov":70}})");
  run("add", "", R"({"name":"Player","id":"g2"})");
  run("set", "g2", R"({"path":"Transform.rotation","value":[0,90,0]})");
  run("addcomp", "g2", R"({"type":"Health"})");
  run("addcomp", "g2", R"({"type":"MeshRenderer","value":{"mesh":"기사.glb"}})");
  run("add", "", R"({"name":"Weapon","id":"g3","parent":"g2"})");
  run("set", "g3", R"({"path":"Transform.position","value":[0.4,0.9,0.2]})");
  run("add", "", R"({"name":"g4","id":"g4","parent":"g2","index":0})"); // name == id, first child
  run("addcomp", "g4", R"({"type":"Light","value":{"color":[1,0.75,0.45,1],"type":"Spot"}})");
  s.Find("g2")->prefab = true;
}
}

TEST_CASE("scene file: save, open, save gives the same bytes") {
  Scene a;
  Build(a);
  std::string first = WriteSceneText(a, "Main");
  Scene b;
  std::string name, err;
  std::vector<std::string> warnings;
  CHECK(ReadSceneText(b, first, &name, &warnings, &err));
  CHECK_EQ(err, std::string(""));
  CHECK(warnings.empty());
  CHECK_EQ(name, std::string("Main"));
  CHECK_EQ(WriteSceneText(b, "Main"), first);
  CHECK_EQ(b.Dump(), a.Dump());
  auto path = (std::filesystem::temp_directory_path() / "bdg_test.scene").string();
  CHECK(SaveSceneFile(b, "Main", path, &err));
  Scene c;
  CHECK(LoadSceneFile(c, path, nullptr, nullptr, &err));
  CHECK_EQ(WriteSceneText(c, "Main"), first);
  std::filesystem::remove(path);
}

TEST_CASE("scene file: defaults are left out, hierarchy order kept") {
  Scene a;
  Build(a);
  std::string text = WriteSceneText(a, "Main");
  std::vector<std::string> lines;
  for (size_t p = 0, e; (e = text.find('\n', p)) != std::string::npos; p = e + 1) lines.push_back(text.substr(p, e - p));
  CHECK_EQ(lines.size(), size_t(5));
  CHECK_EQ(lines[0], std::string(R"({"v":1,"defaults":"1.0","scene":"Main","mode":"3d"})"));
  CHECK_EQ(lines[1], std::string(R"({"id":"g1","name":"Main Camera","pos":[0,2,-6],"Camera":{"fov":70}})"));
  CHECK_EQ(lines[2], std::string(R"({"id":"g2","name":"Player","prefab":true,"rot":[0,90,0],"Health":{},"MeshRenderer":{"mesh":"기사.glb"}})"));
  CHECK_EQ(lines[3], std::string(R"({"id":"g4","parent":"@g2","Light":{"type":"Spot","color":[1,0.75,0.45,1]}})")); // first child first
  CHECK_EQ(lines[4], std::string(R"({"id":"g3","name":"Weapon","parent":"@g2","pos":[0.4,0.9,0.2]})"));
}

TEST_CASE("scene file: errors and warnings") {
  std::string err;
  std::vector<std::string> warnings;
  { Scene s; CHECK(!ReadSceneText(s, "", nullptr, nullptr, &err)); CHECK(err.find("no header") != std::string::npos); }
  { Scene s; CHECK(!ReadSceneText(s, "{\"v\":2}\n", nullptr, nullptr, &err)); CHECK(err.find("v\":1") != std::string::npos); }
  { Scene s; CHECK(!ReadSceneText(s, "{\"v\":1,\"defaults\":\"1.0\"}\n{\"name\":\"x\"}\n", nullptr, nullptr, &err)); CHECK(err.find("line 2") != std::string::npos); }
  { Scene s; CHECK(!ReadSceneText(s, "{\"v\":1,\"defaults\":\"1.0\"}\n{\"id\":\"a\",\"parent\":\"@b\"}\n", nullptr, nullptr, &err)); }
  { Scene s; CHECK(!ReadSceneText(s, "{\"v\":1,\"defaults\":\"1.0\"}\n{\"id\":\"a\"\n", nullptr, nullptr, &err)); CHECK(err.find("line 2") != std::string::npos); }
  Scene s;
  CHECK(ReadSceneText(s, "{\"v\":1,\"defaults\":\"0.9\"}\n{\"id\":\"a\",\"Jetpack\":{\"fuel\":3},\"tag\":\"x\",\"pos\":[1,2,3]}\n", nullptr, &warnings, &err));
  CHECK_EQ(warnings.size(), size_t(3)); // old defaults set, unknown component, unknown key
  CHECK(s.Find("a") && s.Find("a")->Find("Transform") != nullptr);
  CHECK(Near(s.WorldPosition("a"), Vec3{1, 2, 3}));
}

TEST_CASE("scene file: mutated files never crash the loader") {
  Scene a;
  Build(a);
  std::string valid = WriteSceneText(a, "Main");
  std::mt19937 rng(5);
  int loaded = 0;
  for (int i = 0; i < 1500; ++i) {
    std::string m = valid;
    for (int k = 0; k < 1 + (int)(rng() % 5); ++k) {
      size_t at = rng() % m.size();
      switch (rng() % 3) {
        case 0: m[at] = "{}[]\",:@0-x\n"[rng() % 12]; break;
        case 1: m.erase(at, 1 + rng() % 6); break;
        default: m.insert(at, m.substr(rng() % m.size(), rng() % 10)); break;
      }
      if (m.empty()) m = "{";
    }
    Scene s;
    loaded += ReadSceneText(s, m);
  }
  CHECK(loaded >= 0);
}
