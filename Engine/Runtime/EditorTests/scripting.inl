// Scripted editor checks: scripting (moved out of Engine.cpp). Included by Engine.cpp just before the
// C API, in the same translation unit: bdg_engine and every editor helper are visible here.
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h) instead of editing Engine.cpp.

// Scripted check of 4-1 (BDG_CSHARP_TEST=1): the editor starts .NET, C# finds an object of the open scene, calls
// Health.Damage, reads Health.Ratio and writes fields; the edits are undo steps from "script" and Ctrl+Z takes
// them back.
void InjectCSharpTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  if (!m || frame != 3) return;
  bdg::Command add;
  add.name = "add";
  add.args = R"({"id":"cs1","name":"Scripted"})";
  bdg::Command comp;
  comp.name = "addcomp";
  comp.target = "cs1";
  comp.args = R"({"type":"Health","value":{"current":70}})";
  std::string err;
  if (!m->bus.Execute(add, &err) || !m->bus.Execute(comp, &err)) bdg::LogError("csharp test: {}", err);
  if (m->scene.Find("cs1") && !m->scene.Find("cs1")->Find("Transform")) {
    comp.args = R"({"type":"Transform"})";
    if (!m->bus.Execute(comp, &err)) bdg::LogError("csharp test: {}", err);
  }
  bdg::DotNetHost& host = bdg::DotNetHost::Instance();
  const bool started = host.Start({}, &err);
  host.Attach(&m->scene, &m->bus);
  const size_t depth = m->bus.UndoDepth();
  const int self = started ? host.SelfTest("Scripted") : -2;
  const auto* h = m->scene.Find("cs1") ? m->scene.Find("cs1")->Find("Health") : nullptr;
  std::string current, max;
  if (h) {
    bdg::Registry::Get(h->data, *h->type, "current", current);
    bdg::Registry::Get(h->data, *h->type, "maxHealth", max);
  }
  const size_t steps = m->bus.UndoDepth() - depth;
  const auto hist = m->bus.History();
  const bool fromScript = !hist.empty() && hist.back().source == "script";
  for (size_t i = 0; i < steps; ++i) m->bus.Undo();
  std::string undone;
  if (h) bdg::Registry::Get(h->data, *h->type, "current", undone);
  host.Attach(nullptr, nullptr);
  bdg::LogInfo("csharp test: started={} self={} values={} steps={} script={} undo={}", started ? 1 : 0, self,
               current == "60" && max == "250" ? 1 : 0, steps, fromScript ? 1 : 0, undone == "70" ? 1 : 0);
}

// Scripted check of 4-2 (BDG_BEHAVIOUR_TEST=1 with BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll): an object
// with the C# Spinner and LifecycleProbe behaviours; Play runs them (the object turns), disabling the probe in the
// Inspector's way (a set command) calls OnDisable / OnEnable, Stop calls OnDestroy and the edited world comes back.
void InjectBehaviourTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  static std::string probeLine;
  auto run = [&](const char* name, const char* target, const char* args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("behaviour test: {}", err);
  };
  auto rotY = [&] {
    const bdg::SceneObject* o = m->scene.Find("bh1");
    const bdg::Component* t = o ? const_cast<bdg::SceneObject*>(o)->Find("Transform") : nullptr;
    return t ? static_cast<const bdg::Transform*>(t->data)->rotation.y : -1.f;
  };
  static int instances = 0;
  static float spun = 0;
  if (frame == 2) {
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.rfind("probe: bh1 ", 0) == 0) probeLine = l; });
    run("add", "", R"({"id":"bh1","name":"Spinning"})");
    run("addcomp", "bh1", R"({"type":"Spinner"})");
    run("addcomp", "bh1", R"({"type":"LifecycleProbe"})");
  }
  if (frame == 3) TogglePlay(e);
  if (frame > 3 && frame < 14) std::this_thread::sleep_for(std::chrono::milliseconds(15)); // let game time pass
  if (frame == 8) run("set", "bh1", R"({"path":"LifecycleProbe.enabled","value":false})");
  if (frame == 10) run("set", "bh1", R"({"path":"LifecycleProbe.enabled","value":true})");
  if (frame == 12) {
    instances = (int)e->play.Scripts().Instances();
    spun = rotY();
  }
  if (frame == 14) TogglePlay(e);
  if (frame == 16) {
    bdg::SetLogHook({});
    const bool order = probeLine.find("events=Awake,OnEnable,Start,Update,OnDisable,OnEnable,Update,OnDisable,OnDestroy") != std::string::npos;
    bdg::LogInfo("behaviour test: types={} instances={} spun={} order={} restored={} ended={}", bdg::DotNetHost::Instance().BehaviourTypeCount(),
                 instances, spun > 0.01f ? 1 : 0, order ? 1 : 0, rotY() == 0.f ? 1 : 0, e->play.Scripts().Instances() == 0 ? 1 : 0);
  }
}

// Scripted check of 4-3 (BDG_EDITFIELD_TEST=1 with BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll): the C#
// Spinner's [Edit] fields are component fields with the C# defaults (speed 90), the Inspector shows speed as a slider
// ([Range]) and axis as a choice; a saved speed of 0 reaches the script before it runs (no turn), an Inspector edit
// while playing reaches it too (it turns), the script's own counter comes back to the component, and Stop restores 0.
void InjectEditFieldTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto run = [&](const char* name, const char* target, const char* args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("edit field test: {}", err);
  };
  auto get = [&](const char* path) {
    std::string json;
    bdg::SceneObject* o = m->scene.Find("bh2");
    bdg::Component* c = o ? o->Find("Spinner") : nullptr;
    if (c) bdg::Registry::Get(c->data, *c->type, path, json);
    return json;
  };
  auto rotY = [&] {
    bdg::SceneObject* o = m->scene.Find("bh2");
    const bdg::Component* t = o ? o->Find("Transform") : nullptr;
    return t ? static_cast<const bdg::Transform*>(t->data)->rotation.y : -1.f;
  };
  static std::string v[6];
  if (frame == 2) {
    run("add", "", R"({"id":"bh2","name":"Spinning"})");
    run("addcomp", "bh2", R"({"type":"Spinner"})");
    v[0] = get("speed") == "90" && get("axis") == "\"Y\"" && get("label") == "\"spinner\"" ? "1" : "0(" + get("speed") + "," + get("axis") + ")";
    run("set", "bh2", R"({"path":"Spinner.speed","value":0})");
    m->Select("bh2");
  }
  if (frame == 4) {
    bdg::Widget* speed = e->editor->Find("f.Spinner.speed");
    bdg::Widget* axis = e->editor->Find("f.Spinner.axis");
    v[1] = speed && speed->slider && speed->sliderMax == 720 && axis && axis->kind == bdg::WidgetKind::Choice ? "1" : "0";
    TogglePlay(e);
  }
  if (frame > 4 && frame < 14) std::this_thread::sleep_for(std::chrono::milliseconds(15)); // game time passes
  if (frame == 8) {
    v[2] = rotY() == 0.f ? "1" : std::format("0({})", rotY());
    run("set", "bh2", R"({"path":"Spinner.speed","value":180})"); // the Inspector's way, while playing
  }
  if (frame == 12) {
    v[3] = rotY() > 0.01f ? "1" : "0";
    v[4] = std::atoi(get("frames").c_str()) > 0 ? "1" : "0(" + get("frames") + ")";
  }
  if (frame == 13) TogglePlay(e);
  if (frame == 15) {
    v[5] = get("speed") == "0" && get("frames") == "0" ? "1" : "0";
    bdg::LogInfo("edit field test: defaults={} inspector={} saved={} push={} pull={} restored={}", v[0], v[1], v[2], v[3], v[4], v[5]);
  }
}

// Scripted check of 4-4 (BDG_HOTRELOAD_TEST=1): a Spinner (speed 45, label "kept") runs in play mode from a copy
// of the samples; the v2 build (Spinner gains `bonus = 7`, counted up in Update) is written over the copy and the
// watcher reloads it: the component keeps its values and gains bonus, the instance keeps its private frame count
// and runs the new code (bonus grows), Awake / Start do not run again, and the reload is well under 2 seconds.
void InjectHotReloadTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  auto run = [&](const char* name, const char* target, const char* args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("hot reload test: {}", err);
  };
  auto get = [&](const char* path) {
    std::string json;
    bdg::SceneObject* o = m->scene.Find("hr1");
    bdg::Component* c = o ? o->Find("Spinner") : nullptr;
    if (c) bdg::Registry::Get(c->data, *c->type, path, json);
    return json;
  };
  static int framesBefore = 0;
  static std::string v[5];
  if (frame == 2) {
    run("add", "", R"({"id":"hr1","name":"Hot"})");
    run("addcomp", "hr1", R"({"type":"Spinner","value":{"speed":45,"label":"kept"}})");
    m->Select("hr1");
  }
  if (frame == 3) TogglePlay(e);
  if (frame > 3 && frame < 16) std::this_thread::sleep_for(std::chrono::milliseconds(15));
  if (frame == 7) {
    framesBefore = std::atoi(get("frames").c_str());
    const std::filesystem::path managed = bdg::PathU8(bdg::DotNetHost::DefaultManagedDir());
    const std::filesystem::path dst = std::filesystem::current_path() / ".cache" / "hotreload-test" / "Bidulgi.Samples.dll";
    std::error_code ec;
    std::filesystem::copy_file(managed / "HotReloadV2" / "Bidulgi.Samples.dll", dst, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::last_write_time(dst, std::filesystem::file_time_type::clock::now(), ec); // a new time for sure
    if (ec) bdg::LogError("hot reload test: {}", ec.message());
    PollScriptReload(e); // sees the new time
  }
  if (frame == 8) PollScriptReload(e); // the time stayed: reload
  if (frame == 12) {
    const auto& host = bdg::DotNetHost::Instance();
    v[0] = host.Reloads() == 1 && host.LastReloadMs() < 2000 ? "1" : std::format("0({},{:.0f}ms)", host.Reloads(), host.LastReloadMs());
    v[1] = get("speed") == "45" && get("label") == "\"kept\"" ? "1" : "0(" + get("speed") + get("label") + ")";
    v[2] = std::atoi(get("bonus").c_str()) > 7 ? "1" : "0(" + get("bonus") + ")";
    v[3] = std::atoi(get("frames").c_str()) > framesBefore && framesBefore > 0 && e->play.Scripts().Instances() == 1 ? "1" : std::format("0({}->{})", framesBefore, get("frames"));
  }
  if (frame == 14) TogglePlay(e);
  if (frame == 16) {
    v[4] = get("bonus") == "7" && get("speed") == "45" ? "1" : "0(" + get("bonus") + ")"; // the edited world, with the new field
    bdg::LogInfo("hot reload test: reloaded={} kept={} newcode={} state={} stopped={} ms={:.0f}", v[0], v[1], v[2], v[3], v[4],
                 bdg::DotNetHost::Instance().LastReloadMs());
  }
}

// Scripted check of 4-4c (BDG_EASYAPI_TEST=1 with BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll): the C#
// EasyProbe runs in play mode and uses [Auto], Get / GetInChildren, Find / FindAll / FindNearest, Spawn, Destroy (now
// and later), After, Every (+ Cancel), await NextFrame / Wait, an awaited Transform tween and seeded Random, then logs
// one line. Stop must take every spawned object away again and leave no timer behind.
void InjectEasyApiTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  static std::string probe;
  auto run = [&](const char* name, const char* target, const char* args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("easy api test: {}", err);
  };
  if (frame == 2) {
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.rfind("easy probe: ", 0) == 0) probe = l; });
    run("add", "", R"({"id":"ez1","name":"Prober"})");
    run("add", "", R"({"id":"ez2","name":"Kid","parent":"ez1"})");
    run("addcomp", "ez2", R"({"type":"Health"})");
    run("add", "", R"({"id":"ez3","name":"Coin"})");
    run("set", "ez3", R"({"path":"Transform.position","value":[10,0,0]})");
    run("addcomp", "ez1", R"({"type":"EasyProbe"})");
  }
  if (frame == 3) TogglePlay(e);
  if (frame > 3 && frame < 70 && probe.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(15)); // game time passes
  if (frame == 72) TogglePlay(e);
}
void ReportEasyApiTest(bdg_engine* e) {
  bdg::SetLogHook({});
  bdg::EditorModel* m = e->model.get();
  size_t coins = 0;
  if (m)
    for (const std::string& id : m->scene.Roots())
      if (const bdg::SceneObject* o = m->scene.Find(id); o && (o->name == "Coin" || o->name == "Marker")) ++coins;
  bdg::LogInfo("easy api test: restored={} instances={}", coins == 1 ? 1 : 0, e->play.Scripts().Instances());
}
BDG_EDITOR_TEST("BDG_EASYAPI_TEST", InjectEasyApiTest, ReportEasyApiTest)
