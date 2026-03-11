#include "LatencyTracker.hpp"
#include "Utils.hpp"
#include <cstdio>
#include <fstream>
#include <hdr/hdr_histogram.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

LatencyTracker::LatencyTracker() {
  for (size_t i = 0; i < kMetricCount; ++i) {
    if (hdr_init(kLowestTrackable, kHighestTrackable, kSignificantDigits,
                 &histograms_[i]) != 0) {
      for (size_t j = 0; j < i; ++j) {
        hdr_close(histograms_[j]);
      }
      throw std::runtime_error("Failed to initialize HdrHistogram");
    }
  }
}

LatencyTracker::~LatencyTracker() {
  for (auto *h : histograms_) {
    if (h) {
      hdr_close(h);
    }
  }
}

void LatencyTracker::record(LatencyMetric metric, uint64_t nanos) noexcept {
  const auto idx = static_cast<size_t>(metric);
  if (idx >= kMetricCount) [[unlikely]] {
    return;
  }
  hdr_record_value_atomic(histograms_[idx], static_cast<int64_t>(nanos));
}

void LatencyTracker::recordOrderSubmit(std::string_view client_order_id) {
  const uint64_t now = nowNanos();
  std::lock_guard lock(submit_mutex_);
  submit_timestamps_[std::string(client_order_id)] = now;
}

void LatencyTracker::recordFillReceived(std::string_view client_order_id) {
  const uint64_t now = nowNanos();
  std::lock_guard lock(submit_mutex_);
  if (auto it = submit_timestamps_.find(std::string(client_order_id));
      it != submit_timestamps_.end()) {
    const uint64_t latency = now - it->second;
    hdr_record_value_atomic(
        histograms_[static_cast<size_t>(LatencyMetric::FillRoundTrip)],
        static_cast<int64_t>(latency));
    submit_timestamps_.erase(it);
  }
}

int64_t LatencyTracker::percentile(LatencyMetric metric, double pct) const {
  const auto idx = static_cast<size_t>(metric);
  if (idx >= kMetricCount) {
    return 0;
  }
  return hdr_value_at_percentile(histograms_[idx], pct);
}

int64_t LatencyTracker::count(LatencyMetric metric) const {
  const auto idx = static_cast<size_t>(metric);
  if (idx >= kMetricCount) {
    return 0;
  }
  return histograms_[idx]->total_count;
}

void LatencyTracker::dumpOne(LatencyMetric metric,
                             const std::string &output_dir) const {
  const auto idx = static_cast<size_t>(metric);
  auto *h = histograms_[idx];
  const char *name = kMetricNames[idx];
  const char *desc = kMetricDescriptions[idx];

  if (h->total_count == 0) {
    std::cout << "\n=== " << desc << " ===\n";
    std::cout << "  (no samples recorded)\n";
    return;
  }

  const auto p50 = hdr_value_at_percentile(h, 50.0);
  const auto p90 = hdr_value_at_percentile(h, 90.0);
  const auto p99 = hdr_value_at_percentile(h, 99.0);
  const auto p999 = hdr_value_at_percentile(h, 99.9);
  const auto min_val = hdr_min(h);
  const auto max_val = hdr_max(h);
  const auto mean_val = hdr_mean(h);

  std::cout << "\n=== " << desc << " ===\n";
  std::cout << "  Samples: " << h->total_count << '\n';

  auto fmt = [](int64_t nanos) -> std::string {
    std::ostringstream oss;
    if (nanos < 1000) {
      oss << nanos << "ns";
    } else if (nanos < 1'000'000) {
      oss << std::fixed << std::setprecision(2)
          << (static_cast<double>(nanos) / 1000.0) << "us";
    } else if (nanos < 1'000'000'000) {
      oss << std::fixed << std::setprecision(2)
          << (static_cast<double>(nanos) / 1'000'000.0) << "ms";
    } else {
      oss << std::fixed << std::setprecision(3)
          << (static_cast<double>(nanos) / 1'000'000'000.0) << "s";
    }
    return oss.str();
  };

  std::cout << "  Min:     " << fmt(min_val) << '\n';
  std::cout << "  Mean:    " << fmt(static_cast<int64_t>(mean_val)) << '\n';
  std::cout << "  p50:     " << fmt(p50) << '\n';
  std::cout << "  p90:     " << fmt(p90) << '\n';
  std::cout << "  p99:     " << fmt(p99) << '\n';
  std::cout << "  p99.9:   " << fmt(p999) << '\n';
  std::cout << "  Max:     " << fmt(max_val) << '\n';

  const std::string filepath = output_dir + "/latency_" + name + ".txt";
  {
    std::ofstream file(filepath);
    if (!file.is_open()) {
      return;
    }
    file << desc << "\nSamples: " << h->total_count << "\n\n";
  }
  FILE *fp = std::fopen(filepath.c_str(), "a");
  if (fp) {
    hdr_percentiles_print(h, fp, 5, 1.0, CLASSIC);
    std::fclose(fp);
    std::cout << "  Written: " << filepath << '\n';
  }
}

void LatencyTracker::dump(const std::string &output_dir) const {
  std::cout << "\n"
            << "========================================\n"
            << "  LATENCY REPORT\n"
            << "========================================\n";

  for (size_t i = 0; i < kMetricCount; ++i) {
    dumpOne(static_cast<LatencyMetric>(i), output_dir);
  }

  std::cout << "========================================\n";
}
