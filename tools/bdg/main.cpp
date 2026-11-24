// `bdg` command line tool (queue 2-7): a thin wrapper over the engine's C API (bdg_cli_main).
// Arguments arrive as UTF-16 on Windows and go on as UTF-8; the console prints UTF-8 (Korean messages).
#include <bdg/bdg_api.h>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> utf8;
  for (int i = 0; i < argc; ++i) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
    utf8.push_back(std::move(s));
  }
  std::vector<const char*> args;
  for (const std::string& s : utf8) args.push_back(s.c_str());
  return bdg_cli_main((int)args.size(), args.data());
}
#else
int main(int argc, char** argv) { return bdg_cli_main(argc, argv); }
#endif
