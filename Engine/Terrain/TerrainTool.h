#pragma once
// The Scene view's Terrain tool (T, round 6): drag on a terrain to sculpt or paint with the brush set on its Terrain
// component (Raise, Lower, Smooth, Flatten, Paint layer, Paint / Erase foliage; radius, strength, layer). The stroke
// changes the terrain live; when the mouse is let go it becomes one "terrain" command per changed map (one undo
// step), and the asset's files are written.
#include "Platform/Window.h"
#include "Render3D/Renderer3D.h"
#include "Terrain/TerrainData.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {
class EditorModel;
class Batch2D;
struct Widget;

class TerrainTool {
public:
  // Takes a Scene view mouse event while the tool is active (true: used). cam / aspect: the view's camera.
  bool Handle(const InputEvent& e, const Widget& view, const Camera3D& cam, EditorModel& model);
  // The brush ring where the mouse is (UI units); project: world -> UI point.
  void Draw(Batch2D& b, const EditorModel& model, const std::function<bool(const glm::vec3&, float&, float&)>& project) const;
  bool Stroking() const { return stroking_; }
  int strokes = 0; // finished strokes (test hook)
  glm::vec3 LastPoint() const { return hoverWorld_; } // world point under the mouse last time (test hook)

  // The terrain under a UI point: object id, its data and the hit (terrain-local metres). false: none.
  static bool Pick(const EditorModel& model, const Widget& view, const Camera3D& cam, float x, float y, std::string& object, TerrainData*& data,
                   glm::vec3& local);
  // A ray against a terrain's heights (terrain-local origin and direction): the first point below the surface.
  static bool Raycast(const TerrainData& t, const glm::vec3& origin, const glm::vec3& dir, float maxDistance, glm::vec3& hit);

private:
  void Apply(EditorModel& model, const glm::vec3& local);
  void Finish(EditorModel& model);
  bool stroking_ = false;
  std::string object_;
  TerrainData* data_ = nullptr;
  std::vector<uint16_t> heights0_;
  std::vector<std::vector<uint8_t>> splat0_;
  std::vector<uint8_t> density0_;
  int x0_ = 0, z0_ = 0, x1_ = -1, z1_ = -1; // samples touched by the stroke
  float flatten_ = 0;
  bool hover_ = false;
  glm::vec3 hoverWorld_{0};
  float hoverRadius_ = 0;
};
} // namespace bdg
