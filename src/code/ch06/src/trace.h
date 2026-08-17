#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class SpanStatus { UNSET, OK, ERROR };

struct TraceId {
  uint64_t high;
  uint64_t low;
  bool operator==(const TraceId&) const = default;
};

struct TraceRecord {
  TraceId traceId;
  uint64_t spanId;
  uint64_t parentSpanId;
  std::string name;
  uint64_t startedNs;
  uint64_t durationNs;
  SpanStatus status;
};

class TraceWriter final {
 public:
  struct Options {
    size_t capacity = 4096;
    size_t batchSize = 256;
    bool startThread = true;
  };

  explicit TraceWriter(std::filesystem::path path);
  TraceWriter(std::filesystem::path path, Options options);
  ~TraceWriter() noexcept;
  bool submit(TraceRecord record) noexcept;
  void flush();
  size_t queueDepth() const;
  uint64_t dropped() const;

 private:
  void run();
  std::vector<TraceRecord> takeBatch();
  void append(const std::vector<TraceRecord>& records);

  std::filesystem::path path;
  Options options;
  mutable std::mutex mutex;
  std::mutex outputMutex;
  std::condition_variable changed;
  std::deque<TraceRecord> queue;
  std::atomic<uint64_t> droppedCount = 0;
  bool stopping = false;
  std::thread worker;
};

class TraceContext;

class Span final {
 public:
  Span(TraceWriter& writer, TraceId traceId, uint64_t spanId,
       uint64_t parentSpanId, std::string name,
       std::function<uint64_t()> now);
  ~Span() noexcept;
  Span(Span&& other) noexcept;
  Span& operator=(Span&& other) noexcept;
  Span(const Span&) = delete;
  Span& operator=(const Span&) = delete;
  void finish(SpanStatus status = SpanStatus::OK) noexcept;
  uint64_t id() const { return spanId; }
  TraceId trace() const { return traceId; }

 private:
  TraceWriter* writer;
  TraceId traceId;
  uint64_t spanId;
  uint64_t parentSpanId;
  std::string name;
  std::function<uint64_t()> now;
  uint64_t startedNs;
  bool finished = false;
};

class TraceContext final {
 public:
  explicit TraceContext(TraceWriter& writer,
                        std::function<uint64_t()> now = monotonicNow);
  Span span(std::string name, uint64_t parentSpanId = 0);
  TraceId id() const { return traceId; }
  static uint64_t monotonicNow();

 private:
  TraceWriter& writer;
  std::function<uint64_t()> now;
  TraceId traceId;
  std::atomic<uint64_t> nextSpanId = 1;
};
