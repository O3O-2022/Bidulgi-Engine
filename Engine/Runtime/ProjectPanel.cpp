// Project panel (queue 2-5): the project's Assets folder as a folder tree and a list of the current folder.
// C++ classes show as one item ("Weapon · C++ class") with their .h and .cpp nested under it; renaming the
// item renames both files (GUIDs kept, references updated). Part of EditorModel.
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Runtime/EditorModel.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <map>
#include <set>

#include "Scene/SceneFile.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
namespace bdg {
namespace {
std::string Q(const std::string& s) { return Registry::ToJson(&s, reflect::ValueTypeOf<std::string>()); } // JSON string
std::string Folder(const std::string& path) { size_t s = path.rfind('/'); return s == std::string::npos ? "" : path.substr(0, s); }
std::string FileName(const std::string& path) { size_t s = path.rfind('/'); return s == std::string::npos ? path : path.substr(s + 1); }
std::string Stem(const std::string& name) { size_t d = name.rfind('.'); return d == std::string::npos ? name : name.substr(0, d); }
std::string Ext(const std::string& name) {
  size_t d = name.rfind('.');
  std::string e = d == std::string::npos ? "" : name.substr(d);
  for (char& c : e) c = (char)std::tolower((unsigned char)c);
  return e;
}
bool IsHeader(const std::string& e) { return e == ".h" || e == ".hpp"; }
bool IsSource(const std::string& e) { return e == ".cpp" || e == ".cc" || e == ".cxx"; }

std::string TypeLabel(const AssetRecord& r) {
  std::string e = Ext(r.path);
  if (r.importer == "script") return e == ".cs" ? "C# script" : e == ".lua" ? "Lua script" : e == ".py" ? "Python script" : "C++";
  static const std::map<std::string, std::string> names = {{"texture", "Texture"}, {"model", "Model"}, {"audio", "Audio"},
      {"font", "Font"}, {"scene", "Scene"}, {"prefab", "Prefab"}, {"material", "Material"}, {"graph", "Graph"}};
  auto it = names.find(r.importer);
  return it != names.end() ? it->second : "File";
}
}

void EditorModel::AttachProject(Widget& panel, AssetDatabase* db) {
  project_ = &panel;
  assets = db;
  RebuildProject();
}

std::vector<std::string> EditorModel::ProjectItemFiles(const std::string& key) const {
  if (!assets) return {};
  if (assets->ByPath(key)) return {key};
  std::vector<std::string> out; // a C++ pair: "<folder>/<stem>"
  for (const AssetRecord* r : assets->All())
    if (Folder(r->path) == Folder(key) && Stem(FileName(r->path)) == FileName(key) && (IsHeader(Ext(r->path)) || IsSource(Ext(r->path))))
      out.push_back(r->path);
  return out;
}

void EditorModel::RebuildProject() {
  needProject_ = false;
  if (!project_ || !assets) return;
  if (projectView == ProjectView::References) shownRefs_ = projectSelected_.empty() ? std::vector<AssetRef>{} : FindReferences(projectSelected_);
  else if (deleteWarning_.empty()) shownRefs_.clear();
  if (projectView == ProjectView::Unused) shownUnused_ = FindUnused();
  if (projectView == ProjectView::Rules) shownIssues_ = CheckAssetRules();
  project_->children.clear();
  project_->axis = Axis::Row;
  project_->gap = 1;
  std::vector<const AssetRecord*> all = assets->All();

  // Left: folder tree
  std::set<std::string> folders;
  for (const AssetRecord* r : all)
    for (std::string f = Folder(r->path); !f.empty(); f = Folder(f)) folders.insert(f);
  if (!projectFolder_.empty() && !folders.count(projectFolder_)) projectFolder_.clear();
  auto& tree = project_->Add(MakeBox(Axis::Column));
  tree.width = Length::Px(220);
  tree.padT = 4;
  auto& rootRow = tree.Add(MakeListRow("p.folder.", "Assets", "Folder"));
  rootRow.selected = projectFolder_.empty();
  rootRow.onClick = [this](Widget&) { projectFolder_.clear(); projectSelected_.clear(); needProject_ = true; };
  for (const std::string& f : folders) {
    int depth = (int)std::count(f.begin(), f.end(), '/') + 1;
    auto& row = tree.Add(MakeListRow("p.folder." + f, FileName(f), "", depth));
    row.selected = f == projectFolder_;
    row.onClick = [this, f](Widget&) { projectFolder_ = f; projectSelected_.clear(); needProject_ = true; };
  }

  // Right: tools row + items of the current folder
  auto& right = project_->Add(MakeBox(Axis::Column));
  right.width = Length::Fill();
  auto& bar = right.Add(MakeBox(Axis::Row, 6));
  bar.padL = 8; bar.padT = 4; bar.padB = 4; bar.padR = 8;
  bar.crossAlign = Align::Center;
  bar.Add(MakeLabel("Assets" + (projectFolder_.empty() ? "" : "/" + projectFolder_))).variant = 1;
  bar.Add(MakeButton("p.up", "위로")).onClick = [this](Widget&) { projectFolder_ = Folder(projectFolder_); projectSelected_.clear(); needProject_ = true; };
  auto& name = bar.Add(MakeField("p.name", "이름", projectSelected_.empty() ? "" : FileName(projectSelected_)));
  name.width = Length::Fill();
  name.onCommit = [this](Widget& w) { RenameProjectItem(w.text2); };
  bar.Add(MakeButton("p.open", "열기")).onClick = [this](Widget&) { OpenProjectItem(); };
  // 2-5b: where is it used, what is not used, delete with a warning, replace by another asset
  bar.Add(MakeButton("p.refs", "참조", projectView == ProjectView::References)).onClick = [this](Widget&) {
    ShowProjectView(projectView == ProjectView::References ? ProjectView::Files : ProjectView::References);
  };
  bar.Add(MakeButton("p.unused", "안 쓰는 것", projectView == ProjectView::Unused)).onClick = [this](Widget&) {
    ShowProjectView(projectView == ProjectView::Unused ? ProjectView::Files : ProjectView::Unused);
  };
  bar.Add(MakeButton("p.rules", "규칙", projectView == ProjectView::Rules)).onClick = [this](Widget&) { // 2-5d
    ShowProjectView(projectView == ProjectView::Rules ? ProjectView::Files : ProjectView::Rules);
  };
  bar.Add(MakeButton("p.delete", deleteWarning_.empty() || deleteWarning_ != projectSelected_ ? "지우기" : "그래도 지우기",
                     !deleteWarning_.empty() && deleteWarning_ == projectSelected_))
      .onClick = [this](Widget&) { DeleteProjectItem(!deleteWarning_.empty() && deleteWarning_ == projectSelected_); };
  if (!deleteWarning_.empty() && deleteWarning_ != projectSelected_) deleteWarning_.clear();
  if (projectView == ProjectView::References && !projectSelected_.empty()) {
    auto& rep = right.Add(MakeBox(Axis::Row, 6));
    rep.padL = 8; rep.padR = 8; rep.padB = 4;
    auto& with = rep.Add(MakeField("p.replace", "다른 에셋으로 바꾸기", ""));
    with.width = Length::Fill();
    const std::string from = projectSelected_;
    with.onCommit = [this, from](Widget& w) { if (!w.text2.empty()) ReplaceReferences(from, w.text2); };
  }
  if (!deleteWarning_.empty() || projectView == ProjectView::References) { // the places that use the selected asset
    if (!deleteWarning_.empty())
      right.Add(MakeLabel(std::format("\"{}\"을(를) {}곳에서 쓰고 있습니다. 한 번 더 누르면 지웁니다.", FileName(deleteWarning_), shownRefs_.size()))).variant = 1;
    else if (projectSelected_.empty())
      right.Add(MakeLabel("에셋을 고르면 어디서 쓰는지 보여 줍니다.")).variant = 1;
    else
      right.Add(MakeLabel(std::format("\"{}\"을(를) 쓰는 곳 {}개", FileName(projectSelected_), shownRefs_.size()))).variant = 1;
    int i = 0;
    for (const AssetRef& r : shownRefs_) {
      auto& row = right.Add(MakeListRow(std::format("p.ref.{}", i++), r.from + "  ·  " + r.where, r.token));
      const std::string where = r.where, from = r.from;
      row.onClick = [this, where, from](Widget&) { // a place in the open scene: select that object
        const size_t sp = where.find(' ');
        const std::string id = where.substr(0, sp);
        if (from == "열린 장면" && scene.Find(id)) Select(id);
      };
    }
    if (ui_) ui_->ForgetRemovedWidgets();
    return;
  }
  if (projectView == ProjectView::Rules) {
    right.Add(MakeLabel(shownIssues_.empty() ? std::string("규칙에 어긋나는 에셋이 없습니다 (ProjectSettings/AssetRules.json)")
                                             : std::format("규칙에 어긋나는 곳 {}개 (ProjectSettings/AssetRules.json)", shownIssues_.size())))
        .variant = 1;
    int i = 0;
    for (const AssetIssue& is : shownIssues_) {
      auto& row = right.Add(MakeListRow(std::format("p.issue.{}", i++), is.path + "  ·  " + is.message, is.rule));
      const std::string path = is.path;
      row.selected = path == projectSelected_;
      row.onClick = [this, path](Widget&) { projectSelected_ = path; needProject_ = true; };
    }
    if (ui_) ui_->ForgetRemovedWidgets();
    return;
  }
  if (projectView == ProjectView::Unused) {
    right.Add(MakeLabel(std::format("어디서도 쓰지 않는 에셋 {}개 (장면·스크립트에서 닿지 않음)", shownUnused_.size()))).variant = 1;
    for (const std::string& path : shownUnused_) {
      auto& row = right.Add(MakeListRow("p.unused." + path, path, ""));
      row.selected = path == projectSelected_;
      row.onClick = [this, path](Widget&) { projectSelected_ = path; needProject_ = true; };
    }
    if (ui_) ui_->ForgetRemovedWidgets();
    return;
  }

  // Subfolders, then files; .h + .cpp with the same name become one C++ class item
  for (const std::string& f : folders)
    if (Folder(f) == projectFolder_) {
      auto& row = right.Add(MakeListRow("p.item." + f, FileName(f), "Folder"));
      row.onClick = [this, f](Widget&) { projectFolder_ = f; projectSelected_.clear(); needProject_ = true; };
    }
  std::map<std::string, std::vector<const AssetRecord*>> pairs; // stem -> header/source files
  std::vector<const AssetRecord*> files;
  for (const AssetRecord* r : all) {
    if (Folder(r->path) != projectFolder_) continue;
    std::string e = Ext(r->path);
    if (IsHeader(e) || IsSource(e)) pairs[Stem(FileName(r->path))].push_back(r);
    else files.push_back(r);
  }
  for (auto& [stem, list] : pairs) {
    bool pair = list.size() >= 2;
    std::string key = pair ? (projectFolder_.empty() ? stem : projectFolder_ + "/" + stem) : list[0]->path;
    auto& row = right.Add(MakeListRow("p.item." + key, pair ? stem : FileName(list[0]->path), pair ? "C++ class" : "C++"));
    row.selected = key == projectSelected_;
    row.onClick = [this, key](Widget&) { projectSelected_ = key; needProject_ = true; };
    if (pair) // nested: the files of the class
      for (const AssetRecord* r : list) {
        auto& sub = right.Add(MakeListRow("p.item." + r->path, FileName(r->path), IsHeader(Ext(r->path)) ? "Header" : "Source", 1));
        std::string path = r->path;
        sub.selected = path == projectSelected_;
        sub.onClick = [this, path](Widget&) { projectSelected_ = path; needProject_ = true; };
      }
  }
  for (const AssetRecord* r : files) {
    auto& row = right.Add(MakeListRow("p.item." + r->path, FileName(r->path), TypeLabel(*r)));
    std::string path = r->path;
    row.selected = path == projectSelected_;
    row.onClick = [this, path](Widget&) { projectSelected_ = path; needProject_ = true; };
  }
  if (ui_) ui_->ForgetRemovedWidgets();
}

bool EditorModel::RenameProjectItem(const std::string& newName) {
  if (!assets || projectSelected_.empty() || newName.empty()) return false;
  std::vector<std::string> files = ProjectItemFiles(projectSelected_);
  if (files.empty()) return false;
  std::string folder = Folder(files[0]);
  bool pair = files.size() > 1;
  std::vector<std::pair<std::string, std::string>> moves;
  for (const std::string& f : files) {
    std::string to = pair ? Stem(newName) + Ext(f)                            // a class: both files, extensions kept
                          : (Ext(newName).empty() ? newName + Ext(f) : newName); // a file: keep its extension if none given
    moves.emplace_back(f, folder.empty() ? to : folder + "/" + to);
  }
  for (auto& [from, to] : moves)
    if (from != to && assets->ByPath(to)) { LogWarn("rename: \"{}\" already exists", to); needProject_ = true; return false; }
  for (auto& [from, to] : moves) {
    if (from == to) continue;
    std::string err;
    if (!assets->Move(from, to, &err)) { LogWarn("rename: {}", err); needProject_ = true; return false; }
    RetargetAssetPath(from, to); // scene references follow (one undo group each)
    LogInfo("project: renamed {} -> {}", from, to);
  }
  projectSelected_ = pair ? (folder.empty() ? Stem(newName) : folder + "/" + Stem(newName)) : moves[0].second;
  needProject_ = true;
  return true;
}

std::vector<AssetRef> EditorModel::FindReferences(const std::string& assetPath) {
  if (!assets) return {};
  AssetRefIndex index;
  index.Build(*assets, {{"열린 장면", WriteSceneText(scene, sceneName)}});
  std::vector<AssetRef> out;
  for (const std::string& f : ProjectItemFiles(assetPath)) { // a C++ class: both files
    std::vector<AssetRef> r = index.RefsTo(f);
    out.insert(out.end(), r.begin(), r.end());
  }
  return out;
}

std::vector<AssetIssue> EditorModel::CheckAssetRules(const std::vector<std::string>* only) {
  if (!assets) return {};
  std::string err;
  if (!assetRulesPath.empty() && !assetRules.Load(assetRulesPath, &err)) LogWarn("asset rules: {}", err);
  return ruleChecker_.Check(*assets, assetRules, only);
}

std::vector<std::string> EditorModel::FindUnused() {
  if (!assets) return {};
  AssetRefIndex index;
  index.Build(*assets, {{"열린 장면", WriteSceneText(scene, sceneName)}});
  return index.Unused();
}

bool EditorModel::DeleteProjectItem(bool force) {
  if (!assets || projectSelected_.empty()) return false;
  const std::vector<std::string> files = ProjectItemFiles(projectSelected_);
  if (files.empty()) return false;
  if (!force) {
    std::vector<AssetRef> refs = FindReferences(projectSelected_);
    if (!refs.empty()) { // in use: warn first, a second press deletes
      deleteWarning_ = projectSelected_;
      shownRefs_ = std::move(refs);
      needProject_ = true;
      LogWarn("project: {} is used in {} place(s); press again to delete", projectSelected_, shownRefs_.size());
      return false;
    }
  }
  int removed = 0;
  for (const std::string& f : files) {
    const std::filesystem::path abs = std::filesystem::path(std::u8string((const char8_t*)assets->Root().c_str())) /
                                      std::filesystem::path(std::u8string((const char8_t*)f.c_str()));
    for (const std::filesystem::path& p : {abs, std::filesystem::path(abs.wstring() + L".meta")}) {
      std::error_code ec;
      if (!std::filesystem::exists(p, ec)) continue;
      // To the recycle bin (undo there), not gone for good
      std::wstring from = p.wstring();
      from.push_back(L'\0'); // double-null-terminated list
      SHFILEOPSTRUCTW op{};
      op.wFunc = FO_DELETE;
      op.pFrom = from.c_str();
      op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
      if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) removed += p.extension() == L".meta" ? 0 : 1;
      else LogWarn("project: could not move {} to the recycle bin", p.string());
    }
  }
  LogInfo("project: deleted {} ({} file(s) to the recycle bin)", projectSelected_, removed);
  deleteWarning_.clear();
  shownRefs_.clear();
  projectSelected_.clear();
  assets->Refresh();
  needProject_ = true;
  return removed > 0;
}

int EditorModel::ReplaceReferences(const std::string& from, const std::string& to) {
  if (!assets) return 0;
  const AssetRecord* a = assets->ByPath(from);
  const AssetRecord* b = assets->ByPath(to);
  if (!a || !b || a == b) { LogWarn("replace: {} -> {}: both must be project assets", from, to); return 0; }
  const std::vector<std::string> tokens = RefTokens(*a);
  // the open scene: every string field holding one of from's names, through commands (one undo step)
  std::vector<Command> sets;
  std::vector<std::string> stack(scene.Roots().begin(), scene.Roots().end());
  while (!stack.empty()) {
    const SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    for (const Component& c : o->components)
      for (const TypeInfo* t = c.type; t; t = t->base)
        for (const FieldInfo& f : t->fields) {
          if (f.value.type != FieldType::String) continue;
          const std::string& v = *reinterpret_cast<const std::string*>(static_cast<const char*>(c.data) + f.offset);
          if (std::find(tokens.begin(), tokens.end(), v) == tokens.end()) continue;
          Command set;
          set.name = "set";
          set.target = o->id;
          set.args = std::format("{{\"path\":{},\"value\":{}}}", Q(c.type->name + "." + f.name), Q(MatchingToken(*a, *b, v)));
          sets.push_back(set);
        }
    for (auto& k : o->children) stack.push_back(k);
  }
  int n = 0;
  if (!sets.empty()) {
    bus.BeginGroup(std::format("Replace {} with {}", FileName(from), FileName(to)), "ui");
    for (auto& c : sets) n += bus.Execute(c) ? 1 : 0;
    bus.EndGroup();
  }
  // the other project files (the open scene's own file is left to Save)
  std::set<std::string> skip;
  const std::string root = std::filesystem::path(std::u8string((const char8_t*)assets->Root().c_str())).generic_string();
  const std::string sp = std::filesystem::path(scenePath).generic_string();
  if (sp.size() > root.size() + 1 && sp.compare(0, root.size(), root) == 0) skip.insert(sp.substr(root.size() + 1));
  int onDisk = 0;
  std::vector<std::string> files = ReplaceReferencesOnDisk(*assets, from, to, &onDisk, skip);
  LogInfo("replace: {} -> {}: {} in the open scene, {} in {} file(s)", from, to, n, onDisk, files.size());
  needProject_ = true;
  return n + onDisk;
}

std::string EditorModel::ApplyNamePattern(const std::string& pattern, const std::string& current, int number) {
  std::string out;
  for (size_t i = 0; i < pattern.size();) {
    if (pattern[i] == '#') {
      size_t run = 0;
      while (i + run < pattern.size() && pattern[i + run] == '#') ++run;
      std::string digits = std::to_string(number);
      if (digits.size() < run) digits.insert(0, run - digits.size(), '0');
      out += digits;
      i += run;
    } else if (pattern[i] == '*') {
      out += current;
      ++i;
    } else {
      out += pattern[i++];
    }
  }
  return out;
}

int EditorModel::RenameSelection(const std::string& pattern) {
  std::vector<Command> renames;
  int number = 0;
  for (const std::string& id : selection_) {
    const SceneObject* o = scene.Find(id);
    if (!o) continue;
    const std::string name = ApplyNamePattern(pattern, o->name, ++number);
    if (name.empty() || name == o->name) continue;
    Command c;
    c.name = "rename";
    c.target = id;
    c.args = std::format("{{\"name\":{}}}", Q(name));
    renames.push_back(c);
  }
  if (renames.empty()) { needRebuild_ = true; return 0; }
  bus.BeginGroup(std::format("Rename {} objects", renames.size()), "ui");
  int n = 0;
  for (auto& c : renames) n += bus.Execute(c) ? 1 : 0;
  bus.EndGroup();
  needRebuild_ = true;
  return n;
}

bool EditorModel::OpenProjectItem() {
  std::vector<std::string> files = ProjectItemFiles(projectSelected_);
  if (files.empty() || !assets) return false;
  for (const std::string& f : files) { // a C++ class opens both files (the pair)
    std::string url = "file:///" + (std::filesystem::path(assets->Root()) / f).generic_string();
    if (ui_ && ui_->GetWindow()) ui_->GetWindow()->OpenURL(url);
    LogInfo("project: opened {}", f);
  }
  return true;
}
}
