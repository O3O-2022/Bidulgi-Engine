#include "Core/Image.h"
#include "Core/DDS.h"
#include <cstring>
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

bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes) {
  bytes.clear();
  std::FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return false;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
  std::fclose(f);
  return true;
}

bool LoadImageRGBA(const std::filesystem::path& path, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> bytes;
  return ReadFileBytes(path, bytes) && DecodeImageRGBA(bytes.data(), bytes.size(), w, h, rgba);
}

bool ImageSize(const std::filesystem::path& path, int32_t& w, int32_t& h) {
  std::vector<uint8_t> bytes;
  int comp = 0;
  if (!ReadFileBytes(path, bytes) || bytes.empty() || bytes.size() > 0x7fffffff) return false;
  if (IsDDS(bytes.data(), bytes.size())) {
    uint32_t hd[3] = {};
    if (bytes.size() < 4 + 16) return false;
    std::memcpy(hd, bytes.data() + 8, sizeof(hd)); // flags, height, width
    h = (int32_t)hd[1];
    w = (int32_t)hd[2];
    return w > 0 && h > 0;
  }
  return stbi_info_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp) != 0;
}

bool DecodeImageRGBA(const uint8_t* data, size_t size, int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) {
  if (!data || size == 0 || size > 0x7fffffff) return false;
  if (IsDDS(data, size)) {
    DdsImage dds;
    return ParseDDS(data, size, dds) && DecodeDDS(dds, 0, rgba, w, h);
  }
  int comp = 0;
  stbi_uc* px = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
  if (!px) return false;
  rgba.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return true;
}
}
