#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace wtprof {

using Clock = std::chrono::steady_clock;

inline bool enabled() {
    return std::getenv("WARPTEMPO_PROFILE") != nullptr;
}

inline Clock::time_point now() {
    return Clock::now();
}

inline double ms(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

inline double bytes_to_mb(unsigned long long bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

}  // namespace wtprof
