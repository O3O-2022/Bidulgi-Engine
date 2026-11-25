#include "Physics/PhysicsWorld.h"
#include "Assets/AssetDatabase.h"
#include "Assets/GltfImport.h"
#include "Assets/ModelImport.h"
#include "Core/Log.h"
#include "Core/Space.h"
#include "Render3D/MeshGen.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg {
namespace fs = std::filesystem;
namespace {
template <class T> T* Comp(SceneObject* o, const char* name) {
  Component* c = o ? o->Find(name) : nullptr;
  return c ? static_cast<T*>(c->data) : nullptr;
}
glm::vec3 V(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 V(const glm::vec3& v) { return {v.x, v.y, v.z}; }

void Decompose(const glm::mat4& m, glm::vec3& pos, glm::quat& rot, glm::vec3& scale) {
  pos = glm::vec3(m[3]);
  scale = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
  const glm::mat3 r(glm::vec3(m[0]) / std::max(scale.x, 1e-8f), glm::vec3(m[1]) / std::max(scale.y, 1e-8f), glm::vec3(m[2]) / std::max(scale.z, 1e-8f));
  rot = glm::normalize(glm::quat_cast(r));
}

// What an object draws, for Collider Auto: a built-in shape (exact colliders) or the mesh itself.
struct Drawn {
  std::string kind; // "cube", "sphere", "cylinder", "plane", "cone", "block-box", "block-cylinder", "block-stairs", "block-ramp", "model", ""
  MeshData mesh;    // local (object) space, before the object's scale
  glm::vec3 blockSize{1};
};
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
Drawn DrawnOf(SceneObject* o, const AssetDatabase* assets) {
  Drawn d;
  if (const Blockout* b = Comp<Blockout>(o, "Blockout")) {
    static const char* kinds[] = {"block-box", "block-stairs", "block-ramp", "block-cylinder"};
    d.kind = kinds[std::clamp((int)b->shape, 0, 3)];
    d.blockSize = V(b->size);
    d.mesh = MakeBlock((BlockShape)b->shape, b->size.x, b->size.y, b->size.z, b->steps);
    return d;
  }
  const MeshRenderer* mr = Comp<MeshRenderer>(o, "MeshRenderer");
  if (!mr || mr->mesh.empty()) return d;
  const std::string ext = Lower(fs::path(mr->mesh).extension().string());
  const AssetRecord* rec = assets && (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") ? assets->ByPath(mr->mesh) : nullptr;
  if (rec) {
    ImportedModel im;
    std::string err;
    const fs::path full = fs::path(std::u8string((const char8_t*)assets->Root().c_str())) / fs::path(std::u8string((const char8_t*)mr->mesh.c_str()));
    if (ImportModel(full, im, &err)) {
      d.kind = "model";
      for (const ImportedModel::Instance& in : im.instances) { // every placed primitive, in model space
        const MeshData& p = im.primitives[in.primitive].mesh;
        const uint32_t base = (uint32_t)d.mesh.vertices.size();
        for (Vertex3D v : p.vertices) {
          const glm::vec3 w = glm::vec3(in.world * glm::vec4(v.px, v.py, v.pz, 1));
          v.px = w.x; v.py = w.y; v.pz = w.z;
          d.mesh.vertices.push_back(v);
        }
        for (uint32_t i : p.indices) d.mesh.indices.push_back(base + i);
      }
      return d;
    }
    LogWarn("physics: {} did not import for its collider ({})", mr->mesh, err);
    return d;
  }
  const std::string stem = Lower(fs::path(mr->mesh).stem().string());
  auto centred = [](MeshData m) { for (Vertex3D& v : m.vertices) v.py -= 0.5f; return m; };
  if (stem == "cube") { d.kind = "cube"; d.mesh = MakeBox(1, 1, 1); }
  else if (stem == "sphere") { d.kind = "sphere"; d.mesh = MakeSphere(0.5f); }
  else if (stem == "plane") { d.kind = "plane"; d.mesh = MakePlaneXZ(1, 1); }
  else if (stem == "cylinder") { d.kind = "cylinder"; d.mesh = centred(MakeCylinder(0.5f, 1)); }
  else if (stem == "cone") { d.kind = "cone"; d.mesh = centred(MakeCone(0.5f, 1)); }
  else { d.kind = "cube"; d.mesh = MakeBox(1, 1, 1); } // the placeholder the view draws for a missing mesh
  return d;
}

// Closed-mesh volume (divergence theorem) and surface area, scaled.
void VolumeArea(const MeshData& m, const glm::vec3& s, double& volume, double& area) {
  volume = area = 0;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    auto p = [&](uint32_t k) { const Vertex3D& v = m.vertices[k]; return glm::dvec3(v.px * s.x, v.py * s.y, v.pz * s.z); };
    const glm::dvec3 a = p(m.indices[i]), b = p(m.indices[i + 1]), c = p(m.indices[i + 2]);
    volume += glm::dot(a, glm::cross(b, c)) / 6.0;
    area += glm::length(glm::cross(b - a, c - a)) * 0.5;
  }
  volume = std::abs(volume);
}

void BoundsOf(const MeshData& m, const glm::vec3& s, glm::vec3& lo, glm::vec3& hi) {
  lo = glm::vec3(1e30f);
  hi = glm::vec3(-1e30f);
  for (const Vertex3D& v : m.vertices) {
    const glm::vec3 p(v.px * s.x, v.py * s.y, v.pz * s.z);
    lo = glm::min(lo, p);
    hi = glm::max(hi, p);
  }
  if (m.vertices.empty()) lo = hi = glm::vec3(0);
}

bool Near(float a, float b) { return std::abs(a - b) <= 1e-4f * std::max({1.f, std::abs(a), std::abs(b)}); }
uint64_t Fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}
}

void PhysicsWorld::Begin(Scene& scene, const Settings& settings, const AssetDatabase* assets) {
  End();
  assets_ = assets;
  jolt_ = phys::CreateJolt();
  box2d_ = phys::CreateBox2D();
  jolt_->SetGravity(settings.gravity);
  box2d_->SetGravity({settings.gravity2D.x, settings.gravity2D.y, 0});
  active_ = true;
  // hierarchy order (parents first, children in their order): the same bodies in the same order every run
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
    if (o->Find("Rigidbody") || o->Find("Collider")) MakeBody(scene, o->id, false);
    if (o->Find("Rigidbody2D") || o->Find("Collider2D")) MakeBody(scene, o->id, true);
  }
  LogInfo("physics: {}", Describe());
}

bool PhysicsWorld::MakeBody(Scene& scene, const std::string& id, bool is2D) {
  SceneObject* o = scene.Find(id);
  Body b;
  b.object = id;
  b.is2D = is2D;
  glm::vec3 pos, scale;
  glm::quat rot;
  Decompose(scene.WorldMatrix(id), pos, rot, scale);
  b.scale = scale;
  phys::BodyDesc d;
  d.user = bodies_.size() + 1;
  d.position = pos;
  d.rotation = rot;
  if (const PhysicsMaterial* pm = Comp<PhysicsMaterial>(o, "PhysicsMaterial")) {
    d.friction = pm->friction;
    d.restitution = pm->bounciness;
  }
  auto motionOf = [](RigidbodyType t) {
    return t == RigidbodyType::Static ? phys::Motion::Static : t == RigidbodyType::Kinematic ? phys::Motion::Kinematic : phys::Motion::Dynamic;
  };
  const Drawn drawn = DrawnOf(o, assets_);
  std::string method;
  bool autoShape = false, hull = false;
  std::string error;
  if (!is2D) {
    const Rigidbody* rb = Comp<Rigidbody>(o, "Rigidbody");
    const Collider* col = Comp<Collider>(o, "Collider");
    const RigidbodyType type = rb ? rb->type : RigidbodyType::Static;
    d.motion = rb ? motionOf(type) : phys::Motion::Static;
    if (rb) {
      if (rb->simulation == PhysicsSimulation::GPU) LogWarn("physics: {}: GPU simulation comes later (L track); using Jolt", o->name);
      d.mass = rb->mass;
      d.linearDamping = rb->linearDamping;
      d.angularDamping = rb->angularDamping;
      d.gravityScale = rb->gravityScale;
      d.freezeRotation = rb->freezeRotation || type == RigidbodyType::Character;
      d.velocity = V(rb->velocity);
      d.angularVelocity = glm::radians(V(rb->angularVelocity));
      b.lastVel = d.velocity;
      b.lastAng = V(rb->angularVelocity);
    }
    d.trigger = col && col->isTrigger;
    const glm::vec3 center = col ? V(col->center) * scale : glm::vec3(0);
    phys::ShapeDesc& s = d.shape;
    s.offset = center;
    const ColliderShape shape = col ? col->shape : ColliderShape::Auto;
    const float maxS = std::max({scale.x, scale.y, scale.z});
    auto fromMesh = [&](bool asMesh) {
      for (const Vertex3D& v : drawn.mesh.vertices) s.points.push_back(glm::vec3(v.px, v.py, v.pz) * scale + center);
      if (asMesh) s.indices = drawn.mesh.indices;
      s.kind = asMesh ? phys::ShapeKind::Mesh : phys::ShapeKind::ConvexHull;
    };
    if (shape == ColliderShape::Box) {
      s.kind = phys::ShapeKind::Box;
      s.halfExtents = V(col->size) * 0.5f * scale;
    } else if (shape == ColliderShape::Sphere) {
      s.kind = phys::ShapeKind::Sphere;
      s.radius = col->radius * maxS;
    } else if (shape == ColliderShape::Capsule) {
      s.kind = phys::ShapeKind::Capsule;
      s.radius = col->radius * std::max(scale.x, scale.z);
      s.halfHeight = std::max(col->height * scale.y * 0.5f - s.radius, 0.001f);
    } else if (shape == ColliderShape::Mesh && !drawn.mesh.vertices.empty()) {
      fromMesh(d.motion == phys::Motion::Static);
      if (d.motion != phys::Motion::Static) LogWarn("physics: {}: a moving body cannot use its triangle mesh; using the convex hull", o->name);
    } else { // Auto (or Mesh with nothing drawn)
      autoShape = true;
      const bool uniformXZ = Near(scale.x, scale.z), uniform = uniformXZ && Near(scale.x, scale.y);
      if (type == RigidbodyType::Character && rb) { // an upright capsule around what is drawn
        glm::vec3 lo(-0.5f, 0, -0.5f), hi(0.5f, 2, 0.5f);
        if (!drawn.mesh.vertices.empty()) BoundsOf(drawn.mesh, scale, lo, hi);
        s.kind = phys::ShapeKind::Capsule;
        s.radius = std::max(0.5f * std::max(hi.x - lo.x, hi.z - lo.z), 0.01f);
        s.halfHeight = std::max(0.5f * (hi.y - lo.y) - s.radius, 0.001f);
        s.offset = center + 0.5f * (lo + hi);
        method = "capsule (character)";
      } else if (drawn.kind == "cube" || drawn.kind == "block-box" || drawn.kind == "plane") {
        s.kind = phys::ShapeKind::Box;
        const glm::vec3 size = drawn.kind == "block-box" ? drawn.blockSize : drawn.kind == "plane" ? glm::vec3(1, 0.02f, 1) : glm::vec3(1);
        s.halfExtents = size * 0.5f * scale;
        if (drawn.kind == "block-box") s.offset = center + glm::vec3(0, size.y * 0.5f * scale.y, 0);
        if (drawn.kind == "plane") s.offset = center - glm::vec3(0, 0.01f * scale.y, 0); // the top face is the plane
        method = "box (exact)";
      } else if (drawn.kind == "sphere" && uniform) {
        s.kind = phys::ShapeKind::Sphere;
        s.radius = 0.5f * scale.x;
        method = "sphere (exact)";
      } else if ((drawn.kind == "cylinder" || drawn.kind == "block-cylinder") && uniformXZ) {
        s.kind = phys::ShapeKind::Cylinder;
        const glm::vec3 size = drawn.kind == "block-cylinder" ? drawn.blockSize : glm::vec3(1);
        s.radius = 0.5f * size.x * scale.x;
        s.halfHeight = 0.5f * size.y * scale.y;
        if (drawn.kind == "block-cylinder") s.offset = center + glm::vec3(0, s.halfHeight, 0);
        method = "cylinder (exact)";
      } else if (!drawn.mesh.vertices.empty()) {
        fromMesh(d.motion == phys::Motion::Static);
        hull = d.motion != phys::Motion::Static;
        method = hull ? std::format("convex hull ({} points)", s.points.size()) : std::format("triangle mesh ({} triangles)", s.indices.size() / 3);
      } else {
        s.kind = phys::ShapeKind::Box;
        s.halfExtents = 0.5f * scale;
        method = "box (nothing drawn)";
      }
    }
    b.id = jolt_->Add(d, &error);
  } else {
    const Rigidbody2D* rb = Comp<Rigidbody2D>(o, "Rigidbody2D");
    const Collider2D* col = Comp<Collider2D>(o, "Collider2D");
    d.motion = rb ? motionOf(rb->type) : phys::Motion::Static;
    const float angle = glm::radians(space::QuatToEulerDegrees(rot).z);
    d.rotation = glm::angleAxis(angle, glm::vec3(0, 0, 1));
    if (rb) {
      d.mass = rb->mass;
      d.linearDamping = rb->linearDamping;
      d.angularDamping = rb->angularDamping;
      d.gravityScale = rb->gravityScale;
      d.freezeRotation = rb->freezeRotation || rb->type == RigidbodyType::Character;
      d.velocity = {rb->velocity.x, rb->velocity.y, 0};
      d.angularVelocity = {0, 0, glm::radians(rb->angularVelocity)};
      b.lastVel = d.velocity;
      b.lastAng = {0, 0, rb->angularVelocity};
    }
    d.trigger = col && col->isTrigger;
    phys::ShapeDesc& s = d.shape;
    s.offset = col ? glm::vec3(col->center.x * scale.x, col->center.y * scale.y, 0) : glm::vec3(0);
    const Collider2DShape shape = col ? col->shape : Collider2DShape::Auto;
    const float maxS = std::max(scale.x, scale.y);
    if (shape == Collider2DShape::Box) {
      s.kind = phys::ShapeKind::Box2D;
      s.halfExtents = {col->size.x * 0.5f * scale.x, col->size.y * 0.5f * scale.y, 0};
    } else if (shape == Collider2DShape::Circle) {
      s.kind = phys::ShapeKind::Circle2D;
      s.radius = col->radius * maxS;
    } else if (shape == Collider2DShape::Capsule) {
      s.kind = phys::ShapeKind::Capsule2D;
      s.radius = col->radius * scale.x;
      s.halfHeight = std::max(col->height * scale.y * 0.5f - s.radius, 0.001f);
    } else {
      autoShape = true;
      if (drawn.kind == "sphere" || drawn.kind == "cylinder" || drawn.kind == "block-cylinder") {
        s.kind = phys::ShapeKind::Circle2D;
        s.radius = 0.5f * maxS * (drawn.kind == "block-cylinder" ? drawn.blockSize.x : 1.f);
        method = "circle";
      } else {
        glm::vec3 lo(-0.5f), hi(0.5f);
        if (!drawn.mesh.vertices.empty()) BoundsOf(drawn.mesh, scale, lo, hi);
        s.kind = phys::ShapeKind::Box2D;
        s.halfExtents = 0.5f * (hi - lo);
        s.offset += glm::vec3(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0);
        method = "box around the drawn shape";
      }
    }
    b.id = box2d_->Add(d, &error);
  }
  if (b.id == phys::kNoBody) {
    LogWarn("physics: {} ({}): no body: {}", o->name, id, error);
    bodies_.push_back(b); // keeps user numbers in step with the list
    return false;
  }
  b.motion = d.motion;
  b.lastPos = d.position;
  b.lastRot = d.rotation;
  if (autoShape) {
    AutoChoice a{id, o->name, method, 0.f};
    if (hull) { // average gap: the hull's extra volume spread over the drawn surface
      double vol = 0, area = 0;
      VolumeArea(drawn.mesh, b.scale, vol, area);
      const double hv = jolt_->Volume(b.id);
      a.error = area > 0 ? (float)std::max((hv - vol) / area, 0.0) : 0.f;
    }
    LogInfo("physics: collider auto {} \"{}\": {}{}", id, o->name, method, hull ? std::format(", error {:.1f} cm", a.error * 100) : "");
    auto_.push_back(a);
  }
  bodies_.push_back(b);
  return true;
}

void PhysicsWorld::Step(Scene& scene, float dt) {
  if (!active_) return;
  events_.clear();
  // 1. edits since the last step: moved objects, changed velocities, pushes from scripts, removed objects
  for (Body& b : bodies_) {
    if (b.id == phys::kNoBody) continue;
    phys::Backend& be = b.is2D ? *box2d_ : *jolt_;
    SceneObject* o = scene.Find(b.object);
    if (!o) { // destroyed while playing
      be.Remove(b.id);
      b.id = phys::kNoBody;
      continue;
    }
    glm::vec3 pos, scale;
    glm::quat rot;
    Decompose(scene.WorldMatrix(b.object), pos, rot, scale);
    if (b.is2D) {
      rot = glm::angleAxis(glm::radians(space::QuatToEulerDegrees(rot).z), glm::vec3(0, 0, 1));
      pos.z = 0;
    }
    const bool moved = glm::length(pos - b.lastPos) > 1e-5f || std::abs(glm::dot(rot, b.lastRot)) < 1 - 1e-7f;
    if (b.motion == phys::Motion::Kinematic) be.MoveKinematic(b.id, pos, rot, dt);
    else if (moved) be.Teleport(b.id, pos, rot);
    if (b.motion != phys::Motion::Dynamic) { b.lastPos = pos; b.lastRot = rot; }
    if (!b.is2D) {
      if (Rigidbody* rb = Comp<Rigidbody>(o, "Rigidbody")) {
        if (V(rb->velocity) != b.lastVel || V(rb->angularVelocity) != b.lastAng)
          be.SetVelocity(b.id, V(rb->velocity), glm::radians(V(rb->angularVelocity)));
        be.Push(b.id, V(rb->pendingForce), V(rb->pendingTorque), V(rb->pendingImpulse));
        rb->pendingForce = rb->pendingTorque = rb->pendingImpulse = Vec3{};
      }
    } else if (Rigidbody2D* rb = Comp<Rigidbody2D>(o, "Rigidbody2D")) {
      if (glm::vec3(rb->velocity.x, rb->velocity.y, 0) != b.lastVel || rb->angularVelocity != b.lastAng.z)
        be.SetVelocity(b.id, {rb->velocity.x, rb->velocity.y, 0}, {0, 0, glm::radians(rb->angularVelocity)});
      be.Push(b.id, {rb->pendingForce.x, rb->pendingForce.y, 0}, {}, {rb->pendingImpulse.x, rb->pendingImpulse.y, 0});
      rb->pendingForce = rb->pendingImpulse = Vec2{};
    }
  }
  // 2. simulate
  jolt_->Step(dt);
  box2d_->Step(dt);
  ++steps_;
  // 3. results back into the scene
  for (Body& b : bodies_)
    if (b.id != phys::kNoBody && b.motion == phys::Motion::Dynamic) WriteBack(scene, b);
  // 4. events, to both objects of each pair
  for (const phys::Backend* be : {static_cast<const phys::Backend*>(jolt_.get()), static_cast<const phys::Backend*>(box2d_.get())})
    for (const phys::Contact& c : be->Contacts()) {
      if (c.a == 0 || c.b == 0 || c.a > bodies_.size() || c.b > bodies_.size()) continue;
      const std::string& a = bodies_[c.a - 1].object;
      const std::string& bo = bodies_[c.b - 1].object;
      const EventKind k = c.trigger ? (c.begin ? EventKind::TriggerEnter : EventKind::TriggerExit)
                                    : (c.begin ? EventKind::CollisionEnter : EventKind::CollisionExit);
      events_.push_back({a, bo, k, c.point, -c.normal});
      events_.push_back({bo, a, k, c.point, c.normal});
    }
}

void PhysicsWorld::WriteBack(Scene& scene, Body& b) {
  SceneObject* o = scene.Find(b.object);
  Transform* t = Comp<Transform>(o, "Transform");
  if (!t) return;
  const phys::Backend& be = b.is2D ? *box2d_ : *jolt_;
  const phys::BodyState st = be.Get(b.id);
  glm::vec3 pos = st.position;
  glm::quat rot = st.rotation;
  if (b.is2D) { // keep z and the tilt around X / Y; the body moves in the XY plane and turns around Z
    glm::vec3 p0, s0;
    glm::quat r0;
    Decompose(scene.WorldMatrix(b.object), p0, r0, s0);
    pos.z = p0.z;
    Vec3 e = space::QuatToEulerDegrees(r0);
    e.z = glm::degrees(2.f * std::atan2(st.rotation.z, st.rotation.w));
    rot = space::EulerDegreesToQuat(e);
  }
  glm::mat4 world = glm::translate(glm::mat4(1.f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.f), b.scale);
  if (!o->parent.empty()) world = glm::inverse(scene.WorldMatrix(o->parent)) * world;
  glm::vec3 lp, ls;
  glm::quat lr;
  Decompose(world, lp, lr, ls);
  t->position = V(lp);
  t->rotation = space::QuatToEulerDegrees(lr);
  b.lastPos = b.is2D ? glm::vec3(st.position.x, st.position.y, 0) : st.position;
  b.lastRot = b.is2D ? glm::angleAxis(2.f * std::atan2(st.rotation.z, st.rotation.w), glm::vec3(0, 0, 1)) : st.rotation;
  if (!b.is2D) {
    if (Rigidbody* rb = Comp<Rigidbody>(o, "Rigidbody")) {
      rb->velocity = V(st.velocity);
      rb->angularVelocity = V(glm::degrees(st.angularVelocity));
      b.lastVel = st.velocity;
      b.lastAng = V(rb->angularVelocity);
    }
  } else if (Rigidbody2D* rb = Comp<Rigidbody2D>(o, "Rigidbody2D")) {
    rb->velocity = {st.velocity.x, st.velocity.y};
    rb->angularVelocity = glm::degrees(st.angularVelocity.z);
    b.lastVel = {st.velocity.x, st.velocity.y, 0};
    b.lastAng = {0, 0, rb->angularVelocity};
  }
}

void PhysicsWorld::End() {
  bodies_.clear();
  events_.clear();
  auto_.clear();
  jolt_.reset();
  box2d_.reset();
  active_ = false;
  steps_ = 0;
}

uint64_t PhysicsWorld::StateHash() const {
  uint64_t h = 1469598103934665603ull;
  for (const Body& b : bodies_) {
    if (b.id == phys::kNoBody) continue;
    const phys::BodyState st = (b.is2D ? *box2d_ : *jolt_).Get(b.id);
    h = Fnv(h, &st.position, sizeof(st.position));
    h = Fnv(h, &st.rotation, sizeof(st.rotation));
  }
  return h;
}

std::string PhysicsWorld::Describe() const {
  if (!active_) return "off";
  return std::format("jolt {} bodies, box2d {} bodies, {} auto colliders, {} steps", Bodies3D(), Bodies2D(), auto_.size(), steps_);
}

std::vector<Command> PhysicsWorld::AddPhysicsCommands(const Scene& scene, const std::string& id) {
  std::vector<Command> out;
  const SceneObject* o = scene.Find(id);
  if (!o) return out;
  auto has = [&](const char* t) {
    for (const Component& c : o->components)
      if (c.type && c.type->name == t) return true;
    return false;
  };
  const bool is2D = has("Rigidbody2D") || has("Collider2D");
  bool level = has("Blockout"); // greybox blocks and flat planes are level geometry
  for (const Component& c : o->components)
    if (c.type && c.type->name == "MeshRenderer") {
      const std::string m = Lower(fs::path(static_cast<const MeshRenderer*>(c.data)->mesh).stem().string());
      level = level || m == "plane";
    }
  auto add = [&](const char* type, const std::string& value) {
    if (has(type)) return;
    Command c;
    c.name = "addcomp";
    c.target = id;
    c.args = std::format(R"({{"type":"{}","value":{}}})", type, value);
    out.push_back(c);
  };
  const std::string body = level ? R"({"type":"Static"})" : "{}";
  add(is2D ? "Rigidbody2D" : "Rigidbody", body);
  add(is2D ? "Collider2D" : "Collider", "{}");
  return out;
}
}
