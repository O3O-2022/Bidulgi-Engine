// Scripted editor checks: the editor's own UI fixes from the UI review (Docs/planning/ui-review-2026-11-26.md, lane A
// batches). Included by Engine.cpp after uidesigner.inl.

// BDG_MENUBAR_TEST=1 (UI review B2): the menu bar with real clicks. Every menu opens a list; Save shows its shortcut;
// GameObject > 3D 도형 > Cube adds a cube (one undo step, selected); Edit > 실행 취소 takes it away; Window > 패널 열기
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
  if (frame == 34) pick("Cube");
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
  if (frame == 49) pick("컴포넌트 추가… ▸");
  if (frame == 50) { // the list is searchable: type to narrow it, as a user would
    bdg::InputEvent t{T::Text};
    t.text = "AudioS";
    e->window.InjectEvent(t);
  }
  if (frame == 52) pick("AudioSource");
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
