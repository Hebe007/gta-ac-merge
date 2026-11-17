#include "shm_win.hpp"
#include <windows.h>

SharedLink::~SharedLink() {
  if (view_) UnmapViewOfFile(const_cast<LinkState*>(view_));
  if (mapping_) CloseHandle(mapping_);
}

const volatile LinkState* SharedLink::get(double now) {
  if (view_ || now < nextTry_) return view_;
  nextTry_ = now + 1.0;
  mapping_ = OpenFileMappingA(FILE_MAP_READ, FALSE, name_.c_str());
  if (!mapping_) return nullptr;
  void* v = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(LinkState));
  if (!v) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
    return nullptr;
  }
  // We keep the mapping open, so if AC restarts, CSP reopens the same memory and the link resumes.
  view_ = static_cast<const volatile LinkState*>(v);
  return view_;
}
