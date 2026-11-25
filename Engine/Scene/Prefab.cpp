#include "Scene/Prefab.h"
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>

namespace bdg {
namespace fs = std::filesystem;
namespace {
const AssetDatabase* g_db = nullptr;
PrefabLoader g_loader;
struct Cached { std::string text, path; };
std::map<std::string, Cached> g_cache;      // canonical -> what instances are built from
std::map<std::string, std::string> g_alias; // any reference -> canonical
int g_depth = 0;                            // nested expansion (a prefab that contains itself stops here)

std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); }
bool Fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}
std::string ReadText(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return in ? std::string((std::istreambuf_iterator<char>(in)), {}) : std::string();
}
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

// Reads a prefab from its source (not the cache).
bool Load(const std::string& ref, std::string& canonical, std::string& text, std::string& path, std::string* error) {
  if (g_loader) return g_loader(ref, canonical, text, path) ? true : Fail(error, "no prefab \"" + ref + "\"");
  if (!g_db) return Fail(error, "no project is open, so prefab \"" + ref + "\" cannot be read");
  const AssetRecord* r = ref.starts_with("guid:") ? g_db->ByGuid(ref.substr(5)) : g_db->ByPath(ref);
  if (!r) { // a file name or a name without the extension
    const std::string want = Lower(ref);
    for (const AssetRecord* a : g_db->All()) {
      if (a->importer != "prefab") continue;
      const std::string p = Lower(a->path), file = p.substr(p.find_last_of('/') + 1);
      if (file == want || file == want + ".prefab" || p == want || p == want + ".prefab") { r = a; break; }
    }
  }
  if (!r) return Fail(error, "no prefab \"" + ref + "\" in the project");
  canonical = "guid:" + r->guid;
  path = r->path;
  text = ReadText(PathU8(g_db->Root()) / PathU8(r->path));
  if (text.empty()) return Fail(error, "cannot read " + r->path);
  return true;
}

// Prefab references a prefab text names (nested instances).
std::vector<std::string> NestedRefs(const std::string& text) {
  std::vector<std::string> out;
  for (size_t at = 0; (at = text.find("\"prefab\":\"", at)) != std::string::npos;) {
    at += 10;
    const size_t end = text.find('"', at);
    if (end == std::string::npos) break;
    out.push_back(text.substr(at, end - at));
  }
  return out;
}

bool DependsOn(const std::string& canonical, const std::string& target, int depth = 0) {
  if (canonical == target) return true;
  if (depth > 16) return false;
  auto it = g_cache.find(canonical);
  if (it == g_cache.end()) return false;
  for (const std::string& ref : NestedRefs(it->second.text)) {
    std::string c;
    if (ResolvePrefab(ref, c) && DependsOn(c, target, depth + 1)) return true;
  }
  return false;
}

const Component* FindComp(const SceneObject& o, const TypeInfo* t) {
  for (const Component& c : o.components)
    if (c.type == t) return &c;
  return nullptr;
}

ValueType StructOf(const TypeInfo& t) {
  ValueType vt;
  vt.type = FieldType::Struct;
  vt.structType = &t;
  return vt;
}

// Objects of the instance below `rootId`, the root first, hierarchy order.
std::vector<const SceneObject*> Subtree(const Scene& scene, const std::string& rootId) {
  std::vector<const SceneObject*> out;
  std::vector<std::string> stack{rootId};
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    out.push_back(o);
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
  }
  return out;
}
}

void SetPrefabAssets(const AssetDatabase* db) {
  g_db = db;
  ClearPrefabCache();
}
void ForgetPrefabAssets(const AssetDatabase* db) {
  if (g_db != db) return;
  g_db = nullptr;
  ClearPrefabCache();
}
void SetPrefabLoader(PrefabLoader loader) {
  g_loader = std::move(loader);
  ClearPrefabCache();
}
void ClearPrefabCache() {
  g_cache.clear();
  g_alias.clear();
}

bool ResolvePrefab(const std::string& ref, std::string& canonical, std::string* text, std::string* path, std::string* error) {
  auto a = g_alias.find(ref);
  if (a == g_alias.end()) {
    std::string c, t, p;
    if (!Load(ref, c, t, p, error)) return false;
    if (!g_cache.count(c)) g_cache[c] = {t, p};
    a = g_alias.emplace(ref, c).first;
    g_alias[c] = c;
  }
  canonical = a->second;
  const Cached& cached = g_cache[canonical];
  if (text) *text = cached.text;
  if (path) *path = cached.path;
  return true;
}

bool InstantiatePrefab(Scene& scene, const std::string& ref, const std::string& id, const std::string& parent, int index, std::string* error) {
  if (id.empty()) return Fail(error, "an instance needs an id");
  if (scene.Find(id)) return Fail(error, std::format("id \"{}\" is already used", id));
  if (!parent.empty() && !scene.Find(parent)) return Fail(error, std::format("no parent object \"{}\"", parent));
  if (g_depth > 16) return Fail(error, "prefabs nested more than 16 deep (does a prefab contain itself?)");
  std::string canonical, text;
  if (!ResolvePrefab(ref, canonical, &text, nullptr, error)) return false;
  Scene temp;
  std::vector<std::string> warnings;
  std::string err;
  ++g_depth;
  const bool read = ReadSceneText(temp, text, nullptr, &warnings, &err);
  --g_depth;
  if (!read) return Fail(error, std::format("prefab {}: {}", ref, err));
  for (const std::string& w : warnings) LogWarn("prefab {}: {}", ref, w);
  if (temp.Roots().empty()) return Fail(error, std::format("prefab {} has no objects", ref));
  if (temp.Roots().size() > 1) LogWarn("prefab {}: only its first root object is used", ref);
  const std::string tempRoot = temp.Roots()[0];
  auto map = [&](const std::string& tid) { return tid == tempRoot ? id : id + "/" + tid; };
  for (const SceneObject* o : Subtree(temp, tempRoot)) {
    const bool root = o->id == tempRoot;
    SceneObject* n = scene.Create(map(o->id), o->name, root ? parent : map(o->parent), root ? index : -1);
    if (!n) {
      if (scene.Find(id)) scene.Destroy(id);
      return Fail(error, std::format("cannot create \"{}\"", map(o->id)));
    }
    n->prefabRef = root ? canonical : o->prefabRef; // nested prefab roots keep their own prefab
    n->prefabRoot = id;
    n->prefab = root || !o->prefabRef.empty();
    for (const Component& c : o->components) scene.AddComponent(n->id, *c.type, Registry::ToJson(c.data, StructOf(*c.type)));
  }
  return true;
}

std::string InstanceRootOf(const Scene& scene, const std::string& id) {
  const SceneObject* o = scene.Find(id);
  if (!o || o->prefabRoot.empty()) return "";
  const SceneObject* r = scene.Find(o->prefabRoot);
  return r && IsInstanceRoot(*r) ? r->id : "";
}

std::vector<PrefabOverride> PrefabOverrides(const Scene& scene, const std::string& rootId) {
  std::vector<PrefabOverride> out;
  const SceneObject* root = scene.Find(rootId);
  if (!root || !IsInstanceRoot(*root)) return out;
  Scene ref;
  if (!InstantiatePrefab(ref, root->prefabRef, rootId, "", -1)) return out;
  for (const SceneObject* o : Subtree(scene, rootId)) {
    const SceneObject* r = ref.Find(o->id);
    if (!r) { // added under the instance: one override for the whole object (its children come with it)
      if (ref.Find(o->parent)) out.push_back({o->id, "+object"});
      continue;
    }
    if (o->name != r->name) out.push_back({o->id, "name"});
    for (const Component& c : o->components) {
      const Component* rc = FindComp(*r, c.type);
      if (!rc) { out.push_back({o->id, "+" + c.type->name}); continue; }
      for (const TypeInfo* t = c.type; t; t = t->base)
        for (const FieldInfo& f : t->fields) {
          if (!(f.flags & kSave)) continue;
          if (o->id == rootId && c.type->name == "Transform" && (f.name == "position" || f.name == "rotation")) continue;
          if (Registry::ToJson(static_cast<const char*>(c.data) + f.offset, f.value) !=
              Registry::ToJson(static_cast<const char*>(rc->data) + f.offset, f.value))
            out.push_back({o->id, c.type->name + "." + f.name});
        }
    }
    for (const Component& rc : r->components)
      if (!FindComp(*o, rc.type)) out.push_back({o->id, "-" + rc.type->name});
  }
  return out;
}

std::vector<Command> RevertPrefabCommands(const Scene& scene, const std::string& object, const std::string& path) {
  std::vector<Command> out;
  const std::string rootId = InstanceRootOf(scene, object);
  const SceneObject* root = rootId.empty() ? nullptr : scene.Find(rootId);
  if (!root) return out;
  Scene ref;
  if (!InstantiatePrefab(ref, root->prefabRef, rootId, "", -1)) return out;
  for (const PrefabOverride& ov : PrefabOverrides(scene, rootId)) {
    if (!path.empty() && (ov.object != object || ov.path != path)) continue;
    const SceneObject* r = ref.Find(ov.object);
    Command c;
    c.target = ov.object;
    if (ov.path == "+object") {
      c.name = "del";
    } else if (ov.path == "name") {
      c.name = "rename";
      c.args = std::format("{{\"name\":{}}}", Q(r->name));
    } else if (ov.path[0] == '+') {
      c.name = "rmcomp";
      c.args = std::format("{{\"type\":{}}}", Q(ov.path.substr(1)));
    } else if (ov.path[0] == '-') {
      const Component* rc = nullptr;
      for (const Component& x : r->components)
        if (x.type->name == ov.path.substr(1)) rc = &x;
      if (!rc) continue;
      c.name = "addcomp";
      c.args = std::format("{{\"type\":{},\"value\":{}}}", Q(rc->type->name), Registry::ToJson(rc->data, StructOf(*rc->type)));
    } else {
      const size_t dot = ov.path.find('.');
      const SceneObject* rr = r;
      const Component* rc = nullptr;
      for (const Component& x : rr->components)
        if (x.type->name == ov.path.substr(0, dot)) rc = &x;
      std::string value;
      if (!rc || !Registry::Get(rc->data, *rc->type, ov.path.substr(dot + 1), value, nullptr)) continue;
      c.name = "set";
      c.args = std::format("{{\"path\":{},\"value\":{}}}", Q(ov.path), value);
    }
    out.push_back(c);
  }
  return out;
}

std::string WritePrefabText(const Scene& scene, const std::string& rootId) {
  const SceneObject* root = scene.Find(rootId);
  if (!root) return "";
  auto local = [&](const std::string& id) { return id == rootId ? std::string("root") : id.starts_with(rootId + "/") ? id.substr(rootId.size() + 1) : id; };
  std::string lines = WriteObjectLines(scene, {rootId}, local, rootId);
  // the root's own place in the scene stays out of the prefab (Unity does the same)
  const size_t end = lines.find('\n');
  std::string first = lines.substr(0, end);
  for (const char* key : {",\"pos\":", ",\"rot\":"})
    if (size_t at = first.find(key); at != std::string::npos) {
      const size_t close = first.find(']', at);
      first.erase(at, close - at + 1);
    }
  return std::format("{{\"v\":1,\"defaults\":{},\"prefab\":{}}}\n", Q(kSceneDefaults), Q(root->name)) + first + lines.substr(end);
}

bool SavePrefabFile(const std::string& canonical, const std::string& text, std::string* error) {
  std::string c, path;
  if (!ResolvePrefab(canonical, c, nullptr, &path, error)) return false;
  if (!g_db) return Fail(error, "no project is open");
  const fs::path full = PathU8(g_db->Root()) / PathU8(path);
  const std::u8string u = full.u8string();
  return SaveTextFile(std::string(u.begin(), u.end()), text, error);
}

int ReloadPrefab(Scene& scene, const std::string& ref, const std::string* newText) {
  std::string canonical, oldText, path;
  if (!ResolvePrefab(ref, canonical, &oldText, &path)) return 0;
  std::string text;
  if (newText) {
    text = *newText;
  } else {
    std::string c, p, err;
    if (!Load(canonical, c, text, p, &err)) { LogWarn("prefab {}: {}", ref, err); return 0; }
  }
  if (text == oldText) return 0; // e.g. the file watcher after our own save
  // 1. every outer instance that uses it, as lines made against the old prefab (= its overrides)
  struct Saved { std::string id, parent, lines; int index; };
  std::vector<Saved> saved;
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    if (IsInstanceRoot(*o) && DependsOn(o->prefabRef, canonical)) {
      saved.push_back({o->id, o->parent, WriteObjectLines(scene, {o->id}, nullptr, ""), scene.IndexInParent(o->id)});
      continue; // instances inside it come back with it
    }
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
  }
  // 2. the new prefab, then the instances rebuilt from it plus their overrides
  g_cache[canonical] = {text, path};
  for (const Saved& s : saved) {
    scene.Destroy(s.id);
    std::vector<std::string> warnings;
    std::string err;
    if (!ReadObjectLines(scene, s.lines, &warnings, &err)) LogWarn("prefab {}: rebuilding {}: {}", path, s.id, err);
    for (const std::string& w : warnings) LogWarn("prefab {}: {}: {}", path, s.id, w);
    if (scene.Find(s.id)) scene.Reparent(s.id, s.parent, s.index);
  }
  if (!saved.empty()) LogInfo("prefab: {} changed: {} instance(s) rebuilt", path, saved.size());
  return (int)saved.size();
}
}
