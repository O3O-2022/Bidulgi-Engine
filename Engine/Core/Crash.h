#pragma once
// Crash reports (queue 1-4b). When the editor crashes, the handler writes
//   <root>/crash-<time>/crash.dmp   minidump (open in Visual Studio / WinDbg)
//   <root>/crash-<time>/crash.txt   exception code and address
//   <root>/crash-<time>/Bidulgi.log the log up to the crash
// and leaves <root>/pending-crash.txt, so the next start can offer to report it. Nothing is uploaded.
// Windows dbghelp for now (no extra dependency); crashpad (out-of-process, all platforms) later.
#include <string>

namespace bdg {
void InstallCrashHandler(const std::string& reportRoot); // no-op while a debugger is attached
std::string TakePendingCrash(const std::string& reportRoot); // folder of the last crash ("" = none); clears the marker
[[noreturn]] void CrashForTest();                          // BDG_CRASH_TEST: an access violation on purpose
}
