// The terrain's collider (round 6): a static Jolt height field made from the .terrain heights, added by
// PhysicsWorld::Begin for every object with a Terrain component (collider on).
#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Scene.h"
#include "Terrain/TerrainComponents.h"
#include "Terrain/TerrainData.h"
#include <glm/gtc/quaternion.hpp>

namespace bdg {

bool PhysicsWorld::MakeTerrainBody(Scene& scene, const std::string& id) {
  SceneObject* o = scene.Find(id);
  Component* c = o ? o->Find("Terrain") : nullptr;
  const Terrain* tc = c ? static_cast<const Terrain*>(c->data) : nullptr;
  if (!tc || !tc->collider || !jolt_) return false;
  std::string err;
  const TerrainData* t = TerrainRegistry::Instance().Get(tc->asset, assets_ ? assets_->Root() : std::string(), &err);
  if (!t) {
    LogWarn("physics: {} ({}): no terrain collider: {}", o->name, id, err);
    return false;
  }
  const glm::mat4 world = scene.WorldMatrix(id);
  Body b;
  b.object = id;
  b.motion = phys::Motion::Static;
  b.lastPos = glm::vec3(world[3]);
  glm::mat3 basis(world);
  for (int k = 0; k < 3; ++k)
    if (glm::length(basis[k]) > 1e-8f) basis[k] = glm::normalize(basis[k]);
  b.lastRot = glm::quat_cast(basis); // the terrain ignores the object's rotation: compared as is, so it is not teleported every step
  phys::BodyDesc d;
  d.user = bodies_.size() + 1;
  d.motion = phys::Motion::Static;
  d.position = b.lastPos;
  d.friction = 0.8f;
  d.shape.kind = phys::ShapeKind::HeightField;
  d.shape.samples = (uint32_t)t->resolution;
  d.shape.fieldScale = {t->SampleSpacingX(), 1.f, t->SampleSpacingZ()};
  d.shape.heights.resize(t->heights.size());
  for (size_t i = 0; i < t->heights.size(); ++i) d.shape.heights[i] = t->heights[i] * (t->height / 65535.f);
  b.id = jolt_->Add(d, &err);
  bodies_.push_back(b);
  if (b.id == phys::kNoBody) {
    LogWarn("physics: {} ({}): no terrain collider: {}", o->name, id, err);
    return false;
  }
  LogInfo("physics: terrain {} \"{}\": height field {}x{} samples, {:.0f} x {:.0f} m", id, o->name, t->resolution, t->resolution, t->size.x, t->size.y);
  return true;
}

} // namespace bdg
