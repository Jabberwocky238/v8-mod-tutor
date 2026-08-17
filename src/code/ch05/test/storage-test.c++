#include <filesystem>
#include <optional>
#include <string>

#include <kj/async-io.h>
#include <kj/test.h>

#include "kv-binding.h"

namespace {

class TempDirectory {
 public:
  TempDirectory() {
    path = std::filesystem::temp_directory_path() /
        std::filesystem::path("v8-kv-" + std::to_string(counter++));
    std::filesystem::remove_all(path);
  }
  ~TempDirectory() { std::filesystem::remove_all(path); }
  static inline uint64_t counter = 1;
  std::filesystem::path path;
};

KJ_TEST("physical keys isolate namespaces") {
  auto one = encodeKey("one", "x");
  auto two = encodeKey("two", "x");
  KJ_EXPECT(one != two);
  KJ_EXPECT(one.size() == 5);
  KJ_EXPECT(one[3] == '\0');
}

KJ_TEST("KvStore supports put get delete and prefix list") {
  TempDirectory directory;
  KvStore store(directory.path.string().c_str());
  store.put("app", "room:a", "A");
  store.put("app", "room:b", "B");
  KJ_EXPECT(store.get("app", "room:a") == std::optional<std::string>("A"));
  auto page = store.list("app", "room:", 10);
  KJ_EXPECT(page.keys.size() == 2);
  KJ_EXPECT(page.complete);
  store.erase("app", "room:a");
  KJ_EXPECT(!store.get("app", "room:a").has_value());
}

KJ_TEST("StorageExecutor rejects work beyond its bound") {
  auto io = kj::setupAsyncIo();
  TempDirectory directory;
  KvStore store(directory.path.string().c_str());
  StorageExecutor executor(store, 1, 2, true);
  auto first = executor.get("app", "a");
  auto second = executor.get("app", "b");
  auto third = executor.get("app", "c");
  KJ_EXPECT(executor.queued() == 2);
  KJ_EXPECT_THROW_MESSAGE("StorageBusyError", third.wait(io.waitScope));
  executor.resume();
  first.wait(io.waitScope);
  second.wait(io.waitScope);
}

KJ_TEST("env KV resolves JavaScript promises on the runtime thread") {
  auto io = kj::setupAsyncIo();
  TempDirectory directory;
  Runtime runtime("storage-test", io.provider->getTimer());
  KvStore store(directory.path.string().c_str());
  StorageExecutor executor(store);
  KvBinding binding(runtime, executor, kj::str("app"));
  runtime.loadWorker(
      "globalThis.worker = { async fetch(req, env) {"
      "await env.KV.put('topic', 'V8');"
      "const value = await env.KV.get('topic');"
      "await env.KV.delete('topic');"
      "return new Response(value + ':' + (await env.KV.get('topic'))); } };");
  auto response = runtime.dispatch("GET", "/").wait(io.waitScope);
  KJ_EXPECT(response.body == "V8:null");
}

KJ_TEST("RocksDB data survives a clean reopen") {
  TempDirectory directory;
  {
    KvStore first(directory.path.string().c_str());
    first.put("app", "x", "saved", true);
  }
  {
    KvStore second(directory.path.string().c_str());
    KJ_EXPECT(second.get("app", "x") == std::optional<std::string>("saved"));
  }
}

}  // namespace
