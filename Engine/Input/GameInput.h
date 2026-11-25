#pragma once
// Editor play mode and input actions (queue 6-4), Unity's Game view focus: while playing, the Game view has the
// keyboard, mouse and gamepad when it is focused — right after Play, or after a click into it. Escape or a click
// elsewhere gives them back to the editor (keys held then are released). Ctrl shortcuts (Ctrl+P stop, ...) always
// reach the editor. The game's events are taken out of `events`; the rest go on to the editor UI.
#include "Platform/Window.h"
#include <filesystem>
#include <vector>

namespace bdg {
struct Widget;
// Call once per frame before the game ticks (input is the first stage of a game frame). gameView: the Game panel
// (nullptr or hidden = not on screen). projectRoot: where Input/Actions.jsonl is (read when Play starts).
void RouteGameInput(std::vector<InputEvent>& events, const Widget* gameView, bool playing, double dt, const std::filesystem::path& projectRoot);
}
