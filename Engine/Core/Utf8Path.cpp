#include "Core/Utf8Path.h"

namespace bdg {

bool IsUtf8(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = (unsigned char)s[i];
    const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
    if (extra < 0) return false;
    for (int k = 1; k <= extra; ++k)
      if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 0x2) return false;
    i += (size_t)extra + 1;
  }
  return true;
}

std::string PathToU8(const std::filesystem::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::filesystem::path PathU8(std::string_view s) {
  if (IsUtf8(s)) return std::filesystem::path(std::u8string((const char8_t*)s.data(), s.size()));
  return std::filesystem::path(std::string(s)); // system code page
}

std::FILE* OpenU8(std::string_view path, const char* mode) {
#ifdef _WIN32
  std::wstring wmode;
  for (const char* m = mode; *m; ++m) wmode += (wchar_t)*m;
  return _wfopen(PathU8(path).c_str(), wmode.c_str());
#else
  return std::fopen(std::string(path).c_str(), mode);
#endif
}

bool ReadTextU8(std::string_view path, std::string& text) {
  std::FILE* f = OpenU8(path, "rb");
  if (!f) return false;
  text.clear();
  char buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  return true;
}

} // namespace bdg
