#pragma once

#include <fstream>

#include <kj/string.h>

class AppendLog final {
 public:
  explicit AppendLog(kj::StringPtr path);

  void write(kj::StringPtr event, kj::StringPtr room, kj::StringPtr user,
             kj::StringPtr detail = nullptr);
  void flush();

 private:
  std::ofstream output;
};

kj::String jsonEscape(kj::StringPtr text);
