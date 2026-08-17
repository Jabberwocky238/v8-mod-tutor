#pragma once

#include <kj/async.h>
#include <kj/compat/http.h>

#include "chat-hub.h"

class WebSocketSession final : public ChatClient {
 public:
  WebSocketSession(kj::Own<kj::WebSocket> socket, ChatHub& hub,
                   kj::String room, kj::String user)
      : socket(kj::mv(socket)), hub(hub), room(kj::mv(room)), user(kj::mv(user)) {}

  kj::Promise<void> run();
  bool enqueue(kj::StringPtr message) override;
  void overloaded() override;

 private:
  kj::Promise<void> readLoop();
  void cleanup();

  kj::Own<kj::WebSocket> socket;
  ChatHub& hub;
  kj::String room;
  kj::String user;
  kj::Promise<void> outgoing = kj::READY_NOW;
  size_t queued = 0;
  uint64_t id = 0;
  bool joined = false;
};
