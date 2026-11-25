// Scripted editor checks: graphics, round 6 (lane A): TAA, SSR, volumetric fog, realtime GI (Tier 1), DDS textures.
// Included by Engine.cpp after the other EditorTests files. New checks: register with BDG_EDITOR_TEST.
#include "Core/DDS.h"
#include "Render3D/DynamicGI.h"

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

// BDG_FOG_TEST=1: a dark back wall 25 m away, a roof over the left half (the sun straight above: the left air is in
// shadow), a red point light in the shaded half, a post volume with volumetric fog; high tier, fog off then on.
// What the fog adds (on - off) is compared: the lit air glows more than the shaded air (the sun's shadow cuts the
// fog), the air around the red light glows red, and with a height falloff the fog in front of the wall's top is
// thinner than at its foot. The stats say the froxel fog ran; medium tier does not run it.
namespace fogtest {
std::string ran = "-", shafts = "-", glow = "-", height = "-", tiers = "-";
gfxtest::Shot off, uniform;
}
void InjectFogTest(bdg_engine* e, int frame, float) {
  using namespace fogtest;
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv) return;
  using gfxtest::Run;
  auto at = [&](const gfxtest::Shot& s, const glm::vec3& p, int c) { // channel c (3 = luminance) at a world point
    float px = 0, py = 0;
    if (!bdg::ProjectToPixel(sv->camera.ToCamera(), s.w, s.h, p, px, py)) return -1.0;
    const int x = std::clamp((int)px, 0, s.w - 1), y = std::clamp((int)py, 0, s.h - 1);
    const uint8_t* q = &s.px[((size_t)y * s.w + x) * 4];
    return c < 3 ? (double)q[c] : 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
  };
  const glm::vec3 litP{7, 3, -25}, shadeP{-14, 3, -25}, glowP{-6, 3, -12}, footP{7, 0.6f, -25}, topP{7, 10, -25};
  if (frame == 2) {
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) Run(m, "del", id, "{}");
    Run(m, "add", "", R"({"id":"fsun","name":"Sun"})");
    Run(m, "set", "fsun", R"({"path":"Transform.rotation","value":[-85,0,0]})");
    Run(m, "addcomp", "fsun", R"({"type":"Light","value":{"type":"Directional","intensity":4}})");
    auto box = [&](const char* id, const char* pos, const char* scale, const char* mat) {
      Run(m, "add", "", std::format(R"({{"id":"{}","name":"{}"}})", id, id));
      Run(m, "set", id, std::format(R"({{"path":"Transform.position","value":[{}]}})", pos));
      Run(m, "set", id, std::format(R"({{"path":"Transform.scale","value":[{}]}})", scale));
      Run(m, "addcomp", id, std::format(R"({{"type":"MeshRenderer","value":{{"mesh":"Cube","material":"{}"}}}})", mat));
    };
    box("fground", "0,-0.5,-10", "60,1,60", "M_Ground");
    box("fwall", "0,6,-26", "60,12,1", "M_Ground");
    box("froof", "-15,8.5,-10", "30,0.5,40", "M_Ground");
    Run(m, "add", "", R"({"id":"flamp","name":"Lamp"})");
    Run(m, "set", "flamp", R"({"path":"Transform.position","value":[-6,3,-12]})");
    Run(m, "addcomp", "flamp", R"({"type":"Light","value":{"type":"Point","color":[1,0.1,0.05,1],"intensity":40,"range":6,"castShadows":false}})");
    Run(m, "add", "", R"({"id":"fvol","name":"Fog"})");
    Run(m, "addcomp", "fvol", R"({"type":"PostProcessVolume","value":{"fogDensity":0,"fogHeightFalloff":0,"fogDistance":80,"fogAnisotropy":0.3}})");
    m->SetSelection({});
    sv->showGrid = false;
    sv->iconSize = 0;
    sv->camera.pivot = {0, 3, -10};
    sv->camera.yaw = 0;
    sv->camera.pitch = 0;
    sv->camera.distance = 14;
    sv->quality = bdg::QualityPreset(bdg::QualityTier::High);
    sv->quality.ssr = false;
  }
  if (frame == 6) {
    off = gfxtest::Grab(e);
    Run(m, "set", "fvol", R"({"path":"PostProcessVolume.fogDensity","value":0.04})");
  }
  if (frame == 10) {
    uniform = gfxtest::Grab(e);
    ran = sv->LastStats().volumetricFog ? "1" : "0";
    if (const char* dump = std::getenv("BDG_FOG_DUMP")) {
      bdg::WritePNG((std::string(dump) + "_off.png").c_str(), off.w, off.h, off.px.data(), true);
      bdg::WritePNG((std::string(dump) + "_on.png").c_str(), uniform.w, uniform.h, uniform.px.data(), true);
    }
    const double lit = at(uniform, litP, 3) - at(off, litP, 3), shade = at(uniform, shadeP, 3) - at(off, shadeP, 3);
    shafts = lit > 8 && lit > shade * 1.5 ? "1" : std::format("0(lit {:.1f} shade {:.1f})", lit, shade);
    const double r = at(uniform, glowP, 0) - at(off, glowP, 0), g = at(uniform, glowP, 1) - at(off, glowP, 1);
    glow = r > 10 && r > g * 2 && r > shade ? "1" : std::format("0(r {:.1f} g {:.1f})", r, g);
    Run(m, "set", "fvol", R"({"path":"PostProcessVolume.fogHeightFalloff","value":0.5})");
  }
  if (frame == 14) {
    const gfxtest::Shot hf = gfxtest::Grab(e);
    const double foot = at(hf, footP, 3) - at(off, footP, 3), top = at(hf, topP, 3) - at(off, topP, 3);
    height = foot > 4 && top < foot * 0.5 ? "1" : std::format("0(foot {:.1f} top {:.1f})", foot, top);
    sv->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
  }
  if (frame == 17) tiers = !sv->LastStats().volumetricFog ? "1" : "0";
}
void ReportFogTest(bdg_engine*) {
  using namespace fogtest;
  bdg::LogInfo("fog test: ran={} shafts={} glow={} height={} tiers={}", ran, shafts, glow, height, tiers);
}
BDG_EDITOR_TEST("BDG_FOG_TEST", InjectFogTest, ReportFogTest)

// BDG_GI1_TEST=1: realtime GI (Halo Tier 1, round 6). A closed room of 10 cm walls on a sunlit ground, a light probe
// volume (1 m cells) across inside and outside, the camera inside; high tier traces the volume while running. With
// visibility (depth maps and masks) the room stays dark; with it off the sunlit probes outside light the inside
// (leak). A lamp switched on in the room: the traced probes take most of its bounce light within a few sweeps.
// Medium tier does not trace.
namespace gi1test {
std::string ran = "-", leak = "-", react = "-", tiers = "-";
double dark = 0;
std::vector<float> trace;
}
void InjectGI1Test(bdg_engine* e, int frame, float) {
  using namespace gi1test;
  bdg::EditorModel* m = e->model.get();
  bdg::SceneView* sv = e->sceneView.get();
  if (!m || !sv || !e->r3d) return;
  using gfxtest::Run;
  auto mean = [](const gfxtest::Shot& s) { // mean luminance of the middle half of the picture
    double sum = 0;
    size_t n = 0;
    for (int y = s.h / 4; y < s.h * 3 / 4; ++y)
      for (int x = s.w / 4; x < s.w * 3 / 4; ++x, ++n) {
        const uint8_t* q = &s.px[((size_t)y * s.w + x) * 4];
        sum += 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
      }
    return n ? sum / (double)n : 0.0;
  };
  if (frame == 2) {
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) Run(m, "del", id, "{}");
    Run(m, "add", "", R"({"id":"gsun","name":"Sun"})");
    Run(m, "set", "gsun", R"({"path":"Transform.rotation","value":[-50,30,0]})");
    Run(m, "addcomp", "gsun", R"({"type":"Light","value":{"type":"Directional","intensity":4}})");
    auto box = [&](const char* id, const char* pos, const char* scale) {
      Run(m, "add", "", std::format(R"({{"id":"{}","name":"{}"}})", id, id));
      Run(m, "set", id, std::format(R"({{"path":"Transform.position","value":[{}]}})", pos));
      Run(m, "set", id, std::format(R"({{"path":"Transform.scale","value":[{}]}})", scale));
      Run(m, "addcomp", id, R"({"type":"MeshRenderer","value":{"mesh":"Cube","material":"M_Ground"}})");
    };
    box("gground", "0,-0.5,0", "40,1,40");
    box("gwest", "-2,1.5,0", "0.1,3,4.1");
    box("geast", "2,1.5,0", "0.1,3,4.1");
    box("gnorth", "0,1.5,-2", "4.1,3,0.1");
    box("gsouth", "0,1.5,2", "4.1,3,0.1");
    box("groof", "0,3,0", "4.2,0.1,4.2");
    Run(m, "add", "", R"({"id":"gvol","name":"Probes"})");
    Run(m, "set", "gvol", R"({"path":"Transform.position","value":[0,2,0]})");
    Run(m, "addcomp", "gvol", R"({"type":"LightProbeVolume","value":{"size":[12,4,12],"countX":12,"countY":4,"countZ":12}})");
    m->SetSelection({});
    sv->showGrid = false;
    sv->iconSize = 0;
    sv->camera.pivot = {0, 1.0f, 0};
    sv->camera.yaw = 30;
    sv->camera.pitch = -20;
    sv->camera.distance = 1.5f;
    sv->quality = bdg::QualityPreset(bdg::QualityTier::High);
    sv->quality.ssr = false;
    sv->quality.volumetricFog = false;
    sv->quality.giProbesPerFrame = 600; // every probe each frame: settled quickly
  }
  if (frame == 14) {
    const bdg::DynamicGI* gi = e->r3d->TracedGI();
    ran = sv->LastStats().dynamicGI && gi && gi->RaysTraced() > 0 && gi->Triangles() == 6 * 12 ? "1" : "0";
    const gfxtest::Shot closed = gfxtest::Grab(e);
    if (const char* dump = std::getenv("BDG_GI1_DUMP")) bdg::WritePNG((std::string(dump) + "_dark.png").c_str(), closed.w, closed.h, closed.px.data(), true);
    dark = mean(closed);
    sv->quality.giVisibility = false;
  }
  if (frame == 17) {
    const gfxtest::Shot open = gfxtest::Grab(e);
    if (const char* dump = std::getenv("BDG_GI1_DUMP")) bdg::WritePNG((std::string(dump) + "_leak.png").c_str(), open.w, open.h, open.px.data(), true);
    const double lit = mean(open);
    leak = lit > dark * 1.5 + 6 ? "1" : std::format("0(vis {:.1f} novis {:.1f})", dark, lit);
    sv->quality.giVisibility = true;
    sv->quality.giProbesPerFrame = 144; // a quarter of the probes per frame: a sweep takes 4 frames
    Run(m, "add", "", R"({"id":"glamp","name":"Lamp"})");
    Run(m, "set", "glamp", R"({"path":"Transform.position","value":[0,2.2,0]})");
    Run(m, "addcomp", "glamp", R"({"type":"Light","value":{"type":"Point","color":[1,0.5,0.25,1],"intensity":20,"range":6,"castShadows":false}})");
  }
  if (frame >= 18 && frame <= 60)
    if (const bdg::DynamicGI* gi = e->r3d->TracedGI()) trace.push_back(gi->Irradiance({1.0f, 0.0f, 0.5f}, {0, 1, 0}).r);
  if (frame == 60) {
    if (const char* dump = std::getenv("BDG_GI1_DUMP")) {
      const gfxtest::Shot s = gfxtest::Grab(e);
      bdg::WritePNG((std::string(dump) + "_lamp.png").c_str(), s.w, s.h, s.px.data(), true);
    }
    const float final = trace.empty() ? 0.f : trace.back();
    size_t reach = 0;
    while (reach < trace.size() && trace[reach] < 0.9f * final) ++reach;
    react = final > 0.05f && reach + 1 <= 24 ? "1" : std::format("0(90% after {} frames, final {:.3f})", reach + 1, final);
    bdg::LogInfo("gi1: lamp bounce light 90% after {} frames (a sweep every 4), final {:.3f}", reach + 1, final);
    sv->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
  }
  if (frame == 63) tiers = !sv->LastStats().dynamicGI ? "1" : "0";
}
void ReportGI1Test(bdg_engine*) {
  using namespace gi1test;
  bdg::LogInfo("gi1 test: ran={} leak={} react={} tiers={}", ran, leak, react, tiers);
}
BDG_EDITOR_TEST("BDG_GI1_TEST", InjectGI1Test, ReportGI1Test)

// BDG_DDS_TEST=1: DDS textures on the GPU (Core/DDS.h, Renderer3D::UploadDDS). BC1, BC3, BC5 and BC7 files made
// here (8 x 8, 4 mips, one block repeated) are uploaded and their smallest mip read back: each must match the CPU
// decode of that level. The log says how many went up as blocks and how many were decoded first;
// BDG_NO_BLOCK_TEXTURES=1 forces decoding (the path for drivers without the format).
void InjectDDSTest(bdg_engine* e, int frame, float) {
  if (frame != 2 || !e->r3d) return;
  auto block = [](int fmt) -> std::vector<uint8_t> { // one 4 x 4 block per format
    switch (fmt) {
      case 1: return {0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};                             // BC1: red
      case 3: return {200, 200, 0, 0, 0, 0, 0, 0, 0xE0, 0x07, 0x00, 0x00, 0, 0, 0, 0}; // BC3: green, alpha 200
      case 5: return {64, 64, 0, 0, 0, 0, 0, 0, 192, 192, 0, 0, 0, 0, 0, 0};           // BC5: X 64, Y 192
      default: return {0x40, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // BC7 (mode 6)
    }
  };
  auto make = [&](int fmt) {
    uint32_t hd[31] = {};
    hd[0] = 124;
    hd[1] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;
    hd[2] = hd[3] = 8;
    hd[6] = 4;
    hd[18] = 32;
    hd[19] = 0x4;
    hd[20] = fmt == 7 ? 0x30315844u : fmt == 1 ? 0x31545844u : fmt == 3 ? 0x35545844u : 0x32495441u; // DX10 DXT1 DXT5 ATI2
    std::vector<uint8_t> f = {'D', 'D', 'S', ' '};
    f.insert(f.end(), (const uint8_t*)hd, (const uint8_t*)hd + sizeof(hd));
    if (fmt == 7) {
      const uint32_t dx10[5] = {98, 3, 0, 1, 0};
      f.insert(f.end(), (const uint8_t*)dx10, (const uint8_t*)dx10 + sizeof(dx10));
    }
    const std::vector<uint8_t> b = block(fmt);
    for (int n : {4, 1, 1, 1}) // 8x8 = 4 blocks, then 4x4, 2x2, 1x1
      for (int i = 0; i < n; ++i) f.insert(f.end(), b.begin(), b.end());
    return f;
  };
  std::string results;
  bool all = true;
  for (int fmt : {1, 3, 5, 7}) {
    const std::vector<uint8_t> file = make(fmt);
    bdg::DdsImage img;
    std::vector<uint8_t> want;
    int32_t w = 0, h = 0;
    bool ok = bdg::ParseDDS(file.data(), file.size(), img) && bdg::DecodeDDS(img, 3, want, w, h);
    const bdg::TextureHandle t = ok ? e->r3d->UploadDDS(img, false) : bdg::TextureHandle{};
    float mean[4] = {};
    ok = ok && t.Valid() && e->rhi.ReadTextureMean(t, mean);
    const int channels = fmt == 5 ? 2 : fmt == 3 ? 4 : 3;
    for (int c = 0; ok && c < channels; ++c) ok = std::fabs(mean[c] * 255.f - (float)want[(size_t)c]) < 2.f;
    if (t.Valid()) e->rhi.DestroyTexture(t);
    all = all && ok;
    results += std::format(" bc{}={}", fmt, ok ? 1 : 0);
  }
  bdg::LogInfo("dds test: uploaded={} decoded={}{} all={}", e->r3d->DdsUploaded(), e->r3d->DdsDecoded(), results, all ? 1 : 0);
}
void ReportDDSTest(bdg_engine*) {}
BDG_EDITOR_TEST("BDG_DDS_TEST", InjectDDSTest, ReportDDSTest)
