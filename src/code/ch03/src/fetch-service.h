#pragma once

#include <kj/compat/http.h>

#include "runtime.h"

class FetchService final : public kj::HttpService {
 public:
  FetchService(Runtime& runtime, const kj::HttpHeaderTable& table)
      : runtime(runtime), table(table) {}

  kj::Promise<void> request(kj::HttpMethod method, kj::StringPtr url,
                            const kj::HttpHeaders& headers,
                            kj::AsyncInputStream& requestBody,
                            Response& response) override;

 private:
  Runtime& runtime;
  const kj::HttpHeaderTable& table;
};
