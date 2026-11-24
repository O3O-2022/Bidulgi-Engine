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
  return true;
}

void SceneView::Shutdown() {
  if (!r_) return;
  for (auto& [name, e] : builtins_) r_->DestroyMesh(e.mesh);
  for (auto& [name, e] : meshes_) r_->DestroyModel(e.model);
  builtins_.clear();
  meshes_.clear();
  rhi_->DestroyRenderTarget(rt_);
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
    for (const Component& c : o->components) {
      if (c.type == &MeshRenderer::StaticType()) mr = static_cast<const MeshRenderer*>(c.data);
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
        bool any = false;
        Bounds(e->cpuPrims[in.mesh], items_.back().model, info_.back().lo, info_.back().hi, any);
      }
    } else {
      if (sel) extras_.outlined.push_back(items_.size());
      items_.push_back({&e->mesh, world, MaterialFor(mr->material)});
      items_.back().lightmapped = mr->bakeLightmap;
      info_.push_back({id, &e->cpu});
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
          ApplyGizmo(model, gizmo_.Update({e.x, e.y}, e.ctrl));
          used = true;
          break;
        }
        if (drag_ == Drag::None) {
          GizmoMode mode;
          GizmoFrame frame;
          GizmoView gv;
          hot_ = hover_ && CurrentGizmo(model, mode, frame, gv) ? GizmoHit(mode, frame, gv, {e.x, e.y}) : GizmoHandle::None;
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
                       : model->tool == "scale" ? "Scale (R)" : "Select";
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
                          {"", toolChip, false}, {"", spaceChip, false}};
  for (const ChipDef& chip : defs) {
    if (chip.text.empty()) continue;
    float cw = ui.Measure(11, chip.text) + 18;
    b.Rect(px, w.y + 8, cw, 22, chip.on ? WithAlpha(t.C("accent"), 0.85f) : WithAlpha(t.C("win"), 0.85f), 11, 1, t.C("line"));
    ui.Text(px + 9, w.y + 12, 11, chip.on ? Color{1, 1, 1, 1} : t.C("fg-dim"), chip.text);
    if (!chip.name.empty() && chip.name != "tier") chips_.push_back({chip.name, px, w.y + 8, cw, 22});
    px += cw + 6;
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
