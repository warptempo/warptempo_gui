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

// Per-channel PGHI counters and time split. All fields except sort_s are
// accumulated inside pghi_integrate when a non-null pointer is passed; sort_s
// is accumulated by the channel's analysis producer thread (the prev-stream
// fill+sort runs there). Plain member adds; one instance per channel, and
// within an instance each field has a single writer thread (distinct members,
// so the concurrent producer/consumer writes never race), read only after the
// joins.
struct PghiProfile {
    long long frames = 0;            // one per pghi_integrate call
    long long seed_frames = 0;       // calls that take the seed early-return
    long long quiet_bins = 0;        // bins assigned a random synthesis phase
    long long significant_bins = 0;  // bins entering I (walk-assigned)
    long long pops_total = 0;        // drain-loop selection events: prev-stream
                                     // takes + prev-stream done-skips +
                                     // frontier takes (a conceptual combined
                                     // heap's pops minus its no-op pops)
    long long prev_done_skips = 0;   // prev-stream entries skipped because their
                                     // bin was already assigned (the analogue of
                                     // a combined heap's discarded pops)
    long long current_noop_pops = 0; // frontier selections that spread to
                                     // neither neighbor (no theta write) —
                                     // structurally zero: an active frontier
                                     // bin has an undone neighbor by
                                     // definition, so the measured zero is the
                                     // frontier's proof and a nonzero count
                                     // indicates a frontier-update defect
    double quiet_s = 0.0;            // quiet-bin RNG assignment loop
    double sort_s = 0.0;             // radix sort of the key order + cur_order
                                     // copy + prev-stream filter, on the
                                     // analysis producer thread — overlaps the
                                     // consumer's drain rather than adding to
                                     // pghi serial time
    double drain_s = 0.0;            // selection walk (seed frames' trivial
                                     // seat time is attributed here too)
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
