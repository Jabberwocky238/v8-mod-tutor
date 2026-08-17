#include <kj/test.h>

#include "test-fixture.h"

KJ_TEST("cppgc keeps Request alive through its JavaScript wrapper") {
  FetchRuntimeFixture fixture;
  auto weak = fixture.makeWeakRequest("GET", "http://test/hello");
  fixture.collectGarbage();
  KJ_EXPECT(weak.isAlive());
  fixture.dropJavaScriptWrapper();
  fixture.collectGarbage();
  KJ_EXPECT(!weak.isAlive());
}

KJ_TEST("setTimeout resolves only after the requested delay") {
  FetchRuntimeFixture fixture;
  auto promise = fixture.evaluatePromise(
      "new Promise(resolve => setTimeout(() => resolve('done'), 30))");
  fixture.advanceTime(29 * kj::MILLISECONDS);
  KJ_EXPECT(promise.isPending());
  fixture.advanceTime(1 * kj::MILLISECONDS);
  KJ_EXPECT(promise.stringResult() == "done");
}

KJ_TEST("clearTimeout releases the callback without running it") {
  FetchRuntimeFixture fixture;
  fixture.evaluate("let fired = false; const id = setTimeout(() => fired = true, 10);"
                   "clearTimeout(id)");
  fixture.advanceTime(20 * kj::MILLISECONDS);
  KJ_EXPECT(fixture.evaluateString("String(fired)") == "false");
  KJ_EXPECT(fixture.activeTimerCount() == 0);
}

KJ_TEST("fetch waits for its returned promise before responding") {
  FetchRuntimeFixture fixture;
  fixture.loadWorker("export default { async fetch() {"
                     "await new Promise(r => setTimeout(r, 3000));"
                     "return new Response('ready'); } }");
  auto request = fixture.startRequest("GET", "/");
  fixture.advanceTime(2999 * kj::MILLISECONDS);
  KJ_EXPECT(request.isPending());
  fixture.advanceTime(1 * kj::MILLISECONDS);
  KJ_EXPECT(request.status() == 200);
  KJ_EXPECT(request.body() == "ready");
}

KJ_TEST("waitUntil outlives the response but remains request-owned") {
  FetchRuntimeFixture fixture;
  fixture.loadWorker("export default { fetch(req, env, ctx) {"
                     "ctx.waitUntil(new Promise(r => setTimeout(r, 250)));"
                     "return new Response('sent'); } }");
  auto request = fixture.startRequest("GET", "/");
  KJ_EXPECT(request.body() == "sent");
  KJ_EXPECT(fixture.backgroundTaskCount() == 1);
  fixture.advanceTime(250 * kj::MILLISECONDS);
  KJ_EXPECT(fixture.backgroundTaskCount() == 0);
}

KJ_TEST("a rejected fetch becomes HTTP 500 without killing the runtime") {
  FetchRuntimeFixture fixture;
  fixture.loadWorker("export default { fetch() { throw new Error('boom') } }");
  auto failed = fixture.request("GET", "/");
  KJ_EXPECT(failed.status == 500);
  fixture.loadWorker("export default { fetch() { return new Response('ok') } }");
  KJ_EXPECT(fixture.request("GET", "/").body == "ok");
}
