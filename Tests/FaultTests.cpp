// Fault injection tests (queue 1-4c): bad input, failing files and crashing code end in errors, not crashes.
#include "Core/Command.h"
#include "Core/Guard.h"
#include "Scene/Components.h"
#include "UI/Dock.h"
#include "bdg/bdg_api.h"
#include "bdg_test.h"
#include <cstdio>
#include <filesystem>
#include <stdexcept>

using namespace bdg;

namespace {
#pragma warning(push)
#pragma warning(disable : 4717) // recursion without end is the point here
int Recurse(int n) { volatile char pad[512]; pad[0] = (char)n; return Recurse(n + 1) + pad[0]; } // stack overflow on purpose
#pragma warning(pop)
}

TEST_CASE("faults: C API rejects null arguments") {
  bdg_engine* e = nullptr;
  CHECK(bdg_engine_create(nullptr, &e) == BDG_ERR_INVALID_ARG);
  bdg_engine_desc desc{};
  CHECK(bdg_engine_create(&desc, nullptr) == BDG_ERR_INVALID_ARG);
  CHECK(bdg_engine_run(nullptr) == BDG_ERR_INVALID_ARG);
  bdg_engine_destroy(nullptr); // allowed
  CHECK(std::string(bdg_result_string(BDG_ERR_INVALID_ARG)) == "invalid argument");
}

TEST_CASE("faults: guarded calls survive crashes") {
  uint32_t before = GuardFailureCount();
  std::string why;
  CHECK(GuardedCall("ok", [] {}));
  CHECK(!GuardedCall("null write", [] { volatile int* p = nullptr; *p = 1; }, &why));
  CHECK(why.find("access violation") != std::string::npos);
  CHECK(!GuardedCall("divide", [] { volatile int z = 0; volatile int x = 10 / z; (void)x; }, &why));
  CHECK(why.find("divide by zero") != std::string::npos);
  CHECK(!GuardedCall("recursion", [] { Recurse(0); }, &why));
  CHECK(why.find("stack overflow") != std::string::npos);
  CHECK(!GuardedCall("recursion again", [] { Recurse(0); }, &why)); // the guard page was restored
  CHECK(!GuardedCall("throw", [] { throw std::runtime_error("boom"); }, &why));
  CHECK_EQ(GuardFailureCount() - before, uint32_t(5));
  int handled = 0;
  SetGuardFailureHandler([&](const std::string& what, const std::string&) { handled += what == "with handler"; });
  GuardedCall("with handler", [] { volatile int* p = nullptr; *p = 2; });
  SetGuardFailureHandler(nullptr);
  CHECK_EQ(handled, 1);
}

TEST_CASE("faults: checks report and let the caller recover") {
  uint32_t before = CheckFailureCount();
  int value = 3;
  CHECK(BDG_CHECK(value == 3));
  bool ok = BDG_CHECK(value == 4, "value is {}, expected 4", value);
  CHECK(!ok);
  CHECK(!BDG_CHECK(false));
  CHECK_EQ(CheckFailureCount() - before, uint32_t(2));
}

TEST_CASE("faults: files that cannot be written or read") {
  const char* bad = "Q:\\no\\such\\drive\\file.jsonl";
  DockState d;
  d.titles = {{"a", "A"}};
  d.AddLeaf({"a"});
  CHECK(!d.Save(bad));
  CHECK(!d.Load(bad));
  CHECK_EQ(d.Describe(), std::string("[a*]")); // unchanged
  Scene s;
  CommandBus bus(s);
  CHECK(!bus.SaveLog(bad));
  std::string err;
  CHECK(!bus.Replay(bad, &err));
  CHECK(err.find("cannot open") != std::string::npos);
}

TEST_CASE("faults: a broken log line stops the replay with its line number") {
  Scene s;
  s.Create("g1", "Box");
  s.AddComponent("g1", Transform::StaticType());
  std::string path = (std::filesystem::temp_directory_path() / "bdg_fault_log.jsonl").string();
  if (std::FILE* f = std::fopen(path.c_str(), "w")) {
    std::fputs("{\"cmd\":\"set\",\"target\":\"g1\",\"args\":{\"path\":\"Transform.scale.x\",\"value\":2}}\n", f);
    std::fputs("{\"cmd\":\"set\",\"target\":\"g1\",\"args\":{\"path\":\"Transform.scale.x\",\"value\":\n", f); // cut off
    std::fputs("{\"cmd\":\"rename\",\"target\":\"g1\",\"args\":{\"name\":\"Never\"}}\n", f);
    std::fclose(f);
  }
  CommandBus bus(s);
  std::string err;
  size_t n = 0;
  CHECK(!bus.Replay(path, &err, &n));
  CHECK(err.find("(2)") != std::string::npos);
  CHECK_EQ(n, size_t(1));                       // the good line before it was applied
  CHECK_EQ(s.Find("g1")->name, std::string("Box")); // nothing after it
  std::remove(path.c_str());
}
