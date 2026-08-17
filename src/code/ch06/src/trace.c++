#include "trace.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace {
const char* statusName(SpanStatus status) {
  switch (status) {
    case SpanStatus::OK: return "ok";
    case SpanStatus::ERROR: return "error";
    default: return "unset";
  }
}

std::string escapeJson(const std::string& value) {
  std::ostringstream out;
  for (unsigned char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) out << "\\u" << std::hex << std::setw(4)
                          << std::setfill('0') << static_cast<int>(c);
        else out << c;
    }
  }
  return out.str();
}
}  // namespace

TraceWriter::TraceWriter(std::filesystem::path path)
    : TraceWriter(std::move(path), Options{}) {}

TraceWriter::TraceWriter(std::filesystem::path path, Options options)
    : path(std::move(path)), options(options) {
  if (options.startThread) worker = std::thread([this] { run(); });
}

TraceWriter::~TraceWriter() noexcept {
  try {
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    changed.notify_all();
    if (worker.joinable()) worker.join();
    flush();
  } catch (...) {
  }
}

bool TraceWriter::submit(TraceRecord record) noexcept {
  try {
    std::lock_guard lock(mutex);
    if (queue.size() >= options.capacity) {
      ++droppedCount;
      return false;
    }
    queue.push_back(std::move(record));
    changed.notify_one();
    return true;
  } catch (...) {
    ++droppedCount;
    return false;
  }
}

std::vector<TraceRecord> TraceWriter::takeBatch() {
  std::vector<TraceRecord> batch;
  std::lock_guard lock(mutex);
  auto count = std::min(options.batchSize, queue.size());
  batch.reserve(count);
  while (count-- > 0) {
    batch.push_back(std::move(queue.front()));
    queue.pop_front();
  }
  return batch;
}

void TraceWriter::append(const std::vector<TraceRecord>& records) {
  if (records.empty()) return;
  std::lock_guard outputLock(outputMutex);
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::app);
  if (!out) throw std::runtime_error("cannot open trace file: " + path.string());
  for (const auto& record : records) {
    out << "{\"trace_id\":\"" << std::hex << std::setw(16)
        << std::setfill('0') << record.traceId.high << std::setw(16)
        << record.traceId.low << std::dec << "\",\"span_id\":"
        << record.spanId << ",\"parent_span_id\":" << record.parentSpanId
        << ",\"name\":\"" << escapeJson(record.name)
        << "\",\"started_ns\":" << record.startedNs
        << ",\"duration_ns\":" << record.durationNs
        << ",\"status\":\"" << statusName(record.status) << "\"}\n";
  }
  out.flush();
}

void TraceWriter::flush() {
  for (;;) {
    auto batch = takeBatch();
    if (batch.empty()) return;
    append(batch);
  }
}

void TraceWriter::run() {
  for (;;) {
    {
      std::unique_lock lock(mutex);
      changed.wait(lock, [this] { return stopping || !queue.empty(); });
      if (stopping && queue.empty()) return;
    }
    auto batch = takeBatch();
    try {
      append(batch);
    } catch (...) {
      droppedCount += batch.size();
    }
  }
}

size_t TraceWriter::queueDepth() const {
  std::lock_guard lock(mutex);
  return queue.size();
}

uint64_t TraceWriter::dropped() const { return droppedCount.load(); }

Span::Span(TraceWriter& writer, TraceId traceId, uint64_t spanId,
           uint64_t parentSpanId, std::string name,
           std::function<uint64_t()> now)
    : writer(&writer), traceId(traceId), spanId(spanId),
      parentSpanId(parentSpanId), name(std::move(name)), now(std::move(now)),
      startedNs(this->now()) {}

Span::~Span() noexcept { finish(SpanStatus::UNSET); }

Span::Span(Span&& other) noexcept
    : writer(other.writer), traceId(other.traceId), spanId(other.spanId),
      parentSpanId(other.parentSpanId), name(std::move(other.name)),
      now(std::move(other.now)), startedNs(other.startedNs),
      finished(other.finished) {
  other.finished = true;
}

Span& Span::operator=(Span&& other) noexcept {
  if (this != &other) {
    finish(SpanStatus::UNSET);
    writer = other.writer;
    traceId = other.traceId;
    spanId = other.spanId;
    parentSpanId = other.parentSpanId;
    name = std::move(other.name);
    now = std::move(other.now);
    startedNs = other.startedNs;
    finished = other.finished;
    other.finished = true;
  }
  return *this;
}

void Span::finish(SpanStatus status) noexcept {
  if (finished) return;
  finished = true;
  auto endedNs = now();
  writer->submit({traceId, spanId, parentSpanId, std::move(name), startedNs,
                  endedNs - startedNs, status});
}

TraceContext::TraceContext(TraceWriter& writer, std::function<uint64_t()> now)
    : writer(writer), now(std::move(now)) {
  std::random_device random;
  traceId = {static_cast<uint64_t>(random()) << 32 | random(),
             static_cast<uint64_t>(random()) << 32 | random()};
}

Span TraceContext::span(std::string name, uint64_t parentSpanId) {
  return Span(writer, traceId, nextSpanId++, parentSpanId, std::move(name), now);
}

uint64_t TraceContext::monotonicNow() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
