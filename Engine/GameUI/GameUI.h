#pragma once
// In-game UI (queue 7-3, runtime): lays out and draws the UICanvas / UIElement objects of a scene over the game
// view, and turns mouse clicks into button clicks. Used by the editor's Game view and by the game player.
//
//   GameUI ui;                                    one per view
//   ui.Input(scene, rect, mouseX, mouseY, down);  while playing: hover, press, click (UIElement.clicks + 1)
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
  void Layout(Scene& scene, const Rect& view);           // element rects (also done by Draw / Input)
  void Input(Scene& scene, const Rect& view, float mouseX, float mouseY, bool mouseDown);
  void Draw(Scene& scene, Batch2D& batch, Font* font, RHI& rhi, const Rect& view, const std::string& assetsDir);
  void Shutdown(RHI& rhi); // the images it loaded
  bool ElementRect(const std::string& objectId, Rect& out) const; // after Layout (tests, tools)
  // Where each element was placed by the last Layout, in drawing order (the UI designer turns dragged rects back
  // into anchor / position / size with the parent's rect and the canvas scale).
  struct Placed { std::string object, parent, canvas; Rect r, parentRect; float scale = 1; };
  std::vector<Placed> Placement() const;
  size_t Elements() const { return layout_.size(); }
  int Clicks() const { return clicks_; } // clicks seen since the view started (tests)

private:
  struct Item {
    std::string object;
    UIElement* e = nullptr;
    Rect r;
    float scale = 1;
    int order = 0;
    std::string parent, canvas; // parent element or canvas object, the canvas object
    Rect parentRect;
  };
  std::vector<Item> layout_; // drawing order
  std::map<std::string, TextureHandle> images_;
  std::string pressedOn_;    // object id of the button the press started on
  bool wasDown_ = false;
  int clicks_ = 0;
};

} // namespace bdg
