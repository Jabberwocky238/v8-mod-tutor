#include "websocket-session.h"

#include <kj/debug.h>

kj::Promise<void> WebSocketSession::run() {
  id = hub.join(room, user, *this);
  joined = true;
  return readLoop().then([this]() { cleanup(); }, [this](kj::Exception&& error) {
    cleanup();
    if (error.getType() != kj::Exception::Type::DISCONNECTED) {
      KJ_LOG(WARNING, "WebSocket session ended", error);
    }
  });
}

kj::Promise<void> WebSocketSession::readLoop() {
  return socket->receive(8192).then([this](kj::WebSocket::Message&& message)
      -> kj::Promise<void> {
    KJ_SWITCH_ONEOF(message) {
      KJ_CASE_ONEOF(text, kj::String) {
        hub.broadcast(room, id, text);
        return readLoop();
      }
      KJ_CASE_ONEOF(data, kj::Array<kj::byte>) {
        return socket->close(1003, "text messages only");
      }
      KJ_CASE_ONEOF(close, kj::WebSocket::Close) {
        return socket->close(close.code, close.reason);
      }
    }
    KJ_UNREACHABLE;
  });
}

bool WebSocketSession::enqueue(kj::StringPtr message) {
  if (queued >= 64) return false;
  ++queued;
  auto owned = kj::str(message);
  outgoing = outgoing.then([this, owned = kj::mv(owned)]() mutable {
    return socket->send(owned).attach(kj::mv(owned));
  }).then([this]() { --queued; }).eagerlyEvaluate(nullptr);
  return true;
}

void WebSocketSession::overloaded() {
  outgoing = outgoing.then([this]() {
    return socket->close(1013, "client is too slow");
  }).eagerlyEvaluate(nullptr);
}

void WebSocketSession::cleanup() {
  if (!joined) return;
  joined = false;
  hub.leave(room, id);
}
