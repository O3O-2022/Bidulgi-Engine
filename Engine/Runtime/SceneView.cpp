#include "Runtime/SceneView.h"
#include "Assets/AssetDatabase.h"
#include "Assets/GltfImport.h"
#include "Core/Command.h"
#include "Core/Log.h"
#include "Core/Space.h"
#include "Render2D/Batch2D.h"
#include "Runtime/EditorModel.h"
#include "Scene/Components.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }

// Ray / triangle (Moller-Trumbore); t along the ray, false when missed or behind.
bool RayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
  glm::vec3 e1 = b - a, e2 = c - a, p = glm::cross(d, e2);
  float det = glm::dot(e1, p);
  if (std::fabs(det) < 1e-12f) return false;
  float inv = 1.f / det;
  glm::vec3 s = o - a;
  float u = glm::dot(s, p) * inv;
  if (u < 0 || u > 1) return false;
  glm::vec3 q = glm::cross(s, e1);
  float v = glm::dot(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  t = glm::dot(e2, q) * inv;
  return t > 1e-6f;
}

void Bounds(const MeshData& m, const glm::mat4& world, glm::vec3& lo, glm::vec3& hi, bool& any) {
  for (const Vertex3D& v : m.vertices) {
    glm::vec3 p = glm::vec3(world * glm::vec4(v.px, v.py, v.pz, 1.f));
    lo = any ? glm::min(lo, p) : p;
    hi = any ? glm::max(hi, p) : p;
    any = true;
  }
}

MeshData Centered(MeshData m) { // cylinder and cone are built from y = 0; built-ins sit on their centre
  float lo = 1e9f, hi = -1e9f;
  for (const Vertex3D& v : m.vertices) { lo = std::min(lo, v.py); hi = std::max(hi, v.py); }
  for (Vertex3D& v : m.vertices) v.py -= (lo + hi) * 0.5f;
  return m;
}
}

glm::vec3 EditorCamera::Forward() const {
  float y = glm::radians(yaw), p = glm::radians(pitch);
  return {-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

Camera3D EditorCamera::ToCamera() const {
  Camera3D c;
  c.position = Position();
  c.target = c.position + Forward();
  c.fovY = fovY;
  c.nearZ = std::clamp(distance * 0.002f, 0.01f, 1.f);
  c.farZ = std::max(2000.f, distance * 50.f);
  return c;
}

bool SceneView::Init(RHI& rhi, Renderer3D& r) {
  rhi_ = &rhi;
  r_ = &r;
  auto add = [&](const char* name, MeshData d) {
    MeshEntry e;
    e.cpu = std::move(d);
    e.mesh = r.CreateMesh(e.cpu);
    builtins_[name] = std::move(e);
  };
  add("plane", MakePlaneXZ(1, 1));
  add("cube", MakeBox(1, 1, 1));
  add("sphere", MakeSphere(0.5f));
  add("cylinder", Centered(MakeCylinder(0.5f, 1)));
  add("cone", Centered(MakeCone(0.5f, 1)));
  { // greybox grid (3-3c): light grey with darker lines at the edges of every 1 m tile, a fine line every 25 cm
    std::vector<uint8_t> px(128 * 128 * 4);
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x) {
        const bool edge = x < 2 || y < 2 || x > 125 || y > 125, fine = x % 32 == 0 || y % 32 == 0;
        const uint8_t c = edge ? 150 : fine ? 190 : 214;
        uint8_t* o = &px[(y * 128 + x) * 4];
        o[0] = o[1] = c;
        o[2] = (uint8_t)std::min(255, c + 6);
        o[3] = 255;
      }
    TextureDesc td;
    td.w = td.h = 128;
    td.format = TextureFormat::SRGBA8;
    td.mipmaps = true;
    td.repeat = true;
    gridTex_ = rhi.CreateTexture(td, px.data());
  }
  return true;
}

void SceneView::Shutdown() {
  if (!r_) return;
  for (auto& [name, e] : builtins_) r_->DestroyMesh(e.mesh);
  for (auto& [name, e] : meshes_) r_->DestroyModel(e.model);
  builtins_.clear();
  meshes_.clear();
  rhi_->DestroyRenderTarget(rt_);
  rhi_->DestroyRenderTarget(gameRT_);
  r_ = nullptr;
}

const SceneView::MeshEntry* SceneView::Resolve(const std::string& name, const AssetDatabase* assets) {
  if (name.empty()) return nullptr;
  std::string ext = Lower(fs::path(name).extension().string());
  const AssetRecord* rec = assets && (ext == ".glb" || ext == ".gltf") ? assets->ByPath(name) : nullptr;
  auto it = meshes_.find(name);
  if (it != meshes_.end() && (!rec || it->second.hash == rec->hash)) return it->second.missing ? &builtins_["cube"] : &it->second;
  if (it != meshes_.end()) { r_->DestroyModel(it->second.model); meshes_.erase(it); } // the file changed: import again
  MeshEntry e;
  if (rec) {
    ImportedModel im;
    std::string err;
    fs::path full = fs::path(std::u8string((const char8_t*)assets->Root().c_str())) / fs::path(std::u8string((const char8_t*)name.c_str()));
    if (ImportGltf(full, im, &err)) {
      e.isModel = true;
      e.hash = rec->hash;
      e.model = r_->UploadModel(im);
      for (ImportedPrimitive& p : im.primitives) e.cpuPrims.push_back(std::move(p.mesh));
      return &(meshes_[name] = std::move(e));
    }
    LogWarn("scene view: {} did not import ({}); showing a placeholder cube", name, err);
  } else {
    auto b = builtins_.find(Lower(fs::path(name).stem().string()));
    if (b != builtins_.end()) return &b->second;
    LogWarn("scene view: mesh '{}' is not in the project; showing a placeholder cube", name);
  }
  e.missing = true;
  e.hash = rec ? rec->hash : 0;
  meshes_[name] = std::move(e);
  return &builtins_["cube"];
}

Material3D SceneView::MaterialFor(const std::string& name) {
  Material3D m;
  if (materials_ && !name.empty() && materials_->Find(name, m)) return m;
  std::string n = Lower(name);
  auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
  if (has("ground")) { m.baseColor = {0.26f, 0.27f, 0.28f, 1}; m.roughness = 0.95f; }
  else if (has("gold")) { m.baseColor = {1.f, 0.766f, 0.336f, 1}; m.metallic = 1; m.roughness = 0.3f; }
  else if (has("steel") || has("metal")) { m.baseColor = {0.56f, 0.57f, 0.58f, 1}; m.metallic = 1; m.roughness = 0.35f; }
  else if (has("copper")) { m.baseColor = {0.955f, 0.638f, 0.538f, 1}; m.metallic = 1; m.roughness = 0.3f; }
  else if (has("glow") || has("emissive")) { m.baseColor = {0.9f, 0.6f, 0.3f, 1}; m.emissive = {6.f, 3.6f, 1.2f, 1}; }
  else if (has("glass")) { m.baseColor = {0.7f, 0.85f, 0.95f, 0.35f}; m.roughness = 0.05f; m.alpha = Material3D::Alpha::Blend; }
  else if (!n.empty()) { // stable pastel from the name (FNV-1a)
    uint32_t h = 2166136261u;
    for (char c : n) h = (h ^ (uint8_t)c) * 16777619u;
    float hue = (h % 360) / 60.f;
    float x = 1.f - std::fabs(std::fmod(hue, 2.f) - 1.f);
    float r = 0, g = 0, b = 0;
    if (hue < 1) { r = 1; g = x; } else if (hue < 2) { r = x; g = 1; } else if (hue < 3) { g = 1; b = x; }
    else if (hue < 4) { g = x; b = 1; } else if (hue < 5) { r = x; b = 1; } else { r = 1; b = x; }
    m.baseColor = {0.25f + 0.45f * r, 0.25f + 0.45f * g, 0.25f + 0.45f * b, 1};
    m.roughness = 0.55f;
  }
  return m;
}

void SceneView::BuildItems(EditorModel& model, const AssetDatabase* assets) {
  for (auto it = blocks_.begin(); it != blocks_.end();) { // greybox meshes nobody used last frame
    if (it->second.hash == 0) { r_->DestroyMesh(it->second.mesh); it = blocks_.erase(it); }
    else { it->second.hash = 0; ++it; }
  }
  items_.clear();
  info_.clear();
  icons_.clear();
  extras_ = {};
  extras_.grid = true;
  extras_.view = debugView;
  probes_.clear();
  hasVolume_ = false;
  lightmapItems_ = 0;
  extras_.wireframe = wireframe;
  light_ = {};
  light_.toLight = {-0.35f, 0.85f, 0.4f};
  light_.intensity = 2.6f;
  light_.shadows = true;
  light_.background.draw = true; // default sky until a Sky component says otherwise
  bool sun = false, skySet = false;
  hasGrading_ = false;
  const Scene& scene = model.scene;
  std::set<std::string> selected(model.Selection().begin(), model.Selection().end());
  auto isSelected = [&](std::string id) { // the object or one of its parents
    while (!id.empty()) {
      if (selected.count(id)) return true;
      const SceneObject* o = scene.Find(id);
      id = o ? o->parent : "";
    }
    return false;
  };
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    std::string id = stack.back();
    stack.pop_back();
    const SceneObject* o = scene.Find(id);
    if (!o) continue;
    for (auto c = o->children.rbegin(); c != o->children.rend(); ++c) stack.push_back(*c);
    glm::mat4 world = scene.WorldMatrix(id);
    const MeshRenderer* mr = nullptr;
    const Blockout* bo = nullptr;
    for (const Component& c : o->components) {
      if (c.type == &MeshRenderer::StaticType()) mr = static_cast<const MeshRenderer*>(c.data);
      if (c.type == &Blockout::StaticType()) bo = static_cast<const Blockout*>(c.data);
      if (c.type == &Light::StaticType()) {
        const Light& l = *static_cast<const Light*>(c.data);
        bool directional = l.type == LightType::Directional;
        glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0))); // lights shine along -Z
        if (directional && !sun) {
          sun = true;
          light_.toLight = -forward;
          light_.color = l.color;
          light_.intensity = l.intensity;
          light_.shadows = l.castShadows;
        } else if (!directional) {
          LocalLight3D local;
          local.position = glm::vec3(world[3]);
          local.direction = forward;
          local.color = l.color;
          local.intensity = l.intensity;
          local.range = l.range;
          local.spot = l.type == LightType::Spot;
          local.outerAngle = l.spotAngle;
          local.innerAngle = l.innerSpotAngle;
          light_.locals.push_back(local);
        }
        icons_.push_back({id, glm::vec3(world[3]), directional ? 0 : 1});
      }
      if (c.type == &Camera::StaticType()) icons_.push_back({id, glm::vec3(world[3]), 2});
      if (c.type == &LightProbeVolume::StaticType() && !hasVolume_) {
        const LightProbeVolume& lv = *static_cast<const LightProbeVolume*>(c.data);
        glm::vec3 centre = glm::vec3(world[3]), half = glm::abs(space::ToGlm(lv.size)) * 0.5f;
        volume_.boxMin = centre - half;
        volume_.boxMax = centre + half;
        volume_.count = glm::clamp(glm::ivec3(lv.countX, lv.countY, lv.countZ), glm::ivec3(1), glm::ivec3(16));
        volume_.intensity = lv.intensity;
        hasVolume_ = true;
      }
      if (c.type == &ReflectionProbe::StaticType() && probes_.size() < (size_t)kMaxProbes) {
        const ReflectionProbe& rp = *static_cast<const ReflectionProbe*>(c.data);
        ReflectionProbe3D p;
        p.position = glm::vec3(world[3]);
        glm::vec3 half = glm::abs(space::ToGlm(rp.size)) * 0.5f;
        p.boxMin = p.position - half;
        p.boxMax = p.position + half;
        p.boxProjection = rp.boxProjection;
        p.intensity = rp.intensity;
        probes_.push_back(p);
      }
      if (c.type == &ColorGrading::StaticType() && !hasGrading_) { // 3-5b
        const ColorGrading& cg = *static_cast<const ColorGrading*>(c.data);
        hasGrading_ = true;
        grading_ = {};
        grading_.intensity = cg.intensity;
        grading_.contrast = cg.contrast;
        grading_.saturation = cg.saturation;
        grading_.temperature = cg.temperature;
        grading_.tint = cg.tint;
        if (cg.lut != lutPath_) { // read the .cube file once per path
          lutPath_ = cg.lut;
          lutCache_.clear();
          lutCacheSize_ = 0;
          if (!cg.lut.empty() && assets) {
            fs::path full = fs::path(std::u8string((const char8_t*)assets->Root().c_str())) / fs::path(std::u8string((const char8_t*)cg.lut.c_str()));
            std::ifstream in(full, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string err;
            if (!in && text.empty()) LogWarn("scene view: colour grading table {} not found", cg.lut);
            else if (!ParseCubeLut(text, lutCacheSize_, lutCache_, &err)) LogWarn("scene view: {}: {}", cg.lut, err);
          }
        }
        grading_.lutSize = lutCacheSize_;
        grading_.lut = lutCache_;
      }
      if (c.type == &Sky::StaticType() && !skySet) {
        const Sky& sky = *static_cast<const Sky*>(c.data);
        skySet = true;
        light_.background.zenith = sky.zenith;
        light_.background.horizon = sky.horizon;
        light_.background.ground = sky.ground;
        light_.background.intensity = sky.intensity;
        light_.background.sunSize = sky.sunSize;
      }
    }
    if (bo) { // greybox block (3-3c)
      const MeshEntry* be = BlockMesh((int)bo->shape, space::ToGlm(bo->size), bo->steps);
      Material3D bm;
      if (!bo->material.empty()) bm = MaterialFor(bo->material);
      else {
        bm.baseColor = {0.78f, 0.78f, 0.8f, 1};
        bm.roughness = 0.85f;
        bm.baseMap = gridTex_;
      }
      if (isSelected(id)) extras_.outlined.push_back(items_.size());
      items_.push_back({&be->mesh, world, bm});
      info_.push_back({id, &be->cpu});
      info_.back().ownerWorld = world;
      bool any = false;
      Bounds(be->cpu, world, info_.back().lo, info_.back().hi, any);
    }
    if (!mr) continue;
    const MeshEntry* e = Resolve(mr->mesh, assets);
    if (!e) continue;
    bool sel = isSelected(id);
    if (e->isModel) {
      for (const Model3D::Instance& in : e->model.instances) {
        if (in.mesh < 0 || in.mesh >= (int)e->model.meshes.size() || !e->model.meshes[in.mesh].indexCount) continue;
        if (sel) extras_.outlined.push_back(items_.size());
        items_.push_back({&e->model.meshes[in.mesh], world * in.world, e->model.materials[in.mesh]});
        items_.back().lightmapped = mr->bakeLightmap;
        info_.push_back({id, &e->cpuPrims[in.mesh]});
        info_.back().ownerWorld = world;
        bool any = false;
        Bounds(e->cpuPrims[in.mesh], items_.back().model, info_.back().lo, info_.back().hi, any);
      }
    } else {
      if (sel) extras_.outlined.push_back(items_.size());
      items_.push_back({&e->mesh, world, MaterialFor(mr->material)});
      items_.back().lightmapped = mr->bakeLightmap;
      info_.push_back({id, &e->cpu});
      info_.back().ownerWorld = world;
      bool any = false;
      Bounds(e->cpu, world, info_.back().lo, info_.back().hi, any);
    }
  }
  if ((int)light_.locals.size() > kMaxLocalLights) // nearest to what the camera looks at
    std::partial_sort(light_.locals.begin(), light_.locals.begin() + kMaxLocalLights, light_.locals.end(),
                      [&](const LocalLight3D& a, const LocalLight3D& b) {
                        return glm::distance(a.position, camera.pivot) < glm::distance(b.position, camera.pivot);
                      });
}

void SceneView::Render(EditorModel& model, const AssetDatabase* assets, const Widget& v, float scale) {
  vx_ = v.x; vy_ = v.y; vw_ = std::max(v.w, 1.f); vh_ = std::max(v.h, 1.f);
  int32_t pw = std::max(1, (int32_t)std::lround(v.w * scale)), ph = std::max(1, (int32_t)std::lround(v.h * scale));
  if (!rt_.Valid() || pw != pw_ || ph != ph_) {
    rhi_->DestroyRenderTarget(rt_);
    rt_ = rhi_->CreateRenderTarget(pw, ph, 1); // MSAA (by tier) happens inside the renderer
    pw_ = pw;
    ph_ = ph;
  }
  auto t0 = std::chrono::steady_clock::now();
  BuildItems(model, assets);
  r_->SetColorGrading(hasGrading_ ? &grading_ : nullptr);
  { // GI Tier 0: probes or the volume changed (added, moved, resized) -> bake again, two bounces; the chip adds one more
    uint64_t key = 1469598103934665603ull;
    auto fold = [&](float value) {
      uint32_t bits;
      std::memcpy(&bits, &value, 4);
      key = (key ^ bits) * 1099511628211ull;
    };
    for (const ReflectionProbe3D& p : probes_)
      for (float value : {p.position.x, p.position.y, p.position.z, p.boxMin.x, p.boxMin.y, p.boxMin.z, p.boxMax.x, p.boxMax.y, p.boxMax.z,
                          p.intensity, p.boxProjection ? 1.f : 0.f}) fold(value);
    if (hasVolume_)
      for (float value : {volume_.boxMin.x, volume_.boxMin.y, volume_.boxMin.z, volume_.boxMax.x, volume_.boxMax.y, volume_.boxMax.z,
                          (float)volume_.count.x, (float)volume_.count.y, (float)volume_.count.z, volume_.intensity}) fold(value);
    const bool anyGI = !probes_.empty() || hasVolume_;
    // Lightmaps (GI Tier 0-C) take longer: they bake from the chip only; the chip lights up while they are stale.
    uint64_t lmKey = 1469598103934665603ull;
    for (const DrawItem3D& it : items_) {
      if (!it.lightmapped) continue;
      ++lightmapItems_;
      lmKey = (lmKey ^ (uint64_t)(uintptr_t)it.mesh) * 1099511628211ull;
      for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
          uint32_t bits;
          std::memcpy(&bits, &it.model[c][r], 4);
          lmKey = (lmKey ^ bits) * 1099511628211ull;
        }
    }
    lightmapKey_ = lightmapItems_ ? lmKey : 0;
    if (!lightmapItems_ && r_->LightmappedObjects()) {
      r_->ClearLightmaps();
      bakedLightmapKey_ = 0;
    }
    // Baked GI cache (GI Tier 0-D): a bake of exactly these inputs is loaded instead of baked again.
    auto cachePath = [](uint64_t h) { return (std::filesystem::current_path() / ".cache" / "gi" / std::format("scene-{:016x}.bdggi", h)).string(); };
    auto tryLoad = [&]() {
      const uint64_t h = r_->GIInputHash(items_, light_);
      std::string err;
      if (!r_->LoadGI(cachePath(h), items_, h, &err)) return false;
      r_->giCacheNote = "loaded " + cachePath(h);
      if (r_->LightmappedObjects()) bakedLightmapKey_ = lightmapKey_;
      LogInfo("gi: loaded {} ({} probe(s), {} volume probe(s), {} lightmapped)", cachePath(h), r_->BakedProbes(), r_->VolumeProbes(),
              r_->LightmappedObjects());
      return true;
    };
    bool baked = false;
    if (bakeRequested_ && lightmapItems_) {
      r_->BakeLightmaps(items_, light_);
      bakedLightmapKey_ = lightmapKey_;
      baked = true;
    }
    if (key != probeKey_) {
      probeKey_ = key;
      r_->SetProbes(probes_);
      r_->SetVolume(hasVolume_ ? &volume_ : nullptr);
      firstBake_ = anyGI && !(bakeRequested_ ? false : tryLoad());
    } else if (lightmapItems_ && lightmapKey_ != lastLoadTry_ && lightmapKey_ != bakedLightmapKey_ && !bakeRequested_) {
      lastLoadTry_ = lightmapKey_; // lightmapped objects changed: a matching earlier bake may be on disk
      tryLoad();
    }
    if ((firstBake_ || bakeRequested_) && anyGI) {
      auto b0 = std::chrono::steady_clock::now();
      r_->BakeProbes(items_, light_);
      if (firstBake_) r_->BakeProbes(items_, light_);
      LogInfo("gi: baked {} probe(s){}{} in {:.0f} ms", probes_.size(), firstBake_ ? ", 2 bounces" : ", one more bounce",
              hasVolume_ ? std::format(", volume {} probes", r_->VolumeProbes()) : std::string(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count());
      baked = true;
    }
    if (baked) { // keep it for the next time these inputs come back (reopening the scene, undo)
      const uint64_t h = r_->GIInputHash(items_, light_);
      std::string err;
      if (r_->SaveGI(cachePath(h), h, &err)) {
        r_->giCacheNote = "saved " + cachePath(h);
        LogInfo("gi: saved {}", cachePath(h));
      } else {
        LogWarn("gi: could not save the bake: {}", err);
      }
      if (std::getenv("BDG_GI_DIAGNOSE")) {
        std::string text = r_->DiagnoseGI(items_);
        for (size_t a = 0, b; a < text.size(); a = b + 1) {
          b = text.find('\n', a);
          if (b == std::string::npos) b = text.size();
          LogInfo("gi diagnose: {}", text.substr(a, b - a));
        }
      }
    }
    firstBake_ = bakeRequested_ = false;
  }
  r_->Render(rt_, pw_, ph_, camera.ToCamera(), light_, items_, {0.17f, 0.19f, 0.22f, 1}, &extras_, &quality);
  renderMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool SceneView::ProjectUI(const glm::vec3& p, float& x, float& y) const {
  float px = 0, py = 0;
  if (pw_ <= 0 || !ProjectToPixel(camera.ToCamera(), pw_, ph_, p, px, py)) return false;
  x = vx_ + px / pw_ * vw_;
  y = vy_ + (1.f - py / ph_) * vh_;
  return true;
}

std::string SceneView::Pick(const EditorModel& model, float x, float y) const {
  if (pw_ <= 0) return {};
  // Icons first (lights and cameras have no surface): within 14 UI units of the cursor.
  std::string best;
  float bestD = 14.f;
  for (const Icon& ic : icons_) {
    float ix, iy;
    if (ProjectUI(ic.pos, ix, iy) && std::hypot(ix - x, iy - y) < bestD) { bestD = std::hypot(ix - x, iy - y); best = ic.id; }
  }
  if (!best.empty()) return best;
  Camera3D cam = camera.ToCamera();
  glm::mat4 inv = glm::inverse(cam.Projection((float)pw_ / ph_) * cam.View());
  float nx = (x - vx_) / vw_ * 2.f - 1.f, ny = 1.f - (y - vy_) / vh_ * 2.f;
  glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
  glm::vec3 o = glm::vec3(a) / a.w, d = glm::normalize(glm::vec3(b) / b.w - o);
  float bestT = 1e30f;
  for (size_t i = 0; i < items_.size(); ++i) {
    if (!info_[i].cpu || !model.scene.Find(info_[i].owner)) continue;
    glm::mat4 toLocal = glm::inverse(items_[i].model);
    glm::vec3 lo = glm::vec3(toLocal * glm::vec4(o, 1)), ld = glm::vec3(toLocal * glm::vec4(d, 0));
    const MeshData& m = *info_[i].cpu;
    for (size_t k = 0; k + 2 < m.indices.size(); k += 3) {
      const Vertex3D &p0 = m.vertices[m.indices[k]], &p1 = m.vertices[m.indices[k + 1]], &p2 = m.vertices[m.indices[k + 2]];
      float t;
      if (!RayTriangle(lo, ld, {p0.px, p0.py, p0.pz}, {p1.px, p1.py, p1.pz}, {p2.px, p2.py, p2.pz}, t)) continue;
      float dist = glm::distance(o, glm::vec3(items_[i].model * glm::vec4(lo + ld * t, 1))); // compare in world units
      if (dist < bestT) { bestT = dist; best = info_[i].owner; }
    }
  }
  return best;
}

glm::mat4 SceneView::ItemMatrix(const EditorModel& model, size_t i) const {
  if (!model.scene.Find(info_[i].owner)) return items_[i].model;
  return model.scene.WorldMatrix(info_[i].owner) * glm::inverse(info_[i].ownerWorld) * items_[i].model;
}

bool SceneView::Owned(const EditorModel& model, const std::string& owner, const std::set<std::string>& ids) const {
  for (std::string o = owner; !o.empty();) {
    if (ids.count(o)) return true;
    const SceneObject* so = model.scene.Find(o);
    o = so ? so->parent : "";
  }
  return false;
}

bool SceneView::ObjectBounds(const EditorModel& model, const std::string& id, glm::vec3& lo, glm::vec3& hi) const {
  bool any = false;
  const std::set<std::string> ids{id};
  for (size_t i = 0; i < items_.size(); ++i)
    if (info_[i].cpu && Owned(model, info_[i].owner, ids)) Bounds(*info_[i].cpu, ItemMatrix(model, i), lo, hi, any);
  return any;
}

bool SceneView::RaycastUI(const EditorModel& model, float x, float y, const std::set<std::string>& skip, glm::vec3& hit,
                          glm::vec3& normal, std::string* owner) const {
  if (pw_ <= 0) return false;
  Camera3D cam = camera.ToCamera();
  glm::mat4 inv = glm::inverse(cam.Projection((float)pw_ / ph_) * cam.View());
  float nx = (x - vx_) / vw_ * 2.f - 1.f, ny = 1.f - (y - vy_) / vh_ * 2.f;
  glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
  glm::vec3 o = glm::vec3(a) / a.w, d = glm::normalize(glm::vec3(b) / b.w - o);
  float best = 1e30f;
  for (size_t i = 0; i < items_.size(); ++i) {
    if (!info_[i].cpu || !model.scene.Find(info_[i].owner) || Owned(model, info_[i].owner, skip)) continue;
    const glm::mat4 m = ItemMatrix(model, i);
    const MeshData& md = *info_[i].cpu;
    for (size_t k = 0; k + 2 < md.indices.size(); k += 3) {
      const Vertex3D &v0 = md.vertices[md.indices[k]], &v1 = md.vertices[md.indices[k + 1]], &v2 = md.vertices[md.indices[k + 2]];
      const glm::vec3 p0 = glm::vec3(m * glm::vec4(v0.px, v0.py, v0.pz, 1)), p1 = glm::vec3(m * glm::vec4(v1.px, v1.py, v1.pz, 1)),
                      p2 = glm::vec3(m * glm::vec4(v2.px, v2.py, v2.pz, 1));
      float t;
      if (!RayTriangle(o, d, p0, p1, p2, t) || t >= best) continue;
      best = t;
      hit = o + d * t;
      glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
      normal = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : glm::vec3(0, 1, 0);
      if (glm::dot(normal, d) > 0) normal = -normal; // facing the camera
      if (owner) *owner = info_[i].owner;
    }
  }
  return best < 1e30f;
}

bool SceneView::NearestVertexUI(const EditorModel& model, float x, float y, const std::set<std::string>& ids, bool inIds, float maxDist,
                                glm::vec3& vertex) const {
  float best = maxDist;
  bool found = false;
  for (size_t i = 0; i < items_.size(); ++i) {
    if (!info_[i].cpu || !model.scene.Find(info_[i].owner) || Owned(model, info_[i].owner, ids) != inIds) continue;
    const glm::mat4 m = ItemMatrix(model, i);
    for (const Vertex3D& v : info_[i].cpu->vertices) {
      const glm::vec3 p = glm::vec3(m * glm::vec4(v.px, v.py, v.pz, 1));
      float sx, sy;
      if (!ProjectUI(p, sx, sy)) continue;
      const float dist = std::hypot(sx - x, sy - y);
      if (dist < best) { best = dist; vertex = p; found = true; }
    }
  }
  return found;
}

bool SceneView::MeasurePoint(const EditorModel& model, float x, float y, glm::vec3& p) const {
  if (vHeld_ && NearestVertexUI(model, x, y, {}, false, 24.f, p)) return true; // V: onto a vertex
  glm::vec3 n;
  if (RaycastUI(model, x, y, {}, p, n)) return true;
  // nothing under the mouse: the ground plane (y = 0)
  Camera3D cam = camera.ToCamera();
  glm::mat4 inv = glm::inverse(cam.Projection((float)pw_ / ph_) * cam.View());
  float nx = (x - vx_) / vw_ * 2.f - 1.f, ny = 1.f - (y - vy_) / vh_ * 2.f;
  glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
  glm::vec3 o = glm::vec3(a) / a.w, d = glm::normalize(glm::vec3(b) / b.w - o);
  if (std::abs(d.y) < 1e-6f || -o.y / d.y <= 0) return false;
  p = o + d * (-o.y / d.y);
  return true;
}

const SceneView::MeshEntry* SceneView::BlockMesh(int shape, const glm::vec3& size, int steps) {
  const std::string key = std::format("{}|{}|{}|{}|{}", shape, size.x, size.y, size.z, shape == (int)BlockoutShape::Stairs ? steps : 0);
  auto it = blocks_.find(key);
  if (it == blocks_.end()) {
    MeshEntry e;
    e.cpu = MakeBlock((BlockShape)shape, std::abs(size.x), std::abs(size.y), std::abs(size.z), steps);
    e.mesh = r_->CreateMesh(e.cpu);
    it = blocks_.emplace(key, std::move(e)).first;
  }
  it->second.hash = 1; // used this frame
  return &it->second;
}

std::vector<SceneView::FaceHandle> SceneView::FaceHandles(const EditorModel& model) const {
  std::vector<FaceHandle> out;
  if (model.tool != "blockout" || model.Selected().empty()) return out;
  SceneObject* o = const_cast<SceneObject*>(model.scene.Find(model.Selected()));
  Component* c = o ? o->Find("Blockout") : nullptr;
  if (!c) return out;
  const glm::vec3 s = space::ToGlm(static_cast<const Blockout*>(c->data)->size);
  const glm::mat4 w = model.scene.WorldMatrix(o->id);
  const glm::vec3 centers[6] = {{s.x * 0.5f, s.y * 0.5f, 0}, {-s.x * 0.5f, s.y * 0.5f, 0}, {0, s.y, 0}, {0, 0, 0},
                                {0, s.y * 0.5f, s.z * 0.5f}, {0, s.y * 0.5f, -s.z * 0.5f}};
  const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(w)));
  for (int f = 0; f < 6; ++f) out.push_back({f, glm::vec3(w * glm::vec4(centers[f], 1)), glm::normalize(nm * dirs[f])});
  return out;
}

bool SceneView::FaceHandlePoint(const EditorModel& model, int face, float& x, float& y) const {
  for (const FaceHandle& h : FaceHandles(model))
    if (h.face == face) return ProjectUI(h.center, x, y);
  return false;
}

std::string SceneView::CreateBlock(EditorModel& model, int shape) {
  static const char* names[] = {"Box", "Stairs", "Ramp", "Cylinder"};
  static const char* sizes[] = {"[1,1,1]", "[1,1,2]", "[1,1,2]", "[1,1,1]"};
  shape = std::clamp(shape, 0, 3);
  glm::vec3 at{0}, n;
  if (!RaycastUI(model, vx_ + vw_ * 0.5f, vy_ + vh_ * 0.5f, {}, at, n)) at = glm::vec3(camera.pivot.x, 0, camera.pivot.z);
  const float step = snap.always ? snap.move : 0.5f; // on the grid
  at.x = SnapValue(at.x, step);
  at.z = SnapValue(at.z, step);
  std::string id;
  for (int k = 1; id.empty() || model.scene.Find(id); ++k) id = std::format("b{}", k);
  model.bus.BeginGroup(std::format("Add {}", names[shape]), "ui");
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!model.bus.Execute(c, &err)) LogWarn("blockout: {}", err);
  };
  run("add", "", std::format(R"({{"id":"{}","name":"{}"}})", id, names[shape]));
  run("addcomp", id, std::format(R"({{"type":"Blockout","value":{{"shape":"{}","size":{}}}}})", names[shape], sizes[shape]));
  run("set", id, std::format(R"({{"path":"Transform.position","value":[{},{},{}]}})", at.x, at.y, at.z));
  model.bus.EndGroup();
  model.Select(id);
  return id;
}

bool SceneView::AlignSelection(EditorModel& model, int axis, AlignMode mode) {
  std::set<std::string> sel(model.Selection().begin(), model.Selection().end());
  std::vector<std::string> ids;
  for (const std::string& id : model.Selection()) { // top-level picks only: a selected parent carries its children
    const SceneObject* o = model.scene.Find(id);
    if (o && const_cast<SceneObject*>(o)->Find("Transform") && !(!o->parent.empty() && Owned(model, o->parent, sel))) ids.push_back(id);
  }
  std::vector<AlignBox> boxes;
  std::vector<std::string> kept;
  for (const std::string& id : ids) {
    AlignBox b;
    if (!ObjectBounds(model, id, b.lo, b.hi)) b.lo = b.hi = space::ToGlm(model.scene.WorldPosition(id));
    boxes.push_back(b);
    kept.push_back(id);
  }
  if (boxes.size() < (mode == AlignMode::Distribute ? 3u : 2u)) return false;
  const std::vector<float> offsets = AlignOffsets(boxes, axis, mode);
  const char* modeName = mode == AlignMode::Min ? "min" : mode == AlignMode::Center ? "centre" : mode == AlignMode::Max ? "max" : "distribute";
  model.bus.BeginGroup(std::format("Align {} {} ({} objects)", "XYZ"[axis], modeName, kept.size()), "ui");
  int moved = 0;
  for (size_t k = 0; k < kept.size(); ++k) {
    if (std::abs(offsets[k]) < 1e-6f) continue;
    const SceneObject* o = model.scene.Find(kept[k]);
    glm::vec3 world = glm::vec3(model.scene.WorldMatrix(kept[k])[3]);
    world[axis] += offsets[k];
    const glm::mat4 toParent = o->parent.empty() ? glm::mat4(1.f) : glm::inverse(model.scene.WorldMatrix(o->parent));
    const glm::vec3 local = glm::vec3(toParent * glm::vec4(world, 1.f));
    Command c;
    c.name = "set";
    c.target = kept[k];
    c.args = std::format(R"({{"path":"Transform.position","value":[{},{},{}]}})", local.x, local.y, local.z);
    std::string err;
    if (model.bus.Execute(c, &err)) ++moved;
    else LogWarn("align: {}", err);
  }
  if (moved) model.bus.EndGroup();
  else { std::string err; model.bus.CancelGroup(&err); }
  return moved > 0;
}

bool SceneView::ObjectCenter(const EditorModel& model, const std::string& id, glm::vec3& center, float& radius) const {
  glm::vec3 lo{0}, hi{0};
  bool any = false;
  for (size_t i = 0; i < items_.size(); ++i) {
    std::string owner = info_[i].owner; // the object or one of its children
    bool inside = false;
    while (!owner.empty() && !inside) {
      inside = owner == id;
      const SceneObject* o = model.scene.Find(owner);
      owner = o ? o->parent : "";
    }
    if (inside && info_[i].cpu) Bounds(*info_[i].cpu, items_[i].model, lo, hi, any);
  }
  if (!any) {
    if (!model.scene.Find(id)) return false;
    lo = hi = space::ToGlm(model.scene.WorldPosition(id));
    lo -= glm::vec3(0.5f);
    hi += glm::vec3(0.5f);
  }
  center = (lo + hi) * 0.5f;
  radius = std::max(glm::length(hi - lo) * 0.5f, 0.25f);
  return true;
}

bool SceneView::CurrentGizmo(const EditorModel& model, GizmoMode& mode, GizmoFrame& frame, GizmoView& view) const {
  if (model.tool == "move") mode = GizmoMode::Move;
  else if (model.tool == "rotate") mode = GizmoMode::Rotate;
  else if (model.tool == "scale") mode = GizmoMode::Scale;
  else return false;
  const SceneObject* o = model.Selected().empty() ? nullptr : model.scene.Find(model.Selected());
  if (!o || !const_cast<SceneObject*>(o)->Find("Transform") || pw_ <= 0) return false;
  view.camera = camera.ToCamera();
  view.x = vx_; view.y = vy_; view.w = vw_; view.h = vh_;
  glm::mat4 world = model.scene.WorldMatrix(o->id);
  glm::mat3 axes(1.f);
  if (localSpace || mode == GizmoMode::Scale)
    for (int i = 0; i < 3; ++i) {
      glm::vec3 c = glm::vec3(world[i]);
      axes[i] = glm::length(c) > 1e-8f ? glm::normalize(c) : glm::mat3(1.f)[i];
    }
  frame = MakeGizmoFrame(view, glm::vec3(world[3]), axes);
  return true;
}

bool SceneView::GizmoPoint(const EditorModel& model, GizmoHandle h, float& x, float& y) const {
  GizmoMode mode;
  GizmoFrame f;
  GizmoView v;
  glm::vec2 p;
  if (!CurrentGizmo(model, mode, f, v) || !GizmoHandlePoint(mode, h, f, v, p)) return false;
  x = p.x;
  y = p.y;
  return true;
}

// Applies a drag delta to every dragged object from its values at the start of the drag (never accumulates).
void SceneView::ApplyGizmo(EditorModel& model, const GizmoDelta& d) {
  auto rotationOf = [](const glm::mat4& m) {
    glm::mat3 r;
    for (int i = 0; i < 3; ++i) {
      glm::vec3 c = glm::vec3(m[i]);
      r[i] = glm::length(c) > 1e-8f ? glm::normalize(c) : glm::mat3(1.f)[i];
    }
    return glm::normalize(glm::quat_cast(r));
  };
  auto set = [&](const std::string& id, const char* field, const glm::vec3& v) {
    Command c;
    c.name = "set";
    c.target = id;
    c.args = std::format(R"({{"path":"Transform.{}","value":[{},{},{}]}})", field, v.x, v.y, v.z);
    std::string err;
    if (!model.bus.Execute(c, &err)) LogWarn("gizmo: {}", err);
  };
  const GizmoMode mode = gizmo_.Mode();
  const glm::vec3 pivot = gizmo_.Frame().origin;
  for (const DragStart& s : starts_) {
    glm::vec3 worldPos = glm::vec3(s.world[3]);
    glm::mat4 toParent = glm::inverse(s.parentWorld);
    if (mode == GizmoMode::Move) {
      set(s.id, "position", glm::vec3(toParent * glm::vec4(worldPos + d.move, 1.f)));
    } else if (mode == GizmoMode::Rotate) {
      glm::quat local = glm::inverse(rotationOf(s.parentWorld)) * (d.rotate * rotationOf(s.world));
      set(s.id, "rotation", space::ToGlm(space::QuatToEulerDegrees(glm::normalize(local))));
      if (starts_.size() > 1) // several objects turn around the primary one
        set(s.id, "position", glm::vec3(toParent * glm::vec4(pivot + d.rotate * (worldPos - pivot), 1.f)));
    } else {
      set(s.id, "scale", space::ToGlm(s.scale) * d.scale);
    }
  }
  gizmoChanged_ = true;
}

void SceneView::Frame(const EditorModel& model) {
  glm::vec3 lo{0}, hi{0};
  bool any = false;
  std::vector<std::string> ids = model.Selection();
  if (ids.empty())
    for (const ItemInfo& in : info_) ids.push_back(in.owner);
  for (const std::string& id : ids) {
    glm::vec3 c;
    float r;
    if (!ObjectCenter(model, id, c, r)) continue;
    lo = any ? glm::min(lo, c - glm::vec3(r)) : c - glm::vec3(r);
    hi = any ? glm::max(hi, c + glm::vec3(r)) : c + glm::vec3(r);
    any = true;
  }
  if (!any) return;
  camera.pivot = (lo + hi) * 0.5f;
  float radius = glm::length(hi - lo) * 0.5f / std::sqrt(3.f) * 1.2f; // the cube around a sphere back to its radius
  camera.distance = std::max(radius / std::sin(glm::radians(camera.fovY * 0.5f)), 0.5f);
}

void SceneView::HandleEvents(std::vector<InputEvent>& events, const Widget& v, EditorModel& model, double dt, bool textFocused) {
  using T = InputEvent::Type;
  vx_ = v.x; vy_ = v.y; vw_ = std::max(v.w, 1.f); vh_ = std::max(v.h, 1.f);
  auto inside = [&](const InputEvent& e) { return e.x >= v.x && e.y >= v.y && e.x < v.x + v.w && e.y < v.y + v.h; };
  const float deg = 0.3f; // degrees per UI unit of mouse travel
  for (size_t i = 0; i < events.size();) {
    InputEvent& e = events[i];
    bool used = false;
    switch (e.type) {
      case T::MouseDown:
        if (drag_ == Drag::None && inside(e) && e.button == 1) { // chips first
          const Chip* hit = nullptr;
          for (const Chip& c : chips_)
            if (e.x >= c.x && e.y >= c.y && e.x < c.x + c.w && e.y < c.y + c.h) hit = &c;
          if (hit) {
            if (hit->name == "view") debugView = (DebugView)(((int)debugView + 1) % 11);
            else if (hit->name == "wire") wireframe = !wireframe;
            else if (hit->name == "bounds") bounds = !bounds;
            else if (hit->name == "stats") statsPanel = !statsPanel;
            else if (hit->name == "gi") bakeRequested_ = true;
            else if (hit->name == "snap") { // Off -> 0.1 -> 0.25 -> 0.5 -> 1 -> 2 m -> Off
              const size_t n = std::size(kSnapSteps);
              size_t at = 0;
              for (size_t k = 0; k < n; ++k)
                if (snap.always && kSnapSteps[k] == snap.move) at = k;
              const float next = kSnapSteps[(at + 1) % n];
              snap.always = next > 0;
              if (next > 0) snap.move = next;
            } else if (hit->name == "align") alignOpen_ = !alignOpen_;
            else if (hit->name.starts_with("bo.")) CreateBlock(model, hit->name[3] - '0'); // bo.<shape>
            else if (hit->name.starts_with("al.")) { // al.<axis>.<mode>
              const int axis = hit->name[3] - 'x';
              const std::string m = hit->name.substr(5);
              AlignSelection(model, axis, m == "min" ? AlignMode::Min : m == "mid" ? AlignMode::Center : m == "max" ? AlignMode::Max
                                                                        : AlignMode::Distribute);
            }
            used = true;
            break;
          }
        }
        if (drag_ == Drag::None && inside(e) && e.button == 1 && !e.alt) { // a gizmo handle wins over picking
          GizmoMode mode;
          GizmoFrame frame;
          GizmoView gv;
          GizmoHandle h = CurrentGizmo(model, mode, frame, gv) ? GizmoHit(mode, frame, gv, {e.x, e.y}) : GizmoHandle::None;
          if (h != GizmoHandle::None && gizmo_.Begin(mode, h, frame, gv, {e.x, e.y})) {
            starts_.clear();
            std::set<std::string> sel(model.Selection().begin(), model.Selection().end());
            std::vector<std::string> ids = model.Selection();
            std::stable_partition(ids.begin(), ids.end(), [&](const std::string& id) { return id == model.Selected(); });
            for (const std::string& id : ids) {
              SceneObject* o = model.scene.Find(id);
              Component* c = o ? o->Find("Transform") : nullptr;
              if (!c) continue;
              bool underSelected = false; // a selected parent already carries it
              for (const SceneObject* p = o->parent.empty() ? nullptr : model.scene.Find(o->parent); p && !underSelected;
                   p = p->parent.empty() ? nullptr : model.scene.Find(p->parent))
                underSelected = sel.count(p->id) > 0;
              if (underSelected) continue;
              const Transform& t = *static_cast<const Transform*>(c->data);
              starts_.push_back({id, t.position, t.rotation, t.scale, model.scene.WorldMatrix(id),
                                 o->parent.empty() ? glm::mat4(1.f) : model.scene.WorldMatrix(o->parent)});
            }
            const char* label = mode == GizmoMode::Move ? "Move" : mode == GizmoMode::Rotate ? "Rotate" : "Scale";
            model.bus.BeginGroup(std::format("{} {}", label, starts_.size() == 1 ? model.scene.Find(starts_[0].id)->name
                                                                                 : std::format("{} objects", starts_.size())), "ui");
            gizmoChanged_ = false;
            drag_ = Drag::Gizmo;
            dragButton_ = 1;
            downX_ = lastX_ = e.x;
            downY_ = lastY_ = e.y;
            vertexSourceSet_ = hasSnapTarget_ = false;
            lastMove_ = glm::vec3(0);
            if (starts_.empty() || !ObjectBounds(model, starts_[0].id, startLo_, startHi_))
              startLo_ = startHi_ = starts_.empty() ? glm::vec3(0) : glm::vec3(starts_[0].world[3]);
            used = true;
            break;
          }
        }
        if (drag_ == Drag::None && inside(e) && e.button == 1 && !e.alt && model.tool == "blockout") { // a face handle (3-3c)
          for (const FaceHandle& fh : FaceHandles(model)) {
            float hx, hy;
            if (!ProjectUI(fh.center, hx, hy) || std::hypot(hx - e.x, hy - e.y) > 9.f) continue;
            const SceneObject* o = model.scene.Find(model.Selected());
            Component* tc = const_cast<SceneObject*>(o)->Find("Transform");
            Component* bc = const_cast<SceneObject*>(o)->Find("Blockout");
            if (!tc || !bc) break;
            GizmoView gv;
            gv.camera = camera.ToCamera();
            gv.x = vx_; gv.y = vy_; gv.w = vw_; gv.h = vh_;
            GizmoFrame f;
            f.origin = fh.center;
            const glm::vec3 helper = std::abs(fh.normal.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 t1 = glm::normalize(glm::cross(helper, fh.normal));
            f.axes = glm::mat3(fh.normal, t1, glm::cross(fh.normal, t1));
            if (!gizmo_.Begin(GizmoMode::Move, GizmoHandle::X, f, gv, {e.x, e.y})) break;
            face_ = {o->id, fh.face, static_cast<const Blockout*>(bc->data)->size, static_cast<const Transform*>(tc->data)->position,
                     model.scene.WorldMatrix(o->id), o->parent.empty() ? glm::mat4(1.f) : model.scene.WorldMatrix(o->parent)};
            static const char* faceNames[] = {"+X", "-X", "top", "bottom", "+Z", "-Z"};
            model.bus.BeginGroup(std::format("Resize {} ({})", o->name, faceNames[fh.face]), "ui");
            drag_ = Drag::Face;
            dragButton_ = 1;
            used = true;
            break;
          }
          if (used) break;
        }
        if (drag_ == Drag::None && inside(e) && e.button == 1 && !e.alt && model.tool == "measure") { // the ruler (3-3b)
          glm::vec3 p;
          if (MeasurePoint(model, e.x, e.y, p)) {
            measure_ = {p, p};
            hasMeasure_ = true;
            drag_ = Drag::Measure;
            dragButton_ = 1;
            used = true;
            break;
          }
        }
        if (drag_ == Drag::None && inside(e)) {
          dragButton_ = e.button;
          drag_ = e.button == 1 ? (e.alt ? Drag::Orbit : Drag::Click) : e.button == 2 ? Drag::Pan : e.button == 3 ? Drag::Fly : Drag::None;
          lastX_ = downX_ = e.x;
          lastY_ = downY_ = e.y;
          clickCtrl_ = e.ctrl || e.shift;
          used = drag_ != Drag::None;
        }
        break;
      case T::MouseMove: {
        hover_ = inside(e);
        if (drag_ == Drag::Gizmo) {
          const bool surface = e.ctrl && e.shift && gizmo_.Mode() == GizmoMode::Move;
          GizmoDelta d = gizmo_.Update({e.x, e.y}, !surface && snap.always != e.ctrl, snap); // Ctrl flips grid snapping
          hasSnapTarget_ = false;
          if (gizmo_.Mode() == GizmoMode::Move && !starts_.empty()) {
            std::set<std::string> moving;
            for (const DragStart& s : starts_) moving.insert(s.id);
            if (vHeld_) { // vertex snap: the selection's vertex nearest the grab point onto the vertex under the mouse
              glm::vec3 p;
              if (!vertexSourceSet_ && NearestVertexUI(model, downX_, downY_, moving, true, 1e9f, p)) {
                vertexSource_ = p - lastMove_; // where it was when the drag began
                vertexSourceSet_ = true;
              }
              if (vertexSourceSet_ && NearestVertexUI(model, e.x, e.y, moving, false, 24.f, p)) {
                d.move = p - vertexSource_;
                snapTarget_ = p;
                hasSnapTarget_ = true;
              }
            } else if (surface) { // surface snap: rest on what is under the mouse
              glm::vec3 hit, n;
              if (RaycastUI(model, e.x, e.y, moving, hit, n)) {
                const glm::vec3 pivot = glm::vec3(starts_[0].world[3]);
                d.move = hit + n * RestOffset(pivot, startLo_, startHi_, n) - pivot;
                snapTarget_ = hit;
                hasSnapTarget_ = true;
              }
            }
          }
          lastMove_ = d.move;
          ApplyGizmo(model, d);
          used = true;
          break;
        }
        if (drag_ == Drag::Measure) {
          glm::vec3 p;
          if (MeasurePoint(model, e.x, e.y, p)) measure_.b = p;
          used = true;
          break;
        }
        if (drag_ == Drag::Face) { // push / pull: the size grows on that side, the opposite face stays
          const GizmoDelta d = gizmo_.Update({e.x, e.y}, snap.always != e.ctrl, snap);
          const int a = face_.face / 2;
          const float sign = face_.face % 2 == 0 ? 1.f : -1.f;
          const glm::vec3 normal = gizmo_.Frame().axes[0];
          const float scaleOnAxis = std::max(glm::length(glm::vec3(face_.world[a])), 1e-6f);
          const glm::vec3 size0 = space::ToGlm(face_.size);
          glm::vec3 size = size0;
          size[a] = std::max(size0[a] + glm::dot(d.move, normal) / scaleOnAxis, 0.05f);
          const float grown = size[a] - size0[a];
          glm::vec3 shift(0); // pivot (bottom centre) in the block's own space
          if (a != 1) shift[a] = sign * grown * 0.5f;
          else if (sign < 0) shift.y = -grown;
          const glm::vec3 world = glm::vec3(face_.world * glm::vec4(shift, 1.f));
          const glm::vec3 local = glm::vec3(glm::inverse(face_.parentWorld) * glm::vec4(world, 1.f));
          auto set = [&](const char* path, const glm::vec3& v) {
            Command c;
            c.name = "set";
            c.target = face_.id;
            c.args = std::format(R"({{"path":"{}","value":[{},{},{}]}})", path, v.x, v.y, v.z);
            std::string err;
            if (!model.bus.Execute(c, &err)) LogWarn("blockout: {}", err);
          };
          set("Blockout.size", size);
          set("Transform.position", local);
          used = true;
          break;
        }
        if (drag_ == Drag::None) {
          GizmoMode mode;
          GizmoFrame frame;
          GizmoView gv;
          hot_ = hover_ && CurrentGizmo(model, mode, frame, gv) ? GizmoHit(mode, frame, gv, {e.x, e.y}) : GizmoHandle::None;
          hotFace_ = -1;
          for (const FaceHandle& fh : FaceHandles(model)) {
            float hx, hy;
            if (hover_ && ProjectUI(fh.center, hx, hy) && std::hypot(hx - e.x, hy - e.y) <= 9.f) hotFace_ = fh.face;
          }
          break;
        }
        float dx = e.x - lastX_, dy = e.y - lastY_;
        lastX_ = e.x;
        lastY_ = e.y;
        if (drag_ == Drag::Orbit) {
          camera.yaw -= dx * deg;
          camera.pitch = std::clamp(camera.pitch - dy * deg, -89.f, 89.f);
        } else if (drag_ == Drag::Fly) { // look around from where the camera is
          glm::vec3 pos = camera.Position();
          camera.yaw -= dx * deg;
          camera.pitch = std::clamp(camera.pitch - dy * deg, -89.f, 89.f);
          camera.pivot = pos + camera.Forward() * camera.distance;
        } else if (drag_ == Drag::Pan) { // the point under the cursor follows it
          glm::vec3 f = camera.Forward(), right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0))), up = glm::cross(right, f);
          float k = 2.f * camera.distance * std::tan(glm::radians(camera.fovY * 0.5f)) / vh_;
          camera.pivot += (-dx * right + dy * up) * k;
        }
        used = true;
        break;
      }
      case T::MouseUp:
        if (drag_ == Drag::Gizmo && e.button == 1) {
          model.bus.EndGroup();
          drag_ = Drag::None;
          hasSnapTarget_ = false;
          used = true;
          break;
        }
        if (drag_ == Drag::Face && e.button == 1) {
          model.bus.EndGroup();
          drag_ = Drag::None;
          used = true;
          break;
        }
        if (drag_ == Drag::Measure && e.button == 1) {
          LogInfo("measure: {:.3f} m (x {:.3f} y {:.3f} z {:.3f})", measure_.Length(), measure_.Delta().x, measure_.Delta().y, measure_.Delta().z);
          drag_ = Drag::None;
          used = true;
          break;
        }
        if (drag_ != Drag::None && e.button == dragButton_) {
          if (drag_ == Drag::Click && std::hypot(e.x - downX_, e.y - downY_) < 4.f) {
            std::string id = Pick(model, e.x, e.y);
            if (clickCtrl_) {
              std::vector<std::string> sel = model.Selection();
              auto it = std::find(sel.begin(), sel.end(), id);
              if (it != sel.end()) sel.erase(it);
              else if (!id.empty()) sel.push_back(id);
              model.SetSelection(sel);
            } else if (id.empty()) model.SetSelection({});
            else model.Select(id);
          }
          if (drag_ == Drag::Fly) held_.clear();
          drag_ = Drag::None;
          used = true;
        }
        break;
      case T::Wheel:
        if (inside(e)) {
          if (drag_ == Drag::Fly) flySpeed_ = std::clamp(flySpeed_ * std::pow(1.2f, e.wheel), 0.1f, 500.f);
          else camera.distance = std::clamp(camera.distance * std::pow(0.88f, e.wheel), 0.05f, 20000.f);
          used = true;
        }
        break;
      case T::KeyDown:
        if (e.key == Key::V && !textFocused && !e.ctrl && !e.alt && (hover_ || drag_ != Drag::None)) { // vertex snap while held
          vHeld_ = true;
          used = true;
          break;
        }
        if (e.key == Key::Escape && hasMeasure_ && drag_ != Drag::Gizmo && hover_) {
          hasMeasure_ = false;
          drag_ = drag_ == Drag::Measure ? Drag::None : drag_;
          used = true;
          break;
        }
        if (drag_ == Drag::Face && e.key == Key::Escape) {
          std::string err;
          if (!model.bus.CancelGroup(&err)) LogWarn("blockout: cancel failed: {}", err);
          drag_ = Drag::None;
          used = true;
          break;
        }
        if (drag_ == Drag::Gizmo && e.key == Key::Escape) { // cancel: the whole drag goes away
          std::string err;
          if (!model.bus.CancelGroup(&err)) LogWarn("gizmo: cancel failed: {}", err);
          drag_ = Drag::None;
          used = true;
          break;
        }
        if (e.key >= Key::Num1 && e.key <= Key::Num9 && hover_ && !textFocused && drag_ == Drag::None) {
          int n = (int)e.key - (int)Key::Num0;
          if (e.alt) debugView = (DebugView)std::min(n - 1, 8);              // Alt + 1..9: what the picture shows
          else if (e.ctrl) StoreBookmark(model, n);                       // Ctrl + 1..9: remember this view
          else if (!JumpToBookmark(model, n)) LogInfo("scene view: no camera bookmark {}", n);
          used = true;
          break;
        }
        if (e.key == Key::X && hover_ && !textFocused && !e.ctrl && !e.alt && drag_ == Drag::None) {
          localSpace = !localSpace;
          used = true;
          break;
        }
        if (drag_ == Drag::Fly && (e.key == Key::W || e.key == Key::A || e.key == Key::S || e.key == Key::D || e.key == Key::Q || e.key == Key::E)) {
          held_.insert(e.key);
          used = true;
        } else if (e.key == Key::F && hover_ && !textFocused && !e.ctrl && !e.alt) {
          Frame(model);
          used = true;
        }
        if (drag_ == Drag::Fly && e.shift) flySpeed_ = std::max(flySpeed_, 15.f);
        break;
      case T::KeyUp:
        held_.erase(e.key);
        if (e.key == Key::V) vHeld_ = false;
        used = true; // the UI has no use for key releases
        break;
      default: break;
    }
    if (used) events.erase(events.begin() + (std::ptrdiff_t)i);
    else ++i;
  }
  if (drag_ == Drag::Fly && !held_.empty()) {
    glm::vec3 f = camera.Forward(), right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
    glm::vec3 move{0};
    if (held_.count(Key::W)) move += f;
    if (held_.count(Key::S)) move -= f;
    if (held_.count(Key::D)) move += right;
    if (held_.count(Key::A)) move -= right;
    if (held_.count(Key::E)) move += glm::vec3(0, 1, 0);
    if (held_.count(Key::Q)) move -= glm::vec3(0, 1, 0);
    if (glm::length(move) > 0) camera.pivot += glm::normalize(move) * flySpeed_ * (float)std::clamp(dt, 0.0, 0.1);
  }
}

namespace {
// Every CameraBookmark component in the scene: (object id, component).
std::vector<std::pair<std::string, CameraBookmark*>> Bookmarks(const EditorModel& model) {
  std::vector<std::pair<std::string, CameraBookmark*>> out;
  std::vector<std::string> stack(model.scene.Roots().rbegin(), model.scene.Roots().rend());
  while (!stack.empty()) {
    std::string id = stack.back();
    stack.pop_back();
    const SceneObject* o = model.scene.Find(id);
    if (!o) continue;
    for (auto c = o->children.rbegin(); c != o->children.rend(); ++c) stack.push_back(*c);
    for (const Component& c : o->components)
      if (c.type == &CameraBookmark::StaticType()) out.push_back({id, static_cast<CameraBookmark*>(c.data)});
  }
  return out;
}
}

bool SceneView::JumpToBookmark(const EditorModel& model, int slot) {
  for (auto& [id, b] : Bookmarks(model)) {
    if (b->slot != slot) continue;
    camera.pivot = space::ToGlm(b->pivot);
    camera.yaw = b->yaw;
    camera.pitch = std::clamp(b->pitch, -89.f, 89.f);
    camera.distance = std::max(b->distance, 0.05f);
    QualityTier tier;
    if (!b->tier.empty() && TierFromString(b->tier, tier)) quality = QualityPreset(tier);
    const SceneObject* o = model.scene.Find(id);
    bookmarkNote_ = std::format("{}  ·  {}", slot, o ? o->name : id);
    noteFrames_ = 90;
    return true;
  }
  return false;
}

std::string SceneView::BookmarkName(const EditorModel& model, int slot) const {
  for (auto& [id, b] : Bookmarks(model))
    if (b->slot == slot) {
      const SceneObject* o = model.scene.Find(id);
      return o ? o->name : id;
    }
  return {};
}

bool SceneView::StoreBookmark(EditorModel& model, int slot) {
  std::string target;
  for (auto& [id, b] : Bookmarks(model))
    if (b->slot == slot) target = id;
  auto run = [&](const char* name, const std::string& id, const std::string& args) {
    Command c;
    c.name = name;
    c.target = id;
    c.args = args;
    std::string err;
    if (!model.bus.Execute(c, &err)) { LogWarn("bookmark: {}", err); return false; }
    return true;
  };
  model.bus.BeginGroup(std::format("Store camera bookmark {}", slot), "ui");
  bool ok = true;
  std::string view = std::format(R"({{"slot":{},"pivot":[{},{},{}],"yaw":{},"pitch":{},"distance":{}}})", slot, camera.pivot.x,
                                 camera.pivot.y, camera.pivot.z, camera.yaw, camera.pitch, camera.distance);
  if (target.empty()) { // a new object under "Bookmarks" when the scene has one
    std::string parent;
    for (const std::string& r : model.scene.Roots())
      if (const SceneObject* o = model.scene.Find(r); o && o->name == "Bookmarks") parent = r;
    target = model.scene.NewId();
    ok = run("add", "", std::format(R"({{"name":"Bookmark {}","id":"{}"{}}})", slot, target,
                                    parent.empty() ? "" : std::format(R"(,"parent":"{}")", parent))) &&
         run("addcomp", target, std::format(R"({{"type":"CameraBookmark","value":{}}})", view));
  } else {
    ok = run("set", target, std::format(R"({{"path":"CameraBookmark.pivot","value":[{},{},{}]}})", camera.pivot.x, camera.pivot.y,
                                        camera.pivot.z)) &&
         run("set", target, std::format(R"({{"path":"CameraBookmark.yaw","value":{}}})", camera.yaw)) &&
         run("set", target, std::format(R"({{"path":"CameraBookmark.pitch","value":{}}})", camera.pitch)) &&
         run("set", target, std::format(R"({{"path":"CameraBookmark.distance","value":{}}})", camera.distance));
  }
  model.bus.EndGroup();
  bookmarkNote_ = std::format("{}  ·  stored", slot);
  noteFrames_ = 90;
  return ok;
}

bool SceneView::ChipCenter(const std::string& name, float& x, float& y) const {
  for (const Chip& c : chips_)
    if (c.name == name) { x = c.x + c.w * 0.5f; y = c.y + c.h * 0.5f; return true; }
  return false;
}

void SceneView::ReadPixels(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) { rhi_->ReadRenderTarget(rt_, w, h, rgba); }
void SceneView::ReadGamePixels(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) { rhi_->ReadRenderTarget(gameRT_, w, h, rgba); }

void SceneView::RenderGame(EditorModel& model, const AssetDatabase* assets, const Widget& v, float scale) {
  const int32_t pw = std::max(1, (int32_t)std::lround(v.w * scale)), ph = std::max(1, (int32_t)std::lround(v.h * scale));
  // The first Camera component in hierarchy order
  std::string camId;
  const Camera* cam = nullptr;
  std::vector<std::string> stack(model.scene.Roots().rbegin(), model.scene.Roots().rend());
  while (!stack.empty() && !cam) {
    const SceneObject* o = model.scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    for (auto c = o->children.rbegin(); c != o->children.rend(); ++c) stack.push_back(*c);
    for (const Component& c : o->components)
      if (c.type == &Camera::StaticType()) { cam = static_cast<const Camera*>(c.data); camId = o->id; break; }
  }
  gameCamera_ = cam != nullptr;
  if (!cam) return;
  if (!gameRT_.Valid() || gameW_ != pw || gameH_ != ph) {
    rhi_->DestroyRenderTarget(gameRT_);
    gameRT_ = rhi_->CreateRenderTarget(pw, ph, 1);
    gameW_ = pw;
    gameH_ = ph;
  }
  BuildItems(model, assets); // the Scene view may be hidden behind the Game tab
  const glm::mat4 w = model.scene.WorldMatrix(camId);
  Camera3D c;
  c.position = glm::vec3(w[3]);
  glm::vec3 fwd = -glm::vec3(w[2]), up = glm::vec3(w[1]); // objects look along -Z (Core/Space.h)
  c.target = c.position + (glm::length(fwd) > 1e-6f ? glm::normalize(fwd) : glm::vec3(0, 0, -1));
  c.up = glm::length(up) > 1e-6f ? glm::normalize(up) : glm::vec3(0, 1, 0);
  c.fovY = std::clamp(cam->fov, 1.f, 179.f);
  c.nearZ = std::max(cam->nearClip, 0.001f);
  c.farZ = std::max(cam->farClip, c.nearZ * 2);
  r_->Render(gameRT_, pw, ph, c, light_, items_, {0.17f, 0.19f, 0.22f, 1}, nullptr, &quality);
  ++gameFrames_;
}

void SceneView::DrawGame(Widget& w, UIContext& ui, double playTime) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  if (!gameCamera_ || !gameRT_.Valid()) {
    b.Rect(w.x, w.y, w.w, w.h, t.C("ground"));
    const char* msg = "Game view: the scene has no Camera";
    ui.Text(w.x + (w.w - ui.Measure(12, msg)) * 0.5f, w.y + w.h * 0.5f - 12, 12, t.C("fg-faint"), msg);
    return;
  }
  b.Image(w.x, w.y, w.w, w.h, rhi_->RenderTargetColor(gameRT_), 0, 1, 1, 0);
  const std::string badge = playTime >= 0 ? std::format("▶ Playing  {:.1f} s", playTime) : std::string("Game view (press Play)");
  const float bw = ui.Measure(11, badge) + 18;
  b.Rect(w.x + 10, w.y + 8, bw, 22, WithAlpha(playTime >= 0 ? t.C("accent") : t.C("win"), 0.85f), 11, 1, t.C("line"));
  ui.Text(w.x + 19, w.y + 12, 11, playTime >= 0 ? Color{1, 1, 1, 1} : t.C("fg-dim"), badge);
}

void SceneView::Draw(Widget& w, UIContext& ui, const EditorModel* model) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  if (!rt_.Valid()) { b.Rect(w.x, w.y, w.w, w.h, t.C("sunk")); return; }
  b.Image(w.x, w.y, w.w, w.h, rhi_->RenderTargetColor(rt_), 0, 1, 1, 0);
  b.PushClip(w.x, w.y, w.w, w.h);
  // Light and camera icons (they have no surface to click).
  for (const Icon& ic : icons_) {
    float x, y;
    if (!ProjectUI(ic.pos, x, y)) continue;
    bool sel = model && std::find(model->Selection().begin(), model->Selection().end(), ic.id) != model->Selection().end();
    Color ring = sel ? Color{1.f, 0.58f, 0.12f, 1} : WithAlpha(t.C("line-strong"), 0.9f);
    b.Rect(x - 11, y - 11, 22, 22, WithAlpha(t.C("win"), 0.82f), 11, sel ? 2.f : 1.f, ring);
    if (ic.kind == 0) { // sun: disc and rays
      b.Rect(x - 4, y - 4, 8, 8, {1.f, 0.85f, 0.35f, 1}, 4);
      for (int k = 0; k < 8; ++k) {
        float a = k * 0.785398f;
        b.Line(x + std::cos(a) * 6, y + std::sin(a) * 6, x + std::cos(a) * 8.5f, y + std::sin(a) * 8.5f, 1.4f, {1.f, 0.85f, 0.35f, 1});
      }
    } else if (ic.kind == 1) { // light bulb
      b.Rect(x - 5, y - 6, 10, 10, {1.f, 0.78f, 0.45f, 1}, 5);
      b.Rect(x - 3, y + 4, 6, 3, t.C("fg-dim"), 1);
    } else { // camera body and lens
      b.Rect(x - 7, y - 4, 10, 8, t.C("fg"), 2);
      b.Triangle(x + 2, y - 4, 6, 8, t.C("fg"));
    }
  }
  if (model) { // gizmo of the active tool on the selection
    GizmoMode mode;
    GizmoFrame frame;
    GizmoView gv;
    if (CurrentGizmo(*model, mode, frame, gv))
      DrawGizmo(b, t, mode, frame, gv, drag_ == Drag::Gizmo ? gizmo_.Handle() : hot_);
  }
  // Chips (top-left) and stats (top-right).
  float px = w.x + 10;
  std::string toolChip = !model ? "" : model->tool == "move" ? "Move (W)" : model->tool == "rotate" ? "Rotate (E)"
                       : model->tool == "scale" ? "Scale (R)" : model->tool == "measure" ? "Measure (M)"
                       : model->tool == "blockout" ? "Blockout (B)" : "Select";
  std::string snapChip = snap.always ? std::format("Snap {} m", snap.move) : "Snap: Ctrl";
  const bool several = model && model->Selection().size() >= 2;
  std::string spaceChip = localSpace ? "Local (X)" : "Global (X)";
  std::string tierChip = std::string(TierLabel(quality.tier)) + (quality.lighting == LightingMode::Unlit ? " · Unlit" : "");
  chips_.clear();
  struct ChipDef { std::string name, text; bool on; };
  const ChipDef defs[] = {{"", "Perspective", false}, {"tier", tierChip, false}, {"view", std::string("View: ") + DebugViewName(debugView), debugView != DebugView::Lit},
                          {"wire", "Wire", wireframe}, {"bounds", "Bounds", bounds}, {"stats", "Stats", statsPanel},
                          {probes_.empty() && !hasVolume_ && !lightmapItems_ ? "" : "gi",
                           probes_.empty() && !hasVolume_ && !lightmapItems_ ? ""
                               : std::format("Bake GI ({})", probes_.size() + (hasVolume_ ? 1 : 0) + lightmapItems_),
                           lightmapItems_ > 0 && lightmapKey_ != bakedLightmapKey_},
                          {"", toolChip, false}, {"", spaceChip, false}, {"snap", snapChip, snap.always},
                          {several ? "align" : "", several ? "Align" : "", alignOpen_}};
  const std::string toolIcon = !model ? "" : model->tool == "move" ? "tool-move" : model->tool == "rotate" ? "tool-rotate"
                             : model->tool == "scale" ? "tool-scale" : model->tool == "measure" ? "tool-measure"
                             : model->tool == "blockout" ? "tool-blockout" : model->tool == "view" ? "tool-view" : "tool-select";
  for (const ChipDef& chip : defs) {
    if (chip.text.empty()) continue;
    const std::string icon = chip.text == toolChip ? toolIcon : chip.name == "snap" ? "tool-snap" : ""; // Assets/Icons
    const float iw = !icon.empty() && ui.HasIcon(icon) ? 17.f : 0.f;
    float cw = ui.Measure(11, chip.text) + 18 + iw;
    b.Rect(px, w.y + 8, cw, 22, chip.on ? WithAlpha(t.C("accent"), 0.85f) : WithAlpha(t.C("win"), 0.85f), 11, 1, t.C("line"));
    if (iw > 0) ui.Icon(icon, px + 7, w.y + 11.5f, 15);
    ui.Text(px + 9 + iw, w.y + 12, 11, chip.on ? Color{1, 1, 1, 1} : t.C("fg-dim"), chip.text);
    if (!chip.name.empty() && chip.name != "tier") chips_.push_back({chip.name, px, w.y + 8, cw, 22});
    px += cw + 6;
  }
  if (model && model->tool == "blockout") { // greybox (3-3c): add blocks; handles on the selected block's faces
    float bx2 = w.x + 10, by2 = w.y + (several && alignOpen_ ? 62 : 36);
    for (int s = 0; s < 4; ++s) {
      static const char* labels[] = {"+ Box", "+ Stairs", "+ Ramp", "+ Cylinder"};
      const float cw = ui.Measure(11, labels[s]) + 16;
      b.Rect(bx2, by2, cw, 20, WithAlpha(t.C("win"), 0.85f), 10, 1, t.C("accent"));
      ui.Text(bx2 + 8, by2 + 3, 11, t.C("fg"), labels[s]);
      chips_.push_back({std::format("bo.{}", s), bx2, by2, cw, 20});
      bx2 += cw + 4;
    }
    for (const FaceHandle& fh : FaceHandles(*model)) {
      float hx, hy;
      if (!ProjectUI(fh.center, hx, hy)) continue;
      const bool hot = (drag_ == Drag::Face && face_.face == fh.face) || (drag_ != Drag::Face && hotFace_ == fh.face);
      const Color col = hot ? Color{1.f, 0.85f, 0.2f, 1} : fh.face / 2 == 0 ? t.C("axis-x") : fh.face / 2 == 1 ? t.C("axis-y") : t.C("axis-z");
      b.Rect(hx - 6, hy - 6, 12, 12, col, 2, 1.5f, {0, 0, 0, 0.6f});
    }
  }
  if (several && alignOpen_) { // second row: align / distribute per axis
    float ax2 = w.x + 10;
    for (const char* axis : {"x", "y", "z"})
      for (const char* m : {"min", "mid", "max", "dist"}) {
        const std::string name = std::format("al.{}.{}", axis, m), text = std::format("{} {}", (char)std::toupper(axis[0]), m);
        const float cw = ui.Measure(11, text) + 14;
        b.Rect(ax2, w.y + 36, cw, 20, WithAlpha(t.C("win"), 0.85f), 10, 1, t.C("line"));
        ui.Text(ax2 + 7, w.y + 39, 11, t.C("fg"), text);
        chips_.push_back({name, ax2, w.y + 36, cw, 20});
        ax2 += cw + (m[0] == 'd' ? 12 : 4);
      }
  }
  { // snap target (vertex / surface) and the ruler
    float sx, sy;
    if (hasSnapTarget_ && ProjectUI(snapTarget_, sx, sy)) b.Rect(sx - 5, sy - 5, 10, 10, {0, 0, 0, 0}, 2, 2.f, {1.f, 0.85f, 0.2f, 1});
    float ax3, ay3, bx3, by3;
    if (hasMeasure_ && ProjectUI(measure_.a, ax3, ay3) && ProjectUI(measure_.b, bx3, by3)) {
      b.Line(ax3, ay3, bx3, by3, 2.f, {1.f, 0.85f, 0.2f, 1});
      for (auto [px3, py3] : {std::pair{ax3, ay3}, std::pair{bx3, by3}}) b.Rect(px3 - 4, py3 - 4, 8, 8, {1.f, 0.85f, 0.2f, 1}, 4);
      const glm::vec3 d = measure_.Delta();
      const std::string label = std::format("{:.2f} m  ·  x {:.2f}  y {:.2f}  z {:.2f}", measure_.Length(), d.x, d.y, d.z);
      const float lw = ui.Measure(11, label) + 16;
      b.Rect((ax3 + bx3) * 0.5f - lw * 0.5f, (ay3 + by3) * 0.5f - 26, lw, 20, WithAlpha(t.C("win"), 0.9f), 6, 1, {1.f, 0.85f, 0.2f, 1});
      ui.Text((ax3 + bx3) * 0.5f - lw * 0.5f + 8, (ay3 + by3) * 0.5f - 23, 11, t.C("fg"), label);
    }
  }
  if (bounds) // world-space boxes of every drawn mesh
    for (size_t i = 0; i < info_.size(); ++i) {
      glm::vec3 lo = info_[i].lo, hi = info_[i].hi, c[8];
      for (int k = 0; k < 8; ++k) c[k] = {(k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z};
      float sx[8], sy[8];
      bool ok = true;
      for (int k = 0; k < 8 && ok; ++k) ok = ProjectUI(c[k], sx[k], sy[k]);
      if (!ok) continue;
      bool sel = model && std::find(model->Selection().begin(), model->Selection().end(), info_[i].owner) != model->Selection().end();
      Color col = sel ? Color{1.f, 0.58f, 0.12f, 0.95f} : Color{1.f, 0.9f, 0.3f, 0.55f};
      for (int k = 0; k < 8; ++k)
        for (int bit : {1, 2, 4})
          if (!(k & bit)) b.Line(sx[k], sy[k], sx[k | bit], sy[k | bit], 1.f, col);
    }
  if (statsPanel) { // what the renderer did, and the GPU time of each pass
    const RenderStats& st = r_->Stats();
    std::vector<std::string> lines = {
      std::format("{} · {}x{} · MSAA {}x", TierLabel(quality.tier), st.w, st.h, st.msaa),
      std::format("draws {} · triangles {}", st.draws, st.triangles),
      std::format("lights {} · shadow map {} · casters {}", st.localLights, st.shadowMap, st.shadowCasters),
      std::format("post: tone map {} · FXAA {} · bloom {}", st.toneMap ? "on" : "off", st.fxaa ? "on" : "off", st.bloom ? "on" : "off")};
    double total = 0;
    for (const RenderStats::Pass& p : st.passes) { lines.push_back(std::format("{:<14} {:6.3f} ms", p.name, p.ms)); total += p.ms; }
    lines.push_back(st.passes.empty() ? std::string("GPU timings: waiting for the GPU") : std::format("{:<14} {:6.3f} ms", "GPU total", total));
    float pw = 0;
    for (const std::string& l : lines) pw = std::max(pw, ui.Measure(11, l, true));
    float bx = w.x + 10, by = w.y + 38;
    b.Rect(bx, by, pw + 20, lines.size() * 16.f + 12, WithAlpha(t.C("win"), 0.88f), 6, 1, t.C("line"));
    for (size_t i = 0; i < lines.size(); ++i) ui.Text(bx + 10, by + 6 + i * 16.f, 11, t.C("fg"), lines[i], true);
  }
  if (noteFrames_ > 0) { // the bookmark just jumped to
    --noteFrames_;
    float nw = ui.Measure(13, bookmarkNote_) + 24;
    b.Rect(w.x + (w.w - nw) * 0.5f, w.y + 44, nw, 28, WithAlpha(t.C("win"), 0.9f), 14, 1, t.C("accent"));
    ui.Text(w.x + (w.w - nw) * 0.5f + 12, w.y + 49, 13, t.C("fg"), bookmarkNote_);
  }
  std::string stats = std::format("Draw {}  ·  Tris {}  ·  {:.2f} ms", r_->DrawsLastFrame(), r_->TrianglesLastFrame(), renderMs_);
  float sw = ui.Measure(11, stats) + 18;
  float sy = px + 10 > w.x + w.w - sw - 10 ? w.y + 34 : w.y + 8; // a second row when the chips need the width
  b.Rect(w.x + w.w - sw - 10, sy, sw, 22, WithAlpha(t.C("win"), 0.85f), 11, 1, t.C("line"));
  ui.Text(w.x + w.w - sw - 1, sy + 4, 11, t.C("fg-dim"), stats);
  // Axis widget (bottom-left): world axes turned by the view.
  float ax = w.x + 36, ay = w.y + w.h - 36;
  b.Rect(ax - 26, ay - 26, 52, 52, WithAlpha(t.C("win"), 0.6f), 26);
  glm::mat4 view = camera.ToCamera().View();
  struct Axis { glm::vec3 d; const char* token; const char* name; } axes[3] = {
    {{1, 0, 0}, "axis-x", "X"}, {{0, 1, 0}, "axis-y", "Y"}, {{0, 0, 1}, "axis-z", "Z"}};
  std::sort(std::begin(axes), std::end(axes), [&](const Axis& a, const Axis& c) { // far axes first
    return (view * glm::vec4(a.d, 0)).z < (view * glm::vec4(c.d, 0)).z;
  });
  for (const Axis& a : axes) {
    glm::vec3 s = glm::vec3(view * glm::vec4(a.d, 0));
    float ex = ax + s.x * 18, ey = ay - s.y * 18;
    b.Line(ax, ay, ex, ey, 2, t.C(a.token));
    ui.Text(ex + (s.x >= 0 ? 2 : -9), ey - 7, 10, t.C(a.token), a.name);
  }
  // Controls hint (bottom-right, only while the pointer is over the view).
  if (hover_) {
    const char* hint = "Alt+드래그 궤도 · 휠 확대 · 가운데 드래그 이동 · 우클릭+WASD 비행 · F 맞추기";
    float hw = ui.Measure(11, hint) + 16;
    b.Rect(w.x + w.w - hw - 10, w.y + w.h - 30, hw, 22, WithAlpha(t.C("win"), 0.8f), 6);
    ui.Text(w.x + w.w - hw - 2, w.y + w.h - 26, 11, t.C("fg-dim"), hint);
  }
  b.PopClip();
}
}
