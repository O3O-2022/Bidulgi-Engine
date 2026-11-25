#include "Input/GameInput.h"
#include "Core/Log.h"
#include "Input/InputActions.h"
#include "Platform/Gamepad.h"
#include "UI/UI.h"
#include <algorithm>

namespace bdg {
void RouteGameInput(std::vector<InputEvent>& events, const Widget* game, bool playing, double dt, const std::filesystem::path& projectRoot) {
  static bool wasPlaying = false, focusOnShow = false; // Play focuses the Game view as soon as it is laid out
  InputSystem& in = InputSystem::Instance();
  const bool visible = game && !game->hidden && game->w >= 2 && game->h >= 2;
  if (!playing) {
    if (wasPlaying) in.Reset();
    wasPlaying = false;
    return;
  }
  if (!wasPlaying) { // Play: the project's actions, the Game view takes the input
    in.Load(projectRoot);
    in.Reset();
    focusOnShow = true;
    wasPlaying = true;
  }
  using T = InputEvent::Type;
  auto inside = [&](const InputEvent& ev) { return visible && ev.x >= game->x && ev.y >= game->y && ev.x < game->x + game->w && ev.y < game->y + game->h; };
  auto focus = [&](bool on) {
    if (in.gameFocused == on) return;
    in.gameFocused = on;
    if (!on) in.ReleaseAll();
    LogInfo("input: game view {}", on ? "has the input" : "gave the input back to the editor");
  };
  if (!visible) focus(false);
  else if (focusOnShow) {
    focusOnShow = false;
    focus(true);
  }
  std::erase_if(events, [&](const InputEvent& ev) {
    if (ev.type == T::MouseDown) { // clicking the game takes the input, clicking elsewhere returns it
      focusOnShow = false;
      focus(inside(ev));
    }
    if (!in.gameFocused) return false;
    if (ev.type == T::KeyDown && ev.key == Key::Escape && !ev.ctrl) { focus(false); return true; }
    const bool keyboard = ev.type == T::KeyDown || ev.type == T::KeyUp || ev.type == T::Text || ev.type == T::TextEditing;
    if (keyboard && ev.ctrl && ev.type != T::KeyUp) return false; // editor shortcuts
    const bool mouse = ev.type == T::MouseMove || ev.type == T::MouseDown || ev.type == T::MouseUp || ev.type == T::Wheel;
    if (keyboard || ev.type == T::MouseUp || (mouse && inside(ev)) || ev.type == T::MouseMove) {
      in.Feed(ev);
      return keyboard || inside(ev);
    }
    return false;
  });
  GamepadState pad;
  const bool havePad = in.gameFocused && ReadGamepad(pad);
  in.Update(dt, havePad ? &pad : nullptr);
}
}
