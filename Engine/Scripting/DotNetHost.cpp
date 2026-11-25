#include "Scripting/DotNetHost.h"
#include "Core/Command.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Scripting/ScriptApi.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <chrono>
#include <fstream>
#include <utility>
#include <vector>
#include <yyjson.h>
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
  void* entry[9] = {};
  const wchar_t* names[9] = {L"SelfTest", L"LoadAssembly", L"CreateBehaviour", L"SetBehaviourEnabled", L"DestroyBehaviour", L"TickBehaviours",
                             L"SetBehaviourFields", L"GetBehaviourFields", L"ReloadAssembly"};
  int rl = load(dll.c_str(), L"Bidulgi.Host, Bidulgi.Scripting", L"Initialize", kUnmanagedCallersOnly, nullptr, &initialize);
  for (int i = 0; i < 9 && rl >= 0; ++i) {
    rl = load(dll.c_str(), L"Bidulgi.Host, Bidulgi.Scripting", names[i], kUnmanagedCallersOnly, nullptr, &entry[i]);
    if (rl >= 0 && !entry[i]) rl = -1;
  }
  if (rl < 0 || !initialize)
    return fail(std::format("Bidulgi.Scripting의 진입점을 찾을 수 없음 (0x{:08x}) {}", (uint32_t)rl, g_hostError));
  const int api = ((int(__stdcall*)(void*))initialize)((void*)&DotNetHost::Request);
  if (api != 4) return fail(std::format("스크립팅 API 버전이 다름 ({} != 4)", api));
  loadAssembly_ = (int (*)(const char*))entry[1];
  create_ = (int (*)(const char*, const char*, int, const char*))entry[2];
  setEnabled_ = (void (*)(int, int))entry[3];
  destroy_ = (void (*)(int))entry[4];
  tick_ = (void (*)(int, double, double, int))entry[5];
  setFields_ = (void (*)(int, const char*))entry[6];
  getFields_ = (int (*)(int, char*, int))entry[7];
  reload_ = (int (*)(const char*))entry[8];
  selfTest_ = (int (*)(const char*))entry[0];
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

int DotNetHost::SelfTest(const std::string& target) { return selfTest_ ? selfTest_(target.c_str()) : -2; }

namespace {
// One storage shape for every C# Behaviour component (4-3): "enabled", then the class's [Edit] fields packed into
// `raw` (numbers, vectors, enums as int32) and `str` (strings). TypeInfo::create is a plain function pointer, so
// each registered class gets one of kMaxScriptTypes creator functions (a slot) that copies its default values.
constexpr size_t kScriptRawBytes = 1024, kScriptStrings = 16, kMaxScriptTypes = 256;
struct ScriptComponentData {
  bool enabled = true;
  union { // 8-byte aligned without alignas (which would pad the struct and warn)
    double words[kScriptRawBytes / 8] = {};
    unsigned char raw[kScriptRawBytes];
  };
  std::string str[kScriptStrings];
};
struct ScriptDefaults {
  unsigned char raw[kScriptRawBytes] = {};
  std::string str[kScriptStrings];
};
std::deque<ScriptDefaults>& Defaults() {
  static std::deque<ScriptDefaults> d;
  return d;
}
template <size_t N> void* CreateScriptData() {
  auto* p = new ScriptComponentData();
  if (N < Defaults().size()) {
    std::memcpy(p->raw, Defaults()[N].raw, kScriptRawBytes);
    for (size_t i = 0; i < kScriptStrings; ++i) p->str[i] = Defaults()[N].str[i];
  }
  return p;
}
template <size_t... I> constexpr auto MakeCreators(std::index_sequence<I...>) {
  return std::array<void* (*)(), sizeof...(I)>{&CreateScriptData<I>...};
}
constexpr auto kCreators = MakeCreators(std::make_index_sequence<kMaxScriptTypes>{});
} // namespace

// [Edit] fields: {"name","type":"bool|int|float|double|string|vec2|vec3|vec4|enum","default":..,"min","max",
// "tooltip","header","values":[["Name",int],..]}. Unsupported types were skipped (with a warning) on the C# side.
void DotNetHost::BuildScriptFields(TypeInfo& t, void* defsRef, yyjson_val* fields) {
  ScriptDefaults& defs = *static_cast<ScriptDefaults*>(defsRef);
  const std::string& name = t.name;
  size_t at = 0, strings = 0;
  size_t i, n;
  yyjson_val* fv;
  yyjson_arr_foreach(fields, i, n, fv) {
    auto str = [&](const char* k) { const char* s = yyjson_get_str(yyjson_obj_get(fv, k)); return std::string(s ? s : ""); };
    const std::string fname = str("name"), type = str("type");
    FieldInfo x;
    x.name = fname;
    x.flags = kEdit | kSave;
    x.meta.tooltip = str("tooltip");
    x.meta.header = str("header");
    if (yyjson_val* mn = yyjson_obj_get(fv, "min"), *mx = yyjson_obj_get(fv, "max"); yyjson_is_num(mn) && yyjson_is_num(mx)) {
      x.meta.hasRange = true;
      x.meta.rangeMin = yyjson_get_num(mn);
      x.meta.rangeMax = yyjson_get_num(mx);
    }
    size_t bytes = 0;
    if (type == "bool") { x.value = reflect::ValueTypeOf<bool>(); bytes = 1; }
    else if (type == "int") { x.value = reflect::ValueTypeOf<int32_t>(); bytes = 4; }
    else if (type == "float") { x.value = reflect::ValueTypeOf<float>(); bytes = 4; }
    else if (type == "double") { x.value = reflect::ValueTypeOf<double>(); bytes = 8; }
    else if (type == "vec2") { x.value = reflect::ValueTypeOf<Vec2>(); bytes = sizeof(Vec2); }
    else if (type == "vec3") { x.value = reflect::ValueTypeOf<Vec3>(); bytes = sizeof(Vec3); }
    else if (type == "vec4") { x.value = reflect::ValueTypeOf<Vec4>(); bytes = sizeof(Vec4); }
    else if (type == "enum") {
      EnumInfo& e = enums_.emplace_back();
      e.name = name + "." + fname;
      size_t j, m;
      yyjson_val* pair;
      yyjson_arr_foreach(yyjson_obj_get(fv, "values"), j, m, pair)
        e.values.push_back({yyjson_get_str(yyjson_arr_get(pair, 0)) ? yyjson_get_str(yyjson_arr_get(pair, 0)) : "?",
                            yyjson_get_sint(yyjson_arr_get(pair, 1))});
      x.value.type = FieldType::Enum;
      x.value.size = 4;
      x.value.enumType = &e;
      bytes = 4;
    } else if (type == "string") {
      if (strings >= kScriptStrings) { LogWarn("C#: {}.{}: more than {} string fields (not shown)", name, fname, kScriptStrings); continue; }
      x.value = reflect::ValueTypeOf<std::string>();
      x.offset = offsetof(ScriptComponentData, str) + sizeof(std::string) * strings;
      defs.str[strings++] = yyjson_get_str(yyjson_obj_get(fv, "default")) ? yyjson_get_str(yyjson_obj_get(fv, "default")) : "";
      t.fields.push_back(x);
      continue;
    } else { LogWarn("C#: {}.{}: type {} cannot be shown yet", name, fname, type); continue; }
    at = (at + std::min<size_t>(bytes, 8) - 1) / std::min<size_t>(bytes, 8) * std::min<size_t>(bytes, 8); // natural alignment
    if (at + bytes > kScriptRawBytes) { LogWarn("C#: {}: [Edit] fields beyond {} bytes are not shown", name, kScriptRawBytes); break; }
    x.offset = offsetof(ScriptComponentData, raw) + at;
    if (yyjson_val* d = yyjson_obj_get(fv, "default")) { // default value into the slot's template
      char* text = yyjson_val_write(d, 0, nullptr);
      std::string err;
      if (text && !Registry::FromJson(defs.raw + at, x.value, text, &err)) LogWarn("C#: {}.{} default: {}", name, fname, err);
      std::free(text);
    }
    at += bytes;
    t.fields.push_back(x);
  }
}

std::string DotNetHost::RegisterBehaviourType(const std::string& name, yyjson_val* fields) {
  if (name.empty()) return "no name";
  if (const TypeInfo* found = Registry::Instance().Find(name)) {
    if (!behaviourTypes_.count(found)) return std::format("\"{}\" is already an engine type; rename the C# class", name);
    // Loaded again (hot reload, 4-4): the fields may have changed. Rebuild them in place (same TypeInfo, same storage
    // shape) and carry every existing component's values over by field name; removed fields drop, new ones get the
    // C# default.
    TypeInfo& t = const_cast<TypeInfo&>(*found);
    const size_t slot = slots_.at(found);
    std::vector<std::pair<Component*, std::string>> kept;
    if (scene_) {
      std::vector<std::string> stack(scene_->Roots().begin(), scene_->Roots().end());
      while (!stack.empty()) {
        SceneObject* o = scene_->Find(stack.back());
        stack.pop_back();
        if (!o) continue;
        stack.insert(stack.end(), o->children.begin(), o->children.end());
        for (Component& c : o->components)
          if (c.type == found) kept.push_back({&c, FieldsJson(c.data, t)});
      }
    }
    Defaults()[slot] = ScriptDefaults{};
    t.fields.resize(1); // "enabled" stays
    BuildScriptFields(t, &Defaults()[slot], fields);
    for (auto& [c, before] : kept) {
      auto* d = static_cast<ScriptComponentData*>(c->data);
      const bool enabled = d->enabled;
      std::memcpy(d->raw, Defaults()[slot].raw, kScriptRawBytes);
      for (size_t i = 0; i < kScriptStrings; ++i) d->str[i] = Defaults()[slot].str[i];
      d->enabled = enabled;
      if (yyjson_doc* doc = yyjson_read(before.data(), before.size(), 0)) {
        yyjson_val* root = yyjson_doc_get_root(doc);
        size_t i, n;
        yyjson_val *k, *v;
        yyjson_obj_foreach(root, i, n, k, v) {
          const char* key = yyjson_get_str(k);
          if (!key || std::strcmp(key, "enabled") == 0) continue;
          char* text = yyjson_val_write(v, 0, nullptr);
          if (text && t.FindField(key)) Registry::Set(c->data, t, key, text); // a changed type keeps the default
          std::free(text);
        }
        yyjson_doc_free(doc);
      }
    }
    if (!kept.empty()) LogInfo("C# hot reload: {} ({} component(s) kept their values)", name, kept.size());
    return {};
  }
  if (Defaults().size() >= kMaxScriptTypes) return std::format("more than {} C# Behaviour classes", kMaxScriptTypes);
  const size_t slot = Defaults().size();
  ScriptDefaults& defs = Defaults().emplace_back();
  TypeInfo& t = types_.emplace_back();
  t.name = name;
  t.size = sizeof(ScriptComponentData);
  t.tags = {"Component", "Script", "CSharp"};
  FieldInfo f;
  f.name = "enabled";
  f.value = reflect::ValueTypeOf<bool>();
  f.offset = offsetof(ScriptComponentData, enabled);
  f.flags = kEdit | kSave;
  f.meta.tooltip = "Runs the behaviour's Update, FixedUpdate and LateUpdate (OnEnable / OnDisable when it changes)";
  t.fields.push_back(f);
  BuildScriptFields(t, &defs, fields);
  slots_[&t] = slot;
  t.create = kCreators[slot];
  t.destroy = [](void* p) { delete static_cast<ScriptComponentData*>(p); };
  Registry::Instance().Add(t);
  behaviourTypes_.insert(&t);
  return {};
}

std::string DotNetHost::FieldsJson(const void* data, const TypeInfo& type) {
  std::string out = "{";
  for (const FieldInfo& f : type.fields) {
    if (out.size() > 1) out += ",";
    out += "\"" + f.name + "\":" + Registry::ToJson(static_cast<const unsigned char*>(data) + f.offset, f.value);
  }
  return out + "}";
}

void DotNetHost::SetBehaviourFields(int handle, const std::string& json) {
  if (setFields_) setFields_(handle, json.c_str());
}

std::string DotNetHost::GetBehaviourFields(int handle) {
  if (!getFields_) return {};
  std::string buf(4096, '\0');
  int len = getFields_(handle, buf.data(), (int)buf.size());
  if (len > (int)buf.size()) { // too long for the first buffer: ask again with room for it
    buf.assign((size_t)len, '\0');
    len = getFields_(handle, buf.data(), (int)buf.size());
  }
  if (len <= 0 || len > (int)buf.size()) return {};
  buf.resize((size_t)len);
  return buf;
}

int DotNetHost::ReloadScripts(const std::string& dllPath, Scene* scene, CommandBus* bus, std::string* error) {
  if (!reload_) {
    if (error) *error = "C# scripting is not running";
    return -1;
  }
  Scene* oldScene = scene_;
  CommandBus* oldBus = bus_;
  if (scene) Attach(scene, bus); // registration carries the scene's component values over to changed fields
  const auto t0 = std::chrono::steady_clock::now();
  const int n = reload_(dllPath.c_str());
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (scene) Attach(oldScene, oldBus);
  if (n < 0) {
    if (error) *error = "cannot reload " + dllPath + " (see the log)";
    return -1;
  }
  ++reloads_;
  lastReloadMs_ = ms;
  LogInfo("C# hot reload: {} class(es) from {} in {:.0f} ms", n, dllPath, ms);
  return n;
}

int DotNetHost::LoadScripts(const std::string& dllPath, std::string* error) {
  if (!loadAssembly_) {
    if (error) *error = "C# scripting is not running";
    return -1;
  }
  const int n = loadAssembly_(dllPath.c_str());
  if (n < 0 && error) *error = "cannot load " + dllPath + " (see the log)";
  if (n >= 0) LogInfo("C# scripting: {} behaviour class(es) from {}", n, dllPath);
  return n;
}

int DotNetHost::CreateBehaviour(const std::string& objectId, const std::string& type, bool enabled, const std::string& fieldsJson) {
  return create_ ? create_(objectId.c_str(), type.c_str(), enabled ? 1 : 0, fieldsJson.c_str()) : 0;
}
void DotNetHost::SetBehaviourEnabled(int handle, bool enabled) {
  if (setEnabled_) setEnabled_(handle, enabled ? 1 : 0);
}
void DotNetHost::DestroyBehaviour(int handle) {
  if (destroy_) destroy_(handle);
}
void DotNetHost::TickBehaviours(Phase phase, double dt, double time, int frame) {
  if (tick_) tick_((int)phase, dt, time, frame);
}

int DotNetHost::Request(const char* json, char* out, int capacity) {
  DotNetHost& h = Instance();
  if (!json) return -1;
  std::string answer;
  if (std::strncmp(json, R"({"op":"register")", 16) == 0) { // a Behaviour class (no scene needed)
    std::string name, why = "bad request";
    if (yyjson_doc* d = yyjson_read(json, std::strlen(json), 0)) {
      yyjson_val* root = yyjson_doc_get_root(d);
      const char* n = yyjson_get_str(yyjson_obj_get(root, "name"));
      name = n ? n : "";
      why = h.RegisterBehaviourType(name, yyjson_obj_get(root, "fields"));
      yyjson_doc_free(d);
    }
    answer = why.empty() ? R"({"ok":true,"value":null})" : std::format(R"({{"ok":false,"error":"{}"}})", why);
  } else if (!h.lastAnswer_.empty() && h.lastRequest_ == json) { // the retry of a request whose answer did not fit
    answer = std::move(h.lastAnswer_);
    h.lastAnswer_.clear();
  } else if (!h.scene_ && (std::strncmp(json, R"({"op":"log")", 11) == 0 || std::strncmp(json, R"({"op":"table")", 13) == 0)) {
    // logging and data tables need no scene
    static Scene none;
    static CommandBus noBus(none);
    answer = HandleScriptRequest(none, noBus, json);
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
