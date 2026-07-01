#pragma once

#include "engine_settings.h"
#include "phase_reset_markers.h"
#include "warpmarkers.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Canonical content fingerprint over everything the engine reads for a
// target-view render: source path, sample rate, the parser-domain warp and
// phase-reset marker fields, the full engine settings, and the trim bounds.
// Mirrors the inputs of build_render_request. GUI-only marker session scratch
// (iteration / BPM authoring) is intentionally excluded — it never reaches the
// engine, so two states differing only there must share a key. Trim seconds
// are normalized to 0 when their bound is unset. Same inputs always produce
// byte-identical output; the result is hashed to name a cache file and stored
// verbatim for an exact-compare confirm on lookup.
std::vector<uint8_t> render_fingerprint(
    const std::string& source_audio_path, int sample_rate,
    const std::vector<GuiWarpMarker>& markers,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    const EngineSettings& settings,
    bool has_trim_begin, double trim_begin_sec,
    bool has_trim_end,   double trim_end_sec);

// Two-tier store for rendered target-view audio, keyed by render_fingerprint.
// Target-view render cache entries are capped at 60 seconds so repeated live
// target-view renders stay fast without turning the cache into whole-track
// archival storage. Cache-eligible renders that fit the RAM budget live in a
// RAM tier; larger eligible renders live as files in a per-process directory
// under the user cache home. Both tiers are LRU-bounded. The store is
// process-local: the disk directory is removed at shutdown and dead-PID orphan
// directories are swept at init. Every public method is a no-op / miss when
// the store could not initialize (no cache home, unmakeable directory), so
// callers need no special-casing.
class RenderCache {
public:
    // Create the per-process directory under <cache home>/warptempo_gui/<pid>/
    // and sweep dead-PID siblings. Idempotent enough to call once at startup.
    // On any failure the store stays disabled (all lookups miss, all inserts
    // drop) rather than erroring.
    void init();

    // Remove this process's directory and free the RAM tier. Call at shutdown.
    // Safe if init() failed or never ran.
    void shutdown();

    // Confirmed lookup. Returns true only on hash match AND exact
    // fingerprint-blob compare AND matching channels/sample_rate, filling
    // out_samples with interleaved float32 audio and bumping the entry's LRU
    // recency. Disk hits are private target-view render cache containers:
    // magic/header, fingerprint confirmation, metadata, then interleaved
    // float32 audio payload. Any miss or mismatch returns false and leaves
    // out_samples untouched. A disk entry whose file is missing or fails
    // validation is dropped from the index and reported as a miss.
    bool lookup(const std::vector<uint8_t>& fingerprint,
                int channels, int sample_rate,
                std::vector<float>& out_samples);

    // Insert a freshly rendered buffer. Drops renders over
    // kTargetViewRenderCacheMaxSeconds, routes cache-eligible buffers that fit
    // the RAM budget to RAM, and routes larger eligible buffers to disk,
    // evicting LRU entries in the chosen tier until within budget. Overwrites
    // any existing entry with the same hash. Empty/degenerate buffers are
    // dropped. Disk write failures are swallowed (the render already played
    // from the live buffer; the cache simply will not hold it).
    void insert(const std::vector<uint8_t>& fingerprint,
                const std::vector<float>& samples,
                int channels, int sample_rate, int64_t frame_count);

private:
    struct RamEntry {
        std::vector<uint8_t> fingerprint;
        std::vector<float>   samples;
        int      channels    = 0;
        int      sample_rate = 0;
        uint64_t seq         = 0;
    };
    struct DiskEntry {
        std::vector<uint8_t> fingerprint;
        std::string          filename;
        uint64_t             size_bytes = 0;
        uint64_t             seq        = 0;
    };

    bool insert_ram(uint64_t h, const std::vector<uint8_t>& fp,
                    const std::vector<float>& samples,
                    int channels, int sample_rate);
    bool insert_disk(uint64_t h, const std::vector<uint8_t>& fp,
                     const std::vector<float>& samples,
                     int channels, int sample_rate, int64_t frame_count);
    void evict_ram_until(uint64_t target_max);
    void evict_disk_until(uint64_t target_max);
    void sweep_orphans();
    bool read_file(const std::string& path,
                   const std::vector<uint8_t>& want_fp,
                   int channels, int sample_rate,
                   std::vector<float>& out);
    bool write_file(const std::string& path,
                    const std::vector<uint8_t>& fp,
                    const std::vector<float>& samples,
                    int channels, int sample_rate, int64_t frame_count,
                    uint64_t& out_bytes);

    static constexpr int      kTargetViewRenderCacheMaxSeconds = 60;
    static constexpr uint64_t kRamBudgetBytes  = 1ull << 30;          // 1 GiB
    static constexpr uint64_t kDiskBudgetBytes = 10ull * (1ull << 30); // 10 GiB

    bool        enabled_ = false;
    std::string parent_;   // <cache home>/warptempo_gui
    std::string dir_;      // parent_/<pid>
    uint64_t    lru_seq_   = 0;

    std::unordered_map<uint64_t, RamEntry>  ram_;
    uint64_t                                ram_bytes_  = 0;
    std::unordered_map<uint64_t, DiskEntry> disk_index_;
    uint64_t                                disk_bytes_ = 0;
};
