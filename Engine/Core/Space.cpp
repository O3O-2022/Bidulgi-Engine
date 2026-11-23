#include "Core/Space.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg::space {
glm::quat EulerDegreesToQuat(const Vec3& d) {
  const glm::vec3 r = glm::radians(glm::vec3(d.x, d.y, d.z));
  const glm::quat qx = glm::angleAxis(r.x, glm::vec3(1, 0, 0));
  const glm::quat qy = glm::angleAxis(r.y, glm::vec3(0, 1, 0));
  const glm::quat qz = glm::angleAxis(r.z, glm::vec3(0, 0, 1));
  return glm::normalize(qy * qx * qz); // Ry * Rx * Rz
}

Vec3 QuatToEulerDegrees(const glm::quat& q) {
  // R = Ry Rx Rz  =>  r12 = -sin(pitch), r02 = sin(yaw)cos(pitch), r22 = cos(yaw)cos(pitch),
  //                   r10 = cos(pitch)sin(roll), r11 = cos(pitch)cos(roll)   (r_ij: row i, column j)
  const glm::mat3 m = glm::mat3_cast(glm::normalize(q));
  auto r = [&](int row, int col) { return m[col][row]; }; // GLM is column-major
  float s = glm::clamp(-r(1, 2), -1.f, 1.f);
  float pitch = std::asin(s), yaw, roll;
  if (std::fabs(s) < 0.99999f) {
    yaw = std::atan2(r(0, 2), r(2, 2));
    roll = std::atan2(r(1, 0), r(1, 1));
  } else { // looking straight up or down: yaw and roll turn around the same axis; keep it all in yaw
    yaw = std::atan2(-r(2, 0), r(0, 0));
    roll = 0;
  }
  glm::vec3 deg = glm::degrees(glm::vec3(pitch, yaw, roll));
  return {deg.x, deg.y, deg.z};
}

glm::mat4 TRS(const Vec3& position, const Vec3& eulerDegrees, const Vec3& scale) {
  glm::mat4 m = glm::translate(glm::mat4(1), ToGlm(position));
  m *= glm::mat4_cast(EulerDegreesToQuat(eulerDegrees));
  return glm::scale(m, ToGlm(scale));
}

Vec3 TransformPoint(const glm::mat4& m, const Vec3& p) { return FromGlm(glm::vec3(m * glm::vec4(ToGlm(p), 1))); }
Vec3 TransformDirection(const glm::mat4& m, const Vec3& d) { return FromGlm(glm::vec3(m * glm::vec4(ToGlm(d), 0))); }
}
