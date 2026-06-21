#include "test_main.h"
#include "../src/concurrency/rwlock.h"

#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("RWLock basic read lock") {
  silo::concurrency::RWLock lock;
  {
    auto guard = silo::concurrency::RWLock::ReadGuard(lock);
  }
  return true;
}

TEST_CASE("RWLock basic write lock") {
  silo::concurrency::RWLock lock;
  {
    auto guard = silo::concurrency::RWLock::WriteGuard(lock);
  }
  return true;
}

TEST_CASE("RWLock concurrent readers") {
  silo::concurrency::RWLock lock;
  std::atomic<int> counter{0};

  auto reader = [&]() {
    auto guard = silo::concurrency::RWLock::ReadGuard(lock);
    counter++;
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(reader);
  }
  for (auto& t : threads) t.join();

  return counter == 10;
}
