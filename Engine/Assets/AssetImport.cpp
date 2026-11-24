#include "Assets/AssetImport.h"
#include "Assets/ModelImport.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <yyjson.h>
#include <algorithm>
#include <format>
#include <fstream>

namespace bdg {
namespace fs = std::filesystem;
namespace {

fs::path U8(const std::string& s) { return fs::path(std::u8string((const char8_t*)s.c_str())); }
std::string Utf8(const fs::path& p) {
  const std::u8string u = p.generic_u8string();
  return std::string((const char*)u.data(), u.size());
}
std::string ReadText(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool SameBytes(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
  return ReadText(a) == ReadText(b);
}
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
// "%20" and friends in glTF URIs
std::string UriDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
      out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
      i += 2;
    } else out += s[i];
  }
  return out;
}

} // namespace

std::string UniqueFileName(const fs::path& dir, const std::string& fileName) {
  std::error_code ec;
  if (!fs::exists(dir / U8(fileName), ec)) return fileName;
  const size_t dot = fileName.find_last_of('.');
  const std::string stem = dot == std::string::npos || dot == 0 ? fileName : fileName.substr(0, dot);
  const std::string ext = dot == std::string::npos || dot == 0 ? std::string() : fileName.substr(dot);
  for (int n = 1;; ++n) {
    std::string candidate = std::format("{} {}{}", stem, n, ext);
    if (!fs::exists(dir / U8(candidate), ec)) return candidate;
  }
}

std::vector<std::string> GltfDependencies(const fs::path& gltf) {
  std::vector<std::string> out;
  const std::string text = ReadText(gltf);
  yyjson_doc* doc = yyjson_read(text.data(), text.size(), 0);
  if (!doc) return out;
  yyjson_val* root = yyjson_doc_get_root(doc);
  for (const char* group : {"buffers", "images"}) {
    size_t i, n;
    yyjson_val* item;
    yyjson_arr_foreach(yyjson_obj_get(root, group), i, n, item) {
      const char* uri = yyjson_get_str(yyjson_obj_get(item, "uri"));
      if (!uri) continue;
      std::string u = UriDecode(uri);
      if (u.starts_with("data:") || u.find("://") != std::string::npos || u.starts_with("/") || (u.size() > 1 && u[1] == ':')) continue;
      std::replace(u.begin(), u.end(), '\\', '/');
      if (std::find(out.begin(), out.end(), u) == out.end()) out.push_back(u);
    }
  }
  yyjson_doc_free(doc);
  return out;
}

ImportResult ImportExternal(const AssetDatabase& db, const std::vector<fs::path>& sources, const std::string& destFolder) {
  ImportResult r;
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(U8(db.Root()), ec);
  const fs::path dest = destFolder.empty() ? root : root / U8(destFolder);
  fs::create_directories(dest, ec);
  auto rel = [&](const fs::path& p) { return Utf8(fs::relative(p, root, ec)); };
  auto inside = [&](const fs::path& p) {
    const std::string a = Lower(Utf8(fs::weakly_canonical(p, ec))), b = Lower(Utf8(root));
    return a.size() >= b.size() && a.compare(0, b.size(), b) == 0 && (a.size() == b.size() || a[b.size()] == '/');
  };
  // one file into `dir`; returns the target (empty on failure)
  auto copyFile = [&](const fs::path& src, const fs::path& dir) -> fs::path {
    const fs::path to = dir / U8(UniqueFileName(dir, Utf8(src.filename())));
    if (!fs::copy_file(src, to, fs::copy_options::none, ec)) {
      r.errors.push_back(std::format("{}: {}", Utf8(src.filename()), ec.message()));
      return {};
    }
    r.added.push_back(rel(to));
    if (Lower(src.extension().string()) == ".gltf" || Lower(src.extension().string()) == ".obj") // bring what it names
      for (const std::string& dep : ModelDependencies(src)) {
        const fs::path from = src.parent_path() / U8(dep), target = dir / U8(dep);
        if (!fs::exists(from, ec)) { r.errors.push_back(std::format("{}: {}이(가) 없음", Utf8(src.filename()), dep)); continue; }
        if (fs::exists(target, ec)) {
          if (!SameBytes(from, target)) r.errors.push_back(std::format("{}: {}이(가) 이미 있고 내용이 달라 덮어쓰지 않음", Utf8(src.filename()), dep));
          continue; // same bytes: shared
        }
        fs::create_directories(target.parent_path(), ec);
        if (fs::copy_file(from, target, ec)) r.added.push_back(rel(target));
        else r.errors.push_back(std::format("{}: {}", dep, ec.message()));
      }
    return to;
  };
  for (const fs::path& src : sources) {
    if (!fs::exists(src, ec)) { r.errors.push_back(std::format("{}: 파일이 없음", Utf8(src))); continue; }
    if (inside(src)) { r.errors.push_back(std::format("{}: 이미 프로젝트 안에 있음", Utf8(src.filename()))); continue; }
    if (fs::is_directory(src, ec)) { // a folder: the same tree under a unique folder name
      const fs::path top = dest / U8(UniqueFileName(dest, Utf8(src.filename())));
      for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::path to = top / fs::relative(it->path(), src, ec);
        if (it->is_directory(ec)) { fs::create_directories(to, ec); continue; }
        if (it->path().extension() == ".meta") continue; // the project makes its own
        fs::create_directories(to.parent_path(), ec);
        if (fs::copy_file(it->path(), to, ec)) r.added.push_back(rel(to));
        else r.errors.push_back(std::format("{}: {}", Utf8(it->path().filename()), ec.message()));
      }
      continue;
    }
    copyFile(src, dest);
  }
  return r;
}

bool AssetRules::Load(const fs::path& path, std::string* err) {
  *this = AssetRules{};
  std::error_code ec;
  if (!fs::exists(path, ec)) return true;
  const std::string text = ReadText(path);
  yyjson_read_err rerr;
  yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), 0, nullptr, &rerr);
  if (!doc) {
    if (err) *err = std::format("{}: {} at {}", Utf8(path), rerr.msg, rerr.pos);
    return false;
  }
  yyjson_val* root = yyjson_doc_get_root(doc);
  size_t i, n;
  yyjson_val *k, *v;
  yyjson_obj_foreach(yyjson_obj_get(root, "prefix"), i, n, k, v)
    if (yyjson_is_str(v)) prefix[yyjson_get_str(k)] = yyjson_get_str(v);
  if (yyjson_val* m = yyjson_obj_get(root, "maxTextureSize"); yyjson_is_num(m)) maxTextureSize = (int)yyjson_get_num(m);
  yyjson_arr_foreach(yyjson_obj_get(root, "requireLicense"), i, n, v)
    if (yyjson_is_str(v)) requireLicense.insert(yyjson_get_str(v));
  yyjson_doc_free(doc);
  return true;
}

std::vector<AssetIssue> AssetRuleChecker::Check(const AssetDatabase& db, const AssetRules& rules, const std::vector<std::string>* only) {
  std::vector<AssetIssue> out;
  std::vector<const AssetRecord*> list;
  if (only) {
    for (const std::string& p : *only)
      if (const AssetRecord* r = db.ByPath(p)) list.push_back(r);
  } else list = db.All();
  const fs::path root = U8(db.Root());
  for (const AssetRecord* r : list) {
    const std::string file = r->path.substr(r->path.find_last_of('/') == std::string::npos ? 0 : r->path.find_last_of('/') + 1);
    if (auto p = rules.prefix.find(r->importer); p != rules.prefix.end() && !p->second.empty() && !file.starts_with(p->second))
      out.push_back({r->path, "prefix", std::format("이름이 {}로 시작해야 함 ({})", p->second, r->importer)});
    if (r->importer == "texture" && rules.maxTextureSize > 0) {
      Size& s = sizes_[r->guid];
      if (s.hash != r->hash || s.w == 0) {
        s = {r->hash, 0, 0};
        if (!ImageSize(root / U8(r->path), s.w, s.h)) s.w = s.h = -1;
      }
      if (std::max(s.w, s.h) > rules.maxTextureSize)
        out.push_back({r->path, "size", std::format("텍스처가 {}보다 큼 ({}×{})", rules.maxTextureSize, s.w, s.h)});
    }
    if (rules.requireLicense.count(r->importer)) {
      const std::string meta = ReadText(root / U8(r->path + ".meta"));
      bool has = false;
      if (yyjson_doc* doc = yyjson_read(meta.data(), meta.size(), 0)) {
        yyjson_val* m = yyjson_doc_get_root(doc);
        const char* lic = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(m, "source"), "license"));
        if (!lic) lic = yyjson_get_str(yyjson_obj_get(m, "license"));
        has = lic && *lic;
        yyjson_doc_free(doc);
      }
      if (!has) out.push_back({r->path, "license", "라이선스 정보 없음 (.meta의 source.license)"});
    }
  }
  return out;
}

} // namespace bdg
