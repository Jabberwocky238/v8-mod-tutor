#pragma once

#include <vector>

#include <v8-function.h>
#include <v8-persistent-handle.h>
#include <v8-promise.h>

class Runtime;

class ExecutionContext final {
 public:
  explicit ExecutionContext(Runtime& runtime) : runtime(runtime) {}

  void waitUntil(v8::Local<v8::Promise> promise);
  size_t pendingTaskCount() const { return pendingTasks; }
  void clear();

 private:
  static void settled(const v8::FunctionCallbackInfo<v8::Value>& info);

  Runtime& runtime;
  size_t pendingTasks = 0;
  std::vector<v8::Global<v8::Promise>> promises;
};
