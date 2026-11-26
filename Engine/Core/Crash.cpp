#include "Core/Crash.h"
#include "Core/Utf8Path.h"
#include "Core/Log.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

namespace bdg {
namespace {
// Everything the handler needs is prepared up front: it runs in a broken process and should not allocate much.
wchar_t g_root[MAX_PATH] = {};
wchar_t g_log[MAX_PATH] = {};

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  wsprintfW(dir, L"%s\\crash-%04d%02d%02d-%02d%02d%02d", g_root, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  CreateDirectoryW(g_root, nullptr);
  CreateDirectoryW(dir, nullptr);

  wsprintfW(path, L"%s\\crash.dmp", dir);
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                      (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo), &mei, nullptr, nullptr);
    CloseHandle(f);
  }

  wsprintfW(path, L"%s\\crash.txt", dir);
  f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    char text[256];
    HMODULE module = nullptr;
    char moduleName[MAX_PATH] = "?";
    void* address = info->ExceptionRecord->ExceptionAddress;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module))
      GetModuleFileNameA(module, moduleName, MAX_PATH);
    const char* base = moduleName;
    for (const char* p = moduleName; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
    int n = wsprintfA(text, "exception: 0x%08lX\r\naddress: %p (%s+0x%IX)\r\n", info->ExceptionRecord->ExceptionCode, address, base,
                      (SIZE_T)((char*)address - (char*)module));
    DWORD written = 0;
    WriteFile(f, text, (DWORD)n, &written, nullptr);
    CloseHandle(f);
  }

  wsprintfW(path, L"%s\\Bidulgi.log", dir);
  CopyFileW(g_log, path, FALSE);

  wsprintfW(path, L"%s\\pending-crash.txt", g_root); // the next start finds this
  f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(f, dir, (DWORD)(lstrlenW(dir) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
  }
  return EXCEPTION_EXECUTE_HANDLER; // end the process quietly (no Windows error dialog)
}
}

void InstallCrashHandler(const std::string& reportRoot) {
  if (IsDebuggerPresent()) return; // let the debugger stop at the crash instead
  std::filesystem::path root = std::filesystem::absolute(reportRoot);
  std::filesystem::path log = std::filesystem::absolute("Bidulgi.log");
  lstrcpynW(g_root, root.wstring().c_str(), MAX_PATH);
  lstrcpynW(g_log, log.wstring().c_str(), MAX_PATH);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  ULONG guarantee = 64 * 1024; // stack left for the handler after a stack overflow
  SetThreadStackGuarantee(&guarantee);
  SetUnhandledExceptionFilter(OnCrash);
}

std::string TakePendingCrash(const std::string& reportRoot) {
  std::filesystem::path marker = bdg::PathU8(reportRoot) / "pending-crash.txt";
  std::error_code ec;
  if (!std::filesystem::exists(marker, ec)) return {};
  std::wstring dir;
  {
    std::ifstream in(marker, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    dir.assign(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
  }
  std::filesystem::remove(marker, ec);
  std::filesystem::path p(dir);
  return std::filesystem::is_directory(p, ec) ? bdg::PathToU8(p) : std::string();
}

void CrashForTest() {
  LogInfo("crash test: writing to a null pointer");
  volatile int* p = nullptr;
  *p = 42;
  std::abort();
}
}
