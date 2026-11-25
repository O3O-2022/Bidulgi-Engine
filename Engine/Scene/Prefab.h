#pragma once
// Prefabs (queue 6-1, Unity's prefabs, Docs/features/data-formats.md). A .prefab is a JSONL file of objects like a
// scene ({"v":1,"defaults":"1.0","prefab":"Enemy"} then the root and its children); it may hold instances of other
// prefabs (nested). A scene places it as an instance: one line naming the prefab ("prefab":"guid:<guid>") plus what
// differs from it (overrides). In memory the instance is expanded: its root has the instance id, the prefab's other
// objects get "<instance id>/<id in the prefab>" ("g1/hpbar"; nested "g1/weapon/trail"), so commands, scripts and the
// CLI reach them by that path.
//
// Overrides are not stored separately: they are whatever differs from the prefab (compared field by field with a
// fresh expansion of it). That is what the Inspector marks, what a scene saves, and what survives when the prefab
// changes: editing a prefab rebuilds every instance from the new prefab plus its own overrides.
#include "Core/Command.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {
class AssetDatabase;

// Where prefab references resolve: "guid:<guid>", a project path ("Prefabs/Enemy.prefab") or a file name. The last
// opened AssetDatabase is used (AssetDatabase::Open sets it); tests can give a loader instead.
void SetPrefabAssets(const AssetDatabase* db);
void ForgetPrefabAssets(const AssetDatabase* db); // the database goes away
using PrefabLoader = std::function<bool(const std::string& ref, std::string& canonical, std::string& text, std::string& path)>;
void SetPrefabLoader(PrefabLoader loader); // nullptr: back to the asset database
void ClearPrefabCache();
// canonical: "guid:<guid>" (the loader's own name for test loaders); text: the file as last read; path: project path.
bool ResolvePrefab(const std::string& ref, std::string& canonical, std::string* text = nullptr, std::string* path = nullptr,
                   std::string* error = nullptr);

// Expands the prefab into `scene` as an instance with root id `id` under `parent` (index -1 = last). No overrides.
bool InstantiatePrefab(Scene& scene, const std::string& ref, const std::string& id, const std::string& parent, int index,
                       std::string* error = nullptr);
inline bool IsInstanceRoot(const SceneObject& o) { return !o.prefabRef.empty() && o.prefabRoot == o.id; }
std::string InstanceRootOf(const Scene& scene, const std::string& id); // the outermost instance holding it, "" = none

// What differs from the prefab, per object of the instance: "Component.field", "name", "+Component" (added), "-Component"
// (removed), "+object" (an object added under the instance). The root's position and rotation are where the instance
// stands, not overrides.
struct PrefabOverride { std::string object, path; };
std::vector<PrefabOverride> PrefabOverrides(const Scene& scene, const std::string& rootId);
// Commands that put the prefab's value back: one override (object + path), or with path "" every override of the
// instance (`object` = its root). Run them as one group.
std::vector<Command> RevertPrefabCommands(const Scene& scene, const std::string& object, const std::string& path);

// The object and its children as a .prefab file (the root's own place in the scene is left out).
std::string WritePrefabText(const Scene& scene, const std::string& rootId);
// Writes the prefab file (the asset database's file for `canonical`).
bool SavePrefabFile(const std::string& canonical, const std::string& text, std::string* error = nullptr);
// The prefab changed (newText, or the file read again when nullptr): every instance that uses it, also through a
// nested prefab, is rebuilt from the new prefab plus its own overrides. Returns how many instances were rebuilt.
int ReloadPrefab(Scene& scene, const std::string& canonical, const std::string* newText = nullptr);
}
