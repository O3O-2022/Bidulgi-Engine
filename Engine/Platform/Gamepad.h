#pragma once
// Gamepads for input actions (queue 6-4): the first connected pad's sticks, triggers and buttons, read through SDL3's
// gamepad API (standard layout: South = Xbox A / PlayStation Cross). Upper layers never see SDL.
#include <cstdint>
#include <string>

namespace bdg {
enum class PadButton : uint32_t {
  South, East, West, North, Back, Start, LeftStick, RightStick, LeftShoulder, RightShoulder, DPadUp, DPadDown, DPadLeft, DPadRight, Count
};
struct GamepadState {
  bool connected = false;
  float leftX = 0, leftY = 0, rightX = 0, rightY = 0; // -1..1, Y up
  float leftTrigger = 0, rightTrigger = 0;             // 0..1
  uint32_t buttons = 0;                                // bit per PadButton
  std::string name;
  bool Down(PadButton b) const { return (buttons >> (uint32_t)b) & 1u; }
};
// Starts SDL's gamepad subsystem on the first call (needs the window's event loop to see pads come and go).
bool ReadGamepad(GamepadState& out);
const char* PadButtonName(PadButton b);              // "South", "DPadUp", ...
}
