#include "Runtime/Gizmo.h"
#include "Render2D/Batch2D.h"
#include "UI/Theme.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace bdg {
namespace {
constexpr int kRingSegments = 64;
const float kPi = 3.14159265f;

int AxisIndex(GizmoHandle h) { return h == GizmoHandle::X ? 0 : h == GizmoHandle::Y ? 1 : h == GizmoHandle::Z ? 2 : -1; }
// Plane handles: normal axis and the two axes spanning the square.
int PlaneNormal(GizmoHandle h) { return h == GizmoHandle::YZ ? 0 : h == GizmoHandle::XZ ? 1 : h == GizmoHandle::XY ? 2 : -1; }

float SegDist(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
  glm::vec2 ab = b - a;
  float t = glm::dot(ab, ab) > 0 ? std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.f, 1.f) : 0.f;
  return glm::length(p - (a + ab * t));
}

float Cross2(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

bool InQuad(const glm::vec2& p, const glm::vec2 q[4]) {
  float s = 0;
  for (int i = 0; i < 4; ++i) {
    float c = Cross2(q[(i + 1) % 4] - q[i], p - q[i]);
    if (c != 0) {
      if (s != 0 && (c > 0) != (s > 0)) return false;
      s = c;
    }
  }
  return true;
}

float QuadArea(const glm::vec2 q[4]) { return std::fabs(Cross2(q[2] - q[0], q[3] - q[1])) * 0.5f; }

glm::vec3 RingPoint(const GizmoFrame& f, int axis, float theta, float radiusScale = 1.f) {
  glm::vec3 u = f.axes[(axis + 1) % 3], w = f.axes[(axis + 2) % 3];
  return f.origin + (u * std::cos(theta) + w * std::sin(theta)) * f.size * radiusScale;
}

void PlaneCorners(const GizmoFrame& f, int normal, glm::vec3 out[4]) {
  glm::vec3 u = f.axes[(normal + 1) % 3], w = f.axes[(normal + 2) % 3];
  const float a = 0.25f * f.size, b = 0.5f * f.size;
  out[0] = f.origin + u * a + w * a;
  out[1] = f.origin + u * b + w * a;
  out[2] = f.origin + u * b + w * b;
  out[3] = f.origin + u * a + w * b;
}

glm::vec3 CameraForward(const Camera3D& c) { return glm::normalize(c.target - c.position); }

float ViewRingPixels(const GizmoFrame& f, const GizmoView& v) {
  glm::vec3 right = glm::normalize(glm::cross(CameraForward(v.camera), glm::vec3(0, 1, 0)));
  if (!std::isfinite(right.x)) right = {1, 0, 0};
  glm::vec2 o, r;
  if (!v.Project(f.origin, o) || !v.Project(f.origin + right * f.size * 1.2f, r)) return 0;
  return glm::length(r - o);
}

// Screen angle of the mouse around the projected origin, counter-clockwise in degrees (screen y points down).
float ScreenAngle(const GizmoView& v, const glm::vec3& origin, const glm::vec2& m) {
  glm::vec2 o;
  if (!v.Project(origin, o)) return 0;
  return glm::degrees(std::atan2(-(m.y - o.y), m.x - o.x));
}

bool RayPlane(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& p, const glm::vec3& n, glm::vec3& hit) {
  float den = glm::dot(rd, n);
  if (std::fabs(den) < 1e-4f) return false;
  float t = glm::dot(p - ro, n) / den;
  if (t < 0) return false;
  hit = ro + rd * t;
  return true;
}

// Parameter s of the point on the line origin + s * axis closest to the ray (axis and dir unit length).
bool ClosestOnAxis(const glm::vec3& origin, const glm::vec3& axis, const glm::vec3& ro, const glm::vec3& rd, float& s) {
  glm::vec3 w0 = origin - ro;
  float b = glm::dot(axis, rd), den = 1.f - b * b;
  if (den < 1e-4f) return false; // looking straight down the axis
  s = (b * glm::dot(rd, w0) - glm::dot(axis, w0)) / den;
  return true;
}

float Snap(float v, float step) { return std::round(v / step) * step; }
}

bool GizmoView::Project(const glm::vec3& p, glm::vec2& out) const {
  glm::vec4 c = camera.Projection(w / std::max(h, 1e-3f)) * camera.View() * glm::vec4(p, 1.f);
  if (c.w <= 1e-6f) return false;
  out = {x + (c.x / c.w * 0.5f + 0.5f) * w, y + (0.5f - c.y / c.w * 0.5f) * h};
  return true;
}

void GizmoView::Ray(const glm::vec2& s, glm::vec3& origin, glm::vec3& dir) const {
  glm::mat4 inv = glm::inverse(camera.Projection(w / std::max(h, 1e-3f)) * camera.View());
  float nx = (s.x - x) / w * 2.f - 1.f, ny = 1.f - (s.y - y) / h * 2.f;
  glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
  origin = glm::vec3(a) / a.w;
  dir = glm::normalize(glm::vec3(b) / b.w - origin);
}

GizmoFrame MakeGizmoFrame(const GizmoView& v, const glm::vec3& origin, const glm::mat3& axes) {
  GizmoFrame f;
  f.origin = origin;
  for (int i = 0; i < 3; ++i) f.axes[i] = glm::normalize(axes[i]);
  float depth = std::max(glm::dot(origin - v.camera.position, CameraForward(v.camera)), 1e-3f);
  float worldPerPixel = 2.f * depth * std::tan(glm::radians(v.camera.fovY * 0.5f)) / std::max(v.h, 1.f);
  f.size = kGizmoPixels * worldPerPixel;
  return f;
}

GizmoHandle GizmoHit(GizmoMode mode, const GizmoFrame& f, const GizmoView& v, const glm::vec2& m) {
  glm::vec2 o;
  if (!v.Project(f.origin, o)) return GizmoHandle::None;
  const GizmoHandle axisHandles[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
  GizmoHandle best = GizmoHandle::None;
  float bestD = 7.f;
  if (mode == GizmoMode::Rotate) {
    for (int i = 0; i < 3; ++i) {
      glm::vec2 prev;
      bool havePrev = v.Project(RingPoint(f, i, 0), prev);
      for (int k = 1; k <= kRingSegments; ++k) {
        glm::vec2 cur;
        bool haveCur = v.Project(RingPoint(f, i, 2 * kPi * k / kRingSegments), cur);
        if (havePrev && haveCur) {
          float d = SegDist(m, prev, cur);
          if (d < bestD) { bestD = d; best = axisHandles[i]; }
        }
        prev = cur;
        havePrev = haveCur;
      }
    }
    if (best != GizmoHandle::None) return best;
    float r = ViewRingPixels(f, v);
    return r > 0 && std::fabs(glm::length(m - o) - r) < 7.f ? GizmoHandle::View : GizmoHandle::None;
  }
  if (glm::length(m - o) < (mode == GizmoMode::Scale ? 10.f : 9.f)) return GizmoHandle::Center;
  for (int i = 0; i < 3; ++i) {
    glm::vec2 tip;
    if (!v.Project(f.origin + f.axes[i] * f.size, tip) || glm::length(tip - o) < 8.f) continue; // axis points at us
    float d = SegDist(m, o + (tip - o) * 0.2f, tip + glm::normalize(tip - o) * 4.f);
    if (d < bestD) { bestD = d; best = axisHandles[i]; }
  }
  if (best != GizmoHandle::None || mode != GizmoMode::Move) return best;
  const GizmoHandle planes[3] = {GizmoHandle::YZ, GizmoHandle::XZ, GizmoHandle::XY};
  for (int n = 0; n < 3; ++n) {
    glm::vec3 c[4];
    PlaneCorners(f, n, c);
    glm::vec2 q[4];
    bool ok = true;
    for (int k = 0; k < 4; ++k) ok = ok && v.Project(c[k], q[k]);
    if (ok && QuadArea(q) > 30.f && InQuad(m, q)) return planes[n];
  }
  return GizmoHandle::None;
}

bool GizmoHandlePoint(GizmoMode mode, GizmoHandle h, const GizmoFrame& f, const GizmoView& v, glm::vec2& out) {
  glm::vec2 o;
  if (!v.Project(f.origin, o)) return false;
  if (h == GizmoHandle::Center) { out = o; return true; }
  int a = AxisIndex(h);
  if (mode == GizmoMode::Rotate && h == GizmoHandle::View) {
    out = o + glm::vec2(ViewRingPixels(f, v), 0);
    return true;
  }
  if (mode == GizmoMode::Rotate && a >= 0) { // front-facing ring point, as far as possible from the other rings
    glm::vec3 toCam = glm::normalize(v.camera.position - f.origin);
    float bestScore = -1e9f;
    for (int k = 0; k < kRingSegments; ++k) {
      glm::vec3 p = RingPoint(f, a, 2 * kPi * k / kRingSegments);
      glm::vec2 s;
      if (!v.Project(p, s) || glm::dot(p - f.origin, toCam) < 0) continue;
      float sep = 1e9f;
      for (int other = 0; other < 3; ++other) {
        if (other == a) continue;
        for (int j = 0; j < kRingSegments; ++j) {
          glm::vec2 q;
          if (v.Project(RingPoint(f, other, 2 * kPi * j / kRingSegments), q)) sep = std::min(sep, glm::length(q - s));
        }
      }
      if (sep > bestScore) { bestScore = sep; out = s; }
    }
    return bestScore > 0;
  }
  if (a >= 0) {
    glm::vec2 tip;
    if (!v.Project(f.origin + f.axes[a] * f.size, tip)) return false;
    out = o + (tip - o) * (mode == GizmoMode::Scale ? 0.8f : 0.7f);
    return true;
  }
  int n = PlaneNormal(h);
  if (n < 0) return false;
  glm::vec3 c[4];
  PlaneCorners(f, n, c);
  return v.Project((c[0] + c[2]) * 0.5f, out);
}

bool GizmoDrag::Begin(GizmoMode mode, GizmoHandle handle, const GizmoFrame& frame, const GizmoView& view, const glm::vec2& mouse) {
  mode_ = mode;
  handle_ = handle;
  frame_ = frame;
  view_ = view;
  start_ = mouse;
  glm::vec3 ro, rd;
  view.Ray(mouse, ro, rd);
  int a = AxisIndex(handle);
  if (mode == GizmoMode::Move) {
    if (a >= 0) {
      axis_ = frame.axes[a];
      return ClosestOnAxis(frame.origin, axis_, ro, rd, startParam_);
    }
    int n = PlaneNormal(handle);
    planeNormal_ = n >= 0 ? frame.axes[n] : CameraForward(view.camera);
    return RayPlane(ro, rd, frame.origin, planeNormal_, startHit_);
  }
  if (mode == GizmoMode::Rotate) {
    axis_ = a >= 0 ? frame.axes[a] : -CameraForward(view.camera);
    startAngle_ = ScreenAngle(view, frame.origin, mouse);
    return a >= 0 || handle == GizmoHandle::View;
  }
  axis_ = a >= 0 ? frame.axes[a] : glm::vec3(0);
  return a >= 0 || handle == GizmoHandle::Center;
}

GizmoDelta GizmoDrag::Update(const glm::vec2& mouse, bool snap, const SnapSettings& steps) const {
  GizmoDelta d;
  glm::vec3 ro, rd;
  view_.Ray(mouse, ro, rd);
  int a = AxisIndex(handle_);
  if (mode_ == GizmoMode::Move) {
    if (a >= 0) {
      float s;
      if (!ClosestOnAxis(frame_.origin, axis_, ro, rd, s)) return d;
      float t = s - startParam_;
      int world = -1; // a world axis: land on the grid there
      for (int i = 0; i < 3; ++i)
        if (std::abs(std::abs(axis_[i]) - 1.f) < 1e-5f) world = i;
      if (snap && world >= 0) d.move = SnapMoveToGrid(frame_.origin, axis_ * t, glm::bvec3(world == 0, world == 1, world == 2), steps.move);
      else d.move = axis_ * (snap ? Snap(t, steps.move) : t);
    } else {
      glm::vec3 hit;
      if (!RayPlane(ro, rd, frame_.origin, planeNormal_, hit)) return d;
      glm::vec3 move = hit - startHit_;
      const bool worldAxes = handle_ == GizmoHandle::Center || frame_.axes == glm::mat3(1.f);
      if (snap && worldAxes) { // land on the grid on the axes this handle moves along
        glm::bvec3 moved = handle_ == GizmoHandle::XY ? glm::bvec3(true, true, false) : handle_ == GizmoHandle::YZ ? glm::bvec3(false, true, true)
                         : handle_ == GizmoHandle::XZ ? glm::bvec3(true, false, true) : glm::greaterThan(glm::abs(move), glm::vec3(1e-4f));
        move = SnapMoveToGrid(frame_.origin, move, moved, steps.move);
      } else if (snap) { // round along the frame axes
        glm::vec3 local = glm::transpose(frame_.axes) * move;
        move = frame_.axes * glm::vec3(Snap(local.x, steps.move), Snap(local.y, steps.move), Snap(local.z, steps.move));
      }
      d.move = move;
    }
    return d;
  }
  if (mode_ == GizmoMode::Rotate) {
    float delta = ScreenAngle(view_, frame_.origin, mouse) - startAngle_;
    while (delta > 180.f) delta -= 360.f;
    while (delta <= -180.f) delta += 360.f;
    // Counter-clockwise on screen is a positive turn when the axis points toward the viewer (right-hand rule).
    if (glm::dot(axis_, view_.camera.position - frame_.origin) < 0) delta = -delta;
    if (snap) delta = Snap(delta, steps.rotate);
    d.angle = delta;
    d.rotate = glm::angleAxis(glm::radians(delta), glm::normalize(axis_));
    return d;
  }
  glm::vec2 o;
  if (!view_.Project(frame_.origin, o)) return d;
  float factor = 1.f;
  if (a >= 0) {
    glm::vec2 tip;
    if (!view_.Project(frame_.origin + frame_.axes[a] * frame_.size, tip) || glm::length(tip - o) < 1.f) return d;
    glm::vec2 u = glm::normalize(tip - o);
    factor = 1.f + glm::dot(mouse - start_, u) / glm::length(tip - o);
  } else {
    factor = 1.f + ((mouse.x - start_.x) - (mouse.y - start_.y)) / kGizmoPixels;
  }
  if (snap) factor = Snap(factor, steps.scale);
  factor = std::max(factor, 0.01f);
  if (a >= 0) d.scale[a] = factor;
  else d.scale = glm::vec3(factor);
  return d;
}

void DrawGizmo(Batch2D& b, const Theme& t, GizmoMode mode, const GizmoFrame& f, const GizmoView& v, GizmoHandle hot) {
  glm::vec2 o;
  if (!v.Project(f.origin, o)) return;
  const Color hotColor = t.C("gizmo-hot"); // the handle under the mouse
  const Color base[3] = {t.C("axis-x"), t.C("axis-y"), t.C("axis-z")};
  const GizmoHandle axisHandles[3] = {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
  auto colorOf = [&](int i) { return hot == axisHandles[i] ? hotColor : base[i]; };
  if (mode == GizmoMode::Rotate) {
    glm::vec3 toCam = glm::normalize(v.camera.position - f.origin);
    float r = ViewRingPixels(f, v);
    Color viewColor = hot == GizmoHandle::View ? hotColor : Color{t.C("fg").r, t.C("fg").g, t.C("fg").b, 0.8f};
    for (int k = 0; k < kRingSegments; ++k) { // outer view ring
      float a0 = 2 * kPi * k / kRingSegments, a1 = 2 * kPi * (k + 1) / kRingSegments;
      b.Line(o.x + std::cos(a0) * r, o.y + std::sin(a0) * r, o.x + std::cos(a1) * r, o.y + std::sin(a1) * r, 1.5f, viewColor);
    }
    for (int i = 0; i < 3; ++i) {
      Color c = colorOf(i);
      for (int k = 0; k < kRingSegments; ++k) {
        glm::vec3 p0 = RingPoint(f, i, 2 * kPi * k / kRingSegments), p1 = RingPoint(f, i, 2 * kPi * (k + 1) / kRingSegments);
        glm::vec2 s0, s1;
        if (!v.Project(p0, s0) || !v.Project(p1, s1)) continue;
        bool front = glm::dot((p0 + p1) * 0.5f - f.origin, toCam) >= -1e-4f * f.size;
        b.Line(s0.x, s0.y, s1.x, s1.y, front ? 2.6f : 1.2f, front ? c : Color{c.r, c.g, c.b, 0.3f});
      }
    }
    return;
  }
  if (mode == GizmoMode::Move) { // plane squares under the arrows
    const GizmoHandle planes[3] = {GizmoHandle::YZ, GizmoHandle::XZ, GizmoHandle::XY};
    for (int n = 0; n < 3; ++n) {
      glm::vec3 c[4];
      PlaneCorners(f, n, c);
      glm::vec2 q[4];
      bool ok = true;
      for (int k = 0; k < 4; ++k) ok = ok && v.Project(c[k], q[k]);
      if (!ok || QuadArea(q) <= 30.f) continue;
      Color col = hot == planes[n] ? hotColor : base[n];
      float pts[4][2] = {{q[0].x, q[0].y}, {q[1].x, q[1].y}, {q[2].x, q[2].y}, {q[3].x, q[3].y}};
      b.Quad(pts, {col.r, col.g, col.b, hot == planes[n] ? 0.55f : 0.3f});
      for (int k = 0; k < 4; ++k) b.Line(q[k].x, q[k].y, q[(k + 1) % 4].x, q[(k + 1) % 4].y, 1.2f, {col.r, col.g, col.b, 0.85f});
    }
  }
  for (int i = 0; i < 3; ++i) {
    glm::vec2 tip;
    if (!v.Project(f.origin + f.axes[i] * f.size, tip) || glm::length(tip - o) < 8.f) continue;
    glm::vec2 dir = glm::normalize(tip - o);
    Color c = colorOf(i);
    if (mode == GizmoMode::Move) {
      glm::vec2 headBase = tip - dir * 16.f;
      b.Line(o.x + dir.x * 10, o.y + dir.y * 10, headBase.x, headBase.y, 2.6f, c);
      for (int k = 0; k < 4; ++k) { // tapered head (a cone seen from the side)
        glm::vec2 a0 = headBase + dir * (4.f * k), a1 = headBase + dir * (4.f * (k + 1));
        b.Line(a0.x, a0.y, a1.x, a1.y, 11.f - 2.6f * k, c);
      }
    } else {
      b.Line(o.x + dir.x * 10, o.y + dir.y * 10, tip.x, tip.y, 2.6f, c);
      b.Rect(tip.x - 5, tip.y - 5, 10, 10, c, 1.5f);
    }
  }
  Color centre = hot == GizmoHandle::Center ? hotColor : Color{t.C("fg").r, t.C("fg").g, t.C("fg").b, 0.9f};
  if (mode == GizmoMode::Scale) b.Rect(o.x - 6, o.y - 6, 12, 12, centre, 2.f);
  else b.Rect(o.x - 5, o.y - 5, 10, 10, centre, 5.f, 1.5f, {0.1f, 0.1f, 0.12f, 0.9f});
}
}
