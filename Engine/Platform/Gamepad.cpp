#include "Platform/Gamepad.h"
#include "Core/Log.h"
#include <SDL3/SDL.h>

namespace bdg {
namespace {
SDL_Gamepad* g_pad = nullptr;
bool g_started = false, g_failed = false;
}

const char* PadButtonName(PadButton b) {
  static const char* names[] = {"South", "East", "West", "North", "Back", "Start", "LeftStick", "RightStick", "LeftShoulder", "RightShoulder",
                                "DPadUp", "DPadDown", "DPadLeft", "DPadRight"};
  return (uint32_t)b < (uint32_t)PadButton::Count ? names[(uint32_t)b] : "";
}

bool ReadGamepad(GamepadState& out) {
  out = {};
  if (g_failed) return false;
  if (!g_started) {
    g_started = true;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      g_failed = true;
      LogWarn("input: gamepads unavailable ({})", SDL_GetError());
      return false;
    }
  }
  if (g_pad && !SDL_GamepadConnected(g_pad)) {
    SDL_CloseGamepad(g_pad);
    g_pad = nullptr;
  }
  if (!g_pad) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
      g_pad = SDL_OpenGamepad(ids[0]);
      if (g_pad) LogInfo("input: gamepad {}", SDL_GetGamepadName(g_pad) ? SDL_GetGamepadName(g_pad) : "?");
    }
    SDL_free(ids);
    if (!g_pad) return false;
  }
  auto axis = [](SDL_GamepadAxis a) { return (float)SDL_GetGamepadAxis(g_pad, a) / 32767.f; };
  out.connected = true;
  out.leftX = axis(SDL_GAMEPAD_AXIS_LEFTX);
  out.leftY = -axis(SDL_GAMEPAD_AXIS_LEFTY); // SDL: down is positive
  out.rightX = axis(SDL_GAMEPAD_AXIS_RIGHTX);
  out.rightY = -axis(SDL_GAMEPAD_AXIS_RIGHTY);
  out.leftTrigger = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  out.rightTrigger = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  static const SDL_GamepadButton map[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
                                          SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                                          SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
                                          SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
  for (uint32_t i = 0; i < (uint32_t)PadButton::Count; ++i)
    if (SDL_GetGamepadButton(g_pad, map[i])) out.buttons |= 1u << i;
  const char* n = SDL_GetGamepadName(g_pad);
  out.name = n ? n : "";
  return true;
}
}
