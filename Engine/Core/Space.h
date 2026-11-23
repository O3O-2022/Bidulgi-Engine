#pragma once
// Coordinate system and units (queue 2-0, decided 2026-11-23; see Docs/planning/decisions.md).
//
//   Right-handed, Y up:  +X right, +Y up, +Z toward the viewer.
//   Forward is -Z: a camera or character with no rotation looks along -Z (glTF, OpenGL, Godot, Maya).
//   1 unit = 1 metre. Mass in kg, time in seconds.
//   Angles: degrees in the editor, files and commands (Transform.rotation); radians inside math code.
//   Positive rotation follows the right-hand rule: counter-clockwise when looking from +axis toward the origin.
//     yaw   +90 (around Y) turns forward (-Z) to the left (-X)
//     pitch +90 (around X) turns forward (-Z) up (+Y)
//   Euler order: R = Ry(yaw) * Rx(pitch) * Rz(roll) — roll first, then pitch, then yaw around world Y
//   (the same order Unity uses, so imported values mean the same turn).
//   Front faces wind counter-clockwise (OpenGL default).
//
// The engine's value types (Vec3, Quat in Core/Math.h) stay plain structs for reflection and files; GLM does
// the math. GLM types never appear in the C API (bdg_api.h).
#include "Core/Math.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace bdg::space {
inline constexpr Vec3 kRight{1, 0, 0};
inline constexpr Vec3 kUp{0, 1, 0};
inline constexpr Vec3 kForward{0, 0, -1};

inline glm::vec3 ToGlm(const Vec3& v) { return {v.x, v.y, v.z}; }
inline Vec3 FromGlm(const glm::vec3& v) { return {v.x, v.y, v.z}; }
inline glm::quat ToGlm(const Quat& q) { return glm::quat(q.w, q.x, q.y, q.z); }
inline Quat FromGlm(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }

glm::quat EulerDegreesToQuat(const Vec3& degrees);  // (pitch x, yaw y, roll z) in the order above
Vec3 QuatToEulerDegrees(const glm::quat& q);        // inverse; pitch in [-90, 90], at ±90 roll is 0
glm::mat4 TRS(const Vec3& position, const Vec3& eulerDegrees, const Vec3& scale); // local -> parent
Vec3 TransformPoint(const glm::mat4& m, const Vec3& p);
Vec3 TransformDirection(const glm::mat4& m, const Vec3& d); // ignores translation
}
