#include "ThreadPinning.hpp"
#include <iostream>

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
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  if (pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset) !=
      0) {
    std::cerr << "Error calling pthread_setaffinity_np\n";
  }
#elif defined(_WIN32)
  if (const DWORD_PTR mask = 1LL << core_id;
      SetThreadAffinityMask(t.native_handle(), mask) == 0) {
    std::cerr << "Error calling SetThreadAffinityMask: " << GetLastError()
              << std::endl;
  }
#elif defined(__APPLE__)
  thread_affinity_policy_data_t policy = {static_cast<integer_t>(core_id)};
  thread_port_t mach_thread = pthread_mach_thread_np(t.native_handle());
  if (thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                        (thread_policy_t)&policy,
                        THREAD_AFFINITY_POLICY_COUNT) != KERN_SUCCESS) {
    std::cerr << "Error calling thread_policy_set" << std::endl;
  }
#else
  (void)t;
  (void)core_id;
  std::cout << "Warning: CPU pinning not supported on this platform."
            << std::endl;
#endif
}
