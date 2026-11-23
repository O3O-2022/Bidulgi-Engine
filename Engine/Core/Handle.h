#pragma once
// Handles (queue 1-4): runtime references that know when their object is gone. A handle is a slot index
// plus the slot's generation; destroying an object bumps the generation, so every old handle to it fails
// IsValid() even after the slot is reused. Handles are runtime-only: files, commands and the network use
// the persistent id ("g7"). Every language binding reports a dead handle with the same message (DeadHandleMessage).
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bdg {
template <class T> struct Handle {
  uint32_t index = 0;
  uint32_t generation = 0; // 0 = null handle (live slots start at 1)
  bool IsNull() const { return generation == 0; }
  bool operator==(const Handle&) const = default;
  uint64_t Bits() const { return (uint64_t)generation << 32 | index; } // for C API / reflection (ObjectHandle)
  static Handle FromBits(uint64_t b) { return {(uint32_t)(b & 0xffffffffu), (uint32_t)(b >> 32)}; }
};

// Same text in C++, C#, Lua, Python and Graph errors.
inline std::string DeadHandleMessage(const char* typeName, uint32_t index, uint32_t generation) {
  return std::format("{} handle {}:{} is no longer valid: the object was destroyed (keep its id, or check IsValid first)",
                     typeName, index, generation);
}

template <class T> class Pool {
public:
  template <class... A> Handle<T> Create(A&&... args) {
    uint32_t i;
    if (!free_.empty()) { i = free_.back(); free_.pop_back(); }
    else { i = (uint32_t)slots_.size(); slots_.push_back({}); }
    Slot& s = slots_[i];
    s.value = std::make_unique<T>(std::forward<A>(args)...); // own allocation: pointers stay put when the pool grows
    ++count_;
    return {i, s.generation};
  }

  bool Destroy(Handle<T> h) {
    if (!IsValid(h)) return false;
    Slot& s = slots_[h.index];
    s.value.reset();
    if (++s.generation == 0) s.generation = 1; // never hand out the null generation
    free_.push_back(h.index);
    --count_;
    return true;
  }

  bool IsValid(Handle<T> h) const {
    return !h.IsNull() && h.index < slots_.size() && slots_[h.index].generation == h.generation && slots_[h.index].value;
  }
  T* Get(Handle<T> h) { return IsValid(h) ? slots_[h.index].value.get() : nullptr; }
  const T* Get(Handle<T> h) const { return IsValid(h) ? slots_[h.index].value.get() : nullptr; }
  T* Resolve(Handle<T> h, const char* typeName, std::string* error) { // Get + the shared error text
    T* p = Get(h);
    if (!p && error) *error = DeadHandleMessage(typeName, h.index, h.generation);
    return p;
  }
  size_t Count() const { return count_; }

  template <class F> void ForEach(F&& f) {
    for (uint32_t i = 0; i < slots_.size(); ++i)
      if (slots_[i].value) f(Handle<T>{i, slots_[i].generation}, *slots_[i].value);
  }

private:
  struct Slot {
    std::unique_ptr<T> value;
    uint32_t generation = 1;
  };
  std::vector<Slot> slots_;
  std::vector<uint32_t> free_;
  size_t count_ = 0;
};
}
