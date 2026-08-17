#include "runtime.h"

#include <algorithm>

#include <kj/debug.h>
#include <v8-cppgc.h>
#include <v8-exception.h>
#include <v8-external.h>
#include <v8-function.h>
#include <v8-initialization.h>
#include <v8-object.h>
#include <v8-primitive.h>
#include <v8-promise.h>
#include <v8-script.h>

namespace {

struct PlatformState {
  PlatformState() {
    platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();
  }

  ~PlatformState() {
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
  }

  std::unique_ptr<v8::Platform> platform;
};

PlatformState& platformState() {
  static PlatformState state;
  return state;
}

v8::Local<v8::String> jsString(v8::Isolate* isolate, kj::StringPtr text) {
  return v8::String::NewFromUtf8(isolate, text.begin(), v8::NewStringType::kNormal,
                                 static_cast<int>(text.size())).ToLocalChecked();
}

kj::String utf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  v8::String::Utf8Value text(isolate, value);
  KJ_REQUIRE(*text != nullptr, "JavaScript value is not UTF-8");
  return kj::str(*text);
}

}  // namespace

Runtime::Runtime(const char* executablePath, kj::Timer& timer)
    : timer(timer), timers(*this, timer), executionContext(*this) {
  v8::V8::InitializeICUDefaultLocation(executablePath);
  v8::V8::InitializeExternalStartupData(executablePath);
  auto& platform = platformState().platform;

  allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate::CreateParams params;
  params.array_buffer_allocator = allocator;
  params.cpp_heap = v8::CppHeap::Create(platform.get(), v8::CppHeapCreateParams({})).release();
  isolate = v8::Isolate::New(params);
  isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);

  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  context.Reset(isolate, v8::Context::New(isolate));
  v8::Context::Scope contextScope(localContext());
  installGlobals();
}

Runtime::~Runtime() noexcept {
  timers.clear();
  executionContext.clear();
  worker.Reset();
  context.Reset();
  isolate->Dispose();
  delete allocator;
}

v8::Local<v8::Context> Runtime::localContext() {
  return context.Get(isolate);
}

Runtime& Runtime::fromData(const v8::FunctionCallbackInfo<v8::Value>& info) {
  return *static_cast<Runtime*>(info.Data().As<v8::External>()->Value());
}

void Runtime::installGlobals() {
  auto ctx = localContext();
  auto data = v8::External::New(isolate, this);
  auto global = ctx->Global();
  global->Set(ctx, jsString(isolate, "setTimeout"),
              v8::Function::New(ctx, setTimeout, data).ToLocalChecked()).Check();
  global->Set(ctx, jsString(isolate, "clearTimeout"),
              v8::Function::New(ctx, clearTimeout, data).ToLocalChecked()).Check();
  global->Set(ctx, jsString(isolate, "Response"),
              v8::Function::New(ctx, responseConstructor, data).ToLocalChecked()).Check();
}

void Runtime::setTimeout(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& runtime = fromData(info);
  if (info.Length() < 1 || !info[0]->IsFunction()) {
    runtime.getIsolate()->ThrowException(
        v8::Exception::TypeError(jsString(runtime.getIsolate(), "setTimeout needs a function")));
    return;
  }
  uint64_t delay = 0;
  if (info.Length() > 1) {
    delay = static_cast<uint64_t>(std::max(0.0, info[1]->NumberValue(runtime.localContext()).FromMaybe(0)));
  }
  info.GetReturnValue().Set(static_cast<double>(
      runtime.timers.schedule(info[0].As<v8::Function>(), delay)));
}

void Runtime::clearTimeout(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& runtime = fromData(info);
  if (info.Length() > 0) {
    runtime.timers.cancel(info[0]->IntegerValue(runtime.localContext()).FromMaybe(0));
  }
}

void Runtime::responseConstructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& runtime = fromData(info);
  auto ctx = runtime.localContext();
  auto result = info.IsConstructCall() ? info.This() : v8::Object::New(runtime.isolate);
  v8::Local<v8::Value> body = info.Length() > 0
      ? info[0]
      : v8::Local<v8::Value>(v8::String::Empty(runtime.isolate));
  result->Set(ctx, jsString(runtime.isolate, "body"), body).Check();
  uint status = 200;
  kj::String contentType = kj::str("text/plain; charset=utf-8");
  if (info.Length() > 1 && info[1]->IsObject()) {
    auto init = info[1].As<v8::Object>();
    auto statusValue = init->Get(ctx, jsString(runtime.isolate, "status"));
    if (!statusValue.IsEmpty() && statusValue.ToLocalChecked()->IsNumber()) {
      status = statusValue.ToLocalChecked()->Uint32Value(ctx).FromMaybe(200);
    }
  }
  result->Set(ctx, jsString(runtime.isolate, "status"), v8::Integer::New(runtime.isolate, status)).Check();
  result->Set(ctx, jsString(runtime.isolate, "contentType"), jsString(runtime.isolate, contentType)).Check();
  info.GetReturnValue().Set(result);
}

void Runtime::waitUntil(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& runtime = fromData(info);
  if (info.Length() < 1 || !info[0]->IsPromise()) {
    runtime.isolate->ThrowException(
        v8::Exception::TypeError(jsString(runtime.isolate, "waitUntil needs a Promise")));
    return;
  }
  runtime.executionContext.waitUntil(info[0].As<v8::Promise>());
}

void Runtime::call(v8::Global<v8::Function>& callback) {
  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  auto ctx = localContext();
  v8::Context::Scope contextScope(ctx);
  v8::TryCatch caught(isolate);
  auto localCallback = callback.Get(isolate);
  if (localCallback->Call(ctx, v8::Undefined(isolate), 0, nullptr).IsEmpty()) {
    KJ_LOG(ERROR, jsError(caught, "timer callback failed"));
  }
  isolate->PerformMicrotaskCheckpoint();
}

void Runtime::loadWorker(kj::StringPtr sourceText) {
  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  auto ctx = localContext();
  v8::Context::Scope contextScope(ctx);
  v8::TryCatch caught(isolate);
  auto source = jsString(isolate, sourceText);
  v8::Local<v8::Script> script;
  v8::Local<v8::Value> ignored;
  if (!v8::Script::Compile(ctx, source).ToLocal(&script) ||
      !script->Run(ctx).ToLocal(&ignored)) {
    throw jsError(caught, "worker load failed");
  }
  v8::Local<v8::Value> value;
  KJ_REQUIRE(ctx->Global()->Get(ctx, jsString(isolate, "worker")).ToLocal(&value),
             "worker global is missing");
  KJ_REQUIRE(value->IsObject(), "worker must be an object");
  worker.Reset(isolate, value.As<v8::Object>());
}

kj::Promise<FetchResponse> Runtime::dispatch(kj::StringPtr method, kj::StringPtr url) {
  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  auto ctx = localContext();
  v8::Context::Scope contextScope(ctx);
  v8::TryCatch caught(isolate);

  auto request = v8::Object::New(isolate);
  request->Set(ctx, jsString(isolate, "method"), jsString(isolate, method)).Check();
  request->Set(ctx, jsString(isolate, "url"), jsString(isolate, url)).Check();
  auto execution = v8::Object::New(isolate);
  execution->Set(ctx, jsString(isolate, "waitUntil"),
                 v8::Function::New(ctx, waitUntil, v8::External::New(isolate, this)).ToLocalChecked()).Check();

  auto localWorker = worker.Get(isolate);
  v8::Local<v8::Value> fetchValue;
  KJ_REQUIRE(localWorker->Get(ctx, jsString(isolate, "fetch")).ToLocal(&fetchValue) &&
                 fetchValue->IsFunction(),
             "worker.fetch must be a function");
  v8::Local<v8::Value> args[] = {request, v8::Object::New(isolate), execution};
  v8::Local<v8::Value> result;
  if (!fetchValue.As<v8::Function>()->Call(ctx, localWorker, 3, args).ToLocal(&result)) {
    throw jsError(caught, "fetch failed");
  }
  isolate->PerformMicrotaskCheckpoint();
  if (!result->IsPromise()) return readResponse(result);
  return awaitResponse(v8::Global<v8::Promise>(isolate, result.As<v8::Promise>()));
}

kj::Promise<FetchResponse> Runtime::awaitResponse(v8::Global<v8::Promise> promise) {
  return timer.afterDelay(1 * kj::MILLISECONDS).then(
      [this, promise = kj::mv(promise)]() mutable -> kj::Promise<FetchResponse> {
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handles(isolate);
    auto ctx = localContext();
    v8::Context::Scope contextScope(ctx);
    isolate->PerformMicrotaskCheckpoint();
    auto local = promise.Get(isolate);
    if (local->State() == v8::Promise::kPending) return awaitResponse(kj::mv(promise));
    KJ_REQUIRE(local->State() == v8::Promise::kFulfilled,
               "fetch promise rejected", utf8(isolate, local->Result()));
    return readResponse(local->Result());
  });
}

FetchResponse Runtime::readResponse(v8::Local<v8::Value> value) {
  KJ_REQUIRE(value->IsObject(), "fetch must return a Response");
  auto ctx = localContext();
  auto object = value.As<v8::Object>();
  FetchResponse response;
  response.status = object->Get(ctx, jsString(isolate, "status")).ToLocalChecked()
                        ->Uint32Value(ctx).FromMaybe(200);
  response.body = utf8(isolate, object->Get(ctx, jsString(isolate, "body")).ToLocalChecked());
  response.contentType = utf8(
      isolate, object->Get(ctx, jsString(isolate, "contentType")).ToLocalChecked());
  return response;
}

kj::Exception Runtime::jsError(v8::TryCatch& caught, kj::StringPtr prefix) {
  auto message = caught.Exception().IsEmpty()
      ? kj::str("unknown JavaScript exception")
      : utf8(isolate, caught.Exception());
  return KJ_EXCEPTION(FAILED, prefix, message);
}
