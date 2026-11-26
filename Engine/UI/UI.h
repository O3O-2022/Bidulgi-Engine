#pragma once
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "UI/IconAtlas.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bdg {
class Batch2D;
class Font;
class Theme;

// ---- Retained widget tree ------------------------------------------------------------------
// Widgets persist between frames; only layout/draw run each frame. Components follow the
// design system (design-system/project/components) and read every size and color from tokens.
enum class WidgetKind { Box, Label, Panel, Tabs, Button, Field, ListRow, Badge, Node, Cover, Custom, Card, Dock, Choice };
struct DockState; // UI/Dock.h
class UIContext;
enum class Axis { Row, Column };
enum class Align { Start, Center, End, Stretch };

struct Length {
  enum class Mode { Auto, Px, Fill } mode = Mode::Auto;
  float value = 0; // px, or fill weight
  static Length Auto() { return {}; }
  static Length Px(float v) { return {Mode::Px, v}; }
  static Length Fill(float weight = 1) { return {Mode::Fill, weight}; }
};

struct Widget {
  WidgetKind kind = WidgetKind::Box;
  std::string id, text, text2;        // text2: secondary text (type column, node subtitle, cover subtitle)
  std::vector<std::string> items;     // Tabs / Panel header tabs; Node: pins "in|exec|Exec" / "out|float|Value"
  int selectedIndex = 0, indent = 0;
  // Label: 1 dim, 2 component header (.bdg-comp-head), 3 caption (.bdg-caption)
  // Tabs: 1 workspace tabs, 2 workspace bar (.bdg-wsbar)   Button: 1 status dot, 2 menu bar (flat)   Node: stripe kind
  // Badge: 0 pill, 1 ok, 2 warn, 3 err; 10 Unity, 11 Unreal, 12 Blender, 13 native (origin badges)
  int variant = 0;
  float fontSize = 0;                 // Label: 0 = body style
  float lineHeight = 0;               // Label: multiple of fontSize (0 = 1.45 body, -1 = font's normal line height)
  float letterSpacing = 0;            // Label: em units (CSS letter-spacing)
  bool bold = false;                  // Label: weight 600+ (drawn with the regular face for now)
  std::function<void(Widget&, UIContext&)> onDraw; // Custom: draws inside its rect (viewports, previews)
  bool primary = false, selected = false, focusable = false;
  bool collapsed = false;             // Card: body hidden, header still shown
  bool hidden = false;                // not laid out, drawn or hit (e.g. unselected dock tab)
  float scrollY = 0;                  // dock panel content: scrolled distance (wheel, ScrollIntoView)
  std::shared_ptr<DockState> dock;    // Dock: split tree of tabbed panels (children = panel contents)

  Axis axis = Axis::Column;
  Align crossAlign = Align::Stretch;
  float gap = 0, padL = 0, padT = 0, padR = 0, padB = 0;
  Length width, height;

  std::vector<std::unique_ptr<Widget>> children;
  std::function<void(Widget&)> onClick;
  // Right-click (E-4): x, y in UI units. Usually opens a menu with UIContext::OpenMenu.
  std::function<void(Widget&, float x, float y)> onContext;
  // ListRow as a tree row (Hierarchy, E-2). tree: one entry per level, 0 nothing, 1 a guide line passes (an ancestor
  // has siblings below), 2 this row's branch with more siblings after it, 3 this row's branch as the last one (└).
  // fold: -1 no children, 0 open (▼), 1 folded (▶). Non-empty tree replaces `indent` for the row's offset.
  std::vector<uint8_t> tree;
  int fold = -1;
  bool draggable = false;                     // can be picked up and dropped on a widget with onDrop
  std::function<void(Widget& dropped)> onDrop; // something was dropped on this widget (Hierarchy reparent, ...)

  // Text editing state (Field). Byte offsets into text2, always on UTF-8 boundaries.
  size_t cursor = 0, anchor = 0;
  std::string composing;                      // IME composition shown at the cursor, not yet committed
  std::vector<std::string> undoStack, redoStack;
  std::function<void(Widget&)> onCommit;      // Enter pressed or focus left after edits
  // Field as a slider (BPROPERTY Range): drag inside the box sets the value; typing still works.
  bool slider = false, sliderInteger = false;
  bool marked = false; // Field: changed while playing (3-6b): accent label and a dot
  bool overridden = false; // Field: differs from its prefab (6-1): a blue bar at the left edge (Unity's)
  double sliderMin = 0, sliderMax = 1;
  std::function<void(Widget&)> onChange;      // value changed while dragging (each step; edits merge into one undo)
  // Inspector conveniences (E-4b). axes: the value is "x, y, z" — drawn as one box per number with an X/Y/Z/W tag
  // (axis colours), a click selects that number. scrub: dragging on the label (or an axis tag) changes the number
  // (Shift = finer; sliderInteger = whole numbers). swatch: a colour box at the end of the input; a click on it
  // calls onSwatch (the Inspector opens its picker).
  int axes = 0;
  bool scrub = false, swatch = false;
  Color swatchColor{0, 0, 0, 0};
  std::function<void(Widget&)> onSwatch;
  bool edited = false;
  // Hover help: shown in a small box after the mouse rests on the widget (BPROPERTY Tooltip, button hints).
  std::string tooltip;
  // Choice (picking instead of typing): text = label, text2 = current value (kMixed-style "—" = several values),
  // items = the options. variant 0 combo box (popup list, search box when long), 1 segmented buttons (2-4 short
  // options side by side), 2 toggle switch (items {"false","true"}). onChange runs after the value changed.

  // Computed each frame.
  float x = 0, y = 0, w = 0, h = 0, prefW = 0, prefH = 0;
  bool hovered = false, pressed = false;
  std::vector<float> itemX; // Tabs: tab right edges for hit testing

  Widget& Add(std::unique_ptr<Widget> child) { children.push_back(std::move(child)); return *children.back(); }
  Widget& Pad(float all) { padL = padT = padR = padB = all; return *this; }
};

// Builders (return owning pointers to pass to Widget::Add).
std::unique_ptr<Widget> MakeBox(Axis axis, float gap = 0);
std::unique_ptr<Widget> MakeLabel(std::string text);
std::unique_ptr<Widget> MakePanel(std::string id, std::string title);
std::unique_ptr<Widget> MakeTabs(std::string id, std::vector<std::string> tabs, int selected = 0);
std::unique_ptr<Widget> MakeButton(std::string id, std::string text, bool primary = false);
std::unique_ptr<Widget> MakeField(std::string id, std::string label, std::string value);
std::unique_ptr<Widget> MakeListRow(std::string id, std::string text, std::string type = {}, int indent = 0);
std::unique_ptr<Widget> MakeBadge(std::string text, int variant = 0); // 1 = ok
std::unique_ptr<Widget> MakeNode(std::string id, std::string title, std::string subtitle, int variant,
                                 std::vector<std::string> pins); // variant: 0 flow, 1 event, 2 function, 3 pure
std::unique_ptr<Widget> MakeCover(std::string id, std::string title, std::string subtitle); // 960x300 brand cover, children <id>.name / <id>.tag
std::unique_ptr<Widget> MakePanelTabs(std::string id, std::vector<std::string> tabs, int selected = 0);
std::unique_ptr<Widget> MakeCustom(std::string id, std::function<void(Widget&, UIContext&)> draw);
// Collapsible component card (Inspector). badge: language tag such as "C#", "C++", "Lua"; empty = built-in.
std::unique_ptr<Widget> MakeCard(std::string id, std::string title, std::string badge = {});
// Choices: pick a value with the mouse or the arrow keys instead of typing it.
std::unique_ptr<Widget> MakeCombo(std::string id, std::string label, std::vector<std::string> options, std::string value);
std::unique_ptr<Widget> MakeSegmented(std::string id, std::string label, std::vector<std::string> options, std::string value);
std::unique_ptr<Widget> MakeToggle(std::string id, std::string label, bool on);
// Chooses combo or segmented by option count and length (short sets of up to 4 become segmented buttons).
std::unique_ptr<Widget> MakeChoice(std::string id, std::string label, std::vector<std::string> options, std::string value);

// ---- Context: layout, drawing, input, focus ----------------------------------------------
class UIContext {
public:
  UIContext(Batch2D& batch, Font* font, const Theme& theme) : batch_(batch), font_(font), theme_(theme) {}

  void SetRoot(std::unique_ptr<Widget> root) { root_ = std::move(root); focused_ = nullptr; }
  void SetWindow(Window* window) { window_ = window; } // clipboard + IME candidate window placement
  Window* GetWindow() const { return window_; }
  bool ClickCtrl() const { return clickCtrl_; }        // modifiers of the click being handled (multi-select)
  bool ClickShift() const { return clickShift_; }
  bool ClickAlt() const { return clickAlt_; }
  float ClickX() const { return clickX_; }               // UI units, of the click being handled
  bool OnTreeArrow(const Widget& row, float x) const;    // x is on a tree row's fold arrow (E-2)
  // Popup menu (E-4): drawn above everything at (x, y); a click on an item runs it, a click elsewhere or Esc closes.
  // keys: the item's shortcut, shown dimmed at the right ("Ctrl+S"); "" = none. header: a group title row (caption
  // style, not an item; hidden while a search filter is typed).
  struct MenuItem { std::string label; std::function<void()> run; bool enabled = true; bool checked = false; std::string keys = {}; bool header = false; };
  // search: a filter box on top (type to narrow the list; Up/Down/Enter pick). Long combo lists use it.
  void OpenMenu(float x, float y, std::vector<MenuItem> items, bool search = false);
  void CloseMenu() { menu_.clear(); menuAll_.clear(); menuFilter_.clear(); }
  bool MenuOpen() const { return !menu_.empty(); }
  bool MenuItemRect(size_t i, float& x, float& y, float& w, float& h) const; // test hook
  // Modal dialog: a box in the middle over a dimmed window; nothing else takes input until a button closes it.
  // Enter = the primary button, Esc = the cancel button. check: an optional checkbox ("다시 묻지 않기").
  // A dialog may list items with checkboxes (Unreal's "Save Content": what to save). A button's run gets the
  // dialog as it was closed, so it can read which items were ticked.
  struct Dialog;
  struct DialogButton { std::string label; std::function<void(const Dialog&)> run; bool primary = false, cancel = false; };
  struct DialogItem { std::string label, detail; bool checked = true; };
  struct Dialog {
    std::string title, message;
    std::vector<DialogItem> items;
    std::vector<DialogButton> buttons;
    std::string check; // checkbox label ("" = none)
    bool checked = false;
    std::function<void(bool)> onCheck;
  };
  void OpenDialog(Dialog d);
  void CloseDialog() { dialog_.reset(); }
  bool DialogOpen() const { return dialog_ != nullptr; }
  const Dialog* CurrentDialog() const { return dialog_.get(); }
  bool DialogButtonRect(size_t i, float& x, float& y, float& w, float& h) const; // test hook
  bool DialogItemRect(size_t i, float& x, float& y, float& w, float& h) const;   // checkbox row i (test hook)
  bool ModalOpen() const { return DialogOpen() || MenuOpen(); } // panels outside the UI tree should leave input alone
  // Byte ranges of the numbers in "x, y, z" (spaces trimmed); empty when the text has another shape.
  static std::vector<std::pair<size_t, size_t>> FieldParts(const std::string& v, int count);
  bool AxisBox(const Widget& f, int k, float& x, float& w) const; // a vector field's box k (UI units)
  bool SwatchBox(const Widget& f, float& x, float& y, float& w, float& h) const;
  bool Scrubbing() const { return scrub_ != nullptr; }
  const std::vector<MenuItem>& Menu() const { return menu_; }
  void SetClipboard(const std::string& text);
  std::string Clipboard() const;
  void SetScale(float scale) { scale_ = scale; }       // UI units -> drawable pixels (display scale)
  Widget* Root() { return root_.get(); }
  Widget* Find(const std::string& id);
  Widget* Focused() const { return focused_; }
  void SetKeyHandler(std::function<bool(const InputEvent&)> h) { onKey_ = std::move(h); } // keys no widget used
  void ForgetRemovedWidgets(); // call after replacing a subtree: drops hover/press/focus on removed widgets
  // Accessibility and keyboard navigation (queue 1-6).
  void Focus(Widget* w) { SetFocus(w); }             // nullptr clears focus
  void ScrollIntoView(const Widget& w);              // scrolls its dock panel so it is fully visible (next layout)
  void Invoke(Widget& w) { Activate(w, -1, -1); }   // same as a click / Enter
  void SetFocusListener(std::function<void(Widget*)> f) { onFocus_ = std::move(f); }
  std::vector<Widget*> FocusOrder();                // visible focusable widgets in Tab order
  float Scale() const { return scale_; }            // UI units -> drawable pixels
  static const char* AccessibleRole(const Widget& w); // "button", "tab", "textbox", "listitem", ...
  static std::string AccessibleName(const Widget& w); // what a screen reader says
  int NextTooltipMs() const; // ms until a pending tooltip appears (-1 = none): idle wake-up timer
  void ChoiceSet(Widget& w, int index); // pick option index (runs onChange)
  bool ChoiceSegment(const Widget& w, int k, float& x, float& w2) const; // segmented button k (UI units)
  int ChoiceStyle(const Widget& w) const; // how it draws now: 0 combo, 1 segmented (names fit), 2 toggle
  int NextBlinkMs() const; // ms until the text caret toggles (-1 = no focused text field): idle wake-up timer

  void Layout(float width, float height);
  void HandleEvents(const std::vector<InputEvent>& events);
  void Draw();
  bool DumpJSONL(const char* path) const; // widget boxes for the design parity test

  // For Custom widgets.
  Batch2D& Batch() { return batch_; }
  const Theme& Tokens() const { return theme_; }
  float Text(float x, float y, float size, Color c, const std::string& s, bool mono = false);
  // Editor icons (UI/IconAtlas). Icon() draws one by name in a size x size box; false when there is no such icon
  // (the caller draws its fallback).
  void SetIcons(const IconAtlas* icons);
  bool Icon(const std::string& name, float x, float y, float size, Color tint = {1, 1, 1, 1});
  bool HasIcon(const std::string& name) const;
  float Measure(float size, const std::string& s, bool mono = false) const;
  float MeasureSpaced(float size, const std::string& s, float letterSpacingEm) const; // + CSS letter-spacing per character
  float NormalLineHeight(float size) const;

private:
  const IconAtlas* icons_ = nullptr;
  void Measure(Widget& w);
  void Arrange(Widget& w, float x, float y, float width, float height);
  void DrawWidget(Widget& w);
  Widget* HitTest(Widget& w, float x, float y);
  void CollectFocusable(Widget& w, std::vector<Widget*>& out);
  void Activate(Widget& w, float mouseX, float mouseY);

  // Field editing (one shared text input implementation for every text box in the editor).
  void SetFocus(Widget* w);
  bool FieldKey(Widget& f, const InputEvent& e);
  void FieldInsert(Widget& f, const std::string& s);
  void FieldPushUndo(Widget& f);
  size_t FieldHit(const Widget& f, float mouseX) const;
  void FieldInputBox(const Widget& f, float& x, float& y, float& w, float& h) const;

  // Docking (UI/Dock.cpp).
  void ArrangeDock(Widget& w);
  void DrawDock(Widget& w);
  void DrawDockOverlay(Widget& w);
  bool DockMouse(const InputEvent& e); // true = consumed (splitter or tab drag)
  std::vector<Widget*> docks_;         // filled by Layout
  std::function<bool(const InputEvent&)> onKey_;
  std::function<void(Widget*)> onFocus_;
  Widget* dragSource_ = nullptr; // pressed draggable widget
  Widget* dropTarget_ = nullptr; // widget under the mouse that accepts the drop
  bool dragging_ = false;
  bool clickCtrl_ = false, clickShift_ = false, clickAlt_ = false;
  std::vector<MenuItem> menu_;
  std::string focusedId_;  // id of focused_ (a rebuild may reuse its address)
  Widget* scrub_ = nullptr; // label drag in progress (E-4b)
  int scrubAxis_ = -1;
  double scrubStart_ = 0;
  float scrubX_ = 0;
  void ScrubTo(float x, bool fine);
  float menuX_ = 0, menuY_ = 0, menuW_ = 0;
  int menuHot_ = -1, menuPressed_ = -1;
  int menuTop_ = 0, menuRows_ = 0; // first row shown and how many fit (long menus scroll: wheel, arrow keys)
  void MenuKeepHotVisible();
  Widget* ContextHit(Widget& w, float x, float y);
  bool MenuEvent(const InputEvent& e); // true: the open menu used it
  void DrawMenu();
  void FilterMenu();
  std::unique_ptr<Dialog> dialog_;
  int dialogHot_ = -1, dialogPressed_ = -1;
  bool DialogEvent(const InputEvent& e);
  void DrawDialog();
  void DialogBox(float& x, float& y, float& w, float& h, std::vector<std::string>* lines = nullptr) const;
  std::vector<std::string> Wrap(const std::string& text, float size, float maxW) const;
  void OpenChoice(Widget& w);
  void DrawChoice(Widget& w);
  void DrawTooltip();
  bool ChoiceKey(Widget& w, const InputEvent& e);
  std::vector<MenuItem> menuAll_;  // unfiltered items of a search menu
  std::string menuFilter_;
  bool menuSearch_ = false;
  long long hoverSince_ = 0;       // steady ms when hovered_ last changed
  float clickX_ = 0;
  Widget* sliderDrag_ = nullptr;
  void SliderSet(Widget& f, float mouseX);
  float pressX_ = 0, pressY_ = 0, dragX_ = 0, dragY_ = 0;
  Widget* DropTargetAt(float x, float y);
  void DrawDragOverlay();

  Window* window_ = nullptr;
  float scale_ = 1.f;
  Batch2D& batch_;
  Font* font_;
  const Theme& theme_;
  std::unique_ptr<Widget> root_;
  Widget* hovered_ = nullptr;
  Widget* pressed_ = nullptr;
  Widget* focused_ = nullptr;
};
}
