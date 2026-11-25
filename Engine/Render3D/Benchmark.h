#pragma once
// Device benchmark for the automatic quality tier (queue 3-5c): a fixed scene (a 20 x 20 field of boxes on a
// ground, a sun with shadows, 64 point lights of which 4 cast shadows, the sky) rendered offscreen at the high tier,
// 1920 x 1080, timed from submit to finished picture. TierFromBenchmark (Render3D/Quality.h) turns the time into a tier.
#include "Render3D/Renderer3D.h"

namespace bdg {
struct BenchmarkResult {
  double highMs = 0; // per frame at high quality (median of the timed rounds)
  int frames = 0;
  int32_t w = 0, h = 0;
};
bool RunQualityBenchmark(RHI& rhi, Renderer3D& renderer, BenchmarkResult& out, int frames = 12, int32_t w = 1920, int32_t h = 1080);
}
