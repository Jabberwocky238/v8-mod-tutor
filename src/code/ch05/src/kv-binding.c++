#include "kv-binding.h"

#include <kj/debug.h>
#include <v8-container.h>
#include <v8-exception.h>
#include <v8-external.h>
#include <v8-function.h>
#include <v8-object.h>
#include <v8-primitive.h>
#include <v8-promise.h>

namespace {

v8::Local<v8::String> jsString(v8::Isolate* isolate, kj::StringPtr text) {
  return v8::String::NewFromUtf8(isolate, text.begin(), v8::NewStringType::kNormal,
                                 static_cast<int>(text.size())).ToLocalChecked();
}

std::string argument(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     const v8::FunctionCallbackInfo<v8::Value>& info, int index) {
  if (info.Length() <= index) return {};
  v8::Local<v8::String> string;
  if (!info[index]->ToString(context).ToLocal(&string)) return {};
  v8::String::Utf8Value value(isolate, string);
  return *value == nullptr ? std::string() : std::string(*value, value.length());
}

}  // namespace

KvBinding::KvBinding(Runtime& runtime, StorageExecutor& executor,
                     kj::String nameSpace)
    : runtime(runtime), executor(executor), nameSpace(kj::mv(nameSpace)), tasks(*this) {
  auto* isolate = runtime.getIsolate();
  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  auto context = runtime.getContext();
  v8::Context::Scope contextScope(context);
  auto data = v8::External::New(isolate, this);
  auto kv = v8::Object::New(isolate);
  kv->Set(context, jsString(isolate, "get"),
          v8::Function::New(context, get, data).ToLocalChecked()).Check();
  kv->Set(context, jsString(isolate, "put"),
          v8::Function::New(context, put, data).ToLocalChecked()).Check();
  kv->Set(context, jsString(isolate, "delete"),
          v8::Function::New(context, erase, data).ToLocalChecked()).Check();
  kv->Set(context, jsString(isolate, "list"),
          v8::Function::New(context, list, data).ToLocalChecked()).Check();
  auto env = v8::Object::New(isolate);
  env->Set(context, jsString(isolate, "KV"), kv).Check();
  runtime.setEnvironment(env);
}

KvBinding& KvBinding::self(const v8::FunctionCallbackInfo<v8::Value>& info) {
  return *static_cast<KvBinding*>(info.Data().As<v8::External>()->Value());
}

v8::Global<v8::Promise::Resolver> KvBinding::makeResolver(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto resolver = v8::Promise::Resolver::New(runtime.getContext()).ToLocalChecked();
  info.GetReturnValue().Set(resolver->GetPromise());
  return v8::Global<v8::Promise::Resolver>(runtime.getIsolate(), resolver);
}

void KvBinding::get(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& binding = self(info);
  auto resolver = binding.makeResolver(info);
  auto rejectResolver = v8::Global<v8::Promise::Resolver>(
      binding.runtime.getIsolate(), resolver.Get(binding.runtime.getIsolate()));
  auto key = argument(binding.runtime.getIsolate(), binding.runtime.getContext(), info, 0);
  binding.tasks.add(binding.executor.get(binding.nameSpace.cStr(), kj::mv(key)).then(
      [&binding, resolver = kj::mv(resolver)](std::optional<std::string>&& value) mutable {
    v8::Isolate::Scope isolateScope(binding.runtime.getIsolate());
    v8::HandleScope handles(binding.runtime.getIsolate());
    auto result = value.has_value()
        ? v8::Local<v8::Value>(jsString(binding.runtime.getIsolate(), value->c_str()))
        : v8::Local<v8::Value>(v8::Null(binding.runtime.getIsolate()));
    binding.resolve(kj::mv(resolver), result);
  }, [&binding, resolver = kj::mv(rejectResolver)](kj::Exception&& error) mutable {
    binding.reject(kj::mv(resolver), kj::mv(error));
  }));
}

void KvBinding::put(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& binding = self(info);
  auto resolver = binding.makeResolver(info);
  auto rejectResolver = v8::Global<v8::Promise::Resolver>(
      binding.runtime.getIsolate(), resolver.Get(binding.runtime.getIsolate()));
  auto key = argument(binding.runtime.getIsolate(), binding.runtime.getContext(), info, 0);
  auto value = argument(binding.runtime.getIsolate(), binding.runtime.getContext(), info, 1);
  binding.tasks.add(binding.executor.put(binding.nameSpace.cStr(), kj::mv(key), kj::mv(value)).then(
      [&binding, resolver = kj::mv(resolver)]() mutable {
    v8::Isolate::Scope isolateScope(binding.runtime.getIsolate());
    v8::HandleScope handles(binding.runtime.getIsolate());
    binding.resolve(kj::mv(resolver), v8::Undefined(binding.runtime.getIsolate()));
  }, [&binding, resolver = kj::mv(rejectResolver)](kj::Exception&& error) mutable {
    binding.reject(kj::mv(resolver), kj::mv(error));
  }));
}

void KvBinding::erase(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& binding = self(info);
  auto resolver = binding.makeResolver(info);
  auto rejectResolver = v8::Global<v8::Promise::Resolver>(
      binding.runtime.getIsolate(), resolver.Get(binding.runtime.getIsolate()));
  auto key = argument(binding.runtime.getIsolate(), binding.runtime.getContext(), info, 0);
  binding.tasks.add(binding.executor.erase(binding.nameSpace.cStr(), kj::mv(key)).then(
      [&binding, resolver = kj::mv(resolver)]() mutable {
    v8::Isolate::Scope isolateScope(binding.runtime.getIsolate());
    v8::HandleScope handles(binding.runtime.getIsolate());
    binding.resolve(kj::mv(resolver), v8::Undefined(binding.runtime.getIsolate()));
  }, [&binding, resolver = kj::mv(rejectResolver)](kj::Exception&& error) mutable {
    binding.reject(kj::mv(resolver), kj::mv(error));
  }));
}

void KvBinding::list(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& binding = self(info);
  auto resolver = binding.makeResolver(info);
  auto rejectResolver = v8::Global<v8::Promise::Resolver>(
      binding.runtime.getIsolate(), resolver.Get(binding.runtime.getIsolate()));
  auto prefix = argument(binding.runtime.getIsolate(), binding.runtime.getContext(), info, 0);
  auto limit = info.Length() > 1
      ? info[1]->Uint32Value(binding.runtime.getContext()).FromMaybe(100)
      : 100;
  binding.tasks.add(binding.executor.list(binding.nameSpace.cStr(), kj::mv(prefix), limit).then(
      [&binding, resolver = kj::mv(resolver)](ListResult&& page) mutable {
    auto* isolate = binding.runtime.getIsolate();
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handles(isolate);
    auto context = binding.runtime.getContext();
    v8::Context::Scope contextScope(context);
    auto keys = v8::Array::New(isolate, static_cast<int>(page.keys.size()));
    for (size_t i = 0; i < page.keys.size(); ++i) {
      keys->Set(context, static_cast<uint32_t>(i), jsString(isolate, page.keys[i].c_str())).Check();
    }
    auto result = v8::Object::New(isolate);
    result->Set(context, jsString(isolate, "keys"), keys).Check();
    result->Set(context, jsString(isolate, "complete"),
                v8::Boolean::New(isolate, page.complete)).Check();
    binding.resolve(kj::mv(resolver), result);
  }, [&binding, resolver = kj::mv(rejectResolver)](kj::Exception&& error) mutable {
    binding.reject(kj::mv(resolver), kj::mv(error));
  }));
}

void KvBinding::resolve(v8::Global<v8::Promise::Resolver> resolver,
                        v8::Local<v8::Value> value) {
  auto context = runtime.getContext();
  v8::Context::Scope contextScope(context);
  resolver.Get(runtime.getIsolate())->Resolve(context, value).Check();
  runtime.getIsolate()->PerformMicrotaskCheckpoint();
}

void KvBinding::reject(v8::Global<v8::Promise::Resolver> resolver,
                       kj::Exception&& error) {
  auto* isolate = runtime.getIsolate();
  v8::Isolate::Scope isolateScope(isolate);
  v8::HandleScope handles(isolate);
  auto context = runtime.getContext();
  v8::Context::Scope contextScope(context);
  auto message = jsString(isolate, error.getDescription());
  resolver.Get(isolate)->Reject(context, v8::Exception::Error(message)).Check();
  isolate->PerformMicrotaskCheckpoint();
}

void KvBinding::taskFailed(kj::Exception&& error) {
  KJ_LOG(ERROR, "KV binding task failed", error);
}
