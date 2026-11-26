#include "Tools/Mcp.h"
#include "Tools/Cli.h"
#include "Tools/LiveLink.h"
#include <yyjson.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {
using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
Doc Parse(std::string_view s) { return Doc(yyjson_read(s.data(), s.size(), 0), yyjson_doc_free); }

std::string Q(std::string_view s) { // a JSON string
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20) o += std::format("\\u{:04x}", (unsigned)c);
    else o += (char)c;
  }
  return o + "\"";
}
std::string Raw(yyjson_val* v) {
  if (!v) return "null";
  size_t n = 0;
  char* s = yyjson_val_write(v, 0, &n);
  std::string r = s ? std::string(s, n) : "null";
  std::free(s);
  return r;
}
std::string Str(yyjson_val* o, const char* k) {
  const char* s = yyjson_get_str(yyjson_obj_get(o, k));
  return s ? s : "";
}
bool Bool(yyjson_val* o, const char* k) { return yyjson_get_bool(yyjson_obj_get(o, k)); }
std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}
fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
fs::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  return fs::path(std::wstring(buf, n)).parent_path();
#else
  return fs::current_path();
#endif
}
// A JSON value as `bdg set` text: arrays of numbers as "x,y,z", strings as they are, the rest as JSON.
std::string CliValue(yyjson_val* v) {
  if (yyjson_is_str(v)) return yyjson_get_str(v);
  if (yyjson_is_arr(v)) {
    std::string s;
    size_t i, n;
    yyjson_val* x;
    bool numbers = true;
    yyjson_arr_foreach(v, i, n, x) numbers = numbers && yyjson_is_num(x);
    if (numbers) {
      yyjson_arr_foreach(v, i, n, x) s += (i ? "," : "") + Raw(x);
      return s;
    }
  }
  return Raw(v);
}

struct Tool {
  const char* name;
  const char* description;
  const char* schema; // inputSchema
  bool edits;         // changes the project (dry_run supported, live editor forwarding)
};
const char* kScene = R"J("scene":{"type":"string","description":"Scene name in Scenes/ or a .scene path (default Main)"})J";
const char* kDry = R"("dry_run":{"type":"boolean","description":"Show the lines that would change, save nothing"})";

const std::vector<Tool>& Tools() {
  static const std::vector<Tool> tools = {
      {"scene_view", "The open scene, one object per line (id, name, kind, changed fields). Start here.",
       R"J({"type":"object","properties":{SCENE}})J", false},
      {"scene_outline", "The scene as a tree of names with component initials.", R"J({"type":"object","properties":{SCENE}})J", false},
      {"query", "A table of fields for the objects matching a pattern (id, name or path glob).",
       R"J({"type":"object","properties":{"pattern":{"type":"string","description":"id, name or path glob, e.g. \"Enemy*\""},"fields":{"type":"string","description":"Comma list, e.g. pos,Health.hp"},SCENE},"required":["pattern"]})J",
       false},
      {"schema", "Component types: fields, types and defaults (all, or one type).",
       R"J({"type":"object","properties":{"type":{"type":"string","description":"Component type, e.g. Light"}}})J", false},
      {"set", "Sets fields on objects through the command bus (one undo step). values: {\"Health.hp\": 20, \"pos\": [6,0,4], \"name\": \"Boss\"}.",
       R"J({"type":"object","properties":{"target":{"type":"string","description":"Object id, name or glob"},"values":{"type":"object","description":"path -> value (pos / rot / scale / name shortcuts)"},DRY,SCENE},"required":["target","values"]})J",
       true},
      {"add", "Adds an object (optionally with components and a position).",
       R"J({"type":"object","properties":{"id":{"type":"string"},"name":{"type":"string"},"parent":{"type":"string"},"pos":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"components":{"type":"array","items":{"type":"string"}},DRY,SCENE},"required":["id"]})J",
       true},
      {"del", "Deletes objects (and their children).",
       R"J({"type":"object","properties":{"ids":{"type":"array","items":{"type":"string"},"description":"ids, names or globs"},DRY,SCENE},"required":["ids"]})J", true},
      {"addcomp", "Adds a component to an object.",
       R"J({"type":"object","properties":{"id":{"type":"string"},"type":{"type":"string"},DRY,SCENE},"required":["id","type"]})J", true},
      {"rmcomp", "Removes a component from an object.",
       R"J({"type":"object","properties":{"id":{"type":"string"},"type":{"type":"string"},DRY,SCENE},"required":["id","type"]})J", true},
      {"validate", "Checks the scene, references, orphan .meta files and asset rules.", R"J({"type":"object","properties":{SCENE}})J", false},
      {"shot", "A picture of the scene (PNG, returned as an image) from the editor's view, a camera or every bookmark.",
       R"J({"type":"object","properties":{"camera":{"type":"string","description":"yaw,pitch,dist[,x,y,z]"},"quality":{"type":"string","description":"최소|낮음|보통|높음|최고 (or potato..ultra)"},"bookmarks":{"type":"boolean"},SCENE}})J",
       false},
      {"run_check", "Runs one automated editor check (BDG_*_TEST) and returns its result line(s).",
       R"J({"type":"object","properties":{"test":{"type":"string","description":"e.g. BDG_FPS_TEST"},"frames":{"type":"integer","description":"Editor frames (default 120)"},"env":{"type":"object","description":"Extra environment variables"}},"required":["test"]})J",
       false},
      {"build", "Builds the game (or only checks it).",
       R"J({"type":"object","properties":{"check":{"type":"boolean","description":"Only check, build nothing"},"config":{"type":"string","enum":["Debug","Development","Shipping"]},"zip":{"type":"boolean"}}})J",
       false},
      {"read_log", "The end of the project's Bidulgi.log (the editor's last run).",
       R"J({"type":"object","properties":{"lines":{"type":"integer","description":"How many lines (default 80)"}}})J", false},
      {"list_docs", "The engine's AI reference and docs (llms.txt, api/*, features/*).", R"J({"type":"object","properties":{}})J", false},
      {"read_doc", "Reads one doc by the name list_docs gives (e.g. llms.txt, api/components.md).",
       R"J({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})J", false},
  };
  return tools;
}
std::string SchemaOf(const Tool& t) { // SCENE / DRY placeholders -> the shared properties
  std::string s = t.schema;
  const std::pair<const char*, const char*> parts[] = {{"SCENE", kScene}, {"DRY", kDry}};
  for (const auto& [key, value] : parts)
    for (size_t at = s.find(key); at != std::string::npos; at = s.find(key)) s.replace(at, std::strlen(key), value);
  return s;
}

std::string Reply(const std::string& id, const std::string& result) { return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", id, result); }
std::string Error(const std::string& id, int code, const std::string& message) {
  return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":{},"message":{}}}}})", id, code, Q(message));
}
} // namespace

std::string Base64(const unsigned char* d, size_t n) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  o.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += k[v >> 18 & 63];
    o += k[v >> 12 & 63];
    o += i + 1 < n ? k[v >> 6 & 63] : '=';
    o += i + 2 < n ? k[v & 63] : '=';
  }
  return o;
}

McpServer::McpServer(fs::path project, fs::path cwd) : project_(std::move(project)), cwd_(std::move(cwd)) {
  cli = [this](const std::vector<std::string>& args, std::string& out, std::string& err) { return RunCli(args, out, err, cwd_); };
}

fs::path McpServer::DocsDir() const { // the engine's Docs (next to the tools in a source tree or an install)
  std::error_code ec;
  for (fs::path p = ExeDir(); !p.empty(); p = p.parent_path()) {
    if (fs::exists(p / "Docs" / "llms.txt", ec)) return p / "Docs";
    if (p == p.parent_path()) break;
  }
  return {};
}

std::string McpServer::Handle(std::string_view message) {
  Doc doc = Parse(message);
  yyjson_val* root = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
  if (!root) return Error("null", -32700, "parse error: not JSON");
  if (yyjson_is_arr(root)) { // a batch
    std::string out;
    size_t i, n;
    yyjson_val* m;
    yyjson_arr_foreach(root, i, n, m) {
      const std::string r = Handle(Raw(m));
      if (!r.empty()) out += (out.empty() ? "" : ",") + r;
    }
    return out.empty() ? std::string() : "[" + out + "]";
  }
  if (!yyjson_is_obj(root)) return Error("null", -32600, "invalid request");
  yyjson_val* idv = yyjson_obj_get(root, "id");
  const std::string method = Str(root, "method");
  if (method.empty()) return idv ? Error(Raw(idv), -32600, "invalid request: no method") : std::string(); // a reply to us: ignored
  if (!idv) { // a notification: no answer
    if (method == "notifications/initialized") initialized = true;
    return {};
  }
  return Call(Raw(idv), method, Raw(yyjson_obj_get(root, "params")));
}

std::string McpServer::Call(const std::string& id, const std::string& method, const std::string& params) {
  if (method == "initialize") {
    Doc p = Parse(params);
    const std::string asked = Str(p ? yyjson_doc_get_root(p.get()) : nullptr, "protocolVersion");
    for (const char* v : {"2025-06-18", "2025-03-26", "2024-11-05"})
      if (asked == v) protocol = v;
    return Reply(id, std::format(
        R"({{"protocolVersion":{},"capabilities":{{"tools":{{"listChanged":false}},"resources":{{"listChanged":false}}}},"serverInfo":{{"name":"bidulgi","title":"Bidulgi Engine","version":"0.1"}},"instructions":{}}})",
        Q(protocol),
        Q(std::format("Bidulgi Engine project {}. Start with scene_view; edit with set / add / del (dry_run first); check with validate, shot "
                      "and run_check. The engine reference is the llms.txt resource (read_doc llms.txt).",
                      U(project_)))));
  }
  if (method == "ping") return Reply(id, "{}");
  if (method == "tools/list") return Reply(id, ToolsList());
  if (method == "tools/call") {
    std::string error;
    const std::string r = ToolCall(params, error);
    return error.empty() ? Reply(id, r) : Error(id, -32602, error);
  }
  if (method == "resources/list") return Reply(id, ResourcesList());
  if (method == "resources/templates/list") return Reply(id, R"({"resourceTemplates":[]})");
  if (method == "resources/read") {
    std::string error;
    const std::string r = ResourceRead(params, error);
    return error.empty() ? Reply(id, r) : Error(id, -32002, error);
  }
  if (method == "prompts/list") return Reply(id, R"({"prompts":[]})");
  return Error(id, -32601, "method not found: " + method);
}

std::string McpServer::ToolsList() const {
  std::string list;
  for (const Tool& t : Tools()) {
    list += std::format(R"({}{{"name":{},"description":{},"inputSchema":{})", list.empty() ? "" : ",", Q(t.name), Q(t.description), SchemaOf(t));
    if (!t.edits) list += R"(,"annotations":{"readOnlyHint":true})";
    list += "}";
  }
  return "{\"tools\":[" + list + "]}";
}

std::string McpServer::ToolCall(const std::string& params, std::string& error) {
  Doc p = Parse(params);
  yyjson_val* root = p ? yyjson_doc_get_root(p.get()) : nullptr;
  const std::string name = Str(root, "name");
  yyjson_val* a = yyjson_obj_get(root, "arguments");
  const Tool* tool = nullptr;
  for (const Tool& t : Tools())
    if (name == t.name) tool = &t;
  if (!tool) { error = "unknown tool: " + name; return {}; }
  auto need = [&](const char* k) {
    if (Str(a, k).empty() && !yyjson_obj_get(a, k)) error = std::format("{}: \"{}\" is required", name, k);
    return error.empty();
  };
  // text-only results (read_log, docs)
  auto text = [](const std::string& t, bool isError = false) {
    return std::format(R"({{"content":[{{"type":"text","text":{}}}],"isError":{}}})", Q(t), isError ? "true" : "false");
  };
  if (name == "read_log") {
    const int lines = yyjson_is_int(yyjson_obj_get(a, "lines")) ? (int)yyjson_get_int(yyjson_obj_get(a, "lines")) : 80;
    std::ifstream in(project_ / "Bidulgi.log", std::ios::binary);
    if (!in) return text("No Bidulgi.log in the project yet (it is written when the editor runs there).", true);
    std::vector<std::string> all;
    for (std::string l; std::getline(in, l);) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      all.push_back(l);
    }
    std::string out;
    for (size_t i = all.size() > (size_t)std::max(lines, 1) ? all.size() - (size_t)std::max(lines, 1) : 0; i < all.size(); ++i) out += all[i] + "\n";
    return text(out);
  }
  if (name == "list_docs" || name == "read_doc") {
    const fs::path docs = DocsDir();
    if (docs.empty()) return text("The engine's Docs folder was not found next to bdg.", true);
    if (name == "list_docs") {
      std::string out = "llms.txt\n";
      std::error_code ec;
      for (const char* sub : {"api", "features"}) {
        std::vector<std::string> files;
        for (const auto& f : fs::directory_iterator(docs / sub, ec))
          if (f.path().extension() == ".md" || f.path().extension() == ".txt") files.push_back(std::string(sub) + "/" + U(f.path().filename()));
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) out += f + "\n";
      }
      return text(out);
    }
    const std::string doc = Str(a, "name");
    if (doc.empty() || doc.find("..") != std::string::npos || P(doc).is_absolute()) { error = "read_doc: a name from list_docs"; return {}; }
    std::ifstream in(docs / P(doc), std::ios::binary);
    if (!in) return text("No doc " + doc + " (see list_docs).", true);
    std::stringstream ss;
    ss << in.rdbuf();
    return text(ss.str());
  }
  // the rest: a `bdg` command
  std::vector<std::string> args;
  const std::string scene = Str(a, "scene");
  if (name == "scene_view") args = {"view"};
  else if (name == "scene_outline") args = {"outline"};
  else if (name == "query") {
    if (!need("pattern")) return {};
    args = {"query", Str(a, "pattern")};
    if (!Str(a, "fields").empty()) args.insert(args.end(), {"-f", Str(a, "fields")});
  } else if (name == "schema") {
    args = {"schema"};
    if (!Str(a, "type").empty()) args.push_back(Str(a, "type"));
  } else if (name == "set") {
    if (!need("target") || !need("values")) return {};
    yyjson_val* values = yyjson_obj_get(a, "values");
    if (!yyjson_is_obj(values)) { error = "set: values is an object {\"path\": value}"; return {}; }
    args = {"set", Str(a, "target")};
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(values, i, n, k, v) args.push_back(std::string(yyjson_get_str(k)) + "=" + CliValue(v));
  } else if (name == "add") {
    if (!need("id")) return {};
    args = {"add", Str(a, "id")};
    if (!Str(a, "name").empty()) args.insert(args.end(), {"--name", Str(a, "name")});
    if (!Str(a, "parent").empty()) args.insert(args.end(), {"--parent", Str(a, "parent")});
    if (yyjson_val* pos = yyjson_obj_get(a, "pos")) args.insert(args.end(), {"--pos", CliValue(pos)});
    size_t i, n;
    yyjson_val* c;
    yyjson_arr_foreach(yyjson_obj_get(a, "components"), i, n, c)
      if (yyjson_is_str(c)) args.insert(args.end(), {"--comp", yyjson_get_str(c)});
  } else if (name == "del") {
    if (!need("ids")) return {};
    args = {"del"};
    size_t i, n;
    yyjson_val* x;
    yyjson_arr_foreach(yyjson_obj_get(a, "ids"), i, n, x)
      if (yyjson_is_str(x)) args.push_back(yyjson_get_str(x));
    if (args.size() == 1) { error = "del: ids is a list of ids"; return {}; }
  } else if (name == "addcomp" || name == "rmcomp") {
    if (!need("id") || !need("type")) return {};
    args = {name, Str(a, "id"), Str(a, "type")};
  } else if (name == "validate") {
    args = {"validate"};
  } else if (name == "shot") {
    args = {"shot"};
    if (!Str(a, "camera").empty()) args.insert(args.end(), {"--camera", Str(a, "camera")});
    if (!Str(a, "quality").empty()) args.insert(args.end(), {"--quality", Str(a, "quality")});
    if (Bool(a, "bookmarks")) args.push_back("--bookmarks");
  } else if (name == "run_check") {
    if (!need("test")) return {};
    args = {"check", Str(a, "test")};
    if (yyjson_is_int(yyjson_obj_get(a, "frames"))) args.insert(args.end(), {"--frames", std::to_string(yyjson_get_int(yyjson_obj_get(a, "frames")))});
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(yyjson_obj_get(a, "env"), i, n, k, v) args.insert(args.end(), {"--env", std::string(yyjson_get_str(k)) + "=" + CliValue(v)});
  } else if (name == "build") {
    args = {"build"};
    if (Bool(a, "check")) args.push_back("--check");
    if (Bool(a, "zip")) args.push_back("--zip");
    if (!Str(a, "config").empty()) args.insert(args.end(), {"--config", Str(a, "config")});
  }
  if (!scene.empty()) args.insert(args.end(), {"--scene", scene});
  if (tool->edits && Bool(a, "dry_run")) args.push_back("--dry-run");
  // live editor: the open scene's tools go to the editor (its command bus, its undo, its unsaved state); the files
  // stay as the editor saves them. Another scene, validate, shot, build, check: files.
  const bool sceneTool = tool->edits || name == "scene_view" || name == "scene_outline" || name == "query";
  if (sceneTool && scene.empty() && live) {
    std::string req = "[";
    for (size_t i = 0; i < args.size(); ++i) req += (i ? "," : "") + Q(args[i]);
    const std::string answer = live(std::format(R"({{"cli":{}]}})", req));
    if (!answer.empty()) {
      Doc ad = Parse(answer);
      const bool ok = ad && yyjson_get_bool(yyjson_obj_get(yyjson_doc_get_root(ad.get()), "ok"));
      return std::format(R"({{"content":[{{"type":"text","text":{}}}],"structuredContent":{},"isError":{}}})", Q(answer), answer, ok ? "false" : "true");
    }
  }
  args.insert(args.end(), {"--project", U(project_), "--json"});
  std::string out, err;
  const int code = cli(args, out, err);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  Doc od = Parse(out);
  const bool isObject = od && yyjson_is_obj(yyjson_doc_get_root(od.get()));
  const std::string structured = isObject ? out : std::format(R"({{"ok":{},"exit":{},"output":[{}],"stderr":{}}})", code == 0 ? "true" : "false", code, "", Q(err));
  std::string content = std::format(R"({{"type":"text","text":{}}})", Q(isObject ? out : out + err));
  if (name == "shot" && code == 0 && isObject) { // the picture itself, for the model to look at
    yyjson_val* lines = yyjson_obj_get(yyjson_doc_get_root(od.get()), "output");
    size_t i, n;
    yyjson_val* l;
    int images = 0;
    yyjson_arr_foreach(lines, i, n, l) {
      const char* path = yyjson_get_str(l);
      if (!path || images >= 4) continue;
      std::ifstream in(P(path), std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (bytes.empty()) continue;
      content += std::format(R"(,{{"type":"image","data":"{}","mimeType":"image/png"}})", Base64(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
      ++images;
    }
  }
  return std::format(R"({{"content":[{}],"structuredContent":{},"isError":{}}})", content, structured, code == 0 ? "false" : "true");
}

std::string McpServer::ResourcesList() const {
  std::string list = R"({"uri":"bidulgi://docs/llms.txt","name":"llms.txt","title":"Bidulgi Engine AI reference","description":"Components, bdg commands and the C# API in one file","mimeType":"text/plain"})";
  list += R"J(,{"uri":"bidulgi://schema/components","name":"components","title":"Component schema","description":"Every component type with fields, types and defaults (bdg schema --json)","mimeType":"application/json"})J";
  list += std::format(R"(,{{"uri":"bidulgi://project/scene","name":"scene","title":"The project's main scene","description":{},"mimeType":"application/json"}})",
                      Q("bdg view of " + U(project_)));
  return "{\"resources\":[" + list + "]}";
}

std::string McpServer::ResourceRead(const std::string& params, std::string& error) {
  Doc p = Parse(params);
  const std::string uri = Str(p ? yyjson_doc_get_root(p.get()) : nullptr, "uri");
  std::string text, mime = "application/json";
  if (uri == "bidulgi://docs/llms.txt") {
    std::ifstream in(DocsDir() / "llms.txt", std::ios::binary);
    if (!in) { error = "llms.txt not found next to bdg"; return {}; }
    std::stringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    mime = "text/plain";
  } else if (uri == "bidulgi://schema/components" || uri == "bidulgi://project/scene") {
    std::string out, err;
    cli({uri.ends_with("components") ? "schema" : "view", "--project", U(project_), "--json"}, out, err);
    while (!out.empty() && out.back() == '\n') out.pop_back();
    text = out;
  } else {
    error = "unknown resource: " + uri;
    return {};
  }
  return std::format(R"({{"contents":[{{"uri":{},"mimeType":{},"text":{}}}]}})", Q(uri), Q(mime), Q(text));
}

int McpServer::RunStdio(std::FILE* in, std::FILE* out) {
  std::string line;
  for (int ch; (ch = std::fgetc(in)) != EOF;) {
    if (ch != '\n') { line += (char)ch; continue; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) {
      const std::string reply = Handle(line);
      if (!reply.empty()) {
        std::fwrite(reply.data(), 1, reply.size(), out);
        std::fputc('\n', out);
        std::fflush(out);
      }
    }
    line.clear();
  }
  if (!line.empty()) {
    const std::string reply = Handle(line);
    if (!reply.empty()) { std::fwrite(reply.data(), 1, reply.size(), out); std::fputc('\n', out); std::fflush(out); }
  }
  return 0;
}

int RunMcp(const std::vector<std::string>& args, const fs::path& cwd) {
  fs::path project = cwd;
  for (size_t i = 0; i + 1 < args.size(); ++i)
    if (args[i] == "--project") project = P(args[i + 1]).is_absolute() ? P(args[i + 1]) : cwd / P(args[i + 1]);
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY); // exact bytes: UTF-8, \n line ends
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  McpServer server(project, cwd);
  server.live = [project](const std::string& request) { return live::Forward(project, request); };
  return server.RunStdio(stdin, stdout);
}

} // namespace bdg
