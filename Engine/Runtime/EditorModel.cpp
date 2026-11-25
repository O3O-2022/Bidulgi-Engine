#include "Runtime/EditorModel.h"
#include "Core/Utf8Path.h"
#include "Core/Expr.h"
#include "UI/Theme.h"
#include <fstream>
#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Scene/SceneFile.h"
#include "Physics/PhysicsWorld.h"
#include <yyjson.h>
#include <cctype>
#include <format>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
  Transform& camT = Add<Transform>(scene, "g1");
  camT.position = {0, 2, -6};
  camT.rotation = {-12, 180, 0}; // objects look along -Z: turn around to face the scene, a little down (Game view, 3-6)
  Add<Camera>(scene, "g1");
  obj("g2", "Sun");
  Add<Transform>(scene, "g2").rotation = {-50, -30, 0}; // pitch -50: shining down (positive pitch looks up)
  Light& sun = Add<Light>(scene, "g2");
  sun.type = LightType::Directional;
  sun.intensity = 3;
  obj("g3", "Sky");
  Add<Transform>(scene, "g3");
  Add<Sky>(scene, "g3");
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
  Light& lantern = Add<Light>(scene, "g9");
  lantern.color = {1, 0.75f, 0.45f, 1};
  lantern.intensity = 6;
  lantern.range = 8;
  selected_ = "g6";
  selection_ = {"g6"};
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
  pendingSelectMode_ = 0;
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
  selection_ = selected_.empty() ? std::vector<std::string>{} : std::vector<std::string>{selected_};
  needRebuild_ = true;
  savedRevision_ = bus.Revision();
  LogInfo("scene: opened {} ({} objects)", path, scene.ObjectCount());
  LoadFolds();
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
  savedRevision_ = bus.Revision();
  DeleteBackup(); // the file has everything now
  if (assets) { // asset rules (2-5d): warn, never block the save
    const std::vector<AssetIssue> issues = CheckAssetRules();
    lastRuleIssues = issues.size();
    if (!issues.empty()) {
      LogWarn("asset rules: {} problem(s) (Project > 규칙)", issues.size());
      for (size_t i = 0; i < issues.size() && i < 20; ++i) LogWarn("  {}: {}", issues[i].path, issues[i].message);
    }
  }
  return true;
}

void EditorModel::NewScene(SceneTemplate t) {
  namespace fs = std::filesystem;
  BuildDefaultScene(scene, t);
  bus.Reset();
  savedRevision_ = bus.Revision() - 1; // a new scene is not on disk yet
  const fs::path dir = scenePath.empty() ? fs::current_path() / "Scenes" : fs::path(std::u8string((const char8_t*)scenePath.c_str())).parent_path();
  std::error_code ec;
  std::string file = "New Scene.scene";
  for (int n = 1; fs::exists(dir / fs::path(std::u8string((const char8_t*)file.c_str())), ec); ++n) file = std::format("New Scene {}.scene", n);
  const std::u8string full = (dir / fs::path(std::u8string((const char8_t*)file.c_str()))).generic_u8string();
  scenePath.assign((const char*)full.data(), full.size());
  sceneName = file.substr(0, file.size() - 6);
  selected_ = "cam";
  selection_ = {"cam"};
  needRebuild_ = true;
  LogInfo("scene: new {} scene ({} objects), saves to {}", SceneTemplateName(t), scene.ObjectCount(), scenePath);
  LoadFolds();
}

void EditorModel::SetFolded(const std::string& id, bool folded, bool withChildren) {
  std::vector<std::string> ids{id};
  for (size_t i = 0; withChildren && i < ids.size(); ++i)
    if (const SceneObject* o = scene.Find(ids[i])) ids.insert(ids.end(), o->children.begin(), o->children.end());
  for (const std::string& k : ids) {
    const SceneObject* o = scene.Find(k);
    if (!o || o->children.empty()) continue;
    if (folded) folded_.insert(k);
    else folded_.erase(k);
  }
  needRebuild_ = true;
  SaveFolds();
}

// One line per scene: {"scene":"<path>","folded":["g6","g12"]}
void EditorModel::LoadFolds() {
  folded_.clear();
  if (foldsPath.empty()) return;
  std::ifstream in(std::filesystem::path(std::u8string((const char8_t*)foldsPath.c_str())), std::ios::binary);
  const std::string key = FoldKey();
  for (std::string line; std::getline(in, line);) {
    yyjson_doc* doc = yyjson_read(line.data(), line.size(), 0);
    if (!doc) continue;
    yyjson_val* r = yyjson_doc_get_root(doc);
    const char* sc = yyjson_get_str(yyjson_obj_get(r, "scene"));
    if (sc && key == sc) {
      size_t i, n;
      yyjson_val* v;
      yyjson_arr_foreach(yyjson_obj_get(r, "folded"), i, n, v)
        if (yyjson_is_str(v)) folded_.insert(yyjson_get_str(v));
    }
    yyjson_doc_free(doc);
  }
}

void EditorModel::SaveFolds() const {
  if (foldsPath.empty()) return;
  namespace fs = std::filesystem;
  const fs::path p(std::u8string((const char8_t*)foldsPath.c_str()));
  std::vector<std::string> keep; // the other scenes' lines
  {
    std::ifstream in(p, std::ios::binary);
    for (std::string line; std::getline(in, line);) {
      yyjson_doc* doc = yyjson_read(line.data(), line.size(), 0);
      if (!doc) continue;
      const char* sc = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "scene"));
      if (sc && FoldKey() != sc) keep.push_back(line);
      yyjson_doc_free(doc);
    }
  }
  std::string mine = "{\"scene\":" + Q(FoldKey()) + ",\"folded\":[";
  bool first = true;
  for (const std::string& id : folded_) {
    mine += (first ? "" : ",") + Q(id);
    first = false;
  }
  mine += "]}";
  if (!folded_.empty()) keep.push_back(mine);
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary);
  for (const std::string& l : keep) out << l << '\n';
}

bool EditorModel::HierarchyKey(Key k) {
  if (k != Key::Up && k != Key::Down && k != Key::Left && k != Key::Right) return false;
  std::vector<std::string> rows; // the visible rows in order
  std::function<void(const std::string&)> walk = [&](const std::string& id) {
    rows.push_back(id);
    if (folded_.count(id)) return;
    if (const SceneObject* o = scene.Find(id))
      for (const std::string& c : o->children) walk(c);
  };
  for (const std::string& r : scene.Roots()) walk(r);
  if (rows.empty()) return false;
  const auto at = std::find(rows.begin(), rows.end(), selected_);
  if (at == rows.end()) { Select(rows.front()); return true; }
  const SceneObject* o = scene.Find(selected_);
  switch (k) {
    case Key::Up: if (at != rows.begin()) Select(*(at - 1)); break;
    case Key::Down: if (at + 1 != rows.end()) Select(*(at + 1)); break;
    case Key::Left:
      if (o && !o->children.empty() && !folded_.count(selected_)) SetFolded(selected_, true);
      else if (o && !o->parent.empty()) Select(o->parent);
      break;
    default: // Right
      if (o && !o->children.empty() && folded_.count(selected_)) SetFolded(selected_, false);
      else if (o && !o->children.empty()) Select(o->children.front());
      break;
  }
  return true;
}

std::string EditorModel::BackupPath() const {
  namespace fs = std::filesystem;
  const std::string file = scenePath.empty() ? std::string("Untitled.scene") : [&] {
    const std::u8string u = PathU8(scenePath).filename().u8string();
    return std::string(u.begin(), u.end());
  }();
  const std::u8string dir = (fs::current_path() / ".cache" / "autosave").u8string();
  return std::string(dir.begin(), dir.end()) + "/" + file;
}

bool EditorModel::WriteBackup() {
  std::error_code ec;
  std::filesystem::create_directories(PathU8(BackupPath()).parent_path(), ec);
  std::string err;
  if (!SaveSceneFile(scene, sceneName, BackupPath(), &err)) { LogWarn("autosave: {}", err); return false; }
  LogInfo("autosave: {} ({} objects)", BackupPath(), scene.ObjectCount());
  return true;
}

void EditorModel::DeleteBackup() {
  std::error_code ec;
  std::filesystem::remove(PathU8(BackupPath()), ec);
}

bool EditorModel::BackupNewerThanScene() const {
  std::error_code e1, e2;
  const auto backup = std::filesystem::last_write_time(PathU8(BackupPath()), e1);
  if (e1) return false;
  const auto file = std::filesystem::last_write_time(PathU8(scenePath), e2);
  return e2 || backup > file; // no scene file: any backup is newer
}

bool EditorModel::RecoverBackup() {
  const std::string original = scenePath;
  if (!OpenScene(BackupPath())) return false;
  scenePath = original; // Save writes the real file
  MarkDirty();
  LogInfo("autosave: recovered {} from {}", original, BackupPath());
  return true;
}

bool EditorModel::Autosave() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = fs::current_path() / ".cache" / "autosave";
  fs::create_directories(dir, ec);
  std::string dump = scene.Dump();
  std::FILE* f = _wfopen((dir / "scene.jsonl").c_str(), L"wb");
  bool ok = f && std::fwrite(dump.data(), 1, dump.size(), f) == dump.size();
  if (f) std::fclose(f);
  ok = bus.SaveLog((dir / "commands.jsonl").string()) && ok;
  if (ok) LogInfo("autosave: {} ({} objects, {} commands)", dir.string(), scene.ObjectCount(), bus.Log().size());
  else LogError("autosave failed: {}", dir.string());
  return ok;
}

const char* const EditorModel::kMixed = "—";

void EditorModel::ApplySelection(const std::string& id, int mode) {
  auto it = std::find(selection_.begin(), selection_.end(), id);
  if (mode == 1) { // Ctrl: toggle
    if (it != selection_.end()) { selection_.erase(it); selected_ = selection_.empty() ? "" : selection_.back(); return; }
    selection_.push_back(id);
  } else if (mode == 2 && !selected_.empty() && scene.Find(selected_)) { // Shift: range in Hierarchy order
    std::vector<std::string> order, stack(scene.Roots().rbegin(), scene.Roots().rend());
    while (!stack.empty()) {
      std::string x = stack.back(); stack.pop_back();
      order.push_back(x);
      const SceneObject* o = scene.Find(x);
      for (auto k = o->children.rbegin(); k != o->children.rend(); ++k) stack.push_back(*k);
    }
    auto a = std::find(order.begin(), order.end(), selected_), b = std::find(order.begin(), order.end(), id);
    if (a > b) std::swap(a, b);
    selection_.assign(a, b + 1);
  } else {
    selection_ = {id};
  }
  selected_ = id;
}

void EditorModel::SetSelection(std::vector<std::string> ids) {
  selection_.clear();
  for (auto& id : ids) if (scene.Find(id)) selection_.push_back(id);
  selected_ = selection_.empty() ? "" : selection_.back();
  needRebuild_ = true;
}

std::string EditorModel::DisplayFor(const std::vector<std::string>& targets, const std::string& path, FieldType type) const {
  size_t dot = path.find('.');
  std::string shown;
  bool first = true;
  for (auto& id : targets) {
    const SceneObject* o = scene.Find(id);
    Component* c = o ? const_cast<SceneObject*>(o)->Find(path.substr(0, dot)) : nullptr;
    std::string json;
    if (!c || !Registry::Get(c->data, *c->type, path.substr(dot + 1), json)) continue;
    std::string d = ToDisplay(json, type);
    if (first) { shown = d; first = false; }
    else if (d != shown) return kMixed; // values differ
  }
  return shown;
}

Command EditorModel::SetComp(const std::string& id, const std::string& type, const std::string& value) {
  Command cmd;
  cmd.name = "setcomp";
  cmd.target = id;
  cmd.args = std::format("{{\"type\":{},\"value\":{}}}", Q(type), value);
  return cmd;
}

bool EditorModel::CopyComponent(const std::string& type) {
  const SceneObject* o = scene.Find(selected_);
  Component* c = o ? const_cast<SceneObject*>(o)->Find(type) : nullptr;
  if (!c) return false;
  ValueType vt;
  vt.type = FieldType::Struct;
  vt.structType = c->type;
  clipType_ = type;
  clipValue_ = Registry::ToJson(c->data, vt);
  if (ui_ && ui_->GetWindow()) // also on the system clipboard, as text an AI or the terminal can use
    ui_->GetWindow()->SetClipboardText(std::format("{{\"bdgComponent\":{},\"value\":{}}}", Q(type), clipValue_));
  LogInfo("copy: {} values of {}", type, o->name);
  needRebuild_ = true; // "paste as new component" may appear
  return true;
}

std::string EditorModel::PresetPath(const std::string& type) const {
  return (std::filesystem::path(presetDir.empty() ? std::filesystem::current_path().string() : presetDir) / (type + ".preset")).string();
}

bool EditorModel::SavePreset(const std::string& type) {
  const SceneObject* o = scene.Find(selected_);
  Component* c = o ? const_cast<SceneObject*>(o)->Find(type) : nullptr;
  if (!c) return false;
  ValueType vt;
  vt.type = FieldType::Struct;
  vt.structType = c->type;
  std::string path = PresetPath(type), text = std::format("{{\"preset\":1,\"type\":{},\"value\":{}}}\n", Q(type), Registry::ToJson(c->data, vt));
  std::error_code ec;
  std::filesystem::create_directories(PathU8(path).parent_path(), ec);
  std::FILE* f = OpenU8(path, "wb");
  if (!f) { LogError("preset: cannot write {}", path); return false; }
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  LogInfo("preset: saved {}", path);
  return true;
}

bool EditorModel::LoadPreset(const std::string& type, std::string& value) const {
  std::string path = PresetPath(type), text;
  std::FILE* f = OpenU8(path, "rb");
  if (!f) { LogInfo("preset: no {} preset yet ({})", type, path); return false; }
  char buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  yyjson_doc* d = yyjson_read(text.data(), text.size(), 0);
  yyjson_val* r = d ? yyjson_doc_get_root(d) : nullptr;
  const char* t = yyjson_get_str(yyjson_obj_get(r, "type"));
  yyjson_val* v = yyjson_obj_get(r, "value");
  bool ok = t && type == t && yyjson_is_obj(v);
  if (ok) { char* s = yyjson_val_write(v, 0, nullptr); value = s ? s : "{}"; std::free(s); }
  else LogWarn("preset: {} is not a {} preset", path, type);
  yyjson_doc_free(d);
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
    if (scene.Find(pendingSelect_)) ApplySelection(pendingSelect_, pendingSelectMode_);
    inspectAsset_.clear(); // back to the objects (E-1)
    bool opened = false;   // picked in the Scene view under a folded parent: open the way to it (E-2)
    for (const SceneObject* o = scene.Find(pendingSelect_); o && !o->parent.empty(); o = scene.Find(o->parent))
      opened = folded_.erase(o->parent) > 0 || opened;
    if (opened) SaveFolds();
    pendingSelect_.clear();
    pendingSelectMode_ = 0;
    needRebuild_ = true;
  }
  size_t before = selection_.size(); // objects deleted: drop them from the selection
  selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [&](const std::string& id) { return !scene.Find(id); }), selection_.end());
  if (!selected_.empty() && !scene.Find(selected_)) selected_ = selection_.empty() ? "" : selection_.back();
  if (selection_.size() != before) needRebuild_ = true;
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
  if (project_ && projectTiles && project_->w > 1 && std::abs(project_->w - lastProjectW_) > 1) { // tiles per row
    lastProjectW_ = project_->w;
    needProject_ = true;
  }
  if (!pendingFold_.empty()) {
    SetFolded(pendingFold_, !folded_.count(pendingFold_), pendingFoldAll_);
    pendingFold_.clear();
  }
  if (needProject_) RebuildProject();
  bool historyChanged = needRebuild_ || needRefresh_;
  if (needRebuild_) Rebuild();
  else if (needRefresh_) Refresh();
  if (historyChanged) {
    for (Binding& b : bindings_) { // changed while playing (3-6b)
      b.field->marked = false;
      for (const std::string& id : b.targets) b.field->marked = b.field->marked || playChanged_.count(id + "|" + b.path) > 0;
    }
    RebuildHistory();
  }
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
  // Arithmetic (E-4): "2*3" in a number, or in each part of a vector ("1+1, 0, 90/2")
  auto eval = [](const std::string& part, bool integer) {
    double v = 0;
    if (!EvalExpression(part, v)) return Trim(part);
    if (integer) return std::to_string((long long)std::llround(v));
    std::string s = std::format("{}", (float)v);
    return s;
  };
  if (type == FieldType::Int32 || type == FieldType::Float || type == FieldType::Double) return eval(t, type == FieldType::Int32);
  if (IsVectorLike(type) && !t.empty() && t[0] != '[' && t[0] != '{' && t[0] != '"') {
    std::string out = "[";
    size_t start = 0;
    for (size_t k = 0; k <= t.size(); ++k)
      if (k == t.size() || t[k] == ',') {
        out += (start ? "," : "") + eval(t.substr(start, k - start), false);
        start = k + 1;
      }
    return out + "]";
  }
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
  std::function<void(const std::string&, int, const std::vector<uint8_t>&, bool)> addRow = [&](const std::string& id, int depth,
                                                                                              const std::vector<uint8_t>& lines, bool last) {
    const SceneObject* o = scene.Find(id);
    std::string kind = o->prefab ? "Prefab" : "";
    if (kind.empty())
      for (auto& c : o->components) {
        if (c.type->name == "Camera") kind = "Camera";
        else if (c.type->name == "Light") kind = "Light";
        else if (c.type->name == "MeshRenderer" && kind.empty()) kind = "Mesh";
      }
    auto& row = hierarchy_->Add(MakeListRow("h." + id, o->name, kind, depth));
    row.selected = std::find(selection_.begin(), selection_.end(), id) != selection_.end();
    row.tree = lines;
    row.tree.push_back(last ? 3 : 2);
    row.fold = o->children.empty() ? -1 : folded_.count(id) ? 1 : 0;
    row.onClick = [this, id](Widget& w) { // applied in Update (never rebuild inside a click); Ctrl toggles, Shift extends
      if (ui_ && ui_->OnTreeArrow(w, ui_->ClickX())) { // the arrow: fold / open (Alt: everything under it)
        pendingFold_ = id;
        pendingFoldAll_ = ui_->ClickAlt();
        return;
      }
      pendingSelect_ = id;
      pendingSelectMode_ = ui_ && ui_->ClickCtrl() ? 1 : ui_ && ui_->ClickShift() ? 2 : 0;
    };
    row.draggable = true;
    row.onDrop = [this, id](Widget& w) {
      if (w.id.rfind("h.", 0) == 0) { moveSource_ = w.id.substr(2); moveTarget_ = id; pendingMove_ = true; }
    };
    if (folded_.count(id)) return;
    std::vector<uint8_t> next = lines;
    next.push_back(last ? 0 : 1);
    for (size_t i = 0; i < o->children.size(); ++i) addRow(o->children[i], depth + 1, next, i + 1 == o->children.size());
  };
  for (size_t i = 0; i < scene.Roots().size(); ++i) addRow(scene.Roots()[i], 1, {}, i + 1 == scene.Roots().size());

  // Inspector: one card per component, one field per editable property. With several objects selected only
  // the components they all have are shown; a field whose values differ shows "—"; an edit sets it on every
  // object as one undo step.
  inspector_->children.clear();
  std::vector<std::string> targets;
  for (auto& id : selection_) if (scene.Find(id)) targets.push_back(id);
  const SceneObject* o = scene.Find(selected_);
  if (!inspectAsset_.empty() && assets && assets->ByPath(inspectAsset_)) {
    BuildAssetInspector();
  } else if (!o || targets.empty()) {
    inspector_->Add(MakeLabel("Nothing selected")).variant = 1;
  } else {
    bool multi = targets.size() > 1;
    auto& nameRow = inspector_->Add(MakeBox(Axis::Row, 8));
    nameRow.padR = 10; nameRow.padT = 2; nameRow.padB = 2;
    nameRow.crossAlign = Align::Center;
    if (multi) { // batch rename (2-5b): '#' numbers, '*' the current name
      auto& name = nameRow.Add(MakeField("insp.name", std::format("{}개 이름", targets.size()), kMixed));
      name.width = Length::Fill();
      name.onCommit = [this](Widget& w) {
        if (w.text2.empty() || w.text2 == kMixed) { needRebuild_ = true; return; }
        RenameSelection(w.text2);
      };
    } else {
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
    }
    // Runs one command per target; several targets = one undo group.
    auto runAll = [this, targets](const std::string& label, const std::function<Command(const std::string&)>& make) {
      if (targets.size() > 1) bus.BeginGroup(label + std::format(" ({} objects)", targets.size()), "ui");
      for (auto& t : targets) {
        Command c = make(t);
        if (c.name.empty()) continue;
        std::string err;
        if (!bus.Execute(c, &err)) LogWarn("{}: {}", label, err);
      }
      if (targets.size() > 1) bus.EndGroup();
      needRefresh_ = true;
    };
    for (const Component& c : o->components) {
      const TypeInfo& t = *c.type;
      bool everywhere = true;
      for (auto& id : targets) everywhere = everywhere && scene.Find(id)->Find(t.name);
      if (!everywhere) continue; // not common to the whole selection
      auto& card = inspector_->Add(MakeCard("c." + t.name, Pretty(t.name), LanguageBadge(t)));
      card.collapsed = collapsed_.count(t.name) ? collapsed_[t.name] : false;
      std::vector<const TypeInfo*> chain;
      for (const TypeInfo* x = &t; x; x = x->base) chain.insert(chain.begin(), x);
      for (const TypeInfo* x : chain)
        for (const FieldInfo& f : x->fields) {
          if (!(f.flags & kEdit)) continue;
          if (!f.meta.header.empty()) card.Add(MakeLabel(f.meta.header)).variant = 3; // section caption
          if (!ShowIfPasses(c, f.meta.showIf)) continue;                                // hidden until its condition holds
          std::string path = t.name + "." + f.name;
          FieldType type = f.value.type;
          // Pick instead of type: an enum becomes segmented buttons (few short names) or a combo box, a bool a switch
          const std::string shown = DisplayFor(targets, path, type);
          std::unique_ptr<Widget> made;
          if (type == FieldType::Enum && f.value.enumType) {
            std::vector<std::string> names;
            for (auto& [n, v] : f.value.enumType->values) names.push_back(n);
            made = MakeChoice("f." + path, Pretty(f.name), std::move(names), shown);
          } else if (type == FieldType::Bool) {
            made = MakeToggle("f." + path, Pretty(f.name), shown == "true");
            if (shown != "true" && shown != "false") made->text2 = shown; // several objects disagree: "—"
          } else {
            made = MakeField("f." + path, Pretty(f.name), shown);
          }
          auto& field = card.Add(std::move(made));
          field.tooltip = f.meta.tooltip;
          bool numeric = type == FieldType::Int32 || type == FieldType::Float || type == FieldType::Double;
          // E-4b: drag the label to change a number; vectors get one box per axis (drag the X / Y / Z tag)
          field.scrub = (numeric || (IsVectorLike(type) && type != FieldType::Color)) && !multi;
          field.sliderInteger = type == FieldType::Int32;
          field.axes = multi ? 0 : type == FieldType::Vec2 ? 2 : type == FieldType::Vec3 ? 3 : type == FieldType::Vec4 || type == FieldType::Quat ? 4 : 0;
          if (f.meta.hasRange && numeric && !multi) { // Range -> slider (drag edits merge into one undo step)
            field.slider = true;
            field.sliderInteger = type == FieldType::Int32;
            field.sliderMin = f.meta.rangeMin;
            field.sliderMax = f.meta.rangeMax;
          }
          auto set = [this, targets, path, type, runAll](Widget& w) {
            if (w.text2 == kMixed) return; // untouched "—"
            std::string value = FromDisplay(w.text2, type);
            runAll("Set " + path, [&](const std::string& id) {
              Command cmd;
              cmd.name = "set";
              cmd.target = id;
              cmd.args = std::format("{{\"path\":{},\"value\":{}}}", Q(path), value);
              return cmd;
            });
          };
          field.onChange = set;
          field.onCommit = set;
          if (type == FieldType::Color && !multi) { // colour box + HSV picker under the field (E-4b)
            field.swatch = true;
            double rgba[4] = {0, 0, 0, 1};
            sscanf_s(field.text2.c_str(), "%lf , %lf , %lf , %lf", &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
            field.swatchColor = {(float)rgba[0], (float)rgba[1], (float)rgba[2], 1};
            field.onSwatch = [this, path](Widget&) {
              openColor_ = openColor_ == path ? std::string() : path;
              needRebuild_ = true;
            };
          }
          if (type == FieldType::String) // a Project item dropped on a text field puts its path there (E-4b)
            field.onDrop = [set, fp = &field](Widget& dropped) {
              if (dropped.id.rfind("p.item.", 0) != 0) return;
              fp->text2 = dropped.id.substr(7);
              set(*fp);
            };
          // Right-click (E-4): back to the type's default, copy the value, paste one (Unity's field menu)
          field.onContext = [this, t = &t, fieldName = f.name, path, type, set](Widget& w, float x, float y) {
            if (!ui_) return;
            std::string def;
            if (t->create) {
              void* fresh = t->create();
              Registry::Get(fresh, *t, fieldName, def);
              if (t->destroy) t->destroy(fresh);
            }
            Widget* wp = &w;
            std::vector<UIContext::MenuItem> items;
            items.push_back({"기본값으로" + (def.empty() ? std::string() : "  (" + ToDisplay(def, type) + ")"), [this, wp, def, type, set] {
                               wp->text2 = ToDisplay(def, type);
                               set(*wp);
                             }, !def.empty()});
            items.push_back({"값 복사", [this, wp] { ui_->SetClipboard(wp->text2); }, wp->text2 != kMixed});
            items.push_back({"값 붙여넣기", [this, wp, set] {
                               const std::string clip = ui_->Clipboard();
                               if (clip.empty()) return;
                               wp->text2 = clip;
                               set(*wp);
                             }, true});
            ui_->OpenMenu(x, y, std::move(items));
          };
          bindings_.push_back({&field, targets, path, type});
          if (type == FieldType::Color && openColor_ == path && !multi) { // the open picker: hue, saturation, value, alpha
            double rgba[4] = {0, 0, 0, 1};
            sscanf_s(field.text2.c_str(), "%lf , %lf , %lf , %lf", &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
            float h, sv, vv;
            Theme::ToHSV({(float)rgba[0], (float)rgba[1], (float)rgba[2], 1}, h, sv, vv);
            if (sv > 0.001f) pickerHue_ = h;
            auto& picker = card.Add(MakeBox(Axis::Column, 2));
            picker.id = "pick." + path;
            picker.padL = 18; picker.padB = 4;
            Widget* colorField = &field;
            const struct { const char* id; const char* label; double max; double value; } rows[] = {
                {"h", "색상", 359, pickerHue_}, {"s", "채도", 100, sv * 100}, {"v", "밝기", 100, vv * 100}, {"a", "불투명도", 100, rgba[3] * 100}};
            for (auto& r : rows) {
              auto& sl = picker.Add(MakeField("pick." + path + "." + r.id, r.label, std::format("{}", (int)std::lround(r.value))));
              sl.slider = sl.sliderInteger = true;
              sl.sliderMin = 0;
              sl.sliderMax = r.max;
              auto apply = [this, path, colorField, set](Widget&) {
                auto num = [&](const char* k) {
                  Widget* w = ui_ ? ui_->Find("pick." + path + "." + k) : nullptr;
                  return w ? (float)std::strtod(w->text2.c_str(), nullptr) : 0.f;
                };
                pickerHue_ = num("h");
                const Color col = Theme::FromHSV(num("h"), num("s") / 100, num("v") / 100, num("a") / 100);
                colorField->text2 = std::format("{}, {}, {}, {}", col.r, col.g, col.b, col.a);
                colorField->swatchColor = {col.r, col.g, col.b, 1};
                set(*colorField);
              };
              sl.onChange = apply;
              sl.onCommit = apply;
            }
          }
        }
      for (const TypeInfo* x : chain) // BFUNCTION(Button): one button per function, run as an undoable "call"
        for (const FunctionInfo& fn : x->functions) {
          if (!(fn.flags & kButton) || !fn.params.empty()) continue;
          auto& row = card.Add(MakeBox(Axis::Row));
          row.padL = 12; row.padT = 4; row.padB = 4;
          auto& btn = row.Add(MakeButton("b." + t.name + "." + fn.name, Pretty(fn.name)));
          std::string type = t.name, func = fn.name;
          btn.onClick = [runAll, type, func](Widget&) {
            runAll(type + "." + func, [&](const std::string& id) {
              Command cmd;
              cmd.name = "call";
              cmd.target = id;
              cmd.args = std::format("{{\"type\":{},\"function\":{}}}", Q(type), Q(func));
              return cmd;
            });
          };
        }
      // Component tools: copy / paste values, save / apply the type's preset (no menus in the UI yet)
      auto& tools = card.Add(MakeBox(Axis::Row, 4));
      tools.padL = 12; tools.padT = 2; tools.padB = 4;
      std::string type = t.name;
      tools.Add(MakeButton("cc." + type, "값 복사")).onClick = [this, type](Widget&) { CopyComponent(type); };
      auto& paste = tools.Add(MakeButton("cp." + type, "값 붙여넣기"));
      paste.onClick = [this, type, runAll](Widget&) {
        if (clipType_ != type) { LogInfo("paste: the copied values are a {}, not a {}", clipType_.empty() ? "nothing" : clipType_, type); return; }
        std::string value = clipValue_;
        runAll("Paste " + type, [&](const std::string& id) { return SetComp(id, type, value); });
      };
      tools.Add(MakeButton("cs." + type, "프리셋 저장")).onClick = [this, type](Widget&) { SavePreset(type); };
      tools.Add(MakeButton("ca." + type, "프리셋 적용")).onClick = [this, type, runAll](Widget&) {
        std::string value;
        if (!LoadPreset(type, value)) return;
        runAll("Apply preset " + type, [&](const std::string& id) { return SetComp(id, type, value); });
      };
    }
    auto& addRow2 = inspector_->Add(MakeBox(Axis::Row, 6));
    addRow2.padL = 8; addRow2.padT = 6;
    addRow2.crossAlign = Align::Center;
    addRow2.Add(MakeButton("insp.add", "+  Add Component"));
    bool needPhysics = false; // 6-2: Rigidbody + Auto Collider in one click (one undo step)
    for (auto& id : targets) needPhysics = needPhysics || !PhysicsWorld::AddPhysicsCommands(scene, id).empty();
    if (needPhysics)
      addRow2.Add(MakeButton("insp.physics", "+  물리 추가")).onClick = [this, targets](Widget&) {
        bus.BeginGroup("Add Physics", "ui");
        for (const std::string& id : targets)
          for (const Command& c : PhysicsWorld::AddPhysicsCommands(scene, id)) {
            std::string err;
            if (!bus.Execute(c, &err)) LogWarn("add physics: {}", err);
          }
        bus.EndGroup();
      };
    bool missing = false; // "paste as new component" when some selected object does not have the copied type yet
    for (auto& id : targets) missing = missing || (!clipType_.empty() && !scene.Find(id)->Find(clipType_));
    if (missing) {
      std::string type = clipType_, value = clipValue_;
      addRow2.Add(MakeButton("insp.pastenew", "+  붙여넣기: " + Pretty(type))).onClick = [this, type, value, runAll](Widget&) {
        runAll("Paste " + type + " as new", [&](const std::string& id) {
          Command cmd;
          if (scene.Find(id) && scene.Find(id)->Find(type)) return cmd; // already there: skip
          cmd.name = "addcomp";
          cmd.target = id;
          cmd.args = std::format("{{\"type\":{},\"value\":{}}}", Q(type), value);
          return cmd;
        });
      };
    }
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
    b.field->text2 = DisplayFor(b.targets, b.path, b.type);
    b.field->cursor = std::min(b.field->cursor, b.field->text2.size());
    b.field->anchor = std::min(b.field->anchor, b.field->text2.size());
  }
}
}
