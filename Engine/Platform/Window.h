#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace bdg {
enum class Key { None, Tab, Enter, Space, Escape, Backspace, Delete, Left, Right, Up, Down, Home, End, F1, A, C, V, X, Z, Y };

// Platform-neutral input event. Coordinates are in drawable pixels (same space as rendering).
struct InputEvent {
  enum class Type { MouseMove, MouseDown, MouseUp, KeyDown, Text, TextEditing } type = Type::MouseMove;
  float x = 0, y = 0;
  int button = 0;     // 1 = left
  Key key = Key::None;
  bool shift = false, ctrl = false;
  std::string text;   // Text: committed UTF-8 text. TextEditing: IME composition in progress ("" = cleared)
};

// Platform layer: window, GL context, events. Upper layers never see SDL.
class Window {
public:
  bool Create(const char* title, int32_t w, int32_t h);
  void Destroy();
  bool PumpEvents(); // false = quit requested
  void Present();
  void GetDrawableSize(int32_t& w, int32_t& h) const;
  float GetDisplayScale() const; // OS UI scale for this window's monitor (1.0 = 100%, 1.5 = 150%)
  static void* GetGLProcAddress(const char* name); // for the GL loader

  const std::vector<InputEvent>& Events() const { return events_; } // events of the last PumpEvents
  void InjectEvent(const InputEvent& e) { events_.push_back(e); }    // tests / automation

  // IME: where the composition / candidate window should appear (drawable pixels).
  void SetTextInputArea(float x, float y, float w, float h, int cursorOffsetPx);
  std::string GetClipboardText() const;
  void SetClipboardText(const std::string& text);

private:
  std::vector<InputEvent> events_;
  SDL_Window* window_ = nullptr;
  void* glContext_ = nullptr;
};
}
