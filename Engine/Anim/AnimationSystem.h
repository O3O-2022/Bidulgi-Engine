#pragma once
// Animation in play mode (queue 7-1, 7-1b): every object with an Animator and a MeshRenderer naming a skinned model gets
// a runtime per graph layer (AnimatorRuntime: machine, shared variables, cross-fades). Each frame it takes the script
// calls waiting on the component (Play, SetFloat, SetTrigger, SetLayerWeight), advances the layers, samples and mixes
// the clips (blend spaces, override / additive layers over bone masks), moves the Transform by the root joint when
// rootMotion is on, runs the object's AnimatorIK (feet on the ground through physics raycasts, look-at / aim offset,
// reach, FABRIK chains), hands the skeleton to physics when its Ragdoll is active, and turns the pose into joint
// matrices (the Scene and Game views skin with them on the GPU). Live state, blend weights and variables go back to
// the component for the Inspector; the events crossed go back to PlayMode (C# OnAnimationEvent).
// Outside play mode skinned models show their rest pose.
#include "Anim/AnimGraph.h"
#include "Anim/Skeleton.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
class AssetDatabase;
class PhysicsWorld;
class Scene;
struct SceneObject;
struct AnimatorIK;
struct Ragdoll;

// Skeletal data of a project model by path (imported once without textures; the Scene view hands over what it
// imported so the file is read once). Null: not a skinned model, or not readable.
std::shared_ptr<const SkinnedModelData> SkinnedModel(const AssetDatabase* assets, const std::string& path);
void RememberSkinnedModel(const std::string& path, uint64_t hash, std::shared_ptr<const SkinnedModelData> data);
void ForgetSkinnedModels();
// A graph by path without a project (tests, generated content); found before the asset database.
void RememberAnimGraph(const std::string& path, std::shared_ptr<AnimGraphDef> graph);

class AnimationSystem {
public:
  void Begin(const AssetDatabase* assets);
  void End();
  bool Running() const { return running_; }
  struct Event { std::string object, name, state; };
  // physics: raycasts for foot IK and the bodies of ragdolls (null: no foot IK, no ragdolls)
  void Tick(Scene& scene, float dt, std::vector<Event>& events, PhysicsWorld* physics = nullptr);

  struct Pose {
    std::string mesh;                // the model it belongs to (MeshRenderer.mesh)
    std::vector<glm::mat4> skin;     // per joint: jointModel * inverseBind
    std::vector<glm::mat4> model;    // per joint: model space (tests, gizmos)
    uint64_t serial = 0;             // changes every update
  };
  const Pose* PoseOf(std::string_view objectId) const;
  const AnimatorRuntime* RuntimeOf(std::string_view objectId, size_t layer = 0) const;
  size_t Count() const { return insts_.size(); }
  uint64_t EventsSent() const { return eventsSent_; }
  bool RagdollActive(std::string_view objectId) const;
  // The running system (play mode or the built player); null outside play. The views read poses through it.
  static const AnimationSystem* Active();

private:
  struct Inst {
    std::string mesh, graphPath;
    std::shared_ptr<const SkinnedModelData> data;
    std::shared_ptr<AnimGraphDef> graph;
    std::vector<AnimatorRuntime> layers;           // [0] base, then the graph's layers
    std::vector<std::vector<char>> masks;          // per extra layer: joints it drives
    std::map<std::string, float> layerWeights;     // SetLayerWeight overrides by layer name
    std::shared_ptr<AnimatorRuntime::Params> params;
    std::vector<glm::mat4> restModel;
    Pose pose;
    float lift[2] = {0, 0}, hipDrop = 0;           // foot IK, smoothed
    int ragdoll = -1;                              // PhysicsWorld handle
    std::vector<int> ragdollBones;                 // joint -> bone index (-1: not simulated)
    std::vector<glm::mat4> boneToJoint;            // per bone: body frame -> joint frame
    bool seen = false;
  };
  std::map<std::string, Inst, std::less<>> insts_;
  std::map<std::string, std::shared_ptr<AnimGraphDef>> graphs_; // by path; null = could not be read (warned once)
  const AssetDatabase* assets_ = nullptr;
  bool running_ = false;
  uint64_t eventsSent_ = 0;
  std::shared_ptr<AnimGraphDef> LoadGraph(const std::string& path);
  void SetUp(Inst& in, const SceneObject& o, const std::string& id, const std::string& clip);
  void SampleLayer(const Inst& in, const AnimatorRuntime& rt, bool atStart, LocalPose& pose) const;
  void Evaluate(Inst& in, Scene& scene, const std::string& id, bool rootMotion, float dt, PhysicsWorld* physics);
  void ApplyIK(Inst& in, Scene& scene, const std::string& id, AnimatorIK& ik, LocalPose& pose, std::vector<glm::mat4>& model, float dt,
               PhysicsWorld* physics);
  bool ApplyRagdoll(Inst& in, Scene& scene, const std::string& id, const Ragdoll& rag, const LocalPose& pose, std::vector<glm::mat4>& model,
                    PhysicsWorld* physics);
};
}
