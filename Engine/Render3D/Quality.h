#pragma once
// Quality tiers (queue 3-5, Docs/architecture/rendering-and-platforms.md): one setting picks a whole group of
// renderer settings, from potato (no lighting) to ultra. The same scene renders at every tier; what a tier
// cannot afford is switched off or replaced.
//
//            lighting       lights  shadows             textures  post                      MSAA  view
//   potato   none (unlit)   0       none                <= 1/4    none                      1x    0.5
//   low      per vertex     4       none (blob later)   1/4       none                      1x    0.5
//   medium   per pixel      8       1 map, 1024         1/2       ACES tone map, FXAA       1x    0.75
//   high     per pixel      16      1 map, 2048         full      + bloom                   4x    1.0
//   ultra    per pixel      16      1 map, 2048         full      + bloom                   TAA   1.25
// Point / spot shadows (3-4b): potato and low none, medium 1 light (512 tiles), high 4 (512), ultra 8 (1024).
// high and ultra get their own features later (CSM, SSAO, Forward+, GI tiers); until then they differ in
// view distance only. Tiers are keyed potato / low / medium / high / ultra in files and on the command line.
#include <string>
#include <string_view>

namespace bdg {
enum class QualityTier { Potato, Low, Medium, High, Ultra };
enum class LightingMode { Unlit, Vertex, Pixel };

// Texture groups (3-5c, Unreal's texture LOD groups / Unity's mipmap limits): each tier caps the largest side a
// texture of a group is uploaded with. The cooked DDS keeps every mip level, so loading simply starts at the first
// level that fits: switching tiers needs no re-cook. The groups follow the texture presets (Assets/TextureCooker.h).
enum class TextureGroup { World, Normal, Masks, Grayscale, UI };
constexpr int kTextureGroups = 5;
const char* TextureGroupKey(TextureGroup g); // "world", "normal", "masks", "grayscale", "ui"
// First mip level (of `levels`) whose larger side is <= maxSide; the last level when none is that small.
int FirstMipWithin(int w, int h, int levels, int maxSide);

struct QualitySettings {
  QualityTier tier = QualityTier::Medium;
  LightingMode lighting = LightingMode::Pixel;
  int maxLights = 8;           // point + spot
  bool shadows = true;
  int shadowMapSize = 1024;   // per cascade
  int shadowCascades = 1;     // 1..4 (3-5b): more = sharp near the camera and shadows further away
  float shadowSoftness = 0;   // penumbra radius in metres (0 = the plain 3x3 filter)
  // Point and spot light shadows (3-4b): how many local lights may cast shadows at once (nearest to the camera
  // first) and the size of one tile of their shadow atlas. A spot light takes one tile, a point light six (cube faces).
  int localShadows = 1;       // medium 1, high 4, ultra 8; potato / low 0
  int localShadowSize = 512;  // pixels per atlas tile (halved while the lights' tiles do not fit a 4096 x 4096 atlas)
  bool clustered = false;     // Forward+ (3-5b): each pixel lights itself with its screen cluster's lights only
  int anisotropy = 1;         // texture filtering at grazing angles (3-5b): 1 off .. 16
  bool blobShadows = false;   // a soft dark spot under each object where there are no shadow maps (3-5b, low)
  bool ssao = false;          // screen-space ambient occlusion on indirect light (3-5b)
  float ssaoRadius = 0.5f;    // metres
  float ssaoIntensity = 1.5f; // exponent on the occlusion
  float textureBias = 1;       // mip bias: 1 = half resolution, 2 = quarter
  bool toneMap = true;         // ACES; false = clamp (the reference board keeps exact colours)
  float exposure = 1;
  bool fxaa = true;
  // Temporal anti-aliasing (round 6): the projection moves by a sub-pixel each frame (Halton 2, 3), each pixel blends
  // with its reprojected history (motion vectors of objects and camera; the history clamped to the pixel's
  // neighbourhood so moving things do not smear), then a light sharpen. Replaces MSAA and FXAA where it is on.
  bool taa = false;
  float taaSharpen = 0.2f;     // 0 = off .. 1 = strong
  float taaFeedback = 0.9f;    // share of the history kept each frame
  // Screen-space reflections (round 6): smooth surfaces mirror what is on screen (traced against the depth prepass,
  // coloured from the previous frame), fading with roughness, at the edges and with distance into the probe / sky
  // reflection. High and ultra.
  bool ssr = false;
  float ssrDistance = 20.f;    // metres a reflected ray travels
  int ssrSteps = 48;
  // Volumetric fog (round 6): a froxel grid (screen tiles x depth slices) lit by the sun with shadows and by point /
  // spot lights, integrated front to back; where a post volume sets fog density. High and ultra.
  bool volumetricFog = false;
  int fogGridX = 64, fogGridY = 36, fogSlices = 48;
  // GI Tier 1 (round 6, Render3D/DynamicGI.h): a light probe volume is traced again while running, a share of probes
  // per frame (rays against the scene's triangles), with per-probe visibility against light leaks. High and ultra.
  bool dynamicGI = false;
  bool giVisibility = true;
  int giRays = 64, giProbesPerFrame = 24;
  float giHysteresis = 0.5f;
  bool bloom = false;
  float bloomIntensity = 0.2f;    // share of the blurred image added back (glow around bright light)
  int msaa = 1;
  bool instancing = true;      // objects sharing mesh, pipeline and textures go out in one draw call
  bool culling = true;         // skip objects outside the camera (and, for shadows, the light's) frustum
  float viewDistance = 0.75f;  // multiplies far plane and shadow distance
  // Largest texture side per TextureGroup (3-5c): world colour, normal, masks, grayscale, UI (UI is never reduced).
  int textureMax[kTextureGroups] = {2048, 1024, 1024, 1024, 16384};
  int TextureMax(TextureGroup g) const { return textureMax[(int)g]; }
};

QualitySettings QualityPreset(QualityTier t);
// Exact output for test boards: per-pixel lighting, shadows, full textures, no tone mapping or post, 4x MSAA.
QualitySettings ReferenceQuality();
const char* TierKey(QualityTier t);   // "potato", "low", ...
const char* TierLabel(QualityTier t); // "최소", "낮음", "보통", "높음", "최고" (TierFromString also reads the old "감자" .. "초고사양")
const char* TierHint(QualityTier t);  // one line for the tooltip: what is drawn, the hardware it is meant for
bool TierFromString(std::string_view s, QualityTier& out);

// Project rule: the game supports tiers min..max and starts at `fallback` (medium unless set).
// ProjectSettings/Quality.json: {"min":"low","max":"high","default":"medium"} ("default":"auto" = benchmark, 3-5c)
struct ProjectQuality {
  QualityTier min = QualityTier::Potato, max = QualityTier::Ultra, fallback = QualityTier::Medium;
  bool autoTier = false; // "default":"auto" (3-5c): measure the device, then clamp to min..max
  QualityTier Clamp(QualityTier t) const;
  QualityTier Next(QualityTier t) const; // the next tier up, wrapping from max back to min
};
bool ParseProjectQuality(std::string_view json, ProjectQuality& out, std::string* error = nullptr);
// "msaa=1,bloom=0,fxaa=1,shadows=0,tonemap=0,lights=4,bias=0,exposure=1.2,shadowmap=2048": changes on top of a
// tier (BDG_QUALITY_SET, experiments). false names the first unknown key.
bool ApplyQualityOverrides(std::string_view list, QualitySettings& q, std::string* error = nullptr);

// Automatic tier (3-5c). The benchmark renders a fixed scene at high quality, 1920x1080, and times the frames.
// TierFromBenchmark picks the most detailed tier whose expected frame time (relative cost per tier x the measured
// high time) stays within 60 % of the frame budget, leaving the rest to the game. "auto" in Quality.json or
// BDG_QUALITY asks for it; the result is cached per GPU in .cache/QualityBenchmark.json.
float TierCost(QualityTier t); // frame time relative to high: potato 0.15, low 0.25, medium 0.45, high 1, ultra 1.6
QualityTier TierFromBenchmark(double highMs, double budgetMs = 1000.0 / 60.0);
std::string BenchmarkCacheJson(const std::string& device, double highMs, QualityTier tier);
bool ParseBenchmarkCache(std::string_view json, const std::string& device, double& highMs, QualityTier& tier);

// Frame drop guard (3-5c): fed with the time of each rendered frame; when the frames of the last `window` seconds
// average more than `over` x the budget (and there were at least `minFrames`), it asks for one tier down, then
// waits `cooldown` seconds of new frames before judging again. It never goes up by itself.
class QualityGovernor {
public:
  double budgetMs = 1000.0 / 60.0, over = 1.2, window = 2.0, cooldown = 4.0;
  int minFrames = 30;
  bool Feed(double nowSeconds, double frameMs); // true: step down one tier now
  void Reset();
  double AverageMs() const;
  int Frames() const { return count_; }
  double lastStepAverageMs = 0; // the average that made the last step down

private:
  static constexpr int kMax = 1024;
  double t_[kMax] = {}, ms_[kMax] = {};
  int head_ = 0, count_ = 0;
  double quietUntil_ = -1e30;
};

// Same curve as the tone-map pass (ACES fit by Narkowicz), for tests and colour pickers. Input linear, output 0..1.
float ToneMapACES(float x);
}
