#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

struct hdr_histogram;

enum class LatencyMetric : uint8_t {
  EndToEnd,
  ZmqTransport,
  StrategyRisk,
  MpscQueue,
  FillRoundTrip,
  Count
};

class LatencyTracker {
public:
  LatencyTracker();
  ~LatencyTracker();

  LatencyTracker(const LatencyTracker &) = delete;
  LatencyTracker &operator=(const LatencyTracker &) = delete;
  LatencyTracker(LatencyTracker &&) = delete;
  LatencyTracker &operator=(LatencyTracker &&) = delete;

  void record(LatencyMetric metric, uint64_t nanos) noexcept;
  void recordOrderSubmit(std::string_view client_order_id);
  void recordFillReceived(std::string_view client_order_id);
  void dump(const std::string &output_dir = ".") const;

  [[nodiscard]] int64_t percentile(LatencyMetric metric, double pct) const;
  [[nodiscard]] int64_t count(LatencyMetric metric) const;

private:
  static constexpr size_t kMetricCount =
      static_cast<size_t>(LatencyMetric::Count);
  static constexpr int64_t kLowestTrackable = 1;
  static constexpr int64_t kHighestTrackable = 60'000'000'000LL;
  static constexpr int kSignificantDigits = 3;

  static constexpr std::array<const char *, kMetricCount> kMetricNames = {
      "end_to_end", "zmq_transport", "strategy_risk", "mpsc_queue",
      "fill_round_trip"};

  static constexpr std::array<const char *, kMetricCount> kMetricDescriptions = {
      "End-to-end hot path (WebSocket recv -> pre-REST call)",
      "ZMQ transport (pre-publish -> post-receive)",
      "Strategy + Risk check (post-ZMQ recv -> pre-queue push)",
      "MPSC queue (push -> pop)",
      "Fill round-trip [cold] (post-REST return -> fill received)"};

  void dumpOne(LatencyMetric metric, const std::string &output_dir) const;

  std::array<hdr_histogram *, kMetricCount> histograms_{};

  mutable std::mutex submit_mutex_;
  std::unordered_map<std::string, uint64_t> submit_timestamps_;
};
