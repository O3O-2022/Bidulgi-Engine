#include "Anim/AnimationSystem.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ModelImport.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
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

const AnimatorRuntime* AnimationSystem::RuntimeOf(std::string_view id) const {
  auto it = insts_.find(id);
  return it == insts_.end() ? nullptr : &it->second.rt;
}

std::shared_ptr<AnimGraphDef> AnimationSystem::LoadGraph(const std::string& path) {
  if (auto it = graphs_.find(path); it != graphs_.end()) return it->second;
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

void AnimationSystem::Tick(Scene& scene, float dt, std::vector<Event>& events) {
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
      Inst in;
      in.mesh = mesh;
      in.graphPath = anim.graph;
      in.data = SkinnedModel(assets_, mesh);
      if (!in.data && !mesh.empty()) LogWarn("animation: {} ({}): {} has no skeleton to animate", o->name, id, mesh);
      if (!anim.graph.empty()) in.graph = LoadGraph(anim.graph);
      std::shared_ptr<const SkinnedModelData> data = in.data;
      in.rt.Init(in.graph.get(), [data](const std::string& clip) -> float {
        const AnimClip* c = data ? data->Clip(clip) : nullptr;
        return c ? c->duration : -1.f;
      });
      in.rt.Start(anim.clip.empty() && !in.graph && data && !data->clips.empty() ? data->clips[0].name : anim.clip);
      it = insts_.insert_or_assign(id, std::move(in)).first;
    }
    Inst& in = it->second;
    in.seen = true;
    for (const AnimatorOp& op : anim.pending) {
      if (op.kind == AnimatorOp::Play) {
        if (!in.rt.PlayState(op.name, op.value < 0 ? anim.crossFade : op.value))
          LogWarn("animation: {}: no state or clip \"{}\" to play", o->name, op.name);
      } else if (op.kind == AnimatorOp::SetTrigger) in.rt.SetTrigger(op.name);
      else in.rt.SetFloat(op.name, op.value);
    }
    anim.pending.clear();
    std::vector<AnimatorRuntime::FiredEvent> fired;
    in.rt.Update(dt, std::max(anim.speed, 0.f), fired);
    for (AnimatorRuntime::FiredEvent& f : fired) events.push_back({id, std::move(f.name), std::move(f.state)});
    eventsSent_ += fired.size();
    Evaluate(in, scene, id, anim.rootMotion);
    anim.liveState = in.rt.StateName();
    anim.liveFade = in.rt.FadeWeight();
    anim.liveParams.assign(in.rt.Params().begin(), in.rt.Params().end());
  }
  for (auto it = insts_.begin(); it != insts_.end();) it = it->second.seen ? std::next(it) : insts_.erase(it);
}

void AnimationSystem::Evaluate(Inst& in, Scene& scene, const std::string& id, bool rootMotion) {
  if (!in.data || in.data->skeleton.Empty()) return;
  const Skeleton& sk = in.data->skeleton;
  const AnimatorRuntime::Play& cur = in.rt.Current();
  const AnimatorRuntime::Play& next = in.rt.Next();
  const float w = in.rt.FadeWeight();
  LocalPose pose;
  RestPose(sk, pose);
  if (const AnimClip* c = cur.Valid() ? in.data->Clip(cur.clip) : nullptr) SampleClip(*c, cur.time, pose);
  if (in.rt.Fading())
    if (const AnimClip* c = next.Valid() ? in.data->Clip(next.clip) : nullptr) {
      LocalPose other;
      RestPose(sk, other);
      SampleClip(*c, next.time, other);
      BlendPose(pose, other, w);
    }
  std::vector<glm::mat4> model;
  PoseToModel(sk, pose, model);
  const int root = sk.root;
  if (rootMotion && root >= 0) {
    // Horizontal motion of the root joint (model space): taken out of the pose (it plays in place) and added to the
    // Transform. Per clip: from the clip start (offset) and since the last update (delta, across a loop's wrap).
    const glm::mat3 parent = sk.joints[root].parent >= 0 ? glm::mat3(model[sk.joints[root].parent]) : glm::mat3(1.f);
    glm::vec3 offset(0.f), delta(0.f);
    auto addPlay = [&](const AnimatorRuntime::Play& p, float weight) {
      const AnimClip* c = p.Valid() && weight > 0 ? in.data->Clip(p.clip) : nullptr;
      if (!c) return;
      const glm::vec3 rest = sk.joints[root].t;
      const glm::vec3 now = RootTranslation(*c, root, p.time, rest), start = RootTranslation(*c, root, 0.f, rest);
      glm::vec3 d = now - RootTranslation(*c, root, p.prevTime, rest);
      if (p.loop && p.time < p.prevTime) d += RootTranslation(*c, root, p.duration, rest) - start; // wrapped
      offset += parent * (now - start) * weight;
      delta += parent * d * weight;
    };
    addPlay(cur, in.rt.Fading() ? 1.f - w : 1.f);
    if (in.rt.Fading()) addPlay(next, w);
    offset.y = delta.y = 0.f;
    for (glm::mat4& m : model) m[3] -= glm::vec4(offset, 0.f);
    if (SceneObject* o = scene.Find(id); o && glm::dot(delta, delta) > 0.f)
      if (Component* tc = o->Find("Transform")) {
        Transform& t = *static_cast<Transform*>(tc->data);
        const glm::vec3 world = glm::mat3(scene.WorldMatrix(id)) * delta;
        const glm::mat3 parentWorld = o->parent.empty() ? glm::mat3(1.f) : glm::mat3(scene.WorldMatrix(o->parent));
        const glm::vec3 local = glm::inverse(parentWorld) * world;
        t.position = {t.position.x + local.x, t.position.y + local.y, t.position.z + local.z};
      }
  }
  ModelToSkin(sk, model, in.pose.skin);
  in.pose.mesh = in.mesh;
  in.pose.serial = ++g_poseSerial; // never repeats, across play sessions too
}
}
