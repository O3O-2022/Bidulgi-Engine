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
namespace {
Color WithAlphaUI(Color c, float a) { c.a = a; return c; }
}
constexpr long long kCaretBlinkMs = 530; // text caret on/off period
constexpr long long kTooltipDelayMs = 550; // mouse rest before hover help shows
long long SteadyMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int ChoiceIndex(const Widget& w) { // -1: the value is not one of the options (several values, unknown name)
  for (size_t i = 0; i < w.items.size(); ++i) if (w.items[i] == w.text2) return (int)i;
  return -1;
}
// ---- Builders ------------------------------------------------------------------------------
namespace {
std::unique_ptr<Widget> Make(WidgetKind k, std::string id = {}, std::string text = {}) {
  auto w = std::make_unique<Widget>();
  w->kind = k; w->id = std::move(id); w->text = std::move(text);
  return w;
}
const char* KindName(WidgetKind k) {
  static const char* names[] = {"Box", "Label", "Panel", "Tabs", "Button", "Field", "ListRow", "Badge", "Node", "Cover", "Custom", "Card", "Dock", "Choice"};
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
std::unique_ptr<Widget> MakeCombo(std::string id, std::string label, std::vector<std::string> options, std::string value) {
  auto w = Make(WidgetKind::Choice, std::move(id), std::move(label));
  w->items = std::move(options);
  w->text2 = std::move(value);
  w->selectedIndex = ChoiceIndex(*w);
  w->focusable = true;
  return w;
}
std::unique_ptr<Widget> MakeSegmented(std::string id, std::string label, std::vector<std::string> options, std::string value) {
  auto w = MakeCombo(std::move(id), std::move(label), std::move(options), std::move(value));
  w->variant = 1;
  return w;
}
std::unique_ptr<Widget> MakeToggle(std::string id, std::string label, bool on) {
  auto w = MakeCombo(std::move(id), std::move(label), {"false", "true"}, on ? "true" : "false");
  w->variant = 2;
  return w;
}
std::unique_ptr<Widget> MakeChoice(std::string id, std::string label, std::vector<std::string> options, std::string value) {
  size_t chars = 0;
  for (auto& o : options) chars += o.size();
  const bool segmented = options.size() >= 2 && options.size() <= 4 && chars <= 28; // fits the value column
  return segmented ? MakeSegmented(std::move(id), std::move(label), std::move(options), std::move(value))
                   : MakeCombo(std::move(id), std::move(label), std::move(options), std::move(value));
}
std::unique_ptr<Widget> MakeCustom(std::string id, std::function<void(Widget&, UIContext&)> draw) {
  auto w = Make(WidgetKind::Custom, std::move(id)); w->onDraw = std::move(draw); return w;
}

// ---- Helpers -------------------------------------------------------------------------------
void UIContext::SetIcons(const IconAtlas* icons) {
  icons_ = icons;
  batch_.SetIconAtlas(icons ? icons->texture : TextureHandle{});
}

bool UIContext::HasIcon(const std::string& name) const { return icons_ && icons_->uv.count(name) > 0; }

bool UIContext::Icon(const std::string& name, float x, float y, float size, Color tint) {
  if (!icons_ || name.empty()) return false;
  auto it = icons_->uv.find(name);
  if (it == icons_->uv.end()) return false;
  const auto& uv = it->second;
  batch_.Icon(x, y, size, size, uv[0], uv[1], uv[2], uv[3], tint);
  return true;
}

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

int UIContext::NextTooltipMs() const {
  if (!hovered_ || hovered_->tooltip.empty() || !menu_.empty()) return -1;
  const long long left = kTooltipDelayMs - (SteadyMs() - hoverSince_);
  return left > 0 ? (int)left : -1;
}

// ---- Choices: combo box, segmented buttons, toggle switch ---------------------------------
void UIContext::ChoiceSet(Widget& w, int index) {
  if (index < 0 || index >= (int)w.items.size()) return;
  const bool changed = w.text2 != w.items[(size_t)index];
  w.text2 = w.items[(size_t)index];
  w.selectedIndex = index;
  if (changed && w.onChange) w.onChange(w);
}

int UIContext::ChoiceStyle(const Widget& w) const { // segmented buttons whose names do not fit act as a combo box
  if (w.variant != 1 || w.items.empty()) return w.variant;
  float ix, iy, iw, ih;
  FieldInputBox(w, ix, iy, iw, ih);
  const float sw = iw / (float)w.items.size(), fs = theme_.FontSize("label", 12);
  for (const std::string& o : w.items) if (Measure(fs, o) + 10 > sw) return 0;
  return 1;
}

bool UIContext::ChoiceSegment(const Widget& w, int k, float& x, float& w2) const {
  if (w.kind != WidgetKind::Choice || w.variant != 1 || k < 0 || k >= (int)w.items.size()) return false;
  float ix, iy, iw, ih;
  FieldInputBox(w, ix, iy, iw, ih);
  w2 = iw / (float)w.items.size();
  x = ix + w2 * (float)k;
  return true;
}

void UIContext::OpenChoice(Widget& w) {
  float ix, iy, iw, ih;
  FieldInputBox(w, ix, iy, iw, ih);
  std::vector<MenuItem> items;
  Widget* wp = &w;
  for (size_t i = 0; i < w.items.size(); ++i)
    items.push_back({w.items[i], [this, wp, i] { ChoiceSet(*wp, (int)i); }, true, w.items[i] == w.text2});
  OpenMenu(ix, iy + ih + 2, std::move(items), w.items.size() > 8);
  menuW_ = std::max(menuW_, iw); // at least as wide as the box it drops from
  if (root_) menuX_ = std::min(menuX_, root_->w - menuW_ - 2);
}

bool UIContext::ChoiceKey(Widget& w, const InputEvent& e) {
  const int n = (int)w.items.size(), cur = ChoiceIndex(w);
  if (!n) return false;
  if (e.key == Key::Left || e.key == Key::Up) { ChoiceSet(w, cur <= 0 ? (w.variant == 2 ? 0 : n - 1) : cur - 1); return true; }
  if (e.key == Key::Right || e.key == Key::Down) { ChoiceSet(w, cur < 0 ? 0 : w.variant == 2 ? 1 : (cur + 1) % n); return true; }
  if (e.key == Key::Enter || e.key == Key::Space) { Activate(w, -1, -1); return true; }
  return false;
}

void UIContext::DrawChoice(Widget& w) {
  const Theme& t = theme_;
  Batch2D& b = batch_;
  const float fs = t.FontSize("label", 12), rsm = t.N("radius-sm", 3), s6 = t.N("space-6", 12);
  float ix, iy, iw, ih;
  FieldInputBox(w, ix, iy, iw, ih);
  const bool focus = focused_ == &w;
  const int cur = ChoiceIndex(w);
  if (w.overridden) b.Rect(w.x, w.y + 2, 2, w.h - 4, t.C("prefab"), 1); // prefab override (6-1): the prefab colour
  if (w.marked) b.Rect(w.x + s6 - 8, w.y + w.h * 0.5f - 3, 6, 6, t.C("accent"), 3);
  Text(w.x + s6, w.y + (w.h - fs) * 0.5f - 1, fs, w.marked ? t.C("accent") : w.hovered ? t.C("fg") : t.C("fg-dim"), w.text);
  const float ty = iy + (ih - fs) * 0.5f - 1;
  const int style = ChoiceStyle(w);
  if (style == 2) { // toggle switch: pill track + knob, then the state in words
    const float tw = 30, th = 16, tx = ix, tyy = iy + (ih - th) * 0.5f;
    const bool on = cur == 1, mixed = cur < 0;
    b.Rect(tx, tyy, tw, th, on ? t.C("accent") : t.C("sunk"), th * 0.5f, 1, on || focus ? t.C("accent") : t.C("line-strong"));
    const float knob = th - 4, kx = mixed ? tx + (tw - knob) * 0.5f : on ? tx + tw - knob - 2 : tx + 2;
    b.Rect(kx, tyy + 2, knob, knob, on ? t.C("on-accent") : t.C("fg-dim"), knob * 0.5f);
    Text(tx + tw + 8, ty, fs, on ? t.C("fg") : t.C("fg-faint"), mixed ? "—" : on ? "켜짐" : "꺼짐");
    return;
  }
  if (style == 1) { // segmented: one button per option, the current one filled
    b.Rect(ix, iy, iw, ih, t.C("sunk"), rsm, 1, focus ? t.C("accent") : t.C("line"));
    for (int k = 0; k < (int)w.items.size(); ++k) {
      float sx, sw;
      ChoiceSegment(w, k, sx, sw);
      const bool sel = k == cur;
      if (sel) b.Rect(sx + 2, iy + 2, sw - 4, ih - 4, t.C("accent-soft"), rsm, 1, t.C("accent"));
      else if (k > 0 && k - 1 != cur) b.Rect(sx, iy + 5, 1, ih - 10, t.C("line"));
      const std::string& label = w.items[(size_t)k];
      b.PushClip(sx + 2, iy, sw - 4, ih);
      Text(sx + std::max(4.f, (sw - Measure(fs, label)) * 0.5f), ty, fs, sel ? t.C("fg") : t.C("fg-dim"), label);
      b.PopClip();
    }
    return;
  }
  // combo box: value + a down chevron; the list opens below on click / Enter / Space
  b.Rect(ix, iy, iw, ih, w.hovered ? t.C("raise") : t.C("sunk"), rsm, 1, focus ? t.C("accent") : w.hovered ? t.C("fg-faint") : t.C("line"));
  b.PushClip(ix + 1, iy, iw - 22, ih);
  Text(ix + t.N("space-3", 6), ty, fs, cur < 0 ? t.C("fg-faint") : t.C("fg"), w.text2.empty() ? "—" : w.text2);
  b.PopClip();
  const float cx = ix + iw - 13, cy = iy + ih * 0.5f;
  const float tri[4][2] = {{cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}, {cx, cy + 3}};
  b.Quad(tri, w.hovered || focus ? t.C("fg") : t.C("fg-dim"));
}

void UIContext::DrawTooltip() {
  if (!hovered_ || hovered_->tooltip.empty() || !menu_.empty() || dialog_ || dragging_ || pressed_) return;
  if (SteadyMs() - hoverSince_ < kTooltipDelayMs) return;
  const Theme& t = theme_;
  Batch2D& b = batch_;
  const float fs = t.FontSize("caption", 11), pad = 7, maxW = 320;
  std::vector<std::string> lines; // word wrap at maxW, '\n' forces a break
  std::string line, word;
  auto flushWord = [&] {
    if (word.empty()) return;
    const std::string tryLine = line.empty() ? word : line + " " + word;
    if (!line.empty() && Measure(fs, tryLine) > maxW - pad * 2) { lines.push_back(line); line = word; }
    else line = tryLine;
    word.clear();
  };
  for (char c : hovered_->tooltip) {
    if (c == ' ') flushWord();
    else if (c == '\n') { flushWord(); lines.push_back(line); line.clear(); }
    else word += c;
  }
  flushWord();
  if (!line.empty()) lines.push_back(line);
  float w = 0;
  for (auto& l : lines) w = std::max(w, Measure(fs, l));
  w += pad * 2;
  const float lh = fs * 1.45f, h = lines.size() * lh + pad * 2 - (lh - fs);
  float x = hovered_->x + 12, y = hovered_->y + hovered_->h + 4;
  if (root_) {
    x = std::min(x, root_->w - w - 4);
    if (y + h > root_->h - 4) y = hovered_->y - h - 4;
  }
  b.Shadow(x, y + 3, w, h, {0, 0, 0, 0.4f}, 5, 10);
  b.Rect(x, y, w, h, t.C("raise"), 5, 1, t.C("line-strong"));
  for (size_t i = 0; i < lines.size(); ++i) Text(x + pad, y + pad + i * lh - 1, fs, t.C("fg"), lines[i]);
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
    case WidgetKind::Choice:
    case WidgetKind::Field:   w.prefW = 240; w.prefH = std::max(26.f, t.N("size-row", 22) + 4); break;
    case WidgetKind::Badge:
      if (w.variant >= 10) w.prefW = MeasureSpaced(10, w.text, 0.04f) + s3 * 2 + 2; // origin badge (.bdg-src)
      else w.prefW = Measure(t.FontSize("badge", 10), w.text) + s3 * 2 + 2;          // pill (.bdg-pill)
      w.prefH = 18; break;
    case WidgetKind::ListRow:
      w.prefW = s5 * 2 + (w.tree.empty() ? w.indent * 12.f : w.tree.size() * 14.f + 12) + 18 + Measure(t.FontSize("control", 12.5f), w.text) +
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
      // component icon; a language badge (C#, C++ ...) means a script
      if (!Icon(w.text2.empty() ? ComponentIconName(w.text) : "comp-script", w.x + 25, w.y + 8, 16))
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
    case WidgetKind::Choice: DrawChoice(w); break;
    case WidgetKind::Field: {
      float fs = t.FontSize("label", 12), ix, iy, iw, ih;
      FieldInputBox(w, ix, iy, iw, ih);
      bool focus = focused_ == &w;
      if (w.overridden) b.Rect(w.x, w.y + 2, 2, w.h - 4, t.C("prefab"), 1); // prefab override (6-1): the prefab colour
      if (w.marked) b.Rect(w.x + s6 - 8, w.y + w.h * 0.5f - 3, 6, 6, t.C("accent"), 3);
      Text(w.x + s6, w.y + (w.h - fs) * 0.5f - 1, fs, w.marked || scrub_ == &w ? t.C("accent") : w.scrub && w.hovered ? t.C("fg") : t.C("fg-dim"), w.text);
      const auto parts = w.axes > 0 ? FieldParts(w.text2, w.axes) : std::vector<std::pair<size_t, size_t>>{};
      if (!parts.empty() && w.composing.empty()) { // vector: one box per number with its axis tag (E-4b)
        static const char* tags[] = {"X", "Y", "Z", "W"};
        static const char* tagColors[] = {"axis-x", "axis-y", "axis-z", "fg-faint"};
        const std::string& v = w.text2;
        const size_t selA = std::min(w.cursor, w.anchor), selZ = std::max(w.cursor, w.anchor);
        for (int k = 0; k < (int)parts.size(); ++k) {
          float bx, bw;
          AxisBox(w, k, bx, bw);
          const auto [a, z] = parts[(size_t)k];
          const bool here = focus && w.cursor >= a && w.cursor <= z;
          b.Rect(bx, iy, bw, ih, t.C("sunk"), rsm, 1, here ? t.C("accent") : t.C("line"));
          Color tag = t.C(tagColors[k]);
          b.Rect(bx + 1, iy + 1, 14, ih - 2, WithAlphaUI(tag, scrub_ == &w && scrubAxis_ == k ? 0.55f : 0.28f), rsm);
          Text(bx + 4, iy + (ih - 10) * 0.5f - 1, 10, tag, tags[k]);
          const float tx = bx + 18, ty = iy + (ih - fs) * 0.5f - 1;
          b.PushClip(bx + 16, iy, bw - 17, ih);
          if (focus && selZ > selA && selA < z && selZ > a) { // selection inside this number
            const size_t s0 = std::max(selA, a), s1 = std::min(selZ, z);
            const float sx = tx + Measure(fs, v.substr(a, s0 - a)), ex = tx + Measure(fs, v.substr(a, s1 - a));
            b.Rect(sx, iy + 3, ex - sx, ih - 6, t.C("accent-soft"));
          }
          Text(tx, ty, fs, t.C("fg"), v.substr(a, z - a));
          if (here) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const float cx = tx + Measure(fs, v.substr(a, w.cursor - a));
            if ((ms / kCaretBlinkMs) % 2 == 0) b.Rect(cx, iy + 4, 1.f, ih - 8, t.C("fg"));
            if (window_) window_->SetTextInputArea(bx * scale_, iy * scale_, bw * scale_, ih * scale_, (int)((cx - bx) * scale_));
          }
          b.PopClip();
        }
        break;
      }
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
      if (w.swatch) { // colour box (E-4b): the value itself, a click opens the picker
        float sx, sy, sw, sh;
        SwatchBox(w, sx, sy, sw, sh);
        b.Rect(sx - 1, sy - 1, sw + 2, sh + 2, t.C("line-strong"), 3);
        b.Rect(sx, sy, sw, sh, w.swatchColor, 2);
      }
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
      if (!w.tree.empty()) { // tree row (E-2): guide lines per level, the branch to this row, the fold arrow
        constexpr float kStep = 14;
        const Color gl = t.C("line-strong");
        const float my = w.y + w.h * 0.5f, ax = w.x + s5 + w.tree.size() * kStep;
        for (size_t k = 0; k < w.tree.size(); ++k) {
          const float cx = std::floor(w.x + s5 + k * kStep + 6) + 0.5f;
          const uint8_t g = w.tree[k];
          if (g == 1 || g == 2) b.Line(cx, w.y, cx, w.y + w.h, 1, gl);
          if (g == 3) b.Line(cx, w.y, cx, my, 1, gl);
          if (g >= 2) b.Line(cx, my, w.fold >= 0 ? ax : ax + 10, my, 1, gl);
        }
        if (w.fold == 1) { b.Line(ax + 3, my - 4, ax + 7, my, 1.5f, dim); b.Line(ax + 7, my, ax + 3, my + 4, 1.5f, dim); }      // ▶
        else if (w.fold == 0) { b.Line(ax + 1, my - 2, ax + 5, my + 2, 1.5f, dim); b.Line(ax + 5, my + 2, ax + 9, my - 2, 1.5f, dim); } // ▼
        kx = ax + 12;
      }
      const char* rowIcon = w.text2 == "Camera" ? "comp-camera" : w.text2 == "Light" ? "comp-light" : w.text2 == "Prefab" ? "obj-prefab"
                          : w.text2 == "Scene" ? "obj-scene" : w.text2 == "Mesh" ? "comp-mesh" : "obj-object";
      if (Icon(rowIcon, kx - 2, w.y + (w.h - 15) * 0.5f, 15)) {
      } else if (w.text2 == "Camera") {
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
  // A rebuilt panel can put a new widget at the freed address of the focused one: check the id too, and keep the
  // focus on the new widget with the same id (a rebuild should not move the keyboard focus elsewhere).
  if (focused_ && (!reachable(focused_) || focused_->id != focusedId_)) {
    focused_ = nullptr;
    if (!focusedId_.empty()) focused_ = Find(focusedId_);
    if (!focused_) focusedId_.clear();
  }
  if (!reachable(dragSource_)) { dragSource_ = nullptr; dragging_ = false; }
  if (!reachable(dropTarget_)) dropTarget_ = nullptr;
  if (!reachable(sliderDrag_)) sliderDrag_ = nullptr;
  if (!reachable(scrub_)) scrub_ = nullptr;
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
  DrawTooltip();
  DrawMenu();
  DrawDialog();
}

// ---- Modal dialog ---------------------------------------------------------------------------
namespace {
constexpr float kDialogW = 460, kDialogPad = 20, kDialogBtnH = 30, kDialogTitle = 15, kDialogItemH = 30;
}

std::vector<std::string> UIContext::Wrap(const std::string& text, float size, float maxW) const {
  std::vector<std::string> lines; // word wrap at maxW, '\n' forces a break; Korean words wrap at spaces too
  std::string line, word;
  auto flushWord = [&] {
    if (word.empty()) return;
    const std::string tryLine = line.empty() ? word : line + " " + word;
    if (!line.empty() && Measure(size, tryLine) > maxW) { lines.push_back(line); line = word; }
    else line = tryLine;
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flushWord();
    else if (c == '\n') { flushWord(); lines.push_back(line); line.clear(); }
    else word += c;
  }
  flushWord();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

void UIContext::OpenDialog(Dialog d) {
  CloseMenu();
  dialog_ = std::make_unique<Dialog>(std::move(d));
  dialogHot_ = dialogPressed_ = -1;
  for (size_t i = 0; i < dialog_->buttons.size(); ++i) if (dialog_->buttons[i].primary) dialogHot_ = (int)i;
}

void UIContext::DialogBox(float& x, float& y, float& w, float& h, std::vector<std::string>* lines) const {
  const float fs = theme_.FontSize("control", 12.5f);
  const std::vector<std::string> l = dialog_ ? Wrap(dialog_->message, fs, kDialogW - kDialogPad * 2) : std::vector<std::string>{};
  w = kDialogW;
  const size_t items = dialog_ ? dialog_->items.size() : 0;
  h = kDialogPad + kDialogTitle + 14 + l.size() * fs * 1.55f + (items ? items * kDialogItemH + 12 : 0) + 18 +
      (dialog_ && !dialog_->check.empty() ? 30 : 0) + kDialogBtnH + kDialogPad;
  x = root_ ? (root_->w - w) * 0.5f : 100;
  y = root_ ? std::max(20.f, (root_->h - h) * 0.42f) : 100;
  if (lines) *lines = l;
}

bool UIContext::DialogItemRect(size_t i, float& x, float& y, float& w, float& h) const {
  if (!dialog_ || i >= dialog_->items.size()) return false;
  float dx, dy, dw, dh;
  std::vector<std::string> lines;
  DialogBox(dx, dy, dw, dh, &lines);
  x = dx + kDialogPad;
  w = dw - kDialogPad * 2;
  y = dy + kDialogPad + kDialogTitle + 14 + lines.size() * theme_.FontSize("control", 12.5f) * 1.55f + 6 + i * kDialogItemH;
  h = kDialogItemH;
  return true;
}

bool UIContext::DialogButtonRect(size_t i, float& x, float& y, float& w, float& h) const {
  if (!dialog_ || i >= dialog_->buttons.size()) return false;
  float dx, dy, dw, dh;
  DialogBox(dx, dy, dw, dh);
  const float fs = theme_.FontSize("label", 12);
  float right = dx + dw - kDialogPad; // buttons from the right edge, in reverse order (the first ends up leftmost)
  for (size_t k = dialog_->buttons.size(); k-- > 0;) {
    const float bw = std::max(84.f, Measure(fs, dialog_->buttons[k].label) + 28);
    if (k == i) { x = right - bw; y = dy + dh - kDialogPad - kDialogBtnH; w = bw; h = kDialogBtnH; return true; }
    right -= bw + 8;
  }
  return false;
}

bool UIContext::DialogEvent(const InputEvent& e) {
  if (!dialog_) return false;
  auto buttonAt = [&](float x, float y) {
    for (size_t i = 0; i < dialog_->buttons.size(); ++i) {
      float bx, by, bw, bh;
      if (DialogButtonRect(i, bx, by, bw, bh) && x >= bx && y >= by && x < bx + bw && y < by + bh) return (int)i;
    }
    return -1;
  };
  auto press = [&](int i) {
    if (i < 0 || i >= (int)dialog_->buttons.size()) return;
    const Dialog closed = *dialog_;
    std::function<void(const Dialog&)> fn = dialog_->buttons[(size_t)i].run;
    dialog_.reset(); // the action may open another dialog
    if (fn) fn(closed);
  };
  auto itemAt = [&](float x, float y) {
    for (size_t i = 0; i < dialog_->items.size(); ++i) {
      float ix, iy, iw, ih;
      if (DialogItemRect(i, ix, iy, iw, ih) && x >= ix && y >= iy && x < ix + iw && y < iy + ih) return (int)i;
    }
    return -1;
  };
  auto onCheck = [&](float x, float y) {
    if (dialog_->check.empty()) return false;
    float dx, dy, dw, dh;
    DialogBox(dx, dy, dw, dh);
    const float cy = dy + dh - kDialogPad - kDialogBtnH - 30;
    return x >= dx + kDialogPad && x < dx + dw * 0.6f && y >= cy && y < cy + 24;
  };
  switch (e.type) {
    case InputEvent::Type::MouseMove: dialogHot_ = buttonAt(e.x, e.y) >= 0 ? buttonAt(e.x, e.y) : dialogHot_; return true;
    case InputEvent::Type::MouseDown: dialogPressed_ = buttonAt(e.x, e.y); return true;
    case InputEvent::Type::MouseUp: {
      const int i = buttonAt(e.x, e.y);
      if (i >= 0 && i == dialogPressed_) press(i);
      else if (const int it = itemAt(e.x, e.y); dialogPressed_ < 0 && it >= 0) dialog_->items[(size_t)it].checked = !dialog_->items[(size_t)it].checked;
      else if (dialogPressed_ < 0 && onCheck(e.x, e.y)) {
        dialog_->checked = !dialog_->checked;
        if (dialog_->onCheck) dialog_->onCheck(dialog_->checked);
      }
      dialogPressed_ = -1;
      return true;
    }
    case InputEvent::Type::KeyDown: {
      const int n = (int)dialog_->buttons.size();
      if (e.key == Key::Escape) {
        for (int i = 0; i < n; ++i) if (dialog_->buttons[(size_t)i].cancel) { press(i); return true; }
        dialog_.reset();
        return true;
      }
      if (e.key == Key::Enter || e.key == Key::Space) { press(dialogHot_); return true; }
      if ((e.key == Key::Left || (e.key == Key::Tab && e.shift)) && n) dialogHot_ = (dialogHot_ + n - 1) % n;
      else if ((e.key == Key::Right || e.key == Key::Tab) && n) dialogHot_ = (dialogHot_ + 1) % n;
      return true;
    }
    default: return true; // text, wheel: nothing behind the dialog gets them
  }
}

void UIContext::DrawDialog() {
  if (!dialog_ || !root_) return;
  const Theme& t = theme_;
  Batch2D& b = batch_;
  b.Rect(0, 0, root_->w, root_->h, {0, 0, 0, 0.45f}); // dim everything behind
  float x, y, w, h;
  std::vector<std::string> lines;
  DialogBox(x, y, w, h, &lines);
  b.Shadow(x, y + 8, w, h, {0, 0, 0, 0.5f}, 10, 28);
  b.Rect(x, y, w, h, t.C("raise"), 10, 1, t.C("line-strong"));
  b.Rect(x, y, 4, h, t.C("accent"), 2); // brand edge
  const float fs = t.FontSize("control", 12.5f);
  Text(x + kDialogPad, y + kDialogPad - 2, kDialogTitle, t.C("fg"), dialog_->title);
  float ly = y + kDialogPad + kDialogTitle + 14;
  for (const std::string& l : lines) { Text(x + kDialogPad, ly - 1, fs, t.C("fg-dim"), l); ly += fs * 1.55f; }
  if (!dialog_->items.empty()) { // the checklist: one row per item, a box, the name and where it is saved
    float lx, ly0, lw, lh;
    DialogItemRect(0, lx, ly0, lw, lh);
    b.Rect(lx - 6, ly0 - 4, lw + 12, dialog_->items.size() * kDialogItemH + 8, t.C("sunk"), 6, 1, t.C("line"));
    auto box = [&](float bx, float by, bool on) {
      b.Rect(bx, by, 16, 16, on ? t.C("accent") : t.C("sunk"), 3, 1, on ? t.C("accent") : t.C("line-strong"));
      if (on) {
        b.Line(bx + 4, by + 8, bx + 7, by + 11.5f, 2, t.C("on-accent"));
        b.Line(bx + 7, by + 11.5f, bx + 12.5f, by + 4.5f, 2, t.C("on-accent"));
      }
    };
    for (size_t i = 0; i < dialog_->items.size(); ++i) {
      float ix, iy, iw, ih;
      DialogItemRect(i, ix, iy, iw, ih);
      const DialogItem& it = dialog_->items[i];
      box(ix + 4, iy + (ih - 16) * 0.5f, it.checked);
      Text(ix + 30, iy + (ih - fs) * 0.5f - 1, fs, it.checked ? t.C("fg") : t.C("fg-faint"), it.label);
      if (!it.detail.empty()) {
        const float dfs = t.FontSize("caption", 11);
        b.PushClip(ix + 30 + Measure(fs, it.label) + 12, iy, iw - 40 - Measure(fs, it.label), ih);
        Text(ix + 30 + Measure(fs, it.label) + 12, iy + (ih - dfs) * 0.5f - 1, dfs, t.C("fg-faint"), it.detail, true);
        b.PopClip();
      }
    }
  }
  if (!dialog_->check.empty()) {
    const float cy = y + h - kDialogPad - kDialogBtnH - 30 + 4;
    b.Rect(x + kDialogPad, cy, 16, 16, dialog_->checked ? t.C("accent") : t.C("sunk"), 3, 1, dialog_->checked ? t.C("accent") : t.C("line-strong"));
    if (dialog_->checked) { // tick drawn with two lines (no glyph needed)
      b.Line(x + kDialogPad + 4, cy + 8, x + kDialogPad + 7, cy + 11.5f, 2, t.C("on-accent"));
      b.Line(x + kDialogPad + 7, cy + 11.5f, x + kDialogPad + 12.5f, cy + 4.5f, 2, t.C("on-accent"));
    }
    Text(x + kDialogPad + 24, cy + 1, fs, t.C("fg-dim"), dialog_->check);
  }
  const float lfs = t.FontSize("label", 12);
  for (size_t i = 0; i < dialog_->buttons.size(); ++i) {
    float bx, by, bw, bh;
    DialogButtonRect(i, bx, by, bw, bh);
    const DialogButton& btn = dialog_->buttons[i];
    const bool hot = (int)i == dialogHot_;
    Color fill = btn.primary ? t.C("accent") : t.C("win");
    if ((int)i == dialogPressed_) fill.a *= 0.8f;
    b.Rect(bx, by, bw, bh, fill, 5, hot ? 2.f : 1.f, hot ? (btn.primary ? t.C("fg") : t.C("accent")) : btn.primary ? t.C("accent") : t.C("line-strong"));
    Text(bx + (bw - Measure(lfs, btn.label)) * 0.5f, by + (bh - lfs) * 0.5f - 1, lfs, btn.primary ? t.C("on-accent") : t.C("fg"), btn.label);
  }
}

// ---- Popup menu (E-4) ------------------------------------------------------------------------
namespace {
constexpr float kMenuRow = 26, kMenuPad = 4;
}

void UIContext::OpenMenu(float x, float y, std::vector<MenuItem> items, bool search) {
  menuSearch_ = search;
  menuFilter_.clear();
  menuAll_ = search ? items : std::vector<MenuItem>{};
  menu_ = std::move(items);
  menuHot_ = menuPressed_ = -1;
  for (size_t i = 0; i < menu_.size(); ++i) if (menu_[i].checked) menuHot_ = (int)i; // keyboard starts on the current value
  const float fs = theme_.FontSize("control", 12.5f);
  menuW_ = 160;
  for (const MenuItem& it : menu_) menuW_ = std::max(menuW_, Measure(fs, it.label) + 32);
  const float h = (menu_.size() + (search ? 1 : 0)) * kMenuRow + kMenuPad * 2;
  menuX_ = root_ ? std::min(x, root_->w - menuW_ - 2) : x; // keep it on screen
  menuY_ = root_ ? std::min(y, root_->h - h - 2) : y;
}

bool UIContext::MenuItemRect(size_t i, float& x, float& y, float& w, float& h) const {
  if (i >= menu_.size()) return false;
  x = menuX_;
  y = menuY_ + kMenuPad + (i + (menuSearch_ ? 1 : 0)) * kMenuRow;
  w = menuW_;
  h = kMenuRow;
  return true;
}

bool UIContext::MenuEvent(const InputEvent& e) {
  if (menu_.empty()) return false;
  const float top = menuY_ + kMenuPad + (menuSearch_ ? kMenuRow : 0);
  auto itemAt = [&](float x, float y) {
    if (x < menuX_ || x >= menuX_ + menuW_ || y < top) return -1;
    const int i = (int)((y - top) / kMenuRow);
    return i < (int)menu_.size() ? i : -1;
  };
  auto insideSearch = [&](float x, float y) { return menuSearch_ && x >= menuX_ && x < menuX_ + menuW_ && y >= menuY_ && y < top; };
  auto run = [&](int i) {
    if (i < 0 || i >= (int)menu_.size() || !menu_[(size_t)i].enabled) return;
    std::function<void()> fn = menu_[(size_t)i].run; // the item may open another menu
    CloseMenu();
    if (fn) fn();
  };
  switch (e.type) {
    case InputEvent::Type::MouseMove: menuHot_ = itemAt(e.x, e.y); return true;
    case InputEvent::Type::MouseDown:
      menuPressed_ = itemAt(e.x, e.y);
      if (menuPressed_ < 0 && !insideSearch(e.x, e.y)) CloseMenu(); // outside: close (the click does nothing else)
      return true;
    case InputEvent::Type::MouseUp: {
      const int i = itemAt(e.x, e.y);
      if (i >= 0 && i == menuPressed_) run(i);
      menuPressed_ = -1;
      return true;
    }
    case InputEvent::Type::KeyDown: {
      const int n = (int)menu_.size();
      if (e.key == Key::Escape) { CloseMenu(); return true; }
      if (e.key == Key::Down && n) { menuHot_ = (menuHot_ + 1) % n; return true; }
      if (e.key == Key::Up && n) { menuHot_ = menuHot_ <= 0 ? n - 1 : menuHot_ - 1; return true; }
      if (e.key == Key::Enter || (e.key == Key::Space && !menuSearch_)) { run(menuHot_ >= 0 ? menuHot_ : (n == 1 ? 0 : -1)); return true; }
      if (menuSearch_ && e.key == Key::Backspace) {
        while (!menuFilter_.empty() && ((unsigned char)menuFilter_.back() & 0xC0) == 0x80) menuFilter_.pop_back();
        if (!menuFilter_.empty()) menuFilter_.pop_back();
        FilterMenu();
        return true;
      }
      return menuSearch_; // a search menu keeps the keyboard (letters arrive as Text)
    }
    case InputEvent::Type::Text:
      if (!menuSearch_) return false;
      menuFilter_ += e.text;
      FilterMenu();
      return true;
    case InputEvent::Type::TextEditing: return menuSearch_;
    case InputEvent::Type::Wheel: return true;
    default: return false;
  }
}

void UIContext::FilterMenu() {
  auto lower = [](std::string v) { for (char& c : v) c = (char)std::tolower((unsigned char)c); return v; };
  const std::string f = lower(menuFilter_);
  menu_.clear();
  for (const MenuItem& it : menuAll_)
    if (f.empty() || lower(it.label).find(f) != std::string::npos) menu_.push_back(it);
  menuHot_ = menu_.empty() ? -1 : 0;
  if (menu_.empty()) menu_.push_back({"(맞는 항목 없음)", nullptr, false}); // keeps the popup open while typing
}

void UIContext::DrawMenu() {
  if (menu_.empty()) return;
  const Theme& t = theme_;
  Batch2D& b = batch_;
  const float h = (menu_.size() + (menuSearch_ ? 1 : 0)) * kMenuRow + kMenuPad * 2;
  const Theme::Shadow sh = t.S("shadow-pop");
  if (sh.color.a > 0) b.Shadow(menuX_ + sh.x, menuY_ + sh.y, menuW_, h, sh.color, 6, sh.blur);
  else b.Shadow(menuX_, menuY_ + 4, menuW_, h, {0, 0, 0, 0.45f}, 6, 14);
  b.Rect(menuX_, menuY_, menuW_, h, t.C("raise"), 6, 1, t.C("line-strong"));
  const float fs = t.FontSize("control", 12.5f);
  if (menuSearch_) { // filter box: what was typed, or a hint
    const float y = menuY_ + kMenuPad;
    b.Rect(menuX_ + 4, y + 2, menuW_ - 8, kMenuRow - 4, t.C("sunk"), 4, 1, t.C("accent"));
    const bool empty = menuFilter_.empty();
    const float tw = Text(menuX_ + 12, y + (kMenuRow - fs) * 0.5f - 1, fs, empty ? t.C("fg-faint") : t.C("fg"), empty ? "검색..." : menuFilter_);
    if (!empty && (SteadyMs() / kCaretBlinkMs) % 2 == 0) b.Rect(menuX_ + 12 + tw + 1, y + 6, 1, kMenuRow - 12, t.C("fg"));
  }
  for (size_t i = 0; i < menu_.size(); ++i) {
    float x, y, w, rh;
    MenuItemRect(i, x, y, w, rh);
    if ((int)i == menuHot_ && menu_[i].enabled) b.Rect(menuX_ + 3, y, menuW_ - 6, kMenuRow, t.C("selection"), 4);
    if (menu_[i].checked) b.Rect(menuX_ + 6, y + 7, 3, kMenuRow - 14, t.C("accent"), 1.5f); // the current value
    Text(menuX_ + 14, y + (kMenuRow - fs) * 0.5f - 1, fs, !menu_[i].enabled ? t.C("fg-faint") : menu_[i].checked ? t.C("accent") : t.C("fg"), menu_[i].label);
  }
}

void UIContext::SetClipboard(const std::string& text) { if (window_) window_->SetClipboardText(text); }
std::string UIContext::Clipboard() const { return window_ ? window_->GetClipboardText() : std::string(); }

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
  if (w.kind == WidgetKind::Choice) {
    const int cur = ChoiceIndex(w), n = (int)w.items.size();
    const int style = ChoiceStyle(w);
    if (style == 2) { ChoiceSet(w, cur == 1 ? 0 : 1); return; } // toggle (a mixed value turns on)
    if (style == 1 && n) {                                      // segmented: the one clicked, or the next one
      int pick = (cur + 1) % n;
      for (int k = 0; mouseX >= 0 && k < n; ++k) {
        float sx, sw;
        if (ChoiceSegment(w, k, sx, sw) && mouseX >= sx && mouseX < sx + sw) pick = k;
      }
      ChoiceSet(w, pick);
      return;
    }
    OpenChoice(w);
    return;
  }
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
    if (DialogEvent(e)) continue; // a modal dialog takes everything
    if (MenuEvent(e)) continue; // an open popup menu takes the mouse (and Esc)
    bool mouse = e.type == InputEvent::Type::MouseMove || e.type == InputEvent::Type::MouseDown || e.type == InputEvent::Type::MouseUp;
    if (e.type == InputEvent::Type::MouseDown && e.button == 3) { // right-click: the widget's menu (E-4)
      for (Widget* w = HitTest(*root_, e.x, e.y); w;) {
        if (w->onContext) w->onContext(*w, e.x, e.y);
        break;
      }
      continue;
    }
    if (mouse && DockMouse(e)) continue; // splitter / tab drag owns the mouse
    switch (e.type) {
      case InputEvent::Type::MouseMove: {
        if (sliderDrag_) { SliderSet(*sliderDrag_, e.x); break; }
        if (scrub_) { ScrubTo(e.x, e.shift); break; }
        if (dragSource_) { // drag and drop: starts after 6 units so clicks stay clicks
          dragX_ = e.x; dragY_ = e.y;
          if (!dragging_ && std::hypot(e.x - pressX_, e.y - pressY_) > 6.f) dragging_ = true;
          if (dragging_) { dropTarget_ = DropTargetAt(e.x, e.y); break; }
        }
        Widget* hit = HitTest(*root_, e.x, e.y);
        if (hovered_ && hovered_ != hit) hovered_->hovered = false;
        if (hovered_ != hit) hoverSince_ = SteadyMs();
        hovered_ = hit;
        if (hit) hit->hovered = true;
        break;
      }
      case InputEvent::Type::MouseDown: {
        if (e.button != 1) break;
        Widget* under = HitTest(*root_, e.x, e.y);
        if (under && under->kind == WidgetKind::Field) {
          float ix, iy, iw, ih, sx, sy, sw, sh;
          FieldInputBox(*under, ix, iy, iw, ih);
          if (SwatchBox(*under, sx, sy, sw, sh) && e.x >= sx && e.x < sx + sw && e.y >= sy && e.y < sy + sh) { // colour box
            if (under->onSwatch) under->onSwatch(*under);
            break;
          }
          int axis = -2; // label drag: on the label (-1) or on an axis tag (k)
          if (under->scrub && e.x < ix && under->axes == 0) axis = -1;
          for (int k = 0; under->scrub && k < under->axes; ++k) {
            float bx, bw;
            if (AxisBox(*under, k, bx, bw) && e.x >= bx && e.x < bx + 15) axis = k;
          }
          const auto parts = under->axes > 0 ? FieldParts(under->text2, under->axes) : std::vector<std::pair<size_t, size_t>>{};
          if (axis >= -1 && (axis < 0 || !parts.empty())) {
            double start = 0;
            const std::string num = axis < 0 ? under->text2 : under->text2.substr(parts[(size_t)axis].first, parts[(size_t)axis].second - parts[(size_t)axis].first);
            start = std::strtod(num.c_str(), nullptr);
            if (axis < 0 || under->axes > 0) {
              SetFocus(nullptr);
              scrub_ = under;
              scrubAxis_ = axis;
              scrubStart_ = start;
              scrubX_ = e.x;
              break;
            }
          }
        }
        pressed_ = under;
        if (pressed_) pressed_->pressed = true;
        dragSource_ = pressed_ && pressed_->draggable ? pressed_ : nullptr;
        dragging_ = false;
        dropTarget_ = nullptr;
        pressX_ = dragX_ = e.x; pressY_ = dragY_ = e.y;
        SetFocus((pressed_ && pressed_->focusable) ? pressed_ : nullptr);
        if (focused_ && focused_->kind == WidgetKind::Field) focused_->cursor = focused_->anchor = FieldHit(*focused_, e.x);
        if (focused_ && focused_->kind == WidgetKind::Field && focused_->axes > 0) { // a click in a vector box selects that number
          for (const auto& [a, z] : FieldParts(focused_->text2, focused_->axes))
            if (focused_->cursor >= a && focused_->cursor <= z) { focused_->anchor = a; focused_->cursor = z; break; }
        }
        if (focused_ && focused_->kind == WidgetKind::Field && focused_->slider) {
          float ix, iy, iw, ih;
          FieldInputBox(*focused_, ix, iy, iw, ih);
          if (e.x >= ix && e.x < ix + iw && e.y >= iy && e.y < iy + ih) { sliderDrag_ = focused_; SliderSet(*focused_, e.x); }
        }
        break;
      }
      case InputEvent::Type::MouseUp: {
        if (e.button != 1) break;
        if (scrub_) { scrub_ = nullptr; scrubAxis_ = -1; break; }
        if (sliderDrag_) { sliderDrag_ = nullptr; if (pressed_) pressed_->pressed = false; pressed_ = nullptr; break; }
        Widget* hit = HitTest(*root_, e.x, e.y);
        if (dragging_) { // a drop, not a click
          if (dropTarget_ && dropTarget_ != dragSource_ && dropTarget_->onDrop) dropTarget_->onDrop(*dragSource_);
          if (pressed_) pressed_->pressed = false;
        } else if (pressed_) {
          pressed_->pressed = false;
          clickCtrl_ = e.ctrl;
          clickShift_ = e.shift;
          clickAlt_ = e.alt;
          clickX_ = e.x;
          if (hit == pressed_) Activate(*hit, e.x, e.y);
          clickCtrl_ = clickShift_ = clickAlt_ = false;
        }
        pressed_ = nullptr;
        dragSource_ = dropTarget_ = nullptr;
        dragging_ = false;
        break;
      }
      case InputEvent::Type::KeyDown: {
        if (focused_ && focused_->kind == WidgetKind::Field && FieldKey(*focused_, e)) break;
        if (focused_ && focused_->kind == WidgetKind::Choice && ChoiceKey(*focused_, e)) break;
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
  focusedId_ = w ? w->id : std::string();
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
    case WidgetKind::Choice: return w.variant == 2 ? "checkbox" : w.variant == 1 ? "radiogroup" : "combobox";
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
    case WidgetKind::Choice: return w.text + ", " + (w.variant == 2 ? (w.text2 == "true" ? "on" : "off") : w.text2);
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

bool UIContext::OnTreeArrow(const Widget& row, float x) const {
  if (row.tree.empty() || row.fold < 0) return false;
  const float ax = row.x + theme_.N("space-5", 10) + row.tree.size() * 14.f;
  return x >= ax - 3 && x < ax + 13;
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

std::vector<std::pair<size_t, size_t>> UIContext::FieldParts(const std::string& v, int count) {
  std::vector<std::pair<size_t, size_t>> out;
  size_t start = 0;
  for (size_t i = 0; i <= v.size(); ++i)
    if (i == v.size() || v[i] == ',') {
      size_t a = start, z = i;
      while (a < z && v[a] == ' ') ++a;
      while (z > a && v[z - 1] == ' ') --z;
      out.emplace_back(a, z);
      start = i + 1;
    }
  if ((int)out.size() != count) out.clear();
  return out;
}

bool UIContext::AxisBox(const Widget& f, int k, float& x, float& w) const {
  if (f.axes <= 0 || k < 0 || k >= f.axes) return false;
  float ix, iy, iw, ih;
  FieldInputBox(f, ix, iy, iw, ih);
  const float gap = 4, bw = (iw - gap * (f.axes - 1)) / f.axes;
  x = ix + k * (bw + gap);
  w = bw;
  return true;
}

bool UIContext::SwatchBox(const Widget& f, float& x, float& y, float& w, float& h) const {
  if (!f.swatch) return false;
  float ix, iy, iw, ih;
  FieldInputBox(f, ix, iy, iw, ih);
  w = 22;
  h = ih - 6;
  x = ix + iw - w - 3;
  y = iy + 3;
  return true;
}

// Label drag (E-4b): the number moves with the mouse, about 1 % of its size per unit (at least 0.01), whole steps
// for integers; Shift is ten times finer.
void UIContext::ScrubTo(float x, bool fine) {
  Widget& f = *scrub_;
  const double dx = x - scrubX_;
  const double step = f.sliderInteger ? 0.25 : std::max(0.01, std::abs(scrubStart_) * 0.01);
  double v = scrubStart_ + dx * step * (fine ? 0.1 : 1.0);
  std::string text;
  if (f.sliderInteger) text = std::to_string((long long)std::llround(v));
  else {
    text = std::format("{:.3f}", v);
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
  }
  if (scrubAxis_ < 0) {
    if (text == f.text2) return;
    f.text2 = text;
  } else {
    const auto parts = FieldParts(f.text2, f.axes);
    if (parts.empty()) return;
    const auto [a, z] = parts[(size_t)scrubAxis_];
    if (f.text2.substr(a, z - a) == text) return;
    f.text2.replace(a, z - a, text);
  }
  f.cursor = f.anchor = f.text2.size();
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
  if (const auto parts = f.axes > 0 ? FieldParts(f.text2, f.axes) : std::vector<std::pair<size_t, size_t>>{}; !parts.empty()) {
    for (int k = (int)parts.size() - 1; k >= 0; --k) { // the box under the mouse, then the character in it
      float bx, bw;
      AxisBox(f, k, bx, bw);
      if (mouseX < bx && k > 0) continue;
      const auto [a, z] = parts[(size_t)k];
      const float local = mouseX - (bx + 18);
      size_t best = a;
      float bestDist = 1e9f;
      for (size_t i = a;; i = NextBoundary(f.text2, i)) {
        const float d = std::abs(Measure(fs, f.text2.substr(a, i - a)) - local);
        if (d < bestDist) { bestDist = d; best = i; }
        if (i >= z) break;
      }
      return best;
    }
  }
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
