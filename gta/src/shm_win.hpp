// Read-only view of AC's LinkState shared memory. Retries opening once a second until AC creates it.
#pragma once
#include <string>
#include "link_protocol.h"

// Writable command block for the AC app (created here if AC hasn't created it yet).
class SharedCommand {
 public:
  explicit SharedCommand(const std::string& name);
  ~SharedCommand();
  volatile LinkCommand* get() { return view_; }

 private:
  void* mapping_ = nullptr;
  volatile LinkCommand* view_ = nullptr;
};

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
