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
