#pragma once
// Data tables (queue 4-4d): rows of game data in a text file, one JSON object per line, read by scripts with
// Data.Get<Weapon>("rifle"). File: Data/Weapons.table.jsonl
//
//   {"$table":{"key":"id","columns":[{"name":"id","type":"string"},{"name":"damage","type":"int"}]}}   (optional)
//   {"id":"rifle","damage":12,"range":50.5}
//   {"id":"pistol","damage":5,"range":20}
//
// The first line may describe the table (key column, columns and their types: int, float, bool, string, json);
// without it the key is "id" and the columns are the keys the rows use, in order of first appearance. A CSV file
// (Data/Enemies.csv: a header row, then rows) is a table too: types are inferred per column (int, float, bool,
// otherwise string) and an empty cell is a missing value. `bdg table import` turns a CSV into a .table.jsonl.
#include <string>
#include <string_view>
#include <vector>

namespace bdg {

struct DataTable {
  struct Column {
    std::string name, type; // int | float | bool | string | json
  };
  std::string name;              // file name without .table.jsonl / .csv
  std::string key = "id";        // the column that names a row
  std::vector<Column> columns;
  std::vector<std::string> keys; // row keys, in file order
  std::vector<std::string> rows; // each row as compact JSON object text, same order
  bool hasHeader = false;
  const std::string* Row(std::string_view key) const;
};

// Parses .table.jsonl text. Returns false only when nothing usable is left (not JSON at all); problems (bad lines,
// a row without its key, duplicate keys, values of the wrong type) are listed with line numbers and those rows
// skipped (a duplicate keeps the first row).
bool ParseDataTable(std::string_view text, DataTable& out, std::vector<std::string>* problems = nullptr);
std::string WriteDataTable(const DataTable& t); // .table.jsonl text with a header line

// CSV (RFC 4180: commas, "quoted, ""escaped"" and multi-line" cells, CRLF or LF, an optional UTF-8 BOM).
// The key column is "id" when there is one, else the first column.
bool ImportCsv(std::string_view csv, DataTable& out, std::string* error = nullptr, std::vector<std::string>* problems = nullptr);
std::string ExportCsv(const DataTable& t);

// Where tables are looked up: <folder>/<name>.table.jsonl, then <folder>/<name>.csv, in each folder in order.
// Default: Data and Assets/Data under the working folder (the project).
void SetDataFolders(std::vector<std::string> folders);
std::vector<std::string> DataFolders();
bool LoadDataTable(const std::string& name, DataTable& out, std::string* error = nullptr, std::vector<std::string>* problems = nullptr);
// {"name":"Weapons","key":"id","rows":[{...},...]} (what the script request "table" answers)
std::string DataTableJson(const DataTable& t);

} // namespace bdg
