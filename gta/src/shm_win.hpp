// Read-only view of AC's LinkState shared memory. Retries opening once a second until AC creates it.
#pragma once
#include <string>
#include "link_protocol.h"

// Writable block shared with the AC app (created here if AC hasn't created it yet): commands, hits.
template <class T>
class SharedBlock {
 public:
  explicit SharedBlock(const std::string& name);
  ~SharedBlock();
  volatile T* get() { return view_; }

 private:
  void* mapping_ = nullptr;
  volatile T* view_ = nullptr;
};
using SharedCommand = SharedBlock<LinkCommand>;
using SharedHits = SharedBlock<HitBlock>;

class SharedLink {
 public:
  explicit SharedLink(std::string name) : name_(std::move(name)) {}
  ~SharedLink();
  const volatile LinkState* get(double nowSec);  // null until AC's GTA Link app has created it
  const std::string& status() const { return status_; }  // why it isn't open yet

 private:
  std::string name_;
  void* mapping_ = nullptr;
  const volatile LinkState* view_ = nullptr;
  double nextTry_ = 0;
  std::string status_;
};
