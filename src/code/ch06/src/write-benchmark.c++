#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "kv-store.h"
#include "write-coordinator.h"

namespace {
using Clock = std::chrono::steady_clock;

long elapsedMs(Clock::time_point started) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started)
      .count();
}
}  // namespace

int main(int argc, char** argv) {
  size_t count = argc > 1 ? std::stoull(argv[1]) : 10'000;
  auto root = std::filesystem::temp_directory_path() / "v8lab-write-benchmark";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::string value(1024, 'x');

  {
    auto dbPath = (root / "individual").string();
    KvStore store(dbPath.c_str());
    auto started = Clock::now();
    for (size_t i = 0; i < count; ++i) {
      auto key = std::to_string(i);
      store.put("bench", key.c_str(), value.c_str());
    }
    std::cout << "mode=individual operations=" << count
              << " write_calls=" << count
              << " elapsed_ms=" << elapsedMs(started) << '\n';
  }
  {
    auto dbPath = (root / "batch128").string();
    KvStore store(dbPath.c_str());
    WriteCoordinator coordinator(store, {.maxOperations = 128});
    auto started = Clock::now();
    for (size_t i = 0; i < count; ++i)
      coordinator.put("bench", std::to_string(i), value);
    coordinator.flush();
    auto duration = elapsedMs(started);
    std::cout << "mode=batch128 operations=" << coordinator.operations()
              << " write_calls=" << coordinator.writeCalls()
              << " elapsed_ms=" << duration << '\n';
  }
  std::filesystem::remove_all(root);
}
