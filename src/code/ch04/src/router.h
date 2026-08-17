#pragma once

#include <kj/string.h>

enum class Route { FETCH, WEBSOCKET, ASSET, SPA, NOT_FOUND, METHOD_NOT_ALLOWED };

Route classifyRoute(kj::StringPtr method, kj::StringPtr url, bool assetExists);
