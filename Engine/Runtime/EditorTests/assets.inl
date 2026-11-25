// Scripted editor checks: assets (moved out of Engine.cpp). Included by Engine.cpp just before the
// C API, in the same translation unit: bdg_engine and every editor helper are visible here.
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h) instead of editing Engine.cpp.

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
    click("p.layout");
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
    click("p.unused");
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
