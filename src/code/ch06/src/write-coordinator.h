#pragma once

#include <kj/async.h>
#include <kj/string.h>
#include <kj/timer.h>
#include <kj/vector.h>
#include <rocksdb/db.h>

class WriteCoordinator final {
 public:
  struct Limits {
    size_t maxOperations = 128;
    size_t maxBytes = 1u << 20;
    kj::Duration maxDelay = kj::MILLISECONDS;
  };

  WriteCoordinator(rocksdb::DB& db, rocksdb::ColumnFamilyHandle& kv,
                   kj::Timer& timer, Limits limits = {});

  kj::Promise<void> put(kj::String key, kj::String value);
  kj::Promise<void> erase(kj::String key);
  kj::Promise<void> flush();

 private:
  struct Pending;
  void scheduleFlush();

  rocksdb::DB& db;
  rocksdb::ColumnFamilyHandle& kv;
  kj::Timer& timer;
  Limits limits;
  kj::Vector<Pending> pending;
  size_t pendingBytes = 0;
  kj::Promise<void> scheduled = nullptr;
};
