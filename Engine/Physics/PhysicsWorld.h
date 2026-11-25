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

  // Scene queries (6-5b, the easy API's Raycast / OverlapSphere), by object id. Both worlds are asked (2D bodies in
  // the XY plane); the nearest hit wins. Bodies are where the last step left them.
  struct QueryHit { std::string object; glm::vec3 point{0}, normal{0}; float distance = 0; };
  bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool hitTriggers, QueryHit& hit) const;
  std::vector<std::string> OverlapSphere(const glm::vec3& center, float radius, bool hitTriggers) const; // each object once
  static const PhysicsWorld* Current(); // the world of the game that is playing (script queries), or nullptr

  // "Add Physics" (physics.md): a Rigidbody (Static for greybox blocks and planes, else Dynamic) and an Auto Collider,
  // whichever the object does not have yet; 2D objects (with a Rigidbody2D or Collider2D) get the 2D pair.
  static std::vector<Command> AddPhysicsCommands(const Scene& scene, const std::string& id);

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
  void WriteBack(Scene& scene, Body& b);
  std::unique_ptr<phys::Backend> jolt_, box2d_;
  std::vector<Body> bodies_;
  std::vector<Event> events_;
  std::vector<AutoChoice> auto_;
  const AssetDatabase* assets_ = nullptr;
  bool active_ = false;
  uint64_t steps_ = 0;
};
}
