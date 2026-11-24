#pragma once
// Scene view (queue 3-2): the open scene drawn in 3D inside the Scene panel. The view renders into its own
// target (Renderer3D) and the panel shows it as an image with overlays (camera chips, stats, light/camera icons,
// axis widget). Editor camera: Alt + left drag orbits, middle drag pans, wheel zooms, right drag looks around
// and W A S D Q E fly while it is held (Shift = faster, wheel = speed), F frames the selection. A left click
// selects the object under the cursor (Ctrl/Shift add or remove), a click on empty space clears the selection.
// Gizmos (queue 3-3): the active tool (W move, E rotate, R scale) puts handles on the selection; one drag is one
// undo step (a command group of "set Transform.*"), Ctrl snaps, Esc cancels, X switches world / local axes.
// Snapping and measuring (queue 3-3b): the Snap chip sets the grid (moves land on it; Ctrl turns snapping on or
// off for one drag), V while moving puts a vertex of the selection onto the vertex under the mouse, Ctrl+Shift
// while moving rests the object on the surface under the mouse. With two or more objects selected the Align chip
// opens min / centre / max / distribute per axis (one undo step each). The Measure tool (M) drags a ruler between
// two surface points (V snaps its ends to vertices).
// Greybox (queue 3-3c): the Blockout tool (B) shows chips that add a box, stairs, ramp or cylinder in front of the
// camera, and handles on the faces of the selected block: dragging one pushes or pulls that face (grid snapping
// applies, the opposite face stays put, one undo step).
// Debug tools (queue 3-8): keys 1-9 jump to CameraBookmark views (Ctrl + key stores the current view), Alt + 1-7
// pick what is shown (Lit, Albedo, Normal, Roughness, Metallic, Depth, Shadow), chips toggle wireframe, bounding
// boxes and a stats panel with GPU time per render pass.
//
// Meshes: MeshRenderer.mesh names a project asset (.gltf / .glb through the importer) or a built-in shape by its
// file stem: Plane (1 x 1), Cube, Sphere, Cylinder, Cone (1 m). Anything else shows a placeholder cube.
// Materials: a project .material / .matinst asset (path, guid: or file stem); without one, temporary rules by name
// (M_Ground, M_Gold, M_Steel, M_Copper, M_Glass; others a stable colour from the name).
#include "Platform/Window.h"
#include "Render3D/Renderer3D.h"
#include "Runtime/Gizmo.h"
#include "Runtime/Budget.h"
#include "Runtime/Snapping.h"
#include "Runtime/MaterialLibrary.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdg {
class AssetDatabase;
class EditorModel;
class UIContext;
struct Widget;

struct EditorCamera {
  glm::vec3 pivot{0, 0.8f, 0};
  float yaw = 35.f, pitch = -24.f; // degrees; yaw 0 looks along -Z, positive pitch looks up (Core/Space.h)
  float distance = 11.f;
  float fovY = 50.f;
  glm::vec3 Forward() const;
  glm::vec3 Position() const { return pivot - Forward() * distance; }
  Camera3D ToCamera() const;
};

class SceneView {
public:
  bool Init(RHI& rhi, Renderer3D& renderer);
  void SetMaterials(MaterialLibrary* lib) { materials_ = lib; } // the project's .material / .matinst assets
  void Shutdown();

  // Takes the events that belong to the view out of `events` (UI units). dt: seconds since the last frame.
  void HandleEvents(std::vector<InputEvent>& events, const Widget& view, EditorModel& model, double dt, bool textFocused);
  bool Animating() const { return (drag_ == Drag::Fly && !held_.empty()) || noteFrames_ > 0; } // flying, a note fading
  void Render(EditorModel& model, const AssetDatabase* assets, const Widget& view, float scale);
  void Draw(Widget& w, UIContext& ui, const EditorModel* model);

  void Frame(const EditorModel& model);                        // camera onto the selection (whole scene if none)
  std::string Pick(const EditorModel& model, float x, float y) const; // UI point -> object id ("" = nothing)
  bool ProjectUI(const glm::vec3& world, float& x, float& y) const;   // world -> UI point in the last view
  bool ObjectCenter(const EditorModel& model, const std::string& id, glm::vec3& center, float& radius) const;
  EditorCamera camera;
  QualitySettings quality = QualityPreset(QualityTier::Medium); // tier shown in the editor (queue 3-5)
  DebugView debugView = DebugView::Lit;
  bool wireframe = false, bounds = false, statsPanel = false;
  void RequestGIBake() { bakeRequested_ = true; } // one more bounce on the next render
  bool JumpToBookmark(const EditorModel& model, int slot);     // false: no bookmark with that number
  bool StoreBookmark(EditorModel& model, int slot);            // through commands (one undo step)
  std::string BookmarkName(const EditorModel& model, int slot) const; // "" when there is none
  bool ChipCenter(const std::string& name, float& x, float& y) const; // test hook: "view", "wire", "bounds", "stats"
  bool localSpace = false; // gizmo axes: the object's own (true) or the world's; scale always uses the object's
  SnapSettings snap;       // grid, rotate and scale steps; `always` = snap without Ctrl (3-3b)
  // Lines up the selected objects' bounds on an axis (0 x, 1 y, 2 z), or gives them equal gaps: one undo step.
  // false: too few objects (2, or 3 to distribute) or nothing to move.
  bool AlignSelection(EditorModel& model, int axis, AlignMode mode);
  // The nearest surface under a UI point, skipping the objects in `skip` and their children.
  bool RaycastUI(const EditorModel& model, float x, float y, const std::set<std::string>& skip, glm::vec3& hit, glm::vec3& normal,
                 std::string* owner = nullptr) const;
  // The mesh vertex nearest to a UI point within `maxDist` UI units: of the objects in `ids` (and their children)
  // when `inIds`, otherwise of every other object.
  bool NearestVertexUI(const EditorModel& model, float x, float y, const std::set<std::string>& ids, bool inIds, float maxDist,
                       glm::vec3& vertex) const;
  // World bounds of an object's meshes (and its children's), as the scene is now.
  bool ObjectBounds(const EditorModel& model, const std::string& id, glm::vec3& lo, glm::vec3& hi) const;
  const Measure* CurrentMeasure() const { return hasMeasure_ ? &measure_ : nullptr; } // the ruler (Measure tool)
  bool SnapTarget(glm::vec3& p) const { p = snapTarget_; return hasSnapTarget_; }    // vertex / surface under the drag
  // Platform budget (queue 3-7): the Budget chip opens bars for the target platform at the shown tier, from the
  // last render; over budget the chip turns to the warning colour and the heaviest objects can be picked.
  int budgetPlatform = 0;                                  // PlatformName index (the toolbar badge)
  bool budgetPanel = false;
  std::array<BudgetLimits, kBudgetPlatforms> budgetMedium{DefaultBudget(0), DefaultBudget(1), DefaultBudget(2), DefaultBudget(3)};
  std::vector<BudgetBar> BudgetBars() const;              // of the last render
  bool BudgetOver() const;
  struct Culprit { std::string id, name; double triangles = 0; int draws = 0; };
  std::vector<Culprit> BudgetCulprits() const;            // heaviest objects of the last render (3 at most)
  std::string DescribeBudget() const;                     // "Android · 중사양 · Draw 11 / 200 · ..." (log, bdg budget)
  // Game view (queue 3-6): the scene seen by its first Camera component, no editor overlays. `playTime` < 0: not
  // playing (the corner badge says so).
  void RenderGame(EditorModel& model, const AssetDatabase* assets, const Widget& view, float scale);
  void DrawGame(Widget& w, UIContext& ui, double playTime);
  uint64_t GameFrames() const { return gameFrames_; } // test hook: game views rendered
  void ReadGamePixels(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba);
  // Greybox (3-3c): adds a block where the view looks (on the surface there, on the grid), selected; one undo step.
  std::string CreateBlock(EditorModel& model, int shape); // BlockoutShape as int; returns the new object's id
  // Face handle of the selected block: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z (test hook; false: none there).
  bool FaceHandlePoint(const EditorModel& model, int face, float& x, float& y) const;
  // Gizmo of the current tool and selection in the last view (false: no gizmo).
  bool CurrentGizmo(const EditorModel& model, GizmoMode& mode, GizmoFrame& frame, GizmoView& view) const;
  bool GizmoPoint(const EditorModel& model, GizmoHandle h, float& x, float& y) const; // test hook
  // Test hooks: the last rendered picture (bottom-up RGBA8 rows) and what it contained.
  void ReadPixels(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba);
  int32_t PixelW() const { return pw_; }
  int32_t PixelH() const { return ph_; }
  size_t ItemCount() const { return items_.size(); }
  size_t OutlinedCount() const { return extras_.outlined.size(); }

private:
  struct MeshEntry {
    Mesh3D mesh;                 // built-in shape
    MeshData cpu;                // its triangles (picking)
    Model3D model;               // glTF asset
    std::vector<MeshData> cpuPrims;
    uint64_t hash = 0;
    bool isModel = false, missing = false;
  };
  const MeshEntry* Resolve(const std::string& name, const AssetDatabase* assets);
  Material3D MaterialFor(const std::string& name); // project material asset first, then the name rules
  void BuildItems(EditorModel& model, const AssetDatabase* assets);

  RHI* rhi_ = nullptr;
  Renderer3D* r_ = nullptr;
  MaterialLibrary* materials_ = nullptr;
  RenderTargetHandle rt_;
  int32_t pw_ = 0, ph_ = 0;
  float vx_ = 0, vy_ = 0, vw_ = 1, vh_ = 1; // last view rect (UI units)
  std::unordered_map<std::string, MeshEntry> meshes_;
  std::unordered_map<std::string, MeshEntry> builtins_;
  std::unordered_map<std::string, MeshEntry> blocks_; // greybox meshes by shape and size (unused ones are dropped)
  TextureHandle gridTex_;                             // the greybox 1 m grid
  RenderTargetHandle gameRT_;                         // Game view (3-6)
  BudgetUse budgetUse_;                               // measured in the last scene view render (3-7)
  std::vector<Culprit> culprits_;
  int32_t gameW_ = 0, gameH_ = 0;
  uint64_t gameFrames_ = 0;
  bool gameCamera_ = false;
  const MeshEntry* BlockMesh(int shape, const glm::vec3& size, int steps);
  struct FaceHandle { int face; glm::vec3 center, normal; };
  std::vector<FaceHandle> FaceHandles(const EditorModel& model) const; // of the primary selection's block
  struct FaceStart { std::string id; int face = 0; Vec3 size, position; glm::mat4 world{1.f}, parentWorld{1.f}; } face_;
  int hotFace_ = -1;
  std::vector<DrawItem3D> items_;
  struct ItemInfo { std::string owner; const MeshData* cpu; glm::vec3 lo{0}, hi{0}; glm::mat4 ownerWorld{1.f}; }; // world bounds
  glm::mat4 ItemMatrix(const EditorModel& model, size_t i) const; // item i as the scene is now (moved since the render)
  bool Owned(const EditorModel& model, const std::string& owner, const std::set<std::string>& ids) const; // owner or a parent in ids
  std::vector<ItemInfo> info_;
  RenderExtras extras_;
  Light3D light_;
  struct Icon { std::string id; glm::vec3 pos; int kind; }; // 0 sun, 1 light, 2 camera
  std::vector<Icon> icons_;
  struct Chip { std::string name; float x, y, w, h; };
  std::vector<Chip> chips_; // clickable chips of the last Draw
  std::string bookmarkNote_; // "2 Lights" after a jump, shown for a moment
  std::vector<ReflectionProbe3D> probes_; // from the scene (GI Tier 0)
  uint64_t probeKey_ = 0;
  ProbeVolume3D volume_; // the first LightProbeVolume in the scene (GI Tier 0-B)
  bool hasVolume_ = false;
  size_t lightmapItems_ = 0;                        // items marked bakeLightmap (GI Tier 0-C)
  uint64_t lightmapKey_ = 0, bakedLightmapKey_ = 0; // their meshes and placements now / at the last bake
  uint64_t lastLoadTry_ = 0;                        // lightmap key last looked up in .cache/gi
  ColorGrading3D grading_;                          // from the first ColorGrading component (3-5b)
  bool hasGrading_ = false;
  std::string lutPath_;                             // .cube file read into lutCache_
  int lutCacheSize_ = 0;
  std::vector<float> lutCache_;
  bool bakeRequested_ = false, firstBake_ = false;
  int noteFrames_ = 0;
  float renderMs_ = 0;

  enum class Drag { None, Click, Orbit, Pan, Fly, Gizmo, Measure, Face } drag_ = Drag::None;
  // 3-3b: vertex / surface snapping during a move drag, the ruler, the align chips
  bool vHeld_ = false, vertexSourceSet_ = false, hasSnapTarget_ = false, hasMeasure_ = false, alignOpen_ = false;
  glm::vec3 vertexSource_{0}, lastMove_{0}, snapTarget_{0}, startLo_{0}, startHi_{0};
  Measure measure_;
  bool MeasurePoint(const EditorModel& model, float x, float y, glm::vec3& p) const;
  GizmoDrag gizmo_;
  GizmoHandle hot_ = GizmoHandle::None;
  bool gizmoChanged_ = false;
  struct DragStart { std::string id; Vec3 position, rotation, scale; glm::mat4 world, parentWorld; };
  std::vector<DragStart> starts_;
  void ApplyGizmo(EditorModel& model, const GizmoDelta& d);
  int dragButton_ = 0;
  float lastX_ = 0, lastY_ = 0, downX_ = 0, downY_ = 0;
  bool clickCtrl_ = false, hover_ = false;
  std::set<Key> held_;
  float flySpeed_ = 5.f;
};
}
