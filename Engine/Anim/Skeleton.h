#pragma once
// Skeletal animation data (queue 7-1, Docs/features/animation.md): skeletons, skin weights, clips and poses, as the
// glTF (cgltf) and FBX (ufbx) importers produce them. Model space = the imported file's space (Y up, metres); a
// skinned vertex lands at sum(weight * skin[joint]) * position, skin[j] = jointModel[j] * inverseBind[j].
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
struct Joint {
  std::string name;
  int parent = -1;                                     // always a lower index (parents come first)
  glm::vec3 t{0.f};                                    // rest pose, local to the parent
  glm::quat r{1.f, 0.f, 0.f, 0.f};
  glm::vec3 s{1.f};
  glm::mat4 inverseBind{1.f};                          // model space -> joint space at bind time
  bool skinned = false;                                // a skin weights vertices to it
};

struct Skeleton {
  std::vector<Joint> joints;
  int root = -1; // root motion joint: the topmost joint a skin uses (hips or root bone)
  int Find(std::string_view name) const;
  bool Empty() const { return joints.empty(); }
};

// Four influences per vertex (the largest ones, normalised).
struct SkinVertex {
  uint16_t joint[4] = {0, 0, 0, 0};
  float weight[4] = {1.f, 0.f, 0.f, 0.f};
};
// Keeps the 4 largest of `n` influences and makes the weights sum to 1 (none: joint 0, weight 1).
SkinVertex MakeSkinVertex(const int* joints, const float* weights, int n);

struct AnimChannel {
  enum Path : uint8_t { Translation, Rotation, Scale };
  int joint = -1;
  Path path = Translation;
  bool step = false;              // hold each key (glTF STEP); otherwise linear (rotations: slerp)
  std::vector<float> times;       // seconds, ascending
  std::vector<glm::vec4> values;  // xyz (translation, scale) or quaternion xyzw
};

struct AnimClip {
  std::string name;
  float duration = 0;
  std::vector<AnimChannel> channels;
};

// Local transforms of every joint.
struct LocalPose {
  std::vector<glm::vec3> t, s;
  std::vector<glm::quat> r;
  size_t Size() const { return t.size(); }
};

void RestPose(const Skeleton& sk, LocalPose& pose);
// Overwrites the joints the clip animates with their value at `time` (clamped to the clip); others keep theirs.
void SampleClip(const AnimClip& clip, float time, LocalPose& pose);
// a = a * (1 - w) + b * w per joint (rotations: shortest-path normalised lerp).
void BlendPose(LocalPose& a, const LocalPose& b, float w);
void PoseToModel(const Skeleton& sk, const LocalPose& pose, std::vector<glm::mat4>& model);
void ModelToSkin(const Skeleton& sk, const std::vector<glm::mat4>& model, std::vector<glm::mat4>& skin);
glm::mat4 JointLocalMatrix(const LocalPose& pose, size_t j);

// Bounds that follow the pose: per joint, the box of the bind-pose vertices it moves; a pose's bounds are these boxes
// moved by the skin matrices (exact enough for culling and selection, no vertex loop per frame).
struct JointBox {
  glm::vec3 lo{1e30f}, hi{-1e30f};
  bool Valid() const { return lo.x <= hi.x; }
};
void JointBoxes(const float* positions, size_t stride, const std::vector<SkinVertex>& skin, size_t jointCount, std::vector<JointBox>& out);
bool PoseBounds(const std::vector<JointBox>& boxes, const std::vector<glm::mat4>& skin, glm::vec3& lo, glm::vec3& hi);

// Imported skeletal data of one model file: shared by the renderer (skins) and the animation system (clips).
struct SkinnedModelData {
  Skeleton skeleton;
  std::vector<AnimClip> clips;
  const AnimClip* Clip(std::string_view name) const; // by name; "" = the first clip
};
}
