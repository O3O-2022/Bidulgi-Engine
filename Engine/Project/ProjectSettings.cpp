#include "Project/ProjectSettings.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include <yyjson.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>

namespace bdg {

namespace {
bool Fail(std::string* error, std::string why) {
  if (error) *error = std::move(why);
  return false;
}
const unsigned char* At(const void* base, const FieldInfo& f) { return static_cast<const unsigned char*>(base) + f.offset; }
} // namespace

std::string ProjectSettingsDiffJson(const ProjectSettings& s) {
  const ProjectSettings def;
  const TypeInfo& top = ProjectSettings::StaticType();
  std::string out = "{\n";
  bool firstCat = true;
  for (const FieldInfo& cat : top.fields) {
    const TypeInfo* ct = cat.value.structType;
    if (!ct) continue;
    std::string fields;
    for (const FieldInfo& f : ct->fields) {
      const std::string now = Registry::ToJson(At(At(&s, cat), f), f.value);
      if (now == Registry::ToJson(At(At(&def, cat), f), f.value)) continue; // the default: not written
      fields += std::format("{}    \"{}\": {}", fields.empty() ? "" : ",\n", f.name, now);
    }
    if (fields.empty()) continue;
    out += std::format("{}  \"{}\": {{\n{}\n  }}", firstCat ? "" : ",\n", cat.name, fields);
    firstCat = false;
  }
  return out + (firstCat ? "}\n" : "\n}\n");
}

bool SaveProjectSettings(const std::string& path, const ProjectSettings& s, std::string* error) {
  std::error_code ec;
  const std::filesystem::path p = PathU8(path);
  std::filesystem::create_directories(p.parent_path(), ec);
  const std::string text = ProjectSettingsDiffJson(s);
  std::ofstream out(p, std::ios::binary);
  if (!out || !(out << text)) return Fail(error, "cannot write " + path);
  return true;
}

bool LoadProjectSettings(const std::string& path, ProjectSettings& out, std::string* error) {
  out = ProjectSettings{};
  std::string text;
  if (!ReadTextU8(path, text)) return true; // no file yet: every value is the default
  yyjson_doc* doc = yyjson_read(text.data(), text.size(), YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS);
  if (!doc) return Fail(error, path + ": not valid JSON");
  yyjson_val* root = yyjson_doc_get_root(doc);
  const TypeInfo& top = ProjectSettings::StaticType();
  size_t i, n, j, m;
  yyjson_val *ck, *cv, *fk, *fv;
  yyjson_obj_foreach(root, i, n, ck, cv) {
    const char* cat = yyjson_get_str(ck);
    if (!cat || !yyjson_is_obj(cv) || !top.FindField(cat)) { LogWarn("project settings: unknown section {}", cat ? cat : "?"); continue; }
    yyjson_obj_foreach(cv, j, m, fk, fv) {
      char* value = yyjson_val_write(fv, 0, nullptr);
      std::string err;
      const std::string key = std::format("{}.{}", cat, yyjson_get_str(fk) ? yyjson_get_str(fk) : "");
      if (!value || !Registry::Set(&out, top, key, value, &err)) LogWarn("project settings: {}: {}", key, err.empty() ? "bad value" : err);
      std::free(value);
    }
  }
  yyjson_doc_free(doc);
  return true;
}

std::vector<std::string> ValidateProjectSettings(const ProjectSettings& s) {
  std::vector<std::string> out;
  if (s.description.name.empty()) out.push_back("게임 이름이 비어 있습니다");
  if (!std::regex_match(s.description.version, std::regex(R"(\d+\.\d+\.\d+([-+][0-9A-Za-z.-]+)?)")))
    out.push_back("버전은 1.2.3 형식이어야 합니다 (지금: " + s.description.version + ")");
  if (s.description.buildNumber < 1) out.push_back("빌드 번호는 1 이상이어야 합니다");
  if (!std::regex_match(s.description.identifier, std::regex(R"([a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+)")))
    out.push_back("식별자는 com.studio.game 처럼 소문자·숫자·점이어야 합니다");
  if (s.description.company.empty()) out.push_back("회사(제작자) 이름이 비어 있습니다 (저장 폴더 이름에 쓰임)");
  const auto& p = s.platforms;
  if (!p.windows && !p.linux && !p.macOS && !p.android && !p.iOS && !p.web) out.push_back("대상 플랫폼이 하나도 없습니다");
  if (s.maps.gameStartScene.empty() || !s.maps.gameStartScene.ends_with(".scene")) out.push_back("게임 시작 장면(.scene)을 정하세요");
  if ((p.android || p.iOS) && s.description.icon.empty()) out.push_back("모바일 빌드에는 아이콘이 필요합니다");
  if (("," + s.localization.languages + ",").find("," + s.localization.defaultLanguage + ",") == std::string::npos)
    out.push_back("기본 언어 " + s.localization.defaultLanguage + "가 언어 목록에 없습니다");
  return out;
}

} // namespace bdg
