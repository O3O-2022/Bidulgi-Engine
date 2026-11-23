#pragma once
// Minimal unit test macros (no dependency): TEST_CASE("name") { CHECK(x); CHECK_EQ(a, b); }
// Every TEST_CASE registers itself; Tests/TestMain.cpp runs them and returns 1 on any failure.
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace bdgtest {
struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& Cases() { static std::vector<Case> c; return c; }
inline int& Failures() { static int n = 0; return n; }
inline int& Checks() { static int n = 0; return n; }
struct Register { Register(const char* name, void (*fn)()) { Cases().push_back({name, fn}); } };

inline bool Check(bool ok, const char* expr, const char* file, int line) {
  ++Checks();
  if (!ok) { ++Failures(); std::printf("  FAIL %s(%d): %s\n", file, line, expr); }
  return ok;
}

template <class T> std::string Show(const T& v) {
  std::ostringstream s;
  s << v;
  return s.str();
}
inline std::string Show(const std::string& v) { return "\"" + v + "\""; }
inline std::string Show(const char* v) { return Show(std::string(v)); }

template <class A, class B> bool CheckEq(const A& a, const B& b, const char* expr, const char* file, int line) {
  ++Checks();
  if (a == b) return true;
  ++Failures();
  std::printf("  FAIL %s(%d): %s\n       got %s, expected %s\n", file, line, expr, Show(a).c_str(), Show(b).c_str());
  return false;
}
}

#define BDG_TEST_CAT2(a, b) a##b
#define BDG_TEST_CAT(a, b) BDG_TEST_CAT2(a, b)
#define TEST_CASE(name)                                                                                   \
  static void BDG_TEST_CAT(bdg_test_fn_, __LINE__)();                                                     \
  static ::bdgtest::Register BDG_TEST_CAT(bdg_test_reg_, __LINE__)(name, &BDG_TEST_CAT(bdg_test_fn_, __LINE__)); \
  static void BDG_TEST_CAT(bdg_test_fn_, __LINE__)()
#define CHECK(expr) ::bdgtest::Check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::bdgtest::CheckEq((a), (b), #a " == " #b, __FILE__, __LINE__)
