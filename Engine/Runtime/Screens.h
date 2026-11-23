#pragma once
#include "UI/UI.h"
#include <memory>

namespace bdg {
// Editor screens built from the UI framework. Temporary home until the editor module exists.
std::unique_ptr<Widget> BuildEditorScreen();               // fixed-layout editor (queue item 0-9)
std::unique_ptr<Widget> BuildGalleryScreen(int* clickCounter); // design system component gallery (0-7)
// Design parity test (queue 0-10): the same layout as design-system/project/components/<name>/preview.html,
// with the same data-bdg-id ids. nullptr for an unknown component name.
std::unique_ptr<Widget> BuildParityScreen(const std::string& component);
void PushFrameTime(float ms);                                  // feeds the Profiler tab (last 240 frames)
}
