#pragma once
// Paths held in std::string (queue 2-7 fix). The engine keeps paths as UTF-8, but on Windows the narrow C file
// functions read a std::string in the system code page, so a project under a Korean folder name was written to a
// garbled path. These helpers read a valid UTF-8 string as UTF-8; anything else (a string that came from
// std::filesystem::path::string(), which is in the system code page) the old way, so both kinds keep working.
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace bdg {
bool IsUtf8(std::string_view s);
std::filesystem::path PathU8(std::string_view s);
std::string PathToU8(const std::filesystem::path& p); // the path as UTF-8 (never throws, unlike path::string())
std::FILE* OpenU8(std::string_view path, const char* mode); // fopen with the path read as above
bool ReadTextU8(std::string_view path, std::string& text);  // whole file; false if it cannot be opened
}
