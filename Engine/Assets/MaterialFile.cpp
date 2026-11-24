#include "Assets/MaterialFile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <set>
#include <yyjson.h>

namespace bdg {
namespace {
const char* kKnown[] = {"BaseColor", "BaseColorMap", "Metallic", "Roughness", "ORMMap", "NormalMap", "NormalStrength",
                        "AOStrength", "Emissive", "EmissiveStrength", "EmissiveMap", "Opacity"};

float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float LinearToSrgb(float c) {
  c = std::clamp(c, 0.f, 1.f);
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

std::string Quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

// Reads "value" for a parameter of a known or given type. false = bad value.
bool ReadValue(yyjson_val* v, MaterialParam::Type type, MaterialParam& p) {
  p.type = type;
  switch (type) {
    case MaterialParam::Type::Float:
      if (!yyjson_is_num(v)) return false;
      p.f = (float)yyjson_get_num(v);
      return true;
    case MaterialParam::Type::Bool:
      if (!yyjson_is_bool(v)) return false;
      p.f = yyjson_get_bool(v) ? 1.f : 0.f;
      return true;
    case MaterialParam::Type::Texture:
      if (!yyjson_is_str(v)) return false;
      p.texture = yyjson_get_str(v);
      return true;
    case MaterialParam::Type::Color: {
      if (const char* s = yyjson_get_str(v)) {
        bool ok = false;
        p.c = ParseColorValue(s, ok);
        return ok;
      }
      if (!yyjson_is_arr(v) || yyjson_arr_size(v) < 3) return false;
      float c[4] = {0, 0, 0, 1};
      for (size_t i = 0; i < std::min<size_t>(4, yyjson_arr_size(v)); ++i) {
        yyjson_val* e = yyjson_arr_get(v, i);
        if (!yyjson_is_num(e)) return false;
        c[i] = (float)yyjson_get_num(e);
      }
      p.c = {c[0], c[1], c[2], c[3]};
      return true;
    }
  }
  return false;
}

bool TypeFromString(std::string_view s, MaterialParam::Type& t) {
  if (s == "float" || s == "scalar") { t = MaterialParam::Type::Float; return true; }
  if (s == "color" || s == "vector") { t = MaterialParam::Type::Color; return true; }
  if (s == "texture") { t = MaterialParam::Type::Texture; return true; }
  if (s == "bool" || s == "switch") { t = MaterialParam::Type::Bool; return true; }
  return false;
}

// Type of an instance override with no declaration in sight (from the value itself).
MaterialParam::Type GuessType(yyjson_val* v) {
  if (yyjson_is_bool(v)) return MaterialParam::Type::Bool;
  if (yyjson_is_num(v)) return MaterialParam::Type::Float;
  if (yyjson_is_arr(v)) return MaterialParam::Type::Color;
  const char* s = yyjson_get_str(v);
  return s && s[0] == '#' ? MaterialParam::Type::Color : MaterialParam::Type::Texture;
}
}

bool IsKnownMaterialParam(const std::string& name) {
  return std::find_if(std::begin(kKnown), std::end(kKnown), [&](const char* k) { return name == k; }) != std::end(kKnown);
}

Color ParseColorValue(std::string_view s, bool& ok) {
  ok = false;
  if (s.size() != 7 && s.size() != 9) return {1, 1, 1, 1};
  if (s[0] != '#') return {1, 1, 1, 1};
  unsigned v[4] = {0, 0, 0, 255};
  for (size_t i = 0; i < (s.size() - 1) / 2; ++i) {
    unsigned x = 0;
    for (char ch : s.substr(1 + i * 2, 2)) {
      int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
      if (d < 0) return {1, 1, 1, 1};
      x = x * 16 + (unsigned)d;
    }
    v[i] = x;
  }
  ok = true;
  return {SrgbToLinear(v[0] / 255.f), SrgbToLinear(v[1] / 255.f), SrgbToLinear(v[2] / 255.f), v[3] / 255.f}; // alpha stays linear
}

bool ParseMaterial(std::string_view text, MaterialDef& out, std::string* error) {
  out = {};
  bool header = false;
  size_t lineNo = 0;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    std::string_view line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
    at = end == std::string_view::npos ? text.size() : end + 1;
    ++lineNo;
    if (line.find_first_not_of(" \t\r") == std::string_view::npos) continue;
    yyjson_doc* d = yyjson_read(line.data(), line.size(), YYJSON_READ_ALLOW_COMMENTS);
    yyjson_val* o = d ? yyjson_doc_get_root(d) : nullptr;
    if (!yyjson_is_obj(o)) {
      yyjson_doc_free(d);
      if (error) *error = std::format("line {}: not a JSON object", lineNo);
      return false;
    }
    if (const char* m = yyjson_get_str(yyjson_obj_get(o, "material"))) {
      header = true;
      out.name = m;
      if (const char* s = yyjson_get_str(yyjson_obj_get(o, "shading"))) out.shading = s;
      if (const char* b = yyjson_get_str(yyjson_obj_get(o, "blend"))) out.blend = b;
      if (yyjson_val* t = yyjson_obj_get(o, "twoSided"); yyjson_is_bool(t)) out.twoSided = yyjson_get_bool(t);
      if (yyjson_val* c = yyjson_obj_get(o, "cutoff"); yyjson_is_num(c)) out.cutoff = (float)yyjson_get_num(c);
      if (out.shading != "lit" && out.shading != "unlit") out.warnings.push_back("unknown shading \"" + out.shading + "\" (lit)");
      if (out.blend != "opaque" && out.blend != "masked" && out.blend != "translucent")
        out.warnings.push_back("unknown blend \"" + out.blend + "\" (opaque)");
    } else if (const char* inst = yyjson_get_str(yyjson_obj_get(o, "instance"))) {
      header = true;
      out.instance = true;
      out.name = inst;
      if (const char* p = yyjson_get_str(yyjson_obj_get(o, "parent"))) out.parent = p;
    } else if (const char* name = yyjson_get_str(yyjson_obj_get(o, "param"))) {
      MaterialParam p;
      const char* type = yyjson_get_str(yyjson_obj_get(o, "type"));
      MaterialParam::Type t;
      if (!type || !TypeFromString(type, t)) out.warnings.push_back(std::format("line {}: {} has no known type", lineNo, name));
      else if (!ReadValue(yyjson_obj_get(o, "value"), t, p)) out.warnings.push_back(std::format("line {}: bad value for {}", lineNo, name));
      else {
        out.params[name] = p;
        if (!IsKnownMaterialParam(name)) out.warnings.push_back(std::format("{} is not used by the standard shader yet", name));
      }
    } else if (const char* set = yyjson_get_str(yyjson_obj_get(o, "set"))) {
      MaterialParam p;
      yyjson_val* v = yyjson_obj_get(o, "value");
      if (ReadValue(v, GuessType(v), p)) out.params[set] = p;
      else out.warnings.push_back(std::format("line {}: bad value for {}", lineNo, set));
    } else {
      out.warnings.push_back(std::format("line {}: ignored (node graphs come with M5)", lineNo));
    }
    yyjson_doc_free(d);
  }
  if (!header) {
    if (error) *error = "no {\"material\":...} or {\"instance\":...} line";
    return false;
  }
  if (out.instance && out.parent.empty()) {
    if (error) *error = "an instance needs a parent";
    return false;
  }
  return true;
}

std::string SaveMaterial(const MaterialDef& m) {
  std::string s = m.instance ? std::format("{{\"instance\":{},\"parent\":{}}}\n", Quote(m.name), Quote(m.parent))
                             : std::format("{{\"material\":{},\"shading\":{},\"blend\":{},\"twoSided\":{},\"cutoff\":{}}}\n", Quote(m.name),
                                           Quote(m.shading), Quote(m.blend), m.twoSided, m.cutoff);
  for (const auto& [name, p] : m.params) {
    std::string value, type;
    switch (p.type) {
      case MaterialParam::Type::Float: value = std::format("{}", p.f); type = "float"; break;
      case MaterialParam::Type::Bool: value = p.f != 0 ? "true" : "false"; type = "bool"; break;
      case MaterialParam::Type::Texture: value = Quote(p.texture); type = "texture"; break;
      case MaterialParam::Type::Color: {
        auto hex = [](float v) { return (int)std::lround(LinearToSrgb(v) * 255.f); };
        value = p.c.a >= 1.f ? std::format("\"#{:02x}{:02x}{:02x}\"", hex(p.c.r), hex(p.c.g), hex(p.c.b))
                             : std::format("\"#{:02x}{:02x}{:02x}{:02x}\"", hex(p.c.r), hex(p.c.g), hex(p.c.b), (int)std::lround(p.c.a * 255.f));
        type = "color";
        break;
      }
    }
    s += m.instance ? std::format("{{\"set\":{},\"value\":{}}}\n", Quote(name), value)
                    : std::format("{{\"param\":{},\"type\":\"{}\",\"value\":{}}}\n", Quote(name), type, value);
  }
  return s;
}

bool ResolveMaterial(const MaterialDef& m, const std::function<const MaterialDef*(const std::string&)>& lookup, MaterialDef& resolved,
                     std::string* error) {
  std::vector<const MaterialDef*> chain{&m};
  std::set<std::string> seen{m.name};
  while (chain.back()->instance) {
    const MaterialDef* parent = lookup ? lookup(chain.back()->parent) : nullptr;
    if (!parent) {
      if (error) *error = std::format("{}: parent \"{}\" not found", chain.back()->name, chain.back()->parent);
      return false;
    }
    if (!seen.insert(parent->name).second || chain.size() > 32) {
      if (error) *error = std::format("{}: parent loop through \"{}\"", m.name, parent->name);
      return false;
    }
    chain.push_back(parent);
  }
  resolved = *chain.back(); // the root material, then each instance's overrides down to `m`
  for (auto it = chain.rbegin() + 1; it != chain.rend(); ++it)
    for (const auto& [name, p] : (*it)->params) {
      auto base = resolved.params.find(name);
      if (base == resolved.params.end()) {
        resolved.warnings.push_back(std::format("{} sets {}, which {} does not have", (*it)->name, name, resolved.name));
        continue;
      }
      MaterialParam v = p;
      if (base->second.type == MaterialParam::Type::Texture && v.type != MaterialParam::Type::Texture) continue;
      if (base->second.type == MaterialParam::Type::Color && v.type == MaterialParam::Type::Float) { v.c = {v.f, v.f, v.f, 1}; }
      v.type = base->second.type;
      base->second = v;
    }
  resolved.name = m.name;
  resolved.instance = false;
  resolved.parent.clear();
  return true;
}
}
