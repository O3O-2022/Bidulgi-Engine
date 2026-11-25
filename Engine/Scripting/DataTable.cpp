#include "Scripting/DataTable.h"
#include "Core/Utf8Path.h"
#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <set>

namespace bdg {
namespace {
using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using MutDoc = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

std::string Write(yyjson_val* v) {
  size_t len = 0;
  char* s = yyjson_val_write(v, 0, &len);
  std::string r = s ? std::string(s, len) : "null";
  std::free(s);
  return r;
}
std::string WriteMut(yyjson_mut_val* v) {
  size_t len = 0;
  char* s = yyjson_mut_val_write(v, 0, &len);
  std::string r = s ? std::string(s, len) : "null";
  std::free(s);
  return r;
}

bool KnownType(const std::string& t) { return t == "int" || t == "float" || t == "bool" || t == "string" || t == "json"; }

bool Fits(yyjson_val* v, const std::string& type) {
  if (yyjson_is_null(v) || type == "json") return true;
  if (type == "int") return yyjson_is_int(v);
  if (type == "float") return yyjson_is_num(v);
  if (type == "bool") return yyjson_is_bool(v);
  if (type == "string") return yyjson_is_str(v);
  return true;
}

std::string KeyText(yyjson_val* v) { // a key may be a string or an integer
  if (yyjson_is_str(v)) return yyjson_get_str(v);
  if (yyjson_is_int(v)) return Write(v);
  return {};
}

std::string TypeOf(yyjson_val* v) {
  if (yyjson_is_int(v)) return "int";
  if (yyjson_is_num(v)) return "float";
  if (yyjson_is_bool(v)) return "bool";
  if (yyjson_is_str(v)) return "string";
  return "json";
}

std::string Merge(const std::string& a, const std::string& b) { // column type when rows disagree
  if (a.empty() || a == b) return b;
  if ((a == "int" && b == "float") || (a == "float" && b == "int")) return "float";
  return "json";
}

std::mutex g_foldersMutex;
std::vector<std::string> g_folders; // empty = defaults
} // namespace

const std::string* DataTable::Row(std::string_view k) const {
  for (size_t i = 0; i < keys.size(); ++i)
    if (keys[i] == k) return &rows[i];
  return nullptr;
}

bool ParseDataTable(std::string_view text, DataTable& out, std::vector<std::string>* problems) {
  auto problem = [&](std::string s) { if (problems) problems->push_back(std::move(s)); };
  const std::string name = out.name;
  out = DataTable{};
  out.name = name;
  std::set<std::string> seen;
  bool inferColumns = true, anyJson = false, anyLine = false;
  size_t lineNo = 0, pos = 0;
  if (text.substr(0, 3) == "\xEF\xBB\xBF") pos = 3;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;
    ++lineNo;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    if (line.empty() || line.substr(0, 2) == "//") continue;
    anyLine = true;
    Doc d(yyjson_read(line.data(), line.size(), 0), yyjson_doc_free);
    yyjson_val* row = d ? yyjson_doc_get_root(d.get()) : nullptr;
    if (!yyjson_is_obj(row)) { problem(std::format("line {}: not a JSON object", lineNo)); continue; }
    anyJson = true;
    if (yyjson_val* head = yyjson_obj_get(row, "$table")) { // the header line
      if (!out.rows.empty() || out.hasHeader) { problem(std::format("line {}: \"$table\" must be the first line", lineNo)); continue; }
      out.hasHeader = true;
      if (const char* k = yyjson_get_str(yyjson_obj_get(head, "key")); k && *k) out.key = k;
      size_t i, n;
      yyjson_val* c;
      yyjson_arr_foreach(yyjson_obj_get(head, "columns"), i, n, c) {
        const char* cn = yyjson_get_str(yyjson_obj_get(c, "name"));
        const char* ct = yyjson_get_str(yyjson_obj_get(c, "type"));
        if (!cn || !*cn) { problem(std::format("line {}: a column without a name", lineNo)); continue; }
        std::string type = ct ? ct : "json";
        if (!KnownType(type)) { problem(std::format("line {}: column \"{}\": unknown type \"{}\" (int, float, bool, string, json)", lineNo, cn, type)); type = "json"; }
        out.columns.push_back({cn, type});
      }
      inferColumns = out.columns.empty();
      continue;
    }
    const std::string key = KeyText(yyjson_obj_get(row, out.key.c_str()));
    if (key.empty()) { problem(std::format("line {}: no \"{}\" (the row's key)", lineNo, out.key)); continue; }
    if (!seen.insert(key).second) { problem(std::format("line {}: \"{}\" is used twice (the first row is kept)", lineNo, key)); continue; }
    bool ok = true;
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(row, i, n, k, v) {
      const std::string field = yyjson_get_str(k);
      auto col = std::find_if(out.columns.begin(), out.columns.end(), [&](const DataTable::Column& c) { return c.name == field; });
      if (col == out.columns.end()) {
        if (inferColumns) out.columns.push_back({field, TypeOf(v)});
        else problem(std::format("line {}: \"{}\" is not a column of the table (kept)", lineNo, field));
        continue;
      }
      if (inferColumns) col->type = Merge(col->type, TypeOf(v));
      else if (!Fits(v, col->type)) {
        problem(std::format("line {}: {}.{} should be {}, got {}", lineNo, key, field, col->type, Write(v)));
        ok = false;
      }
    }
    if (!ok) continue;
    out.keys.push_back(key);
    out.rows.push_back(Write(row));
  }
  return anyJson || !anyLine;
}

std::string WriteDataTable(const DataTable& t) {
  MutDoc d(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
  yyjson_mut_val* root = yyjson_mut_obj(d.get());
  yyjson_mut_val* head = yyjson_mut_obj(d.get());
  yyjson_mut_obj_add_strcpy(d.get(), head, "key", t.key.c_str());
  yyjson_mut_val* cols = yyjson_mut_arr(d.get());
  for (const DataTable::Column& c : t.columns) {
    yyjson_mut_val* o = yyjson_mut_obj(d.get());
    yyjson_mut_obj_add_strcpy(d.get(), o, "name", c.name.c_str());
    yyjson_mut_obj_add_strcpy(d.get(), o, "type", c.type.c_str());
    yyjson_mut_arr_append(cols, o);
  }
  yyjson_mut_obj_add_val(d.get(), head, "columns", cols);
  yyjson_mut_obj_add_val(d.get(), root, "$table", head);
  std::string text = WriteMut(root) + "\n";
  for (const std::string& r : t.rows) text += r + "\n";
  return text;
}

namespace {
// CSV cells, row by row.
bool ParseCsv(std::string_view s, std::vector<std::vector<std::string>>& rows, std::string* error) {
  size_t i = s.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
  std::vector<std::string> row;
  std::string cell;
  bool quoted = false, any = false;
  size_t line = 1;
  for (; i < s.size(); ++i) {
    const char ch = s[i];
    if (quoted) {
      if (ch == '"') {
        if (i + 1 < s.size() && s[i + 1] == '"') { cell += '"'; ++i; }
        else quoted = false;
      } else {
        if (ch == '\n') ++line;
        cell += ch;
      }
      continue;
    }
    if (ch == '"' && cell.empty()) { quoted = true; any = true; continue; }
    if (ch == ',') { row.push_back(std::move(cell)); cell.clear(); any = true; continue; }
    if (ch == '\r') continue;
    if (ch == '\n') {
      ++line;
      if (any || !cell.empty()) { row.push_back(std::move(cell)); rows.push_back(std::move(row)); }
      row.clear();
      cell.clear();
      any = false;
      continue;
    }
    cell += ch;
  }
  if (quoted) {
    if (error) *error = std::format("line {}: a quoted cell is not closed", line);
    return false;
  }
  if (any || !cell.empty()) { row.push_back(std::move(cell)); rows.push_back(std::move(row)); }
  return true;
}

bool IsInt(std::string_view s) {
  long long v = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  return !s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size();
}
bool IsFloat(std::string_view s) {
  double v = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  return !s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size();
}
bool IsBool(std::string_view s) {
  std::string l(s);
  for (char& c : l) c = (char)std::tolower((unsigned char)c);
  return l == "true" || l == "false";
}
std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
} // namespace

bool ImportCsv(std::string_view csv, DataTable& out, std::string* error, std::vector<std::string>* problems) {
  std::vector<std::vector<std::string>> cells;
  if (!ParseCsv(csv, cells, error)) return false;
  if (cells.empty()) {
    if (error) *error = "the CSV is empty (it needs a header row)";
    return false;
  }
  const std::string name = out.name;
  out = DataTable{};
  out.name = name;
  out.hasHeader = true;
  const std::vector<std::string>& header = cells[0];
  std::set<std::string> names;
  for (const std::string& h : header) {
    std::string n = Trim(h);
    if (n.empty() || !names.insert(n).second) {
      if (error) *error = n.empty() ? "an empty column name in the header row" : std::format("column \"{}\" appears twice", n);
      return false;
    }
    out.columns.push_back({n, ""});
  }
  out.key = names.count("id") ? "id" : out.columns[0].name;
  // types: int / float / bool when every non-empty cell of the column is one, else string; the key stays a string
  // unless it is all integers
  for (size_t c = 0; c < out.columns.size(); ++c) {
    bool allInt = true, allFloat = true, allBool = true, any = false;
    for (size_t r = 1; r < cells.size(); ++r) {
      if (c >= cells[r].size()) continue;
      const std::string v = Trim(cells[r][c]);
      if (v.empty()) continue;
      any = true;
      allInt = allInt && IsInt(v);
      allFloat = allFloat && IsFloat(v);
      allBool = allBool && IsBool(v);
    }
    out.columns[c].type = !any ? "string" : allInt ? "int" : allFloat && out.columns[c].name != out.key ? "float" : allBool && out.columns[c].name != out.key ? "bool" : "string";
  }
  std::set<std::string> seen;
  for (size_t r = 1; r < cells.size(); ++r) {
    const std::vector<std::string>& line = cells[r];
    if (line.size() > header.size() && problems) problems->push_back(std::format("row {}: {} cells, the header has {} (extra cells ignored)", r + 1, line.size(), header.size()));
    MutDoc d(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
    yyjson_mut_val* o = yyjson_mut_obj(d.get());
    std::string key;
    for (size_t c = 0; c < out.columns.size() && c < line.size(); ++c) {
      const std::string v = Trim(line[c]);
      const DataTable::Column& col = out.columns[c];
      if (col.name == out.key) key = v;
      if (v.empty()) continue; // missing value: the script's default
      yyjson_mut_val* k = yyjson_mut_strcpy(d.get(), col.name.c_str());
      yyjson_mut_val* val;
      if (col.type == "int") val = yyjson_mut_sint(d.get(), std::strtoll(v.c_str(), nullptr, 10));
      else if (col.type == "float") val = yyjson_mut_real(d.get(), std::strtod(v.c_str(), nullptr));
      else if (col.type == "bool") val = yyjson_mut_bool(d.get(), IsBool(v) && (v[0] == 't' || v[0] == 'T'));
      else val = yyjson_mut_strcpy(d.get(), line[c].c_str()); // strings keep their spaces
      yyjson_mut_obj_add(o, k, val);
    }
    if (key.empty()) { if (problems) problems->push_back(std::format("row {}: no {} (skipped)", r + 1, out.key)); continue; }
    if (!seen.insert(key).second) { if (problems) problems->push_back(std::format("row {}: \"{}\" is used twice (the first row is kept)", r + 1, key)); continue; }
    out.keys.push_back(key);
    out.rows.push_back(WriteMut(o));
  }
  return true;
}

std::string ExportCsv(const DataTable& t) {
  auto cell = [](const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos && (s.empty() || (s.front() != ' ' && s.back() != ' '))) return s;
    std::string q = "\"";
    for (char c : s) q += c == '"' ? std::string("\"\"") : std::string(1, c);
    return q + "\"";
  };
  std::string out;
  for (size_t c = 0; c < t.columns.size(); ++c) out += (c ? "," : "") + cell(t.columns[c].name);
  out += "\n";
  for (const std::string& r : t.rows) {
    Doc d(yyjson_read(r.data(), r.size(), 0), yyjson_doc_free);
    yyjson_val* o = d ? yyjson_doc_get_root(d.get()) : nullptr;
    for (size_t c = 0; c < t.columns.size(); ++c) {
      yyjson_val* v = yyjson_obj_get(o, t.columns[c].name.c_str());
      std::string text = !v || yyjson_is_null(v) ? "" : yyjson_is_str(v) ? yyjson_get_str(v) : Write(v);
      out += (c ? "," : "") + cell(text);
    }
    out += "\n";
  }
  return out;
}

void SetDataFolders(std::vector<std::string> folders) {
  std::lock_guard lock(g_foldersMutex);
  g_folders = std::move(folders);
}

std::vector<std::string> DataFolders() {
  std::lock_guard lock(g_foldersMutex);
  if (!g_folders.empty()) return g_folders;
  return {"Data", "Assets/Data"};
}

bool LoadDataTable(const std::string& name, DataTable& out, std::string* error, std::vector<std::string>* problems) {
  if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos) {
    if (error) *error = std::format("\"{}\" is not a table name (a file name without folders, e.g. \"Weapons\")", name);
    return false;
  }
  for (const std::string& folder : DataFolders()) {
    for (const char* ext : {".table.jsonl", ".csv"}) {
      const std::string path = folder + "/" + name + ext;
      std::string text;
      if (!ReadTextU8(path, text)) continue;
      out.name = name;
      std::string why;
      const bool ok = std::string_view(ext) == ".csv" ? ImportCsv(text, out, &why, problems) : ParseDataTable(text, out, problems);
      if (!ok && error) *error = std::format("{}: {}", path, why.empty() ? std::string("not a table") : why);
      if (problems)
        for (std::string& p : *problems)
          if (p.rfind(path, 0) != 0) p = path + ": " + p;
      return ok;
    }
  }
  if (error) {
    std::string where;
    for (const std::string& f : DataFolders()) where += (where.empty() ? "" : ", ") + f;
    *error = std::format("no table \"{}\" ({}.table.jsonl or {}.csv in {})", name, name, name, where);
  }
  return false;
}

std::string DataTableJson(const DataTable& t) {
  MutDoc d(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free);
  yyjson_mut_val* head = yyjson_mut_obj(d.get());
  yyjson_mut_obj_add_strcpy(d.get(), head, "name", t.name.c_str());
  yyjson_mut_obj_add_strcpy(d.get(), head, "key", t.key.c_str());
  std::string text = WriteMut(head);
  text.pop_back(); // the closing brace: rows are appended as written
  text += ",\"rows\":[";
  for (size_t i = 0; i < t.rows.size(); ++i) text += (i ? "," : "") + t.rows[i];
  return text + "]}";
}

} // namespace bdg
