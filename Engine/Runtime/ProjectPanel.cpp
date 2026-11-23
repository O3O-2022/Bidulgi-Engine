// Project panel (queue 2-5): the project's Assets folder as a folder tree and a list of the current folder.
// C++ classes show as one item ("Weapon · C++ class") with their .h and .cpp nested under it; renaming the
// item renames both files (GUIDs kept, references updated). Part of EditorModel.
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "Runtime/EditorModel.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <map>
#include <set>

namespace bdg {
namespace {
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
