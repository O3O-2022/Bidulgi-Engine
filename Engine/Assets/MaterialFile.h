#pragma once
// Material files (queue 3-1b, Docs/features/materials-and-textures.md): JSON lines, diffable and AI-editable.
// Until the node graph arrives (M5) a material is the standard PBR shader with fixed parameters.
//
// M_Rock.material
//   {"material":"M_Rock","shading":"lit","blend":"opaque","twoSided":false,"cutoff":0.5}
//   {"param":"BaseColor","type":"color","value":"#b8b2a6"}          sRGB hex, or [r,g,b(,a)] linear
//   {"param":"Roughness","type":"float","value":0.8,"range":[0,1]}
//   {"param":"BaseColorMap","type":"texture","value":"Textures/Rock_D.png"}   asset path or "guid:<guid>"
// MI_Rock_Wet.matinst
//   {"instance":"MI_Rock_Wet","parent":"M_Rock"}                    parent: name, asset path or guid
//   {"set":"Roughness","value":0.25}
//
// Parameters the shader reads: BaseColor, BaseColorMap, Metallic, Roughness, ORMMap (R AO, G roughness,
// B metallic), NormalMap, NormalStrength, AOStrength, Emissive, EmissiveStrength, EmissiveMap, Opacity.
// Other parameters are kept (the graph will use them) and listed as unused. shading: lit | unlit;
// blend: opaque | masked | translucent.
#include "Core/Math.h"
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
struct MaterialParam {
  enum class Type { Float, Color, Texture, Bool } type = Type::Float;
  float f = 0;
  Color c{1, 1, 1, 1}; // linear
  std::string texture; // asset path or "guid:..."
};

struct MaterialDef {
  std::string name;
  bool instance = false;
  std::string parent;                               // instances
  std::string shading = "lit", blend = "opaque";
  bool twoSided = false;
  float cutoff = 0.5f;
  std::map<std::string, MaterialParam> params;      // material: declared parameters; instance: overrides
  std::vector<std::string> warnings;                // unknown lines, bad values (the file still loads)
};

bool ParseMaterial(std::string_view text, MaterialDef& out, std::string* error = nullptr);
std::string SaveMaterial(const MaterialDef& m);     // the same JSON lines (instances write "set" lines)
// Follows parent links (instance of instance of material) and applies the overrides from the root down.
// lookup: parent reference -> its definition (nullptr: missing). Fails on a missing parent or a loop.
bool ResolveMaterial(const MaterialDef& m, const std::function<const MaterialDef*(const std::string&)>& lookup,
                     MaterialDef& resolved, std::string* error = nullptr);
bool IsKnownMaterialParam(const std::string& name);
Color ParseColorValue(std::string_view hexOrEmpty, bool& ok); // "#rrggbb" / "#rrggbbaa" (sRGB) -> linear
}
