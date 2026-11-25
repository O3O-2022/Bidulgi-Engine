#pragma once
// In-game UI (queue 7-3, runtime part): screen-space UI the game draws over its view, in the editor's Game view and
// in the built player. An object with a UICanvas holds child objects with a UIElement each (text, image, panel,
// button, progress bar); the object's name is the element's name for scripts (C# UI.Text("score")). Elements are
// placed in the canvas's reference size and scaled with the screen (Unity's Canvas Scaler); a child element is placed
// inside its parent element. The designer (.ui assets, Docs/features/ui-widgets.md) comes later.
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BENUM()
enum class UIAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight, Stretch };
BENUM()
enum class UIKind { Panel, Text, Image, Button, ProgressBar };
BENUM()
enum class UITextAlign { Left, Center, Right };

BCLASS(Component)
struct UICanvas {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "The size the elements are laid out in (pixels); scaled to fit the screen") Vec2 referenceSize{1920, 1080};
  BPROPERTY(Edit, Range = "0,1", Tooltip = "Scale by the screen's width (0), height (1) or between") float matchHeight = 1.f;
  BPROPERTY(Edit, Tooltip = "Canvases with a higher order are drawn on top") int32_t order = 0;
  BPROPERTY(Edit) bool visible = true;
};

BCLASS(Component)
struct UIElement {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) UIKind kind = UIKind::Text;
  BPROPERTY(Edit, Header = "Place", Tooltip = "Which point of the parent (canvas or element) it is attached to; its own same point sits there") UIAnchor anchor = UIAnchor::TopLeft;
  BPROPERTY(Edit, Tooltip = "Offset from the anchor in reference pixels (+x right, +y down). Stretch: left / top inset") Vec2 position;
  BPROPERTY(Edit, Tooltip = "Width and height in reference pixels. Stretch: right / bottom inset") Vec2 size{300, 60};
  BPROPERTY(Edit, Header = "Look") std::string text;
  BPROPERTY(Edit, Range = "4,400") float fontSize = 36.f;
  BPROPERTY(Edit) UITextAlign align = UITextAlign::Center;
  BPROPERTY(Edit) Color textColor{1, 1, 1, 1};
  BPROPERTY(Edit, Tooltip = "Panel / button / bar background; an image's tint") Color color{0, 0, 0, 0};
  BPROPERTY(Edit, Tooltip = "Progress bar fill") Color fillColor{0.35f, 0.8f, 0.4f, 1};
  BPROPERTY(Edit, Range = "0,1", Tooltip = "Progress bar fill (0..1)") float value = 1.f;
  BPROPERTY(Edit, Tooltip = "Image file (Assets-relative): PNG, JPG") std::string image;
  BPROPERTY(Edit, Range = "0,200") float cornerRadius = 8.f;
  BPROPERTY(Edit) bool visible = true;
  BPROPERTY(Edit, Tooltip = "Buttons react to the mouse") bool interactable = true;
  BPROPERTY(ReadOnly, Tooltip = "Times this button was clicked while playing (scripts read it)") int32_t clicks = 0;
  bool hovered = false, pressed = false; // not saved: the button's state this frame
};

} // namespace bdg
