#pragma once
// Asset references (queue 2-5b): which project files use an asset, which assets nothing uses, and replacing one
// asset by another everywhere. Works on the files on disk (plus documents the editor has open in memory), so the
// editor and the `bdg refs` / `bdg unused` commands (2-7) give the same answers.
//
// An asset can be named four ways, and all count: its project path ("Textures/Rock_D.png"), its file name
// ("Knight.glb", how MeshRenderer.mesh names models), "guid:<guid>" or the bare GUID, and - for materials - the
// file stem ("M_Rock", how MeshRenderer.material and .matinst parents name them). JSON documents (.scene,
// .material, .matinst, .prefab, .json) are matched on whole string values; scripts on quoted strings.
//
// Unused = not reachable from a root: scenes, scripts and open documents are roots; what they name is used, and
// so is what those assets name in turn (a scene -> a material -> its textures).
#include "Assets/AssetDatabase.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bdg {

struct AssetRef {
  std::string asset; // the referenced asset (project path)
  std::string from;  // the referencing document (project path, or the name of an open document)
  std::string where; // "g6 Player · MeshRenderer.mesh", "BaseColorMap", "line 12"
  std::string token; // the text that named it ("Knight.glb", "M_Rock", "guid:...")
};

// The names an asset can be referred to by (see above).
std::vector<std::string> RefTokens(const AssetRecord& r);

class AssetRefIndex {
public:
  // extra: documents that are not on disk (the editor's open scene): (name, JSON lines text).
  void Build(const AssetDatabase& db, const std::vector<std::pair<std::string, std::string>>& extra = {});
  std::vector<AssetRef> RefsTo(const std::string& assetPath) const; // every reference, by document then place
  std::vector<std::string> Unused() const;                           // asset paths, sorted
  size_t Documents() const { return documents_; }
  static bool IsRoot(const std::string& path);                       // scenes and scripts

private:
  std::vector<AssetRef> refs_;
  std::set<std::string> assets_, roots_, extraNames_;
  size_t documents_ = 0;
};

// Replaces references to `from` by the same kind of name for `to` (a path by a path, a stem by a stem, ...) in
// the JSON documents and scripts on disk: exact quoted strings only, the rest of each file byte-for-byte as it
// was. Returns the files changed; `count` gets the number of replacements. The editor's open scene is not touched
// here (it changes through commands, so it can be undone).
std::vector<std::string> ReplaceReferencesOnDisk(const AssetDatabase& db, const std::string& from, const std::string& to, int* count = nullptr,
                                                 const std::set<std::string>& skip = {});
// The name of `to` matching the kind of `token` used for `from` (path -> path, file name -> file name, ...).
std::string MatchingToken(const AssetRecord& from, const AssetRecord& to, const std::string& token);

} // namespace bdg
