#include "UI/UI.h"
#include "UI/Dock.h"
#include "Core/Guard.h"
#include "UI/Theme.h"
#include "Render2D/Batch2D.h"
#include "Text/Font.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace bdg {
constexpr long long kCaretBlinkMs = 530; // text caret on/off period
// ---- Builders ------------------------------------------------------------------------------
namespace {
std::unique_ptr<Widget> Make(WidgetKind k, std::string id = {}, std::string text = {}) {
  auto w = std::make_unique<Widget>();
  w->kind = k; w->id = std::move(id); w->text = std::move(text);
  return w;
}
const char* KindName(WidgetKind k) {
  static const char* names[] = {"Box", "Label", "Panel", "Tabs", "Button", "Field", "ListRow", "Badge", "Node", "Cover", "Custom", "Card", "Dock"};
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
std::unique_ptr<Widget> MakeCover(std::string id, std::string title, std::string subtitle) {
  // Matches design-system Cover: 960x300, name 96px bold (line-height .95, letter-spacing -.02em) 64px above the
  // bottom, tag 13px 32px above the bottom, both 40px from the left.
  auto w = Make(WidgetKind::Cover, id);
  w->axis = Axis::Column; w->crossAlign = Align::Start;
  w->padL = 40; w->padB = 32; w->gap = 17;
  w->width = Length::Px(960); w->height = Length::Px(300);
  auto name = Make(WidgetKind::Label, id + ".name", std::move(title));
  name->fontSize = 96; name->lineHeight = 0.95f; name->letterSpacing = -0.02f; name->bold = true;
  auto tag = Make(WidgetKind::Label, id + ".tag", std::move(subtitle));
  tag->fontSize = 13; tag->lineHeight = -1; tag->variant = 1;
  w->Add(std::move(name));
  w->Add(std::move(tag));
  return w;
}
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
float UIContext::MeasureSpaced(float size, const std::string& s, float ls) const {
  size_t chars = 0;
  for (unsigned char c : s) chars += (c & 0xC0) != 0x80; // UTF-8 code points
  return Measure(size, s) + ls * size * (float)chars;
}
float UIContext::NormalLineHeight(float size) const { return font_ ? font_->NormalLineHeight(size) : size * 1.2f; }

namespace {
std::vector<std::string> Lines(const std::string& s) {
  std::vector<std::string> out;
  size_t a = 0;
  for (size_t b; (b = s.find('\n', a)) != std::string::npos; a = b + 1) out.push_back(s.substr(a, b - a));
  out.push_back(s.substr(a));
  return out;
}
std::string Upper(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32); return s; }
// Graph node metrics, shared by measure and draw (design-system .bdg-node).
constexpr float kPinRow = 11.5f * 1.45f, kPinIcon = 10.f, kPinGap = 5.f;
struct PinInfo { bool out; std::string type, label; };
PinInfo ParsePin(const std::string& pin) {
  size_t a = pin.find('|'), c = pin.find('|', a + 1);
  return {pin.rfind("out|", 0) == 0, pin.substr(a + 1, c - a - 1), pin.substr(c + 1)};
}
float PinHeight(const PinInfo& p) { return p.label.empty() ? kPinIcon : kPinRow; }
}

int UIContext::NextBlinkMs() const {
  if (!focused_ || focused_->kind != WidgetKind::Field) return -1;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  return (int)(kCaretBlinkMs - ms % kCaretBlinkMs);
}

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
void UIContext::Measure(Widget& w) {
  const Theme& t = theme_;
  for (auto& c : w.children) Measure(*c);
  float s3 = t.N("space-3", 6), s5 = t.N("space-5", 10), s6 = t.N("space-6", 12);
  switch (w.kind) {
    case WidgetKind::Label: {
      if (w.variant == 2) { // component header: padding space-3 space-5, 12.5px, border-top 1px
        w.prefW = Measure(12.5f, w.text) + s5 * 2; w.prefH = 12.5f * 1.45f + s3 * 2 + 1; break;
      }
      if (w.variant == 3) { // caption: uppercase 10.5px, letter-spacing .07em, padding space-4 space-5 space-2
        w.prefW = MeasureSpaced(10.5f, Upper(w.text), 0.07f) + s5 * 2;
        w.prefH = 10.5f * 1.45f + t.N("space-4", 8) + t.N("space-2", 4); break;
      }
      float fs = w.fontSize > 0 ? w.fontSize : t.FontSize("body", 13);
      float lh = w.lineHeight < 0 ? NormalLineHeight(fs) : fs * (w.lineHeight > 0 ? w.lineHeight : 1.45f);
      std::vector<std::string> lines = Lines(w.text);
      w.prefW = 0;
      for (auto& l : lines) w.prefW = std::max(w.prefW, MeasureSpaced(fs, l, w.letterSpacing));
      w.prefH = lh * (float)lines.size(); break;
    }
    case WidgetKind::Custom:
    case WidgetKind::Dock: w.prefW = 0; w.prefH = 0; break;
    case WidgetKind::Button:
      w.prefW = Measure(t.FontSize("label", 12), w.text) + s5 * 2 + 2 + (w.variant == 1 ? 8 + s3 : 0); w.prefH = 24; break;
    case WidgetKind::Field:   w.prefW = 240; w.prefH = std::max(26.f, t.N("size-row", 22) + 4); break;
    case WidgetKind::Badge:
      if (w.variant >= 10) w.prefW = MeasureSpaced(10, w.text, 0.04f) + s3 * 2 + 2; // origin badge (.bdg-src)
      else w.prefW = Measure(t.FontSize("badge", 10), w.text) + s3 * 2 + 2;          // pill (.bdg-pill)
      w.prefH = 18; break;
    case WidgetKind::ListRow:
      w.prefW = s5 * 2 + w.indent * 12.f + 18 + Measure(t.FontSize("control", 12.5f), w.text) +
                (w.text2.empty() ? 0 : 16 + Measure(11, w.text2));
      w.prefH = t.N("size-row", 22);
      break;
    case WidgetKind::Tabs: {
      float sum = 0;
      float fs = w.variant == 1 ? t.FontSize("control", 12.5f) : t.FontSize("label", 12);
      float fsTab = w.variant >= 1 ? t.FontSize("control", 12.5f) : fs;
      for (auto& it : w.items) sum += Measure(fsTab, it) + s6 * 2;
      if (w.variant >= 1 && !w.items.empty()) sum += t.N("space-1", 2) * (float)(w.items.size() - 1);
      w.prefW = sum;
      w.prefH = w.variant == 1 ? 12.5f * 1.45f + s3 * 2 : t.N("size-panel-header", 30);
      if (w.variant == 2) { w.prefW += t.N("space-4", 8) * 2; w.prefH = t.N("size-workspace-bar", 38); }
      break;
    }
    case WidgetKind::Node: {
      float head = t.N("size-node-stripe", 3) + 5 + 12 * 1.45f + 5 + 1;
      float headW = s5 * 2 + Measure(12, w.text) + (w.text2.empty() ? 0 : s3 + Measure(10.5f, w.text2));
      float colW[2] = {0, 0}, colH[2] = {0, 0};
      int count[2] = {0, 0};
      for (auto& s : w.items) {
        PinInfo p = ParsePin(s);
        int k = p.out ? 1 : 0;
        float pw = 9 * 2 + (p.type == "exec" ? 8.f : kPinIcon) + (p.label.empty() ? 0 : s3 + Measure(11.5f, p.label));
        colW[k] = std::max(colW[k], pw);
        colH[k] += PinHeight(p) + (count[k]++ ? kPinGap : 0);
      }
      float pinsW = colW[0] + 14 + colW[1], pinsH = s3 * 2 + std::max(colH[0], colH[1]);
      w.prefW = std::max(150.f, std::max(headW, pinsW) + 2);
      w.prefH = 2 + head + pinsH;
      break;
    }
    case WidgetKind::Box:
    case WidgetKind::Cover:
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
        if (c->hidden) continue;
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
      if (w.kind == WidgetKind::Cover) { w.prefW = 960; w.prefH = 300; }
      break;
    }
  }
  if (w.width.mode == Length::Mode::Px) w.prefW = w.width.value;
  if (w.height.mode == Length::Mode::Px) w.prefH = w.height.value;
}

void UIContext::Arrange(Widget& w, float x, float y, float width, float height) {
  w.x = x; w.y = y; w.w = width; w.h = height;
  if (w.kind == WidgetKind::Dock) { ArrangeDock(w); return; }
  if (w.children.empty() || w.collapsed) return;
  if (w.kind == WidgetKind::Cover) { // children stacked from the bottom edge
    float by = y + height - w.padB;
    for (auto it = w.children.rbegin(); it != w.children.rend(); ++it) {
      Widget& c = **it;
      by -= c.prefH;
      Arrange(c, x + w.padL, by, c.prefW, c.prefH);
      by -= w.gap;
    }
    return;
  }
  bool row = w.axis == Axis::Row;
  float cx = x + w.padL, cy = y + w.padT;
  float cw = std::max(0.f, width - w.padL - w.padR), ch = std::max(0.f, height - w.padT - w.padB);
  size_t shown = 0;
  for (auto& c : w.children) shown += c->hidden ? 0 : 1;
  float avail = (row ? cw : ch) - w.gap * (float)(shown ? shown - 1 : 0), fillWeight = 0;
  for (auto& c : w.children) {
    if (c->hidden) continue;
    const Length& m = row ? c->width : c->height;
    if (m.mode == Length::Mode::Fill) fillWeight += m.value;
    else avail -= row ? c->prefW : c->prefH;
  }
  float pos = row ? cx : cy;
  for (auto& c : w.children) {
    if (c->hidden) continue;
    const Length& m = row ? c->width : c->height;
    const Length& crossLen = row ? c->height : c->width;
    float mainSize = m.mode == Length::Mode::Fill ? std::max(0.f, avail) * (m.value / std::max(fillWeight, 1e-4f))
                                                  : (row ? c->prefW : c->prefH);
    float crossAvail = row ? ch : cw;
    bool stretch = (w.crossAlign == Align::Stretch && crossLen.mode != Length::Mode::Px) || crossLen.mode == Length::Mode::Fill;
    float crossSize = stretch ? crossAvail
                    : crossLen.mode == Length::Mode::Px ? crossLen.value                 // fixed size may overflow (CSS)
                    : std::min(crossAvail, row ? c->prefH : c->prefW);
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
  docks_.clear();
  Measure(*root_);
  Arrange(*root_, 0, 0, width, height);
}

// ---- Drawing (design system components) ----------------------------------------------------
void UIContext::DrawWidget(Widget& w) {
  if (w.hidden) return;
  const Theme& t = theme_;
  Batch2D& b = batch_;
  const float rsm = t.N("radius-sm", 3), rmd = t.N("radius-md", 4);
  const float s3 = t.N("space-3", 6), s5 = t.N("space-5", 10), s6 = t.N("space-6", 12);
  switch (w.kind) {
    case WidgetKind::Box: break;
    case WidgetKind::Label: {
      if (w.variant == 2) {
        b.Rect(w.x, w.y, w.w, w.h, t.C("raise"));
        b.Rect(w.x, w.y, w.w, 1, t.C("line"));
        Text(w.x + s5, w.y + 1 + s3 + (12.5f * 1.45f - 12.5f) * 0.5f - 1, 12.5f, t.C("fg"), w.text);
        break;
      }
      if (w.variant == 3) {
        float x = w.x + s5, y = w.y + t.N("space-4", 8) + (10.5f * 1.45f - 10.5f) * 0.5f - 1;
        std::string up = Upper(w.text);
        for (size_t i = 0; i < up.size();) { // one code point at a time: letter-spacing .07em
          size_t n = 1;
          while (i + n < up.size() && ((unsigned char)up[i + n] & 0xC0) == 0x80) ++n;
          x += Text(x, y, 10.5f, t.C("fg-faint"), up.substr(i, n)) + 0.07f * 10.5f;
          i += n;
        }
        break;
      }
      float fs = w.fontSize > 0 ? w.fontSize : t.FontSize("body", 13);
      float lh = w.lineHeight < 0 ? NormalLineHeight(fs) : fs * (w.lineHeight > 0 ? w.lineHeight : 1.45f);
      float y = w.y;
      for (auto& line : Lines(w.text)) {
        Text(w.x, y + (lh - fs) * 0.5f - 1, fs, w.variant == 1 ? t.C("fg-dim") : t.C("fg"), line);
        y += lh;
      }
      break;
    }
    case WidgetKind::Custom:
      if (w.onDraw) {
        std::string reason;
        if (!GuardedCall(w.id.empty() ? "custom panel" : w.id.c_str(), [&] { w.onDraw(w, *this); }, &reason)) {
          w.onDraw = [reason](Widget& self, UIContext& ui) { // stop calling the broken code; say so in its place
            const Theme& th = ui.Tokens();
            ui.Batch().Rect(self.x, self.y, self.w, self.h, th.C("ground"));
            ui.Text(self.x + 12, self.y + 12, 12, th.C("err"), "이 패널의 그리기 코드가 오류로 멈췄습니다 (작업은 자동 저장됨)");
            ui.Text(self.x + 12, self.y + 32, 11, th.C("fg-dim"), reason);
          };
        }
      }
      break;
    case WidgetKind::Dock: DrawDock(w); break;
    case WidgetKind::Card: {
      float fs = t.FontSize("control", 12.5f);
      b.Rect(w.x + 4, w.y + 2, w.w - 8, w.h - 4, t.C("panel"), t.N("radius-lg", 5), 1, t.C("line-strong"));
      b.Rect(w.x + 5, w.y + 3, w.w - 10, 26, w.hovered ? t.C("line-strong") : t.C("raise"), t.N("radius-lg", 5));
      b.Rect(w.x + 5, w.y + 3, w.w - 10, 1, t.C("line"));                      // .bdg-comp-head border-top
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
      bool ws = w.variant >= 1;
      float fs = ws ? t.FontSize("control", 12.5f) : t.FontSize("label", 12), px = w.x;
      float ty = w.y, th = w.h;
      if (!ws || w.variant == 2) b.Rect(w.x, w.y, w.w, w.h, t.C("win"), rsm);
      if (w.variant == 2) { px += t.N("space-4", 8); th = 12.5f * 1.45f + s3 * 2; ty = w.y + (w.h - th) * 0.5f; }
      w.itemX.clear();
      for (size_t i = 0; i < w.items.size(); ++i) {
        float tw = Measure(fs, w.items[i]) + s6 * 2;
        bool on = (int)i == w.selectedIndex;
        if (on && ws) { b.Rect(px, ty, tw, th, t.C("raise"), t.N("radius-lg", 5)); b.Rect(px + 2, ty + th - 2, tw - 4, 2, t.C("accent")); }
        else if (on) { b.Rect(px, w.y, tw, w.h, t.C("panel")); b.Rect(px, w.y, tw, 2, t.C("accent")); }
        Text(px + s6, ty + (th - fs) * 0.5f - 1, fs, on ? t.C("fg") : t.C("fg-dim"), w.items[i]);
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
      float tx = w.x + (w.w - Measure(fs, w.text)) * 0.5f;
      if (w.variant == 1) { // status dot before the label
        b.Rect(w.x + 1 + s5, w.y + (w.h - 8) * 0.5f, 8, 8, t.C("ok"), 4);
        tx = w.x + 1 + s5 + 8 + s3;
      }
      Text(tx, w.y + (w.h - fs) * 0.5f - 1, fs, w.primary ? t.C("on-accent") : t.C("fg"), w.text);
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
      b.PushClip(ix + 1, iy, iw - 2, ih); // long values stay inside the box
      if (w.slider && w.sliderMax > w.sliderMin) { // filled part of the range + thumb
        double sv = std::strtod(w.text2.c_str(), nullptr);
        float k = (float)std::clamp((sv - w.sliderMin) / (w.sliderMax - w.sliderMin), 0.0, 1.0);
        Color fill = t.C("accent");
        fill.a = 0.22f;
        b.Rect(ix + 1, iy + 1, (iw - 2) * k, ih - 2, fill, rsm);
        b.Rect(ix + 1 + (iw - 4) * k, iy + 2, 2, ih - 4, t.C("accent"));
      }
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
        if ((ms / kCaretBlinkMs) % 2 == 0 || !w.composing.empty()) b.Rect(caretX, iy + 4, 1.f, ih - 8, t.C("fg"));
        if (window_) window_->SetTextInputArea(ix * scale_, iy * scale_, iw * scale_, ih * scale_, (int)((caretX - ix) * scale_));
      }
      b.PopClip();
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
      Text(kx + 18, w.y + (w.h - fs) * 0.5f - 1, fs, w.variant == 1 ? t.C("fg-faint") : t.C("fg"), w.text); // 1: dimmed (undone step)
      if (!w.text2.empty() && w.text2 != "Mesh") Text(w.x + w.w - s5 - Measure(11, w.text2), w.y + (w.h - 11) * 0.5f - 1, 11, t.C("fg-faint"), w.text2);
      break;
    }
    case WidgetKind::Badge: {
      float fs = t.FontSize("badge", 10);
      if (w.variant >= 10) { // origin badge: outline in the source colour
        static const char* src[] = {"source-unity", "source-unreal", "source-blender", "accent"};
        Color c = t.C(src[std::min(w.variant - 10, 3)]);
        b.Rect(w.x, w.y, w.w, w.h, w.variant == 13 ? t.C("accent-soft") : Color{0, 0, 0, 0}, rsm, 1, c);
        float x = w.x + s3 + 1;
        Text(x, w.y + (w.h - fs) * 0.5f - 1, fs, c, w.text);
        break;
      }
      static const char* pill[] = {"fg-dim", "ok", "warn", "err"};
      b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"), t.N("radius-pill", 12), 1, t.C("line"));
      Text(w.x + s3 + 1, w.y + (w.h - fs) * 0.5f - 1, fs, t.C(pill[std::clamp(w.variant, 0, 3)]), w.text);
      break;
    }
    case WidgetKind::Node: {
      static const char* stripes[] = {"node-flow", "node-event", "node-function", "node-pure"};
      float stripe = t.N("size-node-stripe", 3), head = stripe + 5 + 12 * 1.45f + 5 + 1;
      if (Theme::Shadow sh = t.S("shadow-node"); sh.color.a > 0)                      // shadow-node token
        b.Shadow(w.x + sh.x, w.y + sh.y, w.w, w.h, sh.color, rmd, sh.blur);
      b.Rect(w.x, w.y, w.w, w.h, t.C("panel"), rmd, 1, t.C("line-strong"));
      b.Rect(w.x + 1, w.y + 1, w.w - 2, head - 1, t.C("win"), rmd);
      b.Rect(w.x + 1, w.y + 1, w.w - 2, stripe, t.C(stripes[w.variant & 3]), rmd);
      b.Rect(w.x + 1, w.y + head - 1, w.w - 2, 1, t.C("line"));
      float tx = w.x + s5;
      float textY = w.y + 1 + stripe + 5 + (12 * 1.45f - 12) * 0.5f - 1;
      tx += Text(tx, textY, 12, t.C("fg"), w.text) + s3;
      Text(tx, textY + 1.5f, 10.5f, t.C("fg-dim"), w.text2);
      float inY = w.y + 1 + head + s3, outY = inY;
      for (auto& s : w.items) {
        PinInfo p = ParsePin(s);
        Color pc = t.C("pin-" + p.type);
        float& py = p.out ? outY : inY;
        float rowH = PinHeight(p), iconW = p.type == "exec" ? 8.f : kPinIcon;
        float ix = p.out ? w.x + w.w - 1 - 9 - iconW : w.x + 1 + 9, iy = py + (rowH - kPinIcon) * 0.5f;
        if (p.type == "exec") b.Triangle(ix, iy, 8, 10, pc);                    // exec pin: right-pointing triangle
        else b.Rect(ix, iy, 10, 10, {0, 0, 0, 0}, 5, 2, pc);
        if (!p.label.empty()) {
          float lw = Measure(11.5f, p.label);
          Text(p.out ? ix - s3 - lw : ix + iconW + s3, py + (rowH - 11.5f) * 0.5f - 1, 11.5f, t.C("fg"), p.label);
        }
        py += rowH + kPinGap;
      }
      if (w.selected) b.Rect(w.x - 2, w.y - 2, w.w + 4, w.h + 4, {0, 0, 0, 0}, rmd + 2, 2, t.C("accent"));
      break;
    }
    case WidgetKind::Cover: { // design-system Cover: canvas field, node, selection, accent slab, dot grid
      float r = t.N("radius-md", 4);
      b.Rect(w.x, w.y, w.w, w.h, t.C("ground"));
      b.Rect(w.x + 480, w.y, 480, 300, t.C("raise"));
      b.Rect(w.x + 560, w.y + 40, 176, 88, t.C("win"), r);
      b.Rect(w.x + 560, w.y + 40, 176, 3, t.C("node-function"));
      b.Rect(w.x + 560, w.y + 144, 176, 110, t.C("selection"), r);
      b.Rect(w.x + 752, w.y + 40, 96, 220, t.C("accent"), r);
      for (float dy = 48; dy <= 248; dy += 16)
        for (float dx = 872; dx <= 920; dx += 16) b.Rect(w.x + dx - 1.4f, w.y + dy - 1.4f, 2.8f, 2.8f, t.C("line-strong"), 1.4f);
      break;
    }
  }
  if (focused_ == &w && w.kind != WidgetKind::Field)
    b.Rect(w.x - 3, w.y - 3, w.w + 6, w.h + 6, {0, 0, 0, 0}, rmd + 3, 2, t.C("accent")); // focus-visible ring
  if (w.kind == WidgetKind::Dock) { // each panel's content stays inside its body
    for (auto& c : w.children) {
      if (c->hidden) continue;
      auto body = w.dock->bodyRects.find(c->id);
      if (body == w.dock->bodyRects.end()) continue;
      const DockRect& r = body->second;
      b.PushClip(r.x, r.y, r.w, r.h);
      DrawWidget(*c);
      if (c->h > r.h + 0.5f) { // scroll bar: where the visible part is
        float th = std::max(24.f, r.h * r.h / c->h), ty = r.y + (r.h - th) * (c->scrollY / (c->h - r.h));
        Color bar = t.C("fg-faint");
        bar.a = 0.5f;
        b.Rect(r.x + r.w - 5, ty + 2, 3, th - 4, bar, 1.5f);
      }
      b.PopClip();
    }
    return;
  }
  if (!w.collapsed) for (auto& c : w.children) DrawWidget(*c);
}

void UIContext::ForgetRemovedWidgets() {
  auto reachable = [&](Widget* target) {
    if (!target || !root_) return false;
    std::vector<Widget*> stack{root_.get()};
    while (!stack.empty()) {
      Widget* w = stack.back(); stack.pop_back();
      if (w == target) return true;
      for (auto& c : w->children) stack.push_back(c.get());
    }
    return false;
  };
  if (!reachable(hovered_)) hovered_ = nullptr;
  if (!reachable(pressed_)) pressed_ = nullptr;
  if (!reachable(focused_)) focused_ = nullptr;
  if (!reachable(dragSource_)) { dragSource_ = nullptr; dragging_ = false; }
  if (!reachable(dropTarget_)) dropTarget_ = nullptr;
  if (!reachable(sliderDrag_)) sliderDrag_ = nullptr;
}

Widget* UIContext::DropTargetAt(float x, float y) {
  Widget* hit = root_ ? HitTest(*root_, x, y) : nullptr;
  return hit && hit->onDrop ? hit : nullptr;
}

void UIContext::DrawDragOverlay() {
  if (!dragging_ || !dragSource_) return;
  const Theme& t = theme_;
  if (dropTarget_ && dropTarget_ != dragSource_) {
    Widget& d = *dropTarget_;
    batch_.Rect(d.x + 1, d.y + 1, d.w - 2, d.h - 2, {0, 0, 0, 0}, t.N("radius-sm", 3), 2, t.C("accent"));
  }
  std::string label = dragSource_->text.empty() ? dragSource_->id : dragSource_->text;
  float fs = t.FontSize("label", 12), w = Measure(fs, label) + 20;
  batch_.Rect(dragX_ + 12, dragY_ + 8, w, 22, t.C("raise"), t.N("radius-sm", 3), 1, t.C("accent"));
  Text(dragX_ + 22, dragY_ + 8 + (22 - fs) * 0.5f - 1, fs, t.C("fg"), label);
}

void UIContext::ScrollIntoView(const Widget& target) {
  for (Widget* d : docks_)
    for (auto& c : d->children) {
      auto body = d->dock->bodyRects.find(c->id);
      if (c->hidden || body == d->dock->bodyRects.end()) continue;
      bool inside = false; // is target inside this panel's content?
      std::vector<const Widget*> stack{c.get()};
      while (!stack.empty() && !inside) {
        const Widget* x = stack.back(); stack.pop_back();
        inside = x == &target;
        for (auto& k : x->children) stack.push_back(k.get());
      }
      if (!inside) continue;
      const DockRect& r = body->second;
      if (target.y < r.y) c->scrollY -= r.y - target.y + 4;
      else if (target.y + target.h > r.y + r.h) c->scrollY += target.y + target.h - (r.y + r.h) + 4;
      return;
    }
}

void UIContext::Draw() {
  if (!root_) return;
  DrawWidget(*root_);
  for (Widget* d : docks_) DrawDockOverlay(*d); // drag previews above everything
  DrawDragOverlay();
}

// ---- Input ----------------------------------------------------------------------------------
Widget* UIContext::HitTest(Widget& w, float x, float y) {
  if (w.hidden) return nullptr;
  if (w.kind == WidgetKind::Dock && w.dock) { // only the visible body of each panel can be hit
    if (x < w.x || y < w.y || x >= w.x + w.w || y >= w.y + w.h) return nullptr;
    for (auto it = w.children.rbegin(); it != w.children.rend(); ++it) {
      auto body = w.dock->bodyRects.find((*it)->id);
      if ((*it)->hidden || body == w.dock->bodyRects.end() || !body->second.Contains(x, y)) continue;
      if (Widget* hit = HitTest(**it, x, y)) return hit;
    }
    return nullptr;
  }
  if (x < w.x || y < w.y || x >= w.x + w.w || y >= w.y + w.h) return nullptr;
  if (w.kind == WidgetKind::Card && y < w.y + 30) return &w;
  if (!w.collapsed)
    for (auto it = w.children.rbegin(); it != w.children.rend(); ++it)
      if (Widget* hit = HitTest(**it, x, y)) return hit;
  bool headerTabs = w.kind == WidgetKind::Panel && !w.items.empty() && y < w.y + theme_.N("size-panel-header", 30);
  return (w.focusable || w.onClick || headerTabs) ? &w : nullptr;
}

void UIContext::CollectFocusable(Widget& w, std::vector<Widget*>& out) {
  if (w.hidden) return;
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
    bool mouse = e.type == InputEvent::Type::MouseMove || e.type == InputEvent::Type::MouseDown || e.type == InputEvent::Type::MouseUp;
    if (mouse && DockMouse(e)) continue; // splitter / tab drag owns the mouse
    switch (e.type) {
      case InputEvent::Type::MouseMove: {
        if (sliderDrag_) { SliderSet(*sliderDrag_, e.x); break; }
        if (dragSource_) { // drag and drop: starts after 6 units so clicks stay clicks
          dragX_ = e.x; dragY_ = e.y;
          if (!dragging_ && std::hypot(e.x - pressX_, e.y - pressY_) > 6.f) dragging_ = true;
          if (dragging_) { dropTarget_ = DropTargetAt(e.x, e.y); break; }
        }
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
        dragSource_ = pressed_ && pressed_->draggable ? pressed_ : nullptr;
        dragging_ = false;
        dropTarget_ = nullptr;
        pressX_ = dragX_ = e.x; pressY_ = dragY_ = e.y;
        SetFocus((pressed_ && pressed_->focusable) ? pressed_ : nullptr);
        if (focused_ && focused_->kind == WidgetKind::Field) focused_->cursor = focused_->anchor = FieldHit(*focused_, e.x);
        if (focused_ && focused_->kind == WidgetKind::Field && focused_->slider) {
          float ix, iy, iw, ih;
          FieldInputBox(*focused_, ix, iy, iw, ih);
          if (e.x >= ix && e.x < ix + iw && e.y >= iy && e.y < iy + ih) { sliderDrag_ = focused_; SliderSet(*focused_, e.x); }
        }
        break;
      case InputEvent::Type::MouseUp: {
        if (e.button != 1) break;
        if (sliderDrag_) { sliderDrag_ = nullptr; if (pressed_) pressed_->pressed = false; pressed_ = nullptr; break; }
        Widget* hit = HitTest(*root_, e.x, e.y);
        if (dragging_) { // a drop, not a click
          if (dropTarget_ && dropTarget_ != dragSource_ && dropTarget_->onDrop) dropTarget_->onDrop(*dragSource_);
          if (pressed_) pressed_->pressed = false;
        } else if (pressed_) {
          pressed_->pressed = false;
          clickCtrl_ = e.ctrl;
          clickShift_ = e.shift;
          if (hit == pressed_) Activate(*hit, e.x, e.y);
          clickCtrl_ = clickShift_ = false;
        }
        pressed_ = nullptr;
        dragSource_ = dropTarget_ = nullptr;
        dragging_ = false;
        break;
      }
      case InputEvent::Type::KeyDown: {
        if (focused_ && focused_->kind == WidgetKind::Field && FieldKey(*focused_, e)) break;
        { // global shortcuts; while typing in a field only Ctrl/Alt combos and F-keys reach them (W stays a letter)
          bool typing = focused_ && focused_->kind == WidgetKind::Field;
          bool chord = e.ctrl || e.alt || (e.key >= Key::F1 && e.key <= Key::F12);
          if (onKey_ && (!typing || chord) && onKey_(e)) break;
        }
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
      case InputEvent::Type::Wheel: // scroll the dock panel under the mouse
        for (Widget* d : docks_)
          for (auto& c : d->children)
            if (auto body = d->dock->bodyRects.find(c->id); !c->hidden && body != d->dock->bodyRects.end() && body->second.Contains(e.x, e.y))
              c->scrollY -= e.wheel * 48.f; // clamped by the next layout
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
    focused_->undoStack.clear();
    focused_->redoStack.clear();
  }
  focused_ = w;
  if (w && w->kind == WidgetKind::Field) { w->cursor = w->anchor = w->text2.size(); w->composing.clear(); }
  if (w) ScrollIntoView(*w);
  if (onFocus_) onFocus_(w); // screen readers announce the new focus
}

std::vector<Widget*> UIContext::FocusOrder() {
  std::vector<Widget*> out;
  if (root_) CollectFocusable(*root_, out);
  return out;
}

const char* UIContext::AccessibleRole(const Widget& w) {
  switch (w.kind) {
    case WidgetKind::Button: return "button";
    case WidgetKind::Tabs: return "tab";
    case WidgetKind::Field: return "textbox";
    case WidgetKind::ListRow: return "listitem";
    case WidgetKind::Node: return "group";
    case WidgetKind::Card: return "group";
    default: return "text";
  }
}

std::string UIContext::AccessibleName(const Widget& w) {
  switch (w.kind) {
    case WidgetKind::Tabs: return w.items.empty() ? w.id : w.items[std::clamp(w.selectedIndex, 0, (int)w.items.size() - 1)] + " tab";
    case WidgetKind::Field: return w.text;  // label; the value is reported separately
    case WidgetKind::ListRow: return w.text2.empty() ? w.text : w.text + ", " + w.text2;
    case WidgetKind::Node: return w.text + (w.text2.empty() ? "" : " " + w.text2) + " node";
    default: {
      // Drop decoration a screen reader would read aloud: "+  Add Component" -> "Add Component", "▶  Play" -> "Play".
      size_t i = 0;
      while (i < w.text.size()) {
        unsigned char c = (unsigned char)w.text[i];
        if (c < 0x80 && !std::isalnum(c)) ++i;
        else if (c == 0xE2 && i + 2 < w.text.size()) i += 3; // U+2000-U+2FFF: arrows, shapes, punctuation
        else break;
      }
      std::string name = w.text.substr(i);
      return name.empty() ? w.id : name;
    }
  }
}

void UIContext::SliderSet(Widget& f, float mouseX) {
  float ix, iy, iw, ih;
  FieldInputBox(f, ix, iy, iw, ih);
  double k = std::clamp((double)(mouseX - ix - 2) / std::max(1.f, iw - 4), 0.0, 1.0);
  double v = f.sliderMin + k * (f.sliderMax - f.sliderMin);
  std::string text;
  if (f.sliderInteger) text = std::to_string((long long)std::llround(v));
  else {
    text = std::format("{:.3f}", v);
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
  }
  if (text == f.text2) return;
  f.text2 = text;
  f.cursor = f.anchor = text.size();
  f.edited = false; // the drag sends its own edits
  if (f.onChange) f.onChange(f);
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
      f.undoStack.clear(); // committed: from here Ctrl+Z is the global (command bus) undo
      f.redoStack.clear();
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
      if (!f.edited) return false; // nothing uncommitted: let the global undo handle it
      if (!f.undoStack.empty()) { f.redoStack.push_back(v); v = f.undoStack.back(); f.undoStack.pop_back(); f.cursor = f.anchor = v.size(); f.edited = true; }
      return true;
    case Key::Y:
      if (!f.edited) return false;
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
