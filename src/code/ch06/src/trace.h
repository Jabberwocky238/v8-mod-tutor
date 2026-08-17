#pragma once

#include <cstdint>

#include <kj/string.h>
#include <kj/timer.h>

enum class SpanStatus { UNSET, OK, ERROR };

struct TraceId { uint64_t high; uint64_t low; };

struct TraceRecord {
  TraceId traceId;
  uint64_t spanId;
  uint64_t parentSpanId;
  kj::String name;
  kj::Duration duration;
  SpanStatus status;
};

class TraceWriter;

class Span final {
 public:
  Span(TraceWriter& writer, TraceId traceId, uint64_t spanId,
       uint64_t parentSpanId, kj::String name,
       const kj::MonotonicClock& clock);
  ~Span() noexcept;

  Span(Span&&) noexcept;
  Span& operator=(Span&&) noexcept;
  void finish(SpanStatus status = SpanStatus::OK) noexcept;

 private:
  TraceWriter* writer;
  TraceId traceId;
  uint64_t spanId;
  uint64_t parentSpanId;
  kj::String name;
  const kj::MonotonicClock* clock;
  kj::TimePoint started;
  bool finished = false;
};
