#pragma once
// Perceptual image difference (queue 3-9, `bdg render compare`): the LDR FLIP metric of Andersson et al.,
// "FLIP: A Difference Evaluator for Alternating Images" (HPG 2020), implemented here from the paper. It follows the
// paper's steps (opponent colour space, contrast sensitivity filtering, HyAB colour distance with FLIP's
// compression, edge and point feature differences) but is not bit-identical to NVIDIA's reference code, so only
// compare numbers made by this engine with each other.
#include <cstdint>
#include <vector>

namespace bdg {
struct ImageDiff {
  double mean = 0;     // average per-pixel error, 0 = identical, 1 = as different as it gets
  double max = 0;
  double over = 0;     // share of pixels whose error is above 0.1
  int32_t w = 0, h = 0;
  std::vector<float> error;    // per pixel, top row first
  std::vector<uint8_t> heat;   // RGBA heat map of `error` (black, blue, red, yellow, white), top row first
};

// a, b: RGBA8 sRGB, w x h, same layout. pixelsPerDegree: viewing condition (67 = a 24" 1080p screen at 70 cm).
bool CompareImages(const uint8_t* a, const uint8_t* b, int32_t w, int32_t h, ImageDiff& out, double pixelsPerDegree = 67.0);
}
