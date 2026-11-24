#pragma once
// Bringing files into a project and checking them (queue 2-5d).
//
// Import: files or folders dragged in from Explorer are copied into a project folder. A .gltf brings the files it
// names by relative URI (buffers, images), keeping their sub-folders, so the model still opens. A name that is
// already taken gets " 1", " 2", ... before the extension; a dependency that already exists with the same bytes is
// shared, with different bytes it is reported and not overwritten.
//
// Rules: ProjectSettings/AssetRules.json next to the Assets folder (every key optional):
//   {"prefix":{"texture":"T_","material":"M_","model":"SM_"},"maxTextureSize":2048,"requireLicense":["model","audio"]}
// Defaults: no name prefixes, textures up to 8192, no license needed. A license is "source":{"license":"..."} (or
// "license":"...") in the asset's .meta. The editor checks on import and on save; `bdg validate` (2-7) reuses this.
#include "Assets/AssetDatabase.h"
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bdg {

struct ImportResult {
  std::vector<std::string> added;  // project paths of the copied files (dependencies included), in drop order
  std::vector<std::string> errors; // readable problems (missing file, name clash of a dependency, ...)
};
// destFolder: project-relative folder ("" = the Assets root). Does not refresh the database.
ImportResult ImportExternal(const AssetDatabase& db, const std::vector<std::filesystem::path>& sources, const std::string& destFolder);
// The relative URIs a .gltf file names (buffers and images; data: URIs and absolute paths skipped), '/' separators.
std::vector<std::string> GltfDependencies(const std::filesystem::path& gltf);
// "rock.png" in a folder that already has it -> "rock 1.png" (then 2, 3, ...).
std::string UniqueFileName(const std::filesystem::path& dir, const std::string& fileName);

struct AssetRules {
  std::map<std::string, std::string> prefix; // importer ("texture", "model", "material", ...) -> name prefix
  int maxTextureSize = 8192;                 // 0 = no limit
  std::set<std::string> requireLicense;      // importers that need a license in their .meta
  // A missing file keeps the defaults (true). Bad JSON -> false with err.
  bool Load(const std::filesystem::path& path, std::string* err = nullptr);
};

struct AssetIssue {
  std::string path; // project path
  std::string rule; // "prefix", "size", "license"
  std::string message;
};

// Checks assets against the rules. Texture sizes are read from the file header once per content hash.
class AssetRuleChecker {
public:
  std::vector<AssetIssue> Check(const AssetDatabase& db, const AssetRules& rules, const std::vector<std::string>* only = nullptr);

private:
  struct Size { uint64_t hash = 0; int32_t w = 0, h = 0; };
  std::map<std::string, Size> sizes_; // guid -> last read size
};

} // namespace bdg
