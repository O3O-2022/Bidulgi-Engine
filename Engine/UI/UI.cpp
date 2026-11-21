#include "UI/UI.h"
#include "UI/Theme.h"
#include "Render2D/Batch2D.h"
#include "Text/Font.h"
#include <algorithm>
#include <cstdio>

namespace bdg {
// ---- Builders ------------------------------------------------------------------------------
namespace {
std::unique_ptr<Widget> Make(WidgetKind k, std::string id = {}, std::string text = {}) {
  auto w = std::make_unique<Widget>();
  w->kind = k; w->id = std::move(id); w->text = std::move(text);
  return w;
}
const char* KindName(WidgetKind k) {
  static const char* names[] = {"Box", "Label", "Panel", "Tabs", "Button", "Field", "ListRow", "Badge", "Node", "Cover"};
  return names[(int)k];
}
Color Alpha(Color c, float a) { c.a = a; return c; }
}

std::unique_ptr<Widget> MakeBox(Axis axis, float gap) { auto w = Make(WidgetKind::Box); w->axis = axis; w->gap = gap; return w; }
std::unique_ptr<Widget> MakeLabel(std::string text) { return Make(WidgetKind::Label, {}, std::move(text)); }
std::unique_ptr<Widget> MakePanel(std::string id, std::string title) { return Make(WidgetKind::Panel, std::move(id), std::move(title)); }
std::unique_ptr<Widget> MakeTabs(std::string id, std::vector<std::string> tabs, int selected) {
  auto w = Make(WidgetKind::Tabs, std::move(id)); w->items = std::move(tabs); w->selectedIndex = selected; w->focusable = true; return w;
}
std::unique_ptr<Widget> MakeButton(std::string id, std::string text, bool primary) {
  auto w = Make(WidgetKind::Button, std::move(id), std::move(text)); w->primary = primary; w->focusable = true; return w;
}
std::unique_ptr<Widget> MakeField(std::string id, std::string label, std::string value) {
  auto w = Make(WidgetKind::Field, std::move(id), std::move(label)); w->text2 = std::move(value); w->focusable = true; return w;
}
std::unique_ptr<Widget> MakeListRow(std::string id, std::string text, std::string type, int indent) {
  auto w = Make(WidgetKind::ListRow, std::move(id), std::move(text)); w->text2 = std::move(type); w->indent = indent; w->focusable = true; return w;
}
std::unique_ptr<Widget> MakeBadge(std::string text, int variant) { auto w = Make(WidgetKind::Badge, {}, std::move(text)); w->variant = variant; return w; }
std::unique_ptr<Widget> MakeNode(std::string id, std::string title, std::string subtitle, int variant, std::vector<std::string> pins) {
  auto w = Make(WidgetKind::Node, std::move(id), std::move(title)); w->text2 = std::move(subtitle); w->variant = variant;
  w->items = std::move(pins); w->focusable = true; return w;
}
std::unique_ptr<Widget> MakeCover(std::string title, std::string subtitle) { auto w = Make(WidgetKind::Cover, {}, std::move(title)); w->text2 = std::move(subtitle); return w; }

// ---- Helpers -------------------------------------------------------------------------------
float UIContext::Text(float x, float y, float size, Color c, const std::string& s) { return font_ ? font_->DrawText(batch_, x, y, size, c, s) : 0.f; }
float UIContext::Measure(float size, const std::string& s) const { return font_ ? font_->Measure(size, s) : s.size() * size * 0.55f; }

Widget* UIContext::Find(const std::string& id) {
  std::vector<Widget*> stack{root_.get()};
  while (!stack.empty()) {
    Widget* w = stack.back(); stack.pop_back();
    if (!w) continue;
    if (w->id == id) return w;
    for (auto& c : w->children) stack.push_back(c.get());
  }
  return nullptr;
}

// ---- Layout: measure (bottom-up preferred sizes), arrange (top-down rects, flex fill) -------
namespace {
struct NodePins { int in = 0, out = 0; };
NodePins CountPins(const Widget& w) {
  NodePins p;
  for (auto& s : w.items) (s.rfind("out|", 0) == 0 ? p.out : p.in)++;
  return p;
}
}

void UIContext::Measure(Widget& w) {
  const Theme& t = theme_;
  for (auto& c : w.children) Measure(*c);
  float s3 = t.N("space-3", 6), s5 = t.N("space-5", 10), s6 = t.N("space-6", 12);
  switch (w.kind) {
    case WidgetKind::Label:   w.prefW = Measure(t.FontSize("body", 13), w.text); w.prefH = t.FontSize("body", 13) * 1.45f; break;
    case WidgetKind::Button:  w.prefW = Measure(t.FontSize("label", 12), w.text) + s5 * 2 + 2; w.prefH = 24; break;
    case WidgetKind::Field:   w.prefW = 240; w.prefH = std::max(26.f, t.N("size-row", 22) + 4); break;
    case WidgetKind::Badge:   w.prefW = Measure(t.FontSize("badge", 10), w.text) + s3 * 2 + 2; w.prefH = 18; break;
    case WidgetKind::ListRow:
      w.prefW = s5 * 2 + w.indent * 12.f + 18 + Measure(t.FontSize("control", 12.5f), w.text) +
                (w.text2.empty() ? 0 : 16 + Measure(11, w.text2));
      w.prefH = t.N("size-row", 22);
      break;
    case WidgetKind::Tabs: {
      float sum = 0;
      for (auto& it : w.items) sum += Measure(t.FontSize("label", 12), it) + s6 * 2;
      w.prefW = sum; w.prefH = t.N("size-panel-header", 30);
      break;
    }
    case WidgetKind::Node: {
      NodePins p = CountPins(w);
      float head = 5 + 16 + 5 + t.N("size-node-stripe", 3);
      w.prefW = std::max(150.f, Measure(12, w.text) + Measure(10.5f, w.text2) + s5 * 2 + 12);
      w.prefH = head + s3 * 2 + std::max(p.in, p.out) * 20.f;
      break;
    }
    case WidgetKind::Cover: w.prefW = 320; w.prefH = 112; break;
    case WidgetKind::Box:
    case WidgetKind::Panel: {
      if (w.kind == WidgetKind::Panel) { // header + body padding come from tokens
        w.padT = t.N("size-panel-header", 30) + t.N("space-2", 4);
        w.padB = t.N("space-2", 4);
      }
      float main = 0, cross = 0;
      int n = 0;
      for (auto& c : w.children) {
        float cw = c->width.mode == Length::Mode::Px ? c->width.value : c->prefW;
        float ch = c->height.mode == Length::Mode::Px ? c->height.value : c->prefH;
        main += w.axis == Axis::Row ? cw : ch;
        cross = std::max(cross, w.axis == Axis::Row ? ch : cw);
        ++n;
      }
      if (n > 1) main += w.gap * (n - 1);
      float padMain = w.axis == Axis::Row ? w.padL + w.padR : w.padT + w.padB;
      float padCross = w.axis == Axis::Row ? w.padT + w.padB : w.padL + w.padR;
      w.prefW = w.axis == Axis::Row ? main + padMain : cross + padCross;
      w.prefH = w.axis == Axis::Row ? cross + padCross : main + padMain;
      if (w.kind == WidgetKind::Panel) w.prefW = std::max(w.prefW, Measure(12, w.text) + 40);
      break;
    }
  }
  if (w.width.mode == Length::Mode::Px) w.prefW = w.width.value;
  if (w.height.mode == Length::Mode::Px) w.prefH = w.height.value;
}

void UIContext::Arrange(Widget& w, float x, float y, float width, float height) {
  w.x = x; w.y = y; w.w = width; w.h = height;
  if (w.children.empty()) return;
  bool row = w.axis == Axis::Row;
  float cx = x + w.padL, cy = y + w.padT;
  float cw = std::max(0.f, width - w.padL - w.padR), ch = std::max(0.f, height - w.padT - w.padB);
  float avail = (row ? cw : ch) - w.gap * (float)(w.children.size() - 1), fillWeight = 0;
  for (auto& c : w.children) {
    const Length& m = row ? c->width : c->height;
    if (m.mode == Length::Mode::Fill) fillWeight += m.value;
    else avail -= row ? c->prefW : c->prefH;
  }
  float pos = row ? cx : cy;
  for (auto& c : w.children) {
    const Length& m = row ? c->width : c->height;
    const Length& crossLen = row ? c->height : c->width;
    float mainSize = m.mode == Length::Mode::Fill ? std::max(0.f, avail) * (m.value / std::max(fillWeight, 1e-4f))
                                                  : (row ? c->prefW : c->prefH);
    float crossAvail = row ? ch : cw;
    bool stretch = (w.crossAlign == Align::Stretch && crossLen.mode != Length::Mode::Px) || crossLen.mode == Length::Mode::Fill;
    float crossSize = stretch ? crossAvail : std::min(crossAvail, row ? c->prefH : c->prefW);
    float crossOff = 0;
    if (!stretch && w.crossAlign == Align::Center) crossOff = (crossAvail - crossSize) * 0.5f;
    if (!stretch && w.crossAlign == Align::End) crossOff = crossAvail - crossSize;
    if (row) Arrange(*c, pos, cy + crossOff, mainSize, crossSize);
    else Arrange(*c, cx + crossOff, pos, crossSize, mainSize);
    pos += mainSize + w.gap;
  }
}

void UIContext::Layout(float width, float height) {
  if (!root_) return;
  Measure(*root_);
  Arrange(*root_, 0, 0, width, height);
}

// ---- Drawing (design system components) ----------------------------------------------------
void UIContext::DrawWidget(Widget& w) {
  const Theme& t = theme_;
  Batch2D& b = batch_;
  const float rsm = t.N("radius-sm", 3), rmd = t.N("radius-md", 4);
  const float s3 = t.N("space-3", 6), s5 = t.N("space-5", 10), s6 = t.N("space-6", 12);
  switch (w.kind) {
    case WidgetKind::Box: break;
    case WidgetKind::Label: Text(w.x, w.y + 2, t.FontSize("body", 13), t.C("fg"), w.text); break;
    case WidgetKind::Panel: {
      float hh = t.N("size-panel-header", 30), fs = t.FontSize("label", 12);
      b.Rect(w.x, w.y, w.w, w.h, t.C("panel"), rsm, 1, t.C("line"));
      b.Rect(w.x + 1, w.y + 1, w.w - 2, hh - 1, t.C("win"));
      b.Rect(w.x + 1, w.y + hh - 1, w.w - 2, 1, t.C("line"));
      float tw = Measure(fs, w.text) + s6 * 2;
      b.Rect(w.x + 1, w.y + 1, tw, hh - 1, t.C("panel"));
      b.Rect(w.x + 1, w.y + 1, tw, 2, t.C("accent"));
      Text(w.x + 1 + s6, w.y + (hh - fs) * 0.5f - 1, fs, t.C("fg"), w.text);
      break;
    }
    case WidgetKind::Tabs: {
      float fs = t.FontSize("label", 12), px = w.x;
      b.Rect(w.x, w.y, w.w, w.h, t.C("win"), rsm);
      w.itemX.clear();
      for (size_t i = 0; i < w.items.size(); ++i) {
        float tw = Measure(fs, w.items[i]) + s6 * 2;
        bool on = (int)i == w.selectedIndex;
        if (on) { b.Rect(px, w.y, tw, w.h, t.C("panel")); b.Rect(px, w.y, tw, 2, t.C("accent")); }
        Text(px + s6, w.y + (w.h - fs) * 0.5f - 1, fs, on ? t.C("fg") : t.C("fg-dim"), w.items[i]);
        px += tw;
        w.itemX.push_back(px);
      }
      break;
    }
    case WidgetKind::Button: {
      float fs = t.FontSize("label", 12);
      Color fill = w.primary ? t.C("accent") : t.C("raise");
      Color border = w.primary ? t.C("accent") : (w.hovered ? t.C("fg-faint") : t.C("line-strong"));
      if (w.pressed) fill = Alpha(fill, 0.8f);
      b.Rect(w.x, w.y, w.w, w.h, fill, rmd, 1, border);
      Text(w.x + (w.w - Measure(fs, w.text)) * 0.5f, w.y + (w.h - fs) * 0.5f - 1, fs, w.primary ? t.C("on-accent") : t.C("fg"), w.text);
      break;
    }
    case WidgetKind::Field: {
      float fs = t.FontSize("label", 12), row = t.N("size-row", 22);
      float labelW = std::max(80.f, w.w * 0.4f), ix = w.x + s6 + labelW + t.N("space-4", 8);
      float iy = w.y + (w.h - row) * 0.5f, iw = w.x + w.w - s5 - ix;
      Text(w.x + s6, w.y + (w.h - fs) * 0.5f - 1, fs, t.C("fg-dim"), w.text);
      b.Rect(ix, iy, iw, row, t.C("sunk"), rsm, 1, focused_ == &w ? t.C("accent") : t.C("line"));
      Text(ix + s3, iy + (row - fs) * 0.5f - 1, fs, t.C("fg"), w.text2);
      break;
    }
    case WidgetKind::ListRow: {
      float fs = t.FontSize("control", 12.5f);
      if (w.selected) b.Rect(w.x, w.y, w.w, w.h, t.C("selection"));
      else if (w.hovered) b.Rect(w.x, w.y, w.w, w.h, t.C("raise"));
      float kx = w.x + s5 + w.indent * 12.f;
      b.Rect(kx, w.y + (w.h - 11) * 0.5f, 11, 11, {0, 0, 0, 0}, 2, 1.5f, t.C("fg-dim"));
      Text(kx + 18, w.y + (w.h - fs) * 0.5f - 1, fs, t.C("fg"), w.text);
      if (!w.text2.empty()) Text(w.x + w.w - s5 - Measure(11, w.text2), w.y + (w.h - 11) * 0.5f - 1, 11, t.C("fg-faint"), w.text2);
      break;
    }
    case WidgetKind::Badge: {
      float fs = t.FontSize("badge", 10);
      b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"), t.N("radius-pill", 12), 1, t.C("line"));
      Text(w.x + s3 + 1, w.y + (w.h - fs) * 0.5f - 1, fs, w.variant == 1 ? t.C("ok") : t.C("fg-dim"), w.text);
      break;
    }
    case WidgetKind::Node: {
      static const char* stripes[] = {"node-flow", "node-event", "node-function", "node-pure"};
      float stripe = t.N("size-node-stripe", 3), head = 5 + 16 + 5 + stripe;
      b.Rect(w.x + 2, w.y + 4, w.w, w.h, {0, 0, 0, 0.35f}, rmd);                       // shadow-node (approx.)
      b.Rect(w.x, w.y, w.w, w.h, t.C("panel"), rmd, 1, t.C("line-strong"));
      b.Rect(w.x + 1, w.y + 1, w.w - 2, head - 1, t.C("win"), rmd);
      b.Rect(w.x + 1, w.y + 1, w.w - 2, stripe, t.C(stripes[w.variant & 3]), rmd);
      b.Rect(w.x + 1, w.y + head - 1, w.w - 2, 1, t.C("line"));
      float tx = w.x + s5;
      tx += Text(tx, w.y + stripe + 6, 12, t.C("fg"), w.text) + 6;
      Text(tx, w.y + stripe + 7.5f, 10.5f, t.C("fg-dim"), w.text2);
      float inY = w.y + head + s3, outY = inY;
      for (auto& pin : w.items) {
        bool out = pin.rfind("out|", 0) == 0;
        size_t a = pin.find('|'), c = pin.find('|', a + 1);
        std::string type = pin.substr(a + 1, c - a - 1), label = pin.substr(c + 1);
        Color pc = t.C("pin-" + type);
        float py = out ? outY : inY;
        float cxp = out ? w.x + w.w - 9 - 10 : w.x + 9;
        b.Rect(cxp, py + 3, 10, 10, {0, 0, 0, 0}, 5, 2, pc);
        float lw = Measure(11.5f, label);
        Text(out ? cxp - 6 - lw : cxp + 16, py, 11.5f, t.C("fg"), label);
        (out ? outY : inY) += 20;
      }
      if (w.selected) b.Rect(w.x - 2, w.y - 2, w.w + 4, w.h + 4, {0, 0, 0, 0}, rmd + 2, 2, t.C("accent"));
      break;
    }
    case WidgetKind::Cover: {
      b.Rect(w.x, w.y, w.w, w.h, t.C("raise"), t.N("radius-window", 8), 1, t.C("line-strong"));
      b.Rect(w.x + 20, w.y + 22, 36, 4, t.C("accent"), 2);
      Text(w.x + 20, w.y + 36, t.FontSize("page-title", 20), t.C("fg"), w.text);
      Text(w.x + 20, w.y + 68, t.FontSize("body", 13), t.C("fg-dim"), w.text2);
      break;
    }
  }
  if (focused_ == &w && w.kind != WidgetKind::Field)
    b.Rect(w.x - 3, w.y - 3, w.w + 6, w.h + 6, {0, 0, 0, 0}, rmd + 3, 2, t.C("accent")); // focus-visible ring
  for (auto& c : w.children) DrawWidget(*c);
}

void UIContext::Draw() { if (root_) DrawWidget(*root_); }

// ---- Input ----------------------------------------------------------------------------------
Widget* UIContext::HitTest(Widget& w, float x, float y) {
  if (x < w.x || y < w.y || x >= w.x + w.w || y >= w.y + w.h) return nullptr;
  for (auto it = w.children.rbegin(); it != w.children.rend(); ++it)
    if (Widget* hit = HitTest(**it, x, y)) return hit;
  return (w.focusable || w.onClick) ? &w : nullptr;
}

void UIContext::CollectFocusable(Widget& w, std::vector<Widget*>& out) {
  if (w.focusable) out.push_back(&w);
  for (auto& c : w.children) CollectFocusable(*c, out);
}

void UIContext::Activate(Widget& w, float mouseX) {
  if (w.kind == WidgetKind::Tabs && mouseX >= 0) {
    for (size_t i = 0; i < w.itemX.size(); ++i)
      if (mouseX < w.itemX[i]) { w.selectedIndex = (int)i; break; }
  }
  if (w.kind == WidgetKind::ListRow || w.kind == WidgetKind::Node) {
    // Single selection among siblings of the same kind.
    std::vector<Widget*> stack{root_.get()};
    while (!stack.empty()) {
      Widget* p = stack.back(); stack.pop_back();
      for (auto& c : p->children) {
        if (c.get() == &w)
          for (auto& s : p->children) if (s->kind == w.kind) s->selected = false;
        stack.push_back(c.get());
      }
    }
    w.selected = true;
  }
  if (w.onClick) w.onClick(w);
}

void UIContext::HandleEvents(const std::vector<InputEvent>& events) {
  if (!root_) return;
  for (const InputEvent& e : events) {
    switch (e.type) {
      case InputEvent::Type::MouseMove: {
        Widget* hit = HitTest(*root_, e.x, e.y);
        if (hovered_ && hovered_ != hit) hovered_->hovered = false;
        hovered_ = hit;
        if (hit) hit->hovered = true;
        break;
      }
      case InputEvent::Type::MouseDown:
        if (e.button != 1) break;
        pressed_ = HitTest(*root_, e.x, e.y);
        if (pressed_) pressed_->pressed = true;
        focused_ = (pressed_ && pressed_->focusable) ? pressed_ : nullptr;
        break;
      case InputEvent::Type::MouseUp: {
        if (e.button != 1) break;
        Widget* hit = HitTest(*root_, e.x, e.y);
        if (pressed_) {
          pressed_->pressed = false;
          if (hit == pressed_) Activate(*hit, e.x);
        }
        pressed_ = nullptr;
        break;
      }
      case InputEvent::Type::KeyDown: {
        if (e.key == Key::Tab) {
          std::vector<Widget*> list;
          CollectFocusable(*root_, list);
          if (list.empty()) break;
          auto it = std::find(list.begin(), list.end(), focused_);
          int i = it == list.end() ? (e.shift ? (int)list.size() - 1 : 0)
                                   : ((int)(it - list.begin()) + (e.shift ? -1 : 1) + (int)list.size()) % (int)list.size();
          focused_ = list[i];
        } else if (focused_ && (e.key == Key::Enter || e.key == Key::Space)) {
          Activate(*focused_, -1);
        } else if (focused_ && focused_->kind == WidgetKind::Tabs && (e.key == Key::Left || e.key == Key::Right)) {
          int n = (int)focused_->items.size();
          if (n) focused_->selectedIndex = (focused_->selectedIndex + (e.key == Key::Right ? 1 : n - 1)) % n;
        }
        break;
      }
      case InputEvent::Type::Text: break; // text editing arrives with queue item 0-8
    }
  }
}

// ---- Dump -------------------------------------------------------------------------------
bool UIContext::DumpJSONL(const char* path) const {
  std::FILE* f = std::fopen(path, "w");
  if (!f || !root_) { if (f) std::fclose(f); return false; }
  std::vector<std::pair<const Widget*, int>> stack{{root_.get(), 0}};
  while (!stack.empty()) {
    auto [w, depth] = stack.back(); stack.pop_back();
    std::fprintf(f, "{\"id\":\"%s\",\"kind\":\"%s\",\"depth\":%d,\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f}\n",
                 w->id.c_str(), KindName(w->kind), depth, w->x, w->y, w->w, w->h);
    for (auto it = w->children.rbegin(); it != w->children.rend(); ++it) stack.push_back({it->get(), depth + 1});
  }
  std::fclose(f);
  return true;
}
}
