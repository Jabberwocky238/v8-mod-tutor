#include <kj/test.h>

#include "chat-fixture.h"

KJ_TEST("router sends API, WebSocket, assets, and SPA routes to distinct targets") {
  ChatFixture fixture;
  KJ_EXPECT(fixture.route("GET", "/api/topic") == Route::FETCH);
  KJ_EXPECT(fixture.route("GET", "/ws") == Route::WEBSOCKET);
  KJ_EXPECT(fixture.route("GET", "/assets/app.js") == Route::ASSET);
  KJ_EXPECT(fixture.route("GET", "/rooms/lobby") == Route::SPA);
  KJ_EXPECT(fixture.route("GET", "/assets/missing.js") == Route::NOT_FOUND);
}

KJ_TEST("ChatHub joins, broadcasts, and leaves in order") {
  ChatFixture fixture;
  auto alice = fixture.connect("lobby", "alice");
  auto bob = fixture.connect("lobby", "bob");
  alice.sendText("hello");
  KJ_EXPECT(alice.nextText().contains("hello"));
  KJ_EXPECT(bob.nextText().contains("hello"));
  alice.close(1000);
  KJ_EXPECT(fixture.memberCount("lobby") == 1);
}

KJ_TEST("a slow WebSocket is closed without blocking its room") {
  ChatFixture fixture;
  auto slow = fixture.connect("lobby", "slow", 64);
  auto fast = fixture.connect("lobby", "fast");
  slow.stopReading();
  for (auto i : kj::zeroTo(100)) fast.sendText(kj::str("message-", i));
  KJ_EXPECT(slow.closeCode() == 1013);
  KJ_EXPECT(fast.isOpen());
}

KJ_TEST("chat events are appended in causal order") {
  ChatFixture fixture;
  auto client = fixture.connect("lobby", "alice");
  client.sendText("hello");
  client.close(1000);
  fixture.flushLog();
  auto events = fixture.readLogEvents();
  KJ_EXPECT(events.size() == 3);
  KJ_EXPECT(events[0].type == "join");
  KJ_EXPECT(events[1].type == "message");
  KJ_EXPECT(events[2].type == "leave");
}
