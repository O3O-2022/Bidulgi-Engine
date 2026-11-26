#pragma once
// UI designer (queue 7-3, Unity UGUI / UMG-like): the "UI Designer" workspace tab shows the scene's game UI
// (UICanvas / UIElement, Engine/GameUI) on a device screen in the centre panel instead of the 3D view.
//
//   - Screen: a device / resolution preset (16:9, 16:10, 21:9, 4:3, phone portrait / landscape); the canvas scales
//     to it as in the game (its reference size), so switching shows how anchored elements follow. Wheel zooms around
//     the pointer, middle drag (or Space + drag) pans, Fit returns.
//   - Edit: click selects (Ctrl / Shift add), drag moves, eight handles resize; edges and centres snap to the
//     parent's and the siblings' (guide lines), Ctrl turns snapping off; arrow keys nudge (Shift x10). The anchor
//     picker (9 points + stretch) changes the anchor and keeps the element where it is on screen. Two or more
//     selected: align left / centre / right / top / middle / bottom, distribute across / down.
//   - Every edit is "set UIElement.*" commands, one command group per drag or click: one undo step each.
#include "GameUI/GameUI.h"
#include "Platform/Window.h"
#include <string>
#include <vector>

namespace bdg {
class EditorModel;
class Font;
class UIContext;
struct UIElement;
struct Widget;

class UIDesigner {
public:
  struct Preset { const char* name; int w, h; };
  static const std::vector<Preset>& Presets();
  int preset = 0;       // index into Presets()
  float zoom = 1;       // 1 = the screen fits the view
  float panX = 0, panY = 0;
  bool preview = false; // buttons react to the mouse (hover / press) as in the game, without Play

  // Takes the events over the view (UI units) out of `events`.
  void HandleEvents(std::vector<InputEvent>& events, const Widget& view, EditorModel& model, UIContext& ui);
  void Draw(Widget& view, UIContext& ui, EditorModel& model, Font* font, RHI& rhi, const std::string& assetsDir);
  void Shutdown(RHI& rhi) { game_.Shutdown(rhi); }

  // Test hooks (UI units, the last Draw)
  GameUI::Rect Screen() const { return screen_; }
  bool ElementRect(const std::string& id, GameUI::Rect& r) const;
  bool HandlePoint(int handle, float& x, float& y) const; // 0..7: TL T TR R BR B BL L of the selected element
  bool ControlCenter(const std::string& name, float& x, float& y) const; // toolbar controls ("preset", "anchor", ...)
  int Guides() const { return (int)guides_.size(); }      // snap guide lines shown by the last drag step
  void SetAnchor(EditorModel& model, int anchor);         // what the anchor picker does (0..8, 9 = stretch)

private:
  struct Control { std::string name; float x, y, w, h; };
  struct Guide { bool vertical; float at, from, to; };
  enum class Drag { None, Move, Resize, Pan };
  GameUI game_;
  GameUI::Rect screen_{}, view_{};
  std::vector<GameUI::Placed> placed_;
  std::vector<std::string> selected_; // the model's selection, UI elements only
  std::vector<Control> controls_;
  std::vector<Guide> guides_;
  Drag drag_ = Drag::None;
  int handle_ = -1;
  float downX_ = 0, downY_ = 0;
  std::vector<std::pair<std::string, GameUI::Rect>> start_; // dragged elements and their rects at the press
  bool anchorOpen_ = false, spaceHeld_ = false, mouseDown_ = false;
  float mouseX_ = 0, mouseY_ = 0;
  UIContext* ui_ = nullptr;

  const GameUI::Placed* Find(const std::string& id) const;
  void Layout(const Widget& view, EditorModel& model);
  void Apply(EditorModel& model, const std::string& id, const GameUI::Rect& r, int anchorOverride = -1); // rect -> fields
  GameUI::Rect Snap(EditorModel& model, const GameUI::Rect& r, bool moving, bool snapX0, bool snapX1, bool snapY0, bool snapY1);
  void Align(EditorModel& model, const std::string& how);
  void OpenPresetMenu(float x, float y);
  void OpenZoomMenu(float x, float y);
};
} // namespace bdg
