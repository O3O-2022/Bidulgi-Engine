#include "Platform/Window.h"
#include "Core/Log.h"
#include <SDL3/SDL.h>
#include <cctype>

namespace bdg {
void Window::SetTitle(const std::string& title) {
  if (window_) SDL_SetWindowTitle(window_, title.c_str());
}

void Window::SetGameMode(int mode, bool resizable, bool showCursor) {
  if (!window_) return;
  SDL_SetWindowResizable(window_, resizable);
  if (mode == 2) { // exclusive: the display's mode closest to the window size
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    SDL_DisplayMode closest{};
    if (SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window_), w, h, 0.f, true, &closest)) SDL_SetWindowFullscreenMode(window_, &closest);
    SDL_SetWindowFullscreen(window_, true);
  } else if (mode == 1) { // borderless: fullscreen at the desktop's own size
    SDL_SetWindowFullscreenMode(window_, nullptr);
    SDL_SetWindowFullscreen(window_, true);
  } else {
    SDL_SetWindowFullscreen(window_, false);
  }
  if (showCursor) SDL_ShowCursor();
  else SDL_HideCursor();
}

bool Window::Create(const char* title, int32_t w, int32_t h, bool hidden) {
  hidden_ = hidden;
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");       // window/taskbar icon from the exe resource
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { LogError("SDL_Init failed: {}", SDL_GetError()); return false; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef BDG_DEBUG
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG); // GL errors reported via KHR_debug
#endif
  window_ = SDL_CreateWindow(title, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                             (hidden ? SDL_WINDOW_HIDDEN : 0));
  if (!window_) { LogError("window creation failed: {}", SDL_GetError()); return false; }
  // Requested size is in UI units: grow it by the display scale, but keep it inside the usable screen area.
  float scale = SDL_GetWindowDisplayScale(window_);
  SDL_Rect usable{};
  if (scale > 1.01f && SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &usable)) {
    int sw = (int)(w * scale), sh = (int)(h * scale);
    if (sw > usable.w * 0.95f) sw = (int)(usable.w * 0.95f);
    if (sh > usable.h * 0.9f) sh = (int)(usable.h * 0.9f);
    SDL_SetWindowSize(window_, sw, sh);
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
  glContext_ = SDL_GL_CreateContext(window_);
  if (!glContext_) { LogError("OpenGL 4.5 context failed: {}", SDL_GetError()); return false; }
  SetSwapInterval(-1);
  wakeEvent_ = SDL_RegisterEvents(1);
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

namespace {
Key MapKey(SDL_Keycode k) {
  if (k >= SDLK_A && k <= SDLK_Z) return (Key)((int)Key::A + (int)(k - SDLK_A));
  if (k >= SDLK_0 && k <= SDLK_9) return (Key)((int)Key::Num0 + (int)(k - SDLK_0));
  if (k >= SDLK_F1 && k <= SDLK_F12) return (Key)((int)Key::F1 + (int)(k - SDLK_F1));
  switch (k) {
    case SDLK_PAGEUP: return Key::PageUp; case SDLK_PAGEDOWN: return Key::PageDown; case SDLK_INSERT: return Key::Insert;
    case SDLK_TAB: return Key::Tab;       case SDLK_RETURN: return Key::Enter;  case SDLK_SPACE: return Key::Space;
    case SDLK_ESCAPE: return Key::Escape; case SDLK_BACKSPACE: return Key::Backspace; case SDLK_DELETE: return Key::Delete;
    case SDLK_LEFT: return Key::Left;     case SDLK_RIGHT: return Key::Right;   case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;     case SDLK_HOME: return Key::Home;     case SDLK_END: return Key::End;
    case SDLK_MINUS: case SDLK_KP_MINUS: return Key::Minus;
    case SDLK_EQUALS: case SDLK_KP_PLUS: return Key::Equals;
    default: return Key::None;
  }
}
}

namespace {
// Translates one SDL event into an InputEvent (if it is one the UI cares about). false = quit requested.
bool Translate(const SDL_Event& e, float density, std::vector<InputEvent>& out) {
  InputEvent ev;
  switch (e.type) {
    case SDL_EVENT_QUIT: return false;
    case SDL_EVENT_MOUSE_MOTION:
      ev.type = InputEvent::Type::MouseMove; ev.x = e.motion.x * density; ev.y = e.motion.y * density;
      { SDL_Keymod m = SDL_GetModState(); ev.ctrl = (m & SDL_KMOD_CTRL) != 0; ev.shift = (m & SDL_KMOD_SHIFT) != 0; ev.alt = (m & SDL_KMOD_ALT) != 0; }
      out.push_back(ev); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      ev.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? InputEvent::Type::MouseDown : InputEvent::Type::MouseUp;
      ev.x = e.button.x * density; ev.y = e.button.y * density; ev.button = e.button.button;
      { SDL_Keymod m = SDL_GetModState(); ev.ctrl = (m & SDL_KMOD_CTRL) != 0; ev.shift = (m & SDL_KMOD_SHIFT) != 0; ev.alt = (m & SDL_KMOD_ALT) != 0; }
      out.push_back(ev); break;
    case SDL_EVENT_KEY_DOWN:
      ev.type = InputEvent::Type::KeyDown; ev.key = MapKey(e.key.key);
      ev.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0; ev.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0; ev.alt = (e.key.mod & SDL_KMOD_ALT) != 0;
      if (ev.key != Key::None) out.push_back(ev);
      break;
    case SDL_EVENT_KEY_UP: // held keys (scene view fly mode); the UI ignores these
      ev.type = InputEvent::Type::KeyUp; ev.key = MapKey(e.key.key);
      if (ev.key != Key::None) out.push_back(ev);
      break;
    case SDL_EVENT_TEXT_INPUT:
      ev.type = InputEvent::Type::Text; ev.text = e.text.text;
      out.push_back(ev); break;
    case SDL_EVENT_MOUSE_WHEEL:
      ev.type = InputEvent::Type::Wheel; ev.x = e.wheel.mouse_x * density; ev.y = e.wheel.mouse_y * density;
      { SDL_Keymod m = SDL_GetModState(); ev.ctrl = (m & SDL_KMOD_CTRL) != 0; ev.shift = (m & SDL_KMOD_SHIFT) != 0; ev.alt = (m & SDL_KMOD_ALT) != 0; }
      ev.wheel = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
      out.push_back(ev); break;
    case SDL_EVENT_DROP_FILE: // dragged in from Explorer (one event per file or folder)
      ev.type = InputEvent::Type::DropFile; ev.text = e.drop.data ? e.drop.data : "";
      ev.x = e.drop.x * density; ev.y = e.drop.y * density;
      if (!ev.text.empty()) out.push_back(ev);
      break;
    case SDL_EVENT_TEXT_EDITING: // IME composition (e.g. Hangul jamo being combined)
      ev.type = InputEvent::Type::TextEditing; ev.text = e.edit.text ? e.edit.text : "";
      out.push_back(ev); break;
    default: break;
  }
  return true;
}
}

bool Window::PumpEvents(int32_t waitMs) {
  events_.clear();
  hadEvents_ = false;
  float density = SDL_GetWindowPixelDensity(window_);
  if (density <= 0.f) density = 1.f;
  SDL_Event e;
  // Any event (input, resize, expose, focus, Wake) means the picture may have changed.
  if (waitMs > 0 && SDL_WaitEventTimeout(&e, waitMs)) {
    hadEvents_ = true;
    if (!Translate(e, density, events_)) return false;
  }
  while (SDL_PollEvent(&e)) {
    hadEvents_ = true;
    if (!Translate(e, density, events_)) return false;
  }
  return true;
}

void Window::Wake() {
  if (!wakeEvent_) return;
  SDL_Event e{};
  e.type = wakeEvent_;
  SDL_PushEvent(&e);
}

void Window::SetSwapInterval(int interval) {
  // Some drivers accept adaptive vsync (-1) but leave the interval at 0: read it back and fall back to 1.
  auto apply = [](int want) {
    int actual = 0;
    if (!SDL_GL_SetSwapInterval(want) || !SDL_GL_GetSwapInterval(&actual)) return 0;
    return actual;
  };
  int actual = apply(interval);
  if (interval == -1 && actual == 0) actual = apply(1);
  swapInterval_ = actual;
  if (interval != 0 && actual == 0) LogWarn("vsync: unavailable, frames are limited by a timer");
  else LogInfo("vsync: swap interval {} ({})", actual, actual == -1 ? "adaptive" : actual == 1 ? "on" : "off");
}

float Window::RefreshRate() const {
  const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
  return m && m->refresh_rate > 1.f ? m->refresh_rate : 60.f;
}

void Window::SleepPrecise(double ms) {
  if (ms > 0) SDL_DelayPrecise((Uint64)(ms * 1e6));
}

namespace {
const char* kKeyNames[] = {"", "Tab", "Enter", "Space", "Escape", "Backspace", "Delete", "Left", "Right", "Up", "Down", "Home",
                           "End", "PageUp", "PageDown", "Insert", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
                           "F11", "F12", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q",
                           "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-", "="};
}
const char* KeyName(Key k) { return (size_t)k < sizeof kKeyNames / sizeof *kKeyNames ? kKeyNames[(int)k] : ""; }
Key KeyFromName(std::string_view name) {
  for (int i = 1; i < (int)(sizeof kKeyNames / sizeof *kKeyNames); ++i) {
    std::string_view k = kKeyNames[i];
    if (k.size() != name.size()) continue;
    bool same = true;
    for (size_t j = 0; j < k.size() && same; ++j) same = std::tolower((unsigned char)k[j]) == std::tolower((unsigned char)name[j]);
    if (same) return (Key)i;
  }
  if (name == "Del") return Key::Delete;
  if (name == "Esc") return Key::Escape;
  return Key::None;
}

void Window::Present() {
  if (!hidden_) SDL_GL_SwapWindow(window_); // a hidden window shows nothing (its frames are offscreen)
}
void Window::SetCursor(Cursor c) {
  static SDL_Cursor* cursors[3] = {};
  static const SDL_SystemCursor ids[3] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NS_RESIZE};
  int i = (int)c;
  if (!cursors[i]) cursors[i] = SDL_CreateSystemCursor(ids[i]);
  if (cursors[i]) SDL_SetCursor(cursors[i]);
}
void Window::SetMouseCapture(bool on) { SDL_CaptureMouse(on); }
void Window::GetDrawableSize(int32_t& w, int32_t& h) const { SDL_GetWindowSizeInPixels(window_, &w, &h); }
float Window::GetDisplayScale() const {
  float s = SDL_GetWindowDisplayScale(window_);
  return s > 0.f ? s : 1.f;
}

void* Window::GetGLProcAddress(const char* name) { return (void*)SDL_GL_GetProcAddress(name); }

void Window::SetTextInputArea(float x, float y, float w, float h, int cursorOffsetPx) {
  float density = SDL_GetWindowPixelDensity(window_);
  if (density <= 0.f) density = 1.f;
  SDL_Rect r{(int)(x / density), (int)(y / density), (int)(w / density), (int)(h / density)};
  SDL_SetTextInputArea(window_, &r, (int)(cursorOffsetPx / density));
}

std::string Window::GetClipboardText() const {
  if (hidden_) return clipboard_; // automated runs keep their own clipboard: tests never touch the user's
  char* t = SDL_GetClipboardText();
  std::string s = t ? t : "";
  SDL_free(t);
  return s;
}

void Window::SetClipboardText(const std::string& text) {
  if (hidden_) { clipboard_ = text; return; }
  SDL_SetClipboardText(text.c_str());
}
bool Window::OpenURL(const std::string& url) { return SDL_OpenURL(url.c_str()); }
void* Window::NativeHandle() const {
  return window_ ? SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr) : nullptr;
}
}
