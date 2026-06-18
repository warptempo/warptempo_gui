#pragma once

#include "engine_settings.h"
#include "render.h"
#include "text_editor.h"
#include "phase_reset_clipboard.h"
#include "phase_reset_markers.h"
#include "frame_map_view.h"
#include "warpmarkers.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

class GuiAudio;

// Zoom level numbering. kFitFileLevel = 0 ("whole file visible", computed
// at zoom / resize time, not stored as a fixed ms/pixel). Numeric levels
// run kMinNumericLevel..kMaxNumericLevel inclusive, each exactly 2x the
// previous in ms-per-pixel. kZoomTableSize is the size of the table in
// main.cpp (one sentinel slot at index 0 plus the numeric levels).
//
// Identity binding for the bare-digit keys: digit N selects level N.
// Smaller digit = less file per window = more zoomed in; digit 0 =
// fit-file (most file possible).
constexpr int kFitFileLevel     = 0;
constexpr int kMinNumericLevel  = 1;
constexpr int kMaxNumericLevel  = 9;
constexpr int kZoomTableSize    = 10;

// Viewport lead/overlap fraction, expressed as a divisor of the visible
// span. Follow mode keeps this much of the window as lead context when it
// re-anchors; paged scroll (PageUp/PageDown) retains the same fraction as
// overlap so the two behaviors stay visually consistent. One source of
// truth — do not inline the divisor at either site.
constexpr int64_t kViewportLeadDivisor = 10;

// Hoisted from main.cpp's anonymous namespace so the hit_test_*
// free functions (in app_state.cpp) and the GuiInputHandler mouse handler
// (in input_handler.cpp) can reach them.
constexpr int kMarkerHitHalfPx    = 4;

// Op-kind tag carried on every undo/redo entry. Marker selection collapses
// per kind on undo: Create restores selection to the just-created marker,
// Destroy restores to the hint-last-selected captured pre-op so the user
// gets back the focus they had, Move/Other use the snapshot's last_selected
// to reproduce playhead behavior; count-preserving ops split Move vs Other
// so Move can restore the "what just moved" selection.
enum class OpKind { Create, Destroy, Move, Other };

// Wholesale snapshot of the authoring-class settings. Captured at undo-push
// time and restored on undo/redo. Holds the typed EngineSettings plus the
// project-level trim pair. Cost stays negligible per entry.
struct SettingsSnapshot {
    EngineSettings engine_settings;
    double trim_begin     = 0.0;
    double trim_end       = 0.0;
    bool   has_trim_begin = false;
    bool   has_trim_end   = false;
};

// One entry on either stack. Carries the pre-mutation marker snapshot plus
// a pre-op selection hint (so Undo-of-Destroy / Undo-of-Move can restore
// a sensible selection anchor) and the op kind.
//
// Every entry also carries the pre-mutation phase reset
// snapshot and the mode the operation was performed in. Both lists are
// always restored on undo/redo so the inverse is symmetric regardless of
// which list the op actually touched. `op_mode` lets undo flip the active
// mode; `tab` lets undo switch the active tab — both are context tags
// that restore the original authoring view as visual feedback for what's
// being undone.
//
// Carry-everywhere shape: every entry — marker, phase reset, or settings
// — populates `settings` from app at push time, so do_undo/do_redo can
// restore wholesale without caring which subset actually changed.
// op_mode 'S' marks settings-only entries: those skip the mode-switch
// and post-restore-rules dispatch since they don't carry an authoring
// selection-anchor.
struct UndoEntry {
    std::vector<GuiWarpMarker>      snapshot;
    std::vector<GuiPhaseResetMarker> phase_reset_snapshot;
    SettingsSnapshot          settings;
    char                      op_mode              = 'W';
    char                      tab                  = 'A';
    OpKind                    op_kind              = OpKind::Other;
    int                       hint_last_selected   = -1;
};

// Ctrl+drag state. `active` gates motion handling; the rest captures the
// pre-drag snapshot so Escape can restore positions and clamps can be
// evaluated without re-scanning the marker list on every motion event.
//
// Storing per-marker (min_allowed, max_allowed) as the spec suggests works
// for a contiguous drag set, but a non-contiguous set (e.g. indices 2 and
// 5 selected, 3 and 4 not) can be bounded more tightly by the nearest
// non-selected neighbors of every dragged marker. We precompute a single
// scalar `delta_min` / `delta_max` that's correct for both cases: the delta
// is applied uniformly, so its feasible range is the intersection of per-
// marker per-neighbor bounds. Trim is purely cosmetic and does not
// constrain edits.
struct DragState {
    bool                active = false;
    std::vector<int>    dragging_markers;   // sorted ascending
    std::vector<double> original_times;     // parallel to dragging_markers
    // Proposed new times during motion, parallel to dragging_markers.
    // Written by apply_drag_motion as original_times[k] + delta; consumed
    // by paint via DragOverlay so the live marker store stays untouched
    // until commit. Seeded from original_times at begin_drag.
    std::vector<double> moveable_times;
    double              anchor_mouse_time_seconds = 0.0;
    double              delta_min = -std::numeric_limits<double>::infinity();
    double              delta_max =  std::numeric_limits<double>::infinity();
    bool                moved = false;
    // Pre-drag frame_map snapshot. Captured at begin_drag via
    // build_target_view_frame_map so paint can route selected-marker
    // positions and target-view waveform through a frozen coordinate
    // system for the duration of the drag. Empty when source view is
    // active at begin_drag time, or when the build failed.
    std::vector<FrameMapSegment> frozen_frame_map;
    // Full pre-drag marker state. Captured at button-press so commit_drag
    // can push it onto the undo stack when motion landed; discarded on
    // commit when no motion occurred (DragState is reset wholesale there).
    std::vector<GuiWarpMarker>      pre_drag_snapshot;
    std::vector<GuiPhaseResetMarker> pre_drag_phase_reset_snapshot;
    // Pre-drag last_selected for the undo hint; carried onto the entry at commit.
    int                    pre_drag_last_selected = -1;
    // Index of the marker that was clicked to start the drag. Used to track
    // the playhead during motion so the audio cursor follows the grabbed
    // marker as it moves.
    int                    hit_marker           = -1;
    // Which list this drag operates on. The motion / commit
    // handlers dispatch on this so a drag started in phase reset view
    // mutates the phase reset list.
    char                   drag_mode = 'W';
};

// Drag-time position overlay. Paint sites consult this when a marker
// index appears in `indices` to read the proposed new time from
// `times` rather than the live store's time_seconds. The two spans
// alias DragState's `dragging_markers` and `moveable_times` (parallel
// vectors, sorted ascending). Empty overlay (default-constructed) is
// equivalent to "no drag active" and falls back to the live store.
struct DragOverlay {
    const std::vector<int>*    indices = nullptr;
    const std::vector<double>* times   = nullptr;

    // Returns the overlay time for marker `marker_idx`, or
    // `fallback_time_seconds` when the index is not in the overlay.
    // Caller passes the live store's time_seconds as the fallback.
    double effective_time(int marker_idx,
                          double fallback_time_seconds) const {
        if (!indices || !times) return fallback_time_seconds;
        for (size_t k = 0; k < indices->size(); ++k) {
            if ((*indices)[k] == marker_idx) return (*times)[k];
        }
        return fallback_time_seconds;
    }
};

// Two-stack undo/redo history for marker mutations. Entries are full
// snapshots of the marker vector plus an op-kind tag and pre-op selection
// hint — small enough to store directly rather than diff. Both stacks are
// capped at kCap; the oldest undo entry is evicted when the cap is exceeded.
//
// The saved reference is a signed distance from the current position to the
// snapshot corresponding to what's on disk. Positive = ahead on the redo
// stack; negative = behind on the undo stack; 0 = at current. `saved_valid`
// tracks whether the saved reference is still reachable: a new mutation
// that clears the redo stack while saved was ahead orphans it (saved_valid
// becomes false), and dirty stays true until the next save rebinds it.
struct UndoHistory {
    static constexpr size_t kCap = 500;
    std::vector<UndoEntry> undo_stack;
    std::vector<UndoEntry> redo_stack;
    int  saved_distance = 0;
    bool saved_valid    = true;

    bool is_dirty() const {
        return !(saved_valid && saved_distance == 0);
    }

    // Push the pre-mutation entry. Clears the redo stack. If the saved
    // reference was on the redo stack, it's orphaned (saved_valid = false).
    // If pushing would evict the bottom of the undo stack and the saved
    // reference pointed at or below the evicted entry, it's pinned to the
    // new bottom — that's the least-surprising user-facing behavior even
    // though it's not strictly correct.
    void push(UndoEntry entry) {
        if (saved_valid && saved_distance > 0) saved_valid = false;
        redo_stack.clear();
        if (saved_valid) saved_distance -= 1;
        undo_stack.push_back(std::move(entry));
        if (undo_stack.size() > kCap) {
            undo_stack.erase(undo_stack.begin());
            if (saved_valid &&
                saved_distance < -static_cast<int>(undo_stack.size())) {
                saved_distance = -static_cast<int>(undo_stack.size());
            }
        }
    }

    void mark_saved() {
        saved_distance = 0;
        saved_valid    = true;
    }

    void reset() {
        undo_stack.clear();
        redo_stack.clear();
        saved_distance = 0;
        saved_valid    = true;
    }
};

// State for the plain/Shift left-button playhead-drag gesture. The drag
// only positions the playhead (with a 3px snap-to-marker magnet); selection
// is set at press time and never mutated by motion. The gesture ends on
// release (or on Escape, which ends at current position).
//
// Mouse-side click-keep-alive: a waveform-area press during playback
// reseeks audio to the clicked sample (Reaper-style) instead of stopping.
// Motion does NOT reseek — the scanner advances independently from the
// click position via the predictor while the cursor moves freely with
// the drag (split-playhead model).
struct PlayheadDragState {
    bool active                    = false;
    // Marker index the press landed on, or -1 if pressed on empty space;
    // release uses it to suppress the snap-action when no actual drag
    // occurred.
    int  press_marker_idx          = -1;
    // Active-domain playhead position at the previous motion event (the
    // press position until the first motion). Left edge of the
    // selection sweep interval: the Shift branch of the motion handler
    // adds every marker the playhead PASSED between events, not just
    // the one under the pointer at event time — point-sampling skipped
    // markers at fast pointer speeds. -1 = unseeded (sweep disabled
    // until a begin site seeds it).
    int64_t last_swept_sample      = -1;
};

// F2.1: mouse drag-to-select inside the active text editor. Only one
// editor is active at a time, so the active editor (and thus its text
// geometry) is discoverable from the per-editor is_active checks; a single
// armed flag is enough. Set on a press that lands on the active editor's
// text region; cleared on release, on a lost button mid-drag, and on file
// load (the motion / release handlers also self-heal if the backing editor
// closes out from under an in-flight drag).
struct EditorTextDragState {
    bool active = false;
};

// Which selection group the most recent selecting gesture
// targeted. Group-acting gestures (Delete, Ctrl+drag) act on exactly one
// group, chosen by this tag. Set to Trim when a click/gesture lands on a
// trim boundary, Markers when it lands on a marker.
enum class LastSelGroup { Markers, Trim };

// Ctrl+drag of a trim boundary stem. Parallel to DragState but
// writes the project trim directly (no overlay): motion mutates the
// dragged bound's seconds live, release pushes a single SettingsSnapshot
// undo. `pre` is captured at drag-begin so release can push the inverse.
// Session-only.
struct TrimDragState {
    bool active   = false;
    bool is_begin = false;   // which bound the cursor is dragging
    bool moved    = false;   // whether motion actually changed the bound
    double orig_seconds       = 0.0;  // dragged bound's pre-drag value (Escape-restore)
    // Press position in source-domain seconds, captured at drag-begin.
    // Motion applies the cursor's displacement from here (anchor-relative),
    // matching warp-marker drag — so the bound tracks the grab point with no
    // initial snap. See DragState::anchor_mouse_time_seconds.
    double anchor_seconds     = 0.0;
    SettingsSnapshot pre;    // pre-drag settings snapshot for the undo
};

// Hover popup state. A popup-eligible warp marker (pass marker or
// label_ref) under the cursor shows a bottom-strip readout of its
// resolved tempo. The motion and viewport-recompute handlers set
// `marker_index` and `cached_text` and derive `visible` the instant the
// cursor lands on an eligible rect (no dwell); mutation paths / dismiss
// conditions clear the whole struct.
//
// `cached_text` is the readout's content string, computed once per
// rect-entry (def lookup, frame-distance ratio) and read unchanged by
// the paint path, so paint never repeats the math. `visible` is simply
// whether `cached_text` is non-empty. Discarded on rect-exit; there is
// no asynchronous work to cancel — a transition recomputes the text and
// the prior result is dropped.
struct HoverPopupState {
    int         marker_index = -1;
    bool        visible      = false;
    std::string cached_text;
};

// What action triggered the modal prompt; the activate-response dispatch
// switches on this together with the response key. Save/Discard/Cancel
// applies to the unsaved-work prompts (CLOSE_WINDOW, REVERT_TO_BLANK).
enum class DialogTrigger {
    CLOSE_WINDOW,
    REVERT_TO_BLANK,
    PASTE_CONFIRM,
};

// In-window modal prompt state. When `active` is true, the bottom strip
// overlays the prompt's text and response options in place of the
// timestamp / tab letter / dirty indicator / render-view filename.
// Input is owned by the prompt: only the response keys (and Esc, which
// activates the rightmost response) do anything; everything else is
// swallowed. `response_keys` holds lowercase letters; the activator
// lowercases incoming keypresses before comparing.
struct PromptState {
    bool                     active = false;
    std::string              text;
    std::vector<char>        response_keys;     // lowercase
    std::vector<std::string> response_labels;   // e.g. "[S]ave"
    DialogTrigger            trigger = DialogTrigger::CLOSE_WINDOW;
};

// Navigational bookmark. Holds a snapshot of the three fields that define
// what the user sees and where playback would start. Not in the undo domain.
//
// Each tab also carries per-mode selection slots so switching
// tabs (Ctrl+Tab) and switching modes (`p`) both restore the right
// selection set for the destination cell. The active selection lives in
// AppState; these slots are the persistent snapshots.
//
// The same struct is reused by RenderViewEntry::state to carry
// per-render persisted view-state across render-view exit/enter and
// batch-nav. Render-view entries leave the viewport/zoom/playhead fields
// at default (those still flow through the live AppState fields
// and the .rendersettings sidecar).
struct ViewState {
    int64_t viewport_start_sample      = 0;
    int     zoom_level                 = 0;
    int64_t playhead_cursor_sample     = 0;

    std::set<int> warp_selected;
    int           warp_last_selected      = -1;
    std::set<int> phase_reset_selected;
    int           phase_reset_last_selected = -1;

    // Per-tab read-only lock. Toggled by bare `o`. While true, the active
    // tab admits a subset of keys (navigation, playback, view-switch) and
    // its mouse handlers block authoring gestures (drop, drag, label
    // edit). Persisted as tab_a_read_only / tab_b_read_only in .settings.
    bool   read_only          = false;
};

struct AppState {
    int     width                 = 1400;
    int     height                = 800;
    bool    loading               = false;
    // Still written by the file-loader progress callback, but no longer read:
    // the determinate load bar it fed was replaced by a static status message.
    // Left in place rather than chased across its several write sites.
    float   load_progress         = 0.0f;

    int64_t playhead_cursor_sample = 0;
    int     zoom_level             = 0;
    int64_t viewport_start_sample  = 0;
    bool    follow_mode            = true;

    // True when a cursor-moving interaction has overridden follow mode
    // for the current playback session. Cleared when playback ends
    // (via restore_playhead_to_lsp or stop_playback_if_playing); never
    // set or cleared except by these paths.
    bool    follow_overridden_for_session = false;

    // Split-playhead state. The cursor (above, mirrored from the active
    // ViewState) is the user's stationary reference frame; the scanner
    // is the engine's playback position. They coincide when nothing is
    // playing — every stop path restores playhead_scanner_sample ==
    // playhead_cursor_sample before paint returns. The cursor is
    // per-tab; the scanner is session-only and not persisted.
    // `playback_speed` is authoritative on the main thread and pushed
    // to the playback engine on every change.
    int64_t playhead_scanner_sample = 0;
    bool    playhead_scanner_active = false;
    float   playback_speed          = 1.0f;

    // One-shot stash of the scanner's last painted pixel-x under the
    // OLD viewport, set by viewport-mutating operations during
    // playback. The next pre-paint reads this in place of computing
    // scanner_pixel_x against the new viewport, then clears it.
    // Negative sentinel = no stash.
    double playhead_scanner_old_px_stash = -1.0;

    // Companion files discovered alongside the loaded audio.
    std::string warpmarkers_path;
    std::string settings_path;
    // Sibling `.phaseresetmarkers` path. Computed at file load. Empty when
    // no audio is loaded.
    std::string phase_reset_markers_path;

    // Absolute or relative path of the currently loaded audio file. Used by
    // the render hotkeys (Ctrl+Alt+R / Ctrl+E / Ctrl+Alt+E / Ctrl+Alt+I) and
    // the render pipeline to compute output paths. Empty when no file is
    // loaded (blank state).
    std::string source_audio_path;

    // Parsed warp markers for the currently loaded audio. Empty on load
    // failure or before the first audio load.
    GuiWarpMarkers  warpmarkers;

    // Parsed phase reset markers. Authored by the GUI but not
    // yet consumed by the render pipeline.
    GuiPhaseResetMarkers phase_reset_markers;

    // Multi-selection set + focus. `last_selected_marker` is either -1 or
    // a member of `selected_markers`; keyed operations (Tab cycling, `j`)
    // anchor on it.
    //
    // This pair holds the *active* selection — i.e. for the
    // current tab + current `active_markers_view`. The persistent per-tab per-mode
    // slots live on ViewState and are saved/restored on mode/tab transitions.
    std::set<int> selected_markers;
    int           last_selected_marker = -1;

    // Active markers view: 'W' = warp markers, 'P' = phase reset markers.
    // Toggled by `p`. Determines which marker collection
    // is visible / edited / hit-tested and which color set is used for
    // the playhead and selected indicators.
    char active_markers_view = 'W';

    // Active audio view: 'S' = source (the authored timeline), 'T' =
    // target (the engine's deformed-output timeline). Orthogonal to
    // active_markers_view ('W'/'P'): `t` toggles S/T, `p` toggles
    // W/P. While 'T', app.viewport_start_sample / playhead_cursor_sample /
    // zoom_level carry target-frame values; the live fields'
    // interpretation flips on toggle. Render-view leaves this
    // unchanged — `t` is dropped in render-view. Target view was
    // formerly read-only; the target-render audio subsystem makes target
    // view playable with live engine output.
    char active_audio_view = 'S';

    // Memoized target-view frame_map (see timemap.h). Mutable: consulted and
    // refreshed from const hit-test paths.
    mutable TargetTimemapCache target_timemap_cache;

    // Ctrl+drag state. Not reset across file loads — explicitly cleared
    // there and on button release / Escape.
    DragState     drag;

    // Playhead drag state (plain / Shift left-button). Cleared on button
    // release, Escape, and file load.
    PlayheadDragState playhead_drag;

    // Ctrl+drag of a trim boundary stem. Cleared on button
    // release, Escape, and file load.
    TrimDragState trim_drag;

    // Mouse drag-to-select inside the active text editor. Cleared on
    // button release, on a lost button mid-drag, and on file load.
    EditorTextDragState editor_text_drag;

    // Which selection group the last selecting gesture targeted.
    // Drives Delete / Ctrl+drag group dispatch. Session-only.
    LastSelGroup last_sel_group = LastSelGroup::Markers;

    // Hover-popup state. See HoverPopupState above.
    HoverPopupState   hover_popup;

    // Cursor screen position from the last on_motion
    // event. Used by recompute_hover_at_cursor() to re-evaluate hover
    // after a viewport mutation (when the cursor is stationary but rects
    // have shifted). -1 means "no motion seen yet".
    int               last_mouse_x = -1;
    int               last_mouse_y = -1;

    // Undo/redo history for marker mutations. `dirty` below becomes a
    // derived signal: dirty = history.is_dirty(). Save/load reshape the
    // saved reference rather than touching dirty directly.
    UndoHistory history;

    // True if any authoring-class file has changes since the last save.
    // app.dirty = warp_dirty || phase_reset_dirty || settings_dirty,
    // recomputed after every push/undo/redo by walking saved_distance
    // against each entry's op_mode. Drives both the unsaved-work dialog
    // and the dirty-dot.
    //
    // Authoring-class settings (trim, engine, scale, N, limiter, title,
    // audio_input, plus any free-form key typed
    // into the settings editor that isn't a view-state key) participate
    // in dirty via settings_dirty. View-state keys (viewport, zoom,
    // playhead, follow_mode, active_markers_view, playback_speed) do NOT
    // participate: they are silently persisted on Ctrl+S and silently
    // discarded on Ctrl+W without save.
    bool        warp_dirty           = false;
    bool        phase_reset_dirty    = false;
    bool        settings_dirty       = false;
    bool        dirty                = false;

    // True until the first save in this session; used to log a one-time
    // notice if the on-disk file had content the canonical form drops.
    bool        first_save_pending   = true;

    // If a drop arrives mid-load, the path is stashed here and processed
    // after the active load returns. Empty means "no pending drop."
    std::string pending_drop_path;

    // For redraw-time diagnostics (acceptance criterion 15).
    std::chrono::steady_clock::time_point stats_last_report =
        std::chrono::steady_clock::now();
    double  stats_max_redraw_ms = 0.0;
    int     stats_over_1ms_count = 0;

    // Timestamp of the most recent user input event (key/button/motion).
    // Read by the perf-instrumentation path to compute event-to-paint
    // latency (e2e). Default-constructed (epoch zero) means "no input yet."
    std::chrono::steady_clock::time_point last_input_event_time{};

    // Identity counter for the currently loaded audio. Bumped on every
    // successful file load. Used by the waveform cache as part of its
    // invalidation fingerprint so a file swap forces a re-render.
    long long audio_generation = 0;

    // Target-view live audio buffer. The render pipeline appends
    // synthesised samples here via RenderRequest::output_buffer when the
    // target_render.trigger() helper dispatches a render in target view.
    // On completion the playback device rebinds to this buffer via
    // GuiPlayback::rebind_buffer so playback plays the warped result. The
    // buffer is replaced wholesale per target render — no historical
    // cache. Source-view playback continues to read source.wav from the
    // GuiAudio store, not this buffer.
    std::vector<float> target_buffer;
    // Cached frame count for the populated target_buffer (i.e.
    // target_buffer.size() / channels). Set in the target render's on_done
    // callback; read by the playback rebind. Zero before the first
    // successful target render in this session.
    int64_t target_buffer_frames = 0;
    // Full-target-frame coordinate that target_buffer[0] represents.
    // With trim set: map_source_to_target(trim_begin_frame) against the
    // full-source frame_map. With trim unset: 0 (the target buffer is
    // the full-song render starting at target frame 0). Captured in
    // on_render_done's Success branch when target_buffer_frames
    // has just been set and the frame_map is still derivable from the
    // live AppState. Read by toggle_playback (to translate the
    // playhead's target-domain coordinate into a target-buffer
    // frame index for playback.play()) and by the pre-paint hook (to
    // translate playback.cursor() back into target-domain when writing
    // to app.playhead_cursor_sample). Cleared at the same lifecycle points
    // that clear target_buffer: failure/cancel, file load, revert,
    // rebind_to_source.
    int64_t target_buffer_start_frame = 0;

    // Active tab view: 'A' or 'B'. Selects which ViewState snapshot
    // (tab_a or tab_b) is mirrored into the live AppState fields.
    // Toggled by Ctrl+Tab; persisted to .settings. tab_a and tab_b
    // each hold an independent viewport/zoom/playhead/selection
    // tuple, but share the same warpmarkers, phase_reset_markers,
    // engine_settings, and project-level trim.
    ViewState tab_a;
    ViewState tab_b;
    char active_tab_view = 'A';

    // Typed engine settings. The live authoring store: settings editor
    // commits, .settings file load, and the BPM-sweep / Ctrl+Alt+C scale
    // commit paths all mutate fields of this struct directly. Carried
    // by RenderRequest at dispatch; serialized to .settings on Ctrl+S.
    // Default-constructed before any source load.
    EngineSettings engine_settings;

    // Project-level trim region. One trim per project (shared across both
    // A/B tabs), so it lives on AppState rather than per-tab ViewState.
    // Trim edits participate in the undo domain as part of the
    // SettingsSnapshot. Seconds for sample-rate stability (consistent with
    // marker times). has_trim_* distinguishes "no trim set" from "trim set
    // to 0.0" — both round-trip through .settings. Reset on file load /
    // revert.
    double trim_begin_seconds = 0.0;
    double trim_end_seconds   = 0.0;
    bool   has_trim_begin     = false;
    bool   has_trim_end       = false;

    // Transient selection of the trim boundary stems. A separate
    // selection channel from the marker sets (selected_markers /
    // phase_reset_selected) — the two groups are orthogonal and can be
    // co-selected. Not persisted to .settings; defaults false and resets on
    // file load. Which group a group-acting gesture (Delete, Ctrl+drag)
    // targets is decided by last_sel_group.
    bool   trim_begin_selected = false;
    bool   trim_end_selected   = false;

    // Bottom-strip command prompt. Active only when a close / revert /
    // re-detect gesture fires while a confirmation is required. Originally
    // a centered modal dialog; the same modal semantics now live in the
    // bottom strip.
    PromptState prompt;

    // Top-flag text editor. Active only when editing a flag rect
    // in warp view. The editor owns the keyboard while active and
    // overlays a custom rect + cursor on top of render_flags.
    text_editor::State top_flag_editor;
    // Last-painted cursor visibility, so the tick can detect a flip and
    // invalidate the top strip without redundant repaints.
    bool top_flag_editor_blink_last = false;

    // Settings-prompt editor. Opens on `;`, accepts a single `key=value`
    // line, writes to engine_settings on commit. Lives in the bottom
    // strip; separate from top_flag_editor so the two paint regions stay
    // independent (the in-practice mutual exclusion comes from the flag
    // editor swallowing all keys while active).
    text_editor::State settings_editor;
    bool settings_editor_blink_last = false;

    // Tick backstop bookkeeping: last live-domain total observed by the
    // on_tick clamp (see main.cpp). 0 = not yet observed.
    int64_t last_tick_live_total = 0;

    // Render-queue state. `queue_running` is true only inside the
    // Ctrl+Alt+R queue walker. The Esc handler checks it to scope the
    // cancel binding away from normal interaction. `queue_cancel_requested`
    // is set by Esc during a queue run and read between entries.
    bool queue_running           = false;
    bool queue_cancel_requested  = false;

    // Non-interactive bottom-strip status text. Set by long-running
    // operations (currently only the multi-render queue runner) so the
    // user has visual feedback while no other UI is updating. Empty
    // means "no status — render the timestamp normally." Mutually
    // exclusive with prompt.active in practice (the queue runner can't
    // fire while a prompt is up).
    std::string queue_progress_text;

    // Transient one-line status message shown in the bottom strip after
    // the dirty dot, alongside the S/T·W/P·A/B indicators. Set by a
    // command that wants to report a non-fatal outcome (e.g. phase-reset
    // state-paste divergence); cleared on the next keyboard press in
    // on_key. Empty = nothing to show. General-purpose: not specific to
    // any one command, so future commands can reuse it.
    std::string transient_status_message;

    // In-memory queue of pending renders. Ctrl+E pushes a
    // snapshot of the current authoring state onto the back of this list;
    // Ctrl+Alt+E consumes it, materializing one batch folder per execution
    // with one rendered output per queued entry. The list is session-only:
    // discarded on app close, never written to disk between sessions.
    // Settings are not snapshotted per-entry — all entries render against
    // the GUI's live `engine_settings` at execution time, mirroring the
    // single-render convention.
    struct QueuedRender {
        std::string                source_audio_path;
        std::vector<GuiWarpMarker>     markers;
        std::vector<GuiPhaseResetMarker>  phase_resets;
    };
    std::vector<QueuedRender> queued_renders;

    // Phase reset propagate (W-mode Ctrl+P / Ctrl+Alt+P). Single-slot
    // session-only clipboard cleared on app exit. `pending_paste_anchor`
    // is the destination warp-marker index captured when the paste
    // confirmation prompt opens; consumed by the prompt response.
    PhaseResetClipboard phase_reset_clipboard;
    int                pending_paste_anchor = -1;

    // Iteration mode. Toggled by plain `i` in warp view (no-op in
    // phase reset view). Session-only; survives view-switches but is lost
    // on app close. When true, hover popups are suppressed and a
    // persistent iteration popup is rendered above every owning
    // marker's flag rect.
    bool iteration_mode_enabled = false;

    // BPM mode. Toggled by plain `m` in warp view. Mutually
    // exclusive with iteration_mode_enabled (toggling one ON forces the
    // other OFF). Session-only. The BPM owner is identified at runtime
    // by walking markers for bpm_owner=true; at most one marker holds
    // the flag at a time, maintained as an invariant by the toggle.
    bool bpm_mode_enabled = false;

    // Render analysis mode. Plain `r` toggles between source-view
    // (authoring) and render-view (read-only auditioning of rendered
    // outputs from <source_parent>/renders/). All authoring state above
    // is preserved untouched while render-view is active; this struct
    // holds the parallel context that drives render-view's display.
    bool render_view_enabled = false;
    // Path to the last-displayed render's .wav, persisted across toggle
    // off/on cycles within a session. Empty before the first entry; reset
    // to whatever path was active when the previous toggle-off fired.
    std::string last_render_view_path;
    // One entry in the flat list of valid renders enumerated on toggle-in.
    struct RenderViewEntry {
        std::filesystem::path batch_folder;     // <source_parent>/renders/<i>_<tag>
        std::string           basename;         // e.g. "01" (no extension)
        std::filesystem::path wav_path;         // batch_folder / (basename + ".wav")

        // Per-entry persisted view-state across render-view
        // exit/enter and batch-nav. Selection + sub-mode are valid only
        // when the wav still has the same (size, mtime) as when stashed;
        // mismatch on reload drops them silently. The viewport/zoom/
        // playhead fields on `state` are unused — render-view's
        // viewport state continues to flow through the live AppState
        // fields and the .rendersettings sidecar.
        ViewState state;

        // Stat-tuple key for selection validity. Captured when stashed,
        // compared against the current file's stat on re-load.
        uintmax_t     persisted_size             = 0;
        int64_t       persisted_mtime            = 0;
    };
    std::vector<RenderViewEntry> render_view_list;
    int                          render_view_index = -1;     // -1 = unset
    // The current render's loaded markers + phase resets, parsed from
    // sibling `<basename>.renderwarpmarkers` /
    // `<basename>.renderphaseresetmarkers`.
    std::vector<GuiWarpMarker>       render_view_markers;
    std::vector<GuiPhaseResetMarker>    render_view_phase_resets;
    // Source-frame mapping of the current render: F_begin..F_end (source
    // sample-rate frames) is what the render's full audio covers. When the
    // render's warpmarkers carry no `b=` flag, F_begin is 0; when it carries
    // no `e=` flag, F_end is the source's total_frames. Used by the render
    // -view waveform mapping and the timestamp readout.
    int64_t                      render_view_src_F_begin = 0;
    int64_t                      render_view_src_F_end   = 0;
    // Source audio's sample rate / total frames at the time render-view
    // was entered. Cached so timestamp computation and trim resolution
    // don't have to peek at the swapped-out source GuiAudio.
    int                          render_view_src_sr      = 0;
    int64_t                      render_view_src_total   = 0;
};

// Geometry helpers — definitions live at file scope in main.cpp. Declared
// here so viewport.cpp can call them.
int     strip_h(const AppState& a);
GuiRect waveform_area(const AppState& a);
GuiRect top_strip_area(const AppState& a);
GuiRect bottom_strip_area(const AppState& a);
GuiRect top_upper_row_area(const AppState& a);
GuiRect top_lower_row_area(const AppState& a);
GuiRect bottom_upper_row_area(const AppState& a);
GuiRect bottom_lower_row_area(const AppState& a);
int64_t samples_visible(const AppState& a, const GuiAudio& audio);
double  current_samples_per_pixel(const AppState& a, const GuiAudio& audio);

// Drop-marker dedup pre-check shared by GuiWarpMarkersOps::drop_marker and
// GuiPhaseResetMarkersOps::drop_phase_reset_at_position. Iterates `markers`
// and rejects the drop if any existing marker sits within `eps` seconds of
// `time_seconds`, logging a `<label> marker already exists near …` line to
// stderr. The marker container is read only through `m.time_seconds`, so
// any marker type exposing that field works.
template <typename MarkerVec>
bool reject_if_marker_within_eps(
    const MarkerVec& markers,
    double time_seconds,
    double eps,
    const char* label) {
    for (const auto& m : markers) {
        if (std::abs(m.time_seconds - time_seconds) < eps) {
            std::fprintf(stderr,
                "warptempo_gui: %s marker already exists near %.3fs\n",
                label, time_seconds);
            return true;
        }
    }
    return false;
}

// Shared neighbor-walk core for compute_selection_delta_bounds (warp) and
// compute_phase_reset_delta_bounds (phase reset). For each selected index,
// walks past contiguous selected neighbors on each side to find the
// nearest non-selected neighbor, intersecting per-marker (delta_min,
// delta_max) bounds. When a selected marker has no neighbor on a side it
// clamps to [eps, total_duration - eps]. The marker container is read
// only through `markers[idx].time_seconds`, so any marker type exposing
// that field works.
//
// Preconditions enforced here: every idx in `selected` is in range of
// `markers` (returns {0.0, 0.0} on violation, which the caller treats as
// the same no-op shape it returns for its own precondition failures).
// Caller-side preconditions (warp's frame-zero pin; non-empty selection;
// positive sample rate) are not the helper's responsibility.
template <typename MarkerVec>
std::pair<double, double> compute_neighbor_walk_bounds(
    const MarkerVec& markers,
    const std::set<int>& selected,
    double eps,
    double total_duration) {
    double d_min = -std::numeric_limits<double>::infinity();
    double d_max =  std::numeric_limits<double>::infinity();
    const int n = static_cast<int>(markers.size());
    for (int idx : selected) {
        if (idx < 0 || idx >= n) return {0.0, 0.0};
        const double orig_t = markers[idx].time_seconds;
        int prev = idx - 1;
        while (prev >= 0 && selected.count(prev)) --prev;
        if (prev >= 0) {
            const double lb = (markers[prev].time_seconds + eps) - orig_t;
            if (lb > d_min) d_min = lb;
        } else {
            const double lb = eps - orig_t;
            if (lb > d_min) d_min = lb;
        }
        int next = idx + 1;
        while (next < n && selected.count(next)) ++next;
        if (next < n) {
            const double ub = (markers[next].time_seconds - eps) - orig_t;
            if (ub < d_max) d_max = ub;
        } else {
            const double ub = (total_duration - eps) - orig_t;
            if (ub < d_max) d_max = ub;
        }
    }
    return {d_min, d_max};
}

void    clamp_viewport_start(AppState& a, const GuiAudio& audio);
// Returns the pixel column (offset from waveform_area.x) for the cursor.
// The (app, audio) form reads a.viewport_start_sample — the live/logical
// viewport. Use it from invalidation math, hit-testing, and pre-paint
// updates: anywhere that wants "where is the playhead RIGHT NOW".
//
// The (app, audio, vp_start, spp) form takes the viewport AND its
// samples-per-pixel explicitly. Use it from on_redraw to align the live
// cursor/scanner paint with the cached layers (waveform, marker stems,
// flags) — those layers render against wf_cache.fp_vp_start at the
// displayed spp (derivable as (fp_vp_end - fp_vp_start) / fp_area_w) for
// the 1-2 paint frames while the worker rebuilds against a viewport
// change. Threading BOTH parameters through here keeps cursor/scanner
// and surrounding markers in lockstep during that window; passing
// fp_vp_start alone but reading the live spp would mix frames of
// reference and visibly displace the cursor for one frame after each
// zoom gesture. Do NOT reroute invalidation through the displayed-
// viewport form: invalidation already widens to the full waveform-area
// span at viewport-change gestures, and the narrow-damage path (arrow
// step, drag, predictor advance at fixed viewport) needs the live
// position because live == displayed in steady state.
double  playhead_pixel_x(const AppState& a, const GuiAudio& audio);
double  playhead_pixel_x(const AppState& a, const GuiAudio& audio,
                         int64_t vp_start, double spp);
// Returns the pixel column (offset from waveform_area.x) for the scanner.
// Equal to playhead_pixel_x when playhead_scanner_active is false (by the
// invariant: scanner sample tracks cursor sample when inactive). The
// (app, audio, vp_start, spp) overload follows the same live-vs-displayed
// split documented on playhead_pixel_x above.
double  scanner_pixel_x(const AppState& a, const GuiAudio& audio);
double  scanner_pixel_x(const AppState& a, const GuiAudio& audio,
                        int64_t vp_start, double spp);
// Active-domain total frame count. Source view returns audio.total_frames();
// target view returns the deformed total derived from the frame_map cache
// (the forward-translated source length). Used by every viewport helper that needs the
// "length of the timeline currently being viewed" — clamp, fit-file zoom,
// and the numeric-level cap. Declared here so any TU touching the
// viewport math can reach it.
int64_t live_total_frames(const AppState& a, const GuiAudio& audio);
int     max_valid_numeric_level(int waveform_width_px,
                                int64_t total_frames,
                                int sample_rate);
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, int sample_rate, long long total_frames);
GuiRect timestamp_invalidate_rect(const AppState& a);
GuiRect playhead_invalidate_rect(const GuiRect& area, double px_x);
bool    rects_intersect(GuiRect a, GuiRect b);
GuiRect union_rect(GuiRect a, GuiRect b);

// Free-function form of GuiActiveViews::active_view_state() restricted to
// source-view (the A/B tab pair). The renderer / the b/e/u
// handlers don't have access to GuiActiveViews but need to reach the active
// tab's view-state from an AppState reference alone. Source-view-only:
// render-view callers must keep using GuiActiveViews::active_view_state(),
// which handles the render-entry case.
inline ViewState& active_view_state(AppState& a) {
    return (a.active_tab_view == 'B') ? a.tab_b : a.tab_a;
}
inline const ViewState& active_view_state(const AppState& a) {
    return (a.active_tab_view == 'B') ? a.tab_b : a.tab_a;
}

// Snapshot the authoring-class settings from `app` (engine_settings +
// project-level trim pair). Called by Undo's push helpers at push time
// so every entry carries-everywhere; also called by do_undo / do_redo
// when constructing the inverse entry. Body in app_state.cpp.
SettingsSnapshot capture_current_settings(const AppState& app);

// True iff the bottom strip is showing a modal overlay (prompt,
// queue progress, settings editor) rather than the regular flowed
// content. Consumed by the paint-time branch in on_redraw to pick
// the right strip content. Pure read against AppState.
// (Name is mildly stale — "wide" referred to the wide-vs-narrow
// invalidation rect, which is gone. Rename rides its own commit.)
bool bottom_strip_wide(const AppState& app);

// Promoted from lambdas in main(). Mode-aware hit-tests against
// the visible marker / flag / popup geometry. Bodies live in app_state.cpp
// and pull in cairo + paint_handler.h for the popup-rect math; the
// signatures stay free of cairo so the header keeps a clean include list.
//
// hit_test_marker_line: scan the active list (render-view markers in
// render-view; phase resets in 'P' mode; warp markers otherwise) and return
// the index whose pixel column is within kMarkerHitHalfPx of `mouse_x`,
// or -1 if no marker line is within reach.
int hit_test_marker_line(const AppState& app, const GuiAudio& audio,
                         int mouse_x);

// hit_test_flag: scan the active flag-pack rects in the top strip and
// return the marker index under (mouse_x, mouse_y), or -1. Returns -1
// in render-view's phase reset sub-view (no flag rects there).
int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y);

// Which trim boundary, if any, a waveform-area click lands on.
enum class TrimHit { None, Begin, End };

// hit_test_trim_boundary: return which set trim boundary's painted
// column is within kMarkerHitHalfPx of `mouse_x`, or None. Only set
// bounds (has_trim_begin / has_trim_end) are testable. Walks the same
// frame_map as hit_test_marker_line in target view so the hit lands on the
// visually-drawn stem. Trim is project-level, and applies in both 'W'
// and 'P' views. When both bounds are within reach, the nearer wins.
TrimHit hit_test_trim_boundary(const AppState& app, const GuiAudio& audio,
                               int mouse_x);

// hit_test_trim_chip: like hit_test_trim_boundary, but tests the press
// against each set bound's painted CHIP RECT in the upper top row rather
// than the stem column. The chip glyph ("b"/"e") is drawn hl_pad to the
// right of the bound's column, so a column-only test misses clicks on the
// visible chip. The rect mirrors regular-flag hit geometry
// (flag_chip_rect, the shared chip-rect helper): x = round(text_left),
// w = round(glyph_advance + 2*kFlagPadXPx), with y/h from the row metrics;
// the same rect the renderers fill, so paint and hit cannot drift.
// Tests both mouse_x and mouse_y. Used for upper-row
// presses; the stem elsewhere still routes through hit_test_trim_boundary.
TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y);

// Promoted from a lambda in main(). True iff the warp marker
// at `idx` is hover-popup-eligible — i.e. its rect doesn't already
// display a numeric tempo (pass markers and label_ref markers qualify;
// owning markers don't). Render-view honors the loaded render's
// markers regardless of the pre-toggle mode; source-view requires warp
// view with iteration mode off. Always false in phase reset view (no
// pass concept).
bool popup_eligible_marker(const AppState& app, int idx);
