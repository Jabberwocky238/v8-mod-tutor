#include "engine.h"

#include <kj/debug.h>
#include <v8-exception.h>
#include <v8-initialization.h>
#include <v8-script.h>

Engine::Engine(const char* executablePath) {
  v8::V8::InitializeICUDefaultLocation(executablePath);
  v8::V8::InitializeExternalStartupData(executablePath);

  platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();

  allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate::CreateParams params;
  params.array_buffer_allocator = allocator;
  isolate = v8::Isolate::New(params);

  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  context.Reset(isolate, v8::Context::New(isolate));
}

Engine::~Engine() noexcept {
  context.Reset();
  isolate->Dispose();
  v8::V8::Dispose();
  v8::V8::DisposePlatform();
  delete allocator;
}

kj::String Engine::evaluate(kj::StringPtr sourceText) {
  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  auto localContext = context.Get(isolate);
  v8::Context::Scope contextScope(localContext);
  v8::TryCatch caught(isolate);

  auto source = v8::String::NewFromUtf8(
      isolate, sourceText.begin(), v8::NewStringType::kNormal,
      static_cast<int>(sourceText.size())).ToLocalChecked();

  v8::Local<v8::Script> script;
  v8::Local<v8::Value> result;
  if (!v8::Script::Compile(localContext, source).ToLocal(&script) ||
      !script->Run(localContext).ToLocal(&result)) {
    v8::String::Utf8Value message(isolate, caught.Exception());
    KJ_FAIL_REQUIRE("JavaScript evaluation failed", *message);
  }

  v8::String::Utf8Value text(isolate, result);
  KJ_REQUIRE(*text != nullptr, "JavaScript result is not UTF-8");
  return kj::str(*text);
}
