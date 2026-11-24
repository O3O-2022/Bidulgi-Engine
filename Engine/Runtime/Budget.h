#pragma once
// Platform budgets while editing (queue 3-7, rendering-and-platforms.md "편집 중 플랫폼 예산 표시"): what the
// scene view draws now, against what the target platform affords at the chosen quality tier. Math only (no
// renderer, no UI) so the rules are unit-testable; the scene view fills BudgetUse from its last render.
//
// Budgets are given per platform at the medium tier and scale with the tier (potato 0.25x ... ultra 3x).
// ProjectSettings/Budgets.json overrides them: {"Android": {"draws": 250, "triangles": 1200000, "textureMB": 512,
// "lights": 8, "shadowLights": 2}, ...} (keys may be left out; platform names as PlatformName).
#include "Render3D/Quality.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {

constexpr int kBudgetPlatforms = 4;
const char* PlatformName(int platform); // "Windows", "Android", "iOS", "Web"

struct BudgetLimits {
  double draws = 0, triangles = 0, textureMB = 0, lights = 0, shadowLights = 0;
};
using BudgetUse = BudgetLimits; // the same quantities, measured

BudgetLimits DefaultBudget(int platform);                                  // medium tier
BudgetLimits BudgetForTier(const BudgetLimits& medium, QualityTier tier);  // scaled
// Overrides entries of `medium` (one per platform). false names the first problem.
bool ParseBudgets(std::string_view json, std::array<BudgetLimits, kBudgetPlatforms>& medium, std::string* error = nullptr);

struct BudgetBar {
  std::string key, label;  // "draws" / "Draw"
  double used = 0, limit = 0;
  float fraction = 0;      // used / limit (may exceed 1)
  int level = 0;           // 0 within, 1 near (80 % and up), 2 over
  std::string text;        // "182 / 200", "1.18M / 1.0M", "310 / 512 MB", "3 / 8 (그림자 1 / 2)"
};
std::vector<BudgetBar> EvaluateBudget(const BudgetUse& use, const BudgetLimits& limits);
std::string FormatCount(double v); // 980, 12.4k, 1.18M

} // namespace bdg
