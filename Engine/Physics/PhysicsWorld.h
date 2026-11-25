#pragma once
// The scene's physics while playing (queue 6-2): makes a body for every object with a Rigidbody / Collider (3D,
// Jolt) or Rigidbody2D / Collider2D (2D, Box2D), steps them on the fixed step, writes the results back into the
// Transforms and velocities, and collects collision and trigger events for scripts. Edits while playing (the
// gizmo, a script setting the position or velocity, AddForce) reach the bodies on the next step.
//
// Bodies are made in hierarchy order, so the same scene gives the same simulation every run (determinism check).
#include "Core/Command.h"
#include "Physics/Physics.h"
#include <memory>
#include <string>
#include <vector>

namespace bdg {
class Scene;
class AssetDatabase;

class PhysicsWorld {
public:
  struct Settings {
    glm::vec3 gravity{0, -9.81f, 0};
    glm::vec2 gravity2D{0, -9.81f};
  };
  enum class EventKind { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit };
  struct Event {
    std::string self, other; // object ids; every pair is reported to both objects
    EventKind kind = EventKind::CollisionEnter;
    glm::vec3 point{0}, normal{0}; // world; the normal points away from `other` (enter only)
  };
  // Collider Auto (physics.md): what the engine picked for each Auto collider, with its error.
  struct AutoChoice {
    std::string id, name, method; // method: "box (exact)", "triangle mesh", "convex hull (26 points)", ...
    float error = 0;              // metres: average gap between the collision shape and the drawn surface
  };

  void Begin(Scene& scene, const Settings& settings, const AssetDatabase* assets);
  void Step(Scene& scene, float dt);
  void End();
  bool Active() const { return active_; }
  const std::vector<Event>& Events() const { return events_; } // of the last Step
  size_t Bodies3D() const { return jolt_ ? jolt_->Bodies() : 0; }
  size_t Bodies2D() const { return box2d_ ? box2d_->Bodies() : 0; }
  uint64_t Steps() const { return steps_; }
  uint64_t StateHash() const;                                   // positions and rotations of every body (bit exact)
  const std::vector<AutoChoice>& AutoChoices() const { return auto_; }
  std::string Describe() const; // "jolt 12 bodies (3 awake), box2d 2 bodies, 120 steps"

  // "Add Physics" (physics.md): a Rigidbody (Static for greybox blocks and planes, else Dynamic) and an Auto Collider,
  // whichever the object does not have yet; 2D objects (with a Rigidbody2D or Collider2D) get the 2D pair.
  static std::vector<Command> AddPhysicsCommands(const Scene& scene, const std::string& id);

  // Scene queries by object id (7-1b foot IK and cameras, 6-5b scripts' Raycast / OverlapSphere). Raycast: the first
  // surface along the ray, skipping the bodies of the objects in `ignore` (an object's own capsule, its ragdoll) and
  // triggers unless hitTriggers; the 3D world and the 2D one (XY plane) are both asked, the nearest hit wins.
  // OverlapSphere: objects with a body inside or touching the sphere, each once. Bodies are where the last step left them.
  struct RayResult { std::string object; glm::vec3 point{0}, normal{0}; float distance = 0; };
  bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RayResult& hit,
               const std::vector<std::string>& ignore = {}, bool hitTriggers = false) const;
  std::vector<std::string> OverlapSphere(const glm::vec3& center, float radius, bool hitTriggers = false) const;
  // 7-1b ragdolls: one dynamic capsule per bone (world space at creation), bones of one ragdoll do not collide with
  // each other, joints (swing cone + twist) between each bone and its parent bone. Returns a handle (-1: failed).
  struct RagdollBone {
    glm::vec3 position{0};      // capsule centre
    glm::quat rotation{1, 0, 0, 0}; // capsule axis = local Y
    float radius = 0.06f, halfHeight = 0.1f, mass = 3;
    int parent = -1;            // index in the list
    glm::vec3 anchor{0}, twistAxis{0, 1, 0}; // joint to the parent bone (world)
    float swing = 45, twist = 20;
  };
  int AddRagdoll(const std::string& object, const std::vector<RagdollBone>& bones, const glm::vec3& velocity);
  bool RagdollState(int ragdoll, std::vector<phys::BodyState>& bones) const;
  void RemoveRagdoll(int ragdoll);
  size_t Ragdolls() const;
  // The world of the running game (set by Begin, cleared by End): script raycasts go through it. Null when stopped.
  static const PhysicsWorld* Current();

private:
  struct Body {
    std::string object;
    bool is2D = false;
    phys::BodyId id = phys::kNoBody;
    phys::Motion motion = phys::Motion::Dynamic;
    glm::vec3 lastPos{0};
    glm::quat lastRot{1, 0, 0, 0};
    glm::vec3 lastVel{0}, lastAng{0}; // component values as last written (degrees for the angular one)
    glm::vec3 scale{1};
  };
  bool MakeBody(Scene& scene, const std::string& id, bool is2D);
  std::string ObjectOfUser(uint64_t user) const; // a body's object id from its BodyDesc::user ("" = none)
  void WriteBack(Scene& scene, Body& b);
  std::unique_ptr<phys::Backend> jolt_, box2d_;
  std::vector<Body> bodies_;
  struct Ragdoll { std::string object; std::vector<phys::BodyId> bodies; std::vector<phys::JointId> joints; bool alive = true; };
  std::vector<Ragdoll> ragdolls_;
  static constexpr uint64_t kRagdollUser = 1ull << 40; // body user values of ragdoll bones: kRagdollUser + ragdoll index
  std::vector<Event> events_;
  std::vector<AutoChoice> auto_;
  const AssetDatabase* assets_ = nullptr;
  bool active_ = false;
  uint64_t steps_ = 0;
};
}
