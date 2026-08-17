#include "router.h"

Route classifyRoute(kj::StringPtr method, kj::StringPtr url, bool assetExists) {
  auto path = url.findFirst('?').map([&](size_t pos) { return url.slice(0, pos); })
      .orDefault(url);
  if (path == kj::StringPtr("/ws")) return Route::WEBSOCKET;
  if (path.startsWith(kj::StringPtr("/api/"))) return Route::FETCH;
  if (method != "GET") return Route::METHOD_NOT_ALLOWED;
  if (path.startsWith(kj::StringPtr("/assets/"))) return assetExists ? Route::ASSET : Route::NOT_FOUND;
  if (path == kj::StringPtr("/app.js") || path == kj::StringPtr("/style.css")) {
    return assetExists ? Route::ASSET : Route::NOT_FOUND;
  }
  return path.findLast('.').map([](size_t) { return Route::NOT_FOUND; })
      .orDefault(Route::SPA);
}
