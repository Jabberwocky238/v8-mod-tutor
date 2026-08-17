#include "fetch-service.h"

kj::Promise<void> FetchService::request(
    kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders&,
    kj::AsyncInputStream&, Response& response) {
  auto methodText = kj::str(method);
  auto ownedUrl = kj::str(url);
  return runtime.dispatch(methodText, ownedUrl).then(
      [&response, this](FetchResponse&& result) -> kj::Promise<void> {
    kj::HttpHeaders headers(table);
    headers.setPtr(kj::HttpHeaderId::CONTENT_TYPE, result.contentType);
    auto body = response.send(result.status, result.status == 200 ? "OK" : "Error",
                              headers, result.body.size());
    auto bytes = result.body.asBytes();
    return body->write(bytes).attach(kj::mv(body), kj::mv(result.body));
  });
}
