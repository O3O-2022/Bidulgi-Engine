#pragma once
// Scripted editor checks that register themselves (so adding one never touches Engine.cpp). In an area file
// (EditorTests/<area>.inl):
//
//   void InjectMyTest(bdg_engine* e, int frame, float scale) { ... }   // called every frame from frame 1
//   void ReportMyTest(bdg_engine* e) { bdg::LogInfo("my test: ok={}", ...); } // on the last frame (--frames N)
//   BDG_EDITOR_TEST("BDG_MY_TEST", InjectMyTest, ReportMyTest)
//
// The editor runs every registered check whose environment variable is set (BDG_MY_TEST=1 ... --frames N);
// CI greps the report line. Keep the check's state in function-local statics.
//
// BDG_EDITOR_SERVICE(tick) registers an always-on per-frame hook (no variable; runs in every editor session with a
// project open), e.g. the live link for `bdg mcp` (assets.inl).
#include <vector>

struct bdg_engine;

namespace bdg::edtest {
struct Test {
  const char* env; // nullptr: a service, always on
  void (*inject)(bdg_engine*, int frame, float scale);
  void (*report)(bdg_engine*); // may be nullptr
};
inline std::vector<Test>& All() {
  static std::vector<Test> tests;
  return tests;
}
struct Add {
  explicit Add(Test t) { All().push_back(t); }
};
} // namespace bdg::edtest

#define BDG_EDITOR_TEST(env, inject, report) static const bdg::edtest::Add bdg_edtest_##inject{bdg::edtest::Test{env, inject, report}};
#define BDG_EDITOR_SERVICE(tick) static const bdg::edtest::Add bdg_edtest_##tick{bdg::edtest::Test{nullptr, tick, nullptr}};
