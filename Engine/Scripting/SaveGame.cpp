#include "Scripting/SaveGame.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Project/ProjectSettings.h"
#include <yyjson.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <system_error>

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}

// An environment variable as UTF-8 (Windows: the wide one, so a Korean user folder stays right).
bool Env(const std::string& name, std::string& out) {
#ifdef _WIN32
  const std::wstring wname = PathU8(name).wstring();
  wchar_t* v = nullptr;
  size_t len = 0;
  if (_wdupenv_s(&v, &len, wname.c_str()) != 0 || !v) return false;
  std::unique_ptr<wchar_t, decltype(&std::free)> hold(v, std::free);
  const std::u8string s = fs::path(v).u8string();
  out.assign(s.begin(), s.end());
  return true;
#else
  const char* v = std::getenv(name.c_str());
  if (!v) return false;
  out = v;
  return true;
#endif
}

std::string FolderSafe(std::string_view s) {
  std::string o;
  for (char c : s)
    if (std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos &&(unsigned char)c >= 0x20) o += c;
  while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
  return o.empty() ? std::string("_") : o;
}

std::string Quote(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += std::format("\\u{:04x}", (unsigned)(unsigned char)c);
    else o += c;
  }
  return o + "\"";
}

bool Fail(std::string* error, std::string why) {
  if (error) *error = std::move(why);
  return false;
}

// Reads one save file: true and the data object when it is a good save of a version this engine knows.
bool ReadFile(const fs::path& p, std::string& data, std::string& why) {
  std::string text;
  if (!ReadTextU8(U(p), text)) { why = "cannot be opened"; return false; }
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(text.data(), text.size(), 0), yyjson_doc_free);
  yyjson_val* root = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
  if (!yyjson_is_obj(root)) { why = "damaged (not JSON)"; return false; }
  const char* format = yyjson_get_str(yyjson_obj_get(root, "format"));
  if (!format || std::string_view(format) != "bidulgi-save") { why = "not a Bidulgi save"; return false; }
  const int version = (int)yyjson_get_int(yyjson_obj_get(root, "version"));
  if (version < 1 || version > kSaveFormatVersion) {
    why = std::format("save format version {} (this game reads up to {}; it was written by a newer version)", version, kSaveFormatVersion);
    return false;
  }
  yyjson_val* d = yyjson_obj_get(root, "data");
  if (!yyjson_is_obj(d)) { why = "damaged (no data)"; return false; }
  char* s = yyjson_val_write(d, 0, nullptr);
  data = s ? s : "{}";
  std::free(s);
  return true;
}
} // namespace

std::string ExpandSaveFolder(std::string_view pattern, std::string_view company, std::string_view name) {
  std::string s(pattern);
  auto replaceAll = [&](std::string_view from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  };
  replaceAll("{company}", FolderSafe(company));
  replaceAll("{name}", FolderSafe(name));
  for (size_t at = s.find('%'); at != std::string::npos;) { // %VAR%
    const size_t end = s.find('%', at + 1);
    if (end == std::string::npos) break;
    std::string value;
    if (end > at + 1 && Env(s.substr(at + 1, end - at - 1), value)) {
      s.replace(at, end - at + 1, value);
      at = s.find('%', at + value.size());
    } else {
      at = s.find('%', end + 1);
    }
  }
  if (s.starts_with("~/")) {
    std::string home;
    if (Env("HOME", home) || Env("USERPROFILE", home)) s = home + s.substr(1);
  }
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

std::string GameSaveFolder(const ProjectSettings& settings) {
  std::string over;
  if (Env("BDG_SAVE_DIR", over) && !over.empty()) {
    std::replace(over.begin(), over.end(), '\\', '/');
    return over;
  }
  return ExpandSaveFolder(settings.saves.saveFolder, settings.description.company, settings.description.name);
}

std::string GameSaveFolder() {
  ProjectSettings settings;
  std::error_code ec;
  const fs::path file = fs::current_path(ec) / "ProjectSettings" / "Project.json";
  if (fs::exists(file, ec)) LoadProjectSettings(U(file), settings);
  return GameSaveFolder(settings);
}

bool SaveSlotNameOk(std::string_view slot) {
  if (slot.empty() || slot.size() > 64 || slot == "." || slot == "..") return false;
  for (char c : slot)
    if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || (unsigned char)c < 0x20) return false;
  return true;
}

std::string SaveSlotPath(const std::string& folder, const std::string& slot) { return U(PathU8(folder) / PathU8(slot + ".save.json")); }

bool WriteSaveSlot(const std::string& folder, const std::string& slot, std::string_view dataJson, const ProjectSettings& game, std::string* error) {
  if (!SaveSlotNameOk(slot)) return Fail(error, std::format("\"{}\" is not a save slot name (letters, digits, - and _; up to 64)", slot));
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(dataJson.data(), dataJson.size(), 0), yyjson_doc_free);
  if (!doc || !yyjson_is_obj(yyjson_doc_get_root(doc.get()))) return Fail(error, "save data must be a JSON object");
  std::error_code ec;
  const fs::path dir = PathU8(folder);
  fs::create_directories(dir, ec);
  if (!fs::is_directory(dir, ec)) return Fail(error, std::format("cannot make the save folder {}", folder));
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char when[32];
  std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &utc);
  const std::string text = std::format("{{\"format\":\"bidulgi-save\",\"version\":{},\"game\":{},\"gameVersion\":{},\"slot\":{},\"saved\":\"{}\",\"data\":{}}}\n",
                                       kSaveFormatVersion, Quote(game.description.name), Quote(game.description.version), Quote(slot), when, dataJson);
  const fs::path file = dir / PathU8(slot + ".save.json"), tmp = dir / PathU8(slot + ".save.json.tmp"), bak = dir / PathU8(slot + ".save.json.bak");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    if (!out) return Fail(error, std::format("cannot write {}", U(tmp)));
  }
  if (fs::exists(file, ec)) fs::copy_file(file, bak, fs::copy_options::overwrite_existing, ec); // the last good save
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return Fail(error, std::format("cannot replace {}", U(file)));
  }
  return true;
}

bool ReadSaveSlot(const std::string& folder, const std::string& slot, std::string& dataJson, std::string* error) {
  if (error) error->clear();
  if (!SaveSlotNameOk(slot)) return Fail(error, std::format("\"{}\" is not a save slot name", slot));
  std::error_code ec;
  const fs::path file = PathU8(SaveSlotPath(folder, slot)), bak = PathU8(SaveSlotPath(folder, slot) + ".bak");
  const bool hasFile = fs::exists(file, ec), hasBak = fs::exists(bak, ec);
  if (!hasFile && !hasBak) return false; // no save yet
  std::string why;
  if (hasFile && ReadFile(file, dataJson, why)) return true;
  std::string bakWhy;
  if (hasBak && ReadFile(bak, dataJson, bakWhy)) {
    LogWarn("save: {} is {}; read the previous save (.bak) instead", U(file), hasFile ? why : "missing");
    return true;
  }
  return Fail(error, std::format("save \"{}\": {}", slot, hasFile ? why : bakWhy));
}

std::vector<std::string> ListSaveSlots(const std::string& folder) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& f : fs::directory_iterator(PathU8(folder), ec)) {
    const std::string n = U(f.path().filename());
    if (n.size() > 10 && n.ends_with(".save.json")) out.push_back(n.substr(0, n.size() - 10));
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool DeleteSaveSlot(const std::string& folder, const std::string& slot) {
  if (!SaveSlotNameOk(slot)) return false;
  std::error_code ec;
  const bool a = fs::remove(PathU8(SaveSlotPath(folder, slot)), ec);
  const bool b = fs::remove(PathU8(SaveSlotPath(folder, slot) + ".bak"), ec);
  return a || b;
}
} // namespace bdg
