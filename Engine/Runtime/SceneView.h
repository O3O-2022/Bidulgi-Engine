#pragma once
// Scene view (queue 3-2): the open scene drawn in 3D inside the Scene panel. The view renders into its own
// target (Renderer3D) and the panel shows it as an image with overlays (camera chips, stats, light/camera icons,
// axis widget). Editor camera: Alt + left drag orbits, middle drag pans, wheel zooms, right drag looks around
// and W A S D Q E fly while it is held (Shift = faster, wheel = speed), F frames the selection. A left click
// selects the object under the cursor (Ctrl/Shift add or remove), a click on empty space clears the selection.
// Gizmos (queue 3-3): the active tool (W move, E rotate, R scale) puts handles on the selection; one drag is one
// undo step (a command group of "set Transform.*"), Ctrl snaps, Esc cancels, X switches world / local axes.
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
  std::vector<DrawItem3D> items_;
  struct ItemInfo { std::string owner; const MeshData* cpu; glm::vec3 lo{0}, hi{0}; }; // world bounds
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
  bool bakeRequested_ = false, firstBake_ = false;
  int noteFrames_ = 0;
  float renderMs_ = 0;

  enum class Drag { None, Click, Orbit, Pan, Fly, Gizmo } drag_ = Drag::None;
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
