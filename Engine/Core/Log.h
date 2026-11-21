#pragma once
#include <format>
#include <string_view>

namespace bdg {
enum class LogLevel { Info, Warn, Error };
void LogWrite(LogLevel level, std::string_view msg);

template <class... A> void LogInfo(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogWarn(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogError(std::format_string<A...> f, A&&... a) { LogWrite(LogLevel::Error, std::format(f, std::forward<A>(a)...)); }
}
