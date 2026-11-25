// Game builds (queues 7-4, 7-4b): .bpak archives, zip, the build of a small project, and the pre-check that
// explains every problem of a deliberately broken project.
#include "Build/Build.h"
#include "Build/Pak.h"
#include "Core/Image.h"
#include "Project/ProjectSettings.h"
#include "bdg_test.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
fs::path Fresh(const char* name) {
  const fs::path d = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}
std::string S(const fs::path& p) {
  const std::u8string u = p.generic_u8string();
  return std::string(u.begin(), u.end());
}
void Write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
const char* kScene = R"({"v":1,"defaults":"1.0","scene":"Main","mode":"3d"}
{"id":"cam","name":"Main Camera","pos":[0,1.5,-6],"Camera":{}}
{"id":"box","name":"Box","MeshRenderer":{"mesh":"Cube","material":"M_Rock"}}
)";
bool Has(const BuildResult& r, const std::string& step, const std::string& text, bool error = true) {
  return std::any_of(r.problems.begin(), r.problems.end(), [&](const BuildProblem& p) {
    return p.error == error && p.step == step && (p.what + " " + p.cause).find(text) != std::string::npos && !p.fix.empty();
  });
}
} // namespace

TEST_CASE("bpak: files round-trip, compression, hashes, damage is caught (queue 7-4)") {
  const fs::path d = Fresh("bdg_pak");
  std::vector<uint8_t> big(100000), small{'h', 'i'};
  for (size_t i = 0; i < big.size(); ++i) big[i] = (uint8_t)(i % 7);
  PakWriter w;
  std::string err;
  CHECK(w.Open(S(d / "a.bpak"), &err));
  CHECK(w.Add("Scenes/Main.scene", big, true, &err));
  CHECK(w.Add("Data/한글.txt", small, true, &err));
  CHECK(w.Add("empty.txt", {}, true, &err));
  CHECK(!w.Add("../evil.txt", small, true, &err) && err.find("relative") != std::string::npos);
  CHECK(!w.Add("empty.txt", small, true, &err) && err.find("twice") != std::string::npos);
  CHECK(w.Finish(&err));
  CHECK(w.Entries()[0].method == 1 && w.Entries()[0].stored < 2000); // compressible: zstd
  CHECK(w.Entries()[1].method == 0);                                  // tiny: stored
  PakReader r;
  CHECK(r.Open(S(d / "a.bpak"), &err) && r.Entries().size() == 3);
  std::vector<uint8_t> out;
  CHECK(r.Find("Scenes/Main.scene") && r.Read(*r.Find("Scenes/Main.scene"), out, &err) && out == big);
  CHECK(r.Find("Data/한글.txt") && r.Read(*r.Find("Data/한글.txt"), out, &err) && out == small);
  size_t written = 0;
  CHECK(r.ExtractAll(S(d / "x"), &err, &written) && written == 3 && fs::file_size(d / "x" / "Scenes" / "Main.scene") == big.size());
  CHECK(r.ExtractAll(S(d / "x"), &err, &written) && written == 0); // unchanged files are not written again
  const uint64_t hash = r.ContentHash();
  // a flipped byte in the data: reading that file fails (hash), the archive still opens
  {
    std::fstream f(d / "a.bpak", std::ios::in | std::ios::out | std::ios::binary);
    f.seekp((std::streamoff)r.Find("Data/한글.txt")->offset);
    f.put('X');
  }
  r.Close();
  PakReader r2;
  CHECK(r2.Open(S(d / "a.bpak"), &err) && r2.ContentHash() == hash);
  CHECK(!r2.Read(*r2.Find("Data/한글.txt"), out, &err) && err.find("damaged") != std::string::npos);
  Write(d / "not.bpak", "hello");
  PakReader r3;
  CHECK(!r3.Open(S(d / "not.bpak"), &err) && err.find("not a .bpak") != std::string::npos);
  std::error_code ec;
  fs::remove_all(d, ec);
}

TEST_CASE("build: a small project becomes a player folder with a pack, report and zip (queue 7-4)") {
  const fs::path d = Fresh("bdg_build_ok");
  const fs::path proj = d / "Game";
  Write(proj / "Scenes" / "Main.scene", kScene);
  Write(proj / "Data" / "Weapons.table.jsonl", "{\"id\":\"rifle\",\"damage\":12}\n");
  Write(proj / "Assets" / "Materials" / "M_Rock.material", R"({"baseColor":[0.5,0.4,0.3,1]})");
  Write(proj / "Assets" / "Unused" / "Old.material", R"({"baseColor":[1,0,0,1]})");
  std::vector<uint8_t> px(64 * 64 * 4, 200);
  CHECK(WritePNG(S(proj / "Icon.png").c_str(), 64, 64, px.data(), false));
  ProjectSettings s;
  s.description.name = "Test Game";
  s.description.icon = "Icon.png";
  s.packaging.zipBuild = true;
  CHECK(SaveProjectSettings(S(proj / "ProjectSettings" / "Project.json"), s));
  BuildOptions o;
  o.project = S(proj);
  o.output = S(d / "Out");
  const BuildResult r = BuildGame(o);
  for (const BuildProblem& p : r.problems) std::printf("  %s\n", p.Line().c_str());
  CHECK(r.ok && r.Errors() == 0);
  CHECK(fs::exists(d / "Out" / "Test Game.exe") && fs::exists(d / "Out" / "Content" / "game.bpak"));
  CHECK(fs::exists(d / "Out" / "THIRD-PARTY-NOTICES.txt") && fs::exists(r.zipPath));
  CHECK(Has(r, "아이콘", "작음", false)); // 64 px icon: a warning, still built
  std::string report;
  {
    std::ifstream in(d / "Out" / "build-report.txt", std::ios::binary);
    report.assign(std::istreambuf_iterator<char>(in), {});
  }
  CHECK(report.find("Test Game") != std::string::npos && report.find("Scenes/Main.scene") != std::string::npos);
  CHECK(report.find("Assets/Unused/Old.material") != std::string::npos); // listed as left out
  PakReader pak;
  std::string err;
  CHECK(pak.Open(S(d / "Out" / "Content" / "game.bpak"), &err));
  CHECK(pak.Find("ProjectSettings/Project.json") && pak.Find("Scenes/Main.scene") && pak.Find("Data/Weapons.table.jsonl"));
  CHECK(pak.Find("Assets/Materials/M_Rock.material") && pak.Find("Assets/Materials/M_Rock.material.meta"));
  CHECK(!pak.Find("Assets/Unused/Old.material")); // nothing uses it: stripped
  pak.Close();
  // building again replaces the previous build; a foreign non-empty folder is refused
  const BuildResult again = BuildGame(o);
  for (const BuildProblem& p : again.problems) if (p.error) std::printf("  %s\n", p.Line().c_str());
  CHECK(again.ok);
  Write(d / "Foreign" / "keep.txt", "mine");
  o.output = S(d / "Foreign");
  const BuildResult foreign = BuildGame(o);
  CHECK(!foreign.ok && Has(foreign, "출력", "비어 있지 않은") && fs::exists(d / "Foreign" / "keep.txt"));
  std::error_code ec;
  fs::remove_all(d, ec);
}

TEST_CASE("build --check: a deliberately broken project reports every problem with its cause and fix (queue 7-4b)") {
  const fs::path d = Fresh("bdg_build_broken");
  const fs::path proj = d / "Broken";
  // 1 version, 2 identifier (settings), 3 executable name, 4 icon file, 5 output inside Assets, 6 scene listed but
  // missing, 7 unreadable scene, 8 unknown component, 9 missing mesh, 10 no camera in the start scene,
  // 11 material naming a missing texture, 12 script assembly missing, 13 data table rows (warning), 14 sound
  Write(proj / "Scenes" / "Main.scene", R"({"v":1,"defaults":"1.0","scene":"Main","mode":"3d"}
{"id":"box","name":"Box","MeshRenderer":{"mesh":"Statue.glb"},"Spinnner":{},"AudioSource":{"clip":"Boom.wav"}}
)");
  Write(proj / "Scenes" / "Bad.scene", "this is not a scene\n{{{\n");
  Write(proj / "Assets" / "M_Wall.material", R"({"baseColorMap":"Textures/Wall_D.png"})");
  Write(proj / "Data" / "Items.table.jsonl", "{\"id\":\"a\"}\n{\"id\":\"a\"}\n");
  ProjectSettings s;
  s.description.version = "one";
  s.description.identifier = "Not An Id";
  s.description.icon = "Art/Icon.png";
  s.packaging.executableName = "My:Game";
  s.packaging.outputFolder = "Assets/Builds";
  s.maps.scenesInBuild = "Scenes/Main.scene;Scenes/Bad.scene;Scenes/Gone.scene";
  CHECK(SaveProjectSettings(S(proj / "ProjectSettings" / "Project.json"), s));
  BuildOptions o;
  o.project = S(proj);
  o.scriptDlls = {S(d / "NoSuch.dll")};
  const BuildResult r = CheckBuild(o);
  for (const BuildProblem& p : r.problems) std::printf("  %s\n", p.Line().c_str());
  CHECK(!r.ok);
  CHECK(Has(r, "설정", "버전"));
  CHECK(Has(r, "설정", "식별자"));
  CHECK(Has(r, "설정", "My:Game"));
  CHECK(Has(r, "아이콘", "파일이 없음"));
  CHECK(Has(r, "출력", "Assets/ 안"));
  CHECK(Has(r, "장면", "Gone.scene"));
  CHECK(Has(r, "장면", "Bad.scene"));
  CHECK(Has(r, "장면", "Spinnner"));
  CHECK(Has(r, "에셋", "Statue.glb"));
  CHECK(Has(r, "장면", "Camera"));
  CHECK(Has(r, "에셋", "Wall_D.png"));
  CHECK(Has(r, "스크립트", "NoSuch.dll"));
  CHECK(Has(r, "데이터", "twice", false));
  CHECK(Has(r, "에셋", "Boom.wav")); // 14: a sound file that is not there (6-3)
  CHECK(r.Errors() >= 13);
  CHECK(!fs::exists(proj / "Assets" / "Builds")); // a check writes nothing
  std::error_code ec;
  fs::remove_all(d, ec);
}
