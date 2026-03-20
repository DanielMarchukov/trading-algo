#include "LatencyTracker.hpp"
#include "ThreadGuard.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

class LatencyTrackerTest : public ::testing::Test {
protected:
  LatencyTracker tracker_;
};

TEST_F(LatencyTrackerTest, RecordAndRetrievePercentile) {
  for (int i = 1; i <= 1000; ++i) {
    tracker_.record(LatencyMetric::EndToEnd, static_cast<uint64_t>(i) * 1000);
  }

  EXPECT_EQ(tracker_.count(LatencyMetric::EndToEnd), 1000);

  const auto p50 = tracker_.percentile(LatencyMetric::EndToEnd, 50.0);
  EXPECT_GE(p50, 400'000);
  EXPECT_LE(p50, 600'000);

  const auto p99 = tracker_.percentile(LatencyMetric::EndToEnd, 99.0);
  EXPECT_GE(p99, 900'000);
}

TEST_F(LatencyTrackerTest, EmptyHistogramReturnsZero) {
  EXPECT_EQ(tracker_.count(LatencyMetric::MpscQueue), 0);
  EXPECT_EQ(tracker_.percentile(LatencyMetric::MpscQueue, 50.0), 0);
}

TEST_F(LatencyTrackerTest, MultipleMetricsAreIndependent) {
  tracker_.record(LatencyMetric::ZmqTransport, 5000);
  tracker_.record(LatencyMetric::StrategyRisk, 3000);

  EXPECT_EQ(tracker_.count(LatencyMetric::ZmqTransport), 1);
  EXPECT_EQ(tracker_.count(LatencyMetric::StrategyRisk), 1);
  EXPECT_EQ(tracker_.count(LatencyMetric::EndToEnd), 0);
}

TEST_F(LatencyTrackerTest, InvalidMetricDoesNotCrash) {
  tracker_.record(LatencyMetric::Count, 1000);
  EXPECT_EQ(tracker_.percentile(LatencyMetric::Count, 50.0), 0);
  EXPECT_EQ(tracker_.count(LatencyMetric::Count), 0);
}

TEST_F(LatencyTrackerTest, ConcurrentRecordFromMultipleThreads) {
  constexpr int num_threads = 8;
  constexpr int samples_per_thread = 10000;

  std::atomic<bool> start{false};
  std::atomic<int> done_count{0};
  std::promise<void> all_done;
  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&, t]() {
      while (!start.load(std::memory_order_acquire)) {
      }
      for (int i = 0; i < samples_per_thread; ++i) {
        tracker_.record(LatencyMetric::ZmqTransport,
                        static_cast<uint64_t>(t + 1) * 1000 +
                            static_cast<uint64_t>(i));
      }
      if (done_count.fetch_add(1, std::memory_order_acq_rel) + 1 ==
          num_threads) {
        all_done.set_value();
      }
    });
  }

  start.store(true, std::memory_order_release);

  auto status = all_done.get_future().wait_for(std::chrono::seconds(5));
  ASSERT_EQ(status, std::future_status::ready) << "timeout — possible deadlock";

  for (auto &t : threads) {
    t.join();
  }

  EXPECT_EQ(tracker_.count(LatencyMetric::ZmqTransport),
            num_threads * samples_per_thread);
}

TEST_F(LatencyTrackerTest, FillRoundTripTracking) {
  tracker_.recordOrderSubmit("order-abc");
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  tracker_.recordFillReceived("order-abc");

  EXPECT_EQ(tracker_.count(LatencyMetric::FillRoundTrip), 1);
  EXPECT_GE(tracker_.percentile(LatencyMetric::FillRoundTrip, 50.0), 50'000);
}

TEST_F(LatencyTrackerTest, FillWithUnknownOrderIdIsIgnored) {
  tracker_.recordFillReceived("unknown-order");
  EXPECT_EQ(tracker_.count(LatencyMetric::FillRoundTrip), 0);
}

TEST_F(LatencyTrackerTest, DumpProducesFiles) {
  const auto tmp = std::filesystem::temp_directory_path() / "latency_test";
  std::filesystem::create_directories(tmp);

  tracker_.record(LatencyMetric::EndToEnd, 50'000);
  tracker_.record(LatencyMetric::EndToEnd, 100'000);
  tracker_.dump(tmp.string());

  EXPECT_TRUE(std::filesystem::exists(tmp / "latency_end_to_end.txt"));

  std::filesystem::remove_all(tmp);
}

TEST_F(LatencyTrackerTest, DumpEmptyMetricsDoesNotCrash) {
  const auto tmp =
      std::filesystem::temp_directory_path() / "latency_empty_test";
  std::filesystem::create_directories(tmp);

  tracker_.dump(tmp.string());

  EXPECT_FALSE(std::filesystem::exists(tmp / "latency_end_to_end.txt"));

  std::filesystem::remove_all(tmp);
}

TEST_F(LatencyTrackerTest, DuplicateSubmitOverwritesTimestamp) {
  tracker_.recordOrderSubmit("order-dup");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  tracker_.recordOrderSubmit("order-dup");
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  tracker_.recordFillReceived("order-dup");

  EXPECT_EQ(tracker_.count(LatencyMetric::FillRoundTrip), 1);
  const auto latency = tracker_.percentile(LatencyMetric::FillRoundTrip, 50.0);
  EXPECT_LT(latency, 40'000'000);
}

TEST_F(LatencyTrackerTest, SecondFillForSameOrderIsIgnored) {
  tracker_.recordOrderSubmit("order-once");
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  tracker_.recordFillReceived("order-once");
  EXPECT_EQ(tracker_.count(LatencyMetric::FillRoundTrip), 1);

  tracker_.recordFillReceived("order-once");
  EXPECT_EQ(tracker_.count(LatencyMetric::FillRoundTrip), 1);
}

TEST_F(LatencyTrackerTest, LargeLatencyValuesRecorded) {
  const uint64_t large_value = 59'000'000'000ULL;
  tracker_.record(LatencyMetric::EndToEnd, large_value);

  EXPECT_EQ(tracker_.count(LatencyMetric::EndToEnd), 1);
  const auto p50 = tracker_.percentile(LatencyMetric::EndToEnd, 50.0);
  EXPECT_GE(p50, 58'000'000'000LL);
}

TEST_F(LatencyTrackerTest, DumpWritesAllMetricFilesAndFormatsAllRanges) {
  const auto tmp =
      std::filesystem::temp_directory_path() / "latency_all_metrics";
  std::filesystem::create_directories(tmp);

  tracker_.record(LatencyMetric::EndToEnd, 500);
  tracker_.record(LatencyMetric::ZmqTransport, 50'000);
  tracker_.record(LatencyMetric::StrategyRisk, 5'000'000);
  tracker_.record(LatencyMetric::MpscQueue, 2'000'000'000);
  tracker_.record(LatencyMetric::FillRoundTrip, 5000);
  tracker_.dump(tmp.string());

  EXPECT_TRUE(std::filesystem::exists(tmp / "latency_end_to_end.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp / "latency_zmq_transport.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp / "latency_strategy_risk.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp / "latency_mpsc_queue.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp / "latency_fill_round_trip.txt"));

  {
    std::ifstream file(tmp / "latency_end_to_end.txt");
    std::string first_line;
    std::getline(file, first_line);
    EXPECT_FALSE(first_line.empty());
  }

  std::filesystem::remove_all(tmp);
}
