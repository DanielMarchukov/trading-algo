#include "RateLimiter.hpp"
#include <gtest/gtest.h>
#include <thread>

class RateLimiterTest : public ::testing::Test {
protected:
  RateLimiter limiter_{5};
};

TEST_F(RateLimiterTest, StartsWithFullCapacity) {
  EXPECT_EQ(limiter_.remaining(), RateLimiter::kMaxRequestsPerWindow);
  EXPECT_TRUE(limiter_.canSend());
}

struct UpdateParam {
  int64_t remaining;
  bool expected_can_send;
};

class RateLimiterUpdateTest : public ::testing::TestWithParam<UpdateParam> {};

TEST_P(RateLimiterUpdateTest, CanSendMatchesThreshold) {
  RateLimiter limiter{5};
  const auto &[remaining, expected] = GetParam();
  limiter.update(remaining);
  EXPECT_EQ(limiter.remaining(), remaining);
  EXPECT_EQ(limiter.canSend(), expected);
}

INSTANTIATE_TEST_SUITE_P(
    BelowAndAboveThreshold, RateLimiterUpdateTest,
    ::testing::Values(UpdateParam{0, false}, UpdateParam{1, false},
                      UpdateParam{4, false}, UpdateParam{5, true},
                      UpdateParam{50, true}, UpdateParam{200, true}));

TEST_F(RateLimiterTest, UpdateAboveThresholdClearsThrottle) {
  limiter_.update(2);
  EXPECT_FALSE(limiter_.canSend());

  limiter_.update(100);
  EXPECT_TRUE(limiter_.canSend());
}

TEST_F(RateLimiterTest, OnRateLimitedBlocksSending) {
  limiter_.onRateLimited();
  EXPECT_EQ(limiter_.remaining(), 0);
  EXPECT_FALSE(limiter_.canSend());
}

TEST_F(RateLimiterTest, OnRateLimitedThenUpdateAboveThresholdResumes) {
  limiter_.onRateLimited();
  EXPECT_FALSE(limiter_.canSend());

  limiter_.update(150);
  EXPECT_TRUE(limiter_.canSend());
}

TEST_F(RateLimiterTest, DefaultThresholdIsTen) {
  RateLimiter default_limiter;
  EXPECT_EQ(default_limiter.threshold(), RateLimiter::kDefaultThreshold);

  default_limiter.update(10);
  EXPECT_TRUE(default_limiter.canSend());

  default_limiter.update(9);
  EXPECT_FALSE(default_limiter.canSend());
}

TEST_F(RateLimiterTest, WaitPolicyBlocksUntilReady) {
  RateLimiter wait_limiter{5, ThrottlePolicy::Wait};
  wait_limiter.onRateLimited();

  std::thread waiter([&]() { EXPECT_TRUE(wait_limiter.waitOrDrop()); });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  wait_limiter.update(100);
  waiter.join();
  EXPECT_TRUE(wait_limiter.canSend());
}

TEST_F(RateLimiterTest, DropPolicyReturnsFalseWhenThrottled) {
  RateLimiter drop_limiter{5, ThrottlePolicy::Drop};
  drop_limiter.onRateLimited();
  EXPECT_FALSE(drop_limiter.waitOrDrop());
}

TEST_F(RateLimiterTest, WaitOrDropReturnsTrueWhenNotThrottled) {
  EXPECT_TRUE(limiter_.waitOrDrop());
  RateLimiter drop_limiter{5, ThrottlePolicy::Drop};
  EXPECT_TRUE(drop_limiter.waitOrDrop());
}

TEST_F(RateLimiterTest, PolicyAccessor) {
  EXPECT_EQ(limiter_.policy(), ThrottlePolicy::Wait);
  RateLimiter drop_limiter{5, ThrottlePolicy::Drop};
  EXPECT_EQ(drop_limiter.policy(), ThrottlePolicy::Drop);
}

TEST_F(RateLimiterTest, RepeatedUpdatesTrackLatestValue) {
  limiter_.update(100);
  EXPECT_EQ(limiter_.remaining(), 100);

  limiter_.update(50);
  EXPECT_EQ(limiter_.remaining(), 50);

  limiter_.update(3);
  EXPECT_EQ(limiter_.remaining(), 3);
  EXPECT_FALSE(limiter_.canSend());
}

TEST_F(RateLimiterTest, MultipleOnRateLimitedCallsAreIdempotent) {
  limiter_.onRateLimited();
  limiter_.onRateLimited();
  limiter_.onRateLimited();
  EXPECT_FALSE(limiter_.canSend());

  limiter_.update(100);
  EXPECT_TRUE(limiter_.canSend());
}
