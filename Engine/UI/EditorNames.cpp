#include "UI/EditorNames.h"
#include "Core/Utf8Path.h"
#include <yyjson.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::mutex g_mutex;
std::string g_file;
bool g_loaded = false;
std::map<std::string, std::string, std::less<>> g_names;

fs::path FindTable() { // design-system/project/editor.ko.json above the exe (repo builds, Bin/) or the working folder
  std::vector<fs::path> starts;
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  if (n) starts.push_back(fs::path(std::wstring(buf, n)).parent_path());
#endif
  std::error_code ec;
  starts.push_back(fs::current_path(ec));
  for (fs::path p : starts)
    for (; !p.empty(); p = p.parent_path()) {
      const fs::path f = p / "design-system" / "project" / "editor.ko.json";
      if (fs::exists(f, ec)) return f;
      if (p == p.parent_path()) break;
    }
  return {};
}

void Load() {
  g_loaded = true;
  g_names.clear();
  const fs::path file = g_file.empty() ? FindTable() : PathU8(g_file);
  if (file.empty()) return;
  std::ifstream in(file, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
  yyjson_val* names = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), "names") : nullptr;
  size_t i, max;
  yyjson_val *k, *v;
  yyjson_obj_foreach(names, i, max, k, v)
    if (yyjson_is_str(v)) g_names.emplace(yyjson_get_str(k), yyjson_get_str(v));
  yyjson_doc_free(doc);
}
} // namespace

const std::string& KoreanName(std::string_view key) {
  static const std::string none;
  std::lock_guard lock(g_mutex);
  if (!g_loaded) Load();
  auto it = g_names.find(key);
  return it == g_names.end() ? none : it->second;
}

std::string TermTooltip(std::string_view key, const std::string& description) {
  const std::string& ko = KoreanName(key);
  if (ko.empty()) return description;
  return description.empty() ? ko : ko + "\n" + description;
}

void SetKoreanNamesFile(const std::string& path) {
  std::lock_guard lock(g_mutex);
  g_file = path;
  g_loaded = false;
}
} // namespace bdg
