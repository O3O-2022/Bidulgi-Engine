// Animation part 2 and IK (queue 7-1, 7-1b): blend spaces, layers, the generated humanoid, IK solvers, raycasts,
// foot IK on a step, ragdolls.
#include "bdg_test.h"
#include "Anim/AnimGraph.h"
#include "Anim/AnimationSystem.h"
#include "Anim/IK.h"
#include "Anim/TestRig.h"
#include "Assets/GltfImport.h"
#include "Build/Templates.h"
#include "Core/Utf8Path.h"
#include <filesystem>
#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <cmath>

using namespace bdg;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

std::shared_ptr<SkinnedModelData> Humanoid() {
  static std::shared_ptr<SkinnedModelData> data;
  if (data) return data;
  const std::string text = MakeHumanoidGltf();
  ImportedModel im;
  std::string err;
  if (ImportGltfMemory(text.data(), text.size(), ".", im, &err)) data = im.skinned;
  return data;
}
std::shared_ptr<AnimGraphDef> HumanoidGraph() {
  auto g = std::make_shared<AnimGraphDef>();
  std::string err;
  if (!ParseAnimGraph(HumanoidAnimGraph(), *g, &err)) return nullptr;
  return g;
}
float Weight(const std::vector<AnimatorRuntime::ClipSample>& s, const char* clip) {
  for (const auto& c : s)
    if (c.clip == clip) return c.weight;
  return 0.f;
}
glm::vec3 P(const glm::mat4& m) { return glm::vec3(m[3]); }
}

TEST_CASE("blend spaces and layers: 1D / 2D weights, normalised time, override and additive layers (queue 7-1, part 2)") {
  auto data = Humanoid();
  CHECK(data != nullptr);
  if (!data) return;
  CHECK_EQ(data->skeleton.joints.size(), (size_t)18);
  CHECK(data->skeleton.joints[data->skeleton.root].name == "Hips");
  for (const char* c : {"Idle", "Walk", "Run", "WalkBack", "StrafeLeft", "StrafeRight", "Jump", "Wave", "LeanAdd"}) CHECK(data->Clip(c) != nullptr);

  using S = AnimGraphDef::BlendSample;
  const std::vector<S> line = {{"Idle", 0, 0}, {"Walk", 1.6f, 0}, {"Run", 5, 0}};
  std::vector<float> w;
  BlendSpaceWeights(line, false, 0.8f, 0, w);
  CHECK(Near(w[0], 0.5f) && Near(w[1], 0.5f) && w[2] == 0);
  BlendSpaceWeights(line, false, 3.3f, 0, w);
  CHECK(Near(w[1], 0.5f) && Near(w[2], 0.5f));
  BlendSpaceWeights(line, false, 9, 0, w);
  CHECK(w[2] == 1 && w[0] == 0);
  const std::vector<S> plane = {{"Idle", 0, 0}, {"Walk", 0, 1}, {"WalkBack", 0, -1}, {"StrafeLeft", -1, 0}, {"StrafeRight", 1, 0}};
  BlendSpaceWeights(plane, true, 0, 1, w);
  CHECK(Near(w[1], 1.f)); // exact at a sample
  BlendSpaceWeights(plane, true, 0.5f, 0.5f, w);
  float sum = 0;
  for (float v : w) { sum += v; CHECK(v >= 0); }
  CHECK(Near(sum, 1.f) && w[1] > 0.2f && w[4] > 0.2f && w[2] == 0 && w[3] == 0);

  auto g = HumanoidGraph();
  CHECK(g != nullptr);
  if (!g) return;
  CHECK(g->machines.size() == 3 && g->layers.size() == 2 && g->layers[0].weightVar == "Wave" && g->layers[1].additive);
  CHECK(g->machines[0].states[0].IsBlend() && g->machines[0].states[1].blendY == "MoveY");
  AnimGraphDef g2;
  std::string err;
  CHECK(ParseAnimGraph(WriteAnimGraph(*g), g2, &err));
  CHECK(WriteAnimGraph(g2) == WriteAnimGraph(*g));
  CHECK(!ParseAnimGraph(R"({"vars":{},"stateMachines":{"M":{"states":{"A":{"blendSpace":{"x":"Nope","samples":[{"clip":"Idle"}]}}}}}})", g2, &err));
  CHECK(!ParseAnimGraph(R"({"vars":{},"stateMachines":{"M":{"states":{"A":{}}}},"layers":[{"machine":"X"}]})", g2, &err));

  AnimatorRuntime rt;
  rt.Init(g.get(), [data](const std::string& c) { const AnimClip* a = data->Clip(c); return a ? a->duration : -1.f; });
  rt.Start("");
  CHECK(rt.StateName() == "Move" && rt.Current().blend);
  rt.SetFloat("Speed", 3.3f);
  std::vector<AnimatorRuntime::ClipSample> s;
  rt.Samples(rt.Current(), s);
  CHECK(Near(Weight(s, "Walk"), 0.5f) && Near(Weight(s, "Run"), 0.5f) && Weight(s, "Idle") == 0);
  // normalised time: the mix lasts 0.5 * 1.0 + 0.5 * 0.66 = 0.83 s; both clips stay in step
  std::vector<AnimatorRuntime::FiredEvent> ev;
  for (int i = 0; i < 25; ++i) rt.Update(1.f / 60, 1, ev);
  CHECK(Near(rt.Current().time, (25.f / 60) / 0.83f, 0.01f));
  rt.Samples(rt.Current(), s);
  CHECK(Near(s[0].time / s[0].duration, s[1].time / s[1].duration, 1e-4f));
  CHECK(ev.size() == 1 && ev[0].name == "Footstep"); // at 0.25 of the cycle
  rt.SetFloat("Strafe", 1);
  rt.SetFloat("MoveX", 1);
  rt.Update(1.f / 60, 1, ev);
  CHECK(rt.StateName() == "Strafe");

  // layers through the animation system: the Wave override moves only the right arm, the Lean layer adds to the spine
  RememberSkinnedModel("Tests/Hero.gltf", 0, data);
  RememberAnimGraph("Tests/Hero.animgraph", g);
  Scene scene;
  scene.Create("hero", "Hero");
  CHECK(scene.AddComponent("hero", Transform::StaticType(), "", &err));
  CHECK(scene.AddComponent("hero", MeshRenderer::StaticType(), R"({"mesh":"Tests/Hero.gltf"})", &err));
  CHECK(scene.AddComponent("hero", Animator::StaticType(), R"({"graph":"Tests/Hero.animgraph"})", &err));
  AnimationSystem sys;
  sys.Begin(nullptr);
  std::vector<AnimationSystem::Event> events;
  sys.Tick(scene, 1.f / 60, events);
  const Skeleton& sk = data->skeleton;
  const int rh = sk.Find("RightHand"), lh = sk.Find("LeftHand"), chest = sk.Find("Chest");
  const glm::vec3 rh0 = P(sys.PoseOf("hero")->model[rh]), lh0 = P(sys.PoseOf("hero")->model[lh]), chest0 = P(sys.PoseOf("hero")->model[chest]);
  Animator& a = *static_cast<Animator*>(scene.Find("hero")->Find("Animator")->data);
  a.SetFloat("Wave", 1);
  sys.Tick(scene, 1.f / 60, events);
  CHECK(P(sys.PoseOf("hero")->model[rh]).y > rh0.y + 0.5f);              // the right hand up
  CHECK(glm::length(P(sys.PoseOf("hero")->model[lh]) - lh0) < 0.02f);   // the left one where it was
  CHECK(a.liveBlend.find("Idle 1.00") != std::string::npos);
  a.SetFloat("Wave", 0);
  a.SetFloat("Lean", 1);
  sys.Tick(scene, 1.f / 60, events);
  CHECK(P(sys.PoseOf("hero")->model[chest]).x < chest0.x - 0.03f); // leaning to the side (spine roll)
  a.SetFloat("Lean", 0);
  a.SetLayerWeight("Lean", 1); // the variable wins over SetLayerWeight
  sys.Tick(scene, 1.f / 60, events);
  CHECK(Near(P(sys.PoseOf("hero")->model[chest]).x, chest0.x, 0.01f));
  a.SetFloat("Speed", 1.6f);
  sys.Tick(scene, 1.f / 60, events);
  CHECK(a.liveBlend.find("Walk 1.00") != std::string::npos);
  sys.End();
  RememberAnimGraph("Tests/Hero.animgraph", nullptr);
}

TEST_CASE("IK solvers: two-bone, look-at with an aim offset, FABRIK (queue 7-1b)") {
  auto data = Humanoid();
  if (!data) return;
  const Skeleton& sk = data->skeleton;
  LocalPose pose;
  RestPose(sk, pose);
  std::vector<glm::mat4> model;
  PoseToModel(sk, pose, model);
  const std::vector<int> leg = JointChain(sk, sk.Find("LeftFoot"), 3);
  CHECK(leg.size() == 3 && sk.joints[leg[0]].name == "LeftUpperLeg" && sk.joints[leg[1]].name == "LeftLowerLeg");
  const glm::vec3 foot0 = P(model[leg[2]]), target = foot0 + glm::vec3(0, 0.25f, 0.05f);
  const glm::quat footRot0 = ModelRotation(model[leg[2]]);
  CHECK(TwoBoneIK(sk, pose, model, leg[0], leg[1], leg[2], target, glm::vec3(0, 0, 1)));
  CHECK(glm::length(P(model[leg[2]]) - target) < 0.002f);
  CHECK(P(model[leg[1]]).z > P(model[leg[0]]).z + 0.05f);                  // the knee bends forward
  CHECK(std::fabs(glm::dot(ModelRotation(model[leg[2]]), footRot0)) > 0.9999f); // the foot keeps its orientation
  CHECK(Near(glm::length(P(model[leg[1]]) - P(model[leg[0]])), 0.441f, 0.002f)); // bones keep their length
  // out of reach: straight towards it
  RestPose(sk, pose);
  PoseToModel(sk, pose, model);
  CHECK(TwoBoneIK(sk, pose, model, leg[0], leg[1], leg[2], foot0 + glm::vec3(0, -1, 0), glm::vec3(0, 0, 1)));
  CHECK(P(model[leg[2]]).y < foot0.y);

  // look-at: a target to the character's left (+X); the head turns, the chest takes part of it
  RestPose(sk, pose);
  PoseToModel(sk, pose, model);
  const int head = sk.Find("Head");
  const glm::vec3 fwd = glm::inverse(ModelRotation(model[head])) * glm::vec3(0, 0, 1);
  const glm::vec3 look = P(model[head]) + glm::vec3(2, 0, 2);
  std::vector<std::pair<int, float>> chain;
  for (int j : JointChain(sk, head, 3)) chain.push_back({j, j == head ? 1.f : 0.2f});
  const float err = LookAt(sk, pose, model, chain, fwd, look, 1, 80);
  CHECK(err < 1.f);
  const glm::vec3 nowFwd = ModelRotation(model[head]) * fwd;
  CHECK(nowFwd.x > 0.6f);
  CHECK(std::fabs(pose.r[sk.Find("Chest")].w) < 0.9999f); // the aim offset: the bones above the head turned too
  // limited: a target behind turns at most maxDegrees
  RestPose(sk, pose);
  PoseToModel(sk, pose, model);
  const float behind = LookAt(sk, pose, model, chain, fwd, P(model[head]) + glm::vec3(0, 0, -3), 1, 60);
  CHECK(behind > 100.f);
  CHECK(Near(glm::degrees(std::acos(std::clamp(glm::dot(ModelRotation(model[head]) * fwd, glm::vec3(0, 0, 1)), -1.f, 1.f))), 60.f, 1.f));

  // FABRIK: the right arm reaches a point in front
  RestPose(sk, pose);
  PoseToModel(sk, pose, model);
  const std::vector<int> arm = JointChain(sk, sk.Find("RightHand"), 3);
  const glm::vec3 reach = P(model[arm[0]]) + glm::vec3(0, -0.1f, 0.4f);
  CHECK(Fabrik(sk, pose, model, arm, reach) < 0.01f);
  CHECK(glm::length(P(model[arm[2]]) - reach) < 0.01f);
}

TEST_CASE("physics for IK: raycasts, foot IK on a step, ragdoll falls (queue 7-1b)") {
  auto data = Humanoid();
  if (!data) return;
  RememberSkinnedModel("Tests/Hero.gltf", 0, data);
  Scene scene;
  std::string err;
  auto add = [&](const char* id, const char* comps) {
    scene.Create(id, id);
    CHECK(scene.AddComponent(id, Transform::StaticType(), "", &err));
    (void)comps;
  };
  auto set = [&](const char* id, Vec3 pos, Vec3 scale) {
    Transform& t = *static_cast<Transform*>(scene.Find(id)->Find("Transform")->data);
    t.position = pos;
    t.scale = scale;
  };
  add("ground", "");
  set("ground", {0, -0.5f, 0}, {20, 1, 20});
  CHECK(scene.AddComponent("ground", Collider::StaticType(), R"({"shape":"Box"})", &err));
  add("step", "");
  set("step", {0.1f, 0.1f, 0.05f}, {0.3f, 0.2f, 0.6f}); // top at 0.2 m, under the left foot only
  CHECK(scene.AddComponent("step", Collider::StaticType(), R"({"shape":"Box"})", &err));
  add("hero", "");
  CHECK(scene.AddComponent("hero", MeshRenderer::StaticType(), R"({"mesh":"Tests/Hero.gltf"})", &err));
  CHECK(scene.AddComponent("hero", Animator::StaticType(), R"({"clip":"Idle"})", &err));
  CHECK(scene.AddComponent("hero", AnimatorIK::StaticType(), "", &err));
  add("doll", "");
  set("doll", {3, 0, 0}, {1, 1, 1});
  CHECK(scene.AddComponent("doll", MeshRenderer::StaticType(), R"({"mesh":"Tests/Hero.gltf"})", &err));
  CHECK(scene.AddComponent("doll", Animator::StaticType(), R"({"clip":"Idle"})", &err));
  CHECK(scene.AddComponent("doll", Ragdoll::StaticType(), "", &err));

  PhysicsWorld physics;
  physics.Begin(scene, {}, nullptr);
  PhysicsWorld::RayResult hit;
  CHECK(physics.Raycast({0.1f, 2, 0}, {0, -1, 0}, 5, hit) && hit.object == "step" && Near(hit.point.y, 0.2f, 0.01f) && Near(hit.normal.y, 1.f, 0.01f));
  CHECK(physics.Raycast({0.1f, 2, 0}, {0, -1, 0}, 5, hit, {"step"}) && hit.object == "ground" && Near(hit.point.y, 0.f, 0.01f));
  CHECK(!physics.Raycast({0.1f, 2, 0}, {0, 1, 0}, 5, hit));

  AnimationSystem sys;
  sys.Begin(nullptr);
  std::vector<AnimationSystem::Event> events;
  const Skeleton& sk = data->skeleton;
  const int lf = sk.Find("LeftFoot"), rf = sk.Find("RightFoot"), head = sk.Find("Head");
  for (int i = 0; i < 30; ++i) {
    physics.Step(scene, 1.f / 60);
    sys.Tick(scene, 1.f / 60, events, &physics);
  }
  const AnimatorIK& ik = *static_cast<const AnimatorIK*>(scene.Find("hero")->Find("AnimatorIK")->data);
  const AnimationSystem::Pose* pose = sys.PoseOf("hero");
  CHECK(pose != nullptr);
  if (pose) {
    CHECK(Near(ik.liveLeftLift, 0.2f, 0.01f) && Near(ik.liveRightLift, 0.f, 0.01f));
    CHECK(Near(P(pose->model[lf]).y - P(pose->model[rf]).y, 0.2f, 0.01f)); // the left foot on the step
    CHECK(Near(ik.liveHipDrop, 0.f, 1e-3f));
  }
  // the doll goes limp and falls: the head ends near the ground
  const float head0 = P(sys.PoseOf("doll")->model[head]).y;
  Ragdoll& rag = *static_cast<Ragdoll*>(scene.Find("doll")->Find("Ragdoll")->data);
  rag.Activate();
  for (int i = 0; i < 150; ++i) {
    sys.Tick(scene, 1.f / 60, events, &physics);
    physics.Step(scene, 1.f / 60);
  }
  CHECK(sys.RagdollActive("doll") && physics.Ragdolls() == 1);
  const float head1 = P(sys.PoseOf("doll")->model[head]).y;
  CHECK(head0 > 1.6f && head1 < 0.6f);
  rag.Deactivate();
  sys.Tick(scene, 1.f / 60, events, &physics);
  CHECK(!sys.RagdollActive("doll") && physics.Ragdolls() == 0);
  sys.End();
  physics.End();
  ForgetSkinnedModels();
}

TEST_CASE("third-person template: generated character, its graph, files (queue 7-1b)") {
  namespace fs = std::filesystem;
  // the template's graph is the one the engine generates (kept in step)
  std::string text;
  CHECK(ReadTextU8(std::string(BDG_SOURCE_DIR) + "/Templates/thirdperson/Assets/Anim/Hero.animgraph", text));
  AnimGraphDef a, b;
  std::string err;
  CHECK(ParseAnimGraph(text, a, &err) && ParseAnimGraph(HumanoidAnimGraph(), b, &err));
  CHECK(WriteAnimGraph(a) == WriteAnimGraph(b));
  CHECK(IsGameTemplate("thirdperson"));
  const fs::path dir = fs::temp_directory_path() / "bdg_tp_template";
  std::error_code ec;
  fs::remove_all(dir, ec);
  TemplateResult out;
  CHECK(CreateFromTemplate("thirdperson", dir.string(), "Hero Test", out, &err, false));
  ImportedModel m;
  CHECK(ImportGltf(dir / "Assets" / "Models" / "Hero.gltf", m, &err) && m.skinned && m.skinned->clips.size() == 9);
  for (const char* f : {"Scenes/Main.scene", "Assets/Player/ThirdPersonController.cs", "Assets/Camera/FollowCamera.cs", "Input/Actions.jsonl",
                        "Assets/Audio/Jump.wav", "ProjectSettings/Project.json"})
    CHECK(fs::exists(dir / f));
  fs::remove_all(dir, ec);
}
