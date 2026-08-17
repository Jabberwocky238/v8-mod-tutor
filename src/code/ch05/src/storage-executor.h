#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <kj/async.h>
#include <kj/function.h>

#include "kv-store.h"

class StorageExecutor final {
 public:
  StorageExecutor(KvStore& store, size_t threadCount = 2, size_t capacity = 1024,
                  bool startPaused = false);
  ~StorageExecutor() noexcept;

  kj::Promise<std::optional<std::string>> get(std::string nameSpace, std::string key);
  kj::Promise<void> put(std::string nameSpace, std::string key, std::string value);
  kj::Promise<void> erase(std::string nameSpace, std::string key);
  kj::Promise<ListResult> list(std::string nameSpace, std::string prefix, uint32_t limit);
  void resume();
  size_t queued() const;

 private:
  template <typename T, typename Func>
  kj::Promise<T> submit(Func&& function);
  void workerLoop();

  KvStore& store;
  size_t capacity;
  mutable std::mutex mutex;
  std::condition_variable ready;
  std::deque<kj::Function<void()>> jobs;
  std::vector<std::thread> threads;
  bool paused;
  bool stopping = false;
};
