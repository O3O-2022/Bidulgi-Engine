#pragma once
// Animation in play mode (queue 7-1): every object with an Animator and a MeshRenderer naming a skinned model gets a
// runtime (AnimatorRuntime: graph, variables, cross-fades). Each frame it takes the script calls waiting on the
// component (Play, SetFloat, SetTrigger), advances, samples and blends the clips into joint matrices (the Scene and
// Game views skin with them on the GPU), moves the Transform by the root joint when rootMotion is on, writes the live
// state and variables back for the Inspector, and returns the events crossed (PlayMode hands them to C# as
// OnAnimationEvent). Outside play mode skinned models show their rest pose.
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
class Scene;

// Skeletal data of a project model by path (imported once without textures; the Scene view hands over what it
// imported so the file is read once). Null: not a skinned model, or not readable.
std::shared_ptr<const SkinnedModelData> SkinnedModel(const AssetDatabase* assets, const std::string& path);
void RememberSkinnedModel(const std::string& path, uint64_t hash, std::shared_ptr<const SkinnedModelData> data);
void ForgetSkinnedModels();

class AnimationSystem {
public:
  void Begin(const AssetDatabase* assets);
  void End();
  bool Running() const { return running_; }
  struct Event { std::string object, name, state; };
  void Tick(Scene& scene, float dt, std::vector<Event>& events);

  struct Pose {
    std::string mesh;                // the model it belongs to (MeshRenderer.mesh)
    std::vector<glm::mat4> skin;     // per joint: jointModel * inverseBind
    uint64_t serial = 0;             // changes every update
  };
  const Pose* PoseOf(std::string_view objectId) const;
  const AnimatorRuntime* RuntimeOf(std::string_view objectId) const;
  size_t Count() const { return insts_.size(); }
  uint64_t EventsSent() const { return eventsSent_; }
  // The running system (play mode or the built player); null outside play. The views read poses through it.
  static const AnimationSystem* Active();

private:
  struct Inst {
    std::string mesh, graphPath;
    std::shared_ptr<const SkinnedModelData> data;
    std::shared_ptr<AnimGraphDef> graph;
    AnimatorRuntime rt;
    Pose pose;
    bool seen = false;
  };
  std::map<std::string, Inst, std::less<>> insts_;
  std::map<std::string, std::shared_ptr<AnimGraphDef>> graphs_; // by path; null = could not be read (warned once)
  const AssetDatabase* assets_ = nullptr;
  bool running_ = false;
  uint64_t eventsSent_ = 0;
  std::shared_ptr<AnimGraphDef> LoadGraph(const std::string& path);
  void Evaluate(Inst& in, Scene& scene, const std::string& id, bool rootMotion);
};
}
