#pragma once

#include <thread>

// Pins a thread to a specific logical core.
//
// Why it matters in HFT:
//   The OS scheduler is free to migrate threads between cores. Each migration
//   flushes the L1/L2 cache and reloads NUMA-local memory. For a hot-path
//   thread that owns an OrderBook (64K orders * 64 bytes = 4 MB), a single
//   migration costs thousands of nanoseconds in cache-miss latency.
//   Pinning eliminates migrations entirely for that thread.
//
// Platform notes:
//   Linux: pthread_setaffinity_np — hard binding, enforced by scheduler.
//   macOS: THREAD_AFFINITY_POLICY — advisory only; the kernel may still migrate
//          but heavily prefers the requested core. No strong guarantees.

#ifdef __linux__
#include <pthread.h>
#include <sched.h>

inline void pin_thread_to_core(std::thread& t, int core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
}

#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>

inline void pin_thread_to_core(std::thread& t, int core_id) noexcept {
    thread_affinity_policy_data_t policy{core_id};
    thread_port_t mach_thread = pthread_mach_thread_np(t.native_handle());
    thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                      reinterpret_cast<thread_policy_t>(&policy), 1);
}

#else
// No-op on unsupported platforms
inline void pin_thread_to_core(std::thread&, int) noexcept {}
#endif
