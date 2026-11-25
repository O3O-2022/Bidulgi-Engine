#include "Build/Build.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetRefs.h"
#include "Assets/TextureCooker.h"
#include "Build/Pak.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Core/Utf8Path.h"
#include "Project/ProjectSettings.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include "Scripting/DataTable.h"
#include "Scripting/DotNetHost.h"
#include <yyjson.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {

std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}
fs::path P(const std::string& s) { return PathU8(s); }

std::string ProgramDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  if (n > 0 && n < std::size(buf)) return U(fs::path(std::wstring(buf, n)).parent_path());
#endif
  return U(fs::current_path());
}

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

const char* ConfigName(BuildConfig c) { return c == BuildConfig::Debug ? "Debug" : c == BuildConfig::Shipping ? "Shipping" : "Development"; }

struct Step { std::string name; double ms = 0; };

// Everything the check finds out, reused by the build.
struct Plan {
  BuildOptions opt;
  BuildResult res;
  fs::path root, out, player, managed;
  ProjectSettings settings;
  BuildConfig config = BuildConfig::Development;
  std::string exeName;
  bool zip = false;
  std::vector<std::string> scenes;          // project-relative, '/'
  std::vector<fs::path> scripts;            // script assemblies
  bool usesCSharp = false;
  bool haveAssets = false;
  AssetDatabase db;
  std::set<std::string> tokens;              // every name an asset can be referred to by
  std::vector<std::pair<std::string, std::string>> sceneDocs; // (Scenes/x.scene, text) for the reference index
  std::vector<Step> steps;

  void Add(bool error, std::string step, std::string what, std::string cause, std::string fix) {
    res.problems.push_back({error, std::move(step), std::move(what), std::move(cause), std::move(fix)});
  }
  void Error(std::string step, std::string what, std::string cause, std::string fix) { Add(true, step, what, cause, fix); }
  void Warn(std::string step, std::string what, std::string cause, std::string fix) { Add(false, step, what, cause, fix); }
};

bool InsideOf(const fs::path& child, const fs::path& parent) {
  std::error_code ec;
  const fs::path c = fs::weakly_canonical(child, ec), pa = fs::weakly_canonical(parent, ec);
  auto ci = c.begin(), pi = pa.begin();
  for (; pi != pa.end(); ++pi, ++ci)
    if (ci == c.end() || Lower(U(*ci)) != Lower(U(*pi))) return false;
  return true;
}

std::string ReadText(const fs::path& f) {
  std::vector<uint8_t> b;
  return ReadFileBytes(f, b) ? std::string(b.begin(), b.end()) : std::string();
}

// A file system error in words (std::error_code::message() is in the system code page, not UTF-8).
std::string SysError(const std::error_code& ec) {
  switch (ec.value()) {
    case 5: return "권한이 없음 (읽기 전용이거나 관리자 권한이 필요한 폴더)";
    case 32: case 33: return "다른 프로그램이 그 파일을 열고 있음";
    case 39: case 112: return "디스크가 가득 참";
    case 2: case 3: return "파일이나 폴더가 없음";
    default: return std::format("시스템 오류 {}", ec.value());
  }
}

std::string Unquote(std::string s) { return s.size() >= 2 && s.front() == '"' ? s.substr(1, s.size() - 2) : s; }

// ---- check -------------------------------------------------------------------------------------------------

void CheckSettings(Plan& p) {
  const fs::path file = p.root / "ProjectSettings" / "Project.json";
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    p.Warn("설정", "ProjectSettings/Project.json", "파일이 없어 기본값(My Game, Scenes/Main.scene)으로 빌드합니다", "에디터의 Project Settings에서 이름·버전·시작 장면을 정하고 저장하세요");
  } else {
    std::string err;
    if (!LoadProjectSettings(U(file), p.settings, &err))
      p.Error("설정", "ProjectSettings/Project.json", "읽을 수 없음: " + err, "JSON 문법을 고치거나 에디터의 Project Settings에서 다시 저장하세요");
  }
  for (const std::string& m : ValidateProjectSettings(p.settings))
    p.Error("설정", "ProjectSettings/Project.json", m, "에디터의 Project Settings(또는 bdg set으로 Project.json)에서 고치세요");
  if (!p.settings.platforms.windows)
    p.Error("설정", "platforms.windows", "Windows가 대상 플랫폼에 없음 (지금 bdg build는 Windows용만 만듭니다)", "Project Settings › 플랫폼에서 Windows를 켜세요");
  p.config = p.settings.packaging.config;
  if (!p.opt.config.empty()) {
    const std::string c = Lower(p.opt.config);
    if (c == "debug") p.config = BuildConfig::Debug;
    else if (c == "development") p.config = BuildConfig::Development;
    else if (c == "shipping") p.config = BuildConfig::Shipping;
    else p.Error("설정", "--config " + p.opt.config, "알 수 없는 빌드 구성", "Debug, Development, Shipping 중 하나를 쓰세요");
  }
  p.zip = p.opt.zip < 0 ? p.settings.packaging.zipBuild : p.opt.zip != 0;
  p.exeName = p.settings.packaging.executableName.empty() ? p.settings.description.name : p.settings.packaging.executableName;
  static const std::array<const char*, 22> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                                       "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
  const bool badChar = p.exeName.find_first_of("<>:\"/\\|?*") != std::string::npos ||
                       std::any_of(p.exeName.begin(), p.exeName.end(), [](char ch) { return (unsigned char)ch < 32; });
  if (p.exeName.empty() || badChar || p.exeName.back() == '.' || p.exeName.back() == ' ' ||
      std::find(reserved.begin(), reserved.end(), Lower(p.exeName)) != reserved.end())
    p.Error("설정", "packaging.executableName = \"" + p.exeName + "\"", "Windows 파일 이름으로 쓸 수 없음 (빈 이름, < > : \" / \\ | ? *, 끝의 점·공백, CON 같은 예약 이름)",
            "packaging.executableName(비우면 게임 이름)을 글자·숫자로 정하세요");
}

void CheckIcon(Plan& p) {
  const std::string& icon = p.settings.description.icon;
  if (icon.empty()) {
    p.Warn("아이콘", "description.icon", "아이콘이 없어 비둘기 엔진 아이콘이 쓰입니다", "1024 px 정사각형 PNG를 프로젝트에 넣고 description.icon에 경로를 적으세요");
    return;
  }
  const fs::path f = p.root / P(icon);
  int32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  std::error_code ec;
  if (!fs::exists(f, ec)) {
    p.Error("아이콘", "description.icon = " + icon, "파일이 없음", "경로(프로젝트 기준)를 고치거나 아이콘 파일을 넣으세요");
    return;
  }
  if (!LoadImageRGBA(f, w, h, rgba)) {
    p.Error("아이콘", "description.icon = " + icon, "이미지로 읽을 수 없음 (PNG·JPG가 아니거나 손상됨)", "PNG로 다시 저장하세요");
    return;
  }
  if (w != h) p.Warn("아이콘", icon, std::format("정사각형이 아님 ({}x{}): 늘어나 보입니다", w, h), "정사각형으로 자르세요");
  if (std::min(w, h) < 256) p.Warn("아이콘", icon, std::format("작음 ({}x{}): 큰 아이콘이 흐려집니다", w, h), "256 px 이상(권장 1024 px)으로 만드세요");
}

void CheckOutput(Plan& p) {
  const std::string folder = p.settings.packaging.outputFolder;
  fs::path base = folder.empty() ? p.root / "Builds" : (P(folder).is_absolute() ? P(folder) : p.root / P(folder));
  p.out = p.opt.output.empty() ? base / std::format("Windows-{}", ConfigName(p.config)) : (P(p.opt.output).is_absolute() ? P(p.opt.output) : fs::current_path() / P(p.opt.output));
  std::error_code ec;
  if (fs::weakly_canonical(p.out, ec) == fs::weakly_canonical(p.root, ec)) {
    p.Error("출력", U(p.out), "출력 폴더가 프로젝트 폴더 자체임 (빌드가 프로젝트 파일을 지울 수 있음)", "packaging.outputFolder를 Builds 같은 하위 폴더로 정하세요");
    return;
  }
  for (const char* sub : {"Assets", "Scenes", "ProjectSettings", "Data"})
    if (InsideOf(p.out, p.root / sub)) {
      p.Error("출력", U(p.out), std::format("출력 폴더가 {}/ 안에 있음 (빌드 결과가 다음 빌드에 섞임)", sub), "packaging.outputFolder를 Builds 같은 프로젝트 바로 아래 폴더로 정하세요");
      return;
    }
  if (fs::exists(p.out, ec) && !fs::is_empty(p.out, ec) && !fs::exists(p.out / "build-report.txt", ec))
    p.Error("출력", U(p.out), "비어 있지 않은 폴더이고 이전 빌드도 아님 (덮어쓰면 다른 파일을 잃을 수 있음)", "빈 폴더나 새 이름을 쓰거나(--out), 그 폴더를 직접 비우세요");
}

void CheckTools(Plan& p) {
  p.player = p.opt.playerExe.empty() ? P(ProgramDir()) / "BidulgiPlayer.exe" : P(p.opt.playerExe);
  std::error_code ec;
  if (!fs::exists(p.player, ec))
    p.Error("출력", U(p.player), "게임 실행 파일(플레이어)이 없음", "엔진을 빌드하세요: cmake --build build --config Release --target BidulgiPlayer (bdg.exe 옆에 생김)");
  p.managed = p.opt.managedDir.empty() ? P(DotNetHost::DefaultManagedDir()) : P(p.opt.managedDir);
}

void CheckScripts(Plan& p) {
  std::error_code ec;
  for (const auto& f : fs::directory_iterator(p.root / ".cache" / "Scripts", ec))
    if (f.path().extension() == ".dll") p.scripts.push_back(f.path());
  for (const std::string& s : p.opt.scriptDlls) {
    const fs::path f = P(s).is_absolute() ? P(s) : fs::current_path() / P(s);
    if (!fs::exists(f, ec)) p.Error("스크립트", s, "스크립트 어셈블리 파일이 없음", "IDE나 dotnet build로 스크립트를 빌드하고 경로를 확인하세요");
    else p.scripts.push_back(f);
  }
  if (p.scripts.empty()) return;
  DotNetHost& host = DotNetHost::Instance();
  std::string err;
  if (!host.Start(U(p.managed), &err)) {
    p.Error("스크립트", U(p.managed), "C# 런타임을 시작할 수 없음: " + err, ".NET 8 이상을 설치하고, 엔진의 Managed 폴더(Bidulgi.Scripting.dll)가 bdg.exe 옆에 있는지 확인하세요");
    return;
  }
  for (const fs::path& f : p.scripts)
    if (host.LoadScripts(U(f), &err) < 0) p.Error("스크립트", U(f), "스크립트를 불러올 수 없음: " + err, "스크립트를 다시 빌드하세요 (같은 엔진 버전의 Bidulgi.Scripting.dll을 참조)");
}

void CheckAssets(Plan& p) {
  const fs::path assets = p.root / "Assets";
  std::error_code ec;
  if (!fs::exists(assets, ec)) return;
  std::string err;
  p.haveAssets = p.db.Open(U(assets), &err, U(p.root / ".cache" / "assetdb.jsonl"));
  if (!p.haveAssets) { p.Error("에셋", "Assets/", "에셋 폴더를 읽을 수 없음: " + err, "폴더 권한과 파일 이름을 확인하세요"); return; }
  for (const AssetRecord* r : p.db.All())
    for (const std::string& t : RefTokens(*r)) p.tokens.insert(t);
  for (const std::string& m : p.db.OrphanMetas())
    p.Warn("에셋", "Assets/" + m, "짝 잃은 .meta (원본 파일이 없음)", "원본을 되돌리거나 .meta를 지우세요");
  // materials: texture names that are not in the project
  for (const AssetRecord* r : p.db.All()) {
    if (r->importer != "material") continue;
    const std::string text = ReadText(P(p.db.Root()) / P(r->path));
    std::function<void(yyjson_val*, const std::string&)> walk = [&](yyjson_val* v, const std::string& key) {
      if (yyjson_is_obj(v)) {
        size_t i, n;
        yyjson_val *k, *x;
        yyjson_obj_foreach(v, i, n, k, x) walk(x, yyjson_get_str(k));
      } else if (yyjson_is_arr(v)) {
        size_t i, n;
        yyjson_val* x;
        yyjson_arr_foreach(v, i, n, x) walk(x, key);
      } else if (const char* s = yyjson_get_str(v)) {
        const std::string val = s, ext = Lower(P(val).extension().string());
        static const std::set<std::string> images = {".png", ".jpg", ".jpeg", ".tga", ".psd", ".exr", ".hdr", ".bmp", ".ktx2", ".dds"};
        if (images.count(ext) && !p.tokens.count(val))
          p.Error("에셋", std::format("Assets/{} · {}", r->path, key), std::format("텍스처 '{}'가 프로젝트에 없음", val), "텍스처를 Assets에 넣거나 머티리얼에서 다른 텍스처를 고르세요");
      }
    };
    // JSON lines (Assets/MaterialFile.h): one object per line; a bad line is a problem of its own
    int lineNo = 0;
    for (size_t at = 0; at < text.size();) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) end = text.size();
      std::string_view line(text.data() + at, end - at);
      at = end + 1;
      ++lineNo;
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
      if (line.empty()) continue;
      yyjson_doc* d = yyjson_read(line.data(), line.size(), 0);
      if (!d) {
        p.Error("에셋", std::format("Assets/{}:{}", r->path, lineNo), "머티리얼 줄이 JSON이 아님", "그 줄을 고치거나 에디터에서 머티리얼을 다시 저장하세요");
        continue;
      }
      yyjson_val* root = yyjson_doc_get_root(d);
      const char* param = yyjson_get_str(yyjson_obj_get(root, "param"));
      const char* set = yyjson_get_str(yyjson_obj_get(root, "set"));
      walk(root, param ? param : set ? set : "");
      yyjson_doc_free(d);
    }
  }
}

void CheckScene(Plan& p, const std::string& rel, bool start) {
  const fs::path f = p.root / P(rel);
  std::error_code ec;
  if (!fs::exists(f, ec)) {
    p.Error("장면", rel, start ? "게임 시작 장면 파일이 없음" : "빌드에 넣을 장면 파일이 없음",
            start ? "maps.gameStartScene을 있는 장면으로 고치거나 그 장면을 저장하세요" : "maps.scenesInBuild에서 빼거나 장면을 저장하세요");
    return;
  }
  Scene scene;
  std::string name, err;
  std::vector<std::string> warnings;
  if (!LoadSceneFile(scene, U(f), &name, &warnings, &err)) {
    p.Error("장면", rel, "장면 파일을 읽을 수 없음: " + err, "에디터에서 열어 다시 저장하거나 bdg validate로 줄을 확인하세요");
    return;
  }
  p.sceneDocs.emplace_back(rel, ReadText(f));
  for (const std::string& w : warnings) {
    if (w.find("unknown component") != std::string::npos) {
      const size_t a = w.find('"'), b = a == std::string::npos ? a : w.find('"', a + 1);
      const std::string type = b == std::string::npos ? std::string("?") : w.substr(a + 1, b - a - 1);
      const std::string line = w.substr(0, w.find(':'));
      p.Error("장면", std::format("{}:{}", rel, line.rfind("line ", 0) == 0 ? line.substr(5) : line), std::format("컴포넌트 '{}'를 아는 코드가 없음 (게임에서 빠짐)", type),
              p.scripts.empty() ? "C# 클래스라면 스크립트를 빌드해 .cache/Scripts에 두거나 --scripts로 dll을 주세요" : "그 이름의 C# Behaviour가 스크립트에 있는지(철자·빌드) 확인하세요");
    } else {
      p.Warn("장면", rel, w, "에디터에서 장면을 열어 다시 저장하세요");
    }
  }
  bool camera = false;
  static const std::set<std::string> builtinMeshes = {"plane", "cube", "sphere", "cylinder", "cone"};
  DotNetHost& host = DotNetHost::Instance();
  std::function<void(const std::string&, const std::string&)> walk = [&](const std::string& id, const std::string& path) {
    SceneObject* o = scene.Find(id);
    if (!o) return;
    const std::string here = path.empty() ? o->name : path + "/" + o->name;
    for (Component& c : o->components) {
      if (c.type == &Camera::StaticType()) camera = true;
      if (host.IsBehaviourType(c.type)) p.usesCSharp = true;
      if (c.type->name == "AudioSource") { // (6-3) the clip must be a file of the project
        std::string clip;
        Registry::Get(c.data, *c.type, "clip", clip);
        clip = Unquote(clip);
        std::error_code cec;
        if (clip.empty())
          p.Warn("에셋", std::format("{} {} · AudioSource.clip", rel, here), "소리 파일이 정해지지 않음 (아무 소리도 안 남)", "AudioSource의 clip에 Assets의 .wav·.mp3·.flac을 정하세요");
        else if (!p.tokens.count(clip) && !fs::exists(p.root / P(clip), cec))
          p.Error("에셋", std::format("{} {} · AudioSource.clip", rel, here), std::format("소리 파일 '{}'가 프로젝트에 없음", clip), "Assets에 소리 파일을 넣거나 clip의 이름·경로를 고치세요");
      }
      if (c.type->name == "MeshRenderer") {
        std::string mesh, mat;
        Registry::Get(c.data, *c.type, "mesh", mesh);
        Registry::Get(c.data, *c.type, "material", mat);
        mesh = Unquote(mesh);
        mat = Unquote(mat);
        const std::string stem = Lower(P(mesh).stem().string());
        if (!mesh.empty() && !p.tokens.count(mesh) && !builtinMeshes.count(stem))
          p.Error("에셋", std::format("{} {} · MeshRenderer.mesh", rel, here), std::format("메시 '{}'가 프로젝트에 없음", mesh),
                  "Assets에 모델을 넣거나 Plane·Cube·Sphere·Cylinder·Cone 중 하나를 쓰세요");
        if (!mat.empty() && mat.rfind("bdg:", 0) != 0 && !p.tokens.count(mat))
          p.Warn("에셋", std::format("{} {} · MeshRenderer.material", rel, here), std::format("머티리얼 '{}'가 없어 이름 규칙 색으로 그려짐", mat), "머티리얼을 만들거나 이름을 고치세요");
      }
    }
    for (const std::string& ch : o->children) walk(ch, here);
  };
  for (const std::string& r : scene.Roots()) walk(r, "");
  if (start && !camera)
    p.Error("장면", rel, "Camera 컴포넌트가 없음 (게임 화면에 아무것도 안 보임)", "장면에 카메라 오브젝트를 추가하세요 (Hierarchy › 추가 › Camera)");
}

void CheckScenes(Plan& p) {
  std::string start = p.settings.maps.gameStartScene;
  for (char& c : start) if (c == '\\') c = '/';
  if (start.empty()) return; // reported by ValidateProjectSettings
  p.scenes.push_back(start);
  std::string list = p.settings.maps.scenesInBuild;
  if (!list.empty()) {
    for (size_t a = 0; a <= list.size();) {
      size_t b = list.find(';', a);
      if (b == std::string::npos) b = list.size();
      std::string s = list.substr(a, b - a);
      s.erase(0, s.find_first_not_of(" \t"));
      s.erase(s.find_last_not_of(" \t") + 1);
      for (char& c : s) if (c == '\\') c = '/';
      if (!s.empty() && std::find(p.scenes.begin(), p.scenes.end(), s) == p.scenes.end()) p.scenes.push_back(s);
      a = b + 1;
    }
  } else { // every scene of Scenes/ (scene-to-scene links come with scene loading in scripts)
    std::error_code ec;
    std::vector<std::string> more;
    for (const auto& f : fs::directory_iterator(p.root / "Scenes", ec))
      if (f.path().extension() == ".scene") more.push_back("Scenes/" + U(f.path().filename()));
    std::sort(more.begin(), more.end());
    for (const std::string& s : more) if (std::find(p.scenes.begin(), p.scenes.end(), s) == p.scenes.end()) p.scenes.push_back(s);
  }
  for (size_t i = 0; i < p.scenes.size(); ++i) CheckScene(p, p.scenes[i], i == 0);
  if (p.usesCSharp) {
    std::error_code ec;
    if (!fs::exists(p.managed / "Bidulgi.Scripting.dll", ec))
      p.Error("스크립트", U(p.managed), "C# 스크립트를 쓰는데 Bidulgi.Scripting.dll이 없음", "엔진을 .NET SDK와 함께 빌드하세요 (BidulgiScripting 타깃)");
    p.Warn("스크립트", "C# 런타임", "게임이 C# 스크립트를 씀: 플레이어 PC에 .NET 8 이상 런타임이 있어야 함", "배포 페이지에 .NET 런타임 설치 안내를 넣으세요 (자체 포함 런타임은 다음 단계)");
  }
}

void CheckData(Plan& p) {
  std::error_code ec;
  for (const auto& f : fs::directory_iterator(p.root / "Data", ec)) {
    const std::string name = U(f.path().filename());
    const bool jsonl = name.ends_with(".table.jsonl"), csv = name.ends_with(".csv");
    if (!jsonl && !csv) continue;
    DataTable t;
    std::vector<std::string> problems;
    std::string err;
    const std::string text = ReadText(f.path());
    const bool ok = jsonl ? ParseDataTable(text, t, &problems) : ImportCsv(text, t, &err, &problems);
    if (!ok) p.Error("데이터", "Data/" + name, "표로 읽을 수 없음" + (err.empty() ? std::string() : ": " + err), "bdg table check로 줄을 확인하세요");
    for (size_t i = 0; i < problems.size() && i < 5; ++i) p.Warn("데이터", "Data/" + name, problems[i], "그 줄을 고치세요 (bdg table check)");
  }
}

void RunChecks(Plan& p) {
  const auto t0 = std::chrono::steady_clock::now();
  p.root = P(p.opt.project.empty() ? U(fs::current_path()) : p.opt.project);
  std::error_code ec;
  if (!fs::is_directory(p.root, ec)) {
    p.Error("설정", U(p.root), "프로젝트 폴더가 없음", "--project로 ProjectSettings·Scenes가 있는 폴더를 주세요");
    return;
  }
  p.root = fs::weakly_canonical(p.root, ec);
  CheckSettings(p);
  CheckIcon(p);
  CheckOutput(p);
  CheckTools(p);
  CheckScripts(p);
  CheckAssets(p);
  CheckScenes(p);
  CheckData(p);
  p.steps.push_back({"검사", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()});
}

// ---- build -------------------------------------------------------------------------------------------------

struct PackFile { fs::path src; std::string path; };

bool CopyFileTo(const fs::path& from, const fs::path& to, std::string* err) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  if (ec && err) *err = std::format("{} → {}: {}", U(from), U(to), SysError(ec));
  return !ec;
}

std::string HumanBytes(unsigned long long b) {
  if (b >= 1024ull * 1024) return std::format("{:.1f} MB", b / (1024.0 * 1024.0));
  if (b >= 1024) return std::format("{:.1f} KB", b / 1024.0);
  return std::format("{} B", b);
}

const char* kNotices =
    "Third-party software in this game (Bidulgi Engine)\n"
    "===================================================\n"
    "Bidulgi Engine - MIT License - https://github.com/O3O-2022/Bidulgi-Engine\n"
    "SDL 3 - zlib License - https://libsdl.org\n"
    "FreeType - FreeType License (BSD-style) - https://freetype.org\n"
    "yyjson - MIT License - https://github.com/ibireme/yyjson\n"
    "GLM - MIT License - https://github.com/g-truc/glm\n"
    "zstd - BSD License - https://github.com/facebook/zstd\n"
    "glad - MIT / public domain (generated loader) - https://github.com/Dav1dde/glad\n"
    "stb_image, stb_image_write - public domain / MIT - https://github.com/nothings/stb\n"
    "cgltf - MIT License - https://github.com/jkuhlmann/cgltf\n"
    "meshoptimizer - MIT License - https://github.com/zeux/meshoptimizer\n"
    "bc7enc_rdo - MIT / public domain - https://github.com/richgel999/bc7enc_rdo\n"
    "xatlas - MIT License - https://github.com/jpcy/xatlas\n"
    "ufbx - MIT / public domain - https://github.com/ufbx/ufbx\n"
    "miniaudio - public domain / MIT-0 - https://miniaud.io\n"
    "Pretendard (game UI font, Fonts/) - SIL Open Font License 1.1 - https://github.com/orioncactus/pretendard (Fonts/Pretendard-LICENSE.txt)\n"
    ".NET runtime (when the game uses C#) - MIT License - https://github.com/dotnet/runtime\n"
    "\nFull license texts: https://github.com/O3O-2022/Bidulgi-Engine/tree/main/ThirdParty\n";

void Build(Plan& p) {
  auto lap = std::chrono::steady_clock::now();
  auto step = [&](const char* name) {
    const auto now = std::chrono::steady_clock::now();
    p.steps.push_back({name, std::chrono::duration<double, std::milli>(now - lap).count()});
    lap = now;
  };
  auto fail = [&](std::string where, std::string what, std::string cause, std::string fix) { p.Error(where, what, cause, fix); };
  std::error_code ec;

  // collect
  std::vector<PackFile> files;
  std::set<std::string> seen;
  auto add = [&](const fs::path& src, const std::string& path) {
    if (seen.insert(path).second) files.push_back({src, path});
  };
  { // the settings the game runs with (also when the project has no Project.json yet: the defaults)
    const fs::path effective = p.root / ".cache" / "build" / "Project.json";
    fs::create_directories(effective.parent_path(), ec);
    std::string serr;
    if (!SaveProjectSettings(U(effective), p.settings, &serr)) fail("수집", "ProjectSettings/Project.json", serr, "프로젝트 폴더 권한을 확인하세요");
    else add(effective, "ProjectSettings/Project.json");
  }
  for (const auto& f : fs::recursive_directory_iterator(p.root / "ProjectSettings", ec))
    if (f.is_regular_file()) add(f.path(), "ProjectSettings/" + U(fs::relative(f.path(), p.root / "ProjectSettings")));
  for (const std::string& s : p.scenes) add(p.root / P(s), s);
  for (const auto& f : fs::directory_iterator(p.root / "Data", ec))
    if (f.is_regular_file()) add(f.path(), "Data/" + U(f.path().filename()));
  for (const auto& f : fs::recursive_directory_iterator(p.root / "Input", ec)) // input actions and contexts (6-4)
    if (f.is_regular_file()) add(f.path(), "Input/" + U(fs::relative(f.path(), p.root / "Input")));
  std::vector<std::string> unused;
  std::vector<const AssetRecord*> used;
  if (p.haveAssets) {
    std::set<std::string> drop;
    if (p.settings.packaging.stripUnusedAssets) {
      AssetRefIndex index;
      index.Build(p.db, p.sceneDocs);
      for (const std::string& u : index.Unused()) drop.insert(u);
    }
    for (const AssetRecord* r : p.db.All()) {
      const bool editorOnly = r->importer == "script" && (r->path.ends_with(".py") || r->path.ends_with(".cs") || r->path.ends_with(".cpp") || r->path.ends_with(".h"));
      if (drop.count(r->path) || (p.settings.packaging.excludeEditorContent && editorOnly)) { unused.push_back(r->path); continue; }
      used.push_back(r);
      const fs::path src = P(p.db.Root()) / P(r->path);
      add(src, "Assets/" + r->path);
      if (fs::exists(fs::path(src.wstring() + L".meta"), ec)) add(fs::path(src.wstring() + L".meta"), "Assets/" + r->path + ".meta");
    }
  }
  step("수집");

  // cook: textures to their cooked form (the player then only reads the DDS files)
  int cooked = 0;
  if (p.haveAssets) {
    TextureCookCache cache;
    const fs::path cookDir = p.root / ".cache" / "cooked";
    fs::create_directories(cookDir, ec);
    cache.SetDirectory(cookDir);
    std::set<std::string> guids;
    for (const AssetRecord* r : used) {
      if (r->importer != "texture") continue;
      const fs::path src = P(p.db.Root()) / P(r->path);
      CookedTexture t;
      if (!cache.Get(*r, src, ReadText(fs::path(src.wstring() + L".meta")), t)) {
        fail("쿠킹", "Assets/" + r->path, "텍스처를 변환할 수 없음 (손상되었거나 지원하지 않는 형식)", "PNG로 다시 저장하거나 쓰지 않으면 지우세요");
        continue;
      }
      guids.insert(r->guid);
      ++cooked;
    }
    for (const auto& f : fs::directory_iterator(cookDir, ec)) {
      const std::string n = U(f.path().filename());
      const size_t dash = n.find('-');
      if (dash != std::string::npos && guids.count(n.substr(0, dash))) add(f.path(), ".cache/cooked/" + n);
    }
  }
  step("쿠킹");
  if (p.res.Errors()) return;

  // output folder: a previous build is replaced as a whole (the check refused any other non-empty folder)
  if (fs::exists(p.out, ec)) fs::remove_all(p.out, ec);
  if (ec) { fail("출력", U(p.out), "이전 빌드를 지울 수 없음: " + SysError(ec), "그 폴더의 게임을 닫고 다시 하세요"); return; }
  fs::create_directories(p.out, ec);

  // scripts
  std::string err;
  const bool symbols = p.config != BuildConfig::Shipping || p.settings.packaging.debugSymbols;
  if (p.usesCSharp || !p.scripts.empty()) {
    for (const char* n : {"Bidulgi.Scripting.dll", "Bidulgi.Scripting.runtimeconfig.json", "Bidulgi.Scripting.deps.json", "Bidulgi.Scripting.pdb"}) {
      if (std::string(n).ends_with(".pdb") && !symbols) continue;
      if (fs::exists(p.managed / n, ec) && !CopyFileTo(p.managed / n, p.out / "Managed" / n, &err)) fail("스크립트", n, err, "디스크 공간과 권한을 확인하세요");
    }
    for (const fs::path& s : p.scripts) {
      if (!CopyFileTo(s, p.out / "Scripts" / s.filename(), &err)) fail("스크립트", U(s), err, "디스크 공간과 권한을 확인하세요");
      const fs::path pdb = fs::path(s).replace_extension(".pdb");
      if (symbols && fs::exists(pdb, ec)) CopyFileTo(pdb, p.out / "Scripts" / pdb.filename(), &err);
    }
  }
  step("스크립트");

  // pack
  PakWriter pak;
  p.res.pakPath = U(p.out / "Content" / "game.bpak");
  struct Row { std::string path; uint64_t size, stored; };
  std::vector<Row> rows;
  if (!pak.Open(p.res.pakPath, &err)) { fail("묶기", p.res.pakPath, err, "출력 폴더 권한을 확인하세요"); return; }
  for (const PackFile& f : files) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(f.src, bytes)) { fail("묶기", f.path, "파일을 읽을 수 없음", "다른 프로그램이 잠그고 있지 않은지 확인하세요"); continue; }
    if (!pak.Add(f.path, bytes, p.settings.packaging.compressAssets, &err)) { fail("묶기", f.path, err, "파일 이름을 바꾸세요"); continue; }
    rows.push_back({f.path, pak.Entries().back().size, pak.Entries().back().stored});
    p.res.rawBytes += bytes.size();
  }
  if (!pak.Finish(&err)) fail("묶기", p.res.pakPath, err, "디스크 공간을 확인하세요");
  p.res.files = rows.size();
  p.res.pakBytes = pak.BytesWritten();
  step("묶기");

  // player
  p.res.exePath = U(p.out / P(p.exeName + ".exe"));
  if (!CopyFileTo(p.player, P(p.res.exePath), &err)) fail("출력", p.res.exePath, err, "디스크 공간과 권한을 확인하세요");
  else if (!p.settings.description.icon.empty() && !SetExeIcon(p.res.exePath, U(p.root / P(p.settings.description.icon)), &err))
    p.Warn("아이콘", p.settings.description.icon, "실행 파일에 아이콘을 넣지 못함: " + err, "PNG 아이콘을 다시 저장해 보세요 (게임 창 아이콘은 그대로 쓰임)");
  if (FILE* f = OpenU8(U(p.out / "THIRD-PARTY-NOTICES.txt"), "wb")) { std::fputs(kNotices, f); std::fclose(f); }
  { // the game UI's font (7-3): next to bdg.exe (the build copies it there), else the engine source tree's
    std::vector<fs::path> dirs{P(ProgramDir()) / "Fonts"};
    for (fs::path d = P(ProgramDir()); !d.empty() && d != d.parent_path(); d = d.parent_path()) dirs.push_back(d / "Assets" / "Fonts");
    bool copied = false;
    for (const fs::path& d : dirs)
      if (fs::exists(d / "Pretendard-Regular.otf", ec)) {
        copied = CopyFileTo(d / "Pretendard-Regular.otf", p.out / "Fonts" / "Pretendard-Regular.otf", &err);
        if (fs::exists(d / "Pretendard-LICENSE.txt", ec)) CopyFileTo(d / "Pretendard-LICENSE.txt", p.out / "Fonts" / "Pretendard-LICENSE.txt", &err);
        break;
      }
    if (!copied) p.Warn("출력", "Fonts/Pretendard-Regular.otf", "게임 UI 글꼴을 찾지 못함 (게임 화면의 글자가 시스템 글꼴로 나오거나 안 나옴)", "엔진을 다시 빌드해 bdg.exe 옆에 Fonts/가 생기게 하세요");
  }
  step("출력");

  // report
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.stored != b.stored ? a.stored > b.stored : a.path < b.path; });
  std::string r;
  const std::time_t now = std::time(nullptr);
  std::tm tmv{};
  localtime_s(&tmv, &now);
  char when[32];
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
  r += std::format("빌드 보고서 — {} {} ({}), {}\n", p.settings.description.name, p.settings.description.version, ConfigName(p.config), when);
  r += std::format("프로젝트: {}\n출력: {}\n실행 파일: {}.exe\n", U(p.root), U(p.out), p.exeName);
  r += std::format("장면 {}개: ", p.scenes.size());
  for (size_t i = 0; i < p.scenes.size(); ++i) r += (i ? ", " : "") + p.scenes[i] + (i == 0 ? " (시작)" : "");
  r += std::format("\nC# 스크립트: {}\n", p.scripts.empty() ? std::string("없음") : std::to_string(p.scripts.size()) + "개 어셈블리");
  r += std::format("묶은 파일 {}개, 원본 {}, game.bpak {} ({}), 쿠킹한 텍스처 {}개\n\n", rows.size(), HumanBytes(p.res.rawBytes), HumanBytes(p.res.pakBytes),
                   p.settings.packaging.compressAssets ? "zstd 압축" : "압축 안 함", cooked);
  r += "단계별 시간\n";
  for (const Step& s : p.steps) r += std::format("  {:<6} {:8.0f} ms\n", s.name, s.ms);
  r += "\n파일 (묶은 크기 순)\n";
  for (const Row& row : rows) r += std::format("  {:>10}  {:>10}  {}\n", HumanBytes(row.stored), HumanBytes(row.size), row.path);
  if (!unused.empty()) {
    r += std::format("\n빼 놓은 에셋 {}개 (장면이 쓰지 않음 또는 에디터 전용)\n", unused.size());
    for (const std::string& u : unused) r += "  Assets/" + u + "\n";
  }
  const int warnings = p.res.Warnings();
  r += std::format("\n경고 {}개\n", warnings);
  for (const BuildProblem& pr : p.res.problems)
    if (!pr.error) r += "  " + pr.Line() + "\n";
  p.res.reportPath = U(p.out / "build-report.txt");
  if (FILE* f = OpenU8(p.res.reportPath, "wb")) { std::fwrite(r.data(), 1, r.size(), f); std::fclose(f); }

  if (p.zip) {
    const fs::path zip = p.out.parent_path() / P(std::format("{}-{}-{}.zip", p.exeName, p.settings.description.version, ConfigName(p.config)));
    p.res.zipPath = U(zip);
    if (!WriteZipOfFolder(U(p.out), p.res.zipPath, &err)) fail("출력", p.res.zipPath, err, "디스크 공간과 권한을 확인하세요");
  }
  step("보고서");
}

} // namespace

std::string BuildProblem::Line() const {
  return std::format("[{}] {} · {} — {} → {}", error ? "오류" : "경고", step, what, cause, fix);
}
int BuildResult::Errors() const { return (int)std::count_if(problems.begin(), problems.end(), [](const BuildProblem& p) { return p.error; }); }
int BuildResult::Warnings() const { return (int)problems.size() - Errors(); }

BuildResult CheckBuild(const BuildOptions& options) {
  Plan p;
  p.opt = options;
  RunChecks(p);
  p.res.outDir = U(p.out);
  p.res.ok = p.res.Errors() == 0;
  return p.res;
}

BuildResult BuildGame(const BuildOptions& options) {
  Plan p;
  p.opt = options;
  RunChecks(p);
  p.res.outDir = U(p.out);
  if (p.res.Errors() == 0) Build(p);
  p.res.ok = p.res.Errors() == 0;
  return p.res;
}

// ---- zip (stored) ------------------------------------------------------------------------------------------

namespace {
uint32_t Crc32(const uint8_t* d, size_t n) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
template <class T> void Le(std::vector<uint8_t>& b, T v) {
  for (size_t i = 0; i < sizeof(T); ++i) b.push_back((uint8_t)((uint64_t)v >> (8 * i)));
}
} // namespace

bool WriteZipOfFolder(const std::string& dir, const std::string& zipPath, std::string* error) {
  std::vector<std::pair<std::string, fs::path>> list;
  std::error_code ec;
  for (const auto& f : fs::recursive_directory_iterator(P(dir), ec))
    if (f.is_regular_file()) list.emplace_back(U(fs::relative(f.path(), P(dir))), f.path());
  std::sort(list.begin(), list.end());
  std::FILE* out = OpenU8(zipPath, "wb");
  if (!out) { if (error) *error = "cannot write " + zipPath; return false; }
  std::vector<uint8_t> central;
  uint64_t offset = 0;
  bool ok = true;
  for (const auto& [name, path] : list) {
    std::vector<uint8_t> data;
    if (!ReadFileBytes(path, data) || data.size() >= 0xFFFFFFFFull || offset >= 0xFFFFFFFFull) { ok = false; break; }
    const uint32_t crc = Crc32(data.data(), data.size());
    std::vector<uint8_t> h;
    Le<uint32_t>(h, 0x04034b50); Le<uint16_t>(h, 20); Le<uint16_t>(h, 0x0800); Le<uint16_t>(h, 0); // UTF-8 names, stored
    Le<uint16_t>(h, 0); Le<uint16_t>(h, 0x21); Le<uint32_t>(h, crc); Le<uint32_t>(h, (uint32_t)data.size()); Le<uint32_t>(h, (uint32_t)data.size());
    Le<uint16_t>(h, (uint16_t)name.size()); Le<uint16_t>(h, 0);
    h.insert(h.end(), name.begin(), name.end());
    ok = std::fwrite(h.data(), 1, h.size(), out) == h.size() && (data.empty() || std::fwrite(data.data(), 1, data.size(), out) == data.size());
    Le<uint32_t>(central, 0x02014b50); Le<uint16_t>(central, 20); Le<uint16_t>(central, 20); Le<uint16_t>(central, 0x0800); Le<uint16_t>(central, 0);
    Le<uint16_t>(central, 0); Le<uint16_t>(central, 0x21); Le<uint32_t>(central, crc); Le<uint32_t>(central, (uint32_t)data.size()); Le<uint32_t>(central, (uint32_t)data.size());
    Le<uint16_t>(central, (uint16_t)name.size()); Le<uint16_t>(central, 0); Le<uint16_t>(central, 0); Le<uint16_t>(central, 0); Le<uint16_t>(central, 0);
    Le<uint32_t>(central, 0); Le<uint32_t>(central, (uint32_t)offset);
    central.insert(central.end(), name.begin(), name.end());
    offset += h.size() + data.size();
    if (!ok) break;
  }
  std::vector<uint8_t> end;
  Le<uint32_t>(end, 0x06054b50); Le<uint16_t>(end, 0); Le<uint16_t>(end, 0); Le<uint16_t>(end, (uint16_t)list.size()); Le<uint16_t>(end, (uint16_t)list.size());
  Le<uint32_t>(end, (uint32_t)central.size()); Le<uint32_t>(end, (uint32_t)offset); Le<uint16_t>(end, 0);
  ok = ok && std::fwrite(central.data(), 1, central.size(), out) == central.size() && std::fwrite(end.data(), 1, end.size(), out) == end.size();
  ok = std::fclose(out) == 0 && ok;
  if (!ok && error) *error = "zip failed (a file over 4 GB, or the disk is full)";
  return ok;
}

// ---- exe icon ----------------------------------------------------------------------------------------------

bool SetExeIcon(const std::string& exePath, const std::string& pngPath, std::string* error) {
#ifdef _WIN32
  int32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
  if (!LoadImageRGBA(P(pngPath), w, h, rgba) || w <= 0 || h <= 0) { if (error) *error = "cannot read the image"; return false; }
  // square 256 px (box filter down, nearest up), centred on a transparent square when it is not square
  const int S = 256, side = std::max(w, h), ox = (side - w) / 2, oy = (side - h) / 2;
  std::vector<uint8_t> icon((size_t)S * S * 4, 0);
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const int x0 = x * side / S, x1 = std::max(x0 + 1, (x + 1) * side / S), y0 = y * side / S, y1 = std::max(y0 + 1, (y + 1) * side / S);
      uint32_t sum[4] = {}, n = 0;
      for (int sy = y0; sy < y1; ++sy)
        for (int sx = x0; sx < x1; ++sx) {
          const int ix = sx - ox, iy = sy - oy;
          ++n;
          if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
          const uint8_t* px = &rgba[((size_t)iy * w + ix) * 4];
          for (int k = 0; k < 4; ++k) sum[k] += px[k];
        }
      for (int k = 0; k < 4; ++k) icon[((size_t)y * S + x) * 4 + k] = (uint8_t)(sum[k] / std::max(n, 1u));
    }
  std::vector<uint8_t> png;
  if (!EncodePNG(S, S, icon.data(), false, png)) { if (error) *error = "cannot encode the icon"; return false; }
  // RT_GROUP_ICON: GRPICONDIR + one GRPICONDIRENTRY (width/height 0 = 256)
  std::vector<uint8_t> group;
  Le<uint16_t>(group, 0); Le<uint16_t>(group, 1); Le<uint16_t>(group, 1);
  group.push_back(0); group.push_back(0); group.push_back(0); group.push_back(0);
  Le<uint16_t>(group, 1); Le<uint16_t>(group, 32); Le<uint32_t>(group, (uint32_t)png.size()); Le<uint16_t>(group, 1);
  const std::wstring exe = P(exePath).wstring();
  const LPCWSTR kIcon = MAKEINTRESOURCEW(3), kGroup = MAKEINTRESOURCEW(14); // RT_ICON, RT_GROUP_ICON (wide)
  // the icons the exe has now (every RT_ICON and group 1, with their languages): replaced, not added next to
  struct Res { bool group; WORD id, lang; };
  std::vector<Res> old;
  WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
  if (HMODULE m = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
    struct Ctx { std::vector<Res>* out; bool group; WORD id; } ctx{&old, false, 0};
    EnumResourceNamesW(m, kIcon, [](HMODULE mod, LPCWSTR type, LPWSTR name, LONG_PTR param) -> BOOL {
      if (!IS_INTRESOURCE(name)) return TRUE;
      auto* c = reinterpret_cast<Ctx*>(param);
      c->group = false;
      c->id = (WORD)(ULONG_PTR)name;
      EnumResourceLanguagesW(mod, type, name, [](HMODULE, LPCWSTR, LPCWSTR, WORD l, LONG_PTR q) -> BOOL {
        auto* cc = reinterpret_cast<Ctx*>(q);
        cc->out->push_back({cc->group, cc->id, l});
        return TRUE;
      }, param);
      return TRUE;
    }, (LONG_PTR)&ctx);
    ctx.group = true;
    ctx.id = 1;
    EnumResourceLanguagesW(m, kGroup, MAKEINTRESOURCEW(1), [](HMODULE, LPCWSTR, LPCWSTR, WORD l, LONG_PTR q) -> BOOL {
      auto* cc = reinterpret_cast<Ctx*>(q);
      cc->out->push_back({cc->group, cc->id, l});
      return TRUE;
    }, (LONG_PTR)&ctx);
    FreeLibrary(m);
  }
  for (const Res& r : old) if (r.group) lang = r.lang;
  HANDLE u = BeginUpdateResourceW(exe.c_str(), FALSE);
  if (!u) { if (error) *error = std::format("cannot open the exe for writing (error {})", GetLastError()); return false; }
  for (const Res& r : old) UpdateResourceW(u, r.group ? kGroup : kIcon, MAKEINTRESOURCEW(r.id), r.lang, nullptr, 0);
  bool ok = UpdateResourceW(u, kIcon, MAKEINTRESOURCEW(1), lang, png.data(), (DWORD)png.size()) &&
            UpdateResourceW(u, kGroup, MAKEINTRESOURCEW(1), lang, group.data(), (DWORD)group.size());
  ok = EndUpdateResourceW(u, !ok) && ok;
  if (!ok && error) *error = std::format("UpdateResource failed (error {})", GetLastError());
  return ok;
#else
  (void)exePath; (void)pngPath;
  if (error) *error = "Windows only";
  return false;
#endif
}

} // namespace bdg
