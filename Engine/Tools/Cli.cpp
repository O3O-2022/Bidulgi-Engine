#include "Tools/Cli.h"
#include <bdg/bdg_api.h>
#include <cstdio>
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImport.h"
#include "Assets/AssetRefs.h"
#include "Core/Command.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Core/Utf8Path.h"
#include "Scene/DefaultScene.h"
#include "Scene/GraphLibrary.h"
#include "Scene/SceneFile.h"
#include "Scripting/CSharpBindings.h"
#include "Scripting/DataTable.h"
#include "Build/Build.h"
#include "Build/Templates.h"
#include <yyjson.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <set>

namespace bdg {
namespace fs = std::filesystem;
namespace {

fs::path U8(const std::string& s) { return fs::path(std::u8string((const char8_t*)s.c_str())); }
std::string Utf8(const fs::path& p) {
  const std::u8string u = p.generic_u8string();
  return std::string((const char*)u.data(), u.size());
}
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string Q(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o + "\"";
}
std::string Trim0(std::string s) { // 1.500000 -> 1.5 in compact output
  if (s.find('.') == std::string::npos || s.find('e') != std::string::npos) return s;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}
// glob with * and ? (case-insensitive)
bool Glob(std::string_view pat, std::string_view s) {
  size_t p = 0, i = 0, star = std::string::npos, mark = 0;
  auto eq = [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
  while (i < s.size()) {
    if (p < pat.size() && (pat[p] == '?' || eq(pat[p], s[i]))) { ++p; ++i; }
    else if (p < pat.size() && pat[p] == '*') { star = p++; mark = i; }
    else if (star != std::string::npos) { p = star + 1; i = ++mark; }
    else return false;
  }
  while (p < pat.size() && pat[p] == '*') ++p;
  return p == pat.size();
}
// JSON value -> compact text: [1,2,3] -> 1,2,3, "x" -> x
std::string Compact(yyjson_val* v) {
  if (yyjson_is_str(v)) return yyjson_get_str(v);
  if (yyjson_is_arr(v)) {
    std::string o;
    size_t i, n;
    yyjson_val* x;
    yyjson_arr_foreach(v, i, n, x) o += (i ? "," : "") + Compact(x);
    return o;
  }
  if (yyjson_is_obj(v)) {
    std::string o = "{";
    size_t i, n;
    yyjson_val *k, *x;
    yyjson_obj_foreach(v, i, n, k, x) o += std::string(i ? " " : "") + yyjson_get_str(k) + "=" + Compact(x);
    return o + "}";
  }
  size_t len = 0;
  char* t = yyjson_val_write(v, 0, &len);
  std::string o = t ? Trim0(std::string(t, len)) : std::string();
  free(t);
  return o;
}
// What the user typed after '=' -> JSON: JSON stays, "6,0,4" -> [6,0,4], other text -> a string
std::string ToJsonValue(const std::string& v) {
  if (yyjson_doc* d = yyjson_read(v.data(), v.size(), 0)) {
    yyjson_doc_free(d);
    return v;
  }
  if (v.find(',') != std::string::npos) {
    const std::string arr = "[" + v + "]";
    if (yyjson_doc* d = yyjson_read(arr.data(), arr.size(), 0)) {
      yyjson_doc_free(d);
      return arr;
    }
  }
  return Q(v);
}
std::string Alias(const std::string& path) {
  if (path == "pos" || path == "position") return "Transform.position";
  if (path == "rot" || path == "rotation") return "Transform.rotation";
  if (path == "scale") return "Transform.scale";
  return path;
}

struct Project {
  fs::path root;
  fs::path scenePath;
  Scene scene;
  std::string sceneName;
  std::vector<std::string> warnings;
};

struct Ctx {
  std::vector<std::string> pos;          // positional arguments
  std::map<std::string, std::vector<std::string>> opt; // --name value (repeatable)
  std::set<std::string> flags;           // --dry-run
  std::string& out;
  std::string& err;
  fs::path cwd;
  bool Has(const std::string& f) const { return flags.count(f) != 0; }
  std::string Opt(const std::string& k, const std::string& def = {}) const {
    auto it = opt.find(k);
    return it == opt.end() || it->second.empty() ? def : it->second.back();
  }
};

fs::path ProjectRoot(const Ctx& c) {
  const std::string p = c.Opt("project");
  return p.empty() ? c.cwd : (U8(p).is_absolute() ? U8(p) : c.cwd / U8(p));
}
fs::path ScenePath(const Ctx& c, const fs::path& root, const std::string& arg) {
  std::string s = arg.empty() ? c.Opt("scene", "Main") : arg;
  fs::path p = U8(s);
  if (p.extension() != ".scene") p = root / "Scenes" / U8(s + ".scene");
  else if (p.is_relative()) p = fs::exists(c.cwd / p) ? c.cwd / p : root / p;
  return p;
}
bool Load(Ctx& c, Project& pr, const std::string& sceneArg) {
  pr.root = ProjectRoot(c);
  pr.scenePath = ScenePath(c, pr.root, sceneArg);
  std::string error;
  if (!fs::exists(pr.scenePath)) {
    c.err += std::format("오류: 장면 파일이 없음: {} → --scene 이름 또는 경로를 주거나 `bdg new`로 만드세요\n", Utf8(pr.scenePath));
    return false;
  }
  if (!LoadSceneFile(pr.scene, Utf8(pr.scenePath), &pr.sceneName, &pr.warnings, &error)) {
    c.err += std::format("오류: {}: {}\n", Utf8(pr.scenePath), error);
    return false;
  }
  return true;
}
std::string KindOf(SceneObject& o) {
  if (o.prefab) return "Prefab";
  std::string k;
  for (const Component& comp : o.components) {
    const std::string& n = comp.type->name;
    if (n == "Camera") return "Camera";
    if (n == "Light") k = "Light";
    else if ((n == "MeshRenderer" || n == "Blockout") && k.empty()) k = "Mesh";
  }
  return k.empty() ? "Empty" : k;
}
// id -> its JSON line in the saved form (only changed fields): what `view` shows
std::map<std::string, std::string> SavedLines(const Scene& scene, const std::string& name) {
  std::map<std::string, std::string> lines;
  const std::string text = WriteSceneText(scene, name);
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    const std::string l = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (yyjson_doc* d = yyjson_read(l.data(), l.size(), 0)) {
      if (const char* id = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(d), "id"))) lines[id] = l;
      yyjson_doc_free(d);
    }
  }
  return lines;
}
void Walk(Scene& s, const std::function<void(SceneObject&, int, const std::string&)>& fn) {
  std::function<void(const std::string&, int, const std::string&)> go = [&](const std::string& id, int depth, const std::string& path) {
    SceneObject* o = s.Find(id);
    if (!o) return;
    const std::string p = path.empty() ? o->name : path + "/" + o->name;
    fn(*o, depth, p);
    for (const std::string& c : o->children) go(c, depth + 1, p);
  };
  for (const std::string& r : s.Roots()) go(r, 0, "");
}
std::vector<std::string> Match(Scene& s, const std::string& pattern) {
  std::vector<std::string> ids;
  if (s.Find(pattern)) return {pattern};
  Walk(s, [&](SceneObject& o, int, const std::string& path) {
    if (Glob(pattern, o.id) || Glob(pattern, o.name) || Glob(pattern, path)) ids.push_back(o.id);
  });
  return ids;
}
bool Save(Ctx& c, Project& pr, CommandBus& bus, const std::string& before) {
  const std::string after = WriteSceneText(pr.scene, pr.sceneName);
  if (c.Has("dry-run")) { // the lines that would change
    auto split = [](const std::string& t) {
      std::vector<std::string> v;
      size_t p = 0;
      while (p < t.size()) {
        size_t e = t.find('\n', p);
        if (e == std::string::npos) e = t.size();
        v.push_back(t.substr(p, e - p));
        p = e + 1;
      }
      return v;
    };
    const auto a = split(before), b = split(after);
    const std::set<std::string> sa(a.begin(), a.end()), sb(b.begin(), b.end());
    for (const std::string& l : a)
      if (!sb.count(l)) c.out += "- " + l + "\n";
    for (const std::string& l : b)
      if (!sa.count(l)) c.out += "+ " + l + "\n";
    c.out += "(--dry-run: 저장하지 않음)\n";
    return true;
  }
  std::string error;
  if (!SaveSceneFile(pr.scene, pr.sceneName, Utf8(pr.scenePath), &error)) {
    c.err += "오류: " + error + "\n";
    return false;
  }
  std::error_code ec;
  fs::create_directories(pr.root / ".cache", ec);
  std::ofstream log(pr.root / ".cache" / "bdg-commands.jsonl", std::ios::binary | std::ios::app);
  for (const std::string& l : bus.Log()) log << l << '\n';
  return true;
}
bool Exec(Ctx& c, CommandBus& bus, const char* name, const std::string& target, const std::string& args) {
  Command cmd;
  cmd.name = name;
  cmd.target = target;
  cmd.args = args;
  cmd.source = "terminal";
  std::string error;
  if (bus.Execute(cmd, &error)) return true;
  c.err += std::format("오류: {} {} → {}\n", name, target, error);
  return false;
}

// ---- commands ----------------------------------------------------------------------------------------------
int CmdView(Ctx& c) {
  Project pr;
  if (!Load(c, pr, c.pos.empty() ? "" : c.pos[0])) return 1;
  const auto lines = SavedLines(pr.scene, pr.sceneName);
  size_t idW = 2;
  Walk(pr.scene, [&](SceneObject& o, int depth, const std::string&) { idW = std::max(idW, o.id.size() + depth * 2); });
  c.out += std::format("# {} ({} objects)\n", pr.sceneName, pr.scene.ObjectCount());
  Walk(pr.scene, [&](SceneObject& o, int depth, const std::string&) {
    std::string row = std::string(depth * 2, ' ') + o.id;
    row += std::string(idW + 2 - row.size(), ' ') + std::format("{:<7}", KindOf(o));
    if (o.name != o.id) row += " #" + o.name;
    std::string tf, comps;
    auto it = lines.find(o.id);
    if (it != lines.end())
      if (yyjson_doc* d = yyjson_read(it->second.data(), it->second.size(), 0)) {
        yyjson_val* root = yyjson_doc_get_root(d);
        size_t i, n;
        yyjson_val *k, *x;
        yyjson_obj_foreach(root, i, n, k, x) {
          const std::string key = yyjson_get_str(k);
          if (key == "id" || key == "name" || key == "parent" || key == "prefab") continue;
          if (key == "pos" || key == "rot" || key == "scale") { tf += "  " + key + " " + Compact(x); continue; }
          comps += " | " + key;
          size_t fi, fn;
          yyjson_val *fk, *fx;
          yyjson_obj_foreach(x, fi, fn, fk, fx) comps += std::string(" ") + yyjson_get_str(fk) + "=" + Compact(fx);
        }
        yyjson_doc_free(d);
      }
    c.out += row + tf + comps + "\n";
  });
  return 0;
}

int CmdOutline(Ctx& c) {
  Project pr;
  if (!Load(c, pr, c.pos.empty() ? "" : c.pos[0])) return 1;
  Walk(pr.scene, [&](SceneObject& o, int depth, const std::string&) {
    std::string initials; // component names (Transform left out: everything has one)
    for (const Component& comp : o.components)
      if (comp.type->name != "Transform") initials += (initials.empty() ? "" : ",") + comp.type->name;
    c.out += std::string(depth * 2, ' ') + o.name + "  (" + o.id + (initials.empty() ? "" : ": " + initials) + ")\n";
  });
  return 0;
}

int CmdQuery(Ctx& c) {
  if (c.pos.empty()) { c.err += "사용법: bdg query <pattern> [-f pos,Health.hp]\n"; return 2; }
  Project pr;
  if (!Load(c, pr, "")) return 1;
  std::vector<std::string> fields;
  for (std::string f = c.Opt("f", "pos"), part; !f.empty();) {
    const size_t comma = f.find(',');
    part = f.substr(0, comma);
    if (!part.empty()) fields.push_back(part);
    f = comma == std::string::npos ? "" : f.substr(comma + 1);
  }
  c.out += "id\tname";
  for (const std::string& f : fields) c.out += "\t" + f;
  c.out += "\n";
  for (const std::string& id : Match(pr.scene, c.pos[0])) {
    SceneObject* o = pr.scene.Find(id);
    c.out += o->id + "\t" + o->name;
    for (const std::string& f : fields) {
      const std::string path = Alias(f);
      const size_t dot = path.find('.');
      std::string v = "-";
      if (Component* comp = dot == std::string::npos ? nullptr : o->Find(path.substr(0, dot))) {
        std::string json;
        if (Registry::Get(comp->data, *comp->type, path.substr(dot + 1), json))
          if (yyjson_doc* d = yyjson_read(json.data(), json.size(), 0)) {
            v = Compact(yyjson_doc_get_root(d));
            yyjson_doc_free(d);
          }
      }
      c.out += "\t" + v;
    }
    c.out += "\n";
  }
  return 0;
}

int CmdSchema(Ctx& c) {
  bool any = false;
  for (const TypeInfo* t : Registry::Instance().Types()) {
    if (std::find(t->tags.begin(), t->tags.end(), "Component") == t->tags.end()) continue;
    if (!c.pos.empty() && Lower(c.pos[0]) != Lower(t->name)) continue;
    any = true;
    void* fresh = t->create ? t->create() : nullptr;
    std::string line = t->name + ":";
    std::vector<const TypeInfo*> chain;
    for (const TypeInfo* x = t; x; x = x->base) chain.insert(chain.begin(), x);
    for (const TypeInfo* x : chain)
      for (const FieldInfo& f : x->fields) {
        if (!(f.flags & kEdit)) continue;
        std::string type = f.value.type == FieldType::Enum && f.value.enumType ? f.value.enumType->name : FieldTypeName(f.value.type);
        std::string def;
        if (fresh && Registry::Get(fresh, *t, f.name, def))
          if (yyjson_doc* d = yyjson_read(def.data(), def.size(), 0)) {
            def = Compact(yyjson_doc_get_root(d));
            yyjson_doc_free(d);
          }
        line += " " + f.name + ":" + type + (def.empty() ? "" : "=" + def);
      }
    if (fresh && t->destroy) t->destroy(fresh);
    c.out += line + "\n";
  }
  if (!any) {
    c.err += std::format("오류: 컴포넌트 '{}'가 없음 → `bdg schema`로 목록을 보세요\n", c.pos.empty() ? "" : c.pos[0]);
    return 1;
  }
  return 0;
}

int CmdSet(Ctx& c) {
  if (c.pos.size() < 2) { c.err += "사용법: bdg set <target> path=value ... (pos / rot / scale / name, 예: Health.hp=20 pos=6,0,4)\n"; return 2; }
  Project pr;
  if (!Load(c, pr, "")) return 1;
  const std::vector<std::string> ids = Match(pr.scene, c.pos[0]);
  if (ids.empty()) { c.err += std::format("오류: '{}'에 맞는 물체가 없음 → `bdg outline`으로 id를 확인하세요\n", c.pos[0]); return 1; }
  const std::string before = WriteSceneText(pr.scene, pr.sceneName);
  CommandBus bus(pr.scene);
  bus.BeginGroup("bdg set " + c.pos[0], "terminal");
  bool ok = true;
  for (const std::string& id : ids)
    for (size_t i = 1; i < c.pos.size() && ok; ++i) {
      const std::string& kv = c.pos[i];
      const size_t eq = kv.find('=');
      if (eq == std::string::npos) { c.err += std::format("오류: '{}'에 '='가 없음 → path=value\n", kv); ok = false; break; }
      const std::string path = Alias(kv.substr(0, eq)), value = kv.substr(eq + 1);
      if (path == "name") ok = Exec(c, bus, "rename", id, std::format(R"({{"name":{}}})", Q(value)));
      else ok = Exec(c, bus, "set", id, std::format(R"({{"path":{},"value":{}}})", Q(path), ToJsonValue(value)));
    }
  bus.EndGroup();
  if (!ok) return 1;
  if (!Save(c, pr, bus, before)) return 1;
  if (!c.Has("dry-run")) c.out += std::format("{}개 물체를 바꿈: {}\n", ids.size(), Utf8(pr.scenePath));
  return 0;
}

int CmdAdd(Ctx& c) {
  if (c.pos.empty()) { c.err += "사용법: bdg add <id> [--name N] [--parent P] [--pos x,y,z] [--comp Type]\n"; return 2; }
  Project pr;
  if (!Load(c, pr, "")) return 1;
  const std::string before = WriteSceneText(pr.scene, pr.sceneName);
  CommandBus bus(pr.scene);
  bus.BeginGroup("bdg add " + c.pos[0], "terminal");
  const std::string id = c.pos[0], parent = c.Opt("parent");
  std::string args = std::format(R"({{"id":{},"name":{})", Q(id), Q(c.Opt("name", id)));
  if (!parent.empty()) args += std::format(R"(,"parent":{})", Q(parent));
  args += "}";
  bool ok = Exec(c, bus, "add", "", args);
  if (ok && !c.Opt("pos").empty()) ok = Exec(c, bus, "set", id, std::format(R"({{"path":"Transform.position","value":{}}})", ToJsonValue(c.Opt("pos"))));
  auto comps = c.opt.find("comp");
  for (size_t i = 0; ok && comps != c.opt.end() && i < comps->second.size(); ++i)
    ok = Exec(c, bus, "addcomp", id, std::format(R"({{"type":{}}})", Q(comps->second[i])));
  bus.EndGroup();
  if (!ok || !Save(c, pr, bus, before)) return 1;
  if (!c.Has("dry-run")) c.out += std::format("추가함: {}\n", id);
  return 0;
}

int CmdSimple(Ctx& c, const char* name) { // del <id>... / addcomp <id> <Type> / rmcomp <id> <Type>
  const bool comp = std::string(name) != "del";
  if (c.pos.empty() || (comp && c.pos.size() < 2)) { c.err += std::format("사용법: bdg {} <id>{}\n", name, comp ? " <Type>" : "..."); return 2; }
  Project pr;
  if (!Load(c, pr, "")) return 1;
  const std::string before = WriteSceneText(pr.scene, pr.sceneName);
  CommandBus bus(pr.scene);
  bus.BeginGroup(std::string("bdg ") + name, "terminal");
  bool ok = true;
  if (comp) ok = Exec(c, bus, name, c.pos[0], std::format(R"({{"type":{}}})", Q(c.pos[1])));
  else
    for (const std::string& pattern : c.pos) {
      const auto ids = Match(pr.scene, pattern);
      if (ids.empty()) { c.err += std::format("오류: '{}'에 맞는 물체가 없음\n", pattern); ok = false; }
      for (const std::string& id : ids)
        if (ok && pr.scene.Find(id)) ok = Exec(c, bus, "del", id, "{}");
    }
  bus.EndGroup();
  return ok && Save(c, pr, bus, before) ? 0 : 1;
}

int CmdValidate(Ctx& c) {
  Project pr;
  if (!Load(c, pr, c.pos.empty() ? "" : c.pos[0])) return 1;
  int errors = 0, warnings = 0;
  const std::string scene = Utf8(fs::relative(pr.scenePath, pr.root));
  // line numbers of objects in the file, for "file:line" messages
  std::map<std::string, int> lineOf;
  {
    std::ifstream in(pr.scenePath, std::ios::binary);
    int n = 0;
    for (std::string l; std::getline(in, l);) {
      ++n;
      if (yyjson_doc* d = yyjson_read(l.data(), l.size(), 0)) {
        if (const char* id = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(d), "id"))) lineOf[id] = n;
        yyjson_doc_free(d);
      }
    }
  }
  for (const std::string& w : pr.warnings) {
    c.out += std::format("경고 {}: {}\n", scene, w);
    ++warnings;
  }
  const fs::path assetsDir = pr.root / "Assets";
  AssetDatabase db;
  const bool haveAssets = fs::exists(assetsDir) && db.Open(Utf8(assetsDir), nullptr, Utf8(pr.root / ".cache" / "assetdb.jsonl"));
  std::set<std::string> tokens; // every name an asset can be referred to by
  if (haveAssets)
    for (const AssetRecord* r : db.All())
      for (const std::string& t : RefTokens(*r)) tokens.insert(t);
  static const std::set<std::string> builtinMeshes = {"plane", "cube", "sphere", "cylinder", "cone"};
  Walk(pr.scene, [&](SceneObject& o, int, const std::string& path) {
    const std::string where = std::format("{}:{} {}", scene, lineOf.count(o.id) ? lineOf[o.id] : 0, path);
    if (Component* mr = o.Find("MeshRenderer")) {
      std::string mesh, mat;
      Registry::Get(mr->data, *mr->type, "mesh", mesh);
      Registry::Get(mr->data, *mr->type, "material", mat);
      auto unq = [](std::string s) { return s.size() >= 2 && s.front() == '"' ? s.substr(1, s.size() - 2) : s; };
      mesh = unq(mesh);
      mat = unq(mat);
      const std::string stem = Lower(U8(mesh).stem().string());
      if (mesh.empty()) { c.out += std::format("경고 {} MeshRenderer.mesh → 비어 있음 → 메시를 정하세요\n", where); ++warnings; }
      else if (!tokens.count(mesh) && !builtinMeshes.count(stem)) {
        c.out += std::format("오류 {} MeshRenderer.mesh → '{}'가 프로젝트에 없음 → 에셋 경로를 확인하거나 Plane·Cube·Sphere·Cylinder·Cone 중 하나를 쓰세요\n", where, mesh);
        ++errors;
      }
      if (!mat.empty() && mat.rfind("bdg:", 0) != 0 && !tokens.count(mat)) {
        c.out += std::format("경고 {} MeshRenderer.material → '{}' 머티리얼 에셋이 없음 → 이름 규칙 색으로 그려짐\n", where, mat);
        ++warnings;
      }
    }
  });
  if (haveAssets) {
    for (const std::string& m : db.OrphanMetas()) {
      c.out += std::format("경고 Assets/{} → 짝 잃은 .meta(원본이 없음) → 원본을 되돌리거나 .meta를 지우세요\n", m);
      ++warnings;
    }
    AssetRules rules;
    std::string rerr;
    if (!rules.Load(pr.root / "ProjectSettings" / "AssetRules.json", &rerr)) { c.out += "오류 " + rerr + "\n"; ++errors; }
    AssetRuleChecker checker;
    for (const AssetIssue& is : checker.Check(db, rules)) {
      c.out += std::format("경고 Assets/{} → {}\n", is.path, is.message);
      ++warnings;
    }
  }
  c.out += std::format("{}: 오류 {}, 경고 {}\n", errors ? "실패" : "통과", errors, warnings);
  return errors ? 1 : 0;
}

int CmdRefs(Ctx& c, bool unused) {
  const fs::path root = ProjectRoot(c), assetsDir = root / "Assets";
  AssetDatabase db;
  if (!fs::exists(assetsDir) || !db.Open(Utf8(assetsDir), nullptr, Utf8(root / ".cache" / "assetdb.jsonl"))) {
    c.err += std::format("오류: 에셋 폴더가 없음: {}\n", Utf8(assetsDir));
    return 1;
  }
  std::vector<std::pair<std::string, std::string>> extra;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(root / "Scenes", ec))
    if (e.path().extension() == ".scene") {
      std::ifstream in(e.path(), std::ios::binary);
      extra.emplace_back("Scenes/" + Utf8(e.path().filename()), std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    }
  AssetRefIndex index;
  index.Build(db, extra);
  if (unused) {
    for (const std::string& p : index.Unused()) c.out += p + "\n";
    return 0;
  }
  if (c.pos.empty()) { c.err += "사용법: bdg refs <asset path>\n"; return 2; }
  const std::vector<AssetRef> refs = index.RefsTo(c.pos[0]);
  for (const AssetRef& r : refs) c.out += std::format("{}\t{}\t{}\n", r.from, r.where, r.token);
  c.out += std::format("{}곳\n", refs.size());
  return 0;
}

// Graph nodes from reflection (E-8): bdg nodes [filter]
int CmdNodes(Ctx& c) {
  const auto lib = BuildGraphNodeLibrary(c.pos.empty() ? "" : c.pos[0]);
  for (const GraphNodeDesc& n : lib) {
    const char* kind = n.kind == GraphNodeDesc::Kind::Call ? "call" : n.kind == GraphNodeDesc::Kind::Pure ? "pure"
                     : n.kind == GraphNodeDesc::Kind::Get ? "get" : "set";
    c.out += std::format("{:<5} {}\n", kind, n.Signature());
  }
  c.out += std::format("{} nodes\n", lib.size());
  return 0;
}

// bdg call <target> <Node.Id> [args...]: runs a node like the Graph will (calls and sets are undoable commands)
int CmdCall(Ctx& c) {
  if (c.pos.size() < 2) { c.err += "사용법: bdg call <target> <Component.Function | Component.Set field | ...> [인자...]\n"; return 2; }
  Project pr;
  if (!Load(c, pr, "")) return 1;
  const auto lib = BuildGraphNodeLibrary();
  const GraphNodeDesc* node = FindGraphNode(lib, c.pos[1]);
  if (!node) { c.err += std::format("오류: 노드 '{}'가 없음 → `bdg nodes {}`로 찾아보세요\n", c.pos[1], c.pos[1]); return 1; }
  std::vector<std::string> args;
  for (size_t i = 2; i < c.pos.size(); ++i) args.push_back(ToJsonValue(c.pos[i]));
  const std::string before = WriteSceneText(pr.scene, pr.sceneName);
  CommandBus bus(pr.scene);
  std::string result, error;
  if (!RunGraphNode(bus, pr.scene, *node, c.pos[0], args, &result, &error)) { c.err += "오류: " + error + "\n"; return 1; }
  if (!result.empty()) c.out += result + "\n";
  const bool changes = node->kind == GraphNodeDesc::Kind::Call || node->kind == GraphNodeDesc::Kind::Set;
  return !changes || Save(c, pr, bus, before) ? 0 : 1;
}

// bdg csharp-bindings <file>: C# classes for the reflected components (4-1); rewrites only when the text changed
int CmdCSharpBindings(Ctx& c) {
  const std::string text = GenerateCSharpBindings();
  if (c.pos.empty()) { c.out += text; return 0; }
  const fs::path out = U8(c.pos[0]).is_absolute() ? U8(c.pos[0]) : c.cwd / U8(c.pos[0]);
  std::string old;
  if (std::ifstream in(out, std::ios::binary); in) old.assign(std::istreambuf_iterator<char>(in), {});
  if (old == text) return 0;
  std::error_code ec;
  if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
  std::ofstream f(out, std::ios::binary);
  if (!(f << text)) { c.err += std::format("오류: 쓸 수 없음: {}\n", Utf8(out)); return 1; }
  return 0;
}

// bdg table import <file.csv> [out.table.jsonl] | export <table> [out.csv] | check <table> (data tables, 4-4d)
int CmdTable(Ctx& c) {
  if (c.pos.size() < 2 || (c.pos[0] != "import" && c.pos[0] != "export" && c.pos[0] != "check")) {
    c.err += "사용법: bdg table import <파일.csv> [출력.table.jsonl] | export <파일.table.jsonl> [출력.csv] | check <파일>\n";
    return 2;
  }
  const fs::path in = U8(c.pos[1]).is_absolute() ? U8(c.pos[1]) : c.cwd / U8(c.pos[1]);
  std::string text;
  if (!ReadTextU8(Utf8(in), text)) { c.err += std::format("오류: 읽을 수 없음: {}\n", Utf8(in)); return 1; }
  DataTable t;
  std::string stem = Utf8(in.filename());
  for (const char* ext : {".table.jsonl", ".csv"})
    if (stem.size() > std::strlen(ext) && stem.ends_with(ext)) stem.resize(stem.size() - std::strlen(ext));
  t.name = stem;
  std::vector<std::string> problems;
  std::string why;
  const bool csv = Utf8(in).ends_with(".csv");
  if (!(csv ? ImportCsv(text, t, &why, &problems) : ParseDataTable(text, t, &problems))) {
    c.err += std::format("오류: {}: {}\n", Utf8(in), why.empty() ? "표가 아님" : why);
    return 1;
  }
  for (const std::string& p : problems) c.err += std::format("경고: {}\n", p);
  if (c.pos[0] == "check") {
    c.out += std::format("{}: 행 {}개, 열 {}개 (키 {}), 문제 {}개\n", t.name, t.rows.size(), t.columns.size(), t.key, problems.size());
    return problems.empty() ? 0 : 1;
  }
  const std::string result = c.pos[0] == "import" ? WriteDataTable(t) : ExportCsv(t);
  fs::path out = c.pos.size() > 2 ? (U8(c.pos[2]).is_absolute() ? U8(c.pos[2]) : c.cwd / U8(c.pos[2]))
                                  : in.parent_path() / U8(t.name + (c.pos[0] == "import" ? ".table.jsonl" : ".csv"));
  std::error_code ec;
  if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
  std::ofstream f(out, std::ios::binary);
  if (!(f << result)) { c.err += std::format("오류: 쓸 수 없음: {}\n", Utf8(out)); return 1; }
  c.out += std::format("{} -> {} (행 {}개)\n", Utf8(in), Utf8(out), t.rows.size());
  return 0;
}

// bdg build [--check] [--out <dir>] [--config Debug|Development|Shipping] [--zip] [--scripts a.dll;b.dll] (7-4, 7-4b)
int CmdBuild(Ctx& c) {
  BuildOptions o;
  o.project = Utf8(ProjectRoot(c));
  o.output = c.Opt("out");
  o.config = c.Opt("config");
  o.playerExe = c.Opt("player");
  if (c.Has("zip")) o.zip = 1;
  const std::string scripts = c.Opt("scripts"); // kept alive: the view below reads it (a temporary dangled)
  for (std::string_view rest = scripts; !rest.empty();) {
    const size_t semi = rest.find(';');
    if (semi != 0) o.scriptDlls.emplace_back(rest.substr(0, semi));
    rest = semi == std::string_view::npos ? std::string_view() : rest.substr(semi + 1);
  }
  const bool check = c.Has("check");
  const BuildResult r = check ? CheckBuild(o) : BuildGame(o);
  for (const BuildProblem& p : r.problems) c.out += p.Line() + "\n";
  if (!r.ok) {
    c.out += std::format("빌드 {}: 오류 {}개, 경고 {}개 (위의 줄마다 원인과 고치는 법)\n", check ? "검사 실패" : "실패", r.Errors(), r.Warnings());
    return 1;
  }
  if (check) c.out += std::format("검사 통과: 오류 0개, 경고 {}개 → bdg build로 빌드하세요 (출력: {})\n", r.Warnings(), r.outDir);
  else
    c.out += std::format("빌드 완료: {} (파일 {}개, game.bpak {:.1f} MB, 경고 {}개)\n보고서: {}\n{}", r.exePath, r.files, r.pakBytes / (1024.0 * 1024.0),
                         r.Warnings(), r.reportPath, r.zipPath.empty() ? std::string() : "zip: " + r.zipPath + "\n");
  return 0;
}

int CmdNew(Ctx& c) {
  if (c.pos.empty()) { c.err += "사용법: bdg new <dir> [--template basic3d|empty|fps|platformer2d] [--name 게임 이름]\n"; return 2; }
  if (const std::string game = c.Opt("template"); IsGameTemplate(game)) { // a playable game (templates, §4)
    TemplateResult r;
    std::string error;
    const fs::path at = U8(c.pos[0]).is_absolute() ? U8(c.pos[0]) : c.cwd / U8(c.pos[0]);
    if (!CreateFromTemplate(game, Utf8(at), c.Opt("name"), r, &error)) { c.err += "오류: " + error + "\n"; return 1; }
    c.out += std::format("새 게임 프로젝트 ({}): {} — 파일 {}개\n", game, Utf8(at), r.files.size());
    if (r.scriptsBuilt) c.out += "C# 스크립트 빌드됨 (.cache/Scripts). 에디터에서 이 폴더를 열고 Play, 또는 bdg build.\n";
    else c.out += "C# 스크립트를 빌드하지 못함 — 이 폴더에서 dotnet build Game.csproj\n" + r.scriptLog + "\n";
    return 0;
  }
  SceneTemplate t = SceneTemplate::Basic3D;
  if (!ParseSceneTemplate(c.Opt("template", "basic3d"), t)) {
    std::string games;
    for (const GameTemplateInfo& g : GameTemplates()) games += ", " + g.name;
    c.err += "오류: 템플릿은 basic3d, empty" + games + "\n";
    return 2;
  }
  const fs::path dir = U8(c.pos[0]).is_absolute() ? U8(c.pos[0]) : c.cwd / U8(c.pos[0]);
  std::error_code ec;
  if (fs::exists(dir / "Scenes" / "Main.scene", ec)) { c.err += std::format("오류: 이미 프로젝트가 있음: {}\n", Utf8(dir)); return 1; }
  for (const char* sub : {"Assets", "ProjectSettings", "Scenes"}) fs::create_directories(dir / sub, ec);
  std::string error;
  if (!WriteDefaultScene(Utf8(dir / "Scenes" / "Main.scene"), t, "Main", &error)) { c.err += "오류: " + error + "\n"; return 1; }
  if (!fs::exists(dir / ".gitignore")) std::ofstream(dir / ".gitignore", std::ios::binary) << ".cache/\n";
  c.out += std::format("새 프로젝트 ({}): {}\n", SceneTemplateName(t), Utf8(dir));
  return 0;
}

const char* kHelp =
    "bdg — 비둘기 엔진 명령줄 (에디터와 같은 명령 버스)\n"
    "  new <dir> [--template basic3d|empty|fps|platformer2d] [--name 게임 이름]   (fps, platformer2d = 바로 할 수 있는 게임 템플릿)\n"
    "  view [scene] | outline [scene] | query <pattern> [-f pos,Health.hp] | schema [Type]\n"
    "  set <target> path=value ... [--dry-run] | add <id> [--name N] [--parent P] [--pos x,y,z] [--comp Type]\n"
    "  del <id>... | addcomp <id> <Type> | rmcomp <id> <Type>\n"
    "  validate [scene] | refs <asset> | unused\n"
    "  csharp-bindings [file]  (C# 클래스 생성)\n"
    "  table import <csv> [out] | export <table.jsonl> [out] | check <file>  (데이터 테이블)\n"
    "  build [--check] [--out <dir>] [--config Debug|Development|Shipping] [--zip] [--scripts a.dll;b.dll]  (게임 빌드)\n"
    "  nodes [filter] | call <target> <Component.Function | Component.Get x | Component.Set x> [인자...]\n"
    "공통: --project <dir> --scene <이름 또는 경로>\n";

} // namespace

int RunCli(const std::vector<std::string>& args, std::string& out, std::string& err, const fs::path& cwd) {
  Ctx c{{}, {}, {}, out, err, cwd};
  if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
    out += kHelp;
    return args.empty() ? 2 : 0;
  }
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "--dry-run" || a == "--check" || a == "--zip") c.flags.insert(a.substr(2));
    else if ((a.rfind("--", 0) == 0 || a == "-f") && i + 1 < args.size()) c.opt[a.substr(a[1] == '-' ? 2 : 1)].push_back(args[++i]);
    else c.pos.push_back(a);
  }
  const std::string& cmd = args[0];
  if (cmd == "view") return CmdView(c);
  if (cmd == "outline") return CmdOutline(c);
  if (cmd == "query") return CmdQuery(c);
  if (cmd == "schema") return CmdSchema(c);
  if (cmd == "set") return CmdSet(c);
  if (cmd == "add") return CmdAdd(c);
  if (cmd == "del" || cmd == "addcomp" || cmd == "rmcomp") return CmdSimple(c, cmd.c_str());
  if (cmd == "validate") return CmdValidate(c);
  if (cmd == "refs") return CmdRefs(c, false);
  if (cmd == "unused") return CmdRefs(c, true);
  if (cmd == "new") return CmdNew(c);
  if (cmd == "nodes") return CmdNodes(c);
  if (cmd == "call") return CmdCall(c);
  if (cmd == "csharp-bindings") return CmdCSharpBindings(c);
  if (cmd == "table") return CmdTable(c);
  if (cmd == "build") return CmdBuild(c);
  err += std::format("오류: 모르는 명령 '{}'\n", cmd);
  out += kHelp;
  return 2;
}

} // namespace bdg

void bdg_reflect_register_BidulgiEngine(); // generated by bdg_reflect() (CMakeLists.txt)

extern "C" int bdg_cli_main(int argc, const char* const* argv) {
  static bool registered = false;
  if (!registered) {
    bdg::SetLogForCommandLine(true); // stdout is the command's output; no Bidulgi.log in the project
    bdg_reflect_register_BidulgiEngine();
    registered = true;
  }
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i] ? argv[i] : "");
  std::string out, err;
  const int code = bdg::RunCli(args, out, err, std::filesystem::current_path());
  if (!out.empty()) std::fwrite(out.data(), 1, out.size(), stdout);
  if (!err.empty()) std::fwrite(err.data(), 1, err.size(), stderr);
  std::fflush(stdout);
  return code;
}
