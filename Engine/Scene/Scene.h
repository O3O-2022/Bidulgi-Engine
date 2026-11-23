#pragma once
// Minimal scene model (bootstrap for queue 1-3; extended in 2-1): objects with a persistent id ("g1"),
// a name, a parent and reflected components. The command bus is the only code that edits it in the editor.
#include "Core/Reflection.h"
#include <memory>
#include <string>
#include <vector>

namespace bdg {
struct Component {
  const TypeInfo* type = nullptr;
  void* data = nullptr; // owned; created and destroyed through the TypeInfo
};

struct SceneObject {
  std::string id, name, parent;  // parent "" = scene root
  std::vector<std::string> children;
  std::vector<Component> components;
  bool prefab = false;           // shown as a prefab instance (prefabs: queue 6-1)
  Component* Find(std::string_view typeName);
};

class Scene {
public:
  Scene() = default;
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  ~Scene();

  SceneObject* Find(std::string_view id);
  const SceneObject* Find(std::string_view id) const;
  const std::vector<std::string>& Roots() const { return roots_; }
  size_t ObjectCount() const { return objects_.size(); }

  std::string NewId();                                         // "g<n>", never reused
  SceneObject* Create(std::string id, std::string name, std::string parent = {}, int index = -1);
  bool Destroy(std::string_view id);                           // with all children
  bool Reparent(std::string_view id, std::string parent, int index = -1, std::string* error = nullptr);
  int IndexInParent(std::string_view id) const;
  void* AddComponent(std::string_view id, const TypeInfo& type, std::string_view valueJson = {}, std::string* error = nullptr);
  bool RemoveComponent(std::string_view id, std::string_view typeName);

  // JSON snapshot of an object and its subtree (components as reflection JSON); Restore recreates it.
  std::string Snapshot(std::string_view id) const;
  bool Restore(std::string_view snapshotJson, std::string* error = nullptr);
  std::string Dump() const; // whole scene, deterministic order (tests compare dumps)

private:
  std::vector<std::string>& Siblings(const std::string& parent);
  std::vector<std::unique_ptr<SceneObject>> objects_;
  std::vector<std::string> roots_;
  uint64_t nextId_ = 1;
};
}
