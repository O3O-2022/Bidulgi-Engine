// Scene file tests (queue 2-2 exit criterion: save -> open -> save gives the same bytes).
#include "Core/Command.h"
#include "Core/Space.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Scene/DefaultScene.h"
#include "Scene/SceneFile.h"
#include "bdg_test.h"
#include <cmath>
#include <filesystem>
#include <fstream>
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

TEST_CASE("scene files: the rendering reference scene loads cleanly (queue 3-8)") {
  Scene s;
  std::string name, error;
  std::vector<std::string> warnings;
  CHECK(LoadSceneFile(s, std::string(BDG_SOURCE_DIR) + "/Tests/RenderReference/Assets/Scenes/RenderReference.scene", &name, &warnings, &error));
  CHECK(error.empty() && warnings.empty() && name == "RenderReference");
  CHECK(s.ObjectCount() == 290);
  int bookmarks = 0, slots = 0;
  for (const std::string& root : s.Roots())
    if (const SceneObject* o = s.Find(root); o && o->name == "Bookmarks")
      for (const std::string& c : o->children)
        if (SceneObject* b = s.Find(c); b && b->Find("CameraBookmark")) {
          ++bookmarks;
          std::string slot;
          Component* comp = b->Find("CameraBookmark");
          Registry::Get(comp->data, *comp->type, "slot", slot);
          slots |= 1 << std::stoi(slot);
        }
  CHECK(bookmarks == 9 && slots == 0x3fe); // keys 1..9, each once
}

TEST_CASE("scene: templates for new scenes and projects (queue 2-6)") {
  SceneTemplate t;
  CHECK(ParseSceneTemplate("basic3d", t) && t == SceneTemplate::Basic3D);
  CHECK(ParseSceneTemplate("3D", t) && t == SceneTemplate::Basic3D);
  CHECK(ParseSceneTemplate("Empty", t) && t == SceneTemplate::Empty);
  CHECK(!ParseSceneTemplate("fps", t));

  Scene s;
  BuildDefaultScene(s, SceneTemplate::Empty);
  CHECK(s.ObjectCount() == 1 && s.Find("cam") && s.Find("cam")->Find("Camera"));
  BuildDefaultScene(s, SceneTemplate::Basic3D); // replaces what was there
  CHECK_EQ(s.ObjectCount(), size_t(5));
  const char* names[] = {"Main Camera", "Sun", "Sky", "World Settings", "Ground"};
  for (size_t i = 0; i < 5; ++i) CHECK_EQ(s.Find(s.Roots()[i])->name, std::string(names[i]));
  const Transform& cam = *static_cast<const Transform*>(s.Find("cam")->Find("Transform")->data);
  CHECK(Near(cam.position, {0, 1.5f, -6}));
  const glm::vec3 fwd = space::EulerDegreesToQuat(cam.rotation) * glm::vec3(0, 0, -1); // looks at the origin
  CHECK(glm::dot(fwd, glm::normalize(glm::vec3(0, -1.5f, 6))) > 0.9999f);
  const Light& sun = *static_cast<const Light*>(s.Find("sun")->Find("Light")->data);
  CHECK(sun.type == LightType::Directional && sun.castShadows);
  const Transform& sunT = *static_cast<const Transform*>(s.Find("sun")->Find("Transform")->data);
  const glm::vec3 shine = space::EulerDegreesToQuat(sunT.rotation) * glm::vec3(0, 0, -1);
  CHECK(std::abs(std::asin(-shine.y) * 57.29578f - 45) < 0.01f); // 45 degrees down
  CHECK(s.Find("world")->Find("WorldSettings") && s.Find("sky")->Find("Sky"));
  const MeshRenderer& ground = *static_cast<const MeshRenderer*>(s.Find("ground")->Find("MeshRenderer")->data);
  CHECK(ground.mesh == "Plane" && ground.material == "bdg:Grid");

  // the file a new project gets: written, opened, written again -> the same bytes
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bdg_default_scene";
  std::filesystem::remove_all(dir);
  const std::string path = (dir / "Scenes" / "Main.scene").string();
  std::string err;
  CHECK(WriteDefaultScene(path, SceneTemplate::Basic3D, "Main", &err));
  Scene loaded;
  std::string name;
  CHECK(LoadSceneFile(loaded, path, &name) && name == "Main" && loaded.ObjectCount() == 5);
  const std::string again = (dir / "again.scene").string();
  CHECK(SaveSceneFile(loaded, name, again, &err));
  auto read = [](const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  CHECK_EQ(read(path), read(again));
  CHECK(read(path).find("{\"id\":\"world\",\"name\":\"World Settings\",\"WorldSettings\":{}}") != std::string::npos); // defaults left out
  std::filesystem::remove_all(dir);
}
