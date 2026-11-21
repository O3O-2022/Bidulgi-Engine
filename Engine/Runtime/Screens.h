#pragma once
#include "UI/UI.h"
#include <memory>

namespace bdg {
// Editor screens built from the UI framework. Temporary home until the editor module exists.
std::unique_ptr<Widget> BuildEditorScreen();               // fixed-layout editor (queue item 0-9)
std::unique_ptr<Widget> BuildGalleryScreen(int* clickCounter); // design system component gallery (0-7)
}
