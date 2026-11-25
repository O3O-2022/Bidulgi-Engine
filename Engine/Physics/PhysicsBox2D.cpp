// Box2D v3 backend of the PHI (queue 6-2): 2D rigid bodies in the XY plane. Single world, 4 sub-steps per step;
// Box2D is deterministic on one machine and build, which the determinism check relies on.
#include "Physics/Physics.h"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bdg::phys {
namespace {
uint64_t ShapeKey(b2ShapeId s) { return (uint64_t)(uint32_t)s.index1 << 16 | s.generation; }

class Box2DBackend final : public Backend {
public:
  Box2DBackend() {
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {0, -9.81f};
    world_ = b2CreateWorld(&wd);
  }
  ~Box2DBackend() override { b2DestroyWorld(world_); }

  Capabilities Caps() const override {
    Capabilities c;
    c.name = "box2d";
    c.is3D = false;
    c.deterministic = true;
    c.triggers = true;
    return c;
  }
  void SetGravity(const glm::vec3& g) override { b2World_SetGravity(world_, {g.x, g.y}); }

  BodyId Add(const BodyDesc& d, std::string* error) override {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = d.motion == Motion::Static ? b2_staticBody : d.motion == Motion::Kinematic ? b2_kinematicBody : b2_dynamicBody;
    bd.position = {d.position.x, d.position.y};
    bd.rotation = b2MakeRot(Angle(d.rotation));
    bd.linearVelocity = {d.velocity.x, d.velocity.y};
    bd.angularVelocity = d.angularVelocity.z;
    bd.linearDamping = d.linearDamping;
    bd.angularDamping = d.angularDamping;
    bd.gravityScale = d.gravityScale;
    bd.fixedRotation = d.freezeRotation;
    const b2BodyId body = b2CreateBody(world_, &bd);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.friction = d.friction;
    sd.material.restitution = d.restitution;
    sd.isSensor = d.trigger;
    sd.enableSensorEvents = true;
    sd.enableContactEvents = true;
    const ShapeDesc& s = d.shape;
    const b2Vec2 c{s.offset.x, s.offset.y};
    float area = 1;
    b2ShapeId shape = b2_nullShapeId;
    if (s.kind == ShapeKind::Circle2D) {
      area = 3.14159265f * s.radius * s.radius;
      sd.density = d.mass / std::max(area, 1e-6f);
      b2Circle circle{c, s.radius};
      shape = b2CreateCircleShape(body, &sd, &circle);
    } else if (s.kind == ShapeKind::Capsule2D) { // upright: centres halfHeight above and below
      area = 2 * s.radius * 2 * s.halfHeight + 3.14159265f * s.radius * s.radius;
      sd.density = d.mass / std::max(area, 1e-6f);
      b2Capsule cap{{c.x, c.y - s.halfHeight}, {c.x, c.y + s.halfHeight}, s.radius};
      shape = b2CreateCapsuleShape(body, &sd, &cap);
    } else if (s.kind == ShapeKind::Box2D) {
      area = 4 * s.halfExtents.x * s.halfExtents.y;
      sd.density = d.mass / std::max(area, 1e-6f);
      const b2Polygon box = b2MakeOffsetBox(std::max(s.halfExtents.x, 0.005f), std::max(s.halfExtents.y, 0.005f), c, b2Rot_identity);
      shape = b2CreatePolygonShape(body, &sd, &box);
    } else {
      if (error) *error = "2D physics takes boxes, circles and capsules";
      b2DestroyBody(body);
      return kNoBody;
    }
    shapeUser_[ShapeKey(shape)] = d.user;
    bodies_.push_back({body, d.user, true, sd.density});
    return (BodyId)(bodies_.size() - 1);
  }
  void Remove(BodyId id) override {
    if (!Valid(id)) return;
    b2DestroyBody(bodies_[id].body);
    bodies_[id].alive = false;
  }
  void Teleport(BodyId id, const glm::vec3& p, const glm::quat& r) override {
    if (Valid(id)) b2Body_SetTransform(bodies_[id].body, {p.x, p.y}, b2MakeRot(Angle(r)));
  }
  void MoveKinematic(BodyId id, const glm::vec3& p, const glm::quat& r, float dt) override {
    if (!Valid(id) || dt <= 0) return;
    b2Body_SetTargetTransform(bodies_[id].body, b2Transform{{p.x, p.y}, b2MakeRot(Angle(r))}, dt);
  }
  void SetVelocity(BodyId id, const glm::vec3& v, const glm::vec3& w) override {
    if (!Valid(id)) return;
    b2Body_SetLinearVelocity(bodies_[id].body, {v.x, v.y});
    b2Body_SetAngularVelocity(bodies_[id].body, w.z);
  }
  void Push(BodyId id, const glm::vec3& f, const glm::vec3& t, const glm::vec3& i) override {
    if (!Valid(id)) return;
    const b2BodyId b = bodies_[id].body;
    if (f.x != 0 || f.y != 0) b2Body_ApplyForceToCenter(b, {f.x, f.y}, true);
    if (t.z != 0) b2Body_ApplyTorque(b, t.z, true);
    if (i.x != 0 || i.y != 0) b2Body_ApplyLinearImpulseToCenter(b, {i.x, i.y}, true);
  }
  void Step(float dt) override {
    b2World_Step(world_, dt, 4);
    contacts_.clear();
    auto user = [&](b2ShapeId s) {
      auto it = shapeUser_.find(ShapeKey(s));
      return it == shapeUser_.end() ? 0ull : it->second;
    };
    const b2ContactEvents ce = b2World_GetContactEvents(world_);
    for (int k = 0; k < ce.beginCount; ++k) {
      const b2ContactBeginTouchEvent& e = ce.beginEvents[k];
      Contact c{user(e.shapeIdA), user(e.shapeIdB), false, true};
      c.normal = {e.manifold.normal.x, e.manifold.normal.y, 0};
      if (e.manifold.pointCount > 0) c.point = {e.manifold.points[0].point.x, e.manifold.points[0].point.y, 0};
      contacts_.push_back(c);
    }
    for (int k = 0; k < ce.endCount; ++k) contacts_.push_back({user(ce.endEvents[k].shapeIdA), user(ce.endEvents[k].shapeIdB), false, false});
    const b2SensorEvents se = b2World_GetSensorEvents(world_);
    for (int k = 0; k < se.beginCount; ++k) contacts_.push_back({user(se.beginEvents[k].sensorShapeId), user(se.beginEvents[k].visitorShapeId), true, true});
    for (int k = 0; k < se.endCount; ++k) contacts_.push_back({user(se.endEvents[k].sensorShapeId), user(se.endEvents[k].visitorShapeId), true, false});
  }
  BodyState Get(BodyId id) const override {
    BodyState s;
    if (!Valid(id)) return s;
    const b2BodyId b = bodies_[id].body;
    const b2Vec2 p = b2Body_GetPosition(b), v = b2Body_GetLinearVelocity(b);
    s.position = {p.x, p.y, 0};
    s.rotation = glm::angleAxis(b2Rot_GetAngle(b2Body_GetRotation(b)), glm::vec3(0, 0, 1));
    s.velocity = {v.x, v.y, 0};
    s.angularVelocity = {0, 0, b2Body_GetAngularVelocity(b)};
    s.sleeping = !b2Body_IsAwake(b);
    return s;
  }
  const std::vector<Contact>& Contacts() const override { return contacts_; }
  size_t Bodies() const override { return (size_t)std::count_if(bodies_.begin(), bodies_.end(), [](const Body& b) { return b.alive; }); }
  float Volume(BodyId id) const override { return Valid(id) ? b2Body_GetMass(bodies_[id].body) / std::max(bodies_[id].density, 1e-9f) : 0.f; }
  bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool hitTriggers, RayHit& hit,
               const std::vector<uint64_t>& ignore) const override {
    const glm::vec2 d(direction.x, direction.y);
    const float len = glm::length(d);
    if (len < 1e-8f || maxDistance <= 0) return false;
    struct Ctx { const Box2DBackend* self; const std::vector<uint64_t>* ignore; bool triggers, found; b2ShapeId shape; b2Vec2 point, normal; float fraction; } c{
        this, &ignore, hitTriggers, false, b2_nullShapeId, {}, {}, 1};
    const b2Vec2 dir{d.x / len * maxDistance, d.y / len * maxDistance};
    b2World_CastRay(world_, {origin.x, origin.y}, dir, b2DefaultQueryFilter(), [](b2ShapeId s, b2Vec2 p, b2Vec2 n, float f, void* ctx) -> float {
      Ctx& c = *static_cast<Ctx*>(ctx);
      if (!c.triggers && b2Shape_IsSensor(s)) return -1.f; // skip it, go on
      if (!c.ignore->empty()) {
        auto it = c.self->shapeUser_.find(ShapeKey(s));
        if (it != c.self->shapeUser_.end() && std::find(c.ignore->begin(), c.ignore->end(), it->second) != c.ignore->end()) return -1.f;
      }
      c = {c.self, c.ignore, c.triggers, true, s, p, n, f};
      return f; // clip the ray here: only nearer hits follow
    }, &c);
    if (!c.found) return false;
    hit.distance = c.fraction * maxDistance;
    hit.point = {c.point.x, c.point.y, origin.z};
    hit.normal = {c.normal.x, c.normal.y, 0};
    auto it = shapeUser_.find(ShapeKey(c.shape));
    hit.user = it == shapeUser_.end() ? 0 : it->second;
    return true;
  }
  void OverlapSphere(const glm::vec3& center, float radius, bool hitTriggers, std::vector<uint64_t>& users) const override {
    if (radius <= 0) return;
    const b2Vec2 p{center.x, center.y};
    const b2ShapeProxy proxy = b2MakeProxy(&p, 1, radius);
    struct Ctx { const Box2DBackend* self; bool triggers; std::vector<uint64_t>* out; } c{this, hitTriggers, &users};
    b2World_OverlapShape(world_, &proxy, b2DefaultQueryFilter(), [](b2ShapeId s, void* ctx) -> bool {
      Ctx& c = *static_cast<Ctx*>(ctx);
      if (!c.triggers && b2Shape_IsSensor(s)) return true;
      auto it = c.self->shapeUser_.find(ShapeKey(s));
      if (it != c.self->shapeUser_.end()) c.out->push_back(it->second);
      return true;
    }, &c);
  }

private:
  struct Body { b2BodyId body; uint64_t user; bool alive; float density; };
  static float Angle(const glm::quat& q) { return 2.f * std::atan2(q.z, q.w); } // around Z (2D bodies turn only there)
  bool Valid(BodyId id) const { return id < bodies_.size() && bodies_[id].alive; }
  b2WorldId world_;
  std::vector<Body> bodies_;
  std::unordered_map<uint64_t, uint64_t> shapeUser_;
  std::vector<Contact> contacts_;
};
}

std::unique_ptr<Backend> CreateBox2D() { return std::make_unique<Box2DBackend>(); }
}
