#pragma once
// Asset database (queue 2-3). Every asset file under the project's Assets folder gets a sidecar
// "<file>.meta" (one JSON line, created once, never rewritten by the scanner):
//   {"guid":"7f3a9c21e4b84d0f","importer":"texture"}
// plus whatever the user adds later (import settings, "source":{author,url,license}). The GUID follows the
// file when it is renamed or moved, so references survive:
//   - Move() moves the file and its .meta together (what the editor and `bdg mv` use);
//   - files moved outside the editor without their .meta are matched to the orphaned .meta by content
//     (same size and hash) on the next Refresh(); unmatched .meta files are reported, never deleted.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bdg {
struct AssetRecord {
  std::string guid, path, importer; // path: relative to the root, '/' separators
  uint64_t size = 0, hash = 0;      // hash: FNV-1a of the content (matches moved files to their .meta)
  long long mtime = 0;
};

class AssetDatabase {
public:
  struct Changes {
    std::vector<std::string> added, removed, modified;   // paths
    std::vector<std::pair<std::string, std::string>> moved; // old path -> new path (same GUID)
    bool Empty() const { return added.empty() && removed.empty() && modified.empty() && moved.empty(); }
  };

  bool Open(const std::string& root, std::string* error = nullptr); // first scan (creates missing .meta files)
  Changes Refresh();                                                 // rescan and report what changed
  bool Move(const std::string& from, const std::string& to, std::string* error = nullptr); // relative paths

  const AssetRecord* ByGuid(const std::string& guid) const;
  const AssetRecord* ByPath(const std::string& path) const;
  std::vector<std::string> OrphanMetas() const; // .meta files whose asset is gone (relative paths)
  size_t Count() const { return byGuid_.size(); }
  const std::string& Root() const { return root_; }
  static std::string ImporterFor(const std::string& path); // by extension: texture, model, audio, scene, ...

private:
  std::string NewGuid();
  std::string root_;
  bool opened_ = false; // after the first scan, files arriving with their own .meta count as added
  std::unordered_map<std::string, AssetRecord> byGuid_;
  std::unordered_map<std::string, std::string> pathToGuid_;
};

// Watches a folder tree on a background thread; onChange runs on that thread (keep it tiny: set a flag, Wake()).
class FileWatcher {
public:
  ~FileWatcher() { Stop(); }
  bool Start(const std::string& root, void (*onChange)(void*), void* user);
  void Stop();
private:
  void* thread_ = nullptr;
  void* stop_ = nullptr;
};
}
