#include "Runtime/MaterialLibrary.h"
#include "Assets/AssetDatabase.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <algorithm>
#include <filesystem>

namespace bdg {
namespace fs = std::filesystem;
namespace {
fs::path U8Path(const std::string& s) { return fs::path(std::u8string((const char8_t*)s.c_str())); }

std::string ReadText(const fs::path& p) {
  std::vector<uint8_t> b;
  return ReadFileBytes(p, b) ? std::string(b.begin(), b.end()) : std::string();
}

TextureGroup GroupOf(TexturePreset p) {
  switch (p) {
    case TexturePreset::Default: return TextureGroup::World;
    case TexturePreset::Normal: return TextureGroup::Normal;
    case TexturePreset::Masks: return TextureGroup::Masks;
    case TexturePreset::Grayscale: return TextureGroup::Grayscale;
    case TexturePreset::UI: return TextureGroup::UI;
  }
  return TextureGroup::World;
}

TextureFormat ToRhi(CookedFormat f) {
  switch (f) {
    case CookedFormat::RGBA8: return TextureFormat::RGBA8;
    case CookedFormat::RGBA8_SRGB: return TextureFormat::SRGBA8;
    case CookedFormat::BC7: return TextureFormat::BC7;
    case CookedFormat::BC7_SRGB: return TextureFormat::BC7_SRGB;
    case CookedFormat::BC5: return TextureFormat::BC5;
    case CookedFormat::BC4: return TextureFormat::BC4;
  }
  return TextureFormat::RGBA8;
}
}

void MaterialLibrary::Shutdown() {
  if (rhi_)
    for (auto& [guid, t] : textures_) rhi_->DestroyTexture(t.handle);
  textures_.clear();
  defs_.clear();
  resolved_.clear();
}

void MaterialLibrary::SetAssets(const AssetDatabase* db) {
  db_ = db;
  if (db) cooker_.SetDirectory(U8Path(db->Root()).parent_path() / ".cache" / "cooked");
  Invalidate();
}

void MaterialLibrary::Invalidate() { resolved_.clear(); } // definitions and textures re-check their hash on use

void MaterialLibrary::SetTextureLimits(const QualitySettings& q) {
  if (std::equal(q.textureMax, q.textureMax + kTextureGroups, limits_)) return;
  std::copy(q.textureMax, q.textureMax + kTextureGroups, limits_);
  resolved_.clear(); // materials pick up the re-uploaded textures (Texture() compares each entry's limit)
}

MaterialLibrary::GroupUse MaterialLibrary::TextureGroupUse(TextureGroup g) const {
  GroupUse u;
  for (const auto& [guid, t] : textures_) {
    if (!t.handle.Valid() || GroupOf(t.preset) != g) continue;
    ++u.textures;
    u.largest = std::max(u.largest, t.side);
    u.cookedLargest = std::max(u.cookedLargest, t.cookedSide);
    u.bytes += t.bytes;
  }
  return u;
}

const AssetRecord* MaterialLibrary::Locate(const std::string& ref, bool materialsOnly) const {
  if (!db_ || ref.empty()) return nullptr;
  if (ref.starts_with("guid:")) return db_->ByGuid(ref.substr(5));
  if (const AssetRecord* r = db_->ByPath(ref)) return r;
  // By name: the file stem of a material asset ("M_Gold" finds Materials/M_Gold.material or .matinst).
  for (const AssetRecord* r : db_->All()) {
    if (materialsOnly && r->importer != "material") continue;
    if (U8Path(r->path).stem().u8string() == std::u8string((const char8_t*)ref.c_str())) return r;
  }
  return nullptr;
}

const MaterialDef* MaterialLibrary::Def(const AssetRecord& r) {
  DefEntry& e = defs_[r.guid];
  if (!e.ok || e.hash != r.hash) {
    e = {};
    e.hash = r.hash;
    std::string err;
    e.ok = ParseMaterial(ReadText(U8Path(db_->Root()) / U8Path(r.path)), e.def, &err);
    if (!e.ok) LogWarn("material: {}: {}", r.path, err);
    for (const std::string& w : e.def.warnings) LogInfo("material: {}: {}", r.path, w);
  }
  return e.ok ? &e.def : nullptr;
}

TextureHandle MaterialLibrary::Texture(const std::string& ref, TexturePreset* preset) {
  const AssetRecord* r = Locate(ref, false);
  if (!r || r->importer != "texture") {
    LogWarn("material: texture \"{}\" is not a texture asset of the project", ref);
    return {};
  }
  TexEntry& e = textures_[r->guid];
  if (e.handle.Valid() && e.hash == r->hash && e.limit == limits_[(int)GroupOf(e.preset)]) {
    if (preset) *preset = e.preset;
    return e.handle;
  }
  if (e.handle.Valid()) rhi_->DestroyTexture(e.handle);
  e = {};
  fs::path file = U8Path(db_->Root()) / U8Path(r->path);
  CookedTexture cooked;
  TextureSettings s;
  if (!cooker_.Get(*r, file, ReadText(fs::path(file.wstring() + L".meta")), cooked, &s)) return {};
  // Texture group limit (3-5c): start at the first cooked mip that fits the tier's largest side for the group.
  const int limit = limits_[(int)GroupOf(s.preset)];
  const int first = FirstMipWithin(cooked.w, cooked.h, (int)cooked.mips.size(), limit);
  TextureDesc d;
  d.w = std::max(cooked.w >> first, 1);
  d.h = std::max(cooked.h >> first, 1);
  d.format = ToRhi(cooked.format);
  d.repeat = s.repeat;
  std::vector<const void*> levels;
  std::vector<size_t> sizes;
  for (size_t k = (size_t)first; k < cooked.mips.size(); ++k) {
    levels.push_back(cooked.mips[k].data());
    sizes.push_back(cooked.mips[k].size());
    e.bytes += cooked.mips[k].size();
  }
  e.handle = rhi_->CreateTextureMips(d, levels, sizes);
  e.hash = r->hash;
  e.preset = s.preset;
  e.limit = limit;
  e.side = std::max(d.w, d.h);
  e.cookedSide = std::max(cooked.w, cooked.h);
  if (preset) *preset = e.preset;
  return e.handle;
}

bool MaterialLibrary::Find(const std::string& ref, Material3D& out, std::string* error) {
  if (auto it = resolved_.find(ref); it != resolved_.end()) {
    if (it->second.ok) out = it->second.material;
    else if (error) *error = it->second.error;
    return it->second.ok;
  }
  Resolved& res = resolved_[ref];
  const AssetRecord* r = Locate(ref, true);
  if (!r) { res.error = "no material asset \"" + ref + "\""; if (error) *error = res.error; return false; }
  const MaterialDef* def = Def(*r);
  MaterialDef m;
  std::string err;
  auto lookup = [&](const std::string& parent) -> const MaterialDef* {
    const AssetRecord* p = Locate(parent, true);
    return p ? Def(*p) : nullptr;
  };
  if (!def || !ResolveMaterial(*def, lookup, m, &err)) {
    res.error = def ? err : "could not read " + r->path;
    LogWarn("material: {}", res.error);
    if (error) *error = res.error;
    return false;
  }
  for (const std::string& w : m.warnings) LogInfo("material: {}: {}", r->path, w);
  Material3D& mat = res.material;
  auto num = [&](const char* name, float fallback) {
    auto it = m.params.find(name);
    return it != m.params.end() && it->second.type == MaterialParam::Type::Float ? it->second.f : fallback;
  };
  auto color = [&](const char* name, Color fallback) {
    auto it = m.params.find(name);
    return it != m.params.end() && it->second.type == MaterialParam::Type::Color ? it->second.c : fallback;
  };
  auto texture = [&](const char* name) {
    auto it = m.params.find(name);
    return it != m.params.end() && it->second.type == MaterialParam::Type::Texture && !it->second.texture.empty()
               ? Texture(it->second.texture) : TextureHandle{};
  };
  mat.baseColor = color("BaseColor", {1, 1, 1, 1});
  mat.baseColor.a *= num("Opacity", 1.f);
  mat.metallic = num("Metallic", 0.f);
  mat.roughness = num("Roughness", 0.5f);
  Color em = color("Emissive", {0, 0, 0, 1});
  float strength = num("EmissiveStrength", 1.f);
  mat.emissive = {em.r * strength, em.g * strength, em.b * strength, 1};
  mat.normalScale = num("NormalStrength", 1.f);
  mat.occlusionStrength = num("AOStrength", 1.f);
  mat.baseMap = texture("BaseColorMap");
  mat.normalMap = texture("NormalMap");
  mat.emissiveMap = texture("EmissiveMap");
  TextureHandle orm = texture("ORMMap"); // one packed texture feeds both the occlusion and the metal-rough slots
  mat.metalRoughMap = orm;
  mat.occlusionMap = orm;
  mat.unlit = m.shading == "unlit";
  mat.doubleSided = m.twoSided;
  mat.alpha = m.blend == "masked" ? Material3D::Alpha::Mask : m.blend == "translucent" ? Material3D::Alpha::Blend : Material3D::Alpha::Opaque;
  mat.alphaCutoff = m.cutoff;
  res.ok = true;
  out = mat;
  return true;
}
}
