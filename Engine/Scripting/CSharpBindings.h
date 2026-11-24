#pragma once
// C# bindings from reflection (queue 4-1): every reflected component becomes a C# class with a property per
// [Edit] / [ReadOnly] field and a method per Callable / Pure / Button function, every enum a C# enum. The engine
// side stays the single source: `bdg csharp-bindings <file>` writes the file and the build compiles it into
// Bidulgi.Scripting.dll, so a new BPROPERTY or BFUNCTION shows up in C# on the next build.
//
//   C++  BPROPERTY(Edit) int32_t maxHealth;          C#  public int MaxHealth { get; set; }
//        BFUNCTION(Callable) int32_t Damage(int32_t)     public int Damage(int amount)
//        BFUNCTION(Pure) float Ratio() const             public float Ratio()
#include <string>

namespace bdg {
std::string GenerateCSharpBindings(); // the whole Bindings.g.cs text (deterministic: sorted by name)
}
