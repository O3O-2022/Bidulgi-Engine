#pragma once
// Physics Interface (PHI, queue 6-2, Docs/architecture/physics.md): the engine talks to every physics engine through
// this, like rendering talks to the GPU through the RHI. Backends: Jolt (3D, default) and Box2D (2D, the XY plane);
// GPU physics and packaged backends (PhysX, ...) later. Values are backend-independent (metres, kilograms, seconds,
// radians here; degrees only in components). No third-party type appears in this header.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bdg::phys {
enum class Motion { Static, Dynamic, Kinematic };
enum class ShapeKind { Box, Sphere, Capsule, Cylinder, ConvexHull, Mesh, Circle2D, Box2D, Capsule2D, HeightField };

struct ShapeDesc {
  ShapeKind kind = ShapeKind::Box;
  glm::vec3 halfExtents{0.5f};          // Box (3D), Box2D (x, y)
  float radius = 0.5f, halfHeight = 0.5f; // Sphere / Circle2D radius; Capsule, Cylinder: half the straight part / half height
  glm::vec3 offset{0};                  // from the body origin (body space)
  std::vector<glm::vec3> points;        // ConvexHull points, Mesh vertices (body space, scale applied)
  std::vector<uint32_t> indices;        // Mesh triangles
  // HeightField (terrain, static only): samples x samples heights (row = z), body space point of sample (x, z) =
  // (x * fieldScale.x, height * fieldScale.y, z * fieldScale.z)
  std::vector<float> heights;
  uint32_t samples = 0;
  glm::vec3 fieldScale{1};
};

struct BodyDesc {
  uint64_t user = 0;      // the engine's id for this body (events report it)
  Motion motion = Motion::Dynamic;
  ShapeDesc shape;
  glm::vec3 position{0};  // 2D: x, y
  glm::quat rotation{1, 0, 0, 0}; // 2D: around Z only
  glm::vec3 velocity{0}, angularVelocity{0}; // m/s, rad/s (2D: z)
  float mass = 1, friction = 0.6f, restitution = 0, linearDamping = 0.05f, angularDamping = 0.05f, gravityScale = 1;
  bool trigger = false, freezeRotation = false;
  uint32_t group = 0; // 7-1b: bodies of one non-zero group never touch each other (the bones of one ragdoll)
};

using BodyId = uint32_t;
constexpr BodyId kNoBody = 0xFFFFFFFFu;

struct BodyState {
  glm::vec3 position{0};
  glm::quat rotation{1, 0, 0, 0};
  glm::vec3 velocity{0}, angularVelocity{0};
  bool sleeping = false;
};

// A touch that started or ended during the last Step (pairs are reported once, in a deterministic order).
struct Contact {
  uint64_t a = 0, b = 0;
  bool trigger = false, begin = true;
  glm::vec3 point{0}, normal{0}; // world; normal from a to b (begin only)
};

// The first surface along a ray (7-1b foot IK, 6-5b scripts' Raycast).
struct RayHit {
  uint64_t user = 0;       // BodyDesc::user of the body hit
  glm::vec3 point{0}, normal{0};
  float distance = 0;
};

// 7-1b: a ball-and-socket joint with a swing cone and a twist range (ragdoll joints). World space at creation.
struct JointDesc {
  uint32_t a = 0xFFFFFFFFu, b = 0xFFFFFFFFu; // BodyId: parent bone, child bone
  glm::vec3 anchor{0};
  glm::vec3 twistAxis{0, 1, 0};               // along the child bone
  float swingDegrees = 45, twistDegrees = 20;
};
using JointId = uint32_t;

struct Capabilities {
  const char* name = "";
  bool is3D = true, deterministic = false, triggers = false, meshShapes = false, character = false;
};

class Backend {
public:
  virtual ~Backend() = default;
  virtual Capabilities Caps() const = 0;
  virtual void SetGravity(const glm::vec3& g) = 0;
  virtual BodyId Add(const BodyDesc& desc, std::string* error = nullptr) = 0;
  virtual void Remove(BodyId id) = 0;
  virtual void Teleport(BodyId id, const glm::vec3& position, const glm::quat& rotation) = 0;
  virtual void MoveKinematic(BodyId id, const glm::vec3& position, const glm::quat& rotation, float dt) = 0;
  virtual void SetVelocity(BodyId id, const glm::vec3& velocity, const glm::vec3& angularVelocity) = 0;
  // force (N) and torque (N m) act over the next step; impulse (N s) changes the velocity at once
  virtual void Push(BodyId id, const glm::vec3& force, const glm::vec3& torque, const glm::vec3& impulse) = 0;
  virtual void Step(float dt) = 0;
  virtual BodyState Get(BodyId id) const = 0;
  virtual const std::vector<Contact>& Contacts() const = 0; // of the last Step
  virtual size_t Bodies() const = 0;
  virtual float Volume(BodyId id) const = 0; // of the collision shape (m^3; 2D: m^2): Collider Auto error
  // The nearest surface along origin + t * direction (t <= maxDistance), skipping bodies whose user value is in
  // `ignore`; triggers only with hitTriggers (7-1b, 6-5b). 2D: the XY plane (z ignored). false: nothing hit (or the
  // backend has no ray queries). hit.user is the body's BodyDesc::user.
  virtual bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool hitTriggers, RayHit& hit,
                       const std::vector<uint64_t>& ignore = {}) const {
    (void)origin, (void)direction, (void)maxDistance, (void)hitTriggers, (void)hit, (void)ignore;
    return false;
  }
  // Bodies whose shape is inside or touches the sphere (2D: circle), as BodyDesc::user values, in no particular order
  // (may repeat a body) (6-5b).
  virtual void OverlapSphere(const glm::vec3& center, float radius, bool hitTriggers, std::vector<uint64_t>& users) const {
    (void)center, (void)radius, (void)hitTriggers, (void)users;
  }
  virtual JointId AddJoint(const JointDesc& desc) { (void)desc; return 0xFFFFFFFFu; }
  virtual void RemoveJoint(JointId id) { (void)id; }
};

std::unique_ptr<Backend> CreateJolt();  // 3D
std::unique_ptr<Backend> CreateBox2D(); // 2D (XY plane)
}
