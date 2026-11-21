#include "Platform/Window.h"
#include "Core/Log.h"
#include <SDL3/SDL.h>

namespace bdg {
bool Window::Create(const char* title, int32_t w, int32_t h) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { LogError("SDL_Init failed: {}", SDL_GetError()); return false; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  window_ = SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window_) { LogError("window creation failed: {}", SDL_GetError()); return false; }
  glContext_ = SDL_GL_CreateContext(window_);
  if (!glContext_) { LogError("OpenGL 4.5 context failed: {}", SDL_GetError()); return false; }
  SDL_GL_SetSwapInterval(1);
  SDL_StartTextInput(window_); // ready for IME text (Korean input is an M0 exit criterion)
  LogInfo("platform: SDL {}.{}.{}", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
  return true;
}

void Window::Destroy() {
  if (glContext_) SDL_GL_DestroyContext((SDL_GLContext)glContext_);
  if (window_) SDL_DestroyWindow(window_);
  glContext_ = nullptr;
  window_ = nullptr;
  SDL_Quit();
}

bool Window::PumpEvents() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) return false;
    if (e.type == SDL_EVENT_TEXT_INPUT) LogInfo("text input: {}", e.text.text); // committed IME text
  }
  return true;
}

void Window::Present() { SDL_GL_SwapWindow(window_); }
void Window::GetDrawableSize(int32_t& w, int32_t& h) const { SDL_GetWindowSizeInPixels(window_, &w, &h); }
}
