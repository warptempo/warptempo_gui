#pragma once

#include "engine_settings.h"
#include "warp_frame_map_build.h"  // MarkerForRender (the resolver's output)

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct RenderFileIdentity {
    uint64_t size = 0;
    int64_t mtime = 0;
};

// Render fingerprints identify the source by size and mtime only. Folding in
// content identity would bump the fingerprint content version and invalidate
// every archival sidecar, so size and mtime are the source trust boundary.
// Distinct from ArtifactStatIdentity below: this is the PERSISTED recipe-key
// identity of the SOURCE, serialized into the fingerprint (the coarseness is
// deliberate and version-load-bearing); the two are never interconverted.
bool stat_file_identity(const std::string& path, RenderFileIdentity& out);

// Full stat identity of an on-disk file at a single instant: device, inode,
// size, mtime in nanoseconds. Captured by fingerprint_sidecar_matches at
// validation time so a reuse consumer can BIND its later read/copy to the
// exact wav object the sidecar validated (the TOCTOU guard: atomic
// publication of each file does not make the wav/sidecar pair atomic across
// concurrent GUI/CLI processes).
// Distinct from RenderFileIdentity above: this is the EPHEMERAL TOCTOU race
// token on an OUTPUT wav, re-compared after the read/copy and never
// serialized; the two are never interconverted.
struct ArtifactStatIdentity {
    uint64_t dev      = 0;
    uint64_t inode    = 0;
    uint64_t size     = 0;
    int64_t  mtime_ns = 0;
    bool operator==(const ArtifactStatIdentity&) const = default;
};
bool stat_artifact_identity(const std::string& path, ArtifactStatIdentity& out);

// Canonical RENDER-IDENTITY fingerprint: "would a fresh render of this recipe
// produce these bytes". THE KEY NAMES AUTHORED STATE AND RECIPE ONLY: it
// serializes, in order: the content version; source path + source file
// identity; sample rate; every EngineSettings field (full-recipe key — the
// exhaustive decision switch in the serializer is the single drift guard); the
// trim bounds (a NOT-trimmed render — the full window — writing the pre-always-set
// unset bytes, so it hashes like a pre-arc untrimmed render); and the
// RESOLVED marker state — the exact engine inputs, not the raw stores:
// resolve_warp_markers_for_render's survivors (per marker: frame, resolved
// tempo cents, typed scale, label def/ref — precisely the MarkerForRender
// fields build_warp_frame_map reads) and the phase-reset positions the engine
// actually receives, as whole int64 frames. THAT PHASE-RESET LIST IS
// TRIM-AWARE (2026-08-01): the caller passes build_phase_reset_source_frames'
// collapsed enabled positions for an untrimmed render, but plan_trim's own
// translated, RANGE-FILTERED list whenever a trim plan survives — the trimmer
// filters out-of-window resets out of the engine input entirely (the column
// asymmetry recorded at trimmer.h), so under a sub-window an out-of-window
// reset provably cannot move a byte and no longer moves the key either. Raw
// disabled markers, dropped fields, collapsed duplicates, and out-of-window
// resets therefore no longer move the key: two states normalization or the
// trim window proves render-identical share a fingerprint.
// CALLERS OWN THE RESOLVE AND THE TRIM ARM: this function is pure
// serialization; each call site either threads an already-resolved product
// through (do_render, whose own trim_plan selects the arm) or runs its own
// resolve and its own plan_trim and accepts the resolver's per-resolve stderr
// lines (compute_live_render_fingerprint). GUI-only marker session scratch
// (iteration / BPM authoring) never reaches the resolver, so it is excluded
// by construction. Same inputs always produce byte-identical output WITHIN ONE
// LIBRARY EPOCH — the library-environment term is retired (2026-08-09, record
// in settings.md), so a reuse crossing a glibc or FFTW upgrade may differ at
// the accepted inaudible class rather than byte-exactly; the
// result is hashed to name a cache file and stored verbatim for an
// exact-compare confirm on lookup.
std::vector<uint8_t> render_fingerprint(
    const std::string& source_audio_path,
    const RenderFileIdentity& source_identity,
    int sample_rate,
    const std::vector<MarkerForRender>& resolved_warp_markers,
    // The phase-reset positions the engine receives for THIS render: the
    // authored collapsed source frames untrimmed, plan_trim's translated and
    // range-filtered list under a surviving trim plan (the trim-aware ruling
    // above). Both spellings are whole doubles, which is what the serializer's
    // int64 encoding rests on.
    const std::vector<double>& phase_reset_engine_frames,
    const EngineSettings& settings,
    // Trim: `trimmed` is false for the FULL window [0, total-1], which renders
    // untrimmed and therefore hashes IDENTICALLY to the pre-2026-07-30 unset
    // state (has-byte 0 + f64 0.0 on both sides), keeping the default window's
    // renders reuse-identical with pre-arc untrimmed ones. It is true for a
    // proper sub-window, whose two frames serialize exactly as a set pair always
    // did. The caller owns the recognition (trim_window_is_full, settings_file.h
    // — one owner, GUI and CLI alike); this function is pure serialization.
    bool trimmed, int64_t trim_begin_frame, int64_t trim_end_frame);

std::string fingerprint_sidecar_path(const std::string& wav_path);

// Encodes interleaved float32 samples as a complete PCM_24 wav in an
// in-memory byte blob. The blob is byte-identical to the engine's archival
// writer because both use the same in-tree writer and PCM_24 policy.
// Encoding happens exactly once per render; every reuse consumes the bytes
// directly.
bool encode_pcm24_wav_blob(const std::vector<float>& samples,
                           int channels, int sample_rate,
                           std::vector<char>& out_blob);

// Reads a wav's full payload as interleaved float32, verifying the
// header's channels and sample rate against the expected values first.
// Used by the cache's disk-tier read and by target view's archival
// artifact rung. Any anomaly returns false with out untouched.
bool read_wav_to_float(const std::string& path,
                       int expected_channels, int expected_sample_rate,
                       std::vector<float>& out);

// Reads a file's raw bytes verbatim. Used to capture the just-published
// archival wav as a canonical cache blob without decoding it.
bool read_file_bytes(const std::string& path, std::vector<char>& out);

// Stats wav_path and writes its identity plus the hex-encoded fingerprint
// blob to the sidecar via a .tmp staging write and atomic rename. Failure is
// logged by the caller and non-fatal.
bool write_fingerprint_sidecar(const std::string& wav_path,
                               const std::vector<uint8_t>& fingerprint);

// True only when the sidecar exists, parses exactly (magic, version, all
// three fields, no extras), the wav's current stat identity equals the
// recorded one, and the recorded hex decodes to a byte-exact match of
// fingerprint. Any anomaly whatsoever is false — the caller re-renders.
// On a match, a non-null `out_identity` receives the wav's full stat
// identity from the SAME stat the validation used (never a second stat):
// the validation-time capture a reuse consumer compares against a re-stat
// AFTER its read/copy completes, so audio from a different publication than
// the validated sidecar is discarded as a miss (the TOCTOU bind at
// ArtifactStatIdentity above).
bool fingerprint_sidecar_matches(const std::string& wav_path,
                                 const std::vector<uint8_t>& fingerprint,
                                 ArtifactStatIdentity* out_identity = nullptr);

// Single-tier disk store for rendered target-view and archival audio, keyed by
// render_fingerprint. Entries are canonical deliverable wav bytes encoded
// exactly once as PCM_24, the sole deliverable format, on the writer thread.
// For target-route buffers, limited masters arrive pre-quantized, so that
// single PCM_24 writer-thread encode is an exact re-expression rather than a
// lossy step and render completion never waits for it. The in-tree encoder is
// roundtrip-exact over the full 24-bit lattice; byte-canonical entries are
// still retained because byte copies are conversion-free by construction,
// publishes stay zero-work, and PCM_24 blobs are smaller than float buffers.
// The disk tier is uncapped per entry and LRU-bounded at 10 GiB. There is no
// RAM tier: on this host (SSD) undo/redo hits are page-cache-warm reads of the
// disk entry, which the OS page cache already serves, so a second in-process
// copy of the bytes would only duplicate what disk plus page cache provide.
// The store is process-local: the disk directory is removed at shutdown and
// dead-PID orphan directories are swept at init. Every public method is a
// no-op / miss when the store could not initialize (no cache home, unmakeable
// directory), so callers need no special-casing.
//
// Every public method is thread-safe; callers need no external locking. A
// single mutex_ guards the disk index, the byte counter (disk_bytes_),
// lru_seq_, and the writer thread handoff. Large file I/O (disk-tier
// reads/copies, the disk writer's blob write) happens outside the lock: a disk
// lookup copies the entry's filename out under the lock, reads the wav
// unlocked, and re-takes the lock only for the LRU bump or a drop-on-failure.
// The writer thread lifecycle is swap-join-outside — join_writer() swaps
// writer_ into a local under the lock, unlocks, then joins the local, so no
// lock is ever held across a join; the writer body itself takes the lock only
// for its registration/eviction step at the end.
class RenderCache {
public:
    // Create the per-process directory under <cache home>/warptempo_gui/<pid>/
    // and sweep dead-PID siblings. Idempotent enough to call once at startup.
    // On any failure the store stays disabled (all lookups miss, all inserts
    // drop) rather than erroring.
    void init();

    // Remove this process's directory. Call at shutdown.
    // Safe if init() failed or never ran.
    void shutdown();

    // Absolute path of this process's cache directory
    // (<cache home>/warptempo_gui/<pid>), or an empty string when the store is
    // disabled (no cache home / unmakeable directory). The archival render
    // pipeline drops the full framemap pair here as future-proofing: the
    // directory is removed at shutdown and orphan-swept at the next launch, so
    // the pair rests only between a render and program close and nothing
    // accumulates. Read-only; set once at init before any render dispatches.
    std::string process_dir() const;

    // Confirmed lookup. Returns true only on hash match AND exact
    // fingerprint-blob compare AND matching channels/sample_rate, filling
    // out_samples with interleaved float32 audio and bumping the entry's LRU
    // recency. A hit confirms the wav's .fingerprint sidecar, then reads the
    // interleaved float32 wav payload. Any miss or mismatch returns false and
    // leaves out_samples untouched. A disk entry whose file pair is missing or
    // fails validation is dropped from the index and reported as a miss.
    bool lookup(const std::vector<uint8_t>& fingerprint,
                int channels, int sample_rate,
                std::vector<float>& out_samples);

    // Confirmed-hit publish. Keys on the fingerprint alone: confirms the wav's
    // .fingerprint sidecar (which already binds sample rate), then byte-copies
    // the entry to staging_path. Unlike lookup it neither validates nor decodes
    // against a format — a byte copy needs no channels/sample_rate check — so it
    // is byte-identical to an engine publish by construction with no sample
    // conversion.
    bool publish_wav(const std::vector<uint8_t>& fingerprint,
                     const std::string& staging_path);

    // Insert a freshly rendered wav blob into the disk tier, evicting LRU
    // entries until within budget. Overwrites any existing entry with the same
    // hash. Empty/degenerate blobs are dropped. Disk write failures are
    // swallowed (the render already played from the live buffer; the cache
    // simply will not hold it).
    void insert(const std::vector<uint8_t>& fingerprint,
                const std::vector<char>& wav_blob,
                int channels, int sample_rate, int64_t frame_count);

    // Insert freshly rendered target-route samples. On the limited route they
    // are already on the PCM_24 deliverable lattice. The samples are copied
    // into a writer-thread job and encoded to the canonical PCM_24 wav blob
    // there, so the caller (the render worker's completion path) never waits
    // on the encode; the disk write happens after the encode under the usual
    // mutex. A lookup that lands before the encode finishes
    // misses benignly and re-renders. Encode failure drops the entry with one
    // stderr line. cancel_token (nullable) is the dispatching render's
    // per-dispatch session cancel token — created fresh per dispatch and
    // never reset, so it stays truthful across later dispatches. It travels
    // in the writer job: the job is dropped silently when the token is set,
    // either once the previous writer has been joined (before the writer
    // thread launches) or at the writer thread's post-encode re-check
    // (before anything becomes externally observable).
    void insert_master_floats(const std::vector<uint8_t>& fingerprint,
                              const std::vector<float>& samples,
                              int channels, int sample_rate,
                              int64_t frame_count,
                              std::shared_ptr<const std::atomic<bool>>
                                  cancel_token);

private:
    struct WriterJob;
    struct DiskEntry {
        std::vector<uint8_t> fingerprint;
        std::string          filename;
        uint64_t             size_bytes = 0;
        uint64_t             seq        = 0;
    };

    bool insert_disk(uint64_t h, const std::vector<uint8_t>& fp,
                     const std::vector<char>& blob, int64_t frame_count);
    void start_writer_job(WriterJob job);
    void evict_disk_until(uint64_t target_max);
    void sweep_orphans();
    bool read_file(const std::string& path,
                   const std::vector<uint8_t>& want_fp,
                   int channels, int sample_rate,
                   std::vector<float>& out);
    bool write_file(const std::string& path,
                    const std::vector<uint8_t>& fp,
                    const std::vector<char>& blob, int64_t frame_count,
                    uint64_t& out_bytes);
    void join_writer();
    void remove_disk_pair(const std::string& wav_path);

    static constexpr uint64_t kDiskBudgetBytes = 10ull * (1ull << 30); // 10 GiB

    bool        enabled_ = false;
    std::string parent_;   // <cache home>/warptempo_gui
    std::string dir_;      // parent_/<pid>

    std::mutex                              mutex_;
    uint64_t                                lru_seq_    = 0;
    std::thread                             writer_;
    std::unordered_map<uint64_t, DiskEntry> disk_index_;
    uint64_t                                disk_bytes_ = 0;
};
