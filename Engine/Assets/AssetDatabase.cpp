#include "Assets/AssetDatabase.h"
#include "Core/Log.h"
#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <format>
#include <random>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;

namespace bdg {
namespace {
std::string Rel(const fs::path& root, const fs::path& p) { return fs::relative(p, root).generic_string(); }

uint64_t HashFile(const fs::path& p) {
  uint64_t h = 1469598103934665603ull;
  std::FILE* f = _wfopen(p.c_str(), L"rb");
  if (!f) return 0;
  unsigned char buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
    for (size_t i = 0; i < n; ++i) h = (h ^ buf[i]) * 1099511628211ull;
  std::fclose(f);
  return h;
}

std::string ReadGuid(const fs::path& meta) {
  std::FILE* f = _wfopen(meta.c_str(), L"rb");
  if (!f) return {};
  std::string text;
  char buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  yyjson_doc* d = yyjson_read(text.data(), text.size(), YYJSON_READ_ALLOW_COMMENTS);
  const char* g = d ? yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(d), "guid")) : nullptr;
  std::string out = g ? g : "";
  yyjson_doc_free(d);
  return out;
}

bool IsAssetFile(const fs::directory_entry& e) {
  if (!e.is_regular_file()) return false;
  std::string name = e.path().filename().string();
  if (name.empty() || name[0] == '.') return false;
  std::string ext = e.path().extension().string();
  return ext != ".meta" && ext != ".tmp";
}

// Walks the tree; hidden folders (".cache", ".git") are skipped.
template <class F> void Walk(const fs::path& root, F&& f) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->is_directory() && it->path().filename().string().starts_with(".")) { it.disable_recursion_pending(); continue; }
    f(*it);
  }
}

long long MTime(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  return ec ? 0 : (long long)t.time_since_epoch().count();
}
}

std::string AssetDatabase::ImporterFor(const std::string& path) {
  std::string ext = fs::path(path).extension().string();
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  auto any = [&](std::initializer_list<const char*> l) { for (auto* e : l) if (ext == e) return true; return false; };
  if (any({".png", ".jpg", ".jpeg", ".tga", ".psd", ".exr", ".hdr", ".bmp", ".ktx2"})) return "texture";
  if (any({".glb", ".gltf", ".fbx", ".obj", ".blend"})) return "model";
  if (any({".wav", ".ogg", ".mp3", ".flac"})) return "audio";
  if (any({".ttf", ".otf"})) return "font";
  if (any({".scene"})) return "scene";
  if (any({".prefab"})) return "prefab";
  if (any({".material", ".matinst"})) return "material";
  if (any({".cs", ".lua", ".py", ".cpp", ".h"})) return "script";
  if (any({".graph", ".animgraph", ".anim", ".widget"})) return "graph";
  return "file";
}

std::string AssetDatabase::NewGuid() {
  static std::mt19937_64 rng{std::random_device{}() ^ (uint64_t)GetTickCount64()};
  std::string g;
  do { g = std::format("{:016x}", rng()); } while (byGuid_.count(g));
  return g;
}

bool AssetDatabase::Open(const std::string& root, std::string* error) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    if (error) *error = std::format("asset folder not found: {}", root);
    return false;
  }
  root_ = fs::absolute(root).string();
  byGuid_.clear();
  pathToGuid_.clear();
  opened_ = false;
  Changes c = Refresh();
  opened_ = true;
  LogInfo("assets: {} ({} assets, {} new .meta files, {} orphaned .meta)", root_, Count(), c.added.size(), OrphanMetas().size());
  return true;
}

AssetDatabase::Changes AssetDatabase::Refresh() {
  Changes ch;
  fs::path root(root_);
  std::unordered_map<std::string, bool> seen; // guid -> still on disk
  std::vector<fs::path> noMeta;
  Walk(root, [&](const fs::directory_entry& e) {
    if (!IsAssetFile(e)) return;
    std::string rel = Rel(root, e.path());
    fs::path meta = e.path().string() + ".meta";
    std::string guid = fs::exists(meta) ? ReadGuid(meta) : "";
    if (guid.empty() || seen.count(guid)) { noMeta.push_back(e.path()); return; } // copied file with a copied .meta: new GUID
    seen[guid] = true;
    uint64_t size = (uint64_t)e.file_size();
    long long mtime = MTime(e.path());
    auto it = byGuid_.find(guid);
    if (it == byGuid_.end()) { // first time seen (e.g. opened project, or an asset added with its .meta)
      byGuid_[guid] = {guid, rel, ImporterFor(rel), size, HashFile(e.path()), mtime};
      pathToGuid_[rel] = guid;
      if (opened_) ch.added.push_back(rel); // arrived with its own .meta
      return;
    }
    AssetRecord& r = it->second;
    if (r.path != rel) { // moved together with its .meta
      ch.moved.emplace_back(r.path, rel);
      pathToGuid_.erase(r.path);
      pathToGuid_[rel] = guid;
      r.path = rel;
    }
    if (r.size != size || r.mtime != mtime) {
      r.size = size;
      r.mtime = mtime;
      r.hash = HashFile(e.path());
      ch.modified.push_back(rel);
    }
  });
  // Records whose file is gone: maybe moved without its .meta (match by content), else removed.
  std::vector<std::string> gone;
  for (auto& [g, r] : byGuid_) if (!seen.count(g)) gone.push_back(g);
  std::sort(gone.begin(), gone.end());
  for (const fs::path& p : noMeta) {
    std::string rel = Rel(root, p);
    uint64_t size = (uint64_t)fs::file_size(p), hash = HashFile(p);
    auto match = std::find_if(gone.begin(), gone.end(), [&](const std::string& g) { return byGuid_[g].size == size && byGuid_[g].hash == hash; });
    std::string guid;
    fs::path meta = p.string() + ".meta";
    if (match != gone.end()) { // reattach: move the orphaned .meta next to the file, keep the GUID
      guid = *match;
      AssetRecord& r = byGuid_[guid];
      fs::path oldMeta = root / (r.path + ".meta");
      std::error_code ec;
      if (fs::exists(oldMeta)) fs::rename(oldMeta, meta, ec);
      if (ec || !fs::exists(meta)) { // could not move it: write a fresh one with the same GUID
        if (std::FILE* f = _wfopen(meta.c_str(), L"wb")) { std::fprintf(f, "{\"guid\":\"%s\",\"importer\":\"%s\"}\n", guid.c_str(), r.importer.c_str()); std::fclose(f); }
      }
      LogInfo("assets: {} moved without its .meta; reattached to {} (GUID {})", r.path, rel, guid);
      ch.moved.emplace_back(r.path, rel);
      pathToGuid_.erase(r.path);
      r.path = rel;
      r.mtime = MTime(p);
      pathToGuid_[rel] = guid;
      gone.erase(match);
    } else { // new asset: its .meta is written once
      guid = NewGuid();
      std::string importer = ImporterFor(rel);
      if (std::FILE* f = _wfopen(meta.c_str(), L"wb")) { std::fprintf(f, "{\"guid\":\"%s\",\"importer\":\"%s\"}\n", guid.c_str(), importer.c_str()); std::fclose(f); }
      byGuid_[guid] = {guid, rel, importer, size, hash, MTime(p)};
      pathToGuid_[rel] = guid;
      ch.added.push_back(rel);
    }
  }
  for (const std::string& g : gone) {
    ch.removed.push_back(byGuid_[g].path);
    pathToGuid_.erase(byGuid_[g].path);
    byGuid_.erase(g);
  }
  return ch;
}

bool AssetDatabase::Move(const std::string& from, const std::string& to, std::string* error) {
  auto it = pathToGuid_.find(from);
  if (it == pathToGuid_.end()) { if (error) *error = std::format("no asset \"{}\"", from); return false; }
  if (pathToGuid_.count(to)) { if (error) *error = std::format("\"{}\" already exists", to); return false; }
  fs::path root(root_), src = root / from, dst = root / to;
  std::error_code ec;
  fs::create_directories(dst.parent_path(), ec);
  fs::rename(src, dst, ec);
  if (ec) { if (error) *error = std::format("cannot move {}: {}", from, ec.message()); return false; }
  fs::rename(src.string() + ".meta", dst.string() + ".meta", ec);
  if (ec) { fs::rename(dst, src, ec); if (error) *error = std::format("cannot move {}.meta", from); return false; }
  std::string guid = it->second;
  pathToGuid_.erase(it);
  pathToGuid_[to] = guid;
  byGuid_[guid].path = to;
  return true;
}

const AssetRecord* AssetDatabase::ByGuid(const std::string& guid) const {
  auto it = byGuid_.find(guid);
  return it == byGuid_.end() ? nullptr : &it->second;
}

const AssetRecord* AssetDatabase::ByPath(const std::string& path) const {
  auto it = pathToGuid_.find(path);
  return it == pathToGuid_.end() ? nullptr : ByGuid(it->second);
}

std::vector<std::string> AssetDatabase::OrphanMetas() const {
  std::vector<std::string> out;
  fs::path root(root_);
  Walk(root, [&](const fs::directory_entry& e) {
    if (e.is_regular_file() && e.path().extension() == ".meta" && !fs::exists(fs::path(e.path()).replace_extension()))
      out.push_back(Rel(root, e.path()));
  });
  std::sort(out.begin(), out.end());
  return out;
}

// ---- File watcher ---------------------------------------------------------------------------
namespace {
struct WatchArgs { std::wstring root; void (*cb)(void*); void* user; HANDLE stop; };
DWORD WINAPI WatchThread(LPVOID p) {
  WatchArgs* a = static_cast<WatchArgs*>(p);
  HANDLE change = FindFirstChangeNotificationW(a->root.c_str(), TRUE,
                                               FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                                   FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
  if (change != INVALID_HANDLE_VALUE) {
    HANDLE handles[2] = {change, a->stop};
    while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) {
      a->cb(a->user);
      if (!FindNextChangeNotification(change)) break;
    }
    FindCloseChangeNotification(change);
  }
  delete a;
  return 0;
}
}

bool FileWatcher::Start(const std::string& root, void (*onChange)(void*), void* user) {
  Stop();
  HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  auto* args = new WatchArgs{fs::path(root).wstring(), onChange, user, stop};
  HANDLE t = CreateThread(nullptr, 0, WatchThread, args, 0, nullptr);
  if (!t) { delete args; CloseHandle(stop); return false; }
  thread_ = t;
  stop_ = stop;
  return true;
}

void FileWatcher::Stop() {
  if (!thread_) return;
  SetEvent((HANDLE)stop_);
  WaitForSingleObject((HANDLE)thread_, 5000);
  CloseHandle((HANDLE)thread_);
  CloseHandle((HANDLE)stop_);
  thread_ = stop_ = nullptr;
}
}
