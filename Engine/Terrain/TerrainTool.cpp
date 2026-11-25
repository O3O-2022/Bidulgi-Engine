#include "Terrain/TerrainTool.h"
#include "Core/Command.h"
#include "Core/Log.h"
#include "Render2D/Batch2D.h"
#include "Runtime/EditorModel.h"
#include "Terrain/TerrainComponents.h"
#include "UI/UI.h"
#include "Assets/AssetDatabase.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace bdg {
namespace {
const Terrain* TerrainOf(const SceneObject* o) {
  if (!o) return nullptr;
  for (const Component& c : o->components)
    if (c.type == &Terrain::StaticType()) return static_cast<const Terrain*>(c.data);
  return nullptr;
}
std::string Q(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o + "\"";
}
} // namespace

bool TerrainTool::Raycast(const TerrainData& t, const glm::vec3& origin, const glm::vec3& dirIn, float maxDistance, glm::vec3& hit) {
  const glm::vec3 dir = glm::normalize(dirIn);
  const float step = std::max(std::min(t.SampleSpacingX(), t.SampleSpacingZ()) * 0.5f, 0.05f);
  auto below = [&](const glm::vec3& p) { return t.Contains(p.x, p.z) && p.y <= t.HeightAt(p.x, p.z); };
  glm::vec3 prev = origin;
  for (float d = step; d <= maxDistance; d += step) {
    const glm::vec3 p = origin + dir * d;
    if (below(p)) { // refine between the last point above and this one
      glm::vec3 a = prev, b = p;
      for (int i = 0; i < 20; ++i) {
        const glm::vec3 m = (a + b) * 0.5f;
        (below(m) ? b : a) = m;
      }
      hit = b;
      return true;
    }
    prev = p;
  }
  return false;
}

bool TerrainTool::Pick(const EditorModel& model, const Widget& v, const Camera3D& cam, float x, float y, std::string& object, TerrainData*& data,
                       glm::vec3& local) {
  const float aspect = std::max(v.w, 1.f) / std::max(v.h, 1.f);
  const glm::mat4 inv = glm::inverse(cam.Projection(aspect) * cam.View());
  const float nx = (x - v.x) / std::max(v.w, 1.f) * 2 - 1, ny = 1 - (y - v.y) / std::max(v.h, 1.f) * 2;
  glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
  const glm::vec3 from = glm::vec3(a) / a.w, to = glm::vec3(b) / b.w;
  const glm::vec3 dir = glm::normalize(to - from);
  const std::string assetsRoot = model.assets ? model.assets->Root() : std::string();
  float best = 1e30f;
  std::vector<std::string> order;
  for (const std::string& id : model.Selection()) order.push_back(id); // the selected terrain wins
  std::vector<std::string> stack(model.scene.Roots().rbegin(), model.scene.Roots().rend());
  while (!stack.empty()) {
    const SceneObject* o = model.scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
    order.push_back(o->id);
  }
  bool found = false, selectedHit = false;
  for (size_t k = 0; k < order.size(); ++k) {
    const bool selected = k < model.Selection().size();
    const Terrain* tc = TerrainOf(model.scene.Find(order[k]));
    if (!tc || (found && selectedHit)) continue;
    TerrainData* t = TerrainRegistry::Instance().Get(tc->asset, assetsRoot);
    if (!t) continue;
    const glm::vec3 origin(model.scene.WorldMatrix(order[k])[3]);
    glm::vec3 hit;
    if (!Raycast(*t, from - origin, dir, 5000.f, hit)) continue;
    const float d = glm::length(hit + origin - from);
    if (d < best || (selected && !selectedHit)) {
      best = d;
      object = order[k];
      data = t;
      local = hit;
      found = true;
      selectedHit = selected;
    }
  }
  return found;
}

void TerrainTool::Apply(EditorModel& model, const glm::vec3& local) {
  const Terrain* tc = TerrainOf(model.scene.Find(object_));
  if (!tc || !data_) return;
  static const TerrainBrushMode modes[] = {TerrainBrushMode::Raise, TerrainBrushMode::Lower, TerrainBrushMode::Smooth, TerrainBrushMode::Flatten,
                                           TerrainBrushMode::Paint, TerrainBrushMode::FoliagePaint, TerrainBrushMode::FoliageErase};
  const TerrainBrushMode mode = modes[std::clamp((int)tc->brush, 0, 6)];
  const uint64_t before = data_->revision;
  ApplyTerrainBrush(*data_, mode, {local.x, local.z}, tc->brushRadius, tc->brushStrength * 0.25f, tc->brushLayer, flatten_);
  if (data_->revision == before) return;
  // what the stroke has touched so far (the brush marks it; the views clear it when they rebuild)
  const float sx = data_->SampleSpacingX(), sz = data_->SampleSpacingZ();
  const int bx0 = std::max(0, (int)std::floor((local.x - tc->brushRadius) / sx)), bx1 = std::min(data_->resolution - 1, (int)std::ceil((local.x + tc->brushRadius) / sx));
  const int bz0 = std::max(0, (int)std::floor((local.z - tc->brushRadius) / sz)), bz1 = std::min(data_->resolution - 1, (int)std::ceil((local.z + tc->brushRadius) / sz));
  if (x1_ < x0_) { x0_ = bx0; z0_ = bz0; x1_ = bx1; z1_ = bz1; }
  else { x0_ = std::min(x0_, bx0); z0_ = std::min(z0_, bz0); x1_ = std::max(x1_, bx1); z1_ = std::max(z1_, bz1); }
}

void TerrainTool::Finish(EditorModel& model) {
  stroking_ = false;
  if (!data_ || x1_ < x0_) return;
  const Terrain* tc = TerrainOf(model.scene.Find(object_));
  const TerrainBrush brush = tc ? tc->brush : TerrainBrush::Raise;
  const int w = x1_ - x0_ + 1, h = z1_ - z0_ + 1, res = data_->resolution;
  // the maps the brush changes: put the stroke's start back, then let the command do it (it records the undo)
  std::vector<std::pair<std::string, std::string>> changes; // map, data after
  auto take = [&](const std::string& map, auto& now, const auto& before, int bytes) {
    changes.push_back({map, TerrainRegionBase64(*data_, map, x0_, z0_, w, h)});
    for (int z = z0_; z <= z1_; ++z)
      std::copy_n(reinterpret_cast<const uint8_t*>(before.data()) + ((size_t)z * res + x0_) * bytes, (size_t)w * bytes,
                  reinterpret_cast<uint8_t*>(now.data()) + ((size_t)z * res + x0_) * bytes);
  };
  if (brush == TerrainBrush::Paint) {
    for (size_t m = 0; m < data_->splat.size() && m < splat0_.size(); ++m) take(std::format("splat{}", m), data_->splat[m], splat0_[m], 4);
  } else if (brush == TerrainBrush::PaintFoliage || brush == TerrainBrush::EraseFoliage) {
    take("density", data_->density, density0_, 4);
  } else {
    take("height", data_->heights, heights0_, 2);
  }
  RegisterTerrainCommand();
  const std::string asset = tc ? tc->asset : std::string();
  model.bus.BeginGroup(changes.size() > 1 ? "Paint terrain" : "Edit terrain", "ui");
  for (const auto& [map, data] : changes) {
    Command c;
    c.name = "terrain";
    c.target = object_;
    c.args = std::format(R"({{"asset":{},"map":"{}","x":{},"z":{},"w":{},"h":{},"data":"{}"}})", Q(asset), map, x0_, z0_, w, h, data);
    std::string err;
    if (!model.bus.Execute(c, &err)) LogWarn("terrain: {}", err);
  }
  model.bus.EndGroup();
  ++strokes;
  LogInfo("terrain: stroke {} on {} ({}x{} samples)", strokes, object_, w, h);
}

bool TerrainTool::Handle(const InputEvent& e, const Widget& v, const Camera3D& cam, EditorModel& model) {
  using T = InputEvent::Type;
  const bool inside = e.x >= v.x && e.y >= v.y && e.x < v.x + v.w && e.y < v.y + v.h;
  if (e.type == T::MouseMove || e.type == T::MouseDown) {
    std::string id;
    TerrainData* t = nullptr;
    glm::vec3 local(0);
    hover_ = inside && Pick(model, v, cam, e.x, e.y, id, t, local);
    if (hover_) {
      const Terrain* tc = TerrainOf(model.scene.Find(id));
      hoverWorld_ = local + glm::vec3(model.scene.WorldMatrix(id)[3]);
      hoverRadius_ = tc ? tc->brushRadius : 1.f;
    }
    if (e.type == T::MouseDown && e.button == 1 && !e.alt && hover_ && !stroking_) {
      stroking_ = true;
      object_ = id;
      data_ = t;
      heights0_ = t->heights;
      splat0_ = t->splat;
      density0_ = t->density;
      x0_ = z0_ = 0;
      x1_ = z1_ = -1;
      flatten_ = t->HeightAt(local.x, local.z);
      Apply(model, local);
      return true;
    }
    if (e.type == T::MouseMove && stroking_) {
      if (hover_ && id == object_) Apply(model, local);
      return true;
    }
    return false;
  }
  if (e.type == T::MouseUp && e.button == 1 && stroking_) {
    Finish(model);
    return true;
  }
  return false;
}

void TerrainTool::Draw(Batch2D& b, const EditorModel& model, const std::function<bool(const glm::vec3&, float&, float&)>& project) const {
  (void)model;
  if (!hover_) return;
  float px = 0, py = 0;
  bool have = false;
  const Color c{1, 0.85f, 0.3f, 0.9f};
  for (int i = 0; i <= 48; ++i) {
    const float a = i / 48.f * 6.2831853f;
    const glm::vec3 p = hoverWorld_ + glm::vec3(std::cos(a), 0, std::sin(a)) * hoverRadius_;
    float x, y;
    const bool ok = project(p, x, y);
    if (ok && have) b.Line(px, py, x, y, 2.f, c);
    px = x;
    py = y;
    have = ok;
  }
}
} // namespace bdg
