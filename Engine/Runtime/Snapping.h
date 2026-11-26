#pragma once
// Snapping, alignment and measuring (queue 3-3b). Math only (no scene, no commands) so it is unit-testable; the
// scene view turns the results into "set Transform.position" commands inside one command group.
//
//  * Grid snap: moved objects land on the grid (world coordinates on the moved axes are multiples of the step).
//  * Vertex snap (V while moving): a vertex of the selection onto the vertex under the mouse.
//  * Surface snap (Ctrl+Shift while moving): the object rests on the surface under the mouse.
//  * Align / distribute: several objects line up their bounds (min, centre, max) or get equal gaps on an axis.
#include <glm/glm.hpp>
#include <vector>

namespace bdg {

struct SnapSettings {
  float move = 1.f;     // metres
  float rotate = 15.f;  // degrees
  float scale = 0.25f;  // factor step
  bool always = false;  // grid snap on: moves snap without Ctrl (Ctrl then turns it off for the drag)
  bool rotateOn = false, scaleOn = false; // the same for rotations and scaling (Scene view toolbar, Unreal-like)
  bool surface = false; // moves rest on the surface under the mouse without Ctrl+Shift
};
// The values the Scene view toolbar offers (Unreal's lists): grid (m), rotation (degrees), scale steps.
constexpr float kSnapSteps[] = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.f, 5.f, 10.f};
constexpr float kRotateSnapSteps[] = {5.f, 10.f, 15.f, 30.f, 45.f, 90.f};
constexpr float kScaleSnapSteps[] = {0.0625f, 0.125f, 0.25f, 0.5f, 1.f};

float SnapValue(float v, float step); // nearest multiple (step <= 0: unchanged)
// A move of `start` by `move` with the world axes in `axes` (1 = moved) put on the grid: the result's coordinates
// on those axes are multiples of `step`; the others keep the unsnapped move.
glm::vec3 SnapMoveToGrid(const glm::vec3& start, const glm::vec3& move, const glm::bvec3& axes, float step);

// Distance along the normal n from `pivot` to the far side of the box lo..hi in -n: put the pivot this far above a
// surface point (along its normal) and the box just touches the surface.
float RestOffset(const glm::vec3& pivot, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& n);

enum class AlignMode { Min, Center, Max, Distribute };
struct AlignBox { glm::vec3 lo{0}, hi{0}; };
// How far each box moves on `axis` (0 x, 1 y, 2 z). Min / Max / Center: to the selection's overall min, max or
// centre. Distribute: the outermost two stay, the others get equal gaps between them (by their order on the axis).
std::vector<float> AlignOffsets(const std::vector<AlignBox>& boxes, int axis, AlignMode mode);

struct Measure {
  glm::vec3 a{0}, b{0};
  float Length() const { return glm::distance(a, b); }
  glm::vec3 Delta() const { return glm::abs(b - a); }
};

} // namespace bdg
