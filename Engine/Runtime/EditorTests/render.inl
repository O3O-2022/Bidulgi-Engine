// Scripted editor checks: render (moved out of Engine.cpp). Included by Engine.cpp just before the
// C API, in the same translation unit: bdg_engine and every editor helper are visible here.
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h) instead of editing Engine.cpp.

// Scripted check of E-6a (BDG_POSTFX_TEST=1): a Post Process Volume added by commands; on the Scene view's own
// picture: vignette darkens the corners, grain changes a flat patch between frames, chromatic aberration changes
// the edges but not the centre, +2 stops brightens, auto exposure brings an over-exposed picture back down, a
// bounded volume far from the camera does nothing, and removing the volume gives the first picture back.
void InjectPostFxTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  auto* v = e->postTestValues;
  struct Shot { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
  auto grab = [&] { Shot s; sv->ReadPixels(s.w, s.h, s.px); return s; };
  auto mean = [](const Shot& s, float fx, float fy, int r) { // average brightness of a patch (fx, fy from the top-left)
    double sum = 0;
    int n = 0;
    const int cx = (int)(fx * s.w), cy = s.h - 1 - (int)(fy * s.h);
    for (int y = cy - r; y <= cy + r; ++y)
      for (int x = cx - r; x <= cx + r; ++x)
        if (x >= 0 && y >= 0 && x < s.w && y < s.h) {
          const uint8_t* p = &s.px[((size_t)y * s.w + x) * 4];
          sum += (p[0] + p[1] + p[2]) / 3.0;
          ++n;
        }
    return n ? sum / n : 0.0;
  };
  auto differ = [](const Shot& a, const Shot& b, float fx0, float fx1) { // pixels that changed in a vertical band
    int n = 0;
    if (a.px.size() != b.px.size()) return -1;
    for (int y = 0; y < a.h; ++y)
      for (int x = (int)(fx0 * a.w); x < (int)(fx1 * a.w); ++x) {
        const size_t i = ((size_t)y * a.w + x) * 4;
        n += std::abs(a.px[i] - b.px[i]) + std::abs(a.px[i + 1] - b.px[i + 1]) + std::abs(a.px[i + 2] - b.px[i + 2]) > 24;
      }
    return n;
  };
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("postfx test: {}", err);
  };
  auto set = [&](const char* field, const std::string& value) {
    run("set", "pp", std::format(R"({{"path":"PostProcessVolume.{}","value":{}}})", field, value));
  };
  static Shot base, a, b;
  if (frame == 3) {
    base = grab();
    run("add", "", R"({"id":"pp","name":"Post Process Volume"})");
    run("addcomp", "pp", R"({"type":"PostProcessVolume"})");
    set("vignette", "1");
    set("vignetteSmoothness", "0.2");
  }
  if (frame == 6) {
    const Shot s = grab();
    const double c0 = mean(base, 0.97f, 0.96f, 4), c1 = mean(s, 0.97f, 0.96f, 4), m0 = mean(base, 0.5f, 0.5f, 4), m1 = mean(s, 0.5f, 0.5f, 4);
    v[0] = std::format("{}", c1 < c0 * 0.6 && std::abs(m1 - m0) < 6 ? 1 : 0);
    if (v[0] != "1") bdg::LogError("postfx test: vignette corner {:.1f} -> {:.1f}, centre {:.1f} -> {:.1f}", c0, c1, m0, m1);
    set("vignette", "0");
    set("filmGrain", "1");
  }
  if (frame == 8) a = grab();
  if (frame == 9) {
    b = grab();
    const int n = differ(a, b, 0.4f, 0.6f);
    v[1] = std::format("{}", n > 200 ? 1 : 0);
    if (v[1] != "1") bdg::LogError("postfx test: grain changed {} pixels", n);
    set("filmGrain", "0");
  }
  if (frame == 11) { // aberration samples red and blue apart and leaves green where it was
    a = grab();
    set("chromaticAberration", "1");
  }
  if (frame == 13) {
    const Shot s = grab();
    int green = 0, redBlue = 0;
    for (size_t i = 0; s.px.size() == a.px.size() && i < s.px.size(); i += 4) {
      green += std::abs(s.px[i + 1] - a.px[i + 1]) > 3;
      redBlue += std::abs(s.px[i] - a.px[i]) + std::abs(s.px[i + 2] - a.px[i + 2]) > 24;
    }
    v[2] = std::format("{}", s.px.size() == a.px.size() && redBlue > 200 && green < redBlue / 20 ? 1 : 0);
    if (v[2] != "1") bdg::LogError("postfx test: aberration red/blue {} green {}", redBlue, green);
    set("chromaticAberration", "0");
    set("exposureCompensation", "2");
  }
  static double saturated = 0;
  if (frame == 15) {
    const Shot s = grab();
    const double bright = mean(s, 0.5f, 0.75f, 6), before = mean(base, 0.5f, 0.75f, 6);
    v[3] = std::format("{}", bright > before + 25 ? 1 : 0);
    if (v[3] != "1") bdg::LogError("postfx test: exposure {:.1f} -> {:.1f}", before, bright);
    set("exposureCompensation", "0");
    run("set", "g2", R"({"path":"Light.intensity","value":48})"); // the sun 16 times brighter: the picture burns out
  }
  if (frame == 17) {
    saturated = mean(grab(), 0.5f, 0.75f, 6);
    set("autoExposure", "true"); // the eye adapts down
    set("adaptationSpeed", "10");
    set("autoMinEV", "-8");
  }
  if (frame == 20) {
    const double adapted = mean(grab(), 0.5f, 0.75f, 6);
    v[4] = std::format("{}", adapted < saturated - 25 ? 1 : 0);
    if (v[4] != "1") bdg::LogError("postfx test: auto exposure {:.1f} (burnt out: {:.1f})", adapted, saturated);
    run("set", "g2", R"({"path":"Light.intensity","value":3})");
    set("autoExposure", "false");
    set("vignette", "1");
    set("unbound", "false");
    set("size", "[1,1,1]");
    set("blendRadius", "1");
  }
  if (frame == 23) {
    const Shot s = grab();
    const double c0 = mean(base, 0.97f, 0.96f, 4), c1 = mean(s, 0.97f, 0.96f, 4);
    const bdg::PostEffects3D inside = sv->PostAt(glm::vec3(m->scene.WorldMatrix("pp")[3]));
    v[5] = std::format("{}", std::abs(c1 - c0) < 6 && inside.vignette == 1.f ? 1 : 0);
    if (v[5] != "1") bdg::LogError("postfx test: bounded corner {:.1f} vs {:.1f}, inside vignette {}", c0, c1, inside.vignette);
    run("del", "pp", "{}");
  }
  if (frame == 26) {
    const Shot s = grab();
    const int n = differ(base, s, 0.0f, 1.0f);
    v[6] = std::format("{}", n >= 0 && n < 50 ? 1 : 0);
    if (v[6] != "1") bdg::LogError("postfx test: after removing, {} pixels differ", n);
  }
}

// Scripted check of E-9 (BDG_SHAKE_TEST=1): a CameraShakeSource (bdg:Heavy, play on start) near the camera and
// one far away; Play: only the near one shakes the game camera (the Game view's picture moves frame to frame),
// it ends after its duration, Camera.Shake run as a Graph node starts another, and Stop leaves no offset.
void InjectShakeTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  auto* v = e->shakeTestValues;
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("shake test: {}", err);
  };
  static std::vector<uint8_t> prev;
  static int moved = 0, endedAt = 0, nodeAt = 0;
  if (frame == 2) {
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("game");
    const glm::vec3 cam = glm::vec3(m->scene.WorldMatrix("g1")[3]);
    run("add", "", R"({"id":"boom","name":"Boom"})");
    run("addcomp", "boom", R"({"type":"CameraShakeSource","value":{"shake":"bdg:Heavy","playOnStart":true,"innerRadius":5,"outerRadius":30}})");
    run("set", "boom", std::format(R"({{"path":"Transform.position","value":[{},{},{}]}})", cam.x + 1, cam.y, cam.z));
    run("add", "", R"({"id":"far","name":"Far"})");
    run("addcomp", "far", R"({"type":"CameraShakeSource","value":{"shake":"bdg:Explosion","playOnStart":true,"innerRadius":1,"outerRadius":5}})");
    run("set", "far", R"({"path":"Transform.position","value":[500,0,500]})");
  }
  if (frame == 4) TogglePlay(e);
  if (frame == 6) v[0] = std::format("{}", e->play.Playing() && e->play.ShakesPlaying() == 1 && e->play.Shake().Size() > 0 ? 1 : 0);
  if (frame >= 6 && frame <= 12) { // the Game view's picture changes while the camera shakes
    int32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    sv->ReadGamePixels(w, h, px);
    if (!prev.empty() && prev.size() == px.size() && prev != px) ++moved;
    prev = px;
  }
  if (frame == 13) v[1] = std::format("{}", moved >= 4 ? 1 : 0);
  if (frame > 13 && !endedAt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(40)); // let game time pass the shake's 0.8 s
    if (e->play.ShakesPlaying() == 0) {
      endedAt = frame;
      v[2] = std::format("{}", e->play.Shake().Size() == 0 && e->play.Time() >= 0.8 ? 1 : 0);
      const auto lib = bdg::BuildGraphNodeLibrary("Camera.Shake");
      std::string err;
      if (lib.empty() || !bdg::RunGraphNode(m->bus, m->scene, lib[0], "g1", {"\"bdg:Light\"", "1"}, nullptr, &err))
        bdg::LogError("shake test: Camera.Shake node: {}", err);
      nodeAt = frame;
    }
    if (frame > 200) bdg::LogError("shake test: the shake did not end (game time {})", e->play.Time());
  }
  if (nodeAt && frame == nodeAt + 2) {
    v[3] = std::format("{}", e->play.ShakesPlaying() == 1 ? 1 : 0);
    TogglePlay(e);
  }
  if (nodeAt && frame == nodeAt + 4) v[4] = std::format("{}", !e->play.Playing() && e->play.Shake().Size() == 0 ? 1 : 0);
}

// Scripted check of E-7 (BDG_SHAFTS_TEST=1): the editor camera turned toward the sun; light shafts on find the sun
// on the picture and brighten the sky around it; a board hiding the sun cuts them down (occlusion); with the
// camera turned away no shafts are drawn.
void InjectShaftsTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv || !e->r3d) return;
  auto* v = e->shaftTestValues;
  struct Shot { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
  auto grab = [&] { Shot s; sv->ReadPixels(s.w, s.h, s.px); return s; };
  auto brighter = [](const Shot& a, const Shot& b) {
    int n = 0;
    for (size_t i = 0; a.px.size() == b.px.size() && i < a.px.size(); i += 4)
      n += (b.px[i] + b.px[i + 1] + b.px[i + 2]) - (a.px[i] + a.px[i + 1] + a.px[i + 2]) > 9;
    return n;
  };
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("shafts test: {}", err);
  };
  auto shafts = [&](bool on) { run("set", "g2", std::format(R"({{"path":"Light.lightShafts","value":{}}})", on ? "true" : "false")); };
  static Shot off;
  static int open = 0;
  if (frame == 3) { // after the Scene view's own start-up framing: look at the sun from 1.6 m
    sv->showGrid = false;
    sv->iconSize = 0;
    m->SetSelection({});
    const glm::vec3 toSun = -glm::normalize(glm::vec3(m->scene.WorldMatrix("g2") * glm::vec4(0, 0, -1, 0)));
    float best = -2;
    bdg::EditorCamera c = sv->camera;
    for (int yaw = -180; yaw < 180; yaw += 2)
      for (int pitch = -80; pitch <= 80; pitch += 2) {
        c.yaw = (float)yaw;
        c.pitch = (float)pitch;
        const float d = glm::dot(c.Forward(), toSun);
        if (d > best) { best = d; sv->camera.yaw = c.yaw; sv->camera.pitch = c.pitch; }
      }
    sv->camera.pitch = std::max(sv->camera.pitch - 12.f, -80.f); // the sun in the upper half of the picture
    sv->camera.pivot = glm::vec3(0, 1.6f, 0) + sv->camera.Forward() * sv->camera.distance; // eye at 1.6 m
    run("set", "g2", R"({"path":"Light.lightShaftIntensity","value":1})");
  }
  if (frame == 6) {
    off = grab();
    shafts(true);
  }
  if (frame == 9) {
    v[0] = std::format("{}", sv->RenderStatsSun(e->r3d.get()) ? 1 : 0);
    open = brighter(off, grab());
    v[1] = std::format("{}", open > 800 ? 1 : 0);
    if (v[1] != "1") bdg::LogError("shafts test: {} pixels brightened", open);
    shafts(false);
    // a board 8 m out, square to the view, hiding the sun (and the sky right around it)
    const glm::vec3 toSun = -glm::normalize(glm::vec3(m->scene.WorldMatrix("g2") * glm::vec4(0, 0, -1, 0)));
    const glm::vec3 at = sv->camera.Position() + toSun * 8.f;
    const float yawDeg = glm::degrees(std::atan2(-toSun.x, -toSun.z)), pitchDeg = glm::degrees(std::asin(toSun.y));
    run("add", "", R"({"id":"board","name":"Board"})");
    run("addcomp", "board", R"({"type":"Blockout","value":{"shape":"Box","size":[4,4,0.2]}})");
    run("set", "board", std::format(R"({{"path":"Transform.rotation","value":[{},{},0]}})", pitchDeg, yawDeg));
    run("set", "board", std::format(R"({{"path":"Transform.position","value":[{},{},{}]}})", at.x, at.y - 2, at.z));
  }
  if (frame == 12) {
    off = grab();
    shafts(true);
  }
  if (frame == 15) {
    const int blocked = brighter(off, grab());
    v[2] = std::format("{}", blocked < open / 4 ? 1 : 0);
    if (v[2] != "1") bdg::LogError("shafts test: open {} pixels, sun hidden {}", open, blocked);
    sv->camera.yaw += 180; // away from the sun
    sv->camera.pitch = -20;
  }
  if (frame == 18) v[3] = std::format("{}", !sv->RenderStatsSun(e->r3d.get()) ? 1 : 0);
}

// Scripted check of E-6b (BDG_POSTFX2_TEST=1, BDG_QUALITY=high for bloom): depth of field focused a few cm from
// the camera blurs the picture (less edge energy), focused on the Player keeps it sharp around the Player, lens
// flare adds light from a burnt-out picture, and removing the volume gives the first picture back.
void InjectPostFx2Test(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  auto* v = e->post2TestValues;
  struct Shot { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
  auto grab = [&] { Shot s; sv->ReadPixels(s.w, s.h, s.px); return s; };
  auto energy = [](const Shot& s, int x0, int y0, int x1, int y1) { // sharpness: green-channel second differences
    double sum = 0;                                                    // (smooth shading gradients cancel out)
    auto g = [&](int x, int y) { return (int)s.px[((size_t)y * s.w + x) * 4 + 1]; };
    for (int y = std::max(y0, 1); y + 1 < std::min(y1, s.h); ++y)
      for (int x = std::max(x0, 1); x + 1 < std::min(x1, s.w); ++x)
        sum += std::abs(4 * g(x, y) - g(x - 1, y) - g(x + 1, y) - g(x, y - 1) - g(x, y + 1));
    return sum;
  };
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("postfx2 test: {}", err);
  };
  auto set = [&](const char* field, const std::string& value) {
    run("set", "pp", std::format(R"({{"path":"PostProcessVolume.{}","value":{}}})", field, value));
  };
  static Shot base, a;
  static double blurredPatch = 0;
  static int px = 0, py = 0; // the Player on the picture (rows from the bottom)
  if (frame == 1) { // the grid and the selection outline are drawn after post-processing: measure the scene alone
    sv->showGrid = false;
    m->SetSelection({});
  }
  if (frame == 3) {
    base = grab();
    run("add", "", R"({"id":"pp","name":"Post Process Volume"})");
    run("addcomp", "pp", R"({"type":"PostProcessVolume"})");
    set("dofBlur", "1");
    set("dofFocusDistance", "0.2");
    set("dofFocusRange", "0.1");
  }
  if (frame == 6) {
    const Shot s = grab();
    const double e0 = energy(base, 0, 0, base.w, base.h), e1 = energy(s, 0, 0, s.w, s.h);
    v[0] = std::format("{}", e1 < e0 * 0.6 ? 1 : 0);
    if (v[0] != "1") bdg::LogError("postfx2 test: edge energy {:.0f} -> {:.0f}", e0, e1);
    const bdg::Camera3D cam = sv->camera.ToCamera();
    const glm::vec3 player = glm::vec3(m->scene.WorldMatrix("g6")[3]);
    float fx = 0, fy = 0;
    if (bdg::ProjectToPixel(cam, s.w, s.h, player, fx, fy)) { px = (int)fx; py = (int)fy; }
    blurredPatch = energy(s, px - 40, py - 40, px + 40, py + 40);
    set("dofFocusDistance", std::format("{}", glm::length(player - cam.position)));
    set("dofFocusRange", "1.5");
  }
  if (frame == 9) {
    const Shot s = grab();
    const double sharp = energy(s, px - 40, py - 40, px + 40, py + 40), orig = energy(base, px - 40, py - 40, px + 40, py + 40);
    v[1] = std::format("{}", sharp > blurredPatch * 1.4 && sharp > orig * 0.7 ? 1 : 0);
    if (v[1] != "1") bdg::LogError("postfx2 test: player patch blurred {:.0f}, focused {:.0f}, original {:.0f}", blurredPatch, sharp, orig);
    set("dofBlur", "0");
    set("exposureCompensation", "1.5"); // the sky and lit ground above white: something for the flare
  }
  if (frame == 12) {
    a = grab();
    set("lensFlare", "1");
  }
  if (frame == 15) {
    const Shot s = grab();
    int brighter = 0;
    for (size_t i = 0; s.px.size() == a.px.size() && i < s.px.size(); i += 4)
      brighter += (s.px[i] + s.px[i + 1] + s.px[i + 2]) - (a.px[i] + a.px[i + 1] + a.px[i + 2]) > 6;
    v[2] = std::format("{}", brighter > 500 ? 1 : 0);
    if (v[2] != "1") bdg::LogError("postfx2 test: flare brightened {} pixels", brighter);
    run("del", "pp", "{}");
  }
  if (frame == 18) {
    const Shot s = grab();
    int n = 0;
    for (size_t i = 0; s.px.size() == base.px.size() && i < s.px.size(); i += 4)
      n += std::abs(s.px[i] - base.px[i]) + std::abs(s.px[i + 1] - base.px[i + 1]) + std::abs(s.px[i + 2] - base.px[i + 2]) > 24;
    v[3] = std::format("{}", s.px.size() == base.px.size() && n < 50 ? 1 : 0);
    if (v[3] != "1") bdg::LogError("postfx2 test: after removing, {} pixels differ", n);
  }
}

// `bdg render compare` until the CLI exists (queue 3-9): BDG_RENDER_COMPARE=<baseline folder> visits camera bookmarks
// 1-9 of the open scene, compares each picture with <folder>/bookmark_<n>.png (FLIP-style error, Core/ImageCompare)
// and writes <out>/<n>.png plus <out>/<n>_diff.png (heat map); a missing baseline is created from this run.
// <out> = BDG_COMPARE_OUT or .cache/compare. Threshold: BDG_COMPARE_THRESHOLD (mean error, default 0.03).
//
// Regression mode (queue 3-9b): BDG_RENDER_COMPARE=repo (or repo:<root>) compares against the baselines committed in
// <root>/<renderer>/<scene>/ (root Tests/RenderBaselines): one set per renderer family, because a software renderer
// and each GPU vendor rasterise a little differently ("software" = Mesa llvmpipe in CI, "gpu-nvidia" the reference
// GPU, "gpu-amd", "gpu-intel"). Each bookmark is rendered at a fixed 480 x 270 (the same on every machine, whatever
// the panel size or display scale). The threshold comes from <set>/meta.json ("threshold"), else 0.02.
// A missing baseline is not a failure: the picture goes to <out>/bookmark_<n>.png (copy it in to accept it) and the
// summary counts it as missing; BDG_COMPARE_BLESS=1 writes the pictures (and meta.json) into the set instead.
std::string RendererFamily(const std::string& gpuName) {
  std::string g;
  for (char c : gpuName) g += (char)std::tolower((unsigned char)c);
  if (g.find("llvmpipe") != std::string::npos || g.find("softpipe") != std::string::npos || g.find("swiftshader") != std::string::npos ||
      g.find("basic render") != std::string::npos)
    return "software";
  if (g.find("nvidia") != std::string::npos || g.find("geforce") != std::string::npos) return "gpu-nvidia";
  if (g.find("amd") != std::string::npos || g.find("radeon") != std::string::npos) return "gpu-amd";
  if (g.find("intel") != std::string::npos) return "gpu-intel";
  return "gpu-other";
}

void RenderCompareStep(bdg_engine* e, int frame, const char* baseDir) {
  namespace fs = std::filesystem;
  static int created = 0, failed = 0, compared = 0, missing = 0;
  static double worst = 0;
  const int slot = (frame - 4) / 4 + 1, phase = (frame - 4) % 4;
  if (frame < 4 || slot > 9) return;
  bdg::SceneView* sv = e->sceneView.get();
  const std::string arg = baseDir;
  const bool repo = arg == "repo" || arg.starts_with("repo:");
  const bool bless = repo && std::getenv("BDG_COMPARE_BLESS") != nullptr;
  const std::string family = RendererFamily(e->rhi.gpuName);
  const fs::path scenePath = fs::path(std::u8string((const char8_t*)e->model->scenePath.c_str()));
  fs::path base = repo ? fs::path(arg.size() > 5 ? arg.substr(5) : std::string("Tests/RenderBaselines")) / family / scenePath.stem() : fs::path(arg);
  if (phase == 0) { // move to the bookmark; the picture is read three frames later (shadow map and timers settle)
    if (slot == 1 && repo) bdg::LogInfo("compare: renderer {} ({}) baselines {}{}", family, e->rhi.gpuName, base.string(), bless ? " (bless)" : "");
    if (!sv->JumpToBookmark(*e->model, slot)) bdg::LogInfo("compare: bookmark {} -", slot);
    return;
  }
  if (phase != 3 || sv->BookmarkName(*e->model, slot).empty()) return;
  int32_t w, h;
  std::vector<uint8_t> px;
  if (repo) { // a fixed size, independent of the panel and the display scale
    bdg::Widget fixed;
    fixed.x = fixed.y = 0;
    fixed.w = 480;
    fixed.h = 270;
    sv->Render(*e->model, e->assetsOpen ? &e->assets : nullptr, fixed, 1.f);
  }
  sv->ReadPixels(w, h, px);
  std::vector<uint8_t> top((size_t)w * h * 4); // render target rows are bottom-up
  for (int y = 0; y < h; ++y) std::memcpy(&top[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
  const char* outEnv = std::getenv("BDG_COMPARE_OUT");
  fs::path out = outEnv ? fs::path(outEnv) : fs::current_path() / ".cache" / "compare";
  std::error_code ec;
  fs::create_directories(out, ec);
  if (!repo || bless) fs::create_directories(base, ec);
  double threshold = repo ? 0.02 : 0.03;
  if (repo) { // the set's own threshold
    std::vector<uint8_t> meta;
    if (bdg::ReadFileBytes(base.parent_path() / "meta.json", meta)) {
      const std::string text(meta.begin(), meta.end());
      if (size_t at = text.find("\"threshold\":"); at != std::string::npos) threshold = std::atof(text.c_str() + at + 12);
    }
  }
  if (const char* t = std::getenv("BDG_COMPARE_THRESHOLD")) threshold = std::atof(t);
  bdg::WritePNG((out / std::format("{}.png", slot)).string().c_str(), w, h, top.data(), false);
  fs::path basePng = base / std::format("bookmark_{}.png", slot);
  std::string name = sv->BookmarkName(*e->model, slot), result;
  int32_t bw = 0, bh = 0;
  std::vector<uint8_t> baseline;
  if (repo && !bless && !fs::exists(basePng)) { // regression mode: report it, hand the picture over for review
    bdg::WritePNG((out / std::format("bookmark_{}.png", slot)).string().c_str(), w, h, top.data(), false);
    ++missing;
    bdg::LogInfo("compare: bookmark {} {:<24} no baseline for {} (picture in {})", slot, name, family, out.string());
  } else if (!fs::exists(basePng) || bless) {
    bdg::WritePNG(basePng.string().c_str(), w, h, top.data(), false);
    if (bless && slot == 1 && !fs::exists(base.parent_path() / "meta.json"))
      if (std::FILE* f = _wfopen((base.parent_path() / "meta.json").c_str(), L"wb")) {
        const std::string meta = std::format("{{\"renderer\":\"{}\",\"threshold\":0.02}}\n", family);
        std::fwrite(meta.data(), 1, meta.size(), f);
        std::fclose(f);
      }
    ++created;
    result = "created";
    bdg::LogInfo("compare: bookmark {} {:<24} baseline created ({}x{})", slot, name, w, h);
  } else if (!bdg::LoadImageRGBA(basePng, bw, bh, baseline) || bw != w || bh != h) {
    ++failed;
    bdg::LogInfo("compare: bookmark {} {:<24} FAIL size {}x{} vs baseline {}x{}", slot, name, w, h, bw, bh);
  } else {
    bdg::ImageDiff d;
    bdg::CompareImages(baseline.data(), top.data(), w, h, d);
    bdg::WritePNG((out / std::format("{}_diff.png", slot)).string().c_str(), w, h, d.heat.data(), false);
    bool ok = d.mean <= threshold;
    failed += ok ? 0 : 1;
    ++compared;
    worst = std::max(worst, d.mean);
    bdg::LogInfo("compare: bookmark {} {:<24} flip={:.4f} max={:.3f} over0.1={:.2f}% base={:.2f} {}", slot, name, d.mean, d.max,
                 d.over * 100, threshold, ok ? "ok" : "FAIL");
  }
  if (slot == 9 || sv->BookmarkName(*e->model, slot + 1).empty())
    bdg::LogInfo("compare summary: compared={} created={} failed={} worst={:.4f} out={}{}", compared, created, failed, worst, out.string(),
                 repo ? std::format(" missing={} renderer={}", missing, family) : std::string());
}

// Scripted render reference check (BDG_REFSCENE_TEST=1 with the Tests/RenderReference project, queue 3-8):
// bookmarks 1 / 2 / 8 by key, the Albedo / Normal / Depth / Shadow views (Alt + digit), the wireframe chip,
// Ctrl + 5 storing a bookmark (one undo step), GPU pass timings.
void InjectRefSceneTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->refSceneTestValues;
  bdg::Widget* sw = e->editor->Find("scene");
  if (!sw) return;
  float cx = sw->x + sw->w * 0.5f, cy = sw->y + sw->h * 0.5f;
  auto key = [&](bdg::Key k, bool ctrl = false, bool alt = false) {
    e->window.InjectEvent({T::MouseMove, cx * scale, cy * scale});
    bdg::InputEvent i{T::KeyDown};
    i.key = k;
    i.ctrl = ctrl;
    i.alt = alt;
    e->window.InjectEvent(i);
  };
  struct Shot { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
  auto grab = [&] { Shot s; sv->ReadPixels(s.w, s.h, s.px); return s; };
  auto at = [&](const Shot& s, glm::vec3 p, int c[3]) {
    float x, y;
    c[0] = c[1] = c[2] = -1;
    if (!bdg::ProjectToPixel(sv->camera.ToCamera(), s.w, s.h, p, x, y) || x < 0 || y < 0 || x >= s.w || y >= s.h) return false;
    const uint8_t* q = &s.px[((size_t)y * s.w + (size_t)x) * 4];
    c[0] = q[0]; c[1] = q[1]; c[2] = q[2];
    return true;
  };
  // Zone 1: the matrix sphere r3 m0 (white dielectric) faces the camera at (0, 0.6, -1) + its radius toward +Z.
  const glm::vec3 sphereFront{0, 0.6f, -1 + 0.32f}, nearGold{0, 0.35f, 1}, farWall{0, 5.4f, -1};
  // Zone 2: the floating sphere's sun shadow on the ground, and open ground beside it.
  static const glm::vec3 toL = -glm::normalize(glm::vec3(0.321f, -0.766f, -0.557f)); // Sun rotation (-50, -30, 0)
  const glm::vec3 ball{31.8f, 1.4f, 0.5f};
  const glm::vec3 shade = ball - toL * (ball.y / toL.y), open{shade.x, 0, 1.6f};
  if (frame == 2) {
    v[0] = std::format("{}", m->scene.ObjectCount());
    key(bdg::Key::Num1);
  }
  if (frame == 4) {
    v[1] = std::format("{}", sv->camera.pivot.y == 2.4f && sv->camera.yaw == 0.f && sv->camera.distance == 10.5f ? 1 : 0);
    key(bdg::Key::Num2, false, true); // Alt + 2: albedo
  }
  if (frame == 6) {
    int c[3];
    at(grab(), sphereFront, c); // linear 0.85 white -> sRGB 237
    v[2] = sv->debugView == bdg::DebugView::Albedo && std::abs(c[0] - 237) <= 3 && std::abs(c[2] - 237) <= 3 ? "1" : std::format("0({},{},{})", c[0], c[1], c[2]);
    key(bdg::Key::Num3, false, true); // normal
  }
  if (frame == 8) {
    int c[3];
    at(grab(), sphereFront, c); // facing +Z: (0.5, 0.5, 1) -> (128, 128, 255)
    v[3] = c[2] > 235 && std::abs(c[0] - 128) < 30 && std::abs(c[1] - 128) < 30 ? "1" : std::format("0({},{},{})", c[0], c[1], c[2]);
    key(bdg::Key::Num6, false, true); // depth
  }
  if (frame == 10) {
    Shot s = grab();
    int n[3], f[3];
    at(s, nearGold, n);
    at(s, farWall, f);
    v[4] = n[0] >= 0 && f[0] > n[0] + 5 ? "1" : std::format("0({}<{})", n[0], f[0]); // farther = brighter
    key(bdg::Key::Num2);                      // bookmark 2: lights and shadows
    key(bdg::Key::Num7, false, true);         // shadow view
  }
  if (frame == 12) {
    Shot s = grab();
    int in[3], out[3];
    at(s, shade, in);
    at(s, open, out);
    v[5] = in[0] >= 0 && in[0] < 90 && out[0] > 200 ? "1" : std::format("0({},{})", in[0], out[0]);
    key(bdg::Key::Num1, false, true); // back to lit
  }
  static long long wireBefore = 0;
  auto cyan = [](const Shot& s) {
    long long n = 0;
    for (size_t i = 0; i + 3 < s.px.size(); i += 4) n += s.px[i] < 140 && s.px[i + 1] > 170 && s.px[i + 2] > 190;
    return n;
  };
  if (frame == 14) {
    wireBefore = cyan(grab());
    float x, y;
    if (sv->ChipCenter("wire", x, y)) {
      e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
      e->window.InjectEvent({T::MouseDown, x * scale, y * scale, 1});
      e->window.InjectEvent({T::MouseUp, x * scale, y * scale, 1});
    }
  }
  static size_t undo0;
  if (frame == 16) {
    long long after = cyan(grab());
    v[6] = sv->wireframe && after > wireBefore + 500 ? "1" : std::format("0({}->{})", wireBefore, after);
    sv->camera.yaw = 42.f; // a new view, stored on key 5
    undo0 = m->bus.UndoDepth();
    key(bdg::Key::Num5, true);
  }
  if (frame == 18) {
    sv->camera.yaw = 0.f;
    bool stored = sv->JumpToBookmark(*m, 5) && sv->camera.yaw == 42.f && m->bus.UndoDepth() == undo0 + 1;
    v[7] = std::format("{}", stored ? 1 : 0);
    bdg::InputEvent z{T::KeyDown};
    z.key = bdg::Key::Z;
    z.ctrl = true;
    e->window.InjectEvent(z);
  }
  if (frame == 20) {
    sv->camera.yaw = 7.f;
    sv->JumpToBookmark(*m, 5);
    v[8] = std::format("{}", sv->camera.yaw == 0.f ? 1 : 0); // back to the generated "5 Post" view
    key(bdg::Key::Num8); // the materials zone at the potato tier
  }
  if (frame == 22) {
    v[9] = bdg::TierKey(sv->quality.tier);
    const auto& passes = e->r3d->Stats().passes;
    bool timed = false;
    for (const auto& p : passes) timed = timed || (std::string(p.name) == "scene" && p.ms > 0);
    v[10] = std::format("{}", timed ? 1 : 0);
  }
}

// Scripted quality tier check (BDG_QUALITY_TEST=1, queue 3-5): the same scene at potato / low / medium / high.
// Lighting: the Crystal's top and front faces differ only when lit. Shadows: ground in the Crystal's sun shadow
// is darker only from medium up. FXAA lowers the sum of squared neighbour differences (edges get softer); bloom brightens the ground beside a
// glowing Crystal; the top-bar badge steps to the next tier.
void InjectQualityTest(bdg_engine* e, int frame, float scale) {
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  auto* v = e->qualityTestValues;
  struct Frame { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
  auto grab = [&] { Frame f; sv->ReadPixels(f.w, f.h, f.px); return f; };
  auto lum = [&](const Frame& f, glm::vec3 p) {
    float x, y;
    if (!bdg::ProjectToPixel(sv->camera.ToCamera(), f.w, f.h, p, x, y) || x < 0 || y < 0 || x >= f.w || y >= f.h) return -1;
    const uint8_t* q = &f.px[((size_t)y * f.w + (size_t)x) * 4];
    return q[0] + q[1] + q[2];
  };
  auto variation = [](const Frame& f) {
    long long tv = 0;
    for (int y = 0; y < f.h; ++y)
      for (int x = 1; x < f.w; ++x)
        for (int c = 0; c < 3; ++c) { // squared steps: softening an edge (one jump into two halves) lowers it
          long long d = f.px[((size_t)y * f.w + x) * 4 + c] - f.px[((size_t)y * f.w + x - 1) * 4 + c];
          tv += d * d;
        }
    return tv;
  };
  // Crystal: 1 m placeholder box at (3, 0.5, 2). Its sun shadow falls toward +x / -z.
  const glm::vec3 top{3, 1.0f, 2}, front{3, 0.5f, 2.5f}, shade{3.7f, 0, 1.0f}, open{4.6f, 0, 1.0f}, beside{3.6f, 0.15f, 2.6f};
  auto describe = [&](const Frame& f) {
    int t = lum(f, top), fr = lum(f, front), s = lum(f, shade), o = lum(f, open);
    bool lit = std::abs(t - fr) > 10, shadow = s >= 0 && o > 0 && s * 10 < o * 8;
    return std::string(lit ? "lit" : "flat") + (shadow ? ",shadow" : ",noshadow");
  };
  const bdg::QualityTier tiers[4] = {bdg::QualityTier::Potato, bdg::QualityTier::Low, bdg::QualityTier::Medium, bdg::QualityTier::High};
  static long long tvFxaa = 0;
  static int besideMedium = 0;
  if (frame >= 2 && frame <= 14 && (frame - 2) % 4 == 0) sv->quality = bdg::QualityPreset(tiers[(frame - 2) / 4]);
  if (frame >= 4 && frame <= 16 && (frame - 4) % 4 == 0) {
    int i = (frame - 4) / 4;
    Frame f = grab();
    v[i] = describe(f) + (i == 3 ? std::format(",msaa{}", e->r3d->LastQuality().msaa) : "");
    if (i == 2) tvFxaa = variation(f);
  }
  if (frame == 17) { // medium once more, without FXAA
    sv->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
    sv->quality.fxaa = false;
  }
  if (frame == 19) {
    v[4] = std::format("{}", tvFxaa > 0 && tvFxaa < variation(grab()) ? 1 : 0);
    bdg::Command c; // a glowing Crystal: bloom shows around it from high up
    c.name = "set";
    c.target = "g8";
    c.args = R"({"path":"MeshRenderer.material","value":"M_Glow"})";
    std::string err;
    m->bus.Execute(c, &err);
    sv->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
  }
  if (frame == 21) {
    besideMedium = lum(grab(), beside);
    sv->quality = bdg::QualityPreset(bdg::QualityTier::High);
  }
  if (frame == 23) {
    int b = lum(grab(), beside);
    v[5] = b > besideMedium + 6 ? "1" : std::format("0({}->{})", besideMedium, b);
    if (bdg::Widget* badge = e->editor->Find("tool.quality")) { // click: the tier list opens
      using T = bdg::InputEvent::Type;
      float x = (badge->x + badge->w * 0.5f) * scale, y = (badge->y + badge->h * 0.5f) * scale;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    }
  }
  if (frame == 24) { // pick 최고 (the last item) from the list
    using T = bdg::InputEvent::Type;
    const auto& items = e->editor->Menu();
    float x, y, w, h;
    if (!items.empty() && e->editor->MenuItemRect(items.size() - 1, x, y, w, h)) {
      x = (x + w * 0.5f) * scale;
      y = (y + h * 0.5f) * scale;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    } else bdg::LogError("quality test: the tier list did not open");
  }
  if (frame == 25) {
    bdg::Widget* badge = e->editor->Find("tool.quality");
    v[6] = std::format("{}", badge && badge->text == "최고" && sv->quality.tier == bdg::QualityTier::Ultra ? 1 : 0);
  }
}

// Scripted check of 3-5c (BDG_AUTOQ_TEST=1, BDG_ASSETS = an empty folder): the device benchmark picks a tier and is
// cached per GPU; the frame drop guard steps down one tier at a time with a pause in between and stops at the
// project's minimum; texture groups load smaller cooked mips at potato and the full textures at ultra.
namespace autoq {
std::string bench = "bench=-", governor = "governor=-", groups = "groups=-";
uint64_t potatoBytes = 0;
bdg::MaterialLibrary::GroupUse potatoWorld, potatoNormal;
}
void InjectAutoQualityTest(bdg_engine* e, int frame, float) {
  namespace fs = std::filesystem;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  const char* cacheEnv = std::getenv("BDG_AUTOQ_CACHE");
  const fs::path cache = cacheEnv ? fs::path(cacheEnv) : fs::path("ci-autoq") / "QualityBenchmark.json";
  if (frame == 1) {
    std::error_code ec;
    fs::remove(cache, ec);
    bdg::ProjectQuality range;
    range.min = bdg::QualityTier::Low;
    const std::string path = (const char*)cache.u8string().c_str();
    const bdg::QualityTier first = sv->DetectQuality(range, path);
    const bool measured = !sv->autoQuality.cached && sv->autoQuality.highMs > 0 && fs::exists(cache);
    const double ms = sv->autoQuality.highMs;
    const bdg::QualityTier second = sv->DetectQuality(range, path);
    const bool cached = sv->autoQuality.cached && second == first && std::abs(sv->autoQuality.highMs - ms) < 0.01 &&
                        first == range.Clamp(bdg::TierFromBenchmark(ms));
    autoq::bench = std::format("bench={} cached={} tier={} ms={:.2f}", measured ? 1 : 0, cached ? 1 : 0, bdg::TierKey(first), ms);
    // frame drop guard: slow frames at high -> medium; still slow during the pause -> stays; after it -> low; never below low
    const bdg::QualitySettings keep = sv->quality;
    sv->quality = bdg::QualityPreset(bdg::QualityTier::High);
    int steps[4] = {};
    double t = 1000;
    auto run = [&](int k, int n, double ms) {
      for (int i = 0; i < n; ++i, t += 0.02) steps[k] += sv->FeedFrameTime(t, ms) ? 1 : 0;
    };
    run(0, 20, 5);   // fast frames: nothing
    run(0, 60, 30);  // slow: one step (high -> medium)
    const bdg::QualityTier afterFirst = sv->quality.tier;
    run(1, 60, 30);  // inside the 4 s pause: no step
    t += 5;
    run(2, 60, 30);  // after the pause: medium -> low
    t += 5;
    run(3, 200, 30); // low is the project's minimum
    const bool ok = steps[0] == 1 && afterFirst == bdg::QualityTier::Medium && steps[1] == 0 && steps[2] == 1 && steps[3] == 0 &&
                    sv->quality.tier == bdg::QualityTier::Low && sv->autoQuality.stepsDown == 2;
    autoq::governor = ok ? "governor=1"
                         : std::format("governor=0({},{},{},{} {})", steps[0], steps[1], steps[2], steps[3], bdg::TierKey(sv->quality.tier));
    sv->autoQuality.on = false;
    sv->quality = keep;
    fs::remove(cache, ec);
    if (cache.has_parent_path() && fs::is_empty(cache.parent_path(), ec)) fs::remove(cache.parent_path(), ec);
  }
  if (!e->assetsOpen) return;
  const fs::path root = fs::path(std::u8string((const char8_t*)e->assets.Root().c_str()));
  if (frame == 2) { // a 1024 colour texture and a 512 normal map on one material, on the sample scene's g5
    auto write = [&](const char* rel, const std::vector<uint8_t>& bytes) {
      fs::create_directories((root / rel).parent_path());
      if (std::FILE* f = _wfopen((root / rel).c_str(), L"wb")) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
      }
    };
    auto image = [](int n, bool normal) {
      std::vector<uint8_t> px((size_t)n * n * 4, 255);
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
          uint8_t* p = &px[((size_t)y * n + x) * 4];
          if (normal) {
            p[0] = (uint8_t)(128 + 60 * std::sin(x * 0.1f));
            p[1] = (uint8_t)(128 + 60 * std::cos(y * 0.1f));
            p[2] = 220;
          } else {
            p[0] = (uint8_t)(x * 255 / n);
            p[1] = (uint8_t)(y * 255 / n);
            p[2] = (uint8_t)(((x / 32 + y / 32) & 1) * 200);
          }
        }
      std::vector<uint8_t> png;
      bdg::EncodePNG(n, n, px.data(), false, png);
      return png;
    };
    write("Textures/T_Group_D.png", image(1024, false));
    write("Textures/T_Group_N.png", image(512, true));
    const std::string mat = "{\"material\":\"M_Group\"}\n"
                            "{\"param\":\"BaseColorMap\",\"type\":\"texture\",\"value\":\"Textures/T_Group_D.png\"}\n"
                            "{\"param\":\"NormalMap\",\"type\":\"texture\",\"value\":\"Textures/T_Group_N.png\"}\n";
    write("Materials/M_Group.material", std::vector<uint8_t>(mat.begin(), mat.end()));
    e->assets.Refresh();
    e->materials.Invalidate();
    bdg::Command c;
    c.name = "set";
    c.target = "g5";
    c.args = R"({"path":"MeshRenderer.material","value":"M_Group"})";
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("autoq test: {}", err);
    sv->quality = bdg::QualityPreset(bdg::QualityTier::Potato);
  }
  if (frame == 4) {
    autoq::potatoWorld = e->materials.TextureGroupUse(bdg::TextureGroup::World);
    autoq::potatoNormal = e->materials.TextureGroupUse(bdg::TextureGroup::Normal);
    autoq::potatoBytes = autoq::potatoWorld.bytes + autoq::potatoNormal.bytes;
    sv->quality = bdg::QualityPreset(bdg::QualityTier::Ultra);
  }
  if (frame == 6) {
    const auto world = e->materials.TextureGroupUse(bdg::TextureGroup::World), normal = e->materials.TextureGroupUse(bdg::TextureGroup::Normal);
    const auto& pw = autoq::potatoWorld;
    const auto& pn = autoq::potatoNormal;
    const uint64_t ultraBytes = world.bytes + normal.bytes;
    const bool ok = pw.cookedLargest == 1024 && pw.largest == 512 && pn.cookedLargest == 512 && pn.largest == 256 && world.largest == 1024 &&
                    normal.largest == 512 && ultraBytes > autoq::potatoBytes * 3;
    autoq::groups = std::format("groups={} (potato world {}/{} normal {}/{} {} KB | ultra world {} normal {} {} KB)", ok ? 1 : 0, pw.largest,
                                pw.cookedLargest, pn.largest, pn.cookedLargest, autoq::potatoBytes / 1024, world.largest, normal.largest,
                                ultraBytes / 1024);
  }
}
void ReportAutoQualityTest(bdg_engine*) { bdg::LogInfo("autoq test: {} {} {}", autoq::bench, autoq::governor, autoq::groups); }
BDG_EDITOR_TEST("BDG_AUTOQ_TEST", InjectAutoQualityTest, ReportAutoQualityTest)

// Scripted check of 3-8b, the indoor composite scene (BDG_INDOOR_TEST=1 with BDG_ASSETS = Tests/IndoorScene/Assets and
// BDG_SCENE_FILE = its Scenes/Cloister.scene, made by tools/make_indoor_scene.py): every material and texture loads
// (world, normal and masks groups), the lights and the tiers' shadow limits, a point light's shadow on the side room's
// back wall and the window piers' sun shadow on the floor (each pixel against the same view without that shadow).
namespace indoor {
std::string scene = "objects=-", textures = "materials=-", lights = "lights=-", room = "room=-", sun = "sun=-", perf;
bool lightsOk = false, roomOk = false, sunOk = false, texturesOk = false;
struct Shot { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
Shot room1, sun1;
}
void InjectIndoorTest(bdg_engine* e, int frame, float) {
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m) return;
  using indoor::Shot;
  auto grab = [&] { Shot s; sv->ReadPixels(s.w, s.h, s.px); return s; };
  auto lum = [&](const Shot& s, glm::vec3 p) { // 3x3 average around the projected point
    float x, y;
    if (!bdg::ProjectToPixel(sv->camera.ToCamera(), s.w, s.h, p, x, y)) return -1.0;
    double sum = 0;
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int px = (int)x + dx, py = (int)y + dy;
        if (px < 0 || py < 0 || px >= s.w || py >= s.h) continue;
        const uint8_t* q = &s.px[((size_t)py * s.w + px) * 4];
        sum += q[0] * 0.2126 + q[1] * 0.7152 + q[2] * 0.0722;
        ++n;
      }
    return n ? sum / n : -1.0;
  };
  auto tier = [&](bdg::QualityTier t, const char* overrides = nullptr) {
    sv->quality = bdg::QualityPreset(t);
    if (overrides) bdg::ApplyQualityOverrides(overrides, sv->quality, nullptr);
  };
  // side room: the gold sphere / pedestal shadow on the back wall (lamp -> sphere centre -> wall) and the open wall
  // between the pedestals; window light: floor behind a pier (the sun comes through x = 5 toward -x) and floor in a window's patch
  const glm::vec3 roomShadow{-2.45f, 0.625f, -21.38f}, roomLit{0, 0.625f, -21.38f}, pierShadow{0.3f, 0.005f, -3.f}, windowPatch{0.3f, 0.005f, -1.f};
  if (frame == 2) {
    indoor::scene = std::format("objects={}", m->scene.ObjectCount());
    int ok = 0;
    const char* mats[] = {"M_Brick", "M_Tiles", "M_Plaster", "M_Stone", "M_Wood", "M_Painting", "M_Railing", "M_Bulb", "M_Gold", "M_Copper"};
    for (const char* name : mats) {
      bdg::Material3D mat;
      ok += e->materials.Find(name, mat) ? 1 : 0;
    }
    sv->JumpToBookmark(*m, 4);
    tier(bdg::QualityTier::High);
    indoor::textures = std::format("materials={}/{}", ok, std::size(mats));
    indoor::texturesOk = ok == (int)std::size(mats);
  }
  if (frame == 4) {
    const auto world = e->materials.TextureGroupUse(bdg::TextureGroup::World), normal = e->materials.TextureGroupUse(bdg::TextureGroup::Normal),
               masks = e->materials.TextureGroupUse(bdg::TextureGroup::Masks);
    indoor::textures += std::format(" textures={},{},{} ({} KB)", world.textures, normal.textures, masks.textures,
                                    (world.bytes + normal.bytes + masks.bytes) / 1024);
    indoor::texturesOk = indoor::texturesOk && world.textures == 6 && normal.textures == 4 && masks.textures == 2;
    indoor::room1 = grab();
    tier(bdg::QualityTier::High, "localshadows=0");
  }
  if (frame == 6) {
    const Shot off = grab();
    double rs = -1; // the darkest spot of the shadow (a 5 x 5 grid, 0.5 m across, on the wall)
    for (int j = -2; j <= 2; ++j)
      for (int i = -2; i <= 2; ++i) {
        const glm::vec3 p = roomShadow + glm::vec3(i * 0.125f, j * 0.125f, 0);
        const double s1 = lum(indoor::room1, p), s0 = lum(off, p);
        if (s1 >= 0 && s0 > 0 && (rs < 0 || s1 / s0 < rs)) rs = s1 / s0;
      }
    const double l1 = lum(indoor::room1, roomLit), l0 = lum(off, roomLit), rl = l0 > 0 ? l1 / l0 : -1;
    indoor::roomOk = rs >= 0 && rs < 0.62 && rl > 0.9; // ambient and the hall lanterns still reach the wall
    indoor::room = std::format("room={:.2f}/{:.2f}", rs, rl);
    sv->JumpToBookmark(*m, 2);
    tier(bdg::QualityTier::High);
  }
  if (frame == 8) {
    indoor::sun1 = grab();
    tier(bdg::QualityTier::High, "shadows=0");
  }
  if (frame == 10) {
    const Shot off = grab();
    const double s1 = lum(indoor::sun1, pierShadow), s0 = lum(off, pierShadow), l1 = lum(indoor::sun1, windowPatch), l0 = lum(off, windowPatch);
    const double rs = s0 > 0 ? s1 / s0 : -1, rl = l0 > 0 ? l1 / l0 : -1;
    indoor::sunOk = rs >= 0 && rs < 0.6 && rl > 0.9;
    indoor::sun = std::format("sun={:.2f}/{:.2f}", rs, rl);
    sv->JumpToBookmark(*m, 1);
    tier(bdg::QualityTier::High);
  }
  static int highShadows = -1, highLights = -1, highSkipped = -1;
  if (frame == 12) {
    const bdg::RenderStats& st = sv->LastStats();
    highShadows = st.localShadows;
    highLights = st.localLights;
    highSkipped = st.localShadowSkipped;
    double gpu = 0;
    for (const auto& p : st.passes) gpu += p.ms;
    indoor::perf = std::format("| high {}x{} draws={} tris={} gpu={:.2f}ms tiles={}", st.w, st.h, st.sceneDraws, st.triangles, gpu, st.localShadowTiles);
    tier(bdg::QualityTier::Medium);
  }
  if (frame == 14) {
    const bdg::RenderStats& st = sv->LastStats();
    indoor::lightsOk = highLights == 9 && highShadows == 4 && highSkipped >= 1 && st.localShadows == 1;
    indoor::lights = std::format("lights={} shadows_high={} skipped={} shadows_medium={}", highLights, highShadows, highSkipped, st.localShadows);
  }
}
void ReportIndoorTest(bdg_engine*) {
  bdg::LogInfo("indoor test: {} {} {} {} {} | textures={} lights={} room={} sun={} {}", indoor::scene, indoor::textures, indoor::lights,
               indoor::room, indoor::sun, indoor::texturesOk ? "ok" : "bad", indoor::lightsOk ? "ok" : "bad", indoor::roomOk ? "ok" : "bad",
               indoor::sunOk ? "ok" : "bad", indoor::perf);
}
BDG_EDITOR_TEST("BDG_INDOOR_TEST", InjectIndoorTest, ReportIndoorTest)
