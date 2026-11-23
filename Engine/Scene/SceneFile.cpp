#include "Scene/SceneFile.h"
#include "Scene/Components.h"
#include <yyjson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>

namespace bdg {
namespace {
std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); }
bool Fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}

// Fields of a component that differ from a fresh instance, as a JSON object.
std::string ChangedFields(const Component& c) {
  std::unique_ptr<void, void (*)(void*)> fresh(c.type->create ? c.type->create() : nullptr, c.type->destroy ? c.type->destroy : [](void*) {});
  std::string out = "{";
  for (const TypeInfo* t = c.type; t; t = t->base)
    for (const FieldInfo& f : t->fields) {
      if (!(f.flags & kSave)) continue;
      std::string v = Registry::ToJson(static_cast<const char*>(c.data) + f.offset, f.value);
      if (fresh && v == Registry::ToJson(static_cast<const char*>(fresh.get()) + f.offset, f.value)) continue;
      out += (out.size() > 1 ? "," : "") + Q(f.name) + ":" + v;
    }
  return out + "}";
}

std::string Vec(const Vec3& v) { return Registry::ToJson(&v, reflect::ValueTypeOf<Vec3>()); }
}

std::string WriteSceneText(const Scene& scene, const std::string& sceneName) {
  std::string out = std::format("{{\"v\":1,\"defaults\":{},\"scene\":{},\"mode\":\"3d\"}}\n", Q(kSceneDefaults), Q(sceneName));
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    std::string line = "{\"id\":" + Q(o->id);
    if (o->name != o->id) line += ",\"name\":" + Q(o->name);
    if (!o->parent.empty()) line += ",\"parent\":" + Q("@" + o->parent);
    if (o->prefab) line += ",\"prefab\":true"; // marker until prefab assets exist (queue 6-1)
    for (const Component& c : o->components) {
      if (c.type->name == "Transform") { // promoted to pos / rot / scale, defaults left out
        const auto* t = static_cast<const Transform*>(c.data);
        if (!(t->position == Vec3{})) line += ",\"pos\":" + Vec(t->position);
        if (!(t->rotation == Vec3{})) line += ",\"rot\":" + Vec(t->rotation);
        if (!(t->scale == Vec3{1, 1, 1})) line += ",\"scale\":" + Vec(t->scale);
        continue;
      }
      line += "," + Q(c.type->name) + ":" + ChangedFields(c);
    }
    out += line + "}\n";
    for (auto it = o->children.rbegin(); it != o->children.rend(); ++it) stack.push_back(*it);
  }
  return out;
}

bool SaveSceneFile(const Scene& scene, const std::string& sceneName, const std::string& path, std::string* error) {
  std::string text = WriteSceneText(scene, sceneName);
  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::string tmp = path + ".tmp"; // write next to it, then swap: a crash mid-save never leaves half a scene
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return Fail(error, std::format("cannot write {}", path));
  bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  ok = std::fclose(f) == 0 && ok;
  if (!ok) { std::filesystem::remove(tmp, ec); return Fail(error, std::format("cannot write {}", path)); }
  std::filesystem::rename(tmp, p, ec);
  if (ec) { std::filesystem::remove(tmp, ec); return Fail(error, std::format("cannot replace {}", path)); }
  return true;
}

bool ReadSceneText(Scene& scene, std::string_view text, std::string* sceneName, std::vector<std::string>* warnings,
                   std::string* error) {
  if (scene.ObjectCount() != 0) return Fail(error, "scene must be empty before loading");
  auto warn = [&](std::string w) { if (warnings) warnings->push_back(std::move(w)); };
  size_t pos = 0, lineNo = 0;
  bool header = false;
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
    if (!header) { // {"v":1,"defaults":"1.0","scene":...}
      if (yyjson_get_int(yyjson_obj_get(o, "v")) != 1) return Fail(error, "line 1: not a scene file (header needs \"v\":1)");
      const char* defaults = yyjson_get_str(yyjson_obj_get(o, "defaults"));
      if (!defaults || std::strcmp(defaults, kSceneDefaults) != 0)
        warn(std::format("defaults set \"{}\" is not \"{}\": values left out may differ", defaults ? defaults : "?", kSceneDefaults));
      if (sceneName) {
        const char* n = yyjson_get_str(yyjson_obj_get(o, "scene"));
        *sceneName = n ? n : "";
      }
      header = true;
      continue;
    }
    const char* id = yyjson_get_str(yyjson_obj_get(o, "id"));
    if (!id || !*id) return Fail(error, std::format("line {}: every object needs an \"id\"", lineNo));
    const char* name = yyjson_get_str(yyjson_obj_get(o, "name"));
    std::string parent;
    if (const char* p = yyjson_get_str(yyjson_obj_get(o, "parent"))) parent = p[0] == '@' ? p + 1 : p;
    SceneObject* obj = scene.Create(id, name ? name : id, parent);
    if (!obj)
      return Fail(error, std::format("line {}: cannot create \"{}\" (id used twice, or parent \"{}\" not above it)", lineNo, id, parent));
    obj->prefab = yyjson_get_bool(yyjson_obj_get(o, "prefab"));
    auto* t = static_cast<Transform*>(scene.AddComponent(id, Transform::StaticType()));
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(o, i, n, k, v) {
      std::string key = yyjson_get_str(k);
      if (key == "id" || key == "name" || key == "parent" || key == "prefab") continue;
      char* raw = yyjson_val_write(v, 0, nullptr);
      std::string json = raw ? raw : "";
      std::free(raw);
      std::string err;
      if (key == "pos" || key == "rot" || key == "scale") {
        Vec3& dst = key == "pos" ? t->position : key == "rot" ? t->rotation : t->scale;
        if (!Registry::FromJson(&dst, reflect::ValueTypeOf<Vec3>(), json, &err)) warn(std::format("line {}: {}: {}", lineNo, key, err));
        continue;
      }
      if (key.empty() || !(key[0] >= 'A' && key[0] <= 'Z')) { warn(std::format("line {}: unknown key \"{}\" ignored", lineNo, key)); continue; }
      const TypeInfo* type = Registry::Instance().Find(key);
      if (!type) { warn(std::format("line {}: unknown component \"{}\" skipped (its data stays in the file only until saved)", lineNo, key)); continue; }
      if (!scene.AddComponent(id, *type, json, &err)) warn(std::format("line {}: {}: {}", lineNo, key, err));
    }
  }
  if (!header) return Fail(error, "empty scene file (no header line)");
  return true;
}

bool LoadSceneFile(Scene& scene, const std::string& path, std::string* sceneName, std::vector<std::string>* warnings,
                   std::string* error) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return Fail(error, std::format("cannot open {}", path));
  std::string text;
  char buf[8192];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  return ReadSceneText(scene, text, sceneName, warnings, error);
}
}
