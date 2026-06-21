#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>

namespace silo::storage::platform {

#ifdef _WIN32

inline void* mmap_file(const std::string& path, size_t size, bool writable) {
  HANDLE hFile = CreateFileA(
    path.c_str(),
    GENERIC_READ | (writable ? GENERIC_WRITE : 0),
    FILE_SHARE_READ | (writable ? FILE_SHARE_WRITE : 0),
    nullptr,
    OPEN_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  );
  if (hFile == INVALID_HANDLE_VALUE) return nullptr;

  HANDLE hMapping = CreateFileMappingA(
    hFile,
    nullptr,
    writable ? PAGE_READWRITE : PAGE_READONLY,
    0,
    size,
    nullptr
  );
  if (!hMapping) {
    CloseHandle(hFile);
    return nullptr;
  }

  void* addr = MapViewOfFile(
    hMapping,
    writable ? FILE_MAP_WRITE : FILE_MAP_READ,
    0, 0, size
  );
  CloseHandle(hMapping);
  CloseHandle(hFile);
  return addr;
}

inline bool munmap_file(void* addr, size_t size) {
  return UnmapViewOfFile(addr) != 0;
}

inline bool msync_file(void* addr, size_t size) {
  return FlushViewOfFile(addr, size) != 0;
}

#else

inline void* mmap_file(const std::string& path, size_t size, bool writable) {
  int flags = O_RDONLY;
  int prot = PROT_READ;
  if (writable) {
    flags = O_RDWR | O_CREAT;
    prot = PROT_READ | PROT_WRITE;
  }
  int fd = ::open(path.c_str(), flags, 0644);
  if (fd < 0) return nullptr;

  if (writable) {
    ftruncate(fd, size);
  }

  void* addr = mmap(nullptr, size, prot, MAP_SHARED, fd, 0);
  ::close(fd);
  if (addr == MAP_FAILED) return nullptr;
  return addr;
}

inline bool munmap_file(void* addr, size_t size) {
  return munmap(addr, size) == 0;
}

inline bool msync_file(void* addr, size_t size) {
  return msync(addr, size, MS_SYNC) == 0;
}

#endif

} // namespace silo::storage::platform
