#pragma once
// Scene files (.scene, JSONL; queue 2-2, format: Docs/features/data-formats.md).
//   {"v":1,"defaults":"1.0","scene":"Main","mode":"3d"}
//   {"id":"g6","name":"Player","pos":[0,1,0],"rot":[0,90,0],"MeshRenderer":{"mesh":"Knight.glb"},"Health":{}}
//   {"id":"g7","name":"Weapon","parent":"@g6","pos":[0.4,0.9,0.2]}
// One object per line, in hierarchy order (a parent before its children, siblings in order). Values equal to
// the defaults are left out: the Transform becomes pos / rot / scale only when not 0 / 0 / 1, a component
// lists only changed fields ({} when all are default). The same scene always gives the same bytes.
//
// Prefab instances (queue 6-1, Scene/Prefab.h) are one line: the prefab and only what differs from it.
//   {"id":"g1","name":"Goblin 2","prefab":"guid:3fa2…","pos":[6,0,1],"Health":{"hp":12},"/hpbar":{"pos":[0,2,0]},"-Collider":true}
// "/hpbar" holds the overrides of the prefab's object "hpbar" (scene id "g1/hpbar"), "-Type" a prefab component the
// instance removed. Objects added under an instance are their own lines ("parent":"@g1/hpbar"). Prefab files use
// the same lines with a {"v":1,"defaults":"1.0","prefab":"Goblin"} header; their first object is the root.
#include "Scene/Scene.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {
inline constexpr const char* kSceneDefaults = "1.0"; // default-value set version (migrations when it changes)

std::string WriteSceneText(const Scene& scene, const std::string& sceneName);
bool SaveSceneFile(const Scene& scene, const std::string& sceneName, const std::string& path, std::string* error = nullptr);
bool SaveTextFile(const std::string& path, const std::string& text, std::string* error = nullptr); // via a temp file

// Loads into an empty scene. Problems that do not stop loading (unknown component, unknown key) go to warnings.
bool ReadSceneText(Scene& scene, std::string_view text, std::string* sceneName = nullptr,
                   std::vector<std::string>* warnings = nullptr, std::string* error = nullptr);
bool LoadSceneFile(Scene& scene, const std::string& path, std::string* sceneName = nullptr,
                   std::vector<std::string>* warnings = nullptr, std::string* error = nullptr);

// Object lines without a header, for prefabs: written for `roots` and their subtrees (localId renames ids and parent
// references, plainRoot is written as a plain object even if it is an instance: a prefab file's own root), and read
// into a scene that may already hold objects (ids must be free).
std::string WriteObjectLines(const Scene& scene, const std::vector<std::string>& roots,
                             const std::function<std::string(const std::string&)>& localId, const std::string& plainRoot);
bool ReadObjectLines(Scene& scene, std::string_view text, std::vector<std::string>* warnings = nullptr, std::string* error = nullptr);
}
