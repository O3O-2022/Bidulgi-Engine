// Scripted editor checks: scene (moved out of Engine.cpp). Included by Engine.cpp just before the
// C API, in the same translation unit: bdg_engine and every editor helper are visible here.
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h) instead of editing Engine.cpp.

// Scripted docking check (BDG_DOCK_TEST=1, editor screen): drag the hierarchy splitter 100 units right,
// then drag the Console tab onto the right edge of the Scene panel.
void InjectDockTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::Widget* dw = e->editor->Find("dock");
  if (!dw || !dw->dock) return;
  bdg::DockState& d = *dw->dock;
  auto ev = [&](T type, float x, float y) { e->window.InjectEvent({type, x * scale, y * scale, type == T::MouseMove ? 0 : 1}); };
  if (frame == 2) {
    const bdg::DockRect& r = d.nodes[d.FindLeaf("hierarchy")].rect;
    e->dockTestW0 = r.w;
    float sx = r.x + r.w + bdg::DockState::kGap * 0.5f, sy = r.y + r.h * 0.5f;
    ev(T::MouseMove, sx, sy); ev(T::MouseDown, sx, sy); ev(T::MouseMove, sx + 100, sy); ev(T::MouseUp, sx + 100, sy);
  }
  if (frame == 5) {
    const bdg::DockRect& tab = d.tabRects["console"];
    const bdg::DockRect& scene = d.nodes[d.FindLeaf("scene")].rect;
    float tx = tab.x + tab.w * 0.5f, ty = tab.y + tab.h * 0.5f, gx = scene.x + scene.w * 0.9f, gy = scene.y + scene.h * 0.5f;
    ev(T::MouseMove, tx, ty); ev(T::MouseDown, tx, ty); ev(T::MouseMove, tx + 10, ty); ev(T::MouseMove, gx, gy); ev(T::MouseUp, gx, gy);
  }
}

// Scripted command bus check (BDG_CMD_TEST=1, editor screen): type a new Transform.position into the
// Inspector, Ctrl+Z, Ctrl+Y, then change Health.maxHealth. Values are logged at the end of the run.
void InjectCommandTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  if (!e->model) return;
  auto position = [&] {
    std::string v;
    bdg::SceneObject* o = e->model->scene.Find("g6");
    if (bdg::Component* c = o ? o->Find("Transform") : nullptr) bdg::Registry::Get(c->data, *c->type, "position", v);
    return v;
  };
  auto key = [&](bdg::Key k, bool ctrl) { bdg::InputEvent ev{T::KeyDown}; ev.key = k; ev.ctrl = ctrl; e->window.InjectEvent(ev); };
  auto type = [&](const char* id, const char* text) {
    bdg::Widget* f = e->editor->Find(id);
    if (!f) { bdg::LogError("cmd test: no widget {}", id); return; }
    float x = (f->x + f->w - 20) * scale, y = (f->y + f->h * 0.5f) * scale;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
    key(bdg::Key::A, true);
    bdg::InputEvent t{T::Text};
    t.text = text;
    e->window.InjectEvent(t);
    key(bdg::Key::Enter, false);
  };
  if (frame == 2) type("f.Transform.position", "3, 4, 5");
  if (frame == 4) { e->cmdTestValues[0] = position(); key(bdg::Key::Z, true); }
  if (frame == 6) { e->cmdTestValues[1] = position(); key(bdg::Key::Y, true); }
  if (frame == 7) // a narrow Inspector (B9: stacked vectors) may have it below the fold
    if (bdg::Widget* f = e->editor->Find("f.Health.maxHealth")) e->editor->ScrollIntoView(*f);
  if (frame == 8) { e->cmdTestValues[2] = position(); type("f.Health.maxHealth", "250"); }
  if (frame == 12) { // History panel: click the first step (only the position edit stays applied)
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("history");
  }
  if (frame == 14)
    if (bdg::Widget* row = e->editor->Find("history.1")) {
      float x = (row->x + row->w * 0.5f) * scale, y = (row->y + row->h * 0.5f) * scale;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    }
  if (frame == 10) // select another object in the Hierarchy: the Inspector rebuilds for it
    if (bdg::Widget* row = e->editor->Find("h.g9")) {
      float x = (row->x + row->w * 0.5f) * scale, y = (row->y + row->h * 0.5f) * scale;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    }
}

// Scripted check of 2-6 (BDG_NEWSCENE_TEST=1, BDG_SCENE_FILE in a scratch folder): Ctrl+N gives the basic 3D
// scene (five objects, fresh history), the Game view camera looks at the ground, the ground has the grid, fog
// turns the far ground toward the fog colour, a lower exposure darkens the picture, and Ctrl+S writes the new file.
void InjectNewSceneTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->sceneView) return;
  auto* v = e->newSceneTestValues;
  auto key = [&](bdg::Key k, bool ctrl) {
    bdg::InputEvent ev{bdg::InputEvent::Type::KeyDown};
    ev.key = k;
    ev.ctrl = ctrl;
    e->window.InjectEvent(ev);
  };
  // ground far (upper) and near (lower), left of the centre (the camera icon and gizmo sit there); drawable
  // pixels, bottom-up rows
  auto sample = [&](float fy) {
    bdg::Widget* sw = e->editor->Find("scene");
    if (!sw) return glm::vec3(0);
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    std::vector<uint8_t> px;
    e->rhi.ReadPixels(w, h, px);
    const float s = e->editor->Scale();
    glm::vec3 sum(0);
    int n = 0;
    for (int dy = -3; dy <= 3; ++dy)
      for (int dx = -3; dx <= 3; ++dx) {
        const int x = (int)((sw->x + sw->w * 0.25f) * s) + dx, y = h - 1 - ((int)((sw->y + sw->h * fy) * s) + dy);
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        const uint8_t* p = &px[((size_t)y * w + x) * 4];
        sum += glm::vec3(p[0], p[1], p[2]);
        ++n;
      }
    return n ? sum / (float)n : sum;
  };
  auto set = [&](const char* path, const std::string& value) {
    bdg::Command c;
    c.name = "set";
    c.target = "world";
    c.args = std::format(R"({{"path":"{}","value":{}}})", path, value);
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("new scene test: {}", err);
  };
  static glm::vec3 far0, near0;
  if (frame == 2) key(bdg::Key::N, true);
  if (frame == 4) {
    const char* names[] = {"Main Camera", "Sun", "Sky", "World Settings", "Ground"};
    bool ok = m->scene.ObjectCount() == 5 && m->bus.UndoDepth() == 0;
    const auto& roots = m->scene.Roots();
    for (size_t i = 0; ok && i < 5; ++i) ok = i < roots.size() && m->scene.Find(roots[i])->name == names[i];
    v[0] = std::format("{}", ok && m->scenePath.ends_with("New Scene.scene") ? 1 : 0);
    // the camera looks at the origin: its forward (-Z turned by its rotation) points from it to (0, 0, 0)
    bdg::SceneObject* cam = m->scene.Find("cam");
    bdg::Component* t = cam ? cam->Find("Transform") : nullptr;
    if (t) {
      const bdg::Transform& tr = *static_cast<const bdg::Transform*>(t->data);
      const glm::vec3 fwd = bdg::space::EulerDegreesToQuat(tr.rotation) * glm::vec3(0, 0, -1);
      const glm::vec3 to = glm::normalize(-bdg::space::ToGlm(tr.position));
      v[1] = std::format("{}", glm::dot(fwd, to) > 0.999f ? 1 : 0);
    }
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("scene");
    e->sceneView->camera.pivot = {0, 0, 0}; // a fixed editor view of the ground
    m->Select("ground");
    e->sceneView->Frame(*m);
  }
  if (frame == 8) {
    far0 = sample(0.35f);
    near0 = sample(0.85f);
    { // the grid texture: along a row of the near ground the brightness changes (lines); a plain colour would not
      bdg::Widget* sw = e->editor->Find("scene");
      int32_t w = 0, h = 0;
      e->window.GetDrawableSize(w, h);
      std::vector<uint8_t> px;
      e->rhi.ReadPixels(w, h, px);
      const float s = e->editor->Scale();
      int lo = 999, hi = -1;
      if (sw) {
        const int y = h - 1 - (int)((sw->y + sw->h * 0.8f) * s);
        for (int x = (int)((sw->x + sw->w * 0.3f) * s); x < (int)((sw->x + sw->w * 0.7f) * s); ++x) {
          if (x < 0 || x >= w || y < 0 || y >= h) continue;
          const uint8_t* p = &px[((size_t)y * w + x) * 4];
          const int l = (p[0] + p[1] + p[2]) / 3;
          lo = std::min(lo, l);
          hi = std::max(hi, l);
        }
      }
      v[2] = std::format("{}", hi - lo > 6 ? 1 : 0);
      if (v[2] != "1") bdg::LogError("new scene test: ground row {}..{}", lo, hi);
    }
    set("WorldSettings.fog", "true");
    set("WorldSettings.fogColor", "[1,0,0,1]");
    set("WorldSettings.fogDensity", "0.5");
  }
  if (frame == 11) {
    const glm::vec3 far1 = sample(0.35f);
    v[3] = std::format("{}", far1.r > far0.r + 40 && far1.g < far0.g - 20 ? 1 : 0);
    if (v[3] != "1") bdg::LogError("new scene test: fog far {} {} {} -> {} {} {}", far0.r, far0.g, far0.b, far1.r, far1.g, far1.b);
    set("WorldSettings.fog", "false");
    set("WorldSettings.exposure", "0.3");
  }
  if (frame == 14) {
    const glm::vec3 near1 = sample(0.85f);
    const float l0 = near0.r + near0.g + near0.b, l1 = near1.r + near1.g + near1.b;
    v[4] = std::format("{}", l1 < l0 * 0.75f ? 1 : 0); // darker
    if (v[4] != "1") bdg::LogError("new scene test: exposure {} -> {}", l0, l1);
    key(bdg::Key::S, true);
  }
  if (frame == 16) {
    std::string text;
    std::ifstream in(std::filesystem::path(std::u8string((const char8_t*)m->scenePath.c_str())), std::ios::binary);
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    v[5] = std::format("{}", text.find("\"World Settings\"") != std::string::npos && text.find("bdg:Grid") != std::string::npos ? 1 : 0);
  }
}

void InjectSceneIconTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  auto* v = e->sceneIconTestValues;
  const float s = e->editor->Scale();
  auto clickAt = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * s, y * s});
    e->window.InjectEvent({T::MouseDown, x * s, y * s, 1});
    e->window.InjectEvent({T::MouseUp, x * s, y * s, 1});
  };
  auto patch = [&](const char* id) { // average colour around an object's icon, as drawn last frame
    const bdg::SceneObject* o = m->scene.Find(id);
    glm::vec3 sum(0);
    if (!o) return sum;
    float x = 0, y = 0;
    if (!sv->ProjectUI(glm::vec3(m->scene.WorldMatrix(id)[3]), x, y)) return sum;
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    std::vector<uint8_t> px;
    e->rhi.ReadPixels(w, h, px);
    int n = 0;
    for (int dy = -6; dy <= 6; ++dy)
      for (int dx = -6; dx <= 6; ++dx) {
        const int X = (int)(x * s) + dx, Y = h - 1 - ((int)(y * s) + dy);
        if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
        const uint8_t* p = &px[((size_t)Y * w + X) * 4];
        sum += glm::vec3(p[0], p[1], p[2]);
        ++n;
      }
    return n ? sum / (float)n : sum;
  };
  static glm::vec3 on;
  static int linesBefore = -1;
  if (frame == 2) {
    auto has = [&](const char* id, const char* icon) {
      for (auto& [i, n] : sv->IconList())
        if (i == id) return n == icon;
      return false;
    };
    v[0] = std::format("{}", has("g1", "comp-camera") && has("g2", "comp-sun") && has("g9", "comp-light") && sv->iconSize == 2 ? 1 : 0);
    on = patch("g9");
    sv->iconSize = 0;
  }
  if (frame == 4) {
    const glm::vec3 off = patch("g9");
    v[1] = std::format("{}", glm::length(on - off) > 20 ? 1 : 0);
    sv->iconSize = 2;
    linesBefore = sv->GizmoLines(); // the Player is selected: no shape
    m->Select("g1");
  }
  if (frame == 6) {
    // 12 frustum edges; from this view part of the 25 m far plane lies behind the editor camera and is cut away
    v[2] = std::format("{}", linesBefore == 0 && sv->GizmoLines() >= 8 ? 1 : 0);
    if (v[2] != "1") bdg::LogError("scene icon test: frustum lines {}", sv->GizmoLines());
    m->Select("g9");
  }
  if (frame == 8) {
    v[3] = std::format("{}", sv->GizmoLines() >= 60 ? 1 : 0); // three circles of 32 segments
    if (v[3] != "1") bdg::LogError("scene icon test: range lines {}", sv->GizmoLines());
    sv->PressToolbar("show", *m); // the toolbar's Show menu holds the icon sizes (round 7)
  }
  if (frame == 9) { // pick "Icons: L" from the list
    float x, y, w, h;
    const auto& items = e->editor->Menu();
    bool picked = false;
    for (size_t k = 0; k < items.size() && !picked; ++k)
      if (items[k].label == "Icons: L" && e->editor->MenuItemRect(k, x, y, w, h)) {
        clickAt(x + w * 0.5f, y + h * 0.5f);
        picked = true;
      }
    if (!picked) bdg::LogError("scene icon test: the Show menu did not open");
  }
  if (frame == 10) {
    v[4] = std::format("{}", sv->iconSize == 3 ? 1 : 0);
    float x, y;
    if (sv->ProjectUI(glm::vec3(m->scene.WorldMatrix("g2")[3]), x, y)) clickAt(x, y);
  }
  if (frame == 12) v[5] = std::format("{}", m->Selected() == "g2" ? 1 : 0);
}

// Scripted check of E-2 (BDG_TREE_TEST=1, BDG_FOLDS = a scratch file): guide lines on the sample scene's rows,
// a click on Player's arrow folds it without changing the selection, the drawn guide line, picking the hidden
// Weapon opens the way to it, Alt+click folds and opens a branch with everything under it, folds are saved.
void InjectTreeTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto* v = e->treeTestValues;
  const float s = e->editor->Scale();
  auto arrow = [&](const char* id, bool alt) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w) { bdg::LogError("tree test: no row {}", id); return; }
    const float x = (w->x + e->editor->Tokens().N("space-5", 10) + w->tree.size() * 14.f + 5) * s, y = (w->y + w->h * 0.5f) * s;
    bdg::InputEvent dn{T::MouseDown, x, y, 1}, up{T::MouseUp, x, y, 1};
    dn.alt = up.alt = alt;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent(dn);
    e->window.InjectEvent(up);
  };
  auto tree = [&](const char* id) {
    bdg::Widget* w = e->editor->Find(id);
    return w ? std::vector<uint8_t>(w->tree) : std::vector<uint8_t>{9};
  };
  auto fileText = [&] {
    std::ifstream in(std::filesystem::path(m->foldsPath), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  static std::string selected0;
  if (frame == 2) {
    bdg::Widget* g6 = e->editor->Find("h.g6");
    v[0] = std::format("{}", tree("h.g1") == std::vector<uint8_t>{2} && tree("h.g7") == std::vector<uint8_t>{1, 3} &&
                                     tree("h.g9") == std::vector<uint8_t>{3} && g6 && g6->fold == 0 ? 1 : 0);
    selected0 = m->Selected();
    arrow("h.g6", false);
  }
  if (frame == 4) {
    bdg::Widget* g6 = e->editor->Find("h.g6");
    v[1] = std::format("{}", !e->editor->Find("h.g7") && g6 && g6->fold == 1 && m->Selected() == selected0 &&
                                     fileText().find("\"g6\"") != std::string::npos ? 1 : 0);
    // the guide line beside the Crystal row (a branch with more siblings below), as drawn last frame
    bool line = false;
    if (bdg::Widget* row = e->editor->Find("h.g8")) {
      int32_t w = 0, h = 0;
      e->window.GetDrawableSize(w, h);
      std::vector<uint8_t> px;
      e->rhi.ReadPixels(w, h, px);
      const bdg::Color c = e->theme.C("line-strong");
      const int x = (int)std::lround((row->x + e->editor->Tokens().N("space-5", 10) + 6.5f) * s - 0.5f);
      for (int dx = -1; dx <= 1 && !line; ++dx) {
        const int px0 = x + dx, py = h - 1 - (int)((row->y + 3) * s);
        if (px0 < 0 || px0 >= w || py < 0 || py >= h) continue;
        const uint8_t* p = &px[((size_t)py * w + px0) * 4];
        line = std::abs(p[0] - c.r * 255) < 30 && std::abs(p[1] - c.g * 255) < 30 && std::abs(p[2] - c.b * 255) < 30;
      }
    }
    v[2] = std::format("{}", line ? 1 : 0);
    m->Select("g7"); // as a Scene view pick would
  }
  if (frame == 6) {
    v[3] = std::format("{}", e->editor->Find("h.g7") && !m->Folded("g6") ? 1 : 0);
    bdg::Command add;
    add.name = "add";
    add.args = R"({"id":"t1","name":"Tip","parent":"g7"})";
    std::string err;
    if (!m->bus.Execute(add, &err)) bdg::LogError("tree test: {}", err);
  }
  if (frame == 8) arrow("h.g6", true);
  if (frame == 10) {
    v[4] = std::format("{}", m->Folded("g6") && m->Folded("g7") && !e->editor->Find("h.g7") ? 1 : 0);
    arrow("h.g6", true);
  }
  if (frame == 12) {
    v[5] = std::format("{}", !m->Folded("g6") && !m->Folded("g7") && e->editor->Find("h.t1") ? 1 : 0);
    m->SetFolded("g7", true);
    m->LoadFolds(); // what the next start reads
    v[6] = std::format("{}", m->Folded("g7") && !m->Folded("g6") ? 1 : 0);
  }
}

// Scripted Scene view check (BDG_SCENEVIEW_TEST=1, queue 3-2): the sample scene drawn in 3D, click picking, the
// selection outline, orbit / zoom / frame / pan / fly camera moves, a click on empty space, a light icon click.
void InjectSceneViewTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->sceneViewTestValues;
  auto ev = [&](T type, float x, float y, int button = 0, bool alt = false) {
    bdg::InputEvent i{type, x * scale, y * scale, button};
    i.alt = alt;
    e->window.InjectEvent(i);
  };
  auto key = [&](T type, bdg::Key k) { bdg::InputEvent i{type}; i.key = k; e->window.InjectEvent(i); };
  auto at = [&](const char* id, float& x, float& y) {
    glm::vec3 c;
    float r;
    return sv->ObjectCenter(*m, id, c, r) && sv->ProjectUI(c, x, y);
  };
  static float cx, cy, yaw0, dist0;
  static decltype(sv->camera) beforeFly;
  static glm::vec3 pivot0, pos0;
  if (frame == 2) { // the Crystal (g8) must be on screen and drawn (not the background colour)
    sv->frameEaseSeconds = 0; // F and fly react at once here: checked frames run back to back (tiny dt)
    sv->flyEasing = false;
    int32_t w, h;
    std::vector<uint8_t> px;
    sv->ReadPixels(w, h, px);
    float x, y, ux = 0, uy = 0;
    bool ok = at("g8", x, y);
    glm::vec3 c;
    float r;
    sv->ObjectCenter(*m, "g8", c, r);
    float pxX = 0, pxY = 0;
    ok = ok && bdg::ProjectToPixel(sv->camera.ToCamera(), w, h, c, pxX, pxY) && pxX >= 0 && pxY >= 0 && pxX < w && pxY < h;
    if (ok) {
      const uint8_t* p = &px[((size_t)pxY * w + (size_t)pxX) * 4];
      ok = !(std::abs(p[0] - 43) <= 2 && std::abs(p[1] - 48) <= 2 && std::abs(p[2] - 56) <= 2); // not the clear colour
    }
    v[0] = std::format("{}", ok ? 1 : 0);
    (void)ux; (void)uy;
    cx = x; cy = y;
    ev(T::MouseMove, cx, cy);
    ev(T::MouseDown, cx, cy, 1);
    ev(T::MouseUp, cx, cy, 1);
  }
  if (frame == 4) { // picked, and orange outline pixels around it
    v[1] = m->Selected();
    int32_t w, h;
    std::vector<uint8_t> px;
    sv->ReadPixels(w, h, px);
    int orange = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4) orange += px[i] > 230 && px[i + 1] > 130 && px[i + 1] < 170 && px[i + 2] < 60;
    v[2] = std::format("{}", orange > 20 && sv->OutlinedCount() > 0 ? 1 : 0);
    yaw0 = sv->camera.yaw;
    ev(T::MouseDown, cx, cy, 1, true); // Alt + left drag: orbit
    ev(T::MouseMove, cx + 100, cy, 0, true);
    ev(T::MouseUp, cx + 100, cy, 1, true);
  }
  if (frame == 6) {
    v[3] = std::format("{}", std::fabs(sv->camera.yaw - yaw0 + 30.f) < 0.5f ? 1 : 0); // 100 units x 0.3 degrees
    dist0 = sv->camera.distance;
    bdg::InputEvent wh{T::Wheel, cx * scale, cy * scale};
    wh.wheel = 3;
    e->window.InjectEvent(wh);
  }
  if (frame == 8) {
    v[4] = std::format("{}", sv->camera.distance < dist0 * 0.75f ? 1 : 0);
    ev(T::MouseMove, cx, cy);
    key(T::KeyDown, bdg::Key::F); // frame the Crystal
  }
  if (frame == 10) {
    glm::vec3 c;
    float r;
    sv->ObjectCenter(*m, "g8", c, r);
    v[5] = std::format("{}", glm::distance(sv->camera.pivot, c) < 0.01f ? 1 : 0);
    pivot0 = sv->camera.pivot;
    float x = cx, y = cy; // middle drag: pan
    ev(T::MouseDown, x, y, 2);
    ev(T::MouseMove, x + 50, y + 20, 0);
    ev(T::MouseUp, x + 50, y + 20, 2);
  }
  if (frame == 12) {
    v[6] = std::format("{}", glm::distance(sv->camera.pivot, pivot0) > 0.05f ? 1 : 0);
    pos0 = sv->camera.Position();
    beforeFly = sv->camera;
    ev(T::MouseDown, cx, cy, 3); // right button held + W: fly forward
    key(T::KeyDown, bdg::Key::W);
  }
  if (frame == 16) {
    key(T::KeyUp, bdg::Key::W);
    ev(T::MouseUp, cx, cy, 3);
  }
  if (frame == 18) {
    glm::vec3 moved = sv->camera.Position() - pos0;
    v[7] = std::format("{}", glm::length(moved) > 0.001f && glm::dot(glm::normalize(moved), sv->camera.Forward()) > 0.99f ? 1 : 0);
    // The fly distance is speed x real time: on slow software GL (CI, ~300 ms frames) the 4 held frames carry the
    // camera into the Crystal, which then covers the "sky". Only the direction is checked above, so the next steps
    // start again from the camera before the fly, the same on every machine.
    sv->camera = beforeFly;
    float x = 0, y = 0; // empty sky: the top centre of the view
    if (bdg::Widget* s = e->editor->Find("scene")) { x = s->x + s->w * 0.5f; y = s->y + 40; }
    if (const std::string under = sv->Pick(*m, x, y); !under.empty()) bdg::LogError("sceneview test: the sky point {:.0f},{:.0f} is on {}", x, y, under);
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    ev(T::MouseUp, x, y, 1);
  }
  if (frame == 20) {
    v[8] = std::format("{}", m->Selection().empty() ? 1 : 0);
    sv->Frame(*m); // whole scene
  }
  if (frame == 22) { // the Lantern Light (g9) has no surface: its icon is clickable
    float x, y;
    if (sv->ProjectUI(m->scene.WorldMatrix("g9")[3], x, y)) {
      ev(T::MouseMove, x, y);
      ev(T::MouseDown, x, y, 1);
      ev(T::MouseUp, x, y, 1);
    }
  }
  if (frame == 24) v[9] = m->Selected();
}

void InjectPlayTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->sceneView) return;
  auto* v = e->playTestValues;
  auto run = [&](const char* name, const char* target, const char* args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("play test: {} {}: {}", name, target, err);
  };
  auto transform = [&](const char* id) -> bdg::Transform* {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("Transform") : nullptr;
    return c ? static_cast<bdg::Transform*>(c->data) : nullptr;
  };
  auto health = [&]() -> int {
    bdg::SceneObject* o = m->scene.Find("g6");
    bdg::Component* c = o ? o->Find("Health") : nullptr;
    return c ? static_cast<const bdg::Health*>(c->data)->current : -1;
  };
  auto click = [&](const char* id) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w || w->hidden) { bdg::LogError("play test: {} is not clickable", id); return; }
    const float s = e->editor->Scale();
    e->window.InjectEvent({T::MouseMove, (w->x + w->w * 0.5f) * s, (w->y + w->h * 0.5f) * s});
    e->window.InjectEvent({T::MouseDown, (w->x + w->w * 0.5f) * s, (w->y + w->h * 0.5f) * s, 1});
    e->window.InjectEvent({T::MouseUp, (w->x + w->w * 0.5f) * s, (w->y + w->h * 0.5f) * s, 1});
  };
  static bdg::Vec3 rot0, pos0;
  static size_t depth0;
  static uint64_t game0;
  if (frame == 2) {
    run("addcomp", "g8", R"({"type":"Rotator","value":{"speed":[0,90,0]}})");
    rot0 = transform("g8")->rotation;
    pos0 = transform("g8")->position;
    depth0 = m->bus.UndoDepth();
    game0 = e->sceneView->GameFrames();
    TogglePlay(e);
    v[0] = std::format("{}", e->play.Playing() && m->playing && m->bus.UndoDepth() == 0 ? 1 : 0);
  }
  static uint64_t frames0;
  static double time0;
  if (frame == 4) { // 3-6b: pause holds the game
    v[1] = std::format("{}", transform("g8")->rotation.y != rot0.y ? 1 : 0); // it ran before the pause
    SetPlayPaused(e, true);
    frames0 = e->play.Frames();
    time0 = e->play.Time();
  }
  if (frame == 6) {
    // the frame drawn while paused has the paused (warn) border; the game did not move
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    std::vector<uint8_t> px;
    e->rhi.ReadPixels(w, h, px);
    const uint8_t* p = &px[((size_t)(h / 2) * w + 3) * 4];
    const bdg::Color warn = e->theme.C("warn");
    const bool border = std::abs(p[0] - warn.r * 255) < 40 && std::abs(p[1] - warn.g * 255) < 40 && std::abs(p[2] - warn.b * 255) < 40;
    v[8] = std::format("{}", e->play.Frames() == frames0 && e->play.Paused() ? 1 : 0);
    v[11] = std::format("{}", border ? 1 : 0);
    StepPlay(e);
  }
  if (frame == 7) { // exactly one frame of 1/60 s, then still paused
    v[9] = std::format("{}", e->play.Frames() == frames0 + 1 && std::abs(e->play.Time() - time0 - bdg::PlayMode::kStepSeconds) < 1e-9 &&
                                     e->play.Paused() ? 1 : 0);
    SetPlayPaused(e, false);
    if (bdg::Widget* f = e->editor->Find("tool.timescale")) {
      SetTimeScale(e, 9); // clamped to 4x
      v[10] = std::format("{}", e->play.TimeScale() == 4.f && f->text == "4×" ? 1 : 0);
      SetTimeScale(e, 1);
    }
  }
  if (frame == 8) {
    run("set", "g8", R"({"path":"Transform.position","value":[3,2,2]})");
    run("set", "g6", R"({"path":"Health.current","value":10})");
    run("set", "g6", R"({"path":"Health.maxHealth","value":500})");
    std::string err;
    m->bus.Undo(&err); // maxHealth back to 100: undo works inside play mode
  }
  if (frame == 9) { // the Inspector marks what was changed while playing (the undone maxHealth is not)
    bdg::Widget* cur = e->editor->Find("f.Health.current");
    bdg::Widget* max = e->editor->Find("f.Health.maxHealth");
    v[12] = std::format("{}", cur && cur->marked && max && !max->marked ? 1 : 0);
  }
  if (frame == 10) {
    v[2] = std::format("{}", health() == 10 && m->bus.UndoDepth() == 2 ? 1 : 0);
    TogglePlay(e);
  }
  if (frame == 12) {
    const bdg::Transform* t = transform("g8");
    bdg::Widget* banner = e->editor->Find("play.keep.banner");
    v[3] = std::format("{}", t && t->rotation.y == rot0.y && t->position.y == pos0.y && health() == 100 && m->bus.UndoDepth() == depth0 &&
                                     !m->playing ? 1 : 0);
    v[4] = std::format("{}", banner && !banner->hidden ? e->play.Pending().size() : 0);
    v[7] = std::format("{}", e->sceneView->GameFrames() > game0 + 3 ? 1 : 0);
    click("play.keep");
  }
  if (frame == 14) {
    const bdg::Transform* t = transform("g8");
    bdg::Widget* banner = e->editor->Find("play.keep.banner");
    v[5] = std::format("{}", t && t->position.y == 2 && health() == 10 && m->bus.UndoDepth() == depth0 + 1 && banner && banner->hidden ? 1 : 0);
    std::string err;
    m->bus.Undo(&err); // the kept values are one step
  }
  if (frame == 16) {
    const bdg::Transform* t = transform("g8");
    v[6] = std::format("{}", t && t->position.y == pos0.y && health() == 100 ? 1 : 0);
  }
}

// Scripted check of 3-7 (BDG_BUDGET_TEST=1): the Budget chip opens the bars, the toolbar badge switches the platform
// to Android, the bars hold the last render's numbers against Android's medium budget; a tight project budget
// turns the chip to its warning and the heaviest object can be selected from the panel.
void InjectBudgetTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->budgetTestValues;
  const float s = e->editor->Scale();
  auto clickAt = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * s, y * s});
    e->window.InjectEvent({T::MouseDown, x * s, y * s, 1});
    e->window.InjectEvent({T::MouseUp, x * s, y * s, 1});
  };
  if (frame == 2) {
    sv->budgetPanel = true; // (the Show menu's Budget item, round 7)
    if (bdg::Widget* b = e->editor->Find("tool.platform")) clickAt(b->x + b->w * 0.5f, b->y + b->h * 0.5f); // Windows -> Android
  }
  if (frame == 5) {
    const auto bars = sv->BudgetBars();
    const bdg::RenderStats& st = e->r3d->Stats();
    bdg::Widget* badge = e->editor->Find("tool.platform");
    v[0] = std::format("{}", sv->budgetPanel ? 1 : 0);
    v[1] = std::string(bdg::PlatformName(sv->budgetPlatform)) + (badge && badge->text == "Android" ? "" : "(badge)");
    v[2] = std::format("{}", bars.size());
    const bdg::BudgetLimits lim = bdg::BudgetForTier(bdg::DefaultBudget(1), sv->quality.tier);
    v[3] = std::format("{}", bars.size() == 4 && bars[0].used == st.sceneDraws && bars[0].limit == lim.draws && bars[1].limit == lim.triangles &&
                                     bars[1].used > 0 && !sv->BudgetOver() ? 1 : 0);
    sv->budgetMedium[1].draws = 1; // a tight project budget for Android (instancing keeps this scene at 2 draws)
  }
  if (frame == 8) {
    float x = 0, y = 0;
    v[4] = std::format("{}", sv->BudgetOver() && sv->BudgetBars()[0].level == 2 ? 1 : 0);
    v[5] = std::format("{}", sv->BudgetOver() ? 1 : 0); // the Show menu says "Budget ⚠" (no Budget pill now)
    for (const auto& c : sv->BudgetCulprits()) // one that is not selected yet: the click must select it
      if (c.id != m->Selected() && sv->ChipCenter("bud.sel." + c.id, x, y)) {
        v[6] = c.id;
        clickAt(x, y);
        break;
      }
  }
  if (frame == 10) v[6] = std::format("{}", !v[6].empty() && m->Selected() == v[6] ? 1 : 0);
}

void InjectSnapTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->snapTestValues;
  auto ev = [&](T type, float x, float y, int button = 0, bool ctrl = false, bool shift = false) {
    bdg::InputEvent i{type, x * scale, y * scale, button};
    i.ctrl = ctrl;
    i.shift = shift;
    e->window.InjectEvent(i);
  };
  auto keyAs = [&](bdg::Key k, bool down) { bdg::InputEvent i{down ? T::KeyDown : T::KeyUp}; i.key = k; e->window.InjectEvent(i); };
  auto key = [&](bdg::Key k) { keyAs(k, true); };
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("snap test: {} {}: {}", name, target, err);
  };
  auto bounds = [&](const char* id, glm::vec3& lo, glm::vec3& hi) { return sv->ObjectBounds(*m, id, lo, hi); };
  auto click = [&](const char* chip) { // a real click when shown; toolbar controls in the "»" menu are pressed
    float x, y;
    if (!sv->ChipCenter(chip, x, y)) {
      if (!sv->PressToolbar(chip, *m)) bdg::LogError("snap test: no chip {}", chip);
      return;
    }
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    ev(T::MouseUp, x, y, 1);
  };
  auto ui = [&](glm::vec3 p) { glm::vec2 s(0); sv->ProjectUI(p, s.x, s.y); return s; };
  // A move drag from the centre handle to a UI point, with modifiers on the moves.
  auto dragTo = [&](glm::vec2 to, bool ctrl, bool shift) {
    float x, y;
    if (!sv->GizmoPoint(*m, bdg::GizmoHandle::Center, x, y)) { bdg::LogError("snap test: no centre handle"); return; }
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    for (int k = 1; k <= 4; ++k) ev(T::MouseMove, x + (to.x - x) * k / 4, y + (to.y - y) * k / 4, 0, ctrl, shift);
    ev(T::MouseUp, to.x, to.y, 1);
  };
  static glm::vec3 start;
  static size_t depth0;
  if (frame == 2) { // three 1 m cubes standing on the ground
    const float pos[3][3] = {{0, 0.5f, 4}, {2.3f, 0.5f, 4.4f}, {5.1f, 0.5f, 3.7f}};
    for (int i = 0; i < 3; ++i) {
      const std::string id = std::format("s{}", i + 1);
      run("add", "", std::format(R"({{"id":"{}","name":"Snap {}"}})", id, (char)('A' + i)));
      run("addcomp", id, R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
      run("set", id, std::format(R"({{"path":"Transform.position","value":[{},{},{}]}})", pos[i][0], pos[i][1], pos[i][2]));
    }
    sv->camera.pivot = {2.5f, 0.5f, 4.2f};
    sv->camera.yaw = 0;
    sv->camera.pitch = -32;
    sv->camera.distance = 10;
    m->Select("s1");
    key(bdg::Key::W);
  }
  if (frame == 4) { // grid snap: the toolbar's grid toggle on, 0.5 m (round 7: toggle + value list), drag along X
    sv->snap.move = 0.5f;
    click("snap");
    start = bdg::space::ToGlm(m->scene.WorldPosition("s1"));
    float x, y;
    if (sv->GizmoPoint(*m, bdg::GizmoHandle::X, x, y)) {
      ev(T::MouseMove, x, y);
      ev(T::MouseDown, x, y, 1);
      for (int k = 1; k <= 4; ++k) ev(T::MouseMove, x + 20.f * k, y);
      ev(T::MouseUp, x + 80, y, 1);
    }
  }
  if (frame == 6) {
    glm::vec3 p = bdg::space::ToGlm(m->scene.WorldPosition("s1"));
    const float r = p.x / 0.5f;
    v[0] = std::format("{}", sv->snap.always && sv->snap.move == 0.5f && p.x > start.x + 0.2f && std::abs(r - std::round(r)) < 1e-4f &&
                                     p.y == start.y && p.z == start.z ? 1 : 0);
    click("snap"); // off again: free moves for the rest
    key(bdg::Key::V); // vertex snap: hold V and drag onto the second cube's top front-left corner
    dragTo(ui({1.8f, 1.f, 4.9f}), false, false);
    keyAs(bdg::Key::V, false);
  }
  if (frame == 8) {
    glm::vec3 lo, hi;
    bool onCorner = false;
    if (bounds("s1", lo, hi))
      for (int k = 0; k < 8; ++k)
        onCorner = onCorner || glm::distance(glm::vec3((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z), glm::vec3(1.8f, 1.f, 4.9f)) < 1e-3f;
    v[1] = std::format("{}", !sv->snap.always && onCorner ? 1 : 0);
    dragTo(ui({5.1f, 1.f, 3.7f}), true, true); // surface snap: onto the top of the third cube
  }
  if (frame == 10) {
    glm::vec3 lo, hi;
    bool ok = bounds("s1", lo, hi) && std::abs(lo.y - 1.f) < 2e-3f && std::abs((lo.x + hi.x) * 0.5f - 5.1f) < 0.1f &&
              std::abs((lo.z + hi.z) * 0.5f - 3.7f) < 0.1f;
    v[2] = std::format("{}", ok ? 1 : 0);
    key(bdg::Key::Z); // (Ctrl+Z below) back to the vertex-snapped place
    bdg::InputEvent z{T::KeyDown};
    z.key = bdg::Key::Z;
    z.ctrl = true;
    e->window.InjectEvent(z);
  }
  if (frame == 12) { // align: three selected, the Align chip, then "Z min"
    m->SetSelection({"s1", "s2", "s3"});
  }
  if (frame == 14) click("align");
  if (frame == 16) {
    depth0 = m->bus.UndoDepth();
    click("al.z.min");
  }
  if (frame == 18) {
    glm::vec3 lo[3], hi[3];
    bool ok = bounds("s1", lo[0], hi[0]) && bounds("s2", lo[1], hi[1]) && bounds("s3", lo[2], hi[2]);
    v[3] = std::format("{}", ok && std::abs(lo[0].z - lo[1].z) < 1e-4f && std::abs(lo[1].z - lo[2].z) < 1e-4f && m->bus.UndoDepth() == depth0 + 1 ? 1 : 0);
    click("al.x.dist");
  }
  if (frame == 20) {
    glm::vec3 lo[3], hi[3];
    bounds("s1", lo[0], hi[0]);
    bounds("s2", lo[1], hi[1]);
    bounds("s3", lo[2], hi[2]);
    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int a, int b) { return lo[a].x < lo[b].x; });
    const float gap1 = lo[order[1]].x - hi[order[0]].x, gap2 = lo[order[2]].x - hi[order[1]].x;
    v[4] = std::format("{}", std::abs(gap1 - gap2) < 1e-3f && m->bus.UndoDepth() == depth0 + 2 ? 1 : 0);
    m->SetSelection({});
    key(bdg::Key::M); // the Measure tool (the key reaches the shortcuts after the view's mouse events)
  }
  if (frame == 21) { // the ruler: 3 m along x on the ground
    glm::vec2 a = ui({-3.f, 0, 7.f}), b = ui({0.f, 0, 7.f});
    ev(T::MouseMove, a.x, a.y);
    ev(T::MouseDown, a.x, a.y, 1);
    for (int k = 1; k <= 4; ++k) ev(T::MouseMove, a.x + (b.x - a.x) * k / 4, a.y + (b.y - a.y) * k / 4);
    ev(T::MouseUp, b.x, b.y, 1);
  }
  if (frame == 23) {
    const bdg::Measure* ms = sv->CurrentMeasure();
    v[5] = std::format("{}", ms && std::abs(ms->Length() - 3.f) < 0.02f && std::abs(ms->Delta().x - 3.f) < 0.02f && m->tool == "measure" ? 1 : 0);
    v[6] = std::format("{}", m->bus.UndoDepth()); // each drag / align was one step
  }
}

// Scripted check of 3-3c (BDG_BLOCKOUT_TEST=1): the Blockout tool (B) adds a box from its chip, a face handle drag
// pushes +X out onto the 0.5 m grid (the -X face stays), the top is pulled up, Esc cancels a drag, Ctrl+Z undoes a
// drag in one step, stairs / ramp / cylinder chips add the other shapes and a click picks a block.
void InjectBlockoutTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->blockoutTestValues;
  auto ev = [&](T type, float x, float y, int button = 0) { e->window.InjectEvent({type, x * scale, y * scale, button}); };
  auto key = [&](bdg::Key k, bool ctrl = false) { bdg::InputEvent i{T::KeyDown}; i.key = k; i.ctrl = ctrl; e->window.InjectEvent(i); };
  auto click = [&](const char* chip) {
    float x, y;
    if (!sv->ChipCenter(chip, x, y)) { bdg::LogError("blockout test: no chip {}", chip); return; }
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    ev(T::MouseUp, x, y, 1);
  };
  auto block = [&](const std::string& id, glm::vec3& size, glm::vec3& lo, glm::vec3& hi) {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("Blockout") : nullptr;
    if (!c) return false;
    size = bdg::space::ToGlm(static_cast<const bdg::Blockout*>(c->data)->size);
    return sv->ObjectBounds(*m, id, lo, hi);
  };
  // Drag a face handle along the screen direction of `worldDir` by `px` UI units; `release` false keeps holding.
  auto dragFace = [&](int face, glm::vec3 worldDir, float px, bool release = true) {
    float x, y, ox, oy, ax, ay;
    glm::vec3 lo, hi, size;
    if (!sv->FaceHandlePoint(*m, face, x, y) || !block(m->Selected(), size, lo, hi)) { bdg::LogError("blockout test: no face {}", face); return; }
    glm::vec3 c = (lo + hi) * 0.5f;
    sv->ProjectUI(c, ox, oy);
    sv->ProjectUI(c + worldDir, ax, ay);
    glm::vec2 dir = glm::normalize(glm::vec2(ax - ox, ay - oy));
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    for (int k = 1; k <= 4; ++k) ev(T::MouseMove, x + dir.x * px * k / 4, y + dir.y * px * k / 4);
    if (release) ev(T::MouseUp, x + dir.x * px, y + dir.y * px, 1);
  };
  static std::string id;
  static glm::vec3 lo0, hi0, size0;
  static size_t depth0;
  if (frame == 2) {
    sv->camera.pivot = {4, 0.5f, 4};
    sv->camera.yaw = 20;
    sv->camera.pitch = -30;
    sv->camera.distance = 7;
    sv->snap.always = true; // 0.5 m grid
    sv->snap.move = 0.5f;
    key(bdg::Key::B);
  }
  if (frame == 4) {
    depth0 = m->bus.UndoDepth();
    click("bo.0");
  }
  if (frame == 6) {
    id = m->Selected();
    bool ok = !id.empty() && block(id, size0, lo0, hi0) && size0 == glm::vec3(1) && std::abs(lo0.y) < 1e-3f && m->bus.UndoDepth() == depth0 + 1 &&
              m->tool == "blockout";
    v[0] = std::format("{}", ok ? 1 : 0);
    dragFace(0, {1, 0, 0}, 70); // push +X out
  }
  if (frame == 8) {
    glm::vec3 lo, hi, size;
    block(id, size, lo, hi);
    const float r = hi.x / 0.5f;
    v[1] = std::format("{}", size.x > 1.2f && std::abs(lo.x - lo0.x) < 1e-3f && std::abs(r - std::round(r)) < 1e-3f && size.y == 1 ? 1 : 0);
    dragFace(2, {0, 1, 0}, 60); // pull the top up
  }
  if (frame == 10) {
    glm::vec3 lo, hi, size;
    block(id, size, lo, hi);
    v[2] = std::format("{}", size.y > 1.2f && std::abs(lo.y) < 1e-3f ? 1 : 0);
    size0 = size;
    dragFace(5, {0, 0, -1}, 60, false); // pull -Z, then Esc
  }
  if (frame == 11) key(bdg::Key::Escape);
  if (frame == 12) {
    float x, y;
    sv->FaceHandlePoint(*m, 5, x, y);
    ev(T::MouseUp, x, y, 1);
  }
  if (frame == 14) {
    glm::vec3 lo, hi, size;
    block(id, size, lo, hi);
    v[3] = std::format("{}", size == size0 ? 1 : 0);
    depth0 = m->bus.UndoDepth();
    key(bdg::Key::Z, true); // undo the top pull
  }
  if (frame == 16) {
    glm::vec3 lo, hi, size;
    block(id, size, lo, hi);
    v[4] = std::format("{}", size.y == 1 && size.x > 1.2f && m->bus.UndoDepth() == depth0 - 1 ? 1 : 0);
    sv->camera.pivot = {-4, 0.5f, 6}; // somewhere empty for the other shapes
    click("bo.1");
  }
  if (frame == 18) {
    sv->camera.pivot = {-7, 0.5f, 6};
    click("bo.2");
  }
  if (frame == 20) {
    sv->camera.pivot = {-10, 0.5f, 6};
    click("bo.3");
  }
  if (frame == 22) {
    int shapes = 0;
    for (const char* sid : {"b2", "b3", "b4"}) {
      bdg::SceneObject* o = m->scene.Find(sid);
      bdg::Component* c = o ? o->Find("Blockout") : nullptr;
      shapes += c ? 1 : 0;
    }
    v[5] = std::format("{}", shapes);
    glm::vec3 lo, hi, size;
    float x, y;
    bool picked = block("b2", size, lo, hi) && sv->ProjectUI((lo + hi) * 0.5f + glm::vec3(0, 0.2f, 0.3f), x, y) && sv->Pick(*m, x, y) == "b2";
    v[6] = std::format("{}", picked && size == glm::vec3(1, 1, 2) ? 1 : 0);
  }
}

void InjectGizmoTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->gizmoTestValues;
  auto ev = [&](T type, float x, float y, int button = 0, bool ctrl = false) {
    bdg::InputEvent i{type, x * scale, y * scale, button};
    i.ctrl = ctrl;
    e->window.InjectEvent(i);
  };
  auto key = [&](bdg::Key k, bool ctrl = false) { bdg::InputEvent i{T::KeyDown}; i.key = k; i.ctrl = ctrl; e->window.InjectEvent(i); };
  auto vec = [&](const char* field) {
    bdg::SceneObject* o = m->scene.Find("g8");
    bdg::Component* c = o ? o->Find("Transform") : nullptr;
    if (!c) return glm::vec3(0);
    const bdg::Transform& t = *static_cast<const bdg::Transform*>(c->data);
    const bdg::Vec3& r = std::string(field) == "position" ? t.position : std::string(field) == "rotation" ? t.rotation : t.scale;
    return glm::vec3(r.x, r.y, r.z);
  };
  auto near3 = [](glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-4f; };
  // Screen direction of a world axis at the Crystal (where a drag "along X" goes).
  auto axisDir = [&](glm::vec3 axis) {
    float ox, oy, ax, ay;
    glm::vec3 p = vec("position");
    sv->ProjectUI(p, ox, oy);
    sv->ProjectUI(p + axis, ax, ay);
    glm::vec2 d(ax - ox, ay - oy);
    return glm::length(d) > 0 ? glm::normalize(d) : glm::vec2(1, 0);
  };
  auto drag = [&](bdg::GizmoHandle h, glm::vec2 by, bool ctrl = false, bool release = true) {
    float x, y;
    if (!sv->GizmoPoint(*m, h, x, y)) { bdg::LogError("gizmo test: no handle point"); return; }
    ev(T::MouseMove, x, y);
    ev(T::MouseDown, x, y, 1);
    for (int k = 1; k <= 4; ++k) ev(T::MouseMove, x + by.x * k / 4, y + by.y * k / 4, 0, ctrl);
    if (release) ev(T::MouseUp, x + by.x, y + by.y, 1);
  };
  static size_t depth0, future0;
  static glm::vec3 start;
  auto future = [&] { // redo steps on offer
    size_t n = 0;
    for (const auto& h : m->bus.History()) n += h.future ? 1 : 0;
    return n;
  };
  if (frame == 2) {
    m->Select("g8");
    key(bdg::Key::W);
  }
  if (frame == 4) { // move along X
    start = vec("position");
    depth0 = m->bus.UndoDepth();
    drag(bdg::GizmoHandle::X, axisDir({1, 0, 0}) * 60.f);
  }
  if (frame == 6) {
    glm::vec3 p = vec("position");
    v[0] = std::format("{}", p.x > start.x + 0.1f && p.y == start.y && p.z == start.z && m->bus.UndoDepth() == depth0 + 1 ? 1 : 0);
    key(bdg::Key::Z, true); // one Ctrl+Z undoes the whole drag
  }
  if (frame == 8) {
    v[1] = std::format("{}", near3(vec("position"), start) ? 1 : 0);
    drag(bdg::GizmoHandle::XZ, axisDir({1, 0, 0}) * 30.f + axisDir({0, 0, 1}) * 30.f); // ground plane square
  }
  if (frame == 10) {
    glm::vec3 p = vec("position");
    v[2] = std::format("{}", p.x != start.x && p.z != start.z && std::fabs(p.y - start.y) < 1e-5f ? 1 : 0);
    key(bdg::Key::Z, true);
  }
  if (frame == 12) { // Esc in the middle of a drag: nothing changes, nothing to undo
    depth0 = m->bus.UndoDepth();
    future0 = future(); // the undone plane drag stays redoable
    drag(bdg::GizmoHandle::Y, {0, -50}, false, false);
  }
  if (frame == 13) key(bdg::Key::Escape);
  if (frame == 14) {
    float x, y;
    sv->GizmoPoint(*m, bdg::GizmoHandle::Y, x, y);
    ev(T::MouseUp, x, y - 50, 1);
  }
  if (frame == 16) {
    v[3] = std::format("{}", near3(vec("position"), start) && m->bus.UndoDepth() == depth0 && future() == future0 && future0 == 1 ? 1 : 0);
    key(bdg::Key::E); // rotate tool
  }
  if (frame == 18) {
    float x, y, ox, oy;
    if (sv->GizmoPoint(*m, bdg::GizmoHandle::Y, x, y) && sv->ProjectUI(vec("position"), ox, oy)) {
      glm::vec2 r = glm::normalize(glm::vec2(x - ox, y - oy)), tangent(r.y, -r.x); // around the centre
      drag(bdg::GizmoHandle::Y, tangent * 40.f);
    }
  }
  if (frame == 20) {
    glm::vec3 r = vec("rotation");
    v[4] = std::format("{}", std::fabs(r.y) > 1.f && std::fabs(r.x) < 1e-3f && std::fabs(r.z) < 1e-3f ? 1 : 0);
    key(bdg::Key::R); // scale tool
  }
  if (frame == 22) {
    float x, y, ox, oy; // outward along the (local) X handle
    if (sv->GizmoPoint(*m, bdg::GizmoHandle::X, x, y) && sv->ProjectUI(vec("position"), ox, oy))
      drag(bdg::GizmoHandle::X, glm::normalize(glm::vec2(x - ox, y - oy)) * 50.f);
  }
  if (frame == 24) {
    glm::vec3 s = vec("scale");
    v[5] = std::format("{}", s.x > 1.2f && s.y == 1.f && s.z == 1.f ? 1 : 0);
    key(bdg::Key::W);
    start = vec("position");
  }
  if (frame == 26) drag(bdg::GizmoHandle::X, axisDir({1, 0, 0}) * 45.f, true); // Ctrl: whole metres
  if (frame == 28) {
    float dx = vec("position").x - start.x;
    v[6] = std::format("{}", std::fabs(dx - std::round(dx)) < 1e-4f ? 1 : 0);
    float x = 0, y = 0; // X over the view: local axes, the X arrow follows the object's turn
    if (bdg::Widget* s = e->editor->Find("scene")) { x = s->x + s->w * 0.5f; y = s->y + s->h * 0.5f; }
    ev(T::MouseMove, x, y);
    key(bdg::Key::X);
  }
  if (frame == 30) {
    bdg::GizmoMode mode;
    bdg::GizmoFrame f;
    bdg::GizmoView gv;
    glm::vec3 worldX = glm::vec3(m->scene.WorldMatrix("g8")[0]);
    v[7] = std::format("{}", sv->localSpace && sv->CurrentGizmo(*m, mode, f, gv) &&
                                 glm::length(f.axes[0] - glm::normalize(worldX)) < 1e-4f ? 1 : 0);
  }
}

// Scripted check of 6-2 physics (BDG_PHYSICS_TEST=1; with BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll the C#
// parts run too). Objects made by commands on the open scene: a ground block, a box dropped from 3 m, a ramp with a
// sphere at its top, a trigger volume with a sphere falling through it, a box that a C# PhysicsProbe pushes up with
// AddImpulse, and a 2D box on a 2D floor. Play starts paused and advances exactly one fixed step per frame, twice:
// both runs must end bit-identical (determinism).
namespace physicstest {
struct Run {
  float boxY = -1, boxSpeed = -1, sphereZ = -1, sphereY = -1, sphereSpin = 0, box2dY = -1, jumperMaxY = 0, fallerY = 1e9f;
  uint64_t hash = 0, collisions = 0, triggers = 0;
  int probeCollisions = -1, probeTriggers = -1, auto3d = 0;
  float probeSpeed = -1;
  std::string lastOther;
};
Run runs[2];
constexpr int kSteps = 150; // 2.5 s of game time at 1/60
std::string button = "-";   // the Inspector's one-click "물리 추가"
}
void InjectPhysicsTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  using physicstest::runs;
  using physicstest::kSteps;
  const bool csharp = bdg::DotNetHost::Instance().Running() && bdg::Registry::Instance().Find("PhysicsProbe");
  auto run = [&](const char* name, const char* target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("physics test: {} {}: {}", name, target, err);
  };
  auto object = [&](const char* id, const char* name, const char* pos) { run("add", "", std::format(R"({{"id":"{}","name":"{}"}})", id, name)); run("set", id, std::format(R"({{"path":"Transform.position","value":{}}})", pos)); };
  if (frame == 2) {
    object("phg", "Ground", "[0,-1,0]");
    run("addcomp", "phg", R"({"type":"Blockout","value":{"size":[40,1,40]}})");
    run("addcomp", "phg", R"({"type":"Collider"})");
    object("phb", "Dropped box", "[0,3,0]");
    run("addcomp", "phb", R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
    run("addcomp", "phb", R"({"type":"Rigidbody"})");
    run("addcomp", "phb", R"({"type":"Collider"})");
    if (csharp) run("addcomp", "phb", R"({"type":"PhysicsProbe"})");
    object("phr", "Ramp", "[6,0,0]");
    run("addcomp", "phr", R"({"type":"Blockout","value":{"shape":"Ramp","size":[2,2,6]}})");
    run("addcomp", "phr", R"({"type":"Collider"})");
    object("phs", "Rolling sphere", "[6,2.6,-2.4]");
    run("addcomp", "phs", R"({"type":"MeshRenderer","value":{"mesh":"Sphere"}})");
    run("addcomp", "phs", R"({"type":"Rigidbody"})");
    run("addcomp", "phs", R"({"type":"Collider"})");
    object("pht", "Trigger", "[-6,1.5,0]");
    run("addcomp", "pht", R"({"type":"Collider","value":{"shape":"Box","size":[2,1,2],"isTrigger":true}})");
    object("phf", "Faller", "[-6,4,0]");
    run("addcomp", "phf", R"({"type":"MeshRenderer","value":{"mesh":"Sphere"}})");
    run("addcomp", "phf", R"({"type":"Rigidbody"})");
    run("addcomp", "phf", R"({"type":"Collider"})");
    if (csharp) run("addcomp", "phf", R"({"type":"PhysicsProbe"})");
    object("phj", "Jumper", "[-2,0.5,4]");
    run("addcomp", "phj", R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
    run("addcomp", "phj", R"({"type":"Rigidbody"})");
    run("addcomp", "phj", R"({"type":"Collider"})");
    if (csharp) run("addcomp", "phj", R"({"type":"PhysicsProbe","value":{"pushUp":5}})");
    object("ph2g", "Floor 2D", "[14,-0.5,0]");
    run("set", "ph2g", R"({"path":"Transform.scale","value":[10,1,1]})");
    run("addcomp", "ph2g", R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
    run("addcomp", "ph2g", R"({"type":"Collider2D"})");
    object("ph2b", "Box 2D", "[14,3,0]");
    run("addcomp", "ph2b", R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
    run("addcomp", "ph2b", R"({"type":"Rigidbody2D"})");
    run("addcomp", "ph2b", R"({"type":"Collider2D"})");
    object("pha", "Crate (Add Physics)", "[-15,0.5,-15]");
    run("addcomp", "pha", R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
    m->Select("pha");
  }
  if (frame == 3) { // Inspector button: Rigidbody + Auto Collider in one undo step
    const size_t depth = m->bus.UndoDepth();
    bdg::Widget* w = e->editor->Find("insp.physics");
    if (w && w->onClick) w->onClick(*w);
    bdg::SceneObject* o = m->scene.Find("pha");
    const bool added = o && o->Find("Rigidbody") && o->Find("Collider") && m->bus.UndoDepth() == depth + 1;
    physicstest::button = w ? (added ? "1" : "0") : "missing";
  }
  // two runs: frames 4.. and 4 + kSteps + 8 ..; each starts paused and steps one fixed step per frame
  const int start[2] = {4, 4 + kSteps + 8};
  for (int r = 0; r < 2; ++r) {
    const int f = frame - start[r];
    if (f == 0) {
      e->play.SetPaused(true);
      TogglePlay(e);
    } else if (f >= 1 && f <= kSteps) {
      e->play.Step();
    }
    auto tf = [&](const char* id) -> const bdg::Transform* {
      bdg::SceneObject* o = m->scene.Find(id);
      bdg::Component* c = o ? o->Find("Transform") : nullptr;
      return c ? static_cast<const bdg::Transform*>(c->data) : nullptr;
    };
    if (f >= 2 && f <= kSteps + 1 && e->play.Playing()) { // during the run (the previous frame's step is done)
      if (const bdg::Transform* t = tf("phj")) runs[r].jumperMaxY = std::max(runs[r].jumperMaxY, t->position.y);
      if (const bdg::Transform* t = tf("phf")) runs[r].fallerY = std::min(runs[r].fallerY, t->position.y);
    }
    if (f == kSteps + 2 && e->play.Playing()) {
      physicstest::Run& out = runs[r];
      if (const bdg::Transform* t = tf("phb")) out.boxY = t->position.y;
      if (bdg::SceneObject* o = m->scene.Find("phb"))
        if (bdg::Component* c = o->Find("Rigidbody")) {
          const bdg::Vec3 v = static_cast<const bdg::Rigidbody*>(c->data)->velocity;
          out.boxSpeed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
      if (const bdg::Transform* t = tf("phs")) { out.sphereZ = t->position.z; out.sphereY = t->position.y; }
      if (bdg::SceneObject* o = m->scene.Find("phs"))
        if (bdg::Component* c = o->Find("Rigidbody")) {
          const bdg::Vec3 w = static_cast<const bdg::Rigidbody*>(c->data)->angularVelocity;
          out.sphereSpin = std::abs(w.x) + std::abs(w.y) + std::abs(w.z);
        }
      if (const bdg::Transform* t = tf("ph2b")) out.box2dY = t->position.y;
      out.hash = e->play.Physics().StateHash();
      out.collisions = e->play.PhysicsEvents().collisionEnter;
      out.triggers = e->play.PhysicsEvents().triggerEnter;
      out.auto3d = (int)e->play.Physics().AutoChoices().size();
      if (csharp) {
        auto field = [&](const char* id, const char* name) {
          bdg::SceneObject* o = m->scene.Find(id);
          bdg::Component* c = o ? o->Find("PhysicsProbe") : nullptr;
          if (!c) return std::string();
          for (const bdg::FieldInfo& fi : c->type->fields)
            if (fi.name == name) return bdg::Registry::ToJson(static_cast<unsigned char*>(c->data) + fi.offset, fi.value);
          return std::string();
        };
        out.probeCollisions = std::atoi(field("phb", "collisions").c_str());
        out.probeTriggers = std::atoi(field("phf", "triggers").c_str());
        out.probeSpeed = (float)std::atof(field("phj", "speed").c_str());
        out.lastOther = field("phb", "lastOther");
      }
      bdg::LogInfo("physics: run {} {}", r + 1, e->play.Physics().Describe());
      TogglePlay(e);
      e->play.SetPaused(false);
    }
  }
}
void ReportPhysicsTest(bdg_engine* e) {
  using physicstest::runs;
  const physicstest::Run& a = runs[0];
  const bool csharp = a.probeCollisions >= 0;
  const bool fall = std::abs(a.boxY - 0.5f) < 0.05f && a.boxSpeed >= 0 && a.boxSpeed < 0.05f && a.collisions >= 1;
  const bool roll = a.sphereZ > -2.4f + 2.f && a.sphereY < 1.f && a.sphereSpin > 30.f; // rolled down the slope, turning
  const bool trigger = a.triggers >= 1 && a.fallerY < 0.6f;                          // went through the trigger to the ground
  const bool twoD = std::abs(a.box2dY - 0.5f) < 0.05f;
  const bool same = a.hash != 0 && a.hash == runs[1].hash;
  const bool script = !csharp || (a.probeCollisions >= 1 && a.probeTriggers >= 1 && a.jumperMaxY > 1.2f && a.lastOther == "\"Ground\"");
  bdg::LogInfo("physics test: fall={} roll={} trigger={} 2d={} deterministic={} csharp={} button={} | box y={:.3f} v={:.3f} sphere z={:.2f} y={:.2f} "
               "spin={:.0f} faller y={:.2f} 2d y={:.3f} jumper max y={:.2f} events c={} t={} probe c={} t={} speed={:.2f} other={} auto={} "
               "hash {:016x} {:016x}",
               fall ? 1 : 0, roll ? 1 : 0, trigger ? 1 : 0, twoD ? 1 : 0, same ? 1 : 0, csharp ? (script ? "1" : "0") : "-", physicstest::button,
               a.boxY, a.boxSpeed,
               a.sphereZ, a.sphereY, a.sphereSpin, a.fallerY, a.box2dY, a.jumperMaxY, a.collisions, a.triggers, a.probeCollisions,
               a.probeTriggers, a.probeSpeed, a.lastOther, a.auto3d, a.hash, runs[1].hash);
  (void)e;
}
BDG_EDITOR_TEST("BDG_PHYSICS_TEST", InjectPhysicsTest, ReportPhysicsTest)

// Scripted check of 6-4 input actions (BDG_INPUT_TEST=1, BDG_ASSETS = an empty folder, BDG_SCRIPT_ASSEMBLIES =
// <Managed>/Bidulgi.Samples.dll): writes Input/Actions.jsonl and two contexts next to the assets, plays an object with
// the C# InputProbe, and injects keys and mouse moves into the window. The Game view has the input right after Play:
// D moves, Space jumps (seen in Update and in FixedUpdate), the mouse looks; Escape gives the input back to the editor
// (D no longer moves); a click into the Game view takes it again; the Vehicle context (higher priority) takes Space
// for Brake.
namespace inputtest {
std::string focusStart = "-", move = "-", jump = "-", look = "-", escape = "-", click = "-", context = "-", fixed = "-";
}
void InjectInputTest(bdg_engine* e, int frame, float scale) {
  namespace fs = std::filesystem;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen) return;
  using T = bdg::InputEvent::Type;
  auto key = [&](bdg::Key k, bool down) {
    bdg::InputEvent ev{down ? T::KeyDown : T::KeyUp};
    ev.key = k;
    e->window.InjectEvent(ev);
  };
  auto mouse = [&](T type, float x, float y) { e->window.InjectEvent({type, x * scale, y * scale, type == T::MouseMove ? 0 : 1}); };
  auto field = [&](const char* name) {
    bdg::SceneObject* o = m->scene.Find("ip1");
    bdg::Component* c = o ? o->Find("InputProbe") : nullptr;
    if (!c) return std::string("?");
    for (const bdg::FieldInfo& fi : c->type->fields)
      if (fi.name == name) return bdg::Registry::ToJson(static_cast<unsigned char*>(c->data) + fi.offset, fi.value);
    return std::string("?");
  };
  auto num = [&](const char* name) { return std::atof(field(name).c_str()); };
  bdg::InputSystem& in = bdg::InputSystem::Instance();
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20)); // at least one fixed step per frame
  if (frame == 2) {
    const fs::path root = bdg::PathU8(e->assets.Root()).parent_path() / "Input";
    fs::create_directories(root / "Contexts");
    auto write = [](const fs::path& p, const char* text) {
      if (std::FILE* f = _wfopen(p.c_str(), L"wb")) { std::fwrite(text, 1, std::strlen(text), f); std::fclose(f); }
    };
    write(root / "Actions.jsonl", "{\"action\":\"Move\",\"type\":\"vec2\"}\n{\"action\":\"Look\",\"type\":\"vec2\"}\n"
                                  "{\"action\":\"Jump\",\"type\":\"button\"}\n{\"action\":\"Brake\",\"type\":\"button\"}\n");
    write(root / "Contexts" / "OnFoot.jsonl", "{\"context\":\"OnFoot\",\"priority\":0}\n{\"action\":\"Move\",\"key\":\"WASD\"}\n"
                                              "{\"action\":\"Jump\",\"key\":\"Space\"}\n{\"action\":\"Look\",\"key\":\"Mouse.Delta\"}\n");
    write(root / "Contexts" / "Vehicle.jsonl", "{\"context\":\"Vehicle\",\"priority\":10,\"enabled\":false}\n{\"action\":\"Brake\",\"key\":\"Space\"}\n");
    bdg::Command c;
    c.name = "add";
    c.args = R"({"id":"ip1","name":"Player"})";
    std::string err;
    m->bus.Execute(c, &err);
    c.name = "addcomp";
    c.target = "ip1";
    c.args = R"({"type":"InputProbe"})";
    if (!m->bus.Execute(c, &err)) bdg::LogError("input test: {}", err);
    TogglePlay(e);
  }
  bdg::Widget* game = e->editor->Find("game");
  const float gx = game ? game->x + game->w * 0.5f : 0, gy = game ? game->y + game->h * 0.5f : 0;
  if (frame == 4) {
    inputtest::focusStart = in.gameFocused ? "1" : "0";
    key(bdg::Key::D, true);
  }
  if (frame == 6) key(bdg::Key::Space, true);
  if (frame == 7) key(bdg::Key::Space, false);
  if (frame == 9) {
    inputtest::move = num("moveX") > 0.99 ? "1" : "0(" + field("moveX") + ")";
    inputtest::jump = field("jumps") == "1" ? "1" : "0(" + field("jumps") + ")";
    inputtest::fixed = field("fixedJumps") == "1" ? "1" : "0(" + field("fixedJumps") + ")";
    key(bdg::Key::D, false);
    mouse(T::MouseMove, gx, gy);
  }
  if (frame == 10) mouse(T::MouseMove, gx + 30, gy);
  if (frame == 12) {
    inputtest::look = num("lookX") > 20 ? "1" : "0(" + field("lookX") + ")";
    key(bdg::Key::Escape, true);
    key(bdg::Key::Escape, false);
  }
  if (frame == 13) key(bdg::Key::D, true); // goes to the editor now
  if (frame == 15) {
    const bdg::ActionState* mv = in.State("Move");
    inputtest::escape = !in.gameFocused && mv && glm::length(mv->value) == 0 && num("moveX") == 0 ? "1" : "0";
    key(bdg::Key::D, false);
    mouse(T::MouseMove, gx, gy);
    mouse(T::MouseDown, gx, gy);
    mouse(T::MouseUp, gx, gy);
  }
  if (frame == 17) {
    inputtest::click = in.gameFocused ? "1" : "0";
    in.EnableContext("Vehicle", true);
    key(bdg::Key::Space, true);
  }
  if (frame == 20) {
    inputtest::context = field("brakes") == "1" && field("jumps") == "1" ? "1" : "0(brakes " + field("brakes") + " jumps " + field("jumps") + ")";
    key(bdg::Key::Space, false);
  }
  if (frame == 22) TogglePlay(e);
}
void ReportInputTest(bdg_engine*) {
  using namespace inputtest;
  bdg::LogInfo("input test: focus={} move={} jump={} fixed={} look={} escape={} click={} context={}", focusStart, move, jump, fixed, look, escape,
               click, context);
}
BDG_EDITOR_TEST("BDG_INPUT_TEST", InjectInputTest, ReportInputTest)

// Scripted check of 6-1 prefabs (BDG_PREFAB_TEST=1, BDG_ASSETS = an empty project folder; with BDG_SCRIPT_ASSEMBLIES the
// C# part runs too). An object with a child becomes Prefabs/Enemy.prefab (Inspector button), two more instances are
// placed (command, and a drop on the Scene view), one is overridden (the Inspector marks the field), the prefab file is
// edited (only the instance without the override follows), the override is reverted (and undone, redone), another one
// is applied to the prefab (every instance follows), the scene is saved and opened again (links and overrides kept, the
// build's reference index keeps the prefab), a Hierarchy object dropped on the Project panel becomes a second prefab,
// and a C# PrefabSpawner places one with Spawn("Prefabs/Enemy.prefab").
namespace prefabtest {
std::string create = "-", place = "-", inspector = "-", propagate = "-", revert = "-", apply = "-", reopen = "-", build = "-", drop = "-", csharp = "-";
std::string inst1, inst2;
}
void InjectPrefabTest(bdg_engine* e, int frame, float) {
  namespace fs = std::filesystem;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen) return;
  using namespace prefabtest;
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("prefab test: {} {}: {}", name, target, err);
    return c.target;
  };
  auto maxHp = [&](const std::string& id) {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* h = o ? o->Find("Health") : nullptr;
    return h ? static_cast<bdg::Health*>(h->data)->maxHealth : -1;
  };
  auto scaleX = [&](const std::string& id) {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* t = o ? o->Find("Transform") : nullptr;
    return t ? static_cast<bdg::Transform*>(t->data)->scale.x : -1.f;
  };
  const fs::path root = bdg::PathU8(e->assets.Root());
  const std::string prefabPath = "Prefabs/Enemy.prefab";
  if (frame == 2) {
    run("add", "", R"({"id":"pe","name":"Enemy"})");
    run("addcomp", "pe", R"({"type":"MeshRenderer","value":{"mesh":"Cube"}})");
    run("addcomp", "pe", R"({"type":"Health","value":{"maxHealth":50}})");
    run("add", "", R"({"id":"peh","name":"HpBar","parent":"pe"})");
    run("set", "peh", R"({"path":"Transform.position","value":[0,2,0]})");
    m->Select("pe");
  }
  if (frame == 3) { // the Inspector's "+ 프리팹 만들기"
    bdg::Widget* w = e->editor->Find("insp.prefab.create");
    if (w && w->onClick) w->onClick(*w);
    bdg::SceneObject* pe = m->scene.Find("pe");
    create = fs::exists(root / "Prefabs" / "Enemy.prefab") && pe && bdg::IsInstanceRoot(*pe) && m->scene.Find("pe/peh") ? "1" : "0";
    const bdg::Vec3 p{3, 0, 0};
    inst1 = m->AddPrefabInstance(prefabPath, "", &p);
    if (bdg::Widget* scene = e->editor->Find("scene"); scene && scene->onDrop) { // dropped from the Project panel
      bdg::Widget tile;
      tile.id = "p.item." + prefabPath;
      const size_t before = m->scene.ObjectCount();
      scene->onDrop(tile);
      inst2 = m->Selection().empty() ? "" : m->Selection().back();
      place = !inst1.empty() && !inst2.empty() && inst1 != inst2 && m->scene.ObjectCount() == before + 2 && m->scene.Find(inst2 + "/peh") ? "1" : "0";
    }
    run("set", inst1, R"({"path":"Health.maxHealth","value":5})");
    m->Select(inst1);
  }
  if (frame == 5) {
    bdg::Widget* f = e->editor->Find("f.Health.maxHealth");
    const bool marked = f && f->overridden && m->IsOverridden(inst1, "Health.maxHealth");
    bdg::Widget* rv = e->editor->Find("insp.prefab.revert");
    inspector = marked && rv && rv->text.find("(1)") != std::string::npos && bdg::PrefabOverrides(m->scene, inst2).empty() ? "1" : "0";
    // the prefab file edited elsewhere: maxHealth 50 -> 80
    std::ifstream in(root / "Prefabs" / "Enemy.prefab", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const size_t at = text.find("\"maxHealth\":50");
    if (at != std::string::npos) text.replace(at, 14, "\"maxHealth\":80");
    std::ofstream(root / "Prefabs" / "Enemy.prefab", std::ios::binary) << text;
    m->PrefabFilesChanged({prefabPath});
    propagate = maxHp(inst1) == 5 && maxHp(inst2) == 80 && maxHp("pe") == 80 ? "1" : std::format("0({},{},{})", maxHp(inst1), maxHp(inst2), maxHp("pe"));
  }
  if (frame == 6) {
    std::string err;
    m->RevertPrefab(inst1, "Health.maxHealth");
    const int reverted = maxHp(inst1);
    m->bus.Undo(&err);
    const int undone = maxHp(inst1);
    m->bus.Redo(&err);
    revert = reverted == 80 && undone == 5 && maxHp(inst1) == 80 && bdg::PrefabOverrides(m->scene, inst1).empty() ? "1" : std::format("0({},{},{})", reverted, undone, maxHp(inst1));
    run("set", inst1, R"({"path":"Transform.scale","value":[2,2,2]})");
    m->Select(inst1);
  }
  if (frame == 8) {
    bdg::Widget* w = e->editor->Find("insp.prefab.apply");
    if (w && w->onClick) w->onClick(*w);
    apply = scaleX(inst2) == 2.f && scaleX("pe") == 2.f && bdg::PrefabOverrides(m->scene, inst1).empty() ? "1" : std::format("0({},{})", scaleX(inst2), scaleX("pe"));
    run("set", inst2, R"({"path":"Health.maxHealth","value":7})"); // an override that must survive saving
  }
  if (frame == 9) {
    const std::u8string su = (bdg::PathU8(e->assets.Root()).parent_path() / "Scenes" / "Prefabs.scene").u8string();
    m->scenePath.assign(su.begin(), su.end());
    m->SaveScene();
    std::ifstream in(bdg::PathU8(m->scenePath), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const bool oneLine = text.find("\"prefab\":\"guid:") != std::string::npos && text.find("\"id\":\"" + inst2 + "/peh\"") == std::string::npos;
    m->OpenScene(m->scenePath);
    bdg::SceneObject* ob = m->scene.Find(inst2);
    reopen = oneLine && ob && bdg::IsInstanceRoot(*ob) && m->scene.Find(inst2 + "/peh") && maxHp(inst2) == 7 && scaleX(inst2) == 2.f ? "1" : "0";
    bdg::AssetRefIndex index; // what the build packs: the scene names the prefab, so it is not "unused"
    index.Build(e->assets, {{"Scenes/Prefabs.scene", text}});
    const auto unused = index.Unused();
    build = std::find(unused.begin(), unused.end(), prefabPath) == unused.end() ? "1" : "0";
    // a Hierarchy object dropped on a Project folder becomes a prefab in it
    run("add", "", R"({"id":"pd","name":"Crate"})");
    m->ShowProjectView(bdg::EditorModel::ProjectView::Files);
  }
  if (frame == 11) {
    bdg::Widget* folder = e->editor->Find("p.item.Prefabs");
    if (folder && folder->onDrop) {
      bdg::Widget row;
      row.id = "h.pd";
      folder->onDrop(row);
    }
    bdg::SceneObject* pd = m->scene.Find("pd");
    drop = folder && fs::exists(root / "Prefabs" / "Crate.prefab") && pd && bdg::IsInstanceRoot(*pd) ? "1" : "0";
    if (bdg::Registry::Instance().Find("PrefabSpawner")) {
      run("add", "", R"({"id":"spw","name":"Spawner"})");
      run("addcomp", "spw", R"({"type":"PrefabSpawner","value":{"prefab":"Prefabs/Enemy.prefab"}})");
      TogglePlay(e);
    }
  }
  if (frame == 15 && e->play.Playing()) {
    bdg::SceneObject* o = m->scene.Find("spw");
    bdg::Component* c = o ? o->Find("PrefabSpawner") : nullptr;
    std::string id;
    if (c)
      for (const bdg::FieldInfo& fi : c->type->fields)
        if (fi.name == "spawned") id = bdg::Registry::ToJson(static_cast<unsigned char*>(c->data) + fi.offset, fi.value);
    if (id.size() >= 2) id = id.substr(1, id.size() - 2);
    bdg::SceneObject* s = m->scene.Find(id);
    csharp = s && bdg::IsInstanceRoot(*s) && m->scene.Find(id + "/peh") && maxHp(id) == 80 ? "1" : "0(" + id + ")";
    TogglePlay(e);
  }
}
void ReportPrefabTest(bdg_engine*) {
  using namespace prefabtest;
  bdg::LogInfo("prefab test: create={} place={} inspector={} propagate={} revert={} apply={} reopen={} build={} drop={} csharp={}", create, place,
               inspector, propagate, revert, apply, reopen, build, drop, csharp);
}
BDG_EDITOR_TEST("BDG_PREFAB_TEST", InjectPrefabTest, ReportPrefabTest)

// Scripted check of 7-1 skeletal animation (BDG_ANIM_TEST=1, BDG_ASSETS = a project's Assets folder; the C# part with
// BDG_SCRIPT_ASSEMBLIES). Writes the generated test rig (Models/Rig.gltf, Anim/TestRig.h) and its state machine
// (Anim/Rig.animgraph), places it with an Animator (root motion on) and the C# AnimProbe, a camera and a sun, and saves
// Scenes/Main.scene (CI builds that project and runs it in the player). Checks: the Scene view skins it on the GPU and
// draws it, its bounds are the rest pose's; in play (stepped 1/60 s per frame) the probe's SetFloat("Speed", 3) moves
// Idle -> Run, cross-fading over the graph's 0.25 s with the Inspector showing the state and Speed; the bounds move
// (root motion carries it forward, the spine leans) and Footstep events reach C# (OnAnimationEvent).
#include "Anim/TestRig.h"
namespace animtest {
std::string render = "-", inspector = "-", fade = "-", bounds = "-", rootmotion = "-", events = "-", csharp = "-", preview = "-";
int stopFrame = -1;
float previewTime = -1;
double fadeStart = -1, fadeEnd = -1;
int blendFrames = 0;
glm::vec3 restLo{0}, restHi{0};
int playFrame = -1;
}
void InjectAnimTest(bdg_engine* e, int frame, float) {
  namespace fs = std::filesystem;
  using namespace animtest;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen || !e->sceneView) return;
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("anim test: {} {}: {}", name, target, err);
  };
  const fs::path root = bdg::PathU8(e->assets.Root());
  if (frame == 2) {
    std::error_code ec;
    fs::create_directories(root / "Models", ec);
    fs::create_directories(root / "Anim", ec);
    std::ofstream(root / "Models" / "Rig.gltf", std::ios::binary) << bdg::MakeTestRigGltf();
    std::ofstream(root / "Anim" / "Rig.animgraph", std::ios::binary) << bdg::TestRigAnimGraph();
    e->assets.Refresh();
    m->NewScene(bdg::SceneTemplate::Empty); // only what the check places (the project builds this scene)
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) run("del", id, "{}"); // its camera: ours looks from +Z
    run("add", "", R"({"id":"ground","name":"Ground"})");
    run("set", "ground", R"({"path":"Transform.scale","value":[30,1,30]})");
    run("addcomp", "ground", R"({"type":"MeshRenderer","value":{"mesh":"Plane","material":"M_Ground"}})");
    run("add", "", R"({"id":"hero","name":"Hero"})");
    run("addcomp", "hero", R"({"type":"MeshRenderer","value":{"mesh":"Models/Rig.gltf"}})");
    run("addcomp", "hero", R"({"type":"Animator","value":{"graph":"Anim/Rig.animgraph","rootMotion":true}})");
    if (bdg::Registry::Instance().Find("AnimProbe")) run("addcomp", "hero", R"({"type":"AnimProbe","value":{"runAfter":0.5,"speed":3}})");
    run("add", "", R"({"id":"animcam","name":"Camera"})");
    run("set", "animcam", R"({"path":"Transform.position","value":[1.5,1.6,5]})");
    run("set", "animcam", R"({"path":"Transform.rotation","value":[-8,10,0]})");
    run("addcomp", "animcam", R"({"type":"Camera","value":{}})");
    run("add", "", R"({"id":"animsun","name":"Sun"})");
    run("set", "animsun", R"({"path":"Transform.rotation","value":[-50,30,0]})");
    run("addcomp", "animsun", R"({"type":"Light","value":{"type":"Directional","intensity":3}})");
    m->Select("hero");
  }
  if (frame == 3) e->sceneView->Frame(*m); // drawn once: its bounds are known
  if (frame == 5) { // rendered (rest pose, skinned on the GPU)
    glm::vec3 lo, hi;
    const bool b = e->sceneView->ObjectBounds(*m, "hero", lo, hi);
    restLo = lo;
    restHi = hi;
    int32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    e->sceneView->ReadPixels(w, h, px);
    int orange = 0;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
      if (px[i] > 60 && px[i] > px[i + 1] * 1.3f && px[i + 1] > px[i + 2] * 1.1f) ++orange;
    const bool restOk = b && std::fabs(lo.y) < 0.02f && std::fabs(hi.y - 2.f) < 0.02f && std::fabs(hi.x - 0.2f) < 0.02f;
    render = e->sceneView->SkinnedObjects() == 1 && e->sceneView->SkinPasses() > 0 && restOk && orange > 200
                 ? "1" : std::format("0(skinned {} passes {} bounds {} orange {})", e->sceneView->SkinnedObjects(), e->sceneView->SkinPasses(), restOk, orange);
    const std::u8string su = (root.parent_path() / "Scenes" / "Main.scene").u8string(); // the start scene of the project
    m->scenePath.assign(su.begin(), su.end());
    m->SaveScene();
    TogglePlay(e);
    e->play.SetPaused(true); // stepped: exactly 1/60 s of game time per frame below
    playFrame = frame;
  }
  if (playFrame > 0 && frame > playFrame && e->play.Playing()) {
    bdg::SceneObject* o = m->scene.Find("hero");
    bdg::Component* ac = o ? o->Find("Animator") : nullptr;
    if (!ac) return;
    const bdg::Animator& a = *static_cast<const bdg::Animator*>(ac->data);
    const double t = e->play.Time();
    if (a.liveState == "Run" && fadeStart < 0) fadeStart = t;
    if (fadeStart >= 0 && a.liveFade > 0.f && a.liveFade < 1.f) ++blendFrames;
    if (fadeStart >= 0 && fadeEnd < 0 && t > fadeStart && a.liveFade == 0.f && a.liveState == "Run") fadeEnd = t;
    if (inspector == "-" && fadeStart >= 0 && fadeEnd < 0 && a.liveFade > 0.f) { // the Inspector follows the cross-fade
      const bdg::Widget* st = e->editor->Find("anim.live.state");
      const bdg::Widget* sp = e->editor->Find("anim.live.Speed");
      if (st && sp) inspector = st->text.find("Run") != std::string::npos && sp->text.find("3.00") != std::string::npos ? "1" : "0(" + st->text + " / " + sp->text + ")";
    }
    if (frame - playFrame >= 100) { // 1.65 s of game time
      const double d = fadeEnd - fadeStart;
      fade = fadeStart > 0.45 && fadeStart < 0.6 && d > 0.22 && d < 0.3 && blendFrames >= 10 ? "1" : std::format("0(start {:.3f} len {:.3f} blended {})", fadeStart, d, blendFrames);
      glm::vec3 lo, hi;
      const bool b = e->sceneView->ObjectBounds(*m, "hero", lo, hi);
      bounds = b && lo.z < restLo.z - 1.f && hi.z < restHi.z - 1.f ? "1" : std::format("0({:.2f},{:.2f})", lo.z, hi.z);
      bdg::Component* tc = o->Find("Transform");
      const float z = tc ? static_cast<const bdg::Transform*>(tc->data)->position.z : 0.f;
      rootmotion = z < -2.f && z > -4.f ? "1" : std::format("0({:.2f})", z); // ~1.1 s of Run at 3 m/s (part of it fading)
      events = e->play.Animation().EventsSent() >= 2 ? "1" : std::format("0({})", e->play.Animation().EventsSent());
      std::string got, last;
      if (bdg::Component* pc = o->Find("AnimProbe"))
        for (const bdg::FieldInfo& fi : pc->type->fields) {
          if (fi.name == "events") got = bdg::Registry::ToJson(static_cast<unsigned char*>(pc->data) + fi.offset, fi.value);
          if (fi.name == "lastEvent") last = bdg::Registry::ToJson(static_cast<unsigned char*>(pc->data) + fi.offset, fi.value);
        }
      csharp = !got.empty() && got != "0" && last == "\"Footstep\"" ? "1" : "0(" + got + " " + last + ")";
      TogglePlay(e);
      stopFrame = frame;
      m->InspectAsset("Models/Rig.gltf"); // the Inspector's preview plays the model's first clip
    } else e->play.Step();
  }
  if (stopFrame > 0 && frame == stopFrame + 3) previewTime = e->previews.LiveAnimating() ? e->previews.LiveClipTime() : -1.f;
  if (stopFrame > 0 && frame == stopFrame + 8) {
    const bool moving = e->previews.LiveAnimating() && previewTime >= 0 && e->previews.LiveClipTime() != previewTime;
    preview = moving && e->previews.LiveClip() == "Idle" ? "1" : std::format("0({} {} {:.3f})", e->previews.LiveAnimating(), e->previews.LiveClip(), previewTime);
  }
}
void ReportAnimTest(bdg_engine*) {
  using namespace animtest;
  bdg::LogInfo("anim test: render={} inspector={} fade={} bounds={} rootmotion={} events={} csharp={} preview={}", render, inspector, fade, bounds,
               rootmotion, events, csharp, preview);
}
BDG_EDITOR_TEST("BDG_ANIM_TEST", InjectAnimTest, ReportAnimTest)
