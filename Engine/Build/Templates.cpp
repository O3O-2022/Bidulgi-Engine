#include "Build/Templates.h"
#include "Anim/TestRig.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Scripting/DotNetHost.h"
#include <yyjson.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {

std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}

fs::path ProgramDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  if (n > 0 && n < std::size(buf)) return fs::path(std::wstring(buf, n)).parent_path();
#endif
  return fs::current_path();
}

bool Fail(std::string* e, std::string why) {
  if (e) *e = std::move(why);
  return false;
}

bool WriteBytes(const fs::path& p, const void* data, size_t size) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::FILE* f = OpenU8(U(p), "wb");
  if (!f) return false;
  const bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
  return std::fclose(f) == 0 && ok;
}

std::string Replace(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

// What `dotnet build` printed, for the user when it failed.
int RunCapture(const std::wstring& cmd, std::string& out) {
#ifdef _WIN32
  std::FILE* p = _wpopen(cmd.c_str(), L"r");
  if (!p) return -1;
  char buf[512];
  while (std::fgets(buf, sizeof buf, p)) out += buf;
  return _pclose(p);
#else
  (void)cmd; (void)out;
  return -1;
#endif
}

std::string Identifier(const std::string& s) { // "My Game 2" -> "MyGame2" (an assembly name)
  std::string id;
  for (char c : s)
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') id += c;
  if (id.empty() || (id[0] >= '0' && id[0] <= '9')) id = "Game" + id;
  return id;
}
} // namespace

std::string GameTemplatesDir() {
  std::error_code ec;
  const fs::path beside = ProgramDir() / "Templates";
  if (fs::exists(beside, ec)) return U(beside);
  for (fs::path p = ProgramDir(); !p.empty(); p = p.parent_path()) { // developers: the source tree's Templates/
    if (fs::exists(p / "Templates" / "fps" / "template.json", ec)) return U(p / "Templates");
    if (p == p.parent_path()) break;
  }
  return U(beside);
}

std::vector<GameTemplateInfo> GameTemplates() {
  std::vector<GameTemplateInfo> out;
  std::error_code ec;
  for (const auto& d : fs::directory_iterator(PathU8(GameTemplatesDir()), ec)) {
    std::string text;
    if (!ReadTextU8(U(d.path() / "template.json"), text)) continue;
    GameTemplateInfo t;
    t.name = U(d.path().filename());
    if (yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0)) {
      yyjson_val* r = yyjson_doc_get_root(doc);
      if (const char* s = yyjson_get_str(yyjson_obj_get(r, "title"))) t.title = s;
      if (const char* s = yyjson_get_str(yyjson_obj_get(r, "description"))) t.description = s;
      yyjson_doc_free(doc);
    }
    out.push_back(std::move(t));
  }
  std::sort(out.begin(), out.end(), [](const GameTemplateInfo& a, const GameTemplateInfo& b) { return a.name < b.name; });
  return out;
}

bool IsGameTemplate(const std::string& name) {
  for (const GameTemplateInfo& t : GameTemplates())
    if (t.name == name) return true;
  return false;
}

bool CreateFromTemplate(const std::string& name, const std::string& dir, const std::string& gameNameIn, TemplateResult& out, std::string* error,
                        bool buildScripts) {
  const fs::path src = PathU8(GameTemplatesDir()) / PathU8(name), dst = PathU8(dir);
  std::error_code ec;
  if (!fs::exists(src / "template.json", ec)) return Fail(error, std::format("no template \"{}\" in {}", name, GameTemplatesDir()));
  if (fs::exists(dst / "Scenes" / "Main.scene", ec) || fs::exists(dst / "ProjectSettings" / "Project.json", ec))
    return Fail(error, "a project is already there: " + dir);
  const std::string gameName = gameNameIn.empty() ? U(fs::absolute(dst, ec).filename()) : gameNameIn;
  std::string managed = DotNetHost::DefaultManagedDir();
  std::replace(managed.begin(), managed.end(), '\\', '/');
  static const std::set<std::string> text = {".cs", ".md", ".json", ".jsonl", ".scene", ".csproj", ".props", ".txt", ".gitignore"};
  for (const auto& f : fs::recursive_directory_iterator(src, ec)) {
    if (!f.is_regular_file()) continue;
    const fs::path rel = fs::relative(f.path(), src);
    if (rel == "template.json") continue;
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(f.path(), bytes)) return Fail(error, "cannot read " + U(f.path()));
    std::string ext = U(rel.extension());
    if (ext.empty()) ext = U(rel.filename()); // .gitignore
    if (text.count(ext)) {
      std::string s(bytes.begin(), bytes.end());
      s = Replace(s, "{{GAME_NAME}}", gameName);
      s = Replace(s, "{{GAME_ASSEMBLY}}", Identifier(gameName));
      s = Replace(s, "{{BDG_MANAGED}}", managed);
      bytes.assign(s.begin(), s.end());
    }
    if (!WriteBytes(dst / rel, bytes.data(), bytes.size())) return Fail(error, "cannot write " + U(dst / rel));
    out.files.push_back(U(rel));
  }
  // generated sounds (CC0)
  std::string meta;
  ReadTextU8(U(src / "template.json"), meta);
  if (yyjson_doc* doc = yyjson_read(meta.data(), meta.size(), 0)) {
    size_t i, n;
    yyjson_val* v;
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc), "sounds"), i, n, v) {
      const char* rel = yyjson_get_str(v);
      if (!rel) continue;
      std::string kind = U(PathU8(rel).stem());
      std::transform(kind.begin(), kind.end(), kind.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
      std::string err;
      if (!WriteGeneratedSound(kind, U(dst / PathU8(rel)), &err)) { yyjson_doc_free(doc); return Fail(error, err); }
      out.files.push_back(rel);
    }
    // generated characters (CC0, 7-1b): "hero" = the humanoid, "rig" = the 3-joint test rig
    yyjson_arr_foreach(yyjson_obj_get(yyjson_doc_get_root(doc), "models"), i, n, v) {
      const char* rel = yyjson_get_str(v);
      if (!rel) continue;
      std::string kind = U(PathU8(rel).stem());
      std::transform(kind.begin(), kind.end(), kind.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
      const std::string gltf = kind == "hero" ? MakeHumanoidGltf() : kind == "rig" ? MakeTestRigGltf() : std::string();
      if (gltf.empty()) { yyjson_doc_free(doc); return Fail(error, "unknown generated model \"" + kind + "\" (hero, rig)"); }
      if (!WriteBytes(dst / PathU8(rel), gltf.data(), gltf.size())) { yyjson_doc_free(doc); return Fail(error, std::string("cannot write ") + rel); }
      out.files.push_back(rel);
    }
    yyjson_doc_free(doc);
  }
  for (const char* sub : {"Assets", "Scenes", "ProjectSettings"}) fs::create_directories(dst / sub, ec);
  // the C# scripts, once (the editor reloads them when they are rebuilt)
  if (buildScripts && fs::exists(dst / "Game.csproj", ec)) {
    const std::wstring cmd = L"dotnet build \"" + (dst / "Game.csproj").wstring() + L"\" -c Debug --nologo -v:q 2>&1";
    std::string log;
    const int code = RunCapture(cmd, log);
    out.scriptsBuilt = code == 0 && fs::exists(dst / ".cache" / "Scripts" / PathU8(Identifier(gameName) + ".dll"), ec);
    if (!out.scriptsBuilt) {
      // keep the last lines: the errors
      size_t cut = log.size();
      for (int lines = 0; lines < 15 && cut > 0; ++lines) cut = log.rfind('\n', cut - 1);
      out.scriptLog = code < 0 ? std::string("dotnet was not found (install the .NET 8 SDK, then: dotnet build Game.csproj)") : log.substr(cut == std::string::npos ? 0 : cut);
    }
  }
  return true;
}

// ---- generated sounds ----------------------------------------------------------------------------------------

bool WriteGeneratedSound(const std::string& kind, const std::string& path, std::string* error) {
  constexpr int rate = 44100;
  constexpr double pi = 3.14159265358979;
  double seconds = 0.2;
  if (kind == "hit") seconds = 0.35;
  else if (kind == "coin") seconds = 0.3;
  else if (kind == "goal") seconds = 0.9;
  else if (kind == "jump") seconds = 0.22;
  else if (kind != "shoot") return Fail(error, "unknown sound \"" + kind + "\" (shoot, hit, jump, coin, goal)");
  const int frames = (int)(seconds * rate);
  std::vector<int16_t> pcm((size_t)frames);
  uint32_t noise = 0x12345678u;
  double phase = 0, low = 0;
  for (int i = 0; i < frames; ++i) {
    const double t = (double)i / rate, k = t / seconds;
    noise = noise * 1664525u + 1013904223u;
    const double white = (double)(noise >> 8) / (double)(1u << 24) * 2 - 1;
    double s = 0;
    if (kind == "shoot") { // a falling zap with a little grit
      const double f = 1300 * std::pow(250.0 / 1300.0, k);
      phase += 2 * pi * f / rate;
      s = (0.7 * (std::sin(phase) > 0 ? 1 : -1) * 0.5 + 0.5 * std::sin(phase)) * std::exp(-7 * k) + 0.25 * white * std::exp(-25 * k);
    } else if (kind == "hit") { // a dull thump: low sine and filtered noise
      low += (white - low) * 0.08;
      s = 0.9 * std::sin(2 * pi * 90 * t) * std::exp(-9 * k) + 1.6 * low * std::exp(-6 * k);
    } else if (kind == "jump") { // a rising chirp
      const double f = 300 + 450 * k;
      phase += 2 * pi * f / rate;
      s = 0.8 * std::sin(phase) * std::sin(pi * std::min(k * 1.2, 1.0));
    } else if (kind == "coin") { // two quick bell notes (B5, E6)
      const double f = t < 0.08 ? 987.77 : 1318.51;
      phase += 2 * pi * f / rate;
      s = 0.6 * std::sin(phase) * std::exp(-5 * std::max(t - 0.08, 0.0) / seconds);
    } else { // goal: C5 E5 G5 C6 arpeggio
      static const double notes[] = {523.25, 659.25, 783.99, 1046.5};
      const int note = std::min((int)(k * 4), 3);
      const double local = k * 4 - note;
      phase += 2 * pi * notes[note] / rate;
      s = 0.55 * (std::sin(phase) + 0.3 * std::sin(2 * phase)) * std::exp(-2.5 * local);
    }
    const double fade = std::min(1.0, (frames - i) / (0.005 * rate)); // no click at the end
    pcm[(size_t)i] = (int16_t)std::clamp(s * fade * 20000.0, -32767.0, 32767.0);
  }
  std::vector<uint8_t> wav;
  auto u32 = [&](uint32_t v) { for (int b = 0; b < 4; ++b) wav.push_back((uint8_t)(v >> (8 * b))); };
  auto u16 = [&](uint16_t v) { wav.push_back((uint8_t)v); wav.push_back((uint8_t)(v >> 8)); };
  auto tag = [&](const char* s) { wav.insert(wav.end(), s, s + 4); };
  tag("RIFF"); u32(36 + (uint32_t)frames * 2); tag("WAVE");
  tag("fmt "); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
  tag("data"); u32((uint32_t)frames * 2);
  const auto* bytes = reinterpret_cast<const uint8_t*>(pcm.data());
  wav.insert(wav.end(), bytes, bytes + pcm.size() * 2);
  return WriteBytes(PathU8(path), wav.data(), wav.size()) || Fail(error, "cannot write " + path);
}

} // namespace bdg
