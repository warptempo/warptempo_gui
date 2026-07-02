#pragma once

#include "engine_settings.h"
#include "phaseresetmarkers.h"
#include "warpmarkers.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct RenderFileIdentity {
    uint64_t size = 0;
    int64_t mtime = 0;
};

bool stat_file_identity(const std::string& path, RenderFileIdentity& out);

// Canonical content fingerprint over everything the engine reads for a
// target-view render and archival wav render: source path, source file
// identity, sample rate, the parser-domain warp and phase-reset marker fields,
// the full engine settings, and the trim bounds. Mirrors the inputs of
// build_render_request. GUI-only marker session scratch (iteration / BPM
// authoring) is intentionally excluded — it never reaches the engine, so two
// states differing only there must share a key. Trim seconds are normalized to
// 0 when their bound is unset. Same inputs always produce byte-identical
// output; the result is hashed to name a cache file and stored verbatim for an
// exact-compare confirm on lookup.
std::vector<uint8_t> render_fingerprint(
    const std::string& source_audio_path,
    const RenderFileIdentity& source_identity,
    int sample_rate,
    const std::vector<GuiWarpMarker>& markers,
    const std::vector<GuiPhaseResetMarker>& phase_resets,
    const EngineSettings& settings,
    bool has_trim_begin, double trim_begin_sec,
    bool has_trim_end,   double trim_end_sec);

std::string fingerprint_sidecar_path(const std::string& wav_path);

// Writes interleaved float32 samples as a wav with the engine's exact
// writer parameters (SF_FORMAT_WAV | SF_FORMAT_FLOAT, one
// sf_writef_float pass). Single writer for cache entries and cache-hit
// archival publishes, so parameter identity holds by construction.
bool write_float_wav(const std::string& path,
                     const std::vector<float>& samples,
                     int channels, int sample_rate);

// Stats wav_path and writes its identity plus the hex-encoded fingerprint
// blob to the sidecar via a .tmp staging write and atomic rename. Failure is
// logged by the caller and non-fatal.
bool write_fingerprint_sidecar(const std::string& wav_path,
                               const std::vector<uint8_t>& fingerprint);

// True only when the sidecar exists, parses exactly (magic, version, all
// three fields, no extras), the wav's current stat identity equals the
// recorded one, and the recorded hex decodes to a byte-exact match of
// fingerprint. Any anomaly whatsoever is false — the caller re-renders.
bool fingerprint_sidecar_matches(const std::string& wav_path,
                                 const std::vector<uint8_t>& fingerprint);

// Two-tier store for rendered target-view and archival audio, keyed by
// render_fingerprint. The RAM tier serves short live target-view renders.
// The disk tier is uncapped per entry and LRU-bounded at 10 GiB, so
// full-movement renders can be reused without monopolizing RAM. The store is
// process-local: the disk directory is removed at shutdown and dead-PID
// orphan directories are swept at init. Every public method is a no-op /
// miss when the store could not initialize (no cache home, unmakeable
// directory), so callers need no special-casing.
//
// Every public method is thread-safe; callers need no external locking. A
// single mutex_ guards both tiers' indexes, the byte counters, lru_seq_, and
// the writer thread handoff. Large file I/O (disk-tier reads, the disk
// writer's wav encode) happens outside the lock: a disk lookup copies the
// entry's filename out under the lock, reads the wav unlocked, and re-takes
// the lock only for the LRU bump or a drop-on-failure. The writer thread
// lifecycle is swap-join-outside — join_writer() swaps writer_ into a local
// under the lock, unlocks, then joins the local, so no lock is ever held
// across a join; the writer body itself takes the lock only for its
// registration/eviction step at the end.
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
    // recency. Disk hits confirm the wav's .fingerprint sidecar, then read the
    // interleaved float32 wav payload. Any miss or mismatch returns false and
    // leaves out_samples untouched. A disk entry whose file pair is missing or
    // fails validation is dropped from the index and reported as a miss.
    bool lookup(const std::vector<uint8_t>& fingerprint,
                int channels, int sample_rate,
                std::vector<float>& out_samples);

    // Insert a freshly rendered buffer. Routes short buffers that fit the RAM
    // budget to RAM and everything else to the disk tier, evicting LRU entries
    // in the chosen tier until within budget. Overwrites any existing entry
    // with the same hash. Empty/degenerate buffers are dropped. Disk write
    // failures are swallowed (the render already played from the live buffer;
    // the cache simply will not hold it).
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
    void join_writer();
    void remove_disk_pair(const std::string& wav_path);

    static constexpr int      kRamTierMaxSeconds = 60;
    static constexpr uint64_t kRamBudgetBytes  = 1ull << 30;          // 1 GiB
    static constexpr uint64_t kDiskBudgetBytes = 10ull * (1ull << 30); // 10 GiB

    bool        enabled_ = false;
    std::string parent_;   // <cache home>/warptempo_gui
    std::string dir_;      // parent_/<pid>

    std::mutex                              mutex_;
    uint64_t                                lru_seq_    = 0;
    std::unordered_map<uint64_t, RamEntry>  ram_;
    uint64_t                                ram_bytes_  = 0;
    std::thread                             writer_;
    std::unordered_map<uint64_t, DiskEntry> disk_index_;
    uint64_t                                disk_bytes_ = 0;
};

// Process-wide, self-initializing RenderCache used by do_render's archival
// wav path (render_pipeline.cpp), which has no injected RenderCache
// reference of its own. Lazily calls init() on first access. This is
// intentionally a separate instance from the RenderCache main.cpp
// constructs and hands to GuiTargetRender by reference — unifying the two
// so archival and target-view renders share one cache is follow-up work.
RenderCache& shared_render_cache();
