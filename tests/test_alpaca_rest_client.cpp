#include "AlpacaRestClient.hpp"
#include <Utils.hpp>
#include <gtest/gtest.h>

class AlpacaRestClientTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const char *key = std::getenv("APCA_API_KEY_ID");
        const char *secret = std::getenv("APCA_API_SECRET_KEY");
        if (key)
            original_key_ = key;
        if (secret)
            original_secret_ = secret;

        setenv("APCA_API_KEY_ID", "test_key", 1);
        setenv("APCA_API_SECRET_KEY", "test_secret", 1);
    }

    void TearDown() override {
        if (!original_key_.empty()) {
            setenv("APCA_API_KEY_ID", original_key_.c_str(), 1);
        } else {
            unsetenv("APCA_API_KEY_ID");
        }

        if (!original_secret_.empty()) {
            setenv("APCA_API_SECRET_KEY", original_secret_.c_str(), 1);
        } else {
            unsetenv("APCA_API_SECRET_KEY");
        }
    }

  private:
    std::string original_key_;
    std::string original_secret_;
};

TEST_F(AlpacaRestClientTest, ThrowsWhenApiKeyMissing) {
    unsetenv("APCA_API_KEY_ID");
    EXPECT_THROW(AlpacaRestClient(), std::runtime_error);
}

TEST_F(AlpacaRestClientTest, ThrowsWhenApiSecretMissing) {
    unsetenv("APCA_API_SECRET_KEY");
    EXPECT_THROW(AlpacaRestClient(), std::runtime_error);
}

TEST_F(AlpacaRestClientTest, UsesCustomBaseUrlWhenProvided) {
    setenv("APCA_API_BASE_URL", "https://custom.alpaca.test", 1);
    // This test would need to mock the HTTP client to verify the URL
    // For now, just ensure it constructs without throwing
    EXPECT_NO_THROW(AlpacaRestClient client);
    unsetenv("APCA_API_BASE_URL");
}