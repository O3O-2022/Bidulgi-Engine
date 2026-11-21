#pragma once
#include <cstdint>

namespace bdg {
// Writes an RGBA8 image as PNG (uncompressed deflate; no external dependency).
// flipY = true for bottom-up rows such as OpenGL read-backs.
bool WritePNG(const char* path, int32_t w, int32_t h, const uint8_t* rgba, bool flipY);
}
