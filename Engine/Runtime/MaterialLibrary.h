#pragma once
// Materials of the open project on the GPU (queue 3-1b): finds .material / .matinst assets by path, GUID or name,
// resolves instance chains, cooks the textures they use (TextureCookCache, DDS in <project>/.cache/cooked/) and
// uploads them once. Results are cached until the asset database reports a change (Invalidate).
#include "Assets/MaterialFile.h"
#include "Assets/TextureCooker.h"
#include "Render3D/Renderer3D.h"
#include <string>
#include <unordered_map>

namespace bdg {
class AssetDatabase;
struct AssetRecord;

class MaterialLibrary {
public:
  void Init(RHI& rhi) { rhi_ = &rhi; }
  void Shutdown();
  void SetAssets(const AssetDatabase* db); // nullptr: no project (every lookup fails)
  void Invalidate();                       // files changed on disk: read again on the next lookup
  // ref: "Materials/M_Gold.material", "guid:<guid>" or a file stem ("M_Gold"). false: no such material asset.
  bool Find(const std::string& ref, Material3D& out, std::string* error = nullptr);
  const AssetRecord* Locate(const std::string& ref, bool materialsOnly) const;

  struct Stats { int materials = 0, textures = 0; };
  Stats GetStats() const { return {(int)resolved_.size(), (int)textures_.size()}; }
  const TextureCookCache& Cooker() const { return cooker_; }

private:
  const MaterialDef* Def(const AssetRecord& r);
  TextureHandle Texture(const std::string& ref, TexturePreset* preset = nullptr);

  RHI* rhi_ = nullptr;
  const AssetDatabase* db_ = nullptr;
  TextureCookCache cooker_;
  struct DefEntry { uint64_t hash = 0; MaterialDef def; bool ok = false; };
  std::unordered_map<std::string, DefEntry> defs_;            // by guid
  struct TexEntry { uint64_t hash = 0; TextureHandle handle; TexturePreset preset = TexturePreset::Default; };
  std::unordered_map<std::string, TexEntry> textures_;        // by guid
  struct Resolved { Material3D material; bool ok = false; std::string error; };
  std::unordered_map<std::string, Resolved> resolved_;        // by ref
};
}
