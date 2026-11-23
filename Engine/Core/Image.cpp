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
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>
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

bool LoadImageRGBA(const std::filesystem::path& path, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) {
  std::FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return false;
  std::vector<uint8_t> bytes;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
  std::fclose(f);
  int comp = 0;
  stbi_uc* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
  if (!px) return false;
  rgba.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return true;
}
}
