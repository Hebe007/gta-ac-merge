// Single-writer seqlock over a plain struct whose first-class member `seq` is a uint32_t.
// seq is odd while the writer is mid-write. Works across processes on shared memory.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

template <class T>
void seqWrite(volatile T* dst, const T& in) {
  auto* d = const_cast<T*>(dst);
  // Round down to even: a writer that died mid-write leaves seq odd, and we must not inherit that.
  uint32_t s = static_cast<volatile uint32_t&>(dst->seq) & ~1u;
  static_cast<volatile uint32_t&>(dst->seq) = s + 1;  // odd: writing
  std::atomic_thread_fence(std::memory_order_seq_cst);
  T copy = in;
  copy.seq = s + 1;
  std::memcpy(d, &copy, sizeof(T));
  std::atomic_thread_fence(std::memory_order_seq_cst);
  static_cast<volatile uint32_t&>(dst->seq) = s + 2;  // even: done
}

template <class T>
bool seqRead(const volatile T* src, T& out) {
  const auto* s = const_cast<const T*>(src);
  for (int attempt = 0; attempt < 4; attempt++) {
    uint32_t before = static_cast<const volatile uint32_t&>(src->seq);
    if (before & 1u) continue;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::memcpy(&out, s, sizeof(T));
    std::atomic_thread_fence(std::memory_order_seq_cst);
    uint32_t after = static_cast<const volatile uint32_t&>(src->seq);
    if (before == after) { out.seq = before; return true; }
  }
  return false;
}

class StalenessTracker {
 public:
  static constexpr double kStaleSeconds = 0.25;
  void onRead(uint32_t seq, double nowSec) {
    if (!seen_ || seq != last_) { seen_ = true; last_ = seq; changedAt_ = nowSec; }
  }
  bool stale(double nowSec) const { return !seen_ || nowSec - changedAt_ >= kStaleSeconds; }

 private:
  bool seen_ = false;
  uint32_t last_ = 0;
  double changedAt_ = 0;
};
