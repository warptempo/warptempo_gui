#pragma once

#include "engine_settings.h"
#include "render_cache.h"
#include "warp_frame_map.h"
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
//   - Cancelled: the cancel token was observed mid-pipeline; partial output
//                cleaned up by cleanup_all; no final file on disk.
enum class RenderOutcome { Success, Failed, Cancelled };

// Authoring-state snapshot captured at dispatch time. Sole consumer: the
// per-entry `.settings` writer inside do_render, which composes the
// standard whole-file schema from these fields (the commit tab's trim and
// identity plus the session prefs) so the Shift+. render-commit
// (adopt_render_entry) can adopt the entry with plain load semantics. The
// trim fields duplicate the request's own trim
// on purpose: the request trim feeds the engine, this block feeds the
// sidecar, and keeping the block self-contained keeps the writer trivial.
struct AuthoringSnapshot {
    char    active_tab        = 'A';
    bool    has_trim_begin    = false;
    int64_t trim_begin_frame  = 0;     // source frames
    bool    has_trim_end      = false;
    int64_t trim_end_frame    = 0;     // source frames

    // Dispatch-time session prefs the standard .settings schema needs and
    // the request does not otherwise carry. Types match the AppState fields
    // they are captured from.
    char        active_markers_view = 'W';   // 'W' or 'P'
    float       playback_speed      = 1.0f;
    bool        follow              = true;
    double      font_size           = 11.0;
    std::string audio_player;                // empty = unset

    // Dispatch-time browse position, captured on the TARGET axis: the
    // entry's .settings persists active_audio_view=T, so the browse keys
    // live on the target axis. A source-view dispatch forward-maps the live
    // playhead through the live target map exactly the way the S-to-T toggle
    // does (anchoring the same screen column); a target-view dispatch takes
    // the live values verbatim. The values are the position the user was at
    // when the render was queued or dispatched. do_render's wav-arm writer
    // clamps them into the entry's own map domain before writing (a sweep
    // cell's rewritten markers can give the cell a shorter target axis).
    int64_t view_viewport_start_frame = 0;   // target-axis frames
    int     view_zoom_level           = 0;   // persisted zoom vocabulary
    int64_t view_playhead_frame       = 0;   // target-axis frames
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

    // Borrowed source audio: the GUI's resident sample buffer, captured at
    // dispatch. Required contract: every dispatcher populates it
    // (attach_shared_render_resources, or the target view's direct fill); it
    // is never absent in a program-built request, and do_render compensates
    // for nothing — it reads the buffer directly. The source is loaded once
    // at launch and never replaced in-session; shared ownership keeps the
    // buffer alive for the render's duration.
    std::shared_ptr<const std::vector<float>> source_samples;

    // Load-time identity of the file backing source_samples, captured by
    // GuiAudio at decode (a load whose stat fails is refused, so a loaded
    // source always carries its identity). Required contract, same as
    // source_samples: every dispatcher populates the pair. do_render records
    // it directly as the wav render's fingerprint source identity — the
    // source is immutable for the process lifetime, so there is no on-disk
    // re-stat.
    uint64_t source_load_size = 0;
    int64_t  source_load_mtime = 0;

    // Full phase reset store snapshot. Batch source-domain sidecar payload:
    // when batch_folder is set, this list is written verbatim as
    // `<batch_folder>/<batch_basename>.phaseresetmarkers`, including the
    // empty-file form for an empty list. The single-phase reset sidecar
    // path used by the immediate Ctrl+Alt+R render branch does not read
    // this field for sidecar emission.
    std::vector<GuiPhaseResetMarker> phase_resets;

    // Settings-side trim, sourced from AppState by the Ctrl+Alt+R / queue
    // submission paths. AppState::trim is the live mirror of the active tab's
    // trim; .settings stores tab_a_trim_begin / tab_a_trim_end and the B-tab
    // counterparts. Trim is wav-only: the wav arm forwards these bounds to
    // the prepost trimmer (plan_trim: cut source view, translated maps,
    // output crop; a plan refusal falls back to the full, untrimmed
    // deliverable with one stderr line), and the map formats silently
    // ignore any set bounds — they always write the FULL maps.
    bool    has_trim_begin = false;
    int64_t trim_begin_frame = 0;    // source frames
    bool    has_trim_end   = false;
    int64_t trim_end_frame   = 0;    // source frames

    AuthoringSnapshot authoring;

    // Nullable. When non-null and output_format is "wav", do_render routes
    // the render to this buffer instead of a staged .wav file. Skips the
    // atomic rename and every batch sidecar write (.warpmarkers /
    // .phaseresetmarkers / .settings). The post-engine chain
    // (post_trim crop when trimmed, then the always-on spectral + peak
    // limited chain, exactly as on the disk path) runs in place on this
    // buffer; the buffer is then quantized to the deliverable PCM_24 lattice
    // in place. Defaults to nullptr; the existing wav-to-disk path is taken
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
    // and (wav only) `.settings` sidecars in the same folder. For wav
    // renders, those sidecars are commit-critical to success;
    // `.fingerprint` is an optional cache artifact.
    // The folder must already exist; do_render does not create it. When
    // `batch_folder` is empty, do_render uses the
    // source-directory title naming used by the
    // immediate Ctrl+Alt+R path. Sibling wav publishes still emit the
    // `.fingerprint` sidecar; batch-only sidecars are not written on the
    // sibling path.
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
// failure path; Cancelled if `cancel_token` (when non-null) was observed set
// inside the engine. The token is GuiAsyncRenderer's per-dispatch session
// cancel token: created fresh for each dispatch and never reset, so it names
// exactly this render's session even when a copy outlives the render (the
// cache writer thread holds one). The queue walker uses the outcome to count
// successes and to detect mid-render cancellation.
RenderOutcome do_render(const RenderRequest& req,
                        std::shared_ptr<const std::atomic<bool>> cancel_token =
                            nullptr);

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
