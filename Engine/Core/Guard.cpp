#include "Core/Guard.h"
#include <atomic>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <malloc.h>

namespace bdg {
namespace {
std::atomic<uint32_t> g_checks{0}, g_guards{0};
std::function<void(const std::string&, const std::string&)> g_onGuardFailure;

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation (null or dangling pointer)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow (infinite recursion?)";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "float divide by zero";
    case 0xE06D7363: return "C++ exception";
    default: return "structured exception";
  }
}

struct Thunk {
  const std::function<void()>* fn;
};
void CallThunk(void* p) { (*static_cast<Thunk*>(p)->fn)(); }

// __try cannot live in a function that needs C++ unwinding, so this one only takes plain pointers.
DWORD SehCall(void (*call)(void*), void* ctx) {
  __try {
    call(ctx);
    return 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    DWORD code = GetExceptionCode();
    if (code == EXCEPTION_STACK_OVERFLOW) _resetstkoflw(); // make the guard page usable again
    return code ? code : 1;
  }
}
}

bool CheckFailed(const char* expr, const char* file, int line, const std::string& message) {
  ++g_checks;
  const char* base = file;
  for (const char* p = file; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
  LogError("check failed: {}{}{} ({}:{})", expr, message.empty() ? "" : " — ", message, base, line);
  return false;
}

uint32_t CheckFailureCount() { return g_checks; }

bool GuardedCall(const char* what, const std::function<void()>& fn, std::string* error) {
  Thunk t{&fn};
  DWORD code = SehCall(&CallThunk, &t);
  if (code == 0) return true;
  ++g_guards;
  std::string reason = std::format("{} (0x{:08X})", ExceptionName(code), (unsigned)code);
  LogError("guard: {} crashed: {}; the editor keeps running", what, reason);
  if (error) *error = reason;
  if (g_onGuardFailure) g_onGuardFailure(what, reason);
  return false;
}

uint32_t GuardFailureCount() { return g_guards; }

void SetGuardFailureHandler(std::function<void(const std::string&, const std::string&)> handler) {
  g_onGuardFailure = std::move(handler);
}
}
