#pragma once

#include "engine_settings.h"
#include "marker_store_validate.h"
#include "render_pipeline.h"
#include "render.h"
#include "settings_file.h"
#include "text_editor.h"
#include "phase_reset_clipboard.h"
#include "phaseresetmarkers.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"

#include <algorithm>
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

// Zoom level numbering: the range constants (kFitFileLevel,
// kMinNumericLevel, kMaxNumericLevel) live in settings_file.h — the
// persisted zoom vocabulary the whole-settings schema enforces in both
// products — and app_state.h re-exports them through the include above.
//
// Bare-digit keys are unbound for zoom: only `0` toggles between fit-file
// and the snap level, and `C` jumps to snap zoom centered on the playhead.
// Smaller numeric level = less file per window = more zoomed in; level 0 =
// fit-file (most file possible). kMinNumericLevel is the deepest level
// continuous manual zoom-in can reach (1.2 s); kSnapZoomLevel is the level
// every snap/toggle gesture lands on (2.4 s, one step shallower).
constexpr int kSnapZoomLevel    = 2;   // 2.4 s — snap zoom; manual
                                       // zoom-in can go one step deeper to
                                       // kMinNumericLevel (1.2 s)
// Size of the ms-per-pixel table in main.cpp: one sentinel slot at index 0
// plus the numeric levels.
constexpr int kZoomTableSize    = kMaxNumericLevel + 1;

// Viewport lead/overlap fraction, expressed as a divisor of the visible
// span. Follow mode keeps this much of the window as lead context when it
// re-anchors; paged scroll (PageUp/PageDown) retains the same fraction as
// overlap so the two behaviors stay visually consistent. One source of
// truth — do not inline the divisor at either site.
constexpr int64_t kViewportLeadDivisor = 10;

// Ctrl+wheel end-move step as a divisor of samples_visible.
constexpr int64_t kTrimEndWheelDivisor = 10;

// Hoisted from main.cpp's anonymous namespace so the hit_test_*
// free functions (in app_state.cpp) and the GuiInputHandler mouse handler
// (in input_handler.cpp) can reach them. Hit-test half-width only:
// clicking/hovering tolerance for stems, flags, and trim bounds. It is
// NOT a spacing gap — markers may sit arbitrarily close, overlap
// exactly, and cross during gestures; ordering degeneracy is refused at
// the render boundary, not at authoring time.
constexpr int kMarkerHitHalfPx    = 4;

// Wholesale snapshot of the undo-tracked settings. Holds the typed
// EngineSettings captured at undo-push time and restored on undo/redo.
struct SettingsSnapshot {
    EngineSettings engine_settings;
};

// One entry on either stack. Carries the pre-mutation marker snapshot plus
// a pre-op selection hint (so Undo-of-Destroy / Undo-of-Move can restore
// a sensible selection anchor).
//
// Every entry also carries the pre-mutation phase reset snapshot and the mode
// the operation was performed in. Both lists are always restored on undo/redo
// so the inverse is symmetric regardless of which list the op actually
// touched. `op_mode` lets undo flip the active mode; `tab` lets undo switch
// the active tab — both are context tags that restore the original authoring
// view as visual feedback for what's being undone.
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
    int                       hint_last_selected   = -1;
};

// Ctrl+drag state. `active` gates motion handling; the rest captures the
// pre-drag snapshot so Escape can restore positions and clamps can be
// evaluated without re-scanning the marker list on every motion event.
//
// `delta_min` / `delta_max` is a single scalar range for the uniformly-
// applied delta: the intersection of each dragged marker's absolute
// bounds (zero and the column's EOF wall) plus the grabbed marker's
// viewport clamp. Neighbors do not bound a drag — markers may cross
// freely, and commit reorders the store. Trim is purely cosmetic and
// does not constrain edits.
struct DragState {
    bool                active = false;
    std::vector<int>    dragging_markers;   // sorted ascending
    // Pre-drag positions, parallel to dragging_markers. At-rest copies of
    // the stores' authored int64 frames.
    std::vector<int64_t> original_times;
    // Proposed new positions during motion (source-frame doubles — mid-
    // gesture positions are free and fractional), parallel
    // to dragging_markers. Written by apply_drag_motion as
    // original_times[k] + delta; consumed by paint via DragOverlay so the
    // live marker store stays untouched until commit. Seeded from
    // original_times at begin_drag; commit converts back to authored
    // frames through the pixel-anchoring snap.
    std::vector<double> moveable_times;
    // Press position in source-frame doubles; the motion delta
    // (mouse_frame - anchor) therefore lives in source frames.
    double              anchor_mouse_time_frame = 0.0;
    double              delta_min = -std::numeric_limits<double>::infinity();
    double              delta_max =  std::numeric_limits<double>::infinity();
    bool                moved = false;
    // Pre-drag warp_frame_map snapshot. Captured at begin_drag via
    // build_target_view_warp_frame_map so paint can route selected-marker
    // positions and target-view waveform through a frozen coordinate
    // system for the duration of the drag. Empty when source view is
    // active at begin_drag time, or when the build failed.
    std::vector<WarpFrameMapSegment> frozen_warp_frame_map;
    // Full pre-drag marker state. Captured at button-press so commit_drag
    // can push it onto the undo stack when motion landed; discarded on
    // commit when no motion occurred (DragState is reset wholesale there).
    std::vector<GuiWarpMarker>      pre_drag_snapshot;
    std::vector<GuiPhaseResetMarker> pre_drag_phase_reset_snapshot;
    // Pre-drag last_selected for the undo hint; carried onto the entry at commit.
    int                    pre_drag_last_selected = -1;
    // Active-domain playhead position captured at begin_drag. The
    // motion handler tracks the playhead onto the grabbed marker's
    // proposed position during the drag; Esc-cancel restores this so an
    // abandoned drag leaves the playhead where it started. A normal
    // release re-syncs the playhead onto the committed column-snapped
    // marker position (commit_drag's sync_playhead_to_last_selected), so
    // this captured value serves only the Esc-cancel restore.
    int64_t                pre_drag_playhead_sample = 0;
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
// `times` rather than the live store's time_frame. The two spans
// alias DragState's `dragging_markers` and `moveable_times` — parallel
// vectors paired positionally by k. The indices are not necessarily in
// ascending order (a mid-drag store reorder remaps them in place); the
// linear scan below does not care. Empty overlay (default-constructed)
// is equivalent to "no drag active" and falls back to the live store.
struct DragOverlay {
    const std::vector<int>*    indices = nullptr;
    const std::vector<double>* times   = nullptr;

    // Returns the overlay time for marker `marker_idx`, or
    // `fallback_time_frame` when the index is not in the overlay.
    // Caller passes the live store's time_frame as the fallback.
    double effective_time(int marker_idx,
                          double fallback_time_frame) const {
        if (!indices || !times) return fallback_time_frame;
        for (size_t k = 0; k < indices->size(); ++k) {
            if ((*indices)[k] == marker_idx) return (*times)[k];
        }
        return fallback_time_frame;
    }
};

// Two-stack undo/redo history for marker mutations. Entries are full
// snapshots of the marker vector plus a pre-op selection
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

// Ctrl+drag of a trim boundary stem. Parallel to DragState but motion mutates
// the active tab's live trim mirror directly (no overlay); release triggers a
// target render when the bound moved. Trim is excluded from undo/redo.
// Session-only.
struct TrimDragState {
    bool active   = false;
    bool is_begin = false;   // which bound the cursor is dragging
    bool moved    = false;   // whether motion actually changed the bound
    // Dragged bound's pre-drag value (an at-rest copy of the store's
    // authored int64 frame); base for the drag delta.
    int64_t orig_frame      = 0;
    // Press position in source-frame doubles, captured at drag-begin.
    // Motion applies the cursor's displacement from here (anchor-relative),
    // matching warp-marker drag — so the bound tracks the grab point with no
    // initial snap. See DragState::anchor_mouse_time_frame.
    double anchor_frame     = 0.0;

    // Ctrl+Shift move-both-bounds drag: both bounds translate together by
    // the same delta in the active (on-screen) domain, preserving the gap
    // as it appears under warp. `is_begin` still records which stem was
    // grabbed (for cosmetic purposes only — both move regardless).
    bool    both                 = false;
    int64_t orig_begin_frame   = 0;
    int64_t orig_end_frame     = 0;
    int64_t anchor_active_frame  = 0;
};

// Ctrl+drag on empty waveform: continuous 1:1 grab-pan of the viewport,
// driven by pointer motion, panning by the exact per-event pixel delta.
struct ScrollDragState {
    bool   active   = false;
    // Pointer x (px) at the previous motion event, seeded at the ctrl+press.
    int    last_x   = 0;
    // Fractional sample-domain remainder carried between motion events so the
    // 1:1 pixel pan tracks exactly without drifting over a long drag.
    double accum_samples = 0.0;
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
    // The pasteable effective tempo value for the hovered marker, in the exact
    // form the flag editor accepts (base, plus "*scale" when a scale is
    // present). Computed alongside cached_text at each rect-entry and copied to
    // the clipboard by the Ctrl+C hover-copy binding while `visible`. Non-empty
    // whenever `visible` is true (an empty cached_text means no popup, so the
    // binding never fires with an unset payload).
    std::string copy_payload;
};

// What action triggered the modal prompt; the activate-response dispatch
// switches on this together with the response key. Save/Discard/Cancel
// applies to the unsaved-work prompts (CLOSE_WINDOW, REVERT_TO_BLANK).
// ERROR_NOTICE is the dismiss-only error popup: the backstop for failures
// the defect-resolution series does not model, plus the environmental and
// settings-choice refusals (see GuiPrompt::open_error_notice's caller
// list). Its text is the owner's own error string, unmodified, and its
// sole response is acknowledge/dismiss.
enum class DialogTrigger {
    CLOSE_WINDOW,
    REVERT_TO_BLANK,
    PASTE_CONFIRM,
    ERROR_NOTICE,
    // Forced-choice defect-resolution modal (the commit-time series driven
    // by GuiInputHandler::open_defect_series). Esc is SWALLOWED rather than
    // activating the rightmost response — a recorded deviation from the
    // other prompts' Esc rule — because every offered option mutates or
    // rewinds authored state and the state must not silently rest invalid.
    DEFECT_RESOLUTION,
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

// Origin of a pending defect-series validation (DefectSeriesState below).
// Commit: the commit funnel — set by the history push helpers / do_redo
// (undo.cpp) and by trim's history-less commit sites. Load: set at the end
// of a successful source load (file_loader.cpp), so file-borne defects get
// their modal walk on the first tick after the load. Consumed by
// GuiInputHandler::run_commit_validation, which opens the series with
// commit context iff the origin is Commit; a Load-origin series offers no
// [U]ndo naturally — the loader clears history, so the
// undo_stack-non-empty condition already yields none.
enum class PendingValidation { None, Commit, Load };

// Which trim-column defect the current modal shows. ClearBounds covers
// crossed-or-equal and every validate_trim_frames refusal — all sharing
// the single delete-both-bounds resolution. MapFormatConflict is the
// cross-domain map-format-with-trim conflict (trim is wav-only): its
// resolutions are [U]ndo / [R]eset (output_format back to wav, trim
// survives) / [Delete] (both bounds cleared, format survives), so the
// response handler must tell it apart from the delete-both-bounds-only
// kinds.
enum class TrimDefectKind { None, ClearBounds, MapFormatConflict };

// Defect-resolution series state (DialogTrigger::DEFECT_RESOLUTION). The
// series holds only the CURRENT defect; every resolution re-enumerates the
// stores from scratch (one undo can fix several defects at once), so a
// stale defect queue cannot exist. `defect` is meaningful only while a
// marker-defect modal is showing; a non-None `trim_defect_kind` marks a
// trim-column defect instead — trim defects have no MarkerDefect shape
// (trim is not a marker store). `commit_context` records whether the
// series was opened by a commit; the coincident-group delete narrows to
// the touched marker only in that context. `pending_validation` is the
// funnel's once-per-tick flag plus its origin (see PendingValidation),
// consumed by GuiInputHandler::run_commit_validation at the top of
// on_tick. `suspended_for_close` marks a series parked while a Ctrl+Q /
// Ctrl+W close-or-revert prompt is up over it (the defect modal is
// dismissed for the duration): it makes request_close_or_revert confirm
// the close even when the store is clean (a load-origin series has
// app.dirty == false), and, on cancel, is the signal that the series must
// resume — re-queued through pending_validation with the origin derived
// from commit_context, so the same defect (and its coincident-group
// narrowing) reappears. The close prompt is the ordinary
// save/discard/cancel form: every state the series can show is a walkable
// defect, hence load-legal, so a save writes a loadable file that
// re-walks its series on the next load. Any wholesale defect_series reset
// (load, revert) clears it; the resume path clears it explicitly.
struct DefectSeriesState {
    MarkerDefect      defect;
    TrimDefectKind    trim_defect_kind   = TrimDefectKind::None;
    bool              commit_context     = false;
    bool              suspended_for_close = false;
    PendingValidation pending_validation = PendingValidation::None;
};

// Trim store (architect-ruled hardfail model). begin and end are authored
// NAMED ROLES — no gesture ever reassigns which bound is which — holding
// whole source frames in int64_t, exactly like marker times (a fractional
// bound is unrepresentable; the .settings writer persists the exact value as
// integer text via frame_format.h, so a saved bound reloads bit-identically).
// Every trim GESTURE clamps each bound to its own
// absolute walls: begin spans frame 0 to EOF-1 (a begin at or past the
// source end can never render), end spans frame 0 to EOF exactly
// (end-at-EOF is valid, so the GUI must be able to represent it) — plain
// integer compares, the load guard's own comparison — so past-EOF
// cannot be gestured. There are NO partner walls — a bound crosses its
// partner freely during any gesture — but crossed and equal can no longer
// REST past a commit or a load: the funnel's defect-resolution series opens
// on the offending commit (or on the first tick after a load that carried
// them), and the delete-both-bounds resolution clears them. The zero floor
// is now subsumed by the per-bound walls, but it remains the reason the
// floor exists at all: a negative position is unrepresentable in the
// authored frame form the .settings file persists (parse_authored_frame
// rejects negatives as malformed) — a format-representability floor, not a
// validity rule. Loaded values mostly bypass this: the loader stays
// lenient for crossed and equal bounds (there is no trim corruption tripwire
// — those load intact), and the Load-origin defect walk
// demands their resolution on the first tick after the load. A past-EOF
// bound is the exception — it is adversarial (the gesture walls make it
// uncommittable and a .settings applies only to its own audio, so a
// past-EOF bound means the audio was swapped outside the GUI), hard-failed
// at the load boundary (file_loader / CLI) like a corrupt audio file. The
// render boundary owns validity beneath the series: validate_trim_frames
// (trimmer.h) issues every trim refusal — at render dispatch preflight, at
// the target-view gate, and as the breach backstop for hand-edited
// artifacts — and the GUI informs and demands resolution, it never guards a
// gesture. Readers must not assume begin <= end.
struct TrimState {
    int64_t begin_frame = 0;    // whole source frame (int64_t)
    int64_t end_frame   = 0;    // whole source frame (int64_t)
    bool    has_begin   = false;
    bool    has_end     = false;
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
// at default (those flow through the live AppState fields and the
// per-entry .settings autosave).
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

    // Per-tab backing store for app.trim / app.trim_*_selected /
    // app.last_selected_trim / app.last_sel_group. Synced only at the
    // tab-swap boundary in active_views.cpp (same pattern as
    // viewport/zoom/playhead).
    TrimState     trim;
    bool          trim_begin_selected = false;
    bool          trim_end_selected   = false;
    char          last_selected_trim  = 0;
    LastSelGroup  last_sel_group      = LastSelGroup::Markers;
};

struct AppState {
    int     width                 = 1400;
    int     height                = 800;
    bool    loading               = false;

    // Live working copy of the active view's state (viewport / zoom /
    // playhead here, plus selected_markers / last_selected_marker below).
    // This is an INTENTIONAL cache of the active view's per-view slot, not
    // accidental duplication: the paint path and the input handlers touch
    // these constantly, and the active backing store varies (source tab A/B
    // vs the active render-view entry), so reading through active_view_state()
    // on every access would be both hot and conditional. The slot is synced
    // to/from these fields only at view-switch boundaries (see active_views).
    // Do not collapse this into a projection — the duplication is the design.
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
    // playing. Natural end holds the scanner on the exclusive end bound for
    // one paint before restoring it to the cursor; manual stop paths restore
    // immediately. The cursor is per-tab; the scanner is session-only and not
    // persisted.
    // `playback_speed` is authoritative on the main thread and pushed
    // to the playback engine on every change.
    int64_t playhead_scanner_sample = 0;
    bool    playhead_scanner_active = false;
    bool    playhead_scanner_restore_pending = false;
    bool    playhead_scanner_endpoint_painted = false;
    float   playback_speed          = 1.0f;

    // GUI-wide monospace text size in points (the font_size setting; 6..72,
    // default 11). A display preference, not engine input and not authoring
    // state: persisted on Ctrl+S like playback_speed, applied at file load
    // and stepped by the Ctrl+Shift+= / Ctrl+Shift+- hotkeys, and pushed to
    // the renderer's file-scope state via set_gui_font_size_pt at each of
    // those application points.
    double  font_size               = 11.0;

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
    std::string phaseresetmarkers_path;

    // Absolute or relative path of the currently loaded audio file. Used by
    // the render hotkeys (Ctrl+Alt+R / Ctrl+E / Ctrl+Alt+E / Ctrl+Alt+I) and
    // the render pipeline to compute output paths. Empty when no file is
    // loaded (blank state).
    std::string source_audio_path;

    // Sidecar artifact identifiers use the sidecar's exact spelling
    // (warpmarkers, phaseresetmarkers, no underscores): files, path fields,
    // stores, and parse machinery. Musical concepts keep word separators
    // (phase_reset_dirty, phase_reset_frame_map).
    // Parsed warp markers for the currently loaded audio. Empty on load
    // failure or before the first audio load.
    GuiWarpMarkers  warpmarkers;

    // Parsed phase reset markers. Authored by the GUI but not
    // yet consumed by the render pipeline.
    GuiPhaseResetMarkers phaseresetmarkers;

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
    // Toggled by `p`. Determines which marker collection is visible / edited /
    // hit-tested and which color set is used for the playhead and selected
    // indicators.
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

    // Memoized target-view warp_frame_map (see warp_frame_map_view.h). Mutable: consulted and
    // refreshed from const hit-test paths.
    mutable TargetWarpFrameMapCache target_warp_frame_map_cache;

    // Ctrl+drag state. Not reset across file loads — explicitly cleared
    // there and on button release / Escape.
    DragState     drag;

    // Playhead drag state (plain / Shift left-button). Cleared on button
    // release, Escape, and file load.
    PlayheadDragState playhead_drag;

    // Ctrl+drag of a trim boundary stem. Cleared on button
    // release, Escape, and file load.
    TrimDragState trim_drag;

    // Ctrl+drag on empty waveform (stepped viewport scroll). Cleared on
    // button release and file load.
    ScrollDragState scroll_drag;

    // Mouse drag-to-select inside the active text editor. Cleared on
    // button release, on a lost button mid-drag, and on file load.
    EditorTextDragState editor_text_drag;

    // Which selection group the last selecting gesture targeted.
    // Drives Delete / Ctrl+drag group dispatch. Session-only.
    LastSelGroup last_sel_group = LastSelGroup::Markers;

    // Hover-popup state. See HoverPopupState above.
    HoverPopupState   hover_popup;

    // Cursor screen position from the last on_motion event. Used by
    // recompute_hover_at_cursor() to re-evaluate hover after a viewport
    // mutation (when the cursor is stationary but rects have shifted). -1
    // means "no motion seen yet".
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
    // Authoring-class settings (engine, scale, N, limiter, title,
    // audio_input, plus any free-form non-view key typed into the settings
    // editor) participate in dirty via settings_dirty. View-state keys
    // (viewport, zoom, playhead, follow_mode, active_markers_view,
    // playback_speed, trim, and the per-tab read_only flags) do NOT
    // participate: they are silently persisted on Ctrl+S and silently
    // discarded on Ctrl+W without save. Trim is gesture-owned, excluded from
    // undo/redo history, and render-affecting but deliberately treated as
    // transient view state.
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
    // The buffer's domain anchor — the full-target-frame coordinate that
    // target_buffer[0] represents — is NOT app state: it travels with the
    // playback bind as GuiPlayback's domain offset, computed by
    // GuiTargetRender::compute_target_buffer_start_frame at each rebind.

    // Active tab view: 'A' or 'B'. Selects which ViewState snapshot
    // (tab_a or tab_b) is mirrored into the live AppState fields.
    // Toggled by Ctrl+Tab; persisted to .settings. tab_a and tab_b
    // each hold an independent viewport/zoom/playhead/trim/selection
    // tuple, but share the same warpmarkers, phaseresetmarkers,
    // and engine_settings.
    ViewState tab_a;
    ViewState tab_b;
    char active_tab_view = 'A';

    // Typed engine settings. The live authoring store: settings editor
    // commits, .settings file load, and the BPM-sweep / Ctrl+Alt+C scale
    // commit paths all mutate fields of this struct directly. Carried
    // by RenderRequest at dispatch; serialized to .settings on Ctrl+S.
    // Default-constructed before any source load.
    EngineSettings engine_settings;

    // Live working copy of the active tab's trim state. The per-tab
    // backing store lives in ViewState::trim (and the companion
    // *_selected / last_selected_trim / last_sel_group fields there).
    // Excluded from undo/redo. Mirrored to/from the active tab's
    // ViewState slot at the tab-swap boundary in active_views.cpp
    // (same pattern as viewport/zoom/playhead).
    TrimState trim;

    // Transient selection of the trim boundary stems. A separate
    // selection channel from the marker sets (selected_markers /
    // phase_reset_selected) — the two groups are orthogonal and can be
    // co-selected. Not persisted to .settings; defaults false and resets on
    // file load. Which group a group-acting gesture (Delete, Ctrl+drag)
    // targets is decided by last_sel_group.
    bool   trim_begin_selected = false;
    bool   trim_end_selected   = false;
    // Which trim bound was most recently selected by a selecting gesture.
    // 0 = none, 'B' = begin, 'E' = end. Drives Ctrl+wheel end-move: when
    // begin is the last-selected trim bound the wheel moves the end bound
    // rather than nudging the focused warp marker's tempo.
    char   last_selected_trim  = 0;

    // Bottom-strip command prompt. Active only when a close / revert /
    // re-detect gesture fires while a confirmation is required. Originally
    // a centered modal dialog; the same modal semantics now live in the
    // bottom strip.
    PromptState prompt;

    // Defect-resolution series (see DefectSeriesState above). Lives beside
    // the prompt it drives.
    DefectSeriesState defect_series;

    // Top-flag text editor. Active only when editing a flag rect
    // in warp view. The editor owns the keyboard while active and
    // overlays a custom rect + cursor on top of render_flags.
    text_editor::State top_flag_editor;
    // Last-painted cursor visibility, so the tick can detect a flip and
    // invalidate the top strip without redundant repaints.
    bool top_flag_editor_blink_last = false;

    // Settings-prompt editor. Opens on `:`, accepts a single `key=value`
    // line, writes to engine_settings on commit. Lives in the bottom
    // strip; separate from top_flag_editor so the two paint regions stay
    // independent (the in-practice mutual exclusion comes from the flag
    // editor swallowing all keys while active).
    text_editor::State settings_editor;
    bool settings_editor_blink_last = false;

    // Tick backstop bookkeeping: last live-domain total observed by the
    // on_tick clamp (see main.cpp). 0 = not yet observed.
    int64_t last_tick_live_total = 0;

    // Render-queue state. `queue_running` is true while any archival render
    // dispatch is in flight (the Ctrl+Alt+R one-shot and all batch runs). The
    // Esc handler checks it to scope the cancel binding away from normal
    // interaction. `queue_cancel_requested` is set by Esc during a queue run
    // and read between entries.
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
    // Each entry is a complete render snapshot — markers, phase resets,
    // engine settings, trim, and authoring view state are all captured at
    // enqueue time, so two queued states that differ only in trim, settings,
    // or UI state still render distinctly.
    struct QueuedRender {
        std::string                source_audio_path;
        std::vector<GuiWarpMarker>     warp_markers;
        std::vector<GuiPhaseResetMarker>  phase_resets;
        EngineSettings              engine_settings;
        bool                        has_trim_begin = false;
        int64_t                     trim_begin_frame = 0;  // source frames
        bool                        has_trim_end   = false;
        int64_t                     trim_end_frame   = 0;  // source frames
        AuthoringSnapshot           authoring;
    };
    std::vector<QueuedRender> queued_renders;

    // One-slot pending archival render command. An archival dispatch
    // (Ctrl+Alt+R / Ctrl+Alt+E / the iteration or bpm sweep) that finds the
    // worker busy kills the running render (the Esc pair: request_cancel +
    // queue_cancel_requested) and parks its fully built request set here;
    // the worker-idle pump (GuiTargetRender::maybe_dispatch_pending, via
    // GuiInputHandler::dispatch_pending_archival_command) dispatches it once
    // the cooperative cancellation drains — never a synchronous wait on the
    // GUI thread. A newer command replaces an older parked one wholesale.
    // Esc during the drain disarms the slot (Esc means stop rendering, and
    // the parked command would otherwise resurrect a render right after the
    // cancel lands); a source load drops it (cancel_for_load), since the
    // requests were built against the torn-down source.
    struct PendingArchivalCommand {
        bool armed = false;
        // Ctrl+Alt+R shape: reqs holds exactly one request, dispatched
        // through dispatch_single_archival_render with `fingerprint` as the
        // session fingerprint (possibly empty on a load-identity stat
        // failure at build time). Batches (armed && !single) go through
        // start_render_batch with `batch_label` and carry no fingerprint —
        // a batch session never matches a dispatch.
        bool                       single = false;
        std::vector<uint8_t>       fingerprint;
        std::vector<RenderRequest> reqs;
        std::string                batch_label;
    };
    PendingArchivalCommand pending_archival;

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

    // One entry in the flat list of valid renders enumerated on toggle-in.
    struct RenderViewEntry {
        std::filesystem::path batch_folder;     // <source_parent>/renders/<i>_<tag>
        std::string           basename;         // e.g. "01" (no extension)
        std::filesystem::path wav_path;         // batch_folder / (basename + ".wav")

        // Per-entry persisted view-state across render-view
        // exit/enter and batch-nav. The stashed marker selection is valid
        // only when the wav still has the same (size, mtime) as when
        // stashed; mismatch on reload drops it silently. Both mode slots
        // (warp + phase reset) ride on `state`; the loader restores the one
        // matching the entry's persisted W/P mode (applied to
        // app.active_markers_view at load). The viewport/zoom/playhead fields
        // on `state` are unused — render-view's browse position flows through
        // the live AppState fields and the per-entry .settings. Render view is
        // a one-way bubble: disk owns each entry's browse state. The entry's
        // tab/zoom/viewport/playhead/W-P AUTOLOAD from its .settings on every
        // display and AUTOSAVE back on every nav-away; the loader applies them
        // (they no longer inherit from the live session). The authoring
        // session's tab and W/P mode are stashed at render-view entry and
        // restored on exit, so browsing never leaks out except through
        // Ctrl+Alt+C.
        ViewState state;

        // Stat-tuple key for selection validity. Captured when stashed,
        // compared against the current file's stat on re-load.
        uintmax_t     persisted_size             = 0;
        int64_t       persisted_mtime            = 0;
    };

    // Render analysis mode. Plain `r` toggles between source-view
    // (authoring) and render-view (read-only auditioning of rendered
    // outputs from <source_parent>/renders/). All authoring state above
    // is preserved untouched while render-view is active; this struct
    // holds the parallel context that drives render-view's display.
    struct RenderViewContext {
        bool enabled = false;
        // Path to the last-displayed render's .wav, persisted across toggle
        // off/on cycles within a session. Empty before the first entry; reset
        // to whatever path was active when the previous toggle-off fired.
        std::string last_path;
        std::vector<RenderViewEntry> list;
        int                          index = -1;     // -1 = unset
        // The current render's display markers + phase resets: the entry's
        // snapshot stores WHOLESALE (entry load reads the sibling snapshot
        // set `<basename>.warpmarkers` / `.phaseresetmarkers` /
        // `.settings`), disabled rows and label defs included, with
        // time_frame at the AUTHORED whole-frame values. Render view is a
        // read-only 1:1 target view of the snapshot: the FULL deformed
        // timeline, with the snapshot trim's out-of-window region dimmed
        // and its bounds painted (pickable, immutable), and playback bound
        // to the entry wav at the window's target-axis origin. Every consumer
        // translates these positions live through the display context
        // (the snapshot map below), exactly as target view translates
        // through the live map.
        std::vector<GuiWarpMarker>       warp_markers;
        std::vector<GuiPhaseResetMarker>    phase_resets;
        // Snapshot display geometry: built once from the entry's snapshot
        // at load (read-only view — no invalidation), cleared wherever the
        // display stores above are cleared. The Render display context
        // (active_display_context) aliases the map. snapshot_display_total
        // is the snapshot map's target total (target_total_frames_for_map
        // against the source total, computed at entry load) — the Render
        // display domain's total (live_total_frames semantics); the
        // GuiAudio object stays the source in every view, so the displayed
        // total must live context-side. entry_domain_begin is the playback
        // bind anchor: llrint(T_b) of the snapshot map — the entry wav's
        // frame 0 in full-target coordinates; 0 untrimmed (the sibling of
        // GuiTargetRender::compute_target_buffer_start_frame's formula).
        // The snapshot_*trim* fields are the entry's recipe trim (whole
        // int64 source frames from the .settings commit tab) — DISPLAY
        // state only, never editable: the trim display surfaces map them
        // through the snapshot map to paint the rendered window's dim and
        // bounds. sample_rate stays the source's (equal to the render's by
        // construction, verified at entry decode). snapshot_commit_tab is
        // the tab the entry is browsed under, attested at commit: entry load
        // sets it from the entry's persisted active_tab_view, and a
        // render-view Ctrl+Tab updates it to the new tab at the same moment
        // the autosave persists the new active_tab_view, so it always names
        // the tab currently on screen. Ctrl+Alt+C re-reads the .settings
        // fresh and compares its active_tab_view against this stash: the
        // routing keys (active_tab_view, active_audio_view) ride OUTSIDE the
        // render fingerprint (browse autosaves rewrite the same file), so the
        // fingerprint check cannot catch a routing change underneath the
        // display; this exact-match attestation does — passing for
        // GUI-authored browsing, failing for hand edits. Cleared to 'A'
        // beside the other snapshot fields at every clear site.
        std::vector<WarpFrameMapSegment> snapshot_warp_frame_map;
        int64_t                          entry_domain_begin = 0;
        int64_t                          snapshot_display_total = 0;
        bool                             snapshot_has_trim_begin = false;
        int64_t                          snapshot_trim_begin_frame = 0;
        bool                             snapshot_has_trim_end = false;
        int64_t                          snapshot_trim_end_frame = 0;
        char                             snapshot_commit_tab = 'A';
        // The authoring session's tab letter and W/P mode, stashed at
        // render-view ENTRY (the first entry load, beside the tab-slot
        // refresh) and restored wholesale by restore_source_view on exit.
        // Render view is a one-way bubble: browsing is disk-owned per entry
        // (each entry's tab/position/mode autoload from its .settings and
        // autosave back on nav-away), and the authoring tab/mode never leak
        // into that browsing — they wait here for the exit restore. Cleared
        // beside the other snapshot fields at every clear site (to the
        // struct defaults 'A' / 'W').
        char                             stashed_authoring_tab = 'A';
        char                             stashed_authoring_markers_view = 'W';
    };
    RenderViewContext render_view;
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
// The explicit-domain form of current_samples_per_pixel: spp at a zoom
// level against a caller-chosen domain total (fit-file zoom divides the
// total by the width; numeric levels are total-independent). For callers
// that need a domain OTHER than the active display context's — render-view
// entry computes the pre-entry source-view spp after the render display
// context is already installed.
double  samples_per_pixel_at(int zoom_level,
                             int waveform_width_px,
                             int64_t total_frames,
                             int sample_rate);
// Active-domain sample range a marker may occupy to stay within the visible
// strip: pixel 0 (viewport_start) through the last fully-visible pixel
// (area.w - 1). Mouse-driven marker moves clamp the grabbed marker to this so
// it can never hide offscreen, where its precise location would be lost. The
// playhead is exempt — it alone may reach 1px past the strip, and only at EOF.
// Returns {lo, hi} as active-domain samples.
std::pair<int64_t, int64_t> viewport_marker_bounds(const AppState& a,
                                                   const GuiAudio& audio);

// The ONLY route by which a double becomes an authored position anywhere in
// the tree: every gesture commit — nudge, drag release, the
// trim gestures, the trim-end wheel, propagate paste — funnels its final
// position through this before writing a marker time or trim bound, so
// every store mutation commits a whole int64 frame (the field type makes a
// fractional authored position unrepresentable). Banker's rounding
// (std::nearbyint under the default rounding mode — the project-wide
// convention), no epsilon; the cast after nearbyint is exact. The ties are
// real, not theoretical: at 44.1 kHz
// the zoom table's frames-per-pixel values are 27.5625, 55.125, 110.25,
// 220.5, 441, ... (0.625 ms/px deepest, doubling), so at the 5 ms level
// every odd pixel offset is an exact half-frame tie — banker's rounding
// debiases them. No other call site may round or cast an authored
// position on its own.
inline int64_t snap_authored_frame(double frame) {
    return static_cast<int64_t>(std::nearbyint(frame));
}

// Restore ascending time_frame order after a mutation that may have
// moved markers past their neighbors (shift, nudge, drag commit). The
// marker stores are always sorted by time_frame at rest; mutations
// that change times in place call this immediately after writing.
// Stable: equal-time markers keep their pre-sort relative order, so
// ties resolve deterministically. Returns the old-index -> new-index
// permutation when a reorder happened, or an empty vector when the list
// was already in order (the common case — the up-front scan keeps that
// path allocation-free). Callers pass the result to
// remap_marker_indices_after_reorder so every index-shaped piece of
// state that referenced a moved marker follows it. The marker container
// is read only through `time_frame`, so both marker types work.
template <typename Marker>
std::vector<int> reorder_markers_by_time(std::vector<Marker>& markers) {
    const int n = static_cast<int>(markers.size());
    bool sorted = true;
    for (int i = 1; i < n; ++i) {
        if (markers[i - 1].time_frame > markers[i].time_frame) {
            sorted = false;
            break;
        }
    }
    if (sorted) return {};
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
        [&markers](int a, int b) {
            return markers[a].time_frame < markers[b].time_frame;
        });
    std::vector<Marker> reordered;
    reordered.reserve(n);
    for (int old_idx : order) reordered.push_back(std::move(markers[old_idx]));
    markers = std::move(reordered);
    std::vector<int> old_to_new(n);
    for (int new_idx = 0; new_idx < n; ++new_idx) {
        old_to_new[order[new_idx]] = new_idx;
    }
    return old_to_new;
}

// Apply a reorder_markers_by_time permutation to the index-shaped state
// that must follow moved markers: app.selected_markers,
// app.last_selected_marker, and — when a drag is live on the reordered
// store — the drag state's held marker indices. Undo snapshots copy
// whole lists and need no remap. No-op on an empty permutation (the
// store was already in order). Body in app_state.cpp.
void remap_marker_indices_after_reorder(AppState& app,
                                        const std::vector<int>& old_to_new);

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
// target view returns the deformed total derived from the warp_frame_map cache
// (the forward-translated source length). Used by every viewport helper that needs the
// "length of the timeline currently being viewed" — clamp, fit-file zoom,
// and the numeric-level cap. Declared here so any TU touching the
// viewport math can reach it.
int64_t live_total_frames(const AppState& a, const GuiAudio& audio);
int     max_valid_numeric_level(int waveform_width_px,
                                int64_t total_frames,
                                int sample_rate);
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, long long total_frames);
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

// Snapshot the undo-tracked settings from `app` (engine_settings; trim is
// excluded). Called by Undo's push helpers at push time so every entry
// carries-everywhere; also called by do_undo / do_redo when constructing the
// inverse entry. Body in app_state.cpp.
SettingsSnapshot capture_current_settings(const AppState& app);

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
// bounds are testable — the active tab's live pair in authoring views,
// the entry's snapshot recipe bounds in render view. Walks the same
// warp_frame_map as hit_test_marker_line in the mapped views so the hit
// lands on the visually-drawn stem. Trim is project-level, and applies
// in both 'W' and 'P' views. When both bounds are within reach, the
// nearer wins.
TrimHit hit_test_trim_boundary(const AppState& app, const GuiAudio& audio,
                               int mouse_x);

// hit_test_trim_chip: like hit_test_trim_boundary, but tests the press against
// each set bound's painted CHIP RECT in the upper top row rather than the stem
// column. The chip glyph ("b"/"e") is drawn hl_pad to the right of the bound's
// column, so a column-only test misses clicks on the visible chip. The rect
// mirrors regular-flag hit geometry (flag_chip_rect, the shared chip-rect
// helper): x = round(text_left), w = round(glyph_advance + 2*flag_pad_x_px()),
// with y/h from the row metrics; the same rect the renderers fill, so paint
// and hit cannot drift. Tests both mouse_x and mouse_y. Used for upper-row
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

// Sweep-select every marker in the time-ordered `markers` list whose
// time_frame falls in [lo_t, hi_t] (double interval bounds; the stored
// int64 frames widen exactly into the compares), iterating in travel
// order (ascending
// indices when `forward`, else descending) so the final last_selected_marker
// lands on the most recently passed marker. Skips press_marker_idx (preserves
// the Shift-press toggle non-re-add guarantee) and already-selected indices.
// Mutates app's selection set / focus / last_sel_group; returns true if
// anything was added. Shared by the source/target and render-view playhead-drag
// Shift sweeps (input_handler.cpp / input_render_view.cpp); templated on the
// vector element type because the two stores hold different marker types that
// both expose time_frame. O(log n + added) per call.
template <typename MarkerVec>
bool sweep_select_interval(AppState& app, const MarkerVec& markers,
                           double lo_t, double hi_t, bool forward,
                           int press_marker_idx) {
    if (lo_t > hi_t) return false;
    // First index with time_frame >= lo_t through the last with
    // time_frame <= hi_t (half-open [first, last)).
    const int first = static_cast<int>(
        std::lower_bound(markers.begin(), markers.end(), lo_t,
                         [](const auto& m, double t) {
                             return m.time_frame < t;
                         }) - markers.begin());
    const int last = static_cast<int>(
        std::upper_bound(markers.begin(), markers.end(), hi_t,
                         [](double t, const auto& m) {
                             return t < m.time_frame;
                         }) - markers.begin());
    bool changed = false;
    auto add = [&](int idx) {
        if (idx == press_marker_idx) return;
        const bool newly = app.selected_markers.insert(idx).second;
        if (newly || app.last_selected_marker != idx) changed = true;
        app.last_selected_marker = idx;
        app.last_sel_group = LastSelGroup::Markers;
    };
    if (forward) {
        for (int i = first; i < last; ++i) add(i);
    } else {
        for (int i = last - 1; i >= first; --i) add(i);
    }
    return changed;
}
