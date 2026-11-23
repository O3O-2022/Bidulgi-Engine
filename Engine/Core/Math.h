#pragma once
// Minimal value types shared by reflection, UI and rendering. A full math library (GLM) comes with
// queue 2-0 (coordinate system decision); these stay layout-compatible (plain floats).
namespace bdg {
struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Color { float r, g, b, a; }; // linear 0..1 (aggregate: Color{r, g, b, a})

inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
inline bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator==(const Vec4& a, const Vec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
inline bool operator==(const Quat& a, const Quat& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
inline bool operator==(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
}
