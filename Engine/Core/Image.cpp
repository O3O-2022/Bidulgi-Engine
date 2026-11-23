#include "Core/Image.h"
#include <cstdio>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0) // third-party implementation: keep /W4 clean for engine code only
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace bdg {
bool EncodePNG(int32_t w, int32_t h, const uint8_t* rgba, bool flipY, std::vector<uint8_t>& out) {
  out.clear();
  stbi_flip_vertically_on_write(flipY ? 1 : 0);
  auto sink = [](void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    v->insert(v->end(), (const uint8_t*)data, (const uint8_t*)data + size);
  };
  int ok = stbi_write_png_to_func(sink, &out, w, h, 4, rgba, w * 4);
  stbi_flip_vertically_on_write(0);
  return ok != 0;
}

bool WritePNG(const char* path, int32_t w, int32_t h, const uint8_t* rgba, bool flipY) {
  std::vector<uint8_t> png;
  if (!EncodePNG(w, h, rgba, flipY, png)) return false;
  std::FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  std::fclose(f);
  return ok;
}
}
