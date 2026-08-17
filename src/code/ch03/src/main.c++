#include <fstream>
#include <sstream>

#include <kj/async-io.h>
#include <kj/compat/http.h>
#include <kj/debug.h>

#include "fetch-service.h"

int main(int argc, char** argv) {
  KJ_REQUIRE(argc == 3, "usage: v8-fetch WORKER ADDRESS");
  std::ifstream input(argv[1]);
  KJ_REQUIRE(input.good(), "cannot open worker", argv[1]);
  std::ostringstream source;
  source << input.rdbuf();

  auto io = kj::setupAsyncIo();
  Runtime runtime(argv[0], io.provider->getTimer());
  auto sourceText = source.str();
  runtime.loadWorker(kj::StringPtr(sourceText.c_str()));

  kj::HttpHeaderTable::Builder tableBuilder;
  auto table = tableBuilder.build();
  FetchService service(runtime, *table);
  kj::HttpServer server(io.provider->getTimer(), *table, service);
  auto address = io.provider->getNetwork().parseAddress(argv[2]).wait(io.waitScope);
  auto listener = address->listen();
  KJ_LOG(INFO, "listening", argv[2]);
  server.listenHttp(*listener).wait(io.waitScope);
}
