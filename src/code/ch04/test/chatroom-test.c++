#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <kj/test.h>

#include "append-log.h"
#include "chat-hub.h"
#include "router.h"

namespace {

class FakeClient final : public ChatClient {
 public:
  explicit FakeClient(size_t capacity = 64) : capacity(capacity) {}
  bool enqueue(kj::StringPtr message) override {
    if (messages.size() >= capacity) return false;
    messages.emplace_back(message.cStr(), message.size());
    return true;
  }
  void overloaded() override { closeCode = 1013; }

  size_t capacity;
  std::vector<std::string> messages;
  uint16_t closeCode = 0;
};

class Fixture {
 public:
  Fixture()
      : path(std::filesystem::temp_directory_path() / "v8-chatroom-test.log"),
        log((std::filesystem::remove(path), path.string().c_str())), hub(log) {}
  ~Fixture() { std::filesystem::remove(path); }

  std::string readLog() {
    log.flush();
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  std::filesystem::path path;
  AppendLog log;
  ChatHub hub;
};

KJ_TEST("router separates API WebSocket assets and SPA") {
  KJ_EXPECT(classifyRoute("GET", "/api/topic", false) == Route::FETCH);
  KJ_EXPECT(classifyRoute("GET", "/ws?room=lobby", false) == Route::WEBSOCKET);
  KJ_EXPECT(classifyRoute("GET", "/app.js", true) == Route::ASSET);
  KJ_EXPECT(classifyRoute("GET", "/rooms/lobby", false) == Route::SPA);
  KJ_EXPECT(classifyRoute("GET", "/missing.js", false) == Route::NOT_FOUND);
  KJ_EXPECT(classifyRoute("POST", "/rooms/lobby", false) == Route::METHOD_NOT_ALLOWED);
}

KJ_TEST("ChatHub broadcasts and keeps in-memory history") {
  Fixture fixture;
  FakeClient alice;
  FakeClient bob;
  auto aliceId = fixture.hub.join("lobby", "alice", alice);
  auto bobId = fixture.hub.join("lobby", "bob", bob);
  fixture.hub.broadcast("lobby", aliceId, "hello");
  KJ_EXPECT(alice.messages.size() == 1);
  KJ_EXPECT(bob.messages.size() == 1);
  KJ_EXPECT(kj::StringPtr(bob.messages[0].c_str()).contains("hello"));
  KJ_EXPECT(fixture.hub.history("lobby").size() == 1);
  fixture.hub.leave("lobby", bobId);
  KJ_EXPECT(fixture.hub.memberCount("lobby") == 1);
}

KJ_TEST("a slow client is removed without blocking its room") {
  Fixture fixture;
  FakeClient slow(1);
  FakeClient fast;
  auto slowId = fixture.hub.join("lobby", "slow", slow);
  auto fastId = fixture.hub.join("lobby", "fast", fast);
  fixture.hub.broadcast("lobby", fastId, "one");
  fixture.hub.broadcast("lobby", fastId, "two");
  KJ_EXPECT(slow.closeCode == 1013);
  KJ_EXPECT(fixture.hub.memberCount("lobby") == 1);
  KJ_EXPECT(fast.messages.size() == 2);
  KJ_EXPECT(slowId != fastId);
}

KJ_TEST("chat log preserves join message leave order") {
  Fixture fixture;
  FakeClient alice;
  auto id = fixture.hub.join("lobby", "alice", alice);
  fixture.hub.broadcast("lobby", id, "hello\nworld");
  fixture.hub.leave("lobby", id);
  auto text = fixture.readLog();
  auto join = text.find("\"event\":\"join\"");
  auto message = text.find("\"event\":\"message\"");
  auto leave = text.find("\"event\":\"leave\"");
  KJ_EXPECT(join < message && message < leave);
  KJ_EXPECT(text.find("hello\\nworld") != std::string::npos);
}

}  // namespace
