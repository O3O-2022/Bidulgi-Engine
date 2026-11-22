#include "UI/UI.h"
#include "UI/Theme.h"
#include "Render2D/Batch2D.h"
#include "Text/Font.h"
#include <algorithm>
#include <chrono>
#include <cmath>
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
  static const char* names[] = {"Box", "Label", "Panel", "Tabs", "Button", "Field", "ListRow", "Badge", "Node", "Cover", "Custom", "Card"};
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
std::unique_ptr<Widget> MakePanelTabs(std::string id, std::vector<std::string> tabs, int selected) {
  auto w = Make(WidgetKind::Panel, std::move(id), tabs.empty() ? std::string() : tabs[selected]);
  w->items = std::move(tabs); w->selectedIndex = selected; return w;
}
std::unique_ptr<Widget> MakeCard(std::string id, std::string title, std::string badge) {
  auto w = Make(WidgetKind::Card, std::move(id), std::move(title)); w->text2 = std::move(badge); w->gap = 1; return w;
}
std::unique_ptr<Widget> MakeCustom(std::string id, std::function<void(Widget&, UIContext&)> draw) {
  auto w = Make(WidgetKind::Custom, std::move(id)); w->onDraw = std::move(draw); return w;
}

// ---- Helpers -------------------------------------------------------------------------------
float UIContext::Text(float x, float y, float size, Color c, const std::string& s, bool mono) { return font_ ? font_->DrawText(batch_, x, y, size, c, s, mono) : 0.f; }
float UIContext::Measure(float size, const std::string& s, bool mono) const { return font_ ? font_->Measure(size, s, mono) : s.size() * size * 0.55f; }

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
    case WidgetKind::Label: {
      float fs = w.fontSize > 0 ? w.fontSize : t.FontSize("body", 13);
      w.prefW = Measure(fs, w.text); w.prefH = fs * 1.45f; break;
    }
    case WidgetKind::Custom: w.prefW = 0; w.prefH = 0; break;
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
      float fs = w.variant == 1 ? t.FontSize("control", 12.5f) : t.FontSize("label", 12);
      for (auto& it : w.items) sum += Measure(fs, it) + s6 * 2 + (w.variant == 1 ? t.N("space-1", 2) : 0);
      w.prefW = sum; w.prefH = w.variant == 1 ? fs + s3 * 2 + 4 : t.N("size-panel-header", 30);
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
    case WidgetKind::Card:
    case WidgetKind::Panel: {
      if (w.kind == WidgetKind::Card) {
        w.axis = Axis::Column; w.padT = 30; w.padB = w.collapsed ? 0.f : 6.f; w.padL = w.padR = 4.f;
        if (w.collapsed) { w.prefW = 200; w.prefH = 30; break; }
      }
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
  if (w.children.empty() || w.collapsed) return;
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
    case WidgetKind::Label: {
      float fs = w.fontSize > 0 ? w.fontSize : t.FontSize("body", 13);
      Text(w.x, w.y + (w.h - fs) * 0.5f - 1, fs, w.variant == 1 ? t.C("fg-dim") : t.C("fg"), w.text);
      break;
    }
    case WidgetKind::Custom: if (w.onDraw) w.onDraw(w, *this); break;
    case WidgetKind::Card: {
      float fs = t.FontSize("control", 12.5f);
      b.Rect(w.x + 4, w.y + 2, w.w - 8, w.h - 4, t.C("panel"), t.N("radius-lg", 5), 1, t.C("line-strong"));
      b.Rect(w.x + 5, w.y + 3, w.w - 10, 26, w.hovered ? t.C("line-strong") : t.C("raise"), t.N("radius-lg", 5));
      float cx = w.x + 16, cy = w.y + 16;
      if (w.collapsed) { b.Line(cx - 2, cy - 4, cx + 2, cy, 1.5f, t.C("fg-dim")); b.Line(cx + 2, cy, cx - 2, cy + 4, 1.5f, t.C("fg-dim")); }
      else { b.Line(cx - 4, cy - 2, cx, cy + 2, 1.5f, t.C("fg-dim")); b.Line(cx, cy + 2, cx + 4, cy - 2, 1.5f, t.C("fg-dim")); }
      b.Rect(w.x + 27, w.y + 11, 10, 10, {0, 0, 0, 0}, 2, 1.5f, t.C("prefab"));
      Text(w.x + 44, w.y + (32 - fs) * 0.5f - 1, fs, t.C("fg"), w.text);
      if (!w.text2.empty()) {
        std::string key = w.text2 == "C#" ? "lang-cs" : w.text2 == "C++" ? "lang-cpp" : w.text2 == "Lua" ? "lang-lua"
                        : w.text2 == "Python" ? "lang-py" : "lang-graph";
        float bw = Measure(10, w.text2) + 12, bx = w.x + w.w - 14 - bw;
        Color c = t.C(key), soft = c;
        soft.a = 0.16f;
        b.Rect(bx, w.y + 8, bw, 16, soft, 8, 1, c);
        Text(bx + 6, w.y + 10, 10, c, w.text2);
      }
      break;
    }
    case WidgetKind::Panel: {
      float hh = t.N("size-panel-header", 30), fs = t.FontSize("label", 12);
      b.Rect(w.x, w.y, w.w, w.h, t.C("panel"), rsm, 1, t.C("line"));
      b.Rect(w.x + 1, w.y + 1, w.w - 2, hh - 1, t.C("win"));
      b.Rect(w.x + 1, w.y + hh - 1, w.w - 2, 1, t.C("line"));
      std::vector<std::string> one{w.text};
      const auto& tabs = w.items.empty() ? one : w.items;
      float px = w.x + 1;
      w.itemX.clear();
      for (size_t i = 0; i < tabs.size(); ++i) {
        float tw = Measure(fs, tabs[i]) + s6 * 2;
        bool on = w.items.empty() || (int)i == w.selectedIndex;
        if (on) { b.Rect(px, w.y + 1, tw, hh - 1, t.C("panel")); b.Rect(px, w.y + 1, tw, 2, t.C("accent")); }
        Text(px + s6, w.y + (hh - fs) * 0.5f - 1, fs, on ? t.C("fg") : t.C("fg-dim"), tabs[i]);
        px += tw;
        w.itemX.push_back(px);
      }
      break;
    }
    case WidgetKind::Tabs: {
      bool ws = w.variant == 1;
      float fs = ws ? t.FontSize("control", 12.5f) : t.FontSize("label", 12), px = w.x;
      if (!ws) b.Rect(w.x, w.y, w.w, w.h, t.C("win"), rsm);
      w.itemX.clear();
      for (size_t i = 0; i < w.items.size(); ++i) {
        float tw = Measure(fs, w.items[i]) + s6 * 2;
        bool on = (int)i == w.selectedIndex;
        if (on && ws) { b.Rect(px, w.y, tw, w.h, t.C("raise"), t.N("radius-lg", 5)); b.Rect(px + 2, w.y + w.h - 2, tw - 4, 2, t.C("accent")); }
        else if (on) { b.Rect(px, w.y, tw, w.h, t.C("panel")); b.Rect(px, w.y, tw, 2, t.C("accent")); }
        Text(px + s6, w.y + (w.h - fs) * 0.5f - 1, fs, on ? t.C("fg") : t.C("fg-dim"), w.items[i]);
        px += tw + (ws ? t.N("space-1", 2) : 0);
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
      float fs = t.FontSize("label", 12), ix, iy, iw, ih;
      FieldInputBox(w, ix, iy, iw, ih);
      bool focus = focused_ == &w;
      Text(w.x + s6, w.y + (w.h - fs) * 0.5f - 1, fs, t.C("fg-dim"), w.text);
      b.Rect(ix, iy, iw, ih, t.C("sunk"), rsm, 1, focus ? t.C("accent") : t.C("line"));
      float tx = ix + s3, ty = iy + (ih - fs) * 0.5f - 1;
      const std::string& v = w.text2;
      if (focus && w.cursor != w.anchor) { // selection
        size_t a = std::min(w.cursor, w.anchor), z = std::max(w.cursor, w.anchor);
        float sx = tx + Measure(fs, v.substr(0, a)), ex = tx + Measure(fs, v.substr(0, z));
        b.Rect(sx, iy + 3, ex - sx, ih - 6, t.C("accent-soft"));
      }
      std::string before = focus ? v.substr(0, w.cursor) : v, after = focus ? v.substr(w.cursor) : "";
      float px = tx + Text(tx, ty, fs, t.C("fg"), before);
      float caretX = px;
      if (focus && !w.composing.empty()) { // IME composition: drawn inline with an underline
        float cw = Text(px, ty, fs, t.C("fg"), w.composing);
        b.Rect(px, iy + ih - 5, cw, 1.5f, t.C("accent"));
        px += cw;
        caretX = px;
      }
      Text(px, ty, fs, t.C("fg"), after);
      if (focus) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if ((ms / 530) % 2 == 0 || !w.composing.empty()) b.Rect(caretX, iy + 4, 1.f, ih - 8, t.C("fg"));
        if (window_) window_->SetTextInputArea(ix * scale_, iy * scale_, iw * scale_, ih * scale_, (int)((caretX - ix) * scale_));
      }
      break;
    }
    case WidgetKind::ListRow: {
      float fs = t.FontSize("control", 12.5f);
      if (w.selected) b.Rect(w.x, w.y, w.w, w.h, t.C("selection"));
      else if (w.hovered) b.Rect(w.x, w.y, w.w, w.h, t.C("raise"));
      float kx = w.x + s5 + w.indent * 12.f;
      float ky = w.y + (w.h - 11) * 0.5f;
      Color dim = t.C("fg-dim");
      if (w.text2 == "Camera") {
        b.Rect(kx, ky + 2, 8, 7, {0, 0, 0, 0}, 1.5f, 1.5f, dim);
        b.Line(kx + 8, ky + 5.5f, kx + 11, ky + 3, 1.5f, dim); b.Line(kx + 8, ky + 5.5f, kx + 11, ky + 8, 1.5f, dim);
      } else if (w.text2 == "Light") {
        b.Rect(kx + 1, ky + 1, 9, 9, {0, 0, 0, 0}, 4.5f, 1.5f, t.C("warn")); b.Rect(kx + 4, ky + 4, 3, 3, t.C("warn"), 1.5f);
      } else if (w.text2 == "Prefab") {
        Color pc = t.C("prefab"), soft = pc; soft.a = 0.3f; b.Rect(kx, ky, 11, 11, soft, 2, 1.5f, pc);
      } else if (w.text2 == "Scene") {
        Color a = t.C("accent");
        b.Rect(kx, ky, 11, 11, {0, 0, 0, 0}, 2, 1.5f, a);
        b.Line(kx + 2, ky + 8, kx + 5, ky + 4, 1.2f, a); b.Line(kx + 5, ky + 4, kx + 9, ky + 8, 1.2f, a);
      } else if (w.text2 == "Mesh") {
        const float P[6][2] = {{5.5f, 0}, {11, 3}, {11, 8}, {5.5f, 11}, {0, 8}, {0, 3}};
        for (int i = 0; i < 6; ++i) b.Line(kx + P[i][0], ky + P[i][1], kx + P[(i + 1) % 6][0], ky + P[(i + 1) % 6][1], 1.3f, dim);
        b.Line(kx, ky + 3, kx + 5.5f, ky + 6, 1.3f, dim); b.Line(kx + 11, ky + 3, kx + 5.5f, ky + 6, 1.3f, dim);
        b.Line(kx + 5.5f, ky + 6, kx + 5.5f, ky + 11, 1.3f, dim);
      } else {
        b.Rect(kx, ky, 11, 11, {0, 0, 0, 0}, 2, 1.5f, dim);
      }
      Text(kx + 18, w.y + (w.h - fs) * 0.5f - 1, fs, t.C("fg"), w.text);
      if (!w.text2.empty() && w.text2 != "Mesh") Text(w.x + w.w - s5 - Measure(11, w.text2), w.y + (w.h - 11) * 0.5f - 1, 11, t.C("fg-faint"), w.text2);
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
  if (!w.collapsed) for (auto& c : w.children) DrawWidget(*c);
}

void UIContext::Draw() { if (root_) DrawWidget(*root_); }

// ---- Input ----------------------------------------------------------------------------------
Widget* UIContext::HitTest(Widget& w, float x, float y) {
  if (x < w.x || y < w.y || x >= w.x + w.w || y >= w.y + w.h) return nullptr;
  if (w.kind == WidgetKind::Card && y < w.y + 30) return &w;
  if (!w.collapsed)
    for (auto it = w.children.rbegin(); it != w.children.rend(); ++it)
      if (Widget* hit = HitTest(**it, x, y)) return hit;
  bool headerTabs = w.kind == WidgetKind::Panel && !w.items.empty() && y < w.y + theme_.N("size-panel-header", 30);
  return (w.focusable || w.onClick || headerTabs) ? &w : nullptr;
}

void UIContext::CollectFocusable(Widget& w, std::vector<Widget*>& out) {
  if (w.focusable) out.push_back(&w);
  for (auto& c : w.children) CollectFocusable(*c, out);
}

void UIContext::Activate(Widget& w, float mouseX, float mouseY) {
  (void)mouseY;
  if (w.kind == WidgetKind::Card) { w.collapsed = !w.collapsed; return; }
  if ((w.kind == WidgetKind::Tabs || w.kind == WidgetKind::Panel) && mouseX >= 0 && !w.itemX.empty()) {
    for (size_t i = 0; i < w.itemX.size(); ++i)
      if (mouseX < w.itemX[i]) { w.selectedIndex = (int)i; break; }
    if (w.kind == WidgetKind::Panel) w.text = w.items[w.selectedIndex];
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
        SetFocus((pressed_ && pressed_->focusable) ? pressed_ : nullptr);
        if (focused_ && focused_->kind == WidgetKind::Field) focused_->cursor = focused_->anchor = FieldHit(*focused_, e.x);
        break;
      case InputEvent::Type::MouseUp: {
        if (e.button != 1) break;
        Widget* hit = HitTest(*root_, e.x, e.y);
        if (pressed_) {
          pressed_->pressed = false;
          if (hit == pressed_) Activate(*hit, e.x, e.y);
        }
        pressed_ = nullptr;
        break;
      }
      case InputEvent::Type::KeyDown: {
        if (focused_ && focused_->kind == WidgetKind::Field && FieldKey(*focused_, e)) break;
        if (e.key == Key::Tab) {
          std::vector<Widget*> list;
          CollectFocusable(*root_, list);
          if (list.empty()) break;
          auto it = std::find(list.begin(), list.end(), focused_);
          int i = it == list.end() ? (e.shift ? (int)list.size() - 1 : 0)
                                   : ((int)(it - list.begin()) + (e.shift ? -1 : 1) + (int)list.size()) % (int)list.size();
          SetFocus(list[i]);
        } else if (focused_ && focused_->kind != WidgetKind::Field && (e.key == Key::Enter || e.key == Key::Space)) {
          Activate(*focused_, -1, -1);
        } else if (focused_ && focused_->kind == WidgetKind::Tabs && (e.key == Key::Left || e.key == Key::Right)) {
          int n = (int)focused_->items.size();
          if (n) focused_->selectedIndex = (focused_->selectedIndex + (e.key == Key::Right ? 1 : n - 1)) % n;
        }
        break;
      }
      case InputEvent::Type::Text:
        if (focused_ && focused_->kind == WidgetKind::Field) { focused_->composing.clear(); FieldInsert(*focused_, e.text); }
        break;
      case InputEvent::Type::TextEditing:
        if (focused_ && focused_->kind == WidgetKind::Field) focused_->composing = e.text;
        break;
    }
  }
}

// ---- Text input (shared by every Field) -------------------------------------------------
namespace {
size_t PrevBoundary(const std::string& s, size_t i) {
  if (i == 0) return 0;
  do { --i; } while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80);
  return i;
}
size_t NextBoundary(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  do { ++i; } while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80);
  return i;
}
}

void UIContext::SetFocus(Widget* w) {
  if (focused_ == w) return;
  if (focused_ && focused_->kind == WidgetKind::Field) {
    focused_->composing.clear();
    if (focused_->edited && focused_->onCommit) focused_->onCommit(*focused_);
    focused_->edited = false;
  }
  focused_ = w;
  if (w && w->kind == WidgetKind::Field) { w->cursor = w->anchor = w->text2.size(); w->composing.clear(); }
}

void UIContext::FieldInputBox(const Widget& f, float& x, float& y, float& w, float& h) const {
  const Theme& t = theme_;
  float labelW = std::max(80.f, f.w * 0.4f);
  h = t.N("size-row", 22);
  x = f.x + t.N("space-6", 12) + labelW + t.N("space-4", 8);
  y = f.y + (f.h - h) * 0.5f;
  w = f.x + f.w - t.N("space-5", 10) - x;
}

size_t UIContext::FieldHit(const Widget& f, float mouseX) const {
  float x, y, w, h, fs = theme_.FontSize("label", 12);
  FieldInputBox(f, x, y, w, h);
  float local = mouseX - (x + theme_.N("space-3", 6));
  size_t best = 0;
  float bestDist = 1e9f;
  for (size_t i = 0;; i = NextBoundary(f.text2, i)) {
    float d = std::abs(Measure(fs, f.text2.substr(0, i)) - local);
    if (d < bestDist) { bestDist = d; best = i; }
    if (i >= f.text2.size()) break;
  }
  return best;
}

void UIContext::FieldPushUndo(Widget& f) {
  f.undoStack.push_back(f.text2);
  if (f.undoStack.size() > 100) f.undoStack.erase(f.undoStack.begin());
  f.redoStack.clear();
}

void UIContext::FieldInsert(Widget& f, const std::string& s) {
  FieldPushUndo(f);
  size_t a = std::min(f.cursor, f.anchor), z = std::max(f.cursor, f.anchor);
  f.text2.replace(a, z - a, s);
  f.cursor = f.anchor = a + s.size();
  f.edited = true;
}

bool UIContext::FieldKey(Widget& f, const InputEvent& e) {
  if (!f.composing.empty()) return true; // the IME owns keys while composing
  std::string& v = f.text2;
  size_t a = std::min(f.cursor, f.anchor), z = std::max(f.cursor, f.anchor);
  auto move = [&](size_t to) { f.cursor = to; if (!e.shift) f.anchor = to; };
  switch (e.key) {
    case Key::Left:  move(f.cursor != f.anchor && !e.shift ? a : PrevBoundary(v, f.cursor)); return true;
    case Key::Right: move(f.cursor != f.anchor && !e.shift ? z : NextBoundary(v, f.cursor)); return true;
    case Key::Home:  move(0); return true;
    case Key::End:   move(v.size()); return true;
    case Key::Backspace:
    case Key::Delete:
      if (a == z) {
        if (e.key == Key::Backspace && a == 0) return true;
        if (e.key == Key::Delete && a == v.size()) return true;
        if (e.key == Key::Backspace) a = PrevBoundary(v, a); else z = NextBoundary(v, z);
      }
      FieldPushUndo(f);
      v.erase(a, z - a);
      f.cursor = f.anchor = a;
      f.edited = true;
      return true;
    case Key::Enter:
      if (f.edited && f.onCommit) f.onCommit(f);
      f.edited = false;
      return true;
    case Key::Escape: SetFocus(nullptr); return true;
    case Key::Space: return true; // arrives as a Text event too
    default: break;
  }
  if (!e.ctrl) return false;
  switch (e.key) {
    case Key::A: f.anchor = 0; f.cursor = v.size(); return true;
    case Key::C: if (window_ && a != z) window_->SetClipboardText(v.substr(a, z - a)); return true;
    case Key::X:
      if (a != z) { if (window_) window_->SetClipboardText(v.substr(a, z - a)); FieldInsert(f, ""); }
      return true;
    case Key::V: if (window_) { std::string clip = window_->GetClipboardText(); if (!clip.empty()) FieldInsert(f, clip); } return true;
    case Key::Z:
      if (!f.undoStack.empty()) { f.redoStack.push_back(v); v = f.undoStack.back(); f.undoStack.pop_back(); f.cursor = f.anchor = v.size(); f.edited = true; }
      return true;
    case Key::Y:
      if (!f.redoStack.empty()) { f.undoStack.push_back(v); v = f.redoStack.back(); f.redoStack.pop_back(); f.cursor = f.anchor = v.size(); f.edited = true; }
      return true;
    default: return false;
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
