// Scripted editor checks: the editor's own UI fixes from the UI review (Docs/planning/ui-review-2026-11-26.md, lane A
// batches). Included by Engine.cpp after uidesigner.inl.

// BDG_MENUBAR_TEST=1 (UI review B2): the menu bar with real clicks. Every menu opens a list; Save shows its shortcut;
// GameObject > 3D 도형 > 큐브 adds a cube (one undo step, selected); Edit > 실행 취소 takes it away; Window > 패널 열기
// > Console shows the Console; Component > 컴포넌트 추가… > AudioSource adds it to the selection.
namespace menubartest {
std::string open = "-", keys = "-", create = "-", undo = "-", panel = "-", component = "-";
int opened = 0;
size_t depth0 = 0;
}
void InjectMenuBarTest(bdg_engine* e, int frame, float scale) {
  using namespace menubartest;
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  auto click = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
    e->window.InjectEvent({T::MouseDown, x * scale, y * scale, 1});
    e->window.InjectEvent({T::MouseUp, x * scale, y * scale, 1});
  };
  auto clickWidget = [&](const char* id) {
    bdg::Widget* w = e->editor->Find(id);
    if (w) click(w->x + w->w * 0.5f, w->y + w->h * 0.5f);
    else bdg::LogError("menubar test: no {}", id);
  };
  auto pick = [&](const std::string& label) { // an item of the open menu, by its text
    const auto& items = e->editor->Menu();
    float x, y, w, h;
    for (size_t k = 0; k < items.size(); ++k)
      if (items[k].label == label && e->editor->MenuItemRect(k, x, y, w, h)) {
        click(x + w * 0.5f, y + h * 0.5f);
        return true;
      }
    std::string all;
    for (const auto& it : items) all += it.label + "|";
    bdg::LogError("menubar test: no item {} (menu: {})", label, all.substr(0, 300));
    return false;
  };
  static const char* menus[] = {"menu.file", "menu.edit", "menu.assets", "menu.gameobject", "menu.component", "menu.build", "menu.window", "menu.help"};
  if (frame == 2) {
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) gfxtest::Run(m, "del", id, "{}");
    m->SetSelection({});
  }
  // frames 4..27: each menu opens (a click), its items are read, it closes
  for (int i = 0; i < 8; ++i) {
    if (frame == 4 + i * 3) clickWidget(menus[i]);
    if (frame == 6 + i * 3) {
      const auto& items = e->editor->Menu();
      if (!items.empty()) ++opened;
      if (i == 0) // File: Save shows its keys
        for (const auto& it : items)
          if (it.label == "저장") keys = it.keys == e->shortcuts.KeysOf("file.save") && !it.keys.empty() ? "1" : "0(" + it.keys + ")";
      if (i == 3 && std::getenv("BDG_MENUBAR_DUMP")) { // a picture with the GameObject menu open
        int32_t w = 0, h = 0;
        e->window.GetDrawableSize(w, h);
        std::vector<uint8_t> px;
        e->rhi.ReadPixels(w, h, px);
        bdg::WritePNG(std::getenv("BDG_MENUBAR_DUMP"), w, h, px.data(), true);
      }
      e->editor->CloseMenu();
    }
  }
  if (frame == 30) {
    open = opened == 8 ? "1" : std::format("0({}/8)", opened);
    depth0 = m->bus.UndoDepth();
    clickWidget("menu.gameobject");
  }
  if (frame == 32) pick("3D 도형 ▸");
  if (frame == 34) pick("큐브");
  if (frame == 36) {
    bdg::SceneObject* o = m->scene.Find(m->Selected());
    bdg::Component* mr = o ? o->Find("MeshRenderer") : nullptr;
    std::string mesh;
    if (mr) bdg::Registry::Get(mr->data, *mr->type, "mesh", mesh);
    create = o && o->name == "Cube" && mesh == "\"Cube\"" && m->bus.UndoDepth() == depth0 + 1 ? "" : std::format("0(sel '{}' mesh {} undo {}->{})", m->Selected(), mesh, depth0, m->bus.UndoDepth());
    clickWidget("menu.edit");
  }
  if (frame == 38) pick("실행 취소");
  if (frame == 40) {
    if (create.empty()) create = "1";
    undo = m->scene.Roots().empty() && m->bus.UndoDepth() == depth0 ? "1" : "0";
    clickWidget("menu.window");
  }
  if (frame == 42) pick("패널 열기 ▸");
  if (frame == 44) pick("Console");
  if (frame == 46) {
    bdg::Widget* c = e->editor->Find("console");
    panel = c && !c->hidden && c->w > 10 ? "1" : "0";
    gfxtest::Run(m, "add", "", R"({"id":"mbobj","name":"Speaker"})");
    m->Select("mbobj");
  }
  if (frame == 47) clickWidget("menu.component"); // the selection is in place when the menu is made
  if (frame == 49) pick("컴포넌트 추가…");
  if (frame == 50) { // the list is searchable: type to narrow it, as a user would
    bdg::InputEvent t{T::Text};
    t.text = "Audio S";
    e->window.InjectEvent(t);
  }
  if (frame == 52) pick("Audio Source");
  if (frame == 54) {
    bdg::SceneObject* o = m->scene.Find("mbobj");
    component = o && o->Find("AudioSource") ? "1" : "0";
  }
}
void ReportMenuBarTest(bdg_engine*) {
  using namespace menubartest;
  bdg::LogInfo("menubar test: open={} keys={} create={} undo={} panel={} component={}", open, keys, create, undo, panel, component);
}
BDG_EDITOR_TEST("BDG_MENUBAR_TEST", InjectMenuBarTest, ReportMenuBarTest)

// BDG_EMPTYWS_TEST=1 (UI review B4), run without BDG_ASSETS and with BDG_DIALOG_ANSWER = an empty folder: the Project
// panel says no project is open (no sample files); Graph / Animation / Scripting show "준비 중" in the Scene panel and
// its "레벨로 돌아가기" button (a real click) goes back; 새 프로젝트… (a real click; the folder picker answers with
// BDG_DIALOG_ANSWER) makes Assets / Scenes there and opens it in the Project panel.
namespace emptywstest {
std::string empty = "-", tabs = "-", back = "-", project = "-";
int drawn = 0;
}
void InjectEmptyWsTest(bdg_engine* e, int frame, float scale) {
  using namespace emptywstest;
  using T = bdg::InputEvent::Type;
  if (!e->editor) return;
  auto click = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
    e->window.InjectEvent({T::MouseDown, x * scale, y * scale, 1});
    e->window.InjectEvent({T::MouseUp, x * scale, y * scale, 1});
  };
  auto clickTab = [&](int i) {
    bdg::Widget* ws = e->editor->Find("ws.tabs");
    if (ws && (int)ws->itemX.size() > i) click(((i ? ws->itemX[i - 1] : ws->x) + ws->itemX[i]) * 0.5f, ws->y + ws->h * 0.5f);
    else bdg::LogError("emptyws test: no tab {}", i);
    e->wsBack[2] = 0; // drawn again by the empty state
  };
  auto selected = [&] {
    bdg::Widget* ws = e->editor->Find("ws.tabs");
    return ws ? ws->selectedIndex : -1;
  };
  if (frame == 3) {
    bdg::Widget* open = e->editor->Find("project.open");
    bdg::Widget* fresh = e->editor->Find("project.new");
    empty = !e->assetsOpen && open && fresh && !open->hidden && open->w > 10 && !e->editor->Find("project.sample") ? "1" : "0";
    clickTab(1); // Graph
  }
  for (int i : {1, 2, 4}) { // each empty workspace draws its state in the Scene panel
    const int at = i == 1 ? 6 : i == 2 ? 9 : 12;
    if (frame == at) {
      bdg::Widget* scene = e->editor->Find("scene");
      if (selected() == i && e->wsBack[2] > 10 && scene && !scene->hidden && scene->w > 10) ++drawn;
      else bdg::LogError("emptyws test: tab {} selected {} button w {}", i, selected(), e->wsBack[2]);
      if (i == 1 && std::getenv("BDG_EMPTYWS_DUMP")) {
        int32_t w = 0, h = 0;
        e->window.GetDrawableSize(w, h);
        std::vector<uint8_t> px;
        e->rhi.ReadPixels(w, h, px);
        bdg::WritePNG(std::getenv("BDG_EMPTYWS_DUMP"), w, h, px.data(), true);
      }
      if (i != 4) clickTab(i == 1 ? 2 : 4);
      else click(e->wsBack[0] + e->wsBack[2] * 0.5f, e->wsBack[1] + e->wsBack[3] * 0.5f); // 레벨로 돌아가기
    }
  }
  if (frame == 15) {
    tabs = drawn == 3 ? "1" : std::format("0({}/3)", drawn);
    back = selected() == 0 ? "1" : std::format("0({})", selected());
    if (bdg::Widget* b = e->editor->Find("project.new")) click(b->x + b->w * 0.5f, b->y + b->h * 0.5f);
    else bdg::LogError("emptyws test: no project.new");
  }
  if (frame == 19) {
    const char* dir = std::getenv("BDG_DIALOG_ANSWER");
    std::error_code ec;
    const bool made = dir && std::filesystem::is_directory(bdg::PathU8(dir) / "Assets", ec) && std::filesystem::is_directory(bdg::PathU8(dir) / "Scenes", ec);
    project = e->assetsOpen && made && !e->editor->Find("project.empty") ? "1" : std::format("0(open {} made {})", e->assetsOpen, made);
  }
}
void ReportEmptyWsTest(bdg_engine*) {
  using namespace emptywstest;
  bdg::LogInfo("emptyws test: empty={} tabs={} back={} project={}", empty, tabs, back, project);
}
BDG_EDITOR_TEST("BDG_EMPTYWS_TEST", InjectEmptyWsTest, ReportEmptyWsTest)

// BDG_TOPBAR_TEST=1 (UI review B10): the top toolbar. Icon buttons (play options, pause, one frame) are 28 x 24 with
// tooltips that name their keys; the badges are 24 high; the speed is a "1×" button whose list (a real click) sets
// 2×; pause lights and unlights on real clicks; the texts are Korean (U12).
namespace topbartest {
std::string tips = "-", size = "-", badge = "-", korean = "-", speed = "-", pause = "-";
}
void InjectTopBarTest(bdg_engine* e, int frame, float scale) {
  using namespace topbartest;
  using T = bdg::InputEvent::Type;
  if (!e->editor) return;
  auto click = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
    e->window.InjectEvent({T::MouseDown, x * scale, y * scale, 1});
    e->window.InjectEvent({T::MouseUp, x * scale, y * scale, 1});
  };
  auto clickWidget = [&](const char* id) {
    if (bdg::Widget* w = e->editor->Find(id)) click(w->x + w->w * 0.5f, w->y + w->h * 0.5f);
    else bdg::LogError("topbar test: no {}", id);
  };
  auto find = [&](const char* id) { return e->editor->Find(id); };
  if (frame == 3) {
    bdg::Widget* pw = find("tool.pause");
    const std::string keys = e->shortcuts.KeysOf("play.pause");
    bool ok = pw && !keys.empty() && pw->tooltip.find(keys) != std::string::npos;
    for (const char* id : {"tool.playopts", "tool.step", "tool.play", "tool.timescale", "tool.quality", "tool.platform", "tool.build"})
      if (bdg::Widget* w = find(id); !w || w->tooltip.empty()) {
        ok = false;
        bdg::LogError("topbar test: no tooltip on {}", id);
      }
    tips = ok ? "1" : std::format("0(pause tip '{}' keys '{}')", pw ? pw->tooltip : "", keys);
    ok = true;
    for (const char* id : {"tool.playopts", "tool.pause", "tool.step"})
      if (bdg::Widget* w = find(id); !w || std::abs(w->w - 28) > 0.5f || std::abs(w->h - 24) > 0.5f) ok = false;
    size = ok ? "1" : "0";
    bdg::Widget* q = find("tool.quality");
    bdg::Widget* pf = find("tool.platform");
    badge = q && pf && q->h >= 24 && pf->h >= 24 ? "1" : std::format("0({} {})", q ? q->h : 0, pf ? pf->h : 0);
    bdg::Widget* play = find("tool.play");
    bdg::Widget* build = find("tool.build");
    korean = play && build && play->text == "▶  플레이" && build->text == "빌드" ? "1" : "0";
    clickWidget("tool.timescale");
  }
  if (frame == 5) {
    const auto& items = e->editor->Menu();
    bool one = false;
    for (const auto& it : items) one |= it.label == "1×" && it.checked;
    if (items.size() != 6 || !one) speed = std::format("0(items {} checked {})", items.size(), one);
    if (std::getenv("BDG_TOPBAR_DUMP")) { // a picture with the speed list open
      int32_t w = 0, h = 0;
      e->window.GetDrawableSize(w, h);
      std::vector<uint8_t> px;
      e->rhi.ReadPixels(w, h, px);
      bdg::WritePNG(std::getenv("BDG_TOPBAR_DUMP"), w, h, px.data(), true);
    }
    float x, y, w, h;
    for (size_t k = 0; k < items.size(); ++k)
      if (items[k].label == "2×" && e->editor->MenuItemRect(k, x, y, w, h)) click(x + w * 0.5f, y + h * 0.5f);
  }
  if (frame == 7) {
    bdg::Widget* b = find("tool.timescale");
    if (speed == "-") speed = e->play.TimeScale() == 2.f && b && b->text == "2×" ? "1" : std::format("0({} '{}')", e->play.TimeScale(), b ? b->text : "");
    SetTimeScale(e, 1);
    clickWidget("tool.pause");
  }
  if (frame == 9) {
    bdg::Widget* b = find("tool.pause");
    pause = b && b->primary && e->play.Paused() ? "" : "0(on)";
    clickWidget("tool.pause");
  }
  if (frame == 11) {
    bdg::Widget* b = find("tool.pause");
    if (pause.empty()) pause = b && !b->primary && !e->play.Paused() ? "1" : "0(off)";
  }
}
void ReportTopBarTest(bdg_engine*) {
  using namespace topbartest;
  bdg::LogInfo("topbar test: tips={} size={} badge={} korean={} speed={} pause={}", tips, size, badge, korean, speed, pause);
}
BDG_EDITOR_TEST("BDG_TOPBAR_TEST", InjectTopBarTest, ReportTopBarTest)

// BDG_UDPOLISH_TEST=1 (UI review B14): the UI Designer's toolbar and palette. Every control has a tooltip, chips are
// 24 high, the Scene panel's tab says "UI 화면". Align (real clicks) puts two selected elements' left edges together:
// the 24 x 24 left button when the eight fit, else the "정렬" chip's list, else "»" > 정렬 > 왼쪽 맞춤. A view under 700
// wide (BDG_UI_SCALE=1.5 or more) has a 36-wide icon palette that scrolls with the wheel; 1280 x 720 at scale 1 keeps
// the 112-wide palette. Hovering a control makes it the tooltip's; right-click on an element opens its menu and
// 맨 뒤로 moves it first.
namespace udpolishtest {
std::string tips = "-", chips = "-", title = "-", align = "-", hover = "-", context = "-", narrow = "-";
std::string a, b, c, way; // way: how align is reached ("al.left", "align", "more")
bool isNarrow = false;
}
void InjectUdPolishTest(bdg_engine* e, int frame, float scale) {
  using namespace udpolishtest;
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  bdg::UIDesigner& d = e->uiDesigner;
  auto ev = [&](T type, float x, float y, int button = 0) { e->window.InjectEvent({type, x * scale, y * scale, button}); };
  auto click = [&](float x, float y, int button = 1) {
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, button);
    ev(T::MouseUp, x, y, button);
  };
  auto clickControl = [&](const char* name) {
    float x, y;
    if (d.ControlCenter(name, x, y)) click(x, y);
    else bdg::LogError("udpolish test: no control {}", name);
  };
  auto pick = [&](const std::string& label) {
    const auto& items = e->editor->Menu();
    float x, y, w, h;
    for (size_t k = 0; k < items.size(); ++k)
      if (items[k].label == label && e->editor->MenuItemRect(k, x, y, w, h)) return click(x + w * 0.5f, y + h * 0.5f);
    std::string all;
    for (const auto& it : items) all += it.label + "|";
    bdg::LogError("udpolish test: no item {} ({})", label, all);
  };
  auto leftOf = [&](const std::string& id) {
    bdg::GameUI::Rect r;
    return d.ElementRect(id, r) ? r.x : -1.f;
  };
  if (frame == 2) {
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) gfxtest::Run(m, "del", id, "{}");
    m->SetSelection({});
    if (bdg::Widget* ws = e->editor->Find("ws.tabs")) ws->selectedIndex = 3; // UI Designer
  }
  if (frame == 4) {
    const bdg::GameUI::Rect s = d.Screen();
    a = d.AddElement(*m, "Button", s.x + s.w * 0.3f, s.y + s.h * 0.3f);
    b = d.AddElement(*m, "Text", s.x + s.w * 0.6f, s.y + s.h * 0.55f);
    c = d.AddElement(*m, "Image", s.x + s.w * 0.75f, s.y + s.h * 0.75f);
  }
  if (frame == 5) m->SetSelection({a, b}); // after the adds' own Select (applied on the next Update)
  if (frame == 6) {
    bdg::Widget* scene = e->editor->Find("scene");
    isNarrow = scene && scene->w < 700;
    bdg::Widget* dw = e->editor->Find("dock");
    title = dw && dw->dock && dw->dock->titles["scene"] == "UI 화면" ? "1" : "0";
    float x, y;
    way = d.ControlCenter("al.left", x, y) ? "al.left" : d.ControlCenter("align", x, y) ? "align" : "more";
    std::vector<std::string> names = {"preset", way};
    for (const std::string& k : bdg::UIDesigner::PaletteKinds()) names.push_back("pal." + k);
    for (const char* n : {"zoom", "preview", "anchor", "al.center", "al.right", "al.top", "al.middle", "al.bottom", "al.dist.x", "al.dist.y"}) names.push_back(n);
    bool ok = true, sized = true;
    for (const std::string& n : names) {
      if (bdg::UIDesigner::Tooltip(n).empty()) {
        ok = false;
        bdg::LogError("udpolish test: no tooltip for {}", n);
      }
      if (n == "preset" || n == way) // shown: 24 high
        for (const char* id : {n.c_str()}) {
          float cx, cy;
          if (!d.ControlCenter(id, cx, cy)) { sized = false; bdg::LogError("udpolish test: no control {}", id); }
        }
    }
    tips = ok ? "1" : "0";
    chips = sized ? "1" : "0";
    if (isNarrow) narrow = d.PaletteWidth() == 36 ? "" : std::format("0(palette {})", d.PaletteWidth());
    else narrow = d.PaletteWidth() == 112 ? "wide" : "0";
    if (d.ControlCenter(way.c_str(), x, y)) ev(T::MouseMove, x, y); // hover
  }
  if (frame == 8) {
    hover = d.Hovered() == way ? "1" : "0(" + d.Hovered() + ")";
    clickControl(way.c_str());
  }
  if (frame == 10 && way == "more") pick("정렬 ▸");
  if (frame == 10 && way == "align") pick("왼쪽 맞춤");
  if (frame == 12 && way == "more") pick("왼쪽 맞춤");
  if (frame == 14) {
    align = std::fabs(leftOf(a) - leftOf(b)) < 0.5f && leftOf(a) >= 0 ? "1" : std::format("0({} {})", leftOf(a), leftOf(b));
    bdg::GameUI::Rect r;
    if (d.ElementRect(c, r)) click(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 3); // right-click on the image
  }
  if (frame == 16) {
    const auto& items = e->editor->Menu();
    int found = 0;
    for (const auto& it : items) found += it.label == "복제" || it.label == "삭제" || it.label == "맨 앞으로" || it.label == "맨 뒤로" || it.label == "앵커 ▸";
    context = found == 5 && m->Selected() == c ? "" : std::format("0(items {} sel {})", found, m->Selected());
    pick("맨 뒤로");
  }
  if (frame == 18) {
    if (context.empty()) context = m->scene.IndexInParent(c) == 0 ? "1" : std::format("0(index {})", m->scene.IndexInParent(c));
    if (isNarrow) { // the wheel over the palette scrolls it
      float x, y;
      if (d.ControlCenter("pal.Panel", x, y) || d.ControlCenter("pal.Text", x, y)) {
        bdg::InputEvent w{T::Wheel, x * scale, y * scale};
        w.wheel = -3;
        e->window.InjectEvent(w);
      }
    }
  }
  if (frame == 20 && isNarrow && narrow.empty()) narrow = d.PaletteScroll() > 0 || d.PaletteOverflow() == 0 ? "1" : "0(no scroll)";
}
void ReportUdPolishTest(bdg_engine*) {
  using namespace udpolishtest;
  bdg::LogInfo("udpolish test: tips={} chips={} title={} align={} hover={} context={} narrow={}", tips, chips, title, align, hover, context, narrow);
}
BDG_EDITOR_TEST("BDG_UDPOLISH_TEST", InjectUdPolishTest, ReportUdPolishTest)

// BDG_TOKENS_TEST=1 (UI review B11): the screens lane A draws from token names (Scene view and its toolbar, gizmo,
// speed popup, UI Designer with two selected, an empty workspace, menus, the top toolbar) ask the theme only for
// names tokens.json has: a mistyped size or radius would fall back silently, a colour would turn magenta.
namespace tokenstest {
std::string missing = "-", drawn = "-";
int screens = 0;
}
void InjectTokensTest(bdg_engine* e, int frame, float scale) {
  using namespace tokenstest;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  bdg::Widget* ws = e->editor->Find("ws.tabs");
  auto clickWidget = [&](const char* id) {
    using T = bdg::InputEvent::Type;
    if (bdg::Widget* w = e->editor->Find(id)) {
      const float x = (w->x + w->w * 0.5f) * scale, y = (w->y + w->h * 0.5f) * scale;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    }
  };
  if (frame == 2) { // Level: a selected object (gizmo), the camera speed popup
    if (!m->scene.Roots().empty()) m->Select(m->scene.Roots().back());
    if (e->sceneView) e->sceneView->PressToolbar("speed", *m);
  }
  if (frame == 4) ++screens;
  if (frame == 5 && ws) ws->selectedIndex = 3; // UI Designer, two elements
  if (frame == 7) {
    const bdg::GameUI::Rect s = e->uiDesigner.Screen();
    e->uiDesigner.AddElement(*m, "Button", s.x + s.w * 0.3f, s.y + s.h * 0.3f);
    e->uiDesigner.AddElement(*m, "Toggle", s.x + s.w * 0.6f, s.y + s.h * 0.6f);
  }
  if (frame == 8) m->SetSelection(m->Selection().empty() ? std::vector<std::string>{} : std::vector<std::string>{m->Selection().back()});
  if (frame == 10) {
    ++screens;
    if (ws) ws->selectedIndex = 1; // an empty workspace
  }
  if (frame == 12) {
    ++screens;
    if (ws) ws->selectedIndex = 0;
    clickWidget("tool.timescale"); // a menu
  }
  if (frame == 14) {
    ++screens;
    e->editor->CloseMenu();
    clickWidget("menu.gameobject");
  }
  if (frame == 16) {
    ++screens;
    e->editor->CloseMenu();
    std::string list;
    for (const std::string& n : e->theme.Missing()) list += (list.empty() ? "" : " ") + n;
    missing = list.empty() ? "0" : list;
    drawn = std::format("{}", screens);
  }
}
void ReportTokensTest(bdg_engine*) {
  using namespace tokenstest;
  bdg::LogInfo("tokens test: missing={} screens={}", missing, drawn);
}
BDG_EDITOR_TEST("BDG_TOKENS_TEST", InjectTokensTest, ReportTokensTest)

// BDG_CONSOLE_TEST=1 (UI review B3): the Console shows the real log. Three probe lines appear (no sample lines); the
// 오류 toggle hides the error and counts stay (real clicks); typing in the search box narrows to one line; a
// double-click on "file:line" opens that file (a temp file); right-click > 복사 puts the line on the clipboard; 60
// more lines and the wheel scroll up, which stops following; Build > 검사만 fills the Build Log (channel "build").
namespace consoletest {
std::string lines = "-", filter = "-", search = "-", open = "-", copy = "-", scroll = "-", build = "-";
std::string probeFile;
}
void InjectConsoleTest(bdg_engine* e, int frame, float scale) {
  using namespace consoletest;
  using T = bdg::InputEvent::Type;
  if (!e->editor) return;
  bdg::ConsoleView& c = e->console;
  auto click = [&](float x, float y, int button = 1, int times = 1) {
    e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
    for (int k = 0; k < times; ++k) {
      e->window.InjectEvent({T::MouseDown, x * scale, y * scale, button});
      e->window.InjectEvent({T::MouseUp, x * scale, y * scale, button});
    }
  };
  auto clickWidget = [&](const char* id) {
    if (bdg::Widget* w = e->editor->Find(id)) click(w->x + w->w * 0.5f, w->y + w->h * 0.5f);
    else bdg::LogError("console test: no {}", id);
  };
  auto shownWith = [&](const std::string& part) {
    int n = 0;
    for (const std::string& s : c.ShownTexts()) n += s.find(part) != std::string::npos;
    return n;
  };
  auto rowOf = [&](const std::string& part, float& x, float& y) {
    const auto texts = c.ShownTexts();
    for (size_t k = 0; k < texts.size(); ++k)
      if (texts[k].find(part) != std::string::npos) {
        float w, h;
        if (c.RowRect(k, x, y, w, h)) {
          x += 60;
          y += h * 0.5f;
          return true;
        }
      }
    return false;
  };
  if (frame == 2) {
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("console");
    { // a project of its own, so Build > 검사만 below reads that folder (not the working folder)
      const std::filesystem::path proj = std::filesystem::temp_directory_path() / "bdg_console_project";
      std::error_code ec;
      std::filesystem::remove_all(proj, ec);
      std::filesystem::create_directories(proj / "Assets", ec);
      std::filesystem::create_directories(proj / "Scenes", ec);
      OpenProject(e, bdg::PathToU8(proj / "Assets"));
    }
    probeFile = bdg::PathToU8(std::filesystem::temp_directory_path() / "bdg_console_probe.cs");
    std::ofstream(bdg::PathU8(probeFile)) << "// console probe\n";
    bdg::LogInfo("console probe: info");
    bdg::LogWarn("console probe: warn");
    bdg::LogError("console probe: error at {}:12", probeFile);
  }
  if (frame == 4) {
    lines = shownWith("console probe:") == 3 && shownWith("quest.lua") == 0 ? "1" : std::format("0({})", shownWith("console probe:"));
    clickWidget("console.error"); // hide errors
  }
  if (frame == 6) {
    bdg::Widget* b = e->editor->Find("console.error");
    filter = shownWith("console probe: error") == 0 && shownWith("console probe: warn") == 1 && b && !b->selected && b->text.starts_with("오류 ")
                 ? "1" : std::format("0(error {} '{}')", shownWith("console probe: error"), b ? b->text : "");
    clickWidget("console.error"); // show them again
    if (bdg::Widget* f = e->editor->Find("console.search")) click(f->x + f->w * 0.8f, f->y + f->h * 0.5f); // in the box
  }
  if (frame == 8) {
    bdg::InputEvent t{T::Text};
    t.text = "probe: w";
    e->window.InjectEvent(t);
  }
  if (frame == 10) {
    search = c.ShownTexts().size() == 1 && shownWith("probe: warn") == 1 ? "1" : std::format("0({})", c.ShownTexts().size());
    bdg::InputEvent all{T::KeyDown}; // empty the box again as a user would: Ctrl+A, Backspace
    all.key = bdg::Key::A;
    all.ctrl = true;
    e->window.InjectEvent(all);
    bdg::InputEvent back{T::KeyDown};
    back.key = bdg::Key::Backspace;
    e->window.InjectEvent(back);
  }
  if (frame == 12) {
    float x, y;
    if (rowOf("console probe: error", x, y)) click(x, y, 1, 2); // a double-click
    else bdg::LogError("console test: error row not shown");
  }
  if (frame == 14) {
    open = "0"; // read from the log below
    for (const bdg::LogLine& l : bdg::LogHistory())
      if (l.text.starts_with("console: open ") && l.text.find("bdg_console_probe.cs:12") != std::string::npos) open = "1";
    float x, y;
    if (rowOf("console probe: warn", x, y)) click(x, y, 3);; // right-click
  }
  if (frame == 16) {
    const auto& items = e->editor->Menu();
    float x, y, w, h;
    for (size_t k = 0; k < items.size(); ++k)
      if (items[k].label == "복사" && e->editor->MenuItemRect(k, x, y, w, h)) click(x + w * 0.5f, y + h * 0.5f);
  }
  if (frame == 18) {
    copy = e->editor->Clipboard() == "console probe: warn" ? "1" : "0(" + e->editor->Clipboard() + ")";
    for (int k = 0; k < 60; ++k) bdg::LogInfo("console filler {}", k);
  }
  if (frame == 20) {
    const bool followed = c.Following();
    if (bdg::Widget* l = e->editor->Find("console.list")) { // the wheel over the list, up
      bdg::InputEvent wh{T::Wheel, (l->x + l->w * 0.5f) * scale, (l->y + l->h * 0.5f) * scale};
      wh.wheel = 4;
      e->window.InjectEvent(wh);
    }
    scroll = followed ? "" : "0(not following)";
  }
  if (frame == 22) {
    if (scroll.empty()) scroll = !c.Following() && c.Scroll() > 0 ? "1" : std::format("0(follow {} scroll {})", c.Following(), c.Scroll());
    if (bdg::Widget* b = e->editor->Find("menu.build")) click(b->x + b->w * 0.5f, b->y + b->h * 0.5f);
  }
  if (frame == 24) {
    const auto& items = e->editor->Menu();
    float x, y, w, h;
    for (size_t k = 0; k < items.size(); ++k)
      if (items[k].label == "검사만" && e->editor->MenuItemRect(k, x, y, w, h)) click(x + w * 0.5f, y + h * 0.5f);
  }
  if (frame == 27) {
    bdg::Widget* panel = e->editor->Find("buildlog");
    const auto texts = e->buildLog.ShownTexts();
    bool check = false, other = false;
    for (const std::string& s : texts) {
      check |= s.starts_with("build: check");
      other |= s.find("console probe") != std::string::npos;
    }
    build = check && !other && panel && !panel->hidden ? "1" : std::format("0(lines {} check {} shown {})", texts.size(), check, panel && !panel->hidden);
    std::error_code ec;
    std::filesystem::remove(bdg::PathU8(probeFile), ec);
  }
}
void ReportConsoleTest(bdg_engine*) {
  using namespace consoletest;
  bdg::LogInfo("console test: lines={} filter={} search={} open={} copy={} scroll={} build={}", lines, filter, search, open, copy, scroll, build);
}
BDG_EDITOR_TEST("BDG_CONSOLE_TEST", InjectConsoleTest, ReportConsoleTest)

// BDG_NARROW_TEST=1 (UI review B9 / U7), two ways:
//   - a narrow window (under 1000 units, e.g. BDG_UI_SCALE=2.5): the Hierarchy is stacked with the Inspector (the
//     three columns cannot keep their minimum widths), the top toolbar fits (speed, badges and Build in "»", whose
//     list has 빌드 — a real click), the scene title fits, vector fields put their numbers under the label;
//   - a wide window (scale 1): the splits are pushed to their ends and the panels keep their minimum widths, the
//     Hierarchy rows are under 220 (type column dropped, names cut with "…"), the Inspector's vector fields stack and
//     its button rows wrap onto a second line.
namespace narrowtest {
std::string mode = "-", a = "-", b = "-", c = "-", d = "-", e2 = "-";
}
void InjectNarrowTest(bdg_engine* e, int frame, float scale) {
  using namespace narrowtest;
  using T = bdg::InputEvent::Type;
  if (!e->editor || !e->model) return;
  bdg::UIContext& ui = *e->editor;
  bdg::Widget* dw = ui.Find("dock");
  if (!dw || !dw->dock) return;
  bdg::DockState& dock = *dw->dock;
  float lw = 0;
  if (bdg::Widget* root = ui.Root()) lw = root->w;
  std::function<void(bdg::Widget&, const std::function<void(bdg::Widget&)>&)> walk = [&](bdg::Widget& w, const std::function<void(bdg::Widget&)>& f) {
    if (w.hidden) return;
    f(w);
    for (auto& ch : w.children) walk(*ch, f);
  };
  auto stackedVector = [&] { // a Transform vector field on two lines
    bool found = false;
    if (bdg::Widget* insp = ui.Find("inspector"))
      walk(*insp, [&](bdg::Widget& w) { found |= w.kind == bdg::WidgetKind::Field && w.axes > 0 && w.h > 40; });
    return found;
  };
  if (frame == 2) {
    e->model->Select("g6"); // the Player: a Transform and component cards in the Inspector
    mode = lw < 1000 ? "narrow" : "wide";
    if (mode == "wide") { // push the splits: Hierarchy and Inspector to their minimum widths
      bdg::DockNode& root = dock.nodes[(size_t)dock.root];
      root.ratio = 0;
      if (root.b >= 0) dock.nodes[(size_t)root.b].ratio = 1;
    }
  }
  if (frame == 5 && mode == "narrow") {
    a = dock.FindLeaf("hierarchy") == dock.FindLeaf("inspector") ? "1" : "0"; // stacked
    bool fits = true;
    for (const char* id : {"ws.tabs", "tool.play", "tool.pause", "tool.more"})
      if (bdg::Widget* w = ui.Find(id); !w || w->hidden || w->x + w->w > lw + 0.5f) {
        fits = false;
        bdg::LogError("narrow test: {} {}", id, w ? std::format("x {} w {} lw {}", w->x, w->w, lw) : "missing");
      }
    for (const char* id : {"tool.build", "tool.quality", "tool.platform", "tool.timescale"})
      if (bdg::Widget* w = ui.Find(id); w && !w->hidden) fits = false;
    b = fits ? "1" : "0"; // toolbar
    bdg::Widget* title = ui.Find("title.scene");
    c = title && title->x + ui.Measure(title->fontSize > 0 ? title->fontSize : 12, title->text) <= lw + 0.5f ? "1" : "0"; // title
    d = stackedVector() ? "1" : "0";
    if (bdg::Widget* more = ui.Find("tool.more")) {
      e->window.InjectEvent({T::MouseMove, (more->x + more->w * 0.5f) * scale, (more->y + more->h * 0.5f) * scale});
      e->window.InjectEvent({T::MouseDown, (more->x + more->w * 0.5f) * scale, (more->y + more->h * 0.5f) * scale, 1});
      e->window.InjectEvent({T::MouseUp, (more->x + more->w * 0.5f) * scale, (more->y + more->h * 0.5f) * scale, 1});
    }
  }
  if (frame == 7 && mode == "narrow") {
    bool build = false;
    for (const auto& it : ui.Menu()) build |= it.label == "빌드";
    e2 = build ? "1" : "0"; // the "»" list
    ui.CloseMenu();
  }
  if (frame == 5 && mode == "wide") {
    bdg::Widget* hier = ui.Find("hierarchy");
    bdg::Widget* insp = ui.Find("inspector");
    const bool kept = hier && insp && hier->w >= 177 && insp->w >= 277 /* the group minus its 1-unit borders */ && hier->w < 220;
    float rowW = 1e9f;
    if (hier) walk(*hier, [&](bdg::Widget& w) { if (w.kind == bdg::WidgetKind::ListRow) rowW = std::min(rowW, w.w); });
    const std::string cut = ui.FitText(12.5f, "Main Camera", 40);
    a = kept && rowW < 220 && cut.ends_with("…") && ui.Measure(12.5f, cut) <= 40 ? "1"
        : std::format("0(hier {} insp {} row {} cut '{}')", hier ? hier->w : 0, insp ? insp->w : 0, rowW, cut);
    b = stackedVector() ? "1" : "0";
    bool wrapped = false; // a wrapping button row on two lines
    if (insp)
      walk(*insp, [&](bdg::Widget& w) {
        if (!w.wrap || w.children.size() < 2) return;
        const float y0 = w.children.front()->y;
        for (auto& ch : w.children)
          if (!ch->hidden && ch->y > y0 + 4) wrapped = true;
      });
    c = wrapped ? "1" : "0";
  }
}
void ReportNarrowTest(bdg_engine*) {
  using namespace narrowtest;
  if (mode == "narrow") bdg::LogInfo("narrow test: mode=narrow stacked={} toolbar={} title={} vector={} more={}", a, b, c, d, e2);
  else bdg::LogInfo("narrow test: mode=wide rows={} vector={} wrap={}", a, b, c);
}
BDG_EDITOR_TEST("BDG_NARROW_TEST", InjectNarrowTest, ReportNarrowTest)
