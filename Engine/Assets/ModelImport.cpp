#include "Assets/ModelImport.h"
#include "Assets/AssetImport.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <ufbx.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
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

// 7-1: the skeleton of every node a skin cluster binds plus their ancestors (the scene root excepted), parents first.
void BuildFbxSkeleton(const ufbx_scene* scene, std::map<const ufbx_node*, int>& nodeJoint, Skeleton& sk) {
  std::map<const ufbx_node*, bool> want;
  std::map<const ufbx_node*, const ufbx_skin_cluster*> firstCluster;
  for (size_t i = 0; i < scene->skin_deformers.count; ++i) {
    const ufbx_skin_deformer* d = scene->skin_deformers.data[i];
    for (size_t c = 0; c < d->clusters.count; ++c) {
      const ufbx_skin_cluster* cl = d->clusters.data[c];
      if (!cl->bone_node) continue;
      firstCluster.emplace(cl->bone_node, cl);
      for (const ufbx_node* n = cl->bone_node; n && !n->is_root; n = n->parent) want[n] = true;
    }
  }
  std::function<void(const ufbx_node*, int)> visit = [&](const ufbx_node* n, int parent) {
    int self = parent;
    if (!n->is_root && want.count(n)) {
      Joint j;
      j.name = Str(n->name);
      j.parent = parent;
      const ufbx_transform& t = n->local_transform;
      j.t = {(float)t.translation.x, (float)t.translation.y, (float)t.translation.z};
      j.r = glm::normalize(glm::quat((float)t.rotation.w, (float)t.rotation.x, (float)t.rotation.y, (float)t.rotation.z));
      j.s = {(float)t.scale.x, (float)t.scale.y, (float)t.scale.z};
      if (auto it = firstCluster.find(n); it != firstCluster.end()) {
        j.skinned = true;
        j.inverseBind = ToGlm(it->second->geometry_to_bone);
      }
      self = nodeJoint[n] = (int)sk.joints.size();
      sk.joints.push_back(std::move(j));
    }
    for (size_t c = 0; c < n->children.count; ++c) visit(n->children.data[c], self);
  };
  if (scene->root_node) visit(scene->root_node, -1);
  int best = -1, bestDepth = 1 << 30;
  for (size_t j = 0; j < sk.joints.size(); ++j) {
    if (!sk.joints[j].skinned) continue;
    int depth = 0;
    for (int p = sk.joints[j].parent; p >= 0; p = sk.joints[p].parent) ++depth;
    if (depth < bestDepth) { best = (int)j; bestDepth = depth; }
  }
  sk.root = best;
}

// 7-1: every animation stack sampled at 30 frames per second (ufbx evaluates layers, curves and constraints); a value
// that never changes keeps one key.
void ReadFbxClips(const ufbx_scene* scene, const std::map<const ufbx_node*, int>& nodeJoint, std::vector<AnimClip>& clips) {
  constexpr double kRate = 30.0;
  for (size_t si = 0; si < scene->anim_stacks.count; ++si) {
    const ufbx_anim_stack* st = scene->anim_stacks.data[si];
    const double len = std::max(0.0, st->time_end - st->time_begin);
    const size_t frames = (size_t)std::ceil(len * kRate - 1e-6) + 1;
    AnimClip clip;
    clip.name = Str(st->name).empty() ? std::format("Clip{}", si) : Str(st->name);
    if (const size_t bar = clip.name.find('|'); bar != std::string::npos) clip.name = clip.name.substr(bar + 1); // "Armature|Run"
    clip.duration = (float)len;
    for (const auto& [node, joint] : nodeJoint) {
      AnimChannel ch[3];
      for (int k = 0; k < 3; ++k) {
        ch[k].joint = joint;
        ch[k].path = (AnimChannel::Path)k;
        ch[k].times.resize(frames);
        ch[k].values.resize(frames);
      }
      for (size_t f = 0; f < frames; ++f) {
        const double local = std::min(len, f / kRate);
        const ufbx_transform t = ufbx_evaluate_transform(st->anim, node, st->time_begin + local);
        glm::vec4 q((float)t.rotation.x, (float)t.rotation.y, (float)t.rotation.z, (float)t.rotation.w);
        if (f > 0 && glm::dot(q, ch[1].values[f - 1]) < 0.f) q = -q; // keep neighbouring keys on one hemisphere
        ch[0].values[f] = {(float)t.translation.x, (float)t.translation.y, (float)t.translation.z, 0.f};
        ch[1].values[f] = q;
        ch[2].values[f] = {(float)t.scale.x, (float)t.scale.y, (float)t.scale.z, 0.f};
        for (int k = 0; k < 3; ++k) ch[k].times[f] = (float)local;
      }
      for (AnimChannel& c : ch) {
        bool constant = true;
        for (size_t f = 1; f < frames && constant; ++f) constant = glm::all(glm::lessThan(glm::abs(c.values[f] - c.values[0]), glm::vec4(1e-6f)));
        if (constant) {
          c.times.resize(1);
          c.values.resize(1);
        }
        clip.channels.push_back(std::move(c));
      }
    }
    if (!clip.channels.empty()) clips.push_back(std::move(clip));
  }
}

} // namespace

bool IsModelFile(const std::string& p) {
  const std::string e = ExtOf(p);
  return e == ".glb" || e == ".gltf" || e == ".fbx" || e == ".obj";
}

bool ImportModel(const fs::path& path, ImportedModel& out, std::string* error, bool images) {
  const std::string e = Lower(path.extension().string());
  if (e == ".fbx" || e == ".obj") return ImportFbxObj(path, out, error, images);
  return ImportGltf(path, out, error, images);
}

bool ImportFbxObj(const fs::path& path, ImportedModel& out, std::string* error, bool images) {
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
    if (!images) { // skeletons and clips only
    } else if (LoadTexture(t, dir, it)) {
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

  std::map<const ufbx_node*, int> nodeJoint; // 7-1
  if (scene->skin_deformers.count) {
    out.skinned = std::make_shared<SkinnedModelData>();
    BuildFbxSkeleton(scene, nodeJoint, out.skinned->skeleton);
    ReadFbxClips(scene, nodeJoint, out.skinned->clips);
  }

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
      const ufbx_skin_deformer* skinDef = out.skinned && mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
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
          if (skinDef) {
            const uint32_t vert = mesh->vertex_indices.data[ix];
            int joints[8];
            float weights[8];
            int count = 0;
            if (vert < skinDef->vertices.count) {
              const ufbx_skin_vertex& sv = skinDef->vertices.data[vert];
              for (uint32_t w = 0; w < sv.num_weights && count < 8; ++w) {
                const ufbx_skin_weight& sw = skinDef->weights.data[sv.weight_begin + w];
                const ufbx_node* bone = sw.cluster_index < skinDef->clusters.count ? skinDef->clusters.data[sw.cluster_index]->bone_node : nullptr;
                const auto it = bone ? nodeJoint.find(bone) : nodeJoint.end();
                joints[count] = it == nodeJoint.end() ? -1 : it->second;
                weights[count++] = (float)sw.weight;
              }
            }
            ip.skin.push_back(MakeSkinVertex(joints, weights, count));
          }
        }
      }
      if (md.indices.empty()) continue;
      out.stats.sourceVertices += md.vertices.size();
      auto [b, a] = ip.skin.empty() ? OptimizeMesh(md) : OptimizeMesh(md, ip.skin);
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
    for (int prim : meshPrims[node->mesh]) // skinned parts: the joints place them (geometry_to_bone includes the node)
      out.instances.push_back({prim, out.primitives[prim].skin.empty() ? world : glm::mat4(1.f), Str(node->name)});
  }
  ufbx_free_scene(scene);
  if (out.skinned)
    LogInfo("model: {} -> skeleton of {} joints, {} clips", std::string((const char*)path.filename().u8string().c_str()), out.skinned->skeleton.joints.size(),
            out.skinned->clips.size());

  ComputeModelBounds(out);
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
