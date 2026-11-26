// Scripted editor checks: editor ui (moved out of Engine.cpp). Included by Engine.cpp just before the
// C API, in the same translation unit: bdg_engine and every editor helper are visible here.
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h) instead of editing Engine.cpp.

// Scripted check of E-3 (BDG_SCENEICON_TEST=1): the sample scene's camera, sun and lantern get their component
// icons, the icon is really drawn (pixels change when icons are turned off), a selected camera draws its frustum
// and a selected point light its range, the Icons chip steps the size, and a click on the sun's icon selects it.
// Scripted check of the Inspector choices (BDG_CHOICE_TEST=1): the Sun's Light Shafts switch turns on with a click,
// Type opens its list (or segmented buttons) and Spot is picked, one undo brings Directional back, and resting the
// mouse on a field with a Tooltip schedules its hover help.
void InjectChoiceTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto* v = e->choiceTestValues;
  const float s = e->editor->Scale();
  auto clickAt = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * s, y * s});
    e->window.InjectEvent({T::MouseDown, x * s, y * s, 1});
    e->window.InjectEvent({T::MouseUp, x * s, y * s, 1});
  };
  auto light = [&]() -> bdg::Light* {
    bdg::SceneObject* o = m->scene.Find("g2");
    const bdg::Component* c = o ? o->Find("Light") : nullptr;
    return c ? static_cast<bdg::Light*>(c->data) : nullptr;
  };
  auto box = [&](const char* id, float frac) { // a point inside a field's value box
    bdg::Widget* w = e->editor->Find(id);
    if (!w) { bdg::LogError("choice test: no {}", id); return false; }
    clickAt(w->x + w->w * frac, w->y + w->h * 0.5f);
    return true;
  };
  if (frame == 2) m->Select("g2");
  if (frame == 4) box("f.Light.lightShafts", 0.66f); // the switch sits at the start of the value column
  if (frame == 6) {
    v[0] = light() && light()->lightShafts ? "1" : "0";
    box("f.Light.type", 0.97f); // right end: the combo arrow, or the last segment (Spot)
  }
  if (frame == 7 && e->editor->MenuOpen()) {
    float x, y, w, h;
    if (e->editor->MenuItemRect(2, x, y, w, h)) clickAt(x + w * 0.5f, y + h * 0.5f);
  }
  if (frame == 9) {
    v[1] = light() && light()->type == bdg::LightType::Spot ? "1" : std::format("0({})", light() ? (int)light()->type : -1);
    m->bus.Undo();
  }
  if (frame == 11) {
    v[2] = light() && light()->type == bdg::LightType::Directional && light()->lightShafts ? "1" : "0";
    if (bdg::Widget* w = e->editor->Find("f.Light.castShadows")) { // has a Tooltip
      e->window.InjectEvent({T::MouseMove, (w->x + 20) * s, (w->y + w->h * 0.5f) * s});
    }
  }
  if (frame == 12) v[3] = e->editor->NextTooltipMs() >= 0 ? "1" : "0";
}

// Scripted check (BDG_QUIT_TEST=1): an edit makes the scene dirty (title marker), quitting asks with three buttons,
// Cancel keeps the editor open, an autosave writes the backup and the recovery offer appears for it, and "저장하지
// 않고 종료" ends the loop and removes the backup (the scene file is not touched).
void InjectQuitTest(bdg_engine* e, int frame, float scale) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto* v = e->quitTestValues;
  using T = bdg::InputEvent::Type;
  auto clickButton = [&](const char* label) {
    const auto* d = e->editor->CurrentDialog();
    for (size_t i = 0; d && i < d->buttons.size(); ++i)
      if (d->buttons[i].label == label) {
        float x, y, w, h;
        e->editor->DialogButtonRect(i, x, y, w, h);
        x = (x + w * 0.5f) * scale;
        y = (y + h * 0.5f) * scale;
        e->window.InjectEvent({T::MouseMove, x, y});
        e->window.InjectEvent({T::MouseDown, x, y, 1});
        e->window.InjectEvent({T::MouseUp, x, y, 1});
        return;
      }
    bdg::LogError("quit test: no button {}", label);
  };
  static std::filesystem::file_time_type sceneTime;
  if (frame == 2) {
    std::error_code ec;
    sceneTime = std::filesystem::last_write_time(bdg::PathU8(m->scenePath), ec);
    v[0] = m->Dirty() ? "0" : "1";
    bdg::Command c;
    c.name = "set";
    c.target = "g2";
    c.args = R"({"path":"Light.intensity","value":2.5})";
    m->bus.Execute(c);
  }
  if (frame == 3) {
    const bdg::Widget* title = e->editor->Find("title.scene");
    v[0] = v[0] == "1" && m->Dirty() && title && title->text.find("●") != std::string::npos ? "1" : "0";
    RequestQuit(e, true);
    const auto* d = e->editor->CurrentDialog();
    v[1] = d && d->buttons.size() == 3 && d->title == "저장하지 않은 변경 사항" && d->items.size() == 1 && d->items[0].checked ? "1" : "0";
  }
  if (frame == 4) clickButton("취소");
  if (frame == 5) {
    v[2] = !e->editor->DialogOpen() && !e->quitNow ? "1" : "0";
    AutosaveTick(e, true);
    std::error_code ec;
    v[3] = std::filesystem::exists(bdg::PathU8(m->BackupPath()), ec) ? "1" : "0";
    OfferRecovery(e);
    v[3] = v[3] == "1" && e->editor->DialogOpen() && e->editor->CurrentDialog()->title == "자동 저장본이 있습니다" ? "1" : "0";
    e->editor->CloseDialog();
  }
  if (frame == 6) RequestQuit(e, true);
  if (frame == 7) clickButton("저장 안 함");
}

// Scripted check (BDG_SETTINGS_TEST=1, BDG_PROJECT_SETTINGS = a scratch file that does not exist yet): defaults load,
// the panel has a field per setting, typing a game name changes the setting, the title and the unsaved badge, a bad
// version shows a problem, Save All lists only the project settings and its save writes just the changed values,
// which load back the same; the physics step reaches play mode.
void InjectProjectSettingsTest(bdg_engine* e, int frame, float scale) {
  auto* v = e->settingsTestValues;
  auto setField = [&](const char* id, const char* text) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w) { bdg::LogError("project test: no {}", id); return; }
    w->text2 = text;
    if (w->onCommit) w->onCommit(*w);
  };
  if (frame == 2) {
    int fields = 0, shown = 0;
    for (const bdg::FieldInfo& cat : bdg::ProjectSettings::StaticType().fields)
      if (cat.value.structType)
        for (const bdg::FieldInfo& f : cat.value.structType->fields) {
          ++fields;
          shown += e->editor->Find("ps." + cat.name + "." + f.name) ? 1 : 0;
        }
    v[0] = e->project.description.name == "My Game" && !ProjectSettingsDirty(e) && fields >= 50 && shown == fields ? "1" : std::format("0({}/{})", shown, fields);
    setField("ps.description.name", "Pigeon Quest");
    setField("ps.description.version", "abc");
    setField("ps.physics.fixedTimestep", "0.02");
  }
  if (frame == 4) {
    const bdg::Widget* title = e->editor->Find("title.scene");
    const bdg::Widget* badge = e->editor->Find("ps.dirty");
    const bdg::Widget* issues = e->editor->Find("ps.issues");
    v[1] = e->project.description.name == "Pigeon Quest" && title && title->text.rfind("Pigeon Quest", 0) == 0 && badge && !badge->hidden ? "1" : "0";
    v[2] = issues && issues->children.size() == 1 && issues->children[0]->text.find("버전") != std::string::npos ? "1" : "0";
    setField("ps.description.version", "1.2.0");
    v[3] = std::abs(e->play.Scripts().fixedStep - 0.02) < 1e-6 ? "1" : "0";
    SaveAllDialog(e);
    const auto* d = e->editor->CurrentDialog();
    if (!d || d->items.size() != 1) bdg::LogError("project test: save list has {} item(s)", d ? d->items.size() : 0);
  }
  if (frame == 5) { // press the dialog's save button
    float x, y, w, h;
    if (e->editor->DialogButtonRect(0, x, y, w, h)) {
      using T = bdg::InputEvent::Type;
      x = (x + w * 0.5f) * scale;
      y = (y + h * 0.5f) * scale;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    }
  }
  if (frame == 7) {
    std::string text;
    bdg::ReadTextU8(e->projectPath, text);
    bdg::ProjectSettings back;
    bdg::LoadProjectSettings(e->projectPath, back);
    v[4] = !ProjectSettingsDirty(e) && text.find("Pigeon Quest") != std::string::npos && text.find("company") == std::string::npos &&
                   back.description.name == "Pigeon Quest" && back.description.version == "1.2.0" && std::abs(back.physics.fixedTimestep - 0.02f) < 1e-6f
               ? "1" : "0";
    bdg::LogInfo("project test: defaults={} name={} problems={} physics={} saved={}", v[0], v[1], v[2], v[3], v[4]);
  }
}

void LogQuitTest(bdg_engine* e) {
  bdg::EditorModel* m = e->model.get();
  auto* v = e->quitTestValues;
  std::error_code ec;
  v[4] = e->quitNow ? "1" : "0";
  v[5] = !std::filesystem::exists(bdg::PathU8(m->BackupPath()), ec) ? "1" : "0";
  bdg::LogInfo("quit test: dirty={} ask={} cancel={} autosave={} quit={} cleaned={}", v[0], v[1], v[2], v[3], v[4], v[5]);
}

// Scripted check of E-4a (BDG_UIEDIT_TEST=1, BDG_PREFS = a scratch file): arithmetic in a number field and in a
// vector, a right-click menu on a field (default value, copy, paste), the menu closing on a click outside, and the
// UI zoom by Ctrl+wheel and Ctrl+0, saved.
void InjectUiEditTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto* v = e->uiTestValues;
  const float s = e->editor->Scale();
  auto get = [&](const char* id, const char* comp, const char* field) {
    std::string out;
    bdg::SceneObject* o = m->scene.Find(id);
    if (bdg::Component* c = o ? o->Find(comp) : nullptr) bdg::Registry::Get(c->data, *c->type, field, out);
    return out;
  };
  auto commit = [&](const char* id, const char* text) {
    bdg::Widget* f = e->editor->Find(id);
    if (!f || !f->onCommit) { bdg::LogError("ui test: no field {}", id); return; }
    f->text2 = text;
    f->onCommit(*f);
  };
  auto mouse = [&](T type, float x, float y, int button) {
    bdg::InputEvent ev{type, x * s, y * s, button};
    e->window.InjectEvent(ev);
  };
  if (frame == 2) {
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("inspector");
    m->Select("g9"); // Lantern Light: Light.range is a number, Transform.position a vector
  }
  if (frame == 4) {
    commit("f.Light.range", "4*3");
    commit("f.Transform.position", "1+1, 10/4, -(1+2)");
  }
  if (frame == 6) {
    v[0] = std::format("{}", get("g9", "Light", "range") == "12" ? 1 : 0);
    const std::string pos = get("g9", "Transform", "position");
    v[1] = std::format("{}", pos == "[2,2.5,-3]" ? 1 : 0);
    if (v[1] != "1") bdg::LogError("ui test: position {}", pos);
    if (bdg::Widget* f = e->editor->Find("f.Light.range")) { // right-click the field
      mouse(T::MouseMove, f->x + f->w * 0.7f, f->y + f->h * 0.5f, 0);
      mouse(T::MouseDown, f->x + f->w * 0.7f, f->y + f->h * 0.5f, 3);
      mouse(T::MouseUp, f->x + f->w * 0.7f, f->y + f->h * 0.5f, 3);
    }
  }
  if (frame == 8) {
    const auto& items = e->editor->Menu();
    bool ok = e->editor->MenuOpen() && items.size() == 3 && items[0].label.find("10") != std::string::npos; // default range 10
    float x, y, w, h;
    if (e->editor->MenuItemRect(1, x, y, w, h)) { // "값 복사"
      mouse(T::MouseMove, x + 20, y + h * 0.5f, 0);
      mouse(T::MouseDown, x + 20, y + h * 0.5f, 1);
      mouse(T::MouseUp, x + 20, y + h * 0.5f, 1);
    } else ok = false;
    v[2] = std::format("{}", ok ? 1 : 0);
  }
  if (frame == 10) {
    const bool copied = e->window.GetClipboardText() == "12" && !e->editor->MenuOpen();
    commit("f.Light.range", "5");
    bdg::Widget* f = e->editor->Find("f.Light.range");
    if (f && f->onContext) f->onContext(*f, f->x + 40, f->y + 10);
    float x, y, w, h;
    if (e->editor->MenuItemRect(2, x, y, w, h)) { // "값 붙여넣기" -> 12 again
      mouse(T::MouseMove, x + 20, y + h * 0.5f, 0);
      mouse(T::MouseDown, x + 20, y + h * 0.5f, 1);
      mouse(T::MouseUp, x + 20, y + h * 0.5f, 1);
    }
    v[3] = copied ? "1" : "0";
  }
  if (frame == 12) {
    const bool pasted = get("g9", "Light", "range") == "12";
    bdg::Widget* f = e->editor->Find("f.Light.range");
    if (f && f->onContext) f->onContext(*f, f->x + 40, f->y + 10);
    float x, y, w, h;
    if (e->editor->MenuItemRect(0, x, y, w, h)) { // "기본값으로" -> 10
      mouse(T::MouseMove, x + 20, y + h * 0.5f, 0);
      mouse(T::MouseDown, x + 20, y + h * 0.5f, 1);
      mouse(T::MouseUp, x + 20, y + h * 0.5f, 1);
    }
    v[4] = pasted ? "1" : "0";
  }
  if (frame == 14) {
    const bool reset = get("g9", "Light", "range") == "10";
    bdg::Widget* f = e->editor->Find("f.Light.range");
    if (f && f->onContext) f->onContext(*f, f->x + 40, f->y + 10);
    v[4] = std::format("{}", v[4] == "1" && reset ? 1 : 0);
  }
  if (frame == 15) { // a click elsewhere closes the menu and does nothing else
    const std::string sel = m->Selected();
    mouse(T::MouseDown, 5, 5, 1);
    mouse(T::MouseUp, 5, 5, 1);
    v[5] = sel; // checked next frame
  }
  if (frame == 17) {
    v[5] = std::format("{}", !e->editor->MenuOpen() && m->Selected() == v[5] ? 1 : 0);
    bdg::Widget* h = e->editor->Find("hierarchy"); // Ctrl+wheel over the Hierarchy: zoom in
    bdg::InputEvent wh{T::Wheel, (h ? h->x + 40 : 40) * s, (h ? h->y + 60 : 300) * s};
    wh.wheel = 1;
    wh.ctrl = true;
    e->window.InjectEvent(wh);
  }
  if (frame == 19) {
    const bool zoomed = std::abs(e->uiZoom - 1.1f) < 1e-4f && std::abs(e->editor->Scale() - 1.1f * e->window.GetDisplayScale()) < 0.02f;
    std::ifstream in(std::filesystem::path(e->prefsPath), std::ios::binary);
    const std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bdg::InputEvent k{T::KeyDown};
    k.key = bdg::Key::Num0;
    k.ctrl = true;
    e->window.InjectEvent(k);
    v[6] = zoomed && saved.find("1.1") != std::string::npos ? "1" : "0";
    if (v[6] != "1") bdg::LogError("ui test: zoom {} scale {} saved '{}'", e->uiZoom, e->editor->Scale(), saved);
  }
  if (frame == 21) v[6] = std::format("{}", v[6] == "1" && e->uiZoom == 1.f ? 1 : 0);
}

// Scripted check of E-4b (BDG_INSPECT2_TEST=1): dragging a number's label, dragging a vector's Y tag (only Y
// moves), a click in the Z box selects that number for typing, the colour box opens a picker that changes the
// colour, a Project item dropped on a text field, and arrow keys in the Hierarchy.
void InjectInspect2Test(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto* v = e->inspectTestValues;
  const float s = e->editor->Scale();
  auto ev = [&](T type, float x, float y, int button = 1) { e->window.InjectEvent({type, x * s, y * s, button}); };
  auto key = [&](bdg::Key k) { bdg::InputEvent kv{T::KeyDown}; kv.key = k; e->window.InjectEvent(kv); };
  auto get = [&](const char* id, const char* comp, const char* field) {
    std::string out;
    bdg::SceneObject* o = m->scene.Find(id);
    if (bdg::Component* c = o ? o->Find(comp) : nullptr) bdg::Registry::Get(c->data, *c->type, field, out);
    return out;
  };
  auto drag = [&](float x0, float y0, float dx) {
    ev(T::MouseMove, x0, y0, 0);
    ev(T::MouseDown, x0, y0);
    ev(T::MouseMove, x0 + dx * 0.5f, y0, 0);
    ev(T::MouseMove, x0 + dx, y0, 0);
    ev(T::MouseUp, x0 + dx, y0);
  };
  static std::string health0;
  static size_t depth0;
  if (frame == 2) {
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("inspector");
    m->Select("g6");
  }
  if (frame == 3) // a narrow Inspector (B9: stacked vectors) may have it below the fold
    if (bdg::Widget* f = e->editor->Find("f.Health.maxHealth")) e->editor->ScrollIntoView(*f);
  if (frame == 4) { // drag the label of Health.maxHealth 40 units right: +10 (whole numbers, 0.25 per unit)
    health0 = get("g6", "Health", "maxHealth");
    depth0 = m->bus.UndoDepth();
    if (bdg::Widget* f = e->editor->Find("f.Health.maxHealth")) drag(f->x + 20, f->y + f->h * 0.5f, 40);
  }
  if (frame == 6) {
    const std::string h1 = get("g6", "Health", "maxHealth");
    v[0] = std::format("{}", std::atof(h1.c_str()) - std::atof(health0.c_str()) == 10 && m->bus.UndoDepth() == depth0 + 1 ? 1 : 0);
    if (v[0] != "1") bdg::LogError("inspect2 test: maxHealth {} -> {}, undo {} -> {}", health0, h1, depth0, m->bus.UndoDepth());
    float bx = 0, bw = 0; // the Y tag of Transform.position: 20 units right = +0.2 (1 % of 1 per unit)
    bdg::Widget* f = e->editor->Find("f.Transform.position");
    if (f && e->editor->AxisBox(*f, 1, bx, bw)) drag(bx + 6, f->y + f->h * 0.5f, 20);
  }
  if (frame == 8) {
    const std::string pos = get("g6", "Transform", "position");
    v[1] = std::format("{}", pos == "[0,1.2,0]" ? 1 : 0);
    if (v[1] != "1") bdg::LogError("inspect2 test: position {}", pos);
    float bx = 0, bw = 0; // click in the Z box: its number is selected; typing replaces only it
    bdg::Widget* f = e->editor->Find("f.Transform.position");
    if (f && e->editor->AxisBox(*f, 2, bx, bw)) {
      ev(T::MouseMove, bx + bw * 0.6f, f->y + f->h * 0.5f, 0);
      ev(T::MouseDown, bx + bw * 0.6f, f->y + f->h * 0.5f);
      ev(T::MouseUp, bx + bw * 0.6f, f->y + f->h * 0.5f);
    }
  }
  if (frame == 9) {
    bdg::InputEvent t{T::Text};
    t.text = "7";
    e->window.InjectEvent(t);
    key(bdg::Key::Enter);
  }
  if (frame == 11) {
    const std::string pos = get("g6", "Transform", "position");
    v[2] = std::format("{}", pos == "[0,1.2,7]" ? 1 : 0);
    if (v[2] != "1") bdg::LogError("inspect2 test: typed position {}", pos);
    m->Select("g9"); // Lantern: Light.color
  }
  if (frame == 13) {
    float sx, sy, sw, sh;
    bdg::Widget* f = e->editor->Find("f.Light.color");
    if (f && e->editor->SwatchBox(*f, sx, sy, sw, sh)) {
      ev(T::MouseMove, sx + sw * 0.5f, sy + sh * 0.5f, 0);
      ev(T::MouseDown, sx + sw * 0.5f, sy + sh * 0.5f);
      ev(T::MouseUp, sx + sw * 0.5f, sy + sh * 0.5f);
    }
  }
  if (frame == 15) {
    bdg::Widget* val = e->editor->Find("pick.Light.color.v");
    if (val && val->onChange) {
      val->text2 = "50";
      val->onChange(*val);
    }
  }
  if (frame == 17) {
    const std::string col = get("g9", "Light", "color"); // was [1,0.75,0.45,1]: brightness 50 % -> 0.5 at most
    float r = 0, g = 0, b = 0, a = 0;
    sscanf_s(col.c_str(), "[%f,%f,%f,%f]", &r, &g, &b, &a);
    v[3] = std::format("{}", m->OpenColorPicker() == "Light.color" && std::abs(r - 0.5f) < 0.01f && std::abs(g - 0.375f) < 0.01f && a == 1 ? 1 : 0);
    if (v[3] != "1") bdg::LogError("inspect2 test: colour {}", col);
    m->Select("g6");
  }
  if (frame == 19) { // a Project item dropped on MeshRenderer.mesh
    bdg::Widget* f = e->editor->Find("f.MeshRenderer.mesh");
    bdg::Widget item;
    item.id = "p.item.Models/Rock.glb";
    if (f && f->onDrop) f->onDrop(item);
  }
  if (frame == 21) {
    v[4] = std::format("{}", get("g6", "MeshRenderer", "mesh") == "\"Models/Rock.glb\"" ? 1 : 0);
    if (bdg::Widget* row = e->editor->Find("h.g6")) { // click Player in the Hierarchy, then arrows
      ev(T::MouseMove, row->x + row->w * 0.6f, row->y + row->h * 0.5f, 0);
      ev(T::MouseDown, row->x + row->w * 0.6f, row->y + row->h * 0.5f);
      ev(T::MouseUp, row->x + row->w * 0.6f, row->y + row->h * 0.5f);
    }
  }
  static std::string trail;
  if (frame == 23) key(bdg::Key::Left);   // fold Player
  if (frame == 25) { trail += m->Folded("g6") ? "F" : "-"; key(bdg::Key::Right); }  // open it
  if (frame == 27) { trail += m->Folded("g6") ? "F" : "O"; key(bdg::Key::Right); }  // into Weapon
  if (frame == 29) { trail += m->Selected(); key(bdg::Key::Left); }                    // back to Player
  if (frame == 31) { trail += m->Selected(); key(bdg::Key::Down); }                    // next row: Weapon
  if (frame == 33) {
    trail += m->Selected();
    v[5] = std::format("{}", trail == "FOg7g6g7" ? 1 : 0);
    if (v[5] != "1")
      bdg::LogError("inspect2 test: keys {} (hierarchy keys {}, focused {})", trail, e->hierarchyKeys,
                    e->editor->Focused() ? e->editor->Focused()->id : std::string("-"));
  }
}

// Scripted check of 2-5c (BDG_THEME_TEST=1, BDG_THEME_FILE = a Theme.json CI writes with "panel" changed): the
// project theme is applied at start, a click on the accent row opens the picker, a hex value shows on the next
// frame (the drawn swatch) and is saved, a slider changes the hue, the token goes back to its default, a hand edit
// of Theme.json shows up live, and "기본 테마로" removes every override.
void InjectThemeTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::Theme& t = e->theme;
  auto* v = e->themeTestValues;
  const float s = e->editor->Scale();
  auto click = [&](const std::string& id) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w || w->hidden || w->w <= 0) { bdg::LogError("theme test: {} is not clickable", id); return; }
    const float x = (w->x + w->w * 0.5f) * s, y = (w->y + w->h * 0.5f) * s;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  auto same = [](bdg::Color a, const char* hex) {
    bdg::Color b;
    return bdg::Theme::ParseColor(hex, b) && bdg::Theme::FormatColor(a) == bdg::Theme::FormatColor(b);
  };
  auto fileText = [&] {
    std::ifstream in(std::filesystem::path(e->themePath), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  auto revertHidden = [&] {
    bdg::Widget* b = e->editor->Find("theme.revert");
    return b && b->hidden;
  };
  static int handEdit = 0, resetAt = 0;
  if (frame == 2) {
    v[0] = std::format("{}", same(t.C("panel"), "#203040") && t.Overridden("panel") ? 1 : 0);
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("settings");
  }
  if (frame == 3)
    if (bdg::Widget* row = e->editor->Find("theme.row.accent")) e->editor->ScrollIntoView(*row);
  if (frame == 5) click("theme.row.accent");
  if (frame == 7) {
    bdg::Widget* hex = e->editor->Find("theme.hex");
    v[1] = std::format("{}", e->themeSelected == "accent" && hex && hex->text2 == bdg::Theme::FormatColor(t.Base("accent")) ? 1 : 0);
    if (hex && hex->onCommit) {
      hex->text2 = "#3b82f6";
      hex->onCommit(*hex);
    }
  }
  if (frame == 9) { // the swatch drawn last frame has the new color
    bool drawn = false;
    if (bdg::Widget* row = e->editor->Find("theme.row.accent")) {
      int32_t w = 0, h = 0;
      e->window.GetDrawableSize(w, h);
      std::vector<uint8_t> px;
      e->rhi.ReadPixels(w, h, px);
      const int x = (int)((row->x + 12 + (row->h - 8) * 0.8f) * s), y = h - 1 - (int)((row->y + row->h * 0.5f) * s);
      if (x >= 0 && x < w && y >= 0 && y < h) {
        const uint8_t* p = &px[((size_t)y * w + x) * 4];
        drawn = std::abs(p[0] - 0x3b) < 6 && std::abs(p[1] - 0x82) < 6 && std::abs(p[2] - 0xf6) < 6;
      }
    }
    v[2] = std::format("{}", same(t.C("accent"), "#3b82f6") && drawn && fileText().find("\"accent\":\"#3b82f6\"") != std::string::npos ? 1 : 0);
    if (bdg::Widget* f = e->editor->Find("theme.h"); f && f->onChange) {
      f->text2 = "120";
      f->onChange(*f);
    }
  }
  if (frame == 10)
    if (bdg::Widget* b = e->editor->Find("theme.revert")) e->editor->ScrollIntoView(*b);
  if (frame == 12) {
    float h = 0, sat = 0, val = 0;
    bdg::Theme::ToHSV(t.C("accent"), h, sat, val);
    bdg::Widget* hex = e->editor->Find("theme.hex");
    v[3] = std::format("{}", std::abs(h - 120) < 2 && sat > 0.5f && hex && hex->text2 == bdg::Theme::FormatColor(t.C("accent")) ? 1 : 0);
    click("theme.revert");
  }
  if (frame == 14) {
    const std::string text = fileText();
    v[4] = std::format("{}", !t.Overridden("accent") && same(t.C("accent"), "#f59e32") && text.find("panel") != std::string::npos &&
                                     text.find("accent") == std::string::npos && revertHidden() ? 1 : 0);
    bool written = false; // by hand (another program may hold the file for a moment: try again)
    for (int tries = 0; tries < 20 && !written; ++tries) {
      std::ofstream out(std::filesystem::path(e->themePath), std::ios::binary);
      out << "{\"colors\":{\"ok\":\"#ff00ff\"}}\n";
      out.close();
      written = !out.fail();
      if (!written) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!written) bdg::LogError("theme test: cannot write {}", e->themePath);
    std::this_thread::sleep_for(std::chrono::milliseconds(600)); // the editor polls the file twice a second
    handEdit = frame;
  }
  if (handEdit && !resetAt && frame == handEdit + 25)
    bdg::LogError("theme test: hand edit not seen (ok={} panel overridden={})", bdg::Theme::FormatColor(t.C("ok")), t.Overridden("panel"));
  if (handEdit && !resetAt && frame > handEdit && frame <= handEdit + 24 && same(t.C("ok"), "#ff00ff") && !t.Overridden("panel")) {
    v[5] = "1";
    click("theme.reset");
    resetAt = frame;
  }
  if (resetAt && frame == resetAt + 3)
    v[6] = std::format("{}", t.OverrideCount() == 0 && !std::filesystem::exists(std::filesystem::path(e->themePath)) && same(t.C("panel"), "#1c222b") ? 1 : 0);
}

// Scripted check (BDG_A11Y_TEST=1): focus the Add Component button, let a UI Automation client read the
// window from another thread, then press Ctrl+F6 and see where focus went.
void InjectA11yTest(bdg_engine* e, int frame) {
  if (e->a11yStage == 0 && frame == 2) {
    if (bdg::Widget* b = e->editor->Find("insp.add")) e->editor->Focus(b);
    e->a11yStage = 1;
  } else if (e->a11yStage == 1 && frame >= 4) {
    bdg::a11y::StartProbe();
    e->a11yStage = 2;
  } else if (e->a11yStage == 2 && bdg::a11y::ProbeDone()) {
    e->a11yResult = bdg::a11y::ProbeResult();
    bdg::InputEvent ev{bdg::InputEvent::Type::KeyDown};
    ev.key = bdg::Key::F6;
    ev.ctrl = true;
    e->window.InjectEvent(ev);
    e->a11yStage = 3;
  } else if (e->a11yStage == 3) {
    bdg::Widget* f = e->editor->Focused();
    e->a11yNextPanel = f ? f->id : "-";
    e->a11yStage = 4;
  }
}

// Scripted check (BDG_SHORTCUT_TEST=1 with BDG_SHORTCUTS pointing at a missing file): pick Unity on the
// first-run banner, press Ctrl+P / Alt+P, switch to the Unreal preset in Project Settings, press them again
// and Ctrl+W (Unreal's duplicate).
void InjectShortcutTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  auto click = [&](const char* id) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w || w->hidden) { bdg::LogError("shortcut test: {} not clickable", id); return; }
    float x = (w->x + w->w * 0.5f) * scale, y = (w->y + w->h * 0.5f) * scale;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  auto key = [&](bdg::Key k, bool ctrl, bool alt) {
    bdg::InputEvent ev{T::KeyDown};
    ev.key = k; ev.ctrl = ctrl; ev.alt = alt;
    e->window.InjectEvent(ev);
  };
  if (frame == 2) click("shortcut.unity");
  if (frame == 4) { key(bdg::Key::P, true, false); key(bdg::Key::P, false, true); }
  if (frame == 6) {
    e->shortcutTestValues[0] = e->model->playing ? 1 : 0;
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("settings");
  }
  if (frame == 7) // the preset buttons sit below the project settings: scroll them into view
    if (bdg::Widget* b = e->editor->Find("shortcut.preset.unreal")) e->editor->ScrollIntoView(*b);
  if (frame == 8) click("shortcut.preset.unreal");
  if (frame == 10) { key(bdg::Key::P, false, true); key(bdg::Key::P, true, false); key(bdg::Key::W, true, false); }
  if (frame == 12) e->shortcutTestValues[1] = e->model->playing ? 1 : 0;
}

// Scripted Inspector check (BDG_INSPECTOR_TEST=1): drag the PlayerMove.speed slider to the middle, undo,
// rename with F2, press the Health "Revive" button and undo it, then watch Light.range follow its ShowIf.
void InjectInspectorTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto value = [&](const char* id, const char* comp, const char* field) {
    std::string v;
    bdg::SceneObject* o = m->scene.Find(id);
    if (bdg::Component* c = o ? o->Find(comp) : nullptr) bdg::Registry::Get(c->data, *c->type, field, v);
    return v;
  };
  auto key = [&](bdg::Key k, bool ctrl) { bdg::InputEvent ev{T::KeyDown}; ev.key = k; ev.ctrl = ctrl; e->window.InjectEvent(ev); };
  auto clickAt = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
    e->window.InjectEvent({T::MouseDown, x * scale, y * scale, 1});
    e->window.InjectEvent({T::MouseUp, x * scale, y * scale, 1});
  };
  auto* v = e->inspectorTestValues;
  if (frame == 2) if (bdg::Widget* f = e->editor->Find("f.PlayerMove.speed")) {
    float labelW = std::max(80.f, f->w * 0.4f), ix = f->x + 12 + labelW + 8, iw = f->x + f->w - 10 - ix, y = f->y + f->h * 0.5f;
    e->window.InjectEvent({T::MouseMove, (ix + 3) * scale, y * scale});
    e->window.InjectEvent({T::MouseDown, (ix + 3) * scale, y * scale, 1});
    for (int i = 1; i <= 8; ++i) e->window.InjectEvent({T::MouseMove, (ix + 2 + (iw - 4) * 0.5f * i / 8) * scale, y * scale});
    e->window.InjectEvent({T::MouseUp, (ix + 2 + (iw - 4) * 0.5f) * scale, y * scale, 1});
  }
  if (frame == 4) { v[0] = value("g6", "PlayerMove", "speed"); v[1] = std::to_string(m->bus.UndoDepth()); key(bdg::Key::Z, true); }
  if (frame == 6) {
    v[2] = value("g6", "PlayerMove", "speed");
    key(bdg::Key::F2, false);
    bdg::InputEvent t{T::Text};
    t.text = "Hero";
    e->window.InjectEvent(t);
    key(bdg::Key::Enter, false);
  }
  if (frame == 8) {
    v[3] = m->scene.Find("g6") ? m->scene.Find("g6")->name : "?";
    bdg::Command c;
    c.name = "set"; c.target = "g6"; c.args = R"({"path":"Health.current","value":10})";
    m->bus.Execute(c);
  }
  if (frame == 9) if (bdg::Widget* b = e->editor->Find("b.Health.Revive")) e->editor->ScrollIntoView(*b); // below the fold
  if (frame == 10) if (bdg::Widget* b = e->editor->Find("b.Health.Revive")) clickAt(b->x + b->w * 0.5f, b->y + b->h * 0.5f);
  if (frame == 12) { v[4] = value("g6", "Health", "current"); key(bdg::Key::Z, true); }
  if (frame == 14) { v[5] = value("g6", "Health", "current"); m->Select("g2"); }
  if (frame == 16) {
    v[6] = e->editor->Find("f.Light.range") ? "0" : "1"; // the Sun is Directional: range hidden
    bdg::Command c;
    c.name = "set"; c.target = "g2"; c.args = R"({"path":"Light.type","value":"Point"})";
    m->bus.Execute(c);
  }
  if (frame == 18) v[7] = e->editor->Find("f.Light.range") ? "1" : "0";
}

// Scripted multi-edit check (BDG_MULTI_TEST=1, BDG_PRESETS=<folder>): Ctrl-click Crystal + Player, edit a common
// field once for both, undo; copy Player's Health, paste it as a new component on Crystal, save and apply a preset.
void InjectMultiTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto value = [&](const char* id, const char* comp, const char* field) {
    std::string v;
    bdg::SceneObject* o = m->scene.Find(id);
    if (bdg::Component* c = o ? o->Find(comp) : nullptr) bdg::Registry::Get(c->data, *c->type, field, v);
    return v;
  };
  auto click = [&](const char* id, bool ctrl) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w) { bdg::LogError("multi test: no widget {}", id); return; }    float x = (w->x + w->w * 0.5f) * scale, y = (w->y + w->h * 0.5f) * scale;
    bdg::InputEvent mv{T::MouseMove, x, y}, dn{T::MouseDown, x, y, 1}, up{T::MouseUp, x, y, 1};
    dn.ctrl = up.ctrl = ctrl;
    e->window.InjectEvent(mv); e->window.InjectEvent(dn); e->window.InjectEvent(up);
  };
  auto key = [&](bdg::Key k, bool ctrl) { bdg::InputEvent ev{T::KeyDown}; ev.key = k; ev.ctrl = ctrl; e->window.InjectEvent(ev); };
  auto set = [&](const char* id, const char* args) { bdg::Command c; c.name = "set"; c.target = id; c.args = args; m->bus.Execute(c); };
  auto* v = e->multiTestValues;
  if (frame == 2) { click("h.g8", false); }
  if (frame == 4) { click("h.g6", true); }
  if (frame == 6) {
    std::string common;
    for (const char* c : {"Transform", "MeshRenderer", "PlayerMove", "Health"}) if (e->editor->Find(std::string("c.") + c)) common += (common.empty() ? "" : ",") + std::string(c);
    v[0] = common;
    bdg::Widget* mesh = e->editor->Find("f.MeshRenderer.mesh");
    bdg::Widget* scaleF = e->editor->Find("f.Transform.scale");
    v[1] = mesh && mesh->text2 == bdg::EditorModel::kMixed ? "1" : "0";
    v[2] = scaleF && scaleF->text2 == "1, 1, 1" ? "1" : "0";
    v[4] = std::to_string(m->bus.UndoDepth());
    if (bdg::Widget* mat = e->editor->Find("f.MeshRenderer.material")) {
      e->editor->Focus(mat);
      key(bdg::Key::A, true);
      bdg::InputEvent t{T::Text};
      t.text = "M_Shared";
      e->window.InjectEvent(t);
      key(bdg::Key::Enter, false);
    }
  }
  if (frame == 8) {
    v[3] = value("g8", "MeshRenderer", "material") + "," + value("g6", "MeshRenderer", "material");
    v[4] = std::to_string(m->bus.UndoDepth() - std::stoul(v[4]));
    key(bdg::Key::Z, true);
  }
  if (frame == 10) {
    v[5] = value("g8", "MeshRenderer", "material") + "," + value("g6", "MeshRenderer", "material");
    set("g6", R"({"path":"Health.maxHealth","value":321})");
    m->Select("g6");
  }
  if (frame == 11) if (bdg::Widget* insp = e->editor->Find("inspector")) { // wheel down over the Inspector
    bdg::InputEvent wh{T::Wheel, (insp->x + 40) * scale, (insp->y + 200) * scale};
    wh.wheel = -30;
    e->window.InjectEvent(wh);
  }
  if (frame == 12) { click("cc.Health", false); m->Select("g8"); }
  if (frame == 14) click("insp.pastenew", false);
  if (frame == 16) { v[6] = value("g8", "Health", "maxHealth"); click("cs.Health", false); }
  if (frame == 18) { set("g8", R"({"path":"Health.maxHealth","value":5})"); }
  if (frame == 20) click("ca.Health", false);
  if (frame == 22) v[7] = value("g8", "Health", "maxHealth");
}
