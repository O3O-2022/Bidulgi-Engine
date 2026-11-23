#include "Runtime/SceneView.h"
#include "Assets/AssetDatabase.h"
#include "Assets/GltfImport.h"
#include "Core/Log.h"
#include "Core/Space.h"
#include "Render2D/Batch2D.h"
#include "Runtime/EditorModel.h"
#include "Scene/Components.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <chrono>
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

Material3D SceneView::MaterialFor(const std::string& name) const {
  Material3D m;
  std::string n = Lower(name);
  auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
  if (has("ground")) { m.baseColor = {0.26f, 0.27f, 0.28f, 1}; m.roughness = 0.95f; }
  else if (has("gold")) { m.baseColor = {1.f, 0.766f, 0.336f, 1}; m.metallic = 1; m.roughness = 0.3f; }
  else if (has("steel") || has("metal")) { m.baseColor = {0.56f, 0.57f, 0.58f, 1}; m.metallic = 1; m.roughness = 0.35f; }
  else if (has("copper")) { m.baseColor = {0.955f, 0.638f, 0.538f, 1}; m.metallic = 1; m.roughness = 0.3f; }
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
  light_ = {};
  light_.toLight = {-0.35f, 0.85f, 0.4f};
  light_.intensity = 2.6f;
  bool sun = false;
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
        if (directional && !sun) {
          sun = true;
          light_.toLight = -glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
          light_.color = l.color;
          light_.intensity = l.intensity;
        }
        icons_.push_back({id, glm::vec3(world[3]), directional ? 0 : 1});
      }
      if (c.type == &Camera::StaticType()) icons_.push_back({id, glm::vec3(world[3]), 2});
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
        info_.push_back({id, &e->cpuPrims[in.mesh]});
      }
    } else {
      if (sel) extras_.outlined.push_back(items_.size());
      items_.push_back({&e->mesh, world, MaterialFor(mr->material)});
      info_.push_back({id, &e->cpu});
    }
  }
}

void SceneView::Render(EditorModel& model, const AssetDatabase* assets, const Widget& v, float scale) {
  vx_ = v.x; vy_ = v.y; vw_ = std::max(v.w, 1.f); vh_ = std::max(v.h, 1.f);
  int32_t pw = std::max(1, (int32_t)std::lround(v.w * scale)), ph = std::max(1, (int32_t)std::lround(v.h * scale));
  if (!rt_.Valid() || pw != pw_ || ph != ph_) {
    rhi_->DestroyRenderTarget(rt_);
    rt_ = rhi_->CreateRenderTarget(pw, ph, 4);
    pw_ = pw;
    ph_ = ph;
  }
  auto t0 = std::chrono::steady_clock::now();
  BuildItems(model, assets);
  r_->Render(rt_, pw_, ph_, camera.ToCamera(), light_, items_, {0.17f, 0.19f, 0.22f, 1}, &extras_);
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
        if (drag_ == Drag::None) break;
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
  // Chips (top-left) and stats (top-right).
  float px = w.x + 10;
  for (const char* chip : {"Perspective", "Lit", "Grid"}) {
    float cw = ui.Measure(11, chip) + 18;
    b.Rect(px, w.y + 8, cw, 22, WithAlpha(t.C("win"), 0.85f), 11, 1, t.C("line"));
    ui.Text(px + 9, w.y + 12, 11, t.C("fg-dim"), chip);
    px += cw + 6;
  }
  std::string stats = std::format("Draw {}  ·  Tris {}  ·  {:.2f} ms", r_->DrawsLastFrame(), r_->TrianglesLastFrame(), renderMs_);
  float sw = ui.Measure(11, stats) + 18;
  b.Rect(w.x + w.w - sw - 10, w.y + 8, sw, 22, WithAlpha(t.C("win"), 0.85f), 11, 1, t.C("line"));
  ui.Text(w.x + w.w - sw - 1, w.y + 12, 11, t.C("fg-dim"), stats);
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
