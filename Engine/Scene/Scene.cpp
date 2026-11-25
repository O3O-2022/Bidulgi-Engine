#include "Scene/Scene.h"
#include "Scene/Components.h"
#include <yyjson.h>
#include <algorithm>
#include <cstdlib>
#include <format>

namespace bdg {
namespace {
std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); }
ValueType StructOf(const TypeInfo& t) {
  ValueType v;
  v.type = FieldType::Struct;
  v.structType = &t;
  v.size = t.size;
  return v;
}
bool Fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}
}

Component* SceneObject::Find(std::string_view typeName) {
  for (auto& c : components) if (c.type->name == typeName) return &c;
  return nullptr;
}

Scene::~Scene() {
  objects_.ForEach([](Handle<SceneObject>, SceneObject& o) {
    for (auto& c : o.components) if (c.type->destroy) c.type->destroy(c.data);
  });
}

SceneObject* Scene::Find(std::string_view id) {
  auto it = ids_.find(std::string(id));
  return it == ids_.end() ? nullptr : objects_.Get(it->second);
}

Handle<SceneObject> Scene::HandleOf(std::string_view id) const {
  auto it = ids_.find(std::string(id));
  return it == ids_.end() ? Handle<SceneObject>{} : it->second;
}

SceneObject* Scene::Resolve(Handle<SceneObject> h, std::string* error) { return objects_.Resolve(h, "SceneObject", error); }
const SceneObject* Scene::Find(std::string_view id) const { return const_cast<Scene*>(this)->Find(id); }

std::string Scene::NewId() {
  std::string id;
  do { id = "g" + std::to_string(nextId_++); } while (Find(id));
  return id;
}

std::vector<std::string>& Scene::Siblings(const std::string& parent) {
  if (parent.empty()) return roots_;
  return Find(parent)->children;
}

SceneObject* Scene::Create(std::string id, std::string name, std::string parent, int index) {
  if (id.empty()) id = NewId();
  if (Find(id) || (!parent.empty() && !Find(parent))) return nullptr;
  if (id.size() > 1 && id[0] == 'g') { // keep generated ids unique after loading or restoring "g<n>"
    char* end = nullptr;
    unsigned long long n = std::strtoull(id.c_str() + 1, &end, 10);
    if (end && !*end && n >= nextId_) nextId_ = n + 1;
  }
  Handle<SceneObject> h = objects_.Create();
  SceneObject* o = objects_.Get(h);
  o->id = id;
  o->name = std::move(name);
  o->parent = parent;
  ids_[id] = h;
  auto& sib = Siblings(parent);
  if (index < 0 || index > (int)sib.size()) index = (int)sib.size();
  sib.insert(sib.begin() + index, id);
  return o;
}

bool Scene::Destroy(std::string_view id) {
  SceneObject* o = Find(id);
  if (!o) return false;
  std::vector<std::string> kids = o->children;
  for (auto& k : kids) Destroy(k);
  o = Find(id);
  auto& sib = Siblings(o->parent);
  sib.erase(std::remove(sib.begin(), sib.end(), o->id), sib.end());
  for (auto& c : o->components) if (c.type->destroy) c.type->destroy(c.data);
  auto it = ids_.find(o->id);
  Handle<SceneObject> h = it->second;
  ids_.erase(it);
  objects_.Destroy(h); // every handle to it is now invalid
  return true;
}

void Scene::Clear() {
  std::vector<std::string> roots = roots_;
  for (auto& r : roots) Destroy(r);
  nextId_ = 1;
}

int Scene::IndexInParent(std::string_view id) const {
  const SceneObject* o = Find(id);
  if (!o) return -1;
  const auto& sib = o->parent.empty() ? roots_ : Find(o->parent)->children;
  return (int)(std::find(sib.begin(), sib.end(), o->id) - sib.begin());
}

bool Scene::Reparent(std::string_view id, std::string parent, int index, std::string* error) {
  SceneObject* o = Find(id);
  if (!o) return Fail(error, std::format("no object \"{}\"", id));
  if (!parent.empty() && !Find(parent)) return Fail(error, std::format("no parent object \"{}\"", parent));
  for (std::string p = parent; !p.empty(); p = Find(p)->parent)
    if (p == o->id) return Fail(error, std::format("cannot move \"{}\" under itself", id));
  auto& from = Siblings(o->parent);
  from.erase(std::remove(from.begin(), from.end(), o->id), from.end());
  o->parent = parent;
  auto& to = Siblings(parent);
  if (index < 0 || index > (int)to.size()) index = (int)to.size();
  to.insert(to.begin() + index, o->id);
  return true;
}

void* Scene::AddComponent(std::string_view id, const TypeInfo& type, std::string_view valueJson, std::string* error) {
  SceneObject* o = Find(id);
  if (!o) { Fail(error, std::format("no object \"{}\"", id)); return nullptr; }
  if (o->Find(type.name)) { Fail(error, std::format("\"{}\" already has a {}", o->name, type.name)); return nullptr; }
  if (!type.create) { Fail(error, std::format("{} cannot be created (no default constructor)", type.name)); return nullptr; }
  void* data = type.create();
  if (!valueJson.empty() && !Registry::FromJson(data, StructOf(type), valueJson, error)) {
    type.destroy(data);
    return nullptr;
  }
  o->components.push_back({&type, data});
  return data;
}

bool Scene::RemoveComponent(std::string_view id, std::string_view typeName) {
  SceneObject* o = Find(id);
  if (!o) return false;
  auto it = std::find_if(o->components.begin(), o->components.end(), [&](auto& c) { return c.type->name == typeName; });
  if (it == o->components.end()) return false;
  if (it->type->destroy) it->type->destroy(it->data);
  o->components.erase(it);
  return true;
}

std::string Scene::Snapshot(std::string_view id) const {
  const SceneObject* o = Find(id);
  if (!o) return "null";
  std::string s = std::format("{{\"id\":{},\"name\":{},\"parent\":{},\"index\":{},\"prefab\":{},", Q(o->id), Q(o->name), Q(o->parent),
                              IndexInParent(id), o->prefab ? "true" : "false");
  if (!o->prefabRef.empty()) s += "\"prefabRef\":" + Q(o->prefabRef) + ",";
  if (!o->prefabRoot.empty()) s += "\"prefabRoot\":" + Q(o->prefabRoot) + ",";
  if (!o->prefabUnresolved.empty()) s += "\"prefabUnresolved\":" + Q(o->prefabUnresolved) + ",";
  s += "\"components\":[";
  for (size_t i = 0; i < o->components.size(); ++i)
    s += std::format("{}{{\"type\":{},\"value\":{}}}", i ? "," : "", Q(o->components[i].type->name),
                     Registry::ToJson(o->components[i].data, StructOf(*o->components[i].type)));
  s += "],\"children\":[";
  for (size_t i = 0; i < o->children.size(); ++i) s += (i ? "," : "") + Snapshot(o->children[i]);
  return s + "]}";
}

namespace {
bool RestoreVal(Scene& scene, yyjson_val* v, std::string* error) {
  const char* id = yyjson_get_str(yyjson_obj_get(v, "id"));
  const char* name = yyjson_get_str(yyjson_obj_get(v, "name"));
  const char* parent = yyjson_get_str(yyjson_obj_get(v, "parent"));
  if (!id || !name) return Fail(error, "snapshot needs \"id\" and \"name\"");
  yyjson_val* idx = yyjson_obj_get(v, "index");
  SceneObject* o = scene.Create(id, name, parent ? parent : "", idx ? (int)yyjson_get_int(idx) : -1);
  if (!o) return Fail(error, std::format("cannot create \"{}\" (id taken or parent missing)", id));
  o->prefab = yyjson_get_bool(yyjson_obj_get(v, "prefab"));
  if (const char* r = yyjson_get_str(yyjson_obj_get(v, "prefabRef"))) o->prefabRef = r;
  if (const char* r = yyjson_get_str(yyjson_obj_get(v, "prefabRoot"))) o->prefabRoot = r;
  if (const char* r = yyjson_get_str(yyjson_obj_get(v, "prefabUnresolved"))) o->prefabUnresolved = r;
  size_t i, n;
  yyjson_val* c;
  yyjson_arr_foreach(yyjson_obj_get(v, "components"), i, n, c) {
    const char* type = yyjson_get_str(yyjson_obj_get(c, "type"));
    const TypeInfo* t = type ? Registry::Instance().Find(type) : nullptr;
    if (!t) return Fail(error, std::format("unknown component type \"{}\"", type ? type : "?"));
    char* value = yyjson_val_write(yyjson_obj_get(c, "value"), 0, nullptr);
    bool ok = scene.AddComponent(id, *t, value ? value : "", error) != nullptr;
    free(value);
    if (!ok) return false;
  }
  yyjson_arr_foreach(yyjson_obj_get(v, "children"), i, n, c) if (!RestoreVal(scene, c, error)) return false;
  return true;
}
}

bool Scene::Restore(std::string_view snapshotJson, std::string* error) {
  yyjson_doc* doc = yyjson_read(snapshotJson.data(), snapshotJson.size(), 0);
  if (!doc) return Fail(error, "invalid snapshot JSON");
  bool ok = RestoreVal(*this, yyjson_doc_get_root(doc), error);
  yyjson_doc_free(doc);
  return ok;
}

glm::mat4 Scene::LocalMatrix(std::string_view id) const {
  const SceneObject* o = Find(id);
  const Component* c = o ? const_cast<SceneObject*>(o)->Find("Transform") : nullptr;
  if (!c) return glm::mat4(1);
  const auto* t = static_cast<const Transform*>(c->data);
  return space::TRS(t->position, t->rotation, t->scale);
}

glm::mat4 Scene::WorldMatrix(std::string_view id) const {
  glm::mat4 m(1);
  for (const SceneObject* o = Find(id); o; o = o->parent.empty() ? nullptr : Find(o->parent)) m = LocalMatrix(o->id) * m;
  return m;
}

Vec3 Scene::WorldPosition(std::string_view id) const { return space::TransformPoint(WorldMatrix(id), {}); }

std::string Scene::Dump() const {
  std::string out;
  std::vector<std::string> stack(roots_.rbegin(), roots_.rend());
  while (!stack.empty()) {
    const SceneObject* o = Find(stack.back());
    stack.pop_back();
    out += std::format("{{\"id\":{},\"name\":{},\"parent\":{},\"components\":[", Q(o->id), Q(o->name), Q(o->parent));
    for (size_t i = 0; i < o->components.size(); ++i)
      out += std::format("{}{{\"type\":{},\"value\":{}}}", i ? "," : "", Q(o->components[i].type->name),
                         Registry::ToJson(o->components[i].data, StructOf(*o->components[i].type)));
    out += "]}\n";
    for (auto it = o->children.rbegin(); it != o->children.rend(); ++it) stack.push_back(*it);
  }
  return out;
}
}
