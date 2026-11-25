// Game API (queue 6-5b): saves (folder from the project settings, versioned files, .bak recovery) and the physics
// scene queries behind Raycast / OverlapSphere (Jolt 3D and Box2D 2D).
#include "bdg_test.h"
#include "Physics/Physics.h"
#include "Project/ProjectSettings.h"
#include "Scripting/SaveGame.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace bdg;
namespace fs = std::filesystem;

TEST_CASE("saves: folder pattern, write / read round trip, versions, damaged file falls back to .bak (queue 6-5b)") {
#ifdef _WIN32
  _wputenv_s(L"BDG_TEST_SAVE_ROOT", L"C:/Users/한글/AppData");
#else
  setenv("BDG_TEST_SAVE_ROOT", "/home/한글", 1);
#endif
  const std::string folder = ExpandSaveFolder("%BDG_TEST_SAVE_ROOT%/{company}/{name}/Saves", "Pigeon: Studio", "My Game?");
#ifdef _WIN32
  CHECK(folder == "C:/Users/한글/AppData/Pigeon Studio/My Game/Saves");
#else
  CHECK(folder == "/home/한글/Pigeon Studio/My Game/Saves");
#endif
  CHECK(ExpandSaveFolder("%NO_SUCH_VAR_BDG%/x", "a", "b") == "%NO_SUCH_VAR_BDG%/x");
  CHECK(SaveSlotNameOk("main") && SaveSlotNameOk("slot_2") && !SaveSlotNameOk("") && !SaveSlotNameOk("../x") && !SaveSlotNameOk("a:b"));

  const fs::path dir = fs::temp_directory_path() / "bdg-save-test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  const std::u8string u = dir.generic_u8string();
  const std::string d(u.begin(), u.end());
  ProjectSettings game;
  game.description.name = "Save Test";
  std::string data, err;
  CHECK(!ReadSaveSlot(d, "main", data, &err) && err.empty()); // no save yet: not an error
  CHECK(WriteSaveSlot(d, "main", R"({"best":12,"name":"비둘기","items":[1,2]})", game, &err));
  CHECK(ReadSaveSlot(d, "main", data, &err) && data == R"({"best":12,"name":"비둘기","items":[1,2]})");
  CHECK(!WriteSaveSlot(d, "main", "[1,2]", game, &err)); // data must be an object
  CHECK(WriteSaveSlot(d, "main", R"({"best":13})", game, &err)); // the previous one becomes .bak
  CHECK(fs::exists(dir / "main.save.json.bak"));
  { // a damaged newest file: the last good one is read
    std::ofstream(dir / "main.save.json", std::ios::binary | std::ios::trunc) << "{\"format\":\"bidulgi-save\",\"vers";
  }
  CHECK(ReadSaveSlot(d, "main", data, &err) && data == R"({"best":12,"name":"비둘기","items":[1,2]})");
  { // written by a newer version of the game: refused, with the reason
    std::ofstream(dir / "other.save.json", std::ios::binary) << R"({"format":"bidulgi-save","version":99,"data":{}})";
  }
  CHECK(!ReadSaveSlot(d, "other", data, &err) && err.find("version 99") != std::string::npos);
  std::vector<std::string> slots = ListSaveSlots(d);
  CHECK(slots.size() == 2 && slots[0] == "main" && slots[1] == "other");
  CHECK(DeleteSaveSlot(d, "main") && !fs::exists(dir / "main.save.json") && !fs::exists(dir / "main.save.json.bak"));
  CHECK(!DeleteSaveSlot(d, "main"));
  fs::remove_all(dir, ec);
}

TEST_CASE("physics queries: raycast hits the nearest body with its normal, overlap finds bodies in a sphere (queue 6-5b)") {
  auto add = [](phys::Backend& be, uint64_t user, phys::ShapeKind kind, glm::vec3 pos, glm::vec3 half, bool trigger = false) {
    phys::BodyDesc d;
    d.user = user;
    d.motion = phys::Motion::Static;
    d.shape.kind = kind;
    d.shape.halfExtents = half;
    d.position = pos;
    d.trigger = trigger;
    CHECK(be.Add(d) != phys::kNoBody);
  };
  auto jolt = phys::CreateJolt();
  add(*jolt, 1, phys::ShapeKind::Box, {0, -0.5f, 0}, {10, 0.5f, 10});      // ground
  add(*jolt, 2, phys::ShapeKind::Box, {0, 1, -5}, {0.5f, 0.5f, 0.5f});     // a crate ahead
  add(*jolt, 3, phys::ShapeKind::Box, {0, 1, -2}, {0.5f, 0.5f, 0.5f}, true); // a trigger before it
  jolt->Step(1.f / 60.f);
  phys::RayHit hit;
  CHECK(jolt->Raycast({0, 1, 0}, {0, 0, -1}, 100, false, hit) && hit.user == 2 && std::abs(hit.distance - 4.5f) < 0.05f);
  CHECK(std::abs(hit.normal.z - 1) < 0.01f && std::abs(hit.point.z + 4.5f) < 0.05f);
  CHECK(jolt->Raycast({0, 1, 0}, {0, 0, -1}, 100, true, hit) && hit.user == 3); // triggers only when asked
  CHECK(!jolt->Raycast({0, 1, 0}, {0, 0, -1}, 3, false, hit));                 // too short
  CHECK(jolt->Raycast({3, 5, 3}, {0, -2, 0}, 100, false, hit) && hit.user == 1 && std::abs(hit.point.y) < 0.05f && hit.normal.y > 0.99f);
  std::vector<uint64_t> users;
  jolt->OverlapSphere({0, 1, -4.2f}, 0.5f, false, users);
  CHECK(std::find(users.begin(), users.end(), 2) != users.end() && std::find(users.begin(), users.end(), 1) == users.end());
  users.clear();
  jolt->OverlapSphere({0, 1, -2}, 0.3f, false, users);
  CHECK(users.empty());
  jolt->OverlapSphere({0, 1, -2}, 0.3f, true, users);
  CHECK(users.size() == 1 && users[0] == 3);

  auto box2d = phys::CreateBox2D();
  add(*box2d, 7, phys::ShapeKind::Box2D, {0, -0.5f, 0}, {10, 0.5f, 1});
  add(*box2d, 8, phys::ShapeKind::Box2D, {4, 1, 0}, {0.5f, 0.5f, 1});
  box2d->Step(1.f / 60.f);
  CHECK(box2d->Raycast({0, 1, 0}, {1, 0, 0}, 50, false, hit) && hit.user == 8 && std::abs(hit.distance - 3.5f) < 0.05f && hit.normal.x < -0.99f);
  CHECK(box2d->Raycast({2, 4, 0}, {0, -1, 0}, 50, false, hit) && hit.user == 7 && std::abs(hit.point.y) < 0.05f);
  users.clear();
  box2d->OverlapSphere({4, 1, 0}, 0.2f, false, users);
  CHECK(users.size() == 1 && users[0] == 8);
}
