#include <fstream>
#include <iostream>
#include <sstream>

#include <kj/async-io.h>
#include <kj/debug.h>

#include "kv-binding.h"

int main(int argc, char** argv) {
  KJ_REQUIRE(argc == 3, "usage: v8-kv WORKER DB_DIRECTORY");
  std::ifstream input(argv[1]);
  KJ_REQUIRE(input.good(), "cannot open worker", argv[1]);
  std::ostringstream source;
  source << input.rdbuf();
  auto sourceText = source.str();

  auto io = kj::setupAsyncIo();
  Runtime runtime(argv[0], io.provider->getTimer());
  KvStore store(argv[2]);
  StorageExecutor executor(store);
  KvBinding binding(runtime, executor, kj::str("tutorial"));
  runtime.loadWorker(kj::StringPtr(sourceText.c_str()));
  auto response = runtime.dispatch("GET", "/").wait(io.waitScope);
  std::cout << response.body.cStr() << '\n';
}
