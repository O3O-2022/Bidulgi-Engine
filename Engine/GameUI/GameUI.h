#pragma once
// In-game UI (queue 7-3, runtime): lays out and draws the UICanvas / UIElement objects of a scene over the game
// view, and turns the mouse and keys into control input. Used by the editor's Game view, the UI designer's preview
// and the game player.
//
//   GameUI ui;                                    one per view
//   ui.Input(scene, rect, pointer);               while playing: hover, press, click (UIElement.clicks + 1), toggles
//                                                 flip, sliders follow the drag, input fields take typed text, scroll
//                                                 views scroll with the wheel, dropdowns open and pick
//   ui.Draw(scene, batch, font, rect, rhi, assets folder);
//
// rect is the game view in the caller's units (editor: UI units; player: pixels); mouse positions are in the same.
#include "Core/Math.h"
#include "RHI/RHI.h"
#include <map>
#include <string>
#include <vector>

namespace bdg {
class Scene;
class Batch2D;
class Font;
struct UIElement;

class GameUI {
public:
  struct Rect { float x = 0, y = 0, w = 0, h = 0; };
  // One frame of input for the UI: the mouse, and what the keyboard / wheel gave since the last frame.
  struct Pointer {
    float x = 0, y = 0;
    bool down = false;     // left button held
    float wheel = 0;       // notches, + = scroll up
    std::string text;      // typed UTF-8 text (input fields)
    int backspaces = 0;
    bool enter = false;    // ends editing an input field
  };
  void Layout(Scene& scene, const Rect& view);           // element rects (also done by Draw / Input)
  void Input(Scene& scene, const Rect& view, const Pointer& p);
  void Input(Scene& scene, const Rect& view, float mouseX, float mouseY, bool mouseDown) { Input(scene, view, Pointer{mouseX, mouseY, mouseDown}); }
  void Draw(Scene& scene, Batch2D& batch, Font* font, RHI& rhi, const Rect& view, const std::string& assetsDir);
  void Shutdown(RHI& rhi); // the images it loaded
  void ResetInput() { pressedOn_.clear(); sliding_.clear(); focused_.clear(); openList_.clear(); wasDown_ = false; } // no focus, press or open list
  bool ElementRect(const std::string& objectId, Rect& out) const; // after Layout (tests, tools)
  // Where each element was placed by the last Layout, in drawing order (the UI designer turns dragged rects back
  // into anchor / position / size with the parent's rect and the canvas scale).
  struct Placed { std::string object, parent, canvas; Rect r, parentRect; float scale = 1; bool clipped = false; Rect clip; }; // clip: inside a scroll view
  std::vector<Placed> Placement() const;
  size_t Elements() const { return layout_.size(); }
  int Clicks() const { return clicks_; } // clicks seen since the view started (tests)
  const std::string& Focused() const { return focused_; } // the input field that has the keyboard ("" = none)
  // A dropdown's open list: the row rect of option `index` (after Draw / Input); false when that list is not open.
  bool DropdownRow(const std::string& objectId, int index, Rect& out) const;

private:
  struct Item {
    std::string object;
    UIElement* e = nullptr;
    Rect r;
    float scale = 1;
    int order = 0;
    std::string parent, canvas; // parent element or canvas object, the canvas object
    Rect parentRect;
    bool clipped = false;       // inside a scroll view: drawn and hit only within `clip`
    Rect clip;
    float contentH = 0;         // scroll views: how tall their content is (reference pixels)
  };
  const Item* Hit(float x, float y, bool controlsOnly) const;
  std::vector<std::string> Options(const UIElement& e) const;
  std::vector<Item> layout_; // drawing order
  std::map<std::string, TextureHandle> images_;
  std::string pressedOn_;    // object id of the control the press started on
  std::string sliding_;      // slider being dragged
  std::string focused_;      // input field with the keyboard
  std::string openList_;     // dropdown whose list is open
  bool wasDown_ = false;
  int clicks_ = 0;
};

} // namespace bdg
