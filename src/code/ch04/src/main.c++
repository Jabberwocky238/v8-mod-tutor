#include <fstream>
#include <sstream>

#include <kj/async-io.h>
#include <kj/compat/http.h>
#include <kj/debug.h>

#include "append-log.h"
#include "router-service.h"

int main(int argc, char** argv) {
  KJ_REQUIRE(argc == 5, "usage: v8-chat WORKER PUBLIC_DIR LOG_FILE ADDRESS");
  std::ifstream input(argv[1]);
  KJ_REQUIRE(input.good(), "cannot open worker", argv[1]);
  std::ostringstream source;
  source << input.rdbuf();
  auto sourceText = source.str();

  auto io = kj::setupAsyncIo();
  Runtime runtime(argv[0], io.provider->getTimer());
  runtime.loadWorker(kj::StringPtr(sourceText.c_str()));
  AppendLog log(argv[3]);
  ChatHub hub(log);
  kj::HttpHeaderTable::Builder tableBuilder;
  auto table = tableBuilder.build();
  RouterService service(runtime, *table, argv[2], hub);
  kj::HttpServer server(io.provider->getTimer(), *table, service);
  auto address = io.provider->getNetwork().parseAddress(argv[4]).wait(io.waitScope);
  auto listener = address->listen();
  KJ_LOG(INFO, "chatroom listening", argv[4]);
  server.listenHttp(*listener).wait(io.waitScope);
}
