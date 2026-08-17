#include "execution-context.h"

#include <v8-external.h>

#include "runtime.h"

void ExecutionContext::waitUntil(v8::Local<v8::Promise> promise) {
  ++pendingTasks;
  promises.emplace_back(runtime.getIsolate(), promise);
  auto callback = v8::Function::New(
      runtime.getContext(), settled,
      v8::External::New(runtime.getIsolate(), this)).ToLocalChecked();
  promise->Then(runtime.getContext(), callback, callback).ToLocalChecked();
}

void ExecutionContext::clear() {
  promises.clear();
  pendingTasks = 0;
}

void ExecutionContext::settled(const v8::FunctionCallbackInfo<v8::Value>& info) {
  auto& self = *static_cast<ExecutionContext*>(
      info.Data().As<v8::External>()->Value());
  if (self.pendingTasks > 0) --self.pendingTasks;
  if (info.Length() > 0) info.GetReturnValue().Set(info[0]);
}
