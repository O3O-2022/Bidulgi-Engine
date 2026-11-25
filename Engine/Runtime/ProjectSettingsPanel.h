#pragma once
// Project Settings panel (Unreal-style categories): one card per ProjectSettings section, fields built from
// reflection (text, number sliders for ranges, switches for bools, choices for enums), hover help from Tooltip,
// a problems list (what a build would trip over) and a Save button. Edits change the settings in memory; they are
// written by Save, Save All (Ctrl+Shift+S) or the quit checklist.
#include "Project/ProjectSettings.h"
#include <functional>

namespace bdg {
struct Widget;
class UIContext;

struct ProjectSettingsPanel {
  // Builds (or rebuilds) the panel's widgets inside `parent` (its children are replaced).
  static void Build(Widget& parent, UIContext& ui, ProjectSettings& settings, std::function<void()> changed,
                    std::function<void()> save);
  // After a change or a save: the unsaved badge and the problems list.
  static void Refresh(UIContext& ui, const ProjectSettings& settings, bool dirty);
};

} // namespace bdg
