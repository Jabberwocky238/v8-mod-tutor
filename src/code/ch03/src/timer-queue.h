#pragma once

#include <cstdint>
#include <unordered_map>

#include <kj/async.h>
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
  void clear() { active.clear(); }
  size_t size() const { return active.size(); }

 private:
  void fire(uint64_t id);
  void taskFailed(kj::Exception&& error) override;

  Runtime& runtime;
  kj::Timer& timer;
  kj::TaskSet tasks;
  std::unordered_map<uint64_t, v8::Global<v8::Function>> active;
  uint64_t nextId = 1;
};
