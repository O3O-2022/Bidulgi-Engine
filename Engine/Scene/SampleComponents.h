#pragma once
// Sample gameplay components for the editor's sample scene. PlayerMove and QuestLogic stand in for the
// C# and Lua script components until those hosts exist (queue 4-1, 5-3); the tag names the language badge.
#include "Core/Reflection.h"
#include <string>

namespace bdg {
BCLASS(Component, CSharp)
struct PlayerMove {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "0,50", Header = "Movement") float speed = 5.f;
  BPROPERTY(Edit, Range = "0,20") float jumpHeight = 4.5f;
};

BCLASS(Component, Cpp)
struct Health {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,10000") int32_t maxHealth = 100;
  BPROPERTY(Edit, Replicated) int32_t current = 100;

  BFUNCTION(Button) void Revive() { current = maxHealth; } // Inspector button
  // Graph nodes (E-8): a callable with a parameter and a result, and a pure value
  BFUNCTION(Callable) int32_t Damage(int32_t amount) { current = current > amount ? current - amount : 0; return current; }
  BFUNCTION(Pure) float Ratio() const { return maxHealth > 0 ? (float)current / (float)maxHealth : 0.f; }
};

BCLASS(Component, Lua)
struct QuestLogic {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) std::string questId = "q_intro";
};
}
