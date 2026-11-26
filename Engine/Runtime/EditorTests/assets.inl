// Scripted editor checks: assets (moved out of Engine.cpp). Included by Engine.cpp just before the
// C API, in the same translation unit: bdg_engine and every editor helper are visible here.
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h) instead of editing Engine.cpp.
#include "Scripting/SaveGame.h"
#include "VFX/VfxComponents.h"
#include "VFX/VfxWorld.h"
#include "Terrain/TerrainData.h"
#include "Terrain/TerrainTool.h"
#include "Terrain/TerrainView.h"
#include "Net/PlaySession.h"

// Scripted check of 2-5d (BDG_IMPORT_TEST=1, BDG_IMPORT_SRC = a folder outside the project with rock.png 8x8,
// Box.gltf + Box.bin; BDG_ASSETS = an empty project whose AssetRules.json wants T_ textures up to 4 px): drop two
// files on the Project panel (the .gltf brings its .bin), the same file again (renamed), rule problems, the Rules
// view, the model dropped on the Scene view (placed, one undo step, the .bin shared), the .bin changed on disk (the
// model is imported again) and the warnings on save.
void InjectImportTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen) return;
  auto* v = e->importTestValues;
  const float s = e->editor->Scale();
  const char* srcEnv = std::getenv("BDG_IMPORT_SRC");
  const std::filesystem::path src = srcEnv ? srcEnv : "";
  auto drop = [&](const char* widget, std::initializer_list<const char*> names) {
    bdg::Widget* w = e->editor->Find(widget);
    if (!w || w->hidden || w->w <= 0) { bdg::LogError("import test: {} is not visible", widget); return; }
    for (const char* n : names) {
      bdg::InputEvent ev{T::DropFile, (w->x + w->w * 0.5f) * s, (w->y + w->h * 0.5f) * s};
      ev.text = (src / n).string();
      e->window.InjectEvent(ev);
    }
  };
  static size_t depth0;
  static int reimportAt = 0;
  if (frame == 2) {
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("project");
  }
  if (frame == 3) drop("project", {"rock.png", "Box.gltf"});
  if (frame == 5) {
    v[0] = std::format("{}", e->assets.ByPath("rock.png") && e->assets.ByPath("Box.gltf") && e->assets.ByPath("Box.bin") ? 1 : 0);
    drop("project", {"rock.png"});
  }
  if (frame == 7) {
    v[1] = std::format("{}", e->assets.ByPath("rock 1.png") ? 1 : 0);
    const std::vector<std::string> only = {"rock.png", "Box.gltf"};
    const auto issues = m->CheckAssetRules(&only);
    auto has = [&](const char* path, const char* rule) {
      return std::any_of(issues.begin(), issues.end(), [&](const bdg::AssetIssue& i) { return i.path == path && i.rule == rule; });
    };
    v[2] = std::format("{}", issues.size() == 2 && has("rock.png", "prefix") && has("rock.png", "size") ? 1 : 0);
    m->ShowProjectView(bdg::EditorModel::ProjectView::Rules);
  }
  if (frame == 9) {
    v[3] = std::format("{}", e->editor->Find("p.issue.0") && e->editor->Find("p.issue.3") && !e->editor->Find("p.issue.4") ? 1 : 0);
    m->ShowProjectView(bdg::EditorModel::ProjectView::Files);
    depth0 = m->bus.UndoDepth();
    drop("scene", {"Box.gltf"});
  }
  if (frame == 11) {
    bdg::SceneObject* o = m->scene.Find(m->Selected());
    bdg::Component* c = o ? o->Find("MeshRenderer") : nullptr;
    const std::string mesh = c ? static_cast<const bdg::MeshRenderer*>(c->data)->mesh : std::string();
    v[4] = std::format("{}", mesh == "Box 1.gltf" && m->bus.UndoDepth() == depth0 + 1 && !e->assets.ByPath("Box 1.bin") ? 1 : 0);
  }
  if (frame == 14) { // a bigger triangle, written by another program
    const float tri[9] = {0, 0, 0, 3, 0, 0, 0, 3, 0};
    std::ofstream(std::filesystem::path(std::u8string((const char8_t*)e->assets.Root().c_str())) / "Box.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(tri), sizeof tri);
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // the watcher thread sees it
  }
  if (frame > 14 && !reimportAt && e->sceneView && e->sceneView->ReimportCount() > 0) {
    v[5] = "1";
    reimportAt = frame;
    m->SaveScene();
    v[6] = std::format("{}", m->lastRuleIssues == 4 ? 1 : 0); // rock.png and rock 1.png: prefix + size each
  }
}

// Scripted check of E-5 (BDG_FBX_TEST=1, BDG_ASSETS = a project with Quad.obj + .mtl + texture and an ASCII
// Tri.fbx): both get rendered thumbnails that show something, both are placed in the scene and drawn (the
// Scene view's triangle count grows), and the Inspector calls the FBX a Model.
void InjectFbxTest(bdg_engine* e, int frame) {
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen || !e->previewsReady || !e->sceneView || !e->r3d) return;
  auto* v = e->fbxTestValues;
  const std::filesystem::path cache = std::filesystem::path(std::u8string((const char8_t*)e->assets.Root().c_str())).parent_path() / ".cache" / "thumbnails";
  static int ready = 0;
  static uint64_t tris0 = 0;
  if (frame > 2 && !ready && frame < 60) {
    bool all = true;
    for (const char* p : {"Quad.obj", "Tri.fbx"})
      if (const bdg::AssetRecord* r = e->assets.ByPath(p)) all = all && e->previews.Thumbnail(*r).Valid();
      else all = false;
    if (all) {
      ready = frame;
      v[0] = "1";
      bool shows = true;
      for (const char* p : {"Quad.obj", "Tri.fbx"}) {
        const bdg::AssetRecord* r = e->assets.ByPath(p);
        int32_t w = 0, h = 0;
        std::vector<uint8_t> px;
        int differ = 0;
        if (r && bdg::LoadImageRGBA(cache / std::format("{}-{:016x}-r1.png", r->guid, r->hash), w, h, px) && w == 64)
          for (int i = 0; i < 64 * 64; ++i)
            differ += std::abs(px[i * 4] - px[0]) + std::abs(px[i * 4 + 1] - px[1]) + std::abs(px[i * 4 + 2] - px[2]) > 30;
        shows = shows && differ > 40;
        if (differ <= 40) bdg::LogError("fbx test: {} thumbnail shows {} pixels", p, differ);
      }
      v[1] = shows ? "1" : "0";
      tris0 = e->r3d->TrianglesLastFrame();
      if (bdg::Widget* sw = e->editor->Find("scene")) {
        e->sceneView->PlaceModel(*m, "Quad.obj", sw->x + sw->w * 0.4f, sw->y + sw->h * 0.6f);
        e->sceneView->PlaceModel(*m, "Tri.fbx", sw->x + sw->w * 0.6f, sw->y + sw->h * 0.6f);
      }
    }
  }
  if (ready && frame == ready + 4) {
    const uint64_t tris1 = e->r3d->TrianglesLastFrame();
    v[2] = std::format("{}", tris1 >= tris0 + 3 ? 1 : 0); // the quad's 2 triangles and the FBX's 1 (shadow passes add more)
    if (v[2] != "1") bdg::LogError("fbx test: triangles {} -> {}", tris0, tris1);
    m->InspectAsset("Tri.fbx");
  }
  if (ready && frame == ready + 6) {
    bdg::Widget* kind = e->editor->Find("insp.asset.종류");
    v[3] = std::format("{}", kind && kind->text2 == "Model" && e->editor->Find("insp.preview") ? 1 : 0);
  }
}

// Scripted check of E-1 (BDG_PREVIEW_TEST=1, BDG_ASSETS = a project with T_Red.png, Tri.gltf + Tri.bin and a red
// M_Red.material): tiles, thumbnails for all three (rendered ones cached as PNG; the material's is red; the
// model's shows something), clicking a tile shows the asset in the Inspector with a live preview that turns when
// dragged, the Hierarchy brings the objects back, and the list layout still works.
void InjectPreviewTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen || !e->previewsReady) return;
  auto* v = e->previewTestValues;
  const float s = e->editor->Scale();
  auto click = [&](const std::string& id) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w || w->hidden || w->w <= 0) { bdg::LogError("preview test: {} is not clickable", id); return; }
    const float x = (w->x + w->w * 0.5f) * s, y = (w->y + w->h * 0.5f) * s;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  const std::filesystem::path cache = std::filesystem::path(std::u8string((const char8_t*)e->assets.Root().c_str())).parent_path() / ".cache" / "thumbnails";
  auto rendered = [&](const char* path, std::vector<uint8_t>& px) {
    const bdg::AssetRecord* r = e->assets.ByPath(path);
    int32_t w = 0, h = 0;
    return r && bdg::LoadImageRGBA(cache / std::format("{}-{:016x}-r1.png", r->guid, r->hash), w, h, px) && w == 64 && h == 64;
  };
  static int ready = 0;
  static std::vector<uint8_t> liveA;
  if (frame == 2)
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("project");
  if (frame == 4) {
    bdg::Widget* tile = e->editor->Find("p.item.T_Red.png");
    v[0] = std::format("{}", tile && tile->kind == bdg::WidgetKind::Custom && std::abs(tile->w - 84) < 1 ? 1 : 0);
  }
  if (frame > 4 && !ready && frame < 60) {
    bool all = true;
    for (const char* p : {"T_Red.png", "Tri.gltf", "M_Red.material"})
      if (const bdg::AssetRecord* r = e->assets.ByPath(p)) all = all && e->previews.Thumbnail(*r).Valid();
      else all = false;
    if (all) {
      ready = frame;
      v[1] = "1";
      std::vector<uint8_t> mat, mdl;
      const bool okMat = rendered("M_Red.material", mat), okMdl = rendered("Tri.gltf", mdl);
      const uint8_t* c = okMat ? &mat[(32 * 64 + 32) * 4] : nullptr;
      v[2] = std::format("{}", okMat && okMdl && c[0] > c[1] + 60 && c[0] > c[2] + 60 ? 1 : 0);
      int differ = 0; // the model's picture is not just the background
      if (okMdl)
        for (int i = 0; i < 64 * 64; ++i)
          differ += std::abs(mdl[i * 4] - mdl[0]) + std::abs(mdl[i * 4 + 1] - mdl[1]) + std::abs(mdl[i * 4 + 2] - mdl[2]) > 30;
      v[3] = std::format("{}", differ > 40 ? 1 : 0);
      click("p.item.Tri.gltf");
    }
  }
  if (ready && frame == ready + 3) {
    bdg::Widget* kind = e->editor->Find("insp.asset.종류");
    v[4] = std::format("{}", m->InspectedAsset() == "Tri.gltf" && e->editor->Find("insp.preview") && kind && kind->text2 == "Model" ? 1 : 0);
    int32_t w = 0, h = 0;
    e->previews.ReadLive(w, h, liveA);
    if (bdg::Widget* pv = e->editor->Find("insp.preview")) { // drag across the preview
      const float x = (pv->x + pv->w * 0.5f) * s, y = (pv->y + pv->h * 0.5f) * s;
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseMove, x + 100 * s, y + 20 * s});
      e->window.InjectEvent({T::MouseUp, x + 100 * s, y + 20 * s, 1});
    }
  }
  if (ready && frame == ready + 6) {
    int32_t w = 0, h = 0;
    std::vector<uint8_t> liveB;
    e->previews.ReadLive(w, h, liveB);
    int differ = 0;
    for (size_t i = 0; i + 3 < std::min(liveA.size(), liveB.size()); i += 4)
      differ += std::abs(liveA[i] - liveB[i]) + std::abs(liveA[i + 1] - liveB[i + 1]) + std::abs(liveA[i + 2] - liveB[i + 2]) > 30;
    v[5] = std::format("{}", !liveA.empty() && liveA.size() == liveB.size() && differ > 100 && std::abs(e->previewYaw - 35) > 20 ? 1 : 0);
    if (v[5] != "1") bdg::LogError("preview test: orbit differ={} yaw={} sizes {} {}", differ, e->previewYaw, liveA.size(), liveB.size());
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("inspector");
    click("h.g8");
  }
  if (ready && frame == ready + 9) {
    v[6] = std::format("{}", m->InspectedAsset().empty() && !e->editor->Find("insp.preview") && m->Selected() == "g8" ? 1 : 0);
    m->SetProjectTiles(!m->projectTiles); // the view button is in the Project ⋯ menu now (UI review B8; BDG_SEARCH_TEST clicks it)
  }
  if (ready && frame == ready + 12) {
    bdg::Widget* row = e->editor->Find("p.item.T_Red.png");
    v[7] = std::format("{}", row && row->kind == bdg::WidgetKind::ListRow ? 1 : 0);
  }
}

// Scripted material check (BDG_MATERIAL_TEST=1 with BDG_ASSETS = an empty folder, queue 3-1b): writes a small project
// (two materials, an instance, two textures), assigns them through commands and reads the rendered colours back.
void InjectMaterialTest(bdg_engine* e, int frame) {
  namespace fs = std::filesystem;
  bdg::SceneView* sv = e->sceneView.get();
  bdg::EditorModel* m = e->model.get();
  if (!sv || !m || !e->assetsOpen) return;
  auto* v = e->materialTestValues;
  fs::path root = fs::path(std::u8string((const char8_t*)e->assets.Root().c_str()));
  auto write = [&](const char* rel, const std::string& text) {
    fs::create_directories((root / rel).parent_path());
    std::FILE* f = _wfopen((root / rel).c_str(), L"wb");
    if (f) { std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); }
  };
  // 64x64 picture in 4 quadrants (top-left first) and a gently bumpy normal map.
  static const uint8_t quad[4][3] = {{230, 60, 160}, {60, 200, 220}, {250, 150, 30}, {120, 90, 230}};
  auto set = [&](const char* id, const char* material) {
    bdg::Command c;
    c.name = "set";
    c.target = id;
    c.args = std::format(R"({{"path":"MeshRenderer.material","value":"{}"}})", material);
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("material test: {}", err);
  };
  auto pixel = [&](glm::vec3 world, int rgb[3]) {
    int32_t w, h;
    std::vector<uint8_t> px;
    sv->ReadPixels(w, h, px);
    float x, y;
    rgb[0] = rgb[1] = rgb[2] = -1;
    if (!bdg::ProjectToPixel(sv->camera.ToCamera(), w, h, world, x, y) || x < 0 || y < 0 || x >= w || y >= h) return;
    const uint8_t* p = &px[((size_t)y * w + (size_t)x) * 4];
    rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2];
  };
  auto close = [](const int a[3], int r, int g, int b, int tol) {
    return std::abs(a[0] - r) <= tol && std::abs(a[1] - g) <= tol && std::abs(a[2] - b) <= tol;
  };
  static size_t undo0;
  if (frame == 2) {
    std::vector<uint8_t> img(64 * 64 * 4), nrm(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const uint8_t* c = quad[(y / 32) * 2 + x / 32];
        uint8_t* p = &img[(y * 64 + x) * 4];
        p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
        float nx = 0.3f * std::sin(x * 0.4f), ny = 0.3f * std::cos(y * 0.4f), nz = std::sqrt(1 - nx * nx - ny * ny);
        uint8_t* q = &nrm[(y * 64 + x) * 4];
        q[0] = (uint8_t)std::lround((nx * 0.5f + 0.5f) * 255); q[1] = (uint8_t)std::lround((ny * 0.5f + 0.5f) * 255);
        q[2] = (uint8_t)std::lround((nz * 0.5f + 0.5f) * 255); q[3] = 255;
      }
    std::vector<uint8_t> png;
    bdg::EncodePNG(64, 64, img.data(), false, png);
    write("Textures/T_Check_D.png", std::string(png.begin(), png.end()));
    bdg::EncodePNG(64, 64, nrm.data(), false, png);
    write("Textures/T_Bumps_N.png", std::string(png.begin(), png.end()));
    write("Materials/M_TestRed.material", "{\"material\":\"M_TestRed\",\"shading\":\"unlit\",\"blend\":\"opaque\"}\n"
                                           "{\"param\":\"BaseColor\",\"type\":\"color\",\"value\":\"#d02020\"}\n");
    write("Materials/MI_TestBlue.matinst", "{\"instance\":\"MI_TestBlue\",\"parent\":\"M_TestRed\"}\n"
                                            "{\"set\":\"BaseColor\",\"value\":\"#2050e0\"}\n");
    write("Materials/M_Textured.material", "{\"material\":\"M_Textured\",\"shading\":\"unlit\"}\n"
                                            "{\"param\":\"BaseColorMap\",\"type\":\"texture\",\"value\":\"Textures/T_Check_D.png\"}\n"
                                            "{\"param\":\"NormalMap\",\"type\":\"texture\",\"value\":\"Textures/T_Bumps_N.png\"}\n");
    e->assets.Refresh();
    e->materials.Invalidate();
    undo0 = m->bus.UndoDepth();
    set("g8", "M_TestRed");
    set("g6", "MI_TestBlue");
    set("g5", "Materials/M_Textured.material");
  }
  if (frame == 4) {
    int c[3];
    glm::vec3 center;
    float r;
    sv->ObjectCenter(*m, "g8", center, r);
    pixel(center, c);
    auto shown = [&](int r, int g, int b, int out[3]) { // what the tier's tone map makes of an unlit sRGB colour
      bdg::Color lin = bdg::SrgbToLinear((uint8_t)r, (uint8_t)g, (uint8_t)b);
      const bdg::QualitySettings& q = sv->quality;
      float ch[3] = {lin.r, lin.g, lin.b};
      for (int k = 0; k < 3; ++k) {
        float x = ch[k] * q.exposure;
        x = q.toneMap ? bdg::ToneMapACES(x) : std::clamp(x, 0.f, 1.f);
        out[k] = (int)std::lround((x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.f / 2.4f) - 0.055f) * 255.f);
      }
    };
    int red[3], blue[3], q0[3], q3[3];
    shown(208, 32, 32, red);
    shown(32, 80, 224, blue);
    shown(quad[0][0], quad[0][1], quad[0][2], q0);
    shown(quad[3][0], quad[3][1], quad[3][2], q3);
    v[0] = std::format("{}", close(c, red[0], red[1], red[2], 3) ? 1 : 0);
    sv->ObjectCenter(*m, "g6", center, r);
    pixel(center + glm::vec3(0, -0.25f, 0), c); // below the Weapon child
    v[1] = std::format("{}", close(c, blue[0], blue[1], blue[2], 3) ? 1 : 0);
    int tl[3], br[3]; // ground plane: picture top-left at (-x, -z), bottom-right at (+x, +z)
    pixel({-4.5f, 0, -4.5f}, tl); // cell centres: the 1 m grid lines are drawn over the ground
    pixel({1.5f, 0, 3.5f}, br);
    bool texOk = close(tl, q0[0], q0[1], q0[2], 8) && close(br, q3[0], q3[1], q3[2], 8);
    v[2] = texOk ? "1" : std::format("0(tl={},{},{} br={},{},{})", tl[0], tl[1], tl[2], br[0], br[1], br[2]);
    auto preset = [&](const char* rel) {
      std::vector<uint8_t> b;
      bdg::ReadFileBytes(root / (std::string(rel) + ".meta"), b);
      return std::string(bdg::PresetName(bdg::ParseTextureSettings(std::string(b.begin(), b.end()), rel).preset));
    };
    v[3] = preset("Textures/T_Check_D.png") + "," + preset("Textures/T_Bumps_N.png");
    v[4] = std::format("{}", e->materials.Cooker().GetStats().cooked);
    e->materials.Shutdown(); // drop the GPU copies: the next lookups read the cooked DDS files
    e->materials.Invalidate();
  }
  if (frame == 6) {
    v[5] = std::format("{}", e->materials.Cooker().GetStats().loaded);
    std::string formats;
    for (const char* rel : {"Textures/T_Check_D.png", "Textures/T_Bumps_N.png"}) {
      const bdg::AssetRecord* r = e->assets.ByPath(rel);
      bdg::CookedTexture t;
      for (auto& f : fs::directory_iterator(root.parent_path() / ".cache" / "cooked"))
        if (r && f.path().filename().string().starts_with(r->guid)) bdg::ReadDDS(f.path(), t);
      formats += std::string(formats.empty() ? "" : ",") + (t.format == bdg::CookedFormat::BC7_SRGB ? "BC7_SRGB" : t.format == bdg::CookedFormat::BC5 ? "BC5" : "?");
      formats += std::format("x{}", t.mips.size());
    }
    v[6] = formats;
    v[7] = std::format("{}", m->bus.UndoDepth() - undo0);
  }
}

// Scripted check of 2-5b (BDG_REFS_TEST=1, BDG_ASSETS = the small project CI writes): references of the knight
// model (from the open scene), a click on one selects the object, the unused list (a texture reached through the
// material counts as used), a delete warning for a used asset, deleting an unused one, replacing the model in the
// open scene (one undo step) and a batch rename of two objects.
void InjectRefsTest(bdg_engine* e, int frame) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen) return;
  auto* v = e->refsTestValues;
  const float s = e->editor->Scale();
  auto click = [&](const std::string& id) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w || w->hidden || w->w <= 0) { bdg::LogError("refs test: {} is not clickable", id); return; }
    const float x = (w->x + w->w * 0.5f) * s, y = (w->y + w->h * 0.5f) * s;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  auto mesh = [&](const char* id) {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("MeshRenderer") : nullptr;
    return c ? static_cast<const bdg::MeshRenderer*>(c->data)->mesh : std::string();
  };
  static size_t depth0;
  if (frame == 2) {
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("project");
    m->SelectProjectItem("Characters/Knight.glb");
    m->Select("g8");
  }
  if (frame == 4) click("p.refs");
  if (frame == 6) {
    const auto refs = m->FindReferences("Characters/Knight.glb");
    v[0] = std::format("{}", refs.size() == 1 && refs[0].from == "열린 장면" && refs[0].where == "g6 Player · MeshRenderer.mesh" ? 1 : 0);
    click("p.ref.0"); // selects the Player
  }
  if (frame == 8) {
    v[1] = std::format("{}", m->Selected() == "g6" ? 1 : 0);
    m->ShowProjectView(bdg::EditorModel::ProjectView::Unused); // in the Project ⋯ menu now (UI review B8)
  }
  if (frame == 10) {
    const auto unused = m->FindUnused();
    auto has = [&](const char* p) { return std::find(unused.begin(), unused.end(), p) != unused.end(); };
    v[2] = std::format("{}", has("Textures/old.png") && has("Models/Unused.glb") && !has("Textures/knight.png") && !has("Characters/Knight.glb") &&
                                     e->editor->Find("p.unused.Models/Unused.glb") ? 1 : 0);
    m->ShowProjectView(bdg::EditorModel::ProjectView::Files);
    m->SelectProjectItem("Characters/Knight.glb");
  }
  if (frame == 12) click("p.delete"); // used: only a warning
  if (frame == 14) {
    v[3] = std::format("{}", m->DeleteWarningShown() && e->assets.ByPath("Characters/Knight.glb") ? 1 : 0);
    m->SelectProjectItem("Models/Unused.glb");
  }
  if (frame == 16) click("p.delete"); // unused: deleted at once
  if (frame == 18) {
    v[4] = std::format("{}", !e->assets.ByPath("Models/Unused.glb") && !std::filesystem::exists(std::filesystem::path(e->assets.Root()) / "Models/Unused.glb")
                                 ? 1 : 0);
    depth0 = m->bus.UndoDepth();
    m->ReplaceReferences("Characters/Knight.glb", "Characters/Other.glb");
  }
  if (frame == 20) {
    v[5] = std::format("{}", mesh("g6") == "Other.glb" && m->bus.UndoDepth() == depth0 + 1 ? 1 : 0);
    std::string err;
    m->bus.Undo(&err);
  }
  if (frame == 22) {
    v[6] = std::format("{}", mesh("g6") == "Knight.glb" ? 1 : 0);
    m->SetSelection({"g6", "g8"});
  }
  if (frame == 24) {
    bdg::Widget* name = e->editor->Find("insp.name");
    if (name && name->onCommit) {
      name->text2 = "Hero_##";
      name->onCommit(*name);
    }
  }
  if (frame == 26) {
    const bdg::SceneObject* a = m->scene.Find("g6");
    const bdg::SceneObject* b = m->scene.Find("g8");
    v[7] = std::format("{}", a && b && a->name == "Hero_01" && b->name == "Hero_02" ? 1 : 0);
  }
}

// Scripted Project panel check (BDG_PROJECT_TEST=1 with BDG_ASSETS holding Scripts/Weapon.h + .cpp and more):
// open the Scripts folder, select the C++ class item, rename it to Sword through the name field.
void InjectProjectTest(bdg_engine* e, int frame, float scale) {
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen) return;
  auto click = [&](const std::string& id) {
    bdg::Widget* w = e->editor->Find(id);
    if (!w) { bdg::LogError("project test: no widget {}", id); return; }
    float x = (w->x + 30) * scale, y = (w->y + w->h * 0.5f) * scale;
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  auto* v = e->projectTestValues;
  static std::string guidH, guidCpp;
  if (frame == 2) {
    guidH = e->assets.ByPath("Scripts/Weapon.h") ? e->assets.ByPath("Scripts/Weapon.h")->guid : "";
    guidCpp = e->assets.ByPath("Scripts/Weapon.cpp") ? e->assets.ByPath("Scripts/Weapon.cpp")->guid : "";
    click("p.folder.Scripts");
  }
  if (frame == 4) {
    std::string rows;
    for (const char* id : {"p.item.Scripts/Weapon", "p.item.Scripts/Weapon.h", "p.item.Scripts/Weapon.cpp", "p.item.Scripts/PlayerMove.cs"})
      if (bdg::Widget* w = e->editor->Find(id)) rows += (rows.empty() ? "" : ",") + w->text + ":" + w->text2;
    v[0] = rows;
    click("p.item.Scripts/Weapon");
  }
  if (frame == 6) {
    v[1] = m->ProjectSelected();
    if (bdg::Widget* f = e->editor->Find("p.name")) {
      e->editor->Focus(f);
      bdg::InputEvent a{T::KeyDown}; a.key = bdg::Key::A; a.ctrl = true; e->window.InjectEvent(a);
      bdg::InputEvent t{T::Text}; t.text = "Sword"; e->window.InjectEvent(t);
      bdg::InputEvent en{T::KeyDown}; en.key = bdg::Key::Enter; e->window.InjectEvent(en);
    }
  }
  if (frame == 8) {
    const bdg::AssetRecord* h = e->assets.ByPath("Scripts/Sword.h");
    const bdg::AssetRecord* c = e->assets.ByPath("Scripts/Sword.cpp");
    v[2] = std::string(h ? "Sword.h" : "-") + "," + (c ? "Sword.cpp" : "-");
    v[3] = h && c && h->guid == guidH && c->guid == guidCpp ? "1" : "0";
  }
}

// Scripted check of 6-3 (BDG_AUDIO_TEST=1 with BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll; no output
// device: BDG_AUDIO_NULL is set): a camera with an AudioListener at the origin and a looping 3D AudioSource 2 m in
// front. In play mode the source plays at once (playOnStart), the mixed level drops when the source is moved 40 m
// away (an Inspector-style set command), pausing silences it and resuming brings it back; then the C# AudioProbe is
// added (Audio.Play, groups, a missing clip, AudioSource.Play), and Stop ends every sound.
#include "Audio/Audio.h"

void InjectAudioTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  static std::string probe;
  static float level[4] = {-1, -1, -1, -1};
  static int playing[2] = {-1, -1};
  const std::filesystem::path dir = std::filesystem::current_path() / ".cache" / "audio-test";
  auto run = [&](const char* name, const char* target, const char* args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("audio test: {}", err);
  };
  bdg::AudioSystem& audio = bdg::AudioSystem::Instance();
  if (frame == 2) {
    _putenv_s("BDG_AUDIO_NULL", "1"); // measure instead of playing out loud
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    { // a 2 s, 440 Hz mono tone
      const int rate = 48000;
      const uint32_t frames = rate * 2;
      std::vector<int16_t> pcm(frames);
      for (uint32_t i = 0; i < frames; ++i) pcm[i] = (int16_t)(std::sin(6.283185307 * 440.0 * i / rate) * 16000);
      std::ofstream f(dir / "Beep.wav", std::ios::binary);
      auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
      auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
      f.write("RIFF", 4); u32(36 + frames * 2); f.write("WAVE", 4);
      f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
      f.write("data", 4); u32(frames * 2);
      f.write(reinterpret_cast<const char*>(pcm.data()), (std::streamsize)pcm.size() * 2);
    }
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.rfind("audio probe: ", 0) == 0) probe = l; });
    run("add", "", R"({"id":"au0","name":"Ears"})");
    run("addcomp", "au0", R"({"type":"Camera"})");
    run("addcomp", "au0", R"({"type":"AudioListener"})");
    run("add", "", R"({"id":"au1","name":"Speaker"})");
    run("set", "au1", R"({"path":"Transform.position","value":[0,0,-2]})");
    run("addcomp", "au1", R"({"type":"AudioSource","value":{"clip":".cache/audio-test/Beep.wav","loop":true,"spatial":true}})");
    run("add", "", R"({"id":"au2","name":"Probe"})");
    run("addcomp", "au2", R"({"type":"AudioSource","value":{"clip":".cache/audio-test/Beep.wav","playOnStart":false,"spatial":false}})");
  }
  if (frame == 3) TogglePlay(e);
  if (frame > 3 && frame < 26) std::this_thread::sleep_for(std::chrono::milliseconds(12));
  if (frame == 8) {
    level[0] = audio.Stats().rms;
    playing[0] = audio.Stats().playing;
    run("set", "au1", R"({"path":"Transform.position","value":[0,0,-40]})");
  }
  if (frame == 13) level[1] = audio.Stats().rms;
  if (frame == 14) SetPlayPaused(e, true);
  if (frame == 16) level[2] = audio.Stats().rms;
  if (frame == 17) SetPlayPaused(e, false);
  if (frame == 20) level[3] = audio.Stats().rms;
  if (frame == 21) // the C# part after the measurements (its own 2D sounds would cover the distance fade)
    run("addcomp", "au2", R"({"type":"AudioProbe","value":{"clip":".cache/audio-test/Beep.wav"}})");
  if (frame == 26) TogglePlay(e);
  if (frame == 28) {
    playing[1] = audio.Stats().playing;
    bdg::SetLogHook({});
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const bool plays = level[0] > 0.02f && playing[0] >= 1;
    const bool follows = level[1] > 0.f && level[1] < level[0] * 0.25f;
    const bool paused = level[2] < 1e-4f && level[3] > 0.f;
    bdg::LogInfo("audio test: plays={} distance={} pause={} csharp={} stopped={} (rms near {:.3f} far {:.4f} paused {:.5f} resumed {:.4f}, playing {})",
                 plays ? 1 : 0, follows ? 1 : 0, paused ? 1 : 0,
                 probe.find("play=1 group=1 missing=1 component=1 stopped=1") != std::string::npos ? 1 : 0, playing[1] == 0 ? 1 : 0, level[0], level[1],
                 level[2], level[3], playing[0]);
  }
}
BDG_EDITOR_TEST("BDG_AUDIO_TEST", InjectAudioTest, nullptr)



// Game UI (7-3) check helpers: an element's field as JSON, and a checksum of its pixels on screen (the back buffer
// holds the last frame while a check injects).
std::string UiField(bdg_engine* e, const char* id, const char* field) {
  bdg::SceneObject* o = e->model ? e->model->scene.Find(id) : nullptr;
  bdg::Component* c = o ? o->Find("UIElement") : nullptr;
  std::string json;
  if (c) bdg::Registry::Get(c->data, *c->type, field, json);
  return json;
}
uint64_t UiPixels(bdg_engine* e, const char* id, float scale) {
  bdg::GameUI::Rect r;
  if (!GameViewUI().ElementRect(id, r)) return 0;
  int32_t w = 0, h = 0;
  e->window.GetDrawableSize(w, h);
  std::vector<uint8_t> px;
  e->rhi.ReadPixels(w, h, px);
  if (px.size() < (size_t)w * h * 4) return 0;
  const int x0 = std::clamp((int)(r.x * scale), 0, w), x1 = std::clamp((int)((r.x + r.w) * scale), 0, w);
  const int y0 = std::clamp((int)(r.y * scale), 0, h), y1 = std::clamp((int)((r.y + r.h) * scale), 0, h);
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (int y = y0; y < y1; ++y) // rows are bottom-up
    for (int x = x0; x < x1; ++x)
      for (int k = 0; k < 3; ++k) { hash ^= px[((size_t)(h - 1 - y) * w + x) * 4 + k]; hash *= 0x100000001b3ULL; }
  return hash;
}

// Scripted check of the FPS template (BDG_FPS_TEST=1, run in a project made by `bdg new <dir> --template fps` with
// that folder as the working folder and BDG_ASSETS=<dir>/Assets): Play, then input injected through the focused Game
// view — W walks the player toward the first target, Space jumps, once landed a left click fires a physics bullet
// that hits the target (it is knocked away and the score in the log goes up), a mouse move turns the head; Stop.
// The HUD (7-3): the score element's text goes from "점수 0" to "점수 1" and its pixels on screen change.
// 6-5b / 6-VFX: the hit throws sparks (particles spawned), the best score goes to "최고 1" and is in the save file after
// Stop (the slot is deleted first; the built game then starts with best 1, checked in CI).
// Run with --frames 150 (the shot waits for the landing).
void InjectFpsTest(bdg_engine* e, int frame, float scale) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  using T = bdg::InputEvent::Type;
  static std::string score;
  static float z0 = 0, z1 = 0, y0 = 0, yMax = -1e9f, yaw0 = 0, yaw1 = 0;
  static bdg::Vec3 target0{};
  static bool targetMoved = false, playing = false;
  static std::string hudBefore, hudAfter, bestAfter;
  static uint64_t pixBefore = 0, pixAfter = 0, sparks = 0;
  auto key = [&](bdg::Key k, bool down) {
    bdg::InputEvent ev{down ? T::KeyDown : T::KeyUp};
    ev.key = k;
    e->window.InjectEvent(ev);
  };
  auto transform = [&](const char* id) -> const bdg::Transform* {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("Transform") : nullptr;
    return c ? static_cast<const bdg::Transform*>(c->data) : nullptr;
  };
  bdg::Widget* game = e->editor->Find("game");
  const float gx = game ? game->x + game->w * 0.5f : 0, gy = game ? game->y + game->h * 0.5f : 0;
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20)); // game time passes (fixed steps)
  if (frame == 2) {
    bdg::DeleteSaveSlot(bdg::GameSaveFolder(), "main"); // a first run: no best score yet
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.rfind("score: ", 0) == 0) score = l; });
    TogglePlay(e);
    playing = e->play.Playing();
  }
  if (frame == 8) { // landed on the ground by now
    if (const auto* t = transform("player")) { z0 = t->position.z; y0 = t->position.y; }
    if (const auto* t = transform("target1")) target0 = t->position;
    key(bdg::Key::W, true);
  }
  if (frame == 16) key(bdg::Key::Space, true);
  if (frame == 18) key(bdg::Key::Space, false);
  if (frame >= 16 && frame <= 34)
    if (const auto* t = transform("player")) yMax = std::max(yMax, t->position.y);
  if (frame == 30) key(bdg::Key::W, false);
  static int fired = 0; // the frame of the shot: once the player has landed again (height steady near the start)
  static float lastY = 0;
  if (frame >= 40 && !fired)
    if (const auto* t = transform("player"); t && ((frame > 44 && std::fabs(t->position.y - lastY) < 0.002f && t->position.y < y0 + 0.2f) || frame >= 110)) {
      fired = frame;
      z1 = t->position.z;
      hudBefore = UiField(e, "ui_score", "text");
      pixBefore = UiPixels(e, "ui_score", scale);
      e->window.InjectEvent({T::MouseMove, gx * scale, gy * scale, 0});
      e->window.InjectEvent({T::MouseDown, gx * scale, gy * scale, 1});
    }
  if (const auto* t = transform("player")) lastY = t->position.y;
  if (fired && frame == fired + 2) e->window.InjectEvent({T::MouseUp, gx * scale, gy * scale, 1});
  if (fired && frame == fired + 20) { // the bullet has hit and the target was pushed (it is removed 1.5 s after the hit)
    const auto* t = transform("target1");
    targetMoved = !t || glm::length(glm::vec3(t->position.x - target0.x, t->position.y - target0.y, t->position.z - target0.z)) > 0.05f;
    if (const auto* h = transform("head")) yaw0 = h->rotation.y;
    e->window.InjectEvent({T::MouseMove, (gx + 40) * scale, gy * scale, 0});
    hudAfter = UiField(e, "ui_score", "text");
    pixAfter = UiPixels(e, "ui_score", scale);
    bestAfter = UiField(e, "ui_best", "text");
    sparks = bdg::VfxWorld::Instance().Totals().spawned;
  }
  if (fired && frame == fired + 24)
    if (const auto* h = transform("head")) yaw1 = h->rotation.y;
  if (fired && frame == fired + 26) TogglePlay(e);
  if (fired && frame == fired + 28) {
    bdg::SetLogHook({});
    const bool moved = z0 - z1 > 1.0f, jumped = yMax - y0 > 0.4f, hit = score.find("score: 1 (+1 Target)") != std::string::npos ||
                                                                         score.find("score: 1 ") != std::string::npos;
    const bool hud = hudBefore == "\"점수 0\"" && hudAfter == "\"점수 1\"" && pixBefore != 0 && pixAfter != 0 && pixBefore != pixAfter;
    std::string saved, err;
    const bool best = bestAfter == "\"최고 1\"" && bdg::ReadSaveSlot(bdg::GameSaveFolder(), "main", saved, &err) && saved.find("\"best\":1") != std::string::npos;
    bdg::LogInfo("fps test: play={} moved={} jumped={} hit={} pushed={} look={} hud={} sparks={} best={} (z {:.2f} -> {:.2f}, jump {:.2f} m, shot at frame {}, yaw {:.1f} -> {:.1f}, hud {} -> {}, {} particles, best {} saved {}, last \"{}\")",
                 playing ? 1 : 0, moved ? 1 : 0, jumped ? 1 : 0, hit ? 1 : 0, targetMoved ? 1 : 0, std::fabs(yaw1 - yaw0) > 1 ? 1 : 0, hud ? 1 : 0,
                 sparks >= 10 ? 1 : 0, best ? 1 : 0, z0, z1, yMax - y0, fired, yaw0, yaw1, hudBefore, hudAfter, sparks, bestAfter, saved, score);
  }
}
BDG_EDITOR_TEST("BDG_FPS_TEST", InjectFpsTest, nullptr)


// Scripted check of the 2D platformer template (BDG_PLATFORMER_TEST=1, run in a project made by `bdg new <dir>
// --template platformer2d` with that folder as the working folder): Play, D held runs the hero right on Box2D and
// through the first two coins (triggers: the coin count in the log goes up), then Space jumps, the camera follows;
// then a click on the HUD's help button (7-3) reaches C# (GameRules shows the help panel); Stop.
void InjectPlatformerTest(bdg_engine* e, int frame, float scale) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  using T = bdg::InputEvent::Type;
  static std::string coins, help, coinsHud;
  static float x0 = 0, x1 = 0, y0 = 0, yMax = -1e9f, cam0 = 0, cam1 = 0;
  static bool panelBefore = true, panelAfter = false;
  auto key = [&](bdg::Key k, bool down) {
    bdg::InputEvent ev{down ? T::KeyDown : T::KeyUp};
    ev.key = k;
    e->window.InjectEvent(ev);
  };
  auto pos = [&](const char* id) -> bdg::Vec3 {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("Transform") : nullptr;
    return c ? static_cast<const bdg::Transform*>(c->data)->position : bdg::Vec3{-999, -999, -999};
  };
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  if (frame == 2) {
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) {
      if (l.rfind("coins: ", 0) == 0) coins = l;
      if (l.rfind("platformer: help", 0) == 0) help = l;
    });
    TogglePlay(e);
  }
  if (frame == 8) {
    x0 = pos("hero").x;
    y0 = pos("hero").y;
    cam0 = pos("camera").x;
    key(bdg::Key::D, true);
  }
  static int jumpedAt = 0; // jump once past the first two coins (x > 5)
  if (frame > 8 && !jumpedAt && (pos("hero").x > 5 || frame >= 50)) {
    jumpedAt = frame;
    key(bdg::Key::Space, true);
  }
  if (jumpedAt && frame == jumpedAt + 10) key(bdg::Key::Space, false);
  if (jumpedAt && frame <= 60) yMax = std::max(yMax, pos("hero").y);
  if (frame == 60) {
    key(bdg::Key::D, false);
    x1 = pos("hero").x;
    cam1 = pos("camera").x;
  }
  bdg::GameUI::Rect hb;
  if (frame == 62 && GameViewUI().ElementRect("ui_help", hb)) { // click the help button (UI units -> window pixels)
    panelBefore = UiField(e, "ui_helppanel", "visible") == "true";
    coinsHud = UiField(e, "ui_coins", "text");
    const float cx = (hb.x + hb.w * 0.5f) * scale, cy = (hb.y + hb.h * 0.5f) * scale;
    e->window.InjectEvent({T::MouseMove, cx, cy, 0});
    e->window.InjectEvent({T::MouseDown, cx, cy, 1});
  }
  if (frame == 64 && GameViewUI().ElementRect("ui_help", hb))
    e->window.InjectEvent({T::MouseUp, (hb.x + hb.w * 0.5f) * scale, (hb.y + hb.h * 0.5f) * scale, 1});
  if (frame == 68) panelAfter = UiField(e, "ui_helppanel", "visible") == "true";
  if (frame == 70) TogglePlay(e);
  if (frame == 72) {
    bdg::SetLogHook({});
    const int count = coins.size() > 7 ? std::atoi(coins.c_str() + 7) : 0;
    const bool button = !panelBefore && panelAfter && help.find("shown") != std::string::npos && GameViewUI().Clicks() >= 1;
    bdg::LogInfo("platformer test: ran={} jumped={} coins={} camera={} button={} hud={} (x {:.2f} -> {:.2f}, jump {:.2f} m, camera {:.2f} -> {:.2f}, hud {}, last \"{}\")",
                 x1 - x0 > 2 ? 1 : 0, yMax - y0 > 1 ? 1 : 0, count >= 2 ? 1 : 0, cam1 - cam0 > 0.5f ? 1 : 0, button ? 1 : 0,
                 coinsHud == "\"동전 2 / 8\"" ? 1 : 0, x0, x1, yMax - y0, cam0, cam1, coinsHud, coins);
  }
}
BDG_EDITOR_TEST("BDG_PLATFORMER_TEST", InjectPlatformerTest, nullptr)


// ---- Game API (6-5b) and particles (6-VFX P-1) ----

namespace edtest_gameapi {
std::string report, run1, run2; // probe lines (BDG_GAMEAPI_TEST)
void Run(bdg::EditorModel* m, const char* name, const char* target, const char* args, const char* who) {
  bdg::Command c;
  c.name = name;
  c.target = target;
  c.args = args;
  std::string err;
  if (!m->bus.Execute(c, &err)) bdg::LogError("{}: {} {}: {}", who, name, target, err);
}
// Writes an effect into <BDG_ASSETS>/Effects (the test project's assets).
bool WriteEffect(const char* file, const char* text) {
  const char* assets = std::getenv("BDG_ASSETS");
  if (!assets) return false;
  const std::filesystem::path dir = bdg::PathU8(assets) / "Effects";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream(dir / file, std::ios::binary | std::ios::trunc) << text;
  return true;
}
// Average brightness (0..255) of the middle of the Game view on screen.
double GameViewLuma(bdg_engine* e, float scale) {
  bdg::Widget* g = e->editor->Find("game");
  if (!g || g->w <= 0) return -1;
  int32_t w = 0, h = 0;
  e->window.GetDrawableSize(w, h);
  std::vector<uint8_t> px;
  e->rhi.ReadPixels(w, h, px);
  if (px.size() < (size_t)w * h * 4) return -1;
  const int x0 = std::clamp((int)((g->x + g->w * 0.3f) * scale), 0, w), x1 = std::clamp((int)((g->x + g->w * 0.7f) * scale), 0, w);
  const int y0 = std::clamp((int)((g->y + g->h * 0.3f) * scale), 0, h), y1 = std::clamp((int)((g->y + g->h * 0.7f) * scale), 0, h);
  double sum = 0;
  size_t n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x, ++n) {
      const uint8_t* p = &px[((size_t)(h - 1 - y) * w + x) * 4];
      sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    }
  return n ? sum / (double)n : -1;
}
} // namespace edtest_gameapi

// Scripted check of 6-5b (BDG_GAMEAPI_TEST=1 with BDG_SCRIPT_ASSEMBLIES=<Managed>/Bidulgi.Samples.dll, BDG_ASSETS = an
// empty assets folder, BDG_SAVE_DIR = an empty folder): the C# GameApiProbe saves values (Vector3, list, Korean
// text) and writes the slot, uses the event bus (by name, with a value, by type, Off, a destroyed listener's handler
// goes), raycasts against static boxes (the crate ahead with its normal, the ground below, a miss), overlaps a sphere
// and plays a particle effect (12 sparks, then gone; a missing effect is a warning). Stop, Play again: the second run
// reads what the first saved. The save file is versioned JSON. Run with --frames 200.
void InjectGameApiTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  using edtest_gameapi::Run;
  using edtest_gameapi::report, edtest_gameapi::run1, edtest_gameapi::run2;
  static int stopAt = 0, play2At = 0;
  if (frame == 2) {
    bdg::DeleteSaveSlot(bdg::GameSaveFolder(), "main");
    edtest_gameapi::WriteEffect("TestSpark.vfx", R"({"vfx":"TestSpark"}
{"emitter":"Sparks","max":32,"duration":0.1,"loop":false}
{"stage":"emitter-update","module":"SpawnBurst","count":12}
{"stage":"particle-spawn","module":"Lifetime","value":0.3}
{"stage":"particle-spawn","module":"VelocityCone","angle":45,"speed":[1,3]}
{"stage":"render","module":"Sprite","blend":"additive","intensity":3}
)");
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) {
      if (l.rfind("game api probe: save=", 0) == 0) report = l;
      else if (l.rfind("game api probe: run=1", 0) == 0) run1 = l;
      else if (l.rfind("game api probe: run=2", 0) == 0) run2 = l;
    });
    const char* who = "game api test";
    Run(m, "add", "", R"({"id":"ga_ground","name":"Ground"})", who);
    Run(m, "set", "ga_ground", R"({"path":"Transform.position","value":[0,-0.5,0]})", who);
    Run(m, "addcomp", "ga_ground", R"({"type":"Collider","value":{"shape":"Box","size":[20,1,20]}})", who);
    Run(m, "add", "", R"({"id":"ga_crate","name":"Crate"})", who);
    Run(m, "set", "ga_crate", R"({"path":"Transform.position","value":[0,1,-5]})", who);
    Run(m, "addcomp", "ga_crate", R"({"type":"Collider","value":{"shape":"Box"}})", who);
    Run(m, "add", "", R"({"id":"ga_listen","name":"Listener"})", who); // before the probe: its Start runs first
    Run(m, "addcomp", "ga_listen", R"({"type":"GameApiProbe","value":{"listener":true}})", who);
    Run(m, "add", "", R"({"id":"ga_probe","name":"Prober"})", who);
    Run(m, "addcomp", "ga_probe", R"({"type":"GameApiProbe"})", who);
  }
  if (frame == 3) TogglePlay(e);
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20)); // game time passes
  if (!stopAt && frame > 3 && (!report.empty() || frame >= 150)) {
    stopAt = frame;
    TogglePlay(e);
  }
  if (stopAt && frame == stopAt + 2) {
    play2At = frame;
    TogglePlay(e); // the second run: reads the first run's save
  }
  if (play2At && frame == play2At + 6) TogglePlay(e);
}
void ReportGameApiTest(bdg_engine*) {
  bdg::SetLogHook({});
  using edtest_gameapi::report, edtest_gameapi::run1, edtest_gameapi::run2;
  auto flag = [&](const char* key) { return report.find(std::string(key) + "=1") != std::string::npos ? 1 : 0; };
  std::string text;
  const std::string path = bdg::SaveSlotPath(bdg::GameSaveFolder(), "main");
  const bool file = bdg::ReadTextU8(path, text) && text.find(R"("format":"bidulgi-save","version":1)") != std::string::npos &&
                    text.find(R"("runs":2)") != std::string::npos && text.find("비둘기") != std::string::npos;
  const bool restored = run1.find("restored=1") != std::string::npos && run2.find("restored=1") != std::string::npos;
  bdg::LogInfo("game api test: save={} events={} raycast={} overlap={} vfx={} restored={} file={} ({}; {})", flag("save"), flag("events"),
               flag("raycast"), flag("overlap"), flag("vfx"), restored ? 1 : 0, file ? 1 : 0, report, run2);
}
BDG_EDITOR_TEST("BDG_GAMEAPI_TEST", InjectGameApiTest, ReportGameApiTest)

// Scripted check of 6-VFX P-1 (BDG_VFX_TEST=1, BDG_ASSETS = an empty assets folder): a ParticleSystem with a looping
// flame (200 particles/s, 1 s each, bright additive sprites) 4 m in front of the game camera. While playing the live
// count climbs to the steady state (rate x lifetime), particles die, the component shows the count; the middle of the
// Game view is brighter than the same view once the flame's particles are cleared (pixels). Then a 3000-particle burst
// at the potato tier is cut to its budget (500 for the whole scene, the rest dropped), at ultra it is not. Stop
// clears all. Run with --frames 150.
void InjectVfxTest(bdg_engine* e, int frame, float scale) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  using edtest_gameapi::Run;
  static double lumaDuring = -1, lumaAfter = -1;
  static size_t aliveEarly = 0, aliveSteady = 0, componentCount = 0, potatoAlive = 0, ultraAlive = 0;
  static uint64_t died = 0, dropped = 0;
  static double tEarly = 0, tSteady = 0;
  static int playAt = 0, burstAt = 0;
  static bool cleared = false;
  bdg::VfxWorld& vfx = bdg::VfxWorld::Instance();
  const char* who = "vfx test";
  if (frame == 2) {
    edtest_gameapi::WriteEffect("TestFlame.vfx", R"({"vfx":"TestFlame"}
{"emitter":"Flame","max":1000,"duration":1,"loop":true}
{"stage":"emitter-update","module":"SpawnRate","rate":200}
{"stage":"particle-spawn","module":"Lifetime","value":1}
{"stage":"particle-spawn","module":"PositionSphere","radius":0.4}
{"stage":"particle-spawn","module":"Velocity","value":[0,0.3,0],"random":[0.1,0.1,0.1]}
{"stage":"particle-spawn","module":"Size","value":[0.6,0.9]}
{"stage":"particle-spawn","module":"Color","value":[1,0.55,0.15,1]}
{"stage":"particle-update","module":"ColorOverLife","gradient":[[0,[1,1,1,1]],[0.8,[1,0.6,0.3,0.8]],[1,[1,0.3,0.1,0]]]}
{"stage":"render","module":"Sprite","blend":"additive","intensity":2}
)");
    edtest_gameapi::WriteEffect("TestBurst.vfx", R"({"vfx":"TestBurst"}
{"emitter":"Many","max":5000,"duration":0.1,"loop":false}
{"stage":"emitter-update","module":"SpawnBurst","count":3000}
{"stage":"particle-spawn","module":"Lifetime","value":3}
{"stage":"particle-spawn","module":"Size","value":0.01}
)");
    // 4 m in front of the first camera (the one the Game view shows); add one if the scene has none
    std::string camId;
    std::vector<std::string> stack(m->scene.Roots().rbegin(), m->scene.Roots().rend());
    while (!stack.empty() && camId.empty()) {
      const bdg::SceneObject* o = m->scene.Find(stack.back());
      stack.pop_back();
      if (!o) continue;
      stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
      if (const_cast<bdg::SceneObject*>(o)->Find("Camera")) camId = o->id;
    }
    if (camId.empty()) {
      Run(m, "add", "", R"({"id":"vx_cam","name":"Camera"})", who);
      Run(m, "addcomp", "vx_cam", R"({"type":"Camera"})", who);
      camId = "vx_cam";
    }
    const glm::mat4 w = m->scene.WorldMatrix(camId);
    const glm::vec3 at = glm::vec3(w[3]) - glm::normalize(glm::vec3(w[2])) * 4.f;
    Run(m, "add", "", R"({"id":"vx_flame","name":"Flame"})", who);
    Run(m, "set", "vx_flame", std::format(R"({{"path":"Transform.position","value":[{},{},{}]}})", at.x, at.y, at.z).c_str(), who);
    Run(m, "addcomp", "vx_flame", R"({"type":"ParticleSystem","value":{"effect":"TestFlame"}})", who);
    Run(m, "add", "", R"({"id":"vx_burst","name":"Burst"})", who);
    Run(m, "addcomp", "vx_burst", R"({"type":"ParticleSystem","value":{"effect":"Effects/TestBurst.vfx","playOnStart":false}})", who);
  }
  if (frame == 7) {
    TogglePlay(e);
    playAt = frame;
  }
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  if (playAt && e->play.Playing() && !aliveEarly && e->play.Time() >= 0.3) {
    aliveEarly = vfx.Alive();
    tEarly = vfx.SimTime(); // simulated time: a slow frame drops time (at most 4 steps per frame)
  }
  if (playAt && e->play.Playing() && !aliveSteady && e->play.Time() >= 1.6) {
    aliveSteady = vfx.Alive();
    tSteady = vfx.SimTime();
    died = vfx.Totals().died;
    if (bdg::SceneObject* o = m->scene.Find("vx_flame"))
      if (bdg::Component* c = o->Find("ParticleSystem")) componentCount = (size_t)static_cast<const bdg::ParticleSystem*>(c->data)->particles;
    lumaDuring = edtest_gameapi::GameViewLuma(e, scale);
    burstAt = frame;
  }
  auto playBurst = [&] {
    if (bdg::SceneObject* o = m->scene.Find("vx_burst"))
      if (bdg::Component* c = o->Find("ParticleSystem")) static_cast<bdg::ParticleSystem*>(c->data)->Play();
  };
  if (burstAt && frame == burstAt + 1) // the flame's particles go at once: the same view without them
    for (const bdg::VfxWorld::Effect& fx : vfx.All())
      if (fx.object == "vx_flame") vfx.Stop(fx.id, true);
  if (burstAt && frame == burstAt + 3) {
    lumaAfter = edtest_gameapi::GameViewLuma(e, scale);
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Potato); // the renderer passes the tier on
  }
  if (burstAt && frame == burstAt + 5) playBurst();
  if (burstAt && frame == burstAt + 7) {
    potatoAlive = vfx.Alive();
    dropped = vfx.Totals().dropped;
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Ultra);
  }
  if (burstAt && frame == burstAt + 9) playBurst(); // again (the first is cleared): all 3000 fit now
  if (burstAt && frame == burstAt + 11) {
    ultraAlive = vfx.Alive();
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
    TogglePlay(e);
  }
  if (burstAt && frame == burstAt + 13) cleared = !vfx.Active() && vfx.Alive() == 0 && vfx.Instances() == 0;
  if (burstAt && frame == burstAt + 14) {
    // rate x time early (+-25%), rate x lifetime at the steady state (+-15%)
    const double early = 200.0 * tEarly;
    const bool count = aliveEarly > early * 0.75 && aliveEarly < early * 1.25 && aliveSteady > 170 && aliveSteady < 230 && died > 50 &&
                       componentCount + 10 >= aliveSteady && componentCount <= aliveSteady + 10;
    const bool render = lumaDuring >= 0 && lumaAfter >= 0 && lumaDuring > lumaAfter + 3;
    const bool tiers = potatoAlive == 500 && dropped >= 2500 && ultraAlive == 3000;
    bdg::LogInfo("vfx test: count={} render={} tiers={} cleared={} (alive {} at {:.2f} s, {} at {:.2f} s, died {}, component {}, luma {:.1f} -> {:.1f} without, potato {} dropped {}, ultra {})",
                 count ? 1 : 0, render ? 1 : 0, tiers ? 1 : 0, cleared ? 1 : 0, aliveEarly, tEarly, aliveSteady, tSteady, died, componentCount,
                 lumaDuring, lumaAfter, potatoAlive, dropped, ultraAlive);
  }
}
BDG_EDITOR_TEST("BDG_VFX_TEST", InjectVfxTest, nullptr)


// ---- Terrain and foliage (round 6) ----
namespace edtest_terrain {
// Share of the Scene view's last picture that is sky (blue above red and green): cracks in the terrain show it.
double SkyShare(bdg_engine* e) {
  int32_t w = 0, h = 0;
  std::vector<uint8_t> px;
  e->sceneView->ReadPixels(w, h, px);
  if (px.size() < (size_t)w * h * 4 || w == 0) return -1;
  size_t sky = 0;
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    const uint8_t* p = &px[i * 4];
    sky += p[2] > p[0] + 8 && p[2] > p[1] + 4;
  }
  return sky / double((size_t)w * h);
}
// Grass plants (foliage layer 0) within `radius` of a terrain-local point. before: the same count from the terrain as it
// was when the test saved its plants list (frame 17), so both counts use the same point.
std::vector<bdg::FoliageInstance> g_before;
size_t PlantsNear(bdg::TerrainData* t, glm::vec2 at, float radius, size_t* before = nullptr) {
  if (!t) return 0;
  std::vector<bdg::FoliageInstance> all, chunk;
  for (int cz = 0; cz < bdg::TerrainChunksZ(*t); ++cz)
    for (int cx = 0; cx < bdg::TerrainChunksX(*t); ++cx) {
      bdg::ScatterFoliage(*t, 0, cx, cz, 1, chunk);
      all.insert(all.end(), chunk.begin(), chunk.end());
    }
  if (!before) g_before = all; // the first call keeps every plant
  auto count = [&](const std::vector<bdg::FoliageInstance>& v) {
    size_t n = 0;
    for (const bdg::FoliageInstance& f : v) n += glm::length(glm::vec2(f.position.x, f.position.z) - at) < radius;
    return n;
  };
  if (before) *before = count(g_before);
  return count(all);
}
// Average colour of the middle of the Scene view's last picture.
glm::vec3 MiddleColor(bdg_engine* e) {
  int32_t w = 0, h = 0;
  std::vector<uint8_t> px;
  e->sceneView->ReadPixels(w, h, px);
  glm::vec3 sum(0);
  int n = 0;
  for (int y = h * 2 / 5; y < h * 3 / 5; ++y)
    for (int x = w * 2 / 5; x < w * 3 / 5; ++x, ++n) sum += glm::vec3(px[((size_t)y * w + x) * 4], px[((size_t)y * w + x) * 4 + 1], px[((size_t)y * w + x) * 4 + 2]);
  return n ? sum / float(n) : glm::vec3(0);
}
} // namespace edtest_terrain

// Scripted check of round 6 terrain + foliage (BDG_TERRAIN_TEST=1, BDG_ASSETS = an empty assets folder,
// BDG_SCRIPT_ASSEMBLIES = the samples): a 128 x 128 m terrain (hills, 4 layers, grass and trees) is written to the
// assets and placed. Seen from above at an angle the Scene view draws its chunks at several LODs, with grass, trees
// and far tree cards; looking straight down no sky shows through (no cracks between LODs). The Terrain tool (T) raises
// the ground under a drag (one undo step: undo puts it back, redo again), paints a layer (the colour there changes)
// and erases grass; per tier the chunk triangles, plants and CPU ms are reported (potato: no grass). In play a C#
// TerrainProbe on a Character capsule falls onto the height field collider and stands where Terrain.HeightAt says.
// Run with --frames 200.
void InjectTerrainTest(bdg_engine* e, int frame, float scale) {
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->sceneView) return;
  using T = bdg::InputEvent::Type;
  static std::string probe, perTier;
  static int render = 0, cracks = 0, lods = 0, sculpt = 0, undo = 0, paint = 0, foliage = 0, tiers = 0, stand = 0;
  static float h0 = 0, h1 = 0;
  static glm::vec3 color0{0}, color1{0};
  static int grass0 = 0, playAt = 0;
  static size_t chunkGrass0 = 0, chunkGrass1 = 0, chunkGrass0Before = 0; // grass within 12 m of the paint stroke, from the data
  static double sky = -1;
  bdg::TerrainView& tv = e->sceneView->Terrains();
  bdg::Widget* sw = e->editor->Find("scene");
  const float cx = sw ? sw->x + sw->w * 0.5f : 0, cy = sw ? sw->y + sw->h * 0.5f : 0;
  auto ev = [&](T type, float x, float y) { e->window.InjectEvent({type, x * scale, y * scale, type == T::MouseMove ? 0 : 1}); };
  auto run = [&](const char* name, const char* target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("terrain test: {} {}: {}", name, target, err);
  };
  auto data = [&]() { return bdg::TerrainRegistry::Instance().Get("Terrain/Test.terrain", m->assets ? m->assets->Root() : std::string()); };
  if (frame == 2) {
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.rfind("terrain probe: ", 0) == 0) probe = l; });
    bdg::TerrainData t = bdg::MakeTerrain("Test", 129, {128, 128}, 24, 10, 11);
    t.foliage.push_back({"Grass", bdg::FoliageKind::Grass, 3, 0, 0.7f, 1.3f, 25, 35, 70, 35, {0.33f, 0.52f, 0.2f}});
    t.foliage.push_back({"Trees", bdg::FoliageKind::Tree, 0.004f, -1, 0.8f, 1.3f, 160, 200, 45, 30, {0.2f, 0.38f, 0.15f}});
    const char* assets = std::getenv("BDG_ASSETS");
    std::string err;
    if (!assets || !bdg::SaveTerrain(t, std::string(assets) + "/Terrain/Test.terrain", &err)) bdg::LogError("terrain test: cannot write the terrain: {}", err);
    run("add", "", R"({"id":"tr_ter","name":"Ground Terrain"})");
    run("set", "tr_ter", R"({"path":"Transform.position","value":[-64,0,-64]})");
    run("addcomp", "tr_ter", R"({"type":"Terrain","value":{"asset":"Terrain/Test.terrain"}})");
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("scene");
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
    e->sceneView->showGrid = false;
    e->sceneView->camera.pivot = {0, 6, 0};
    e->sceneView->camera.distance = 55;
    e->sceneView->camera.pitch = -28;
    e->sceneView->camera.yaw = 20;
  }
  if (frame == 6) { // drawn: several LODs, plants, far cards
    const bdg::TerrainView::Stats& s = tv.LastStats();
    int used = 0;
    for (int c : s.lodCount) used += c > 0;
    lods = used >= 2 ? 1 : 0;
    render = s.terrains == 1 && s.chunks > 0 && s.triangles > 0 && s.culled > 0 && e->sceneView->LastStats().triangles >= (uint64_t)s.triangles ? 1 : 0;
    foliage = s.grass > 500 && s.trees > 0 && s.impostors > 0 ? 1 : 0;
    perTier = std::format("medium: {} chunks {} tris, grass {} trees {} cards {}, {:.2f} ms cpu, bake {:.1f} ms", s.chunks, s.triangles, s.grass, s.trees,
                          s.impostors, s.ms, s.bakeMs);
    e->sceneView->camera.pitch = -89; // straight down from high up: every chunk LOD at once, no sky may show through
    e->sceneView->camera.distance = 120;
  }
  if (frame == 9) {
    sky = edtest_terrain::SkyShare(e);
    cracks = sky >= 0 && sky < 0.0005 && tv.LastStats().chunks >= 9 ? 1 : 0;
    e->sceneView->camera.pitch = -60;
    e->sceneView->camera.distance = 40;
    m->tool = "terrain";
    m->Select("tr_ter");
    run("set", "tr_ter", R"({"path":"Terrain.brushRadius","value":10})");
    run("set", "tr_ter", R"({"path":"Terrain.brushStrength","value":1})");
  }
  if (frame == 12) { // a drag across the middle raises it
    bdg::TerrainData* t = data();
    h0 = t ? t->HeightAt(64, 64) : 0;
    ev(T::MouseMove, cx, cy);
    ev(T::MouseDown, cx, cy);
    for (int k = 1; k <= 6; ++k) ev(T::MouseMove, cx + k * 2.f, cy);
    ev(T::MouseUp, cx + 12, cy);
  }
  if (frame == 15) {
    bdg::TerrainData* t = data();
    h1 = t ? t->HeightAt(64, 64) : 0;
    sculpt = h1 > h0 + 0.3f && e->sceneView->TerrainBrushTool().strokes == 1 ? 1 : 0;
    std::string err;
    const bool undone = m->bus.Undo(&err) && t && std::fabs(t->HeightAt(64, 64) - h0) < 1e-3f;
    const bool redone = m->bus.Redo(&err) && t && std::fabs(t->HeightAt(64, 64) - h1) < 1e-3f;
    undo = undone && redone ? 1 : 0;
    run("set", "tr_ter", R"({"path":"Terrain.brush","value":"Paint"})");
    run("set", "tr_ter", R"({"path":"Terrain.brushLayer","value":3})"); // sand
    run("set", "tr_ter", R"({"path":"Terrain.brushRadius","value":30})");
    e->sceneView->camera.pitch = -28; // near enough for the grass
    e->sceneView->camera.distance = 55;
  }
  if (frame == 17) {
    color0 = edtest_terrain::MiddleColor(e);
    grass0 = tv.LastStats().grass;
    chunkGrass0 = edtest_terrain::PlantsNear(data(), {64, 64}, 12);
    if (const char* shot = std::getenv("BDG_TERRAIN_SHOT")) { // a look at it (not part of the check)
      int32_t w = 0, h = 0;
      std::vector<uint8_t> px;
      e->sceneView->ReadPixels(w, h, px);
      bdg::WritePNG(shot, w, h, px.data(), true);
    }
  }
  if (frame == 18) {
    ev(T::MouseMove, cx, cy);
    ev(T::MouseDown, cx, cy);
    for (int k = 1; k <= 8; ++k) ev(T::MouseMove, cx + (k % 2) * 3.f, cy);
    ev(T::MouseUp, cx, cy);
  }
  if (frame == 22) { // the sand shows (the colour map was rebaked there), the grass that grows on layer 1 is gone there
    color1 = edtest_terrain::MiddleColor(e);
    bdg::TerrainData* t = data();
    paint = t && t->LayerWeight(3, 64, 64) > 0.8f && glm::length(color1 - color0) > 8 ? 1 : 0;
    const int grassAfter = tv.LastStats().grass;
    const glm::vec3 at = e->sceneView->TerrainBrushTool().LastPoint() + glm::vec3(64, 0, 64); // terrain-local
    chunkGrass0 = edtest_terrain::PlantsNear(t, {at.x, at.z}, 12, &chunkGrass0Before);
    chunkGrass1 = chunkGrass0;
    chunkGrass0 = chunkGrass0Before;
    foliage = foliage && grassAfter <= grass0 && chunkGrass1 * 4 < chunkGrass0 ? 1 : 0;
    m->SetSelection({});
    m->tool = "move";
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Potato);
  }
  static int tris[3] = {}, grass[3] = {};
  static double ms[3] = {};
  if (frame == 25) {
    const auto& s = tv.LastStats();
    tris[0] = s.triangles; grass[0] = s.grass; ms[0] = s.ms;
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
  }
  if (frame == 28) {
    const auto& s = tv.LastStats();
    tris[1] = s.triangles; grass[1] = s.grass; ms[1] = s.ms;
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Ultra);
  }
  if (frame == 31) {
    const auto& s = tv.LastStats();
    tris[2] = s.triangles; grass[2] = s.grass; ms[2] = s.ms;
    tiers = grass[0] == 0 && grass[1] > 0 && grass[2] > grass[1] && tris[0] < tris[1] && tris[1] < tris[2] ? 1 : 0;
    perTier += std::format("; potato {} tris {} grass {:.2f} ms; medium {} / {} / {:.2f}; ultra {} / {} / {:.2f}", tris[0], grass[0], ms[0], tris[1], grass[1],
                           ms[1], tris[2], grass[2], ms[2]);
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
    // a walker over the terrain, then Play: it falls onto the height field collider
    run("add", "", R"({"id":"tr_walk","name":"Walker"})");
    run("set", "tr_walk", R"({"path":"Transform.position","value":[10,16,-6]})");
    run("addcomp", "tr_walk", R"({"type":"Rigidbody","value":{"type":"Character"}})");
    run("addcomp", "tr_walk", R"({"type":"Collider","value":{"shape":"Capsule","radius":0.4,"height":2}})");
    run("addcomp", "tr_walk", R"({"type":"TerrainProbe"})");
  }
  if (frame == 33) {
    TogglePlay(e);
    playAt = frame;
  }
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  if (playAt && e->play.Playing() && (!probe.empty() || frame > playAt + 150)) {
    TogglePlay(e);
    stand = probe.find("stand=1 normal=1 missing=1") != std::string::npos ? 1 : 0;
  }
  if (playAt && frame == 198) {
    bdg::SetLogHook({});
    bdg::LogInfo("terrain test: render={} lods={} cracks={} sculpt={} undo={} paint={} foliage={} tiers={} stand={} (sky {:.4f}, height {:.2f} -> {:.2f}, colour {:.0f},{:.0f},{:.0f} -> {:.0f},{:.0f},{:.0f}, grass {} -> {} (painted chunk {} -> {}); {}; \"{}\")",
                 render, lods, cracks, sculpt, undo, paint, foliage, tiers, stand, sky, h0, h1, color0.r, color0.g, color0.b, color1.r, color1.g, color1.b,
                 grass0, tv.LastStats().grass, chunkGrass0, chunkGrass1, perTier, probe);
  }
}
BDG_EDITOR_TEST("BDG_TERRAIN_TEST", InjectTerrainTest, nullptr)


// Scripted check of round 6 foliage (BDG_FOLIAGE_TEST=1, BDG_ASSETS = an empty assets folder): a gentle 128 m
// terrain with dense grass and trees, seen from 1.7 m above the ground. Plants are drawn (grass clumps, trees at
// LOD 0, LOD 1 and as far cards); the wind moves them (two wind times give different pictures) and with the
// Terrain's wind at 0 the same two times give the same picture; from high above every plant has faded out and the
// trees left are cards; facing away from the terrain its chunks and plants are culled; the low tier keeps the grass
// under its budget. Run with --frames 40.
void InjectFoliageTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->sceneView) return;
  static int plants = 0, lods = 0, wind = 0, calm = 0, fade = 0, culled = 0, budget = 0;
  static std::vector<uint8_t> a, b;
  static double windDiff = 0, calmDiff = 0;
  static std::string info;
  bdg::TerrainView& tv = e->sceneView->Terrains();
  auto run = [&](const char* name, const char* target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("foliage test: {} {}: {}", name, target, err);
  };
  auto shot = [&](std::vector<uint8_t>& px) {
    int32_t w = 0, h = 0;
    e->sceneView->ReadPixels(w, h, px);
  };
  auto differ = [](const std::vector<uint8_t>& x, const std::vector<uint8_t>& y) {
    if (x.size() != y.size() || x.empty()) return 1.0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < x.size(); i += 4) n += std::abs(x[i] - y[i]) + std::abs(x[i + 1] - y[i + 1]) + std::abs(x[i + 2] - y[i + 2]) > 6;
    return n / double(x.size() / 4);
  };
  auto view = [&](glm::vec3 pivot, float yaw, float pitch, float distance) {
    e->sceneView->camera.pivot = pivot;
    e->sceneView->camera.yaw = yaw;
    e->sceneView->camera.pitch = pitch;
    e->sceneView->camera.distance = distance;
  };
  if (frame == 2) {
    bdg::TerrainData t = bdg::MakeTerrain("Meadow", 129, {128, 128}, 10, 3, 4);
    t.foliage.push_back({"Grass", bdg::FoliageKind::Grass, 6, -1, 0.8f, 1.4f, 20, 30, 70, 40, {0.36f, 0.58f, 0.22f}});
    t.foliage.push_back({"Trees", bdg::FoliageKind::Tree, 0.02f, -1, 0.9f, 1.3f, 120, 160, 50, 40, {0.2f, 0.38f, 0.15f}});
    for (size_t i = 0; i < t.heights.size(); ++i) t.splat[0][i * 4] = 0, t.splat[0][i * 4 + 2] = 255; // dirt: the grass stands out
    const char* assets = std::getenv("BDG_ASSETS");
    std::string err;
    if (!assets || !bdg::SaveTerrain(t, std::string(assets) + "/Terrain/Meadow.terrain", &err)) bdg::LogError("foliage test: {}", err);
    run("add", "", R"({"id":"fo_ter","name":"Meadow"})");
    run("set", "fo_ter", R"({"path":"Transform.position","value":[-64,0,-64]})");
    run("addcomp", "fo_ter", R"({"type":"Terrain","value":{"asset":"Terrain/Meadow.terrain"}})");
    if (bdg::Widget* dw = e->editor->Find("dock"); dw && dw->dock) dw->dock->SelectPanel("scene");
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
    e->sceneView->showGrid = false;
    bdg::TerrainData* td = bdg::TerrainRegistry::Instance().Get("Terrain/Meadow.terrain");
    const float ground = td ? td->HeightAt(64, 84) : 0;
    view({0, ground + 1.7f, 20}, 0, -8, 0.01f); // standing in the meadow, looking along -Z
    tv.windTime = 0;
  }
  if (frame == 6) {
    const auto& s = tv.LastStats();
    plants = s.grass > 2000 && s.trees > 20 ? 1 : 0;
    lods = s.trees - s.treesLod1 - s.impostors > 0 && s.treesLod1 > 0 && s.impostors > 0 ? 1 : 0;
    info = std::format("grass {} trees {} (lod1 {}, cards {}), {} chunks, {:.2f} ms", s.grass, s.trees, s.treesLod1, s.impostors, s.chunks, s.ms);
    shot(a);
    tv.windTime = 0.8;
  }
  if (frame == 8) {
    shot(b);
    windDiff = differ(a, b);
    wind = windDiff > 0.005 ? 1 : 0;
    run("set", "fo_ter", R"({"path":"Terrain.wind","value":0})");
    tv.windTime = 0;
  }
  if (frame == 10) {
    shot(a);
    tv.windTime = 0.8;
  }
  if (frame == 12) {
    shot(b);
    calmDiff = differ(a, b);
    calm = calmDiff < 0.0005 ? 1 : 0;
    view({0, 0, 0}, 0, -89, 140); // high above: the grass has faded, the trees left are cards
  }
  if (frame == 15) {
    const auto& s = tv.LastStats();
    fade = s.grass == 0 && s.impostors > 0 && s.trees == s.impostors ? 1 : 0;
    info += std::format("; from 140 m: grass {} trees {} cards {}", s.grass, s.trees, s.impostors);
    view({0, 3, -70}, 0, 0, 0.01f); // past the -Z edge, facing away (-Z; the terrain is behind): nothing of it in view
  }
  if (frame == 18) {
    const auto& s = tv.LastStats();
    culled = s.culled > 0 && s.chunks <= 4 && s.grass == 0 ? 1 : 0;
    info += std::format("; facing away: {} chunks drawn, {} culled, grass {}", s.chunks, s.culled, s.grass);
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Low);
    run("set", "fo_ter", R"({"path":"Terrain.wind","value":1})");
    bdg::TerrainData* td = bdg::TerrainRegistry::Instance().Get("Terrain/Meadow.terrain");
    view({0, (td ? td->HeightAt(64, 84) : 0) + 1.7f, 20}, 0, -8, 0.01f);
  }
  if (frame == 21) {
    const auto& s = tv.LastStats();
    budget = s.grass > 0 && s.grass <= bdg::TerrainTier(bdg::QualityTier::Low).grassBudget ? 1 : 0;
    info += std::format("; low tier: grass {} ({:.2f} ms)", s.grass, s.ms);
    e->sceneView->quality = bdg::QualityPreset(bdg::QualityTier::Medium);
    tv.windTime = -1;
  }
  if (frame == 23)
    bdg::LogInfo("foliage test: plants={} lods={} wind={} calm={} fade={} culled={} budget={} (wind {:.4f}, calm {:.5f}; {})", plants, lods, wind, calm,
                 fade, culled, budget, windDiff, calmDiff, info);
}
BDG_EDITOR_TEST("BDG_FOLIAGE_TEST", InjectFoliageTest, nullptr)


// ---- Multiplayer (round 7) ----
// Scripted check of round 7 (BDG_NET_TEST=1, BDG_SCRIPT_ASSEMBLIES = the samples, BDG_NET_DIR = an empty folder for a
// small project): the editor plays as a Listen Server through the Play options (2 players, 10 % loss): one client
// process joins, a second one joins late; the C# NetProbe checks spawns, the owner's predicted moves reaching the
// server, a [Replicated] field with OnRep on the clients, [Server] / [Client] / [Multicast] calls, the late joiner
// getting the world, and the first client's leaving cleaning up its player everywhere. Every stage waits for what it
// needs (a join, a move, the late joiner's world, the goodbye) with a generous time limit, so slow machines (software
// OpenGL: clients take seconds to start) pass the same way; client 1 leaves when the check writes its --quit-file.
// The editor quits when done (at most 400 s). Run with --frames 100000: a ceiling that is never what ends it.
void InjectNetTest(bdg_engine* e, int frame, float) {
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  namespace fs = std::filesystem;
  using clock = std::chrono::steady_clock;
  static std::vector<std::string> lines; // the editor's (server's) own log
  static clock::time_point started, joined1, launched2, joined2, leaveAt;
  static std::string quitFile, c2log;
  static int stage = 0;
  static float p1x0 = 0, p1x1 = 0;
  static std::string dir, p1;
  bdg::PlaySession& ps = bdg::PlaySession::Instance();
  auto seconds = [](clock::time_point t) { return std::chrono::duration<double>(clock::now() - t).count(); };
  auto byName = [&](const char* name) -> std::string {
    std::vector<std::string> stack(m->scene.Roots().begin(), m->scene.Roots().end());
    while (!stack.empty()) {
      const bdg::SceneObject* o = m->scene.Find(stack.back());
      stack.pop_back();
      if (!o) continue;
      if (o->name == name) return o->id;
      stack.insert(stack.end(), o->children.begin(), o->children.end());
    }
    return {};
  };
  auto x = [&](const std::string& id) {
    const bdg::SceneObject* o = m->scene.Find(id);
    const bdg::Component* c = o ? const_cast<bdg::SceneObject*>(o)->Find("Transform") : nullptr;
    return c ? static_cast<const bdg::Transform*>(c->data)->position.x : -999.f;
  };
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (frame == 2) {
    bdg::SetLogHook([](bdg::LogLevel, std::string_view l) { if (l.find("net probe: ") != std::string_view::npos || l.rfind("net: ", 0) == 0) lines.emplace_back(l); });
    const char* d = std::getenv("BDG_NET_DIR");
    dir = d ? d : (fs::temp_directory_path() / "bdg-net-test").string();
    std::error_code ec;
    fs::create_directories(fs::path(dir) / "ProjectSettings", ec);
    fs::create_directories(fs::path(dir) / "Scenes", ec);
    std::ofstream(fs::path(dir) / "ProjectSettings" / "Project.json", std::ios::binary)
        << R"({"description":{"name":"Net Test"},"maps":{"gameStartScene":"Scenes/Main.scene"},"display":{"width":320,"height":240}})";
    std::ofstream(fs::path(dir) / "Scenes" / "Main.scene", std::ios::binary) << R"({"v":1,"scene":"Main"}
{"id":"rules","name":"Rules","GameMode":{"playerPrefab":"PlayerProto"}}
{"id":"start1","name":"PlayerStart A","pos":[0,0,0],"PlayerStart":{"order":0}}
{"id":"start2","name":"PlayerStart B","pos":[4,0,0],"PlayerStart":{"order":1}}
{"id":"start3","name":"PlayerStart C","pos":[8,0,0],"PlayerStart":{"order":2}}
{"id":"proto","name":"PlayerProto","pos":[0,-50,0],"NetworkObject":{},"NetworkTransform":{"maxSpeed":8},"NetProbe":{"role":"player"}}
{"id":"board","name":"Board","NetworkObject":{},"NetProbe":{"role":"board"}}
{"id":"cam","name":"Camera","pos":[4,3,10],"Camera":{}}
)";
    if (!m->OpenScene((fs::path(dir) / "Scenes" / "Main.scene").string())) bdg::LogError("net test: cannot open the scene");
    ps.options = {};
    ps.options.mode = bdg::NetMode::ListenServer;
    ps.options.players = 2;
    ps.options.port = 0;
    ps.options.lossPercent = 10;
    // a starved frame on software GL (three processes on a small CI machine) must not drop peers: 30 s, not 5 s
    ps.options.timeoutMs = std::getenv("BDG_NET_TEST_TIMEOUT") ? (uint32_t)std::atoi(std::getenv("BDG_NET_TEST_TIMEOUT")) : 30000u; // ms
    ps.options.projectDir = dir;
    quitFile = (fs::path(dir) / ".cache" / "net" / "leave-client1").string();
    fs::remove(fs::path(quitFile), ec);
    ps.options.extraArgs = {"--quit-file", quitFile}; // the first client leaves when told (a polite goodbye)
  }
  if (frame == 3) {
    TogglePlay(e); // the editor hosts; the Play options start "Client 1"
    started = clock::now();
    stage = 1;
  }
  auto client2Has = [&](const char* s) { // read again every 10 frames (small file)
    if (frame % 10 == 0) bdg::ReadTextU8((fs::path(dir) / ".cache" / "net" / "Client 2.log").string(), c2log);
    return c2log.find(s) != std::string::npos;
  };
  if (stage == 1 && (byName("Player 1").size() || seconds(started) > 120)) { // client 1 is in
    p1 = byName("Player 1");
    p1x0 = x(p1);
    joined1 = clock::now();
    stage = 2;
  }
  if (stage == 2 && seconds(joined1) > 1 && (x(p1) > p1x0 + 1.5f || seconds(joined1) > 30)) { // its moves arrive; now a late joiner
    p1x1 = x(p1);
    ps.options.extraArgs.clear(); // client 2 stays until Stop
    ps.LaunchClient("Client 2");
    launched2 = clock::now();
    stage = 3;
  }
  if (stage == 3 && ((byName("Player 2").size() && client2Has("net probe: start player Player 1")) || seconds(launched2) > 120)) {
    joined2 = clock::now(); // client 2 is in and has the world: client 1 may go
    if (const char* stall = std::getenv("BDG_NET_TEST_STALL")) // a starved frame (software GL on a busy CI machine), seconds
      std::this_thread::sleep_for(std::chrono::milliseconds((int)(std::atof(stall) * 1000)));
    std::ofstream(fs::path(quitFile), std::ios::binary) << "leave";
    leaveAt = clock::now();
    stage = 4;
  }
  auto onreps2 = [&] { // [Replicated] updates the late joiner has seen
    int n = 0;
    for (size_t at = c2log.find("net probe: onrep score="); at != std::string::npos; at = c2log.find("net probe: onrep score=", at + 1)) ++n;
    return n;
  };
  if (stage == 4 && ((!ps.Running(0) && !m->scene.Find(p1) && client2Has("net probe: gone Player 1") && onreps2() >= 2) || seconds(leaveAt) > 90)) {
    stage = 5; // client 1 has left, the server and client 2 cleaned up
  }
  if (stage >= 1 && stage < 5 && seconds(started) > 400) { // the overall wall-clock limit (the stages have their own)
    bdg::LogError("net test: stage {} still waiting after {:.0f} s", stage, seconds(started));
    stage = 5;
  }
  if (stage == 5) {
    const bool p1Gone = !m->scene.Find(p1);
    TogglePlay(e); // closes Client 2
    bdg::SetLogHook({});
    std::string c1, c2;
    bdg::ReadTextU8((fs::path(dir) / ".cache" / "net" / "Client 1.log").string(), c1);
    bdg::ReadTextU8((fs::path(dir) / ".cache" / "net" / "Client 2.log").string(), c2);
    std::error_code rmErr;
    fs::remove(fs::path(quitFile), rmErr);
    auto has = [&](const std::string& s) { return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(s) != std::string::npos; }); };
    auto count = [](const std::string& text, const std::string& s) {
      int n = 0;
      for (size_t at = text.find(s); at != std::string::npos; at = text.find(s, at + 1)) ++n;
      return n;
    };
    const bool spawn = !p1.empty() && c1.find("net: joined as player 1") != std::string::npos && c1.find("net probe: start player Player 1 server=False client=True owner=True") != std::string::npos;
    const bool sync = p1x1 > p1x0 + 1.0f; // the owner's predicted moves reached the server
    const bool onrep = count(c1, "net probe: onrep score=") >= 3 && count(c2, "net probe: onrep score=") >= 2;
    const bool rpc = has("net probe: fire 7 on Player 1") && c1.find("net probe: hit 14 on Player 1 owner=True") != std::string::npos &&
                     c1.find("net probe: boom on Player 1") != std::string::npos && has("net probe: boom on Player 1");
    const bool late = c2.find("net: joined as player 2") != std::string::npos && c2.find("net probe: start player Player 1 server=False client=True owner=False") != std::string::npos;
    const bool left = p1Gone && has("net probe: left 1") && c2.find("net probe: gone Player 1") != std::string::npos;
    bdg::LogInfo("net test: spawn={} sync={} onrep={} rpc={} late={} left={} (player 1 x {:.2f} -> {:.2f}, onrep {} / {}, stage times {:.1f} s)", spawn ? 1 : 0,
                 sync ? 1 : 0, onrep ? 1 : 0, rpc ? 1 : 0, late ? 1 : 0, left ? 1 : 0, p1x0, p1x1, count(c1, "onrep"), count(c2, "onrep"), seconds(started));
    ps.options = {};
    stage = 6;
    e->quitNow = true; // done: no need to wait for --frames
  }
}
BDG_EDITOR_TEST("BDG_NET_TEST", InjectNetTest, nullptr)

// ---- AI-4 live link for `bdg mcp` (Tools/LiveLink.h): always on in a normal editor session (not in automated or
// hidden runs, so `bdg shot` / `bdg check` editors never take over .cache/editor.json from the one the user has open).
// Requests run here on the main thread: the open scene, its command bus (one undo step each), nothing saved.
#include "Tools/Cli.h"
#include "Tools/LiveLink.h"
#include <SDL3/SDL_events.h>

bdg::live::Server& LiveLinkServer() {
  static bdg::live::Server server;
  return server;
}
void TickLiveLink(bdg_engine* e, int, float) {
  static bool tried = false;
  bdg::live::Server& server = LiveLinkServer();
  if (!tried && e->model) {
    tried = true;
    if (!std::getenv("BDG_MCP_LIVE_TEST") && (e->prefsPath.empty() || std::getenv("BDG_HIDDEN"))) return;
    server.wake = [] { // any SDL event wakes the editor's event loop (safe even after the window is gone)
      SDL_Event ev{};
      ev.type = SDL_EVENT_USER;
      SDL_PushEvent(&ev);
    };
    std::string err;
    if (server.Start(std::filesystem::current_path(), &err)) bdg::LogInfo("live link: 127.0.0.1:{} (.cache/editor.json, bdg mcp)", server.Port());
    else bdg::LogWarn("live link: {}", err);
  }
  if (!server.Running() || !e->model) return;
  server.Serve([e](const std::vector<std::string>& cli) {
    if (e->play.Playing())
      return std::string(R"({"ok":false,"exit":1,"command":"live","output":[],"error":{"code":"playing","message":"에디터가 Play 중: Stop한 뒤 다시"}})");
    std::string out, err;
    bdg::EditorModel& m = *e->model;
    bdg::RunCliLive(cli, m.scene, m.bus, m.sceneName, m.scenePath, out, err, std::filesystem::current_path());
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
  });
}
BDG_EDITOR_SERVICE(TickLiveLink)

// Scripted check of the live link (BDG_MCP_LIVE_TEST=1, --frames 90): a client thread does what `bdg mcp` does —
// reads .cache/editor.json, renames the first object (set), a dry run, a view — while the editor keeps drawing; then
// the rename is in the open scene, one Ctrl+Z restores it, the dry run changed nothing, the view saw the new name.
void InjectMcpLiveTest(bdg_engine* e, int frame, float) {
  static std::thread client;
  static std::atomic<bool> done{false};
  static std::string answers[3];
  static std::string id, oldName;
  static bool checked = false;
  bdg::EditorModel* m = e->model.get();
  if (!m || checked) return;
  if (frame == 3 && !client.joinable()) {
    if (m->scene.Roots().empty()) { bdg::LogError("mcp live test: empty scene"); checked = true; return; }
    id = m->scene.Roots()[0];
    oldName = m->scene.Find(id)->name;
    client = std::thread([] {
      const std::filesystem::path root = std::filesystem::current_path();
      answers[0] = bdg::live::Forward(root, std::format(R"({{"cli":["set","{}","name=LiveRenamed"]}})", id));
      answers[1] = bdg::live::Forward(root, std::format(R"({{"cli":["set","{}","name=DryOnly","--dry-run"]}})", id));
      answers[2] = bdg::live::Forward(root, R"({"cli":["view"]})");
      done = true;
    });
  }
  if (!done) return;
  client.join();
  checked = true;
  const bdg::SceneObject* o = m->scene.Find(id);
  std::error_code ec;
  const bool file = std::filesystem::exists(std::filesystem::current_path() / ".cache" / "editor.json", ec);
  const bool set = answers[0].find(R"("ok":true)") != std::string::npos && o && o->name == "LiveRenamed" && m->Dirty();
  const bool dry = answers[1].find(R"("ok":true)") != std::string::npos && answers[1].find("DryOnly") != std::string::npos && o && o->name == "LiveRenamed";
  const bool view = answers[2].find("LiveRenamed") != std::string::npos;
  std::string err;
  const bool undo = m->bus.Undo(&err) && m->scene.Find(id) && m->scene.Find(id)->name == oldName;
  if (!set || !dry || !view) bdg::LogError("mcp live test answers: {} | {} | {}", answers[0], answers[1], answers[2].substr(0, 300));
  bdg::LogInfo("mcp live test: file={} set={} dry={} view={} undo={}", file ? 1 : 0, set ? 1 : 0, dry ? 1 : 0, view ? 1 : 0, undo ? 1 : 0);
  e->quitNow = true;
}
BDG_EDITOR_TEST("BDG_MCP_LIVE_TEST", InjectMcpLiveTest, nullptr)

// Scripted check of UI review B1 (BDG_CREATE_TEST=1, --frames 40): making objects and adding components with real
// clicks. The Hierarchy "+" opens the make menu, "큐브" adds a Cube (selected, one undo step); a right-click on its row
// -> "자식 만들기…" -> "점광" adds a Point Light under it; the Inspector's "+ Add Component" opens the list, typing
// "rigid" narrows it and Enter adds a Rigidbody; a right-click on the Hierarchy's empty space -> "UI 텍스트" adds a
// Text under a new Canvas (one undo step takes both away); Ctrl+Shift+N adds an empty GameObject at the top level.
namespace createtest {
std::string plus = "-", child = "-", addcomp = "-", uiText = "-", shortcut = "-", undo = "-", hints = "-";
std::string cube, light;
size_t roots0 = 0;
}
void InjectCreateTest(bdg_engine* e, int frame, float scale) {
  using namespace createtest;
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  bdg::UIContext& ui = *e->editor;
  auto at = [&](T type, float x, float y, int button) { e->window.InjectEvent({type, x * scale, y * scale, button}); };
  auto clickAt = [&](float x, float y, int button = 1) { at(T::MouseMove, x, y, 0); at(T::MouseDown, x, y, button); at(T::MouseUp, x, y, button); };
  auto clickWidget = [&](const std::string& id, int button = 1) {
    bdg::Widget* w = ui.Find(id);
    if (!w || w->hidden || w->w <= 0) { bdg::LogError("create test: {} is not on screen", id); return; }
    clickAt(w->x + w->w * 0.5f, w->y + w->h * 0.5f, button);
  };
  auto clickItem = [&](const std::string& label) { // a real click on the open menu's item
    const auto& items = ui.Menu();
    for (size_t i = 0; i < items.size(); ++i)
      if (items[i].label == label) {
        float x, y, w, h;
        if (ui.MenuItemRect(i, x, y, w, h)) { clickAt(x + w * 0.5f, y + h * 0.5f); return; }
      }
    bdg::LogError("create test: no menu item {} (menu open: {})", label, ui.MenuOpen() ? 1 : 0);
  };
  auto byName = [&](const std::string& name, const std::string& parent) {
    const std::vector<std::string>& ids = parent.empty() ? m->scene.Roots() : m->scene.Find(parent)->children;
    for (const std::string& id : ids)
      if (m->scene.Find(id)->name == name) return id;
    return std::string();
  };
  if (frame == 3) { roots0 = m->scene.Roots().size(); m->SetSelection({}); }
  if (frame == 5) clickWidget("h.create");
  if (frame == 7) {
    if (!ui.MenuOpen()) bdg::LogError("create test: + did not open a menu");
    clickItem("큐브");
  }
  if (frame == 9) {
    cube = m->Selected();
    bdg::SceneObject* o = m->scene.Find(cube);
    bdg::Component* c = o ? o->Find("MeshRenderer") : nullptr;
    std::string mesh;
    if (c) bdg::Registry::Get(c->data, *c->type, "mesh", mesh);
    plus = o && o->name == "Cube" && mesh == "\"Cube\"" && m->scene.Roots().size() == roots0 + 1 ? "1" : "0(" + mesh + ")";
    clickWidget("h." + cube, 3); // right-click: the row's menu
  }
  if (frame == 11) {
    bool dup = false, del = false; // the menu shows the keys next to the actions
    for (const auto& it : ui.Menu()) { dup = dup || (it.label == "복제" && it.keys == "Ctrl+D"); del = del || (it.label == "삭제" && it.keys == "Delete"); }
    hints = dup && del ? "1" : "0";
    clickItem("자식 만들기…");
  }
  if (frame == 13) clickItem("점광");
  if (frame == 15) {
    light = byName("Point Light", cube);
    bdg::SceneObject* o = m->scene.Find(light);
    child = o && o->Find("Light") && m->Selected() == light ? "1" : "0";
    clickWidget("insp.add");
  }
  if (frame == 17) {
    bdg::InputEvent t{T::Text};
    t.text = "rigid";
    e->window.InjectEvent(t);
  }
  if (frame == 19) {
    bdg::InputEvent k{T::KeyDown};
    k.key = bdg::Key::Enter;
    e->window.InjectEvent(k);
  }
  if (frame == 21) {
    bdg::SceneObject* o = m->scene.Find(light);
    addcomp = o && o->Find("Rigidbody") && !ui.MenuOpen() ? "1" : "0";
    if (bdg::Widget* h = ui.Find("hierarchy")) clickAt(h->x + h->w * 0.5f, h->y + h->h - 20, 3); // the empty space below the rows
  }
  if (frame == 23) clickItem("UI 텍스트");
  if (frame == 25) {
    const std::string text = m->Selected();
    bdg::SceneObject* o = m->scene.Find(text);
    bdg::SceneObject* p = o && !o->parent.empty() ? m->scene.Find(o->parent) : nullptr;
    uiText = o && o->Find("UIElement") && p && p->Find("UICanvas") ? "1" : "0";
    bdg::InputEvent k{T::KeyDown}; // Ctrl+Shift+N: an empty object at the top level
    k.key = bdg::Key::N;
    k.ctrl = k.shift = true;
    e->window.InjectEvent(k);
  }
  if (frame == 27) {
    bdg::SceneObject* o = m->scene.Find(m->Selected());
    shortcut = o && o->name == "GameObject" && o->parent.empty() && o->components.size() == 1 ? "1" : "0";
    std::string err; // undo: the GameObject, then the Text and its new Canvas together, then the Rigidbody
    const size_t roots = m->scene.Roots().size();
    bool ok = m->bus.Undo(&err) && m->scene.Roots().size() == roots - 1;
    ok = ok && m->bus.Undo(&err) && m->scene.Roots().size() == roots - 2 && byName("Canvas", "").empty();
    ok = ok && m->bus.Undo(&err) && m->scene.Find(light) && !m->scene.Find(light)->Find("Rigidbody");
    undo = ok ? "1" : "0(" + err + ")";
    bdg::LogInfo("create test: plus={} child={} hints={} addcomp={} ui={} shortcut={} undo={}", plus, child, hints, addcomp, uiText, shortcut, undo);
    e->quitNow = true;
  }
}
BDG_EDITOR_TEST("BDG_CREATE_TEST", InjectCreateTest, nullptr)

// Scripted check of UI review B12 / U12 (BDG_LANG_TEST=1, --frames 30): engine terms stay English, the rest is Korean.
// The Light's Intensity field and the Light card keep their English names; a real hover shows the Korean name as the
// tooltip's first line (design-system/project/editor.ko.json); no button in the Inspector, the Project bar or the
// Project Settings head mixes Korean with English other than an engine term (a component name); "+ 컴포넌트 추가"
// opens the list, typing the Korean name "강체" finds Rigidbody and Enter adds it.
#include "UI/EditorNames.h"
namespace langtest {
std::string names = "-", tooltip = "-", card = "-", mixed = "-", search = "-", bad;
}
void InjectLangTest(bdg_engine* e, int frame, float scale) {
  using namespace langtest;
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  bdg::UIContext& ui = *e->editor;
  auto center = [&](bdg::Widget* w, float& x, float& y) {
    if (!w || w->hidden || w->w <= 0) return false;
    x = (w->x + w->w * 0.5f) * scale;
    y = (w->y + w->h * 0.5f) * scale;
    return true;
  };
  const std::string light = [&] { // the sample's first Light
    for (const std::string& r : m->scene.Roots())
      if (bdg::SceneObject* o = m->scene.Find(r); o && o->Find("Light")) return r;
    return std::string();
  }();
  if (frame == 2) m->Select(light);
  if (frame == 5) {
    bdg::Widget* f = ui.Find("f.Light.intensity");
    bdg::Widget* c = ui.Find("c.Light");
    names = f && f->text == "Intensity" && c && c->text == "Light" ? "1" : "0";
    float x, y;
    if (center(f, x, y)) e->window.InjectEvent({T::MouseMove, x, y}); // a real hover
  }
  if (frame == 8) {
    bdg::Widget* f = ui.Find("f.Light.intensity");
    const std::string first = f ? f->tooltip.substr(0, f->tooltip.find('\n')) : std::string(); // the tooltip's first line
    tooltip = f && f->hovered && first == "강도" ? "1" : std::format("0(hovered {} '{}')", f && f->hovered ? 1 : 0, first);
    bdg::Widget* c = ui.Find("c.Light");
    card = c && c->tooltip == "라이트" ? "1" : "0";
    // buttons: Korean, or English engine terms, never both in one label (a component name may sit in a Korean label)
    std::vector<std::string> terms;
    for (const bdg::TypeInfo* t : bdg::Registry::Instance().Types()) terms.push_back(t->name);
    auto hangul = [](const std::string& s) { // U+AC00..U+D7A3 in UTF-8: EA B0 80 .. ED 9E A3
      for (size_t i = 0; i + 2 < s.size(); ++i)
        if ((unsigned char)s[i] >= 0xEA && (unsigned char)s[i] <= 0xED && ((unsigned char)s[i + 1] & 0xC0) == 0x80) return true;
      return false;
    };
    auto latinWords = [&](std::string s) { // English words left after removing engine terms
      for (const std::string& t : terms)
        for (size_t at = s.find(t); at != std::string::npos; at = s.find(t)) s.erase(at, t.size());
      int letters = 0;
      for (char ch : s) letters += std::isalpha((unsigned char)ch) ? 1 : 0;
      return letters >= 2;
    };
    std::function<void(bdg::Widget&)> walk = [&](bdg::Widget& w) {
      if (w.hidden) return;
      if (w.kind == bdg::WidgetKind::Button && hangul(w.text) && latinWords(w.text)) bad += (bad.empty() ? "" : " | ") + w.text;
      for (auto& ch : w.children) walk(*ch);
    };
    for (const char* panel : {"inspector", "project", "settings"})
      if (bdg::Widget* p = ui.Find(panel)) walk(*p);
    mixed = bad.empty() ? "1" : "0(" + bad + ")";
    float x, y;
    if (center(ui.Find("insp.add"), x, y)) {
      e->window.InjectEvent({T::MouseMove, x, y});
      e->window.InjectEvent({T::MouseDown, x, y, 1});
      e->window.InjectEvent({T::MouseUp, x, y, 1});
    }
  }
  if (frame == 10) {
    bdg::InputEvent t{T::Text};
    t.text = "강체";
    e->window.InjectEvent(t);
  }
  if (frame == 12) {
    const auto& items = ui.Menu();
    const bool first = !items.empty() && items[0].label == "Rigidbody";
    bdg::InputEvent k{T::KeyDown};
    k.key = bdg::Key::Enter;
    e->window.InjectEvent(k);
    search = first ? "1" : "0(" + (items.empty() ? std::string("no menu") : items[0].label) + ")";
  }
  if (frame == 14) {
    bdg::SceneObject* o = m->scene.Find(light);
    if (search == "1") search = o && o->Find("Rigidbody") ? "1" : "0(not added)";
    bdg::LogInfo("lang test: names={} tooltip={} card={} mixed={} search={}", names, tooltip, card, mixed, search);
    e->quitNow = true;
  }
}
BDG_EDITOR_TEST("BDG_LANG_TEST", InjectLangTest, nullptr)

// Scripted check of UI review B8 (BDG_SEARCH_TEST=1 with BDG_ASSETS = a copy of Assets/Icons in an Icons folder, --frames
// 60): real clicks and typing in the four search boxes. Hierarchy: "main cam" leaves Main Camera, "t:Light" only the
// lights, empty brings the tree back. Inspector (Sun): the Korean "강도" leaves Light's Intensity. Project (at the
// Assets root): "light" finds the icons in Icons/ (every folder), "t:texture camera" narrows to comp-camera.png; the
// ⋯ menu switches tiles / list. Project Settings: "중력" opens the physics card on its gravity rows, hides the rest.
namespace searchtest {
std::string hier = "-", types = "-", back = "-", insp = "-", proj = "-", kind = "-", more = "-", settings = "-";
bool tiles0 = false;
}
void InjectSearchTest(bdg_engine* e, int frame, float scale) {
  using namespace searchtest;
  using T = bdg::InputEvent::Type;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->editor) return;
  bdg::UIContext& ui = *e->editor;
  auto clickAt = [&](float x, float y) {
    e->window.InjectEvent({T::MouseMove, x * scale, y * scale});
    e->window.InjectEvent({T::MouseDown, x * scale, y * scale, 1});
    e->window.InjectEvent({T::MouseUp, x * scale, y * scale, 1});
  };
  auto click = [&](const char* id) {
    bdg::Widget* w = ui.Find(id);
    if (!w || w->hidden || w->w <= 0) { bdg::LogError("search test: {} is not on screen", id); return; }
    clickAt(w->x + w->w * 0.5f, w->y + w->h * 0.5f);
  };
  auto type = [&](const std::string& text) { // select all, then type (an empty text clears)
    bdg::InputEvent a{T::KeyDown};
    a.key = bdg::Key::A;
    a.ctrl = true;
    e->window.InjectEvent(a);
    if (text.empty()) {
      bdg::InputEvent b{T::KeyDown};
      b.key = bdg::Key::Backspace;
      e->window.InjectEvent(b);
    } else {
      bdg::InputEvent t{T::Text};
      t.text = text;
      e->window.InjectEvent(t);
    }
  };
  auto rows = [&] { // the object ids the Hierarchy lists
    std::vector<std::string> ids;
    if (bdg::Widget* h = ui.Find("hierarchy"))
      for (auto& c : h->children)
        if (c->kind == bdg::WidgetKind::ListRow && c->id.rfind("h.", 0) == 0 && c->id != "h.scene") ids.push_back(c->id.substr(2));
    return ids;
  };
  if (frame == 2)
    if (bdg::Widget* dw = ui.Find("dock"); dw && dw->dock) dw->dock->SelectPanel("project");
  if (frame == 4) click("h.search");
  if (frame == 6) type("main cam");
  if (frame == 9) {
    const auto ids = rows();
    hier = ids.size() == 1 && m->scene.Find(ids[0]) && m->scene.Find(ids[0])->name == "Main Camera" ? "1" : std::format("0({} rows)", ids.size());
    type("t:Light");
  }
  if (frame == 12) {
    const auto ids = rows();
    bool lights = ids.size() >= 2;
    for (const std::string& id : ids) lights = lights && m->scene.Find(id) && m->scene.Find(id)->Find("Light");
    types = lights ? "1" : std::format("0({} rows)", ids.size());
    type("");
  }
  if (frame == 15) {
    back = rows().size() >= 8 ? "1" : std::format("0({} rows)", rows().size());
    std::string sun;
    for (const std::string& r : m->scene.Roots())
      if (m->scene.Find(r)->name == "Sun") sun = r;
    click(("h." + sun).c_str());
  }
  if (frame == 18) click("insp.search");
  if (frame == 20) type("강도");
  if (frame == 23) {
    insp = ui.Find("f.Light.intensity") && !ui.Find("f.Light.color") && !ui.Find("f.Transform.position") ? "1" : "0";
    click("p.search");
  }
  if (frame == 25) type("light");
  if (frame == 28) {
    proj = ui.Find("p.item.Icons/comp-light.png") && ui.Find("p.item.Icons/comp-light-probe.png") && !ui.Find("p.item.Icons/comp-camera.png") &&
                   !ui.Find("p.item.Icons") ? "1" : "0";
    type("t:texture camera");
  }
  if (frame == 31) {
    kind = ui.Find("p.item.Icons/comp-camera.png") && !ui.Find("p.item.Icons/comp-light.png") ? "1" : "0";
    tiles0 = m->projectTiles;
    click("p.more");
  }
  if (frame == 33) { // the ⋯ menu: tiles / list
    const auto& items = ui.Menu();
    for (size_t i = 0; i < items.size(); ++i)
      if (items[i].label == "목록으로 보기" || items[i].label == "타일로 보기") {
        float x, y, w, h;
        if (ui.MenuItemRect(i, x, y, w, h)) clickAt(x + w * 0.5f, y + h * 0.5f);
      }
  }
  if (frame == 36) {
    more = m->projectTiles != tiles0 ? "1" : "0";
    if (bdg::Widget* dw = ui.Find("dock"); dw && dw->dock) dw->dock->SelectPanel("settings");
  }
  if (frame == 38) click("ps.search");
  if (frame == 40) type("중력");
  if (frame == 43) {
    bdg::Widget* physics = ui.Find("ps.physics");
    bdg::Widget* gravity = ui.Find("ps.physics.gravity");
    bdg::Widget* fixed = ui.Find("ps.physics.fixedTimestep");
    bdg::Widget* desc = ui.Find("ps.description");
    settings = physics && !physics->hidden && !physics->collapsed && gravity && !gravity->hidden && fixed && fixed->hidden && desc && desc->hidden ? "1" : "0";
    bdg::LogInfo("search test: hierarchy={} types={} back={} inspector={} project={} kind={} more={} settings={}", hier, types, back, insp, proj, kind,
                 more, settings);
    e->quitNow = true;
  }
}
BDG_EDITOR_TEST("BDG_SEARCH_TEST", InjectSearchTest, nullptr)
