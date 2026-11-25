#pragma once
// The Terrain component (round 6): draws a .terrain asset (Terrain/TerrainData.h) at its object, with its foliage,
// and makes a static height field collider for it while playing. The object's position is the terrain's
// (min x, min z) corner. The brush fields set what the Scene view's Terrain tool does (T).
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BENUM()
enum class TerrainBrush { Raise, Lower, Smooth, Flatten, Paint, PaintFoliage, EraseFoliage };

BCLASS(Component)
struct Terrain {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "The .terrain asset (Assets-relative path)") std::string asset;
  BPROPERTY(Edit, Tooltip = "A static height field collider while playing (characters stand on it)") bool collider = true;
  BPROPERTY(Edit, Range = "5,500", Tooltip = "Chunks nearer than this use full detail; each doubling of the distance halves it (times the tier)") float lodDistance = 40.f;
  BPROPERTY(Edit, Tooltip = "Grass and trees from the asset's foliage layers") bool foliage = true;
  BPROPERTY(Edit, Range = "0,3", Tooltip = "Wind sway strength of the foliage") float wind = 1.f;
  BPROPERTY(Edit, Header = "Brush (Terrain tool, T)", Tooltip = "What a drag on the terrain does") TerrainBrush brush = TerrainBrush::Raise;
  BPROPERTY(Edit, Range = "0.5,100", Tooltip = "Brush radius (metres)") float brushRadius = 6.f;
  BPROPERTY(Edit, Range = "0.01,1", Tooltip = "How much one second of dragging changes") float brushStrength = 0.5f;
  BPROPERTY(Edit, Range = "0,7", Tooltip = "Material layer to paint / foliage layer to paint or erase") int32_t brushLayer = 0;
};

} // namespace bdg
