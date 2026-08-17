#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <kj/string.h>

#include "append-log.h"

class ChatClient {
 public:
  virtual ~ChatClient() noexcept(false) = default;
  virtual bool enqueue(kj::StringPtr message) = 0;
  virtual void overloaded() = 0;
};

class ChatHub final {
 public:
  explicit ChatHub(AppendLog& log) : log(log) {}

  uint64_t join(kj::StringPtr room, kj::StringPtr user, ChatClient& client);
  void leave(kj::StringPtr room, uint64_t id);
  void broadcast(kj::StringPtr room, uint64_t senderId, kj::StringPtr text);
  size_t memberCount(kj::StringPtr room) const;
  const std::deque<std::string>& history(kj::StringPtr room) const;

 private:
  struct Member {
    uint64_t id;
    std::string user;
    ChatClient* client;
  };
  struct Room {
    std::vector<Member> members;
    std::deque<std::string> history;
  };

  AppendLog& log;
  std::unordered_map<std::string, Room> rooms;
  uint64_t nextId = 1;
};
