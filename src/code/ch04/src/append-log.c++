#include "append-log.h"

#include <chrono>
#include <filesystem>

#include <kj/debug.h>

kj::String jsonEscape(kj::StringPtr text) {
  kj::Vector<char> result;
  for (char c : text) {
    switch (c) {
      case '"': result.addAll(kj::StringPtr("\\\"").asArray()); break;
      case '\\': result.addAll(kj::StringPtr("\\\\").asArray()); break;
      case '\n': result.addAll(kj::StringPtr("\\n").asArray()); break;
      case '\r': result.addAll(kj::StringPtr("\\r").asArray()); break;
      case '\t': result.addAll(kj::StringPtr("\\t").asArray()); break;
      default:
        if (static_cast<unsigned char>(c) >= 0x20) result.add(c);
    }
  }
  result.add('\0');
  return kj::String(result.releaseAsArray());
}

AppendLog::AppendLog(kj::StringPtr path) {
  auto filePath = std::filesystem::path(path.cStr());
  if (filePath.has_parent_path()) std::filesystem::create_directories(filePath.parent_path());
  output.open(filePath, std::ios::app);
  KJ_REQUIRE(output.good(), "cannot open chat log", path);
}

void AppendLog::write(kj::StringPtr event, kj::StringPtr room,
                      kj::StringPtr user, kj::StringPtr detail) {
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  output << "{\"event\":\"" << jsonEscape(event).cStr()
         << "\",\"room\":\"" << jsonEscape(room).cStr()
         << "\",\"user\":\"" << jsonEscape(user).cStr() << '"';
  if (detail != nullptr) {
    output << ",\"detail\":\"" << jsonEscape(detail).cStr() << '"';
  }
  output << ",\"time\":" << now << "}\n";
  output.flush();
  KJ_REQUIRE(output.good(), "chat log write failed");
}

void AppendLog::flush() {
  output.flush();
  KJ_REQUIRE(output.good(), "chat log flush failed");
}
