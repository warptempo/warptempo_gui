#pragma once

#include "warpmarkers.h"
#include "phase_reset_markers.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Tristate result of do_render. Returned from the synchronous entry point
// and propagated through GuiAsyncRenderer to on_done callbacks.
//   - Success:   pipeline ran to completion; final output exists on disk.
//   - Failed:    early-return error path (timemap build, engine, subprocess,
//                rename); diagnostics already on stderr.
//   - Cancelled: cancel_flag was observed mid-pipeline; partial output
//                cleaned up by cleanup_all; no final file on disk.
enum class RenderOutcome { Success, Failed, Cancelled };

// Self-contained view of the AppState fields do_render reads. Constructed by
// the Ctrl+Alt+R handler in main.cpp so do_render stays decoupled from
// AppState's (anonymous-namespace) shape.
struct RenderRequest {
    std::string            source_audio_path;
    std::vector<GuiWarpMarker> markers;
    std::vector<std::pair<std::string, std::string>> settings_passthrough;

    // User-curated phase reset frame list (source-frame domain). When non-empty
    // and the active engine is "warptempo", this overrides the engine's
    // internal detection — typical population is the union of inserted +
    // active-detected entries from the GUI's phase reset list, with disabled
    // entries filtered out and time_seconds converted to source frames at
    // the GUI-to-engine boundary via banker's rounding.
    std::vector<int64_t>   phase_reset_frames;

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
    // submission paths. Lifted out of warp markers per the brief: trim
    // now lives in .settings (trim_begin / trim_end keys), not on b=/e=
    // markers. The render pipeline forwards these to TimemapBuildInput,
    // which drives the timemap post-pass + write_trimmed_wav cut.
    bool   has_trim_begin = false;
    double trim_begin_sec = 0.0;
    bool   has_trim_end   = false;
    double trim_end_sec   = 0.0;

    // Batch render output. When `batch_folder` is non-empty, do_render
    // writes its final output to `<batch_folder>/<batch_basename>.wav` (or
    // `.mid` for midi) and, on success, deposits the per-render
    // `<batch_basename>.warpmarkers`, `<batch_basename>.phaseresetmarkers`
    // (when phase resets is non-empty), and `<batch_basename>.peaks` sidecars
    // in the same folder. The folder must already exist; do_render does
    // not create it. When `batch_folder` is empty, the source-directory
    // title/engine/limiter-prefix naming is used (unchanged from the
    // immediate Ctrl+Alt+R path) and no sidecars are written.
    std::string batch_folder;
    std::string batch_basename;
};

// Synchronous render. Blocks the caller until the pipeline finishes (or
// errors out, or is cancelled). All progress / error reporting goes to
// stderr. Returns RenderOutcome — Success on a complete render (including
// the rename-into-place of the staged output); Failed on every early-return
// failure path; Cancelled if `cancel_flag` (when non-null) was observed set
// at a frame boundary inside the warptempo synthesis loop or at the 10 ms
// subprocess-poll cadence for adapter engines. The queue walker uses the
// outcome to count successes and to detect mid-render cancellation.
RenderOutcome do_render(const RenderRequest& req,
                        const std::atomic<bool>* cancel_flag = nullptr);
