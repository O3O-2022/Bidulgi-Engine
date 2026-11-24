#pragma once
// Built-in components (reflected; tools/bht generates Components.gen.cpp).
#include "Core/Reflection.h"
#include <string>

namespace bdg {
BCLASS(Component)
struct Transform {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Local position in metres (Y-up)")
  Vec3 position;
  BPROPERTY(Edit, Tooltip = "Local rotation, Euler angles in degrees")
  Vec3 rotation;
  BPROPERTY(Edit)
  Vec3 scale{1, 1, 1};
};

BCLASS(Component)
struct MeshRenderer {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) std::string mesh;
  BPROPERTY(Edit) std::string material;
  BPROPERTY(Edit, Tooltip = "Static GI: bake its bounced light into a lightmap (Scene view: Bake GI)") bool bakeLightmap = false;
};

BENUM()
enum class LightType { Directional, Point, Spot };

BCLASS(Component)
struct Light {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) LightType type = LightType::Point;
  BPROPERTY(Edit) Color color{1, 1, 1, 1};
  BPROPERTY(Edit, Range = "0,100000") float intensity = 1.f;
  BPROPERTY(Edit, Range = "0,1000", ShowIf = "type != Directional") float range = 10.f;
  BPROPERTY(Edit, Range = "1,179", ShowIf = "type == Spot", Tooltip = "Full cone angle in degrees") float spotAngle = 45.f;
  BPROPERTY(Edit, Range = "0,179", ShowIf = "type == Spot", Tooltip = "Full-brightness cone in degrees (soft edge out to spotAngle)")
  float innerSpotAngle = 30.f;
  BPROPERTY(Edit, ShowIf = "type == Directional", Tooltip = "Shadow map (directional lights; point and spot shadows come later)")
  bool castShadows = true;
};

// Camera bookmark (queue 3-8): number key `slot` (1-9) over the scene view jumps to this view, Ctrl + key stores the
// current view here. Saved with the scene, so test zones keep fixed cameras. The object's name is the label.
BCLASS(Component)
struct CameraBookmark {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,9", Tooltip = "Number key that jumps here (Ctrl + the key stores the current view)") int32_t slot = 1;
  BPROPERTY(Edit, Header = "View") Vec3 pivot;
  BPROPERTY(Edit, Range = "-360,360") float yaw = 35.f;
  BPROPERTY(Edit, Range = "-89,89") float pitch = -24.f;
  BPROPERTY(Edit, Range = "0.05,20000") float distance = 11.f;
  BPROPERTY(Edit, Tooltip = "Quality tier for this view (potato ... ultra); empty keeps the current one") std::string tier;
};

// Reflection probe (GI Tier 0): captures the scene around the object; objects whose centre is inside its box get
// their reflections and ambient light from it. Baked in the editor (Scene view "Bake GI"; each bake adds a bounce).
BCLASS(Component)
struct ReflectionProbe {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Size of the box it covers, centred on the object (metres)") Vec3 size{10, 4, 10};
  BPROPERTY(Edit, Tooltip = "Line reflections up with the box's walls (rooms)") bool boxProjection = true;
  BPROPERTY(Edit, Range = "0,10") float intensity = 1.f;
};

// Light probe volume (GI Tier 0-B): a grid of light probes filling a box centred on the object. Surfaces inside blend
// the nearest probes per pixel for their indirect (bounced) light. Baked with the reflection probes ("Bake GI").
BCLASS(Component)
struct LightProbeVolume {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Size of the box it fills, centred on the object (metres)") Vec3 size{10, 4, 10};
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Probes along X") int32_t countX = 4;
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Probes along Y (up)") int32_t countY = 2;
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Probes along Z") int32_t countZ = 4;
  BPROPERTY(Edit, Range = "0,10") float intensity = 1.f;
};

// Sky and ambient light (queue 3-4): the background of the scene view and the light it gives to every surface.
// The sun disk follows the first directional light.
BCLASS(Component)
struct Sky {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Header = "Colors") Color zenith{0.16f, 0.32f, 0.62f, 1};
  BPROPERTY(Edit) Color horizon{0.62f, 0.70f, 0.80f, 1};
  BPROPERTY(Edit) Color ground{0.20f, 0.18f, 0.16f, 1};
  BPROPERTY(Edit, Range = "0,10", Tooltip = "Scales the background and the ambient light it gives") float intensity = 1.f;
  BPROPERTY(Edit, Range = "0,10", Tooltip = "Sun disk size in degrees (0 = no disk)") float sunSize = 1.5f;
};

// Turns its object while the game is playing (queue 3-6): the first built-in behaviour, so play mode has
// something to run before the script hosts (M4) exist.
BCLASS(Component)
struct Rotator {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Degrees per second around X, Y and Z while playing") Vec3 speed{0, 45, 0};
};

// Greybox block (queue 3-3c): a box, stairs, ramp or cylinder built from its size, for laying out a level before
// the art exists. The pivot is the bottom centre. In the scene view's Blockout tool (B) its faces have handles:
// drag one to push or pull that face (the opposite face stays). Physics will use it as a collider (M6).
BENUM()
enum class BlockoutShape { Box, Stairs, Ramp, Cylinder };

BCLASS(Component)
struct Blockout {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) BlockoutShape shape = BlockoutShape::Box;
  BPROPERTY(Edit, Tooltip = "Width, height, depth in metres (pivot at the bottom centre)") Vec3 size{1, 1, 1};
  BPROPERTY(Edit, Range = "1,64", ShowIf = "shape == Stairs", Tooltip = "Steps from the front (+Z) up to the back") int32_t steps = 6;
  BPROPERTY(Edit, Tooltip = "Project material; empty = the grey 1 m grid") std::string material;
  BPROPERTY(Edit, Tooltip = "Physics uses the block as a collider (M6)") bool collider = true;
};

// Colour grading (3-5b): the final picture through a look-up table (.cube from Resolve, Photoshop ...) and a few
// controls. The first one in the scene is used.
BCLASS(Component)
struct ColorGrading {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Optional .cube file in the project (3D table, 0..1 domain)") std::string lut;
  BPROPERTY(Edit, Range = "0,1", Tooltip = "How much of the grading shows over the original") float intensity = 1.f;
  BPROPERTY(Edit, Range = "0,2") float contrast = 1.f;
  BPROPERTY(Edit, Range = "0,2") float saturation = 1.f;
  BPROPERTY(Edit, Range = "-1,1", Tooltip = "Cool (blue) to warm (orange)") float temperature = 0.f;
  BPROPERTY(Edit, Range = "-1,1", Tooltip = "Green to magenta") float tint = 0.f;
};

BCLASS(Component)
struct Camera {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,179", Tooltip = "Vertical field of view in degrees") float fov = 60.f;
  BPROPERTY(Edit) float nearClip = 0.1f;
  BPROPERTY(Edit) float farClip = 1000.f;
};
}
