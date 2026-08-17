#include "router-service.h"

#include <fstream>
#include <sstream>

#include <kj/debug.h>

#include "router.h"
#include "websocket-session.h"

namespace {

kj::String queryValue(kj::StringPtr url, kj::StringPtr key, kj::StringPtr fallback) {
  KJ_IF_SOME(mark, url.findFirst('?')) {
    auto query = url.slice(mark + 1);
    while (query.size() > 0) {
      auto end = query.findFirst('&').orDefault(query.size());
      auto item = query.slice(0, end);
      KJ_IF_SOME(equal, item.findFirst('=')) {
        if (item.slice(0, equal) == key) return kj::str(item.slice(equal + 1));
      }
      if (end == query.size()) break;
      query = query.slice(end + 1);
    }
  }
  return kj::str(fallback);
}

}  // namespace

kj::Promise<void> RouterService::request(
    kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
    kj::AsyncInputStream& requestBody, Response& response) {
  auto pathEnd = url.findFirst('?').orDefault(url.size());
  auto path = url.slice(0, pathEnd);
  auto relative = path == kj::StringPtr("/")
      ? kj::str("index.html")
      : kj::str(path.slice(1));
  auto candidate = publicRoot / std::string(relative.cStr(), relative.size());
  auto exists = std::filesystem::is_regular_file(candidate);
  switch (classifyRoute(kj::str(method), url, exists)) {
    case Route::FETCH:
      return api.request(method, url, headers, requestBody, response);
    case Route::WEBSOCKET: {
      if (!headers.isWebSocket()) {
        kj::HttpHeaders responseHeaders(table);
        response.send(426, "Upgrade Required", responseHeaders, uint64_t(0));
        return kj::READY_NOW;
      }
      kj::HttpHeaders responseHeaders(table);
      auto socket = response.acceptWebSocket(responseHeaders);
      auto session = kj::heap<WebSocketSession>(
          kj::mv(socket), hub, queryValue(url, "room", "lobby"),
          queryValue(url, "name", "guest"));
      return session->run().attach(kj::mv(session));
    }
    case Route::ASSET:
      return serveFile(relative, response);
    case Route::SPA:
      return serveFile("index.html", response);
    case Route::NOT_FOUND: {
      kj::HttpHeaders responseHeaders(table);
      response.send(404, "Not Found", responseHeaders, uint64_t(0));
      return kj::READY_NOW;
    }
    case Route::METHOD_NOT_ALLOWED: {
      kj::HttpHeaders responseHeaders(table);
      response.send(405, "Method Not Allowed", responseHeaders, uint64_t(0));
      return kj::READY_NOW;
    }
  }
  KJ_UNREACHABLE;
}

kj::Promise<void> RouterService::serveFile(kj::StringPtr relative,
                                           Response& response) {
  KJ_REQUIRE(!relative.contains(".."), "unsafe static path");
  auto path = publicRoot / std::string(relative.cStr(), relative.size());
  std::ifstream input(path, std::ios::binary);
  KJ_REQUIRE(input.good(), "static file disappeared", path.string().c_str());
  std::ostringstream content;
  content << input.rdbuf();
  auto bodyText = content.str();
  auto body = kj::heapArray<kj::byte>(bodyText.size());
  std::copy(bodyText.begin(), bodyText.end(), body.begin());
  kj::HttpHeaders headers(table);
  auto type = relative.endsWith(".js") ? "text/javascript; charset=utf-8"
      : relative.endsWith(".css") ? "text/css; charset=utf-8"
      : "text/html; charset=utf-8";
  headers.setPtr(kj::HttpHeaderId::CONTENT_TYPE, type);
  auto output = response.send(200, "OK", headers, body.size());
  return output->write(body).attach(kj::mv(output), kj::mv(body));
}
