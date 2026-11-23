#pragma once
// Reflection test types (queue 1-2). Tools/bht generates BTestComponent.gen.cpp from this header.
#include "Core/Reflection.h"
#include <string>
#include <vector>

namespace bdgtest {
BENUM()
enum class LightKind { Directional, Point, Spot = 5 };

// Six field kinds + one function: the exit criterion of queue 1-2.
BCLASS(Component)
class BTestComponent {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "0,1000", Tooltip = "Hit points", Category = "Stats")
  int32_t hp = 100;
  BPROPERTY(Edit, Animatable)
  float speed = 1.5f;
  BPROPERTY(Edit, Replicated)
  bool invincible = false;
  BPROPERTY(Edit)
  std::string title = "Knight";
  BPROPERTY(Edit, ShowIf = "invincible")
  bdg::Vec3 offset{0, 1, 0};
  BPROPERTY(Edit)
  LightKind kind = LightKind::Point;

  BFUNCTION(Callable)
  int32_t Heal(int32_t amount) { hp += amount; return hp; }

  int32_t notReflected = 7; // no marker: invisible to reflection
};

BSTRUCT()
struct BTestPoint {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) float x = 0;
  BPROPERTY(Edit) float y = 0;
};

// Remaining supported kinds: Color, double, Quat, handle, nested struct, arrays (of values and structs).
BCLASS()
class BTestExtras {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) bdg::Color tint{1, 1, 1, 1};
  BPROPERTY(Edit) double precise = 0.25;
  BPROPERTY(Edit) bdg::Quat rotation;
  BPROPERTY(Edit) bdg::ObjectHandle target;
  BPROPERTY(Edit) BTestPoint anchor;
  BPROPERTY(Edit) std::vector<int32_t> loot{3, 1, 4};
  BPROPERTY(Edit) std::vector<BTestPoint> path;
  BPROPERTY(Transient) int32_t cache = 0;

  BFUNCTION() float Length() const { return (float)path.size(); }
  BFUNCTION() void Reset(int32_t count, const std::string& reason) { loot.assign((size_t)count, 0); lastReason = reason; }
  std::string lastReason;
};

BCLASS(Component)
class BTestDerived : public BTestComponent {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) int32_t armor{12};
};
}
