#include "kv-store.h"

#include <kj/debug.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>

namespace {

void validate(kj::StringPtr nameSpace, kj::StringPtr key) {
  KJ_REQUIRE(nameSpace.size() > 0 && nameSpace.size() <= 128, "invalid namespace");
  KJ_REQUIRE(nameSpace.findFirst('\0') == kj::none, "namespace contains NUL");
  KJ_REQUIRE(key.size() <= 1024, "key exceeds 1 KiB");
}

void requireOk(const rocksdb::Status& status, kj::StringPtr operation) {
  KJ_REQUIRE(status.ok(), operation, status.ToString().c_str());
}

}  // namespace

std::string encodeKey(kj::StringPtr nameSpace, kj::StringPtr key) {
  validate(nameSpace, key);
  std::string encoded(nameSpace.cStr(), nameSpace.size());
  encoded.push_back('\0');
  encoded.append(key.cStr(), key.size());
  return encoded;
}

KvStore::KvStore(kj::StringPtr directory) {
  rocksdb::Options options;
  options.create_if_missing = true;
  rocksdb::DB* opened = nullptr;
  requireOk(rocksdb::DB::Open(options, directory.cStr(), &opened), "open RocksDB");
  db.reset(opened);
}

std::optional<std::string> KvStore::get(kj::StringPtr nameSpace, kj::StringPtr key) {
  std::string value;
  auto status = db->Get(rocksdb::ReadOptions(), encodeKey(nameSpace, key), &value);
  if (status.IsNotFound()) return std::nullopt;
  requireOk(status, "get");
  return value;
}

void KvStore::put(kj::StringPtr nameSpace, kj::StringPtr key,
                  kj::StringPtr value, bool sync) {
  KJ_REQUIRE(value.size() <= 1024 * 1024, "value exceeds 1 MiB");
  rocksdb::WriteOptions options;
  options.sync = sync;
  requireOk(db->Put(options, encodeKey(nameSpace, key),
                    rocksdb::Slice(value.cStr(), value.size())), "put");
}

void KvStore::erase(kj::StringPtr nameSpace, kj::StringPtr key, bool sync) {
  rocksdb::WriteOptions options;
  options.sync = sync;
  requireOk(db->Delete(options, encodeKey(nameSpace, key)), "delete");
}

void KvStore::writeBatch(const std::vector<WriteOperation>& operations,
                         bool sync) {
  rocksdb::WriteBatch batch;
  for (const auto& operation : operations) {
    auto key = encodeKey(
        kj::StringPtr(operation.nameSpace.data(), operation.nameSpace.size()),
        kj::StringPtr(operation.key.data(), operation.key.size()));
    if (operation.type == WriteOperation::Type::PUT) {
      KJ_REQUIRE(operation.value.size() <= 1024 * 1024,
                 "value exceeds 1 MiB");
      batch.Put(key, operation.value);
    } else {
      batch.Delete(key);
    }
  }
  rocksdb::WriteOptions options;
  options.sync = sync;
  requireOk(db->Write(options, &batch), "write batch");
}

ListResult KvStore::list(kj::StringPtr nameSpace, kj::StringPtr prefix,
                         uint32_t limit) {
  KJ_REQUIRE(limit >= 1 && limit <= 1000, "list limit must be 1..1000");
  auto physicalPrefix = encodeKey(nameSpace, prefix);
  auto namespacePrefix = encodeKey(nameSpace, "");
  std::unique_ptr<rocksdb::Iterator> iterator(db->NewIterator(rocksdb::ReadOptions()));
  ListResult result{{}, true};
  for (iterator->Seek(physicalPrefix); iterator->Valid(); iterator->Next()) {
    auto key = iterator->key();
    if (!key.starts_with(physicalPrefix)) break;
    if (result.keys.size() == limit) {
      result.complete = false;
      break;
    }
    result.keys.emplace_back(key.data() + namespacePrefix.size(),
                             key.size() - namespacePrefix.size());
  }
  requireOk(iterator->status(), "list");
  return result;
}
