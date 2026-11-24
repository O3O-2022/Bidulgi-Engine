#include "Assets/AssetRefs.h"
#include <yyjson.h>
#include <algorithm>
#include <format>
#include <fstream>
#include <functional>
#include <unordered_map>

namespace bdg {
namespace fs = std::filesystem;
namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string Ext(const std::string& p) {
  size_t dot = p.find_last_of('.'), slash = p.find_last_of('/');
  return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Lower(p.substr(dot));
}
std::string FileName(const std::string& p) {
  size_t slash = p.find_last_of('/');
  return slash == std::string::npos ? p : p.substr(slash + 1);
}
std::string Stem(const std::string& p) {
  std::string f = FileName(p);
  size_t dot = f.find_last_of('.');
  return dot == std::string::npos ? f : f.substr(0, dot);
}
bool IsJsonDoc(const std::string& e) { return e == ".scene" || e == ".material" || e == ".matinst" || e == ".prefab" || e == ".json" || e == ".jsonl"; }
bool IsScript(const std::string& e) { return e == ".cs" || e == ".lua" || e == ".py" || e == ".h" || e == ".hpp" || e == ".cpp" || e == ".c"; }
bool IsMaterial(const std::string& e) { return e == ".material" || e == ".matinst"; }

std::string ReadText(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
fs::path Abs(const AssetDatabase& db, const std::string& rel) {
  return fs::path(std::u8string((const char8_t*)db.Root().c_str())) / fs::path(std::u8string((const char8_t*)rel.c_str()));
}

// Every string value of every JSON line, with a readable place: "g6 Player · MeshRenderer.mesh" for scene lines
// (id, name and the key path), the key path otherwise ("BaseColorMap" for a material's {"param":...,"value"}).
void JsonStrings(const std::string& text, const std::function<void(const std::string& value, const std::string& where)>& fn) {
  size_t pos = 0;
  int line = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string_view l(text.data() + pos, eol - pos);
    pos = eol + 1;
    ++line;
    if (l.find('"') == std::string_view::npos) continue;
    yyjson_doc* doc = yyjson_read(l.data(), l.size(), 0);
    if (!doc) continue;
    yyjson_val* root = yyjson_doc_get_root(doc);
    std::string who;
    if (yyjson_is_obj(root)) {
      const char* id = yyjson_get_str(yyjson_obj_get(root, "id"));
      const char* name = yyjson_get_str(yyjson_obj_get(root, "name"));
      const char* param = yyjson_get_str(yyjson_obj_get(root, "param"));
      if (id) who = std::string(id) + (name ? std::string(" ") + name : std::string());
      else if (param) who = param;
    }
    std::function<void(yyjson_val*, const std::string&)> walk = [&](yyjson_val* v, const std::string& key) {
      if (yyjson_is_str(v)) {
        std::string place = who.empty() ? (key.empty() ? std::format("line {}", line) : key) : (key.empty() ? who : who + " · " + key);
        fn(yyjson_get_str(v), place);
      } else if (yyjson_is_obj(v)) {
        size_t i, n;
        yyjson_val *k, *x;
        yyjson_obj_foreach(v, i, n, k, x) {
          std::string kn = yyjson_get_str(k);
          if (kn == "id" || kn == "name" || (kn == "parent" && yyjson_is_str(x) && std::string_view(yyjson_get_str(x)).starts_with("@"))) continue;
          walk(x, key.empty() ? kn : key + "." + kn);
        }
      } else if (yyjson_is_arr(v)) {
        size_t i, n;
        yyjson_val* x;
        yyjson_arr_foreach(v, i, n, x) walk(x, key);
      }
    };
    walk(root, "");
    yyjson_doc_free(doc);
  }
}

} // namespace

std::vector<std::string> RefTokens(const AssetRecord& r) {
  std::vector<std::string> t = {r.path, FileName(r.path), "guid:" + r.guid, r.guid};
  if (IsMaterial(Ext(r.path))) t.push_back(Stem(r.path));
  return t;
}

bool AssetRefIndex::IsRoot(const std::string& path) {
  const std::string e = Ext(path);
  return e == ".scene" || IsScript(e);
}

void AssetRefIndex::Build(const AssetDatabase& db, const std::vector<std::pair<std::string, std::string>>& extra) {
  refs_.clear();
  assets_.clear();
  roots_.clear();
  extraNames_.clear();
  documents_ = 0;
  std::unordered_map<std::string, std::vector<std::string>> byToken; // token -> asset paths (a file name may be shared)
  const std::vector<const AssetRecord*> all = db.All();
  for (const AssetRecord* r : all) {
    assets_.insert(r->path);
    if (IsRoot(r->path)) roots_.insert(r->path);
    for (const std::string& t : RefTokens(*r)) byToken[t].push_back(r->path);
  }
  auto scan = [&](const std::string& from, const std::string& text, bool json) {
    ++documents_;
    if (json) {
      JsonStrings(text, [&](const std::string& value, const std::string& where) {
        auto it = byToken.find(value);
        if (it == byToken.end()) return;
        for (const std::string& a : it->second)
          if (a != from) refs_.push_back({a, from, where, value});
      });
      return;
    }
    // scripts: quoted names only ("Textures/grass.png", 'Knight.glb')
    for (const auto& [token, paths] : byToken) {
      if (token.size() < 3) continue;
      for (char q : {'"', '\''}) {
        const std::string needle = std::string(1, q) + token + q;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
          const int line = 1 + (int)std::count(text.begin(), text.begin() + (std::ptrdiff_t)at, '\n');
          for (const std::string& a : paths)
            if (a != from) refs_.push_back({a, from, std::format("line {}", line), token});
        }
      }
    }
  };
  for (const AssetRecord* r : all) {
    const std::string e = Ext(r->path);
    if (IsJsonDoc(e)) scan(r->path, ReadText(Abs(db, r->path)), true);
    else if (IsScript(e)) scan(r->path, ReadText(Abs(db, r->path)), false);
  }
  for (const auto& [name, text] : extra) {
    extraNames_.insert(name);
    roots_.insert(name);
    scan(name, text, true);
  }
  std::stable_sort(refs_.begin(), refs_.end(), [](const AssetRef& a, const AssetRef& b) {
    return std::tie(a.asset, a.from) < std::tie(b.asset, b.from);
  });
}

std::vector<AssetRef> AssetRefIndex::RefsTo(const std::string& assetPath) const {
  std::vector<AssetRef> out;
  for (const AssetRef& r : refs_)
    if (r.asset == assetPath) out.push_back(r);
  return out;
}

std::vector<std::string> AssetRefIndex::Unused() const {
  std::map<std::string, std::vector<std::string>> edges; // document -> assets it names
  for (const AssetRef& r : refs_) edges[r.from].push_back(r.asset);
  std::set<std::string> reached(roots_.begin(), roots_.end());
  std::vector<std::string> todo(roots_.begin(), roots_.end());
  while (!todo.empty()) {
    const std::string d = todo.back();
    todo.pop_back();
    for (const std::string& a : edges[d])
      if (reached.insert(a).second) todo.push_back(a);
  }
  std::vector<std::string> out;
  for (const std::string& a : assets_)
    if (!reached.count(a)) out.push_back(a);
  return out;
}

std::string MatchingToken(const AssetRecord& from, const AssetRecord& to, const std::string& token) {
  if (token == from.path) return to.path;
  if (token == "guid:" + from.guid) return "guid:" + to.guid;
  if (token == from.guid) return to.guid;
  if (token == FileName(from.path)) return FileName(to.path);
  if (token == Stem(from.path)) return Stem(to.path);
  return to.path;
}

std::vector<std::string> ReplaceReferencesOnDisk(const AssetDatabase& db, const std::string& fromPath, const std::string& toPath, int* count,
                                                 const std::set<std::string>& skip) {
  std::vector<std::string> changed;
  int total = 0;
  const AssetRecord* from = db.ByPath(fromPath);
  const AssetRecord* to = db.ByPath(toPath);
  if (!from || !to) {
    if (count) *count = 0;
    return changed;
  }
  for (const AssetRecord* r : db.All()) {
    const std::string e = Ext(r->path);
    if (!(IsJsonDoc(e) || IsScript(e)) || r->path == fromPath || skip.count(r->path)) continue;
    const fs::path file = Abs(db, r->path);
    std::string text = ReadText(file);
    int n = 0;
    for (const std::string& token : RefTokens(*from)) {
      const std::string with = MatchingToken(*from, *to, token);
      for (char q : {'"', '\''}) {
        if (IsJsonDoc(e) && q == '\'') continue;
        const std::string needle = std::string(1, q) + token + q, repl = std::string(1, q) + with + q;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + repl.size())) {
          text.replace(at, needle.size(), repl);
          ++n;
        }
      }
    }
    if (!n) continue;
    std::ofstream(file, std::ios::binary) << text;
    changed.push_back(r->path);
    total += n;
  }
  if (count) *count = total;
  return changed;
}

} // namespace bdg
