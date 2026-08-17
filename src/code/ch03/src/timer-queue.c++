#include "timer-queue.h"

#include <kj/debug.h>

#include "runtime.h"

uint64_t TimerQueue::schedule(v8::Local<v8::Function> callback, uint64_t delayMs) {
  const auto id = nextId++;
  active.emplace(id, v8::Global<v8::Function>(runtime.getIsolate(), callback));
  tasks.add(timer.afterDelay(delayMs * kj::MILLISECONDS).then(
      [this, id]() { fire(id); }));
  return id;
}

void TimerQueue::cancel(uint64_t id) {
  active.erase(id);
}

void TimerQueue::fire(uint64_t id) {
  auto found = active.find(id);
  if (found == active.end()) return;

  auto callback = std::move(found->second);
  active.erase(found);
  runtime.call(callback);
}

void TimerQueue::taskFailed(kj::Exception&& error) {
  KJ_LOG(ERROR, "timer callback failed", error);
}
