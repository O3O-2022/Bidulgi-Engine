#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace bdg {
// RGBA8 image -> compressed PNG (stb_image_write, deflate + row filters).
// flipY = true for bottom-up rows such as OpenGL read-backs.
bool EncodePNG(int32_t w, int32_t h, const uint8_t* rgba, bool flipY, std::vector<uint8_t>& out);
bool WritePNG(const char* path, int32_t w, int32_t h, const uint8_t* rgba, bool flipY);
// PNG / JPEG / TGA / BMP / PSD / HDR -> RGBA8 (stb_image), top row first. Unicode paths work.
bool LoadImageRGBA(const std::filesystem::path& path, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba);
bool DecodeImageRGBA(const uint8_t* data, size_t size, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba); // same, from memory
bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out); // whole file; Unicode paths work
}
