// Scripted editor checks: graphics, round 6 (lane A): TAA, SSR, volumetric fog.
// Included by Engine.cpp after the other EditorTests files. New checks: register with BDG_EDITOR_TEST.

namespace gfxtest {
// Bottom-up RGBA8 rows of the Scene view's last picture.
struct Shot { int32_t w = 0, h = 0; std::vector<uint8_t> px; };
Shot Grab(bdg_engine* e) {
  Shot s;
  e->sceneView->ReadPixels(s.w, s.h, s.px);
  return s;
}
// Mean absolute difference (0..255 per channel) between two pictures over the pixels near edges of the reference
// (where its luminance changes by more than 24 between neighbours): what aliasing does.
double EdgeError(const Shot& a, const Shot& ref) {
  if (a.w != ref.w || a.h != ref.h || a.px.size() != ref.px.size() || ref.w < 3 || ref.h < 3) return -1;
  auto lum = [&](const Shot& s, int x, int y) {
    const uint8_t* p = &s.px[((size_t)y * s.w + x) * 4];
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
  };
  double sum = 0;
  size_t n = 0;
  for (int y = 1; y < ref.h - 1; ++y)
    for (int x = 1; x < ref.w - 1; ++x) {
      const double c = lum(ref, x, y);
      const double g = std::max({std::fabs(c - lum(ref, x + 1, y)), std::fabs(c - lum(ref, x - 1, y)), std::fabs(c - lum(ref, x, y + 1)),
                                 std::fabs(c - lum(ref, x, y - 1))});
      if (g < 24) continue;
      for (int k = 0; k < 3; ++k) sum += std::fabs((double)a.px[((size_t)y * a.w + x) * 4 + k] - ref.px[((size_t)y * ref.w + x) * 4 + k]);
      n += 3;
    }
  return n ? sum / (double)n : -1;
}
// The same view rendered at `factor` x the size and box-filtered down: a supersampled reference picture.
Shot Supersampled(bdg_engine* e, float scale, int factor) {
  Shot s;
  bdg::Widget* view = e->editor->Find("scene");
  if (!view) return s;
  const Shot small = Grab(e);
  e->sceneView->Render(*e->model, e->assetsOpen ? &e->assets : nullptr, *view, scale * (float)factor);
  const Shot big = Grab(e);
  if (small.w <= 0 || big.w <= 0) return s;
  s.w = small.w;
  s.h = small.h;
  s.px.assign((size_t)s.w * s.h * 4, 0);
  for (int y = 0; y < s.h; ++y)
    for (int x = 0; x < s.w; ++x) {
      const int x0 = x * big.w / s.w, x1 = std::max((x + 1) * big.w / s.w, x0 + 1), y0 = y * big.h / s.h, y1 = std::max((y + 1) * big.h / s.h, y0 + 1);
      for (int k = 0; k < 4; ++k) {
        int sum = 0, n = 0;
        for (int yy = y0; yy < y1; ++yy)
          for (int xx = x0; xx < x1; ++xx, ++n) sum += big.px[((size_t)yy * big.w + xx) * 4 + k];
        s.px[((size_t)y * s.w + x) * 4 + k] = (uint8_t)((sum + n / 2) / std::max(n, 1));
      }
    }
  return s;
}
// Sum of absolute luminance Laplacians: how much fine detail / edge contrast a picture has.
double Detail(const Shot& s) {
  double sum = 0;
  auto lum = [&](int x, int y) {
    const uint8_t* p = &s.px[((size_t)y * s.w + x) * 4];
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
  };
  for (int y = 1; y < s.h - 1; ++y)
    for (int x = 1; x < s.w - 1; ++x) sum += std::fabs(4 * lum(x, y) - lum(x + 1, y) - lum(x - 1, y) - lum(x, y + 1) - lum(x, y - 1));
  return sum;
}
void Run(bdg::EditorModel* m, const char* name, const std::string& target, const std::string& args) {
  bdg::Command c;
  c.name = name;
  c.target = target;
  c.args = args;
  std::string err;
  if (!m->bus.Execute(c, &err)) bdg::LogError("graphics test: {} {}: {}", name, target, err);
}
// A fresh scene: sun, sky, a dark ground, thin rotated bars against the sky (hard edges at every angle).
void BarScene(bdg_engine* e) {
  bdg::EditorModel* m = e->model.get();
  m->NewScene(bdg::SceneTemplate::Empty);
  for (const std::string& id : std::vector<std::string>(m->scene.Roots())) Run(m, "del", id, "{}");
  Run(m, "add", "", R"({"id":"gsun","name":"Sun"})");
  Run(m, "set", "gsun", R"({"path":"Transform.rotation","value":[-50,30,0]})");
  Run(m, "addcomp", "gsun", R"({"type":"Light","value":{"type":"Directional","intensity":3}})");
  for (int i = 0; i < 6; ++i) {
    const std::string id = std::format("bar{}", i);
    Run(m, "add", "", std::format(R"({{"id":"{}","name":"Bar"}})", id));
    Run(m, "set", id, std::format(R"({{"path":"Transform.position","value":[{},{},0]}})", -2.5f + i * 1.0f, 1.2f + 0.15f * (i % 3)));
    Run(m, "set", id, std::format(R"({{"path":"Transform.rotation","value":[0,{},{}]}})", 10 * i, 8 + i * 13));
    Run(m, "set", id, R"({"path":"Transform.scale","value":[2.4,0.06,0.06]})");
    Run(m, "addcomp", id, R"({"type":"MeshRenderer","value":{"mesh":"Cube","material":"M_Ground"}})");
  }
  bdg::SceneView* sv = e->sceneView.get();
  sv->showGrid = false;
  sv->iconSize = 0;
  sv->camera.pivot = {0, 1.3f, 0};
  sv->camera.yaw = 0;
  sv->camera.pitch = 0;
  sv->camera.distance = 6;
  m->SetSelection({});
}
}

// BDG_TAA_TEST=1: thin bars against the sky drawn without anti-aliasing, 4x supersampled (the reference: the same
// view at 4 x 4 the pixels, box filtered), with FXAA and with TAA (after 24 still frames); the error near edges against
// the reference must fall with TAA to well under the no-AA error. Also: TAA reports its history and jitter, a camera cut starts over, a small camera move keeps it.
namespace taatest {
double none = -1, fxaa = -1, taa = -1;
std::string history = "-", sharpen = "-", cut = "-", move = "-";
gfxtest::Shot ref, noAA, soft;
}
void InjectTaaTest(bdg_engine* e, int frame, float scale) {
  using namespace taatest;
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  auto base = [] {
    bdg::QualitySettings q = bdg::QualityPreset(bdg::QualityTier::Medium);
    q.fxaa = false;
    q.taa = false;
    q.msaa = 1;
    q.bloom = false;
    return q;
  };
  if (frame == 2) {
    gfxtest::BarScene(e);
    sv->quality = base();
  }
  if (frame == 6) {
    noAA = gfxtest::Grab(e);
    ref = gfxtest::Supersampled(e, scale, 4); // 16 samples per pixel, box filtered
    none = gfxtest::EdgeError(noAA, ref);
    sv->quality = base();
    sv->quality.fxaa = true;
  }
  if (frame == 9) {
    fxaa = gfxtest::EdgeError(gfxtest::Grab(e), ref);
    sv->quality = base();
    sv->quality.taa = true;
    sv->quality.taaSharpen = 0; // the resolve itself against the box-filtered reference
  }
  if (frame == 36) {
    const gfxtest::Shot t = gfxtest::Grab(e);
    soft = t;
    if (const char* dump = std::getenv("BDG_TAA_DUMP")) { // pictures for a look (no AA, reference, TAA)
      bdg::WritePNG((std::string(dump) + "_none.png").c_str(), noAA.w, noAA.h, noAA.px.data(), true);
      bdg::WritePNG((std::string(dump) + "_ref.png").c_str(), ref.w, ref.h, ref.px.data(), true);
      bdg::WritePNG((std::string(dump) + "_taa.png").c_str(), t.w, t.h, t.px.data(), true);
    }
    taa = gfxtest::EdgeError(t, ref);
    const bdg::RenderStats& st = sv->LastStats();
    history = st.taa && st.taaHistory && (st.jitterX != 0 || st.jitterY != 0) ? "1" : "0";
    sv->quality.taaSharpen = 0.5f;
  }
  if (frame == 40) { // the sharpen pass raises local contrast
    sharpen = gfxtest::Detail(gfxtest::Grab(e)) > gfxtest::Detail(soft) * 1.05 ? "1" : "0";
    sv->camera.yaw += 90; // a cut: no history on the next frame
  }
  if (frame == 41 || frame == 42) { // the frame after the cut has no history
    if (sv->LastStats().taa && !sv->LastStats().taaHistory) cut = "1";
    else if (frame == 42 && cut != "1") cut = "0";
    if (frame == 42) sv->camera.yaw -= 90;
  }
  if (frame == 46) sv->camera.yaw += 0.5f; // a small move keeps the history
  if (frame == 48) move = sv->LastStats().taaHistory ? "1" : "0";
}
void ReportTaaTest(bdg_engine*) {
  using namespace taatest;
  const bool better = none > 0 && taa >= 0 && taa < none * 0.6;
  bdg::LogInfo("taa test: edges none={:.2f} fxaa={:.2f} taa={:.2f} better={} history={} sharpen={} cut={} move={}", none, fxaa, taa, better ? 1 : 0,
               history, sharpen, cut, move);
}
BDG_EDITOR_TEST("BDG_TAA_TEST", InjectTaaTest, ReportTaaTest)

// BDG_SSR_TEST=1 with BDG_ASSETS = Tests/RenderReference/Assets and BDG_SCENE_FILE = its RenderReference.scene: at
// bookmark 1 (the material matrix) a mirror floor (MI_Matrix_r0_m6: metal, roughness 0) and a red box on it are
// added; high tier with SSR off, then on. Checks: where the box's mirror image falls the floor turns red with SSR (the
// probe there shows the sky), a mirror point whose reflection leaves the picture keeps the probe / sky reflection
// (fallback), a rough floor (MI_Matrix_r6_m6) gets no SSR (roughness fade), the stats say SSR ran.
namespace ssrtest {
std::string mirror = "-", fallback = "-", rough = "-", ran = "-";
gfxtest::Shot off;
}
void InjectSsrTest(bdg_engine* e, int frame, float) {
  using namespace ssrtest;
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  auto pixel = [&](const gfxtest::Shot& s, const glm::vec3& p, int c) { // channel c at the pixel showing world point p
    float px = 0, py = 0;
    if (!bdg::ProjectToPixel(sv->camera.ToCamera(), s.w, s.h, p, px, py)) return -1;
    const int x = std::clamp((int)px, 0, s.w - 1), y = std::clamp((int)py, 0, s.h - 1);
    return (int)s.px[((size_t)y * s.w + x) * 4 + c];
  };
  const glm::vec3 image{0, 0.005f - 0.45f, 2.65f}; // mirror image of the box's front face centre (box 0.9 m at z 2.2)
  const glm::vec3 open{3.2f, 0.005f, 3.4f};        // a floor point beside the matrix: it mirrors the sky
  if (frame == 2) {
    sv->JumpToBookmark(*m, 1);
    gfxtest::Run(m, "add", "", R"({"id":"ssrfloor","name":"Mirror"})");
    gfxtest::Run(m, "set", "ssrfloor", R"({"path":"Transform.position","value":[0,0.005,2.4]})");
    gfxtest::Run(m, "set", "ssrfloor", R"({"path":"Transform.scale","value":[7,1,2.8]})");
    gfxtest::Run(m, "addcomp", "ssrfloor", R"({"type":"MeshRenderer","value":{"mesh":"Plane","material":"MI_Matrix_r0_m6"}})");
    gfxtest::Run(m, "add", "", R"({"id":"ssrbox","name":"Red box"})");
    gfxtest::Run(m, "set", "ssrbox", R"({"path":"Transform.position","value":[0,0.455,2.2]})");
    gfxtest::Run(m, "set", "ssrbox", R"({"path":"Transform.scale","value":[0.9,0.9,0.9]})");
    gfxtest::Run(m, "addcomp", "ssrbox", R"({"type":"MeshRenderer","value":{"mesh":"Cube","material":"M_AxisX"}})");
    m->SetSelection({});
    sv->showGrid = false;
    sv->iconSize = 0;
    sv->quality = bdg::QualityPreset(bdg::QualityTier::High);
    sv->quality.ssr = false;
  }
  if (frame == 8) {
    off = gfxtest::Grab(e);
    sv->quality.ssr = true;
  }
  if (frame == 12) {
    const gfxtest::Shot on = gfxtest::Grab(e);
    if (const char* dump = std::getenv("BDG_SSR_DUMP")) {
      bdg::WritePNG((std::string(dump) + "_off.png").c_str(), off.w, off.h, off.px.data(), true);
      bdg::WritePNG((std::string(dump) + "_on.png").c_str(), on.w, on.h, on.px.data(), true);
    }
    ran = sv->LastStats().ssr ? "1" : "0";
    const int rOn = pixel(on, image, 0), gOn = pixel(on, image, 1), rOff = pixel(off, image, 0), gOff = pixel(off, image, 1);
    mirror = rOn > gOn * 1.6 && rOn > 60 && !(rOff > gOff * 1.6) ? "1" : std::format("0(on {},{} off {},{})", rOn, gOn, rOff, gOff);
    int diff = 0;
    for (int c = 0; c < 3; ++c) diff = std::max(diff, std::abs(pixel(on, open, c) - pixel(off, open, c)));
    fallback = pixel(on, open, 0) >= 0 && diff <= 6 ? "1" : std::format("0({})", diff);
    gfxtest::Run(m, "set", "ssrfloor", R"({"path":"MeshRenderer.material","value":"MI_Matrix_r6_m6"})");
    sv->quality.ssr = false;
  }
  if (frame == 16) {
    off = gfxtest::Grab(e);
    sv->quality.ssr = true;
  }
  if (frame == 20) {
    const gfxtest::Shot on = gfxtest::Grab(e);
    int diff = 0;
    for (int c = 0; c < 3; ++c) diff = std::max(diff, std::abs(pixel(on, image, c) - pixel(off, image, c)));
    rough = diff <= 4 ? "1" : std::format("0({})", diff);
  }
}
void ReportSsrTest(bdg_engine*) {
  using namespace ssrtest;
  bdg::LogInfo("ssr test: ran={} mirror={} fallback={} rough={}", ran, mirror, fallback, rough);
}
BDG_EDITOR_TEST("BDG_SSR_TEST", InjectSsrTest, ReportSsrTest)
