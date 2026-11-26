#pragma once
// Multiplayer serialization and the snapshot math (round 7, M-2). No networking here: what goes into a packet and how
// a client turns the server's snapshots into smooth motion.
//  - BitWriter / BitReader: bits, var-ints, floats, strings; a reader never reads past the end (it flags Overflowed).
//  - Quantization: positions in fixed steps (1 mm default), rotations as "smallest three" (32 bits).
//  - Delta: a transform against the last state the receiver acknowledged (only what changed, positions as small
//    differences), and byte blobs (replicated fields) as changed spans.
//  - SnapshotBuffer: server states by server time; a client renders a little in the past (the interpolation delay)
//    and interpolates between the two states around that time (a short extrapolation when one is late).
//  - Prediction: the owner's client moves at once and remembers each move by number; when the server's state says
//    which move it has applied, the client starts from the server's position and applies the moves after it again.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace bdg::net {

class BitWriter {
public:
  void Bits(uint32_t value, int count); // 1..32 bits, low ones
  void Bool(bool b) { Bits(b ? 1 : 0, 1); }
  void VarUint(uint64_t v);             // 7 bits per byte (byte aligned)
  void VarInt(int64_t v);               // zig-zag
  void Float(float f);
  void String(const std::string& s);
  void Bytes(const uint8_t* data, size_t n);
  void Align();
  const std::vector<uint8_t>& Data() const { return bytes_; }
  size_t BitCount() const { return bytes_.size() * 8 - (bit_ ? 8 - bit_ : 0); }
private:
  std::vector<uint8_t> bytes_;
  int bit_ = 0; // bits used in the last byte (0 = full / none)
};

class BitReader {
public:
  BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
  explicit BitReader(const std::vector<uint8_t>& v) : BitReader(v.data(), v.size()) {}
  uint32_t Bits(int count);
  bool Bool() { return Bits(1) != 0; }
  uint64_t VarUint();
  int64_t VarInt();
  float Float();
  std::string String();
  bool Bytes(uint8_t* out, size_t n);
  void Align();
  bool Overflowed() const { return overflow_; }
  bool AtEnd() const { return pos_ >= size_ * 8; }
private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0; // bit position
  bool overflow_ = false;
};

// Positions: fixed steps; rotations: smallest three (2 bits index + 3 x 10 bits).
constexpr float kPositionStep = 0.001f;
int32_t QuantizePosition(float v, float step = kPositionStep);
float DequantizePosition(int32_t q, float step = kPositionStep);
uint32_t PackQuat(const glm::quat& q);
glm::quat UnpackQuat(uint32_t packed);

struct NetTransform {
  glm::vec3 position{0};
  glm::quat rotation{1, 0, 0, 0};
  glm::vec3 scale{1};
};
// What changed since `base` (both quantized the same way): a 3-bit mask, positions as var-int step differences.
void WriteTransformDelta(BitWriter& w, const NetTransform& base, const NetTransform& now);
NetTransform ReadTransformDelta(BitReader& r, const NetTransform& base);
void WriteTransform(BitWriter& w, const NetTransform& t); // full (against the identity)
NetTransform ReadTransform(BitReader& r);
NetTransform Quantized(const NetTransform& t); // what the receiver will see

// Byte blobs: the spans that differ (same length) or the whole new blob (length changed).
std::vector<uint8_t> DeltaBytes(const std::vector<uint8_t>& base, const std::vector<uint8_t>& now);
bool ApplyDeltaBytes(const std::vector<uint8_t>& base, const std::vector<uint8_t>& delta, std::vector<uint8_t>& out);

// Snapshot interpolation of one object's transform.
class SnapshotBuffer {
public:
  void Push(double serverTime, const NetTransform& t); // in any order; old ones are dropped
  // The state at `renderTime` (server time minus the interpolation delay). Before the first: the first; after the
  // last: extrapolated from the last two for at most `maxExtrapolation` seconds, then held.
  NetTransform Sample(double renderTime, double maxExtrapolation = 0.25) const;
  size_t Size() const { return items_.size(); }
  double Newest() const { return items_.empty() ? 0 : items_.back().time; }
  void Clear() { items_.clear(); }
private:
  struct Item { double time; NetTransform t; };
  std::deque<Item> items_;
};

// Client-side prediction with server reconciliation (the owner's character).
class Prediction {
public:
  struct Move { uint32_t seq; glm::vec3 delta; };
  // A local move: applied at once, remembered until the server acknowledges it. Returns its number.
  uint32_t Add(const glm::vec3& delta);
  // The server's position after applying moves up to `ackSeq`: drop those, start from the server's position and apply
  // the rest again. Returns how far the prediction was off (metres).
  float Reconcile(uint32_t ackSeq, const glm::vec3& serverPosition);
  const glm::vec3& Position() const { return position_; }
  void Reset(const glm::vec3& p) { position_ = p; moves_.clear(); }
  size_t Pending() const { return moves_.size(); }
  uint32_t LastSeq() const { return seq_; }
private:
  glm::vec3 position_{0};
  std::deque<Move> moves_;
  uint32_t seq_ = 0;
};

} // namespace bdg::net
