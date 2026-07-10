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
//   - Failed:    early-return error path (warp-frame-map build, engine/render,
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
    int64_t trim_begin_frame  = 0;     // source frames
    bool    has_trim_end      = false;
    int64_t trim_end_frame    = 0;     // source frames
    int     zoom_level        = 0;
    int64_t viewport_start    = 0;
    int64_t playhead          = 0;
};

// Self-contained view of the AppState fields do_render reads. Constructed by
// GUI dispatch code at request-build time so do_render stays decoupled from
// AppState's (anonymous-namespace) shape.
struct RenderRequest {
    std::string            source_audio_path;
    std::vector<GuiWarpMarker> warp_markers;
    // Typed engine settings, copied from app.engine_settings at dispatch
    // time. The only carrier of engine settings into the render pipeline;
    // do_render reads engine-relevant keys exclusively from this struct.
    EngineSettings engine_settings;

    // Borrowed source audio: the GUI's resident sample buffer plus its
    // frame count, captured at dispatch. Required contract: every dispatcher
    // populates these (attach_shared_render_resources, or the target view's
    // direct fill); they are never absent in a program-built request, and
    // do_render compensates for nothing — it reads the buffer directly.
    // Shared ownership makes this safe across file loads mid-render; the
    // transient cost of two live buffers during such a swap is accepted.
    std::shared_ptr<const std::vector<float>> source_samples;
    int64_t source_total_frames = 0;

    // Load-time identity of the file backing source_samples, captured by
    // GuiAudio at decode (a load whose stat fails is refused, so a loaded
    // source always carries its identity). Required contract, same as
    // source_samples: every dispatcher populates the pair. do_render
    // compares it against the dispatch-time identity before rendering and
    // before associating a borrowed-buffer render with a fingerprint.
    uint64_t source_load_size = 0;
    int64_t  source_load_mtime = 0;

    // Full phase reset store snapshot. Batch source-domain sidecar payload:
    // when batch_folder is set, this list is written verbatim as
    // `<batch_folder>/<batch_basename>.phaseresetmarkers`, including the
    // empty-file form for an empty list.
    // A second sidecar `<batch_basename>.renderphaseresetmarkers` carries
    // display resets warped into render-domain frame coordinates. The
    // single-phase reset sidecar path used by the immediate Ctrl+Alt+R
    // render branch does not read this field for sidecar emission.
    std::vector<GuiPhaseResetMarker> phase_resets;

    // Settings-side trim, sourced from AppState by the Ctrl+Alt+R / queue
    // submission paths. AppState::trim is the live mirror of the active tab's
    // trim; .settings stores tab_a_trim_begin / tab_a_trim_end and the B-tab
    // counterparts. Trim is wav-only: the wav arm forwards these bounds to
    // the prepost trimmer (plan_trim: cut source view, translated maps,
    // output crop), and the map formats refuse when a bound is set.
    bool    has_trim_begin = false;
    int64_t trim_begin_frame = 0;    // source frames
    bool    has_trim_end   = false;
    int64_t trim_end_frame   = 0;    // source frames

    AuthoringSnapshot authoring;

    // Nullable. When non-null and output_format is "wav", do_render routes
    // the render to this buffer instead of a staged .wav file. Skips the
    // atomic rename, the peak-pyramid sidecar write, and every batch sidecar
    // write (.warpmarkers / .phaseresetmarkers / .rendersettings /
    // .renderwarpmarkers / .renderphaseresetmarkers). The post-engine chain
    // (post_trim crop when trimmed, then the spectral + peak limited chain
    // whenever the global `limiter` toggle is on, exactly as on the disk
    // path) runs in place on this buffer; on the limited route, the buffer
    // is quantized to the deliverable PCM_24 lattice in place after the
    // chain. Defaults to nullptr; the existing wav-to-disk path is taken
    // when null. Reserved for target-view target rendering; not
    // authoring-facing. Non-wav output_format branches silently ignore this
    // field.
    std::vector<float>* output_buffer = nullptr;

    // Batch render output. When `batch_folder` is non-empty, do_render
    // writes its final output to `<batch_folder>/<batch_basename>` with the
    // selected output-format extension (.wav for wav, .warpframemap for
    // generic_map and for warptempo_maps — whose .phaseresetframemap
    // sibling lands beside it — .miditempomap for midi_map)
    // and attempts the per-render source-domain
    // `<batch_basename>.warpmarkers`, `<batch_basename>.phaseresetmarkers`,
    // and `.rendersettings` sidecars in the same folder. For wav renders,
    // those sidecars are commit-critical to success; `.peaks`,
    // `.fingerprint`, and render-domain marker sidecars are optional
    // display/cache artifacts.
    // The folder must already exist; do_render does not create it. When
    // `batch_folder` is empty, do_render uses the
    // source-directory title/limiter-prefix naming used by the
    // immediate Ctrl+Alt+R path. Sibling wav publishes still emit `.peaks`
    // and `.fingerprint` sidecars; batch-only render-view sidecars are not
    // written on the sibling path.
    std::string batch_folder;
    std::string batch_basename;

    // The process's single RenderCache, populated at request-build time
    // from the same instance main.cpp constructs and hands to
    // GuiTargetRender by reference. Required contract: every dispatcher
    // populates it; it is never null in a program-built request, and
    // do_render dereferences it without a null check.
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

// Output-path composition (render_output_extensions / render_output_stem /
// compose_render_output_paths) lives parser-side in render_output_naming.h so
// the GUI render pipeline and the headless CLI binaries compose byte-identical
// paths from one implementation.

// Assemble a RenderRequest from GUI authoring state. Single construction point
// shared by every dispatch path (single render, queue batch, BPM-sweep batch,
// iteration batch, and the target-view buffer render). RenderRequest carries
// marker positions as authored int64 source frames; do_render validates the
// engine's reset list against the probed source's length at the probe —
// the same validate-at-the-probe shape warp markers follow through
// build_warp_frame_map (a sidecar is authored against one audio file's frame
// grid). output_buffer is left at its nullptr default; the
// target-view caller sets it after the call.
RenderRequest build_render_request(std::string source_audio_path,
                                   std::vector<GuiWarpMarker> warp_markers,
                                   std::vector<GuiPhaseResetMarker> phase_resets,
                                   EngineSettings engine_settings,
                                   bool has_trim_begin, int64_t trim_begin_frame,
                                   bool has_trim_end,   int64_t trim_end_frame,
                                   std::string batch_folder = {},
                                   std::string batch_basename = {});
