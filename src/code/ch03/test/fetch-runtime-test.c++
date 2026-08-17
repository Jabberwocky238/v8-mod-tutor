#include <kj/test.h>
#include <kj/timer.h>

#include "runtime.h"

namespace {

class Fixture {
 public:
  Fixture()
      : timer(kj::origin<kj::TimePoint>()), runtime("fetch-runtime-test", timer) {}

  void advance(kj::Duration amount) {
    timer.advanceTo(timer.now() + amount);
    loop.run();
  }

  kj::EventLoop loop;
  kj::WaitScope waitScope{loop};
  kj::TimerImpl timer;
  Runtime runtime;
};

KJ_TEST("setTimeout fires at the requested virtual time") {
  Fixture fixture;
  fixture.runtime.loadWorker(
      "globalThis.fired = false;"
      "setTimeout(() => globalThis.fired = true, 30);"
      "globalThis.worker = { fetch() { return new Response(String(fired)); } };");
  auto before = fixture.runtime.dispatch("GET", "/").wait(fixture.waitScope);
  KJ_EXPECT(before.body == "false");
  fixture.advance(29 * kj::MILLISECONDS);
  KJ_EXPECT(fixture.runtime.activeTimerCount() == 1);
  fixture.advance(1 * kj::MILLISECONDS);
  auto after = fixture.runtime.dispatch("GET", "/").wait(fixture.waitScope);
  KJ_EXPECT(after.body == "true");
  KJ_EXPECT(fixture.runtime.activeTimerCount() == 0);
}

KJ_TEST("clearTimeout removes the callback") {
  Fixture fixture;
  fixture.runtime.loadWorker(
      "let fired = false; const id = setTimeout(() => fired = true, 10);"
      "clearTimeout(id);"
      "globalThis.worker = { fetch() { return new Response(String(fired)); } };");
  fixture.advance(20 * kj::MILLISECONDS);
  auto response = fixture.runtime.dispatch("GET", "/").wait(fixture.waitScope);
  KJ_EXPECT(response.body == "false");
  KJ_EXPECT(fixture.runtime.activeTimerCount() == 0);
}

KJ_TEST("runtime can stop with a pending timer") {
  Fixture fixture;
  fixture.runtime.loadWorker(
      "setTimeout(() => {}, 10000);"
      "globalThis.worker = { fetch() { return new Response('ok'); } };");
  KJ_EXPECT(fixture.runtime.activeTimerCount() == 1);
}

KJ_TEST("fetch response waits three seconds") {
  Fixture fixture;
  fixture.runtime.loadWorker(
      "globalThis.worker = { async fetch() {"
      "await new Promise(resolve => setTimeout(resolve, 3000));"
      "return new Response('ready'); } };");
  auto promise = fixture.runtime.dispatch("GET", "/");
  fixture.advance(2999 * kj::MILLISECONDS);
  KJ_EXPECT(!promise.poll(fixture.waitScope));
  fixture.advance(1 * kj::MILLISECONDS);
  auto response = promise.wait(fixture.waitScope);
  KJ_EXPECT(response.status == 200);
  KJ_EXPECT(response.body == "ready");
}

KJ_TEST("waitUntil outlives the response") {
  Fixture fixture;
  fixture.runtime.loadWorker(
      "globalThis.worker = { fetch(req, env, ctx) {"
      "ctx.waitUntil(new Promise(resolve => setTimeout(resolve, 250)));"
      "return new Response('sent'); } };");
  auto response = fixture.runtime.dispatch("GET", "/").wait(fixture.waitScope);
  KJ_EXPECT(response.body == "sent");
  KJ_EXPECT(fixture.runtime.backgroundTaskCount() == 1);
  fixture.advance(250 * kj::MILLISECONDS);
  KJ_EXPECT(fixture.runtime.backgroundTaskCount() == 0);
}

KJ_TEST("a thrown fetch does not poison the isolate") {
  Fixture fixture;
  fixture.runtime.loadWorker(
      "globalThis.worker = { fetch() { throw new Error('boom'); } };");
  KJ_EXPECT_THROW_MESSAGE("fetch failed", fixture.runtime.dispatch("GET", "/"));
  fixture.runtime.loadWorker(
      "globalThis.worker = { fetch() { return new Response('ok'); } };");
  auto response = fixture.runtime.dispatch("GET", "/").wait(fixture.waitScope);
  KJ_EXPECT(response.body == "ok");
}

}  // namespace
