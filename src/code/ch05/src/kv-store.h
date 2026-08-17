#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kj/string.h>
#include <rocksdb/db.h>

struct ListResult {
  std::vector<std::string> keys;
  bool complete;
};

struct WriteOperation {
  enum class Type { PUT, DELETE };
  Type type;
  std::string nameSpace;
  std::string key;
  std::string value;
};

std::string encodeKey(kj::StringPtr nameSpace, kj::StringPtr key);

class KvStore final {
 public:
  explicit KvStore(kj::StringPtr directory);
  ~KvStore() noexcept = default;

  std::optional<std::string> get(kj::StringPtr nameSpace, kj::StringPtr key);
  void put(kj::StringPtr nameSpace, kj::StringPtr key, kj::StringPtr value,
           bool sync = false);
  void erase(kj::StringPtr nameSpace, kj::StringPtr key, bool sync = false);
  void writeBatch(const std::vector<WriteOperation>& operations,
                  bool sync = false);
  ListResult list(kj::StringPtr nameSpace, kj::StringPtr prefix, uint32_t limit);

 private:
  std::unique_ptr<rocksdb::DB> db;
};
