#include "Runtime/EditorModel.h"
#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Scene/SceneFile.h"
#include <yyjson.h>
#include <cctype>
#include <format>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace bdg {
namespace {
std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); }

// "jumpHeight" -> "Jump Height", "MeshRenderer" -> "Mesh Renderer"
std::string Pretty(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (i == 0) c = (char)std::toupper((unsigned char)c);
    else if (std::isupper((unsigned char)c) && std::islower((unsigned char)s[i - 1])) out += ' ';
    out += c;
  }
  return out;
}

const char* LanguageBadge(const TypeInfo& t) {
  for (auto& tag : t.tags) {
    if (tag == "CSharp") return "C#";
    if (tag == "Cpp") return "C++";
    if (tag == "Lua") return "Lua";
    if (tag == "Python") return "Python";
  }
  return "";
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool IsVectorLike(FieldType t) {
  return t == FieldType::Vec2 || t == FieldType::Vec3 || t == FieldType::Vec4 || t == FieldType::Quat || t == FieldType::Color;
}

template <class T> T& Add(Scene& s, const std::string& id) {
  return *static_cast<T*>(s.AddComponent(id, T::StaticType()));
}
}

EditorModel::EditorModel() {
  collapsed_["QuestLogic"] = true;
  BuildSampleScene();
  bus.onChanged = [this](const Command& c) {
    if (CommandBus::IsStructural(c.name)) needRebuild_ = true;
    else needRefresh_ = true;
  };
}

void EditorModel::BuildSampleScene() {
  // Loading a scene is not an edit: the sample is built directly, then every change goes through the bus.
  auto obj = [&](const char* id, const char* name, const char* parent = "") { return scene.Create(id, name, parent); };
  obj("g1", "Main Camera");
  Add<Transform>(scene, "g1").position = {0, 2, -6};
  Add<Camera>(scene, "g1");
  obj("g2", "Sun");
  Add<Transform>(scene, "g2").rotation = {50, -30, 0};
  Light& sun = Add<Light>(scene, "g2");
  sun.type = LightType::Directional;
  sun.intensity = 3;
  obj("g3", "Sky");
  Add<Transform>(scene, "g3");
  obj("g4", "World Settings");
  Add<Transform>(scene, "g4");
  obj("g5", "Ground");
  Add<Transform>(scene, "g5").scale = {20, 1, 20};
  MeshRenderer& ground = Add<MeshRenderer>(scene, "g5");
  ground.mesh = "Plane.glb"; ground.material = "M_Ground";
  obj("g6", "Player")->prefab = true;
  Transform& pt = Add<Transform>(scene, "g6");
  pt.position = {0, 1, 0}; pt.rotation = {0, 90, 0};
  MeshRenderer& knight = Add<MeshRenderer>(scene, "g6");
  knight.mesh = "Knight.glb"; knight.material = "M_Knight";
  Add<PlayerMove>(scene, "g6");
  Add<Health>(scene, "g6");
  Add<QuestLogic>(scene, "g6");
  obj("g7", "Weapon", "g6");
  Add<Transform>(scene, "g7").position = {0.4f, 0.9f, 0.2f};
  MeshRenderer& sword = Add<MeshRenderer>(scene, "g7");
  sword.mesh = "Sword.glb"; sword.material = "M_Steel";
  obj("g8", "Crystal")->prefab = true;
  Add<Transform>(scene, "g8").position = {3, 0.5f, 2};
  MeshRenderer& crystal = Add<MeshRenderer>(scene, "g8");
  crystal.mesh = "Crystal.glb"; crystal.material = "M_Gold";
  obj("g9", "Lantern Light");
  Add<Transform>(scene, "g9").position = {-2, 2.5f, 1};
  Add<Light>(scene, "g9").color = {1, 0.75f, 0.45f, 1};
  selected_ = "g6";
}

void EditorModel::Attach(UIContext& ui, Widget& hierarchy, Widget& inspector, Widget* history) {
  ui_ = &ui;
  hierarchy_ = &hierarchy;
  inspector_ = &inspector;
  history_ = history;
  Rebuild();
}

void EditorModel::Select(const std::string& id) {
  pendingSelect_ = id;
}

bool EditorModel::OpenScene(const std::string& path) {
  Scene loaded;
  std::string name, err;
  std::vector<std::string> warnings;
  if (!LoadSceneFile(loaded, path, &name, &warnings, &err)) { LogError("scene: {}", err); return false; }
  for (auto& w : warnings) LogWarn("scene: {}: {}", path, w);
  // Swap the contents into the live scene (other code holds references to it).
  scene.Clear();
  std::string text = WriteSceneText(loaded, name);
  ReadSceneText(scene, text);
  bus.Reset();
  scenePath = path;
  sceneName = name.empty() ? "Main" : name;
  selected_ = scene.Roots().empty() ? "" : scene.Roots().front();
  needRebuild_ = true;
  LogInfo("scene: opened {} ({} objects)", path, scene.ObjectCount());
  return true;
}

int EditorModel::RetargetAssetPath(const std::string& from, const std::string& to) {
  std::vector<Command> sets;
  std::vector<std::string> stack(scene.Roots().begin(), scene.Roots().end());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    for (const Component& c : o->components)
      for (const TypeInfo* t = c.type; t; t = t->base)
        for (const FieldInfo& f : t->fields)
          if (f.value.type == FieldType::String && *reinterpret_cast<const std::string*>(static_cast<const char*>(c.data) + f.offset) == from) {
            Command set;
            set.name = "set";
            set.target = o->id;
            set.args = std::format("{{\"path\":{},\"value\":{}}}", Q(c.type->name + "." + f.name), Q(to));
            sets.push_back(set);
          }
    for (auto& k : o->children) stack.push_back(k);
  }
  if (sets.empty()) return 0;
  bus.BeginGroup("Asset moved: " + from + " -> " + to, "ui");
  int n = 0;
  for (auto& c : sets) n += bus.Execute(c) ? 1 : 0;
  bus.EndGroup();
  LogInfo("assets: {} -> {}: {} reference(s) updated", from, to, n);
  return n;
}

bool EditorModel::SaveScene() {
  std::string err;
  if (scenePath.empty()) { LogWarn("save: no scene file path"); return false; }
  if (!SaveSceneFile(scene, sceneName, scenePath, &err)) { LogError("save: {}", err); return false; }
  LogInfo("scene: saved {} ({} objects)", scenePath, scene.ObjectCount());
  return true;
}

bool EditorModel::Autosave() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = fs::current_path() / ".cache" / "autosave";
  fs::create_directories(dir, ec);
  std::string dump = scene.Dump();
  std::FILE* f = std::fopen((dir / "scene.jsonl").string().c_str(), "wb");
  bool ok = f && std::fwrite(dump.data(), 1, dump.size(), f) == dump.size();
  if (f) std::fclose(f);
  ok = bus.SaveLog((dir / "commands.jsonl").string()) && ok;
  if (ok) LogInfo("autosave: {} ({} objects, {} commands)", dir.string(), scene.ObjectCount(), bus.Log().size());
  else LogError("autosave failed: {}", dir.string());
  return ok;
}

bool EditorModel::Duplicate() {
  const SceneObject* o = scene.Find(selected_);
  if (!o) return false;
  std::string id = scene.NewId(), name = o->name + " (1)", source = o->id;
  bus.BeginGroup("Duplicate " + o->name, "ui");
  Command add;
  add.name = "add";
  add.args = std::format("{{\"name\":{},\"parent\":{},\"index\":{},\"id\":{}}}", Q(name), Q(o->parent),
                         scene.IndexInParent(o->id) + 1, Q(id));
  std::string err;
  bool ok = bus.Execute(add, &err);
  for (const Component& c : scene.Find(source)->components) {
    if (!ok) break;
    ValueType vt;
    vt.type = FieldType::Struct;
    vt.structType = c.type;
    if (scene.Find(id)->Find(c.type->name)) { // the new object already has it (Transform): copy the values
      for (const FieldInfo& f : c.type->fields) {
        std::string v;
        Registry::Get(c.data, *c.type, f.name, v);
        Command set;
        set.name = "set";
        set.target = id;
        set.args = std::format("{{\"path\":{},\"value\":{}}}", Q(c.type->name + "." + f.name), v);
        ok = bus.Execute(set, &err);
        if (!ok) break;
      }
      continue;
    }
    Command comp;
    comp.name = "addcomp";
    comp.target = id;
    comp.args = std::format("{{\"type\":{},\"value\":{}}}", Q(c.type->name), Registry::ToJson(c.data, vt));
    ok = bus.Execute(comp, &err);
  }
  bus.EndGroup();
  if (!ok) { LogWarn("duplicate: {}", err); return false; }
  pendingSelect_ = id;
  return true;
}

bool EditorModel::DeleteSelected() {
  if (selected_.empty()) return false;
  Command del;
  del.name = "del";
  del.target = selected_;
  std::string err;
  if (!bus.Execute(del, &err)) { LogWarn("delete: {}", err); return false; }
  return true;
}

void EditorModel::Update() {
  // Field-level change records (Registry::Set) are the replication feed later (queue N-2); until a network
  // layer consumes them, drain them every frame so they do not pile up for the whole session.
  fieldChanges += Registry::Instance().TakeChanges().size();
  if (!pendingSelect_.empty()) {
    if (scene.Find(pendingSelect_)) selected_ = pendingSelect_;
    pendingSelect_.clear();
    needRebuild_ = true;
  }
  if (!selected_.empty() && !scene.Find(selected_)) { selected_.clear(); needRebuild_ = true; } // deleted
  if (pendingMove_) { // Hierarchy drop: reparent and keep the object where it is in the world (Unity's default)
    pendingMove_ = false;
    Command mv;
    mv.name = "mv";
    mv.target = moveSource_;
    mv.args = std::format("{{\"parent\":{},\"keepWorld\":true}}", Q(moveTarget_));
    std::string err;
    if (!bus.Execute(mv, &err)) LogWarn("move: {}", err);
  }
  if (pendingJump_ >= 0) {
    std::string err;
    if (!bus.JumpTo((size_t)pendingJump_, &err)) LogWarn("history: {}", err);
    pendingJump_ = -1;
  }
  bool historyChanged = needRebuild_ || needRefresh_;
  if (needRebuild_) Rebuild();
  else if (needRefresh_) Refresh();
  if (historyChanged) RebuildHistory();
}

void EditorModel::RebuildHistory() {
  if (!history_) return;
  history_->children.clear();
  auto entries = bus.History();
  size_t depth = bus.UndoDepth();
  auto& start = history_->Add(MakeListRow("history.0", "Scene opened", ""));
  start.selected = depth == 0;
  start.onClick = [this](Widget&) { pendingJump_ = 0; };
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    std::string who = e.source == "ai" ? "AI" : e.source == "script" ? "Script" : e.source == "terminal" ? "Terminal"
                    : e.source == "replay" ? "Replay" : "";
    std::string label = e.commands > 1 ? std::format("{}  ({} edits)", e.label, e.commands) : e.label; // groups stay one row
    auto& row = history_->Add(MakeListRow("history." + std::to_string(i + 1), label, who, 0));
    row.selected = i + 1 == depth;
    row.variant = e.future ? 1 : 0;
    size_t target = i + 1;
    row.onClick = [this, target](Widget&) { pendingJump_ = (long long)target; };
  }
  if (ui_) ui_->ForgetRemovedWidgets();
}

std::string EditorModel::ToDisplay(const std::string& json, FieldType type) {
  if (type == FieldType::String || type == FieldType::Enum) {
    yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
    const char* s = d ? yyjson_get_str(yyjson_doc_get_root(d)) : nullptr;
    std::string out = s ? s : json;
    yyjson_doc_free(d);
    return out;
  }
  if (IsVectorLike(type) && json.size() >= 2 && json.front() == '[') {
    std::string out;
    for (char c : json.substr(1, json.size() - 2)) out += c == ',' ? std::string(", ") : std::string(1, c);
    return out;
  }
  return json;
}

bool EditorModel::ShowIfPasses(const Component& c, const std::string& condition) {
  std::string cond = Trim(condition);
  if (cond.empty()) return true;
  size_t op = cond.find("==");
  bool notEqual = false;
  if (op == std::string::npos) { op = cond.find("!="); notEqual = op != std::string::npos; }
  std::string name, want;
  bool negate = false;
  if (op == std::string::npos) {
    negate = cond[0] == '!';
    name = Trim(negate ? cond.substr(1) : cond);
  } else {
    name = Trim(cond.substr(0, op));
    want = Trim(cond.substr(op + 2));
    if (want.size() >= 2 && want.front() == '"' && want.back() == '"') want = want.substr(1, want.size() - 2);
  }
  const FieldInfo* f = c.type->FindField(name);
  std::string json;
  if (!f || !Registry::Get(c.data, *c.type, name, json)) return true; // unknown field: show rather than hide
  if (op == std::string::npos) {
    bool truthy = json != "false" && json != "0" && json != "\"\"" && json != "[]";
    return truthy != negate;
  }
  bool equal = ToDisplay(json, f->value.type) == want;
  return equal != notEqual;
}

std::string EditorModel::ShowIfState() const {
  std::string s;
  const SceneObject* o = scene.Find(selected_);
  if (!o) return s;
  for (const Component& c : o->components)
    for (const TypeInfo* t = c.type; t; t = t->base)
      for (const FieldInfo& f : t->fields)
        if (!f.meta.showIf.empty()) s += ShowIfPasses(c, f.meta.showIf) ? '1' : '0';
  return s;
}

std::string EditorModel::FromDisplay(const std::string& text, FieldType type) {
  std::string t = Trim(text);
  if (type == FieldType::String) return Q(text);
  if (type == FieldType::Enum) return !t.empty() && t[0] == '"' ? t : Q(t);
  if (IsVectorLike(type) && !t.empty() && t[0] != '[' && t[0] != '{' && t[0] != '"') return "[" + t + "]";
  return t;
}

void EditorModel::Rebuild() {
  needRebuild_ = needRefresh_ = false;
  if (!hierarchy_ || !inspector_) return;
  for (auto& c : inspector_->children) // keep fold state across rebuilds
    if (c->kind == WidgetKind::Card && c->id.rfind("c.", 0) == 0) collapsed_[c->id.substr(2)] = c->collapsed;
  bindings_.clear();

  // Hierarchy: the scene tree in order
  hierarchy_->children.clear();
  auto& sceneRow = hierarchy_->Add(MakeListRow("h.scene", "Main", "Scene"));
  sceneRow.onDrop = [this](Widget& w) { // drop on the scene row: back to the top level
    if (w.id.rfind("h.", 0) == 0) { moveSource_ = w.id.substr(2); moveTarget_.clear(); pendingMove_ = true; }
  };
  std::function<void(const std::string&, int)> addRow = [&](const std::string& id, int depth) {
    const SceneObject* o = scene.Find(id);
    std::string kind = o->prefab ? "Prefab" : "";
    if (kind.empty())
      for (auto& c : o->components) {
        if (c.type->name == "Camera") kind = "Camera";
        else if (c.type->name == "Light") kind = "Light";
        else if (c.type->name == "MeshRenderer" && kind.empty()) kind = "Mesh";
      }
    auto& row = hierarchy_->Add(MakeListRow("h." + id, o->name, kind, depth));
    row.selected = id == selected_;
    row.onClick = [this, id](Widget&) { pendingSelect_ = id; }; // applied in Update (never rebuild inside a click)
    row.draggable = true;
    row.onDrop = [this, id](Widget& w) {
      if (w.id.rfind("h.", 0) == 0) { moveSource_ = w.id.substr(2); moveTarget_ = id; pendingMove_ = true; }
    };
    for (auto& k : o->children) addRow(k, depth + 1);
  };
  for (auto& r : scene.Roots()) addRow(r, 1);

  // Inspector: one card per component, one field per editable property
  inspector_->children.clear();
  const SceneObject* o = scene.Find(selected_);
  if (!o) {
    inspector_->Add(MakeLabel("Nothing selected")).variant = 1;
  } else {
    auto& nameRow = inspector_->Add(MakeBox(Axis::Row, 8));
    nameRow.padR = 10; nameRow.padT = 2; nameRow.padB = 2;
    nameRow.crossAlign = Align::Center;
    auto& name = nameRow.Add(MakeField("insp.name", "Name", o->name)); // F2 focuses it; Enter renames (command)
    name.width = Length::Fill();
    std::string id = o->id;
    name.onCommit = [this, id](Widget& w) {
      if (w.text2.empty() || !scene.Find(id) || scene.Find(id)->name == w.text2) { needRebuild_ = true; return; }
      Command cmd;
      cmd.name = "rename";
      cmd.target = id;
      cmd.args = std::format("{{\"name\":{}}}", Q(w.text2));
      std::string err;
      if (!bus.Execute(cmd, &err)) LogWarn("rename: {}", err);
    };
    if (o->prefab) nameRow.Add(MakeBadge("Prefab"));
    for (const Component& c : o->components) {
      const TypeInfo& t = *c.type;
      auto& card = inspector_->Add(MakeCard("c." + t.name, Pretty(t.name), LanguageBadge(t)));
      card.collapsed = collapsed_.count(t.name) ? collapsed_[t.name] : false;
      std::vector<const TypeInfo*> chain;
      for (const TypeInfo* x = &t; x; x = x->base) chain.insert(chain.begin(), x);
      for (const TypeInfo* x : chain)
        for (const FieldInfo& f : x->fields) {
          if (!(f.flags & kEdit)) continue;
          if (!f.meta.header.empty()) card.Add(MakeLabel(f.meta.header)).variant = 3; // section caption
          if (!ShowIfPasses(c, f.meta.showIf)) continue;                                // hidden until its condition holds
          std::string json;
          Registry::Get(c.data, t, f.name, json);
          auto& field = card.Add(MakeField("f." + t.name + "." + f.name, Pretty(f.name), ToDisplay(json, f.value.type)));
          std::string target = o->id, path = t.name + "." + f.name;
          FieldType type = f.value.type;
          bool numeric = type == FieldType::Int32 || type == FieldType::Float || type == FieldType::Double;
          if (f.meta.hasRange && numeric) { // Range -> slider (drag edits merge into one undo step)
            field.slider = true;
            field.sliderInteger = type == FieldType::Int32;
            field.sliderMin = f.meta.rangeMin;
            field.sliderMax = f.meta.rangeMax;
          }
          field.onChange = [this, target, path, type](Widget& w) {
            Command cmd;
            cmd.name = "set";
            cmd.target = target;
            cmd.args = std::format("{{\"path\":{},\"value\":{}}}", Q(path), FromDisplay(w.text2, type));
            std::string err;
            if (!bus.Execute(cmd, &err)) LogWarn("edit rejected: {}", err);
          };
          field.onCommit = [this, target, path, type](Widget& w) {
            Command cmd;
            cmd.name = "set";
            cmd.target = target;
            cmd.args = std::format("{{\"path\":{},\"value\":{}}}", Q(path), FromDisplay(w.text2, type));
            std::string err;
            if (!bus.Execute(cmd, &err)) LogWarn("edit rejected: {}", err);
            needRefresh_ = true; // show the value the model really holds
          };
          bindings_.push_back({&field, target, path, type});
        }
      for (const TypeInfo* x : chain) // BFUNCTION(Button): one button per function, run as an undoable "call"
        for (const FunctionInfo& fn : x->functions) {
          if (!(fn.flags & kButton) || !fn.params.empty()) continue;
          auto& row = card.Add(MakeBox(Axis::Row));
          row.padL = 12; row.padT = 4; row.padB = 4;
          auto& btn = row.Add(MakeButton("b." + t.name + "." + fn.name, Pretty(fn.name)));
          std::string target = o->id, type = t.name, func = fn.name;
          btn.onClick = [this, target, type, func](Widget&) {
            Command cmd;
            cmd.name = "call";
            cmd.target = target;
            cmd.args = std::format("{{\"type\":{},\"function\":{}}}", Q(type), Q(func));
            std::string err;
            if (!bus.Execute(cmd, &err)) LogWarn("call: {}", err);
          };
        }
    }
    auto& addRow2 = inspector_->Add(MakeBox(Axis::Row));
    addRow2.padL = 8; addRow2.padT = 6;
    addRow2.crossAlign = Align::Center;
    addRow2.Add(MakeButton("insp.add", "+  Add Component"));
  }
  showIfState_ = ShowIfState();
  if (ui_) ui_->ForgetRemovedWidgets();
  RebuildHistory();
}

void EditorModel::Refresh() {
  needRefresh_ = false;
  if (ShowIfState() != showIfState_) { Rebuild(); return; } // a ShowIf condition flipped: fields appear or go away
  for (Binding& b : bindings_) {
    if (b.field->edited) continue; // the user is typing: do not overwrite
    SceneObject* o = scene.Find(b.target);
    size_t dot = b.path.find('.');
    Component* c = o ? o->Find(b.path.substr(0, dot)) : nullptr;
    std::string json;
    if (!c || !Registry::Get(c->data, *c->type, b.path.substr(dot + 1), json)) continue;
    b.field->text2 = ToDisplay(json, b.type);
    b.field->cursor = std::min(b.field->cursor, b.field->text2.size());
    b.field->anchor = std::min(b.field->anchor, b.field->text2.size());
  }
}
}
