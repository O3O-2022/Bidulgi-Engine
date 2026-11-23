// Coordinate system tests (queue 2-0): these pin the conventions in Engine/Core/Space.h.
#include "Core/Space.h"
#include "bdg_test.h"
#include <cmath>
#include <random>

using namespace bdg;
using namespace bdg::space;

namespace {
bool Near(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
  return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}
Vec3 Rotate(const Vec3& euler, const Vec3& v) { return FromGlm(EulerDegreesToQuat(euler) * ToGlm(v)); }
}

TEST_CASE("space: right-handed, Y up, forward -Z") {
  glm::vec3 x = ToGlm(kRight), y = ToGlm(kUp);
  CHECK(Near(FromGlm(glm::cross(x, y)), {0, 0, 1}));   // X x Y = +Z: right-handed
  CHECK(Near(FromGlm(-glm::cross(x, y)), kForward));  // forward points away from the viewer
}

TEST_CASE("space: positive angles follow the right-hand rule") {
  CHECK(Near(Rotate({0, 90, 0}, kForward), {-1, 0, 0})); // yaw +90: turn left
  CHECK(Near(Rotate({0, 90, 0}, kRight), {0, 0, -1}));
  CHECK(Near(Rotate({90, 0, 0}, kForward), {0, 1, 0}));  // pitch +90: look up
  CHECK(Near(Rotate({0, 0, 90}, kRight), {0, 1, 0}));    // roll +90: right side goes up
  // Order: roll, then pitch, then yaw (around world Y): yaw 90 + pitch 90 still looks straight up
  CHECK(Near(Rotate({90, 90, 0}, kForward), {0, 1, 0}));
  CHECK(Near(Rotate({30, 90, 0}, kForward), Rotate({0, 90, 0}, Rotate({30, 0, 0}, kForward))));
}

TEST_CASE("space: TRS puts scale, then rotation, then translation") {
  glm::mat4 m = TRS({1, 2, 3}, {0, 90, 0}, {2, 2, 2});
  CHECK(Near(TransformPoint(m, {1, 0, 0}), {1, 2, 1}));    // (2,0,0) -> (0,0,-2) -> +(1,2,3)
  CHECK(Near(TransformDirection(m, {0, 1, 0}), {0, 2, 0})); // directions ignore translation
  CHECK(Near(TransformPoint(TRS({}, {}, {1, 1, 1}), {4, 5, 6}), {4, 5, 6}));
}

TEST_CASE("space: Euler angles round-trip") {
  std::mt19937 rng(3);
  std::uniform_real_distribution<float> any(-180, 180), pitch(-89, 89);
  int bad = 0;
  for (int i = 0; i < 2000; ++i) {
    Vec3 e{pitch(rng), any(rng), any(rng)};
    Vec3 back = QuatToEulerDegrees(EulerDegreesToQuat(e));
    if (!Near(back, e, 1e-2f)) ++bad;
  }
  CHECK_EQ(bad, 0);
  for (Vec3 e : {Vec3{90, 30, 0}, Vec3{-90, -45, 20}, Vec3{90, 0, 0}}) { // straight up / down: same rotation back
    Vec3 back = QuatToEulerDegrees(EulerDegreesToQuat(e));
    CHECK(Near(Rotate(back, kForward), Rotate(e, kForward), 1e-3f));
    CHECK(Near(Rotate(back, kRight), Rotate(e, kRight), 1e-3f));
    CHECK(std::fabs(std::fabs(back.x) - 90) < 1e-2f);
  }
}
