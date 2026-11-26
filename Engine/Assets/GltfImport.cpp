#include "Assets/GltfImport.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>

#if defined(_MSC_VER)
#pragma warning(push, 0) // third-party implementation: keep /W4 clean for engine code only
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {
// cgltf opens files with fopen (ANSI code page on Windows); these callbacks take its UTF-8 paths through
// std::filesystem so Korean and other non-ASCII folder names work.
cgltf_result ReadCb(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
  std::vector<uint8_t> bytes;
  if (!ReadFileBytes(fs::path(std::u8string((const char8_t*)path)), bytes)) return cgltf_result_file_not_found;
  void* mem = std::malloc(bytes.empty() ? 1 : bytes.size());
  if (!mem) return cgltf_result_out_of_memory;
  if (!bytes.empty()) std::memcpy(mem, bytes.data(), bytes.size());
  *data = mem;
  *size = bytes.size();
  return cgltf_result_success;
}
void ReleaseCb(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

const char* ResultName(cgltf_result r) {
  switch (r) {
    case cgltf_result_success: return "ok";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "not a glTF file";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
    default: return "error";
  }
}

bool DecodeImage(const cgltf_image* img, const fs::path& baseDir, const cgltf_options& opt, ImportedTexture& t) {
  if (img->buffer_view) { // embedded (.glb or bufferView)
    const cgltf_buffer_view* bv = img->buffer_view;
    if (!bv->buffer->data) return false;
    return DecodeImageRGBA((const uint8_t*)bv->buffer->data + bv->offset, bv->size, t.w, t.h, t.rgba);
  }
  if (!img->uri) return false;
  if (std::strncmp(img->uri, "data:", 5) == 0) {
    const char* comma = std::strchr(img->uri, ',');
    if (!comma || std::strstr(img->uri, ";base64,") == nullptr) return false;
    const char* b64 = comma + 1;
    size_t len = std::strlen(b64);
    if (len < 4) return false;
    size_t bytes = len / 4 * 3 - (b64[len - 1] == '=') - (b64[len - 2] == '='); // padding
    void* raw = nullptr;
    if (cgltf_load_buffer_base64(&opt, bytes, b64, &raw) != cgltf_result_success) return false;
    bool ok = DecodeImageRGBA((const uint8_t*)raw, bytes, t.w, t.h, t.rgba);
    std::free(raw);
    return ok;
  }
  std::string uri = img->uri;
  cgltf_decode_uri(uri.data());
  uri.resize(std::strlen(uri.c_str()));
  return LoadImageRGBA(baseDir / fs::path(std::u8string((const char8_t*)uri.c_str())), t.w, t.h, t.rgba);
}

void FillMaterial(const cgltf_data* data, const cgltf_material& m, ImportedMaterial& o, std::vector<int>& texUse) {
  auto texIndex = [&](const cgltf_texture_view& v, bool srgb) {
    if (!v.texture || !v.texture->image) return -1;
    int i = (int)(v.texture->image - data->images);
    if (texUse[i] == 0) texUse[i] = srgb ? 2 : 1; // the first use decides colour vs data
    if (const cgltf_sampler* s = v.texture->sampler) { // likewise the first sampler decides filtering and wrap
      if (s->mag_filter == 9728 /* NEAREST */) texUse[i] |= 4;
      if (s->wrap_s == 33071 /* CLAMP_TO_EDGE */ && s->wrap_t == 33071) texUse[i] |= 8;
    }
    return i;
  };
  o.name = m.name ? m.name : "";
  if (m.has_pbr_metallic_roughness) {
    const cgltf_pbr_metallic_roughness& p = m.pbr_metallic_roughness;
    std::memcpy(o.baseColor, p.base_color_factor, sizeof(o.baseColor));
    o.metallic = p.metallic_factor;
    o.roughness = p.roughness_factor;
    o.baseColorTex = texIndex(p.base_color_texture, true);
    o.metalRoughTex = texIndex(p.metallic_roughness_texture, false);
  }
  float strength = m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.f;
  for (int k = 0; k < 3; ++k) o.emissive[k] = m.emissive_factor[k] * strength;
  o.emissiveTex = texIndex(m.emissive_texture, true);
  o.normalTex = texIndex(m.normal_texture, false);
  o.normalScale = m.normal_texture.scale;
  o.occlusionTex = texIndex(m.occlusion_texture, false);
  o.occlusionStrength = m.occlusion_texture.scale;
  o.alpha = m.alpha_mode == cgltf_alpha_mode_mask ? ImportedMaterial::Alpha::Mask
          : m.alpha_mode == cgltf_alpha_mode_blend ? ImportedMaterial::Alpha::Blend : ImportedMaterial::Alpha::Opaque;
  o.alphaCutoff = m.alpha_cutoff;
  o.doubleSided = m.double_sided;
  o.unlit = m.unlit;
}

// Smooth normals from the triangles (area weighted) when the file has none.
void ComputeNormals(MeshData& m) {
  for (Vertex3D& v : m.vertices) v.nx = v.ny = v.nz = 0;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    Vertex3D &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
    glm::vec3 n = glm::cross(glm::vec3(b.px - a.px, b.py - a.py, b.pz - a.pz), glm::vec3(c.px - a.px, c.py - a.py, c.pz - a.pz));
    for (Vertex3D* v : {&a, &b, &c}) { v->nx += n.x; v->ny += n.y; v->nz += n.z; }
  }
  for (Vertex3D& v : m.vertices) {
    glm::vec3 n(v.nx, v.ny, v.nz);
    float l = glm::length(n);
    n = l > 0 ? n / l : glm::vec3(0, 1, 0);
    v.nx = n.x; v.ny = n.y; v.nz = n.z;
  }
}

// 7-1: JOINTS_0/1 + WEIGHTS_0/1 of a primitive whose mesh a skin deforms, joints mapped to skeleton indices.
void ReadSkin(const cgltf_data* data, const cgltf_primitive& p, const cgltf_skin* skin, const std::vector<int>& nodeJoint, size_t n,
              std::vector<SkinVertex>& out) {
  const cgltf_accessor *jo[2] = {}, *we[2] = {};
  for (cgltf_size i = 0; i < p.attributes_count; ++i) {
    const cgltf_attribute& a = p.attributes[i];
    if (a.index < 0 || a.index > 1) continue;
    if (a.type == cgltf_attribute_type_joints && a.data->count == n) jo[a.index] = a.data;
    if (a.type == cgltf_attribute_type_weights && a.data->count == n) we[a.index] = a.data;
  }
  if (!skin || !jo[0] || !we[0]) return;
  out.resize(n);
  for (size_t i = 0; i < n; ++i) {
    int joints[8];
    float weights[8];
    int count = 0;
    for (int set = 0; set < 2; ++set) {
      if (!jo[set] || !we[set]) continue;
      cgltf_uint u[4] = {0, 0, 0, 0};
      float w[4] = {0, 0, 0, 0};
      cgltf_accessor_read_uint(jo[set], i, u, 4);
      cgltf_accessor_read_float(we[set], i, w, 4);
      for (int k = 0; k < 4; ++k) {
        joints[count] = u[k] < skin->joints_count ? nodeJoint[skin->joints[u[k]] - data->nodes] : -1;
        weights[count++] = w[k];
      }
    }
    out[i] = MakeSkinVertex(joints, weights, count);
  }
}

bool ReadPrimitive(const cgltf_primitive& p, MeshData& m) {
  const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *tan = nullptr;
  for (cgltf_size i = 0; i < p.attributes_count; ++i) {
    const cgltf_attribute& a = p.attributes[i];
    if (a.type == cgltf_attribute_type_position && a.index == 0) pos = a.data;
    if (a.type == cgltf_attribute_type_normal && a.index == 0) nrm = a.data;
    if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
    if (a.type == cgltf_attribute_type_tangent && a.index == 0) tan = a.data;
  }
  if (!pos || pos->count == 0) return false;
  size_t n = pos->count;
  m.vertices.assign(n, Vertex3D{0, 0, 0, 0, 1, 0, 0, 0});
  std::vector<float> f;
  auto unpack = [&](const cgltf_accessor* a, size_t comps) {
    if (!a || a->count != n || cgltf_num_components(a->type) != comps) return false;
    f.resize(n * comps);
    return cgltf_accessor_unpack_floats(a, f.data(), f.size()) == f.size();
  };
  if (!unpack(pos, 3)) return false;
  for (size_t i = 0; i < n; ++i) { m.vertices[i].px = f[i * 3]; m.vertices[i].py = f[i * 3 + 1]; m.vertices[i].pz = f[i * 3 + 2]; }
  bool hasNormals = unpack(nrm, 3);
  if (hasNormals)
    for (size_t i = 0; i < n; ++i) { m.vertices[i].nx = f[i * 3]; m.vertices[i].ny = f[i * 3 + 1]; m.vertices[i].nz = f[i * 3 + 2]; }
  if (unpack(uv, 2))
    for (size_t i = 0; i < n; ++i) { m.vertices[i].u = f[i * 2]; m.vertices[i].v = f[i * 2 + 1]; }
  if (hasNormals && unpack(tan, 4)) // tangents are only meaningful next to the file's own normals
    for (size_t i = 0; i < n; ++i) {
      Vertex3D& v = m.vertices[i];
      v.tx = f[i * 4]; v.ty = f[i * 4 + 1]; v.tz = f[i * 4 + 2]; v.tw = f[i * 4 + 3] < 0 ? -1.f : 1.f;
    }
  if (p.indices) {
    m.indices.resize(p.indices->count);
    for (size_t i = 0; i < m.indices.size(); ++i) {
      size_t idx = cgltf_accessor_read_index(p.indices, i);
      if (idx >= n) return false;
      m.indices[i] = (uint32_t)idx;
    }
  } else {
    m.indices.resize(n);
    for (size_t i = 0; i < n; ++i) m.indices[i] = (uint32_t)i;
  }
  m.indices.resize(m.indices.size() / 3 * 3);
  if (!hasNormals) ComputeNormals(m);
  return !m.indices.empty();
}

// Dedup + vertex cache + overdraw + fetch order. Returns {ACMR before, after} (FIFO cache of 16).
std::pair<float, float> Optimize(MeshData& m) {
  size_t ic = m.indices.size();
  float before = meshopt_analyzeVertexCache(m.indices.data(), ic, m.vertices.size(), 16, 0, 0).acmr;
  std::vector<unsigned int> remap(m.vertices.size());
  size_t vc = meshopt_generateVertexRemap(remap.data(), m.indices.data(), ic, m.vertices.data(), m.vertices.size(), sizeof(Vertex3D));
  std::vector<uint32_t> idx(ic);
  meshopt_remapIndexBuffer(idx.data(), m.indices.data(), ic, remap.data());
  std::vector<Vertex3D> verts(vc);
  meshopt_remapVertexBuffer(verts.data(), m.vertices.data(), m.vertices.size(), sizeof(Vertex3D), remap.data());
  meshopt_optimizeVertexCache(idx.data(), idx.data(), ic, vc);
  meshopt_optimizeOverdraw(idx.data(), idx.data(), ic, &verts[0].px, vc, sizeof(Vertex3D), 1.05f);
  meshopt_optimizeVertexFetch(verts.data(), idx.data(), ic, verts.data(), vc, sizeof(Vertex3D));
  m.indices = std::move(idx);
  m.vertices = std::move(verts);
  float after = meshopt_analyzeVertexCache(m.indices.data(), ic, m.vertices.size(), 16, 0, 0).acmr;
  return {before, after};
}

// Same passes with the skin stream remapped alongside the vertices.
std::pair<float, float> OptimizeSkinned(MeshData& m, std::vector<SkinVertex>& skin) {
  size_t ic = m.indices.size();
  float before = meshopt_analyzeVertexCache(m.indices.data(), ic, m.vertices.size(), 16, 0, 0).acmr;
  const meshopt_Stream streams[2] = {{m.vertices.data(), sizeof(Vertex3D), sizeof(Vertex3D)}, {skin.data(), sizeof(SkinVertex), sizeof(SkinVertex)}};
  std::vector<unsigned int> remap(m.vertices.size());
  size_t vc = meshopt_generateVertexRemapMulti(remap.data(), m.indices.data(), ic, m.vertices.size(), streams, 2);
  std::vector<uint32_t> idx(ic);
  meshopt_remapIndexBuffer(idx.data(), m.indices.data(), ic, remap.data());
  std::vector<Vertex3D> verts(vc);
  std::vector<SkinVertex> sk(vc);
  meshopt_remapVertexBuffer(verts.data(), m.vertices.data(), m.vertices.size(), sizeof(Vertex3D), remap.data());
  meshopt_remapVertexBuffer(sk.data(), skin.data(), skin.size(), sizeof(SkinVertex), remap.data());
  meshopt_optimizeVertexCache(idx.data(), idx.data(), ic, vc);
  meshopt_optimizeOverdraw(idx.data(), idx.data(), ic, &verts[0].px, vc, sizeof(Vertex3D), 1.05f);
  std::vector<unsigned int> fetch(vc);
  const size_t used = meshopt_optimizeVertexFetchRemap(fetch.data(), idx.data(), ic, vc);
  meshopt_remapIndexBuffer(idx.data(), idx.data(), ic, fetch.data());
  m.vertices.resize(used);
  skin.resize(used);
  meshopt_remapVertexBuffer(m.vertices.data(), verts.data(), vc, sizeof(Vertex3D), fetch.data());
  meshopt_remapVertexBuffer(skin.data(), sk.data(), vc, sizeof(SkinVertex), fetch.data());
  m.indices = std::move(idx);
  float after = meshopt_analyzeVertexCache(m.indices.data(), ic, m.vertices.size(), 16, 0, 0).acmr;
  return {before, after};
}

void Decompose(const glm::mat4& m, glm::vec3& t, glm::quat& r, glm::vec3& s) {
  t = glm::vec3(m[3]);
  s = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
  glm::mat3 rot(glm::vec3(m[0]) / std::max(s.x, 1e-12f), glm::vec3(m[1]) / std::max(s.y, 1e-12f), glm::vec3(m[2]) / std::max(s.z, 1e-12f));
  if (glm::determinant(rot) < 0) { s.x = -s.x; rot[0] = -rot[0]; }
  r = glm::normalize(glm::quat_cast(rot));
}

// 7-1: the skeleton is every node a skin names plus their ancestors (an animated armature node moves the whole rig),
// parents first. inverseBind comes from the first skin naming the joint.
void BuildSkeleton(const cgltf_data* data, std::vector<int>& nodeJoint, Skeleton& sk) {
  nodeJoint.assign(data->nodes_count, -1);
  std::vector<char> want(data->nodes_count, 0);
  for (cgltf_size si = 0; si < data->skins_count; ++si)
    for (cgltf_size k = 0; k < data->skins[si].joints_count; ++k)
      for (const cgltf_node* n = data->skins[si].joints[k]; n; n = n->parent) want[n - data->nodes] = 1;
  std::function<void(const cgltf_node*, int)> visit = [&](const cgltf_node* n, int parent) {
    const size_t idx = (size_t)(n - data->nodes);
    int self = parent;
    if (want[idx] && nodeJoint[idx] < 0) {
      Joint j;
      j.name = n->name ? n->name : std::format("joint{}", idx);
      j.parent = parent;
      if (n->has_matrix) Decompose(glm::make_mat4(n->matrix), j.t, j.r, j.s);
      else {
        if (n->has_translation) j.t = {n->translation[0], n->translation[1], n->translation[2]};
        if (n->has_rotation) j.r = glm::normalize(glm::quat(n->rotation[3], n->rotation[0], n->rotation[1], n->rotation[2]));
        if (n->has_scale) j.s = {n->scale[0], n->scale[1], n->scale[2]};
      }
      self = nodeJoint[idx] = (int)sk.joints.size();
      sk.joints.push_back(std::move(j));
    }
    for (cgltf_size c = 0; c < n->children_count; ++c) visit(n->children[c], self);
  };
  for (cgltf_size i = 0; i < data->nodes_count; ++i)
    if (!data->nodes[i].parent) visit(&data->nodes[i], -1);
  for (cgltf_size si = 0; si < data->skins_count; ++si) {
    const cgltf_skin& skin = data->skins[si];
    for (cgltf_size k = 0; k < skin.joints_count; ++k) {
      const int j = nodeJoint[skin.joints[k] - data->nodes];
      if (j < 0 || sk.joints[j].skinned) continue;
      sk.joints[j].skinned = true;
      float f[16];
      if (skin.inverse_bind_matrices && cgltf_accessor_read_float(skin.inverse_bind_matrices, k, f, 16)) sk.joints[j].inverseBind = glm::make_mat4(f);
    }
  }
  int best = -1, bestDepth = 1 << 30;
  for (size_t j = 0; j < sk.joints.size(); ++j) {
    if (!sk.joints[j].skinned) continue;
    int depth = 0;
    for (int p = sk.joints[j].parent; p >= 0; p = sk.joints[p].parent) ++depth;
    if (depth < bestDepth) { best = (int)j; bestDepth = depth; }
  }
  sk.root = best;
}

// 7-1: animations on skeleton joints (translation, rotation, scale; morph weights and other nodes are not read).
// CUBICSPLINE keys keep their values and play linearly.
void ReadClips(const cgltf_data* data, const std::vector<int>& nodeJoint, std::vector<AnimClip>& clips) {
  size_t skipped = 0;
  for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
    const cgltf_animation& a = data->animations[ai];
    AnimClip clip;
    clip.name = a.name && *a.name ? a.name : std::format("Clip{}", ai);
    for (cgltf_size ci = 0; ci < a.channels_count; ++ci) {
      const cgltf_animation_channel& ch = a.channels[ci];
      const int j = ch.target_node ? nodeJoint[ch.target_node - data->nodes] : -1;
      const cgltf_animation_sampler* smp = ch.sampler;
      if (j < 0 || !smp || !smp->input || !smp->output || ch.target_path == cgltf_animation_path_type_weights ||
          ch.target_path == cgltf_animation_path_type_invalid) { ++skipped; continue; }
      AnimChannel c;
      c.joint = j;
      c.path = ch.target_path == cgltf_animation_path_type_rotation ? AnimChannel::Rotation
             : ch.target_path == cgltf_animation_path_type_scale ? AnimChannel::Scale : AnimChannel::Translation;
      c.step = smp->interpolation == cgltf_interpolation_type_step;
      const bool cubic = smp->interpolation == cgltf_interpolation_type_cubic_spline;
      const size_t n = smp->input->count, comps = c.path == AnimChannel::Rotation ? 4 : 3;
      if (smp->output->count < (cubic ? n * 3 : n)) { ++skipped; continue; }
      c.times.resize(n);
      c.values.resize(n, glm::vec4(0.f));
      for (size_t k = 0; k < n; ++k) {
        cgltf_accessor_read_float(smp->input, k, &c.times[k], 1);
        float v[4] = {0, 0, 0, 1};
        cgltf_accessor_read_float(smp->output, cubic ? k * 3 + 1 : k, v, comps);
        c.values[k] = {v[0], v[1], v[2], comps == 4 ? v[3] : 0.f};
      }
      if (n) clip.duration = std::max(clip.duration, c.times.back());
      clip.channels.push_back(std::move(c));
    }
    if (!clip.channels.empty()) clips.push_back(std::move(clip));
  }
  if (skipped) LogInfo("gltf: {} animation channels skipped (not on a skeleton joint, or morph weights)", skipped);
}

void Walk(const cgltf_data* data, const cgltf_node* node, const std::vector<int>& meshFirst, ImportedModel& out) {
  if (node->mesh) {
    float world[16];
    cgltf_node_transform_world(node, world);
    glm::mat4 m = glm::make_mat4(world);
    int first = meshFirst[node->mesh - data->meshes];
    for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
      if (first + (int)p < (int)out.primitives.size() && !out.primitives[first + p].mesh.indices.empty()) {
        const bool skinned = node->skin && !out.primitives[first + p].skin.empty(); // the joints place it; the node is ignored (glTF)
        out.instances.push_back({first + (int)p, skinned ? glm::mat4(1.f) : m, node->name ? node->name : ""});
      }
  }
  for (cgltf_size c = 0; c < node->children_count; ++c) Walk(data, node->children[c], meshFirst, out);
}

bool Import(cgltf_data* data, const fs::path& baseDir, const cgltf_options& opt, ImportedModel& out, std::string* error, bool images) {
  out = {};
  std::vector<int> texUse(data->images_count, 0); // 0 unused, 1 data, 2 colour; | 4 nearest, | 8 clamp
  out.materials.resize(data->materials_count);
  for (cgltf_size i = 0; i < data->materials_count; ++i) FillMaterial(data, data->materials[i], out.materials[i], texUse);

  out.textures.resize(data->images_count);
  for (cgltf_size i = 0; i < data->images_count; ++i) {
    ImportedTexture& t = out.textures[i];
    t.name = data->images[i].name ? data->images[i].name : data->images[i].uri && std::strncmp(data->images[i].uri, "data:", 5) ? data->images[i].uri : std::format("image{}", i);
    t.srgb = (texUse[i] & 3) == 2;
    t.nearest = (texUse[i] & 4) != 0;
    t.clamp = (texUse[i] & 8) != 0;
    if (!images) continue; // skeletons and clips only: the materials keep their texture indices
    if ((texUse[i] & 3) && !DecodeImage(&data->images[i], baseDir, opt, t)) {
      LogWarn("gltf: image '{}' could not be decoded; the material uses its factors only", t.name);
      t.rgba.clear();
      t.w = t.h = 0;
    }
  }
  auto dropMissing = [&](int& tex) { if (tex >= 0 && !out.textures[tex].Valid()) tex = -1; };
  if (images)
    for (ImportedMaterial& m : out.materials)
      for (int* t : {&m.baseColorTex, &m.metalRoughTex, &m.normalTex, &m.occlusionTex, &m.emissiveTex}) dropMissing(*t);

  std::vector<int> nodeJoint;
  std::vector<const cgltf_skin*> meshSkin(data->meshes_count, nullptr);
  if (data->skins_count) {
    out.skinned = std::make_shared<SkinnedModelData>();
    BuildSkeleton(data, nodeJoint, out.skinned->skeleton);
    ReadClips(data, nodeJoint, out.skinned->clips);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
      const cgltf_node& n = data->nodes[i];
      if (n.mesh && n.skin && !meshSkin[n.mesh - data->meshes]) meshSkin[n.mesh - data->meshes] = n.skin;
    }
  }

  std::vector<int> meshFirst(data->meshes_count);
  double cacheBefore = 0, cacheAfter = 0;
  for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
    meshFirst[mi] = (int)out.primitives.size();
    for (cgltf_size pi = 0; pi < data->meshes[mi].primitives_count; ++pi) {
      const cgltf_primitive& p = data->meshes[mi].primitives[pi];
      ImportedPrimitive ip;
      ip.material = p.material ? (int)(p.material - data->materials) : -1;
      if (p.type != cgltf_primitive_type_triangles || p.has_draco_mesh_compression || !ReadPrimitive(p, ip.mesh)) {
        ++out.stats.skippedPrimitives;
        ip.mesh = {};
      } else {
        out.stats.sourceVertices += ip.mesh.vertices.size();
        if (meshSkin[mi]) ReadSkin(data, p, meshSkin[mi], nodeJoint, ip.mesh.vertices.size(), ip.skin);
        auto [b, a] = ip.skin.empty() ? Optimize(ip.mesh) : OptimizeSkinned(ip.mesh, ip.skin);
        cacheBefore += b * ip.mesh.indices.size();
        cacheAfter += a * ip.mesh.indices.size();
        out.stats.vertices += ip.mesh.vertices.size();
        out.stats.triangles += ip.mesh.indices.size() / 3;
      }
      out.primitives.push_back(std::move(ip));
    }
  }
  if (out.stats.triangles) {
    out.stats.acmrBefore = (float)(cacheBefore / (out.stats.triangles * 3.0));
    out.stats.acmrAfter = (float)(cacheAfter / (out.stats.triangles * 3.0));
  }

  const cgltf_scene* scene = data->scene ? data->scene : data->scenes_count ? &data->scenes[0] : nullptr;
  if (scene)
    for (cgltf_size i = 0; i < scene->nodes_count; ++i) Walk(data, scene->nodes[i], meshFirst, out);
  else
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
      if (!data->nodes[i].parent) Walk(data, &data->nodes[i], meshFirst, out);

  ComputeModelBounds(out);
  if (out.instances.empty()) {
    if (error) *error = "no triangle meshes in the default scene";
    return false;
  }
  return true;
}
}

void ComputeModelBounds(ImportedModel& out) {
  std::vector<glm::mat4> skin;
  if (out.skinned) {
    LocalPose pose;
    RestPose(out.skinned->skeleton, pose);
    std::vector<glm::mat4> model;
    PoseToModel(out.skinned->skeleton, pose, model);
    ModelToSkin(out.skinned->skeleton, model, skin);
  }
  bool any = false;
  for (const ImportedModel::Instance& in : out.instances) {
    const ImportedPrimitive& prim = out.primitives[in.primitive];
    const bool skinned = !prim.skin.empty() && !skin.empty();
    for (size_t i = 0; i < prim.mesh.vertices.size(); ++i) {
      const Vertex3D& v = prim.mesh.vertices[i];
      glm::mat4 m = in.world;
      if (skinned) {
        glm::mat4 blend(0.f);
        for (int k = 0; k < 4; ++k)
          if (prim.skin[i].joint[k] < skin.size()) blend += skin[prim.skin[i].joint[k]] * prim.skin[i].weight[k];
        m = in.world * blend;
      }
      const glm::vec3 p = glm::vec3(m * glm::vec4(v.px, v.py, v.pz, 1.f));
      out.boundsMin = any ? glm::min(out.boundsMin, p) : p;
      out.boundsMax = any ? glm::max(out.boundsMax, p) : p;
      any = true;
    }
  }
}

bool ImportGltfMemory(const void* bytes, size_t size, const fs::path& baseDir, ImportedModel& out, std::string* error, bool images) {
  cgltf_options opt{};
  opt.file.read = ReadCb;
  opt.file.release = ReleaseCb;
  cgltf_data* data = nullptr;
  cgltf_result r = cgltf_parse(&opt, bytes, size, &data);
  if (r == cgltf_result_success) {
    // cgltf_load_buffers joins relative URIs with the directory of this path (the file name itself is ignored).
    std::u8string base = (baseDir / "model.gltf").u8string();
    r = cgltf_load_buffers(&opt, data, (const char*)base.c_str());
  }
  if (r == cgltf_result_success) r = cgltf_validate(data);
  bool ok = r == cgltf_result_success && Import(data, baseDir, opt, out, error, images);
  if (r != cgltf_result_success && error) *error = ResultName(r);
  cgltf_free(data);
  return ok;
}

std::pair<float, float> OptimizeMesh(MeshData& m) { return Optimize(m); }
std::pair<float, float> OptimizeMesh(MeshData& m, std::vector<SkinVertex>& skin) { return OptimizeSkinned(m, skin); }

bool ImportGltf(const fs::path& path, ImportedModel& out, std::string* error, bool images) {
  std::vector<uint8_t> bytes;
  if (!ReadFileBytes(path, bytes)) {
    if (error) *error = "file not found";
    return false;
  }
  bool ok = ImportGltfMemory(bytes.data(), bytes.size(), path.parent_path(), out, error, images);
  if (ok && out.skinned)
    LogInfo("gltf: {} -> skeleton of {} joints, {} clips", std::string((const char*)path.filename().u8string().c_str()), out.skinned->skeleton.joints.size(),
            out.skinned->clips.size());
  if (ok)
    LogInfo("gltf: {} -> {} primitives, {} materials, {} textures, {} triangles, vertices {} -> {} (dedup), ACMR {:.2f} -> {:.2f}",
            std::string((const char*)path.filename().u8string().c_str()), out.primitives.size(), out.materials.size(), out.textures.size(), out.stats.triangles,
            out.stats.sourceVertices, out.stats.vertices, out.stats.acmrBefore, out.stats.acmrAfter);
  return ok;
}
}
