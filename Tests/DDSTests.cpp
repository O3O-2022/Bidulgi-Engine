// DDS textures (Core/DDS.h): legacy and DX10 headers, BC1-BC7 and uncompressed layouts, mips, files cut short,
// refused files; DDS through the image loader, OBJ / FBX import (kept as blocks) and the texture cooker.
// Every file is made byte by byte here (no binary test files).
#include "bdg_test.h"
#include "Assets/AssetDatabase.h"
#include "Assets/ModelImport.h"
#include "Assets/TextureCooker.h"
#include "Core/DDS.h"
#include "Core/Image.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
uint32_t CC(const char* s) { return (uint32_t)(uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8 | (uint32_t)(uint8_t)s[2] << 16 | (uint32_t)(uint8_t)s[3] << 24; }

struct DdsSpec {
  int w = 4, h = 4, mipCount = 1;
  uint32_t fourCC = 0, dxgi = 0;     // dxgi != 0: DX10 header
  uint32_t bits = 0, r = 0, g = 0, b = 0, a = 0; // uncompressed masks (fourCC 0, dxgi 0)
  bool volume = false;
};
std::vector<uint8_t> MakeDDS(const DdsSpec& s, const std::vector<uint8_t>& payload) {
  uint32_t hd[31] = {};
  hd[0] = 124;
  hd[1] = 0x1 | 0x2 | 0x4 | 0x1000 | (s.mipCount > 1 ? 0x20000 : 0) | (s.volume ? 0x800000 : 0);
  hd[2] = (uint32_t)s.h;
  hd[3] = (uint32_t)s.w;
  hd[5] = s.volume ? 4 : 0;
  hd[6] = (uint32_t)s.mipCount;
  hd[18] = 32;
  if (s.dxgi) {
    hd[19] = 0x4;
    hd[20] = CC("DX10");
  } else if (s.fourCC) {
    hd[19] = 0x4;
    hd[20] = s.fourCC;
  } else {
    hd[19] = 0x40 | (s.a ? 0x1 : 0);
    hd[21] = s.bits;
    hd[22] = s.r;
    hd[23] = s.g;
    hd[24] = s.b;
    hd[25] = s.a;
  }
  hd[26] = 0x1000;
  hd[27] = s.volume ? 0x200000 : 0;
  std::vector<uint8_t> out = {'D', 'D', 'S', ' '};
  const uint8_t* p = (const uint8_t*)hd;
  out.insert(out.end(), p, p + sizeof(hd));
  if (s.dxgi) {
    const uint32_t dx10[5] = {s.dxgi, 3, 0, 1, 0};
    const uint8_t* q = (const uint8_t*)dx10;
    out.insert(out.end(), q, q + sizeof(dx10));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}
// One BC1 block: two 5:6:5 colours and 2-bit indices (all the same).
std::vector<uint8_t> Bc1(uint16_t c0, uint16_t c1, uint8_t index) {
  const uint8_t ix = (uint8_t)(index | index << 2 | index << 4 | index << 6);
  return {(uint8_t)(c0 & 0xff), (uint8_t)(c0 >> 8), (uint8_t)(c1 & 0xff), (uint8_t)(c1 >> 8), ix, ix, ix, ix};
}
// One BC4 block where every texel takes endpoint 0.
std::vector<uint8_t> Bc4(uint8_t v) { return {v, v, 0, 0, 0, 0, 0, 0}; }
std::vector<uint8_t> Cat(std::initializer_list<std::vector<uint8_t>> parts) {
  std::vector<uint8_t> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}
bool Decode(const std::vector<uint8_t>& file, std::vector<uint8_t>& rgba, DdsImage* keep = nullptr, int mip = 0) {
  DdsImage img;
  int32_t w = 0, h = 0;
  if (!ParseDDS(file.data(), file.size(), img)) return false;
  if (keep) *keep = img;
  return DecodeDDS(img, mip, rgba, w, h);
}
void WriteBytes(const fs::path& p, const std::vector<uint8_t>& b) {
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)b.data(), (std::streamsize)b.size());
}
}

TEST_CASE("DDS: legacy and DX10 headers, BC1-BC7, uncompressed, mips, refused files (Bistro textures)") {
  std::vector<uint8_t> px;
  DdsImage img;
  // BC1 (DXT1): 4-colour block red; 3-colour block index 3 = transparent black (alpha-tested foliage)
  CHECK(Decode(MakeDDS({.fourCC = CC("DXT1")}, Bc1(0xF800, 0x001F, 0)), px, &img) && img.format == DdsFormat::BC1 && !img.srgb);
  CHECK(px.size() == 64 && px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);
  CHECK(Decode(MakeDDS({.fourCC = CC("DXT1")}, Bc1(0x001F, 0xF800, 3)), px) && px[3] == 0 && px[63] == 0);
  // BC2 (DXT3): explicit 4-bit alpha 8 -> 136, colour green
  CHECK(Decode(MakeDDS({.fourCC = CC("DXT3")}, Cat({std::vector<uint8_t>(8, 0x88), Bc1(0x07E0, 0, 0)})), px, &img) && img.format == DdsFormat::BC2);
  CHECK(px[3] == 136 && px[1] == 255 && px[0] == 0);
  // BC3 (DXT5): alpha endpoints 200 / 200, colour blue
  CHECK(Decode(MakeDDS({.fourCC = CC("DXT5")}, Cat({{200, 200, 0, 0, 0, 0, 0, 0}, Bc1(0x001F, 0, 0)})), px, &img) && img.format == DdsFormat::BC3);
  CHECK(px[3] == 200 && px[2] == 255 && px[0] == 0);
  // BC4 (ATI1, and DX10 80): grey; BC5 (ATI2, DX10 83): X and Y in red and green (normal maps)
  CHECK(Decode(MakeDDS({.fourCC = CC("ATI1")}, Bc4(128)), px, &img) && img.format == DdsFormat::BC4 && px[0] == 128 && px[1] == 128);
  CHECK(Decode(MakeDDS({.dxgi = 80}, Bc4(77)), px, &img) && img.format == DdsFormat::BC4 && px[0] == 77);
  CHECK(Decode(MakeDDS({.fourCC = CC("ATI2")}, Cat({Bc4(64), Bc4(192)})), px, &img) && img.format == DdsFormat::BC5 && px[0] == 64 && px[1] == 192);
  CHECK(Decode(MakeDDS({.dxgi = 83}, Cat({Bc4(10), Bc4(250)})), px) && px[0] == 10 && px[1] == 250);
  // DX10 sRGB flags; BC1 in a DX10 file
  CHECK(Decode(MakeDDS({.dxgi = 72}, Bc1(0xF800, 0, 0)), px, &img) && img.format == DdsFormat::BC1 && img.srgb && px[0] == 255);
  // uncompressed: RGBA8 (DX10 28), BGRA8 masks, BGR 24-bit
  CHECK(Decode(MakeDDS({.w = 1, .h = 1, .dxgi = 28}, {1, 2, 3, 4}), px) && px == std::vector<uint8_t>({1, 2, 3, 4}));
  CHECK(Decode(MakeDDS({.w = 1, .h = 1, .bits = 32, .r = 0xff0000, .g = 0xff00, .b = 0xff, .a = 0xff000000}, {10, 20, 30, 40}), px, &img) &&
        img.format == DdsFormat::BGRA8 && px == std::vector<uint8_t>({30, 20, 10, 40}));
  CHECK(Decode(MakeDDS({.w = 2, .h = 1, .bits = 24, .r = 0xff0000, .g = 0xff00, .b = 0xff}, {10, 20, 30, 40, 50, 60}), px, &img) &&
        img.format == DdsFormat::BGR8 && px == std::vector<uint8_t>({30, 20, 10, 255, 60, 50, 40, 255}));
  // BC7 (DX10 98) with mips: cooked by the engine's own encoder, written as DDS, read back and decoded the same
  {
    const int W = 16, H = 8;
    std::vector<uint8_t> src((size_t)W * H * 4);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        uint8_t* p = &src[((size_t)y * W + x) * 4];
        p[0] = (uint8_t)(x * 16);
        p[1] = (uint8_t)(y * 32);
        p[2] = 100;
        p[3] = 255;
      }
    TextureSettings ts = DefaultTextureSettings("Grad_D.png");
    CookedTexture cooked;
    CHECK(CookTexture(src.data(), W, H, ts, cooked) && cooked.format == CookedFormat::BC7_SRGB && cooked.mips.size() == 5);
    const fs::path file = fs::temp_directory_path() / "bdg_dds_bc7.dds";
    CHECK(WriteDDS(file, cooked));
    std::string why;
    CHECK(ReadDDSFile(file, img, &why) && img.format == DdsFormat::BC7 && img.srgb && img.w == W && img.h == H && img.mips.size() == 5);
    std::vector<uint8_t> a, b;
    int32_t w = 0, h = 0, w2 = 0, h2 = 0;
    CHECK(DecodeDDS(img, 1, a, w, h) && DecodeCooked(cooked, 1, b, w2, h2) && a == b && w == 8 && h == 4);
    // the image loader reads DDS too (level 0), and its size from the header
    std::vector<uint8_t> viaLoader;
    CHECK(LoadImageRGBA(file, w, h, viaLoader) && w == W && h == H && DecodeCooked(cooked, 0, b, w2, h2) && viaLoader == b);
    CHECK(ImageSize(file, w, h) && w == W && h == H);
    // the cooker keeps matching blocks as they are, and refuses to when the settings ask for something else
    CookedTexture asIs;
    CHECK(CookFromDDS(img, ts, asIs) && asIs.format == CookedFormat::BC7_SRGB && asIs.mips == cooked.mips);
    TextureSettings small = ts;
    small.maxSize = 8;
    CHECK(!CookFromDDS(img, small, asIs));
    CHECK(!CookFromDDS(img, DefaultTextureSettings("Wave_N.png"), asIs)); // normal maps cook to BC5
    fs::remove(file);
  }
  // mips: an 8 x 8 BC1 with its 4 levels; a file cut short keeps the complete levels
  {
    const std::vector<uint8_t> lv = Cat({Bc1(0xF800, 0, 0), Bc1(0xF800, 0, 0), Bc1(0xF800, 0, 0), Bc1(0xF800, 0, 0), // 8x8
                                         Bc1(0x07E0, 0, 0), Bc1(0x001F, 0, 0), Bc1(0xFFFF, 0, 0)});          // 4x4 2x2 1x1
    std::vector<uint8_t> file = MakeDDS({.w = 8, .h = 8, .mipCount = 4, .fourCC = CC("DXT1")}, lv);
    CHECK(Decode(file, px, &img, 2) && img.mips.size() == 4 && px.size() == 2 * 2 * 4 && px[2] == 255 && px[0] == 0);
    file.resize(file.size() - 8 - 4);
    CHECK(ParseDDS(file.data(), file.size(), img) && img.mips.size() == 2);
    // a mip count past the full chain is clamped
    CHECK(ParseDDS(MakeDDS({.mipCount = 9, .fourCC = CC("DXT1")}, Cat({Bc1(1, 0, 0), Bc1(1, 0, 0), Bc1(1, 0, 0)})).data(), 128 + 24, img) &&
          img.mips.size() == 3);
  }
  // refused: not DDS, no pixels, volume textures, signed BC5, unknown four-CC; BC6H is read but not decoded
  std::string why;
  const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
  CHECK(!ParseDDS(png.data(), png.size(), img, &why) && !why.empty());
  std::vector<uint8_t> f = MakeDDS({.fourCC = CC("DXT1")}, {});
  CHECK(!ParseDDS(f.data(), f.size(), img, &why) && why == "pixel data cut short");
  f = MakeDDS({.fourCC = CC("DXT1"), .volume = true}, Bc1(0, 0, 0));
  CHECK(!ParseDDS(f.data(), f.size(), img, &why) && why.find("volume") != std::string::npos);
  f = MakeDDS({.dxgi = 84}, Cat({Bc4(0), Bc4(0)}));
  CHECK(!ParseDDS(f.data(), f.size(), img, &why) && why.find("SNORM") != std::string::npos);
  f = MakeDDS({.fourCC = CC("ETC2")}, Bc1(0, 0, 0));
  CHECK(!ParseDDS(f.data(), f.size(), img, &why) && why.find("ETC2") != std::string::npos);
  f = MakeDDS({.dxgi = 95}, std::vector<uint8_t>(16, 0));
  int32_t w = 0, h = 0;
  CHECK(ParseDDS(f.data(), f.size(), img) && img.format == DdsFormat::BC6H && !DecodeDDS(img, 0, px, w, h));
}

TEST_CASE("DDS: model import keeps DDS textures as blocks, the cooker reads DDS assets") {
  // an OBJ whose material's map_Kd is a BC1 DDS (the FBX path shares the same texture loader)
  const fs::path dir = fs::temp_directory_path() / "bdg_dds_import";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  WriteBytes(dir / "wall_D.dds", MakeDDS({.w = 8, .h = 4, .mipCount = 2, .fourCC = CC("DXT1")},
                                         Cat({Bc1(0xF800, 0, 0), Bc1(0xF800, 0, 0), Bc1(0x07E0, 0, 0), Bc1(0x07E0, 0, 0)})));
  WriteBytes(dir / "wall_N.dds", MakeDDS({.fourCC = CC("ATI2")}, Cat({Bc4(128), Bc4(128)})));
  {
    std::ofstream mtl(dir / "wall.mtl");
    mtl << "newmtl Wall\nKd 1 1 1\nmap_Kd wall_D.dds\nnorm wall_N.dds\n";
    std::ofstream obj(dir / "wall.obj");
    obj << "mtllib wall.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvn 0 0 1\nusemtl Wall\nf 1/1/1 2/2/1 3/3/1\n";
  }
  ImportedModel m;
  std::string err;
  CHECK(ImportModel(dir / "wall.obj", m, &err));
  CHECK(m.materials.size() == 1 && m.materials[0].baseColorTex >= 0);
  if (!m.materials.empty() && m.materials[0].baseColorTex >= 0) {
    const ImportedTexture& t = m.textures[(size_t)m.materials[0].baseColorTex];
    CHECK(t.dds && t.rgba.empty() && t.dds->format == DdsFormat::BC1 && t.dds->mips.size() == 2 && t.w == 8 && t.h == 4 && t.srgb && t.Valid());
  }
  if (!m.materials.empty() && m.materials[0].normalTex >= 0) { // normal maps stay linear
    const ImportedTexture& n = m.textures[(size_t)m.materials[0].normalTex];
    CHECK(n.dds && n.dds->format == DdsFormat::BC5 && !n.srgb);
  }
  // a .dds in a project is a texture asset; the cooker decodes BC1 and cooks it (BC7 sRGB)
  CHECK(AssetDatabase::ImporterFor("Textures/wall_D.dds") == "texture");
  AssetRecord r;
  r.guid = "0123456789abcdef0123456789abcdef";
  r.path = "Textures/wall_D.dds";
  r.hash = 42;
  TextureCookCache cache;
  cache.SetDirectory(dir / "cooked");
  CookedTexture c;
  CHECK(cache.Get(r, dir / "wall_D.dds", "{}", c) && c.format == CookedFormat::BC7_SRGB && c.w == 8 && c.h == 4);
  std::vector<uint8_t> px;
  int32_t w = 0, h = 0;
  CHECK(DecodeCooked(c, 0, px, w, h) && px[0] > 240 && px[1] < 16);
  fs::remove_all(dir, ec);
}
