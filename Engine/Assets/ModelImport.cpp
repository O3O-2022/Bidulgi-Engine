#include "Assets/ModelImport.h"
#include "Assets/AssetImport.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <ufbx.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

namespace bdg {
namespace fs = std::filesystem;
namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string ExtOf(const std::string& p) {
  const size_t dot = p.find_last_of('.');
  return dot == std::string::npos ? Lower(p) : Lower(p.substr(dot));
}
fs::path U8(const std::string& s) { return fs::path(std::u8string((const char8_t*)s.c_str())); }
std::string Str(const ufbx_string& s) { return std::string(s.data, s.length); }

glm::mat4 ToGlm(const ufbx_matrix& m) {
  glm::mat4 r(1.f);
  for (int c = 0; c < 4; ++c)
    for (int k = 0; k < 3; ++k) r[c][k] = (float)m.cols[c].v[k];
  return r;
}

// A texture of the file: embedded bytes, else the file next to the model, else the path written in the file.
bool LoadTexture(const ufbx_texture* t, const fs::path& dir, ImportedTexture& out) {
  out.name = Str(t->relative_filename).empty() ? Str(t->filename) : Str(t->relative_filename);
  if (t->content.size > 0 && DecodeImageRGBA((const uint8_t*)t->content.data, t->content.size, out.w, out.h, out.rgba)) return true;
  std::error_code ec;
  for (const std::string& cand : {Str(t->relative_filename), Str(t->filename), Str(t->absolute_filename)}) {
    if (cand.empty()) continue;
    fs::path p = U8(cand);
    if (p.is_relative()) p = dir / p;
    if (!fs::exists(p, ec)) p = dir / U8(cand).filename(); // a path from the artist's machine: try the name here
    if (fs::exists(p, ec) && LoadImageRGBA(p, out.w, out.h, out.rgba)) return true;
  }
  return false;
}

} // namespace

bool IsModelFile(const std::string& p) {
  const std::string e = ExtOf(p);
  return e == ".glb" || e == ".gltf" || e == ".fbx" || e == ".obj";
}

bool ImportModel(const fs::path& path, ImportedModel& out, std::string* error) {
  const std::string e = Lower(path.extension().string());
  if (e == ".fbx" || e == ".obj") return ImportFbxObj(path, out, error);
  return ImportGltf(path, out, error);
}

bool ImportFbxObj(const fs::path& path, ImportedModel& out, std::string* error) {
  out = {};
  ufbx_load_opts opts = {};
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
  opts.generate_missing_normals = true;
  opts.load_external_files = true;            // OBJ .mtl
  opts.ignore_missing_external_files = true;
  ufbx_error err;
  const std::u8string u8 = path.u8string();
  ufbx_scene* scene = ufbx_load_file_len((const char*)u8.data(), u8.size(), &opts, &err);
  if (!scene) {
    if (error) *error = std::string(err.description.data, err.description.length);
    return false;
  }
  const fs::path dir = path.parent_path();

  // materials and their textures (shared textures load once)
  std::map<const ufbx_material*, int> materialIndex;
  std::map<const ufbx_texture*, int> textureIndex;
  auto texture = [&](const ufbx_texture* t, bool srgb) {
    if (!t) return -1;
    if (auto it = textureIndex.find(t); it != textureIndex.end()) return it->second;
    ImportedTexture it;
    it.srgb = srgb;
    int index = -1;
    if (LoadTexture(t, dir, it)) {
      index = (int)out.textures.size();
      out.textures.push_back(std::move(it));
    } else {
      LogWarn("model: texture '{}' of {} not found; the material uses its colour only", Str(t->filename), path.filename().string());
    }
    textureIndex[t] = index;
    return index;
  };
  auto material = [&](const ufbx_material* m) {
    if (!m) return -1;
    if (auto it = materialIndex.find(m); it != materialIndex.end()) return it->second;
    ImportedMaterial o;
    o.name = Str(m->name);
    const ufbx_material_pbr_maps& p = m->pbr;
    const double factor = p.base_factor.has_value ? p.base_factor.value_real : 1.0;
    o.baseColor[0] = (float)(p.base_color.value_vec4.x * factor);
    o.baseColor[1] = (float)(p.base_color.value_vec4.y * factor);
    o.baseColor[2] = (float)(p.base_color.value_vec4.z * factor);
    o.baseColor[3] = p.opacity.has_value ? (float)p.opacity.value_real : 1.f;
    o.metallic = p.metalness.has_value ? (float)p.metalness.value_real : 0.f; // FBX Phong materials are not metal
    o.roughness = p.roughness.has_value ? (float)std::clamp(p.roughness.value_real, 0.0, 1.0) : 0.6f;
    const double ef = p.emission_factor.has_value ? p.emission_factor.value_real : 1.0;
    for (int k = 0; k < 3; ++k) o.emissive[k] = (float)(p.emission_color.value_vec4.v[k] * ef);
    if (o.baseColor[3] < 0.999f) o.alpha = ImportedMaterial::Alpha::Blend;
    o.baseColorTex = texture(p.base_color.texture, true);
    o.normalTex = texture(p.normal_map.texture, false);
    o.emissiveTex = texture(p.emission_color.texture, true);
    if (o.baseColorTex >= 0) o.baseColor[0] = o.baseColor[1] = o.baseColor[2] = 1.f; // the map carries the colour
    const int index = (int)out.materials.size();
    out.materials.push_back(o);
    materialIndex[m] = index;
    return index;
  };

  // one primitive per mesh material part; instances per node using the mesh
  std::map<const ufbx_mesh*, std::vector<int>> meshPrims;
  std::vector<uint32_t> tri;
  double cacheBefore = 0, cacheAfter = 0;
  for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
    const ufbx_mesh* mesh = scene->meshes.data[mi];
    tri.resize(mesh->max_face_triangles * 3);
    std::vector<int>& prims = meshPrims[mesh];
    for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
      const ufbx_mesh_part& part = mesh->material_parts.data[pi];
      if (part.num_triangles == 0) continue;
      ImportedPrimitive ip;
      ip.material = part.index < mesh->materials.count ? material(mesh->materials.data[part.index]) : -1;
      MeshData& md = ip.mesh;
      for (size_t fi = 0; fi < part.face_indices.count; ++fi) {
        const ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
        const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
        for (uint32_t k = 0; k < n * 3; ++k) {
          const uint32_t ix = tri[k];
          Vertex3D v{};
          const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
          v.px = (float)p.x; v.py = (float)p.y; v.pz = (float)p.z;
          if (mesh->vertex_normal.exists) {
            const ufbx_vec3 nn = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
            v.nx = (float)nn.x; v.ny = (float)nn.y; v.nz = (float)nn.z;
          } else v.ny = 1;
          if (mesh->vertex_uv.exists) {
            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
            v.u = (float)uv.x;
            v.v = 1.f - (float)uv.y; // FBX / OBJ: v up from the bottom; the engine (like glTF): from the top
          }
          md.indices.push_back((uint32_t)md.vertices.size());
          md.vertices.push_back(v);
        }
      }
      if (md.indices.empty()) continue;
      out.stats.sourceVertices += md.vertices.size();
      auto [b, a] = OptimizeMesh(md);
      cacheBefore += b * md.indices.size();
      cacheAfter += a * md.indices.size();
      out.stats.vertices += md.vertices.size();
      out.stats.triangles += md.indices.size() / 3;
      prims.push_back((int)out.primitives.size());
      out.primitives.push_back(std::move(ip));
    }
  }
  if (out.stats.triangles) {
    out.stats.acmrBefore = (float)(cacheBefore / (out.stats.triangles * 3.0));
    out.stats.acmrAfter = (float)(cacheAfter / (out.stats.triangles * 3.0));
  }
  for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
    const ufbx_node* node = scene->nodes.data[ni];
    if (!node->mesh) continue;
    const glm::mat4 world = ToGlm(node->geometry_to_world);
    for (int prim : meshPrims[node->mesh]) out.instances.push_back({prim, world, Str(node->name)});
  }
  ufbx_free_scene(scene);

  bool any = false;
  for (const ImportedModel::Instance& in : out.instances)
    for (const Vertex3D& v : out.primitives[in.primitive].mesh.vertices) {
      const glm::vec3 p = glm::vec3(in.world * glm::vec4(v.px, v.py, v.pz, 1.f));
      out.boundsMin = any ? glm::min(out.boundsMin, p) : p;
      out.boundsMax = any ? glm::max(out.boundsMax, p) : p;
      any = true;
    }
  if (out.instances.empty()) {
    if (error) *error = "no meshes";
    return false;
  }
  return true;
}

std::vector<std::string> ModelDependencies(const fs::path& path) {
  const std::string e = Lower(path.extension().string());
  if (e == ".gltf") return GltfDependencies(path);
  std::vector<std::string> out;
  if (e != ".obj") return out;
  auto add = [&](std::string p) {
    while (!p.empty() && (p.back() == '\r' || p.back() == ' ')) p.pop_back();
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.empty() || U8(p).is_absolute() || std::find(out.begin(), out.end(), p) != out.end()) return;
    out.push_back(p);
  };
  auto lines = [](const fs::path& p, auto&& fn) {
    std::ifstream in(p, std::ios::binary);
    for (std::string l; std::getline(in, l);) fn(l);
  };
  std::vector<std::string> mtls;
  lines(path, [&](const std::string& l) {
    if (l.rfind("mtllib ", 0) == 0) mtls.push_back(l.substr(7));
  });
  for (const std::string& m : mtls) {
    const size_t before = out.size();
    add(m);
    if (out.size() == before) continue;
    lines(path.parent_path() / U8(out.back()), [&](const std::string& l) { // map_Kd textures/rock.png (last word = file)
      std::string t = l;
      while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
      if (t.rfind("map_", 0) != 0 && t.rfind("bump ", 0) != 0 && t.rfind("norm ", 0) != 0) return;
      const size_t sp = t.find_last_of(" \t");
      if (sp != std::string::npos) add(t.substr(sp + 1));
    });
  }
  return out;
}

} // namespace bdg
