// Jolt Physics backend of the PHI (queue 6-2): 3D rigid bodies. Built with cross-platform determinism; the job
// system is single-threaded for now (same results every run, contact callbacks on the calling thread). Two object
// layers: static bodies never test against each other.
#include "Physics/Physics.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <algorithm>
#include <map>
#include <unordered_map>

namespace bdg::phys {
namespace {
constexpr JPH::ObjectLayer kStatic = 0, kMoving = 1;
const JPH::BroadPhaseLayer kBpStatic(0), kBpMoving(1);

class LayerPairs final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == kMoving || b == kMoving; }
};
class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
  JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return l == kStatic ? kBpStatic : kBpMoving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l == kBpStatic ? "static" : "moving"; }
#endif
};
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer l, JPH::BroadPhaseLayer b) const override { return l == kMoving || b == kBpMoving; }
};

// 7-1b: bodies with the same non-zero group id never collide (a ragdoll's bones; joints hold them together)
class SameGroupFilter final : public JPH::GroupFilter {
public:
  bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
    return a.GetGroupID() == JPH::CollisionGroup::cInvalidGroup || a.GetGroupID() != b.GetGroupID();
  }
};

void InitJolt() {
  static bool done = false;
  if (done) return;
  JPH::RegisterDefaultAllocator();
  JPH::Factory::sInstance = new JPH::Factory(); // lives for the process (types are registered once)
  JPH::RegisterTypes();
  done = true;
}

JPH::Vec3 J(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat J(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
glm::vec3 G(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

// Body pairs touch through one or more sub-shape pairs (mesh triangles); a pair begins at the first and ends at
// the last. Reported sorted at the end of the step (deterministic order).
class Listener final : public JPH::ContactListener {
public:
  std::map<std::pair<uint32_t, uint32_t>, int> touching; // body id pair -> sub-shape contacts
  struct Pending { uint32_t a, b; bool begin; glm::vec3 point, normal; };
  std::vector<Pending> pending;

  void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m, JPH::ContactSettings&) override {
    auto key = Key(a.GetID().GetIndexAndSequenceNumber(), b.GetID().GetIndexAndSequenceNumber());
    if (touching[key.first]++ == 0) {
      const bool swap = key.second;
      const glm::vec3 p = G(JPH::Vec3(m.GetWorldSpaceContactPointOn1(0))), n = G(m.mWorldSpaceNormal);
      pending.push_back({key.first.first, key.first.second, true, p, swap ? -n : n});
    }
  }
  void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
    auto key = Key(pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetBody2ID().GetIndexAndSequenceNumber());
    auto it = touching.find(key.first);
    if (it == touching.end()) return;
    if (--it->second <= 0) {
      touching.erase(it);
      pending.push_back({key.first.first, key.first.second, false, {}, {}});
    }
  }

private:
  static std::pair<std::pair<uint32_t, uint32_t>, bool> Key(uint32_t a, uint32_t b) { return a < b ? std::pair{std::pair{a, b}, false} : std::pair{std::pair{b, a}, true}; }
};

class JoltBackend final : public Backend {
public:
  JoltBackend() {
    InitJolt();
    temp_ = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    jobs_ = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    system_.Init(65536, 0, 65536, 16384, layers_, objVsBp_, pairs_);
    system_.SetContactListener(&listener_);
    system_.SetGravity(JPH::Vec3(0, -9.81f, 0));
    groups_ = new SameGroupFilter();
  }
  ~JoltBackend() override {
    for (JPH::Ref<JPH::Constraint>& c : joints_)
      if (c) system_.RemoveConstraint(c);
    JPH::BodyInterface& bi = system_.GetBodyInterface();
    for (const Body& b : bodies_)
      if (b.alive) { bi.RemoveBody(b.id); bi.DestroyBody(b.id); }
  }

  Capabilities Caps() const override {
    Capabilities c;
    c.name = "jolt";
    c.is3D = true;
    c.deterministic = true;
    c.triggers = true;
    c.meshShapes = true;
    c.character = true;
    return c;
  }
  void SetGravity(const glm::vec3& g) override { system_.SetGravity(J(g)); }

  BodyId Add(const BodyDesc& d, std::string* error) override {
    JPH::Ref<JPH::ShapeSettings> settings = MakeShape(d.shape, d.motion);
    if (!settings) {
      if (error) *error = "this shape needs a static body (triangle meshes cannot move)";
      return kNoBody;
    }
    if (d.shape.offset != glm::vec3(0)) settings = new JPH::RotatedTranslatedShapeSettings(J(d.shape.offset), JPH::Quat::sIdentity(), settings);
    JPH::ShapeSettings::ShapeResult r = settings->Create();
    if (r.HasError()) {
      if (error) *error = std::string("shape: ") + r.GetError().c_str();
      return kNoBody;
    }
    const JPH::EMotionType motion = d.motion == Motion::Static ? JPH::EMotionType::Static
                                    : d.motion == Motion::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings s(r.Get(), JPH::RVec3(J(d.position)), J(d.rotation), motion, d.motion == Motion::Static ? kStatic : kMoving);
    s.mFriction = d.friction;
    s.mRestitution = d.restitution;
    s.mLinearDamping = d.linearDamping;
    s.mAngularDamping = d.angularDamping;
    s.mGravityFactor = d.gravityScale;
    s.mIsSensor = d.trigger;
    s.mUserData = d.user;
    s.mLinearVelocity = J(d.velocity);
    if (d.group) s.mCollisionGroup = JPH::CollisionGroup(groups_, d.group, 0);
    s.mAngularVelocity = J(d.angularVelocity);
    if (d.motion == Motion::Dynamic) {
      s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      s.mMassPropertiesOverride.mMass = std::max(d.mass, 1e-4f);
      if (d.freezeRotation) s.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
    }
    if (d.trigger && d.motion != Motion::Dynamic) s.mCollideKinematicVsNonDynamic = true; // a moving trigger also sees static things
    JPH::BodyInterface& bi = system_.GetBodyInterface();
    JPH::Body* body = bi.CreateBody(s);
    if (!body) {
      if (error) *error = "too many bodies";
      return kNoBody;
    }
    bi.AddBody(body->GetID(), d.motion == Motion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    user_[body->GetID().GetIndexAndSequenceNumber()] = d.user;
    bodies_.push_back({body->GetID(), true});
    added_ = true;
    return (BodyId)(bodies_.size() - 1);
  }
  void Remove(BodyId id) override {
    if (!Valid(id)) return;
    JPH::BodyInterface& bi = system_.GetBodyInterface();
    bi.RemoveBody(bodies_[id].id);
    bi.DestroyBody(bodies_[id].id);
    bodies_[id].alive = false;
  }
  void Teleport(BodyId id, const glm::vec3& p, const glm::quat& r) override {
    if (Valid(id)) system_.GetBodyInterface().SetPositionAndRotation(bodies_[id].id, JPH::RVec3(J(p)), J(r), JPH::EActivation::Activate);
  }
  void MoveKinematic(BodyId id, const glm::vec3& p, const glm::quat& r, float dt) override {
    if (Valid(id) && dt > 0) system_.GetBodyInterface().MoveKinematic(bodies_[id].id, JPH::RVec3(J(p)), J(r), dt);
  }
  void SetVelocity(BodyId id, const glm::vec3& v, const glm::vec3& w) override {
    if (Valid(id)) system_.GetBodyInterface().SetLinearAndAngularVelocity(bodies_[id].id, J(v), J(w));
  }
  void Push(BodyId id, const glm::vec3& f, const glm::vec3& t, const glm::vec3& i) override {
    if (!Valid(id)) return;
    JPH::BodyInterface& bi = system_.GetBodyInterface();
    if (f != glm::vec3(0)) bi.AddForce(bodies_[id].id, J(f));
    if (t != glm::vec3(0)) bi.AddTorque(bodies_[id].id, J(t));
    if (i != glm::vec3(0)) bi.AddImpulse(bodies_[id].id, J(i));
  }
  void Step(float dt) override {
    if (added_) { // after loading a batch of bodies: a balanced broad phase (same order, same tree)
      system_.OptimizeBroadPhase();
      added_ = false;
    }
    listener_.pending.clear();
    system_.Update(dt, 1, temp_.get(), jobs_.get());
    contacts_.clear();
    const JPH::BodyLockInterfaceNoLock& lock = system_.GetBodyLockInterfaceNoLock();
    auto isSensor = [&](uint32_t key) {
      JPH::BodyLockRead l(lock, JPH::BodyID(key));
      return l.Succeeded() && l.GetBody().IsSensor();
    };
    for (const Listener::Pending& p : listener_.pending) {
      Contact c;
      c.a = user_.count(p.a) ? user_[p.a] : 0;
      c.b = user_.count(p.b) ? user_[p.b] : 0;
      c.begin = p.begin;
      c.trigger = isSensor(p.a) || isSensor(p.b);
      c.point = p.point;
      c.normal = p.normal;
      contacts_.push_back(c);
    }
    std::stable_sort(contacts_.begin(), contacts_.end(), [](const Contact& x, const Contact& y) {
      return std::tie(x.begin, x.a, x.b) < std::tie(y.begin, y.a, y.b);
    });
  }
  BodyState Get(BodyId id) const override {
    BodyState s;
    if (!Valid(id)) return s;
    const JPH::BodyInterface& bi = system_.GetBodyInterface();
    JPH::RVec3 p;
    JPH::Quat q;
    bi.GetPositionAndRotation(bodies_[id].id, p, q);
    s.position = {(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()};
    s.rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    s.velocity = G(bi.GetLinearVelocity(bodies_[id].id));
    s.angularVelocity = G(bi.GetAngularVelocity(bodies_[id].id));
    s.sleeping = !bi.IsActive(bodies_[id].id);
    return s;
  }
  const std::vector<Contact>& Contacts() const override { return contacts_; }
  size_t Bodies() const override { return (size_t)std::count_if(bodies_.begin(), bodies_.end(), [](const Body& b) { return b.alive; }); }
  float Volume(BodyId id) const override {
    if (!Valid(id)) return 0;
    JPH::BodyLockRead l(system_.GetBodyLockInterface(), bodies_[id].id);
    return l.Succeeded() ? l.GetBody().GetShape()->GetVolume() : 0.f;
  }

  // Scene queries skip triggers (sensors) unless asked, and bodies whose user value is in `ignore`.
  class QueryFilter final : public JPH::BodyFilter {
  public:
    QueryFilter(bool triggers, const std::vector<uint64_t>& ignore) : triggers_(triggers), ignore_(ignore) {}
    bool ShouldCollideLocked(const JPH::Body& b) const override {
      return (triggers_ || !b.IsSensor()) && std::find(ignore_.begin(), ignore_.end(), b.GetUserData()) == ignore_.end();
    }
  private:
    bool triggers_;
    const std::vector<uint64_t>& ignore_;
  };
  bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool hitTriggers, RayHit& hit,
               const std::vector<uint64_t>& ignore) const override {
    const float len = glm::length(direction);
    if (len < 1e-9f || maxDistance <= 0) return false;
    const glm::vec3 dir = direction / len;
    const JPH::RRayCast ray(JPH::RVec3(J(origin)), J(dir * maxDistance));
    const QueryFilter filter(hitTriggers, ignore);
    JPH::RayCastResult r;
    if (!system_.GetNarrowPhaseQuery().CastRay(ray, r, {}, {}, filter)) return false;
    hit.distance = r.mFraction * maxDistance;
    hit.point = origin + dir * hit.distance;
    hit.normal = -dir;
    hit.user = 0;
    JPH::BodyLockRead lock(system_.GetBodyLockInterface(), r.mBodyID);
    if (lock.Succeeded()) {
      hit.user = lock.GetBody().GetUserData();
      hit.normal = G(lock.GetBody().GetWorldSpaceSurfaceNormal(r.mSubShapeID2, ray.GetPointOnRay(r.mFraction)));
    }
    return true;
  }
  void OverlapSphere(const glm::vec3& center, float radius, bool hitTriggers, std::vector<uint64_t>& users) const override {
    if (radius <= 0) return;
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    const std::vector<uint64_t> none;
    const QueryFilter filter(hitTriggers, none);
    system_.GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sReplicate(1), JPH::RMat44::sTranslation(JPH::RVec3(J(center))), settings,
                                               JPH::RVec3::sZero(), hits, {}, {}, filter);
    for (const JPH::CollideShapeResult& h : hits.mHits) {
      JPH::BodyLockRead lock(system_.GetBodyLockInterface(), h.mBodyID2);
      if (lock.Succeeded()) users.push_back(lock.GetBody().GetUserData());
    }
  }
  JointId AddJoint(const JointDesc& d) override {
    if (!Valid(d.a) || !Valid(d.b)) return 0xFFFFFFFFu;
    JPH::SwingTwistConstraintSettings s;
    s.mSpace = JPH::EConstraintSpace::WorldSpace;
    s.mPosition1 = s.mPosition2 = JPH::RVec3(J(d.anchor));
    const glm::vec3 twist = glm::length(d.twistAxis) > 1e-6f ? glm::normalize(d.twistAxis) : glm::vec3(0, 1, 0);
    glm::vec3 plane = glm::cross(twist, std::fabs(twist.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    plane = glm::normalize(plane);
    s.mTwistAxis1 = s.mTwistAxis2 = J(twist);
    s.mPlaneAxis1 = s.mPlaneAxis2 = J(plane);
    s.mNormalHalfConeAngle = s.mPlaneHalfConeAngle = glm::radians(std::clamp(d.swingDegrees, 0.f, 170.f));
    s.mTwistMinAngle = -glm::radians(std::clamp(d.twistDegrees, 0.f, 170.f));
    s.mTwistMaxAngle = glm::radians(std::clamp(d.twistDegrees, 0.f, 170.f));
    const JPH::BodyID ids[2] = {bodies_[d.a].id, bodies_[d.b].id};
    JPH::BodyLockMultiWrite lock(system_.GetBodyLockInterface(), ids, 2);
    if (!lock.GetBody(0) || !lock.GetBody(1)) return 0xFFFFFFFFu;
    JPH::Ref<JPH::Constraint> c = s.Create(*lock.GetBody(0), *lock.GetBody(1));
    system_.AddConstraint(c);
    joints_.push_back(c);
    return (JointId)(joints_.size() - 1);
  }
  void RemoveJoint(JointId id) override {
    if (id >= joints_.size() || !joints_[id]) return;
    system_.RemoveConstraint(joints_[id]);
    joints_[id] = nullptr;
  }

private:
  struct Body { JPH::BodyID id; bool alive; };
  bool Valid(BodyId id) const { return id < bodies_.size() && bodies_[id].alive; }

  static JPH::Ref<JPH::ShapeSettings> MakeShape(const ShapeDesc& s, Motion motion) {
    auto convexRadius = [](float smallest) { return std::min(JPH::cDefaultConvexRadius, 0.45f * smallest); };
    switch (s.kind) {
      case ShapeKind::Box: {
        const glm::vec3 h = glm::max(s.halfExtents, glm::vec3(0.005f));
        return new JPH::BoxShapeSettings(J(h), convexRadius(std::min({h.x, h.y, h.z})));
      }
      case ShapeKind::Sphere: return new JPH::SphereShapeSettings(std::max(s.radius, 0.005f));
      case ShapeKind::Capsule: return new JPH::CapsuleShapeSettings(std::max(s.halfHeight, 0.001f), std::max(s.radius, 0.005f));
      case ShapeKind::Cylinder: {
        const float hh = std::max(s.halfHeight, 0.005f), r = std::max(s.radius, 0.005f);
        return new JPH::CylinderShapeSettings(hh, r, convexRadius(std::min(hh, r)));
      }
      case ShapeKind::ConvexHull: {
        JPH::Array<JPH::Vec3> pts;
        pts.reserve(s.points.size());
        for (const glm::vec3& p : s.points) pts.push_back(J(p));
        return new JPH::ConvexHullShapeSettings(pts, JPH::cDefaultConvexRadius);
      }
      case ShapeKind::Mesh: {
        if (motion != Motion::Static) return nullptr;
        JPH::VertexList verts;
        verts.reserve(s.points.size());
        for (const glm::vec3& p : s.points) verts.push_back(JPH::Float3(p.x, p.y, p.z));
        JPH::IndexedTriangleList tris;
        for (size_t i = 0; i + 2 < s.indices.size(); i += 3) tris.push_back(JPH::IndexedTriangle(s.indices[i], s.indices[i + 1], s.indices[i + 2], 0));
        return new JPH::MeshShapeSettings(verts, tris);
      }
      default: return nullptr; // 2D shapes belong to the 2D backend
    }
  }

  LayerPairs pairs_;
  BroadPhaseLayers layers_;
  ObjectVsBroadPhase objVsBp_;
  std::unique_ptr<JPH::TempAllocatorImpl> temp_;
  std::unique_ptr<JPH::JobSystemSingleThreaded> jobs_;
  JPH::PhysicsSystem system_;
  Listener listener_;
  std::vector<Body> bodies_;
  std::unordered_map<uint32_t, uint64_t> user_;
  std::vector<Contact> contacts_;
  std::vector<JPH::Ref<JPH::Constraint>> joints_; // 7-1b ragdoll joints
  JPH::Ref<JPH::GroupFilter> groups_;
  bool added_ = false;
};
}

std::unique_ptr<Backend> CreateJolt() { return std::make_unique<JoltBackend>(); }
}
