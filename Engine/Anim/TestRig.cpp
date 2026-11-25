#include "Anim/TestRig.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
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
}
