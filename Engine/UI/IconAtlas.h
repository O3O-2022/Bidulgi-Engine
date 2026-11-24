#pragma once
// Editor icons: the PNGs in Assets/Icons (made by tools/make_icons.py with PhotoCraft) packed into one texture at
// start-up. Batch2D samples it through its own slot, so icons never split the UI's single draw call.
#include "RHI/RHI.h"
#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace bdg {

struct IconAtlas {
  TextureHandle texture;
  int32_t w = 0, h = 0;
  std::unordered_map<std::string, std::array<float, 4>> uv; // file stem -> u0, v0, u1, v1
};

// Every *.png in `dir`, 64 px cells with 4 px of padding, colours spread into transparent pixels so mipmaps keep
// clean edges. false (with a reason) when the folder has no icons.
bool BuildIconAtlas(RHI& rhi, const std::filesystem::path& dir, IconAtlas& out, std::string* error = nullptr);

// The icon for a file or folder name (by extension; "" for a name without one = folder). Empty: no icon for it.
const char* FileIconName(const std::string& name);
// The icon for a component by its display name ("Mesh Renderer"); "comp-component" when there is no own one.
const char* ComponentIconName(const std::string& displayName);

} // namespace bdg
