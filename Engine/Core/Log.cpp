#include "Core/Log.h"
#include <cstdio>
#include <deque>
#include <mutex>
#include <share.h>

namespace bdg {
namespace {
bool g_commandLine = false;
}

void SetLogForCommandLine(bool on) { g_commandLine = on; }
static std::string& LogPath() {
  static std::string path = "Bidulgi.log";
  return path;
}
void SetLogFile(const std::string& path) { LogPath() = path; }

static std::function<void(LogLevel, std::string_view)>& LogHookSlot() {
  static std::function<void(LogLevel, std::string_view)> h;
  return h;
}
void SetLogHook(std::function<void(LogLevel, std::string_view)> hook) { LogHookSlot() = std::move(hook); }

namespace {
std::mutex g_historyMutex;
std::deque<LogLine> g_history;
uint64_t g_seq = 0;
thread_local std::string t_channel;
}

LogChannel::LogChannel(std::string name) : previous_(std::move(t_channel)) { t_channel = std::move(name); }
LogChannel::~LogChannel() { t_channel = std::move(previous_); }

std::vector<LogLine> LogHistory(uint64_t after) {
  std::lock_guard<std::mutex> lock(g_historyMutex);
  std::vector<LogLine> out;
  for (const LogLine& l : g_history)
    if (l.seq > after) out.push_back(l);
  return out;
}

uint64_t LogLatestSeq() {
  std::lock_guard<std::mutex> lock(g_historyMutex);
  return g_seq;
}

void LogWrite(LogLevel level, std::string_view msg) {
  {
    std::lock_guard<std::mutex> lock(g_historyMutex);
    g_history.push_back({++g_seq, level, std::string(msg), t_channel});
    if (g_history.size() > kLogHistory) g_history.pop_front();
  }
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
  static std::FILE* file = _fsopen(LogPath().c_str(), "w", _SH_DENYNO); // readable while running
  if (file) { std::fprintf(file, "[%s] %.*s\n", tag, (int)msg.size(), msg.data()); std::fflush(file); }
}
}
