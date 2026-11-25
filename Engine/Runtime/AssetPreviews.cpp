#include "Runtime/AssetPreviews.h"
#include "Assets/AssetDatabase.h"
#include "Assets/GltfImport.h"
#include "Assets/ModelImport.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Render3D/MeshGen.h"
#include "Render3D/Quality.h"
#include "Runtime/MaterialLibrary.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace bdg {
namespace fs = std::filesystem;
namespace {
constexpr int kRenderVersion = 1;   // bump when the thumbnail look changes: old cached renders are ignored
constexpr int32_t kRenderSize = 128; // rendered, then halved to the 64 px thumbnail (smoother edges)

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string ExtOf(const std::string& p) {
  size_t dot = p.find_last_of('.'), slash = p.find_last_of('/');
  return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Lower(p.substr(dot));
}

// Bottom-up RGBA rows of size x size -> top-down rows of size/2 (2x2 box)
std::vector<uint8_t> HalveFlip(const std::vector<uint8_t>& px, int32_t size) {
  const int32_t h = size / 2;
  std::vector<uint8_t> out((size_t)h * h * 4);
  for (int32_t y = 0; y < h; ++y)
    for (int32_t x = 0; x < h; ++x)
      for (int c = 0; c < 4; ++c) {
        auto at = [&](int32_t sx, int32_t sy) { return (int)px[((size_t)(size - 1 - sy) * size + sx) * 4 + c]; };
        out[((size_t)y * h + x) * 4 + c] = (uint8_t)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
      }
  return out;
}
} // namespace

AssetPreviews::Kind AssetPreviews::KindOf(const AssetRecord& r) {
  if (ThumbnailCache::Supported(r)) return Kind::Image;
  const std::string e = ExtOf(r.path);
  if (IsModelFile(e)) return Kind::Model;
  if (e == ".material" || e == ".matinst") return Kind::Material;
  return Kind::None;
}

void AssetPreviews::Init(RHI& rhi, Renderer3D& r3d, MaterialLibrary* materials) {
  rhi_ = &rhi;
  r3d_ = &r3d;
  materials_ = materials;
  sphere_ = r3d.CreateMesh(MakeSphere(0.5f, 48, 24));
  thumbRT_ = rhi.CreateRenderTarget(kRenderSize, kRenderSize, 1);
}

void AssetPreviews::SetAssets(const AssetDatabase* db, const fs::path& cacheDir) {
  db_ = db;
  cacheDir_ = cacheDir;
  images_.SetDirectory(cacheDir);
}

void AssetPreviews::Shutdown() {
  if (!rhi_) return;
  ReleaseModel();
  for (auto& [guid, e] : entries_)
    if (e.tex.Valid()) rhi_->DestroyTexture(e.tex);
  entries_.clear();
  if (liveImage_.Valid()) rhi_->DestroyTexture(liveImage_);
  r3d_->DestroyMesh(sphere_);
  if (thumbRT_.Valid()) rhi_->DestroyRenderTarget(thumbRT_);
  if (liveRT_.Valid()) rhi_->DestroyRenderTarget(liveRT_);
  rhi_ = nullptr;
}

fs::path AssetPreviews::Full(const std::string& rel) const {
  return fs::path(std::u8string((const char8_t*)db_->Root().c_str())) / fs::path(std::u8string((const char8_t*)rel.c_str()));
}

TextureHandle AssetPreviews::Thumbnail(const AssetRecord& r) {
  if (!rhi_ || KindOf(r) == Kind::None) return {};
  Entry& e = entries_[r.guid];
  if (e.hash != r.hash) { // new or changed: make it again
    if (e.tex.Valid()) rhi_->DestroyTexture(e.tex);
    e = Entry{};
    e.hash = r.hash;
  }
  if (!e.tex.Valid() && !e.failed && !e.queued) {
    e.queued = true;
    queue_.push_back(r.guid);
  }
  return e.tex;
}

int AssetPreviews::Update(int budget) {
  int made = 0;
  while (made < budget && !queue_.empty() && db_) {
    const std::string guid = queue_.front();
    queue_.pop_front();
    const AssetRecord* r = db_->ByGuid(guid);
    auto it = entries_.find(guid);
    if (!r || it == entries_.end()) continue;
    Entry& e = it->second;
    e.queued = false;
    if (e.hash != r->hash) continue; // changed since: asked again on the next draw
    std::vector<uint8_t> rgba;
    if (!Make(*r, rgba)) {
      e.failed = true;
      ++stats_.failed;
      continue;
    }
    TextureDesc td;
    td.w = td.h = ThumbnailCache::kSize;
    td.format = TextureFormat::RGBA8; // display values, like the UI
    e.tex = rhi_->CreateTexture(td, rgba.data());
    ++made;
  }
  return made;
}

bool AssetPreviews::Make(const AssetRecord& r, std::vector<uint8_t>& rgba) {
  const Kind k = KindOf(r);
  if (k == Kind::Image) {
    const bool ok = images_.Get(r, Full(r.path), rgba);
    if (ok) ++stats_.imageThumbs;
    return ok;
  }
  const fs::path cached = cacheDir_ / std::format("{}-{:016x}-r{}.png", r.guid, r.hash, kRenderVersion);
  int32_t w = 0, h = 0;
  std::error_code ec;
  if (fs::exists(cached, ec) && LoadImageRGBA(cached, w, h, rgba) && w == ThumbnailCache::kSize && h == ThumbnailCache::kSize) {
    ++stats_.fromCache;
    return true;
  }
  std::vector<uint8_t> px;
  if (!RenderToPixels(r, kRenderSize, 35, -25, 1, thumbRT_, &px)) return false;
  rgba = HalveFlip(px, kRenderSize);
  fs::create_directories(cacheDir_, ec);
  for (auto& old : fs::directory_iterator(cacheDir_, ec)) // the previous render of this asset
    if (old.path().filename().string().starts_with(r.guid + "-") && old.path() != cached && old.path().string().find("-r") != std::string::npos)
      fs::remove(old.path(), ec);
  WritePNG(cached.string().c_str(), ThumbnailCache::kSize, ThumbnailCache::kSize, rgba.data(), false);
  ++stats_.rendered;
  return true;
}

void AssetPreviews::ReleaseModel() {
  if (hasModel_) r3d_->DestroyModel(model_);
  hasModel_ = false;
  for (Renderer3D::SkinSource& s : skinSrc_) r3d_->DestroySkinSource(s);
  for (Mesh3D& m : skinTargets_) r3d_->DestroyMesh(m);
  if (joints_.Valid()) rhi_->DestroyBuffer(joints_);
  skinSrc_.clear();
  skinTargets_.clear();
  joints_ = {};
  skinned_.reset();
  liveClip_ = nullptr;
  clipStart_ = -1;
  preparedGuid_.clear();
}

bool AssetPreviews::Prepare(const AssetRecord& r) {
  if (preparedGuid_ == r.guid && preparedHash_ == r.hash) return true;
  ReleaseModel();
  const Kind k = KindOf(r);
  if (k == Kind::Model) {
    ImportedModel im;
    std::string err;
    if (!ImportModel(Full(r.path), im, &err)) {
      LogWarn("preview: {}: {}", r.path, err);
      return false;
    }
    model_ = r3d_->UploadModel(im);
    hasModel_ = true;
    if (im.skinned && !im.skinned->clips.empty()) { // 7-1: the Inspector preview plays its first clip
      skinned_ = im.skinned;
      for (const ImportedPrimitive& p : im.primitives) {
        skinSrc_.push_back(p.skin.empty() ? Renderer3D::SkinSource{} : r3d_->CreateSkinSource(p.mesh, p.skin));
        skinTargets_.push_back(p.skin.empty() ? Mesh3D{} : r3d_->CreateSkinTarget(p.mesh));
      }
      joints_ = rhi_->CreateBuffer(BufferUsage::Storage, nullptr, skinned_->skeleton.joints.size() * sizeof(glm::mat4), true);
    }
  } else if (k == Kind::Material) {
    std::string err;
    if (!materials_ || !materials_->Find(r.path, material_, &err)) {
      LogWarn("preview: {}: {}", r.path, err);
      return false;
    }
  } else {
    return false;
  }
  preparedGuid_ = r.guid;
  preparedHash_ = r.hash;
  return true;
}

bool AssetPreviews::RenderToPixels(const AssetRecord& r, int32_t size, float yaw, float pitch, float zoom, RenderTargetHandle rt,
                                   std::vector<uint8_t>* rgba, bool animate) {
  if (!Prepare(r)) return false;
  std::vector<DrawItem3D> items;
  glm::vec3 lo(-0.5f), hi(0.5f);
  liveClip_ = nullptr;
  if (hasModel_) {
    Renderer3D::AppendModel(model_, glm::mat4(1.f), items);
    lo = model_.boundsMin; // framed on the rest pose: the camera stays put while the clip plays
    hi = model_.boundsMax;
    if (animate && skinned_ && joints_.Valid()) { // 7-1: the first clip, looping on the wall clock
      const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      if (clipStart_ < 0) clipStart_ = now;
      const AnimClip& clip = skinned_->clips[0];
      liveClipTime_ = clip.duration > 0 ? (float)std::fmod(now - clipStart_, (double)clip.duration) : 0.f;
      LocalPose pose;
      RestPose(skinned_->skeleton, pose);
      SampleClip(clip, liveClipTime_, pose);
      std::vector<glm::mat4> model, skin;
      PoseToModel(skinned_->skeleton, pose, model);
      ModelToSkin(skinned_->skeleton, model, skin);
      rhi_->UpdateBuffer(joints_, skin.data(), skin.size() * sizeof(glm::mat4));
      bool any = false;
      for (size_t p = 0; p < skinSrc_.size(); ++p) any = r3d_->Skin(skinSrc_[p], joints_, skinTargets_[p]) || any;
      if (any) {
        r3d_->SkinBarrier();
        size_t k = 0; // AppendModel added the instances in order: swap the skinned ones for their posed copies
        for (const Model3D::Instance& in : model_.instances) {
          if (in.mesh < 0 || in.mesh >= (int)model_.meshes.size() || !model_.meshes[in.mesh].indexCount) continue;
          if (in.mesh < (int)skinTargets_.size() && skinTargets_[in.mesh].indexCount) items[k].mesh = &skinTargets_[in.mesh];
          ++k;
        }
        liveClip_ = &clip;
      }
    }
  } else {
    DrawItem3D it;
    it.mesh = &sphere_;
    it.material = material_;
    items.push_back(it);
  }
  if (items.empty()) return false;
  const glm::vec3 center = (lo + hi) * 0.5f;
  const float radius = std::max(glm::length(hi - lo) * 0.5f, 1e-3f);
  Camera3D cam;
  cam.fovY = 30.f;
  const float yr = glm::radians(yaw), pr = glm::radians(pitch);
  const glm::vec3 dir(std::sin(yr) * std::cos(pr), -std::sin(pr), std::cos(yr) * std::cos(pr)); // from the asset to the camera
  const float dist = radius / std::sin(glm::radians(cam.fovY * 0.5f)) * 1.05f / std::max(zoom, 0.05f);
  cam.target = center;
  cam.position = center + glm::normalize(dir) * dist;
  cam.nearZ = std::max(dist - radius * 2.f, dist * 0.01f);
  cam.farZ = dist + radius * 2.f;
  Light3D light;
  light.toLight = glm::normalize(glm::vec3(-0.4f, 0.8f, 0.6f));
  light.intensity = 2.6f;
  light.sky = {0.32f, 0.36f, 0.42f, 1};
  light.ground = {0.12f, 0.11f, 0.10f, 1};
  light.background.draw = false;
  QualitySettings q = QualityPreset(QualityTier::Medium);
  q.shadows = false;
  r3d_->Render(rt, size, size, cam, light, items, {0.16f, 0.18f, 0.21f, 1}, nullptr, &q);
  if (rgba) {
    int32_t w = 0, h = 0;
    rhi_->ReadRenderTarget(rt, w, h, *rgba);
    if (w != size || h != size) return false;
  }
  return true;
}

TextureHandle AssetPreviews::Live(const std::string& path, int32_t w, int32_t h, float yaw, float pitch, float zoom, bool& flipY) {
  flipY = false;
  liveClip_ = nullptr; // set again below when a skinned model plays (7-1)
  if (!rhi_ || !db_) return {};
  const AssetRecord* r = db_->ByPath(path);
  if (!r) return {};
  const Kind k = KindOf(*r);
  if (k == Kind::Image) {
    if (liveImageGuid_ != r->guid || liveImageHash_ != r->hash) {
      if (liveImage_.Valid()) rhi_->DestroyTexture(liveImage_);
      liveImage_ = {};
      liveImageGuid_ = r->guid;
      liveImageHash_ = r->hash;
      int32_t iw = 0, ih = 0;
      std::vector<uint8_t> px;
      if (LoadImageRGBA(Full(r->path), iw, ih, px)) {
        while (iw > 512 || ih > 512) { // halve big images (box filter) until they fit the panel
          const int32_t nw = std::max(1, iw / 2), nh = std::max(1, ih / 2);
          std::vector<uint8_t> half((size_t)nw * nh * 4);
          for (int32_t y = 0; y < nh; ++y)
            for (int32_t x = 0; x < nw; ++x)
              for (int c = 0; c < 4; ++c) {
                auto at = [&](int32_t sx, int32_t sy) { return (int)px[((size_t)std::min(sy, ih - 1) * iw + std::min(sx, iw - 1)) * 4 + c]; };
                half[((size_t)y * nw + x) * 4 + c] = (uint8_t)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
              }
          px.swap(half);
          iw = nw;
          ih = nh;
        }
        TextureDesc td;
        td.w = iw;
        td.h = ih;
        td.format = TextureFormat::RGBA8;
        liveImage_ = rhi_->CreateTexture(td, px.data());
        liveImageAspect_ = (float)iw / (float)std::max(ih, 1);
      }
    }
    liveAspect_ = liveImageAspect_;
    return liveImage_;
  }
  if (k == Kind::None) return {};
  const int32_t size = std::clamp(std::min(w, h), 16, 1024);
  if (!liveRT_.Valid() || liveW_ != size) {
    if (liveRT_.Valid()) rhi_->DestroyRenderTarget(liveRT_);
    liveRT_ = rhi_->CreateRenderTarget(size, size, 1);
    liveW_ = liveH_ = size;
  }
  if (!RenderToPixels(*r, size, yaw, pitch, zoom, liveRT_, nullptr, true)) return {};
  flipY = true;
  liveAspect_ = 1;
  return rhi_->RenderTargetColor(liveRT_);
}

void AssetPreviews::ReadLive(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba) {
  w = h = 0;
  rgba.clear();
  if (rhi_ && liveRT_.Valid()) rhi_->ReadRenderTarget(liveRT_, w, h, rgba);
}

} // namespace bdg
