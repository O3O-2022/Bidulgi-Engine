#pragma once
// Korean names of engine terms (UI review U12, 2026-11-26). The editor keeps component and field names in English
// (engine terms, as in Unity / Unreal) and shows the Korean name as the first tooltip line. The table is
// design-system/project/editor.ko.json ({"names": {"Light": "라이트", "Light.intensity": "강도", ...}}), shared by both
// editor lanes, the AI reference and translators. A missing entry is "" (no Korean line).
#include <string>
#include <string_view>

namespace bdg {
// "Light" or "Light.intensity" -> the Korean name, or "" when the table has none.
const std::string& KoreanName(std::string_view key);
// The tooltip for a term: the Korean name, then the English description (either may be empty).
std::string TermTooltip(std::string_view key, const std::string& description);
// Reads the table from this file instead of looking for it next to the editor (tests). "" goes back to the default.
void SetKoreanNamesFile(const std::string& path);
} // namespace bdg
