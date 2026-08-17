#include <kj/debug.h>
#include <kj/test.h>

#include "engine.h"

namespace {

Engine& engine() {
  static Engine instance("engine-test");
  return instance;
}

KJ_TEST("Engine evaluates a JavaScript expression") {
  KJ_EXPECT(engine().evaluate("'Hello' + ', World!'") == "Hello, World!");
}

KJ_TEST("Engine translates a JavaScript exception") {
  KJ_EXPECT_THROW_MESSAGE(
      "JavaScript evaluation failed", engine().evaluate("throw new Error('boom')"));
}

}  // namespace
