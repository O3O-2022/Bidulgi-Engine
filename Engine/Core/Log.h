#pragma once
#include <cstdint>
#include <string>
#include <format>
#include <functional>
#include <string_view>
#include <vector>

namespace bdg {
enum class LogLevel { Info, Warn, Error };
void LogWrite(LogLevel level, std::string_view msg);
// Command line tools (queue 2-7): stdout stays the command's own output; only warnings and errors are printed (to
// stderr) and no Bidulgi.log is written in the project folder.
void SetLogForCommandLine(bool on);
// The log file (default "Bidulgi.log" in the working folder); set before the first line (the player's --log).
void SetLogFile(const std::string& path);
// Also sends every line to `hook` (tests read what scripts logged; the console panel later). Empty = none.
void SetLogHook(std::function<void(LogLevel, std::string_view)> hook);

// The last kLogHistory lines, kept for the editor's Console / Build Log (UI review B3). Thread-safe; seq counts
// from 1 across the run, so a reader asks only for what came after the last line it has.
constexpr size_t kLogHistory = 2000;
struct LogLine {
  uint64_t seq = 0;
  LogLevel level = LogLevel::Info;
  std::string text;
  std::string channel; // "" = general; "build" = written while building (LogChannel)
};
std::vector<LogLine> LogHistory(uint64_t after = 0);
uint64_t LogLatestSeq();
// While one lives, lines logged on this thread carry its channel name (RunBuild: "build" -> the Build Log).
class LogChannel {
public:
  explicit LogChannel(std::string name);
  ~LogChannel();
  LogChannel(const LogChannel&) = delete;
  LogChannel& operator=(const LogChannel&) = delete;
private:
  std::string previous_;
};

template <class... A> void LogInfo(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogWarn(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogError(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Error, std::format(f, std::forward<A>(a)...)); }
}
