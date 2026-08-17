#include <kj/test.h>

#include "storage-fixture.h"

KJ_TEST("physical keys isolate namespaces") {
  KJ_EXPECT(encodeKey("one", "x") != encodeKey("two", "x"));
  KJ_EXPECT(encodeKey("one", "x").startsWith("one\0"_kjb));
}

KJ_TEST("KvStore supports put, get, delete, and prefix list") {
  StorageFixture fixture;
  fixture.store.put("app", "room:a", bytes("A"));
  fixture.store.put("app", "room:b", bytes("B"));
  KJ_EXPECT(asText(KJ_ASSERT_NONNULL(fixture.store.get("app", "room:a"))) == "A");
  auto page = fixture.store.list("app", "room:", kj::none, 10);
  KJ_EXPECT(page.keys.size() == 2);
  fixture.store.erase("app", "room:a");
  KJ_EXPECT(fixture.store.get("app", "room:a") == kj::none);
}

KJ_TEST("StorageExecutor reports saturation instead of growing forever") {
  StorageFixture fixture(2);
  auto first = fixture.executor.pauseAndSubmitGet("a");
  auto second = fixture.executor.submitGet("b");
  auto third = fixture.executor.submitGet("c");
  KJ_EXPECT(first.isPending());
  KJ_EXPECT(second.isPending());
  KJ_EXPECT_THROW_MESSAGE("StorageBusyError", third.wait(fixture.waitScope));
}

KJ_TEST("env.KV resolves on the runtime thread") {
  StorageFixture fixture;
  fixture.evaluate("await env.KV.put('topic', 'V8');");
  KJ_EXPECT(fixture.evaluateString("await env.KV.get('topic')") == "V8");
  KJ_EXPECT(fixture.lastResolutionThread() == fixture.runtimeThread());
}

KJ_TEST("RocksDB data survives a clean reopen") {
  auto directory = makeTemporaryDirectory();
  { StorageFixture first(directory); first.store.put("app", "x", bytes("saved")); }
  { StorageFixture second(directory);
    KJ_EXPECT(asText(KJ_ASSERT_NONNULL(second.store.get("app", "x"))) == "saved"); }
}
