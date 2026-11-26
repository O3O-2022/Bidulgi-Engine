#include "Net/NetCodec.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace bdg::net {

// ---- bits ----
void BitWriter::Bits(uint32_t value, int count) {
  for (int i = 0; i < count; ++i) {
    if (bit_ == 0) bytes_.push_back(0);
    if ((value >> i) & 1) bytes_.back() |= (uint8_t)(1u << bit_);
    bit_ = (bit_ + 1) & 7;
  }
}
void BitWriter::Align() { bit_ = 0; }
void BitWriter::VarUint(uint64_t v) {
  Align();
  do {
    uint8_t b = v & 0x7f;
    v >>= 7;
    if (v) b |= 0x80;
    bytes_.push_back(b);
  } while (v);
}
void BitWriter::VarInt(int64_t v) { VarUint(((uint64_t)v << 1) ^ (uint64_t)(v >> 63)); }
void BitWriter::Float(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  Bits(u, 32);
}
void BitWriter::String(const std::string& s) {
  VarUint(s.size());
  bytes_.insert(bytes_.end(), s.begin(), s.end());
}
void BitWriter::Bytes(const uint8_t* data, size_t n) {
  Align();
  bytes_.insert(bytes_.end(), data, data + n);
}

uint32_t BitReader::Bits(int count) {
  uint32_t v = 0;
  for (int i = 0; i < count; ++i) {
    if (pos_ >= size_ * 8) { overflow_ = true; return 0; }
    if ((data_[pos_ >> 3] >> (pos_ & 7)) & 1) v |= 1u << i;
    ++pos_;
  }
  return v;
}
void BitReader::Align() { pos_ = (pos_ + 7) & ~(size_t)7; }
uint64_t BitReader::VarUint() {
  Align();
  uint64_t v = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (pos_ >= size_ * 8) { overflow_ = true; return 0; }
    const uint8_t b = data_[pos_ >> 3];
    pos_ += 8;
    v |= (uint64_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) return v;
  }
  overflow_ = true;
  return v;
}
int64_t BitReader::VarInt() {
  const uint64_t u = VarUint();
  return (int64_t)(u >> 1) ^ -(int64_t)(u & 1);
}
float BitReader::Float() {
  const uint32_t u = Bits(32);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
std::string BitReader::String() {
  const uint64_t n = VarUint();
  if (overflow_ || pos_ / 8 + n > size_) { overflow_ = true; return {}; }
  std::string s(reinterpret_cast<const char*>(data_) + pos_ / 8, (size_t)n);
  pos_ += n * 8;
  return s;
}
bool BitReader::Bytes(uint8_t* out, size_t n) {
  Align();
  if (pos_ / 8 + n > size_) { overflow_ = true; return false; }
  std::memcpy(out, data_ + pos_ / 8, n);
  pos_ += n * 8;
  return true;
}

// ---- quantization ----
int32_t QuantizePosition(float v, float step) { return (int32_t)std::lround(v / step); }
float DequantizePosition(int32_t q, float step) { return q * step; }

uint32_t PackQuat(const glm::quat& qin) {
  glm::quat q = glm::normalize(qin);
  const float c[4] = {q.x, q.y, q.z, q.w};
  int big = 0;
  for (int i = 1; i < 4; ++i)
    if (std::fabs(c[i]) > std::fabs(c[big])) big = i;
  const float sign = c[big] < 0 ? -1.f : 1.f;
  uint32_t out = (uint32_t)big;
  int shift = 2;
  constexpr float kRange = 0.70710678f; // the three smaller ones lie in [-1/sqrt2, 1/sqrt2]
  for (int i = 0; i < 4; ++i) {
    if (i == big) continue;
    const float v = std::clamp(c[i] * sign / kRange, -1.f, 1.f);
    const uint32_t u = (uint32_t)std::lround((v * 0.5f + 0.5f) * 1023.f);
    out |= u << shift;
    shift += 10;
  }
  return out;
}
glm::quat UnpackQuat(uint32_t p) {
  const int big = (int)(p & 3);
  float c[4];
  float sum = 0;
  int shift = 2;
  constexpr float kRange = 0.70710678f;
  for (int i = 0; i < 4; ++i) {
    if (i == big) continue;
    const float v = (((p >> shift) & 1023u) / 1023.f * 2.f - 1.f) * kRange;
    c[i] = v;
    sum += v * v;
    shift += 10;
  }
  c[big] = std::sqrt(std::max(0.f, 1.f - sum));
  return glm::normalize(glm::quat(c[3], c[0], c[1], c[2]));
}

NetTransform Quantized(const NetTransform& t) {
  NetTransform q;
  for (int k = 0; k < 3; ++k) {
    q.position[k] = DequantizePosition(QuantizePosition(t.position[k]));
    q.scale[k] = DequantizePosition(QuantizePosition(t.scale[k]));
  }
  q.rotation = UnpackQuat(PackQuat(t.rotation));
  return q;
}

void WriteTransformDelta(BitWriter& w, const NetTransform& base, const NetTransform& now) {
  int32_t bp[3], np[3], bs[3], ns[3];
  for (int k = 0; k < 3; ++k) {
    bp[k] = QuantizePosition(base.position[k]);
    np[k] = QuantizePosition(now.position[k]);
    bs[k] = QuantizePosition(base.scale[k]);
    ns[k] = QuantizePosition(now.scale[k]);
  }
  const bool pos = bp[0] != np[0] || bp[1] != np[1] || bp[2] != np[2];
  const bool rot = PackQuat(base.rotation) != PackQuat(now.rotation);
  const bool scl = bs[0] != ns[0] || bs[1] != ns[1] || bs[2] != ns[2];
  w.Bool(pos);
  w.Bool(rot);
  w.Bool(scl);
  if (rot) w.Bits(PackQuat(now.rotation), 32);
  if (pos)
    for (int k = 0; k < 3; ++k) w.VarInt((int64_t)np[k] - bp[k]);
  if (scl)
    for (int k = 0; k < 3; ++k) w.VarInt((int64_t)ns[k] - bs[k]);
}

NetTransform ReadTransformDelta(BitReader& r, const NetTransform& base) {
  NetTransform t = Quantized(base);
  const bool pos = r.Bool(), rot = r.Bool(), scl = r.Bool();
  if (rot) t.rotation = UnpackQuat(r.Bits(32));
  if (pos)
    for (int k = 0; k < 3; ++k) t.position[k] = DequantizePosition((int32_t)(QuantizePosition(base.position[k]) + r.VarInt()));
  if (scl)
    for (int k = 0; k < 3; ++k) t.scale[k] = DequantizePosition((int32_t)(QuantizePosition(base.scale[k]) + r.VarInt()));
  return t;
}

void WriteTransform(BitWriter& w, const NetTransform& t) { WriteTransformDelta(w, NetTransform{}, t); }
NetTransform ReadTransform(BitReader& r) { return ReadTransformDelta(r, NetTransform{}); }

// ---- blobs ----
std::vector<uint8_t> DeltaBytes(const std::vector<uint8_t>& base, const std::vector<uint8_t>& now) {
  BitWriter w;
  if (base.size() != now.size()) { // whole
    w.VarUint(0);
    w.VarUint(now.size());
    w.Bytes(now.data(), now.size());
    return w.Data();
  }
  std::vector<std::pair<size_t, size_t>> spans;
  for (size_t i = 0; i < now.size();) {
    if (base[i] == now[i]) { ++i; continue; }
    size_t j = i;
    while (j < now.size()) { // the span goes on over gaps of fewer than 3 equal bytes (a new span costs more)
      if (base[j] != now[j]) { ++j; continue; }
      size_t k = j;
      while (k < now.size() && k < j + 3 && base[k] == now[k]) ++k;
      if (k < now.size() && k < j + 3) j = k;
      else break;
    }
    spans.push_back({i, j - i});
    i = j;
  }
  w.VarUint(spans.size() + 1);
  size_t last = 0;
  for (auto& [at, n] : spans) {
    w.VarUint(at - last);
    w.VarUint(n);
    w.Bytes(now.data() + at, n);
    last = at + n;
  }
  return w.Data();
}

bool ApplyDeltaBytes(const std::vector<uint8_t>& base, const std::vector<uint8_t>& delta, std::vector<uint8_t>& out) {
  BitReader r(delta);
  const uint64_t count = r.VarUint();
  if (count == 0) {
    const uint64_t n = r.VarUint();
    out.resize((size_t)n);
    return n <= delta.size() && r.Bytes(out.data(), (size_t)n) && !r.Overflowed();
  }
  out = base;
  size_t at = 0;
  for (uint64_t s = 1; s < count; ++s) {
    at += (size_t)r.VarUint();
    const size_t n = (size_t)r.VarUint();
    if (r.Overflowed() || at + n > out.size() || !r.Bytes(out.data() + at, n)) return false;
    at += n;
  }
  return !r.Overflowed();
}

// ---- interpolation ----
void SnapshotBuffer::Push(double time, const NetTransform& t) {
  auto it = std::upper_bound(items_.begin(), items_.end(), time, [](double v, const Item& i) { return v < i.time; });
  if (it != items_.begin() && (it - 1)->time == time) return; // a duplicate
  items_.insert(it, {time, t});
  while (items_.size() > 64) items_.pop_front();
}

NetTransform SnapshotBuffer::Sample(double renderTime, double maxExtrapolation) const {
  if (items_.empty()) return {};
  if (renderTime <= items_.front().time) return items_.front().t;
  for (size_t i = 1; i < items_.size(); ++i)
    if (renderTime <= items_[i].time) {
      const Item &a = items_[i - 1], &b = items_[i];
      const float f = (float)((renderTime - a.time) / std::max(b.time - a.time, 1e-9));
      return {glm::mix(a.t.position, b.t.position, f), glm::slerp(a.t.rotation, b.t.rotation, f), glm::mix(a.t.scale, b.t.scale, f)};
    }
  if (items_.size() < 2) return items_.back().t;
  const Item &a = items_[items_.size() - 2], &b = items_.back();
  const double over = std::min(renderTime - b.time, maxExtrapolation);
  const float f = (float)(over / std::max(b.time - a.time, 1e-9));
  NetTransform t = b.t;
  t.position = b.t.position + (b.t.position - a.t.position) * f;
  return t;
}

// ---- prediction ----
uint32_t Prediction::Add(const glm::vec3& delta) {
  moves_.push_back({++seq_, delta});
  position_ += delta;
  while (moves_.size() > 512) moves_.pop_front(); // the server is far behind: forget the oldest
  return seq_;
}

float Prediction::Reconcile(uint32_t ackSeq, const glm::vec3& server) {
  while (!moves_.empty() && (int32_t)(moves_.front().seq - ackSeq) <= 0) moves_.pop_front();
  glm::vec3 p = server;
  for (const Move& m : moves_) p += m.delta;
  const float err = glm::length(p - position_);
  position_ = p;
  return err;
}

} // namespace bdg::net
