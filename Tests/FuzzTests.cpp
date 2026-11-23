// Loader fuzzing (queue 1-4c): thousands of mutated inputs for every text loader. Each call must return
// (true or false) without crashing; run under the ASan build (BDG_ASAN=ON) to also catch memory errors.
#include "Core/Command.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Tests/BTestComponent.h"
#include "UI/Dock.h"
#include "bdg_test.h"
#include <cstdio>
#include <filesystem>
#include <random>

using namespace bdg;

namespace {
// Byte-level mutations of a valid input: flips, deletions, insertions of JSON-ish bytes, truncation.
std::string Mutate(const std::string& in, std::mt19937& rng) {
  std::string s = in;
  static const char pool[] = "{}[]\",:.0123456789-+eEtruefalsn\\ \n\x01\xff\xc3\xa9";
  int edits = 1 + (int)(rng() % 6);
  for (int i = 0; i < edits && !s.empty(); ++i) {
    size_t at = rng() % s.size();
    switch (rng() % 5) {
      case 0: s[at] = (char)(rng() & 0xff); break;
      case 1: s.erase(at, 1 + rng() % 4); break;
      case 2: s.insert(at, 1, pool[rng() % (sizeof pool - 1)]); break;
      case 3: s.resize(at); break;
      default: s.insert(at, s.substr(rng() % s.size(), rng() % 8)); break;
    }
  }
  return s;
}

constexpr int kRounds = 1500;
}

TEST_CASE("fuzz: reflection values") {
  std::mt19937 rng(12345);
  bdgtest::BTestExtras x;
  const auto& t = bdgtest::BTestExtras::StaticType();
  ValueType vt;
  vt.type = FieldType::Struct;
  vt.structType = &t;
  x.path = {{1, 2}, {3, 4}};
  std::string valid = Registry::ToJson(&x, vt);
  int accepted = 0;
  for (int i = 0; i < kRounds; ++i) {
    bdgtest::BTestExtras y;
    std::string err;
    accepted += Registry::FromJson(&y, vt, Mutate(valid, rng), &err);
  }
  const char* paths[] = {"loot[", "loot[-1]", "loot[99999999999]", "path[0].", "..", "anchor.x.y", "tint.q", "[0]", "", "path[1]x"};
  for (const char* p : paths) {
    std::string v;
    Registry::Get(&x, t, p, v);
    Registry::Set(&x, t, p, "1");
  }
  CHECK(accepted >= 0); // reaching here without a crash is the test
  std::printf("       reflection: %d of %d mutated values accepted\n", accepted, kRounds);
}

TEST_CASE("fuzz: scene snapshots and command lines") {
  std::mt19937 rng(777);
  Scene base;
  base.Create("g1", "Root");
  base.AddComponent("g1", Transform::StaticType());
  base.AddComponent("g1", Light::StaticType());
  base.Create("g2", "Child", "g1");
  base.AddComponent("g2", Health::StaticType());
  std::string snap = base.Snapshot("g1");
  const std::string lines[] = {
      R"({"cmd":"set","target":"g1","args":{"path":"Transform.position","value":[1,2,3]},"source":"ui","t":0.5})",
      R"({"cmd":"add","args":{"name":"Cube","parent":"g1","index":0,"id":"g7"},"source":"ai"})",
      R"({"cmd":"mv","target":"g2","args":{"parent":"","index":0}})",
      R"({"cmd":"addcomp","target":"g2","args":{"type":"Camera","value":{"fov":70}}})",
      R"({"cmd":"undo"})"};
  int restored = 0, executed = 0;
  for (int i = 0; i < kRounds; ++i) {
    Scene s;
    std::string err;
    restored += s.Restore(Mutate(snap, rng), &err);
    Scene t2;
    t2.Restore(snap);
    CommandBus bus(t2);
    for (int k = 0; k < 4; ++k) executed += bus.ExecuteLine(Mutate(lines[rng() % 5], rng), &err);
  }
  CHECK(restored >= 0 && executed >= 0);
  std::printf("       scene: %d snapshots restored, %d command lines executed\n", restored, executed);
}

TEST_CASE("fuzz: dock layout files") {
  std::mt19937 rng(4242);
  DockState d;
  d.titles = {{"a", "A"}, {"b", "B"}, {"c", "C"}};
  int la = d.AddLeaf({"a"}), lb = d.AddLeaf({"b", "c"});
  d.AddSplit(Axis::Row, 0.3f, la, lb);
  std::string path = (std::filesystem::temp_directory_path() / "bdg_fuzz_layout.jsonl").string();
  CHECK(d.Save(path));
  std::string valid;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) valid.append(buf, n);
    std::fclose(f);
  }
  int loaded = 0;
  for (int i = 0; i < kRounds; ++i) {
    std::string m = Mutate(valid, rng);
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(m.data(), 1, m.size(), f); std::fclose(f); }
    DockState e;
    e.titles = d.titles;
    e.AddLeaf({"a", "b", "c"});
    if (e.Load(path)) {
      ++loaded;
      CHECK(e.FindLeaf("a") >= 0 && e.FindLeaf("b") >= 0 && e.FindLeaf("c") >= 0); // every panel still placed
      e.Describe();
    }
  }
  std::remove(path.c_str());
  std::printf("       dock: %d of %d mutated layouts loaded\n", loaded, kRounds);
}
