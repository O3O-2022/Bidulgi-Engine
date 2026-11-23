#pragma once
#include "Platform/Window.h"
#include "RHI/RHI.h"
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
enum class WidgetKind { Box, Label, Panel, Tabs, Button, Field, ListRow, Badge, Node, Cover, Custom, Card, Dock };
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
  // Tabs: 1 workspace tabs, 2 workspace bar (.bdg-wsbar)   Button: 1 status dot   Node: stripe kind
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
  std::shared_ptr<DockState> dock;    // Dock: split tree of tabbed panels (children = panel contents)

  Axis axis = Axis::Column;
  Align crossAlign = Align::Stretch;
  float gap = 0, padL = 0, padT = 0, padR = 0, padB = 0;
  Length width, height;

  std::vector<std::unique_ptr<Widget>> children;
  std::function<void(Widget&)> onClick;

  // Text editing state (Field). Byte offsets into text2, always on UTF-8 boundaries.
  size_t cursor = 0, anchor = 0;
  std::string composing;                      // IME composition shown at the cursor, not yet committed
  std::vector<std::string> undoStack, redoStack;
  std::function<void(Widget&)> onCommit;      // Enter pressed or focus left after edits
  bool edited = false;

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

// ---- Context: layout, drawing, input, focus ----------------------------------------------
class UIContext {
public:
  UIContext(Batch2D& batch, Font* font, const Theme& theme) : batch_(batch), font_(font), theme_(theme) {}

  void SetRoot(std::unique_ptr<Widget> root) { root_ = std::move(root); focused_ = nullptr; }
  void SetWindow(Window* window) { window_ = window; } // clipboard + IME candidate window placement
  void SetScale(float scale) { scale_ = scale; }       // UI units -> drawable pixels (display scale)
  Widget* Root() { return root_.get(); }
  Widget* Find(const std::string& id);
  Widget* Focused() const { return focused_; }
  void SetKeyHandler(std::function<bool(const InputEvent&)> h) { onKey_ = std::move(h); } // keys no widget used
  void ForgetRemovedWidgets(); // call after replacing a subtree: drops hover/press/focus on removed widgets
  // Accessibility and keyboard navigation (queue 1-6).
  void Focus(Widget* w) { SetFocus(w); }             // nullptr clears focus
  void Invoke(Widget& w) { Activate(w, -1, -1); }   // same as a click / Enter
  void SetFocusListener(std::function<void(Widget*)> f) { onFocus_ = std::move(f); }
  std::vector<Widget*> FocusOrder();                // visible focusable widgets in Tab order
  float Scale() const { return scale_; }            // UI units -> drawable pixels
  static const char* AccessibleRole(const Widget& w); // "button", "tab", "textbox", "listitem", ...
  static std::string AccessibleName(const Widget& w); // what a screen reader says
  int NextBlinkMs() const; // ms until the text caret toggles (-1 = no focused text field): idle wake-up timer

  void Layout(float width, float height);
  void HandleEvents(const std::vector<InputEvent>& events);
  void Draw();
  bool DumpJSONL(const char* path) const; // widget boxes for the design parity test

  // For Custom widgets.
  Batch2D& Batch() { return batch_; }
  const Theme& Tokens() const { return theme_; }
  float Text(float x, float y, float size, Color c, const std::string& s, bool mono = false);
  float Measure(float size, const std::string& s, bool mono = false) const;
  float MeasureSpaced(float size, const std::string& s, float letterSpacingEm) const; // + CSS letter-spacing per character
  float NormalLineHeight(float size) const;

private:
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
