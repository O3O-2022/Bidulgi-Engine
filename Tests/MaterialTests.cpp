// Material files and texture cooking (queue 3-1b).
#include "Assets/MaterialFile.h"
#include "Assets/TextureCooker.h"
#include "bdg_test.h"
#include <cmath>
#include <filesystem>
#include <map>

using namespace bdg;

TEST_CASE("materials: .material and .matinst parse, save and resolve (queue 3-1b)") {
  const char* rock = R"({"material":"M_Rock","shading":"lit","blend":"masked","twoSided":true,"cutoff":0.3}
{"param":"BaseColor","type":"color","value":"#808080"}
{"param":"Roughness","type":"float","value":0.8,"range":[0,1]}
{"param":"BaseColorMap","type":"texture","value":"Textures/Rock_D.png"}
{"param":"Wetness","type":"float","value":0}
{"id":"t1","node":"TextureSample","in":{"tex":"Albedo"}}
)";
  MaterialDef m;
  std::string err;
  CHECK(ParseMaterial(rock, m, &err));
  CHECK(m.name == "M_Rock" && !m.instance && m.blend == "masked" && m.twoSided && std::fabs(m.cutoff - 0.3f) < 1e-6f);
  CHECK(std::fabs(m.params["BaseColor"].c.r - 0.2158605f) < 1e-4f); // sRGB 128 -> linear
  CHECK(m.params["Roughness"].f == 0.8f && m.params["BaseColorMap"].texture == "Textures/Rock_D.png");
  CHECK(m.warnings.size() == 2); // Wetness (not used by the standard shader yet) + the node line (M5)

  MaterialDef again; // save -> parse keeps everything
  CHECK(ParseMaterial(SaveMaterial(m), again, &err));
  CHECK(again.params.size() == m.params.size() && again.blend == "masked" && again.twoSided);
  CHECK(std::fabs(again.params["BaseColor"].c.g - m.params["BaseColor"].c.g) < 1e-4f);

  MaterialDef wet, wetter;
  CHECK(ParseMaterial("{\"instance\":\"MI_Rock_Wet\",\"parent\":\"M_Rock\"}\n{\"set\":\"Roughness\",\"value\":0.25}\n", wet, &err));
  CHECK(ParseMaterial("{\"instance\":\"MI_Rock_Wetter\",\"parent\":\"MI_Rock_Wet\"}\n{\"set\":\"BaseColor\",\"value\":\"#ff0000\"}\n"
                      "{\"set\":\"Shine\",\"value\":1}\n", wetter, &err));
  std::map<std::string, const MaterialDef*> lib{{"M_Rock", &m}, {"MI_Rock_Wet", &wet}, {"MI_Rock_Wetter", &wetter}};
  auto lookup = [&](const std::string& n) -> const MaterialDef* { auto it = lib.find(n); return it == lib.end() ? nullptr : it->second; };
  MaterialDef r;
  CHECK(ResolveMaterial(wetter, lookup, r, &err));
  CHECK(r.name == "MI_Rock_Wetter" && r.params["Roughness"].f == 0.25f && r.params["BaseColor"].c.r == 1.f && r.params["BaseColor"].c.g == 0.f);
  CHECK(r.blend == "masked" && r.params.count("Shine") == 0 && !r.warnings.empty()); // unknown override reported, not added

  MaterialDef orphan, loopA, loopB;
  CHECK(ParseMaterial("{\"instance\":\"MI_X\",\"parent\":\"M_Missing\"}", orphan, &err));
  CHECK(!ResolveMaterial(orphan, lookup, r, &err) && err.find("not found") != std::string::npos);
  CHECK(ParseMaterial("{\"instance\":\"A\",\"parent\":\"B\"}", loopA, &err));
  CHECK(ParseMaterial("{\"instance\":\"B\",\"parent\":\"A\"}", loopB, &err));
  lib["A"] = &loopA;
  lib["B"] = &loopB;
  CHECK(!ResolveMaterial(loopA, lookup, r, &err) && err.find("loop") != std::string::npos);
  CHECK(!ParseMaterial("{\"instance\":\"MI_NoParent\"}", r, &err));
  CHECK(!ParseMaterial("not json", r, &err));
  bool ok = false;
  ParseColorValue("#12345", ok);
  CHECK(!ok);
}

TEST_CASE("textures: presets from names, settings from .meta (queue 3-1b)") {
  CHECK(PresetFromPath("Textures/Rock_D.png") == TexturePreset::Default);
  CHECK(PresetFromPath("Textures/Rock_N.png") == TexturePreset::Normal);
  CHECK(PresetFromPath("Textures/rock_normal.tga") == TexturePreset::Normal);
  CHECK(PresetFromPath("Textures/Rock_ORM.png") == TexturePreset::Masks);
  CHECK(PresetFromPath("Textures/Rock_Height.png") == TexturePreset::Grayscale);
  CHECK(PresetFromPath("UI/Button.png") == TexturePreset::UI);
  TextureSettings n = DefaultTextureSettings("Rock_N.png");
  CHECK(!n.srgb && n.mips && FormatFor(n) == CookedFormat::BC5);
  CHECK(FormatFor(DefaultTextureSettings("Rock_D.png")) == CookedFormat::BC7_SRGB);
  CHECK(FormatFor(DefaultTextureSettings("UI/Icon.png")) == CookedFormat::RGBA8_SRGB && !DefaultTextureSettings("UI/Icon.png").mips);
  std::string meta = "{\"guid\":\"x\",\"importer\":\"texture\"" + TextureMetaFields(n) + "}";
  TextureSettings back = ParseTextureSettings(meta, "Rock_N.png");
  CHECK(back.preset == TexturePreset::Normal && !back.srgb && back.maxSize == 2048);
  // A user edit: the preset changes (with its defaults), an explicit field still wins.
  TextureSettings edited = ParseTextureSettings(R"({"preset":"masks","maxSize":512,"wrap":"clamp","srgb":true})", "Rock_D.png");
  CHECK(edited.preset == TexturePreset::Masks && edited.maxSize == 512 && !edited.repeat && edited.srgb);
}

TEST_CASE("textures: BC7 / BC5 / BC4 cooking, mips and DDS files (queue 3-1b)") {
  const int W = 64, H = 64;
  std::vector<uint8_t> img(W * H * 4);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t* p = &img[(y * W + x) * 4];
      p[0] = (uint8_t)(x * 4); p[1] = (uint8_t)(y * 4); p[2] = (uint8_t)(128 + 60 * std::sin(x * 0.2f)); p[3] = 255;
    }
  CookedTexture t;
  CHECK(CookTexture(img.data(), W, H, DefaultTextureSettings("Grad_D.png"), t));
  CHECK(t.format == CookedFormat::BC7_SRGB && t.mips.size() == 7 && t.mips[0].size() == 16 * 16 * 16);
  std::vector<uint8_t> dec;
  int32_t w, h;
  CHECK(DecodeCooked(t, 0, dec, w, h) && w == W && h == H);
  double se = 0;
  for (size_t i = 0; i < img.size(); ++i) if (i % 4 != 3) se += (dec[i] - img[i]) * (dec[i] - img[i]);
  double psnr = 10 * std::log10(255.0 * 255.0 / (se / (W * H * 3)));
  CHECK(psnr > 38.0); // BC7 on a smooth gradient

  // Mips of sRGB colour are averaged in linear light: black + white -> 0.5 linear -> sRGB 188, not 128.
  std::vector<uint8_t> checker(2 * 2 * 4, 255);
  for (int i : {0, 3}) checker[i * 4] = checker[i * 4 + 1] = checker[i * 4 + 2] = 0;
  TextureSettings ui = DefaultTextureSettings("UI/Checker.png");
  ui.mips = true;
  CookedTexture c;
  CHECK(CookTexture(checker.data(), 2, 2, ui, c) && c.mips.size() == 2 && std::abs((int)c.mips[1][0] - 188) <= 1);

  // Normal map: BC5 keeps X and Y; Z comes back from the unit length.
  std::vector<uint8_t> nrm(W * H * 4);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float nx = 0.4f * std::sin(x * 0.3f), ny = 0.4f * std::cos(y * 0.25f), nz = std::sqrt(1 - nx * nx - ny * ny);
      uint8_t* p = &nrm[(y * W + x) * 4];
      p[0] = (uint8_t)std::lround((nx * 0.5f + 0.5f) * 255); p[1] = (uint8_t)std::lround((ny * 0.5f + 0.5f) * 255);
      p[2] = (uint8_t)std::lround((nz * 0.5f + 0.5f) * 255); p[3] = 255;
    }
  CookedTexture nt;
  CHECK(CookTexture(nrm.data(), W, H, DefaultTextureSettings("Wave_N.png"), nt) && nt.format == CookedFormat::BC5);
  CHECK(DecodeCooked(nt, 0, dec, w, h));
  int worst = 0;
  for (int i = 0; i < W * H; ++i) for (int k = 0; k < 2; ++k) worst = std::max(worst, std::abs(dec[i * 4 + k] - nrm[i * 4 + k]));
  CHECK(worst <= 6);

  // Size limit, grayscale BC4, DDS round trip.
  TextureSettings gray = DefaultTextureSettings("Rock_Height.png");
  gray.maxSize = 16;
  CookedTexture g;
  CHECK(CookTexture(img.data(), W, H, gray, g) && g.format == CookedFormat::BC4 && g.w == 16 && g.h == 16 && g.mips.size() == 5);
  CHECK(g.mips[0].size() == 4 * 4 * 8);
  auto file = std::filesystem::temp_directory_path() / "bdg-cook-test.dds";
  CHECK(WriteDDS(file, t));
  CookedTexture read;
  CHECK(ReadDDS(file, read) && read.w == W && read.format == t.format && read.mips == t.mips);
  std::filesystem::remove(file);
}
