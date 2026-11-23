#pragma once
// Screen reader bridge (queue 1-6): the focusable widgets of the current screen are exposed through
// Windows UI Automation, so Narrator, NVDA and JAWS read each button, tab, text box and list item by name
// and role, follow focus changes, and can press buttons or set text. No extra dependency (AccessKit later
// if other platforms need it).
#include <functional>
#include <string>

namespace bdg {
class UIContext;
class Window;
namespace a11y {
bool Attach(Window& window, std::function<UIContext*()> currentUi);
void FocusChanged(); // raises the UIA focus event (only when a screen reader listens)
void Detach();

// Automated check (BDG_A11Y_TEST): a UI Automation client on another thread reads this window the way a
// screen reader does. The UI thread must keep pumping messages while it runs (provider calls arrive there).
void StartProbe();
bool ProbeDone();
std::string ProbeResult(); // "focused_name=... control=Button children=N" (waits for the thread)
}
}
