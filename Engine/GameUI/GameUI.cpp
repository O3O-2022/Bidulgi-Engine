#include "GameUI/GameUI.h"
#include "GameUI/GameUIComponents.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Render2D/Batch2D.h"
#include "Scene/Scene.h"
#include "Text/Font.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>

namespace bdg {
namespace {
Color Shade(Color c, float k) { // k > 1 lighter, < 1 darker
  return {std::clamp(c.r * k, 0.f, 1.f), std::clamp(c.g * k, 0.f, 1.f), std::clamp(c.b * k, 0.f, 1.f), c.a};
}
const Color kButtonFill{0.16f, 0.19f, 0.26f, 0.88f};
const Color kBarFill{0.08f, 0.09f, 0.12f, 0.75f};
const Color kFieldFill{0.05f, 0.06f, 0.08f, 0.85f};
const Color kAccent{0.35f, 0.62f, 1.f, 1.f};
bool Inside(const GameUI::Rect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
bool IsControl(UIKind k) {
  return k == UIKind::Button || k == UIKind::Toggle || k == UIKind::Slider || k == UIKind::InputField || k == UIKind::Dropdown;
}
void PopUtf8(std::string& s) { // removes the last UTF-8 character
  if (s.empty()) return;
  size_t n = s.size() - 1;
  while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
  s.erase(n);
}
} // namespace

void GameUI::Layout(Scene& scene, const Rect& view) {
  layout_.clear();
  struct Canvas { std::vector<Item> items; int order; size_t index; };
  std::vector<Canvas> canvases;
  std::string canvasId;
  // children of a canvas (or of an element), placed inside `parent`; inside scroll views they are shifted by its
  // scroll and clipped to it
  auto place = [&](auto&& self, const SceneObject& o, const Rect& parent, float s, bool clipped, const Rect& clip, std::vector<Item>& out) -> float {
    float bottom = 0; // the lowest child edge below the parent's top (reference pixels): a scroll view's content height
    const Component* oc = const_cast<SceneObject&>(o).Find("UIElement"); // (Find is not const)
    const UIElement* group = oc ? static_cast<const UIElement*>(oc->data) : nullptr;
    const UILayout layout = group ? group->layout : UILayout::None;
    float cursor = 0; // layout: where the next child goes (reference pixels from the padded edge)
    int slot = 0;     // grid: the next cell
    for (const std::string& id : o.children) {
      SceneObject* c = scene.Find(id);
      if (!c) continue;
      Component* comp = c->Find("UIElement");
      if (!comp) { bottom = std::max(bottom, self(self, *c, parent, s, clipped, clip, out)); continue; } // a plain grouping object
      auto* e = static_cast<UIElement*>(comp->data);
      if (!e->visible) continue; // hidden with its children
      Rect r;
      const bool laidOut = layout != UILayout::None;
      if (laidOut) { // the parent's layout group places it
        const float pad = group->padding, gap = group->spacing;
        if (layout == UILayout::Horizontal) {
          r = {parent.x + (pad + cursor) * s, parent.y + pad * s, e->size.x * s, e->size.y * s};
          cursor += e->size.x + gap;
        } else if (layout == UILayout::Vertical) {
          r = {parent.x + pad * s, parent.y + (pad + cursor) * s, e->size.x * s, e->size.y * s};
          cursor += e->size.y + gap;
        } else { // grid: as many cells across as fit
          const Vec2 cell{std::max(group->cellSize.x, 1.f), std::max(group->cellSize.y, 1.f)};
          const int cols = std::max(1, (int)std::floor((parent.w / std::max(s, 1e-6f) - 2 * pad + gap) / (cell.x + gap)));
          const int col = slot % cols, row = slot / cols;
          r = {parent.x + (pad + col * (cell.x + gap)) * s, parent.y + (pad + row * (cell.y + gap)) * s, cell.x * s, cell.y * s};
          ++slot;
        }
      } else if (e->anchor == UIAnchor::Stretch) {
        r = {parent.x + e->position.x * s, parent.y + e->position.y * s, parent.w - (e->position.x + e->size.x) * s,
             parent.h - (e->position.y + e->size.y) * s};
      } else {
        const int a = (int)e->anchor;
        const float ax = (a % 3) * 0.5f, ay = (a / 3) * 0.5f;
        r.w = e->size.x * s;
        r.h = e->size.y * s;
        r.x = parent.x + ax * parent.w + e->position.x * s - ax * r.w;
        r.y = parent.y + ay * parent.h + e->position.y * s - ay * r.h;
      }
      bottom = std::max(bottom, (r.y + r.h - parent.y) / std::max(s, 1e-6f));
      out.push_back({c->id, e, r, s, 0, o.id, canvasId, parent, clipped, clip, 0, laidOut});
      const size_t self_ = out.size() - 1;
      if (e->kind == UIKind::ScrollView) { // the content moves up by the scroll and is cut at the view's edges
        Rect inner = r;
        inner.x -= e->scroll.x * s;
        inner.y -= e->scroll.y * s;
        Rect cl = r;
        if (clipped) { // inside another scroll view: both
          const float x0 = std::max(cl.x, clip.x), y0 = std::max(cl.y, clip.y), x1 = std::min(cl.x + cl.w, clip.x + clip.w), y1 = std::min(cl.y + cl.h, clip.y + clip.h);
          cl = {x0, y0, std::max(x1 - x0, 0.f), std::max(y1 - y0, 0.f)};
        }
        const float h = self(self, *c, inner, s, true, cl, out);
        out[self_].contentH = h;
      } else {
        self(self, *c, r, s, clipped, clip, out);
      }
    }
    return bottom;
  };
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    if (Component* cc = o->Find("UICanvas")) {
      const auto* cv = static_cast<const UICanvas*>(cc->data);
      if (!cv->visible) continue;
      const float sx = view.w / std::max(cv->referenceSize.x, 1.f), sy = view.h / std::max(cv->referenceSize.y, 1.f);
      const float m = std::clamp(cv->matchHeight, 0.f, 1.f), s = sx + (sy - sx) * m;
      Canvas c{{}, cv->order, canvases.size()};
      canvasId = o->id;
      place(place, *o, view, s, false, Rect{}, c.items);
      canvases.push_back(std::move(c));
      continue; // a canvas inside a canvas is not supported: its children belong to it
    }
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
  }
  std::stable_sort(canvases.begin(), canvases.end(), [](const Canvas& a, const Canvas& b) { return a.order < b.order; });
  for (Canvas& c : canvases)
    for (Item& i : c.items) layout_.push_back(std::move(i));
}

std::vector<std::string> GameUI::Options(const UIElement& e) const {
  std::vector<std::string> out;
  for (size_t a = 0; a <= e.options.size();) {
    size_t n = e.options.find('|', a);
    if (n == std::string::npos) n = e.options.size();
    if (n > a) out.push_back(e.options.substr(a, n - a));
    a = n + 1;
  }
  return out;
}

const GameUI::Item* GameUI::Hit(float x, float y, bool controlsOnly) const {
  const Item* top = nullptr; // the last drawn (topmost) under the point
  for (const Item& i : layout_)
    if (Inside(i.r, x, y) && (!i.clipped || Inside(i.clip, x, y)) && (!controlsOnly || (IsControl(i.e->kind) && i.e->interactable))) top = &i;
  return top;
}

bool GameUI::DropdownRow(const std::string& id, int index, Rect& out) const {
  if (id != openList_) return false;
  for (const Item& i : layout_)
    if (i.object == id) {
      const int n = (int)Options(*i.e).size();
      if (index < 0 || index >= n) return false;
      out = {i.r.x, i.r.y + i.r.h * (float)(index + 1), i.r.w, i.r.h};
      return true;
    }
  return false;
}

void GameUI::Input(Scene& scene, const Rect& view, const Pointer& p) {
  Layout(scene, view);
  const bool pressed = p.down && !wasDown_, released = !p.down && wasDown_;
  for (const Item& i : layout_) {
    i.e->hovered = i.e->pressed = false;
    i.e->focused = i.object == focused_;
    i.e->open = i.object == openList_;
  }
  // an open dropdown list is on top of everything: a press picks a row or closes it
  if (!openList_.empty() && pressed) {
    const Item* dd = nullptr;
    for (const Item& i : layout_)
      if (i.object == openList_) dd = &i;
    openList_.clear();
    if (dd) {
      dd->e->open = false;
      const int n = (int)Options(*dd->e).size();
      for (int k = 0; k < n; ++k) {
        const Rect row{dd->r.x, dd->r.y + dd->r.h * (float)(k + 1), dd->r.w, dd->r.h}; // as DropdownRow
        if (Inside(row, p.x, p.y)) {
          dd->e->value = (float)k;
          ++dd->e->clicks;
          ++clicks_;
          LogInfo("ui: {} chose {}", scene.Find(dd->object) ? scene.Find(dd->object)->name : dd->object, k);
          wasDown_ = p.down;
          pressedOn_.clear();
          return;
        }
      }
      if (Inside(dd->r, p.x, p.y)) { // a press on the dropdown itself closes it
        wasDown_ = p.down;
        pressedOn_.clear();
        return;
      }
    }
  }
  const Item* top = Hit(p.x, p.y, true);
  if (top) top->e->hovered = true;
  if (pressed) {
    pressedOn_ = top ? top->object : std::string();
    if (focused_ != pressedOn_) focused_.clear(); // a press elsewhere ends typing
    if (top && top->e->kind == UIKind::Slider) sliding_ = top->object;
  }
  if (p.down && top && top->object == pressedOn_) top->e->pressed = true;
  if (!sliding_.empty()) { // the slider follows the pointer while held
    for (const Item& i : layout_)
      if (i.object == sliding_ && i.r.w > 0) i.e->value = std::clamp((p.x - i.r.x) / i.r.w, 0.f, 1.f);
    if (!p.down) sliding_.clear();
  }
  if (released) {
    if (top && top->object == pressedOn_) { // released on the control it was pressed on: a click
      UIElement& e = *top->e;
      switch (e.kind) {
        case UIKind::Toggle: e.value = e.value >= 0.5f ? 0.f : 1.f; break;
        case UIKind::InputField: focused_ = top->object; break;
        case UIKind::Dropdown: openList_ = top->object; break;
        default: break;
      }
      ++e.clicks;
      ++clicks_;
      LogInfo("ui: {} clicked", scene.Find(top->object) ? scene.Find(top->object)->name : top->object);
    }
    pressedOn_.clear();
  }
  if (!focused_.empty()) // typing into the focused input field
    for (const Item& i : layout_)
      if (i.object == focused_) {
        for (int k = 0; k < p.backspaces; ++k) PopUtf8(i.e->text);
        for (char ch : p.text)
          if (ch != '\r' && ch != '\n') i.e->text += ch;
        if (p.enter) focused_.clear();
      }
  if (p.wheel != 0) { // the scroll view under the pointer
    const Item* sv = nullptr;
    for (const Item& i : layout_)
      if (i.e->kind == UIKind::ScrollView && Inside(i.r, p.x, p.y) && (!i.clipped || Inside(i.clip, p.x, p.y))) sv = &i;
    if (sv) {
      const float viewH = sv->r.h / std::max(sv->scale, 1e-6f), maxScroll = std::max(sv->contentH - viewH, 0.f);
      sv->e->scroll.y = std::clamp(sv->e->scroll.y - p.wheel * 40.f, 0.f, maxScroll);
    }
  }
  for (const Item& i : layout_) {
    i.e->focused = i.object == focused_;
    i.e->open = i.object == openList_;
  }
  wasDown_ = p.down;
}

void GameUI::Draw(Scene& scene, Batch2D& b, Font* font, RHI& rhi, const Rect& view, const std::string& assetsDir) {
  Layout(scene, view);
  if (layout_.empty()) return;
  b.PushClip(view.x, view.y, view.w, view.h);
  auto textIn = [&](const Item& i, const Rect& r, const std::string& s, UITextAlign align, Color col) {
    if (!font || s.empty()) return;
    const float size = i.e->fontSize * i.scale, lh = font->LineHeight(size);
    std::vector<std::string_view> lines;
    for (size_t a = 0; a <= s.size();) {
      size_t n = s.find('\n', a);
      if (n == std::string::npos) n = s.size();
      lines.emplace_back(s.data() + a, n - a);
      a = n + 1;
    }
    float y = r.y + (r.h - lh * (float)lines.size()) * 0.5f;
    const float pad = align == UITextAlign::Center ? 0 : std::min(12 * i.scale, r.w * 0.1f);
    for (std::string_view l : lines) {
      const float w = font->Measure(size, l);
      const float x = align == UITextAlign::Left ? r.x + pad : align == UITextAlign::Right ? r.x + r.w - pad - w : r.x + (r.w - w) * 0.5f;
      font->DrawText(b, x, y, size, col, l);
      y += lh;
    }
  };
  auto text = [&](const Item& i, const std::string& s, UITextAlign align) { textIn(i, i.r, s, align, i.e->textColor); };
  const Item* openDropdown = nullptr;
  for (const Item& i : layout_) {
    const UIElement& e = *i.e;
    const float rad = e.cornerRadius * i.scale, s = i.scale;
    if (i.clipped) b.PushClip(i.clip.x, i.clip.y, i.clip.w, i.clip.h);
    switch (e.kind) {
      case UIKind::Panel:
      case UIKind::Text:
        if (e.color.a > 0) b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        text(i, e.text, e.align);
        break;
      case UIKind::Image: {
        auto it = images_.find(e.image);
        if (it == images_.end()) {
          Image t{};
          int32_t w = 0, h = 0;
          std::vector<uint8_t> rgba;
          const std::filesystem::path p = PathU8(assetsDir) / PathU8(e.image);
          if (!e.image.empty() && LoadImageRGBA(p, w, h, rgba)) {
            TextureDesc d;
            d.w = w;
            d.h = h;
            d.mipmaps = true;
            t = {rhi.CreateTexture(d, rgba.data()), w, h};
          } else if (!e.image.empty()) {
            LogWarn("ui: image {} not found under {}", e.image, assetsDir);
          }
          it = images_.emplace(e.image, t).first;
        }
        const Color tint = e.color.a > 0 ? e.color : Color{1, 1, 1, 1};
        if (it->second.t.Valid() && e.slice > 0) { // 9-slice: corners keep their size
          Rect parts[9];
          float uv[9][4];
          NineSlice(i.r, e.slice * s, e.slice, it->second.w, it->second.h, parts, uv);
          for (int k = 0; k < 9; ++k)
            if (parts[k].w > 0 && parts[k].h > 0) b.Image(parts[k].x, parts[k].y, parts[k].w, parts[k].h, it->second.t, uv[k][0], uv[k][1], uv[k][2], uv[k][3], tint);
        } else if (it->second.t.Valid()) {
          b.Image(i.r.x, i.r.y, i.r.w, i.r.h, it->second.t, 0, 0, 1, 1, tint);
        } else if (e.color.a > 0) {
          b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        }
        break;
      }
      case UIKind::Button: {
        Color c = e.color.a > 0 ? e.color : kButtonFill;
        if (!e.interactable) c.a *= 0.5f;
        else if (e.pressed) c = Shade(c, 0.75f);
        else if (e.hovered) c = Shade(c, 1.35f);
        b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, c, rad, e.hovered ? 2 * s : 0, Color{1, 1, 1, 0.5f});
        text(i, e.text, UITextAlign::Center);
        break;
      }
      case UIKind::ProgressBar: {
        b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color.a > 0 ? e.color : kBarFill, rad);
        const float v = std::clamp(e.value, 0.f, 1.f), inset = std::min(3 * s, i.r.h * 0.2f);
        if (v > 0) b.Rect(i.r.x + inset, i.r.y + inset, (i.r.w - 2 * inset) * v, i.r.h - 2 * inset, e.fillColor, std::max(rad - inset, 0.f));
        text(i, e.text, UITextAlign::Center);
        break;
      }
      case UIKind::Toggle: { // a box (ticked when on) and the label beside it
        if (e.color.a > 0) b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        const float box = std::min(i.r.h - 8 * s, 40 * s), bx = i.r.x + 4 * s, by = i.r.y + (i.r.h - box) * 0.5f;
        const bool on = e.value >= 0.5f;
        b.Rect(bx, by, box, box, on ? e.fillColor : kFieldFill, 4 * s, (e.hovered ? 2.5f : 1.5f) * s, Color{1, 1, 1, e.hovered ? 0.8f : 0.5f});
        if (on) {
          b.Line(bx + box * 0.22f, by + box * 0.52f, bx + box * 0.42f, by + box * 0.72f, 3 * s, Color{1, 1, 1, 1});
          b.Line(bx + box * 0.42f, by + box * 0.72f, bx + box * 0.78f, by + box * 0.3f, 3 * s, Color{1, 1, 1, 1});
        }
        textIn(i, {bx + box + 10 * s, i.r.y, i.r.w - box - 14 * s, i.r.h}, e.text, UITextAlign::Left, e.textColor);
        break;
      }
      case UIKind::Slider: { // a track, its filled part and the knob
        const float v = std::clamp(e.value, 0.f, 1.f), th = std::max(6 * s, i.r.h * 0.2f), ty = i.r.y + (i.r.h - th) * 0.5f;
        const float knob = std::min(i.r.h, 34 * s), kx = i.r.x + knob * 0.5f + (i.r.w - knob) * v;
        b.Rect(i.r.x, ty, i.r.w, th, e.color.a > 0 ? e.color : kBarFill, th * 0.5f);
        b.Rect(i.r.x, ty, kx - i.r.x, th, e.fillColor, th * 0.5f);
        b.Rect(kx - knob * 0.5f, i.r.y + (i.r.h - knob) * 0.5f, knob, knob, e.pressed ? Shade(Color{1, 1, 1, 1}, 0.8f) : Color{1, 1, 1, 1}, knob * 0.5f,
               e.hovered ? 2 * s : 0, kAccent);
        break;
      }
      case UIKind::InputField: { // a field, its text (or the placeholder) and a caret while focused
        b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color.a > 0 ? e.color : kFieldFill, rad, (e.focused ? 2.f : 1.f) * s,
               e.focused ? kAccent : Color{1, 1, 1, e.hovered ? 0.6f : 0.3f});
        const bool empty = e.text.empty();
        Color dim = e.textColor;
        dim.a *= 0.45f;
        b.PushClip(i.r.x + 4 * s, i.r.y, i.r.w - 8 * s, i.r.h);
        textIn(i, i.r, empty ? e.placeholder : e.text, e.align, empty ? dim : e.textColor);
        if (e.focused && font) {
          const float size = e.fontSize * s, pad = std::min(12 * s, i.r.w * 0.1f);
          const float tw = empty ? 0.f : font->Measure(size, e.text);
          const float cx = e.align == UITextAlign::Left ? i.r.x + pad + tw : e.align == UITextAlign::Right ? i.r.x + i.r.w - pad : i.r.x + (i.r.w + tw) * 0.5f;
          b.Rect(cx + 1 * s, i.r.y + i.r.h * 0.22f, 2 * s, i.r.h * 0.56f, e.textColor);
        }
        b.PopClip();
        break;
      }
      case UIKind::ScrollView: { // its background, and a bar when the content is taller than it
        if (e.color.a > 0) b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        const float viewH = i.r.h / std::max(s, 1e-6f);
        if (i.contentH > viewH + 0.5f) {
          const float frac = viewH / i.contentH, bh = std::max(i.r.h * frac, 20 * s);
          const float at = std::clamp(e.scroll.y / std::max(i.contentH - viewH, 1.f), 0.f, 1.f);
          b.Rect(i.r.x + i.r.w - 8 * s, i.r.y + (i.r.h - bh) * at, 5 * s, bh, Color{1, 1, 1, 0.35f}, 2.5f * s);
        }
        break;
      }
      case UIKind::Dropdown: { // the chosen option and a chevron; the list is drawn last, on top
        Color c = e.color.a > 0 ? e.color : kButtonFill;
        if (e.hovered || e.open) c = Shade(c, 1.25f);
        b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, c, rad, e.open ? 2 * s : 0, kAccent);
        const std::vector<std::string> opts = Options(e);
        const int at = (int)std::lround(e.value);
        textIn(i, {i.r.x, i.r.y, i.r.w - 24 * s, i.r.h}, at >= 0 && at < (int)opts.size() ? opts[(size_t)at] : std::string(), UITextAlign::Left, e.textColor);
        const float cx = i.r.x + i.r.w - 18 * s, cy = i.r.y + i.r.h * 0.5f;
        const float tri[4][2] = {{cx - 6 * s, cy - 3 * s}, {cx + 6 * s, cy - 3 * s}, {cx, cy + 4 * s}, {cx, cy + 4 * s}};
        b.Quad(tri, e.textColor);
        if (e.open && i.object == openList_) openDropdown = &i;
        break;
      }
    }
    if (i.clipped) b.PopClip();
  }
  if (openDropdown) { // the open list over everything
    const Item& i = *openDropdown;
    const std::vector<std::string> opts = Options(*i.e);
    const int at = (int)std::lround(i.e->value);
    for (int k = 0; k < (int)opts.size(); ++k) {
      Rect row;
      if (!DropdownRow(i.object, k, row)) continue;
      b.Rect(row.x, row.y, row.w, row.h, k == at ? Shade(kButtonFill, 1.5f) : Color{0.1f, 0.11f, 0.15f, 0.97f}, 0, 1 * i.scale, Color{1, 1, 1, 0.15f});
      textIn(i, row, opts[(size_t)k], UITextAlign::Left, i.e->textColor);
    }
  }
  b.PopClip();
}

void GameUI::Shutdown(RHI& rhi) {
  for (auto& [path, t] : images_)
    if (t.t.Valid()) rhi.DestroyTexture(t.t);
  images_.clear();
}

void GameUI::NineSlice(const Rect& r, float border, float slicePx, int texW, int texH, Rect out[9], float uv[9][4]) {
  const float bx = std::clamp(border, 0.f, r.w * 0.5f), by = std::clamp(border, 0.f, r.h * 0.5f);
  const float ux = texW > 0 ? std::clamp(slicePx / (float)texW, 0.f, 0.5f) : 0.f, vy = texH > 0 ? std::clamp(slicePx / (float)texH, 0.f, 0.5f) : 0.f;
  const float xs[4] = {r.x, r.x + bx, r.x + r.w - bx, r.x + r.w}, ys[4] = {r.y, r.y + by, r.y + r.h - by, r.y + r.h};
  const float us[4] = {0, ux, 1 - ux, 1}, vs[4] = {0, vy, 1 - vy, 1};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) {
      const int k = row * 3 + col;
      out[k] = {xs[col], ys[row], xs[col + 1] - xs[col], ys[row + 1] - ys[row]};
      uv[k][0] = us[col];
      uv[k][1] = vs[row];
      uv[k][2] = us[col + 1];
      uv[k][3] = vs[row + 1];
    }
}

std::vector<GameUI::Placed> GameUI::Placement() const {
  std::vector<Placed> out;
  out.reserve(layout_.size());
  for (const Item& i : layout_) out.push_back({i.object, i.parent, i.canvas, i.r, i.parentRect, i.scale, i.clipped, i.clip, i.laidOut});
  return out;
}

bool GameUI::ElementRect(const std::string& objectId, Rect& out) const {
  for (const Item& i : layout_)
    if (i.object == objectId) { out = i.r; return true; }
  return false;
}

} // namespace bdg
