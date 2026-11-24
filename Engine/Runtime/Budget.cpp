#include "Runtime/Budget.h"
#include <yyjson.h>
#include <algorithm>
#include <format>

namespace bdg {

const char* PlatformName(int platform) {
  static const char* names[kBudgetPlatforms] = {"Windows", "Android", "iOS", "Web"};
  return names[std::clamp(platform, 0, kBudgetPlatforms - 1)];
}

BudgetLimits DefaultBudget(int platform) {
  switch (std::clamp(platform, 0, kBudgetPlatforms - 1)) {
    case 1: return {200, 1.0e6, 512, 8, 2};   // Android (mid-range phone)
    case 2: return {400, 1.5e6, 768, 8, 2};   // iOS
    case 3: return {300, 1.0e6, 512, 8, 1};   // Web (WebGL2)
    default: return {2000, 3.0e6, 2048, 16, 4}; // Windows (mid-range PC)
  }
}

BudgetLimits BudgetForTier(const BudgetLimits& m, QualityTier tier) {
  const double k = tier == QualityTier::Potato ? 0.25 : tier == QualityTier::Low ? 0.5 : tier == QualityTier::High ? 2.0
                 : tier == QualityTier::Ultra ? 3.0 : 1.0;
  // lights and shadows follow the tier's own limits more than the device: scale them gently
  const double kl = tier == QualityTier::Potato ? 0.0 : tier == QualityTier::Low ? 0.5 : tier == QualityTier::Medium ? 1.0 : 2.0;
  return {m.draws * k, m.triangles * k, m.textureMB * std::min(k, 2.0), m.lights * kl, m.shadowLights * std::min(kl, 1.0)};
}

bool ParseBudgets(std::string_view json, std::array<BudgetLimits, kBudgetPlatforms>& medium, std::string* error) {
  auto fail = [&](std::string why) { if (error) *error = std::move(why); return false; };
  yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
  if (!doc) return fail("not JSON");
  yyjson_val* root = yyjson_doc_get_root(doc);
  bool ok = yyjson_is_obj(root);
  std::string why = ok ? "" : "expected an object of platforms";
  size_t idx, max;
  yyjson_val *k, *v;
  if (ok)
    yyjson_obj_foreach(root, idx, max, k, v) {
      const std::string name = yyjson_get_str(k);
      int p = -1;
      for (int i = 0; i < kBudgetPlatforms; ++i)
        if (name == PlatformName(i)) p = i;
      if (p < 0 || !yyjson_is_obj(v)) { ok = false; why = std::format("unknown platform \"{}\"", name); break; }
      size_t i2, m2;
      yyjson_val *k2, *v2;
      yyjson_obj_foreach(v, i2, m2, k2, v2) {
        const std::string key = yyjson_get_str(k2);
        if (!yyjson_is_num(v2)) { ok = false; why = std::format("{}.{} is not a number", name, key); break; }
        const double x = yyjson_get_num(v2);
        BudgetLimits& b = medium[p];
        if (key == "draws") b.draws = x;
        else if (key == "triangles") b.triangles = x;
        else if (key == "textureMB") b.textureMB = x;
        else if (key == "lights") b.lights = x;
        else if (key == "shadowLights") b.shadowLights = x;
        else { ok = false; why = std::format("{}: unknown key \"{}\"", name, key); break; }
      }
      if (!ok) break;
    }
  yyjson_doc_free(doc);
  return ok ? true : fail(why);
}

std::string FormatCount(double v) {
  if (v >= 1e6) return std::format("{:.2g}M", v / 1e6);
  if (v >= 1e4) return std::format("{:.3g}k", v / 1e3);
  return std::format("{:.0f}", v);
}

std::vector<BudgetBar> EvaluateBudget(const BudgetUse& u, const BudgetLimits& l) {
  std::vector<BudgetBar> out;
  auto add = [&](const char* key, const char* label, double used, double limit, std::string text) {
    BudgetBar b;
    b.key = key;
    b.label = label;
    b.used = used;
    b.limit = limit;
    b.fraction = limit > 0 ? (float)(used / limit) : (used > 0 ? 2.f : 0.f);
    b.level = b.fraction > 1.f ? 2 : b.fraction >= 0.8f ? 1 : 0;
    b.text = std::move(text);
    out.push_back(std::move(b));
  };
  add("draws", "Draw", u.draws, l.draws, std::format("{} / {}", FormatCount(u.draws), FormatCount(l.draws)));
  add("triangles", "Tris", u.triangles, l.triangles, std::format("{} / {}", FormatCount(u.triangles), FormatCount(l.triangles)));
  add("textures", "텍스처", u.textureMB, l.textureMB, std::format("{:.0f} / {:.0f} MB", u.textureMB, l.textureMB));
  add("lights", "라이트", u.lights, l.lights,
      std::format("{:.0f} / {:.0f} (그림자 {:.0f} / {:.0f})", u.lights, l.lights, u.shadowLights, l.shadowLights));
  if (u.shadowLights > l.shadowLights && out.back().level < 2) out.back().level = 2; // too many shadowed lights
  return out;
}

} // namespace bdg
