#include "shm_win.hpp"
#include <windows.h>
#include <string>

SharedLink::~SharedLink() {
  if (view_) UnmapViewOfFile(const_cast<LinkState*>(view_));
  if (mapping_) CloseHandle(mapping_);
}

const volatile LinkState* SharedLink::get(double now) {
  if (view_ || now < nextTry_) return view_;
  nextTry_ = now + 1.0;
  DWORD err = 0;
  for (const std::string& n : {name_, "Local\\" + name_, "Global\\" + name_}) {
    mapping_ = OpenFileMappingA(FILE_MAP_READ, FALSE, n.c_str());
    if (mapping_) break;
    if (!err || GetLastError() != ERROR_FILE_NOT_FOUND) err = GetLastError();
  }
  if (!mapping_) {
    status_ = "Windows error " + std::to_string(err) +
              (err == ERROR_ACCESS_DENIED ? ": access denied - run AC and GTA both without admin rights"
               : err == ERROR_FILE_NOT_FOUND ? ": not found" : "");
    return nullptr;
  }
  void* v = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(LinkState));
  if (!v) {
    status_ = "Windows error " + std::to_string(GetLastError()) + " mapping the view";
    CloseHandle(mapping_);
    mapping_ = nullptr;
    return nullptr;
  }
  // We keep the mapping open, so if AC restarts, CSP reopens the same memory and the link resumes.
  view_ = static_cast<const volatile LinkState*>(v);
  return view_;
}

template <class T>
SharedBlock<T>::SharedBlock(const std::string& name) {
  mapping_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(T), name.c_str());
  if (!mapping_) return;
  void* v = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(T));
  if (!v) {
    CloseHandle(mapping_);
    mapping_ = nullptr;
    return;
  }
  view_ = static_cast<volatile T*>(v);
}

template <class T>
SharedBlock<T>::~SharedBlock() {
  if (view_) UnmapViewOfFile(const_cast<T*>(view_));
  if (mapping_) CloseHandle(mapping_);
}

template class SharedBlock<LinkCommand>;
template class SharedBlock<HitBlock>;
