#pragma once

#include <memory>

#include <kj/string.h>
#include <libplatform/libplatform.h>
#include <v8-context.h>
#include <v8-isolate.h>

class Engine final {
 public:
  explicit Engine(const char* executablePath);
  ~Engine() noexcept;

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  kj::String evaluate(kj::StringPtr source);

 private:
  std::unique_ptr<v8::Platform> platform;
  v8::ArrayBuffer::Allocator* allocator = nullptr;
  v8::Isolate* isolate = nullptr;
  v8::Global<v8::Context> context;
};
