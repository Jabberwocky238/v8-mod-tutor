#pragma once

#include <kj/async.h>
#include <v8-persistent-handle.h>

#include "runtime.h"
#include "storage-executor.h"

class KvBinding final : private kj::TaskSet::ErrorHandler {
 public:
  KvBinding(Runtime& runtime, StorageExecutor& executor, kj::String nameSpace);

 private:
  static KvBinding& self(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void get(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void put(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void erase(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void list(const v8::FunctionCallbackInfo<v8::Value>& info);

  v8::Global<v8::Promise::Resolver> makeResolver(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  void resolve(v8::Global<v8::Promise::Resolver> resolver,
               v8::Local<v8::Value> value);
  void reject(v8::Global<v8::Promise::Resolver> resolver, kj::Exception&& error);
  void taskFailed(kj::Exception&& error) override;

  Runtime& runtime;
  StorageExecutor& executor;
  kj::String nameSpace;
  kj::TaskSet tasks;
};
