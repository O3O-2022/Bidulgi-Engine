// Scene view toolbar (round 7, Unreal-like): one row over the picture.
//   Left:  view (Perspective / Top / ...), quality badge, view mode, Show (wireframe, bounds, stats, icons, budget,
//          grid), then Bake GI / Align / other tools when they apply.
//   Right: Move / Rotate / Scale, World / Local, surface snapping, grid / rotation / scale snapping (a toggle and
//          its value each), camera speed (1-8 like Unreal, x a scalar; a popup sets both).
// It never wraps: the left group's labels shorten first, then controls go into a "»" menu. The values are kept per
// project (toolbarStatePath, JSON). Every control has a tooltip with its shortcut.
#include "Runtime/SceneView.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Render2D/Batch2D.h"
#include "Runtime/EditorModel.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <yyjson.h>

namespace bdg {
namespace {
// Camera speed levels (m/s), Unreal's 1..8 mapped onto this editor's metres; level 4 = the old default 5 m/s.
constexpr float kSpeedLevels[8] = {0.5f, 1.f, 2.5f, 5.f, 10.f, 20.f, 40.f, 80.f};
constexpr float kSpeedScalars[] = {0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};
constexpr float kRowH = 22, kGap = 6, kIconW = 26;
Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

std::string Num(float v) { // 0.25, 5, 0.0625: no trailing zeros
  std::string s = std::format("{:.4f}", v);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}
}

float SceneView::SpeedOfLevel(int level) { return kSpeedLevels[std::clamp(level, 1, 8) - 1]; }

void SceneView::SetSpeedLevel(int level) {
  speedLevel = std::clamp(level, 1, 8);
  flySpeed_ = std::clamp(SpeedOfLevel(speedLevel) * speedScalar, 0.05f, 1000.f);
  bookmarkNote_ = std::format("카메라 속도 {}  ·  {:.2g} m/s", speedLevel, flySpeed_);
  noteFrames_ = 90;
}

void SceneView::SetSpeedScalar(float s) {
  speedScalar = std::clamp(s, 0.05f, 16.f);
  SetSpeedLevel(speedLevel);
}

std::string SceneView::ToolbarTooltip(const std::string& name) const {
  if (name == "persp") return "보기 방향: 원근 / 위 / 앞 / 옆";
  if (name == "tier") return "품질 단계 (에디터 미리보기)";
  if (name == "view") return "뷰 모드 (Alt+4 Lit · Alt+3 Unlit · Alt+2 Wireframe)";
  if (name == "show") return "표시: 와이어프레임, 경계 상자, 통계, 아이콘, 예산, 격자";
  if (name == "gi") return "GI 굽기 (한 번 더 튐)";
  if (name == "align") return "정렬 / 분배 (두 개 이상 선택)";
  if (name == "tool") return "다른 도구: 선택 Q · 측정 M · 블록아웃 B · 지형 T";
  if (name == "gz.move") return "이동 (W)";
  if (name == "gz.rotate") return "회전 (E)";
  if (name == "gz.scale") return "크기 (R)";
  if (name == "space") return localSpace ? "로컬 축 — 월드 축으로 (X)" : "월드 축 — 로컬 축으로 (X)";
  if (name == "surf") return "표면 스냅: 이동하면 마우스 아래 표면에 놓임 (끄면 Ctrl+Shift 드래그)";
  if (name == "snap") return "격자 스냅 켜기/끄기 (드래그 중 Ctrl은 반대로)";
  if (name == "snapv") return "격자 간격 (m)";
  if (name == "rsnap") return "회전 스냅 켜기/끄기 (드래그 중 Ctrl은 반대로)";
  if (name == "rsnapv") return "회전 스냅 각도";
  if (name == "ssnap") return "크기 스냅 켜기/끄기 (드래그 중 Ctrl은 반대로)";
  if (name == "ssnapv") return "크기 스냅 간격";
  if (name == "speed") return "카메라 속도 1~8 × 배율 (우클릭 비행 중 휠로 바꿈)";
  if (name == "more") return "더 보기 (좁아서 숨긴 도구)";
  return "";
}

// ---- persistence (per project) ----
std::string SceneView::ToolbarStateJson() const {
  return std::format(R"({{"snap":{{"grid":{},"gridOn":{},"rotate":{},"rotateOn":{},"scale":{},"scaleOn":{},"surface":{}}},)"
                     R"("speedLevel":{},"speedScalar":{},"local":{},"wireframe":{},"bounds":{},"stats":{},"icons":{},"grid":{}}})",
                     Num(snap.move), snap.always, Num(snap.rotate), snap.rotateOn, Num(snap.scale), snap.scaleOn, snap.surface, speedLevel,
                     Num(speedScalar), localSpace, wireframe, bounds, statsPanel, iconSize, showGrid);
}

bool SceneView::LoadToolbarState() {
  savedToolbar_ = ToolbarStateJson(); // nothing is written until a value changes
  if (toolbarStatePath.empty()) return false;
  std::ifstream f(PathU8(toolbarStatePath));
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
  if (!doc) {
    LogWarn("scene view: toolbar settings {} are not JSON; defaults used", toolbarStatePath);
    return false;
  }
  yyjson_val* root = yyjson_doc_get_root(doc);
  auto num = [](yyjson_val* o, const char* k, float def) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_num(v) ? (float)yyjson_get_num(v) : def; };
  auto flag = [](yyjson_val* o, const char* k, bool def) { yyjson_val* v = yyjson_obj_get(o, k); return v && yyjson_is_bool(v) ? yyjson_get_bool(v) : def; };
  if (yyjson_val* s = yyjson_obj_get(root, "snap")) {
    snap.move = std::clamp(num(s, "grid", snap.move), 0.001f, 1000.f);
    snap.always = flag(s, "gridOn", snap.always);
    snap.rotate = std::clamp(num(s, "rotate", snap.rotate), 0.1f, 180.f);
    snap.rotateOn = flag(s, "rotateOn", snap.rotateOn);
    snap.scale = std::clamp(num(s, "scale", snap.scale), 0.001f, 10.f);
    snap.scaleOn = flag(s, "scaleOn", snap.scaleOn);
    snap.surface = flag(s, "surface", snap.surface);
  }
  speedScalar = std::clamp(num(root, "speedScalar", speedScalar), 0.05f, 16.f);
  speedLevel = std::clamp((int)num(root, "speedLevel", (float)speedLevel), 1, 8);
  flySpeed_ = SpeedOfLevel(speedLevel) * speedScalar;
  localSpace = flag(root, "local", localSpace);
  wireframe = flag(root, "wireframe", wireframe);
  bounds = flag(root, "bounds", bounds);
  statsPanel = flag(root, "stats", statsPanel);
  iconSize = std::clamp((int)num(root, "icons", (float)iconSize), 0, 3);
  showGrid = flag(root, "grid", showGrid);
  yyjson_doc_free(doc);
  savedToolbar_ = ToolbarStateJson();
  return true;
}

void SceneView::SaveToolbarStateIfChanged() {
  if (toolbarStatePath.empty()) return;
  const std::string now = ToolbarStateJson();
  if (now == savedToolbar_) return;
  std::error_code ec;
  const std::filesystem::path p = PathU8(toolbarStatePath);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return;
  f << now << "\n";
  savedToolbar_ = now;
}

// ---- menus ----
void SceneView::OpenChipMenu(const Chip& chip, EditorModel& model) {
  if (!ui_) return;
  std::vector<UIContext::MenuItem> items;
  if (chip.name == "view") {
    // Unreal's grouping: the modes, then buffer visualisation, optimisation and debug views (titles are disabled rows)
    static const char* groups[] = {"", "- Buffer Visualization -", "- Optimization -", "- Debug -"};
    for (int g = 0; g < 4; ++g) {
      if (g > 0) items.push_back({groups[g], nullptr, false, false});
      for (int k = 0; k < (int)DebugView::Count; ++k) {
        if (DebugViewGroup((DebugView)k) != g) continue;
        const char* key = DebugViewKey((DebugView)k);
        items.push_back({std::string(g > 0 ? "   " : "") + DebugViewName((DebugView)k) + (*key ? std::string("   ") + key : std::string()),
                         [this, k] { debugView = (DebugView)k; }, true, (int)debugView == k});
      }
    }
  } else if (chip.name == "persp") {
    static const struct { const char* name; float yaw, pitch; } views[] = {
        {"Perspective", 35, -24}, {"Top", 0, -89.5f}, {"Bottom", 0, 89.5f}, {"Front", 0, 0}, {"Back", 180, 0}, {"Left", 90, 0}, {"Right", -90, 0}};
    for (auto& v : views)
      items.push_back({v.name, [this, v] {
                         camera.yaw = v.yaw;
                         camera.pitch = v.pitch;
                         viewPreset_ = v.name;
                       }, true, viewPreset_ == v.name});
  } else if (chip.name == "show") {
    items.push_back({"Wireframe", [this] { wireframe = !wireframe; }, true, wireframe});
    items.push_back({"Bounds", [this] { bounds = !bounds; }, true, bounds});
    items.push_back({"Stats", [this] { statsPanel = !statsPanel; }, true, statsPanel});
    items.push_back({BudgetOver() ? "Budget ⚠" : "Budget", [this] { budgetPanel = !budgetPanel; }, true, budgetPanel});
    items.push_back({"Grid", [this] { showGrid = !showGrid; }, true, showGrid});
    static const char* names[] = {"Icons: Off", "Icons: S", "Icons: M", "Icons: L"};
    for (int k = 0; k < 4; ++k) items.push_back({names[k], [this, k] { iconSize = k; }, true, iconSize == k});
  } else if (chip.name == "tool") {
    static const struct { const char* id; const char* label; } tools[] = {
        {"select", "Select (Q)"}, {"move", "Move (W)"}, {"rotate", "Rotate (E)"}, {"scale", "Scale (R)"},
        {"measure", "Measure (M)"}, {"blockout", "Blockout (B)"}, {"terrain", "Terrain (T)"}, {"view", "View (pan)"}};
    EditorModel* m = &model;
    for (auto& tl : tools)
      items.push_back({tl.label, [m, id = std::string(tl.id)] { m->tool = id; LogInfo("tool: {}", id); }, true, model.tool == tl.id});
  } else if (chip.name == "tier") {
    for (int k = (int)QualityTier::Potato; k <= (int)QualityTier::Ultra; ++k)
      items.push_back({TierLabel((QualityTier)k), [this, k] { quality = QualityPreset((QualityTier)k); }, true, (int)quality.tier == k});
  } else if (chip.name == "snapv" || chip.name == "snap") {
    for (float step : kSnapSteps)
      items.push_back({Num(step) + " m", [this, step] { snap.move = step; snap.always = true; }, true, snap.move == step});
  } else if (chip.name == "rsnapv" || chip.name == "rsnap") {
    for (float step : kRotateSnapSteps)
      items.push_back({Num(step) + "°", [this, step] { snap.rotate = step; snap.rotateOn = true; }, true, snap.rotate == step});
  } else if (chip.name == "ssnapv" || chip.name == "ssnap") {
    for (float step : kScaleSnapSteps)
      items.push_back({Num(step), [this, step] { snap.scale = step; snap.scaleOn = true; }, true, snap.scale == step});
  } else if (chip.name == "more") { // what did not fit: each opens or toggles as its own control would
    for (const std::string& name : overflow_) {
      const std::string label = ToolbarLabel(name, false);
      items.push_back({label.empty() ? ToolbarTooltip(name) : label, [this, name, &model] {
                         const Chip* c = nullptr;
                         for (const Chip& k : chips_)
                           if (k.name == "more") c = &k;
                         if (c) ToolbarClick(Chip{name, c->x, c->y, c->w, c->h}, model, true);
                       }, true, ToolbarOn(name)});
    }
  }
  if (!items.empty()) ui_->OpenMenu(chip.x, chip.y + chip.h + 3, std::move(items));
}

bool SceneView::ToolbarOn(const std::string& name) const {
  const std::string tool = toolModel_ ? toolModel_->tool : "";
  if (name == "gz.move") return tool == "move";
  if (name == "gz.rotate") return tool == "rotate";
  if (name == "gz.scale") return tool == "scale";
  if (name == "space") return localSpace;
  if (name == "surf") return snap.surface;
  if (name == "snap") return snap.always;
  if (name == "rsnap") return snap.rotateOn;
  if (name == "ssnap") return snap.scaleOn;
  if (name == "view") return debugView != DebugView::Lit;
  if (name == "speed") return speedPopup_;
  return false;
}

std::string SceneView::ToolbarLabel(const std::string& name, bool compact) const {
  if (name == "persp") return compact ? viewPreset_.substr(0, 4) : viewPreset_;
  if (name == "tier") return std::string(TierLabel(quality.tier)) + (quality.lighting == LightingMode::Unlit && !compact ? " · Unlit" : "");
  if (name == "view") return compact ? std::string(DebugViewName(debugView)).substr(0, 6) : DebugViewName(debugView);
  if (name == "show") return "Show";
  if (name == "gi") return compact ? "GI" : std::format("Bake GI ({})", probes_.size() + (hasVolume_ ? 1 : 0) + lightmapItems_);
  if (name == "align") return "Align";
  if (name == "tool") return compact ? "…" : "Tools";
  if (name == "snapv") return Num(snap.move);
  if (name == "rsnapv") return Num(snap.rotate) + "°";
  if (name == "ssnapv") return Num(snap.scale);
  if (name == "speed") return std::format("{}", speedLevel) + (speedScalar != 1.f ? std::format(" ×{}", Num(speedScalar)) : "");
  if (name == "more") return "»";
  return "";
}

// A click on a toolbar control (also from the "»" menu: fromMenu, the menus open at the » button).
bool SceneView::ToolbarClick(const Chip& hit, EditorModel& model, bool fromMenu) {
  const std::string& n = hit.name;
  auto tool = [&](const char* id) { model.tool = id; LogInfo("tool: {}", id); };
  if (n == "view" || n == "persp" || n == "show" || n == "tool" || n == "tier" || n == "snapv" || n == "rsnapv" || n == "ssnapv" || n == "more") {
    if (fromMenu && ui_) ui_->CloseMenu();
    OpenChipMenu(hit, model);
  } else if (n == "gz.move") tool("move");
  else if (n == "gz.rotate") tool("rotate");
  else if (n == "gz.scale") tool("scale");
  else if (n == "space") localSpace = !localSpace;
  else if (n == "surf") snap.surface = !snap.surface;
  else if (n == "snap") snap.always = !snap.always;
  else if (n == "rsnap") snap.rotateOn = !snap.rotateOn;
  else if (n == "ssnap") snap.scaleOn = !snap.scaleOn;
  else if (n == "speed") speedPopup_ = !speedPopup_;
  else if (n == "gi") bakeRequested_ = true;
  else if (n == "align") alignOpen_ = !alignOpen_;
  else if (n.starts_with("sp.l")) SetSpeedLevel(n[4] - '0');                 // sp.l1 .. sp.l8
  else if (n == "sp.minus" || n == "sp.plus") {
    size_t at = 3;
    for (size_t k = 0; k < std::size(kSpeedScalars); ++k)
      if (std::fabs(kSpeedScalars[k] - speedScalar) < 1e-4f) at = k;
    at = n == "sp.plus" ? std::min(at + 1, std::size(kSpeedScalars) - 1) : (at > 0 ? at - 1 : 0);
    SetSpeedScalar(kSpeedScalars[at]);
  } else return false;
  return true;
}

// ---- drawing ----
void SceneView::DrawToolbar(Batch2D& b, const Theme& t, UIContext& ui, const Widget& w, const EditorModel* model, float rowY) {
  toolModel_ = model;
  if (viewPreset_ != "Perspective") { // orbiting away from a preset is a free perspective view again
    static const struct { const char* name; float yaw, pitch; } views[] = {
        {"Top", 0, -89.5f}, {"Bottom", 0, 89.5f}, {"Front", 0, 0}, {"Back", 180, 0}, {"Left", 90, 0}, {"Right", -90, 0}};
    bool same = false;
    for (auto& v : views)
      if (viewPreset_ == v.name) same = std::fabs(camera.yaw - v.yaw) < 0.01f && std::fabs(camera.pitch - v.pitch) < 0.01f;
    if (!same) viewPreset_ = "Perspective";
  }
  const bool several = model && model->Selection().size() >= 2;
  const bool hasGI = !(probes_.empty() && !hasVolume_ && !lightmapItems_);
  std::vector<std::string> left = {"persp", "tier", "view", "show"};
  if (hasGI) left.push_back("gi");
  if (several) left.push_back("align");
  left.push_back("tool");
  // right cluster, in order; value dropdowns sit right after their toggles
  const std::vector<std::string> right = {"gz.move", "gz.rotate", "gz.scale", "space", "surf", "snap", "snapv", "rsnap", "rsnapv", "ssnap", "ssnapv", "speed"};
  auto widthOf = [&](const std::string& n, bool compact) {
    if (n == "gz.move" || n == "gz.rotate" || n == "gz.scale" || n == "space" || n == "surf" || n == "snap" || n == "rsnap" || n == "ssnap") return kIconW;
    const std::string label = ToolbarLabel(n, compact);
    const bool dropdown = n != "gi" && n != "align" && n != "more";
    return ui.Measure(t.FontSize("overlay", 11), label) + 16 + (dropdown ? 10.f : 0.f) + (n == "speed" ? 16.f : 0.f);
  };
  const float x0 = w.x + 10, x1 = w.x + w.w - 10;
  auto total = [&](const std::vector<std::string>& l, const std::vector<std::string>& r, bool compact, bool more) {
    float s = 0;
    for (const std::string& n : l) s += widthOf(n, compact) + kGap;
    for (const std::string& n : r) s += widthOf(n, compact) + (n.starts_with("gz.") && n != "gz.scale" ? 0.f : kGap);
    if (more) s += widthOf("more", compact) + kGap;
    return s + 12; // the gap between the groups
  };
  // fit: full labels, then short ones, then into the "»" menu (left group from its end first, then the right
  // cluster's optional parts: scale snap, rotation snap, surface)
  bool compact = false;
  std::vector<std::string> l = left, r = right;
  overflow_.clear();
  if (total(l, r, false, false) > x1 - x0) compact = true;
  const std::vector<std::vector<std::string>> dropRight = {{"ssnap", "ssnapv"}, {"rsnap", "rsnapv"}, {"surf"}, {"space"}};
  size_t dropAt = 0;
  while (total(l, r, compact, !overflow_.empty()) > x1 - x0) {
    // the left group goes first, least used first; the view mode menu stays longest
    static const char* order[] = {"tool", "align", "persp", "tier", "gi", "show"};
    auto next = l.end();
    for (const char* o : order)
      if (next == l.end()) next = std::find(l.begin(), l.end(), std::string(o));
    if (next != l.end()) {
      overflow_.push_back(*next);
      l.erase(next);
    } else if (dropAt < dropRight.size()) {
      for (const std::string& n : dropRight[dropAt]) {
        overflow_.push_back(n);
        r.erase(std::remove(r.begin(), r.end(), n), r.end());
      }
      ++dropAt;
    } else break; // as small as it gets
  }
  toolbarCompact_ = compact;
  chips_.erase(std::remove_if(chips_.begin(), chips_.end(), [](const Chip& c) { return c.toolbar; }), chips_.end());
  auto drawChip = [&](const std::string& n, float x, float cw) {
    const bool on = ToolbarOn(n);
    const bool warn = n == "show" && BudgetOver();
    const bool bake = n == "gi" && lightmapItems_ > 0 && lightmapKey_ != bakedLightmapKey_;
    b.Rect(x, rowY, cw, kRowH, warn ? WithAlpha(t.C("warn"), 0.9f) : on || bake ? WithAlpha(t.C("accent"), 0.85f) : WithAlpha(t.C("win"), 0.85f), t.N("radius-pill", 12), 1,
           t.C("line"));
    const Color fg = on || warn || bake ? t.C("on-accent") : t.C("fg-dim");
    float tx = x + 8;
    if (n == "speed") { // a little camera, then the level
      b.Rect(x + 7, rowY + 7, 8, 8, fg, t.N("radius-xs", 2));
      b.Triangle(x + 15, rowY + 7, 5, 8, fg);
      tx += 16;
    }
    ui.Text(tx, rowY + 4, t.FontSize("overlay", 11), fg, ToolbarLabel(n, compact));
    if (n != "gi" && n != "align" && n != "more") { // a chevron: opens a list
      const float cx = x + cw - 10, cy = rowY + 11;
      const float tri[4][2] = {{cx - 3, cy - 1.5f}, {cx + 3, cy - 1.5f}, {cx, cy + 2.5f}, {cx, cy + 2.5f}};
      b.Quad(tri, fg);
    }
    chips_.push_back({n, x, rowY, cw, kRowH, true});
  };
  auto drawIcon = [&](const std::string& n, float x, int segment) { // segment: 0 alone, 1 first, 2 middle, 3 last
    const bool on = ToolbarOn(n);
    const float r = segment == 0 ? t.N("radius-pill", 12) : t.N("radius-md", 4);
    b.Rect(x, rowY, kIconW, kRowH, on ? WithAlpha(t.C("accent"), 0.9f) : WithAlpha(t.C("win"), 0.85f), r, 1, t.C("line"));
    const Color fg = on ? t.C("on-accent") : t.C("fg");
    const float cx = x + kIconW * 0.5f, cy = rowY + kRowH * 0.5f;
    const std::string icon = n == "gz.move" ? "tool-move" : n == "gz.rotate" ? "tool-rotate" : n == "gz.scale" ? "tool-scale" : n == "snap" ? "tool-snap" : "";
    if (!icon.empty() && ui.HasIcon(icon)) ui.Icon(icon, cx - 7.5f, cy - 7.5f, 15);
    else if (n == "space") { // world: a globe; local: a cube
      if (!localSpace) {
        b.Rect(cx - 6, cy - 6, 12, 12, {0, 0, 0, 0}, t.N("radius-lg", 5), 1.3f, fg);
        b.Line(cx - 6, cy, cx + 6, cy, 1.2f, fg);
        b.Rect(cx - 2.5f, cy - 6, 5, 12, {0, 0, 0, 0}, 2.5f, 1.1f, fg); // a capsule
      } else {
        b.Rect(cx - 6, cy - 3, 9, 9, {0, 0, 0, 0}, t.N("radius-xs", 2), 1.3f, fg);
        b.Line(cx - 6, cy - 3, cx - 3, cy - 6, 1.2f, fg);
        b.Line(cx + 3, cy - 3, cx + 6, cy - 6, 1.2f, fg);
        b.Line(cx - 3, cy - 6, cx + 6, cy - 6, 1.2f, fg);
        b.Line(cx + 6, cy - 6, cx + 6, cy + 3, 1.2f, fg);
      }
    } else if (n == "surf") { // a box resting on a surface
      b.Line(cx - 7, cy + 5, cx + 7, cy + 5, 1.6f, fg);
      b.Rect(cx - 3.5f, cy - 4, 7, 7, fg, t.N("radius-xs", 2));
    } else if (n == "rsnap") { // an angle
      b.Line(cx - 6, cy + 5, cx + 6, cy + 5, 1.4f, fg);
      b.Line(cx - 6, cy + 5, cx + 4, cy - 6, 1.4f, fg);
      b.Line(cx + 1, cy + 5, cx + 0.5f, cy + 0.5f, 1.2f, fg);
    } else if (n == "ssnap") { // two nested squares
      b.Rect(cx - 6, cy - 6, 12, 12, {0, 0, 0, 0}, t.N("radius-xs", 2), 1.2f, fg);
      b.Rect(cx - 6, cy + 0, 6, 6, fg, t.N("radius-xs", 2));
    } else if (n == "snap") { // a grid
      for (int k = -1; k <= 1; ++k) {
        b.Line(cx + k * 4.f, cy - 6, cx + k * 4.f, cy + 6, 1.f, fg);
        b.Line(cx - 6, cy + k * 4.f, cx + 6, cy + k * 4.f, 1.f, fg);
      }
    }
    chips_.push_back({n, x, rowY, kIconW, kRowH, true});
  };
  float px = x0;
  for (const std::string& n : l) {
    const float cw = widthOf(n, compact);
    drawChip(n, px, cw);
    px += cw + kGap;
  }
  float rx = x1 - (total({}, r, compact, !overflow_.empty()) - 12);
  for (size_t k = 0; k < r.size(); ++k) {
    const std::string& n = r[k];
    const bool seg = n.starts_with("gz.");
    if (widthOf(n, compact) == kIconW) {
      const int segment = !seg ? 0 : n == "gz.move" ? 1 : n == "gz.scale" ? 3 : 2;
      drawIcon(n, rx, segment);
      rx += kIconW + (seg && n != "gz.scale" ? 0.f : kGap);
    } else {
      const float cw = widthOf(n, compact);
      drawChip(n, rx, cw);
      rx += cw + kGap;
    }
  }
  if (!overflow_.empty()) {
    const float cw = widthOf("more", compact);
    drawChip("more", rx, cw);
  }
  // camera speed popup under its button: 8 levels (click or drag across) and the scalar
  speedPopupRect_ = {0, 0, 0, 0};
  if (speedPopup_) {
    const Chip* sc = nullptr;
    for (const Chip& c : chips_)
      if (c.name == "speed") sc = &c;
    if (!sc) speedPopup_ = false;
    else {
      const float pw = 8 * 22 + 20, ph = 78, ppx = std::max(x0, sc->x + sc->w - pw), ppy = rowY + kRowH + 4;
      speedPopupRect_ = {ppx, ppy, pw, ph};
      b.Rect(ppx, ppy, pw, ph, WithAlpha(t.C("win"), 0.97f), t.N("radius-window", 8), 1, t.C("line"));
      ui.Text(ppx + 10, ppy + 6, t.FontSize("overlay", 11), t.C("fg"), std::format("카메라 속도  {:.2g} m/s", flySpeed_));
      for (int k = 1; k <= 8; ++k) {
        const float cx = ppx + 10 + (k - 1) * 22;
        const bool on = k <= speedLevel;
        b.Rect(cx, ppy + 24, 20, 16, on ? WithAlpha(t.C("accent"), k == speedLevel ? 1.f : 0.5f) : WithAlpha(t.C("sunk"), 0.9f), t.N("radius-sm", 3));
        ui.Text(cx + 6, ppy + 25, t.FontSize("overlay", 11), on ? t.C("on-accent") : t.C("fg-dim"), std::format("{}", k));
        chips_.push_back({std::format("sp.l{}", k), cx, ppy + 24, 20, 16, true});
      }
      ui.Text(ppx + 10, ppy + 52, t.FontSize("overlay", 11), t.C("fg-dim"), "배율");
      const float bx = ppx + 50;
      b.Rect(bx, ppy + 49, 22, 20, WithAlpha(t.C("sunk"), 0.9f), t.N("radius-md", 4));
      ui.Text(bx + 8, ppy + 51, t.FontSize("label", 12), t.C("fg"), "−");
      chips_.push_back({"sp.minus", bx, ppy + 49, 22, 20, true});
      ui.Text(bx + 30, ppy + 51, t.FontSize("label", 12), t.C("fg"), std::format("×{}", Num(speedScalar)));
      b.Rect(bx + 80, ppy + 49, 22, 20, WithAlpha(t.C("sunk"), 0.9f), t.N("radius-md", 4));
      ui.Text(bx + 87, ppy + 51, t.FontSize("label", 12), t.C("fg"), "+");
      chips_.push_back({"sp.plus", bx + 80, ppy + 49, 22, 20, true});
    }
  }
  // tooltip after the pointer rests on a control
  if (!tipName_.empty() && tipSeconds_ > 0.45) {
    const std::string tip = ToolbarTooltip(tipName_);
    for (const Chip& c : chips_)
      if (c.name == tipName_ && !tip.empty()) {
        const float tw = ui.Measure(t.FontSize("overlay", 11), tip) + 16, tx = std::clamp(c.x, x0, std::max(x0, x1 - tw));
        b.Rect(tx, c.y + c.h + 4, tw, 20, WithAlpha(t.C("win"), 0.96f), t.N("radius-lg", 5), 1, t.C("line"));
        ui.Text(tx + 8, c.y + c.h + 7, t.FontSize("overlay", 11), t.C("fg"), tip);
        break;
      }
  }
}

bool SceneView::PressToolbar(const std::string& name, EditorModel& model) {
  const Chip* at = nullptr;
  for (const Chip& c : chips_)
    if (c.toolbar && (c.name == name || (!at && c.name == "more"))) at = &c;
  if (!at) return false;
  const Chip hit{name, at->x, at->y, at->w, at->h, true};
  return ToolbarClick(hit, model);
}

SceneView::ToolbarLayout SceneView::ToolbarInfo() const {
  ToolbarLayout out;
  float y = -1;
  for (const Chip& c : chips_) {
    if (!c.toolbar || c.name.starts_with("sp.")) continue;
    if (y < 0) y = c.y;
    if (std::fabs(c.y - y) > 0.5f) out.oneRow = false;
    out.right = std::max(out.right, c.x + c.w);
    out.left = out.left < 0 ? c.x : std::min(out.left, c.x);
    ++out.controls;
  }
  out.hidden = (int)overflow_.size();
  out.compact = toolbarCompact_;
  return out;
}
} // namespace bdg
