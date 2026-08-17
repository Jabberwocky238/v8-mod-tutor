#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <kj/test.h>

#include "kv-store.h"
#include "trace.h"
#include "write-coordinator.h"

namespace {
class TempDirectory final {
 public:
  TempDirectory() {
    static size_t next = 0;
    path = std::filesystem::temp_directory_path() /
        ("v8lab-ch06-" + std::to_string(++next));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDirectory() { std::filesystem::remove_all(path); }
  std::filesystem::path path;
};

std::vector<std::string> lines(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<std::string> result;
  for (std::string line; std::getline(input, line);) result.push_back(line);
  return result;
}
}  // namespace

KJ_TEST("nested spans preserve trace and parent identifiers") {
  TempDirectory temp;
  uint64_t now = 10;
  TraceWriter writer(temp.path / "trace.jsonl", {.startThread = false});
  TraceContext context(writer, [&] { return now; });
  auto root = context.span("http.request");
  auto child = context.span("rocksdb.write", root.id());
  child.finish();
  root.finish();
  writer.flush();
  auto output = lines(temp.path / "trace.jsonl");
  KJ_REQUIRE(output.size() == 2);
  KJ_EXPECT(output[0].find("\"span_id\":2,\"parent_span_id\":1") !=
            std::string::npos);
  auto traceId = output[0].substr(output[0].find("\"trace_id\""), 46);
  KJ_EXPECT(output[1].find(traceId) != std::string::npos);
}

KJ_TEST("span duration uses a monotonic clock and finish is idempotent") {
  TempDirectory temp;
  uint64_t now = 1'000;
  TraceWriter writer(temp.path / "trace.jsonl", {.startThread = false});
  TraceContext context(writer, [&] { return now; });
  {
    auto span = context.span("timer.wait");
    now += 30'000'000;
    span.finish();
    span.finish(SpanStatus::ERROR);
  }
  writer.flush();
  auto output = lines(temp.path / "trace.jsonl");
  KJ_REQUIRE(output.size() == 1);
  KJ_EXPECT(output[0].find("\"duration_ns\":30000000") != std::string::npos);
  KJ_EXPECT(output[0].find("\"status\":\"ok\"") != std::string::npos);
}

KJ_TEST("span destructor records an unfinished scope") {
  TempDirectory temp;
  uint64_t now = 20;
  TraceWriter writer(temp.path / "trace.jsonl", {.startThread = false});
  TraceContext context(writer, [&] { return now; });
  { auto span = context.span("scope.exit"); now = 25; }
  writer.flush();
  auto output = lines(temp.path / "trace.jsonl");
  KJ_REQUIRE(output.size() == 1);
  KJ_EXPECT(output[0].find("\"duration_ns\":5") != std::string::npos);
  KJ_EXPECT(output[0].find("\"status\":\"unset\"") != std::string::npos);
}

KJ_TEST("TraceWriter drops at capacity without blocking the caller") {
  TempDirectory temp;
  TraceWriter writer(temp.path / "trace.jsonl",
                     {.capacity = 2, .batchSize = 2, .startThread = false});
  TraceRecord record{{1, 2}, 1, 0, "one", 0, 1, SpanStatus::OK};
  KJ_EXPECT(writer.submit(record));
  record.name = "two";
  KJ_EXPECT(writer.submit(record));
  record.name = "three";
  KJ_EXPECT(!writer.submit(record));
  KJ_EXPECT(writer.queueDepth() == 2);
  KJ_EXPECT(writer.dropped() == 1);
}

KJ_TEST("TraceWriter writes escaped JSONL records in submission order") {
  TempDirectory temp;
  TraceWriter writer(temp.path / "trace.jsonl",
                     {.capacity = 4, .batchSize = 2, .startThread = false});
  writer.submit({{1, 1}, 1, 0, "first\nline", 10, 2, SpanStatus::OK});
  writer.submit({{1, 1}, 2, 1, "second", 12, 3, SpanStatus::ERROR});
  writer.flush();
  auto output = lines(temp.path / "trace.jsonl");
  KJ_REQUIRE(output.size() == 2);
  KJ_EXPECT(output[0].find("first\\nline") != std::string::npos);
  KJ_EXPECT(output[1].find("\"span_id\":2") != std::string::npos);
}

KJ_TEST("WriteCoordinator flushes at the operation threshold") {
  TempDirectory temp;
  auto dbPath = (temp.path / "db").string();
  KvStore store(dbPath.c_str());
  WriteCoordinator coordinator(store, {.maxOperations = 3});
  coordinator.put("test", "a", "1");
  coordinator.put("test", "b", "2");
  KJ_EXPECT(coordinator.writeCalls() == 0);
  coordinator.put("test", "c", "3");
  KJ_EXPECT(coordinator.writeCalls() == 1);
  KJ_EXPECT(store.get("test", "a") == std::optional<std::string>("1"));
  KJ_EXPECT(store.get("test", "c") == std::optional<std::string>("3"));
}

KJ_TEST("batched data and deletes survive reopening RocksDB") {
  TempDirectory temp;
  {
    auto dbPath = (temp.path / "db").string();
    KvStore store(dbPath.c_str());
    store.put("test", "gone", "old");
    WriteCoordinator coordinator(store, {.maxOperations = 8});
    coordinator.put("test", "kept", "value");
    coordinator.erase("test", "gone");
    coordinator.flush();
    KJ_EXPECT(coordinator.operations() == 2);
    KJ_EXPECT(coordinator.writeCalls() == 1);
  }
  auto dbPath = (temp.path / "db").string();
  KvStore reopened(dbPath.c_str());
  KJ_EXPECT(reopened.get("test", "kept") ==
            std::optional<std::string>("value"));
  KJ_EXPECT(!reopened.get("test", "gone").has_value());
}
