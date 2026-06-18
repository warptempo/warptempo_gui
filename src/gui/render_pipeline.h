#pragma once

#include "engine_settings.h"
#include "warpmarkers.h"
#include "phase_reset_markers.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Tristate result of do_render. Returned from the synchronous entry point
// and propagated through GuiAsyncRenderer to on_done callbacks.
//   - Success:   pipeline ran to completion; final output exists on disk.
//   - Failed:    early-return error path (frame_map build, engine, subprocess,
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
    // submission paths. Lifted out of warp markers into project settings: trim
    // now lives in .settings (trim_begin / trim_end keys), not on b=/e=
    // markers. The render pipeline forwards these to MapBuildInput,
    // which drives the frame_map post-pass + write_trimmed_wav cut.
    bool   has_trim_begin = false;
    double trim_begin_sec = 0.0;
    bool   has_trim_end   = false;
    double trim_end_sec   = 0.0;

    // Nullable. When non-null and output_format is "wav", do_render routes
    // the engine to this buffer instead of a staged .wav file. Skips the
    // atomic rename, the peak-pyramid sidecar write, and every batch sidecar
    // write (.warpmarkers / .phaseresetmarkers / .rendersettings /
    // .renderwarpmarkers / .renderphaseresetmarkers). The limited chain
    // (spectral + peak backstop) runs in place on this buffer whenever the
    // global `limiter` toggle is on, exactly as on the disk path. Defaults
    // to nullptr; the existing wav-to-disk path is taken when null. Reserved
    // for target-view target rendering; not authoring-facing. Non-wav
    // output_format branches silently ignore this field.
    std::vector<float>* output_buffer = nullptr;

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
