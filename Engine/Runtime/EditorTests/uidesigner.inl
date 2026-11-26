// Scripted editor checks: the UI designer (queue 7-3, lane A). Included by Engine.cpp after graphics.inl.
#include "GameUI/GameUIComponents.h"

// BDG_UIDESIGNER_TEST=1: a canvas with a panel (top left), a title (centre) and a button (bottom right); the "UI
// Designer" workspace tab (a real click) shows them in the Scene panel. Real drags: a click selects the panel, a
// drag moves it by the dragged amount in reference pixels, the bottom-right handle resizes it, a drag near the
// screen's left edge snaps onto it (a guide is shown); the anchor picker turns the title's anchor to bottom right
// and it stays where it is on screen; Ctrl+Z puts the anchor back in one step; the phone portrait preset keeps the
// button at its distance from the bottom-right corner and the panel at its top-left offset. The palette: a Slider
// dragged onto the screen lands centred on the drop point, selected, one undo step (Ctrl+Z takes it away); a click on
// Toggle adds one in the middle. Preview: a click flips the toggle as in the game; leaving the preview puts it back
// (no undo steps).
namespace uidesignertest {
std::string tab = "-", select = "-", move = "-", resize = "-", snap = "-", anchor = "-", undo = "-", resolution = "-", palette = "-", preview = "-";
std::string added;
float dropX = 0, dropY = 0;
bdg::GameUI::Rect r0, title0;
float btnRight = 0, btnBottom = 0, scale0 = 1;
size_t depth0 = 0;
}
void InjectUIDesignerTest(bdg_engine* e, int frame, float scale) {
  using namespace uidesignertest;
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  bdg::UIDesigner& d = e->uiDesigner;
  auto ev = [&](T type, float x, float y, int button = 0, bool ctrl = false) {
    bdg::InputEvent i{type, x * scale, y * scale, button};
    i.ctrl = ctrl;
    e->window.InjectEvent(i);
  };
  auto click = [&](float x, float y) {
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    ev(T::MouseUp, x, y, 1);
  };
  auto drag = [&](float x, float y, float dx, float dy, bool release = true, bool ctrl = false) { // Ctrl: no snapping
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    for (int k = 1; k <= 4; ++k) ev(T::MouseMove, x + dx * k / 4, y + dy * k / 4, 0, ctrl);
    if (release) ev(T::MouseUp, x + dx, y + dy, 1, ctrl);
  };
  auto element = [&](const char* id) -> bdg::UIElement* {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("UIElement") : nullptr;
    return c ? static_cast<bdg::UIElement*>(c->data) : nullptr;
  };
  auto approx = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };
  auto sc = [&] { return d.Screen().w / 1920.f; }; // UI units per reference pixel (16:9 preset, canvas 1920 x 1080)
  if (frame == 2) {
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) gfxtest::Run(m, "del", id, "{}");
    gfxtest::Run(m, "add", "", R"({"id":"uican","name":"HUD"})");
    gfxtest::Run(m, "addcomp", "uican", R"({"type":"UICanvas","value":{"referenceSize":[1920,1080]}})");
    gfxtest::Run(m, "add", "", R"({"id":"uip","name":"Panel","parent":"uican"})");
    gfxtest::Run(m, "addcomp", "uip", R"({"type":"UIElement","value":{"kind":"Panel","anchor":"TopLeft","position":[300,200],"size":[400,200],"color":[0.2,0.4,0.8,0.9]}})");
    gfxtest::Run(m, "add", "", R"({"id":"uit","name":"Title","parent":"uican"})");
    gfxtest::Run(m, "addcomp", "uit", R"({"type":"UIElement","value":{"kind":"Text","anchor":"Center","position":[0,-60],"size":[600,90],"text":"Title"}})");
    gfxtest::Run(m, "add", "", R"({"id":"uib","name":"Play","parent":"uican"})");
    gfxtest::Run(m, "addcomp", "uib", R"({"type":"UIElement","value":{"kind":"Button","anchor":"BottomRight","position":[-60,-50],"size":[320,90],"text":"Play"}})");
    m->SetSelection({});
    if (bdg::Widget* ws = e->editor->Find("ws.tabs"); ws && ws->itemX.size() >= 4) click((ws->itemX[2] + ws->itemX[3]) * 0.5f, ws->y + ws->h * 0.5f);
  }
  if (frame == 4) {
    tab = UIDesignerActive(e) && d.Screen().w > 100 ? "1" : "0";
    bdg::GameUI::Rect r;
    if (d.ElementRect("uip", r)) click(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
  }
  if (frame == 6) {
    select = m->Selected() == "uip" ? "1" : "0(" + m->Selected() + ")";
    d.ElementRect("uip", r0);
    drag(r0.x + r0.w * 0.5f, r0.y + r0.h * 0.5f, 40, 25, true, true);
  }
  if (frame == 8) {
    bdg::UIElement* p = element("uip");
    move = p && approx(p->position.x, 300 + 40 / sc(), 1.5f) && approx(p->position.y, 200 + 25 / sc(), 1.5f) && approx(p->size.x, 400, 0.01f)
               ? "1" : std::format("0({:.1f},{:.1f})", p ? p->position.x : -1, p ? p->position.y : -1);
    float hx, hy;
    if (d.HandlePoint(4, hx, hy)) drag(hx, hy, 30, 20, true, true); // bottom-right handle
  }
  if (frame == 10) {
    bdg::UIElement* p = element("uip");
    resize = p && approx(p->size.x, 400 + 30 / sc(), 1.5f) && approx(p->size.y, 200 + 20 / sc(), 1.5f) ? "1" : std::format("0({:.1f},{:.1f})", p ? p->size.x : -1, p ? p->size.y : -1);
    bdg::GameUI::Rect r;
    d.ElementRect("uip", r);
    drag(r.x + 10, r.y + 10, d.Screen().x + 3 - r.x, 0, false); // the left edge 3 units from the screen's: snaps
  }
  if (frame == 11) {
    snap = d.Guides() > 0 ? "" : "0(no guide)";
    bdg::GameUI::Rect r;
    d.ElementRect("uip", r);
    ev(T::MouseUp, r.x + 10, r.y + 10, 1);
  }
  if (frame == 13) {
    bdg::UIElement* p = element("uip");
    if (snap.empty()) snap = p && p->position.x == 0 ? "1" : std::format("0({:.2f})", p ? p->position.x : -1);
    bdg::GameUI::Rect r;
    if (d.ElementRect("uit", r)) click(r.x + r.w * 0.5f, r.y + r.h * 0.5f); // select the title
  }
  if (frame == 15) {
    d.ElementRect("uit", title0);
    float x, y;
    if (d.ControlCenter("anchor", x, y)) click(x, y);
  }
  if (frame == 17) {
    depth0 = m->bus.UndoDepth();
    float x, y;
    if (d.ControlCenter("anc.8", x, y)) click(x, y); // bottom right
  }
  if (frame == 19) {
    bdg::UIElement* t = element("uit");
    bdg::GameUI::Rect r;
    const bool same = d.ElementRect("uit", r) && approx(r.x, title0.x, 0.6f) && approx(r.y, title0.y, 0.6f) && approx(r.w, title0.w, 0.6f);
    anchor = t && t->anchor == bdg::UIAnchor::BottomRight && same && m->bus.UndoDepth() == depth0 + 1 ? "1"
             : std::format("0(anchor {} same {} undo {}->{})", t ? (int)t->anchor : -1, same, depth0, m->bus.UndoDepth());
    bdg::InputEvent z{T::KeyDown};
    z.key = bdg::Key::Z;
    z.ctrl = true;
    e->window.InjectEvent(z);
  }
  if (frame == 21) {
    bdg::UIElement* t = element("uit");
    bdg::GameUI::Rect r;
    undo = t && t->anchor == bdg::UIAnchor::Center && d.ElementRect("uit", r) && approx(r.x, title0.x, 0.6f) && m->bus.UndoDepth() == depth0 ? "1" : "0";
    // the button's distance from the bottom-right corner and the panel's offset, in reference pixels
    bdg::GameUI::Rect b, s = d.Screen();
    scale0 = sc();
    d.ElementRect("uib", b);
    btnRight = (s.x + s.w - (b.x + b.w)) / scale0;
    btnBottom = (s.y + s.h - (b.y + b.h)) / scale0;
    float x, y;
    if (d.ControlCenter("preset", x, y)) click(x, y);
  }
  if (frame == 23) {
    const auto& items = e->editor->Menu();
    float x, y, w, h;
    for (size_t k = 0; k < items.size(); ++k)
      if (items[k].label.starts_with("폰 세로") && e->editor->MenuItemRect(k, x, y, w, h)) click(x + w * 0.5f, y + h * 0.5f);
  }
  if (frame == 25) {
    bdg::GameUI::Rect b, p, s = d.Screen();
    const float k = s.h / 1080.f; // the canvas matches height (UICanvas.matchHeight 1)
    const bool portrait = s.h > s.w;
    d.ElementRect("uib", b);
    d.ElementRect("uip", p);
    const bool btn = approx((s.x + s.w - (b.x + b.w)) / k, btnRight, 1.f) && approx((s.y + s.h - (b.y + b.h)) / k, btnBottom, 1.f);
    const bool pan = approx((p.x - s.x) / k, 0, 1.f) && approx((p.y - s.y) / k, element("uip") ? element("uip")->position.y : -1, 1.f);
    resolution = portrait && btn && pan ? "1" : std::format("0(portrait {} button {} panel {})", portrait, btn, pan);
    if (const char* dump = std::getenv("BDG_UIDESIGNER_DUMP")) {
      int32_t w = 0, h = 0;
      std::vector<uint8_t> px;
      e->rhi.ReadPixels(w, h, px);
      bdg::WritePNG(dump, w, h, px.data(), true);
    }
  }
  if (frame == 26) d.preset = 0; // 16:9 again
  if (frame == 28) { // drag a Slider from the palette onto the screen
    depth0 = m->bus.UndoDepth();
    const bdg::GameUI::Rect sr = d.Screen();
    dropX = sr.x + sr.w * 0.3f;
    dropY = sr.y + sr.h * 0.78f;
    float x, y;
    if (d.ControlCenter("pal.Slider", x, y)) drag(x, y, dropX - x, dropY - y);
  }
  if (frame == 30) {
    added = m->Selected();
    bdg::UIElement* s = element(added.c_str());
    bdg::GameUI::Rect r;
    const bool placed = s && s->kind == bdg::UIKind::Slider && d.ElementRect(added, r) && approx(r.x + r.w * 0.5f, dropX, 1.f) && approx(r.y + r.h * 0.5f, dropY, 1.f);
    palette = placed && m->bus.UndoDepth() == depth0 + 1 ? "" : std::format("0(added '{}' placed {} undo {}->{})", added, placed, depth0, m->bus.UndoDepth());
    bdg::InputEvent z{T::KeyDown};
    z.key = bdg::Key::Z;
    z.ctrl = true;
    e->window.InjectEvent(z);
  }
  if (frame == 32) {
    if (palette.empty()) palette = !m->scene.Find(added) ? "" : "0(undo left it)";
    float x, y;
    if (d.ControlCenter("pal.Toggle", x, y)) click(x, y); // a click: in the screen's middle
  }
  if (frame == 34) {
    added = m->Selected();
    bdg::UIElement* t = element(added.c_str());
    bdg::GameUI::Rect r;
    const bdg::GameUI::Rect sr = d.Screen();
    if (palette.empty())
      palette = t && t->kind == bdg::UIKind::Toggle && t->value == 1.f && d.ElementRect(added, r) && approx(r.x + r.w * 0.5f, sr.x + sr.w * 0.5f, 1.f) ? "1" : "0(toggle)";
    depth0 = m->bus.UndoDepth();
    float x, y;
    if (d.ControlCenter("preview", x, y)) click(x, y);
  }
  if (frame == 36) {
    bdg::GameUI::Rect r;
    if (d.ElementRect(added, r)) click(r.x + 20, r.y + r.h * 0.5f); // on the toggle, running
  }
  if (frame == 38) {
    bdg::UIElement* t = element(added.c_str());
    preview = d.preview && t && t->value == 0.f ? "" : std::format("0(preview {} value {})", d.preview, t ? t->value : -1);
    float x, y;
    if (d.ControlCenter("preview", x, y)) click(x, y); // off: the toggle as it was
  }
  if (frame == 40) {
    bdg::UIElement* t = element(added.c_str());
    if (preview.empty()) preview = !d.preview && t && t->value == 1.f && m->bus.UndoDepth() == depth0 ? "1" : std::format("0(after: value {} undo {}->{})", t ? t->value : -1, depth0, m->bus.UndoDepth());
  }
}
void ReportUIDesignerTest(bdg_engine*) {
  using namespace uidesignertest;
  bdg::LogInfo("uidesigner test: tab={} select={} move={} resize={} snap={} anchor={} undo={} resolution={} palette={} preview={}", tab, select, move,
               resize, snap, anchor, undo, resolution, palette, preview);
}
BDG_EDITOR_TEST("BDG_UIDESIGNER_TEST", InjectUIDesignerTest, ReportUIDesignerTest)

// BDG_UICSHARP_TEST=1 (BDG_SCRIPT_ASSEMBLIES = the samples): the C# UIProbe drives a toggle, slider, input field,
// dropdown and scroll view through UI.Toggle / Slider / Input / Dropdown / ScrollView while playing: reading, setting,
// OnChanged once per change, a missing element as a stand-in.
namespace uicsharptest {
std::string probe;
}
void InjectUICSharpTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  if (frame == 2) {
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) gfxtest::Run(m, "del", id, "{}");
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.rfind("ui probe: ", 0) == 0) uicsharptest::probe = l; });
    gfxtest::Run(m, "add", "", R"({"id":"pcan","name":"HUD"})");
    gfxtest::Run(m, "addcomp", "pcan", R"({"type":"UICanvas"})");
    const char* controls[][2] = {{"probe_sound", R"("kind":"Toggle","value":1,"text":"Sound")"},
                                 {"probe_volume", R"("kind":"Slider","position":[0,80],"value":0.5)"},
                                 {"probe_name", R"("kind":"InputField","position":[0,160],"placeholder":"Name")"},
                                 {"probe_quality", R"("kind":"Dropdown","position":[0,240],"options":"Low|Mid|High","value":0)"},
                                 {"probe_list", R"("kind":"ScrollView","position":[400,0],"size":[300,200])"}};
    for (auto& c : controls) {
      gfxtest::Run(m, "add", "", std::format(R"({{"id":"{}","name":"{}","parent":"pcan"}})", c[0], c[0]));
      gfxtest::Run(m, "addcomp", c[0], std::format(R"({{"type":"UIElement","value":{{{}}}}})", c[1]));
    }
    gfxtest::Run(m, "add", "", R"({"id":"pdrv","name":"Driver"})");
    gfxtest::Run(m, "addcomp", "pdrv", R"({"type":"UIProbe"})");
  }
  if (frame == 4) TogglePlay(e);
  if (frame == 20) TogglePlay(e);
}
void ReportUICSharpTest(bdg_engine*) {
  bdg::SetLogHook({});
  bdg::LogInfo("uicsharp test: {}", uicsharptest::probe.empty() ? std::string("(no probe line)") : uicsharptest::probe);
}
BDG_EDITOR_TEST("BDG_UICSHARP_TEST", InjectUICSharpTest, ReportUICSharpTest)
