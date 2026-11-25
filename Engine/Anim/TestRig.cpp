#include "Anim/TestRig.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace bdg {
namespace {
std::string Base64(const std::vector<uint8_t>& in) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
    o += k[v >> 18 & 63]; o += k[v >> 12 & 63]; o += k[v >> 6 & 63]; o += k[v & 63];
  }
  if (i + 1 == in.size()) {
    const uint32_t v = (uint32_t)in[i] << 16;
    o += k[v >> 18 & 63]; o += k[v >> 12 & 63]; o += "==";
  } else if (i + 2 == in.size()) {
    const uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8;
    o += k[v >> 18 & 63]; o += k[v >> 12 & 63]; o += k[v >> 6 & 63]; o += '=';
  }
  return o;
}

struct Builder {
  std::vector<uint8_t> bin;
  std::string views, accessors;
  int viewCount = 0, accessorCount = 0;
  // One buffer view + accessor over floats (componentType 5126) or unsigned shorts (5123) / ints (5125).
  int Add(const void* data, size_t bytes, size_t count, const char* type, int componentType, const std::string& minMax = {}, int target = 0) {
    while (bin.size() % 4) bin.push_back(0);
    const size_t offset = bin.size();
    bin.insert(bin.end(), (const uint8_t*)data, (const uint8_t*)data + bytes);
    views += std::format("{}{{\"buffer\":0,\"byteOffset\":{},\"byteLength\":{}{}}}", viewCount ? "," : "", offset, bytes,
                         target ? std::format(",\"target\":{}", target) : "");
    accessors += std::format("{}{{\"bufferView\":{},\"componentType\":{},\"count\":{},\"type\":\"{}\"{}}}", accessorCount ? "," : "", viewCount,
                             componentType, count, type, minMax);
    ++viewCount;
    return accessorCount++;
  }
  int Floats(const std::vector<float>& f, const char* type, size_t comps, bool minMax = false) {
    std::string mm;
    if (minMax) {
      std::vector<float> lo(comps, 1e30f), hi(comps, -1e30f);
      for (size_t i = 0; i < f.size(); ++i) {
        lo[i % comps] = std::min(lo[i % comps], f[i]);
        hi[i % comps] = std::max(hi[i % comps], f[i]);
      }
      std::string a, b;
      for (size_t c = 0; c < comps; ++c) {
        a += std::format("{}{}", c ? "," : "", lo[c]);
        b += std::format("{}{}", c ? "," : "", hi[c]);
      }
      mm = std::format(",\"min\":[{}],\"max\":[{}]", a, b);
    }
    return Add(f.data(), f.size() * sizeof(float), f.size() / comps, type, 5126, mm);
  }
};

void Quat(float axisX, float axisY, float axisZ, float degrees, std::vector<float>& out) {
  const float h = degrees * 3.14159265f / 360.f;
  out.insert(out.end(), {axisX * std::sin(h), axisY * std::sin(h), axisZ * std::sin(h), std::cos(h)});
}
}

std::string MakeTestRigGltf() {
  Builder b;
  const float h = 0.2f;
  const float cx[4] = {-h, h, h, -h}, cz[4] = {-h, -h, h, h};
  std::vector<float> pos;
  std::vector<uint16_t> joints;
  std::vector<float> weights;
  for (int ring = 0; ring < 6; ++ring) {
    const float y = ring * 0.4f;
    // weights: Root below 0.8 m, Spine around 1.2 m, Head at the top; halfway rings share
    float w[3] = {0, 0, 0};
    if (ring <= 1) w[0] = 1;
    else if (ring == 2) w[0] = w[1] = 0.5f;
    else if (ring == 3) w[1] = 1;
    else if (ring == 4) w[1] = w[2] = 0.5f;
    else w[2] = 1;
    for (int c = 0; c < 4; ++c) {
      pos.insert(pos.end(), {cx[c], y, cz[c]});
      joints.insert(joints.end(), {0, 1, 2, 0});
      weights.insert(weights.end(), {w[0], w[1], w[2], 0.f});
    }
  }
  std::vector<uint16_t> idx;
  for (int ring = 0; ring < 5; ++ring)
    for (int c = 0; c < 4; ++c) {
      const uint16_t la = (uint16_t)(ring * 4 + c), lb = (uint16_t)(ring * 4 + (c + 1) % 4), ua = (uint16_t)(la + 4), ub = (uint16_t)(lb + 4);
      idx.insert(idx.end(), {la, ub, lb, la, ua, ub}); // counter-clockwise seen from outside
    }
  idx.insert(idx.end(), {20, 23, 22, 20, 22, 21}); // top (+Y)
  idx.insert(idx.end(), {0, 1, 2, 0, 2, 3});       // bottom (-Y)
  const int aPos = b.Floats(pos, "VEC3", 3, true);
  const int aJoints = b.Add(joints.data(), joints.size() * 2, joints.size() / 4, "VEC4", 5123);
  const int aWeights = b.Floats(weights, "VEC4", 4);
  const int aIdx = b.Add(idx.data(), idx.size() * 2, idx.size(), "SCALAR", 5123, {}, 34963);
  std::vector<float> ibm;
  for (float y : {0.f, -0.8f, -1.6f}) ibm.insert(ibm.end(), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, y, 0, 1}); // column major
  const int aIbm = b.Floats(ibm, "MAT4", 16);

  // clips: {node, path, times, values}
  struct Ch { int node; const char* path; std::vector<float> t, v; };
  auto clip = [&](const char* name, const std::vector<Ch>& chans) {
    std::string samplers, channels;
    for (size_t i = 0; i < chans.size(); ++i) {
      const int in = b.Floats(chans[i].t, "SCALAR", 1, true);
      const bool rot = std::strcmp(chans[i].path, "rotation") == 0;
      const int out = b.Floats(chans[i].v, rot ? "VEC4" : "VEC3", rot ? 4 : 3);
      samplers += std::format("{}{{\"input\":{},\"output\":{},\"interpolation\":\"LINEAR\"}}", i ? "," : "", in, out);
      channels += std::format("{}{{\"sampler\":{},\"target\":{{\"node\":{},\"path\":\"{}\"}}}}", i ? "," : "", i, chans[i].node, chans[i].path);
    }
    return std::format("{{\"name\":\"{}\",\"samplers\":[{}],\"channels\":[{}]}}", name, samplers, channels);
  };
  std::vector<float> idleRot, runRot;
  Quat(0, 0, 1, 0, idleRot);
  Quat(0, 0, 1, 6, idleRot);
  Quat(0, 0, 1, 0, idleRot);
  Quat(1, 0, 0, -25, runRot); // lean forward: +Y turns towards -Z
  Quat(1, 0, 0, -30, runRot);
  Quat(1, 0, 0, -25, runRot);
  const std::string anims = clip("Idle", {{2, "rotation", {0, 1, 2}, idleRot}}) + "," +
                            clip("Run", {{1, "translation", {0, 1}, {0, 0, 0, 0, 0, -3}}, {2, "rotation", {0, 0.5f, 1}, runRot}}) + "," +
                            clip("Jump", {{1, "translation", {0, 0.4f, 0.8f}, {0, 0, 0, 0, 1, 0, 0, 0, 0}}});

  return std::format(
      "{{\"asset\":{{\"version\":\"2.0\",\"generator\":\"Bidulgi test rig\"}},\"scene\":0,\"scenes\":[{{\"nodes\":[0,4]}}],\n"
      "\"nodes\":[{{\"name\":\"Armature\",\"children\":[1]}},{{\"name\":\"Root\",\"children\":[2]}},"
      "{{\"name\":\"Spine\",\"translation\":[0,0.8,0],\"children\":[3]}},{{\"name\":\"Head\",\"translation\":[0,0.8,0]}},"
      "{{\"name\":\"Body\",\"mesh\":0,\"skin\":0}}],\n"
      "\"skins\":[{{\"joints\":[1,2,3],\"inverseBindMatrices\":{}}}],\n"
      "\"meshes\":[{{\"name\":\"Body\",\"primitives\":[{{\"attributes\":{{\"POSITION\":{},\"JOINTS_0\":{},\"WEIGHTS_0\":{}}},\"indices\":{},\"material\":0}}]}}],\n"
      "\"materials\":[{{\"name\":\"RigBody\",\"pbrMetallicRoughness\":{{\"baseColorFactor\":[0.9,0.45,0.2,1],\"metallicFactor\":0,\"roughnessFactor\":0.6}}}}],\n"
      "\"animations\":[{}],\n"
      "\"accessors\":[{}],\n\"bufferViews\":[{}],\n"
      "\"buffers\":[{{\"byteLength\":{},\"uri\":\"data:application/octet-stream;base64,{}\"}}]}}\n",
      aIbm, aPos, aJoints, aWeights, aIdx, anims, b.accessors, b.views, b.bin.size(), Base64(b.bin));
}

std::string TestRigAnimGraph() {
  return R"({"v":1, "skeleton":"Models/Rig.gltf",
 "vars":{"Speed":"float", "Jump":"trigger"},
 "stateMachines":{"Locomotion":{
   "entry":"Idle",
   "states":{
     "Idle":{"play":"Idle"},
     "Run":{"play":"Run", "events":[{"event":"Footstep", "at":0.25}, {"event":"Footstep", "at":0.75}]},
     "Jump":{"play":"Jump", "loop":false, "events":[{"event":"Land", "at":0.8}]}},
   "transitions":[
     {"from":"Idle", "to":"Run", "when":"Speed > 0.1", "blend":0.25},
     {"from":"Run", "to":"Idle", "when":"Speed < 0.1", "blend":0.25},
     {"from":"*", "to":"Jump", "when":"Jump", "blend":0.1},
     {"from":"Jump", "to":"Idle", "exitTime":1, "blend":0.15}]}}}
)";
}

// ---- generated humanoid (7-1 part 2, 7-1b) ----
namespace {
struct HJoint { const char* name; int parent; float x, y, z; };
// local offsets from the parent (metres); the character stands on y = 0 and faces +Z (glTF)
const HJoint kHumanoid[] = {
    {"Hips", -1, 0, 1.0f, 0},          {"Spine", 0, 0, 0.15f, 0},          {"Chest", 1, 0, 0.25f, 0},
    {"Neck", 2, 0, 0.20f, 0},          {"Head", 3, 0, 0.10f, 0},           {"LeftUpperArm", 2, 0.20f, 0.12f, 0},
    {"LeftLowerArm", 5, 0, -0.28f, 0}, {"LeftHand", 6, 0, -0.26f, 0},      {"RightUpperArm", 2, -0.20f, 0.12f, 0},
    {"RightLowerArm", 8, 0, -0.28f, 0}, {"RightHand", 9, 0, -0.26f, 0},    {"LeftUpperLeg", 0, 0.10f, -0.06f, 0},
    {"LeftLowerLeg", 11, 0, -0.44f, 0.03f}, {"LeftFoot", 12, 0, -0.42f, -0.03f}, {"RightUpperLeg", 0, -0.10f, -0.06f, 0},
    {"RightLowerLeg", 14, 0, -0.44f, 0.03f}, {"RightFoot", 15, 0, -0.42f, -0.03f}};
constexpr int kJoints = (int)(sizeof(kHumanoid) / sizeof(kHumanoid[0]));
enum J { Hips, Spine, Chest, Neck, Head, LUA, LLA, LH, RUA, RLA, RH, LUL, LLL, LF, RUL, RLL, RF };
struct HBox { int joint; float x0, x1, y0, y1, z0, z1; };
const HBox kBoxes[] = {
    {Hips, -0.17f, 0.17f, 0.88f, 1.12f, -0.11f, 0.11f},  {Spine, -0.15f, 0.15f, 1.10f, 1.40f, -0.10f, 0.10f},
    {Chest, -0.19f, 0.19f, 1.36f, 1.60f, -0.12f, 0.12f}, {Neck, -0.05f, 0.05f, 1.56f, 1.70f, -0.05f, 0.05f},
    {Head, -0.10f, 0.10f, 1.66f, 1.92f, -0.10f, 0.12f},  {Head, -0.03f, 0.03f, 1.75f, 1.80f, 0.12f, 0.16f}, // nose: which way it faces
    {LUA, 0.155f, 0.245f, 1.24f, 1.54f, -0.05f, 0.05f},  {LLA, 0.16f, 0.24f, 0.99f, 1.26f, -0.045f, 0.045f},
    {LH, 0.17f, 0.23f, 0.88f, 1.0f, -0.04f, 0.04f},      {RUA, -0.245f, -0.155f, 1.24f, 1.54f, -0.05f, 0.05f},
    {RLA, -0.24f, -0.16f, 0.99f, 1.26f, -0.045f, 0.045f}, {RH, -0.23f, -0.17f, 0.88f, 1.0f, -0.04f, 0.04f},
    {LUL, 0.035f, 0.165f, 0.50f, 0.96f, -0.06f, 0.08f},  {LLL, 0.045f, 0.155f, 0.09f, 0.52f, -0.05f, 0.07f},
    {LF, 0.05f, 0.15f, 0.0f, 0.10f, -0.06f, 0.18f},      {RUL, -0.165f, -0.035f, 0.50f, 0.96f, -0.06f, 0.08f},
    {RLL, -0.155f, -0.045f, 0.09f, 0.52f, -0.05f, 0.07f}, {RF, -0.15f, -0.05f, 0.0f, 0.10f, -0.06f, 0.18f}};

glm::quat Euler(float xDeg, float yDeg, float zDeg) { // applied Z, then Y, then X (small angles: order matters little)
  const float r = 3.14159265f / 180.f;
  return glm::angleAxis(xDeg * r, glm::vec3(1, 0, 0)) * glm::angleAxis(yDeg * r, glm::vec3(0, 1, 0)) * glm::angleAxis(zDeg * r, glm::vec3(0, 0, 1));
}
struct Key { float x = 0, y = 0, z = 0; }; // degrees
// One clip: per joint a rotation (and the hips a height) as functions of the phase 0..1, sampled at `keys` + 1 times.
struct Motion {
  std::function<Key(int joint, float phase)> rot;
  std::function<float(float phase)> hipY;
};
float Pos(float v) { return v > 0 ? v : 0; }
}

std::string MakeHumanoidGltf() {
  Builder b;
  glm::vec3 world[kJoints];
  for (int j = 0; j < kJoints; ++j)
    world[j] = (kHumanoid[j].parent >= 0 ? world[kHumanoid[j].parent] : glm::vec3(0)) + glm::vec3(kHumanoid[j].x, kHumanoid[j].y, kHumanoid[j].z);
  // boxes, each on one joint, flat normals
  std::vector<float> pos, nrm, weights;
  std::vector<uint16_t> joints, idx;
  for (const HBox& bx : kBoxes) {
    const float x[2] = {bx.x0, bx.x1}, y[2] = {bx.y0, bx.y1}, z[2] = {bx.z0, bx.z1};
    static const int faces[6][4][3] = {{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
                                       {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}},
                                       {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
    static const float normals[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
      const uint16_t base = (uint16_t)(pos.size() / 3);
      for (int c = 0; c < 4; ++c) {
        pos.insert(pos.end(), {x[faces[f][c][0]], y[faces[f][c][1]], z[faces[f][c][2]]});
        nrm.insert(nrm.end(), {normals[f][0], normals[f][1], normals[f][2]});
        joints.insert(joints.end(), {(uint16_t)bx.joint, 0, 0, 0});
        weights.insert(weights.end(), {1.f, 0.f, 0.f, 0.f});
      }
      idx.insert(idx.end(), {base, (uint16_t)(base + 1), (uint16_t)(base + 2), base, (uint16_t)(base + 2), (uint16_t)(base + 3)});
    }
  }
  const int aPos = b.Floats(pos, "VEC3", 3, true);
  const int aNrm = b.Floats(nrm, "VEC3", 3);
  const int aJoints = b.Add(joints.data(), joints.size() * 2, joints.size() / 4, "VEC4", 5123);
  const int aWeights = b.Floats(weights, "VEC4", 4);
  const int aIdx = b.Add(idx.data(), idx.size() * 2, idx.size(), "SCALAR", 5123, {}, 34963);
  std::vector<float> ibm;
  for (int j = 0; j < kJoints; ++j) ibm.insert(ibm.end(), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -world[j].x, -world[j].y, -world[j].z, 1});
  const int aIbm = b.Floats(ibm, "MAT4", 16);

  // clips: rotation channels for every joint a motion moves, the hips' height
  std::string anims;
  auto clip = [&](const char* name, float duration, int keys, const Motion& m) {
    std::vector<float> times;
    for (int k = 0; k <= keys; ++k) times.push_back(duration * (float)k / (float)keys);
    std::string samplers, channels;
    int n = 0;
    auto channel = [&](int node, const char* path, const std::vector<float>& values, bool rot) {
      const int in = b.Floats(times, "SCALAR", 1, true);
      const int out = b.Floats(values, rot ? "VEC4" : "VEC3", rot ? 4 : 3);
      samplers += std::format("{}{{\"input\":{},\"output\":{},\"interpolation\":\"LINEAR\"}}", n ? "," : "", in, out);
      channels += std::format("{}{{\"sampler\":{},\"target\":{{\"node\":{},\"path\":\"{}\"}}}}", n ? "," : "", n, node, path);
      ++n;
    };
    for (int j = 0; j < kJoints; ++j) {
      std::vector<float> v;
      bool moves = false;
      for (int k = 0; k <= keys; ++k) {
        const Key e = m.rot(j, (float)k / (float)keys);
        moves = moves || e.x != 0 || e.y != 0 || e.z != 0;
        const glm::quat q = Euler(e.x, e.y, e.z);
        v.insert(v.end(), {q.x, q.y, q.z, q.w});
      }
      if (moves) channel(1 + j, "rotation", v, true);
    }
    if (m.hipY) {
      std::vector<float> v;
      for (int k = 0; k <= keys; ++k) v.insert(v.end(), {0.f, m.hipY((float)k / (float)keys), 0.f});
      channel(1 + Hips, "translation", v, false);
    }
    anims += std::format("{}{{\"name\":\"{}\",\"samplers\":[{}],\"channels\":[{}]}}", anims.empty() ? "" : ",", name, samplers, channels);
  };
  const float pi2 = 6.2831853f;
  auto walk = [&](float dir, float thigh, float knee, float arm, float elbow, float lean, float bob) {
    Motion m;
    m.rot = [=](int j, float p) {
      const float s = std::sin(pi2 * p * dir);
      Key k;
      switch (j) {
        case LUL: k.x = -thigh * s; break;
        case RUL: k.x = thigh * s; break;
        case LLL: k.x = 4 + knee * Pos(s); break;   // the knee bends while that leg swings back
        case RLL: k.x = 4 + knee * Pos(-s); break;
        case LF: k.x = -0.3f * thigh * s; break;
        case RF: k.x = 0.3f * thigh * s; break;
        case LUA: k.x = arm * s; k.z = 6; break;     // arms swing against the legs
        case RUA: k.x = -arm * s; k.z = -6; break;
        case LLA: case RLA: k.x = -elbow; break;
        case Spine: k.x = lean; k.y = 4 * s; break;
        case Head: k.x = -lean * 0.5f; break;
        default: break;
      }
      return k;
    };
    m.hipY = [=](float p) { return 1.0f - bob * std::fabs(std::cos(pi2 * p)); };
    return m;
  };
  Motion idle;
  idle.rot = [&](int j, float p) {
    const float s = std::sin(pi2 * p);
    Key k;
    if (j == Spine) k.x = 1.5f * s;
    if (j == Chest) k.x = 1.f * s;
    if (j == LUA) k.z = 5 + s;
    if (j == RUA) k.z = -5 - s;
    if (j == LLA || j == RLA) k.x = -8;
    if (j == Head) k.y = 4 * std::sin(pi2 * p * 0.5f);
    return k;
  };
  idle.hipY = [&](float p) { return 1.0f - 0.006f * (0.5f - 0.5f * std::cos(pi2 * p)); };
  clip("Idle", 2.f, 16, idle);
  clip("Walk", 1.0f, 16, walk(1, 26, 38, 22, 15, 3, 0.025f));
  clip("Run", 0.66f, 16, walk(1, 44, 75, 40, 70, 12, 0.05f));
  clip("WalkBack", 1.0f, 16, walk(-1, 20, 30, 15, 15, -2, 0.02f));
  for (int side = 0; side < 2; ++side) {
    Motion m;
    const float d = side == 0 ? 1.f : -1.f; // StrafeLeft steps towards +X (the character's left)
    m.rot = [=](int j, float p) {
      const float s = std::sin(pi2 * p);
      Key k;
      if (j == LUL) k.z = d * 14 * Pos(s);
      if (j == RUL) k.z = d * 14 * Pos(-s);
      if (j == LLL) k.x = 4 + 20 * Pos(s);
      if (j == RLL) k.x = 4 + 20 * Pos(-s);
      if (j == LUA) k.z = 6 + 4 * s;
      if (j == RUA) k.z = -6 + 4 * s;
      if (j == Spine) k.z = -d * 3;
      return k;
    };
    m.hipY = [=](float p) { return 1.0f - 0.02f * std::fabs(std::cos(pi2 * p)); };
    clip(side == 0 ? "StrafeLeft" : "StrafeRight", 1.0f, 16, m);
  }
  Motion jump; // crouch, push off with the arms up, land
  jump.rot = [&](int j, float p) {
    const float crouch = p < 0.25f ? p / 0.25f : p < 0.4f ? 1 - (p - 0.25f) / 0.15f : p > 0.8f ? (p - 0.8f) / 0.2f * 0.6f : 0.f;
    const float up = p > 0.25f && p < 0.85f ? std::sin((p - 0.25f) / 0.6f * 3.14159f) : 0.f;
    Key k;
    if (j == LUL || j == RUL) k.x = -45 * crouch - 15 * up;
    if (j == LLL || j == RLL) k.x = 4 + 80 * crouch + 30 * up;
    if (j == LF || j == RF) k.x = -35 * crouch;
    if (j == LUA) k.z = 10 + 120 * up;
    if (j == RUA) k.z = -10 - 120 * up;
    if (j == Spine) k.x = 15 * crouch;
    return k;
  };
  jump.hipY = [&](float p) {
    const float crouch = p < 0.25f ? p / 0.25f : p < 0.4f ? 1 - (p - 0.25f) / 0.15f : p > 0.8f ? (p - 0.8f) / 0.2f * 0.6f : 0.f;
    return 1.0f - 0.25f * crouch;
  };
  clip("Jump", 0.8f, 16, jump);
  Motion wave; // the right arm raised to the side, the forearm waving
  wave.rot = [&](int j, float p) {
    Key k;
    if (j == RUA) k.z = -150;
    if (j == RLA) k.z = -25 + 30 * std::sin(pi2 * p);
    return k;
  };
  clip("Wave", 1.2f, 16, wave);
  Motion lean; // an additive pose: the spine leans to the side after the first frame
  lean.rot = [&](int j, float p) {
    Key k;
    if (j == Spine) k.z = p > 0.f ? 14.f : 0.f;
    return k;
  };
  clip("LeanAdd", 1.0f, 100, lean); // the lean is reached 0.01 s in

  std::string nodes = "{\"name\":\"Armature\",\"children\":[1]}";
  for (int j = 0; j < kJoints; ++j) {
    std::string children;
    for (int c = 0; c < kJoints; ++c)
      if (kHumanoid[c].parent == j) children += std::format("{}{}", children.empty() ? "" : ",", c + 1);
    nodes += std::format(",{{\"name\":\"{}\",\"translation\":[{},{},{}]{}}}", kHumanoid[j].name, kHumanoid[j].x, kHumanoid[j].y, kHumanoid[j].z,
                         children.empty() ? "" : ",\"children\":[" + children + "]");
  }
  nodes += std::format(",{{\"name\":\"Body\",\"mesh\":0,\"skin\":0}}");
  std::string skinJoints;
  for (int j = 0; j < kJoints; ++j) skinJoints += std::format("{}{}", j ? "," : "", j + 1);
  return std::format(
      "{{\"asset\":{{\"version\":\"2.0\",\"generator\":\"Bidulgi humanoid (CC0)\"}},\"scene\":0,\"scenes\":[{{\"nodes\":[0,{}]}}],\n"
      "\"nodes\":[{}],\n\"skins\":[{{\"joints\":[{}],\"inverseBindMatrices\":{}}}],\n"
      "\"meshes\":[{{\"name\":\"Body\",\"primitives\":[{{\"attributes\":{{\"POSITION\":{},\"NORMAL\":{},\"JOINTS_0\":{},\"WEIGHTS_0\":{}}},\"indices\":{},\"material\":0}}]}}],\n"
      "\"materials\":[{{\"name\":\"HeroBody\",\"pbrMetallicRoughness\":{{\"baseColorFactor\":[0.25,0.45,0.85,1],\"metallicFactor\":0,\"roughnessFactor\":0.55}}}}],\n"
      "\"animations\":[{}],\n\"accessors\":[{}],\n\"bufferViews\":[{}],\n"
      "\"buffers\":[{{\"byteLength\":{},\"uri\":\"data:application/octet-stream;base64,{}\"}}]}}\n",
      kJoints + 1, nodes, skinJoints, aIbm, aPos, aNrm, aJoints, aWeights, aIdx, anims, b.accessors, b.views, b.bin.size(), Base64(b.bin));
}

std::string HumanoidAnimGraph() {
  return R"({"v":1, "skeleton":"Models/Hero.gltf",
 "vars":{"Speed":"float", "MoveX":"float", "MoveY":"float", "Strafe":"bool", "Jump":"trigger", "Wave":"float", "Lean":"float"},
 "stateMachines":{
  "Locomotion":{
   "entry":"Move",
   "states":{
     "Move":{"blendSpace":{"x":"Speed", "samples":[{"clip":"Idle", "x":0}, {"clip":"Walk", "x":1.6}, {"clip":"Run", "x":5}]},
             "events":[{"event":"Footstep", "at":0.25}, {"event":"Footstep", "at":0.75}]},
     "Strafe":{"blendSpace":{"x":"MoveX", "y":"MoveY", "samples":[{"clip":"Idle", "x":0, "y":0}, {"clip":"Walk", "x":0, "y":1},
               {"clip":"WalkBack", "x":0, "y":-1}, {"clip":"StrafeLeft", "x":-1, "y":0}, {"clip":"StrafeRight", "x":1, "y":0}]}},
     "Jump":{"play":"Jump", "loop":false}},
   "transitions":[
     {"from":"Move", "to":"Strafe", "when":"Strafe", "blend":0.2},
     {"from":"Strafe", "to":"Move", "when":"!Strafe", "blend":0.2},
     {"from":"*", "to":"Jump", "when":"Jump", "blend":0.08},
     {"from":"Jump", "to":"Move", "exitTime":0.95, "blend":0.2}]},
  "Arms":{"entry":"Wave", "states":{"Wave":{"play":"Wave"}}},
  "Lean":{"entry":"Lean", "states":{"Lean":{"play":"LeanAdd"}}}},
 "layers":[
   {"name":"UpperBody", "machine":"Arms", "fromBone":"RightUpperArm", "weight":"Wave", "mode":"override"},
   {"name":"Lean", "machine":"Lean", "fromBone":"Spine", "weight":"Lean", "mode":"additive"}]}
)";
}
}
