#include "Scripting/ScriptApi.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Scripting/DataTable.h"
#include <yyjson.h>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>

namespace bdg {
namespace {
std::string Q(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += std::format("\\u{:04x}", (unsigned)(unsigned char)c);
    else o += c;
  }
  return o + "\"";
}
std::string Ok(std::string_view valueJson = "null") { return std::format(R"({{"ok":true,"value":{}}})", valueJson); }
std::string Fail(std::string_view why) { return std::format(R"({{"ok":false,"error":{}}})", Q(why)); }

std::string Json(yyjson_val* v) {
  if (!v) return "null";
  size_t len = 0;
  char* s = yyjson_val_write(v, 0, &len);
  std::string r = s ? std::string(s, len) : "null";
  std::free(s);
  return r;
}
std::string Str(yyjson_val* obj, const char* key) {
  const char* s = yyjson_get_str(yyjson_obj_get(obj, key));
  return s ? s : "";
}

const SceneObject* FindByName(const Scene& scene, std::string_view name) {
  std::function<const SceneObject*(const std::vector<std::string>&)> walk = [&](const std::vector<std::string>& ids) -> const SceneObject* {
    for (const std::string& id : ids)
      if (const SceneObject* o = scene.Find(id)) {
        if (o->name == name) return o;
        if (const SceneObject* c = walk(o->children)) return c;
      }
    return nullptr;
  };
  return walk(scene.Roots());
}

// Ids of an object's children (deep: the whole subtree), in hierarchy order.
void ChildIds(const Scene& scene, const SceneObject& o, bool deep, std::vector<std::string>& out) {
  for (const std::string& id : o.children)
    if (const SceneObject* c = scene.Find(id)) {
      out.push_back(id);
      if (deep) ChildIds(scene, *c, deep, out);
    }
}

std::string IdList(const std::vector<std::string>& ids) {
  std::string s = "[";
  for (size_t i = 0; i < ids.size(); ++i) s += (i ? "," : "") + Q(ids[i]);
  return s + "]";
}

// A snapshot (Scene::Snapshot) made into a new copy: every id replaced by a fresh one, parent ids followed.
void Renumber(Scene& scene, yyjson_mut_doc* d, yyjson_mut_val* o, const std::string& parent) {
  const std::string id = scene.NewId();
  yyjson_mut_obj_put(o, yyjson_mut_strcpy(d, "id"), yyjson_mut_strcpy(d, id.c_str()));
  yyjson_mut_obj_put(o, yyjson_mut_strcpy(d, "parent"), yyjson_mut_strcpy(d, parent.c_str()));
  yyjson_mut_obj_put(o, yyjson_mut_strcpy(d, "prefab"), yyjson_mut_false(d));
  yyjson_mut_val* kids = yyjson_mut_obj_get(o, "children");
  size_t i, n;
  yyjson_mut_val* c;
  yyjson_mut_arr_foreach(kids, i, n, c) Renumber(scene, d, c, id);
}

// Sets Transform.<field> of a snapshot object to a JSON array value (adds a Transform when it has none).
void PutTransform(yyjson_mut_doc* d, yyjson_mut_val* o, const char* field, yyjson_val* value) {
  yyjson_mut_val* comps = yyjson_mut_obj_get(o, "components");
  size_t i, n;
  yyjson_mut_val* c;
  yyjson_mut_val* t = nullptr;
  yyjson_mut_arr_foreach(comps, i, n, c) {
    const char* type = yyjson_mut_get_str(yyjson_mut_obj_get(c, "type"));
    if (type && std::string_view(type) == "Transform") t = c;
  }
  if (!t) {
    t = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, t, "type", "Transform");
    yyjson_mut_obj_add_val(d, t, "value", yyjson_mut_obj(d));
    yyjson_mut_arr_insert(comps, t, 0);
  }
  yyjson_mut_val* v = yyjson_mut_obj_get(t, "value");
  if (!yyjson_mut_is_obj(v)) {
    v = yyjson_mut_obj(d);
    yyjson_mut_obj_put(t, yyjson_mut_strcpy(d, "value"), v);
  }
  yyjson_mut_obj_put(v, yyjson_mut_strcpy(d, field), yyjson_val_mut_copy(d, value));
}
} // namespace

std::string HandleScriptRequest(Scene& scene, CommandBus& bus, std::string_view requestJson) {
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(requestJson.data(), requestJson.size(), 0), yyjson_doc_free);
  yyjson_val* r = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
  if (!yyjson_is_obj(r)) return Fail("the request is not a JSON object");
  const std::string op = Str(r, "op"), target = Str(r, "target");
  auto run = [&](const char* name, std::string args) {
    Command c;
    c.name = name;
    c.target = target;
    c.args = std::move(args);
    c.source = "script";
    std::string err;
    return bus.Execute(c, &err) ? std::string() : (err.empty() ? std::string("command failed") : err);
  };

  if (op == "log") {
    const std::string level = Str(r, "level"), text = Str(r, "text");
    if (level == "error") LogError("{}", text);
    else if (level == "warn") LogWarn("{}", text);
    else LogInfo("{}", text);
    return Ok();
  }
  if (op == "find") {
    const std::string name = Str(r, "name");
    if (const SceneObject* o = scene.Find(name)) return Ok(Q(o->id));
    const SceneObject* o = FindByName(scene, name);
    return Ok(o ? Q(o->id) : "null");
  }
  if (op == "table") { // a data table, whole (the script caches it until play stops)
    DataTable t;
    std::string err;
    std::vector<std::string> problems;
    if (!LoadDataTable(Str(r, "name"), t, &err, &problems)) return Fail(err);
    for (const std::string& p : problems) LogWarn("data table: {}", p);
    return Ok(DataTableJson(t));
  }
  if (op == "findall") { // by name ("" = all), hierarchy order, with world positions (FindAll / FindNearest)
    const std::string name = Str(r, "name");
    std::string out = "[";
    std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
    while (!stack.empty()) {
      const SceneObject* o = scene.Find(stack.back());
      stack.pop_back();
      if (!o) continue;
      stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
      if (!name.empty() && o->name != name) continue;
      const Vec3 p = scene.WorldPosition(o->id);
      out += std::format("{}{{\"id\":{},\"name\":{},\"pos\":[{},{},{}]}}", out.size() > 1 ? "," : "", Q(o->id), Q(o->name), p.x, p.y, p.z);
    }
    return Ok(out + "]");
  }
  if (op == "spawn") { // a copy of `source` (with its children) or a new empty object, as one "add" command
    const std::string source = Str(r, "source"), parent = Str(r, "parent"), name = Str(r, "name");
    if (!parent.empty() && !scene.Find(parent)) return Fail(std::format("spawn: no parent object \"{}\"", parent));
    std::string snap;
    if (!source.empty()) {
      if (!scene.Find(source)) return Fail(std::format("spawn: no object \"{}\" to copy", source));
      snap = scene.Snapshot(source);
    } else {
      snap = std::format(R"({{"id":"","name":{},"parent":"","index":-1,"prefab":false,"components":[{{"type":"Transform","value":{{}}}}],"children":[]}})",
                         Q(name.empty() ? "GameObject" : name));
    }
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> in(yyjson_read(snap.data(), snap.size(), 0), yyjson_doc_free);
    std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> d(yyjson_doc_mut_copy(in.get(), nullptr), yyjson_mut_doc_free);
    yyjson_mut_val* root = d ? yyjson_mut_doc_get_root(d.get()) : nullptr;
    if (!root) return Fail("spawn: could not copy the object");
    Renumber(scene, d.get(), root, parent);
    yyjson_mut_obj_put(root, yyjson_mut_strcpy(d.get(), "index"), yyjson_mut_sint(d.get(), -1));
    if (!name.empty()) yyjson_mut_obj_put(root, yyjson_mut_strcpy(d.get(), "name"), yyjson_mut_strcpy(d.get(), name.c_str()));
    for (const char* f : {"position", "rotation", "scale"})
      if (yyjson_val* v = yyjson_obj_get(r, f); yyjson_is_arr(v)) PutTransform(d.get(), root, f, v);
    const std::string id = yyjson_mut_get_str(yyjson_mut_obj_get(root, "id"));
    char* text = yyjson_mut_val_write(root, 0, nullptr);
    const std::string json = text ? text : "";
    std::free(text);
    Command c;
    c.name = "add";
    c.args = "{\"snapshot\":" + json + "}";
    c.source = "script";
    std::string err;
    if (!bus.Execute(c, &err)) return Fail("spawn: " + (err.empty() ? std::string("could not create the object") : err));
    return Ok(Q(id));
  }
  if (op != "name" && op != "rename" && op != "children" && op != "destroy" && op != "has" && op != "get" && op != "set" && op != "call")
    return Fail(std::format("unknown op \"{}\"", op));
  SceneObject* o = scene.Find(target);
  if (!o) return Fail(std::format("no object \"{}\"", target));
  if (op == "name") return Ok(Q(o->name));
  if (op == "rename") {
    const std::string err = run("rename", std::format(R"({{"name":{}}})", Q(Str(r, "name"))));
    return err.empty() ? Ok() : Fail(err);
  }
  if (op == "children") {
    std::vector<std::string> ids;
    ChildIds(scene, *o, yyjson_get_bool(yyjson_obj_get(r, "deep")), ids);
    return Ok(IdList(ids));
  }
  if (op == "destroy") { // answers the ids that went away, so the script ends their behaviours right away
    std::vector<std::string> ids{o->id};
    ChildIds(scene, *o, true, ids);
    const std::string err = run("del", "{}");
    return err.empty() ? Ok(IdList(ids)) : Fail(err);
  }
  if (op == "has") return Ok(o->Find(Str(r, "type")) ? "true" : "false");
  if (op == "get" || op == "set") {
    const std::string path = Str(r, "path");
    const size_t dot = path.find('.');
    Component* c = dot == std::string::npos ? nullptr : o->Find(std::string_view(path).substr(0, dot));
    if (!c) return Fail(std::format("\"{}\" has no component for \"{}\"", o->name, path));
    if (op == "get") {
      std::string v, err;
      if (!Registry::Get(c->data, *c->type, path.substr(dot + 1), v, &err)) return Fail(err);
      return Ok(v);
    }
    const std::string err = run("set", std::format(R"({{"path":{},"value":{}}})", Q(path), Json(yyjson_obj_get(r, "value"))));
    return err.empty() ? Ok() : Fail(err);
  }
  if (op == "call") {
    const std::string type = Str(r, "type"), function = Str(r, "function");
    Component* c = o->Find(type);
    if (!c) return Fail(std::format("\"{}\" has no component \"{}\"", o->name, type));
    const FunctionInfo* fn = c->type->FindFunction(function);
    if (!fn || !(fn->flags & (kCallable | kPure | kButton))) return Fail(std::format("{} has no callable function \"{}\"", type, function));
    yyjson_val* args = yyjson_obj_get(r, "args");
    std::vector<std::string> list;
    if (yyjson_is_arr(args)) {
      yyjson_arr_iter it = yyjson_arr_iter_with(args);
      for (yyjson_val* a = yyjson_arr_iter_next(&it); a; a = yyjson_arr_iter_next(&it)) list.push_back(Json(a));
    }
    if (list.size() != fn->params.size())
      return Fail(std::format("{}.{} takes {} argument(s), got {}", type, function, fn->params.size(), list.size()));
    if (fn->flags & kPure) { // a value: read only, no command
      std::string v, err;
      if (!Registry::Call(c->data, *c->type, function, list, &v, &err)) return Fail(err);
      return Ok(v.empty() ? "null" : v);
    }
    std::string joined;
    for (const std::string& a : list) joined += (joined.empty() ? "" : ",") + a;
    const std::string err = run("call", std::format(R"({{"type":{},"function":{},"args":[{}]}})", Q(type), Q(function), joined));
    if (!err.empty()) return Fail(err);
    return Ok(bus.LastResult().empty() ? "null" : bus.LastResult());
  }
  return Fail("unreachable");
}

} // namespace bdg
