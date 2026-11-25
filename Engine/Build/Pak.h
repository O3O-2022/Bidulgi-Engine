#pragma once
// .bpak archives (queue 7-4): the packed content of a built game. One file: a header, every file's bytes (zstd
// compressed when that makes it smaller, else stored), then an index (path, offset, sizes, FNV-1a 64 hash).
//
//   "BPAK" u32 version(1) u32 fileCount u64 indexOffset u64 indexSize u32 flags(0)
//   data ... index: per file u16 pathBytes, path (UTF-8, '/'), u8 method (0 stored, 1 zstd), u64 offset,
//                   u64 storedSize, u64 size, u64 hash
//
// Not encryption: anyone can read it (Docs/architecture/libraries-and-packaging.md §4).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bdg {

uint64_t Fnv1a64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ULL);

struct PakEntry {
  std::string path;
  uint8_t method = 0; // 0 stored, 1 zstd
  uint64_t offset = 0, stored = 0, size = 0, hash = 0;
};

class PakWriter {
public:
  PakWriter() = default;
  PakWriter(const PakWriter&) = delete;
  PakWriter& operator=(const PakWriter&) = delete;
  ~PakWriter();
  bool Open(const std::string& path, std::string* error = nullptr);
  // compress: try zstd (kept only when smaller). Paths are relative ("Scenes/Main.scene"); a repeat is an error.
  bool Add(const std::string& path, const std::vector<uint8_t>& bytes, bool compress, std::string* error = nullptr);
  bool Finish(std::string* error = nullptr); // writes the index and closes
  const std::vector<PakEntry>& Entries() const { return entries_; }
  uint64_t BytesWritten() const { return pos_; }

private:
  std::FILE* f_ = nullptr;
  uint64_t pos_ = 0;
  std::vector<PakEntry> entries_;
};

class PakReader {
public:
  PakReader() = default;
  PakReader(const PakReader&) = delete;
  PakReader& operator=(const PakReader&) = delete;
  ~PakReader();
  void Close();
  bool Open(const std::string& path, std::string* error = nullptr);
  const std::vector<PakEntry>& Entries() const { return entries_; }
  const PakEntry* Find(const std::string& path) const;
  bool Read(const PakEntry& e, std::vector<uint8_t>& out, std::string* error = nullptr); // hash checked
  // Every file under dir (folders made); a file already there with the same size and hash is left alone.
  bool ExtractAll(const std::string& dir, std::string* error = nullptr, size_t* written = nullptr);
  uint64_t ContentHash() const; // of the index: changes when any file changes

private:
  std::FILE* f_ = nullptr;
  std::vector<PakEntry> entries_;
};

} // namespace bdg
