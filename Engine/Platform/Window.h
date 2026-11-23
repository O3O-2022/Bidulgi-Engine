#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace bdg {
enum class Key {
  None, Tab, Enter, Space, Escape, Backspace, Delete, Left, Right, Up, Down, Home, End, PageUp, PageDown, Insert,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9
};
const char* KeyName(Key k);            // "A", "F5", "Delete", "0"
Key KeyFromName(std::string_view name); // inverse of KeyName (case-insensitive); Key::None if unknown

// Platform-neutral input event. Coordinates are in drawable pixels (same space as rendering).
struct InputEvent {
  enum class Type { MouseMove, MouseDown, MouseUp, KeyDown, Text, TextEditing, Wheel, KeyUp } type = Type::MouseMove;
  float x = 0, y = 0;
  int button = 0;     // 1 = left
  Key key = Key::None;
  bool shift = false, ctrl = false, alt = false;
  std::string text;   // Text: committed UTF-8 text. TextEditing: IME composition in progress ("" = cleared)
  float wheel = 0;     // Wheel: notches, positive = away from the user (scroll up)
};

// Platform layer: window, GL context, events. Upper layers never see SDL.
class Window {
public:
  bool Create(const char* title, int32_t w, int32_t h);
  void Destroy();
  // false = quit requested. waitMs > 0: sleep in the OS until an event arrives or the timeout passes
  // (idle editor uses ~0% CPU/GPU instead of redrawing thousands of times per second).
  bool PumpEvents(int32_t waitMs = 0);
  bool HadEvents() const { return hadEvents_; } // anything arrived in the last PumpEvents (input, resize, expose, Wake)
  void Wake();                                  // thread-safe: makes a sleeping PumpEvents return now
  void Present();
  enum class Cursor { Default, ResizeH, ResizeV };
  void SetCursor(Cursor c);
  void SetMouseCapture(bool on); // keep receiving mouse events outside the window while dragging
  // Swap interval actually in effect: -1 adaptive vsync, 1 vsync, 0 off. Request -1 to try adaptive, then 1.
  int SwapInterval() const { return swapInterval_; }
  void SetSwapInterval(int interval);
  float RefreshRate() const;             // Hz of the window's display (60 if unknown)
  static void SleepPrecise(double ms);   // frame limiter
  void GetDrawableSize(int32_t& w, int32_t& h) const;
  float GetDisplayScale() const; // OS UI scale for this window's monitor (1.0 = 100%, 1.5 = 150%)
  static void* GetGLProcAddress(const char* name); // for the GL loader

  const std::vector<InputEvent>& Events() const { return events_; } // events of the last PumpEvents
  void InjectEvent(const InputEvent& e) { events_.push_back(e); }    // tests / automation

  // IME: where the composition / candidate window should appear (drawable pixels).
  void SetTextInputArea(float x, float y, float w, float h, int cursorOffsetPx);
  std::string GetClipboardText() const;
  bool OpenURL(const std::string& url); // default browser / file explorer (file:///)
  void* NativeHandle() const;           // HWND on Windows (accessibility bridge)
  void SetClipboardText(const std::string& text);

private:
  std::vector<InputEvent> events_;
  SDL_Window* window_ = nullptr;
  void* glContext_ = nullptr;
  uint32_t wakeEvent_ = 0;
  int swapInterval_ = 0;
  bool hadEvents_ = false;
};
}
