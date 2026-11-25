#pragma once
// Minimal scene model (bootstrap for queue 1-3; extended in 2-1): objects with a persistent id ("g1"),
// a name, a parent and reflected components. The command bus is the only code that edits it in the editor.
#include "Core/Handle.h"
#include "Core/Reflection.h"
#include "Core/Space.h"
#include <memory>
#include <string>
#include <unordered_map>
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
  bool prefab = false;           // shown as a prefab instance (the Hierarchy badge): instance roots
  // Prefab instances (queue 6-1, Scene/Prefab.h). An instance is expanded in the scene: its root has the instance's
  // id and the prefab's objects below it get "<root id>/<id in the prefab>" ("g1/hpbar", nested: "g1/weapon/trail").
  std::string prefabRef;  // a prefab root (the outer instance, or a nested one inside it): "guid:<guid>" of the .prefab
  std::string prefabRoot; // the outermost instance root this object came from ("" = not from a prefab)
  std::string prefabUnresolved; // an instance whose prefab could not be read: its line, written back unchanged
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
  size_t ObjectCount() const { return objects_.Count(); }
  // Runtime handles (queue 1-4): invalid as soon as the object is destroyed, even if the id comes back.
  Handle<SceneObject> HandleOf(std::string_view id) const;
  SceneObject* Resolve(Handle<SceneObject> h, std::string* error = nullptr);
  bool IsValid(Handle<SceneObject> h) const { return objects_.IsValid(h); }

  std::string NewId();                                         // "g<n>", never reused
  SceneObject* Create(std::string id, std::string name, std::string parent = {}, int index = -1);
  bool Destroy(std::string_view id);                           // with all children
  void Clear();                                                // every object (before loading another scene)
  bool Reparent(std::string_view id, std::string parent, int index = -1, std::string* error = nullptr);
  int IndexInParent(std::string_view id) const;
  void* AddComponent(std::string_view id, const TypeInfo& type, std::string_view valueJson = {}, std::string* error = nullptr);
  bool RemoveComponent(std::string_view id, std::string_view typeName);

  // JSON snapshot of an object and its subtree (components as reflection JSON); Restore recreates it.
  std::string Snapshot(std::string_view id) const;
  bool Restore(std::string_view snapshotJson, std::string* error = nullptr);
  std::string Dump() const; // whole scene, deterministic order (tests compare dumps)

  // Transforms (queue 2-1): local = the object's Transform (identity without one), world = parents' world * local.
  glm::mat4 LocalMatrix(std::string_view id) const;
  glm::mat4 WorldMatrix(std::string_view id) const;
  Vec3 WorldPosition(std::string_view id) const;

private:
  std::vector<std::string>& Siblings(const std::string& parent);
  Pool<SceneObject> objects_;
  std::unordered_map<std::string, Handle<SceneObject>> ids_; // persistent id -> current handle
  std::vector<std::string> roots_;
  uint64_t nextId_ = 1;
};
}
