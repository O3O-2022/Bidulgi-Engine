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
  BPROPERTY(Edit, Range = "0,50") float speed = 5.f;
  BPROPERTY(Edit, Range = "0,20") float jumpHeight = 4.5f;
};

BCLASS(Component, Cpp)
struct Health {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,10000") int32_t maxHealth = 100;
  BPROPERTY(Edit, Replicated) int32_t current = 100;
};

BCLASS(Component, Lua)
struct QuestLogic {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) std::string questId = "q_intro";
};
}
