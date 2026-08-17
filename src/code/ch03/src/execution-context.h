#pragma once

#include <kj/async.h>
#include <kj/debug.h>
#include <v8-object.h>
#include <v8-persistent-handle.h>

class Runtime;

class ExecutionContext final : public v8::Object::Wrappable,
                               private kj::TaskSet::ErrorHandler {
 public:
  static constexpr auto MAX_LIFETIME = 30 * kj::SECONDS;

  explicit ExecutionContext(Runtime& runtime)
      : runtime(runtime), tasks(*this) {}

  void waitUntil(v8::Local<v8::Promise> promise);
  kj::Promise<void> drain();
  size_t pendingTaskCount() const { return pending.size(); }

  void Trace(cppgc::Visitor* visitor) const override {
    v8::Object::Wrappable::Trace(visitor);
    for (auto& promise : pending) visitor->Trace(promise);
  }

 private:
  void taskFailed(kj::Exception&& error) override;

  Runtime& runtime;
  kj::TaskSet tasks;
  kj::Vector<v8::TracedReference<v8::Promise>> pending;
};
