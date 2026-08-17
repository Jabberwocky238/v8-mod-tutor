#include "storage-executor.h"

#include <kj/debug.h>

StorageExecutor::StorageExecutor(KvStore& store, size_t threadCount,
                                 size_t capacity, bool startPaused)
    : store(store), capacity(capacity), paused(startPaused) {
  for (size_t i = 0; i < threadCount; ++i) {
    threads.emplace_back([this]() { workerLoop(); });
  }
}

StorageExecutor::~StorageExecutor() noexcept {
  {
    std::lock_guard lock(mutex);
    stopping = true;
    paused = false;
  }
  ready.notify_all();
  for (auto& thread : threads) thread.join();
}

template <typename T, typename Func>
kj::Promise<T> StorageExecutor::submit(Func&& function) {
  auto pair = kj::newPromiseAndCrossThreadFulfiller<T>();
  {
    std::lock_guard lock(mutex);
    if (jobs.size() >= capacity) {
      pair.fulfiller->reject(KJ_EXCEPTION(OVERLOADED, "StorageBusyError"));
      return kj::mv(pair.promise);
    }
    jobs.emplace_back([fulfiller = kj::mv(pair.fulfiller),
                       function = kj::fwd<Func>(function)]() mutable {
      try {
        if constexpr (std::is_void_v<T>) {
          function();
          fulfiller->fulfill();
        } else {
          fulfiller->fulfill(function());
        }
      } catch (...) {
        fulfiller->reject(kj::getCaughtExceptionAsKj());
      }
    });
  }
  ready.notify_one();
  return kj::mv(pair.promise);
}

kj::Promise<std::optional<std::string>> StorageExecutor::get(
    std::string nameSpace, std::string key) {
  return submit<std::optional<std::string>>(
      [this, nameSpace = kj::mv(nameSpace), key = kj::mv(key)]() {
    return store.get(nameSpace.c_str(), key.c_str());
  });
}

kj::Promise<void> StorageExecutor::put(std::string nameSpace, std::string key,
                                       std::string value) {
  return submit<void>([this, nameSpace = kj::mv(nameSpace), key = kj::mv(key),
                       value = kj::mv(value)]() {
    store.put(nameSpace.c_str(), key.c_str(), value.c_str());
  });
}

kj::Promise<void> StorageExecutor::erase(std::string nameSpace, std::string key) {
  return submit<void>([this, nameSpace = kj::mv(nameSpace), key = kj::mv(key)]() {
    store.erase(nameSpace.c_str(), key.c_str());
  });
}

kj::Promise<ListResult> StorageExecutor::list(std::string nameSpace,
                                              std::string prefix, uint32_t limit) {
  return submit<ListResult>([this, nameSpace = kj::mv(nameSpace),
                             prefix = kj::mv(prefix), limit]() {
    return store.list(nameSpace.c_str(), prefix.c_str(), limit);
  });
}

void StorageExecutor::resume() {
  {
    std::lock_guard lock(mutex);
    paused = false;
  }
  ready.notify_all();
}

size_t StorageExecutor::queued() const {
  std::lock_guard lock(mutex);
  return jobs.size();
}

void StorageExecutor::workerLoop() {
  while (true) {
    kj::Function<void()> job;
    {
      std::unique_lock lock(mutex);
      ready.wait(lock, [this]() { return stopping || (!paused && !jobs.empty()); });
      if (stopping && jobs.empty()) return;
      if (paused) continue;
      job = kj::mv(jobs.front());
      jobs.pop_front();
    }
    job();
  }
}
