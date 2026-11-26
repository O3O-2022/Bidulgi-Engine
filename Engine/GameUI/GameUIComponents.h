#pragma once
// In-game UI (queue 7-3, runtime part): screen-space UI the game draws over its view, in the editor's Game view and
// in the built player. An object with a UICanvas holds child objects with a UIElement each (text, image, panel,
// button, progress bar); the object's name is the element's name for scripts (C# UI.Text("score")). Elements are
// placed in the canvas's reference size and scaled with the screen (Unity's Canvas Scaler); a child element is placed
// inside its parent element. The UI designer (Runtime/UIDesigner) edits them in the "UI Designer" workspace.
// Controls (round 7): Toggle (value 0 / 1, a click flips it), Slider (value 0..1, dragged), InputField (text typed
// after a click; placeholder while empty), ScrollView (clips its children; the wheel scrolls them), Dropdown
// (options "A|B|C", value = the chosen index; a click opens the list).
// Layout groups: an element with a layout places its children in a row, a column or a grid (their own anchor and
// position are not used; their size is, or the grid's cell size). Images with a slice border draw 9-sliced: the
// corners keep their size, the edges stretch one way, the middle both (panels and buttons from one small image).
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BENUM()
enum class UIAnchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight, Stretch };
BENUM()
enum class UIKind { Panel, Text, Image, Button, ProgressBar, Toggle, Slider, InputField, ScrollView, Dropdown };
BENUM()
enum class UITextAlign { Left, Center, Right };
BENUM()
enum class UILayout { None, Horizontal, Vertical, Grid };

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
  BPROPERTY(Edit, Tooltip = "Progress bar fill and slider position (0..1), toggle on (1) / off (0), dropdown choice (0, 1, ...)") float value = 1.f;
  BPROPERTY(Edit, Tooltip = "Input field: shown dimmed while the text is empty") std::string placeholder;
  BPROPERTY(Edit, Tooltip = "Dropdown: the choices, separated by |") std::string options;
  BPROPERTY(Edit, Tooltip = "Scroll view: how far its content is scrolled (reference pixels, +y down)") Vec2 scroll;
  BPROPERTY(Edit, Tooltip = "Image file (Assets-relative): PNG, JPG") std::string image;
  BPROPERTY(Edit, Range = "0,200") float cornerRadius = 8.f;
  BPROPERTY(Edit, Range = "0,512", Tooltip = "Image: border kept unstretched, in the image's pixels (9-slice); 0 = stretched whole") float slice = 0.f;
  BPROPERTY(Edit, Header = "Layout", Tooltip = "Places the children in a row, a column or a grid (their anchor and position are not used)") UILayout layout = UILayout::None;
  BPROPERTY(Edit, Range = "0,1000", Tooltip = "Layout: gap between children (reference pixels)") float spacing = 8.f;
  BPROPERTY(Edit, Range = "0,1000", Tooltip = "Layout: space inside the edges (reference pixels)") float padding = 0.f;
  BPROPERTY(Edit, Tooltip = "Grid layout: each child's cell (reference pixels)") Vec2 cellSize{100, 100};
  BPROPERTY(Edit) bool visible = true;
  BPROPERTY(Edit, Tooltip = "Buttons react to the mouse") bool interactable = true;
  BPROPERTY(ReadOnly, Tooltip = "Times this button was clicked while playing (scripts read it)") int32_t clicks = 0;
  bool hovered = false, pressed = false; // not saved: the button's state this frame
  bool focused = false, open = false;    // not saved: input field has the keyboard, dropdown list is open
};

} // namespace bdg
