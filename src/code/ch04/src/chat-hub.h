#pragma once

#include <cstdint>

#include <kj/async.h>
#include <kj/map.h>
#include <kj/string.h>
#include <kj/vector.h>

class WebSocketSession;

class ChatHub final {
 public:
  struct Client {
    uint64_t id;
    kj::String user;
    WebSocketSession* session;
  };

  uint64_t join(kj::StringPtr room, kj::StringPtr user,
                WebSocketSession& session);
  void leave(kj::StringPtr room, uint64_t id);
  kj::Promise<void> broadcast(
      kj::StringPtr room, kj::ArrayPtr<const kj::byte> message);
  size_t memberCount(kj::StringPtr room) const;

 private:
  kj::HashMap<kj::String, kj::Vector<Client>> rooms;
  uint64_t nextId = 1;
};
