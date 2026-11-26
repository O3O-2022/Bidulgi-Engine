#include "Assets/AssetDatabase.h"
#include "Scene/Prefab.h"
#include "Core/Utf8Path.h"
#include "Core/Log.h"
#include <yyjson.h>
#include "Assets/TextureCooker.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <random>
#include <unordered_set>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;

namespace bdg {
namespace {
std::string Registry_Quote(const std::string& s) { // JSON string literal
  std::string o = "\"";
  for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o + "\"";
}

std::wstring Wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string Utf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

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

long long MTime(const fs::path& p) { // 100 ns ticks since 1601 (= FILETIME, = MSVC file_clock)
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  return ec ? 0 : (long long)t.time_since_epoch().count();
}

// One pass over the tree with FindFirstFileEx: sizes and times come with the listing, so an unchanged project
// costs one directory read per folder and no per-file system calls. Names starting with '.' are skipped
// (.cache, .git, .DS_Store). Relative paths are UTF-8 with '/'.
struct DiskFile { std::string rel; uint64_t size; long long mtime; };
struct DiskTree {
  std::vector<DiskFile> files;
  std::unordered_map<std::string, long long> metas; // asset rel path -> .meta mtime
};
void ScanTree(const std::wstring& dir, const std::string& prefix, DiskTree& out) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.cFileName[0] == L'.') continue;
    std::string name = Utf8(fd.cFileName), rel = prefix.empty() ? name : prefix + "/" + name;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) ScanTree(dir + L"\\" + fd.cFileName, rel, out);
      continue;
    }
    long long mtime = (long long)(((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime);
    uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    if (rel.size() > 5 && rel.ends_with(".meta")) out.metas[rel.substr(0, rel.size() - 5)] = mtime;
    else if (!rel.ends_with(".tmp")) out.files.push_back({rel, size, mtime});
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}
}

std::string AssetDatabase::ImporterFor(const std::string& path) {
  std::string ext;
  if (size_t dot = path.find_last_of('.'); dot != std::string::npos && path.find('/', dot) == std::string::npos) ext = path.substr(dot);
  for (char& c : ext) c = (char)std::tolower((unsigned char)c);
  auto any = [&](std::initializer_list<const char*> l) { for (auto* e : l) if (ext == e) return true; return false; };
  if (any({".png", ".jpg", ".jpeg", ".tga", ".psd", ".exr", ".hdr", ".bmp", ".ktx2", ".dds"})) return "texture";
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

fs::path AssetDatabase::Abs(const std::string& rel) const {
  std::wstring w = Wide(rel);
  for (wchar_t& c : w) if (c == L'/') c = L'\\';
  return fs::path(rootW_ + L"\\" + w);
}

bool AssetDatabase::LoadCache() {
  std::FILE* f = cachePath_.empty() ? nullptr : OpenU8(cachePath_, "rb");
  if (!f) return false;
  char line[2048];
  bool ok = false;
  if (std::fgets(line, sizeof line, f)) { // header: same version and same root, else start over
    yyjson_doc* d = yyjson_read(line, std::strlen(line), 0);
    yyjson_val* h = d ? yyjson_doc_get_root(d) : nullptr;
    const char* r = yyjson_get_str(yyjson_obj_get(h, "root"));
    ok = yyjson_get_int(yyjson_obj_get(h, "assetdb")) == 1 && r && root_ == r;
    yyjson_doc_free(d);
  }
  while (ok && std::fgets(line, sizeof line, f)) {
    yyjson_doc* d = yyjson_read(line, std::strlen(line), 0);
    yyjson_val* o = d ? yyjson_doc_get_root(d) : nullptr;
    const char* g = yyjson_get_str(yyjson_obj_get(o, "guid"));
    const char* p = yyjson_get_str(yyjson_obj_get(o, "path"));
    const char* imp = yyjson_get_str(yyjson_obj_get(o, "importer"));
    if (g && p && imp) {
      AssetRecord r{g, p, imp, yyjson_get_uint(yyjson_obj_get(o, "size")), yyjson_get_uint(yyjson_obj_get(o, "hash")),
                    yyjson_get_sint(yyjson_obj_get(o, "mtime")), yyjson_get_sint(yyjson_obj_get(o, "metaMtime"))};
      pathToGuid_[r.path] = r.guid;
      byGuid_[r.guid] = std::move(r);
    }
    yyjson_doc_free(d);
  }
  std::fclose(f);
  return ok;
}

bool AssetDatabase::SaveCache() const {
  if (cachePath_.empty()) return false;
  std::error_code ec;
  fs::create_directories(PathU8(cachePath_).parent_path(), ec);
  std::FILE* f = OpenU8(cachePath_, "wb");
  if (!f) return false;
  std::fprintf(f, "{\"assetdb\":1,\"root\":%s}\n", Registry_Quote(root_).c_str());
  for (auto& [g, r] : byGuid_)
    std::fprintf(f, "{\"guid\":\"%s\",\"path\":%s,\"importer\":\"%s\",\"size\":%llu,\"hash\":%llu,\"mtime\":%lld,\"metaMtime\":%lld}\n",
                 g.c_str(), Registry_Quote(r.path).c_str(), r.importer.c_str(), (unsigned long long)r.size,
                 (unsigned long long)r.hash, r.mtime, r.metaMtime);
  std::fclose(f);
  return true;
}

AssetDatabase::~AssetDatabase() { ForgetPrefabAssets(this); }

bool AssetDatabase::Open(const std::string& root, std::string* error, const std::string& cachePath) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    if (error) *error = std::format("asset folder not found: {}", root);
    return false;
  }
  fs::path abs = fs::absolute(root);
  rootW_ = abs.wstring();
  while (!rootW_.empty() && (rootW_.back() == L'\\' || rootW_.back() == L'/')) rootW_.pop_back();
  root_ = Utf8(rootW_.c_str());
  byGuid_.clear();
  pathToGuid_.clear();
  cachePath_ = cachePath;
  bool cached = LoadCache(); // known files come back from the cache; the scan only reads what changed
  opened_ = cached;
  Changes c = Refresh();
  opened_ = true;
  SaveCache();
  LogInfo("assets: {} ({} assets, {} new, {} removed since last run, {} orphaned .meta) scan {:.0f} ms: {} files, {} .meta read, {} hashed{}",
          root_, Count(), c.added.size(), c.removed.size(), orphans_.size(), stats_.ms, stats_.files, stats_.metasRead,
          stats_.hashed, cached ? " (index cache)" : "");
  SetPrefabAssets(this); // prefab instances in scenes read their .prefab through this project (6-1)
  return true;
}

AssetDatabase::Changes AssetDatabase::Refresh() {
  Changes ch;
  auto t0 = std::chrono::steady_clock::now();
  stats_ = {};
  auto hashFile = [&](const std::string& rel) { ++stats_.hashed; return HashFile(Abs(rel)); };
  auto writeMeta = [&](const std::string& rel, const std::string& guid, const std::string& importer) {
    if (std::FILE* f = _wfopen(Abs(rel + ".meta").c_str(), L"wb")) {
      // Textures start with the import settings their file name suggests (queue 3-1b); users edit them later.
      std::string extra = importer == "texture" ? TextureMetaFields(DefaultTextureSettings(rel)) : std::string();
      std::fprintf(f, "{\"guid\":\"%s\",\"importer\":\"%s\"%s}\n", guid.c_str(), importer.c_str(), extra.c_str());
      std::fclose(f);
    }
    return MTime(Abs(rel + ".meta"));
  };
  DiskTree tree;
  ScanTree(rootW_, "", tree);
  std::unordered_set<std::string> seen;       // guids still on disk
  std::unordered_set<std::string> filesOnDisk;
  std::vector<const DiskFile*> noMeta;
  for (const DiskFile& df : tree.files) {
    ++stats_.files;
    filesOnDisk.insert(df.rel);
    auto mm = tree.metas.find(df.rel);
    long long metaMtime = mm == tree.metas.end() ? 0 : mm->second;
    std::string guid;
    auto known = pathToGuid_.find(df.rel);
    if (known != pathToGuid_.end() && metaMtime != 0 && byGuid_[known->second].metaMtime == metaMtime) guid = known->second; // .meta unchanged
    else if (metaMtime != 0) { guid = ReadGuid(Abs(df.rel + ".meta")); ++stats_.metasRead; }
    if (guid.empty() || seen.count(guid)) { noMeta.push_back(&df); continue; } // no .meta, or a copied one: new GUID
    seen.insert(guid);
    auto it = byGuid_.find(guid);
    if (it == byGuid_.end()) { // first time seen (opened project, or an asset that arrived with its .meta)
      byGuid_[guid] = {guid, df.rel, ImporterFor(df.rel), df.size, hashFile(df.rel), df.mtime, metaMtime};
      pathToGuid_[df.rel] = guid;
      if (opened_) ch.added.push_back(df.rel);
      continue;
    }
    AssetRecord& r = it->second;
    r.metaMtime = metaMtime;
    if (r.path != df.rel) { // moved together with its .meta
      ch.moved.emplace_back(r.path, df.rel);
      pathToGuid_.erase(r.path);
      pathToGuid_[df.rel] = guid;
      r.path = df.rel;
    }
    if (r.size != df.size || r.mtime != df.mtime) {
      r.size = df.size;
      r.mtime = df.mtime;
      r.hash = hashFile(df.rel);
      ch.modified.push_back(df.rel);
    }
  }
  // Records whose file is gone: maybe moved without its .meta (match by content), else removed.
  std::vector<std::string> gone;
  for (auto& [g, r] : byGuid_) if (!seen.count(g)) gone.push_back(g);
  std::sort(gone.begin(), gone.end());
  for (const DiskFile* df : noMeta) {
    uint64_t hash = hashFile(df->rel);
    auto match = std::find_if(gone.begin(), gone.end(), [&](const std::string& g) { return byGuid_[g].size == df->size && byGuid_[g].hash == hash; });
    if (match != gone.end()) { // reattach: move the orphaned .meta next to the file, keep the GUID
      std::string guid = *match;
      AssetRecord& r = byGuid_[guid];
      std::error_code ec;
      fs::rename(Abs(r.path + ".meta"), Abs(df->rel + ".meta"), ec);
      if (ec) writeMeta(df->rel, guid, r.importer); // could not move it: same GUID in a fresh .meta
      LogInfo("assets: {} moved without its .meta; reattached to {} (GUID {})", r.path, df->rel, guid);
      ch.moved.emplace_back(r.path, df->rel);
      pathToGuid_.erase(r.path);
      tree.metas.erase(r.path);
      r.path = df->rel;
      r.mtime = df->mtime;
      r.metaMtime = MTime(Abs(df->rel + ".meta"));
      pathToGuid_[df->rel] = guid;
      gone.erase(match);
    } else { // new asset: its .meta is written once
      std::string guid = NewGuid(), importer = ImporterFor(df->rel);
      long long metaMtime = writeMeta(df->rel, guid, importer);
      byGuid_[guid] = {guid, df->rel, importer, df->size, hash, df->mtime, metaMtime};
      pathToGuid_[df->rel] = guid;
      ch.added.push_back(df->rel);
    }
    tree.metas[df->rel] = 1;
  }
  for (const std::string& g : gone) {
    ch.removed.push_back(byGuid_[g].path);
    // the path may already belong to another GUID (the file was replaced together with a different .meta): keep that
    if (auto p = pathToGuid_.find(byGuid_[g].path); p != pathToGuid_.end() && p->second == g) pathToGuid_.erase(p);
    byGuid_.erase(g);
  }
  orphans_.clear();
  for (auto& [rel, t] : tree.metas) if (!filesOnDisk.count(rel)) orphans_.push_back(rel + ".meta");
  std::sort(orphans_.begin(), orphans_.end());
  stats_.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (opened_ && !ch.Empty()) SaveCache();
  return ch;
}

bool AssetDatabase::Move(const std::string& from, const std::string& to, std::string* error) {
  auto it = pathToGuid_.find(from);
  if (it == pathToGuid_.end()) { if (error) *error = std::format("no asset \"{}\"", from); return false; }
  if (pathToGuid_.count(to)) { if (error) *error = std::format("\"{}\" already exists", to); return false; }
  fs::path src = Abs(from), dst = Abs(to);
  std::error_code ec;
  fs::create_directories(dst.parent_path(), ec);
  fs::rename(src, dst, ec);
  if (ec) { if (error) *error = std::format("cannot move {}: {}", from, ec.message()); return false; }
  fs::rename(Abs(from + ".meta"), Abs(to + ".meta"), ec);
  if (ec) { fs::rename(dst, src, ec); if (error) *error = std::format("cannot move {}.meta", from); return false; }
  std::string guid = it->second;
  pathToGuid_.erase(it);
  pathToGuid_[to] = guid;
  AssetRecord& r = byGuid_[guid];
  r.path = to;
  r.metaMtime = MTime(Abs(to + ".meta"));
  SaveCache();
  return true;
}

const AssetRecord* AssetDatabase::ByGuid(const std::string& guid) const {
  auto it = byGuid_.find(guid);
  return it == byGuid_.end() ? nullptr : &it->second;
}

std::vector<const AssetRecord*> AssetDatabase::All() const {
  std::vector<const AssetRecord*> out;
  for (auto& [g, r] : byGuid_) out.push_back(&r);
  std::sort(out.begin(), out.end(), [](const AssetRecord* a, const AssetRecord* b) { return a->path < b->path; });
  return out;
}

const AssetRecord* AssetDatabase::ByPath(const std::string& path) const {
  auto it = pathToGuid_.find(path);
  return it == pathToGuid_.end() ? nullptr : ByGuid(it->second);
}

std::vector<std::string> AssetDatabase::OrphanMetas() const {
  DiskTree tree; // fresh look at the disk (the panel and `bdg validate` ask after outside changes)
  ScanTree(rootW_, "", tree);
  std::unordered_set<std::string> files;
  for (auto& f : tree.files) files.insert(f.rel);
  std::vector<std::string> out;
  for (auto& [rel, t] : tree.metas) if (!files.count(rel)) out.push_back(rel + ".meta");
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
  auto* args = new WatchArgs{Wide(root), onChange, user, stop};
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
