#pragma once
#include <string>
#include <format>
#include <functional>
#include <string_view>

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

template <class... A> void LogInfo(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogWarn(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogError(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Error, std::format(f, std::forward<A>(a)...)); }
}
