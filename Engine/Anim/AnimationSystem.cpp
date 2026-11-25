#include "Anim/AnimationSystem.h"
#include "Anim/IK.h"
#include "Physics/PhysicsWorld.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ModelImport.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>

namespace bdg {
namespace {
struct CachedModel { uint64_t hash = 0; std::shared_ptr<const SkinnedModelData> data; bool failed = false; };
std::map<std::string, CachedModel>& Models() {
  static std::map<std::string, CachedModel> m;
  return m;
}
std::mutex g_modelsMutex;
const AnimationSystem* g_active = nullptr;
uint64_t g_poseSerial = 0;

const AssetRecord* FindAsset(const AssetDatabase* db, const std::string& path) {
  if (!db || path.empty()) return nullptr;
  if (const AssetRecord* r = db->ByPath(path)) return r;
  for (const AssetRecord* r : db->All()) { // a file name alone
    const size_t slash = r->path.find_last_of('/');
    if ((slash == std::string::npos ? r->path : r->path.substr(slash + 1)) == path) return r;
  }
  return nullptr;
}

std::string FullPath(const AssetDatabase* db, const std::string& rel) { return db->Root() + "/" + rel; }

// The root joint's local translation in a clip at time t (rest value when the clip does not move it).
glm::vec3 RootTranslation(const AnimClip& clip, int joint, float t, const glm::vec3& rest) {
  for (const AnimChannel& c : clip.channels) {
    if (c.joint != joint || c.path != AnimChannel::Translation || c.times.empty()) continue;
    LocalPose p;
    p.t.assign((size_t)joint + 1, rest);
    p.r.assign((size_t)joint + 1, glm::quat(1, 0, 0, 0));
    p.s.assign((size_t)joint + 1, glm::vec3(1));
    AnimClip one;
    one.channels.push_back(c);
    SampleClip(one, t, p);
    return p.t[joint];
  }
  return rest;
}
}

std::shared_ptr<const SkinnedModelData> SkinnedModel(const AssetDatabase* assets, const std::string& path) {
  const AssetRecord* rec = FindAsset(assets, path);
  if (!rec) { // not in a project (tests, or a file the database has not seen): only what was handed over
    std::lock_guard<std::mutex> lock(g_modelsMutex);
    auto it = Models().find(path);
    return it == Models().end() ? nullptr : it->second.data;
  }
  {
    std::lock_guard<std::mutex> lock(g_modelsMutex);
    auto it = Models().find(rec->path);
    if (it != Models().end() && it->second.hash == rec->hash) return it->second.data;
  }
  ImportedModel im;
  std::string err;
  std::shared_ptr<const SkinnedModelData> data;
  if (ImportModel(PathU8(FullPath(assets, rec->path)), im, &err, false)) data = im.skinned;
  else LogWarn("animation: {} did not import: {}", rec->path, err);
  RememberSkinnedModel(rec->path, rec->hash, data);
  return data;
}

void RememberSkinnedModel(const std::string& path, uint64_t hash, std::shared_ptr<const SkinnedModelData> data) {
  std::lock_guard<std::mutex> lock(g_modelsMutex);
  Models()[path] = {hash, std::move(data), false};
}

void ForgetSkinnedModels() {
  std::lock_guard<std::mutex> lock(g_modelsMutex);
  Models().clear();
}

const AnimationSystem* AnimationSystem::Active() { return g_active; }

void AnimationSystem::Begin(const AssetDatabase* assets) {
  assets_ = assets;
  insts_.clear();
  graphs_.clear();
  running_ = true;
  eventsSent_ = 0;
  g_active = this;
}

void AnimationSystem::End() {
  if (running_ && (insts_.size() || eventsSent_)) LogInfo("animation: stopped ({} animators, {} events)", insts_.size(), eventsSent_);
  insts_.clear();
  graphs_.clear();
  running_ = false;
  if (g_active == this) g_active = nullptr;
}

const AnimationSystem::Pose* AnimationSystem::PoseOf(std::string_view id) const {
  auto it = insts_.find(id);
  return it == insts_.end() || it->second.pose.skin.empty() ? nullptr : &it->second.pose;
}

const AnimatorRuntime* AnimationSystem::RuntimeOf(std::string_view id, size_t layer) const {
  auto it = insts_.find(id);
  return it == insts_.end() || layer >= it->second.layers.size() ? nullptr : &it->second.layers[layer];
}

bool AnimationSystem::RagdollActive(std::string_view id) const {
  auto it = insts_.find(id);
  return it != insts_.end() && it->second.ragdoll >= 0;
}

namespace {
std::map<std::string, std::shared_ptr<AnimGraphDef>>& KnownGraphs() {
  static std::map<std::string, std::shared_ptr<AnimGraphDef>> g;
  return g;
}
}
void RememberAnimGraph(const std::string& path, std::shared_ptr<AnimGraphDef> graph) {
  std::lock_guard<std::mutex> lock(g_modelsMutex);
  if (graph) KnownGraphs()[path] = std::move(graph);
  else KnownGraphs().erase(path);
}

std::shared_ptr<AnimGraphDef> AnimationSystem::LoadGraph(const std::string& path) {
  if (auto it = graphs_.find(path); it != graphs_.end()) return it->second;
  {
    std::lock_guard<std::mutex> lock(g_modelsMutex);
    if (auto k = KnownGraphs().find(path); k != KnownGraphs().end()) return graphs_[path] = k->second;
  }
  std::shared_ptr<AnimGraphDef> g;
  const AssetRecord* rec = FindAsset(assets_, path);
  std::string text, err;
  if (!rec || !ReadTextU8(FullPath(assets_, rec->path), text)) LogWarn("animation: graph {} not found in the project", path);
  else {
    auto def = std::make_shared<AnimGraphDef>();
    if (ParseAnimGraph(text, *def, &err)) g = def;
    else LogWarn("animation: {}: {}", rec->path, err);
  }
  graphs_[path] = g;
  return g;
}

void AnimationSystem::SetUp(Inst& in, const SceneObject& o, const std::string& id, const std::string& clip) {
  in.data = SkinnedModel(assets_, in.mesh);
  if (!in.data && !in.mesh.empty()) LogWarn("animation: {} ({}): {} has no skeleton to animate", o.name, id, in.mesh);
  if (!in.graphPath.empty()) in.graph = LoadGraph(in.graphPath);
  std::shared_ptr<const SkinnedModelData> data = in.data;
  auto duration = [data](const std::string& c) -> float {
    const AnimClip* a = data ? data->Clip(c) : nullptr;
    return a ? a->duration : -1.f;
  };
  in.params = std::make_shared<AnimatorRuntime::Params>();
  if (in.graph)
    for (const AnimGraphDef::Var& v : in.graph->vars) (*in.params)[v.name] = 0.f;
  in.layers.assign(1, AnimatorRuntime{});
  in.layers[0].Init(in.graph.get(), duration, 0, in.params);
  in.layers[0].Start(clip.empty() && !in.graph && data && !data->clips.empty() ? data->clips[0].name : clip);
  in.masks.clear();
  if (in.graph && data)
    for (const AnimGraphDef::Layer& l : in.graph->layers) {
      AnimatorRuntime rt;
      rt.Init(in.graph.get(), duration, l.machine, in.params);
      rt.Start("");
      in.layers.push_back(std::move(rt));
      // mask: the named bones and everything under them (parents come first)
      const Skeleton& sk = data->skeleton;
      std::vector<char> mask(sk.joints.size(), 0);
      std::vector<std::string> roots = l.bones;
      if (!l.fromBone.empty()) roots.push_back(l.fromBone);
      for (const std::string& b : roots) {
        const int j = sk.Find(b);
        if (j < 0) LogWarn("animation: layer '{}': no bone '{}' in {}", l.name, b, in.mesh);
        else mask[j] = 1;
      }
      if (roots.empty()) std::fill(mask.begin(), mask.end(), 1);
      for (size_t j = 0; j < sk.joints.size(); ++j)
        if (sk.joints[j].parent >= 0 && mask[sk.joints[j].parent]) mask[j] = 1;
      in.masks.push_back(std::move(mask));
    }
  if (data) {
    LocalPose rest;
    RestPose(data->skeleton, rest);
    PoseToModel(data->skeleton, rest, in.restModel);
  }
}

void AnimationSystem::Tick(Scene& scene, float dt, std::vector<Event>& events, PhysicsWorld* physics) {
  if (!running_) return;
  for (auto& [id, in] : insts_) in.seen = false;
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    const std::string id = stack.back();
    stack.pop_back();
    SceneObject* o = scene.Find(id);
    if (!o) continue;
    for (auto c = o->children.rbegin(); c != o->children.rend(); ++c) stack.push_back(*c);
    Component* ac = o->Find("Animator");
    Component* mc = ac ? o->Find("MeshRenderer") : nullptr;
    if (!ac) continue;
    Animator& anim = *static_cast<Animator*>(ac->data);
    const std::string mesh = mc ? static_cast<const MeshRenderer*>(mc->data)->mesh : std::string();
    auto it = insts_.find(id);
    if (it == insts_.end() || it->second.mesh != mesh || it->second.graphPath != anim.graph) {
      if (it != insts_.end() && it->second.ragdoll >= 0 && physics) physics->RemoveRagdoll(it->second.ragdoll);
      Inst in;
      in.mesh = mesh;
      in.graphPath = anim.graph;
      SetUp(in, *o, id, anim.clip);
      it = insts_.insert_or_assign(id, std::move(in)).first;
    }
    Inst& in = it->second;
    in.seen = true;
    for (const AnimatorOp& op : anim.pending) {
      if (op.kind == AnimatorOp::Play) { // the first layer whose machine has that state (else the base: a clip by name)
        size_t l = 0;
        for (size_t k = 0; k < in.layers.size(); ++k)
          if (in.layers[k].HasState(op.name)) { l = k; break; }
        if (!in.layers[l].PlayState(op.name, op.value < 0 ? anim.crossFade : op.value))
          LogWarn("animation: {}: no state or clip \"{}\" to play", o->name, op.name);
      } else if (op.kind == AnimatorOp::SetTrigger) (*in.params)[op.name] = 1.f;
      else if (op.kind == AnimatorOp::LayerWeight) in.layerWeights[op.name] = std::clamp(op.value, 0.f, 1.f);
      else (*in.params)[op.name] = op.value;
    }
    anim.pending.clear();
    std::vector<AnimatorRuntime::FiredEvent> fired;
    for (AnimatorRuntime& rt : in.layers) rt.Update(dt, std::max(anim.speed, 0.f), fired);
    for (AnimatorRuntime::FiredEvent& f : fired) events.push_back({id, std::move(f.name), std::move(f.state)});
    eventsSent_ += fired.size();
    Evaluate(in, scene, id, anim.rootMotion, dt, physics);
    const AnimatorRuntime& base = in.layers[0];
    anim.liveState = base.StateName();
    anim.liveFade = base.FadeWeight();
    anim.liveParams.assign(in.params->begin(), in.params->end());
    anim.liveBlend.clear();
    std::vector<AnimatorRuntime::ClipSample> samples;
    base.Samples(base.Fading() ? base.Next() : base.Current(), samples);
    if (samples.size() > 1 || ((base.Fading() ? base.Next() : base.Current()).blend && !samples.empty()))
      for (const AnimatorRuntime::ClipSample& s : samples)
        anim.liveBlend += std::format("{}{} {:.2f}", anim.liveBlend.empty() ? "" : " · ", s.clip, s.weight);
  }
  for (auto it = insts_.begin(); it != insts_.end();) {
    if (it->second.seen) { ++it; continue; }
    if (it->second.ragdoll >= 0 && physics) physics->RemoveRagdoll(it->second.ragdoll);
    it = insts_.erase(it);
  }
}

// One layer's pose: its clip samples mixed by weight, then cross-faded into the state it goes to. atStart: every clip
// at time 0 (the reference an additive layer's motion is measured from).
void AnimationSystem::SampleLayer(const Inst& in, const AnimatorRuntime& rt, bool atStart, LocalPose& pose) const {
  const Skeleton& sk = in.data->skeleton;
  auto mix = [&](const AnimatorRuntime::Play& p, LocalPose& out) {
    RestPose(sk, out);
    std::vector<AnimatorRuntime::ClipSample> samples;
    rt.Samples(p, samples);
    float sum = 0;
    LocalPose tmp;
    for (const AnimatorRuntime::ClipSample& s : samples) {
      const AnimClip* c = in.data->Clip(s.clip);
      if (!c || s.weight <= 0) continue;
      if (sum <= 0) {
        SampleClip(*c, atStart ? 0.f : s.time, out);
        sum = s.weight;
        continue;
      }
      RestPose(sk, tmp);
      SampleClip(*c, atStart ? 0.f : s.time, tmp);
      sum += s.weight;
      BlendPose(out, tmp, s.weight / sum);
    }
  };
  mix(rt.Current(), pose);
  if (rt.Fading()) {
    LocalPose other;
    mix(rt.Next(), other);
    BlendPose(pose, other, rt.FadeWeight());
  }
}

void AnimationSystem::Evaluate(Inst& in, Scene& scene, const std::string& id, bool rootMotion, float dt, PhysicsWorld* physics) {
  if (!in.data || in.data->skeleton.Empty() || in.layers.empty()) return;
  const Skeleton& sk = in.data->skeleton;
  SceneObject* obj = scene.Find(id);
  Component* rc = obj ? obj->Find("Ragdoll") : nullptr;
  const Ragdoll* rag = rc ? static_cast<const Ragdoll*>(rc->data) : nullptr;
  const bool limp = rag && rag->active && physics;
  LocalPose pose;
  SampleLayer(in, in.layers[0], false, pose);
  // layers over the base: override (masked bones move towards the layer's pose) or additive (the layer's motion
  // from its clips' first frame added on top)
  for (size_t l = 1; l < in.layers.size() && in.graph; ++l) {
    const AnimGraphDef::Layer& def = in.graph->layers[l - 1];
    float w = def.weight;
    if (auto it = in.layerWeights.find(def.name); it != in.layerWeights.end()) w = it->second;
    if (!def.weightVar.empty()) w = std::clamp(in.layers[l].Get(def.weightVar), 0.f, 1.f);
    if (w <= 0.f) continue;
    LocalPose lp;
    SampleLayer(in, in.layers[l], false, lp);
    const std::vector<char>& mask = in.masks[l - 1];
    if (def.additive) {
      LocalPose ref;
      SampleLayer(in, in.layers[l], true, ref);
      for (size_t j = 0; j < pose.Size(); ++j) {
        if (!mask[j]) continue;
        pose.t[j] += (lp.t[j] - ref.t[j]) * w;
        const glm::quat delta = glm::normalize(glm::inverse(ref.r[j]) * lp.r[j]);
        pose.r[j] = glm::normalize(pose.r[j] * glm::slerp(glm::quat(1, 0, 0, 0), delta, w));
      }
    } else {
      for (size_t j = 0; j < pose.Size(); ++j) {
        if (!mask[j]) continue;
        pose.t[j] = glm::mix(pose.t[j], lp.t[j], w);
        pose.s[j] = glm::mix(pose.s[j], lp.s[j], w);
        pose.r[j] = glm::normalize(glm::slerp(pose.r[j], glm::dot(pose.r[j], lp.r[j]) < 0 ? -lp.r[j] : lp.r[j], w));
      }
    }
  }
  std::vector<glm::mat4> model;
  PoseToModel(sk, pose, model);
  const int root = sk.root;
  if (rootMotion && root >= 0 && !limp) {
    // Horizontal motion of the root joint (model space): taken out of the pose (it plays in place) and added to the
    // Transform. Per clip sample of the base layer: from the clip start (offset) and since the last update (delta).
    const AnimatorRuntime& rt = in.layers[0];
    const glm::mat3 parent = sk.joints[root].parent >= 0 ? glm::mat3(model[sk.joints[root].parent]) : glm::mat3(1.f);
    glm::vec3 offset(0.f), delta(0.f);
    auto addPlay = [&](const AnimatorRuntime::Play& p, float weight) {
      std::vector<AnimatorRuntime::ClipSample> samples;
      rt.Samples(p, samples);
      for (const AnimatorRuntime::ClipSample& s : samples) {
        const AnimClip* c = in.data->Clip(s.clip);
        const float w = weight * s.weight;
        if (!c || w <= 0) continue;
        const glm::vec3 rest = sk.joints[root].t;
        const glm::vec3 now = RootTranslation(*c, root, s.time, rest), start = RootTranslation(*c, root, 0.f, rest);
        glm::vec3 d = now - RootTranslation(*c, root, s.prevTime, rest);
        if (s.wrapped) d += RootTranslation(*c, root, s.duration, rest) - start;
        offset += parent * (now - start) * w;
        delta += parent * d * w;
      }
    };
    addPlay(rt.Current(), rt.Fading() ? 1.f - rt.FadeWeight() : 1.f);
    if (rt.Fading()) addPlay(rt.Next(), rt.FadeWeight());
    offset.y = delta.y = 0.f;
    pose.t[root] -= glm::inverse(parent) * offset; // in place
    PoseToModel(sk, pose, model);
    if (obj && glm::dot(delta, delta) > 0.f)
      if (Component* tc = obj->Find("Transform")) {
        Transform& t = *static_cast<Transform*>(tc->data);
        const glm::vec3 world = glm::mat3(scene.WorldMatrix(id)) * delta;
        const glm::mat3 parentWorld = obj->parent.empty() ? glm::mat3(1.f) : glm::mat3(scene.WorldMatrix(obj->parent));
        const glm::vec3 local = glm::inverse(parentWorld) * world;
        t.position = {t.position.x + local.x, t.position.y + local.y, t.position.z + local.z};
      }
  }
  if (rag && in.ragdoll >= 0 && !rag->active && physics) { // back to the Animator
    physics->RemoveRagdoll(in.ragdoll);
    in.ragdoll = -1;
  }
  if (limp) ApplyRagdoll(in, scene, id, *rag, pose, model, physics);
  else if (Component* ic = obj ? obj->Find("AnimatorIK") : nullptr) ApplyIK(in, scene, id, *static_cast<AnimatorIK*>(ic->data), pose, model, dt, physics);
  in.pose.model = model;
  ModelToSkin(sk, model, in.pose.skin);
  in.pose.mesh = in.mesh;
  in.pose.serial = ++g_poseSerial; // never repeats, across play sessions too
}

void AnimationSystem::ApplyIK(Inst& in, Scene& scene, const std::string& id, AnimatorIK& ik, LocalPose& pose, std::vector<glm::mat4>& model,
                              float dt, PhysicsWorld* physics) {
  const Skeleton& sk = in.data->skeleton;
  const glm::mat4 world = scene.WorldMatrix(id), inv = glm::inverse(world);
  const glm::mat3 inv3 = glm::mat3(inv);
  auto targetOf = [&](const std::string& object, glm::vec3& out) {
    if (object.empty() || !scene.Find(object)) return false;
    out = glm::vec3(inv * glm::vec4(glm::vec3(scene.WorldMatrix(object)[3]), 1.f)); // model space
    return true;
  };
  const float k = std::clamp(dt * ik.smoothing, 0.f, 1.f);
  // feet on the ground: raycast down under each foot, lift (or lower) it there, drop the hips for the lower foot
  if (ik.footIK && physics) {
    std::vector<std::string> ignore; // the object itself and its parents (a character's capsule)
    for (const SceneObject* o = scene.Find(id); o; o = o->parent.empty() ? nullptr : scene.Find(o->parent)) ignore.push_back(o->id);
    const int feet[2] = {sk.Find(ik.leftFoot), sk.Find(ik.rightFoot)};
    float want[2] = {0, 0};
    for (int f = 0; f < 2; ++f) {
      if (feet[f] < 0) continue;
      const float height = ik.footHeight > 0 ? ik.footHeight : std::max(in.restModel[feet[f]][3].y, 0.f);
      const glm::vec3 foot = glm::vec3(world * model[feet[f]][3]);
      // how high the clip lifts the foot (a step in the air): kept above the ground under it, so walking still swings
      const float swing = std::max(model[feet[f]][3].y - in.restModel[feet[f]][3].y, 0.f);
      PhysicsWorld::RayResult hit;
      if (physics->Raycast(foot + glm::vec3(0, ik.maxStep, 0), {0, -1, 0}, 2 * ik.maxStep + height, hit, ignore))
        want[f] = std::clamp(hit.point.y + height + swing - foot.y, -ik.maxStep, ik.maxStep);
    }
    for (int f = 0; f < 2; ++f) in.lift[f] += (want[f] - in.lift[f]) * k;
    in.hipDrop += (std::min({in.lift[0], in.lift[1], 0.f}) - in.hipDrop) * k;
    const int root = sk.root;
    if (root >= 0 && std::fabs(in.hipDrop) > 1e-5f) {
      const int p = sk.joints[root].parent;
      const glm::mat3 parent = p >= 0 ? glm::mat3(model[p]) : glm::mat3(1.f);
      pose.t[root] += glm::inverse(parent) * (inv3 * glm::vec3(0, in.hipDrop, 0));
      PoseToModel(sk, pose, model);
    }
    for (int f = 0; f < 2; ++f) {
      const std::vector<int> leg = feet[f] >= 0 ? JointChain(sk, feet[f], 3) : std::vector<int>{};
      if (leg.size() != 3) continue;
      const glm::vec3 target = glm::vec3(model[leg[2]][3]) + inv3 * glm::vec3(0, in.lift[f] - in.hipDrop, 0);
      TwoBoneIK(sk, pose, model, leg[0], leg[1], leg[2], target, glm::vec3(0, 0, 1));
    }
    ik.liveLeftLift = in.lift[0];
    ik.liveRightLift = in.lift[1];
    ik.liveHipDrop = in.hipDrop;
  }
  // reach: a hand onto a target
  glm::vec3 target;
  if (ik.reachWeight > 0 && targetOf(ik.reachTarget, target)) {
    const std::vector<int> arm = JointChain(sk, sk.Find(ik.hand), 3);
    if (arm.size() == 3) TwoBoneIK(sk, pose, model, arm[0], arm[1], arm[2], target, glm::vec3(0, -0.3f, -1.f), ik.reachWeight);
  }
  // FABRIK chain
  if (targetOf(ik.chainTarget, target) && !ik.chainEnd.empty()) {
    const std::vector<int> chain = JointChain(sk, sk.Find(ik.chainEnd), std::max(ik.chainLength, 2));
    if (!chain.empty()) Fabrik(sk, pose, model, chain, target);
  }
  // look-at / aim offset: the two bones above the head take `aimSpine` of the turn, the head the rest
  if (ik.lookWeight > 0 && targetOf(ik.lookAt, target)) {
    const int head = sk.Find(ik.head);
    if (head >= 0) {
      std::vector<std::pair<int, float>> chain;
      const std::vector<int> up = JointChain(sk, head, 3);
      for (size_t i = 0; i + 1 < up.size(); ++i) chain.push_back({up[i], ik.aimSpine * 0.5f});
      chain.push_back({head, 1.f});
      const glm::vec3 forward = glm::inverse(ModelRotation(in.restModel[head])) * glm::vec3(0, 0, 1); // models face +Z (glTF)
      ik.liveLookError = LookAt(sk, pose, model, chain, forward, target, ik.lookWeight, ik.maxLookAngle);
    }
  }
}

bool AnimationSystem::ApplyRagdoll(Inst& in, Scene& scene, const std::string& id, const Ragdoll& rag, const LocalPose& pose,
                                   std::vector<glm::mat4>& model, PhysicsWorld* physics) {
  const Skeleton& sk = in.data->skeleton;
  const glm::mat4 world = scene.WorldMatrix(id), inv = glm::inverse(world);
  if (in.ragdoll < 0) { // switch now: a capsule from each skinned joint to its first skinned child, from this pose
    std::vector<PhysicsWorld::RagdollBone> bones;
    in.ragdollBones.assign(sk.joints.size(), -1);
    in.boneToJoint.clear();
    std::vector<int> jointOf;
    float totalLen = 0;
    std::vector<float> lens;
    for (size_t j = 0; j < sk.joints.size(); ++j) {
      if (!sk.joints[j].skinned) continue;
      const glm::mat4 jw = world * model[j];
      const glm::vec3 start = glm::vec3(jw[3]);
      int child = -1;
      for (size_t c = j + 1; c < sk.joints.size() && child < 0; ++c)
        if (sk.joints[c].parent == (int)j && sk.joints[c].skinned) child = (int)c;
      glm::vec3 end;
      if (child >= 0) end = glm::vec3(world * model[child][3]);
      else { // a leaf (hand, foot, head): a short bone along the direction from its parent
        const int p = sk.joints[j].parent;
        const glm::vec3 from = p >= 0 ? glm::vec3(world * model[p][3]) : start - glm::vec3(0, 0.1f, 0);
        const glm::vec3 d = start - from;
        end = start + (glm::length(d) > 1e-4f ? glm::normalize(d) : glm::vec3(0, 1, 0)) * std::clamp(glm::length(d) * 0.5f, 0.08f, 0.2f);
      }
      glm::vec3 axis = end - start;
      float len = glm::length(axis);
      if (len < 0.04f) { axis = glm::vec3(0, 0.04f, 0); len = 0.04f; }
      PhysicsWorld::RagdollBone b;
      b.position = (start + end) * 0.5f;
      b.rotation = RotationBetween(glm::vec3(0, 1, 0), axis);
      b.radius = std::clamp(len * 0.22f * rag.thickness, 0.025f, 0.16f);
      b.halfHeight = std::max(len * 0.5f - b.radius, 0.005f);
      b.anchor = start;
      b.twistAxis = axis / len;
      b.swing = rag.swingLimit;
      b.twist = rag.twistLimit;
      for (int p = sk.joints[j].parent; p >= 0; p = sk.joints[p].parent)
        if (in.ragdollBones[p] >= 0) { b.parent = in.ragdollBones[p]; break; }
      in.ragdollBones[j] = (int)bones.size();
      const glm::mat4 bodyWorld = glm::translate(glm::mat4(1.f), b.position) * glm::mat4_cast(b.rotation);
      in.boneToJoint.push_back(glm::inverse(bodyWorld) * jw);
      bones.push_back(b);
      lens.push_back(len);
      totalLen += len;
    }
    for (size_t i = 0; i < bones.size(); ++i) bones[i].mass = rag.mass * (totalLen > 0 ? lens[i] / totalLen : 1.f / bones.size());
    glm::vec3 velocity(0.f);
    for (const SceneObject* o = scene.Find(id); o; o = o->parent.empty() ? nullptr : scene.Find(o->parent))
      if (Component* rb = const_cast<SceneObject*>(o)->Find("Rigidbody")) {
        const Vec3 v = static_cast<const Rigidbody*>(rb->data)->velocity;
        velocity = {v.x, v.y, v.z};
        break;
      }
    in.ragdoll = physics->AddRagdoll(id, bones, velocity);
    if (in.ragdoll < 0) return false;
  }
  std::vector<phys::BodyState> states;
  if (!physics->RagdollState(in.ragdoll, states)) return false;
  for (size_t j = 0; j < sk.joints.size(); ++j) { // simulated joints from their bodies, the others follow their parent
    const int b = in.ragdollBones.size() > j ? in.ragdollBones[j] : -1;
    if (b >= 0 && (size_t)b < states.size()) {
      const glm::mat4 bodyWorld = glm::translate(glm::mat4(1.f), states[b].position) * glm::mat4_cast(states[b].rotation);
      model[j] = inv * bodyWorld * in.boneToJoint[b];
    } else {
      const int p = sk.joints[j].parent;
      model[j] = p >= 0 ? model[p] * JointLocalMatrix(pose, j) : JointLocalMatrix(pose, j);
    }
  }
  return true;
}
}
