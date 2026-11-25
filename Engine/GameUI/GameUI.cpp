#include "GameUI/GameUI.h"
#include "GameUI/GameUIComponents.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Render2D/Batch2D.h"
#include "Scene/Scene.h"
#include "Text/Font.h"
#include <algorithm>
#include <filesystem>
#include <string_view>

namespace bdg {
namespace {
Color Shade(Color c, float k) { // k > 1 lighter, < 1 darker
  return {std::clamp(c.r * k, 0.f, 1.f), std::clamp(c.g * k, 0.f, 1.f), std::clamp(c.b * k, 0.f, 1.f), c.a};
}
const Color kButtonFill{0.16f, 0.19f, 0.26f, 0.88f};
const Color kBarFill{0.08f, 0.09f, 0.12f, 0.75f};
} // namespace

void GameUI::Layout(Scene& scene, const Rect& view) {
  layout_.clear();
  struct Canvas { std::vector<Item> items; int order; size_t index; };
  std::vector<Canvas> canvases;
  // children of a canvas (or of an element), placed inside `parent`
  auto place = [&](auto&& self, const SceneObject& o, const Rect& parent, float s, std::vector<Item>& out) -> void {
    for (const std::string& id : o.children) {
      SceneObject* c = scene.Find(id);
      if (!c) continue;
      Component* comp = c->Find("UIElement");
      if (!comp) { self(self, *c, parent, s, out); continue; } // a plain grouping object: same space
      auto* e = static_cast<UIElement*>(comp->data);
      if (!e->visible) continue; // hidden with its children
      Rect r;
      if (e->anchor == UIAnchor::Stretch) {
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
      out.push_back({c->id, e, r, s, 0});
      self(self, *c, r, s, out);
    }
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
      place(place, *o, view, s, c.items);
      canvases.push_back(std::move(c));
      continue; // a canvas inside a canvas is not supported: its children belong to it
    }
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
  }
  std::stable_sort(canvases.begin(), canvases.end(), [](const Canvas& a, const Canvas& b) { return a.order < b.order; });
  for (Canvas& c : canvases)
    for (Item& i : c.items) layout_.push_back(std::move(i));
}

void GameUI::Input(Scene& scene, const Rect& view, float mx, float my, bool down) {
  Layout(scene, view);
  auto inside = [&](const Item& i) { return mx >= i.r.x && my >= i.r.y && mx < i.r.x + i.r.w && my < i.r.y + i.r.h; };
  const Item* top = nullptr; // the topmost button under the mouse
  for (const Item& i : layout_) {
    i.e->hovered = i.e->pressed = false;
    if (i.e->kind == UIKind::Button && i.e->interactable && inside(i)) top = &i;
  }
  if (top) top->e->hovered = true;
  if (down && !wasDown_) pressedOn_ = top ? top->object : std::string();
  if (down && top && top->object == pressedOn_) top->e->pressed = true;
  if (!down && wasDown_) {
    if (top && top->object == pressedOn_) { // released on the button it was pressed on: a click
      ++top->e->clicks;
      ++clicks_;
      LogInfo("ui: {} clicked", scene.Find(top->object) ? scene.Find(top->object)->name : top->object);
    }
    pressedOn_.clear();
  }
  wasDown_ = down;
}

void GameUI::Draw(Scene& scene, Batch2D& b, Font* font, RHI& rhi, const Rect& view, const std::string& assetsDir) {
  Layout(scene, view);
  if (layout_.empty()) return;
  b.PushClip(view.x, view.y, view.w, view.h);
  auto text = [&](const Item& i, const std::string& s, UITextAlign align) {
    if (!font || s.empty()) return;
    const float size = i.e->fontSize * i.scale, lh = font->LineHeight(size);
    std::vector<std::string_view> lines;
    for (size_t a = 0; a <= s.size();) {
      size_t n = s.find('\n', a);
      if (n == std::string::npos) n = s.size();
      lines.emplace_back(s.data() + a, n - a);
      a = n + 1;
    }
    float y = i.r.y + (i.r.h - lh * (float)lines.size()) * 0.5f;
    const float pad = align == UITextAlign::Center ? 0 : std::min(12 * i.scale, i.r.w * 0.1f);
    for (std::string_view l : lines) {
      const float w = font->Measure(size, l);
      const float x = align == UITextAlign::Left ? i.r.x + pad : align == UITextAlign::Right ? i.r.x + i.r.w - pad - w : i.r.x + (i.r.w - w) * 0.5f;
      font->DrawText(b, x, y, size, i.e->textColor, l);
      y += lh;
    }
  };
  for (const Item& i : layout_) {
    const UIElement& e = *i.e;
    const float rad = e.cornerRadius * i.scale;
    switch (e.kind) {
      case UIKind::Panel:
        if (e.color.a > 0) b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        text(i, e.text, e.align);
        break;
      case UIKind::Text:
        if (e.color.a > 0) b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        text(i, e.text, e.align);
        break;
      case UIKind::Image: {
        auto it = images_.find(e.image);
        if (it == images_.end()) {
          TextureHandle t{};
          int32_t w = 0, h = 0;
          std::vector<uint8_t> rgba;
          const std::filesystem::path p = PathU8(assetsDir) / PathU8(e.image);
          if (!e.image.empty() && LoadImageRGBA(p, w, h, rgba)) {
            TextureDesc d;
            d.w = w;
            d.h = h;
            d.mipmaps = true;
            t = rhi.CreateTexture(d, rgba.data());
          } else if (!e.image.empty()) {
            LogWarn("ui: image {} not found under {}", e.image, assetsDir);
          }
          it = images_.emplace(e.image, t).first;
        }
        if (it->second.Valid()) b.Image(i.r.x, i.r.y, i.r.w, i.r.h, it->second, 0, 0, 1, 1, e.color.a > 0 ? e.color : Color{1, 1, 1, 1});
        else if (e.color.a > 0) b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color, rad);
        break;
      }
      case UIKind::Button: {
        Color c = e.color.a > 0 ? e.color : kButtonFill;
        if (!e.interactable) c.a *= 0.5f;
        else if (e.pressed) c = Shade(c, 0.75f);
        else if (e.hovered) c = Shade(c, 1.35f);
        b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, c, rad, e.hovered ? 2 * i.scale : 0, Color{1, 1, 1, 0.5f});
        text(i, e.text, UITextAlign::Center);
        break;
      }
      case UIKind::ProgressBar: {
        b.Rect(i.r.x, i.r.y, i.r.w, i.r.h, e.color.a > 0 ? e.color : kBarFill, rad);
        const float v = std::clamp(e.value, 0.f, 1.f), inset = std::min(3 * i.scale, i.r.h * 0.2f);
        if (v > 0) b.Rect(i.r.x + inset, i.r.y + inset, (i.r.w - 2 * inset) * v, i.r.h - 2 * inset, e.fillColor, std::max(rad - inset, 0.f));
        text(i, e.text, UITextAlign::Center);
        break;
      }
    }
  }
  b.PopClip();
}

void GameUI::Shutdown(RHI& rhi) {
  for (auto& [path, t] : images_)
    if (t.Valid()) rhi.DestroyTexture(t);
  images_.clear();
}

bool GameUI::ElementRect(const std::string& objectId, Rect& out) const {
  for (const Item& i : layout_)
    if (i.object == objectId) { out = i.r; return true; }
  return false;
}

} // namespace bdg
