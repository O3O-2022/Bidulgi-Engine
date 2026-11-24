#include "Scene/DefaultScene.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include <cctype>
#include <filesystem>

namespace bdg {
namespace {
template <class T> T& Add(Scene& s, const std::string& id) { return *static_cast<T*>(s.AddComponent(id, T::StaticType())); }
}

bool ParseSceneTemplate(std::string_view name, SceneTemplate& out) {
  std::string n;
  for (char c : name)
    if (c != '-' && c != '_' && c != ' ') n += (char)std::tolower((unsigned char)c);
  if (n == "empty" || n == "blank") { out = SceneTemplate::Empty; return true; }
  if (n == "basic3d" || n == "3d" || n == "default") { out = SceneTemplate::Basic3D; return true; }
  return false;
}

const char* SceneTemplateName(SceneTemplate t) { return t == SceneTemplate::Empty ? "empty" : "basic3d"; }

void BuildDefaultScene(Scene& scene, SceneTemplate t) {
  scene.Clear();
  scene.Create("cam", "Main Camera", "");
  Transform& cam = Add<Transform>(scene, "cam");
  cam.position = {0, 1.5f, -6};
  cam.rotation = {-14.036243f, 180, 0}; // turned to look along +Z at the origin: atan(1.5 / 6) down
  Add<Camera>(scene, "cam");
  if (t == SceneTemplate::Empty) return;

  scene.Create("sun", "Sun", "");
  Add<Transform>(scene, "sun").rotation = {-45, -30, 0}; // pitch -45: shining down
  Light& sun = Add<Light>(scene, "sun");
  sun.type = LightType::Directional;
  sun.intensity = 3;
  sun.castShadows = true;

  scene.Create("sky", "Sky", "");
  Add<Transform>(scene, "sky");
  Add<Sky>(scene, "sky");

  scene.Create("world", "World Settings", "");
  Add<Transform>(scene, "world");
  Add<WorldSettings>(scene, "world");

  scene.Create("ground", "Ground", "");
  Add<Transform>(scene, "ground").scale = {20, 1, 20};
  MeshRenderer& ground = Add<MeshRenderer>(scene, "ground");
  ground.mesh = "Plane";
  ground.material = "bdg:Grid";
}

bool WriteDefaultScene(const std::string& path, SceneTemplate t, const std::string& sceneName, std::string* err) {
  Scene scene;
  BuildDefaultScene(scene, t);
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::path(std::u8string((const char8_t*)path.c_str()));
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  return SaveSceneFile(scene, sceneName, path, err);
}

} // namespace bdg
