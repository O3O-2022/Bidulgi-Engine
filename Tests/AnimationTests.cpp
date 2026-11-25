#include "bdg_test.h"
#include "Anim/AnimGraph.h"
#include "Anim/AnimationSystem.h"
#include "Anim/Skeleton.h"
#include "Anim/TestRig.h"
#include "Assets/GltfImport.h"
#include "Assets/ModelImport.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace bdg;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

std::shared_ptr<SkinnedModelData> Rig(ImportedModel& im) {
  const std::string text = MakeTestRigGltf();
  std::string err;
  if (!ImportGltfMemory(text.data(), text.size(), ".", im, &err)) return nullptr;
  return im.skinned;
}
}

TEST_CASE("skeletal import: skin, skeleton and clips from glTF; poses, blending, pose bounds (queue 7-1)") {
  ImportedModel im;
  auto data = Rig(im);
  CHECK(data != nullptr);
  if (!data) return;
  const Skeleton& sk = data->skeleton;
  CHECK_EQ(sk.joints.size(), (size_t)4); // Armature (an ancestor, not skinned) + Root, Spine, Head
  CHECK(sk.Find("Armature") == 0 && sk.Find("Root") == 1 && sk.Find("Spine") == 2 && sk.Find("Head") == 3);
  CHECK(!sk.joints[0].skinned && sk.joints[1].skinned && sk.joints[3].skinned);
  CHECK_EQ(sk.root, 1); // the topmost skinned joint moves the character (root motion)
  for (size_t j = 1; j < sk.joints.size(); ++j) CHECK(sk.joints[j].parent == (int)j - 1);
  CHECK_EQ(data->clips.size(), (size_t)3);
  CHECK(data->Clip("Idle") && Near(data->Clip("Idle")->duration, 2.f));
  CHECK(data->Clip("run") && Near(data->Clip("Run")->duration, 1.f)); // names also match without case
  CHECK(data->Clip("Jump") && Near(data->Clip("Jump")->duration, 0.8f));
  CHECK(data->Clip("") == &data->clips[0] && !data->Clip("Swim"));
  CHECK_EQ(im.primitives.size(), (size_t)1);
  CHECK_EQ(im.primitives[0].skin.size(), im.primitives[0].mesh.vertices.size()); // the skin stream survived the optimiser
  CHECK(im.instances.size() == 1 && im.instances[0].world == glm::mat4(1.f));      // skinned: placed by the joints
  CHECK(Near(im.boundsMin.y, 0.f) && Near(im.boundsMax.y, 2.f) && Near(im.boundsMax.x, 0.2f));
  for (const SkinVertex& v : im.primitives[0].skin) CHECK(Near(v.weight[0] + v.weight[1] + v.weight[2] + v.weight[3], 1.f));

  // rest pose: skin matrices are identity (the bind pose)
  LocalPose pose;
  RestPose(sk, pose);
  std::vector<glm::mat4> model, skin;
  PoseToModel(sk, pose, model);
  ModelToSkin(sk, model, skin);
  for (size_t j = 1; j < skin.size(); ++j)
    for (int c = 0; c < 4; ++c)
      for (int r = 0; r < 4; ++r) CHECK(Near(skin[j][c][r], c == r ? 1.f : 0.f));
  CHECK(Near(model[3][3].y, 1.6f)); // Head at 1.6 m

  // Run at 0.5 s: the root half way (1.5 m forward), the spine leaning forward (the head ahead of the hips)
  SampleClip(*data->Clip("Run"), 0.5f, pose);
  PoseToModel(sk, pose, model);
  CHECK(Near(model[1][3].z, -1.5f));
  CHECK(model[3][3].z < model[1][3].z - 0.3f);
  // blending Idle with it half way: the root half of that
  LocalPose idle;
  RestPose(sk, idle);
  SampleClip(*data->Clip("Idle"), 0.f, idle);
  BlendPose(idle, pose, 0.5f);
  std::vector<glm::mat4> half;
  PoseToModel(sk, idle, half);
  CHECK(Near(half[1][3].z, -0.75f));

  // pose bounds: the joint boxes moved by the skin matrices
  std::vector<JointBox> boxes;
  JointBoxes(&im.primitives[0].mesh.vertices[0].px, sizeof(Vertex3D) / sizeof(float), im.primitives[0].skin, sk.joints.size(), boxes);
  CHECK(!boxes[0].Valid() && boxes[1].Valid() && boxes[3].Valid());
  glm::vec3 lo, hi;
  std::vector<glm::mat4> rest;
  RestPose(sk, pose);
  PoseToModel(sk, pose, model);
  ModelToSkin(sk, model, rest);
  CHECK(PoseBounds(boxes, rest, lo, hi));
  CHECK(Near(lo.y, 0.f) && Near(hi.y, 2.f) && Near(lo.z, -0.2f) && Near(hi.z, 0.2f));
  SampleClip(*data->Clip("Run"), 0.5f, pose);
  PoseToModel(sk, pose, model);
  ModelToSkin(sk, model, skin);
  glm::vec3 lo2, hi2;
  CHECK(PoseBounds(boxes, skin, lo2, hi2));
  CHECK(hi2.z < -1.2f && lo2.z < -1.9f && hi2.y < 1.99f); // moved forward and leaning (the top lower)

  CHECK(MakeSkinVertex(nullptr, nullptr, 0).weight[0] == 1.f);
  const int j[6] = {1, 2, 3, 4, 5, 2};
  const float w[6] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.2f};
  const SkinVertex v = MakeSkinVertex(j, w, 6); // the four largest (joint 2 counted twice: 0.4), normalised
  float sum = 0;
  bool has1 = false;
  for (int k = 0; k < 4; ++k) { sum += v.weight[k]; has1 = has1 || (v.joint[k] == 1 && v.weight[k] > 0); }
  CHECK(Near(sum, 1.f) && !has1);
}

TEST_CASE("animation graph: .animgraph text, conditions, cross-fades, triggers, exit time, events (queue 7-1)") {
  AnimGraphDef g;
  std::string err;
  CHECK(ParseAnimGraph(TestRigAnimGraph(), g, &err));
  CHECK_EQ(g.machines[0].states.size(), (size_t)3);
  CHECK_EQ(g.machines[0].transitions.size(), (size_t)4);
  CHECK_EQ(g.vars.size(), (size_t)2);
  CHECK(g.machines[0].states[g.machines[0].entry].name == "Idle" && g.skeleton == "Models/Rig.gltf" && g.machines[0].name == "Locomotion");
  CHECK(g.machines[0].transitions[2].from == -1 && g.machines[0].transitions[3].exitTime == 1.f && !g.machines[0].states[2].loop);
  CHECK_EQ(g.machines[0].states[1].events.size(), (size_t)2);
  // written back: same meaning, and stable
  const std::string text = WriteAnimGraph(g);
  AnimGraphDef g2;
  CHECK(ParseAnimGraph(text, g2, &err));
  CHECK(WriteAnimGraph(g2) == text);
  CHECK(g2.machines[0].transitions[0].when == "Speed > 0.1" && g2.machines[0].transitions[0].blend == 0.25f);

  std::vector<std::vector<AnimGraphDef::Cond>> c;
  CHECK(ParseAnimCondition("A > 1 && !B || C <= -2.5", c, &err));
  CHECK(c.size() == 2 && c[0].size() == 2 && c[0][1].negate && c[0][1].op == AnimGraphDef::Cond::Truthy && c[1][0].op == AnimGraphDef::Cond::Le &&
        c[1][0].value == -2.5f);
  CHECK(!ParseAnimCondition("A > x", c, &err));
  CHECK(!ParseAnimGraph(R"({"stateMachines":{"M":{"states":{"A":{}},"transitions":[{"from":"A","to":"B"}]}}})", g2, &err));
  CHECK(err.find("'B'") != std::string::npos);
  CHECK(!ParseAnimGraph(R"({"stateMachines":{"M":{"states":{"A":{}},"transitions":[{"from":"A","to":"A","when":"Nope"}]}}})", g2, &err));
  CHECK(!ParseAnimGraph("[1]", g2, &err));

  // runtime against the rig's clip lengths
  AnimatorRuntime rt;
  rt.Init(&g, [](const std::string& clip) { return clip == "Idle" ? 2.f : clip == "Run" ? 1.f : clip == "Jump" ? 0.8f : -1.f; });
  rt.Start("");
  CHECK(rt.StateName() == "Idle" && !rt.Fading());
  std::vector<AnimatorRuntime::FiredEvent> ev;
  for (int i = 0; i < 30; ++i) rt.Update(1.f / 60, 1.f, ev);
  CHECK(rt.StateName() == "Idle" && ev.empty() && Near(rt.Current().time, 0.5f));
  rt.SetFloat("Speed", 3);
  rt.Update(1.f / 60, 1.f, ev); // Speed > 0.1: Idle -> Run over 0.25 s
  CHECK(rt.Fading() && rt.StateName() == "Run" && rt.FadeWeight() == 0.f);
  for (int i = 0; i < 6; ++i) rt.Update(1.f / 60, 1.f, ev);
  CHECK(rt.Fading() && Near(rt.FadeWeight(), 0.4f, 0.01f)); // 0.1 s of 0.25
  for (int i = 0; i < 9; ++i) rt.Update(1.f / 60, 1.f, ev);
  CHECK(!rt.Fading() && rt.Current().clip == "Run" && Near(rt.Current().time, 0.25f, 0.02f));
  for (int i = 0; i < 5; ++i) rt.Update(1.f / 60, 1.f, ev); // off the 0.25 s footstep
  ev.clear();
  for (int i = 0; i < 60; ++i) rt.Update(1.f / 60, 1.f, ev); // a full Run loop: two footsteps
  int steps = 0;
  for (auto& e : ev) steps += e.name == "Footstep" && e.state == "Run";
  CHECK_EQ(steps, 2);
  rt.SetTrigger("Jump");
  rt.Update(1.f / 60, 1.f, ev); // any state -> Jump; the trigger is used up
  CHECK(rt.StateName() == "Jump" && rt.Get("Jump") == 0.f && rt.transitionsTaken == 2);
  ev.clear();
  rt.SetFloat("Speed", 0);
  for (int i = 0; i < 60 && rt.StateName() == "Jump"; ++i) rt.Update(1.f / 60, 1.f, ev);
  CHECK(rt.StateName() == "Idle"); // at its end (exitTime 1)
  bool landed = false;
  for (auto& e : ev) landed = landed || e.name == "Land";
  CHECK(landed);
  CHECK(rt.PlayState("Run", 0.f) && !rt.Fading() && rt.StateName() == "Run");
  CHECK(!rt.PlayState("Swim", 0.2f) && rt.StateName() == "Run");
  // without a graph: clips by name
  AnimatorRuntime clips;
  clips.Init(nullptr, [](const std::string& clip) { return clip == "Run" ? 1.f : -1.f; });
  clips.Start("Run");
  CHECK(clips.StateName() == "Run" && clips.Current().loop);
  for (int i = 0; i < 90; ++i) clips.Update(1.f / 60, 2.f, ev); // speed 2: 3 s of clip in 1.5 s
  CHECK(Near(clips.Current().time, 0.f, 0.02f) || Near(clips.Current().time, 1.f, 0.02f));
}

TEST_CASE("animation system: Animator components, script calls, root motion, live values (queue 7-1)") {
  ImportedModel im;
  auto data = Rig(im);
  CHECK(data != nullptr);
  if (!data) return;
  RememberSkinnedModel("Tests/Rig.gltf", 0, data);
  Scene scene;
  scene.Create("hero", "Hero");
  std::string err;
  CHECK(scene.AddComponent("hero", Transform::StaticType(), "", &err));
  CHECK(scene.AddComponent("hero", MeshRenderer::StaticType(), R"({"mesh":"Tests/Rig.gltf"})", &err));
  CHECK(scene.AddComponent("hero", Animator::StaticType(), R"({"clip":"Run","rootMotion":true})", &err));
  AnimationSystem sys;
  sys.Begin(nullptr);
  CHECK(AnimationSystem::Active() == &sys);
  std::vector<AnimationSystem::Event> events;
  for (int i = 0; i < 30; ++i) sys.Tick(scene, 1.f / 60, events);
  Transform& t = *static_cast<Transform*>(scene.Find("hero")->Find("Transform")->data);
  CHECK(Near(t.position.z, -1.5f, 0.02f) && Near(t.position.x, 0.f)); // 3 m/s for 0.5 s, moved by the root
  const AnimationSystem::Pose* pose = sys.PoseOf("hero");
  CHECK(pose && pose->skin.size() == 4 && pose->mesh == "Tests/Rig.gltf");
  if (pose) CHECK(Near(pose->skin[1][3].z, 0.f, 0.01f)); // ...and taken out of the pose (in place)
  for (int i = 0; i < 40; ++i) sys.Tick(scene, 1.f / 60, events); // across the loop's wrap: no jump back
  CHECK(Near(t.position.z, -3.5f, 0.05f));
  Animator& a = *static_cast<Animator*>(scene.Find("hero")->Find("Animator")->data);
  CHECK(a.liveState == "Run" && a.liveFade == 0.f);
  a.rootMotion = false;
  a.Play("Idle"); // script call: cross-fades over crossFade (0.2 s)
  CHECK(a.pending.size() == 1);
  sys.Tick(scene, 1.f / 60, events);
  CHECK(a.pending.empty() && a.liveState == "Idle" && a.liveFade > 0.f && a.liveFade < 0.2f);
  const float z = t.position.z;
  for (int i = 0; i < 20; ++i) sys.Tick(scene, 1.f / 60, events);
  CHECK(t.position.z == z && a.liveFade == 0.f); // root motion off: the Transform stays
  CHECK(a.CurrentState() == "Idle" && a.GetFloat("Nothing") == 0.f);
  a.SetFloat("Speed", 2.5f);
  CHECK(a.GetFloat("Speed") == 2.5f); // a value set this frame reads back before the update
  scene.Destroy("hero");
  sys.Tick(scene, 1.f / 60, events);
  CHECK_EQ(sys.Count(), (size_t)0);
  sys.End();
  CHECK(AnimationSystem::Active() == nullptr);
  ForgetSkinnedModels();
}

TEST_CASE("skeletal import from FBX (ufbx): skin clusters, bones, an animation stack sampled into a clip (queue 7-1)") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "bdg_anim_fbx";
  std::error_code ec;
  fs::create_directories(dir, ec);
  // A 2 m strip (centimetres) on two bones: Hips at the floor, Chest at 1 m; "Take|Wave" turns Chest 90 degrees
  // about X in one second (FBX time: 46186158000 per second).
  std::ofstream(dir / "Strip.fbx", std::ios::binary) << R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Geometry: 1000, "Geometry::Strip", "Mesh" {
		Vertices: *18 {
			a: -10,0,0,10,0,0,-10,100,0,10,100,0,-10,200,0,10,200,0
		}
		PolygonVertexIndex: *8 {
			a: 0,1,3,-3,2,3,5,-5
		}
	}
	Model: 2000, "Model::Strip", "Mesh" {
		Version: 232
	}
	Model: 4000, "Model::Hips", "LimbNode" {
		Version: 232
	}
	Model: 4001, "Model::Chest", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,100,0
		}
	}
	Deformer: 5000, "Deformer::Skin", "Skin" {
		Version: 101
	}
	Deformer: 5001, "SubDeformer::Hips", "Cluster" {
		Version: 100
		Indexes: *4 {
			a: 0,1,2,3
		}
		Weights: *4 {
			a: 1,1,0.5,0.5
		}
		Transform: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
		}
		TransformLink: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
		}
	}
	Deformer: 5002, "SubDeformer::Chest", "Cluster" {
		Version: 100
		Indexes: *4 {
			a: 2,3,4,5
		}
		Weights: *4 {
			a: 0.5,0.5,1,1
		}
		Transform: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,-100,0,1
		}
		TransformLink: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,100,0,1
		}
	}
	AnimationStack: 6000, "AnimStack::Take|Wave", "" {
		Properties70:  {
			P: "LocalStart", "KTime", "Time", "",0
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 6001, "AnimLayer::Base", "" {
	}
	AnimationCurveNode: 6002, "AnimCurveNode::R", "" {
		Properties70:  {
			P: "d|X", "Number", "", "A",0
			P: "d|Y", "Number", "", "A",0
			P: "d|Z", "Number", "", "A",0
		}
	}
	AnimationCurve: 6003, "AnimCurve::", "" {
		Default: 0
		KeyVer: 4009
		KeyTime: *2 {
			a: 0,46186158000
		}
		KeyValueFloat: *2 {
			a: 0,90
		}
		KeyAttrFlags: *1 {
			a: 4
		}
		KeyAttrDataFloat: *4 {
			a: 0,0,0,0
		}
		KeyAttrRefCount: *1 {
			a: 2
		}
	}
}
Connections:  {
	C: "OO",2000,0
	C: "OO",1000,2000
	C: "OO",4000,0
	C: "OO",4001,4000
	C: "OO",5000,1000
	C: "OO",5001,5000
	C: "OO",5002,5000
	C: "OO",4000,5001
	C: "OO",4001,5002
	C: "OO",6001,6000
	C: "OO",6002,6001
	C: "OP",6002,4001, "Lcl Rotation"
	C: "OP",6003,6002, "d|X"
}
)";
  ImportedModel m;
  std::string err;
  CHECK(ImportModel(dir / "Strip.fbx", m, &err));
  if (!err.empty()) std::printf("fbx: %s\n", err.c_str());
  CHECK(m.skinned != nullptr);
  if (!m.skinned) return;
  const Skeleton& sk = m.skinned->skeleton;
  CHECK(sk.joints.size() == 2 && sk.Find("Hips") == 0 && sk.Find("Chest") == 1 && sk.joints[1].parent == 0 && sk.root == 0);
  CHECK(Near(sk.joints[1].t.y, 1.f)); // centimetres -> metres
  CHECK(m.skinned->clips.size() == 1 && m.skinned->clips[0].name == "Wave" && Near(m.skinned->clips[0].duration, 1.f));
  CHECK(m.primitives.size() == 1 && m.primitives[0].skin.size() == m.primitives[0].mesh.vertices.size());
  CHECK(m.instances.size() == 1 && m.instances[0].world == glm::mat4(1.f));
  CHECK(Near(m.boundsMax.y, 2.f) && Near(m.boundsMin.y, 0.f)); // rest pose = bind pose
  // the end of the clip: the chest turned 90 degrees, its top vertices now 1 m forward (+Z) at 1 m height
  LocalPose pose;
  RestPose(sk, pose);
  SampleClip(m.skinned->clips[0], 1.f, pose);
  std::vector<glm::mat4> model, skin;
  PoseToModel(sk, pose, model);
  ModelToSkin(sk, model, skin);
  int tops = 0;
  for (size_t i = 0; i < m.primitives[0].mesh.vertices.size(); ++i) {
    const Vertex3D& v = m.primitives[0].mesh.vertices[i];
    if (v.py < 1.9f) continue;
    const SkinVertex& s = m.primitives[0].skin[i];
    glm::mat4 b(0.f);
    for (int k = 0; k < 4; ++k) b += skin[s.joint[k]] * s.weight[k];
    const glm::vec3 p = glm::vec3(b * glm::vec4(v.px, v.py, v.pz, 1.f));
    tops += Near(p.y, 1.f, 0.01f) && Near(std::fabs(p.z), 1.f, 0.01f);
  }
  CHECK_EQ(tops, 2);
  fs::remove_all(dir, ec);
}
