#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace bdg {
enum class Key { None, Tab, Enter, Space, Escape, Backspace, Delete, Left, Right, Up, Down, Home, End, F1, A, C, V, X, Z, Y };

// Platform-neutral input event. Coordinates are in drawable pixels (same space as rendering).
struct InputEvent {
  enum class Type { MouseMove, MouseDown, MouseUp, KeyDown, Text } type = Type::MouseMove;
  float x = 0, y = 0;
  int button = 0;     // 1 = left
  Key key = Key::None;
  bool shift = false, ctrl = false;
  std::string text;   // committed UTF-8 text (IME)
};

// Platform layer: window, GL context, events. Upper layers never see SDL.
class Window {
public:
  bool Create(const char* title, int32_t w, int32_t h);
  void Destroy();
  bool PumpEvents(); // false = quit requested
  void Present();
  void GetDrawableSize(int32_t& w, int32_t& h) const;
  static void* GetGLProcAddress(const char* name); // for the GL loader

  const std::vector<InputEvent>& Events() const { return events_; } // events of the last PumpEvents
  void InjectEvent(const InputEvent& e) { events_.push_back(e); }    // tests / automation

private:
  std::vector<InputEvent> events_;
  SDL_Window* window_ = nullptr;
  void* glContext_ = nullptr;
};
}
