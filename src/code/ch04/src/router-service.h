#pragma once

#include <filesystem>

#include <kj/async.h>
#include <kj/compat/http.h>

#include "chat-hub.h"
#include "fetch-service.h"

class RouterService final : public kj::HttpService {
 public:
  RouterService(Runtime& runtime, const kj::HttpHeaderTable& table,
                std::filesystem::path publicRoot, ChatHub& hub)
      : api(runtime, table), table(table), publicRoot(kj::mv(publicRoot)),
        hub(hub) {}

  kj::Promise<void> request(kj::HttpMethod method, kj::StringPtr url,
                            const kj::HttpHeaders& headers,
                            kj::AsyncInputStream& requestBody,
                            Response& response) override;

 private:
  kj::Promise<void> serveFile(kj::StringPtr path, Response& response);
  FetchService api;
  const kj::HttpHeaderTable& table;
  std::filesystem::path publicRoot;
  ChatHub& hub;
};
