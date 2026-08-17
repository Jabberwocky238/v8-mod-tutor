#pragma once

#include <memory>

#include <kj/async.h>
#include <kj/string.h>
#include <kj/timer.h>
#include <libplatform/libplatform.h>
#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-persistent-handle.h>

#include "timer-queue.h"
#include "execution-context.h"

struct FetchResponse {
  uint status = 200;
  kj::String body;
  kj::String contentType = kj::str("text/plain; charset=utf-8");
};

class Runtime final {
 public:
  Runtime(const char* executablePath, kj::Timer& timer);
  ~Runtime() noexcept;

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  void loadWorker(kj::StringPtr source);
  kj::Promise<FetchResponse> dispatch(kj::StringPtr method, kj::StringPtr url);

  v8::Isolate* getIsolate() const { return isolate; }
  v8::Local<v8::Context> getContext() { return context.Get(isolate); }
  size_t activeTimerCount() const { return timers.size(); }
  size_t backgroundTaskCount() const { return executionContext.pendingTaskCount(); }
  void call(v8::Global<v8::Function>& callback);

 private:
  static Runtime& fromData(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void setTimeout(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void clearTimeout(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void responseConstructor(const v8::FunctionCallbackInfo<v8::Value>& info);
  static void waitUntil(const v8::FunctionCallbackInfo<v8::Value>& info);

  v8::Local<v8::Context> localContext();
  void installGlobals();
  kj::Promise<FetchResponse> awaitResponse(v8::Global<v8::Promise> promise);
  FetchResponse readResponse(v8::Local<v8::Value> value);
  kj::Exception jsError(v8::TryCatch& caught, kj::StringPtr prefix);

  v8::ArrayBuffer::Allocator* allocator = nullptr;
  v8::Isolate* isolate = nullptr;
  v8::Global<v8::Context> context;
  v8::Global<v8::Object> worker;
  kj::Timer& timer;
  TimerQueue timers;
  ExecutionContext executionContext;
};
