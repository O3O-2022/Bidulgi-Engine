#pragma once
// Crash resistance basics (queue 1-4c).
//
// BDG_CHECK(cond, "fmt", args...) — an assertion the editor survives: when cond is false it logs
// "check failed: ... (file:line)", counts it, and evaluates to false so the caller can bail out:
//     if (!BDG_CHECK(index < size, "bad index {}", index)) return;
//
// GuardedCall(what, fn) — runs code that might crash (extension and module code: custom panel drawing,
// later C++ game modules) under a structured exception guard. A crash inside becomes a logged error, the
// failure handler runs (the editor autosaves there) and the editor goes on. With /EHsc, C++ destructors in
// the crashed frames do not run: the guard keeps the editor alive and the work saved, it does not make
// the crashed code correct.
#include "Core/Log.h"
#include <cstdint>
#include <format>
#include <functional>
#include <string>

namespace bdg {
bool CheckFailed(const char* expr, const char* file, int line, const std::string& message);
uint32_t CheckFailureCount();

// Returns false if fn crashed (access violation, divide by zero, stack overflow, ...); error gets the reason.
bool GuardedCall(const char* what, const std::function<void()>& fn, std::string* error = nullptr);
uint32_t GuardFailureCount();
void SetGuardFailureHandler(std::function<void(const std::string& what, const std::string& reason)> handler);
}

#define BDG_CHECK(cond, ...) \
  ((cond) ? true : ::bdg::CheckFailed(#cond, __FILE__, __LINE__, ::bdg::detail::CheckMessage(__VA_ARGS__)))

namespace bdg::detail {
inline std::string CheckMessage() { return {}; }
template <class... A> std::string CheckMessage(std::format_string<A...> f, A&&... a) { return std::format(f, std::forward<A>(a)...); }
}
