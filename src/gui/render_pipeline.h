#pragma once

#include "engine_settings.h"
#include "render_cache.h"
#include "warpmarkers.h"
#include "phaseresetmarkers.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Tristate result of do_render. Returned from the synchronous entry point
// and propagated through GuiAsyncRenderer to on_done callbacks.
//   - Success:   pipeline ran to completion; final output exists on disk.
//   - Failed:    early-return error path (frame-map build, engine/render,
//                output write/publish, or rename); diagnostics already on
//                stderr.
//   - Cancelled: cancel_flag was observed mid-pipeline; partial output
//                cleaned up by cleanup_all; no final file on disk.
enum class RenderOutcome { Success, Failed, Cancelled };

// Authoring-state snapshot captured at dispatch time, written into the
// batch entry's .rendersettings so Ctrl+Alt+C can restore the exact
// state that produced the render. The trim fields duplicate the
// request's own trim on purpose: the request trim feeds the engine,
// this block feeds the sidecar, and keeping the sidecar block
// self-contained keeps the reader trivial.
struct AuthoringSnapshot {
    bool    valid             = false;  // false: write no authoring block
    char    active_tab        = 'A';
    char    active_audio_view = 'S';
    bool    has_trim_begin    = false;
    double  trim_begin_sec    = 0.0;
    bool    has_trim_end      = false;
    double  trim_end_sec      = 0.0;
    int     zoom_level        = 0;
    int64_t viewport_start    = 0;
    int64_t playhead          = 0;
};

// Self-contained view of the AppState fields do_render reads. Constructed by
// GUI dispatch code at request-build time so do_render stays decoupled from
// AppState's (anonymous-namespace) shape.
struct RenderRequest {
    std::string            source_audio_path;
    std::vector<GuiWarpMarker> markers;
    // Typed engine settings, copied from app.engine_settings at dispatch
    // time. The only carrier of engine settings into the render pipeline;
    // do_render reads engine-relevant keys exclusively from this struct.
    EngineSettings engine_settings;

    // User-curated phase reset frame list (source-frame domain). When non-empty
    // and the active engine is "warptempo", this overrides the engine's
    // internal detection — typical population is the union of inserted +
    // active-detected entries from the GUI's phase reset list, with disabled
    // entries filtered out and time_seconds converted to source frames at
    // the GUI-to-engine boundary via banker's rounding.
    std::vector<int64_t>   phase_reset_frames;

    // Borrowed source audio: the GUI's resident sample buffer plus its
    // frame count, captured at dispatch. When set and sufficient, do_render
    // skips the source-sample-cache read entirely. Null means fall back to
    // the self-contained cache read (defensive; the GUI always populates
    // it). Shared ownership makes this safe across file loads mid-render;
    // the transient cost of two live buffers during such a swap is accepted.
    std::shared_ptr<const std::vector<float>> source_samples;
    int64_t source_total_frames = 0;

    // Full phase reset store snapshot. Batch sidecar payload only: when
    // batch_folder is set and this list is non-empty, written verbatim as
    // `<batch_folder>/<batch_basename>.phaseresetmarkers` so render-view
    // can later display the phase reset set this render was produced from.
    // A second sidecar `<batch_basename>.renderphaseresetmarkers` carries
    // the same set warped into render-domain frame coordinates. The
    // single-phase reset sidecar path used by the immediate Ctrl+Alt+R
    // render branch does not read this field for sidecar emission.
    // Empty list disables sidecar emission cleanly.
    std::vector<GuiPhaseResetMarker> phase_resets;

    // Settings-side trim, sourced from AppState by the Ctrl+Alt+R / queue
    // submission paths. AppState::trim is the live mirror of the active tab's
    // trim; .settings stores tab_a_trim_begin / tab_a_trim_end and the B-tab
    // counterparts. The render pipeline forwards the active tab's trim to
    // MapBuildInput, which drives the frame_map post-pass + write_trimmed_wav
    // cut.
    bool   has_trim_begin = false;
    double trim_begin_sec = 0.0;
    bool   has_trim_end   = false;
    double trim_end_sec   = 0.0;

    AuthoringSnapshot authoring;

    // Nullable. When non-null and output_format is "wav", do_render routes
    // the engine to this buffer instead of a staged .wav file. Skips the
    // atomic rename, the peak-pyramid sidecar write, and every batch sidecar
    // write (.warpmarkers / .phaseresetmarkers / .rendersettings /
    // .renderwarpmarkers / .renderphaseresetmarkers). The limited chain
    // (spectral + peak backstop) runs in place on this buffer whenever the
    // global `limiter` toggle is on, exactly as on the disk path; on that
    // limited route, the buffer is quantized to the deliverable PCM_24 lattice
    // in place after the limited chain. Defaults to nullptr; the existing
    // wav-to-disk path is taken when null. Reserved for target-view target
    // rendering; not authoring-facing. Non-wav output_format branches
    // silently ignore this field.
    std::vector<float>* output_buffer = nullptr;

    // Batch render output. When `batch_folder` is non-empty, do_render
    // writes its final output to `<batch_folder>/<batch_basename>` with the
    // selected output-format extension (.wav, .warpframemap, or .tempomap)
    // and, on success, deposits the per-render
    // `<batch_basename>.warpmarkers`, `<batch_basename>.phaseresetmarkers`
    // (when phase resets is non-empty), and `.rendersettings` sidecars in
    // the same folder. Wav renders also emit `.peaks` and render-domain
    // marker sidecars. The folder must already exist; do_render does not
    // create it. When `batch_folder` is empty, do_render uses the
    // source-directory title/limiter-prefix naming used by the
    // immediate Ctrl+Alt+R path. Sibling wav publishes still emit `.peaks`
    // and `.fingerprint` sidecars; batch-only render-view sidecars are not
    // written on the sibling path.
    std::string batch_folder;
    std::string batch_basename;

    // The process's single RenderCache, populated at request-build time
    // from the same instance main.cpp constructs and hands to
    // GuiTargetRender by reference. do_render's cache lookup/insert rung is
    // skipped (defensively; the GUI always populates this) when null.
    RenderCache* render_cache = nullptr;
};

// Synchronous render. Blocks the caller until the pipeline finishes (or
// errors out, or is cancelled). All progress / error reporting goes to
// stderr. Returns RenderOutcome — Success on a complete render (including
// the rename-into-place of the staged output); Failed on every early-return
// failure path; Cancelled if `cancel_flag` (when non-null) was observed set
// inside the engine. The queue walker uses the outcome to count successes and
// to detect mid-render cancellation.
RenderOutcome do_render(const RenderRequest& req,
                        const std::atomic<bool>* cancel_flag = nullptr);

// Compose the single-render ("Ctrl+Alt+R") sibling output path for a given
// source path and engine settings — the path do_render writes when
// req.batch_folder is empty. Mirrors the inline composition in
// do_render; both must stay in lockstep. Directory is the
// source's parent ("." when the source has no parent). Extension is
// selected by output_format; the clean-float-wav path carries the
// `limiter=false;` filename prefix.
std::filesystem::path compose_sibling_output_path(
    const std::string& source_audio_path,
    const EngineSettings& es);

// Assemble a RenderRequest from GUI authoring state. Single construction point
// shared by every dispatch path (single render, queue batch, BPM-sweep batch,
// iteration batch, and the target-view buffer render). Derives
// phase_reset_frames internally via slice_to_phaseresetmarkers +
// phase_reset_source_frames so all callers stay in lockstep on that
// filter-disabled + banker's-round derivation. output_buffer is left at its
// nullptr default; the target-view caller sets it after the call.
RenderRequest build_render_request(std::string source_audio_path,
                                   std::vector<GuiWarpMarker> markers,
                                   std::vector<GuiPhaseResetMarker> phase_resets,
                                   EngineSettings engine_settings,
                                   bool has_trim_begin, double trim_begin_sec,
                                   bool has_trim_end,   double trim_end_sec,
                                   long sample_rate,
                                   std::string batch_folder = {},
                                   std::string batch_basename = {});
