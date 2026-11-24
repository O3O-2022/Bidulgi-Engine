#include "Scripting/DotNetHost.h"
#include "Core/Command.h"
#include "Core/Log.h"
#include "Scripting/ScriptApi.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}
fs::path U8(const std::string& s) { return fs::path(std::u8string((const char8_t*)s.c_str())); }

// "9.0.20" > "8.0.31"; pre-release tails ("10.0.0-rc.1") sort below the release
bool VersionLess(const std::string& a, const std::string& b) {
  auto parts = [](const std::string& s) {
    std::vector<long> v;
    size_t i = 0;
    while (i < s.size() && v.size() < 4) {
      v.push_back(std::strtol(s.c_str() + i, nullptr, 10));
      i = s.find_first_of(".-", i);
      if (i == std::string::npos || s[i] == '-') break;
      ++i;
    }
    v.resize(4, 0);
    return v;
  };
  const auto pa = parts(a), pb = parts(b);
  if (pa != pb) return pa < pb;
  return (a.find('-') == std::string::npos) < (b.find('-') == std::string::npos);
}

#ifdef _WIN32
// hostfxr / coreclr_delegates declarations (dotnet/runtime src/native/corehost, MIT)
using char_t = wchar_t;
using hostfxr_handle = void*;
struct hostfxr_initialize_parameters {
  size_t size;
  const char_t* host_path;
  const char_t* dotnet_root;
};
enum hostfxr_delegate_type { hdt_com_activation, hdt_load_in_memory_assembly, hdt_winrt_activation, hdt_com_register,
                             hdt_com_unregister, hdt_load_assembly_and_get_function_pointer, hdt_get_function_pointer };
using hostfxr_error_writer_fn = void(__cdecl*)(const char_t* message);
using hostfxr_set_error_writer_fn = hostfxr_error_writer_fn(__cdecl*)(hostfxr_error_writer_fn writer);
using hostfxr_initialize_for_runtime_config_fn = int32_t(__cdecl*)(const char_t* runtime_config_path,
                                                                   const hostfxr_initialize_parameters* parameters,
                                                                   hostfxr_handle* host_context_handle);
using hostfxr_get_runtime_delegate_fn = int32_t(__cdecl*)(const hostfxr_handle host_context_handle, hostfxr_delegate_type type,
                                                         void** delegate);
using hostfxr_close_fn = int32_t(__cdecl*)(const hostfxr_handle host_context_handle);
using load_assembly_and_get_function_pointer_fn = int(__stdcall*)(const char_t* assembly_path, const char_t* type_name,
                                                                  const char_t* method_name, const char_t* delegate_type_name,
                                                                  void* reserved, void** delegate);
const char_t* const kUnmanagedCallersOnly = (const char_t*)-1;

std::string g_hostError; // last message hostfxr wrote
void __cdecl HostErrorWriter(const char_t* message) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? (size_t)n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, message, -1, s.data(), n, nullptr, nullptr);
  g_hostError = s;
  LogWarn(".NET host: {}", s);
}
#endif
} // namespace

DotNetHost& DotNetHost::Instance() {
  static DotNetHost host;
  return host;
}

std::string DotNetHost::FindHostFxr(std::string* error) {
#ifdef _WIN32
  std::vector<fs::path> roots;
  if (const wchar_t* r = _wgetenv(L"DOTNET_ROOT"); r && *r) roots.emplace_back(r);
  if (const wchar_t* pf = _wgetenv(L"ProgramFiles"); pf && *pf) roots.push_back(fs::path(pf) / "dotnet");
  for (const fs::path& root : roots) {
    std::error_code ec;
    std::string best;
    for (const auto& e : fs::directory_iterator(root / "host" / "fxr", ec)) {
      const std::string v = Utf8(e.path().filename());
      if (fs::exists(e.path() / "hostfxr.dll", ec) && (best.empty() || VersionLess(best, v))) best = v;
    }
    if (!best.empty()) return Utf8(root / "host" / "fxr" / U8(best) / "hostfxr.dll");
  }
  if (error) *error = ".NET 8 이상 런타임이 없음 (https://dotnet.microsoft.com 에서 설치하거나 DOTNET_ROOT를 지정)";
#else
  if (error) *error = ".NET hosting is Windows-only for now";
#endif
  return {};
}

std::string DotNetHost::DefaultManagedDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  if (n > 0 && n < std::size(buf)) return Utf8(fs::path(std::wstring(buf, n)).parent_path() / "Managed");
#endif
  return Utf8(fs::current_path() / "Managed");
}

bool DotNetHost::Start(const std::string& managedDir, std::string* error) {
  if (Running()) return true;
  auto fail = [&](std::string why) {
    if (error) *error = why;
    LogError("C# scripting: {}", why);
    return false;
  };
#ifdef _WIN32
  const fs::path dir = U8(managedDir.empty() ? DefaultManagedDir() : managedDir);
  const fs::path dll = dir / "Bidulgi.Scripting.dll", config = dir / "Bidulgi.Scripting.runtimeconfig.json";
  std::error_code ec;
  if (!fs::exists(dll, ec) || !fs::exists(config, ec))
    return fail(std::format("Bidulgi.Scripting.dll이 없음: {} (빌드: cmake --build build --target BidulgiScripting)", Utf8(dir)));
  std::string why;
  const std::string fxr = FindHostFxr(&why);
  if (fxr.empty()) return fail(why);
  HMODULE lib = LoadLibraryW(U8(fxr).c_str());
  if (!lib) return fail(std::format("hostfxr를 열 수 없음: {}", fxr));
  auto setWriter = (hostfxr_set_error_writer_fn)(void*)GetProcAddress(lib, "hostfxr_set_error_writer");
  auto init = (hostfxr_initialize_for_runtime_config_fn)(void*)GetProcAddress(lib, "hostfxr_initialize_for_runtime_config");
  auto getDelegate = (hostfxr_get_runtime_delegate_fn)(void*)GetProcAddress(lib, "hostfxr_get_runtime_delegate");
  auto close = (hostfxr_close_fn)(void*)GetProcAddress(lib, "hostfxr_close");
  if (!init || !getDelegate || !close) return fail("hostfxr가 너무 오래됨 (.NET 8 이상 필요)");
  if (setWriter) setWriter(HostErrorWriter);
  g_hostError.clear();
  hostfxr_handle ctx = nullptr;
  const int32_t rc = init(config.c_str(), nullptr, &ctx);
  if (rc < 0 || !ctx) { // 1 / 2 = already initialized (fine); negative = failure
    if (ctx) close(ctx);
    return fail(std::format("런타임을 시작할 수 없음 (0x{:08x}) {}", (uint32_t)rc, g_hostError));
  }
  void* fn = nullptr;
  const int32_t rd = getDelegate(ctx, hdt_load_assembly_and_get_function_pointer, &fn);
  close(ctx);
  if (rd < 0 || !fn) return fail(std::format("런타임 함수를 얻을 수 없음 (0x{:08x}) {}", (uint32_t)rd, g_hostError));
  auto load = (load_assembly_and_get_function_pointer_fn)fn;
  void* initialize = nullptr;
  void* selfTest = nullptr;
  int rl = load(dll.c_str(), L"Bidulgi.Host, Bidulgi.Scripting", L"Initialize", kUnmanagedCallersOnly, nullptr, &initialize);
  if (rl >= 0) rl = load(dll.c_str(), L"Bidulgi.Host, Bidulgi.Scripting", L"SelfTest", kUnmanagedCallersOnly, nullptr, &selfTest);
  if (rl < 0 || !initialize || !selfTest)
    return fail(std::format("Bidulgi.Scripting의 진입점을 찾을 수 없음 (0x{:08x}) {}", (uint32_t)rl, g_hostError));
  const int api = ((int(__stdcall*)(void*))initialize)((void*)&DotNetHost::Request);
  if (api != 1) return fail(std::format("스크립팅 API 버전이 다름 ({} != 1)", api));
  selfTest_ = (int (*)(const char*))selfTest;
  // the runtime version the host resolved (written next to the dll by the build: *.runtimeconfig.json names the
  // framework; the actual one may be newer with RollForward)
  version_.clear();
  if (std::ifstream in(config); in) {
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (size_t v = text.find("\"version\""); v != std::string::npos)
      if (size_t a = text.find('"', text.find(':', v) + 1), b = a == std::string::npos ? a : text.find('"', a + 1); b != std::string::npos)
        version_ = text.substr(a + 1, b - a - 1);
  }
  LogInfo("C# scripting: .NET runtime started (framework {}, {})", version_, fxr);
  return true;
#else
  (void)managedDir;
  return fail(".NET hosting is Windows-only for now");
#endif
}

void DotNetHost::Attach(Scene* scene, CommandBus* bus) {
  scene_ = scene;
  bus_ = bus;
  lastRequest_.clear();
  lastAnswer_.clear();
}

int DotNetHost::SelfTest(const std::string& target) {
  if (!selfTest_) return -2;
#ifdef _WIN32
  return ((int(__stdcall*)(const char*))selfTest_)(target.c_str());
#else
  return selfTest_(target.c_str());
#endif
}

int DotNetHost::Request(const char* json, char* out, int capacity) {
  DotNetHost& h = Instance();
  if (!json) return -1;
  std::string answer;
  if (!h.lastAnswer_.empty() && h.lastRequest_ == json) { // the retry of a request whose answer did not fit
    answer = std::move(h.lastAnswer_);
    h.lastAnswer_.clear();
  } else if (!h.scene_ || !h.bus_) {
    answer = R"({"ok":false,"error":"no scene is attached to scripting"})";
  } else {
    answer = HandleScriptRequest(*h.scene_, *h.bus_, json);
  }
  if ((int)answer.size() > capacity) {
    h.lastRequest_ = json;
    h.lastAnswer_ = answer;
    return (int)answer.size();
  }
  if (out && !answer.empty()) std::memcpy(out, answer.data(), answer.size());
  return (int)answer.size();
}

} // namespace bdg
