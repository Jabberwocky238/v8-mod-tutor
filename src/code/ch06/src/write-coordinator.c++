#include "write-coordinator.h"

#include <utility>

#include <kj/debug.h>

WriteCoordinator::WriteCoordinator(KvStore& store)
    : WriteCoordinator(store, Limits{}) {}

WriteCoordinator::WriteCoordinator(KvStore& store, Limits limits)
    : store(store), limits(limits) {
  KJ_REQUIRE(limits.maxOperations > 0, "maxOperations must be positive");
  KJ_REQUIRE(limits.maxBytes > 0, "maxBytes must be positive");
}

WriteCoordinator::~WriteCoordinator() noexcept {
  try { flush(); } catch (...) {}
}

void WriteCoordinator::put(std::string nameSpace, std::string key,
                           std::string value) {
  add({WriteOperation::Type::PUT, std::move(nameSpace), std::move(key),
       std::move(value)});
}

void WriteCoordinator::erase(std::string nameSpace, std::string key) {
  add({WriteOperation::Type::DELETE, std::move(nameSpace), std::move(key), {}});
}

void WriteCoordinator::add(WriteOperation operation) {
  auto bytes = operation.nameSpace.size() + operation.key.size() +
               operation.value.size();
  if (!pending.empty() && pendingBytes + bytes > limits.maxBytes) flush();
  pendingBytes += bytes;
  pending.push_back(std::move(operation));
  ++operationCount;
  if (pending.size() >= limits.maxOperations || pendingBytes >= limits.maxBytes)
    flush();
}

void WriteCoordinator::flush() {
  if (pending.empty()) return;
  store.writeBatch(pending);
  ++calls;
  pending.clear();
  pendingBytes = 0;
}
