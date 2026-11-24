#pragma once
// Transform gizmos (queue 3-3): move / rotate / scale handles drawn over the scene view, hit-tested and dragged
// in screen space. This file is math only (no scene, no commands) so the drag rules are unit-testable; the scene
// view turns a GizmoDelta into "set Transform.*" commands inside one command group per drag.
//
// Handles: Move = X, Y, Z arrows, XY / YZ / XZ plane squares, centre (screen plane). Rotate = X, Y, Z rings and
// the outer view ring. Scale = X, Y, Z axes with boxes, centre (uniform). The gizmo keeps a constant size on
// screen. Snapping (Ctrl): move 1 m, rotate 15 degrees, scale 0.1.
#include "Render3D/Renderer3D.h"
#include <glm/gtc/quaternion.hpp>

namespace bdg {
class Batch2D;
class Theme;

enum class GizmoMode { Move, Rotate, Scale };
enum class GizmoHandle { None, X, Y, Z, XY, YZ, XZ, Center, View };

struct GizmoView { // the camera and where its picture sits on screen (UI units)
  Camera3D camera;
  float x = 0, y = 0, w = 1, h = 1;
  bool Project(const glm::vec3& p, glm::vec2& out) const;           // false: behind the camera
  void Ray(const glm::vec2& screen, glm::vec3& origin, glm::vec3& dir) const;
};

struct GizmoFrame {
  glm::vec3 origin{0};
  glm::mat3 axes{1.f}; // columns: X, Y, Z (unit; world axes or the object's own)
  float size = 1;      // world length of an axis
};

struct GizmoDelta {
  glm::vec3 move{0};             // world units
  glm::quat rotate{1, 0, 0, 0};  // world space, about the frame origin
  float angle = 0;               // degrees (rotate)
  glm::vec3 scale{1};            // factors along the frame axes
};

constexpr float kGizmoPixels = 110.f; // axis length on screen
GizmoFrame MakeGizmoFrame(const GizmoView& v, const glm::vec3& origin, const glm::mat3& axes);
GizmoHandle GizmoHit(GizmoMode mode, const GizmoFrame& f, const GizmoView& v, const glm::vec2& mouse);
// A good point to grab a handle (tests, hints): along an arrow, on a ring, in a plane square.
bool GizmoHandlePoint(GizmoMode mode, GizmoHandle h, const GizmoFrame& f, const GizmoView& v, glm::vec2& out);

class GizmoDrag {
public:
  bool Begin(GizmoMode mode, GizmoHandle handle, const GizmoFrame& frame, const GizmoView& view, const glm::vec2& mouse);
  GizmoDelta Update(const glm::vec2& mouse, bool snap) const;
  GizmoMode Mode() const { return mode_; }
  GizmoHandle Handle() const { return handle_; }
  const GizmoFrame& Frame() const { return frame_; }

private:
  GizmoMode mode_ = GizmoMode::Move;
  GizmoHandle handle_ = GizmoHandle::None;
  GizmoFrame frame_;
  GizmoView view_;
  glm::vec2 start_{0};
  glm::vec3 axis_{0}, planeNormal_{0}, startHit_{0};
  float startParam_ = 0, startAngle_ = 0;
};

// Draws the handles; `hot` is highlighted (under the mouse or being dragged).
void DrawGizmo(Batch2D& b, const Theme& t, GizmoMode mode, const GizmoFrame& f, const GizmoView& v, GizmoHandle hot);
}
