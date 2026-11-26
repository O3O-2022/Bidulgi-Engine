#include "Runtime/UIDesigner.h"
#include "Core/Command.h"
#include "Core/Log.h"
#include "GameUI/GameUIComponents.h"
#include "Render2D/Batch2D.h"
#include "Runtime/EditorModel.h"
#include "Scene/Scene.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <format>
#include <set>

namespace bdg {
namespace {
// kHandle: the drawn size of a resize handle; kHandleHit: half the square a press finds it in (12 x 12, UI review D8)
constexpr float kBarH = 34, kHandle = 8, kHandleHit = 6, kSnap = 6, kPaletteW = 112, kPaletteNarrow = 36, kChipH = 24;
const char* kAnchorNames[] = {"TopLeft", "Top", "TopRight", "Left", "Center", "Right", "BottomLeft", "Bottom", "BottomRight", "Stretch"};
const char* kAnchorKo[] = {"왼쪽 위", "위", "오른쪽 위", "왼쪽", "가운데", "오른쪽", "왼쪽 아래", "아래", "오른쪽 아래", "늘이기"};
struct AlignOp { const char* name; const char* tip; };
const AlignOp kAlign[] = {{"al.left", "왼쪽 맞춤"}, {"al.center", "가로 가운데"}, {"al.right", "오른쪽 맞춤"}, {"al.top", "위 맞춤"},
                          {"al.middle", "세로 가운데"}, {"al.bottom", "아래 맞춤"}, {"al.dist.x", "가로로 고르게"}, {"al.dist.y", "세로로 고르게"}};
std::string Spaced(const std::string& k) { // "ProgressBar" -> "Progress Bar"
  std::string out;
  for (size_t i = 0; i < k.size(); ++i) {
    if (i && std::isupper((unsigned char)k[i]) && std::islower((unsigned char)k[i - 1])) out += ' ';
    out += k[i];
  }
  return out;
}
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }
bool Inside(const GameUI::Rect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
UIElement* ElementOf(EditorModel& model, const std::string& id) {
  SceneObject* o = model.scene.Find(id);
  Component* c = o ? o->Find("UIElement") : nullptr;
  return c ? static_cast<UIElement*>(c->data) : nullptr;
}
std::string Num(float v) { // two decimals at most, no trailing zeros
  std::string s = std::format("{:.2f}", std::round(v * 100.f) / 100.f);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}
} // namespace

const std::vector<UIDesigner::Preset>& UIDesigner::Presets() {
  static const std::vector<Preset> p = {{"16:9 · 1920×1080", 1920, 1080},          {"16:10 · 1920×1200", 1920, 1200},
                                        {"21:9 · 2560×1080", 2560, 1080},          {"4:3 · 1600×1200", 1600, 1200},
                                        {"폰 세로 · 1080×1920", 1080, 1920}, {"폰 가로 · 2340×1080", 2340, 1080}};
  return p;
}

const std::vector<std::string>& UIDesigner::PaletteKinds() {
  static const std::vector<std::string> k = {"Panel", "Text", "Image", "Button", "ProgressBar", "Toggle", "Slider", "InputField", "ScrollView", "Dropdown"};
  return k;
}

std::string UIDesigner::AddElement(EditorModel& model, const std::string& kind, float x, float y) {
  struct Look { const char* kind; float w, h; const char* extra; };
  static const Look looks[] = {
      {"Panel", 400, 300, R"(,"color":[0.15,0.17,0.22,0.9])"},
      {"Text", 320, 60, R"(,"text":"Text")"},
      {"Image", 200, 200, R"(,"color":[1,1,1,1])"},
      {"Button", 280, 72, R"(,"text":"Button")"},
      {"ProgressBar", 400, 36, R"(,"value":0.6)"},
      {"Toggle", 260, 48, R"(,"text":"Toggle","value":1)"},
      {"Slider", 400, 40, R"(,"value":0.5)"},
      {"InputField", 400, 64, R"(,"text":"","placeholder":"Enter text...","align":"Left")"},
      {"ScrollView", 500, 400, R"(,"color":[0.1,0.11,0.14,0.85])"},
      {"Dropdown", 320, 64, R"(,"options":"Option A|Option B|Option C","value":0)"}};
  const Look* look = nullptr;
  for (const Look& l : looks)
    if (kind == l.kind) look = &l;
  if (!look) return "";
  auto unique = [&](const std::string& base) {
    std::string id = base;
    for (int n = 2; model.scene.Find(id); ++n) id = std::format("{}{}", base, n);
    return id;
  };
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    const bool ok = model.bus.Execute(c, &err);
    if (!ok) LogWarn("ui designer: {} {}: {}", name, target, err);
    return ok;
  };
  // where it goes: the panel / scroll view under the point, else the canvas there, else the first canvas, else a new one
  std::string parent;
  GameUI::Rect parentRect = screen_;
  float s = screen_.w / std::max((float)Presets()[(size_t)std::clamp(preset, 0, (int)Presets().size() - 1)].w, 1.f);
  for (const GameUI::Placed& p : placed_) {
    SceneObject* o = model.scene.Find(p.object);
    Component* c = o ? o->Find("UIElement") : nullptr;
    const UIElement* e = c ? static_cast<const UIElement*>(c->data) : nullptr;
    if (e && (e->kind == UIKind::Panel || e->kind == UIKind::ScrollView) && Inside(p.r, x, y)) {
      parent = p.object;
      parentRect = p.r;
      s = p.scale;
    }
  }
  model.bus.BeginGroup(std::string("Add UI ") + kind, "ui");
  if (parent.empty()) {
    for (const GameUI::Placed& p : placed_)
      if (!p.canvas.empty()) {
        parent = p.canvas;
        s = p.scale;
        break;
      }
    if (parent.empty())
      for (const std::string& root : model.scene.Roots())
        if (SceneObject* o = model.scene.Find(root); o && o->Find("UICanvas")) parent = root;
    if (parent.empty()) { // no canvas yet: one at this preset's size
      const Preset& p = Presets()[(size_t)std::clamp(preset, 0, (int)Presets().size() - 1)];
      parent = unique("canvas");
      run("add", "", std::format(R"({{"id":"{}","name":"Canvas"}})", parent));
      run("addcomp", parent, std::format(R"({{"type":"UICanvas","value":{{"referenceSize":[{},{}]}}}})", p.w, p.h));
      s = screen_.w / (float)p.w;
    }
    parentRect = screen_;
  }
  const std::string id = unique(std::string("ui_") + kind);
  const float w = look->w, h = look->h;
  const float px = (x - parentRect.x) / s - w * 0.5f, py = (y - parentRect.y) / s - h * 0.5f; // top-left anchor
  run("add", "", std::format(R"({{"id":"{}","name":"{}","parent":"{}"}})", id, kind, parent));
  run("addcomp", id, std::format(R"({{"type":"UIElement","value":{{"kind":"{}","anchor":"TopLeft","position":[{},{}],"size":[{},{}]{}}}}})", kind,
                                 Num(px), Num(py), Num(w), Num(h), look->extra));
  model.bus.EndGroup();
  model.Select(id);
  return id;
}

void UIDesigner::DrawKindIcon(UIContext& ui, const std::string& kind, float x, float y, Color c) {
  Batch2D& b = ui.Batch();
  const Color clear{0, 0, 0, 0};
  if (kind == "Panel") b.Rect(x, y + 1, 14, 12, clear, 2, 1.5f, c);
  else if (kind == "Text") { b.Rect(x + 2, y + 1, 10, 2, c); b.Rect(x + 6, y + 1, 2, 12, c); }
  else if (kind == "Image") {
    b.Rect(x, y + 1, 14, 12, clear, 2, 1.5f, c);
    const float q[4][2] = {{x + 3, y + 11}, {x + 7, y + 6}, {x + 11, y + 11}, {x + 11, y + 11}};
    b.Quad(q, c);
  } else if (kind == "Button") b.Rect(x, y + 3, 14, 8, c, 3);
  else if (kind == "ProgressBar") { b.Rect(x, y + 4, 14, 6, clear, 3, 1.5f, c); b.Rect(x + 1, y + 5, 8, 4, c, 2); }
  else if (kind == "Toggle") { b.Rect(x, y + 3, 14, 8, clear, 4, 1.5f, c); b.Rect(x + 7, y + 4, 6, 6, c, 3); }
  else if (kind == "Slider") { b.Rect(x, y + 6, 14, 2, c, 1); b.Rect(x + 6, y + 3, 6, 8, c, 3); }
  else if (kind == "InputField") { b.Rect(x, y + 2, 14, 10, clear, 2, 1.5f, c); b.Rect(x + 3, y + 4, 1.5f, 6, c); }
  else if (kind == "ScrollView") { b.Rect(x, y + 1, 14, 12, clear, 2, 1.5f, c); b.Rect(x + 10, y + 3, 2, 5, c, 1); }
  else if (kind == "Dropdown") {
    b.Rect(x, y + 2, 14, 10, clear, 2, 1.5f, c);
    const float q[4][2] = {{x + 8, y + 6}, {x + 12, y + 6}, {x + 10, y + 9}, {x + 10, y + 9}};
    b.Quad(q, c);
  }
}

void UIDesigner::DrawAlignIcon(Batch2D& b, int op, float x, float y, Color c) {
  const float t = 1.5f;
  switch (op) {
    case 0: b.Rect(x, y, t, 14, c); b.Rect(x + 3, y + 3, 9, 3, c); b.Rect(x + 3, y + 8, 6, 3, c); break;            // left
    case 1: b.Rect(x + 6.25f, y, t, 14, c); b.Rect(x + 2, y + 3, 10, 3, c); b.Rect(x + 3.5f, y + 8, 7, 3, c); break; // centre
    case 2: b.Rect(x + 12.5f, y, t, 14, c); b.Rect(x + 2, y + 3, 9, 3, c); b.Rect(x + 5, y + 8, 6, 3, c); break;    // right
    case 3: b.Rect(x, y, 14, t, c); b.Rect(x + 3, y + 3, 3, 9, c); b.Rect(x + 8, y + 3, 3, 6, c); break;            // top
    case 4: b.Rect(x, y + 6.25f, 14, t, c); b.Rect(x + 3, y + 2, 3, 10, c); b.Rect(x + 8, y + 3.5f, 3, 7, c); break; // middle
    case 5: b.Rect(x, y + 12.5f, 14, t, c); b.Rect(x + 3, y + 2, 3, 9, c); b.Rect(x + 8, y + 5, 3, 6, c); break;    // bottom
    case 6: for (int k = 0; k < 3; ++k) b.Rect(x + 1 + k * 5, y + 2, 2, 10, c); break;                                 // across
    default: for (int k = 0; k < 3; ++k) b.Rect(x + 2, y + 1 + k * 5, 10, 2, c); break;                                 // down
  }
}

const GameUI::Placed* UIDesigner::Find(const std::string& id) const {
  for (const GameUI::Placed& p : placed_)
    if (p.object == id) return &p;
  return nullptr;
}

bool UIDesigner::ElementRect(const std::string& id, GameUI::Rect& r) const {
  const GameUI::Placed* p = Find(id);
  if (p) r = p->r;
  return p != nullptr;
}

bool UIDesigner::ControlCenter(const std::string& name, float& x, float& y) const {
  for (const Control& c : controls_)
    if (c.name == name) {
      x = c.x + c.w * 0.5f;
      y = c.y + c.h * 0.5f;
      return true;
    }
  return false;
}

void UIDesigner::Layout(const Widget& view, EditorModel& model) {
  view_ = {view.x, view.y, view.w, view.h};
  const Preset& p = Presets()[(size_t)std::clamp(preset, 0, (int)Presets().size() - 1)];
  paletteW_ = view.w < 700 ? kPaletteNarrow : kPaletteW; // a narrow view: the palette is a column of icons (D4)
  const float margin = std::max(8.f, view.w * 0.02f);
  const float aw = std::max(view.w - paletteW_ - 2 * margin, 10.f), ah = std::max(view.h - kBarH - 2 * margin, 10.f);
  const float fit = std::min(aw / (float)p.w, ah / (float)p.h), s = fit * zoom;
  screen_.w = p.w * s;
  screen_.h = p.h * s;
  screen_.x = view.x + paletteW_ + (view.w - paletteW_ - screen_.w) * 0.5f + panX;
  screen_.y = view.y + kBarH + (view.h - kBarH - screen_.h) * 0.5f + panY;
  game_.Layout(model.scene, screen_);
  placed_ = game_.Placement();
}

bool UIDesigner::HandlePoint(int h, float& x, float& y) const {
  if (selected_.size() != 1) return false;
  const GameUI::Placed* p = Find(selected_[0]);
  if (!p) return false;
  const float xs[3] = {p->r.x, p->r.x + p->r.w * 0.5f, p->r.x + p->r.w}, ys[3] = {p->r.y, p->r.y + p->r.h * 0.5f, p->r.y + p->r.h};
  static const int hx[8] = {0, 1, 2, 2, 2, 1, 0, 0}, hy[8] = {0, 0, 0, 1, 2, 2, 2, 1};
  if (h < 0 || h > 7) return false;
  x = xs[hx[h]];
  y = ys[hy[h]];
  return true;
}

void UIDesigner::Apply(EditorModel& model, const std::string& id, const GameUI::Rect& r, int anchorOverride) {
  const GameUI::Placed* p = Find(id);
  UIElement* e = ElementOf(model, id);
  if (!p || !e) return;
  const int a = anchorOverride >= 0 ? anchorOverride : (int)e->anchor;
  const float s = std::max(p->scale, 1e-6f);
  const GameUI::Rect& P = p->parentRect;
  Vec2 pos, size;
  if (a == (int)UIAnchor::Stretch) { // left / top inset, right / bottom inset
    pos = {(r.x - P.x) / s, (r.y - P.y) / s};
    size = {(P.x + P.w - r.x - r.w) / s, (P.y + P.h - r.y - r.h) / s};
  } else {
    const float ax = (a % 3) * 0.5f, ay = (a / 3) * 0.5f;
    size = {r.w / s, r.h / s};
    pos = {(r.x - P.x - ax * P.w + ax * r.w) / s, (r.y - P.y - ay * P.h + ay * r.h) / s};
  }
  auto set = [&](const char* path, const std::string& value) {
    Command c;
    c.name = "set";
    c.target = id;
    c.args = std::format(R"({{"path":"UIElement.{}","value":{}}})", path, value);
    std::string err;
    if (!model.bus.Execute(c, &err)) LogWarn("ui designer: {} {}: {}", id, path, err);
  };
  if (p->laidOut) { // its parent's layout group places it: only the size is its own
    const std::string lv = std::format("[{},{}]", Num(r.w / s), Num(r.h / s));
    if (lv != std::format("[{},{}]", Num(e->size.x), Num(e->size.y))) set("size", lv);
    return;
  }
  if (anchorOverride >= 0 && anchorOverride != (int)e->anchor) set("anchor", std::format("\"{}\"", kAnchorNames[anchorOverride]));
  const std::string pv = std::format("[{},{}]", Num(pos.x), Num(pos.y)), sv = std::format("[{},{}]", Num(size.x), Num(size.y));
  if (pv != std::format("[{},{}]", Num(e->position.x), Num(e->position.y))) set("position", pv);
  if (sv != std::format("[{},{}]", Num(e->size.x), Num(e->size.y))) set("size", sv);
}

// Edges and centres onto the parent's and the siblings' (within kSnap UI units); the guides drawn for them.
GameUI::Rect UIDesigner::Snap(EditorModel&, const GameUI::Rect& r0, bool moving, bool x0, bool x1, bool y0, bool y1) {
  guides_.clear();
  GameUI::Rect r = r0;
  if (start_.empty()) return r;
  const GameUI::Placed* me = Find(start_[0].first);
  if (!me) return r;
  std::set<std::string> dragged;
  for (auto& [id, rect] : start_) dragged.insert(id);
  std::vector<float> tx, ty; // target lines
  std::vector<GameUI::Rect> owners;
  const GameUI::Rect& P = me->parentRect;
  tx = {P.x, P.x + P.w * 0.5f, P.x + P.w};
  ty = {P.y, P.y + P.h * 0.5f, P.y + P.h};
  for (const GameUI::Placed& o : placed_) {
    if (o.parent != me->parent || dragged.count(o.object)) continue;
    tx.insert(tx.end(), {o.r.x, o.r.x + o.r.w * 0.5f, o.r.x + o.r.w});
    ty.insert(ty.end(), {o.r.y, o.r.y + o.r.h * 0.5f, o.r.y + o.r.h});
  }
  auto best = [&](const std::vector<float>& edges, const std::vector<float>& targets, float& delta, float& at) {
    float d = kSnap + 1;
    for (float e : edges)
      for (float t : targets)
        if (std::fabs(t - e) < std::fabs(d)) { d = t - e; at = t; }
    if (std::fabs(d) > kSnap) return false;
    delta = d;
    return true;
  };
  float d = 0, at = 0;
  if (moving) {
    if (best({r.x, r.x + r.w * 0.5f, r.x + r.w}, tx, d, at)) { r.x += d; guides_.push_back({true, at, P.y, P.y + P.h}); }
    if (best({r.y, r.y + r.h * 0.5f, r.y + r.h}, ty, d, at)) { r.y += d; guides_.push_back({false, at, P.x, P.x + P.w}); }
    return r;
  }
  if (x0 && best({r.x}, tx, d, at)) { r.x += d; r.w -= d; guides_.push_back({true, at, P.y, P.y + P.h}); }
  if (x1 && best({r.x + r.w}, tx, d, at)) { r.w += d; guides_.push_back({true, at, P.y, P.y + P.h}); }
  if (y0 && best({r.y}, ty, d, at)) { r.y += d; r.h -= d; guides_.push_back({false, at, P.x, P.x + P.w}); }
  if (y1 && best({r.y + r.h}, ty, d, at)) { r.h += d; guides_.push_back({false, at, P.x, P.x + P.w}); }
  return r;
}

void UIDesigner::SetAnchor(EditorModel& model, int anchor) {
  if (selected_.empty() || anchor < 0 || anchor > 9) return;
  model.bus.BeginGroup(std::format("UI anchor {}", kAnchorNames[anchor]), "ui");
  for (const std::string& id : selected_)
    if (const GameUI::Placed* p = Find(id)) Apply(model, id, p->r, anchor); // the same rect on screen
  model.bus.EndGroup();
}

void UIDesigner::Align(EditorModel& model, const std::string& how) {
  std::vector<std::pair<std::string, GameUI::Rect>> items;
  for (const std::string& id : selected_)
    if (const GameUI::Placed* p = Find(id)) items.push_back({id, p->r});
  if (items.size() < 2) return;
  float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
  for (auto& [id, r] : items) {
    x0 = std::min(x0, r.x);
    y0 = std::min(y0, r.y);
    x1 = std::max(x1, r.x + r.w);
    y1 = std::max(y1, r.y + r.h);
  }
  if ((how == "dist.x" || how == "dist.y") && items.size() < 3) return;
  model.bus.BeginGroup("UI " + how, "ui");
  if (how == "dist.x" || how == "dist.y") { // equal gaps between the outer two
    const bool x = how == "dist.x";
    std::sort(items.begin(), items.end(), [&](auto& a, auto& b) { return x ? a.second.x < b.second.x : a.second.y < b.second.y; });
    float total = 0;
    for (auto& [id, r] : items) total += x ? r.w : r.h;
    const float gap = ((x ? x1 - x0 : y1 - y0) - total) / (float)(items.size() - 1);
    float at = x ? x0 : y0;
    for (auto& [id, r] : items) {
      GameUI::Rect n = r;
      (x ? n.x : n.y) = at;
      at += (x ? r.w : r.h) + gap;
      Apply(model, id, n);
    }
  } else {
    for (auto& [id, r] : items) {
      GameUI::Rect n = r;
      if (how == "left") n.x = x0;
      else if (how == "center") n.x = (x0 + x1 - r.w) * 0.5f;
      else if (how == "right") n.x = x1 - r.w;
      else if (how == "top") n.y = y0;
      else if (how == "middle") n.y = (y0 + y1 - r.h) * 0.5f;
      else if (how == "bottom") n.y = y1 - r.h;
      Apply(model, id, n);
    }
  }
  model.bus.EndGroup();
}

void UIDesigner::OpenPresetMenu(float x, float y) {
  if (!ui_) return;
  std::vector<UIContext::MenuItem> items;
  for (size_t k = 0; k < Presets().size(); ++k)
    items.push_back({Presets()[k].name, [this, k] { preset = (int)k; }, true, (int)k == preset});
  ui_->OpenMenu(x, y, std::move(items));
}

void UIDesigner::OpenZoomMenu(float x, float y) {
  if (!ui_) return;
  std::vector<UIContext::MenuItem> items;
  items.push_back({"맞춤", [this] { zoom = 1; panX = panY = 0; }, true, zoom == 1 && panX == 0 && panY == 0});
  for (float z : {0.5f, 1.5f, 2.f, 3.f})
    items.push_back({std::format("{}%", (int)std::lround(z * 100)), [this, z] { zoom = z; }, true, zoom == z});
  ui_->OpenMenu(x, y, std::move(items));
}

void UIDesigner::OpenAlignMenu(EditorModel& model, float x, float y) {
  if (!ui_) return;
  std::vector<UIContext::MenuItem> items;
  for (const AlignOp& a : kAlign) items.push_back({a.tip, [this, &model, n = std::string(a.name)] { Align(model, n.substr(3)); }, selected_.size() >= 2});
  ui_->OpenMenu(x, y, std::move(items));
}

void UIDesigner::OpenAnchorMenu(EditorModel& model, float x, float y) {
  if (!ui_) return;
  const UIElement* e = ElementOf(model, selected_.empty() ? std::string() : selected_.back());
  std::vector<UIContext::MenuItem> items;
  for (int k = 0; k < 10; ++k) items.push_back({kAnchorKo[k], [this, &model, k] { SetAnchor(model, k); }, e != nullptr, e && (int)e->anchor == k});
  ui_->OpenMenu(x, y, std::move(items));
}

void UIDesigner::OpenMoreMenu(EditorModel& model, float x, float y) { // the toolbar controls that did not fit (D4)
  if (!ui_) return;
  std::vector<UIContext::MenuItem> items;
  for (const std::string& n : folded_) {
    if (n == "preset") items.push_back({"화면 크기 ▸", [this, x, y] { OpenPresetMenu(x, y); }});
    else if (n == "zoom") items.push_back({"확대 ▸", [this, x, y] { OpenZoomMenu(x, y); }});
    else if (n == "preview") items.push_back({"미리보기", [this] { preview = !preview; }, true, preview});
    else if (n == "anchor") items.push_back({"앵커 ▸", [this, &model, x, y] { OpenAnchorMenu(model, x, y); }});
    else if (n == "align") items.push_back({"정렬 ▸", [this, &model, x, y] { OpenAlignMenu(model, x, y); }});
  }
  ui_->OpenMenu(x, y, std::move(items));
}

void UIDesigner::OpenElementMenu(EditorModel& model, const std::string& id, float x, float y) { // right-click (D8)
  if (!ui_) return;
  SceneObject* o = model.scene.Find(id);
  if (!o) return;
  auto order = [&model, id](int index) { // first child = drawn first (at the back)
    SceneObject* ob = model.scene.Find(id);
    if (!ob) return;
    Command c;
    c.name = "mv";
    c.target = id;
    c.args = std::format(R"({{"parent":"{}","index":{}}})", ob->parent, index);
    std::string err;
    if (!model.bus.Execute(c, &err)) LogWarn("ui designer: {}", err);
  };
  std::vector<UIContext::MenuItem> items = {
      {"복제", [&model] { model.Duplicate(); }},
      {"삭제", [&model] { model.DeleteSelected(); }},
      {"맨 앞으로", [order] { order(-1); }},
      {"맨 뒤로", [order] { order(0); }},
      {"앵커 ▸", [this, &model, x, y] { OpenAnchorMenu(model, x, y); }},
  };
  if (model.shortcuts) {
    items[0].keys = model.shortcuts->KeysOf("edit.duplicate");
    items[1].keys = model.shortcuts->KeysOf("edit.delete");
  }
  ui_->OpenMenu(x, y, std::move(items));
}

bool UIDesigner::Animating() const { return !tipName_.empty() && Now() - tipSince_ < 0.6; }

std::string UIDesigner::Tooltip(const std::string& name) {
  if (name == "preset") return "화면 크기 (기기 프리셋)";
  if (name == "zoom") return "확대 (휠: 포인터 기준, 가운데 끌기: 이동)";
  if (name == "preview") return "미리보기: 게임처럼 눌러 봅니다 (끝나면 값이 되돌아감)";
  if (name == "anchor") return "앵커: 부모의 어느 점에 붙을지";
  if (name == "align") return "정렬 (둘 이상 선택)";
  if (name == "more") return "더 보기";
  for (const AlignOp& a : kAlign)
    if (name == a.name) return a.tip;
  if (name.starts_with("anc.")) return kAnchorKo[std::clamp(std::atoi(name.c_str() + 4), 0, 9)];
  if (name.starts_with("pal.")) return Spaced(name.substr(4)) + ": 끌어다 화면에 놓거나, 클릭하면 가운데에 추가";
  return "";
}

void UIDesigner::HandleEvents(std::vector<InputEvent>& events, const Widget& view, EditorModel& model, UIContext& ui) {
  using T = InputEvent::Type;
  ui_ = &ui;
  Layout(view, model);
  // the selection, kept to UI elements (the Hierarchy and the Scene view share the model's selection)
  selected_.clear();
  for (const std::string& id : model.Selection())
    if (Find(id)) selected_.push_back(id);
  const GameUI::Rect V{view.x, view.y, view.w, view.h};
  for (size_t i = 0; i < events.size();) {
    const InputEvent& e = events[i];
    const bool mouse = e.type == T::MouseDown || e.type == T::MouseUp || e.type == T::MouseMove || e.type == T::Wheel;
    const bool over = Inside(V, e.x, e.y);
    if (ui.MenuOpen() && mouse && drag_ == Drag::None) { ++i; continue; } // an open list owns the mouse
    if (mouse && !over && drag_ == Drag::None) { ++i; continue; }
    if (preview && over && drag_ == Drag::None) { // the running UI gets the wheel and the keyboard
      if (e.type == T::Wheel && Inside(screen_, e.x, e.y)) { previewKeys_.wheel += e.wheel; events.erase(events.begin() + (std::ptrdiff_t)i); continue; }
      if (e.type == T::Text) { previewKeys_.text += e.text; events.erase(events.begin() + (std::ptrdiff_t)i); continue; }
      if (e.type == T::KeyDown && (e.key == Key::Backspace || e.key == Key::Enter)) {
        if (e.key == Key::Backspace) ++previewKeys_.backspaces;
        else previewKeys_.enter = true;
        events.erase(events.begin() + (std::ptrdiff_t)i);
        continue;
      }
    }
    if (!mouse && !(over || Inside(V, mouseX_, mouseY_))) { ++i; continue; }
    bool used = mouse;
    switch (e.type) {
      case T::MouseMove: {
        const float dx = e.x - mouseX_, dy = e.y - mouseY_;
        mouseX_ = e.x;
        mouseY_ = e.y;
        if (drag_ == Drag::Pan) {
          panX += dx;
          panY += dy;
        } else if (drag_ == Drag::Move && !start_.empty()) {
          GameUI::Rect r = start_[0].second;
          r.x += e.x - downX_;
          r.y += e.y - downY_;
          if (!e.ctrl) r = Snap(model, r, true, true, true, true, true);
          else guides_.clear();
          const float ox = r.x - start_[0].second.x, oy = r.y - start_[0].second.y;
          for (auto& [id, r0] : start_) Apply(model, id, {r0.x + ox, r0.y + oy, r0.w, r0.h});
        } else if (drag_ == Drag::Resize && !start_.empty()) {
          static const int hx[8] = {0, 1, 2, 2, 2, 1, 0, 0}, hy[8] = {0, 0, 0, 1, 2, 2, 2, 1};
          GameUI::Rect r = start_[0].second;
          const float mx = e.x - downX_, my = e.y - downY_;
          const bool x0 = hx[handle_] == 0, x1 = hx[handle_] == 2, y0 = hy[handle_] == 0, y1 = hy[handle_] == 2;
          if (x0) { r.x += mx; r.w -= mx; }
          if (x1) r.w += mx;
          if (y0) { r.y += my; r.h -= my; }
          if (y1) r.h += my;
          if (!e.ctrl) r = Snap(model, r, false, x0, x1, y0, y1);
          else guides_.clear();
          if (r.w < 4) { if (x0) r.x -= 4 - r.w; r.w = 4; }
          if (r.h < 4) { if (y0) r.y -= 4 - r.h; r.h = 4; }
          Apply(model, start_[0].first, r);
        }
        used = drag_ != Drag::None || over;
        break;
      }
      case T::Wheel: { // over the palette: scroll it (D4); else zoom around the pointer
        if (e.x < view.x + paletteW_ && e.y > view.y + kBarH) {
          paletteScroll_ = std::clamp(paletteScroll_ - e.wheel * 28.f, 0.f, paletteMax_);
          break;
        }
        const float u = (e.x - screen_.x) / std::max(screen_.w, 1.f), v = (e.y - screen_.y) / std::max(screen_.h, 1.f);
        zoom = std::clamp(zoom * std::pow(1.15f, e.wheel), 0.1f, 8.f);
        Layout(view, model);
        panX += e.x - (screen_.x + u * screen_.w);
        panY += e.y - (screen_.y + v * screen_.h);
        Layout(view, model);
        break;
      }
      case T::MouseDown: {
        mouseX_ = downX_ = e.x;
        mouseY_ = downY_ = e.y;
        if (e.button == 2 || (e.button == 1 && spaceHeld_)) { drag_ = Drag::Pan; break; }
        if (e.button == 3 && !preview) { // right-click on an element: its menu (D8)
          const GameUI::Placed* top = nullptr;
          for (const GameUI::Placed& p : placed_)
            if (Inside(p.r, e.x, e.y) && (!p.clipped || Inside(p.clip, e.x, e.y))) top = &p;
          if (top) {
            if (std::find(selected_.begin(), selected_.end(), top->object) == selected_.end()) {
              model.Select(top->object);
              selected_ = {top->object};
            }
            OpenElementMenu(model, top->object, e.x, e.y);
          }
          break;
        }
        if (e.button != 1) break;
        mouseDown_ = true;
        const Control* hit = nullptr;
        for (const Control& c : controls_)
          if (e.x >= c.x && e.y >= c.y && e.x < c.x + c.w && e.y < c.y + c.h) hit = &c;
        if (hit) {
          const std::string n = hit->name;
          if (n.starts_with("pal.")) { drag_ = Drag::Place; placeKind_ = n.substr(4); break; } // dragged onto the screen
          if (n == "preset") OpenPresetMenu(hit->x, hit->y + hit->h + 3);
          else if (n == "zoom") OpenZoomMenu(hit->x, hit->y + hit->h + 3);
          else if (n == "preview") preview = !preview;
          else if (n == "anchor") anchorOpen_ = !anchorOpen_;
          else if (n == "align") OpenAlignMenu(model, hit->x, hit->y + hit->h + 3);
          else if (n == "more") OpenMoreMenu(model, hit->x, hit->y + hit->h + 3);
          else if (n.starts_with("anc.")) { SetAnchor(model, std::stoi(n.substr(4))); anchorOpen_ = false; }
          else if (n.starts_with("al.")) Align(model, n.substr(3));
          break;
        }
        anchorOpen_ = false;
        if (preview) break; // the UI reacts as in the game (Draw feeds it the mouse)
        float hx2, hy2;
        for (int h = 0; h < 8 && drag_ == Drag::None; ++h)
          if (HandlePoint(h, hx2, hy2) && std::fabs(e.x - hx2) <= kHandleHit && std::fabs(e.y - hy2) <= kHandleHit) {
            drag_ = Drag::Resize;
            handle_ = h;
            start_ = {{selected_[0], Find(selected_[0])->r}};
            model.bus.BeginGroup("Resize UI element", "ui");
          }
        if (drag_ != Drag::None) break;
        const GameUI::Placed* top = nullptr; // the topmost element under the pointer (scroll views cut their content)
        for (const GameUI::Placed& p : placed_)
          if (Inside(p.r, e.x, e.y) && (!p.clipped || Inside(p.clip, e.x, e.y))) top = &p;
        if (!top) {
          if (Inside(screen_, e.x, e.y) || over) model.SetSelection({});
          break;
        }
        std::vector<std::string> sel(selected_.begin(), selected_.end());
        const bool in = std::find(sel.begin(), sel.end(), top->object) != sel.end();
        if (e.ctrl || e.shift) {
          if (in) sel.erase(std::remove(sel.begin(), sel.end(), top->object), sel.end());
          else sel.push_back(top->object);
          model.SetSelection(sel);
          selected_ = sel;
          if (in) break;
        } else if (!in) {
          model.Select(top->object);
          selected_ = {top->object};
        } else { // already selected: it leads the drag
          sel.erase(std::remove(sel.begin(), sel.end(), top->object), sel.end());
          sel.push_back(top->object);
          model.SetSelection(sel);
          selected_ = sel;
        }
        start_.clear();
        if (top->laidOut) break; // placed by its parent's layout group: selected, not dragged (order it in the Hierarchy)
        start_.push_back({top->object, top->r});
        for (const std::string& id : selected_) { // the others along, unless a selected parent carries them
          if (id == top->object) continue;
          const GameUI::Placed* p = Find(id);
          if (!p || p->laidOut) continue;
          bool underSelected = false;
          for (const GameUI::Placed* q = Find(p->parent); q && !underSelected; q = Find(q->parent))
            underSelected = std::find(selected_.begin(), selected_.end(), q->object) != selected_.end();
          if (!underSelected) start_.push_back({id, p->r});
        }
        drag_ = Drag::Move;
        model.bus.BeginGroup("Move UI element", "ui");
        break;
      }
      case T::MouseUp:
        mouseDown_ = false;
        if (drag_ == Drag::Place) { // dropped on the screen: there; a click on the palette item: the screen's middle
          const bool moved = std::fabs(e.x - downX_) + std::fabs(e.y - downY_) > 4;
          if (!moved) AddElement(model, placeKind_, screen_.x + screen_.w * 0.5f, screen_.y + screen_.h * 0.5f);
          else if (Inside(screen_, e.x, e.y)) AddElement(model, placeKind_, e.x, e.y);
          placeKind_.clear();
        }
        if (drag_ == Drag::Move || drag_ == Drag::Resize) model.bus.EndGroup();
        used = drag_ != Drag::None || over;
        drag_ = Drag::None;
        guides_.clear();
        start_.clear();
        break;
      case T::KeyDown:
        if (e.key == Key::Space) { spaceHeld_ = true; used = true; break; }
        if (e.key == Key::Escape && (drag_ == Drag::Move || drag_ == Drag::Resize)) {
          std::string err;
          model.bus.CancelGroup(&err);
          drag_ = Drag::None;
          guides_.clear();
          used = true;
          break;
        }
        if ((e.key == Key::Left || e.key == Key::Right || e.key == Key::Up || e.key == Key::Down) && !selected_.empty() && drag_ == Drag::None) {
          const float step = e.shift ? 10.f : 1.f; // reference pixels
          model.bus.BeginGroup("Nudge UI element", "ui");
          for (const std::string& id : selected_)
            if (const GameUI::Placed* p = Find(id)) {
              GameUI::Rect r = p->r;
              const float s = p->scale;
              r.x += (e.key == Key::Left ? -step : e.key == Key::Right ? step : 0) * s;
              r.y += (e.key == Key::Up ? -step : e.key == Key::Down ? step : 0) * s;
              Apply(model, id, r);
            }
          model.bus.EndGroup();
          used = true;
        } else {
          used = false;
        }
        break;
      case T::KeyUp:
        if (e.key == Key::Space) { spaceHeld_ = false; used = true; }
        else used = false;
        break;
      default:
        used = false;
    }
    // preview: every press / release / move reaches the running UI as it happens (two in one frame still click)
    if (preview && drag_ == Drag::None && (e.type == T::MouseDown || e.type == T::MouseUp || e.type == T::MouseMove) && previewWas_)
      game_.Input(model.scene, screen_, GameUI::Pointer{mouseX_, mouseY_, mouseDown_});
    if (used) events.erase(events.begin() + (std::ptrdiff_t)i);
    else ++i;
  }
  if (drag_ != Drag::None) Layout(view, model);
}

void UIDesigner::Draw(Widget& w, UIContext& ui, EditorModel& model, Font* font, RHI& rhi, const std::string& assetsDir) {
  ui_ = &ui;
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  Layout(w, model);
  selected_.clear();
  for (const std::string& id : model.Selection())
    if (Find(id)) selected_.push_back(id);
  b.PushClip(w.x, w.y, w.w, w.h);
  b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"));
  // the device screen and the game's UI on it
  b.Rect(screen_.x - 1, screen_.y - 1, screen_.w + 2, screen_.h + 2, {0, 0, 0, 0}, 0, 1, t.C("line"));
  b.Rect(screen_.x, screen_.y, screen_.w, screen_.h, t.C("ground"));
  if (preview != previewWas_) { // entering the preview keeps the values it may change; leaving puts them back
    if (preview) {
      kept_.clear();
      for (const GameUI::Placed& pl : placed_)
        if (UIElement* e = ElementOf(model, pl.object)) kept_.push_back({pl.object, e->value, e->text, e->scroll, e->clicks});
    } else {
      for (const Kept& k : kept_)
        if (UIElement* e = ElementOf(model, k.id)) {
          e->value = k.value;
          e->text = k.text;
          e->scroll = k.scroll;
          e->clicks = k.clicks;
          e->focused = e->open = false;
        }
      kept_.clear();
      game_.ResetInput(); // no focus or open list left over
    }
    previewWas_ = preview;
  }
  if (preview) { // the UI runs as in the game: hover, press, toggles, sliders, typing, scrolling, dropdowns
    GameUI::Pointer ptr = previewKeys_;
    ptr.x = mouseX_;
    ptr.y = mouseY_;
    ptr.down = mouseDown_;
    game_.Input(model.scene, screen_, ptr);
    previewKeys_ = {};
  } else {
    for (const GameUI::Placed& p : placed_)
      if (UIElement* e = ElementOf(model, p.object)) e->hovered = e->pressed = false;
  }
  game_.Draw(model.scene, b, font, rhi, screen_, assetsDir);
  placed_ = game_.Placement();
  if (placed_.empty()) {
    const char* msg = "UICanvas가 없습니다: 왼쪽 팔레트에서 끌어다 놓으면 만들어집니다";
    const float fs = t.FontSize("label", 12);
    ui.Text(screen_.x + (screen_.w - ui.Measure(fs, msg)) * 0.5f, screen_.y + screen_.h * 0.5f - 8, fs, t.C("fg-dim"), msg);
  }
  if (preview) b.Rect(screen_.x - 2, screen_.y - 2, screen_.w + 4, screen_.h + 4, {0, 0, 0, 0}, 0, 2, t.C("accent")); // running (D3)
  if (!preview) {
    for (const GameUI::Placed& p : placed_) { // every element's outline, faint (cut by scroll views as drawn)
      if (p.clipped) b.PushClip(p.clip.x, p.clip.y, p.clip.w, p.clip.h);
      b.Rect(p.r.x, p.r.y, p.r.w, p.r.h, {0, 0, 0, 0}, 0, 1, WithAlpha(t.C("fg"), 0.18f));
      if (p.clipped) b.PopClip();
    }
    for (const std::string& id : selected_) {
      const GameUI::Placed* p = Find(id);
      if (!p) continue;
      b.Rect(p->r.x - 1, p->r.y - 1, p->r.w + 2, p->r.h + 2, {0, 0, 0, 0}, 0, 2, t.C("accent"));
      if (UIElement* e = ElementOf(model, id); e && e->anchor != UIAnchor::Stretch) { // the anchor point on the parent
        const int a = (int)e->anchor;
        const float ax = p->parentRect.x + (a % 3) * 0.5f * p->parentRect.w, ay = p->parentRect.y + (a / 3) * 0.5f * p->parentRect.h;
        const float q[4][2] = {{ax, ay - 6}, {ax + 6, ay}, {ax, ay + 6}, {ax - 6, ay}};
        b.Quad(q, WithAlpha(t.C("fg"), 0.9f));
        b.Line(ax, ay, p->r.x + (a % 3) * 0.5f * p->r.w, p->r.y + (a / 3) * 0.5f * p->r.h, 1, WithAlpha(t.C("accent"), 0.6f));
      } else if (e) {
        b.Rect(p->parentRect.x, p->parentRect.y, p->parentRect.w, p->parentRect.h, {0, 0, 0, 0}, 0, 1, WithAlpha(t.C("fg"), 0.45f));
      }
    }
    if (selected_.size() == 1)
      for (int h = 0; h < 8; ++h) {
        float x, y;
        if (HandlePoint(h, x, y)) b.Rect(x - kHandle * 0.5f, y - kHandle * 0.5f, kHandle, kHandle, t.C("fg"), t.N("radius-xs", 2), 1, t.C("accent"));
      }
    for (const Guide& g : guides_) // snap guides
      if (g.vertical) b.Line(g.at, g.from, g.at, g.to, 1.f, t.C("guide"));
      else b.Line(g.from, g.at, g.to, g.at, 1.f, t.C("guide"));
    if (selected_.size() == 1) // size label under the element
      if (const GameUI::Placed* p = Find(selected_[0]); p && p->scale > 0) {
        const std::string label = std::format("{} × {}", Num(p->r.w / p->scale), Num(p->r.h / p->scale));
        const float fs = t.FontSize("overlay", 11), lw = ui.Measure(fs, label) + 12;
        b.Rect(p->r.x + (p->r.w - lw) * 0.5f, p->r.y + p->r.h + 6, lw, 18, WithAlpha(t.C("win"), 0.9f), t.N("radius-pill", 12));
        ui.Text(p->r.x + (p->r.w - lw) * 0.5f + 6, p->r.y + p->r.h + 8, fs, t.C("fg"), label);
      }
  }
  controls_.clear();
  const float fsChip = t.FontSize("caption", 11);
  const bool idle = drag_ == Drag::None && !ui.MenuOpen();
  auto hover = [&](float x, float y, float cw, float ch) { return idle && mouseX_ >= x && mouseY_ >= y && mouseX_ < x + cw && mouseY_ < y + ch; };
  { // palette: the element kinds, dragged (or clicked) onto the screen; a narrow view shows icons only (D4, D5)
    const bool narrow = paletteW_ < kPaletteW;
    const float top = w.y + kBarH, clipH = w.h - kBarH;
    b.PushClip(w.x, top, paletteW_, clipH);
    float py = top + 6 - paletteScroll_;
    if (!narrow) {
      ui.Text(w.x + 12, py, t.FontSize("caption", 11), t.C("fg-faint"), "팔레트");
      py += 20;
    }
    const float ix = w.x + 6, iw = paletteW_ - 12;
    for (const std::string& k : PaletteKinds()) {
      const bool hot = drag_ == Drag::Place && placeKind_ == k, over = hover(ix, py, iw, 24) && py >= top && py + 24 <= top + clipH;
      b.Rect(ix, py, iw, 24, hot ? t.C("selection") : over ? t.C("raise-hover") : WithAlpha(t.C("win"), 0.9f), t.N("radius-md", 4), 1,
             hot ? t.C("accent") : t.C("line"));
      DrawKindIcon(ui, k, narrow ? ix + (iw - 14) * 0.5f : ix + 6, py + 5, t.C(hot || over ? "fg" : "fg-dim"));
      if (!narrow) ui.Text(ix + 26, py + (24 - fsChip) * 0.5f - 1, fsChip, t.C("fg"), Spaced(k));
      if (py + 24 > top && py < top + clipH) controls_.push_back({"pal." + k, ix, std::max(py, top), iw, std::min(py + 24, top + clipH) - std::max(py, top)});
      py += 28;
    }
    paletteMax_ = std::max(0.f, py + paletteScroll_ - (top + clipH) + 6);
    paletteScroll_ = std::min(paletteScroll_, paletteMax_);
    b.PopClip();
    if (drag_ == Drag::Place && Inside(screen_, mouseX_, mouseY_)) // where it will land
      b.Rect(mouseX_ - 40, mouseY_ - 16, 80, 32, WithAlpha(t.C("accent"), 0.35f), 4, 1.5f, t.C("accent"));
  }
  // toolbar: preset, zoom, preview, anchor, align; what does not fit folds into "»" (D4)
  float x = w.x + 8 + paletteW_;
  const float y = w.y + (kBarH - kChipH) * 0.5f, right = w.x + w.w - 8;
  auto chipW = [&](const std::string& label, bool menu) { return ui.Measure(fsChip, label) + 20 + (menu ? 12.f : 0.f); };
  auto chip = [&](const std::string& name, const std::string& label, bool on, bool menu) {
    const float cw = chipW(label, menu);
    const bool over = hover(x, y, cw, kChipH);
    // on: the selection colour with an accent edge and normal text (D3: no white on accent)
    b.Rect(x, y, cw, kChipH, on ? t.C("selection") : over ? t.C("raise-hover") : WithAlpha(t.C("win"), 0.9f), t.N("radius-pill", 12), 1,
           on ? t.C("accent") : t.C("line"));
    ui.Text(x + 10, y + (kChipH - fsChip) * 0.5f - 1, fsChip, t.C("fg"), label);
    if (menu) {
      const float cx = x + cw - 12, cy = y + kChipH * 0.5f;
      const float tri[4][2] = {{cx - 3, cy - 1.5f}, {cx + 3, cy - 1.5f}, {cx, cy + 2.5f}, {cx, cy + 2.5f}};
      b.Quad(tri, t.C("fg-dim"));
    }
    controls_.push_back({name, x, y, cw, kChipH});
    x += cw + 6;
  };
  struct Spec { std::string name, label; bool on, menu; };
  std::vector<Spec> specs = {{"preset", Presets()[(size_t)std::clamp(preset, 0, (int)Presets().size() - 1)].name, false, true},
                             {"zoom", zoom == 1 && panX == 0 && panY == 0 ? std::string("맞춤") : std::format("{}%", (int)std::lround(zoom * 100)), false, true},
                             {"preview", "미리보기", preview, false}};
  if (!selected_.empty()) {
    UIElement* e = ElementOf(model, selected_.back());
    specs.push_back({"anchor", std::string("앵커: ") + (e ? kAnchorKo[(int)e->anchor] : "-"), anchorOpen_, true});
  }
  if (selected_.size() >= 2) specs.push_back({"align", "", false, false});
  const float more = 34, alignW = 8 * kChipH;
  folded_.clear();
  for (size_t i = 0; i < specs.size(); ++i) {
    const Spec& sp = specs[i];
    const float need = sp.name == "align" ? alignW : chipW(sp.label, sp.menu), tail = i + 1 < specs.size() ? more : 0.f;
    if (!folded_.empty() || x + need + tail > right) {
      if (sp.name == "align" && folded_.empty() && x + chipW("정렬", true) <= right) { chip("align", "정렬", false, true); continue; }
      folded_.push_back(sp.name);
      continue;
    }
    if (sp.name != "align") { chip(sp.name, sp.label, sp.on, sp.menu); continue; }
    // align: eight 24 x 24 icon buttons in one group, outer corners rounded (D2)
    b.Rect(x, y, alignW, kChipH, WithAlpha(t.C("win"), 0.9f), t.N("radius-md", 4), 1, t.C("line"));
    for (int k = 0; k < 8; ++k) {
      const float cx = x + k * kChipH;
      if (hover(cx, y, kChipH, kChipH)) b.Rect(cx + 1, y + 1, kChipH - 2, kChipH - 2, t.C("raise-hover"), t.N("radius-xs", 2));
      if (k) b.Line(cx, y + 4, cx, y + kChipH - 4, 1, t.C("line"));
      DrawAlignIcon(b, k, cx + 5, y + 5, t.C("fg"));
      controls_.push_back({kAlign[k].name, cx, y, kChipH, kChipH});
    }
    x += alignW + 6;
  }
  if (!folded_.empty()) chip("more", "»", false, false);
  if (anchorOpen_) { // the anchor picker: 3 x 3 points and stretch, the current one lit
    const Control* ac = nullptr;
    for (const Control& c : controls_)
      if (c.name == "anchor") ac = &c;
    if (ac) {
      UIElement* e = ElementOf(model, selected_.empty() ? std::string() : selected_.back());
      const int cur = e ? (int)e->anchor : -1;
      const float px = ac->x, py = ac->y + ac->h + 4, cell = 30;
      b.Rect(px, py, cell * 3 + 16, cell * 3 + 46, WithAlpha(t.C("win"), 0.98f), t.N("radius-window", 8), 1, t.C("line"));
      for (int k = 0; k < 9; ++k) {
        const float cx = px + 8 + (k % 3) * cell, cy = py + 8 + (k / 3) * cell;
        b.Rect(cx, cy, cell - 4, cell - 4, k == cur ? t.C("selection") : hover(cx, cy, cell - 4, cell - 4) ? t.C("raise-hover") : WithAlpha(t.C("sunk"), 0.9f),
               t.N("radius-md", 4), 1, k == cur ? t.C("accent") : t.C("line"));
        const float dx = cx + 4 + (k % 3) * 0.5f * (cell - 12), dy = cy + 4 + (k / 3) * 0.5f * (cell - 12);
        b.Rect(dx, dy, 4, 4, k == cur ? t.C("accent") : t.C("fg"), 1);
        controls_.push_back({std::format("anc.{}", k), cx, cy, cell - 4, cell - 4});
      }
      const float sy = py + 8 + 3 * cell;
      b.Rect(px + 8, sy, cell * 3 - 4, 26, cur == 9 ? t.C("selection") : WithAlpha(t.C("sunk"), 0.9f), t.N("radius-md", 4), 1, cur == 9 ? t.C("accent") : t.C("line"));
      ui.Text(px + 8 + (cell * 3 - 4 - ui.Measure(fsChip, kAnchorKo[9])) * 0.5f, sy + (26 - fsChip) * 0.5f - 1, fsChip, t.C("fg"), kAnchorKo[9]);
      controls_.push_back({"anc.9", px + 8, sy, cell * 3 - 4, 26});
    }
  }
  // tooltip after the pointer rests on a control (D2, D5)
  std::string over;
  for (const Control& c : controls_)
    if (hover(c.x, c.y, c.w, c.h)) over = c.name;
  if (over != tipName_) {
    tipName_ = over;
    tipSince_ = Now();
  }
  if (!tipName_.empty() && Now() - tipSince_ > 0.45)
    for (const Control& c : controls_)
      if (c.name == tipName_) {
        const std::string tip = Tooltip(c.name);
        if (tip.empty()) break;
        const float fs = t.FontSize("caption", 11), tw = ui.Measure(fs, tip) + 16;
        const float tx = std::clamp(c.name.starts_with("pal.") ? c.x + c.w + 6 : c.x, w.x, std::max(w.x, w.x + w.w - tw));
        const float ty = c.name.starts_with("pal.") ? c.y + 2 : c.y + c.h + 4;
        b.Rect(tx, ty, tw, 20, WithAlpha(t.C("win"), 0.96f), t.N("radius-md", 4), 1, t.C("line"));
        ui.Text(tx + 8, ty + (20 - fs) * 0.5f - 1, fs, t.C("fg"), tip);
        break;
      }
  b.PopClip();
}
} // namespace bdg
