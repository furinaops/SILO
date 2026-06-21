#pragma once

#include <shared_mutex>

namespace silo::concurrency {

class RWLock {
 public:
  class ReadGuard {
   public:
    explicit ReadGuard(RWLock& lock) : lock_(lock) { lock_.mutex_.lock_shared(); }
    ~ReadGuard() { lock_.mutex_.unlock_shared(); }
    ReadGuard(const ReadGuard&) = delete;
    ReadGuard& operator=(const ReadGuard&) = delete;
    ReadGuard(ReadGuard&&) = delete;
    ReadGuard& operator=(ReadGuard&&) = delete;
   private:
    RWLock& lock_;
  };

  class WriteGuard {
   public:
    explicit WriteGuard(RWLock& lock) : lock_(lock) { lock_.mutex_.lock(); }
    ~WriteGuard() { lock_.mutex_.unlock(); }
    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;
    WriteGuard(WriteGuard&&) = delete;
    WriteGuard& operator=(WriteGuard&&) = delete;
   private:
    RWLock& lock_;
  };

 private:
  std::shared_mutex mutex_;
};

} // namespace silo::concurrency
