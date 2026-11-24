#include "UI/Dock.h"
#include "Core/Utf8Path.h"
#include "Core/Log.h"
#include "Render2D/Batch2D.h"
#include "UI/Theme.h"
#include <yyjson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>

namespace bdg {
// ---- Tree editing ----------------------------------------------------------------------------
int DockState::AddLeaf(std::vector<std::string> tabs, int sel) {
  DockNode n;
  n.tabs = std::move(tabs);
  n.sel = std::clamp(sel, 0, std::max(0, (int)n.tabs.size() - 1));
  nodes.push_back(std::move(n));
  if (root < 0) root = (int)nodes.size() - 1;
  return (int)nodes.size() - 1;
}

int DockState::AddSplit(Axis axis, float ratio, int a, int b) {
  DockNode n;
  n.split = true; n.axis = axis; n.ratio = ratio; n.a = a; n.b = b;
  nodes.push_back(std::move(n));
  int id = (int)nodes.size() - 1;
  nodes[a].parent = nodes[b].parent = id;
  if (root == a || root == b || root < 0) root = id;
  return id;
}

int DockState::FindLeaf(const std::string& panel) const {
  for (int i = 0; i < (int)nodes.size(); ++i)
    if (nodes[i].alive && !nodes[i].split && std::find(nodes[i].tabs.begin(), nodes[i].tabs.end(), panel) != nodes[i].tabs.end())
      return i;
  return -1;
}

bool DockState::SelectPanel(const std::string& panel) {
  int leaf = FindLeaf(panel);
  if (leaf < 0) return false;
  auto& t = nodes[leaf].tabs;
  nodes[leaf].sel = (int)(std::find(t.begin(), t.end(), panel) - t.begin());
  return true;
}

void DockState::Replace(int oldNode, int newNode) {
  int p = nodes[oldNode].parent;
  nodes[newNode].parent = p;
  if (p < 0) { root = newNode; return; }
  (nodes[p].a == oldNode ? nodes[p].a : nodes[p].b) = newNode;
}

void DockState::MovePanel(const std::string& panel, int target, DockZone zone) {
  int src = FindLeaf(panel);
  if (src < 0 || target < 0 || zone == DockZone::None || !nodes[target].alive || nodes[target].split) return;
  if (src == target && (zone == DockZone::Center || nodes[src].tabs.size() == 1)) return; // nothing changes
  // Take the tab out of its leaf; an emptied leaf is removed and its sibling takes the parent's place.
  auto& tabs = nodes[src].tabs;
  int removed = (int)(std::find(tabs.begin(), tabs.end(), panel) - tabs.begin());
  tabs.erase(tabs.begin() + removed);
  int& sel = nodes[src].sel; // the neighbour on the left takes over a removed selected tab
  if (sel > removed || (sel == removed && sel > 0)) --sel;
  sel = std::clamp(sel, 0, std::max(0, (int)tabs.size() - 1));
  if (tabs.empty()) {
    int p = nodes[src].parent;
    nodes[src].alive = false;
    if (p >= 0) {
      int sibling = nodes[p].a == src ? nodes[p].b : nodes[p].a;
      Replace(p, sibling);
      nodes[p].alive = false;
    }
  }
  if (zone == DockZone::Center) {
    nodes[target].tabs.push_back(panel);
    nodes[target].sel = (int)nodes[target].tabs.size() - 1;
    return;
  }
  // Split the target leaf: the moved panel gets a third of it on the chosen side.
  int leaf = AddLeaf({panel});
  bool first = zone == DockZone::Left || zone == DockZone::Top;
  DockNode s;
  s.split = true;
  s.axis = (zone == DockZone::Left || zone == DockZone::Right) ? Axis::Row : Axis::Column;
  s.ratio = first ? 0.33f : 0.67f;
  s.a = first ? leaf : target;
  s.b = first ? target : leaf;
  nodes.push_back(std::move(s));
  int split = (int)nodes.size() - 1;
  Replace(target, split);
  nodes[target].parent = nodes[leaf].parent = split;
}

DockRect DockState::ZoneRect(int leaf, DockZone zone) const {
  DockRect r = nodes[leaf].rect;
  switch (zone) {
    case DockZone::Left: r.w *= 0.5f; break;
    case DockZone::Right: r.x += r.w * 0.5f; r.w *= 0.5f; break;
    case DockZone::Top: r.h *= 0.5f; break;
    case DockZone::Bottom: r.y += r.h * 0.5f; r.h *= 0.5f; break;
    default: break;
  }
  return r;
}

std::string DockState::DescribeNode(int n) const {
  const DockNode& d = nodes[n];
  if (d.split)
    return std::format("{}({:.2f}: {} | {})", d.axis == Axis::Row ? "row" : "col", d.ratio, DescribeNode(d.a), DescribeNode(d.b));
  std::string s = "[";
  for (size_t i = 0; i < d.tabs.size(); ++i) s += (i ? " " : "") + d.tabs[i] + ((int)i == d.sel ? "*" : "");
  return s + "]";
}

std::string DockState::Describe() const { return root >= 0 ? DescribeNode(root) : "(empty)"; }

// ---- Save / load: ProjectSettings-style JSONL ----------------------------------------------
// {"id":"n1","split":"row","ratio":0.2,"a":"n2","b":"n3"}  /  {"id":"n2","tabs":["hierarchy"],"sel":0}
bool DockState::Save(const std::string& path) const {
  if (root < 0) return false;
  std::error_code ec;
  std::filesystem::create_directories(PathU8(path).parent_path(), ec);
  std::FILE* f = OpenU8(path, "w");
  if (!f) return false;
  std::unordered_map<int, int> ids; // node index -> 1-based id in preorder (stable output)
  std::vector<int> order, stack{root};
  while (!stack.empty()) {
    int n = stack.back(); stack.pop_back();
    ids[n] = (int)order.size() + 1;
    order.push_back(n);
    if (nodes[n].split) { stack.push_back(nodes[n].b); stack.push_back(nodes[n].a); }
  }
  for (int n : order) {
    const DockNode& d = nodes[n];
    if (d.split) {
      std::fprintf(f, "{\"id\":\"n%d\",\"split\":\"%s\",\"ratio\":%.4f,\"a\":\"n%d\",\"b\":\"n%d\"}\n", ids[n],
                   d.axis == Axis::Row ? "row" : "col", d.ratio, ids[d.a], ids[d.b]);
    } else {
      std::string tabs;
      for (size_t i = 0; i < d.tabs.size(); ++i) tabs += (i ? ",\"" : "\"") + d.tabs[i] + "\"";
      std::fprintf(f, "{\"id\":\"n%d\",\"tabs\":[%s],\"sel\":%d}\n", ids[n], tabs.c_str(), d.sel);
    }
  }
  std::fclose(f);
  return true;
}

bool DockState::Load(const std::string& path) {
  std::FILE* f = OpenU8(path, "rb");
  if (!f) return false;
  struct Line { std::string id, a, b; bool split = false; Axis axis = Axis::Row; float ratio = 0.5f; std::vector<std::string> tabs; int sel = 0; };
  std::vector<Line> lines;
  char buf[4096];
  bool ok = true;
  while (ok && std::fgets(buf, sizeof buf, f)) {
    if (buf[0] == '\n' || buf[0] == '\r' || buf[0] == 0) continue;
    yyjson_doc* doc = yyjson_read(buf, std::strlen(buf), 0);
    yyjson_val* o = doc ? yyjson_doc_get_root(doc) : nullptr;
    const char* id = yyjson_get_str(yyjson_obj_get(o, "id"));
    if (!o || !id) { ok = false; yyjson_doc_free(doc); break; }
    Line l;
    l.id = id;
    if (const char* sp = yyjson_get_str(yyjson_obj_get(o, "split"))) {
      l.split = true;
      l.axis = std::string(sp) == "col" ? Axis::Column : Axis::Row;
      l.ratio = (float)yyjson_get_num(yyjson_obj_get(o, "ratio"));
      const char* a = yyjson_get_str(yyjson_obj_get(o, "a"));
      const char* b = yyjson_get_str(yyjson_obj_get(o, "b"));
      if (!a || !b || !(l.ratio > 0.f && l.ratio < 1.f)) ok = false;
      else { l.a = a; l.b = b; }
    } else {
      size_t i, n;
      yyjson_val* t;
      yyjson_arr_foreach(yyjson_obj_get(o, "tabs"), i, n, t) if (const char* s = yyjson_get_str(t)) l.tabs.push_back(s);
      l.sel = (int)yyjson_get_int(yyjson_obj_get(o, "sel"));
    }
    yyjson_doc_free(doc);
    lines.push_back(std::move(l));
  }
  std::fclose(f);
  if (!ok || lines.empty()) return false;

  // Build into a fresh state; keep only panels this editor knows, each once.
  DockState next;
  next.titles = titles;
  std::unordered_map<std::string, int> index;
  std::unordered_map<std::string, bool> seen;
  for (auto& l : lines) {
    DockNode n;
    n.split = l.split; n.axis = l.axis; n.ratio = l.ratio;
    for (auto& t : l.tabs)
      if (titles.count(t) && !seen[t]) { n.tabs.push_back(t); seen[t] = true; }
    n.sel = std::clamp(l.sel, 0, std::max(0, (int)n.tabs.size() - 1));
    index[l.id] = (int)next.nodes.size();
    next.nodes.push_back(std::move(n));
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!lines[i].split) continue;
    if (!index.count(lines[i].a) || !index.count(lines[i].b)) return false;
    DockNode& n = next.nodes[i];
    n.a = index[lines[i].a]; n.b = index[lines[i].b];
    if (n.a == (int)i || n.b == (int)i || next.nodes[n.a].parent >= 0 || next.nodes[n.b].parent >= 0) return false;
    next.nodes[n.a].parent = next.nodes[n.b].parent = (int)i;
  }
  next.root = 0;
  if (next.nodes[0].parent >= 0) return false;
  // Panels added since the layout was saved go into the first leaf; empty leaves are removed.
  int firstLeaf = -1;
  for (int i = 0; i < (int)next.nodes.size(); ++i) if (!next.nodes[i].split) { firstLeaf = i; break; }
  if (firstLeaf < 0) return false;
  for (auto& [id, title] : titles) if (!seen[id]) next.nodes[firstLeaf].tabs.push_back(id);
  for (int i = 0; i < (int)next.nodes.size(); ++i) {
    DockNode& n = next.nodes[i];
    if (n.split || !n.tabs.empty() || n.parent < 0) continue;
    int p = n.parent, sibling = next.nodes[p].a == i ? next.nodes[p].b : next.nodes[p].a;
    n.alive = false;
    next.Replace(p, sibling);
    next.nodes[p].alive = false;
  }
  next.layoutPath = layoutPath;
  *this = std::move(next);
  return true;
}

std::unique_ptr<Widget> MakeDock(std::string id, std::shared_ptr<DockState> state) {
  auto w = std::make_unique<Widget>();
  w->kind = WidgetKind::Dock;
  w->id = std::move(id);
  w->dock = std::move(state);
  return w;
}

// ---- Layout / drawing / input (UIContext members) -------------------------------------------
namespace {
void LayoutNode(DockState& d, int n, DockRect r) {
  DockNode& node = d.nodes[n];
  node.rect = r;
  if (!node.split) return;
  bool row = node.axis == Axis::Row;
  float avail = std::max(0.f, (row ? r.w : r.h) - DockState::kGap);
  float sa = std::round(avail * node.ratio);
  DockRect ra = r, rb = r;
  if (row) { ra.w = sa; rb.x = r.x + sa + DockState::kGap; rb.w = avail - sa; }
  else { ra.h = sa; rb.y = r.y + sa + DockState::kGap; rb.h = avail - sa; }
  LayoutNode(d, node.a, ra);
  LayoutNode(d, node.b, rb);
}

// Splitter grab area of a split node: the gap between a and b, widened for the mouse.
DockRect SplitterRect(const DockState& d, int n) {
  const DockNode& node = d.nodes[n];
  const DockRect& ra = d.nodes[node.a].rect;
  DockRect r = node.rect;
  if (node.axis == Axis::Row) { r.x = ra.x + ra.w - DockState::kGrab * 0.5f + DockState::kGap * 0.5f; r.w = DockState::kGrab; }
  else { r.y = ra.y + ra.h - DockState::kGrab * 0.5f + DockState::kGap * 0.5f; r.h = DockState::kGrab; }
  return r;
}

int SplitAt(const DockState& d, float x, float y) {
  for (int i = 0; i < (int)d.nodes.size(); ++i)
    if (d.nodes[i].alive && d.nodes[i].split && SplitterRect(d, i).Contains(x, y)) return i;
  return -1;
}

int LeafAt(const DockState& d, float x, float y) {
  for (int i = 0; i < (int)d.nodes.size(); ++i)
    if (d.nodes[i].alive && !d.nodes[i].split && d.nodes[i].rect.Contains(x, y)) return i;
  return -1;
}
}

void UIContext::ArrangeDock(Widget& w) {
  DockState& d = *w.dock;
  docks_.push_back(&w);
  if (d.root < 0) return;
  LayoutNode(d, d.root, {w.x, w.y, w.w, w.h});
  const float head = theme_.N("size-panel-header", 30), fs = theme_.FontSize("label", 12), s6 = theme_.N("space-6", 12);
  d.tabRects.clear();
  d.bodyRects.clear();
  for (auto& n : d.nodes) {
    if (!n.alive || n.split) continue;
    float x = n.rect.x + 1;
    for (auto& t : n.tabs) {
      auto it = d.titles.find(t);
      float tw = Measure(fs, it != d.titles.end() ? it->second : t) + s6 * 2;
      d.tabRects[t] = {x, n.rect.y + 1, tw, head - 1};
      x += tw;
    }
  }
  for (auto& c : w.children) {
    int leaf = d.FindLeaf(c->id);
    bool shown = leaf >= 0 && d.nodes[leaf].tabs[d.nodes[leaf].sel] == c->id;
    c->hidden = !shown;
    if (!shown) continue;
    const DockRect& r = d.nodes[leaf].rect;
    float bw = std::max(0.f, r.w - 2), bh = std::max(0.f, r.h - head - 1);
    d.bodyRects[c->id] = {r.x + 1, r.y + head, bw, bh};
    float contentH = std::max(bh, c->prefH);            // taller content scrolls inside the body
    c->scrollY = std::clamp(c->scrollY, 0.f, contentH - bh);
    Arrange(*c, r.x + 1, r.y + head - c->scrollY, bw, contentH);
  }
}

void UIContext::DrawDock(Widget& w) {
  const DockState& d = *w.dock;
  const Theme& t = theme_;
  Batch2D& b = batch_;
  const float head = t.N("size-panel-header", 30), fs = t.FontSize("label", 12), s6 = t.N("space-6", 12);
  for (int i = 0; i < (int)d.nodes.size(); ++i) {
    const DockNode& n = d.nodes[i];
    if (!n.alive) continue;
    if (n.split) {
      if (i == d.hoverSplit || i == d.dragSplit) { // highlight the splitter being dragged / hovered
        DockRect s = SplitterRect(d, i);
        if (n.axis == Axis::Row) b.Rect(s.x + s.w * 0.5f - 1, s.y, 2, s.h, t.C("accent"));
        else b.Rect(s.x, s.y + s.h * 0.5f - 1, s.w, 2, t.C("accent"));
      }
      continue;
    }
    const DockRect& r = n.rect;
    b.Rect(r.x, r.y, r.w, r.h, t.C("panel"), t.N("radius-sm", 3), 1, t.C("line"));
    b.Rect(r.x + 1, r.y + 1, r.w - 2, head - 1, t.C("win"));
    b.Rect(r.x + 1, r.y + head - 1, r.w - 2, 1, t.C("line"));
    for (int k = 0; k < (int)n.tabs.size(); ++k) {
      auto rt = d.tabRects.find(n.tabs[k]);
      if (rt == d.tabRects.end()) continue;
      const DockRect& tr = rt->second;
      bool on = k == n.sel;
      if (on) { b.Rect(tr.x, tr.y, tr.w, tr.h, t.C("panel")); b.Rect(tr.x, tr.y, tr.w, 2, t.C("accent")); }
      auto title = d.titles.find(n.tabs[k]);
      Text(tr.x + s6, tr.y + (tr.h - fs) * 0.5f - 1, fs, on ? t.C("fg") : t.C("fg-dim"),
           title != d.titles.end() ? title->second : n.tabs[k]);
    }
  }
}

void UIContext::DrawDockOverlay(Widget& w) {
  const DockState& d = *w.dock;
  if (!d.tabDragging) return;
  const Theme& t = theme_;
  if (d.dropLeaf >= 0 && d.dropZone != DockZone::None) { // where the tab will land
    DockRect z = d.ZoneRect(d.dropLeaf, d.dropZone);
    Color soft = t.C("accent");
    soft.a = 0.18f;
    batch_.Rect(z.x + 2, z.y + 2, z.w - 4, z.h - 4, soft, t.N("radius-md", 4), 2, t.C("accent"));
  }
  auto title = d.titles.find(d.pressTab); // ghost tab under the mouse
  std::string label = title != d.titles.end() ? title->second : d.pressTab;
  float fs = t.FontSize("label", 12), tw = Measure(fs, label) + 24;
  batch_.Rect(d.mouseX - tw * 0.5f, d.mouseY - 14, tw, 26, t.C("raise"), t.N("radius-sm", 3), 1, t.C("accent"));
  Text(d.mouseX - tw * 0.5f + 12, d.mouseY - 14 + (26 - fs) * 0.5f - 1, fs, t.C("fg"), label);
}

bool UIContext::DockMouse(const InputEvent& e) {
  for (Widget* w : docks_) {
    DockState& d = *w->dock;
    if (w->hidden || d.root < 0) continue;
    bool inside = e.x >= w->x && e.y >= w->y && e.x < w->x + w->w && e.y < w->y + w->h;
    auto save = [&] {
      if (!d.layoutPath.empty() && d.Save(d.layoutPath)) LogInfo("dock: layout saved {}", d.layoutPath);
    };
    switch (e.type) {
      case InputEvent::Type::MouseMove: {
        d.mouseX = e.x; d.mouseY = e.y;
        if (d.dragSplit >= 0) {
          DockNode& n = d.nodes[d.dragSplit];
          bool row = n.axis == Axis::Row;
          float size = (row ? n.rect.w : n.rect.h) - DockState::kGap, pos = (row ? e.x - n.rect.x : e.y - n.rect.y);
          float lo = std::min(0.5f, DockState::kMinPane / std::max(size, 1.f));
          n.ratio = std::clamp(pos / std::max(size, 1.f), lo, 1.f - lo);
          return true;
        }
        if (!d.pressTab.empty()) {
          if (!d.tabDragging && std::hypot(e.x - d.pressX, e.y - d.pressY) > 6.f) d.tabDragging = true;
          if (d.tabDragging) {
            d.dropLeaf = LeafAt(d, e.x, e.y);
            d.dropZone = DockZone::None;
            if (d.dropLeaf >= 0) {
              const DockRect& r = d.nodes[d.dropLeaf].rect;
              float fx = (e.x - r.x) / r.w, fy = (e.y - r.y) / r.h;
              bool header = e.y < r.y + theme_.N("size-panel-header", 30);
              d.dropZone = header ? DockZone::Center : fx < 0.25f ? DockZone::Left : fx > 0.75f ? DockZone::Right
                         : fy < 0.25f ? DockZone::Top : fy > 0.75f ? DockZone::Bottom : DockZone::Center;
            }
          }
          return true;
        }
        int hover = inside ? SplitAt(d, e.x, e.y) : -1;
        if (hover != d.hoverSplit && window_)
          window_->SetCursor(hover < 0 ? Window::Cursor::Default
                             : d.nodes[hover].axis == Axis::Row ? Window::Cursor::ResizeH : Window::Cursor::ResizeV);
        d.hoverSplit = hover;
        if (hover >= 0) return true;
        break;
      }
      case InputEvent::Type::MouseDown: {
        if (e.button != 1 || !inside) break;
        if (int s = SplitAt(d, e.x, e.y); s >= 0) {
          d.dragSplit = s;
          if (window_) window_->SetMouseCapture(true); // keep tracking outside the window
          return true;
        }
        for (auto& [panel, r] : d.tabRects)
          if (r.Contains(e.x, e.y)) {
            d.SelectPanel(panel);
            d.pressTab = panel; d.pressX = e.x; d.pressY = e.y; d.tabDragging = false;
            if (window_) window_->SetMouseCapture(true);
            return true;
          }
        int leaf = LeafAt(d, e.x, e.y); // header background: swallow so nothing behind reacts
        if (leaf >= 0 && e.y < d.nodes[leaf].rect.y + theme_.N("size-panel-header", 30)) return true;
        break;
      }
      case InputEvent::Type::MouseUp: {
        if (e.button != 1) break;
        if (d.dragSplit >= 0) {
          d.dragSplit = -1;
          if (window_) window_->SetMouseCapture(false);
          save();
          return true;
        }
        if (!d.pressTab.empty()) {
          if (d.tabDragging && d.dropLeaf >= 0 && d.dropZone != DockZone::None) {
            std::string before = d.Describe();
            d.MovePanel(d.pressTab, d.dropLeaf, d.dropZone);
            if (d.Describe() != before) save();
          }
          d.pressTab.clear();
          d.tabDragging = false;
          d.dropLeaf = -1;
          d.dropZone = DockZone::None;
          if (window_) window_->SetMouseCapture(false);
          return true;
        }
        break;
      }
      default: break;
    }
  }
  return false;
}
}
