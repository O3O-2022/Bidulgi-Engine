// Asset database tests (queue 2-3).
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImport.h"
#include "Assets/AssetRefs.h"
#include "Assets/Thumbnails.h"
#include "Core/Image.h"
#include "bdg_test.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace bdg;
namespace fs = std::filesystem;

namespace {
void Write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string Read(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
fs::path Fresh(const char* name) {
  fs::path d = fs::temp_directory_path() / name;
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}

TEST_CASE("assets: first scan writes .meta files with stable GUIDs") {
  fs::path d = Fresh("bdg_assets_scan");
  Write(d / "Knight.glb", "model-bytes");
  Write(d / "Textures/grass.png", "png-bytes");
  Write(d / ".cache/skip.png", "hidden folder");
  AssetDatabase db;
  CHECK(db.Open(d.string()));
  CHECK_EQ(db.Count(), size_t(2));
  CHECK(fs::exists(d / "Knight.glb.meta") && fs::exists(d / "Textures/grass.png.meta"));
  CHECK(!fs::exists(d / ".cache/skip.png.meta"));
  const AssetRecord* k = db.ByPath("Knight.glb");
  CHECK(k && k->importer == "model" && k->guid.size() == 16);
  CHECK(db.ByPath("Textures/grass.png")->importer == "texture");
  std::string meta = Read(d / "Knight.glb.meta");
  CHECK(meta.find("\"guid\":\"" + k->guid + "\"") != std::string::npos);
  std::string guid = k->guid;
  AssetDatabase again; // reopening keeps every GUID and does not touch the .meta files
  CHECK(again.Open(d.string()));
  CHECK(again.ByPath("Knight.glb")->guid == guid);
  CHECK_EQ(Read(d / "Knight.glb.meta"), meta);
  CHECK(again.ByGuid(guid) && again.ByGuid(guid)->path == "Knight.glb");
  fs::remove_all(d);
}

TEST_CASE("assets: user data in .meta files survives") {
  fs::path d = Fresh("bdg_assets_meta");
  Write(d / "boss.ogg", "ogg");
  Write(d / "boss.ogg.meta", "{\"guid\":\"a01c55e2b9f04c12\", \"importer\":\"audio\",\n \"source\":{\"author\":\"OO\", \"license\":\"Custom\"}}\n");
  AssetDatabase db;
  CHECK(db.Open(d.string()));
  CHECK(db.ByGuid("a01c55e2b9f04c12") && db.ByGuid("a01c55e2b9f04c12")->path == "boss.ogg");
  CHECK(db.Move("boss.ogg", "Audio/boss.ogg"));
  CHECK(Read(d / "Audio/boss.ogg.meta").find("\"license\":\"Custom\"") != std::string::npos);
  fs::remove_all(d);
}

TEST_CASE("assets: moves keep the GUID, inside and outside the editor") {
  fs::path d = Fresh("bdg_assets_move");
  Write(d / "Knight.glb", "knight");
  Write(d / "Sword.glb", "sword");
  Write(d / "Shield.glb", "shield");
  AssetDatabase db;
  CHECK(db.Open(d.string()));
  std::string knight = db.ByPath("Knight.glb")->guid, sword = db.ByPath("Sword.glb")->guid, shield = db.ByPath("Shield.glb")->guid;
  std::string err;
  CHECK(db.Move("Knight.glb", "Characters/Knight.glb", &err));       // editor move: file + .meta
  CHECK(db.ByPath("Characters/Knight.glb")->guid == knight && !db.ByPath("Knight.glb"));
  CHECK(fs::exists(d / "Characters/Knight.glb.meta") && !fs::exists(d / "Knight.glb.meta"));
  CHECK(!db.Move("Nope.glb", "x.glb", &err));
  fs::create_directories(d / "Weapons");
  fs::rename(d / "Sword.glb", d / "Weapons/Sword.glb");              // file manager: .meta left behind
  fs::rename(d / "Shield.glb", d / "Weapons/Shield.glb");
  fs::rename(d / "Shield.glb.meta", d / "Weapons/Shield.glb.meta");   // moved with its .meta
  CHECK_EQ(db.OrphanMetas().size(), size_t(1));
  auto ch = db.Refresh();
  CHECK_EQ(ch.moved.size(), size_t(2));
  CHECK(ch.added.empty() && ch.removed.empty());
  CHECK(db.ByPath("Weapons/Sword.glb") && db.ByPath("Weapons/Sword.glb")->guid == sword); // matched by content
  CHECK(db.ByPath("Weapons/Shield.glb")->guid == shield);
  CHECK(fs::exists(d / "Weapons/Sword.glb.meta") && !fs::exists(d / "Sword.glb.meta"));
  CHECK(db.OrphanMetas().empty());
  fs::remove_all(d);
}

TEST_CASE("assets: added, modified, removed, copies") {
  fs::path d = Fresh("bdg_assets_changes");
  Write(d / "a.png", "a");
  AssetDatabase db;
  CHECK(db.Open(d.string()));
  Write(d / "b.png", "b");
  Write(d / "a.png", "a changed, longer");
  auto ch = db.Refresh();
  CHECK(ch.added.size() == 1 && ch.added[0] == "b.png");
  CHECK(ch.modified.size() == 1 && ch.modified[0] == "a.png");
  fs::copy_file(d / "b.png", d / "c.png");
  fs::copy_file(d / "b.png.meta", d / "c.png.meta"); // copy with a copied .meta: must not share the GUID
  ch = db.Refresh();
  CHECK(db.ByPath("c.png") && db.ByPath("c.png")->guid != db.ByPath("b.png")->guid);
  fs::remove(d / "a.png");
  ch = db.Refresh();
  CHECK(ch.removed.size() == 1 && ch.removed[0] == "a.png");
  CHECK_EQ(db.OrphanMetas().size(), size_t(1)); // a.png.meta reported, not deleted
  CHECK(fs::exists(d / "a.png.meta"));
  fs::remove_all(d);
}

TEST_CASE("assets: the index cache skips unchanged files (queue 2-3b)") {
  fs::path project = Fresh("bdg_assets_cache"), d = project / "Assets";
  const char* env = std::getenv("BDG_PERF_ASSETS"); // measure big projects: BDG_PERF_ASSETS=10000
  int count = env ? std::atoi(env) : 3000;
  for (int i = 0; i < count; ++i) Write(d / ("Folder" + std::to_string(i % 40)) / ("asset_" + std::to_string(i) + ".png"), "data " + std::to_string(i));
  std::string cache = (project / ".cache" / "assetdb.jsonl").string();
  AssetDatabase first;
  CHECK(first.Open(d.string(), nullptr, cache));
  CHECK_EQ(first.LastScan().hashed, size_t(count));
  double cold = first.LastScan().ms;
  CHECK(fs::exists(cache));
  AssetDatabase second; // next start: nothing changed, nothing is read or hashed
  CHECK(second.Open(d.string(), nullptr, cache));
  CHECK_EQ(second.Count(), size_t(count));
  CHECK_EQ(second.LastScan().files, size_t(count));
  CHECK_EQ(second.LastScan().hashed, size_t(0));
  CHECK_EQ(second.LastScan().metasRead, size_t(0));
  CHECK(second.ByPath("Folder3/asset_3.png")->guid == first.ByPath("Folder3/asset_3.png")->guid);
  double warm = second.LastScan().ms;
  Write(d / "Folder5/asset_5.png", "changed while the editor was closed");
  fs::remove(d / "Folder6/asset_6.png");
  AssetDatabase third;
  CHECK(third.Open(d.string(), nullptr, cache));
  CHECK_EQ(third.LastScan().hashed, size_t(1));
  CHECK(!third.ByPath("Folder6/asset_6.png"));
  std::printf("       %d assets: first scan %.0f ms, cached scan %.0f ms\n", count, cold, warm);
  fs::remove_all(project);
}

TEST_CASE("assets: thumbnails are made once and cached (queue 2-5t)") {
  fs::path project = Fresh("bdg_thumbs"), d = project / "Assets";
  fs::create_directories(d / "Textures");
  std::vector<uint8_t> img(200 * 100 * 4);
  for (int y = 0; y < 100; ++y)
    for (int x = 0; x < 200; ++x) { uint8_t* p = &img[((size_t)y * 200 + x) * 4]; p[0] = (uint8_t)x; p[1] = (uint8_t)(y * 2); p[2] = 128; p[3] = 255; }
  CHECK(WritePNG((d / "Textures/wide.png").string().c_str(), 200, 100, img.data(), false));
  Write(d / "Textures/broken.png", "not a png");
  AssetDatabase db;
  CHECK(db.Open(d.string()));
  ThumbnailCache cache;
  cache.SetDirectory(project / ".cache" / "thumbnails");
  std::vector<uint8_t> t;
  const AssetRecord* r = db.ByPath("Textures/wide.png");
  CHECK(r && ThumbnailCache::Supported(*r));
  CHECK(cache.Get(*r, d / r->path, t));
  CHECK_EQ(t.size(), size_t(64 * 64 * 4));
  CHECK_EQ((int)t[(0 * 64 + 10) * 4 + 3], 0);    // 2:1 image: transparent band above
  CHECK_EQ((int)t[(32 * 64 + 10) * 4 + 3], 255); // picture in the middle
  CHECK_EQ(cache.GetStats().generated, 1);
  ThumbnailCache again;                            // next start: read from .cache, the source is not decoded
  again.SetDirectory(project / ".cache" / "thumbnails");
  std::vector<uint8_t> t2;
  CHECK(again.Get(*r, d / r->path, t2) && t2 == t);
  CHECK(again.GetStats().loaded == 1 && again.GetStats().generated == 0);
  const AssetRecord* bad = db.ByPath("Textures/broken.png");
  CHECK(bad && !cache.Get(*bad, d / bad->path, t) && cache.GetStats().failed == 1);
  fs::remove_all(project);
}

TEST_CASE("assets: references, unused assets and replacing one asset by another (queue 2-5b)") {
  fs::path d = Fresh("bdg_refs_test");
  // a scene naming a model by file name and a material by stem; the material naming a texture by path;
  // a script naming a sound by quoted path; one texture and one model nobody uses
  Write(d / "Scenes/Level.scene", "{\"v\":1,\"scene\":\"Level\"}\n"
                                  "{\"id\":\"g1\",\"name\":\"Rock\",\"MeshRenderer\":{\"mesh\":\"Rock.glb\",\"material\":\"M_Rock\"}}\n");
  Write(d / "Materials/M_Rock.material", "{\"material\":\"M_Rock\"}\n{\"param\":\"BaseColorMap\",\"type\":\"texture\",\"value\":\"Textures/Rock_D.png\"}\n");
  Write(d / "Materials/M_Moss.material", "{\"material\":\"M_Moss\"}\n");
  Write(d / "Textures/Rock_D.png", "png-bytes-1");
  Write(d / "Textures/Unused.png", "png-bytes-2");
  Write(d / "Models/Rock.glb", "glb-bytes");
  Write(d / "Models/Old.glb", "glb-bytes-2");
  Write(d / "Audio/Hit.wav", "wav");
  Write(d / "Scripts/Hit.cs", "class Hit { string clip = \"Audio/Hit.wav\"; }\n");
  AssetDatabase db;
  std::string err;
  CHECK(db.Open(d.string(), &err));
  AssetRefIndex index;
  index.Build(db);
  std::vector<AssetRef> model = index.RefsTo("Models/Rock.glb");
  CHECK(model.size() == 1 && model[0].from == "Scenes/Level.scene" && model[0].where == "g1 Rock · MeshRenderer.mesh" && model[0].token == "Rock.glb");
  std::vector<AssetRef> mat = index.RefsTo("Materials/M_Rock.material");
  CHECK(mat.size() == 1 && mat[0].token == "M_Rock");
  std::vector<AssetRef> tex = index.RefsTo("Textures/Rock_D.png");
  CHECK(tex.size() == 1 && tex[0].from == "Materials/M_Rock.material" && tex[0].where == "BaseColorMap · value");
  std::vector<AssetRef> wav = index.RefsTo("Audio/Hit.wav");
  CHECK(wav.size() == 1 && wav[0].from == "Scripts/Hit.cs" && wav[0].where == "line 1");
  // unused: reachable from neither the scene nor the script (the texture through the material counts as used)
  std::vector<std::string> unused = index.Unused();
  CHECK(unused == std::vector<std::string>({"Materials/M_Moss.material", "Models/Old.glb", "Textures/Unused.png"}));
  // an open document (the editor's unsaved scene) is a root too
  index.Build(db, {{"open", "{\"id\":\"g9\",\"MeshRenderer\":{\"mesh\":\"Old.glb\"}}\n"}});
  CHECK(index.RefsTo("Models/Old.glb").size() == 1 && index.Unused().size() == 2);
  // replace: the same kind of name, only exact strings, the rest of the file untouched
  const std::string before = Read(d / "Scenes/Level.scene");
  int count = 0;
  std::vector<std::string> files = ReplaceReferencesOnDisk(db, "Materials/M_Rock.material", "Materials/M_Moss.material", &count);
  CHECK(files == std::vector<std::string>({"Scenes/Level.scene"}) && count == 1);
  std::string after = Read(d / "Scenes/Level.scene");
  CHECK(after.find("\"material\":\"M_Moss\"") != std::string::npos && after.size() == before.size());
  CHECK(Read(d / "Materials/M_Rock.material").find("M_Rock") != std::string::npos); // its own file is left alone
  ReplaceReferencesOnDisk(db, "Textures/Rock_D.png", "Textures/Unused.png", &count);
  CHECK(count == 1 && Read(d / "Materials/M_Rock.material").find("\"Textures/Unused.png\"") != std::string::npos);
  fs::remove_all(d);
}

TEST_CASE("assets: importing dropped files, glTF dependencies and asset rules (queue 2-5d)") {
  fs::path outside = Fresh("bdg_import_src");
  fs::path d = Fresh("bdg_import_project");
  fs::create_directories(d / "Assets");
  Write(outside / "Box.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"bin/Box%20Data.bin","byteLength":4},{"uri":"data:application/octet-stream;base64,AAAA"}],)"
                              R"("images":[{"uri":"tex/wood.png"},{"uri":"bin/Box%20Data.bin"}]})");
  Write(outside / "bin/Box Data.bin", "1234");
  Write(outside / "tex/wood.png", "png");
  Write(outside / "Pack/a.txt", "a");
  Write(outside / "Pack/sub/b.txt", "b");
  Write(outside / "Pack/sub/b.txt.meta", "{\"guid\":\"theirs\"}");
  const std::vector<std::string> deps = GltfDependencies(outside / "Box.gltf");
  CHECK(deps.size() == 2 && deps[0] == "bin/Box Data.bin" && deps[1] == "tex/wood.png"); // data: skipped, no repeats

  AssetDatabase db;
  CHECK(db.Open((d / "Assets").string()));
  ImportResult r = ImportExternal(db, {outside / "Box.gltf", outside / "Pack", outside / "missing.png"}, "Models");
  CHECK(fs::exists(d / "Assets/Models/Box.gltf") && fs::exists(d / "Assets/Models/bin/Box Data.bin") && fs::exists(d / "Assets/Models/tex/wood.png"));
  CHECK(fs::exists(d / "Assets/Models/Pack/sub/b.txt") && !fs::exists(d / "Assets/Models/Pack/sub/b.txt.meta")); // the project makes its own
  CHECK_EQ(r.added.size(), size_t(5));
  CHECK_EQ(r.added[0], std::string("Models/Box.gltf"));
  CHECK_EQ(r.errors.size(), size_t(1)); // missing.png
  db.Refresh();
  CHECK(db.ByPath("Models/bin/Box Data.bin") != nullptr);

  // again: a new name for the model, the same dependency is shared; a different one is not overwritten
  r = ImportExternal(db, {outside / "Box.gltf"}, "Models");
  CHECK(r.added.size() == 1 && r.added[0] == "Models/Box 1.gltf" && r.errors.empty());
  Write(outside / "tex/wood.png", "other png");
  r = ImportExternal(db, {outside / "Box.gltf"}, "Models");
  CHECK(r.added.size() == 1 && r.added[0] == "Models/Box 2.gltf" && r.errors.size() == 1);
  CHECK_EQ(Read(d / "Assets/Models/tex/wood.png"), std::string("png"));
  r = ImportExternal(db, {d / "Assets/Models/Box.gltf"}, ""); // already in the project
  CHECK(r.added.empty() && r.errors.size() == 1);
  CHECK_EQ(UniqueFileName(d / "Assets/Models", "Box.gltf"), std::string("Box 3.gltf"));
  CHECK_EQ(UniqueFileName(d / "Assets/Models", "New.gltf"), std::string("New.gltf"));

  // rules: prefixes, texture size from the header, license in the .meta
  std::vector<uint8_t> px(16 * 8 * 4, 200);
  CHECK(WritePNG((d / "Assets/T_Wide.png").string().c_str(), 16, 8, px.data(), false));
  CHECK(WritePNG((d / "Assets/small.png").string().c_str(), 4, 4, px.data(), false));
  Write(d / "Assets/SM_Rock.glb", "glb");
  int32_t w = 0, h = 0;
  CHECK(ImageSize(d / "Assets/T_Wide.png", w, h) && w == 16 && h == 8);
  db.Refresh();
  Write(d / "Assets/SM_Rock.glb.meta", Read(d / "Assets/SM_Rock.glb.meta").substr(0, Read(d / "Assets/SM_Rock.glb.meta").find_last_of('}')) +
                                           ",\"source\":{\"author\":\"me\",\"license\":\"CC0\"}}\n");
  AssetRules rules;
  CHECK(rules.Load(d / "ProjectSettings/AssetRules.json")); // no file: defaults
  CHECK(rules.prefix.empty() && rules.maxTextureSize == 8192 && rules.requireLicense.empty());
  Write(d / "ProjectSettings/AssetRules.json", R"({"prefix":{"texture":"T_","model":"SM_"},"maxTextureSize":8,"requireLicense":["model"]})");
  CHECK(rules.Load(d / "ProjectSettings/AssetRules.json"));
  AssetRuleChecker checker;
  const std::vector<std::string> only = {"T_Wide.png", "small.png", "SM_Rock.glb", "Models/Box.gltf"};
  const std::vector<AssetIssue> issues = checker.Check(db, rules, &only);
  auto has = [&](const char* path, const char* rule) {
    return std::any_of(issues.begin(), issues.end(), [&](const AssetIssue& i) { return i.path == path && i.rule == rule; });
  };
  CHECK(has("T_Wide.png", "size") && !has("T_Wide.png", "prefix"));
  CHECK(has("small.png", "prefix") && !has("small.png", "size"));
  CHECK(!has("SM_Rock.glb", "prefix") && !has("SM_Rock.glb", "license"));
  CHECK(has("Models/Box.gltf", "prefix") && has("Models/Box.gltf", "license"));
  CHECK_EQ(issues.size(), size_t(4));
  Write(d / "ProjectSettings/AssetRules.json", "{oops");
  std::string err;
  CHECK(!rules.Load(d / "ProjectSettings/AssetRules.json", &err) && !err.empty());
  fs::remove_all(outside);
  fs::remove_all(d);
}
