#pragma once
// Generated test character (queue 7-1): no downloaded asset is needed to check skeletal animation. A 2 m square
// column skinned to three joints (Root at the floor, Spine at 0.8 m, Head at 1.6 m, under an "Armature" node) with
// three clips: Idle (2 s, the spine sways), Run (1 s, the root moves 3 m forward along -Z and the spine leans
// forward) and Jump (0.8 s, the root goes 1 m up and back). TestRigAnimGraph is a state machine for it
// (Idle <-> Run on Speed, any -> Jump on the Jump trigger, Jump -> Idle at its end) with Footstep / Land events.
#include <string>

namespace bdg {
std::string MakeTestRigGltf();   // a .gltf with its buffer embedded (data URI)
std::string TestRigAnimGraph();  // .animgraph text; the skeleton is "Models/Rig.gltf"
// A generated humanoid (CC0, 7-1 part 2 / 7-1b): 17 joints (Hips, Spine, Chest, Neck, Head, Left/Right UpperArm,
// LowerArm, Hand, UpperLeg, LowerLeg, Foot) under "Armature", boxes skinned to them, 1.92 m tall, standing on y = 0,
// facing +Z. Clips: Idle, Walk, Run, WalkBack, StrafeLeft, StrafeRight (loops), Jump, Wave (right arm), LeanAdd (an
// additive spine lean). HumanoidAnimGraph: Move (1D blend space on Speed: Idle 0, Walk 1.6, Run 5 m/s, Footstep
// events), Strafe (2D on MoveX / MoveY), Jump, an UpperBody override layer (Wave, weight from the Wave variable) and
// an additive Lean layer (weight from Lean); the skeleton is "Models/Hero.gltf".
std::string MakeHumanoidGltf();
std::string HumanoidAnimGraph();
}
