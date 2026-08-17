#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "kv-store.h"

class WriteCoordinator final {
 public:
  struct Limits {
    size_t maxOperations = 128;
    size_t maxBytes = 1u << 20;
  };

  explicit WriteCoordinator(KvStore& store);
  WriteCoordinator(KvStore& store, Limits limits);
  ~WriteCoordinator() noexcept;
  void put(std::string nameSpace, std::string key, std::string value);
  void erase(std::string nameSpace, std::string key);
  void flush();
  size_t writeCalls() const { return calls; }
  size_t operations() const { return operationCount; }

 private:
  void add(WriteOperation operation);
  KvStore& store;
  Limits limits;
  std::vector<WriteOperation> pending;
  size_t pendingBytes = 0;
  size_t calls = 0;
  size_t operationCount = 0;
};
