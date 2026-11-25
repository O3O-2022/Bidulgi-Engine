#include "Build/Pak.h"
#include "Core/Image.h"
#include "Core/Utf8Path.h"
#include <zstd.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <set>

namespace bdg {
namespace {
constexpr uint32_t kVersion = 1;
constexpr size_t kHeader = 4 + 4 + 4 + 8 + 8 + 4;

bool Fail(std::string* e, std::string why) {
  if (e) *e = std::move(why);
  return false;
}
template <class T> void Put(std::vector<uint8_t>& b, T v) {
  for (size_t i = 0; i < sizeof(T); ++i) b.push_back((uint8_t)((uint64_t)v >> (8 * i)));
}
template <class T> bool Get(const std::vector<uint8_t>& b, size_t& at, T& v) {
  if (at + sizeof(T) > b.size()) return false;
  uint64_t x = 0;
  for (size_t i = 0; i < sizeof(T); ++i) x |= (uint64_t)b[at + i] << (8 * i);
  v = (T)x;
  at += sizeof(T);
  return true;
}
bool Seek(std::FILE* f, uint64_t pos) { return _fseeki64(f, (long long)pos, SEEK_SET) == 0; }

bool SafePath(const std::string& p) { // relative, no "..", no drive
  if (p.empty() || p[0] == '/' || p[0] == '\\' || p.find(':') != std::string::npos) return false;
  size_t start = 0;
  while (start <= p.size()) {
    size_t end = p.find_first_of("/\\", start);
    if (end == std::string::npos) end = p.size();
    if (p.compare(start, end - start, "..") == 0) return false;
    start = end + 1;
  }
  return true;
}
} // namespace

uint64_t Fnv1a64(const void* data, size_t size, uint64_t h) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 0x100000001b3ULL; }
  return h;
}

PakWriter::~PakWriter() { if (f_) std::fclose(f_); }

bool PakWriter::Open(const std::string& path, std::string* error) {
  std::error_code ec;
  const std::filesystem::path p = PathU8(path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
  f_ = OpenU8(path, "wb");
  if (!f_) return Fail(error, "cannot write " + path);
  const std::vector<uint8_t> zero(kHeader, 0);
  pos_ = std::fwrite(zero.data(), 1, zero.size(), f_);
  entries_.clear();
  return pos_ == kHeader || Fail(error, "cannot write " + path);
}

bool PakWriter::Add(const std::string& path, const std::vector<uint8_t>& bytes, bool compress, std::string* error) {
  if (!f_) return Fail(error, "the archive is not open");
  if (!SafePath(path)) return Fail(error, std::format("\"{}\" is not a relative path inside the game", path));
  for (const PakEntry& e : entries_)
    if (e.path == path) return Fail(error, std::format("\"{}\" is packed twice", path));
  PakEntry e;
  e.path = path;
  e.offset = pos_;
  e.size = bytes.size();
  e.hash = Fnv1a64(bytes.data(), bytes.size());
  std::vector<uint8_t> packed;
  if (compress && bytes.size() > 64) {
    packed.resize(ZSTD_compressBound(bytes.size()));
    const size_t n = ZSTD_compress(packed.data(), packed.size(), bytes.data(), bytes.size(), 9);
    if (!ZSTD_isError(n) && n < bytes.size()) {
      packed.resize(n);
      e.method = 1;
    }
  }
  const std::vector<uint8_t>& out = e.method == 1 ? packed : bytes;
  e.stored = out.size();
  if (!out.empty() && std::fwrite(out.data(), 1, out.size(), f_) != out.size()) return Fail(error, "write failed (disk full?)");
  pos_ += out.size();
  entries_.push_back(std::move(e));
  return true;
}

bool PakWriter::Finish(std::string* error) {
  if (!f_) return Fail(error, "the archive is not open");
  std::vector<uint8_t> index;
  for (const PakEntry& e : entries_) {
    Put<uint16_t>(index, (uint16_t)e.path.size());
    index.insert(index.end(), e.path.begin(), e.path.end());
    Put<uint8_t>(index, e.method);
    Put<uint64_t>(index, e.offset);
    Put<uint64_t>(index, e.stored);
    Put<uint64_t>(index, e.size);
    Put<uint64_t>(index, e.hash);
  }
  const uint64_t indexOffset = pos_;
  bool ok = index.empty() || std::fwrite(index.data(), 1, index.size(), f_) == index.size();
  pos_ += index.size();
  std::vector<uint8_t> head{'B', 'P', 'A', 'K'};
  Put<uint32_t>(head, kVersion);
  Put<uint32_t>(head, (uint32_t)entries_.size());
  Put<uint64_t>(head, indexOffset);
  Put<uint64_t>(head, (uint64_t)index.size());
  Put<uint32_t>(head, 0);
  ok = ok && Seek(f_, 0) && std::fwrite(head.data(), 1, head.size(), f_) == head.size();
  ok = std::fclose(f_) == 0 && ok;
  f_ = nullptr;
  return ok || Fail(error, "write failed (disk full?)");
}

PakReader::~PakReader() { Close(); }

void PakReader::Close() {
  if (f_) std::fclose(f_);
  f_ = nullptr;
}

bool PakReader::Open(const std::string& path, std::string* error) {
  f_ = OpenU8(path, "rb");
  if (!f_) return Fail(error, "cannot open " + path);
  std::vector<uint8_t> head(kHeader);
  if (std::fread(head.data(), 1, head.size(), f_) != head.size() || std::memcmp(head.data(), "BPAK", 4) != 0)
    return Fail(error, path + " is not a .bpak archive");
  size_t at = 4;
  uint32_t version = 0, count = 0, flags = 0;
  uint64_t indexOffset = 0, indexSize = 0;
  Get(head, at, version);
  Get(head, at, count);
  Get(head, at, indexOffset);
  Get(head, at, indexSize);
  Get(head, at, flags);
  if (version != kVersion) return Fail(error, std::format("{}: archive version {} (this engine reads {})", path, version, kVersion));
  std::vector<uint8_t> index((size_t)indexSize);
  if (!Seek(f_, indexOffset) || (indexSize && std::fread(index.data(), 1, index.size(), f_) != index.size()))
    return Fail(error, path + ": the index is cut off (incomplete download?)");
  at = 0;
  entries_.clear();
  for (uint32_t i = 0; i < count; ++i) {
    PakEntry e;
    uint16_t len = 0;
    if (!Get(index, at, len) || at + len > index.size()) return Fail(error, path + ": damaged index");
    e.path.assign((const char*)index.data() + at, len);
    at += len;
    if (!Get(index, at, e.method) || !Get(index, at, e.offset) || !Get(index, at, e.stored) || !Get(index, at, e.size) || !Get(index, at, e.hash))
      return Fail(error, path + ": damaged index");
    if (!SafePath(e.path) || e.offset + e.stored > indexOffset) return Fail(error, std::format("{}: bad entry \"{}\"", path, e.path));
    entries_.push_back(std::move(e));
  }
  return true;
}

const PakEntry* PakReader::Find(const std::string& path) const {
  for (const PakEntry& e : entries_)
    if (e.path == path) return &e;
  return nullptr;
}

bool PakReader::Read(const PakEntry& e, std::vector<uint8_t>& out, std::string* error) {
  std::vector<uint8_t> stored((size_t)e.stored);
  if (!f_ || !Seek(f_, e.offset) || (e.stored && std::fread(stored.data(), 1, stored.size(), f_) != stored.size()))
    return Fail(error, e.path + ": cannot read");
  if (e.method == 1) {
    out.resize((size_t)e.size);
    const size_t n = ZSTD_decompress(out.data(), out.size(), stored.data(), stored.size());
    if (ZSTD_isError(n) || n != e.size) return Fail(error, e.path + ": damaged (decompression failed)");
  } else {
    out = std::move(stored);
  }
  if (Fnv1a64(out.data(), out.size()) != e.hash) return Fail(error, e.path + ": damaged (hash differs)");
  return true;
}

bool PakReader::ExtractAll(const std::string& dir, std::string* error, size_t* written) {
  const std::filesystem::path root = PathU8(dir);
  size_t n = 0;
  for (const PakEntry& e : entries_) {
    const std::filesystem::path p = root / PathU8(e.path);
    std::vector<uint8_t> have;
    if (ReadFileBytes(p, have) && have.size() == e.size && Fnv1a64(have.data(), have.size()) == e.hash) continue;
    std::vector<uint8_t> bytes;
    if (!Read(e, bytes, error)) return false;
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    const std::u8string u = p.u8string();
    std::FILE* f = OpenU8(std::string(u.begin(), u.end()), "wb");
    if (!f) return Fail(error, "cannot write " + std::string(u.begin(), u.end()));
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    if (!ok) return Fail(error, "write failed (disk full?)");
    ++n;
  }
  if (written) *written = n;
  return true;
}

uint64_t PakReader::ContentHash() const {
  uint64_t h = Fnv1a64(nullptr, 0);
  for (const PakEntry& e : entries_) {
    h = Fnv1a64(e.path.data(), e.path.size(), h);
    h = Fnv1a64(&e.hash, sizeof(e.hash), h);
  }
  return h;
}

} // namespace bdg
