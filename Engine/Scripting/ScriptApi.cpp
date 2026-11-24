#include "Scripting/ScriptApi.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
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
  if (op != "name" && op != "rename" && op != "has" && op != "get" && op != "set" && op != "call")
    return Fail(std::format("unknown op \"{}\"", op));
  SceneObject* o = scene.Find(target);
  if (!o) return Fail(std::format("no object \"{}\"", target));
  if (op == "name") return Ok(Q(o->name));
  if (op == "rename") {
    const std::string err = run("rename", std::format(R"({{"name":{}}})", Q(Str(r, "name"))));
    return err.empty() ? Ok() : Fail(err);
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
