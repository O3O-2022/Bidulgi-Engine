#pragma once
// glTF 2.0 import (queue 3-1): .gltf (external or data-URI buffers) and .glb, through cgltf (MIT).
// glTF is right-handed, Y up, metres, like the engine (Core/Space.h): no axis conversion. Triangle primitives
// only; every primitive goes through meshoptimizer (vertex dedup, vertex cache, overdraw, vertex fetch order).
// cgltf types stay inside GltfImport.cpp.
#include "Anim/Skeleton.h"
#include "Render3D/MeshGen.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace bdg {
struct ImportedTexture {
  std::string name;
  int32_t w = 0, h = 0;
  std::vector<uint8_t> rgba; // top row first
  bool srgb = false;         // colour data (base colour, emissive); others are linear data
  bool nearest = false;      // glTF sampler asks for NEAREST magnification (pixel art, probes)
  bool clamp = false;        // glTF sampler wraps with CLAMP_TO_EDGE (default REPEAT)
};

struct ImportedMaterial {
  enum class Alpha { Opaque, Mask, Blend };
  std::string name;
  float baseColor[4] = {1, 1, 1, 1}; // linear
  float metallic = 1, roughness = 1;  // glTF defaults
  float emissive[3] = {0, 0, 0};      // linear, strength applied
  float normalScale = 1, occlusionStrength = 1, alphaCutoff = 0.5f;
  Alpha alpha = Alpha::Opaque;
  bool doubleSided = false, unlit = false;
  int baseColorTex = -1, metalRoughTex = -1, normalTex = -1, occlusionTex = -1, emissiveTex = -1; // ImportedModel::textures
};

struct ImportedPrimitive {
  MeshData mesh;
  int material = -1;             // -1 = default material
  std::vector<SkinVertex> skin;  // 7-1: per vertex (indices into ImportedModel::skinned->skeleton) when skinned
};

struct ImportedModel {
  std::vector<ImportedPrimitive> primitives;
  std::vector<ImportedMaterial> materials;
  std::vector<ImportedTexture> textures;
  struct Instance { int primitive; glm::mat4 world; std::string node; };
  std::vector<Instance> instances; // node hierarchy flattened (world matrices of the default scene); skinned: identity
  // 7-1: skeleton and clips (null without a skin). Skinned primitives are in model space at bind time; the joints move them.
  std::shared_ptr<SkinnedModelData> skinned;
  glm::vec3 boundsMin{0}, boundsMax{0}; // of all instances, world space
  struct Stats { size_t sourceVertices = 0, vertices = 0, triangles = 0, skippedPrimitives = 0; float acmrBefore = 0, acmrAfter = 0; } stats;
};

// images = false skips decoding textures (the animation system only wants skeletons and clips).
bool ImportGltf(const std::filesystem::path& path, ImportedModel& out, std::string* error = nullptr, bool images = true);
// meshoptimizer pass every importer uses: dedup, vertex cache, overdraw, fetch order. Returns ACMR {before, after}.
std::pair<float, float> OptimizeMesh(MeshData& m);
std::pair<float, float> OptimizeMesh(MeshData& m, std::vector<SkinVertex>& skin); // keeps the skin stream in step
// The model's box over all instances (skinned primitives in their rest pose); both importers call it last.
void ComputeModelBounds(ImportedModel& out);
// Same from bytes already in memory (.gltf text or .glb); relative URIs resolve against baseDir.
bool ImportGltfMemory(const void* data, size_t size, const std::filesystem::path& baseDir, ImportedModel& out, std::string* error = nullptr,
                      bool images = true);
}
