#pragma once
// Editor data side: the open scene, the command bus, the selection, and the Hierarchy / Inspector panels
// built from them by reflection. Panels never write the scene directly: a committed field becomes a "set"
// command; Ctrl+Z / Ctrl+Y undo and redo through the bus. (Temporary home until the editor module exists.)
#include "Core/Command.h"
#include "Scene/Scene.h"
#include "UI/UI.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace bdg {
class EditorModel {
public:
  EditorModel();
  Scene scene;
  CommandBus bus{scene};

  void Attach(UIContext& ui, Widget& hierarchy, Widget& inspector, Widget* history = nullptr);
  void Select(const std::string& id);
  const std::string& Selected() const { return selected_; }
  bool Duplicate();                    // selected object + components, as one undo step
  bool DeleteSelected();
  size_t fieldChanges = 0;             // fields changed this session (drained from Registry each frame)
  bool playing = false;                // play mode comes with 3-6; for now the toggle and its button
  std::string tool = "move";          // active scene tool (gizmos: 3-3)
  void Update();                       // once per frame after input: rebuild / refresh panels if needed
  bool Autosave();                     // scene + command log to .cache/autosave/ (after a guarded crash)
  // Scene file (queue 2-2). Opening is not an edit: history starts fresh. Saving writes the .scene JSONL.
  bool OpenScene(const std::string& path);
  bool SaveScene();                    // to scenePath
  std::string scenePath, sceneName = "Main";

  // Text shown in an Inspector field <-> JSON value ("0, 1, 0" <-> [0,1,0], Knight.glb <-> "Knight.glb").
  static std::string ToDisplay(const std::string& json, FieldType type);
  static std::string FromDisplay(const std::string& text, FieldType type);

private:
  void BuildSampleScene();
  void Rebuild();
  void Refresh();
  struct Binding { Widget* field; std::string target, path; FieldType type; };
  UIContext* ui_ = nullptr;
  Widget* hierarchy_ = nullptr;
  Widget* inspector_ = nullptr;
  Widget* history_ = nullptr;
  long long pendingJump_ = -1; // History row clicked: jump there in Update
  std::string moveSource_, moveTarget_; bool pendingMove_ = false; // Hierarchy drop, applied in Update
  void RebuildHistory();
  std::vector<Binding> bindings_;
  std::unordered_map<std::string, bool> collapsed_; // Inspector card fold state by component type
  std::string selected_, pendingSelect_;
  bool needRebuild_ = false, needRefresh_ = false;
};
}
