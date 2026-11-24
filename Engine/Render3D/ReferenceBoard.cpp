#include "Render3D/ReferenceBoard.h"
#include "Assets/GltfImport.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Assets/TextureCooker.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <format>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg {
namespace {
// ColorChecker Classic, sRGB (D65) values per patch, rows top to bottom (BabelColor averages, rounded).
const uint8_t kChecker[24][3] = {
  {115, 82, 68},  {194, 150, 130}, {98, 122, 157}, {87, 108, 67},  {133, 128, 177}, {103, 189, 170},
  {214, 126, 44}, {80, 91, 166},   {193, 90, 99},  {94, 60, 108},  {157, 188, 64},  {224, 163, 46},
  {56, 61, 150},  {70, 148, 73},   {175, 54, 60},  {231, 199, 31}, {187, 86, 149},  {8, 133, 161},
  {243, 243, 242}, {200, 200, 200}, {160, 160, 160}, {122, 122, 121}, {85, 85, 85},  {52, 52, 52}};
constexpr int kTex = 64, kMark = 10;
const uint8_t kTL[3] = {220, 40, 40}, kTR[3] = {40, 200, 60}, kBL[3] = {40, 80, 220}, kBR[3] = {240, 210, 40};

// Board layout (world units = metres).
const glm::vec3 kGreyCard{-4.4f, 1.3f, 0.5f};
const glm::vec3 kLetters{-4.4f, 3.0f, 0.5f};
const float kLettersSize = 1.3f;
const glm::vec3 kChecker0{4.4f, 1.3f, 0.5f};
const float kPatch = 0.34f, kPitch = 0.38f;
const glm::vec3 kCCW{3.8f, 3.0f, 0.5f}, kCW{5.0f, 3.0f, 0.5f};
// Lighting zone on the ground between the axes and the sphere wall (queue 3-4).
const glm::vec3 kCaster{-2.2f, 1.0f, 1.2f};                       // floating box: its sun shadow lands on the ground
const glm::vec3 kLitGround{-1.7f, 0.f, 1.6f};
const glm::vec3 kPointLight{2.0f, 0.35f, 0.9f}, kPointFar{2.0f, 0.f, -1.2f};
const glm::vec3 kSpotLight{-0.9f, 2.5f, -1.6f}, kSpotOut{-2.3f, 0.f, -1.6f};
const glm::vec3 kProbeParent{-4.4f, 4.6f, 0.f}, kProbeChild{0.f, 0.f, 0.5f}; // glTF probe: parent + child translation
const uint8_t kProbeColors[4][3] = {{230, 60, 160}, {60, 200, 220}, {250, 150, 30}, {120, 90, 230}}; // TL TR BL BR
const glm::vec3 kDepthNear{4.25f, 4.4f, 0.8f}, kDepthFar{4.55f, 4.4f, 0.5f};
const Color kOrange{1.f, 0.45f, 0.1f, 1}, kYellow{1.f, 0.85f, 0.05f, 1}, kMagenta{0.8f, 0.05f, 0.7f, 1};
const Color kAxisX{0.9f, 0.04f, 0.04f, 1}, kAxisY{0.05f, 0.75f, 0.06f, 1}, kAxisZ{0.04f, 0.12f, 0.95f, 1};
const float kAxisLen = 1.2f, kShaft = 0.9f;

glm::vec3 SpherePos(int col, int row) { return {-2.4f + col * 0.8f, 0.55f + row * 0.78f, -3.f}; }
const float kSphereR = 0.32f;

uint8_t LinearToSrgb8(float c) {
  c = std::clamp(c, 0.f, 1.f);
  float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
  return (uint8_t)std::lround(s * 255.f);
}

// The letter board picture: an "F" (mirroring shows at once) and four coloured corners.
std::vector<uint8_t> LetterTexture() {
  std::vector<uint8_t> px((size_t)kTex * kTex * 4);
  auto fill = [&](int x0, int y0, int x1, int y1, const uint8_t c[3]) {
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        uint8_t* p = &px[((size_t)y * kTex + x) * 4];
        p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
      }
  };
  const uint8_t paper[3] = {235, 235, 230}, ink[3] = {20, 20, 24};
  fill(0, 0, kTex, kTex, paper);
  fill(20, 14, 28, 50, ink); // stem
  fill(20, 14, 46, 21, ink); // top bar
  fill(20, 29, 40, 35, ink); // middle bar
  fill(0, 0, kMark, kMark, kTL);
  fill(kTex - kMark, 0, kTex, kMark, kTR);
  fill(0, kTex - kMark, kMark, kTex, kBL);
  fill(kTex - kMark, kTex - kMark, kTex, kTex, kBR);
  return px;
}

glm::mat4 At(const glm::vec3& p, float sx = 1, float sy = 1, float sz = 1) {
  return glm::scale(glm::translate(glm::mat4(1.f), p), glm::vec3(sx, sy, sz));
}
std::string Base64(const uint8_t* p, size_t n) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    out += k[v >> 18 & 63];
    out += k[v >> 12 & 63];
    out += i + 1 < n ? k[v >> 6 & 63] : '=';
    out += i + 2 < n ? k[v & 63] : '=';
  }
  return out;
}

glm::vec3 ChecherPatchPos(int i) {
  int col = i % 6, row = i / 6;
  return kChecker0 + glm::vec3((col - 2.5f) * kPitch, (1.5f - row) * kPitch, 0.f);
}
}

bool ReferenceBoard::Init(RHI& rhi, Renderer3D& r) {
  rhi_ = &rhi;
  renderer_ = &r;
  plane_ = r.CreateMesh(MakePlaneXZ(1, 1));
  sphere_ = r.CreateMesh(MakeSphere(1));
  quad_ = r.CreateMesh(MakeQuad(1, 1));
  MeshData cw = MakeQuad(1, 1);
  cw.FlipWinding();
  quadCW_ = r.CreateMesh(cw);
  cylinder_ = r.CreateMesh(MakeCylinder(1, 1));
  cone_ = r.CreateMesh(MakeCone(1, 1));
  box_ = r.CreateMesh(MakeBox(1, 1, 1));
  std::vector<uint8_t> letters = LetterTexture();
  letters_ = rhi.CreateTexture2D(kTex, kTex, TextureFormat::SRGBA8, letters.data());

  camera.position = {0, 3.0f, 9.5f};
  camera.target = {0, 2.4f, -1.f};
  camera.fovY = 40.f;
  light.toLight = {-0.4f, 0.8f, 0.45f};
  light.intensity = 2.6f;
  light.shadows = true;
  LocalLight3D point; // red, close to the ground
  point.position = kPointLight;
  point.color = {1.f, 0.1f, 0.05f, 1};
  point.intensity = 3.f;
  point.range = 1.6f;
  LocalLight3D spot; // white, straight down
  spot.position = kSpotLight;
  spot.direction = {0, -1, 0};
  spot.intensity = 8.f;
  spot.range = 6.f;
  spot.spot = true;
  spot.outerAngle = 40.f;
  spot.innerAngle = 25.f;
  light.locals = {point, spot};

  auto add = [&](const Mesh3D& m, const glm::mat4& model, Material3D mat) { items_.push_back({&m, model, mat}); };
  auto unlit = [](Color c) { Material3D m; m.baseColor = c; m.unlit = true; return m; };

  Material3D ground;
  ground.baseColor = {0.2f, 0.2f, 0.21f, 1};
  ground.roughness = 0.9f;
  add(plane_, At({0, 0, 0}, 30, 1, 30), ground);

  for (int row = 0; row < 7; ++row) // X: roughness 0 -> 1, Y: metallic 0 -> 1
    for (int col = 0; col < 7; ++col) {
      Material3D m;
      m.baseColor = {0.85f, 0.85f, 0.85f, 1};
      m.roughness = col / 6.f;
      m.metallic = row / 6.f;
      add(sphere_, At(SpherePos(col, row), kSphereR, kSphereR, kSphereR), m);
    }

  // Axis arrows at the world origin: cylinder + cone built along +Y, turned onto each axis.
  auto arrow = [&](const glm::mat4& turn, Color c) {
    add(cylinder_, turn * At({0, 0, 0}, 0.035f, kShaft, 0.035f), unlit(c));
    add(cone_, turn * glm::translate(glm::mat4(1.f), {0, kShaft, 0}) * At({0, 0, 0}, 0.1f, kAxisLen - kShaft, 0.1f), unlit(c));
  };
  arrow(glm::rotate(glm::mat4(1.f), glm::radians(-90.f), {0, 0, 1}), kAxisX); // +Y -> +X
  arrow(glm::mat4(1.f), kAxisY);
  arrow(glm::rotate(glm::mat4(1.f), glm::radians(90.f), {1, 0, 0}), kAxisZ);  // +Y -> +Z
  add(sphere_, At({0, 0, 0}, 0.07f, 0.07f, 0.07f), unlit({0.9f, 0.9f, 0.9f, 1}));
  Material3D casterMat;
  casterMat.baseColor = {0.6f, 0.6f, 0.62f, 1};
  add(box_, At(kCaster, 0.5f, 0.5f, 0.5f), casterMat);

  add(quad_, At(kGreyCard, 1.0f, 1.0f), unlit({0.18f, 0.18f, 0.18f, 1})); // 18% grey: display value 118
  Material3D lettersMat = unlit({1, 1, 1, 1});
  lettersMat.baseMap = letters_;
  add(quad_, At(kLetters, kLettersSize, kLettersSize), lettersMat);

  add(quad_, At(kChecker0 - glm::vec3(0, 0, 0.01f), 6 * kPitch + 0.16f, 4 * kPitch + 0.16f), unlit({0.02f, 0.02f, 0.02f, 1}));
  for (int i = 0; i < 24; ++i)
    add(quad_, At(ChecherPatchPos(i), kPatch, kPatch), unlit(SrgbToLinear(kChecker[i][0], kChecker[i][1], kChecker[i][2])));

  add(quad_, At(kCCW, 0.7f, 0.7f), unlit(kOrange));   // counter-clockwise: front face, drawn
  add(quadCW_, At(kCW, 0.7f, 0.7f), unlit(kOrange));  // clockwise: back face, culled
  add(quad_, At(kDepthNear, 0.7f, 0.7f), unlit(kYellow));  // drawn first but nearer: must stay on top
  add(quad_, At(kDepthFar, 0.7f, 0.7f), unlit(kMagenta));

  // glTF probe (queue 3-1): goes through the real importer, meshoptimizer and the material path.
  ImportedModel im;
  std::string probe = ProbeGltf(), err;
  if (ImportGltfMemory(probe.data(), probe.size(), ".", im, &err)) {
    probe_ = r.UploadModel(im);
    Renderer3D::AppendModel(probe_, glm::mat4(1.f), items_);
    probeLoaded_ = true;
  } else LogError("render-ref: glTF probe did not import: {}", err);

  labels_ = {{kProbeParent + kProbeChild - glm::vec3(0.5f, 0.75f, 0), "glTF import"},
             {{kAxisLen + 0.15f, 0, 0}, "+X"}, {{0, kAxisLen + 0.15f, 0}, "+Y"}, {{0, 0, kAxisLen + 0.2f}, "+Z"},
             {kGreyCard - glm::vec3(0.5f, 0.7f, 0), "18% grey · unlit"},
             {kLetters - glm::vec3(0.65f, 0.85f, 0), "UV · top-left red"},
             {kChecker0 - glm::vec3(1.2f, 1.0f, 0), "ColorChecker"},
             {kCaster + glm::vec3(-0.3f, 0.45f, 0), "shadow"},
             {kPointLight + glm::vec3(0.15f, 0.1f, 0), "point"},
             {kSpotLight + glm::vec3(-0.5f, -2.2f, 0.4f), "spot"},
             {kCCW - glm::vec3(0.35f, 0.55f, 0), "CCW shown · CW culled"},
             {kDepthNear + glm::vec3(-0.35f, 0.45f, 0), "depth: near wins"},
             {SpherePos(0, 0) + glm::vec3(-0.3f, -0.55f, 0.4f), "roughness 0 → 1"},
             {SpherePos(0, 6) + glm::vec3(-1.4f, 0.1f, 0), "metallic"},
             {SpherePos(0, 6) + glm::vec3(-1.4f, -0.25f, 0), "0 → 1 ↑"}};
  return true;
}

bool ReferenceBoard::InitAssets(RHI& rhi, Renderer3D& r, const std::filesystem::path& dir) {
  rhi_ = &rhi;
  renderer_ = &r;
  assetMode_ = true;
  plane_ = r.CreateMesh(MakePlaneXZ(1, 1));
  Material3D ground;
  ground.baseColor = {0.2f, 0.2f, 0.21f, 1};
  ground.roughness = 0.9f;
  items_.push_back({&plane_, At({0, 0, 0}, 40, 1, 40), ground});
  camera.position = {0, 1.7f, 9.f};
  camera.target = {0, 1.1f, 0};
  camera.fovY = 40.f;
  light.toLight = {-0.4f, 0.8f, 0.45f};
  light.intensity = 2.6f;
  const char* names[4] = {"MetalRoughSpheres", "NormalTangentMirrorTest", "TextureCoordinateTest", "AlphaBlendModeTest"};
  assets_.resize(4);
  bool all = true;
  for (int i = 0; i < 4; ++i) {
    AssetModel& a = assets_[i];
    a.name = names[i];
    ImportedModel im;
    std::string err;
    std::filesystem::path file = dir / (a.name + ".glb");
    if (!ImportGltf(file, im, &err)) {
      LogError("render-assets: {} did not import ({}): run `python tools/testassets.py fetch`", a.name, err);
      all = false;
      continue;
    }
    // Facts the renderer depends on, read from the file (they tell whether the import kept what matters).
    int mr = 0, normals = 0, base = 0, tangents = 0, modes[3] = {0, 0, 0};
    for (const ImportedMaterial& m : im.materials) {
      mr += m.metalRoughTex >= 0;
      normals += m.normalTex >= 0;
      base += m.baseColorTex >= 0;
      ++modes[(int)m.alpha];
    }
    for (const ImportedPrimitive& p : im.primitives)
      if (!p.mesh.vertices.empty() && p.mesh.vertices[0].tw != 0) ++tangents;
    a.facts = std::format("{}:mats={},base={},mr={},normal={},tangents={},opaque={},mask={},blend={}", a.name, im.materials.size(), base,
                          mr, normals, tangents, modes[0], modes[1], modes[2]);
    a.model = r.UploadModel(im);
    glm::vec3 size = a.model.boundsMax - a.model.boundsMin, mid = (a.model.boundsMin + a.model.boundsMax) * 0.5f;
    float s = 2.2f / std::max({size.x, size.y, 1e-4f}); // fit width and height (they face +Z, toward the camera)
    glm::vec3 slot{-4.2f + 2.8f * i, 0.f, 0.f};
    glm::mat4 place = glm::translate(glm::mat4(1.f), slot) * glm::scale(glm::mat4(1.f), glm::vec3(s)) *
                      glm::translate(glm::mat4(1.f), {-mid.x, -a.model.boundsMin.y, -mid.z});
    Renderer3D::AppendModel(a.model, place, items_);
    a.center = slot + glm::vec3(0, size.y * s * 0.5f, 0);
    a.ok = true;
    labels_.push_back({slot + glm::vec3(-1.1f, -0.35f, 1.2f), a.name});
  }
  return all;
}

// Cornell box (GI Tier 0): x -3..3, y 0..4, z -3..3, open toward the camera at +z.
const glm::vec3 kMirror{-1.1f, 0.85f, -0.6f}, kMatte{1.2f, 0.75f, 0.7f};
const float kMirrorR = 0.85f, kMatteR = 0.75f;

bool ReferenceBoard::InitGI(RHI& rhi, Renderer3D& r) {
  rhi_ = &rhi;
  renderer_ = &r;
  box_ = r.CreateMesh(MakeBox(1, 1, 1));
  sphere_ = r.CreateMesh(MakeSphere(1));
  auto wall = [&](glm::vec3 center, glm::vec3 size, Color c) {
    Material3D m;
    m.baseColor = c;
    m.roughness = 0.9f;
    items_.push_back({&box_, At(center, size.x, size.y, size.z), m});
  };
  const Color white{0.73f, 0.73f, 0.73f, 1}, red{0.63f, 0.05f, 0.05f, 1}, green{0.12f, 0.45f, 0.08f, 1};
  wall({0, -0.05f, 0}, {6.2f, 0.1f, 6.2f}, white);   // floor
  wall({0, 4.05f, 0}, {6.2f, 0.1f, 6.2f}, white);    // ceiling
  wall({0, 2, -3.05f}, {6.2f, 4.2f, 0.1f}, white);   // back
  wall({-3.05f, 2, 0}, {0.1f, 4.2f, 6.2f}, red);     // left
  wall({3.05f, 2, 0}, {0.1f, 4.2f, 6.2f}, green);    // right
  Material3D mirror;
  mirror.baseColor = {0.95f, 0.95f, 0.95f, 1};
  mirror.metallic = 1;
  mirror.roughness = 0.05f;
  items_.push_back({&sphere_, At(kMirror, kMirrorR, kMirrorR, kMirrorR), mirror});
  Material3D matte;
  matte.baseColor = white;
  matte.roughness = 0.95f;
  items_.push_back({&sphere_, At(kMatte, kMatteR, kMatteR, kMatteR), matte});
  camera.position = {0, 2, 10.5f};
  camera.target = {0, 1.9f, 0};
  camera.fovY = 38.f;
  light = {};
  light.intensity = 0;                 // no sun: the room is lit by its ceiling light and the bounce
  light.sky = {0.02f, 0.02f, 0.025f, 1};
  light.ground = {0.01f, 0.01f, 0.01f, 1};
  LocalLight3D lamp;
  lamp.position = {0, 3.6f, 0.3f};
  lamp.intensity = 14;
  lamp.range = 12;
  light.locals = {lamp};
  ReflectionProbe3D probe;
  probe.position = {0, 2, 0};
  probe.boxMin = {-3, 0, -3};
  probe.boxMax = {3, 4, 3};
  probes = {probe};
  ProbeVolume3D volume;
  volume.boxMin = {-3, 0, -3};
  volume.boxMax = {3, 4, 3};
  volume.count = {4, 3, 4};
  volumes = {volume};
  labels_ = {{kMirror + glm::vec3(-0.5f, -1.15f, 0.9f), "mirror"}, {kMatte + glm::vec3(-0.4f, -1.05f, 0.9f), "rough white"},
             {{-2.9f, 4.4f, 0}, "GI Tier 0 · reflection probe (box projection) · SH probe volume 4x3x4"}};
  return true;
}

std::string ReferenceBoard::CheckGI(Renderer3D& r, const std::vector<uint8_t>& rgba, int32_t w, int32_t h) const {
  std::string failed;
  auto fail = [&](const char* k) { failed += failed.empty() ? k : std::string(",") + k; };
  // 1) the probe's cube faces: +X sees the green wall, -X the red one (orientation of the capture)
  auto faceCentre = [&](int face) {
    std::vector<float> px;
    int size = 0;
    glm::vec3 c{0};
    if (!r.ReadProbeFace(1, face, px, size)) return c;
    for (int y = size / 2 - 4; y < size / 2 + 4; ++y)
      for (int x = size / 2 - 4; x < size / 2 + 4; ++x) c += glm::vec3(px[(y * size + x) * 3], px[(y * size + x) * 3 + 1], px[(y * size + x) * 3 + 2]);
    return c / 64.f;
  };
  glm::vec3 px = faceCentre(0), nx = faceCentre(1);
  bool faces = px.g > px.r * 1.5f && nx.r > nx.g * 1.5f;
  if (!faces) fail("faces");
  // 2) the mirror sphere: its left side reflects the red wall, its right side the green one
  auto at = [&](const glm::vec3& p, int c[3]) {
    float x, y;
    c[0] = c[1] = c[2] = 0;
    if (!ProjectToPixel(camera, w, h, p, x, y) || x < 0 || y < 0 || x >= w || y >= h) return;
    const uint8_t* q = &rgba[((size_t)y * w + (size_t)x) * 4];
    c[0] = q[0]; c[1] = q[1]; c[2] = q[2];
  };
  int ml[3], mr[3];
  at(kMirror + kMirrorR * glm::normalize(glm::vec3(-0.8f, 0, 0.6f)), ml);
  at(kMirror + kMirrorR * glm::normalize(glm::vec3(0.8f, 0, 0.6f)), mr);
  bool mirror = ml[0] > ml[1] + 25 && mr[1] > mr[0] + 15;
  if (!mirror) fail("mirror");
  // 3) colour bleeding on the rough white sphere: redder on the red wall's side, greener on the green wall's side
  int sl[3], sr[3];
  at(kMatte + kMatteR * glm::normalize(glm::vec3(-0.8f, 0, 0.6f)), sl);
  at(kMatte + kMatteR * glm::normalize(glm::vec3(0.8f, 0, 0.6f)), sr);
  bool bleed = (sl[0] - sl[1]) > (sr[0] - sr[1]) + 10;
  if (!bleed) fail("bleed");
  // 4) the probe volume: the floor is redder next to the red wall than next to the green one, in the volume's own
  //    numbers (diffuse light facing up) and in the picture (one probe per object could not do this)
  glm::vec3 vl = r.VolumeIrradiance({-2.5f, 0.05f, 0.5f}, {0, 1, 0}), vr = r.VolumeIrradiance({2.5f, 0.05f, 0.5f}, {0, 1, 0});
  int fl[3], fr[3];
  at({-2.3f, 0, 1.2f}, fl);
  at({2.3f, 0, 1.2f}, fr);
  bool volume = r.VolumeProbes() == 48 && vl.r > vl.g * 1.3f && vr.g > vr.r * 1.3f && (fl[0] - fl[1]) > (fr[0] - fr[1]) + 8;
  if (!volume) fail("volume");
  return std::format("probe_px=({:.2f},{:.2f},{:.2f}) probe_nx=({:.2f},{:.2f},{:.2f}) faces={} mirror={} ({},{},{} | {},{},{}) bleed={} "
                     "volume={} (sh left {:.3f},{:.3f} right {:.3f},{:.3f} | floor {},{},{} | {},{},{}) probes_baked={} volume_probes={} result={}",
                     px.r, px.g, px.b, nx.r, nx.g, nx.b, faces ? "ok" : "bad", mirror ? "ok" : "bad", ml[0], ml[1], ml[2], mr[0], mr[1], mr[2],
                     bleed ? "ok" : "bad", volume ? "ok" : "bad", vl.r, vl.g, vr.r, vr.g, fl[0], fl[1], fl[2], fr[0], fr[1], fr[2],
                     r.BakedProbes(), r.VolumeProbes(), failed.empty() ? "pass" : "FAIL(" + failed + ")");
}

bool ReferenceBoard::InitStress(RHI& rhi, Renderer3D& r, int instances, int textures) {
  rhi_ = &rhi;
  renderer_ = &r;
  plane_ = r.CreateMesh(MakePlaneXZ(1, 1));
  box_ = r.CreateMesh(MakeBox(1, 1, 1));
  quad_ = r.CreateMesh(MakeQuad(1, 1));
  Material3D ground;
  ground.baseColor = {0.2f, 0.2f, 0.21f, 1};
  ground.roughness = 0.9f;
  items_.push_back({&plane_, At({0, 0, 0}, 200, 1, 200), ground});
  int side = std::max(1, (int)std::ceil(std::sqrt((double)instances)));
  for (int i = 0; i < instances; ++i) { // a grid of small cubes, each its own colour and height
    int x = i % side, z = i / side;
    float h = 0.2f + 0.6f * (0.5f + 0.5f * std::sin(x * 0.37f) * std::cos(z * 0.29f));
    Material3D m;
    float hue = (float)(x + z) / (2.f * side);
    m.baseColor = {0.35f + 0.5f * std::fabs(std::sin(hue * 6.28f)), 0.35f + 0.5f * std::fabs(std::sin(hue * 6.28f + 2.1f)),
                   0.35f + 0.5f * std::fabs(std::sin(hue * 6.28f + 4.2f)), 1};
    m.roughness = 0.5f;
    items_.push_back({&box_, At({(x - side * 0.5f) * 0.6f, h * 0.5f, -(float)z * 0.6f}, 0.4f, h, 0.4f), m});
  }
  // 4096 x 4096 BC7 textures, full mip chain: a 256 x 256 tile is encoded once per texture and its blocks repeated
  // (BC blocks are independent, so the repeat is valid texture data); the smallest levels are solid colour.
  for (int t = 0; t < textures; ++t) {
    const int tile = 256, size = 4096;
    std::vector<uint8_t> px((size_t)tile * tile * 4);
    for (int y = 0; y < tile; ++y)
      for (int x = 0; x < tile; ++x) {
        uint8_t* p = &px[((size_t)y * tile + x) * 4];
        bool line = (x % 64) < 3 || (y % 64) < 3;
        p[0] = (uint8_t)(line ? 30 : 60 + (t * 47 + x / 2) % 180);
        p[1] = (uint8_t)(line ? 30 : 60 + (t * 89 + y / 2) % 180);
        p[2] = (uint8_t)(line ? 30 : 60 + (t * 131 + (x + y) / 4) % 180);
        p[3] = 255;
      }
    TextureSettings ts;
    ts.maxSize = tile;
    CookedTexture cooked;
    CookTexture(px.data(), tile, tile, ts, cooked); // BC7 sRGB, 9 levels (256 ... 1)
    std::vector<std::vector<uint8_t>> levels;
    for (int level = 0; (size >> level) >= 1; ++level) {
      int s = size >> level, ts2 = tile >> level, blocks = (s + 3) / 4;
      std::vector<uint8_t> data((size_t)blocks * blocks * 16);
      if (ts2 >= 4) { // repeat the tile's blocks
        int tb = ts2 / 4;
        const std::vector<uint8_t>& src = cooked.mips[level];
        for (int by = 0; by < blocks; ++by)
          for (int bx = 0; bx < blocks; ++bx)
            std::memcpy(&data[((size_t)by * blocks + bx) * 16], &src[((size_t)(by % tb) * tb + (bx % tb)) * 16], 16);
      } else { // average colour, one solid block repeated
        const std::vector<uint8_t>& last = cooked.mips[std::min<size_t>(cooked.mips.size() - 1, 6)];
        for (size_t k = 0; k < data.size(); k += 16) std::memcpy(&data[k], last.data(), 16);
      }
      levels.push_back(std::move(data));
    }
    TextureDesc d;
    d.w = d.h = size;
    d.format = TextureFormat::BC7_SRGB;
    d.repeat = true;
    std::vector<const void*> ptrs;
    std::vector<size_t> sizes;
    for (const auto& l : levels) { ptrs.push_back(l.data()); sizes.push_back(l.size()); }
    stressTextures_.push_back(rhi.CreateTextureMips(d, ptrs, sizes));
    Material3D wall;
    wall.baseMap = stressTextures_.back();
    wall.baseColor = {1, 1, 1, 1};
    wall.roughness = 0.7f;
    int col = t % 8, row = t / 8;
    items_.push_back({&quad_, At({(col - 3.5f) * 4.2f, 2.2f + row * 4.2f, 6.f}, 4.f, 4.f), wall});
  }
  camera.position = {0, 22, 34};
  camera.target = {0, 0, -14};
  camera.fovY = 50.f;
  light.toLight = {-0.4f, 0.8f, 0.45f};
  light.intensity = 2.6f;
  light.shadows = true;
  labels_.push_back({{-side * 0.3f, 0.2f, 1.f}, std::format("{} cubes", instances)});
  if (textures > 0) labels_.push_back({{-16.f, 1.2f, 6.f}, std::format("{} x 4096² BC7", textures)});
  return true;
}

void ReferenceBoard::Shutdown() {
  if (!renderer_) return;
  for (TextureHandle t : stressTextures_) rhi_->DestroyTexture(t);
  stressTextures_.clear();
  for (AssetModel& a : assets_) renderer_->DestroyModel(a.model);
  assets_.clear();
  for (Mesh3D* m : {&plane_, &sphere_, &quad_, &quadCW_, &cylinder_, &cone_, &box_}) renderer_->DestroyMesh(*m);
  rhi_->DestroyTexture(letters_);
  renderer_->DestroyModel(probe_);
  renderer_->DestroyModel(model_);
  items_.clear();
  renderer_ = nullptr;
}

bool ReferenceBoard::AddModel(const std::filesystem::path& path) {
  ImportedModel im;
  std::string err;
  if (!ImportGltf(path, im, &err)) {
    LogError("render-ref: model {} did not import: {}", std::string((const char*)path.u8string().c_str()), err);
    return false;
  }
  renderer_->DestroyModel(model_);
  model_ = renderer_->UploadModel(im);
  glm::vec3 size = model_.boundsMax - model_.boundsMin, center = (model_.boundsMin + model_.boundsMax) * 0.5f;
  float s = 2.2f / std::max({size.x, size.y, size.z, 1e-4f});
  glm::mat4 place = glm::translate(glm::mat4(1.f), {-2.0f, 0.f, 1.6f}) * glm::scale(glm::mat4(1.f), glm::vec3(s)) *
                    glm::translate(glm::mat4(1.f), {-center.x, -model_.boundsMin.y, -center.z});
  Renderer3D::AppendModel(model_, place, items_);
  labels_.push_back({{-3.4f, 2.3f, 1.6f}, std::string((const char*)path.filename().u8string().c_str())});
  return true;
}

std::string ReferenceBoard::ProbeGltf(std::vector<uint8_t>* externalBin) {
  // Buffer: quad positions, normals, uvs, uint16 indices; then an unindexed triangle (positions only).
  const float pos[12] = {-0.5f, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, -0.5f, 0, -0.5f, -0.5f, 0};
  const float nrm[12] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
  const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1}; // uv (0,0) = picture top-left
  const uint16_t idx[6] = {3, 2, 1, 3, 1, 0};   // counter-clockwise seen from +Z
  const float tri[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
  std::vector<uint8_t> bin;
  auto put = [&](const void* p, size_t n) { bin.insert(bin.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
  put(pos, 48); put(nrm, 48); put(uv, 32); put(idx, 12); put(tri, 36);
  // 4x4 picture in 2x2 blocks (a texel centre sits at each quadrant centre, so filtering cannot blur the probe).
  std::vector<uint8_t> px(4 * 4 * 4);
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      const uint8_t* c = kProbeColors[(y / 2) * 2 + x / 2];
      uint8_t* p = &px[(y * 4 + x) * 4];
      p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
    }
  std::vector<uint8_t> png;
  EncodePNG(4, 4, px.data(), false, png);
  std::string bufferUri = externalBin ? "probe%20data.bin" : "data:application/octet-stream;base64," + Base64(bin.data(), bin.size());
  if (externalBin) *externalBin = bin;
  return std::format(R"({{
  "asset": {{"version": "2.0", "generator": "Bidulgi reference board"}},
  "extensionsUsed": ["KHR_materials_unlit"],
  "scene": 0,
  "scenes": [{{"nodes": [0, 2]}}],
  "nodes": [
    {{"name": "probe parent", "translation": [{}, {}, {}], "children": [1]}},
    {{"name": "probe quad", "translation": [{}, {}, {}], "mesh": 0}},
    {{"name": "hidden triangle", "translation": [0, -20, 0], "mesh": 1}}
  ],
  "meshes": [
    {{"primitives": [{{"attributes": {{"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}}, "indices": 3, "material": 0}}]}},
    {{"primitives": [{{"attributes": {{"POSITION": 4}}}}]}}
  ],
  "materials": [{{"name": "probe", "pbrMetallicRoughness": {{"baseColorTexture": {{"index": 0}}, "metallicFactor": 0}},
                 "extensions": {{"KHR_materials_unlit": {{}}}}}}],
  "samplers": [{{"magFilter": 9728, "minFilter": 9986, "wrapS": 33071, "wrapT": 33071}}],
  "textures": [{{"source": 0, "sampler": 0}}],
  "images": [{{"name": "probe colours", "uri": "data:image/png;base64,{}"}}],
  "buffers": [{{"byteLength": {}, "uri": "{}"}}],
  "bufferViews": [
    {{"buffer": 0, "byteOffset": 0, "byteLength": 128}},
    {{"buffer": 0, "byteOffset": 128, "byteLength": 12}},
    {{"buffer": 0, "byteOffset": 140, "byteLength": 36}}
  ],
  "accessors": [
    {{"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-0.5, -0.5, 0], "max": [0.5, 0.5, 0]}},
    {{"bufferView": 0, "byteOffset": 48, "componentType": 5126, "count": 4, "type": "VEC3"}},
    {{"bufferView": 0, "byteOffset": 96, "componentType": 5126, "count": 4, "type": "VEC2"}},
    {{"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"}},
    {{"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]}}
  ]
}})", kProbeParent.x, kProbeParent.y, kProbeParent.z, kProbeChild.x, kProbeChild.y, kProbeChild.z,
                     Base64(png.data(), png.size()), bin.size(), bufferUri);
}

std::string ReferenceBoard::Check(const std::vector<uint8_t>& rgba, int32_t w, int32_t h) const {
  std::string out, failed;
  if (assetMode_) { // stage 2: every model imported, drawn over its slot, and its file facts
    int bg[3] = {(int)std::lround(clear.r * 255), (int)std::lround(clear.g * 255), (int)std::lround(clear.b * 255)};
    for (const AssetModel& a : assets_) {
      float x = 0, y = 0;
      bool drawn = false;
      if (a.ok && ProjectToPixel(camera, w, h, a.center, x, y) && x >= 0 && y >= 0 && x < w && y < h) {
        int covered = 0; // 9 points around the centre: at least 5 must be something other than the background
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            int px = std::clamp((int)x + dx * 12, 0, w - 1), py = std::clamp((int)y + dy * 12, 0, h - 1);
            const uint8_t* q = &rgba[((size_t)py * w + px) * 4];
            covered += std::abs(q[0] - bg[0]) + std::abs(q[1] - bg[1]) + std::abs(q[2] - bg[2]) > 9;
          }
        drawn = covered >= 5;
      }
      out += std::format("{}{} drawn={}", out.empty() ? "" : " ", a.ok ? a.facts : a.name + ":missing", drawn ? 1 : 0);
      if (!drawn) failed += (failed.empty() ? "" : ",") + a.name;
    }
    return out + (failed.empty() ? " result=pass" : " result=FAIL(" + failed + ")");
  }
  auto fail = [&](const char* key) { failed += failed.empty() ? key : std::string(",") + key; };
  auto at = [&](const glm::vec3& p, int c[3]) {
    float x = 0, y = 0;
    c[0] = c[1] = c[2] = -1;
    if (!ProjectToPixel(camera, w, h, p, x, y)) return false;
    int ix = (int)x, iy = (int)y;
    if (ix < 0 || iy < 0 || ix >= w || iy >= h) return false;
    const uint8_t* q = &rgba[((size_t)iy * w + ix) * 4];
    c[0] = q[0]; c[1] = q[1]; c[2] = q[2];
    return true;
  };
  auto close = [](const int c[3], int r, int g, int b, int tol) {
    return std::abs(c[0] - r) <= tol && std::abs(c[1] - g) <= tol && std::abs(c[2] - b) <= tol;
  };
  auto display = [](Color c, int e[3]) { e[0] = LinearToSrgb8(c.r); e[1] = LinearToSrgb8(c.g); e[2] = LinearToSrgb8(c.b); };
  int c[3], e[3];

  at(kGreyCard, c); // 1) gamma: linear 0.18 written through the sRGB curve is 118
  out += std::format("grey={}", c[0]);
  if (!close(c, 118, 118, 118, 1)) fail("grey");

  int worst = 0; // 2) colour round trip: sRGB value -> linear material -> sRGB output
  for (int i = 0; i < 24; ++i) {
    at(ChecherPatchPos(i), c);
    for (int k = 0; k < 3; ++k) worst = std::max(worst, std::abs(c[k] - kChecker[i][k]));
  }
  out += std::format(" checker_maxerr={}", worst);
  if (worst > 2) fail("checker");

  // 3) axes: each arrow head shows its colour where the math puts it
  bool axes = true;
  const Color axisColor[3] = {kAxisX, kAxisY, kAxisZ};
  const glm::vec3 head[3] = {{1.f, 0, 0}, {0, 1.f, 0}, {0, 0, 1.f}};
  for (int a = 0; a < 3; ++a) {
    display(axisColor[a], e);
    axes = at(head[a], c) && close(c, e[0], e[1], e[2], 3) && axes;
  }
  out += std::string(" axes=") + (axes ? "ok" : "bad");
  if (!axes) fail("axes");

  // 4) handedness on screen: +X to the right, +Y up, +Z toward the viewer (below the origin from this camera)
  float ox, oy, xx, xy, yx, yy, zx, zy;
  ProjectToPixel(camera, w, h, {0, 0, 0}, ox, oy);
  ProjectToPixel(camera, w, h, head[0], xx, xy);
  ProjectToPixel(camera, w, h, head[1], yx, yy);
  ProjectToPixel(camera, w, h, head[2], zx, zy);
  bool right = xx > ox + 10 && yy > oy + 10 && zy < oy - 5;
  out += std::string(" handed=") + (right ? "right" : "WRONG");
  if (!right) fail("handed");

  // 5) winding: the counter-clockwise quad is drawn, the clockwise one is culled (background shows)
  display(kOrange, e);
  int bg[3] = {(int)std::lround(clear.r * 255), (int)std::lround(clear.g * 255), (int)std::lround(clear.b * 255)};
  bool ccw = at(kCCW, c) && close(c, e[0], e[1], e[2], 2);
  bool cwCulled = at(kCW, c) && close(c, bg[0], bg[1], bg[2], 2);
  out += std::string(" winding=") + (ccw && cwCulled ? "ccw" : ccw ? "cw-not-culled" : "ccw-missing");
  if (!ccw || !cwCulled) fail("winding");

  // 6) depth: overlap keeps the nearer (earlier) quad, the far quad still shows beside it
  display(kYellow, e);
  bool nearWins = at({4.4f, 4.4f, 0.8f}, c) && close(c, e[0], e[1], e[2], 2);
  display(kMagenta, e);
  bool farShown = at({4.8f, 4.4f, 0.5f}, c) && close(c, e[0], e[1], e[2], 2);
  out += std::string(" depth=") + (nearWins && farShown ? "ok" : "bad");
  if (!nearWins || !farShown) fail("depth");

  // 7) light from above: the upper half of a rough dielectric sphere is brighter than the lower half
  glm::vec3 s = SpherePos(3, 0);
  int top[3], bottom[3];
  at(s + kSphereR * glm::normalize(glm::vec3(0, 0.6f, 0.8f)), top);
  at(s + kSphereR * glm::normalize(glm::vec3(0, -0.6f, 0.8f)), bottom);
  int lt = top[0] + top[1] + top[2], lb = bottom[0] + bottom[1] + bottom[2];
  out += std::format(" light={}", lt > lb + 30 ? "top" : "flat");
  if (lt <= lb + 30) fail("light");

  // 8) UV: picture row 0 is the top of the board, column 0 the left
  float in = kLettersSize * 0.5f - kLettersSize * (kMark * 0.5f / kTex);
  bool tl = at(kLetters + glm::vec3(-in, in, 0), c) && close(c, kTL[0], kTL[1], kTL[2], 3);
  bool tr = at(kLetters + glm::vec3(in, in, 0), c) && close(c, kTR[0], kTR[1], kTR[2], 3);
  bool bl = at(kLetters + glm::vec3(-in, -in, 0), c) && close(c, kBL[0], kBL[1], kBL[2], 3);
  bool br = at(kLetters + glm::vec3(in, -in, 0), c) && close(c, kBR[0], kBR[1], kBR[2], 3);
  out += std::string(" uv=") + (tl && tr && bl && br ? "ok" : "bad");
  if (!(tl && tr && bl && br)) fail("uv");

  // 10) sun shadow: the ground under the floating box (along the light) is darker than open ground beside it
  auto lum = [](const int q[3]) { return q[0] + q[1] + q[2]; };
  glm::vec3 toL = light.toLight;
  glm::vec3 shadowSpot = kCaster - toL * (kCaster.y / toL.y);
  int sh[3], lit[3];
  bool shadowOk = at(shadowSpot, sh) && at(kLitGround, lit) && lum(sh) * 4 < lum(lit) * 3; // sky light still reaches the shadow
  out += shadowOk ? std::string(" shadow=ok") : std::format(" shadow=none(in={},open={})", lum(sh), lum(lit));
  if (!shadowOk) fail("shadow");
  // 11) point light: the ground right under it is redder than ground out of its range
  int under[3], far[3];
  bool pointOk = at({kPointLight.x, 0, kPointLight.z}, under) && at(kPointFar, far) &&
                 (under[0] - under[1]) > (far[0] - far[1]) + 25;
  out += std::format(" point={}", pointOk ? "ok" : "none");
  if (!pointOk) fail("point");
  // 12) spot light: bright inside its cone on the ground, not outside
  int inCone[3], outCone[3];
  bool spotOk = at({kSpotLight.x, 0, kSpotLight.z}, inCone) && at(kSpotOut, outCone) && lum(inCone) > lum(outCone) + 40;
  out += std::format(" spot={}", spotOk ? "ok" : "none");
  if (!spotOk) fail("spot");

  // 9) glTF probe: hierarchy transform, accessors, embedded PNG and UV orientation through the importer
  bool gltf = probeLoaded_;
  const glm::vec3 probe = kProbeParent + kProbeChild;
  const glm::vec3 quadrant[4] = {{-0.25f, 0.25f, 0}, {0.25f, 0.25f, 0}, {-0.25f, -0.25f, 0}, {0.25f, -0.25f, 0}};
  for (int q = 0; q < 4 && gltf; ++q)
    gltf = at(probe + quadrant[q], c) && close(c, kProbeColors[q][0], kProbeColors[q][1], kProbeColors[q][2], 3);
  out += std::string(" gltf=") + (gltf ? "ok" : "bad");
  if (!gltf) fail("gltf");

  out += failed.empty() ? " result=pass" : " result=FAIL(" + failed + ")";
  return out;
}
}
