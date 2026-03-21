#include "ThreadPinning.hpp"
#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__gnu_linux__)
#include <pthread.h>
#elif defined(__APPLE__)
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#endif

void pin_thread_to_core(std::thread &t, uint32_t core_id) {
#if defined(__linux__) || defined(__gnu_linux__)
  if (core_id >= CPU_SETSIZE) {
    spdlog::get("system")->error("core_id {} exceeds CPU_SETSIZE ({})", core_id,
                                 CPU_SETSIZE);
    return;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  if (pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset) !=
      0) {
    spdlog::get("system")->error("pthread_setaffinity_np failed");
  }
#elif defined(_WIN32)
  if (core_id >= 64) {
    spdlog::get("system")->error(
        "core_id {} exceeds 64-bit affinity mask limit", core_id);
    return;
  }
  if (const DWORD_PTR mask = 1ULL << core_id;
      SetThreadAffinityMask(t.native_handle(), mask) == 0) {
    spdlog::get("system")->error("SetThreadAffinityMask failed: {}",
                                 GetLastError());
  }
#elif defined(__APPLE__)
  // THREAD_AFFINITY_POLICY is only a scheduling hint (same-tag threads
  // share L2 cache). It does not pin to a specific core, and on Apple
  // Silicon it is effectively a no-op.
  thread_affinity_policy_data_t policy = {static_cast<integer_t>(core_id)};
  thread_port_t mach_thread = pthread_mach_thread_np(t.native_handle());
  if (thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                        (thread_policy_t)&policy,
                        THREAD_AFFINITY_POLICY_COUNT) != KERN_SUCCESS) {
    spdlog::get("system")->warn(
        "thread_policy_set affinity hint failed for core {}", core_id);
  }
#else
  (void)t;
  (void)core_id;
  spdlog::get("system")->warn("CPU pinning not supported on this platform");
#endif
}
