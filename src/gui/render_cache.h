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
bool stat_file_identity(const std::string& path, RenderFileIdentity& out);

// Full stat identity of an on-disk file at a single instant: device, inode,
// size, mtime in nanoseconds. Captured by fingerprint_sidecar_matches at
// validation time so a reuse consumer can BIND its later read/copy to the
// exact wav object the sidecar validated (the TOCTOU guard: atomic
// publication of each file does not make the wav/sidecar pair atomic across
// concurrent GUI/CLI processes).
struct ArtifactStatIdentity {
    uint64_t dev      = 0;
    uint64_t inode    = 0;
    uint64_t size     = 0;
    int64_t  mtime_ns = 0;
    bool operator==(const ArtifactStatIdentity&) const = default;
};
bool stat_artifact_identity(const std::string& path, ArtifactStatIdentity& out);

// Canonical RENDER-IDENTITY fingerprint (v16): "would a fresh render of this
// recipe, in this environment, produce these bytes". The key serializes, in
// order: the content version; the render-environment quartet
// (compute_render_env_hashes() — the four library content hashes actually
// mapped into THIS process, so a pre-upgrade artifact can never match a
// post-upgrade recipe); source path + source file identity; sample rate; an
// exhaustive per-EngineField decision over the engine settings (`scale`
// serialized, the five naming/provenance fields explicitly inert — the
// decision switch in the serializer is the single drift guard); the trim
// bounds (frame values normalized to 0 when their bound is unset); and the
// RESOLVED marker state — the exact engine inputs, not the raw stores:
// resolve_warp_markers_for_render's survivors (per marker: frame, resolved
// tempo cents, typed scale, label def/ref — precisely the MarkerForRender
// fields build_warp_frame_map reads) and build_phase_reset_source_frames'
// collapsed enabled positions as whole int64 frames. Raw disabled markers,
// dropped fields, and collapsed duplicates therefore no longer move the key:
// two states normalization proves render-identical share a fingerprint.
// CALLERS OWN THE RESOLVE: this function is pure serialization; each call
// site either threads an already-resolved product through (do_render) or
// runs its own resolve and accepts the resolver's per-resolve stderr lines
// (compute_live_render_fingerprint). GUI-only marker session scratch
// (iteration / BPM authoring) never reaches the resolver, so it is excluded
// by construction. Same inputs always produce byte-identical output; the
// result is hashed to name a cache file and stored verbatim for an
// exact-compare confirm on lookup.
std::vector<uint8_t> render_fingerprint(
    const std::string& source_audio_path,
    const RenderFileIdentity& source_identity,
    int sample_rate,
    const std::vector<MarkerForRender>& resolved_warp_markers,
    const std::vector<double>& phase_reset_source_frames,
    const EngineSettings& settings,
    bool has_trim_begin, int64_t trim_begin_frame,
    bool has_trim_end,   int64_t trim_end_frame);

std::string fingerprint_sidecar_path(const std::string& wav_path);

// Encodes interleaved float32 samples as a complete PCM_24 wav in an
// in-memory byte blob. The blob is byte-identical to the engine's archival
// writer because both use the same in-tree writer and PCM_24 policy.
// Encoding happens exactly once per render; every reuse consumes the bytes
// directly.
bool encode_pcm24_wav_blob(const std::vector<float>& samples,
                           int channels, int sample_rate,
                           std::vector<char>& out_blob);

// Decodes a wav blob's full payload to interleaved float32, verifying header
// channels and sample rate. Deterministic: the same blob always yields the
// same floats.
bool decode_wav_blob_to_float(const std::vector<char>& blob,
                              int expected_channels,
                              int expected_sample_rate,
                              std::vector<float>& out_samples);

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

// Returns the path of a deliverable wav in `preferred`'s directory whose
// .fingerprint sidecar matches `fingerprint`, or empty. `preferred` is
// auditioned first (the common current-title case costs one stat chain,
// no scan); on miss, the directory's *.fingerprint entries are tried in
// sorted order (deterministic pick among reuse-equivalent candidates: since
// v16 the fingerprint carries the render-environment quartet beside the
// recipe, so a match names the same recipe rendered under the same
// libraries — byte-equivalent under the fingerprint trust boundary; sorted
// order just makes the choice stable). A non-null `out_identity` receives
// the returned wav's validation-time stat identity (see
// fingerprint_sidecar_matches). `exclude` (may be empty) is never returned —
// the caller has already handled that path (do_render's same-path up-to-date
// rung). The scan is LEXICALLY non-recursive over the one directory:
// renders/ cells are never reuse sources (standing ruling) because they live
// in a subdirectory the scan never descends into. It FOLLOWS symlinks
// (directory_entry::is_regular_file semantics) and excludes `preferred` /
// `exclude` by pathname string only, so a hand-placed symlink or alias can
// point a candidate pair at bytes outside the directory — acceptable by the
// standing non-adversarial ruling: an honestly matching pair has recipe
// validity under the fingerprint trust boundary, and hand-placed symlink
// aliases are outside the catered domain.
std::string find_reusable_artifact(const std::string& preferred,
                                   const std::string& exclude,
                                   const std::vector<uint8_t>& fingerprint,
                                   ArtifactStatIdentity* out_identity = nullptr);

// Two-tier store for rendered target-view and archival audio, keyed by
// render_fingerprint. Entries are canonical deliverable wav bytes encoded
// exactly once as PCM_24, the sole deliverable format. For
// target-route buffers, limited masters arrive pre-quantized, so that single
// PCM_24 writer-thread encode is an exact re-expression rather than a lossy
// step and render completion never waits for it. The in-tree encode/decode
// pair is roundtrip-exact over the full 24-bit lattice; byte-canonical entries
// are still retained because byte copies are conversion-free by construction,
// publishes stay zero-work, and PCM_24 blobs are smaller than float buffers.
// The RAM tier serves short live target-view renders. The disk tier is
// uncapped per entry and LRU-bounded at 10 GiB, so full-movement renders can
// be reused without monopolizing RAM. The store is process-local: the disk
// directory is removed at shutdown and dead-PID orphan directories are swept at
// init. Every public method is a no-op / miss when the store could not
// initialize (no cache home, unmakeable directory), so callers need no
// special-casing.
//
// Every public method is thread-safe; callers need no external locking. A
// single mutex_ guards both tiers' indexes, the byte counters, lru_seq_, and
// the writer thread handoff. Large file I/O (disk-tier reads/copies, the disk
// writer's blob write) happens outside the lock: a disk lookup copies the
// entry's filename out under the lock, reads the wav unlocked, and re-takes
// the lock only for the LRU bump or a drop-on-failure. The writer thread
// lifecycle is swap-join-outside — join_writer() swaps writer_ into a local
// under the lock, unlocks, then joins the local, so no lock is ever held
// across a join; the writer body itself takes the lock only for its
// registration/eviction step at the end, plus the post-encode replacement drop
// when a target-master job routes to disk.
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
    // recency. Disk hits confirm the wav's .fingerprint sidecar, then read the
    // interleaved float32 wav payload. Any miss or mismatch returns false and
    // leaves out_samples untouched. A disk entry whose file pair is missing or
    // fails validation is dropped from the index and reported as a miss.
    bool lookup(const std::vector<uint8_t>& fingerprint,
                int channels, int sample_rate,
                std::vector<float>& out_samples);

    // Confirmed-hit publish. Writes the entry's canonical wav bytes to
    // staging_path (RAM: dump the blob; disk: byte-copy the entry file). Same
    // confirmation rules as lookup. Byte-identical to an engine publish by
    // construction; no sample conversion occurs.
    bool publish_wav(const std::vector<uint8_t>& fingerprint,
                     int channels, int sample_rate,
                     const std::string& staging_path);

    // Insert a freshly rendered wav blob. Routes short buffers that fit the RAM
    // budget to RAM and everything else to the disk tier, evicting LRU entries
    // in the chosen tier until within budget. Overwrites any existing entry
    // with the same hash. Empty/degenerate blobs are dropped. Disk write
    // failures are swallowed (the render already played from the live buffer;
    // the cache simply will not hold it).
    void insert(const std::vector<uint8_t>& fingerprint,
                const std::vector<char>& wav_blob,
                int channels, int sample_rate, int64_t frame_count);

    // Insert freshly rendered target-route samples. On the limited route they
    // are already on the PCM_24 deliverable lattice. The samples are copied
    // into a writer-thread job and encoded to the canonical PCM_24 wav blob
    // there, so the caller (the render worker's completion path) never waits
    // on the encode; routing to the RAM or disk tier happens after the encode
    // under the usual mutex. A lookup that lands before the encode finishes
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
    struct RamEntry {
        std::vector<uint8_t> fingerprint;
        std::vector<char>    blob;
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
                    const std::vector<char>& blob,
                    int channels, int sample_rate);
    bool insert_disk(uint64_t h, const std::vector<uint8_t>& fp,
                     const std::vector<char>& blob, int64_t frame_count);
    void start_writer_job(WriterJob job);
    void evict_ram_until(uint64_t target_max);
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
