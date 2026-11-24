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
//   ultra    per pixel      16      1 map, 2048         full      + bloom                   4x    1.25
// high and ultra get their own features later (CSM, SSAO, Forward+, GI tiers); until then they differ in
// view distance only. Tiers are keyed potato / low / medium / high / ultra in files and on the command line.
#include <string>
#include <string_view>

namespace bdg {
enum class QualityTier { Potato, Low, Medium, High, Ultra };
enum class LightingMode { Unlit, Vertex, Pixel };

struct QualitySettings {
  QualityTier tier = QualityTier::Medium;
  LightingMode lighting = LightingMode::Pixel;
  int maxLights = 8;           // point + spot
  bool shadows = true;
  int shadowMapSize = 1024;
  float textureBias = 1;       // mip bias: 1 = half resolution, 2 = quarter
  bool toneMap = true;         // ACES; false = clamp (the reference board keeps exact colours)
  float exposure = 1;
  bool fxaa = true;
  bool bloom = false;
  float bloomIntensity = 0.2f;    // share of the blurred image added back (glow around bright light)
  int msaa = 1;
  bool instancing = true;      // objects sharing mesh, pipeline and textures go out in one draw call
  bool culling = true;         // skip objects outside the camera (and, for shadows, the light's) frustum
  float viewDistance = 0.75f;  // multiplies far plane and shadow distance
};

QualitySettings QualityPreset(QualityTier t);
// Exact output for test boards: per-pixel lighting, shadows, full textures, no tone mapping or post, 4x MSAA.
QualitySettings ReferenceQuality();
const char* TierKey(QualityTier t);   // "potato", "low", ...
const char* TierLabel(QualityTier t); // "감자", "저사양", "중사양", "고사양", "초고사양"
bool TierFromString(std::string_view s, QualityTier& out);

// Project rule: the game supports tiers min..max and starts at `fallback` (medium unless set).
// ProjectSettings/Quality.json: {"min":"low","max":"high","default":"medium"}
struct ProjectQuality {
  QualityTier min = QualityTier::Potato, max = QualityTier::Ultra, fallback = QualityTier::Medium;
  QualityTier Clamp(QualityTier t) const;
  QualityTier Next(QualityTier t) const; // the next tier up, wrapping from max back to min
};
bool ParseProjectQuality(std::string_view json, ProjectQuality& out, std::string* error = nullptr);
// "msaa=1,bloom=0,fxaa=1,shadows=0,tonemap=0,lights=4,bias=0,exposure=1.2,shadowmap=2048": changes on top of a
// tier (BDG_QUALITY_SET, experiments). false names the first unknown key.
bool ApplyQualityOverrides(std::string_view list, QualitySettings& q, std::string* error = nullptr);

// Same curve as the tone-map pass (ACES fit by Narkowicz), for tests and colour pickers. Input linear, output 0..1.
float ToneMapACES(float x);
}
