#pragma once

#include "engine_settings.h"
#include "warp_frame_map_build.h"  // MarkerForRender (the resolver's output)

#include <cstdint>
#include <string>
#include <vector>

struct RenderFileIdentity {
    uint64_t size = 0;
    int64_t mtime = 0;
};

// The one on-disk stat implementation: size and the mtime in nanoseconds.
// Render fingerprints identify the source by size and mtime only. Folding in
// content identity would bump the fingerprint content version and invalidate
// every archival sidecar, so size and mtime are the source trust boundary.
bool stat_file_identity(const std::string& path, RenderFileIdentity& out);

// Canonical RENDER-IDENTITY fingerprint: "would a fresh render of this
// recipe, in this environment, produce these bytes". The key serializes, in
// order: the content version; the render-environment quartet
// (compute_render_env_hashes() — the four library stat-identity digests
// actually mapped into THIS process, so a pre-upgrade artifact can never match a
// post-upgrade recipe); source path + source file identity; sample rate; every
// EngineSettings field (full-recipe key — the exhaustive decision switch in
// the serializer is the single drift guard); the trim
// bounds (frame values normalized to 0 when their bound is unset); and the
// RESOLVED marker state — the exact engine inputs, not the raw stores:
// resolve_warp_markers_for_render's survivors (per marker: frame, resolved
// tempo cents, typed scale, label def/ref — precisely the MarkerForRender
// fields build_warp_frame_map reads) and build_phase_reset_source_frames'
// collapsed enabled positions as whole int64 frames. Raw disabled markers,
// dropped fields, and collapsed duplicates therefore no longer move the key:
// two states normalization proves render-identical share a fingerprint.
// The sole caller is do_render, which threads an already-resolved product
// through (its own resolve produces the per-resolve stderr lines). GUI-only
// marker session scratch (iteration / BPM authoring) never reaches the
// resolver, so it is excluded by construction. Same inputs always produce
// byte-identical output; the result is hex-encoded into the .fingerprint
// sidecar and exact-compared by fingerprint_sidecar_matches — the render
// attestation and the same-path up-to-date check.
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

// Per-process directory manager for the inert framemap-pair artifacts. The
// archival render pipeline (do_render's disk route) and the CLI drop the FULL
// warp + phase-reset frame map pair into <cache home>/warptempo_gui/<pid>/ as
// write-only future-proofing — no product path reads the pair back; a future
// diff-capable render-comparison algorithm is the intended consumer. The
// directory hosts nothing else: it is created at init, dead-PID orphan
// siblings are swept at the next launch, and this process's directory is
// removed at shutdown, so a pair rests only between a render and program
// close and nothing accumulates. Every method is a no-op / empty result when
// the directory could not be created (no cache home, unmakeable directory),
// so callers need no special-casing.
class RenderCacheDir {
public:
    // Create the per-process directory under <cache home>/warptempo_gui/<pid>/
    // and sweep dead-PID siblings. Idempotent enough to call once at startup.
    // On any failure the manager stays disabled (process_dir() returns empty).
    void init();

    // Remove this process's directory. Call at shutdown. Safe if init()
    // failed or never ran.
    void shutdown();

    // Absolute path of this process's cache directory
    // (<cache home>/warptempo_gui/<pid>), or an empty string when the manager
    // is disabled (no cache home / unmakeable directory). The archival render
    // pipeline drops the full framemap pair here as future-proofing: the
    // directory is removed at shutdown and orphan-swept at the next launch, so
    // the pair rests only between a render and program close and nothing
    // accumulates. Read-only; set once at init before any render dispatches.
    std::string process_dir() const;

private:
    void sweep_orphans();

    bool        enabled_ = false;
    std::string parent_;   // <cache home>/warptempo_gui
    std::string dir_;      // parent_/<pid>
};
