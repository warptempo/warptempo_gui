#pragma once

// Temporary render profiling instrumentation, gated on the environment
// variable WARPTEMPO_PROFILE=1. It only reads values, counts events, and reads
// steady_clock; it never changes rendered bytes. Disabled, the cost is at most
// one cached-bool test or null-pointer check per section — no clock reads in
// hot loops. All output is stderr, printed once at stage ends, prefixed
// "[profile] ". This whole facility is removed when the engine-performance
// measurement campaign closes.

#include <cstdlib>
#include <cstring>

// True iff WARPTEMPO_PROFILE is exactly "1". Read once (function-local static).
inline bool render_profile_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("WARPTEMPO_PROFILE");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

// Per-channel PGHI counters and time split, accumulated inside pghi_integrate
// when a non-null pointer is passed. Plain member adds; one instance per
// channel so concurrent channel threads never share a slot.
struct PghiProfile {
    long long frames = 0;            // one per pghi_integrate call
    long long seed_frames = 0;       // calls that take the seed early-return
    long long quiet_bins = 0;        // bins assigned a random synthesis phase
    long long significant_bins = 0;  // bins entering I (heap-assigned)
    long long pops_total = 0;        // drain-loop selection events: prev-stream
                                     // takes + prev-stream done-skips +
                                     // current-heap pops (the same magnitude a
                                     // single combined heap's pops would be)
    long long pops_inert = 0;        // prev-stream done-skips (the analogue of
                                     // a combined heap's discarded pops)
    double quiet_s = 0.0;            // quiet-bin RNG assignment loop
    double heap_s = 0.0;             // seed seat + prev-stream sort + drain loop
};

// Per-channel synthesis stage timings, accumulated in run_channel when
// profiling is enabled. One instance per channel.
struct SynthChannelProfile {
    long long frames = 0;
    double pghi_s = 0.0;
    double populate_s = 0.0;
    double ifft_s = 0.0;
    double ola_emit_s = 0.0;
    double analysis_wait_s = 0.0;
    PghiProfile pghi;
};
