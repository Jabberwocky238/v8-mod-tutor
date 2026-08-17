#pragma once

#include <cstdint>

#include <kj/async.h>
#include <kj/map.h>
#include <kj/timer.h>
#include <v8-function.h>
#include <v8-persistent-handle.h>

class Runtime;

class TimerQueue final : private kj::TaskSet::ErrorHandler {
 public:
  TimerQueue(Runtime& runtime, kj::Timer& timer)
      : runtime(runtime), timer(timer), tasks(*this) {}

  uint64_t schedule(v8::Local<v8::Function> callback, uint64_t delayMs);
  void cancel(uint64_t id);
  size_t size() const { return active.size(); }

 private:
  struct Entry {
    v8::TracedReference<v8::Function> callback;
    kj::Own<kj::PromiseFulfiller<void>> canceler;
  };

  void taskFailed(kj::Exception&& error) override;

  Runtime& runtime;
  kj::Timer& timer;
  kj::TaskSet tasks;
  kj::HashMap<uint64_t, Entry> active;
  uint64_t nextId = 1;
};
