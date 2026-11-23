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
  bool HandleKey(const InputEvent& e); // global shortcuts: Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z
  void Update();                       // once per frame after input: rebuild / refresh panels if needed

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
  void RebuildHistory();
  std::vector<Binding> bindings_;
  std::unordered_map<std::string, bool> collapsed_; // Inspector card fold state by component type
  std::string selected_, pendingSelect_;
  bool needRebuild_ = false, needRefresh_ = false;
};
}
