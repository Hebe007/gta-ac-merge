// Read-only view of AC's LinkState shared memory. Retries opening once a second until AC creates it.
#pragma once
#include <string>
#include "link_protocol.h"

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
