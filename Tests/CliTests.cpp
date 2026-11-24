// `bdg` command line (queue 2-7) and UTF-8 paths.
#include "Core/Utf8Path.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include "Tools/Cli.h"
#include "bdg_test.h"
#include <filesystem>
#include <fstream>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
struct Run {
  int code = 0;
  std::string out, err;
};
Run Cli(const fs::path& cwd, std::vector<std::string> args) {
  Run r;
  r.code = RunCli(args, r.out, r.err, cwd);
  return r;
}
fs::path Fresh(const char8_t* name) {
  fs::path d = fs::temp_directory_path() / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
}

TEST_CASE("paths: UTF-8 strings open the same files as std::filesystem (queue 2-7)") {
  CHECK(IsUtf8("plain") && IsUtf8("게임/장면.scene") && !IsUtf8(std::string("\xB0\xD4", 2)) && !IsUtf8(std::string("\xE6\x97", 2)));
  const fs::path dir = Fresh(u8"bdg_utf8_게임");
  const std::u8string u = (dir / u8"장면.scene").u8string();
  const std::string utf8((const char*)u.data(), u.size());
  Scene s;
  s.Create("a", "에이", "");
  std::string err;
  CHECK(SaveSceneFile(s, "장면", utf8, &err));
  CHECK(fs::exists(dir / u8"장면.scene")); // not a garbled name
  Scene back;
  std::string name;
  CHECK(LoadSceneFile(back, utf8, &name) && name == "장면" && back.Find("a") && back.Find("a")->name == "에이");
  std::string text;
  CHECK(ReadTextU8(utf8, text) && Has(text, "에이"));
  fs::remove_all(dir);
}

TEST_CASE("cli: new, view, outline, query, schema (queue 2-7)") {
  const fs::path base = Fresh(u8"bdg_cli_새");
  Run r = Cli(base, {"new", "게임"});
  CHECK(r.code == 0 && fs::exists(base / u8"게임" / "Scenes" / "Main.scene") && fs::exists(base / u8"게임" / "Assets"));
  CHECK(Cli(base, {"new", "게임"}).code == 1); // already there
  const fs::path p = base / u8"게임";
  r = Cli(p, {"view"});
  CHECK(r.code == 0 && Has(r.out, "# Main (5 objects)"));
  CHECK(Has(r.out, "sun     Light   #Sun  rot -45,-30,0 | Light type=Directional intensity=3"));
  CHECK(Has(r.out, "ground  Mesh    #Ground  scale 20,1,20 | MeshRenderer mesh=Plane material=bdg:Grid"));
  r = Cli(p, {"outline"});
  CHECK(Has(r.out, "Sun  (sun: Light)") && Has(r.out, "Ground  (ground: MeshRenderer)"));
  r = Cli(p, {"query", "S*", "-f", "pos,Light.intensity"});
  CHECK(r.code == 0 && Has(r.out, "id\tname\tpos\tLight.intensity\n") && Has(r.out, "sun\tSun\t0,0,0\t3\n") && Has(r.out, "sky\tSky\t0,0,0\t-\n"));
  CHECK(!Has(r.out, "cam\t"));
  r = Cli(p, {"schema", "light"});
  CHECK(r.code == 0 && Has(r.out, "Light: type:LightType=Point") && Has(r.out, "intensity:float=1"));
  CHECK(Cli(p, {"schema", "Nope"}).code == 1);
  CHECK(Cli(p, {"frobnicate"}).code == 2 && Cli(p, {}).code == 2 && Cli(p, {"help"}).code == 0);
  CHECK(Cli(p, {"view", "--scene", "Missing"}).code == 1);
  fs::remove_all(base);
}

TEST_CASE("cli: set, add, del through the command bus; dry run; validate (queue 2-7)") {
  const fs::path base = Fresh(u8"bdg_cli_edit");
  CHECK(Cli(base, {"new", "p"}).code == 0);
  const fs::path p = base / "p", scene = p / "Scenes" / "Main.scene";
  auto text = [&] {
    std::ifstream in(scene, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  const std::string original = text();
  Run r = Cli(p, {"set", "ground", "name=바닥", "--dry-run"});
  CHECK(r.code == 0 && Has(r.out, "- {\"id\":\"ground\",\"name\":\"Ground\"") && Has(r.out, "+ {\"id\":\"ground\",\"name\":\"바닥\""));
  CHECK(text() == original); // dry run: nothing written
  CHECK(Cli(p, {"set", "sun", "Light.intensity=5", "rot=-60,-20,0"}).code == 0);
  CHECK(Has(text(), "\"rot\":[-60,-20,0],\"Light\":{\"type\":\"Directional\",\"intensity\":5}"));
  CHECK(Cli(p, {"set", "sun", "Light.nope=1"}).code == 1);
  CHECK(Cli(p, {"set", "nobody", "pos=1,2,3"}).code == 1);
  CHECK(Cli(p, {"add", "crate", "--name", "Crate", "--pos", "1,0.5,2", "--comp", "Blockout", "--comp", "MeshRenderer"}).code == 0);
  CHECK(Has(text(), "{\"id\":\"crate\",\"name\":\"Crate\",\"pos\":[1,0.5,2],\"Blockout\":{},\"MeshRenderer\":{}}"));
  CHECK(Cli(p, {"set", "crate", "MeshRenderer.mesh=Rock.glb"}).code == 0);
  r = Cli(p, {"validate"});
  CHECK(r.code == 1 && Has(r.out, "오류 Scenes/Main.scene:7 Crate MeshRenderer.mesh → 'Rock.glb'가 프로젝트에 없음"));
  CHECK(Cli(p, {"set", "crate", "MeshRenderer.mesh=Cube"}).code == 0);
  r = Cli(p, {"validate"});
  CHECK(r.code == 0 && Has(r.out, "통과: 오류 0"));
  CHECK(Cli(p, {"rmcomp", "crate", "Blockout"}).code == 0 && !Has(text(), "Blockout"));
  CHECK(Cli(p, {"del", "crate"}).code == 0 && !Has(text(), "crate"));
  std::string logText;
  { // closed before the folder is removed
    std::ifstream log(p / ".cache" / "bdg-commands.jsonl", std::ios::binary);
    logText.assign(std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>());
  }
  CHECK(Has(logText, "\"cmd\":\"set\",\"target\":\"sun\"") && Has(logText, "\"source\":\"terminal\""));
  fs::remove_all(base);
}

TEST_CASE("cli: refs and unused over the project's assets and scenes (queue 2-7)") {
  const fs::path base = Fresh(u8"bdg_cli_refs");
  CHECK(Cli(base, {"new", "p"}).code == 0);
  const fs::path p = base / "p";
  std::ofstream(p / "Assets" / "Rock.glb", std::ios::binary) << "glb";
  std::ofstream(p / "Assets" / "Old.png", std::ios::binary) << "png";
  CHECK(Cli(p, {"add", "rock", "--comp", "MeshRenderer"}).code == 0);
  CHECK(Cli(p, {"set", "rock", "MeshRenderer.mesh=Rock.glb"}).code == 0);
  Run r = Cli(p, {"refs", "Rock.glb"});
  CHECK(r.code == 0 && Has(r.out, "Scenes/Main.scene\trock · MeshRenderer.mesh\tRock.glb") && Has(r.out, "1곳"));
  r = Cli(p, {"unused"});
  CHECK(r.code == 0 && Has(r.out, "Old.png") && !Has(r.out, "Rock.glb"));
  CHECK(Cli(p, {"validate"}).code == 0);
  fs::remove_all(base);
}

TEST_CASE("cli: nodes and call run Graph nodes on a saved scene (queue E-8)") {
  const fs::path base = Fresh(u8"bdg_cli_nodes");
  CHECK(Cli(base, {"new", "p"}).code == 0);
  const fs::path p = base / "p";
  Run r = Cli(p, {"nodes", "Health."});
  CHECK(r.code == 0 && Has(r.out, "call  Health.Damage(exec, target: object, amount: int) -> exec, result: int") && Has(r.out, "pure  Health.Ratio("));
  CHECK(Cli(p, {"add", "hero", "--comp", "Health"}).code == 0);
  r = Cli(p, {"call", "hero", "Health.Damage", "30"});
  CHECK(r.code == 0 && r.out == "70\n");
  r = Cli(p, {"call", "hero", "Health.Ratio"});
  CHECK(r.code == 0 && r.out == "0.7\n");
  CHECK(Cli(p, {"call", "hero", "Health.Set current", "5"}).code == 0);
  r = Cli(p, {"query", "hero", "-f", "Health.current"});
  CHECK(Has(r.out, "hero\thero\t5\n"));
  CHECK(Cli(p, {"call", "hero", "Health.Nope"}).code == 1);
  CHECK(Cli(p, {"call", "hero", "Health.Damage"}).code == 1); // missing argument
  fs::remove_all(base);
}
