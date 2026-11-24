#pragma once
// Editor data side: the open scene, the command bus, the selection, and the Hierarchy / Inspector panels
// built from them by reflection. Panels never write the scene directly: a committed field becomes a "set"
// command; Ctrl+Z / Ctrl+Y undo and redo through the bus. (Temporary home until the editor module exists.)
#include "Assets/AssetRefs.h"
#include "Assets/AssetImport.h"
#include "Scene/DefaultScene.h"
#include "Core/Command.h"
#include "Scene/Scene.h"
#include "UI/UI.h"
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdg {
class AssetDatabase;
class EditorModel {
public:
  EditorModel();
  Scene scene;
  CommandBus bus{scene};

  void Attach(UIContext& ui, Widget& hierarchy, Widget& inspector, Widget* history = nullptr);
  void Select(const std::string& id);
  const std::string& Selected() const { return selected_; }          // primary (last clicked)
  const std::vector<std::string>& Selection() const { return selection_; }
  void SetSelection(std::vector<std::string> ids);                   // last one becomes primary
  // Component values (queue 2-4b): copy / paste between objects, presets as <presetDir>/<Type>.preset
  bool CopyComponent(const std::string& type);
  bool SavePreset(const std::string& type);
  bool LoadPreset(const std::string& type, std::string& valueJson) const;
  std::string PresetPath(const std::string& type) const;
  std::string presetDir;                                             // set by the engine (project Presets folder)
  static const char* const kMixed;                                   // shown when selected objects differ: "—"
  bool Duplicate();                    // selected object + components, as one undo step
  bool DeleteSelected();
  size_t fieldChanges = 0;             // fields changed this session (drained from Registry each frame)
  bool playing = false;                // play mode (queue 3-6, Runtime/PlayMode): the scene is the play world
  void ValuesChanged() { needRefresh_ = true; }       // the running game changed values: refresh the Inspector
  void SceneReplaced() { needRebuild_ = true; }       // the whole scene was swapped (play mode stopped)
  // Fields the user changed while playing ("id|Component.field"): marked in the Inspector (3-6b).
  void SetPlayChanged(std::set<std::string> keys) { if (keys != playChanged_) { playChanged_ = std::move(keys); needRefresh_ = true; } }
  std::string tool = "move";          // active scene tool (gizmos: 3-3)
  void Update();                       // once per frame after input: rebuild / refresh panels if needed
  bool Autosave();                     // scene + command log to .cache/autosave/ (after a guarded crash)
  // Scene file (queue 2-2). Opening is not an edit: history starts fresh. Saving writes the .scene JSONL.
  bool OpenScene(const std::string& path);
  // An asset moved (queue 2-3): string fields holding the old path are set to the new one, as one undo step.
  int RetargetAssetPath(const std::string& from, const std::string& to);
  // Project panel (queue 2-5, ProjectPanel.cpp): folder tree + list, C++ .h/.cpp pairs as one item.
  void AttachProject(Widget& panel, AssetDatabase* db);
  void RebuildProject();
  void ProjectChanged() { needProject_ = true; }         // the asset database changed on disk
  bool RenameProjectItem(const std::string& newName);  // a pair renames both files
  bool OpenProjectItem();                              // default app (a pair opens both files)
  std::vector<std::string> ProjectItemFiles(const std::string& key) const;
  const std::string& ProjectSelected() const { return projectSelected_; }
  // Previews (E-1). The engine sets these: a thumbnail texture for a Project tile (invalid = draw the type icon),
  // and the drawing of the Inspector's preview of an asset.
  std::function<TextureHandle(const AssetRecord&)> thumbnailFor;
  std::function<void(Widget&, UIContext&, const std::string& path)> drawPreview;
  bool projectTiles = true;                 // Project panel: tiles with thumbnails (false: list rows)
  void SetProjectTiles(bool on) { projectTiles = on; needProject_ = true; }
  // The asset the Inspector shows (a Project item was clicked); "" = the selected objects. Selecting an object
  // in the Hierarchy or the Scene view goes back to the objects.
  const std::string& InspectedAsset() const { return inspectAsset_; }
  void InspectAsset(const std::string& path) { inspectAsset_ = path; needRebuild_ = true; }
  // Hierarchy folding (E-2). A click on a row's arrow folds or opens it (Alt: with everything under it); selecting
  // a hidden object opens its parents. Remembered per scene in foldsPath ("" = not saved: automated runs).
  void SetFolded(const std::string& id, bool folded, bool withChildren = false);
  bool Folded(const std::string& id) const { return folded_.count(id) != 0; }
  std::string foldsPath;
  void LoadFolds();
  void SelectProjectFolder(const std::string& f) { projectFolder_ = f; projectSelected_.clear(); needProject_ = true; }
  AssetDatabase* assets = nullptr;
  bool SaveScene();                    // to scenePath
  // File > New Scene (2-6): replaces the open scene by a template (history starts over); the file is
  // Scenes/New Scene.scene next to the current one (numbered when taken) until saved.
  void NewScene(SceneTemplate t);
  // References (queue 2-5b, Assets/AssetRefs): on disk plus the open scene (as "열린 장면").
  std::vector<AssetRef> FindReferences(const std::string& assetPath);
  std::vector<std::string> FindUnused();
  // Deletes the selected Project item (files and .meta to the recycle bin). In use somewhere and not `force`:
  // nothing happens, the panel shows the warning and the places, and a second press deletes.
  bool DeleteProjectItem(bool force = false);
  // Every reference to `from` becomes the same kind of name for `to`: the open scene through commands (one undo
  // step), the other project files on disk. Returns the number of references changed.
  int ReplaceReferences(const std::string& from, const std::string& to);
  // Renames the selected objects from a pattern (one undo step): '#' runs are numbers from 1 ("Tree_##" -> Tree_01,
  // Tree_02 ...), '*' is the current name ("Old_*", "*_LOD0"); anything else is the same name for all.
  int RenameSelection(const std::string& pattern);
  static std::string ApplyNamePattern(const std::string& pattern, const std::string& current, int number);
  enum class ProjectView { Files, References, Unused, Rules };
  // Asset rules (2-5d): loaded by the engine from <project>/ProjectSettings/AssetRules.json
  AssetRules assetRules;
  std::string assetRulesPath; // read again before each check (the file is outside the watched Assets folder)
  std::vector<AssetIssue> CheckAssetRules(const std::vector<std::string>* only = nullptr);
  size_t lastRuleIssues = 0; // found by the last check on save
  const std::string& ProjectFolder() const { return projectFolder_; }
  ProjectView projectView = ProjectView::Files; // what the Project panel lists under its tools
  void ShowProjectView(ProjectView v) { projectView = v; needProject_ = true; }
  void SelectProjectItem(const std::string& path) { projectSelected_ = path; needProject_ = true; }
  bool DeleteWarningShown() const { return !deleteWarning_.empty(); }
  std::string scenePath, sceneName = "Main";

  // Text shown in an Inspector field <-> JSON value ("0, 1, 0" <-> [0,1,0], Knight.glb <-> "Knight.glb").
  static std::string ToDisplay(const std::string& json, FieldType type);
  static std::string FromDisplay(const std::string& text, FieldType type);
  // BPROPERTY(ShowIf = ...): "flag", "!flag", "field == Value", "field != Value" (values as the Inspector shows them)
  static bool ShowIfPasses(const Component& c, const std::string& condition);

private:
  void BuildSampleScene();
  void Rebuild();
  void Refresh();
  struct Binding { Widget* field; std::vector<std::string> targets; std::string path; FieldType type; };
  UIContext* ui_ = nullptr;
  Widget* hierarchy_ = nullptr;
  Widget* inspector_ = nullptr;
  Widget* history_ = nullptr;
  long long pendingJump_ = -1; // History row clicked: jump there in Update
  std::string moveSource_, moveTarget_; bool pendingMove_ = false; // Hierarchy drop, applied in Update
  void RebuildHistory();
  std::vector<Binding> bindings_;
  std::set<std::string> playChanged_;
  std::unordered_map<std::string, bool> collapsed_; // Inspector card fold state by component type
  std::string selected_, pendingSelect_;
  std::vector<std::string> selection_;
  int pendingSelectMode_ = 0; // 0 replace, 1 toggle (Ctrl), 2 range (Shift)
  std::string clipType_, clipValue_; // copied component values
  void ApplySelection(const std::string& id, int mode);
  std::string DisplayFor(const std::vector<std::string>& targets, const std::string& path, FieldType type) const;
  static Command SetComp(const std::string& id, const std::string& type, const std::string& value);
  bool needRebuild_ = false, needRefresh_ = false, needProject_ = false;
  std::string deleteWarning_;                 // the item the last delete warned about (second press deletes)
  std::vector<AssetRef> shownRefs_;           // References view
  std::vector<std::string> shownUnused_;
  std::vector<AssetIssue> shownIssues_;      // Rules view
  AssetRuleChecker ruleChecker_;      // Unused view
  Widget* project_ = nullptr;
  std::string inspectAsset_;
  std::set<std::string> folded_;
  std::string pendingFold_;
  bool pendingFoldAll_ = false;
  void SaveFolds() const;
  std::string FoldKey() const { return scenePath.empty() ? std::string("(sample)") : scenePath; }
  float lastProjectW_ = 0; // tiles per row follow the panel width
  void BuildAssetInspector();
  std::string projectFolder_, projectSelected_; // current folder; selected item (path, or folder/stem of a C++ pair)
  std::string showIfState_; // which ShowIf fields are visible; a change rebuilds the Inspector
  std::string ShowIfState() const;
};
}
