// Scripted editor checks: animation part 2, IK, ragdolls, the third-person template (lane A, queue 7-1 / 7-1b).
// Included by Engine.cpp after the other EditorTests files (UiField from assets.inl is visible here).
// New checks: register with BDG_EDITOR_TEST (EditorTests/Registry.h).
#include "Anim/IK.h"
#include "Anim/TestRig.h"

// BDG_IK_TEST=1, BDG_ASSETS = a project's Assets folder (C# parts with BDG_SCRIPT_ASSEMBLIES). The generated humanoid
// (Models/Hero.gltf) and its graph (Anim/Hero.animgraph) three times on a physics ground: "stand" has its left foot
// over a 20 cm step and AnimatorIK looking at a sphere to its left; "walker" gets Speed = 3 from C# AnimProbe (the Move
// blend space mixes Walk and Run); "doll" dies after 0.5 s (AnimProbe calls Ragdoll.Activate()) and falls. Played in
// 1/60 s steps. Checks: GPU-skinned render, blend weights from Speed, the left foot lifted onto the step, the head
// turned to the target, the ragdoll fell, the C# calls arrived.
namespace iktest {
std::string render = "-", blend = "-", footik = "-", lookat = "-", ragdoll = "-", csharp = "-";
int playFrame = -1;
}
void InjectIKTest(bdg_engine* e, int frame, float) {
  namespace fs = std::filesystem;
  using namespace iktest;
  bdg::EditorModel* m = e->model.get();
  if (!m || !e->assetsOpen || !e->sceneView) return;
  auto run = [&](const char* name, const std::string& target, const std::string& args) {
    bdg::Command c;
    c.name = name;
    c.target = target;
    c.args = args;
    std::string err;
    if (!m->bus.Execute(c, &err)) bdg::LogError("ik test: {} {}: {}", name, target, err);
  };
  const fs::path root = bdg::PathU8(e->assets.Root());
  const bool csharpOn = bdg::Registry::Instance().Find("AnimProbe") != nullptr;
  if (frame == 2) {
    std::error_code ec;
    fs::create_directories(root / "Models", ec);
    fs::create_directories(root / "Anim", ec);
    std::ofstream(root / "Models" / "Hero.gltf", std::ios::binary) << bdg::MakeHumanoidGltf();
    std::ofstream(root / "Anim" / "Hero.animgraph", std::ios::binary) << bdg::HumanoidAnimGraph();
    e->assets.Refresh();
    m->NewScene(bdg::SceneTemplate::Empty);
    for (const std::string& id : std::vector<std::string>(m->scene.Roots())) run("del", id, "{}");
    run("add", "", R"({"id":"ground","name":"Ground"})");
    run("set", "ground", R"({"path":"Transform.position","value":[0,-0.5,0]})");
    run("set", "ground", R"({"path":"Transform.scale","value":[30,1,30]})");
    run("addcomp", "ground", R"({"type":"MeshRenderer","value":{"mesh":"Cube","material":"bdg:Grid"}})");
    run("addcomp", "ground", R"({"type":"Collider","value":{"shape":"Box"}})");
    run("add", "", R"({"id":"step","name":"Step"})");
    run("set", "step", R"({"path":"Transform.position","value":[0.1,0.1,0.05]})");
    run("set", "step", R"({"path":"Transform.scale","value":[0.3,0.2,0.6]})");
    run("addcomp", "step", R"({"type":"MeshRenderer","value":{"mesh":"Cube","material":"M_Steel"}})");
    run("addcomp", "step", R"({"type":"Collider","value":{"shape":"Box"}})");
    run("add", "", R"({"id":"target","name":"Target"})");
    run("set", "target", R"({"path":"Transform.position","value":[2,1.75,2]})");
    run("set", "target", R"({"path":"Transform.scale","value":[0.2,0.2,0.2]})");
    run("addcomp", "target", R"({"type":"MeshRenderer","value":{"mesh":"Sphere","material":"M_Gold"}})");
    const char* heroes[3][2] = {{"stand", "0,0,0"}, {"walker", "3,0,3"}, {"doll", "-3,0,0"}};
    for (const auto& h : heroes) {
      run("add", "", std::format(R"({{"id":"{}","name":"{}"}})", h[0], h[0]));
      run("set", h[0], std::format(R"({{"path":"Transform.position","value":[{}]}})", h[1]));
      run("addcomp", h[0], R"({"type":"MeshRenderer","value":{"mesh":"Models/Hero.gltf"}})");
      run("addcomp", h[0], R"({"type":"Animator","value":{"graph":"Anim/Hero.animgraph"}})");
    }
    run("addcomp", "stand", R"({"type":"AnimatorIK","value":{"lookAt":"target"}})");
    run("addcomp", "doll", R"({"type":"Ragdoll","value":{}})");
    if (csharpOn) {
      run("addcomp", "walker", R"({"type":"AnimProbe","value":{"runAfter":0.3,"speed":3}})");
      run("addcomp", "doll", R"({"type":"AnimProbe","value":{"runAfter":100,"dieAfter":0.5}})");
    }
    run("add", "", R"({"id":"ikcam","name":"Camera"})");
    run("set", "ikcam", R"({"path":"Transform.position","value":[0,1.8,8]})");
    run("set", "ikcam", R"({"path":"Transform.rotation","value":[-8,0,0]})");
    run("addcomp", "ikcam", R"({"type":"Camera","value":{}})");
    run("add", "", R"({"id":"iksun","name":"Sun"})");
    run("set", "iksun", R"({"path":"Transform.rotation","value":[-50,30,0]})");
    run("addcomp", "iksun", R"({"type":"Light","value":{"type":"Directional","intensity":3}})");
    m->Select("stand");
  }
  if (frame == 4) {
    render = e->sceneView->SkinnedObjects() == 3 && e->sceneView->SkinPasses() > 0 ? "1" : std::format("0({})", e->sceneView->SkinnedObjects());
    TogglePlay(e);
    e->play.SetPaused(true); // stepped: 1/60 s of game time per frame
    playFrame = frame;
  }
  if (playFrame < 0 || frame <= playFrame || !e->play.Playing()) return;
  if (frame - playFrame < 90) { e->play.Step(); return; } // 1.5 s
  const bdg::AnimationSystem& anim = e->play.Animation();
  auto comp = [&](const char* id, const char* type) -> void* {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find(type) : nullptr;
    return c ? c->data : nullptr;
  };
  // blend weights follow Speed: 3 m/s between Walk (1.6) and Run (5)
  const auto* wa = static_cast<const bdg::Animator*>(comp("walker", "Animator"));
  const std::vector<float> want = [] {
    std::vector<float> w;
    bdg::BlendSpaceWeights({{"Idle", 0, 0}, {"Walk", 1.6f, 0}, {"Run", 5, 0}}, false, 3, 0, w);
    return w;
  }();
  const std::string expect = std::format("Walk {:.2f} · Run {:.2f}", want[1], want[2]);
  blend = wa && wa->liveBlend == expect ? "1" : "0(" + (wa ? wa->liveBlend : std::string("?")) + " / " + expect + ")";
  // the left foot on the step
  const auto* ik = static_cast<const bdg::AnimatorIK*>(comp("stand", "AnimatorIK"));
  const bdg::AnimationSystem::Pose* sp = anim.PoseOf("stand");
  const bdg::SkinnedModelData* data = nullptr;
  std::shared_ptr<const bdg::SkinnedModelData> keep = bdg::SkinnedModel(&e->assets, "Models/Hero.gltf");
  data = keep.get();
  if (ik && sp && data) {
    const bdg::Skeleton& sk = data->skeleton;
    const float dy = sp->model[sk.Find("LeftFoot")][3].y - sp->model[sk.Find("RightFoot")][3].y;
    footik = std::fabs(ik->liveLeftLift - 0.2f) < 0.02f && std::fabs(dy - 0.2f) < 0.03f ? "1" : std::format("0(lift {:.3f} dy {:.3f})", ik->liveLeftLift, dy);
    // the head turned to the target (front of the face towards +X / +Z)
    const glm::vec3 fwd = bdg::ModelRotation(sp->model[sk.Find("Head")]) * glm::vec3(0, 0, 1);
    lookat = ik->liveLookError < 3.f && fwd.x > 0.5f ? "1" : std::format("0(err {:.1f} fwd {:.2f})", ik->liveLookError, fwd.x);
    // the doll is down
    const bdg::AnimationSystem::Pose* dp = anim.PoseOf("doll");
    const float head = dp ? dp->model[sk.Find("Head")][3].y : 9.f;
    ragdoll = anim.RagdollActive("doll") && head < 0.7f ? "1" : std::format("0(active {} head {:.2f})", anim.RagdollActive("doll"), head);
  }
  csharp = csharpOn && wa && wa->GetFloat("Speed") == 3.f && anim.RagdollActive("doll") ? "1" : csharpOn ? "0" : "skipped";
  TogglePlay(e);
  playFrame = -1;
}
void ReportIKTest(bdg_engine*) {
  using namespace iktest;
  bdg::LogInfo("ik test: render={} blend={} footik={} lookat={} ragdoll={} csharp={}", render, blend, footik, lookat, ragdoll, csharp);
}
BDG_EDITOR_TEST("BDG_IK_TEST", InjectIKTest, ReportIKTest)

// BDG_THIRDPERSON_TEST=1, run in a project made by `bdg new <dir> --template thirdperson` (that folder as the working
// folder, BDG_ASSETS=<dir>/Assets). Play with keys injected through the focused Game view: W walks towards the stairs
// (blend space mostly Walk), Shift runs (Run weight up), the stairs make the foot IK lift the feet, Space jumps (the
// graph plays Jump); then the player is put in front of the south wall: the follow camera pulls in instead of going
// through it; a mouse move turns the camera. The HUD's speed text changes. Run with --frames 200.
namespace tptest {
bool shiftHeld = false; // Shift is a flag on key events (InputActions reads it from the latest event)
float z0 = 0, groundY = 0, maxY = -1e9f, maxSpeed = 0, maxRun = 0, maxLift = 0, yaw0 = 0;
bool sawJump = false, walkMostly = false;
float moved = 0;
std::string hud0, hud1, result;
}
void InjectThirdPersonTest(bdg_engine* e, int frame, float scale) {
  using namespace tptest;
  bdg::EditorModel* m = e->model.get();
  if (!m) return;
  using T = bdg::InputEvent::Type;
  auto key = [&](bdg::Key k, bool down) {
    bdg::InputEvent ev{down ? T::KeyDown : T::KeyUp};
    ev.key = k;
    ev.shift = shiftHeld;
    e->window.InjectEvent(ev);
  };
  auto transform = [&](const char* id) -> bdg::Transform* {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find("Transform") : nullptr;
    return c ? static_cast<bdg::Transform*>(c->data) : nullptr;
  };
  auto comp = [&](const char* id, const char* type) -> void* {
    bdg::SceneObject* o = m->scene.Find(id);
    bdg::Component* c = o ? o->Find(type) : nullptr;
    return c ? c->data : nullptr;
  };
  bdg::Widget* game = e->editor->Find("game");
  const float gx = game ? game->x + game->w * 0.5f : 0, gy = game ? game->y + game->h * 0.5f : 0;
  if (e->play.Playing()) std::this_thread::sleep_for(std::chrono::milliseconds(20)); // game time passes (fixed steps)
  if (frame == 2) TogglePlay(e);
  const auto* anim = static_cast<const bdg::Animator*>(comp("hero", "Animator"));
  const auto* ik = static_cast<const bdg::AnimatorIK*>(comp("hero", "AnimatorIK"));
  if (frame == 12) { // standing on the ground by now
    if (const auto* t = transform("player")) { z0 = t->position.z; groundY = t->position.y; }
    hud0 = UiField(e, "ui_speed", "text");
    key(bdg::Key::W, true);
  }
  if (frame == 21 && anim) { // walking: the blend space is mostly Walk
    const size_t w = anim->liveBlend.find("Walk ");
    walkMostly = w != std::string::npos && std::strtof(anim->liveBlend.c_str() + w + 5, nullptr) > 0.5f;
  }
  if (frame == 22) key(bdg::Key::Space, true); // jump while walking on the flat
  if (frame == 25) key(bdg::Key::Space, false);
  if (frame >= 20 && frame <= 45)
    if (const auto* t = transform("player")) maxY = std::max(maxY, t->position.y);
  if (frame == 55) { shiftHeld = true; key(bdg::Key::W, true); } // run: W again with Shift held
  if (frame >= 12 && frame <= 110) {
    if (anim) {
      for (const auto& [n, v] : anim->liveParams)
        if (n == "Speed") maxSpeed = std::max(maxSpeed, v);
      const size_t r = anim->liveBlend.find("Run ");
      if (r != std::string::npos) maxRun = std::max(maxRun, std::strtof(anim->liveBlend.c_str() + r + 4, nullptr));
      sawJump = sawJump || anim->liveState == "Jump";
    }
    if (ik) maxLift = std::max({maxLift, std::fabs(ik->liveLeftLift), std::fabs(ik->liveRightLift)});
  }
  if (frame == 100) {
    if (const auto* t = transform("player")) moved = z0 - t->position.z;
    hud1 = UiField(e, "ui_speed", "text");
    shiftHeld = false;
    key(bdg::Key::W, false);
  }
  if (frame == 120) { // in front of the south wall, the camera behind it would be inside the wall
    bdg::Command c;
    c.name = "set";
    c.target = "player";
    c.args = R"({"path":"Transform.position","value":[0,0.9,18.8]})";
    std::string err;
    m->bus.Execute(c, &err);
  }
  if (frame == 150) {
    const auto* p = transform("player");
    const auto* cam = transform("camera");
    const bool collide = p && cam && cam->position.z < 20.1f && glm::distance(glm::vec3(cam->position.x, cam->position.y, cam->position.z),
                                                                             glm::vec3(p->position.x, p->position.y, p->position.z)) < 3.f;
    if (cam) yaw0 = cam->rotation.y;
    result = std::format("moved={} walk={} run={} jump={} footik={} hud={} collide={}", moved > 2.f ? 1 : 0, walkMostly ? 1 : 0,
                         maxSpeed > 4.5f && maxRun > 0.5f ? 1 : 0, sawJump && maxY > groundY + 0.25f ? 1 : 0, maxLift > 0.04f ? 1 : 0,
                         !hud0.empty() && hud1 != hud0 ? 1 : 0, collide ? 1 : 0);
    bdg::LogInfo("third person test: moved {:.2f} m, speed {:.2f}, run weight {:.2f}, lift {:.3f}, jump height {:.2f}, camera z {:.2f}, hud {} -> {}",
                 moved, maxSpeed, maxRun, maxLift, maxY - groundY, cam ? cam->position.z : 0.f, hud0, hud1);
    e->window.InjectEvent({T::MouseMove, gx * scale, gy * scale, 0});
    e->window.InjectEvent({T::MouseMove, (gx + 60) * scale, gy * scale, 0});
  }
  if (frame == 165) {
    const auto* cam = transform("camera");
    result += std::format(" look={}", cam && std::fabs(cam->rotation.y - yaw0) > 1.f ? 1 : 0);
    TogglePlay(e);
  }
}
void ReportThirdPersonTest(bdg_engine*) { bdg::LogInfo("third person test: {}", tptest::result); }
BDG_EDITOR_TEST("BDG_THIRDPERSON_TEST", InjectThirdPersonTest, ReportThirdPersonTest)
