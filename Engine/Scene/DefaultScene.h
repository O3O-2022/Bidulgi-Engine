#pragma once
// Scene templates for new scenes and new projects (queue 2-6). The editor's File > New Scene (Ctrl+N) and
// `bdg new` (2-7) both build from here, so a new project's Scenes/Main.scene is the same file either way.
//
// Basic 3D (Docs/features/projects-and-templates.md §5):
//   Main Camera  perspective, at (0, 1.5, -6) looking at the origin
//   Sun          directional light, shadows, 45 degrees down
//   Sky          procedural sky (its sun follows the first directional light), sky-coloured ambient light
//   World Settings  fog and exposure for the whole scene
//   Ground       20 x 20 m plane with the built-in grid material ("bdg:Grid")
// Empty: Main Camera only.
#include "Scene/Scene.h"
#include <string>
#include <string_view>

namespace bdg {

enum class SceneTemplate { Empty, Basic3D };

bool ParseSceneTemplate(std::string_view name, SceneTemplate& out); // "empty", "basic3d" / "3d" (any case)
const char* SceneTemplateName(SceneTemplate t);                     // "empty", "basic3d"
// Clears `scene` and builds the template (directly: building a scene is not an edit).
void BuildDefaultScene(Scene& scene, SceneTemplate t);
// Builds the template and saves it as a scene file (folders are created). false + err on failure.
bool WriteDefaultScene(const std::string& path, SceneTemplate t, const std::string& sceneName, std::string* err = nullptr);

} // namespace bdg
