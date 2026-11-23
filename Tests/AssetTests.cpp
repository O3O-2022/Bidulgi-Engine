// Asset database tests (queue 2-3).
#include "Assets/AssetDatabase.h"
#include "bdg_test.h"
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
