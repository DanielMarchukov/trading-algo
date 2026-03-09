#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(__linux__) || defined(__gnu_linux__)
#include <time.h>
#endif

#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif

[[nodiscard]] inline uint64_t nowNanos() noexcept {
#if defined(__linux__) || defined(__gnu_linux__)
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
#else
  auto now = std::chrono::steady_clock::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          now.time_since_epoch())
          .count());
#endif
}

[[nodiscard]] inline std::string getZmqMarketDataAddress() {
#ifdef _WIN32
  return "tcp://127.0.0.1:5555";
#else
  auto temp = std::filesystem::temp_directory_path();
  return "ipc://" + (temp / "market_data.sock").string();
#endif
}
