#include "Core/Command.h"
#include "Scene/Components.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <yyjson.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace bdg {
namespace {
std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); }
bool Fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}

// Small reader over a command's args object.
struct Args {
  yyjson_doc* doc = nullptr;
  yyjson_val* root = nullptr;
  explicit Args(const std::string& json) {
    doc = yyjson_read(json.data(), json.size(), 0);
    root = doc ? yyjson_doc_get_root(doc) : nullptr;
    if (root && !yyjson_is_obj(root)) root = nullptr;
  }
  ~Args() { yyjson_doc_free(doc); }
  Args(const Args&) = delete;
  Args& operator=(const Args&) = delete;
  bool Ok() const { return root != nullptr; }
  yyjson_val* Get(const char* k) const { return root ? yyjson_obj_get(root, k) : nullptr; }
  std::string Str(const char* k, std::string fallback = {}) const {
    const char* s = yyjson_get_str(Get(k));
    return s ? s : fallback;
  }
  int Int(const char* k, int fallback) const {
    yyjson_val* v = Get(k);
    return yyjson_is_int(v) ? (int)yyjson_get_int(v) : fallback;
  }
  std::string Raw(const char* k) const { // JSON text of a member ("" if missing)
    yyjson_val* v = Get(k);
    if (!v) return {};
    char* s = yyjson_val_write(v, 0, nullptr);
    std::string out = s ? s : "";
    std::free(s);
    return out;
  }
};

std::string ComponentNames(const SceneObject& o) {
  std::string s;
  for (auto& c : o.components) s += (s.empty() ? "" : ", ") + c.type->name;
  return s.empty() ? "none" : s;
}
}

std::string Command::ToJson() const {
  std::string s = std::format("{{\"cmd\":{}", Q(name));
  if (!target.empty()) s += ",\"target\":" + Q(target);
  s += ",\"args\":" + (args.empty() ? std::string("{}") : args);
  s += ",\"source\":" + Q(source);
  if (time >= 0) s += std::format(",\"t\":{:.3f}", time);
  return s + "}";
}

double CommandBus::Now() const {
  if (clock) return clock();
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// ---- The operations -------------------------------------------------------------------------
bool CommandBus::Apply(Command& cmd, Command& inv, std::string* error) {
  Args a(cmd.args.empty() ? "{}" : cmd.args);
  if (!a.Ok()) return Fail(error, std::format("{}: args must be a JSON object, got {}", cmd.name, cmd.args));
  inv = {};
  inv.source = cmd.source;
  inv.target = cmd.target;
  const std::string& n = cmd.name;

  if (n == "add") {
    if (yyjson_val* snap = a.Get("snapshot")) { // undo of del: recreate the whole subtree
      std::string json = a.Raw("snapshot");
      if (!scene_.Restore(json, error)) return false;
      cmd.target = yyjson_get_str(yyjson_obj_get(snap, "id")) ? yyjson_get_str(yyjson_obj_get(snap, "id")) : "";
    } else {
      std::string id = a.Str("id", cmd.target);
      std::string parent = a.Str("parent");
      if (!parent.empty() && !scene_.Find(parent)) return Fail(error, std::format("add: no parent object \"{}\"", parent));
      if (!id.empty() && scene_.Find(id)) return Fail(error, std::format("add: id \"{}\" is already used", id));
      SceneObject* o = scene_.Create(id, a.Str("name", "GameObject"), parent, a.Int("index", -1));
      if (!o) return Fail(error, "add: could not create the object");
      cmd.target = o->id;
      scene_.AddComponent(o->id, Transform::StaticType()); // every object has a place in the scene (queue 2-1)
      // Logged with the id it got, so a replay creates the same ids.
      cmd.args = std::format("{{\"name\":{},\"parent\":{},\"index\":{},\"id\":{}}}", Q(o->name), Q(parent),
                             scene_.IndexInParent(o->id), Q(o->id));
    }
    inv.name = "del";
    inv.target = cmd.target;
    return true;
  }

  SceneObject* o = scene_.Find(cmd.target);
  if (!o) return Fail(error, std::format("{}: no object \"{}\" (ids look like \"g1\")", n, cmd.target));

  if (n == "set") {
    std::string path = a.Str("path"), value = a.Raw("value");
    size_t dot = path.find('.');
    if (path.empty() || value.empty()) return Fail(error, "set: needs {\"path\": \"Component.field\", \"value\": ...}");
    std::string compName = path.substr(0, dot), field = dot == std::string::npos ? "" : path.substr(dot + 1);
    Component* c = o->Find(compName);
    if (!c) return Fail(error, std::format("set: \"{}\" has no component \"{}\" (components: {})", o->name, compName, ComponentNames(*o)));
    if (field.empty()) return Fail(error, std::format("set: path needs a field after the component, e.g. \"{}.{}\"", compName,
                                                     c->type->fields.empty() ? "field" : c->type->fields[0].name));
    std::string old;
    if (!Registry::Get(c->data, *c->type, field, old, error)) return false;
    if (!Registry::Set(c->data, *c->type, field, value, error)) return false;
    inv.name = "set";
    inv.args = std::format("{{\"path\":{},\"value\":{}}}", Q(path), old);
    return true;
  }
  if (n == "rename") {
    std::string name = a.Str("name");
    if (name.empty()) return Fail(error, "rename: needs {\"name\": \"New Name\"}");
    inv.name = "rename";
    inv.args = std::format("{{\"name\":{}}}", Q(o->name));
    o->name = name;
    return true;
  }
  if (n == "del") {
    inv.name = "add";
    inv.args = "{\"snapshot\":" + scene_.Snapshot(o->id) + "}";
    scene_.Destroy(o->id);
    return true;
  }
  if (n == "mv") {
    // {"keepWorld": true}: the object stays where it is on screen; the new local Transform is computed once and
    // logged as "transform", so replays and the inverse apply exact values instead of recomputing them.
    std::string oldParent = o->parent, newParent = a.Str("parent");
    int oldIndex = scene_.IndexInParent(o->id);
    Component* tc = o->Find("Transform");
    ValueType tvt;
    tvt.type = FieldType::Struct;
    tvt.structType = &Transform::StaticType();
    std::string oldTransform = tc ? Registry::ToJson(tc->data, tvt) : "", newTransform = a.Raw("transform");
    bool keepWorld = yyjson_get_bool(a.Get("keepWorld"));
    glm::mat4 world = scene_.WorldMatrix(o->id);
    if (!scene_.Reparent(o->id, newParent, a.Int("index", -1), error)) {
      if (error) *error = "mv: " + *error;
      return false;
    }
    if (keepWorld && tc && newTransform.empty()) {
      glm::mat4 local = (newParent.empty() ? glm::mat4(1) : glm::inverse(scene_.WorldMatrix(newParent))) * world;
      glm::vec3 scale, pos, skew;
      glm::vec4 persp;
      glm::quat rot;
      glm::decompose(local, scale, rot, pos, skew, persp);
      Transform t;
      t.position = space::FromGlm(pos);
      t.rotation = space::QuatToEulerDegrees(rot);
      t.scale = space::FromGlm(scale);
      newTransform = Registry::ToJson(&t, tvt);
    }
    if (!newTransform.empty() && tc && !Registry::FromJson(tc->data, tvt, newTransform, error)) {
      scene_.Reparent(o->id, oldParent, oldIndex);
      return false;
    }
    cmd.args = std::format("{{\"parent\":{},\"index\":{}{}}}", Q(newParent), scene_.IndexInParent(o->id),
                           newTransform.empty() ? "" : ",\"transform\":" + newTransform);
    inv.name = "mv";
    inv.args = std::format("{{\"parent\":{},\"index\":{}{}}}", Q(oldParent), oldIndex,
                           newTransform.empty() ? "" : ",\"transform\":" + oldTransform);
    return true;
  }
  if (n == "addcomp") {
    std::string typeName = a.Str("type");
    const TypeInfo* t = Registry::Instance().Find(typeName);
    if (!t) return Fail(error, std::format("addcomp: unknown component type \"{}\"", typeName));
    if (!scene_.AddComponent(o->id, *t, a.Raw("value"), error)) return false;
    int index = a.Int("index", -1);
    if (index >= 0 && index < (int)o->components.size() - 1) // put it back where it was (undo of rmcomp)
      std::rotate(o->components.begin() + index, o->components.end() - 1, o->components.end());
    inv.name = "rmcomp";
    inv.args = std::format("{{\"type\":{}}}", Q(typeName));
    return true;
  }
  if (n == "rmcomp") {
    std::string typeName = a.Str("type");
    Component* c = o->Find(typeName);
    if (!c) return Fail(error, std::format("rmcomp: \"{}\" has no component \"{}\" (components: {})", o->name, typeName, ComponentNames(*o)));
    if (typeName == "Transform")
      return Fail(error, std::format("rmcomp: every object keeps its Transform (it places \"{}\" in the scene); delete the object instead", o->name));
    ValueType vt;
    vt.type = FieldType::Struct;
    vt.structType = c->type;
    int index = (int)(c - o->components.data());
    inv.name = "addcomp";
    inv.args = std::format("{{\"type\":{},\"value\":{},\"index\":{}}}", Q(typeName), Registry::ToJson(c->data, vt), index);
    scene_.RemoveComponent(o->id, typeName);
    return true;
  }
  return Fail(error, std::format("unknown command \"{}\" (set, rename, add, del, mv, addcomp, rmcomp)", n));
}

// ---- History ---------------------------------------------------------------------------------
void CommandBus::Push(Entry e) {
  redo_.clear();
  if (groupDepth_ > 0) {
    group_.forward.insert(group_.forward.end(), e.forward.begin(), e.forward.end());
    group_.inverse.insert(group_.inverse.begin(), e.inverse.begin(), e.inverse.end());
    return;
  }
  undo_.push_back(std::move(e));
}

std::string CommandBus::Label(const Command& cmd) const {
  Args a(cmd.args.empty() ? "{}" : cmd.args);
  const SceneObject* o = scene_.Find(cmd.target);
  std::string who = o ? o->name : cmd.target;
  const std::string& n = cmd.name;
  if (n == "set") {
    std::string v = a.Raw("value");
    if (v.size() > 24) v = v.substr(0, 21) + "...";
    return std::format("{}: {} = {}", who, a.Str("path"), v);
  }
  if (n == "rename") return std::format("Rename {} to {}", who, a.Str("name"));
  if (n == "add") return a.Get("snapshot") ? "Restore object" : "Add " + a.Str("name", "GameObject");
  if (n == "del") return "Delete " + who;
  if (n == "mv") return "Move " + who;
  if (n == "addcomp") return std::format("Add {} to {}", a.Str("type"), who);
  if (n == "rmcomp") return std::format("Remove {} from {}", a.Str("type"), who);
  return n;
}

std::vector<CommandBus::HistoryEntry> CommandBus::History() const {
  std::vector<HistoryEntry> out;
  for (auto& e : undo_) out.push_back({e.label, e.source, e.forward.size(), false});
  for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) out.push_back({it->label, it->source, it->forward.size(), true});
  return out;
}

bool CommandBus::JumpTo(size_t depth, std::string* error) {
  if (depth > undo_.size() + redo_.size()) return Fail(error, std::format("no history step {}", depth));
  while (undo_.size() > depth) if (!Undo(error)) return false;
  while (undo_.size() < depth) if (!Redo(error)) return false;
  return true;
}

bool CommandBus::Execute(Command cmd, std::string* error) {
  if (cmd.time < 0) cmd.time = Now();
  std::string label = Label(cmd); // before applying: a deleted object still has its name
  Command inv;
  if (!Apply(cmd, inv, error)) return false;
  if (yyjson_doc* d = yyjson_read(cmd.args.data(), cmd.args.size(), 0)) { // canonical (minified) args in the log
    if (char* s = yyjson_write(d, 0, nullptr)) { cmd.args = s; std::free(s); }
    yyjson_doc_free(d);
  }
  log_.push_back(cmd.ToJson());
  Entry e;
  e.forward = {cmd};
  e.inverse = {inv};
  e.label = cmd.name == "add" && label == "Add GameObject" ? Label(cmd) : label;
  e.source = cmd.source;
  e.time = cmd.time;
  if (cmd.name == "set") {
    Args a(cmd.args);
    e.mergeKey = cmd.target + "|" + a.Str("path");
  }
  // A drag sends many sets to one field: fold them into one undo step (keep the first inverse).
  if (groupDepth_ == 0 && !e.mergeKey.empty() && cmd.source == "ui" && !undo_.empty() && redo_.empty()) {
    Entry& top = undo_.back();
    if (top.mergeKey == e.mergeKey && top.forward.size() == 1 && cmd.time - top.time <= mergeWindow) {
      top.forward[0] = cmd;
      top.time = cmd.time;
      top.label = label; // shows the latest value
      if (onChanged) onChanged(cmd);
      return true;
    }
  }
  Push(std::move(e));
  if (onChanged) onChanged(cmd);
  return true;
}

bool CommandBus::Undo(std::string* error) {
  if (undo_.empty()) return Fail(error, "nothing to undo");
  Entry e = std::move(undo_.back());
  undo_.pop_back();
  for (Command& c : e.inverse) {
    Command ignored;
    if (!Apply(c, ignored, error)) { undo_.push_back(std::move(e)); return false; }
    if (onChanged) onChanged(c);
  }
  log_.push_back("{\"cmd\":\"undo\"}");
  redo_.push_back(std::move(e));
  return true;
}

bool CommandBus::Redo(std::string* error) {
  if (redo_.empty()) return Fail(error, "nothing to redo");
  Entry e = std::move(redo_.back());
  redo_.pop_back();
  for (Command& c : e.forward) {
    Command ignored;
    if (!Apply(c, ignored, error)) { redo_.push_back(std::move(e)); return false; }
    if (onChanged) onChanged(c);
  }
  log_.push_back("{\"cmd\":\"redo\"}");
  undo_.push_back(std::move(e));
  return true;
}

void CommandBus::BeginGroup(std::string label, std::string source) {
  if (groupDepth_++ == 0) group_ = Entry{{}, {}, label, {}, source, Now()};
  log_.push_back(std::format("{{\"cmd\":\"begin\",\"label\":{},\"source\":{}}}", Q(label), Q(source)));
}

void CommandBus::EndGroup() {
  if (groupDepth_ == 0) return;
  log_.push_back("{\"cmd\":\"end\"}");
  if (--groupDepth_ == 0 && !group_.forward.empty()) {
    redo_.clear();
    undo_.push_back(std::move(group_));
  }
  group_ = {};
}

// ---- Log / replay ------------------------------------------------------------------------------
bool CommandBus::SaveLog(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return false;
  for (auto& line : log_) std::fprintf(f, "%s\n", line.c_str());
  std::fclose(f);
  return true;
}

bool CommandBus::ExecuteLine(std::string_view line, std::string* error) {
  Args a{std::string(line)};
  if (!a.Ok()) return Fail(error, std::format("not a command line: {}", line));
  std::string name = a.Str("cmd");
  if (name == "undo") return Undo(error);
  if (name == "redo") return Redo(error);
  if (name == "begin") { BeginGroup(a.Str("label"), a.Str("source", "ai")); return true; }
  if (name == "end") { EndGroup(); return true; }
  Command c;
  c.name = name;
  c.target = a.Str("target");
  c.args = a.Raw("args");
  if (c.args.empty()) c.args = "{}";
  c.source = a.Str("source", "replay");
  yyjson_val* t = a.Get("t");
  c.time = yyjson_is_num(t) ? yyjson_get_num(t) : -1;
  return Execute(std::move(c), error);
}

bool CommandBus::Replay(const std::string& path, std::string* error, size_t* count) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return Fail(error, std::format("cannot open {}", path));
  std::string line;
  size_t n = 0, lineNo = 0;
  bool ok = true;
  for (int ch; ok;) {
    ch = std::fgetc(f);
    if (ch != EOF && ch != '\n') { if (ch != '\r') line += (char)ch; continue; }
    ++lineNo;
    if (!line.empty()) {
      std::string err;
      if (!ExecuteLine(line, &err)) { ok = Fail(error, std::format("{}({}): {}", path, lineNo, err)); break; }
      ++n;
    }
    line.clear();
    if (ch == EOF) break;
  }
  std::fclose(f);
  if (count) *count = n;
  return ok;
}
}
