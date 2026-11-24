#pragma once
// Model import for every format the engine reads (queue E-5): glTF / GLB through cgltf (GltfImport), FBX and OBJ
// (+ .mtl) through ufbx (MIT). Everything lands in the same ImportedModel, converted to the engine's space
// (right-handed, Y up, metres, Core/Space.h): ufbx bakes the file's axes and units into the geometry.
// FBX: meshes per material, node hierarchy, PBR or legacy (Phong) materials mapped to metal/roughness, embedded
// or external textures. Skeletons and animation come with M7. ufbx types stay inside ModelImport.cpp.
#include "Assets/GltfImport.h"
#include <filesystem>
#include <string>
#include <vector>

namespace bdg {

bool IsModelFile(const std::string& pathOrExt);            // .glb .gltf .fbx .obj
bool ImportModel(const std::filesystem::path& path, ImportedModel& out, std::string* error = nullptr);
bool ImportFbxObj(const std::filesystem::path& path, ImportedModel& out, std::string* error = nullptr);
// The files a model names by relative path (glTF buffers and images, OBJ .mtl and its texture maps), '/'
// separators; what dragging the model into a project copies along (2-5d).
std::vector<std::string> ModelDependencies(const std::filesystem::path& path);

} // namespace bdg
