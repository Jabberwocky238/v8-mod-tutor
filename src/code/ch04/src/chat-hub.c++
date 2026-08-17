#include "chat-hub.h"

#include <algorithm>
#include <chrono>

#include <kj/debug.h>

uint64_t ChatHub::join(kj::StringPtr roomName, kj::StringPtr user,
                       ChatClient& client) {
  KJ_REQUIRE(roomName.size() > 0 && roomName.size() <= 64, "invalid room name");
  KJ_REQUIRE(user.size() > 0 && user.size() <= 32, "invalid user name");
  auto id = nextId++;
  rooms[std::string(roomName.cStr(), roomName.size())].members.push_back(
      {id, std::string(user.cStr(), user.size()), &client});
  log.write("join", roomName, user);
  return id;
}

void ChatHub::leave(kj::StringPtr roomName, uint64_t id) {
  auto found = rooms.find(std::string(roomName.cStr(), roomName.size()));
  if (found == rooms.end()) return;
  auto& members = found->second.members;
  auto member = std::find_if(members.begin(), members.end(),
                             [id](const Member& item) { return item.id == id; });
  if (member == members.end()) return;
  log.write("leave", roomName, kj::StringPtr(member->user.c_str()));
  members.erase(member);
}

void ChatHub::broadcast(kj::StringPtr roomName, uint64_t senderId,
                        kj::StringPtr text) {
  KJ_REQUIRE(text.size() <= 4096, "message exceeds 4 KiB");
  auto found = rooms.find(std::string(roomName.cStr(), roomName.size()));
  KJ_REQUIRE(found != rooms.end(), "room does not exist");
  auto sender = std::find_if(found->second.members.begin(), found->second.members.end(),
                             [senderId](const Member& item) { return item.id == senderId; });
  KJ_REQUIRE(sender != found->second.members.end(), "sender is not in room");
  auto escapedUser = jsonEscape(kj::StringPtr(sender->user.c_str()));
  auto escapedText = jsonEscape(text);
  auto message = kj::str("{\"type\":\"message\",\"room\":\"", jsonEscape(roomName),
                         "\",\"user\":\"", escapedUser, "\",\"text\":\"",
                         escapedText, "\"}");
  auto& history = found->second.history;
  history.emplace_back(message.cStr(), message.size());
  if (history.size() > 50) history.pop_front();
  log.write("message", roomName, kj::StringPtr(sender->user.c_str()), text);

  std::vector<uint64_t> overloaded;
  for (auto& member : found->second.members) {
    if (!member.client->enqueue(message)) {
      member.client->overloaded();
      overloaded.push_back(member.id);
    }
  }
  for (auto id : overloaded) leave(roomName, id);
}

size_t ChatHub::memberCount(kj::StringPtr roomName) const {
  auto found = rooms.find(std::string(roomName.cStr(), roomName.size()));
  return found == rooms.end() ? 0 : found->second.members.size();
}

const std::deque<std::string>& ChatHub::history(kj::StringPtr roomName) const {
  static const std::deque<std::string> empty;
  auto found = rooms.find(std::string(roomName.cStr(), roomName.size()));
  return found == rooms.end() ? empty : found->second.history;
}
