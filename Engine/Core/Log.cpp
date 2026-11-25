#include "Core/Log.h"
#include <cstdio>
#include <share.h>

namespace bdg {
namespace {
bool g_commandLine = false;
}

void SetLogForCommandLine(bool on) { g_commandLine = on; }

static std::function<void(LogLevel, std::string_view)>& LogHookSlot() {
  static std::function<void(LogLevel, std::string_view)> h;
  return h;
}
void SetLogHook(std::function<void(LogLevel, std::string_view)> hook) { LogHookSlot() = std::move(hook); }

void LogWrite(LogLevel level, std::string_view msg) {
  if (LogHookSlot()) LogHookSlot()(level, msg);
  // One text line per entry, "[level] message", easy for AI and CI to read.
  const char* tag = level == LogLevel::Info ? "info" : level == LogLevel::Warn ? "warn" : "error";
  if (g_commandLine) {
    if (level != LogLevel::Info) std::fprintf(stderr, "[%s] %.*s\n", tag, (int)msg.size(), msg.data());
    return;
  }
  std::fprintf(level == LogLevel::Error ? stderr : stdout, "[%s] %.*s\n", tag, (int)msg.size(), msg.data());
  std::fflush(stdout);
  // Also write to Bidulgi.log in the working folder (a windowed app has no console).
  static std::FILE* file = _fsopen("Bidulgi.log", "w", _SH_DENYNO); // readable while running
  if (file) { std::fprintf(file, "[%s] %.*s\n", tag, (int)msg.size(), msg.data()); std::fflush(file); }
}
}
