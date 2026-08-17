#include <kj/test.h>

#include "trace-fixture.h"

KJ_TEST("nested spans preserve trace and parent identifiers") {
  TraceFixture fixture;
  auto root = fixture.start("http.request");
  auto child = root.child("js.fetch");
  child.finish(Status::OK);
  root.finish(Status::OK);
  auto records = fixture.records();
  KJ_EXPECT(records[0].traceId == records[1].traceId);
  KJ_EXPECT(records[0].parentSpanId == records[1].spanId);
}

KJ_TEST("span duration uses the monotonic clock") {
  TraceFixture fixture;
  auto span = fixture.start("timer.wait");
  fixture.advanceTime(30 * kj::MILLISECONDS);
  span.finish(Status::OK);
  KJ_EXPECT(fixture.records()[0].duration == 30 * kj::MILLISECONDS);
}

KJ_TEST("TraceWriter drops at capacity without blocking the caller") {
  TraceFixture fixture(2);
  fixture.pauseWriter();
  fixture.submit(record("one"));
  fixture.submit(record("two"));
  fixture.submit(record("three"));
  KJ_EXPECT(fixture.queueDepth() == 2);
  KJ_EXPECT(fixture.dropped() == 1);
}

KJ_TEST("WriteCoordinator flushes at the operation threshold") {
  TraceFixture fixture;
  WriteCoordinator coordinator(fixture.db, {.maxOperations = 3});
  auto a = coordinator.put("a", "1");
  auto b = coordinator.put("b", "2");
  KJ_EXPECT(fixture.writeCalls() == 0);
  auto c = coordinator.put("c", "3");
  fixture.runReadyTasks();
  KJ_EXPECT(fixture.writeCalls() == 1);
  KJ_EXPECT(a.isFulfilled() && b.isFulfilled() && c.isFulfilled());
}

KJ_TEST("a failed WriteBatch rejects every member") {
  TraceFixture fixture;
  fixture.failNextWrite("disk full");
  WriteCoordinator coordinator(fixture.db, {.maxOperations = 2});
  auto a = coordinator.put("a", "1");
  auto b = coordinator.put("b", "2");
  fixture.runReadyTasks();
  KJ_EXPECT(a.rejection().contains("disk full"));
  KJ_EXPECT(b.rejection().contains("disk full"));
}
