#pragma once
// Asset previews (queue E-1): the pictures the Project panel tiles and the Inspector show.
//   - images: ThumbnailCache (2-5t) -> a 64x64 texture
//   - models (glTF) and materials: rendered off screen (a model framed from above-front, a material on a sphere),
//     read back, shrunk to 64x64 and kept as PNG in <project>/.cache/thumbnails/<guid>-<hash>-r<version>.png,
//     so later starts only read the small file. A few per frame (Update), so a big folder never stalls the editor.
//   - Live(): the Inspector's preview of the selected asset, at panel size, turned by dragging. A skinned model (7-1)
//     plays its first clip in a loop there (GPU skinning, like the views); thumbnails show the rest pose.
// Prefabs and scenes keep their type icon for now (prefabs get a render with 6-1).
#include "Assets/Thumbnails.h"
#include "Render3D/Renderer3D.h"
#include <deque>
#include <filesystem>
#include <map>
#include <string>

namespace bdg {
class AssetDatabase;
class MaterialLibrary;
struct AssetRecord;

class AssetPreviews {
public:
  enum class Kind { None, Image, Model, Material };
  static Kind KindOf(const AssetRecord& r);

  void Init(RHI& rhi, Renderer3D& r3d, MaterialLibrary* materials);
  void SetAssets(const AssetDatabase* db, const std::filesystem::path& cacheDir);
  void Shutdown();

  // The 64x64 thumbnail texture (top row first), or an invalid handle while it is being made / for kinds without
  // one. Asking queues the work.
  TextureHandle Thumbnail(const AssetRecord& r);
  int Update(int budget = 2); // makes up to `budget` queued thumbnails; returns how many it made
  size_t Pending() const { return queue_.size(); }
  struct Stats { int rendered = 0, imageThumbs = 0, fromCache = 0, failed = 0; };
  const Stats& GetStats() const { return stats_; }

  // Inspector preview of `path` at w x h pixels. Models and materials are rendered now (orbit: yaw / pitch in
  // degrees, zoom 1 = framed); images return their texture (up to 512 px). flipY: the texture's rows are bottom-up.
  TextureHandle Live(const std::string& path, int32_t w, int32_t h, float yaw, float pitch, float zoom, bool& flipY);
  float LiveAspect() const { return liveAspect_; } // width / height of what Live() returned last
  // 7-1: the last Live() played a clip (the editor keeps drawing while it is shown); its name and time.
  bool LiveAnimating() const { return liveClip_ != nullptr; }
  std::string LiveClip() const { return liveClip_ ? liveClip_->name : std::string(); }
  float LiveClipTime() const { return liveClipTime_; }
  // Test hook: the pixels of the last Live() render target (bottom-up rows).
  void ReadLive(int32_t& w, int32_t& h, std::vector<uint8_t>& rgba);

private:
  struct Entry {
    uint64_t hash = 0;
    TextureHandle tex;
    bool queued = false, failed = false;
  };
  bool Make(const AssetRecord& r, std::vector<uint8_t>& rgba64); // the 64x64 picture (from cache or rendered)
  bool RenderToPixels(const AssetRecord& r, int32_t size, float yaw, float pitch, float zoom, RenderTargetHandle rt,
                      std::vector<uint8_t>* rgba, bool animate = false);
  bool Prepare(const AssetRecord& r); // loads the model / material for rendering into model_ / material_
  void ReleaseModel();
  std::filesystem::path Full(const std::string& rel) const;

  RHI* rhi_ = nullptr;
  Renderer3D* r3d_ = nullptr;
  MaterialLibrary* materials_ = nullptr;
  const AssetDatabase* db_ = nullptr;
  std::filesystem::path cacheDir_;
  ThumbnailCache images_;
  std::map<std::string, Entry> entries_; // guid -> thumbnail
  std::deque<std::string> queue_;        // guids
  Stats stats_;

  // what Prepare loaded (kept while the same asset is previewed)
  std::string preparedGuid_;
  uint64_t preparedHash_ = 0;
  bool hasModel_ = false;
  Model3D model_;
  // 7-1 skinned model: GPU sources and posed copies per primitive (empty = rigid), joint matrices, the clip clock
  std::shared_ptr<const SkinnedModelData> skinned_;
  std::vector<Renderer3D::SkinSource> skinSrc_;
  std::vector<Mesh3D> skinTargets_;
  BufferHandle joints_;
  double clipStart_ = -1;
  const AnimClip* liveClip_ = nullptr;
  float liveClipTime_ = 0;
  Material3D material_;
  Mesh3D sphere_;
  RenderTargetHandle thumbRT_, liveRT_;
  int32_t liveW_ = 0, liveH_ = 0;
  std::string liveImageGuid_;
  uint64_t liveImageHash_ = 0;
  TextureHandle liveImage_;
  float liveAspect_ = 1, liveImageAspect_ = 1;
};

} // namespace bdg
