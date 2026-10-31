#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
// clang-format on
#endif

// Wall clock for the frame pacer: QueryPerformanceCounter ticks, released by a
// high-resolution waitable timer (Windows 10 1803 and later) with a short
// spin for the final stretch. Falls back to Sleep under timeBeginPeriod(1)
// when the timer cannot be created. On macOS and other POSIX hosts: steady_clock nanoseconds, released by
// nanosleep (already high resolution) with the same final spin.
class FrameClock {
public:
  FrameClock();
  ~FrameClock();
  FrameClock(const FrameClock &) = delete;
  FrameClock &operator=(const FrameClock &) = delete;

  std::uint64_t frequency() const { return frequency_; }
  std::int64_t now() const;
  // Returns once now() >= target. Sleeps until shortly before the target, then spins.
  void wait_until(std::int64_t target);
#ifdef _WIN32
  bool high_resolution_timer() const { return timer_ != nullptr; }
#else
  bool high_resolution_timer() const { return true; }
#endif

private:
  void sleep_ticks(std::int64_t ticks);
  std::uint64_t frequency_ = 0;
#ifdef _WIN32
  HANDLE timer_ = nullptr;
  bool period_raised_ = false;
#endif
};
