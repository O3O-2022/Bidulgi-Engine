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
// Debug tools (queue 3-8): keys 1-9 jump to CameraBookmark views (Ctrl + key stores the current view), Alt + 2-9
// pick view modes (Unreal's keys); the toolbar's Show menu toggles wireframe, bounding boxes and a stats panel with
// GPU time per render pass. Toolbar (round 7): Runtime/SceneViewToolbar.cpp.
//
// Meshes: MeshRenderer.mesh names a project asset (.gltf / .glb through the importer) or a built-in shape by its
// file stem: Plane (1 x 1), Cube, Sphere, Cylinder, Cone (1 m). Anything else shows a placeholder cube.
// Materials: a project .material / .matinst asset (path, guid: or file stem); without one, temporary rules by name
// (M_Ground, M_Gold, M_Steel, M_Copper, M_Glass; others a stable colour from the name).
#include "Platform/Window.h"
#include "Render3D/Renderer3D.h"
#include "Scene/CameraShake.h"
#include "Runtime/Gizmo.h"
#include "Runtime/Budget.h"
#include "Runtime/Snapping.h"
#include "Runtime/MaterialLibrary.h"
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace bdg {
class AssetDatabase;
class EditorModel;
class TerrainView;
class TerrainTool;
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
  // flying (or still gliding to a stop), a note fading, the camera easing onto a framed selection
  bool Animating() const {
    return (drag_ == Drag::Fly && !held_.empty()) || glm::length(flyVel_) > 1e-3f || noteFrames_ > 0 || frameT_ < 1.f ||
           (!tipName_.empty() && tipSeconds_ < 0.6); // a toolbar tooltip about to show
  }
  void Render(EditorModel& model, const AssetDatabase* assets, const Widget& view, float scale);
  void Draw(Widget& w, UIContext& ui, const EditorModel* model);

  void Frame(const EditorModel& model);                        // camera onto the selection (whole scene if none), at once
  void FrameSmooth(const EditorModel& model);                  // the same, eased over a quarter second (F key, menu)
  float FlySpeed() const { return flySpeed_; }                 // m/s while flying (right mouse + WASD), wheel changes it
  void SetFlySpeed(float s);
  // Camera speed like Unreal (round 7): a level 1..8 (SpeedOfLevel m/s) times a scalar; the toolbar's speed button
  // opens both, the wheel while flying steps the level.
  int speedLevel = 4;
  float speedScalar = 1.f;
  static float SpeedOfLevel(int level);
  void SetSpeedLevel(int level);
  void SetSpeedScalar(float s);
  // Toolbar values kept per project (snapping, camera speed, World / Local, Show flags) as JSON at this path
  // ("" = not kept: automated runs). Saved by Draw when they change.
  std::string toolbarStatePath;
  bool LoadToolbarState();
  void SaveToolbarStateIfChanged();
  std::string ToolbarStateJson() const;
  std::string ToolbarTooltip(const std::string& name) const; // a control's hover text (with its shortcut)
  struct ToolbarLayout { bool oneRow = true, compact = false; float left = -1, right = 0; int controls = 0, hidden = 0; };
  ToolbarLayout ToolbarInfo() const; // test hook: the last drawn toolbar
  // Test hook: what a click on a toolbar control does, also when it sits in the "»" menu (menus open there).
  bool PressToolbar(const std::string& name, EditorModel& model);
  bool SpeedPopupOpen() const { return speedPopup_; }
  float frameEaseSeconds = 0.25f; // FrameSmooth's ease (0 = jump at once; scripted checks)
  bool flyEasing = true;           // fly speeds up / glides to a stop (false = instant; scripted checks)
  std::string Pick(const EditorModel& model, float x, float y) const; // UI point -> object id ("" = nothing)
  bool ProjectUI(const glm::vec3& world, float& x, float& y) const;   // world -> UI point in the last view
  bool ObjectCenter(const EditorModel& model, const std::string& id, glm::vec3& center, float& radius) const;
  EditorCamera camera;
  QualitySettings quality = QualityPreset(QualityTier::Medium); // tier shown in the editor (queue 3-5)
  // Automatic tier (3-5c): measures this device once (cached per GPU in `cachePath`, empty = no cache), picks the tier
  // and clamps it to the project's range; from then on a sustained frame drop steps down one tier (never below min).
  QualityTier DetectQuality(const ProjectQuality& range, const std::string& cachePath);
  struct AutoQuality {
    bool on = false, cached = false;
    double highMs = 0;               // benchmark: ms per frame at high, 1920 x 1080
    QualityTier measured = QualityTier::Medium, chosen = QualityTier::Medium, min = QualityTier::Potato;
    int stepsDown = 0;
  } autoQuality;
  QualityGovernor governor;
  std::function<void(QualityTier)> onAutoTier; // the tier changed by itself (badge text)
  // Feeds one frame's time to the frame drop guard (Render does it with the GPU time of each frame); true = stepped down.
  bool FeedFrameTime(double nowSeconds, double frameMs);
  DebugView debugView = DebugView::Lit;
  struct LegendRect { float x, y, w, h; };
  LegendRect LegendArea() const { return legendRect_; } // heat map legend drawn last frame (w = 0: none); test hook
  // Where a toolbar chip ("view", "tier", ...) was drawn last frame (UI units); false if not drawn. Test hook.
  bool ChipArea(const std::string& name, LegendRect& out) const;
  bool wireframe = false, bounds = false, statsPanel = false;
  void RequestGIBake() { bakeRequested_ = true; } // one more bounce on the next render
  bool JumpToBookmark(const EditorModel& model, int slot);     // false: no bookmark with that number
  bool StoreBookmark(EditorModel& model, int slot);            // through commands (one undo step)
  std::string BookmarkName(const EditorModel& model, int slot) const; // "" when there is none
  bool ChipCenter(const std::string& name, float& x, float& y) const; // test hook: "view", "wire", "bounds", "stats"
  // Scene icons (E-3): size 0 off, 1 small, 2 medium, 3 large (the "Icons" chip steps through them).
  int iconSize = 2;
  bool showGrid = true; // the editor ground grid (drawn over the finished picture)
  ShakeOffset gameShake;  // camera shake added to the Game view camera this frame (E-9, from play mode)
  // Test hook (E-7): whether the Scene view's last render drew light shafts
  bool RenderStatsSun(const Renderer3D*) const { return lastShafts_; }
  const RenderStats& LastStats() const { return lastStats_; } // of this view's last render (not the previews')
  static float IconPixels(int size) { return size <= 0 ? 0.f : size == 1 ? 16.f : size == 2 ? 22.f : 30.f; }
  std::vector<std::pair<std::string, std::string>> IconList() const; // test hook: (object id, icon name)
  int GizmoLines() const { return gizmoLines_; }
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
  std::string DescribeBudget() const;                     // "Android · 보통 · Draw 11 / 200 · ..." (log, bdg budget)
  // Game view (queue 3-6): the scene seen by its first Camera component, no editor overlays. `playTime` < 0: not
  // playing (the corner badge says so).
  void RenderGame(EditorModel& model, const AssetDatabase* assets, const Widget& view, float scale);
  void DrawGame(Widget& w, UIContext& ui, double playTime);
  uint64_t GameFrames() const { return gameFrames_; } // test hook: game views rendered
  void ReadGamePixels(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba);
  TextureHandle GameColor() const; // the last RenderGame image (invalid without a Camera): the game player draws it (7-4)
  // Greybox (3-3c): adds a block where the view looks (on the surface there, on the grid), selected; one undo step.
  std::string CreateBlock(EditorModel& model, int shape); // BlockoutShape as int; returns the new object's id
  // Auto re-import (2-5d): drops the imported models that are or use one of these project paths, so the next
  // render imports them again. Returns the models dropped (each logged).
  int Reimport(const std::vector<std::string>& changedPaths);
  int ReimportCount() const { return reimports_; }
  // A model dropped on the view: a new object with a MeshRenderer at the surface (or ground) under the UI point,
  // one undo step. Returns its id.
  std::string PlaceModel(EditorModel& model, const std::string& meshPath, float x, float y);
  // 6-1: a .prefab from the Project panel dropped on the view: an instance where the mouse is (surface, else ground).
  std::string PlacePrefab(EditorModel& model, const std::string& prefabPath);
  std::string PlacePrefabAt(EditorModel& model, const std::string& prefabPath, float x, float y);
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
  // 7-1 test hooks: objects drawn skinned (GPU) in the last BuildItems, and skinning passes run since Init.
  size_t SkinnedObjects() const { return skinnedLast_; }
  uint64_t SkinPasses() const { return skinPasses_; }
  // Terrains and foliage (round 6, Terrain/): drawn in both renders; the Terrain tool (T) sculpts and paints.
  TerrainView& Terrains() const { return *terrain_; }
  TerrainTool& TerrainBrushTool() const { return *terrainTool_; }

private:
  struct MeshEntry {
    Mesh3D mesh;                 // built-in shape
    MeshData cpu;                // its triangles (picking)
    Model3D model;               // glTF asset
    std::vector<MeshData> cpuPrims;
    uint64_t hash = 0;
    bool isModel = false, missing = false;
    std::vector<std::string> deps; // .gltf: the project paths of its buffers and images (2-5d)
    // 7-1 skinned models: per primitive the skin weights (empty = rigid), GPU source and per-joint boxes
    std::shared_ptr<const SkinnedModelData> skinned;
    std::vector<Renderer3D::SkinSource> skinSrc;
    std::vector<std::vector<JointBox>> jointBoxes;
    std::vector<glm::mat4> restSkin; // rest pose (outside play mode)
    uint64_t gen = 0;                // import number (skinned instances made for an older import are rebuilt)
  };
  // 7-1: an object drawing a skinned model owns its posed meshes (one per skinned primitive) and joint buffer.
  struct SkinInst {
    uint64_t gen = 0, serial = ~0ull, used = 0;
    std::vector<Mesh3D> targets; // per primitive (empty mesh for rigid ones)
    BufferHandle joints;
    size_t jointCount = 0;
  };
  std::unordered_map<std::string, SkinInst> skinInsts_;
  uint64_t importGen_ = 0, builds_ = 0, skinPasses_ = 0;
  size_t skinnedLast_ = 0;
  void DestroyEntry(MeshEntry& e);
  void DestroySkinInst(SkinInst& s);
  const MeshEntry* Resolve(const std::string& name, const AssetDatabase* assets);
  Material3D MaterialFor(const std::string& name); // project material asset first, then the name rules
  void BuildItems(EditorModel& model, const AssetDatabase* assets);

  RHI* rhi_ = nullptr;
  Renderer3D* r_ = nullptr;
  std::shared_ptr<TerrainView> terrain_; // round 6
  std::shared_ptr<TerrainTool> terrainTool_;
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
  struct ItemInfo {
    std::string owner;
    const MeshData* cpu;
    glm::vec3 lo{0}, hi{0};       // world bounds
    glm::mat4 ownerWorld{1.f};
    bool posed = false;           // 7-1 skinned: boxLo / boxHi (item space, this pose) instead of the bind-pose vertices
    glm::vec3 boxLo{0}, boxHi{0};
  };
  glm::mat4 ItemMatrix(const EditorModel& model, size_t i) const; // item i as the scene is now (moved since the render)
  bool Owned(const EditorModel& model, const std::string& owner, const std::set<std::string>& ids) const; // owner or a parent in ids
  std::vector<ItemInfo> info_;
  RenderExtras extras_;
  Light3D light_;
  // Scene icons (E-3): one per object that has no surface to click. kind: 0 sun, 1 point light, 2 camera, 3 spot
  // light, 4 reflection probe, 5 light probe volume, 6 camera bookmark. a, b, c: light range and spot angle /
  // camera fov, near, far (for the selected object's gizmo: frustum, range, cone).
  struct Icon {
    std::string id;
    glm::vec3 pos;
    int kind;
    std::string icon;     // Assets/Icons name
    Color tint{1, 1, 1, 1};
    glm::mat4 world{1.f};
    float a = 0, b = 0, c = 0;
  };
  std::vector<Icon> icons_;
  int gizmoLines_ = 0; // segments of the selected objects' shapes drawn last frame (test hook)
  void DrawSelectionShapes(Batch2D& b, const Theme& t, const EditorModel& model);
  struct Chip { std::string name; float x, y, w, h; bool toolbar = false; };
  LegendRect legendRect_{0, 0, 0, 0};
  std::vector<Chip> chips_; // clickable chips of the last Draw
  UIContext* ui_ = nullptr;  // set by Draw: chips open their option lists as popup menus
  void OpenChipMenu(const Chip& chip, EditorModel& model);
  // Toolbar (round 7, Runtime/SceneViewToolbar.cpp)
  void DrawToolbar(Batch2D& b, const Theme& t, UIContext& ui, const Widget& w, const EditorModel* model, float rowY);
  bool ToolbarClick(const Chip& hit, EditorModel& model, bool fromMenu = false);
  bool ToolbarOn(const std::string& name) const;
  std::string ToolbarLabel(const std::string& name, bool compact) const;
  std::vector<std::string> overflow_;  // toolbar controls moved into the "»" menu (narrow view)
  bool speedPopup_ = false, toolbarCompact_ = false;
  std::string viewPreset_ = "Perspective", savedToolbar_, tipName_;
  double tipSeconds_ = 0;               // how long the pointer has rested on tipName_
  const EditorModel* toolModel_ = nullptr;
  LegendRect speedPopupRect_{0, 0, 0, 0};
  std::string bookmarkNote_; // "2 Lights" after a jump, shown for a moment
  std::vector<ReflectionProbe3D> probes_; // from the scene (GI Tier 0)
  uint64_t probeKey_ = 0;
  ProbeVolume3D volume_; // the first LightProbeVolume in the scene (GI Tier 0-B)
  bool hasVolume_ = false;
  size_t lightmapItems_ = 0;                        // items marked bakeLightmap (GI Tier 0-C)
  uint64_t lightmapKey_ = 0, bakedLightmapKey_ = 0; // their meshes and placements now / at the last bake
  int reimports_ = 0;
  bool lastShafts_ = false;
  RenderStats lastStats_;
  std::vector<PostVolume3D> postVolumes_; // post-process volumes of the scene (E-6), blended at each view's camera
public:
  PostEffects3D PostAt(const glm::vec3& viewPos) const { return BlendPostVolumes(postVolumes_, viewPos); }
private:
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
  float hoverX_ = 0, hoverY_ = 0; // the mouse over the view (where a dragged prefab lands)
  bool clickCtrl_ = false, hover_ = false;
  std::set<Key> held_;
  float flySpeed_ = 5.f;
  glm::vec3 flyVel_{0};        // eased fly velocity (m/s): speeds up and glides to a stop instead of jumping
  bool flyBoost_ = false;      // Shift held while flying: x3, only while held
  float panDepth_ = 0;         // distance to the surface under the cursor when a pan starts (0 = the pivot's)
  glm::vec3 frameFromPivot_{0}, frameToPivot_{0};
  float frameFromDist_ = 0, frameToDist_ = 0, frameT_ = 1.f;
};
}
