#include "Scene/SceneFile.h"
#include "Core/Utf8Path.h"
#include "Scene/Components.h"
#include "Scene/Prefab.h"
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <set>

namespace bdg {
namespace {
std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); }
bool Fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}

// Fields of a component that differ from `base` (a fresh instance when nullptr), as a JSON object.
std::string ChangedFields(const Component& c, const void* base = nullptr) {
  std::unique_ptr<void, void (*)(void*)> fresh(!base && c.type->create ? c.type->create() : nullptr, c.type->destroy ? c.type->destroy : [](void*) {});
  if (!base) base = fresh.get();
  std::string out = "{";
  for (const TypeInfo* t = c.type; t; t = t->base)
    for (const FieldInfo& f : t->fields) {
      if (!(f.flags & kSave)) continue;
      std::string v = Registry::ToJson(static_cast<const char*>(c.data) + f.offset, f.value);
      if (base && v == Registry::ToJson(static_cast<const char*>(base) + f.offset, f.value)) continue;
      out += (out.size() > 1 ? "," : "") + Q(f.name) + ":" + v;
    }
  return out + "}";
}

std::string Vec(const Vec3& v) { return Registry::ToJson(&v, reflect::ValueTypeOf<Vec3>()); }

// The keys an object line holds for `o`: plain (every value that is not the default) or, with a reference object
// (the same object as its prefab makes it), only what differs: name, pos / rot / scale, changed component fields,
// components the prefab does not have, and "-Type" for prefab components this object no longer has.
std::string ObjectKeys(const SceneObject& o, const SceneObject* ref) {
  std::string s;
  if (ref && o.name != ref->name) s += ",\"name\":" + Q(o.name);
  const Transform* rt = nullptr;
  if (ref)
    for (const Component& c : ref->components)
      if (c.type->name == "Transform") rt = static_cast<const Transform*>(c.data);
  for (const Component& c : o.components) {
    if (c.type->name == "Transform") { // pos / rot / scale, left out when default (plain) or as in the prefab
      const auto* t = static_cast<const Transform*>(c.data);
      const Transform def;
      const Transform& base = rt ? *rt : def;
      if (!(t->position == base.position)) s += ",\"pos\":" + Vec(t->position);
      if (!(t->rotation == base.rotation)) s += ",\"rot\":" + Vec(t->rotation);
      if (!(t->scale == base.scale)) s += ",\"scale\":" + Vec(t->scale);
      continue;
    }
    const Component* rc = nullptr;
    if (ref)
      for (const Component& x : ref->components)
        if (x.type == c.type) rc = &x;
    if (rc) {
      const std::string diff = ChangedFields(c, rc->data);
      if (diff != "{}") s += "," + Q(c.type->name) + ":" + diff;
    } else {
      s += "," + Q(c.type->name) + ":" + ChangedFields(c);
    }
  }
  if (ref)
    for (const Component& rc : ref->components) {
      bool kept = false;
      for (const Component& c : o.components) kept = kept || c.type == rc.type;
      if (!kept) s += "," + Q("-" + rc.type->name) + ":true";
    }
  return s;
}

void Warn(std::vector<std::string>* warnings, std::string w) {
  if (warnings) warnings->push_back(std::move(w));
}

// Applies an override object (the keys of an instance line, or of one of its "/path" entries) to an existing object.
void ApplyOverride(Scene& scene, const std::string& id, yyjson_val* o, int lineNo, std::vector<std::string>* warnings) {
  SceneObject* obj = scene.Find(id);
  if (!obj) {
    Warn(warnings, std::format("line {}: the prefab has no object \"{}\" any more: its overrides are dropped", lineNo, id));
    return;
  }
  size_t i, n;
  yyjson_val *k, *v;
  yyjson_obj_foreach(o, i, n, k, v) {
    std::string key = yyjson_get_str(k);
    if (key == "id" || key == "parent" || key == "prefab" || key.starts_with("/")) continue;
    if (key == "name") {
      if (const char* s = yyjson_get_str(v)) obj->name = s;
      continue;
    }
    char* raw = yyjson_val_write(v, 0, nullptr);
    std::string json = raw ? raw : "";
    std::free(raw);
    std::string err;
    if (key == "pos" || key == "rot" || key == "scale") {
      Component* tc = obj->Find("Transform");
      auto* t = tc ? static_cast<Transform*>(tc->data) : static_cast<Transform*>(scene.AddComponent(id, Transform::StaticType()));
      Vec3& dst = key == "pos" ? t->position : key == "rot" ? t->rotation : t->scale;
      if (!Registry::FromJson(&dst, reflect::ValueTypeOf<Vec3>(), json, &err)) Warn(warnings, std::format("line {}: {}: {}", lineNo, key, err));
      continue;
    }
    if (key.size() > 1 && key[0] == '-') { // a prefab component this instance removed
      scene.RemoveComponent(id, key.substr(1));
      continue;
    }
    if (key.empty() || !(key[0] >= 'A' && key[0] <= 'Z')) { Warn(warnings, std::format("line {}: unknown key \"{}\" ignored", lineNo, key)); continue; }
    const TypeInfo* type = Registry::Instance().Find(key);
    if (!type) { Warn(warnings, std::format("line {}: unknown component \"{}\" skipped (its data stays in the file only until saved)", lineNo, key)); continue; }
    if (Component* c = obj->Find(key)) { // the prefab has it: only the listed fields change
      ValueType vt;
      vt.type = FieldType::Struct;
      vt.structType = c->type;
      if (!Registry::FromJson(c->data, vt, json, &err)) Warn(warnings, std::format("line {}: {}: {}", lineNo, key, err));
    } else if (!scene.AddComponent(id, *type, json, &err)) {
      Warn(warnings, std::format("line {}: {}: {}", lineNo, key, err));
    }
  }
}

// Header line: {"v":1,"defaults":"1.0","scene":"Main"} or {"v":1,"defaults":"1.0","prefab":"Enemy"}.
bool ReadHeader(yyjson_val* o, std::string* sceneName, std::vector<std::string>* warnings, std::string* error) {
  if (yyjson_get_int(yyjson_obj_get(o, "v")) != 1) return Fail(error, "line 1: not a scene file (header needs \"v\":1)");
  const char* defaults = yyjson_get_str(yyjson_obj_get(o, "defaults"));
  if (!defaults || std::strcmp(defaults, kSceneDefaults) != 0)
    Warn(warnings, std::format("defaults set \"{}\" is not \"{}\": values left out may differ", defaults ? defaults : "?", kSceneDefaults));
  if (sceneName) {
    const char* n = yyjson_get_str(yyjson_obj_get(o, "scene"));
    if (!n) n = yyjson_get_str(yyjson_obj_get(o, "prefab"));
    *sceneName = n ? n : "";
  }
  return true;
}

bool ReadLines(Scene& scene, std::string_view text, bool wantHeader, std::string* sceneName, std::vector<std::string>* warnings, std::string* error) {
  size_t pos = 0, lineNo = 0;
  bool header = !wantHeader;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    std::string_view line = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
    pos = end == std::string_view::npos ? text.size() : end + 1;
    ++lineNo;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line.empty()) continue;
    std::unique_ptr<yyjson_doc, void (*)(yyjson_doc*)> doc(yyjson_read(line.data(), line.size(), 0), yyjson_doc_free);
    yyjson_val* o = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    if (!o || !yyjson_is_obj(o)) return Fail(error, std::format("line {}: not a JSON object", lineNo));
    if (!header) {
      if (!ReadHeader(o, sceneName, warnings, error)) return false;
      header = true;
      continue;
    }
    const char* id = yyjson_get_str(yyjson_obj_get(o, "id"));
    if (!id || !*id) return Fail(error, std::format("line {}: every object needs an \"id\"", lineNo));
    const char* name = yyjson_get_str(yyjson_obj_get(o, "name"));
    std::string parent;
    if (const char* p = yyjson_get_str(yyjson_obj_get(o, "parent"))) parent = p[0] == '@' ? p + 1 : p;
    if (const char* ref = yyjson_get_str(yyjson_obj_get(o, "prefab"))) { // a prefab instance: the prefab, then the line's overrides
      std::string err;
      if (InstantiatePrefab(scene, ref, id, parent, -1, &err)) {
        ApplyOverride(scene, id, o, (int)lineNo, warnings);
        size_t i, n;
        yyjson_val *k, *v;
        yyjson_obj_foreach(o, i, n, k, v) {
          const char* key = yyjson_get_str(k);
          if (key[0] == '/' && yyjson_is_obj(v)) ApplyOverride(scene, std::string(id) + key, v, (int)lineNo, warnings);
        }
        continue;
      }
      if (scene.Find(id)) return Fail(error, std::format("line {}: id \"{}\" is used twice", lineNo, id));
      Warn(warnings, std::format("line {}: prefab {}: {} (an empty object stands in for it)", lineNo, ref, err));
      SceneObject* stand = scene.Create(id, name ? name : id, parent);
      if (!stand) return Fail(error, std::format("line {}: cannot create \"{}\" (parent \"{}\" not above it)", lineNo, id, parent));
      stand->prefabUnresolved = std::string(line); // saved back as it was: the link and its overrides are not lost
      scene.AddComponent(id, Transform::StaticType());
      continue;
    }
    SceneObject* obj = scene.Create(id, name ? name : id, parent);
    if (!obj)
      return Fail(error, std::format("line {}: cannot create \"{}\" (id used twice, or parent \"{}\" not above it)", lineNo, id, parent));
    obj->prefab = yyjson_get_bool(yyjson_obj_get(o, "prefab"));
    scene.AddComponent(id, Transform::StaticType());
    ApplyOverride(scene, id, o, (int)lineNo, warnings); // a plain object: every key on a fresh object
  }
  if (!header) return Fail(error, "empty scene file (no header line)");
  return true;
}
}

std::string WriteObjectLines(const Scene& scene, const std::vector<std::string>& roots, const std::function<std::string(const std::string&)>& localId,
                             const std::string& plainRoot) {
  auto L = [&](const std::string& id) { return localId ? localId(id) : id; };
  std::string out;
  // Objects an instance line already covers (its prefab's objects), with their instance root.
  std::set<std::string> covered;
  std::vector<std::string> stack(roots.rbegin(), roots.rend());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    for (auto it = o->children.rbegin(); it != o->children.rend(); ++it) stack.push_back(*it);
    if (covered.count(o->id)) continue; // written as a "/path" override of its instance
    std::string line = "{\"id\":" + Q(L(o->id));
    const std::string parentKey = !o->parent.empty() && o->id != plainRoot ? ",\"parent\":" + Q("@" + L(o->parent)) : "";
    if (!o->prefabRef.empty() && o->id != plainRoot) { // an instance: the prefab and what differs from it
      line += parentKey;
      Scene ref;
      std::string err;
      if (InstantiatePrefab(ref, o->prefabRef, o->id, "", -1, &err)) {
        line += ",\"prefab\":" + Q(o->prefabRef) + ObjectKeys(*o, ref.Find(o->id));
        std::vector<std::string> inner(o->children.rbegin(), o->children.rend());
        while (!inner.empty()) { // the prefab's objects below the root: their differences go into this line
          const SceneObject* c = scene.Find(inner.back());
          inner.pop_back();
          const SceneObject* rc = c ? ref.Find(c->id) : nullptr;
          if (!rc) continue; // added in the scene: written as its own line
          covered.insert(c->id);
          inner.insert(inner.end(), c->children.rbegin(), c->children.rend());
          const std::string keys = ObjectKeys(*c, rc);
          if (!keys.empty()) line += "," + Q(c->id.substr(o->id.size())) + ":{" + keys.substr(1) + "}";
        }
        out += line + "}\n";
        continue;
      }
      // the prefab cannot be read: keep the object as it is (plain) so nothing is lost
    }
    if (!o->prefabUnresolved.empty()) { // the prefab could not be read when this was loaded: its line as it was
      std::unique_ptr<yyjson_doc, void (*)(yyjson_doc*)> doc(yyjson_read(o->prefabUnresolved.data(), o->prefabUnresolved.size(), 0), yyjson_doc_free);
      yyjson_val* v = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
      line += parentKey;
      size_t i, n;
      yyjson_val *k, *x;
      yyjson_obj_foreach(v, i, n, k, x) {
        const std::string key = yyjson_get_str(k);
        if (key == "id" || key == "parent") continue;
        char* raw = yyjson_val_write(x, 0, nullptr);
        line += "," + Q(key) + ":" + (raw ? raw : "null");
        std::free(raw);
      }
      out += line + "}\n";
      continue;
    }
    if (o->name != L(o->id)) line += ",\"name\":" + Q(o->name);
    line += parentKey;
    if (o->prefab && o->prefabRef.empty()) line += ",\"prefab\":true"; // the old marker of the sample scene
    out += line + ObjectKeys(*o, nullptr) + "}\n";
  }
  return out;
}

std::string WriteSceneText(const Scene& scene, const std::string& sceneName) {
  return std::format("{{\"v\":1,\"defaults\":{},\"scene\":{},\"mode\":\"3d\"}}\n", Q(kSceneDefaults), Q(sceneName)) +
         WriteObjectLines(scene, scene.Roots(), nullptr, "");
}

bool SaveSceneFile(const Scene& scene, const std::string& sceneName, const std::string& path, std::string* error) {
  return SaveTextFile(path, WriteSceneText(scene, sceneName), error);
}

bool SaveTextFile(const std::string& path, const std::string& text, std::string* error) {
  std::error_code ec;
  const std::filesystem::path p = PathU8(path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::string tmp = path + ".tmp"; // write next to it, then swap: a crash mid-save never leaves half a file
  std::FILE* f = OpenU8(tmp, "wb");
  if (!f) return Fail(error, std::format("cannot write {}", path));
  bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  ok = std::fclose(f) == 0 && ok;
  if (!ok) { std::filesystem::remove(PathU8(tmp), ec); return Fail(error, std::format("cannot write {}", path)); }
  std::filesystem::rename(PathU8(tmp), p, ec);
  if (ec) { std::filesystem::remove(PathU8(tmp), ec); return Fail(error, std::format("cannot replace {}", path)); }
  return true;
}

bool ReadSceneText(Scene& scene, std::string_view text, std::string* sceneName, std::vector<std::string>* warnings,
                   std::string* error) {
  if (scene.ObjectCount() != 0) return Fail(error, "scene must be empty before loading");
  return ReadLines(scene, text, true, sceneName, warnings, error);
}

bool ReadObjectLines(Scene& scene, std::string_view text, std::vector<std::string>* warnings, std::string* error) {
  return ReadLines(scene, text, false, nullptr, warnings, error);
}

bool LoadSceneFile(Scene& scene, const std::string& path, std::string* sceneName, std::vector<std::string>* warnings,
                   std::string* error) {
  std::FILE* f = OpenU8(path, "rb");
  if (!f) return Fail(error, std::format("cannot open {}", path));
  std::string text;
  char buf[8192];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  return ReadSceneText(scene, text, sceneName, warnings, error);
}
}
