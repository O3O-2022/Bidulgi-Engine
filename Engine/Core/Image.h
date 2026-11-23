#pragma once
#include <cstdint>
#include <vector>

namespace bdg {
// RGBA8 image -> compressed PNG (stb_image_write, deflate + row filters).
// flipY = true for bottom-up rows such as OpenGL read-backs.
bool EncodePNG(int32_t w, int32_t h, const uint8_t* rgba, bool flipY, std::vector<uint8_t>& out);
bool WritePNG(const char* path, int32_t w, int32_t h, const uint8_t* rgba, bool flipY);
}
