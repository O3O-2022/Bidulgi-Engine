#include "Core/ImageCompare.h"
#include <algorithm>
#include <cmath>

namespace bdg {
namespace {
constexpr double kPi = 3.14159265358979323846;
// D65 white (XYZ, Y = 1)
constexpr double kXn = 0.950428545, kYn = 1.0, kZn = 1.088900371;

double SrgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

struct V3 { double x, y, z; };

V3 LinearRgbToXyz(V3 c) {
  return {0.4124564 * c.x + 0.3575761 * c.y + 0.1804375 * c.z, 0.2126729 * c.x + 0.7151522 * c.y + 0.0721750 * c.z,
          0.0193339 * c.x + 0.1191920 * c.y + 0.9503041 * c.z};
}
V3 XyzToLinearRgb(V3 c) {
  return {3.2404542 * c.x - 1.5371385 * c.y - 0.4985314 * c.z, -0.9692660 * c.x + 1.8760108 * c.y + 0.0415560 * c.z,
          0.0556434 * c.x - 0.2040259 * c.y + 1.0572252 * c.z};
}
// "Linearized" CIELab: YyCxCz (FLIP's opponent space, linear in XYZ so it can be filtered).
V3 XyzToYcc(V3 c) { return {116 * c.y / kYn - 16, 500 * (c.x / kXn - c.y / kYn), 200 * (c.y / kYn - c.z / kZn)}; }
V3 YccToXyz(V3 c) {
  double y = (c.x + 16) / 116 * kYn;
  return {(c.y / 500 + y / kYn) * kXn, y, (y / kYn - c.z / 200) * kZn};
}
double LabF(double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; }
V3 XyzToLab(V3 c) {
  double fx = LabF(c.x / kXn), fy = LabF(c.y / kYn), fz = LabF(c.z / kZn);
  return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}
double HyAB(V3 a, V3 b) { return std::fabs(a.x - b.x) + std::hypot(a.y - b.y, a.z - b.z); }
// FLIP's Hunt adjustment: chroma scaled by lightness
V3 Hunt(V3 lab) { return {lab.x, 0.01 * lab.x * lab.y, 0.01 * lab.x * lab.z}; }

// Separable convolution with clamped edges.
std::vector<double> Convolve(const std::vector<double>& src, int w, int h, const std::vector<double>& k) {
  int r = (int)k.size() / 2;
  std::vector<double> tmp(src.size()), dst(src.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int i = -r; i <= r; ++i) s += k[i + r] * src[(size_t)y * w + std::clamp(x + i, 0, w - 1)];
      tmp[(size_t)y * w + x] = s;
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int i = -r; i <= r; ++i) s += k[i + r] * tmp[(size_t)std::clamp(y + i, 0, h - 1) * w + x];
      dst[(size_t)y * w + x] = s;
    }
  return dst;
}
std::vector<double> Convolve2(const std::vector<double>& src, int w, int h, const std::vector<double>& kx, const std::vector<double>& ky) {
  int rx = (int)kx.size() / 2, ry = (int)ky.size() / 2;
  std::vector<double> tmp(src.size()), dst(src.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int i = -rx; i <= rx; ++i) s += kx[i + rx] * src[(size_t)y * w + std::clamp(x + i, 0, w - 1)];
      tmp[(size_t)y * w + x] = s;
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int i = -ry; i <= ry; ++i) s += ky[i + ry] * tmp[(size_t)std::clamp(y + i, 0, h - 1) * w + x];
      dst[(size_t)y * w + x] = s;
    }
  return dst;
}

// Contrast sensitivity as a sum of two Gaussians in the spatial domain (paper, eq. 3-4); normalised to sum 1.
std::vector<double> CsfKernel(double a1, double b1, double a2, double b2, double ppd) {
  double maxB = std::max(b1, b2);
  int r = (int)std::ceil(3 * std::sqrt(maxB / (2 * kPi * kPi)) * ppd);
  std::vector<double> k(2 * r + 1);
  double sum = 0;
  for (int i = -r; i <= r; ++i) {
    double x = i / ppd, x2 = x * x;
    double v = a1 * std::sqrt(kPi / b1) * std::exp(-kPi * kPi * x2 / b1) + a2 * std::sqrt(kPi / b2) * std::exp(-kPi * kPi * x2 / b2);
    k[i + r] = v;
    sum += v;
  }
  for (double& v : k) v /= sum;
  return k;
}
}

bool CompareImages(const uint8_t* a, const uint8_t* b, int32_t w, int32_t h, ImageDiff& out, double ppd) {
  out = {};
  if (!a || !b || w <= 0 || h <= 0) return false;
  out.w = w;
  out.h = h;
  const size_t n = (size_t)w * h;
  // 1) sRGB -> YyCxCz, three planes per image
  std::vector<double> img[2][3];
  std::vector<double> lum[2]; // normalised achromatic for the feature term
  for (int s = 0; s < 2; ++s) {
    const uint8_t* src = s == 0 ? a : b;
    for (auto& p : img[s]) p.resize(n);
    lum[s].resize(n);
    for (size_t i = 0; i < n; ++i) {
      V3 lin{SrgbToLinear(src[i * 4] / 255.0), SrgbToLinear(src[i * 4 + 1] / 255.0), SrgbToLinear(src[i * 4 + 2] / 255.0)};
      V3 ycc = XyzToYcc(LinearRgbToXyz(lin));
      img[s][0][i] = ycc.x;
      img[s][1][i] = ycc.y;
      img[s][2][i] = ycc.z;
      lum[s][i] = (ycc.x + 16) / 116; // Yy mapped back to 0..1
    }
  }
  // 2) spatial filtering by the contrast sensitivity of each channel (FLIP's constants)
  const std::vector<double> kA = CsfKernel(1, 0.0047, 0, 1e-5, ppd), kRG = CsfKernel(1, 0.0053, 0, 1e-5, ppd),
                            kBY = CsfKernel(34.1, 0.04, 13.5, 0.025, ppd);
  for (int s = 0; s < 2; ++s) {
    img[s][0] = Convolve(img[s][0], w, h, kA);
    img[s][1] = Convolve(img[s][1], w, h, kRG);
    img[s][2] = Convolve(img[s][2], w, h, kBY);
  }
  // 3) colour difference: back to linear RGB (clamped), CIELab, Hunt, HyAB, FLIP's compression
  const double pc = 0.4, pt = 0.95, qc = 0.7;
  const double cmax = std::pow(HyAB(Hunt(XyzToLab(LinearRgbToXyz({0, 1, 0}))), Hunt(XyzToLab(LinearRgbToXyz({0, 0, 1})))), qc);
  std::vector<double> colour(n);
  for (size_t i = 0; i < n; ++i) {
    V3 lab[2];
    for (int s = 0; s < 2; ++s) {
      V3 rgb = XyzToLinearRgb(YccToXyz({img[s][0][i], img[s][1][i], img[s][2][i]}));
      rgb = {std::clamp(rgb.x, 0.0, 1.0), std::clamp(rgb.y, 0.0, 1.0), std::clamp(rgb.z, 0.0, 1.0)};
      lab[s] = Hunt(XyzToLab(LinearRgbToXyz(rgb)));
    }
    double d = std::pow(HyAB(lab[0], lab[1]), qc);
    colour[i] = d < pc * cmax ? (pt / pc) * d / cmax : pt + (d - pc * cmax) / (cmax - pc * cmax) * (1 - pt);
  }
  // 4) features: edges (first derivative) and points (second derivative) of luminance, Gaussian sigma from the paper
  const double sigma = 0.5 * 0.082 * ppd;
  const int r = (int)std::ceil(3 * sigma);
  std::vector<double> g(2 * r + 1), g1(2 * r + 1), g2(2 * r + 1);
  double gs = 0, g1n = 0, g2n = 0;
  for (int i = -r; i <= r; ++i) {
    double x = i, e = std::exp(-x * x / (2 * sigma * sigma));
    g[i + r] = e;
    g1[i + r] = -x * e;
    g2[i + r] = (x * x / (sigma * sigma) - 1) * e;
    gs += e;
  }
  for (int i = -r; i <= r; ++i) { g[i + r] /= gs; g1n += g1[i + r] > 0 ? g1[i + r] : 0; g2n += g2[i + r] > 0 ? g2[i + r] : 0; }
  double g2neg = 0;
  for (int i = -r; i <= r; ++i) g2neg += g2[i + r] < 0 ? -g2[i + r] : 0;
  for (int i = -r; i <= r; ++i) {
    g1[i + r] /= g1n; // positive and negative lobes sum to +1 / -1
    g2[i + r] = g2[i + r] > 0 ? g2[i + r] / g2n : g2[i + r] / g2neg;
  }
  std::vector<double> edge[2], point[2];
  for (int s = 0; s < 2; ++s) {
    std::vector<double> ex = Convolve2(lum[s], w, h, g1, g), ey = Convolve2(lum[s], w, h, g, g1);
    std::vector<double> px = Convolve2(lum[s], w, h, g2, g), py = Convolve2(lum[s], w, h, g, g2);
    edge[s].resize(n);
    point[s].resize(n);
    for (size_t i = 0; i < n; ++i) { edge[s][i] = std::hypot(ex[i], ey[i]); point[s][i] = std::hypot(px[i], py[i]); }
  }
  // 5) combine: error = colour ^ (1 - feature)
  out.error.resize(n);
  out.heat.resize(n * 4);
  double sum = 0;
  size_t over = 0;
  for (size_t i = 0; i < n; ++i) {
    double f = std::max(std::fabs(edge[0][i] - edge[1][i]), std::fabs(point[0][i] - point[1][i]));
    double feature = std::pow(std::min(f / std::sqrt(2.0), 1.0), 0.5);
    double c = std::clamp(colour[i], 0.0, 1.0);
    double e = c <= 0 ? 0.0 : std::pow(c, 1 - feature); // same colour: no error (pow(0, 0) would say 1)
    out.error[i] = (float)e;
    sum += e;
    out.max = std::max(out.max, e);
    over += e > 0.1;
    // heat map: black -> blue -> red -> yellow -> white
    double t = std::clamp(e, 0.0, 1.0) * 4;
    const double stops[5][3] = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}};
    int k = std::min((int)t, 3);
    double u = t - k;
    for (int ch = 0; ch < 3; ++ch) out.heat[i * 4 + ch] = (uint8_t)std::lround((stops[k][ch] * (1 - u) + stops[k + 1][ch] * u) * 255);
    out.heat[i * 4 + 3] = 255;
  }
  out.mean = sum / n;
  out.over = (double)over / n;
  return true;
}
}
