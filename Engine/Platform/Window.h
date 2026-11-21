#pragma once
#include <cstdint>

struct SDL_Window;

namespace bdg {
// Platform layer: window, GL context, events. Upper layers never see SDL.
class Window {
public:
  bool Create(const char* title, int32_t w, int32_t h);
  void Destroy();
  bool PumpEvents(); // false = quit requested
  void Present();
  void GetDrawableSize(int32_t& w, int32_t& h) const;
  static void* GetGLProcAddress(const char* name); // for the GL loader

private:
  SDL_Window* window_ = nullptr;
  void* glContext_ = nullptr;
};
}
