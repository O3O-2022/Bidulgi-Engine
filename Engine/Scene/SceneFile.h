#pragma once
// Scene files (.scene, JSONL; queue 2-2, format: Docs/features/data-formats.md).
//   {"v":1,"defaults":"1.0","scene":"Main","mode":"3d"}
//   {"id":"g6","name":"Player","pos":[0,1,0],"rot":[0,90,0],"MeshRenderer":{"mesh":"Knight.glb"},"Health":{}}
//   {"id":"g7","name":"Weapon","parent":"@g6","pos":[0.4,0.9,0.2]}
// One object per line, in hierarchy order (a parent before its children, siblings in order). Values equal to
// the defaults are left out: the Transform becomes pos / rot / scale only when not 0 / 0 / 1, a component
// lists only changed fields ({} when all are default). The same scene always gives the same bytes.
#include "Scene/Scene.h"
#include <string>
#include <vector>

namespace bdg {
inline constexpr const char* kSceneDefaults = "1.0"; // default-value set version (migrations when it changes)

std::string WriteSceneText(const Scene& scene, const std::string& sceneName);
bool SaveSceneFile(const Scene& scene, const std::string& sceneName, const std::string& path, std::string* error = nullptr);

// Loads into an empty scene. Problems that do not stop loading (unknown component, unknown key) go to warnings.
bool ReadSceneText(Scene& scene, std::string_view text, std::string* sceneName = nullptr,
                   std::vector<std::string>* warnings = nullptr, std::string* error = nullptr);
bool LoadSceneFile(Scene& scene, const std::string& path, std::string* sceneName = nullptr,
                   std::vector<std::string>* warnings = nullptr, std::string* error = nullptr);
}
