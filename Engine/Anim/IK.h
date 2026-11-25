#pragma once
// Inverse kinematics, first usable part (queue 7-1b, Docs/features/animation.md "IK와 물리 애니메이션"). Solvers work
// on a pose in model space: they change local rotations (and keep model matrices up to date), so they run after the
// clips are sampled and before skinning. Targets are model-space points.
//   - TwoBoneIK: upper / middle / end joint (thigh, knee, foot or arm, elbow, hand), analytic, bends towards a pole.
//   - LookAt: a chain (spine ... head) turns so a joint's forward axis points at a target; each joint takes a share
//     of what is left (the spine shares make an aim offset), limited to a maximum angle.
//   - Fabrik: any chain (tails, tentacles, arms with more joints) reaches for a target (FABRIK, Aristidou 2011).
#include "Anim/Skeleton.h"
#include <utility>
#include <vector>

namespace bdg {
glm::quat RotationBetween(const glm::vec3& from, const glm::vec3& to); // shortest arc, unit vectors not required
glm::quat ModelRotation(const glm::mat4& m);                            // rotation of a model matrix (scale removed)
// Sets joint j's rotation in model space (its local rotation follows) and updates the model matrices.
void SetModelRotation(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, int j, const glm::quat& rot);

// false: joints missing / degenerate. weight 0..1 blends from the current pose.
bool TwoBoneIK(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, int upper, int mid, int end, const glm::vec3& target,
               const glm::vec3& pole, float weight = 1.f);
// chain: (joint, share of the remaining turn), root first; the last joint's forward axis is `forward` (its own space).
float LookAt(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, const std::vector<std::pair<int, float>>& chain,
             const glm::vec3& forward, const glm::vec3& target, float weight, float maxDegrees);
// chain: joint indices, root first; the tip is chain.back(). Returns the tip's distance to the target afterwards.
float Fabrik(const Skeleton& sk, LocalPose& pose, std::vector<glm::mat4>& model, const std::vector<int>& chain, const glm::vec3& target,
             int iterations = 12, float tolerance = 0.002f);
// Joint and its ancestors up to `count` joints, root first (count 3 from a foot: thigh, knee, foot). Empty if too short.
std::vector<int> JointChain(const Skeleton& sk, int tip, int count);
}
