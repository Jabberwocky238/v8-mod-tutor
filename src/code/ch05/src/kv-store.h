#pragma once

#include <cstdint>

#include <kj/array.h>
#include <kj/maybe.h>
#include <kj/string.h>
#include <rocksdb/db.h>

struct ListResult {
  kj::Array<kj::String> keys;
  kj::Maybe<kj::String> cursor;
  bool complete;
};

class KvStore final {
 public:
  explicit KvStore(kj::StringPtr directory);
  ~KvStore() noexcept;

  kj::Maybe<kj::Array<kj::byte>> get(
      kj::StringPtr nameSpace, kj::StringPtr key);
  void put(kj::StringPtr nameSpace, kj::StringPtr key,
           kj::ArrayPtr<const kj::byte> value);
  void erase(kj::StringPtr nameSpace, kj::StringPtr key);
  ListResult list(kj::StringPtr nameSpace, kj::StringPtr prefix,
                  kj::Maybe<kj::StringPtr> cursor, uint32_t limit);

 private:
  rocksdb::DB* db = nullptr;
  rocksdb::ColumnFamilyHandle* metadata = nullptr;
  rocksdb::ColumnFamilyHandle* kv = nullptr;
};
