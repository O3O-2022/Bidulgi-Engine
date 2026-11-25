#pragma once
// Built-in components (reflected; tools/bht generates Components.gen.cpp).
#include <vector>
#include <utility>
#include "Core/Reflection.h"
#include <string>

namespace bdg {
BCLASS(Component)
struct Transform {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Local position in metres (Y-up)")
  Vec3 position;
  BPROPERTY(Edit, Tooltip = "Local rotation, Euler angles in degrees")
  Vec3 rotation;
  BPROPERTY(Edit)
  Vec3 scale{1, 1, 1};
};

BCLASS(Component)
struct MeshRenderer {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) std::string mesh;
  BPROPERTY(Edit) std::string material;
  BPROPERTY(Edit, Tooltip = "Static GI: bake its bounced light into a lightmap (Scene view: Bake GI)") bool bakeLightmap = false;
};

BENUM()
enum class LightType { Directional, Point, Spot };

BCLASS(Component)
struct Light {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) LightType type = LightType::Point;
  BPROPERTY(Edit) Color color{1, 1, 1, 1};
  BPROPERTY(Edit, Range = "0,100000") float intensity = 1.f;
  BPROPERTY(Edit, Range = "0,1000", ShowIf = "type != Directional") float range = 10.f;
  BPROPERTY(Edit, Range = "1,179", ShowIf = "type == Spot", Tooltip = "Full cone angle in degrees") float spotAngle = 45.f;
  BPROPERTY(Edit, Range = "0,179", ShowIf = "type == Spot", Tooltip = "Full-brightness cone in degrees (soft edge out to spotAngle)")
  float innerSpotAngle = 30.f;
  BPROPERTY(Edit, Tooltip = "Shadow map. Point and spot lights: the quality tier's nearest few on screen (medium 1, high 4, ultra 8)")
  bool castShadows = true;
  BPROPERTY(Edit, ShowIf = "type == Directional", Tooltip = "God rays: the sky around the sun streaks past objects toward the camera (E-7)")
  bool lightShafts = false;
  BPROPERTY(Edit, Range = "0,4", ShowIf = "lightShafts", Tooltip = "Strength of the light shafts") float lightShaftIntensity = 1.f;
};

// Camera bookmark (queue 3-8): number key `slot` (1-9) over the scene view jumps to this view, Ctrl + key stores the
// current view here. Saved with the scene, so test zones keep fixed cameras. The object's name is the label.
BCLASS(Component)
struct CameraBookmark {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,9", Tooltip = "Number key that jumps here (Ctrl + the key stores the current view)") int32_t slot = 1;
  BPROPERTY(Edit, Header = "View") Vec3 pivot;
  BPROPERTY(Edit, Range = "-360,360") float yaw = 35.f;
  BPROPERTY(Edit, Range = "-89,89") float pitch = -24.f;
  BPROPERTY(Edit, Range = "0.05,20000") float distance = 11.f;
  BPROPERTY(Edit, Tooltip = "Quality tier for this view (potato ... ultra); empty keeps the current one") std::string tier;
};

// Reflection probe (GI Tier 0): captures the scene around the object; objects whose centre is inside its box get
// their reflections and ambient light from it. Baked in the editor (Scene view "Bake GI"; each bake adds a bounce).
BCLASS(Component)
struct ReflectionProbe {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Size of the box it covers, centred on the object (metres)") Vec3 size{10, 4, 10};
  BPROPERTY(Edit, Tooltip = "Line reflections up with the box's walls (rooms)") bool boxProjection = true;
  BPROPERTY(Edit, Range = "0,10") float intensity = 1.f;
};

// Light probe volume (GI Tier 0-B): a grid of light probes filling a box centred on the object. Surfaces inside blend
// the nearest probes per pixel for their indirect (bounced) light. Baked with the reflection probes ("Bake GI").
BCLASS(Component)
struct LightProbeVolume {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Size of the box it fills, centred on the object (metres)") Vec3 size{10, 4, 10};
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Probes along X") int32_t countX = 4;
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Probes along Y (up)") int32_t countY = 2;
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Probes along Z") int32_t countZ = 4;
  BPROPERTY(Edit, Range = "0,10") float intensity = 1.f;
};

// Sky and ambient light (queue 3-4): the background of the scene view and the light it gives to every surface.
// The sun disk follows the first directional light.
BCLASS(Component)
struct Sky {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Header = "Colors") Color zenith{0.16f, 0.32f, 0.62f, 1};
  BPROPERTY(Edit) Color horizon{0.62f, 0.70f, 0.80f, 1};
  BPROPERTY(Edit) Color ground{0.20f, 0.18f, 0.16f, 1};
  BPROPERTY(Edit, Range = "0,10", Tooltip = "Scales the background and the ambient light it gives") float intensity = 1.f;
  BPROPERTY(Edit, Range = "0,10", Tooltip = "Sun disk size in degrees (0 = no disk)") float sunSize = 1.5f;
};

// World Settings (queue 2-6): values for the whole scene on one object; the first one in the scene is used.
BCLASS(Component)
struct WorldSettings {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Header = "Fog", Tooltip = "Distance fog: far surfaces fade toward the fog colour") bool fog = false;
  BPROPERTY(Edit, ShowIf = "fog") Color fogColor{0.62f, 0.70f, 0.80f, 1};
  BPROPERTY(Edit, Range = "0,0.5", ShowIf = "fog", Tooltip = "How fast it thickens per metre (at 0.02 half is gone after 35 m)")
  float fogDensity = 0.02f;
  BPROPERTY(Edit, Range = "0,1000", ShowIf = "fog", Tooltip = "Metres from the camera before the fog starts") float fogStart = 0.f;
  BPROPERTY(Edit, Header = "Exposure", Range = "0.1,8", Tooltip = "Brightness of the whole picture, on top of the quality tier's")
  float exposure = 1.f;
};

// Post Process Volume (queue E-6, Unreal's): camera effects for the whole scene (unbound) or inside a box around
// the object (size, fading over blendRadius metres outside it). Several blend by weight in priority order.
BCLASS(Component)
struct PostProcessVolume {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Header = "Volume", Tooltip = "Applies everywhere (off: only inside the box)") bool unbound = true;
  BPROPERTY(Edit, ShowIf = "!unbound", Tooltip = "Box size in metres, centred on the object") Vec3 size{10, 10, 10};
  BPROPERTY(Edit, Range = "0,50", ShowIf = "!unbound", Tooltip = "Metres outside the box over which the effect fades out") float blendRadius = 2.f;
  BPROPERTY(Edit, Range = "0,1", Tooltip = "How much of this volume applies") float blendWeight = 1.f;
  BPROPERTY(Edit, Range = "-100,100", Tooltip = "Higher priority volumes apply over lower ones") int32_t priority = 0;
  BPROPERTY(Edit, Header = "Exposure", Range = "-5,5", Tooltip = "Exposure compensation in stops") float exposureCompensation = 0.f;
  BPROPERTY(Edit, Tooltip = "Eye adaptation: the picture's average brightness is brought to middle grey") bool autoExposure = false;
  BPROPERTY(Edit, Range = "-8,8", ShowIf = "autoExposure", Tooltip = "Most it may darken (stops)") float autoMinEV = -2.f;
  BPROPERTY(Edit, Range = "-8,8", ShowIf = "autoExposure", Tooltip = "Most it may brighten (stops)") float autoMaxEV = 2.f;
  BPROPERTY(Edit, Range = "0.1,10", ShowIf = "autoExposure", Tooltip = "How fast the eye adapts") float adaptationSpeed = 2.f;
  BPROPERTY(Edit, Header = "Bloom", Range = "0,4", Tooltip = "Times the quality tier's bloom") float bloomIntensity = 1.f;
  BPROPERTY(Edit, Header = "Lens", Range = "0,1") float vignette = 0.f;
  BPROPERTY(Edit, Range = "0,1", ShowIf = "vignette") float vignetteSmoothness = 0.5f;
  BPROPERTY(Edit, Range = "0,1", Tooltip = "Red and blue split toward the edges") float chromaticAberration = 0.f;
  BPROPERTY(Edit, Range = "0,1") float filmGrain = 0.f;
  BPROPERTY(Edit, Range = "0,1", Tooltip = "Lens flare: ghosts and a halo from the brightest parts (needs bloom: High and up)") float lensFlare = 0.f;
  BPROPERTY(Edit, Header = "Depth of Field", Range = "0,1", Tooltip = "0 = off; how wide out-of-focus parts blur") float dofBlur = 0.f;
  BPROPERTY(Edit, Range = "0.1,1000", ShowIf = "dofBlur", Tooltip = "Metres from the camera that are sharp") float dofFocusDistance = 10.f;
  BPROPERTY(Edit, Range = "0,100", ShowIf = "dofBlur", Tooltip = "Depth around the focus distance that stays sharp (m)") float dofFocusRange = 2.f;
};

// Turns its object while the game is playing (queue 3-6): the first built-in behaviour, so play mode has
// something to run before the script hosts (M4) exist.
BCLASS(Component)
struct Rotator {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Degrees per second around X, Y and Z while playing") Vec3 speed{0, 45, 0};
};

// Greybox block (queue 3-3c): a box, stairs, ramp or cylinder built from its size, for laying out a level before
// the art exists. The pivot is the bottom centre. In the scene view's Blockout tool (B) its faces have handles:
// drag one to push or pull that face (the opposite face stays). Physics will use it as a collider (M6).
BENUM()
enum class BlockoutShape { Box, Stairs, Ramp, Cylinder };

BCLASS(Component)
struct Blockout {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) BlockoutShape shape = BlockoutShape::Box;
  BPROPERTY(Edit, Tooltip = "Width, height, depth in metres (pivot at the bottom centre)") Vec3 size{1, 1, 1};
  BPROPERTY(Edit, Range = "1,64", ShowIf = "shape == Stairs", Tooltip = "Steps from the front (+Z) up to the back") int32_t steps = 6;
  BPROPERTY(Edit, Tooltip = "Project material; empty = the grey 1 m grid") std::string material;
  BPROPERTY(Edit, Tooltip = "Physics uses the block as a collider (M6)") bool collider = true;
};

// Colour grading (3-5b): the final picture through a look-up table (.cube from Resolve, Photoshop ...) and a few
// controls. The first one in the scene is used.
BCLASS(Component)
struct ColorGrading {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Optional .cube file in the project (3D table, 0..1 domain)") std::string lut;
  BPROPERTY(Edit, Range = "0,1", Tooltip = "How much of the grading shows over the original") float intensity = 1.f;
  BPROPERTY(Edit, Range = "0,2") float contrast = 1.f;
  BPROPERTY(Edit, Range = "0,2") float saturation = 1.f;
  BPROPERTY(Edit, Range = "-1,1", Tooltip = "Cool (blue) to warm (orange)") float temperature = 0.f;
  BPROPERTY(Edit, Range = "-1,1", Tooltip = "Green to magenta") float tint = 0.f;
};

BCLASS(Component)
struct Camera {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,179", Tooltip = "Vertical field of view in degrees") float fov = 60.f;
  BPROPERTY(Edit) float nearClip = 0.1f;
  BPROPERTY(Edit) float farClip = 1000.f;
  // Camera shake (E-9, Unreal's StartCameraShake): "bdg:Heavy" or a .camshake asset, times scale. Played by the
  // game (play mode); a Graph node and a script call like any Callable.
  BFUNCTION(Callable) void Shake(std::string shake, float scale) { pendingShakes.emplace_back(std::move(shake), scale); }
  std::vector<std::pair<std::string, float>> pendingShakes; // not saved: taken by play mode each frame
};

// A shake in the world (E-9, Unreal's CameraShakeSourceComponent): full strength for a camera within innerRadius
// metres, fading to nothing at outerRadius.
BCLASS(Component)
struct CameraShakeSource {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "bdg:Light / Medium / Heavy / Explosion / Handheld, or a .camshake asset") std::string shake = "bdg:Medium";
  BPROPERTY(Edit, Range = "0,10") float scale = 1.f;
  BPROPERTY(Edit, Range = "0,1000", Tooltip = "Full strength within this distance (m)") float innerRadius = 2.f;
  BPROPERTY(Edit, Range = "0,1000", Tooltip = "No shake beyond this distance (m)") float outerRadius = 20.f;
  BPROPERTY(Edit, Range = "0.1,8", Tooltip = "How the strength falls between the radii (1 = linear)") float falloff = 1.f;
  BPROPERTY(Edit, Tooltip = "Play when the game starts") bool playOnStart = false;
  BFUNCTION(Button) void Play() { ++pendingPlays; } // while playing: shakes the game camera now
  int32_t pendingPlays = 0; // not saved: taken by play mode
};

// ---- Physics (queue 6-2, Docs/architecture/physics.md). Backend-independent values only; the physics engine is
// picked per body (Simulation) and runs behind the PHI layer (Engine/Physics). Names follow Unity.
BENUM()
enum class RigidbodyType { Static, Dynamic, Kinematic, Character };
BENUM()
enum class PhysicsSimulation { Auto, Jolt, GPU }; // GPU: the L track; until then it falls back to Auto

BCLASS(Component)
struct Rigidbody {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Static: never moves. Dynamic: simulated. Kinematic: moved by its Transform, pushes others. Character: dynamic, upright")
  RigidbodyType type = RigidbodyType::Dynamic;
  BPROPERTY(Edit, Tooltip = "Physics engine for this body. Auto = the project's default (3D: Jolt)") PhysicsSimulation simulation = PhysicsSimulation::Auto;
  BPROPERTY(Edit, Range = "0.001,100000", Tooltip = "Kilograms") float mass = 1.f;
  BPROPERTY(Edit, Range = "0,10", Tooltip = "Slows moving down (air drag)") float linearDamping = 0.05f;
  BPROPERTY(Edit, Range = "0,10", Tooltip = "Slows turning down") float angularDamping = 0.05f;
  BPROPERTY(Edit, Range = "-10,10", Tooltip = "Times the project's gravity (0 = floats)") float gravityScale = 1.f;
  BPROPERTY(Edit, Tooltip = "Never turns (characters, crates that must stay upright)") bool freezeRotation = false;
  BPROPERTY(Edit, Tooltip = "Metres per second: the start velocity, and the current one while playing") Vec3 velocity;
  BPROPERTY(Edit, Tooltip = "Degrees per second around X, Y, Z while playing") Vec3 angularVelocity;
  // Pushes, applied on the next physics step (scripts: rb.AddForce(...)). Force in newtons (over the step), impulse in
  // newton-seconds (at once), torque in newton-metres.
  BFUNCTION(Callable) void AddForce(Vec3 force) { pendingForce = {pendingForce.x + force.x, pendingForce.y + force.y, pendingForce.z + force.z}; }
  BFUNCTION(Callable) void AddImpulse(Vec3 impulse) {
    pendingImpulse = {pendingImpulse.x + impulse.x, pendingImpulse.y + impulse.y, pendingImpulse.z + impulse.z};
  }
  BFUNCTION(Callable) void AddTorque(Vec3 torque) { pendingTorque = {pendingTorque.x + torque.x, pendingTorque.y + torque.y, pendingTorque.z + torque.z}; }
  Vec3 pendingForce, pendingImpulse, pendingTorque; // not saved: taken by the physics step
};

BENUM()
enum class ColliderShape { Auto, Box, Sphere, Capsule, Mesh };

// Collision shape. Without a Rigidbody the object is static level geometry. Auto: the engine picks from what is drawn
// (an exact box / sphere / cylinder for the built-in shapes and greybox blocks, the triangle mesh for static objects,
// the convex hull for moving ones, an upright capsule for characters); the choice is logged with its error.
BCLASS(Component)
struct Collider {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) ColliderShape shape = ColliderShape::Auto;
  BPROPERTY(Edit, Tooltip = "Offset from the object's origin (local, metres)") Vec3 center;
  BPROPERTY(Edit, ShowIf = "shape == Box", Tooltip = "Box size (local, metres; times the object's scale)") Vec3 size{1, 1, 1};
  BPROPERTY(Edit, Range = "0.001,1000", Tooltip = "Sphere and capsule radius (metres)") float radius = 0.5f;
  BPROPERTY(Edit, Range = "0.001,1000", ShowIf = "shape == Capsule", Tooltip = "Capsule height, end to end (metres)") float height = 2.f;
  BPROPERTY(Edit, Tooltip = "Reports what enters it (OnTriggerEnter) instead of blocking") bool isTrigger = false;
};

// Surface of the object's colliders (2D and 3D). Without one: friction 0.6, no bounce.
BCLASS(Component)
struct PhysicsMaterial {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "0,2", Tooltip = "0 = ice, 1 = rubber") float friction = 0.6f;
  BPROPERTY(Edit, Range = "0,1", Tooltip = "Bounciness: 0 = stops, 1 = bounces back fully") float bounciness = 0.f;
};

// 2D physics in the XY plane (Box2D): position x, y and rotation around Z.
BCLASS(Component)
struct Rigidbody2D {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) RigidbodyType type = RigidbodyType::Dynamic;
  BPROPERTY(Edit, Range = "0.001,100000", Tooltip = "Kilograms") float mass = 1.f;
  BPROPERTY(Edit, Range = "0,10") float linearDamping = 0.f;
  BPROPERTY(Edit, Range = "0,10") float angularDamping = 0.05f;
  BPROPERTY(Edit, Range = "-10,10", Tooltip = "Times the project's 2D gravity") float gravityScale = 1.f;
  BPROPERTY(Edit) bool freezeRotation = false;
  BPROPERTY(Edit, Tooltip = "Metres per second") Vec2 velocity;
  BPROPERTY(Edit, Tooltip = "Degrees per second") float angularVelocity = 0.f;
  BFUNCTION(Callable) void AddForce(Vec2 force) { pendingForce = {pendingForce.x + force.x, pendingForce.y + force.y}; }
  BFUNCTION(Callable) void AddImpulse(Vec2 impulse) { pendingImpulse = {pendingImpulse.x + impulse.x, pendingImpulse.y + impulse.y}; }
  Vec2 pendingForce, pendingImpulse; // not saved
};

BENUM()
enum class Collider2DShape { Auto, Box, Circle, Capsule };

BCLASS(Component)
struct Collider2D {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Auto: a circle for spheres / round meshes, else the box around the drawn shape") Collider2DShape shape = Collider2DShape::Auto;
  BPROPERTY(Edit) Vec2 center;
  BPROPERTY(Edit, ShowIf = "shape == Box") Vec2 size{1, 1};
  BPROPERTY(Edit, Range = "0.001,1000") float radius = 0.5f;
  BPROPERTY(Edit, ShowIf = "shape == Capsule", Range = "0.001,1000") float height = 2.f;
  BPROPERTY(Edit) bool isTrigger = false;
};

// ---- Animation (queue 7-1, Docs/features/animation.md). The object's MeshRenderer names a skinned model (.gltf / .glb
// / .fbx); the Animator plays its clips, through a state machine when `graph` names an .animgraph. Events written in
// the graph reach the object's Behaviours as OnAnimationEvent(string name). ----
struct AnimatorOp { // a script call waiting for the next animation update (not saved)
  enum Kind { Play, SetFloat, SetTrigger };
  Kind kind = Play;
  std::string name;
  float value = -1.f; // Play: fade seconds (< 0 = crossFade); SetFloat: the value
};

BCLASS(Component)
struct Animator {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "State machine (.animgraph); empty = the model's clips played by name") std::string graph;
  BPROPERTY(Edit, Tooltip = "State or clip played first (empty = the graph's entry, or the model's first clip)") std::string clip;
  BPROPERTY(Edit, Range = "0,4", Tooltip = "Playback speed") float speed = 1.f;
  BPROPERTY(Edit, Range = "0,2", Tooltip = "Seconds Play() cross-fades over") float crossFade = 0.2f;
  BPROPERTY(Edit, Tooltip = "Move the object by the root joint's horizontal motion instead of playing it in place") bool rootMotion = false;
  // Script calls (C#: animator.Play("Run"), SetFloat("Speed", 3), SetTrigger("Jump")); applied on the next update.
  BFUNCTION(Callable) void Play(std::string state) { pending.push_back({AnimatorOp::Play, std::move(state), -1.f}); }
  BFUNCTION(Callable) void CrossFade(std::string state, float seconds) { pending.push_back({AnimatorOp::Play, std::move(state), seconds < 0 ? 0.f : seconds}); }
  BFUNCTION(Callable) void SetFloat(std::string name, float value) { pending.push_back({AnimatorOp::SetFloat, std::move(name), value}); }
  BFUNCTION(Callable) void SetBool(std::string name, bool value) { pending.push_back({AnimatorOp::SetFloat, std::move(name), value ? 1.f : 0.f}); }
  BFUNCTION(Callable) void SetInteger(std::string name, int32_t value) { pending.push_back({AnimatorOp::SetFloat, std::move(name), (float)value}); }
  BFUNCTION(Callable) void SetTrigger(std::string name) { pending.push_back({AnimatorOp::SetTrigger, std::move(name), 1.f}); }
  BFUNCTION(Callable) float GetFloat(std::string name) const {
    for (auto it = pending.rbegin(); it != pending.rend(); ++it)
      if (it->kind != AnimatorOp::Play && it->name == name) return it->value;
    for (const auto& [n, v] : liveParams)
      if (n == name) return v;
    return 0.f;
  }
  BFUNCTION(Callable) bool GetBool(std::string name) const { return GetFloat(std::move(name)) != 0.f; }
  BFUNCTION(Callable) std::string CurrentState() const { return liveState; }
  std::vector<AnimatorOp> pending;                         // not saved: taken by the animation update
  std::string liveState;                                   // not saved: the state playing (or faded into), while playing
  std::vector<std::pair<std::string, float>> liveParams;   // not saved: graph variables, while playing (Inspector)
  float liveFade = 0.f;                                    // not saved: cross-fade progress 0..1 (0 = not fading)
};
}
