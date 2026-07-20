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
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

class GuiAudio;

// Zoom level numbering: the range constants (kMinZoom, kMaxZoom) live in
// settings_file.h — the persisted zoom vocabulary the whole-settings schema
// enforces in both products — and app_state.h re-exports them through the
// include above.
//
// The zoom level is a real-valued exponent resting anywhere in the ONE
// continuous domain [kMinZoom, kMaxZoom] (Ableton-style free rest): manual zoom
// walks it by whole steps from its current, possibly fractional, rung, and
// zoom-out saturates at the per-file effective ceiling
// (effective_max_zoom_level), where full zoom-out rests at whole-song-visible.
// There is no fit-file mode and no sentinel level. Bare-digit keys are unbound
// for zoom: only `0` toggles between the working zoom and full zoom-out, and
// `C` jumps to the working zoom centered on the playhead (or on the focused
// marker). Smaller level = less file per window = more zoomed in. kMinZoom is
// the deepest zoom-in the manual walk can reach (1.2 s); kWorkingZoomLevel is
// the fine-tuning rest point every snap/toggle gesture lands on (2.4 s, one
// step shallower), where the working-zoom authoring-grid bit-exactness claims
// hold.
constexpr double kWorkingZoomLevel = 2.0;  // 2.4 s — working zoom; manual
                                           // zoom-in can go one step deeper to
                                           // kMinZoom (1.2 s)

// Viewport lead/overlap fraction, expressed as a divisor of the visible
// span. Follow mode keeps this much of the window as lead context when it
// re-anchors; paged scroll (PageUp/PageDown) retains the same fraction as
// overlap so the two behaviors stay visually consistent. One source of
// truth — do not inline the divisor at either site.
constexpr int64_t kViewportLeadDivisor = 10;

// Alt+wheel chip-row end-move step as a divisor of samples_visible.
constexpr int64_t kTrimEndWheelDivisor = 10;

// Hoisted from main.cpp's anonymous namespace so the hit_test_*
// free functions (in app_state.cpp) and the GuiInputHandler mouse handler
// (in input_handler.cpp) can reach them. Hit-test half-width only:
// clicking/hovering tolerance for stems, flags, and trim bounds. It is
// NOT a spacing gap — markers may sit arbitrarily close, overlap
// exactly, and cross during gestures; ordering degeneracy collapses at
// the render boundary, not at authoring time.
constexpr int kMarkerHitHalfPx    = 4;

// Vertical drag distance (px) that moves the zoom-strip drag by one continuous
// level. The strip zoom drags DOWN to zoom in (deeper, lower level) and UP to
// zoom out. Both this scale and that direction are architect-tunable on the
// labwc pass.
constexpr double kZoomStripPxPerLevel = 60.0;

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
    // False for an iteration-bracket-only snapshot. Iteration brackets are
    // session state and never serialize, so crossing such an entry must not
    // make recompute_dirty report a warp-file difference.
    bool                      affects_persistence  = true;
};

// The marker selection state a marker drag's first-motion collapse rewrites,
// captured wholesale at drag begin and restored wholesale at cancellation —
// one struct so a future selection field cannot be forgotten in one of the
// copies (the per-field enumeration was the recurring leak). Moved only by
// capture_selection_snapshot / restore_selection_snapshot (declared below
// AppState, where its members are complete).
struct SelectionSnapshot {
    std::set<int> selected_markers;
    int           last_selected_marker = -1;
};

// Alt+drag state. `active` gates motion handling; the rest captures the
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
    // No per-drag map copy: mid-drag target-view translation (paint, playhead
    // tracking, commit snapping, the waveform job's owned snapshot) reads the
    // memoized display map — active_display_context /
    // target_view_warp_frame_map_cached — directly. That cache is keyed on the
    // marker-store generation + scale (+ sample rate + total frames), and
    // nothing that changes either is reachable while a drag is in flight: the
    // frozen-coordinate regime keeps motion in the overlay (no generation
    // bump), the drag-modal gate swallows every key but the cancels, pointer
    // gestures are mutually exclusive, the wheel is blocked mid-gesture, editors
    // cannot open, and resize / WM-close cancel the drag first; a preview
    // completing mid-drag touches only the audio buffer, playback rebind, dirty
    // bit, and status text, never the store or scale. So the cache is stable
    // for the drag's lifetime and equals what a begin_drag copy would hold.
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
    // Pre-drag marker selection snapshot, captured at begin_drag for the
    // Esc / Ctrl+Q cancellation restore (the pre_drag_playhead_sample pattern
    // extended to selection): first motion collapses the selection onto the
    // grabbed marker (set_single_selection), so cancel puts the marker
    // selection back.
    SelectionSnapshot      pre_drag_selection;
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

    // Evict the oldest (bottom) entry of the undo stack for the kCap trim while
    // keeping the saved reference honest. Saved distances into the undo stack
    // are negative. Hopping the saved baseline over an entry is
    // persistence-equivalent iff that entry's affects_persistence is false —
    // the same equivalence the redo-orphan collapse in push() applies. So when
    // the saved reference points at or below the evicted bottom: a session-only
    // evicted entry pins the reference to the stack-reachable bound (provably
    // equivalent for dirty purposes, since recompute_dirty's walk skips
    // session-only entries); a persistence-affecting evicted entry leaves the
    // baseline genuinely unreachable and inequivalent, so invalidate it —
    // recompute_dirty's invalid branch then marks everything dirty, the
    // conservative direction a save re-establishes. The undo stack is the ONLY
    // stack that ever evicts (the restore-side non-trim invariant is recorded
    // at its site in restore_history_entry).
    void evict_undo_bottom_with_saved_ref() {
        const bool evicted_affects_persistence =
            undo_stack.front().affects_persistence;
        undo_stack.erase(undo_stack.begin());
        if (!saved_valid) return;
        const int bound = -static_cast<int>(undo_stack.size());
        if (saved_distance >= bound) return;
        if (evicted_affects_persistence) saved_valid    = false;
        else                             saved_distance = bound;
    }

    // Push the pre-mutation entry. Clears the redo stack. If the saved
    // reference was on the redo stack, it is orphaned only when the path back
    // to it crosses a persistence-affecting entry. A path made solely of
    // session-only iteration-bracket entries is persistence-equivalent to the
    // current cursor, so collapse the saved reference here before clearing it.
    // If pushing evicts the bottom of the undo stack and the saved reference
    // pointed at or below the evicted entry, evict_undo_bottom_with_saved_ref
    // resolves it by the same equivalence rule (pin when session-only,
    // invalidate when persistence-affecting).
    void push(UndoEntry entry) {
        if (saved_valid && saved_distance > 0) {
            const int rs = static_cast<int>(redo_stack.size());
            bool path_affects_persistence = false;
            for (int i = std::max(0, rs - saved_distance); i < rs; ++i) {
                if (redo_stack[i].affects_persistence) {
                    path_affects_persistence = true;
                    break;
                }
            }
            if (path_affects_persistence) saved_valid = false;
            else                          saved_distance = 0;
        }
        redo_stack.clear();
        if (saved_valid) saved_distance -= 1;
        undo_stack.push_back(std::move(entry));
        if (undo_stack.size() > kCap) {
            evict_undo_bottom_with_saved_ref();
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
// positions the playhead ONLY (with a 3px snap-to-marker magnet) and never
// touches selection: selection changes only at press time (plain-press
// single-select, Shift-press toggle, Tab). Only a plain press arms the drag; a
// Shift press is a click. The gesture ends on release (or on Escape, which ends
// at current position); release only resets the struct.
//
// Mouse-side click-keep-alive: a waveform-area press during playback
// reseeks audio to the clicked sample (Reaper-style) instead of stopping.
// Motion does NOT reseek — the scanner advances independently from the
// click position via the predictor while the cursor moves freely with
// the drag (split-playhead model).
struct PlayheadDragState {
    bool active                    = false;
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

// Alt drag of a trim boundary stem/chip (the pointer trim gesture, arming
// after the Alt+drag branch's marker hit test misses; the Alt+wheel chip-row
// end-move is the trim wheel gesture).
// Parallel to DragState but motion mutates the active tab's live trim mirror
// directly (no overlay); release triggers a target render when the bound
// moved. Trim is excluded from undo/redo. Session-only.
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

    // Alt (top-strip inter-chip span) move-both-bounds drag: both bounds
    // translate together by
    // the same delta in the active (on-screen) domain, preserving the gap
    // as it appears under warp. The pair has no grabbed-bound notion — both
    // bounds are the subject, it has no viewport clamp, and it never moves the
    // playhead; `is_begin` is Begin by construction (the router arms it so) and
    // is read only by the single-bound path, which uses it to name the one
    // bound that moves. No pre-drag playhead capture: trim drags never touch
    // the playhead, so an Esc/Ctrl+Q cancel has nothing to restore there (the
    // recorded difference from the marker DragState, which tracks and restores
    // its grabbed marker's playhead).
    bool    both                 = false;
    int64_t orig_begin_frame   = 0;
    int64_t orig_end_frame     = 0;
    int64_t anchor_active_frame  = 0;
};

// Plain left-drag on a live strip row (Ableton-style navigation). ONE anchor
// formula serves both axes: the song position painted under the press x sticks
// to the pointer for the whole drag, so horizontal motion is a grab-pan and
// vertical motion (top zoom row only) zooms around the point under the pointer.
// Navigation-class: never touches the playhead or selection, allowed in
// read-only, does not toggle or override follow. Cleared on button release /
// button-lost and on file load; no Esc-restore (nothing to revert).
struct StripDragState {
    bool   active    = false;
    // True once any motion event has applied a viewport/level change. A
    // motionless press-release must commit nothing, so the terminating event
    // finalizes (re-derives + synchronous rebuild) only when this is set.
    bool   moved     = false;
    // True for the top zoom row (vertical motion changes the level); false for
    // the bottom pan row (level pinned for the whole drag).
    bool   zoom_axis = false;
    // Pointer position at the press (window px).
    int    press_y   = 0;
    // Continuous zoom level at the press — always app.zoom_level verbatim, on
    // both the zoom and pan rows. The zoom row walks the one continuous domain
    // from wherever the level rests; the pan row never changes it.
    double press_level = 0.0;
    // Song position (frames, double) painted under the press x at press time,
    // unclamped — the viewport clamp owns legality. Held to the pointer's x for
    // the whole drag.
    double anchor_sample = 0.0;
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
// applies to the unsaved-work prompt (CLOSE_WINDOW, the quit gesture).
// ERROR_NOTICE is the dismiss-only error popup for the environmental,
// settings-choice, and tripwire-class refusals (see
// GuiPrompt::open_error_notice's caller list). Its text is the owner's own
// error string, unmodified, and its sole response is acknowledge/dismiss.
enum class DialogTrigger {
    CLOSE_WINDOW,
    PASTE_CONFIRM,
    ERROR_NOTICE,
    // Load-time render-environment mismatch (GuiPrompt::open_env_hash_mismatch):
    // advisory only — 'o', the sole response key, stamps the stored hashes to
    // the current environment (history-less, no-dirty GUI-kind state that
    // persists on the next ordinary save). No dismiss-without-ack path:
    // acknowledging is the only way past the prompt.
    ENV_HASH_MISMATCH,
};

// In-window modal prompt state. When `active` is true, the bottom strip
// overlays the prompt's text and response options in place of the
// timestamp / tab letter / dirty indicator.
// Input is owned by the prompt: only the response keys (and Esc, which
// activates the rightmost response) do anything; everything else is
// swallowed. `response_keys` holds lowercase letters; the activator
// lowercases incoming keypresses before comparing.
struct PromptState {
    bool                     active = false;
    std::string              text;
    std::vector<char>        response_keys;     // lowercase
    std::vector<std::string> response_labels;   // e.g. "[s]ave"
    DialogTrigger            trigger = DialogTrigger::CLOSE_WINDOW;
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
// partner freely during any gesture — but crossed or equal bounds can no
// longer REST anywhere: every trim commit auto-clears a pair left with
// end_frame <= begin_frame (both bounds destroyed, silently —
// GuiInputHandler::auto_clear_crossed_trim, the trim sibling of the marker
// normalizations), and a persisted crossed/equal pair clears per tab at
// load with one stderr line (file_loader). The zero floor
// is now subsumed by the per-bound walls, but it remains the reason the
// floor exists at all: a negative position is unrepresentable in the
// authored frame form the .settings file persists (parse_authored_frame
// rejects negatives as malformed) — a format-representability floor, not a
// validity rule. A past-EOF
// bound is adversarial (the gesture walls make it
// uncommittable and a .settings applies only to its own audio, so a
// past-EOF bound means the audio was swapped outside the GUI), hard-failed
// at the load boundary (file_loader / CLI) like a corrupt audio file.
// validate_trim_frames (trimmer.h) stays the sole author of the
// trim-validity vocabulary, but a refusal at render time means "render
// untrimmed" (plan_trim's callers fall back to the full deliverable, one
// stderr line), never a refused render; it never guards a gesture.
// Readers of MID-GESTURE state must not assume begin <= end — crossing is
// free until the commit — but at REST the order begin < end now holds
// whenever both bounds are set.
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
struct ViewState {
    int64_t viewport_start_sample      = 0;
    double  zoom_level                 = kWorkingZoomLevel;
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

    // Per-tab backing store for app.trim. Synced only at the tab-swap boundary
    // in active_views.cpp (same pattern as viewport/zoom/playhead).
    TrimState     trim;
};

struct AppState {
    int     width                 = 1400;
    int     height                = 800;
    bool    loading               = false;

    // Live working copy of the active view's state (viewport / zoom /
    // playhead here, plus selected_markers / last_selected_marker below).
    // This is an INTENTIONAL cache of the active view's per-view slot, not
    // accidental duplication: the paint path and the input handlers touch
    // these constantly, and the active backing store varies (source tab A/B),
    // so reading through active_view_state()
    // on every access would be both hot and conditional. The slot is synced
    // to/from these fields only at view-switch boundaries (see active_views).
    // Do not collapse this into a projection — the duplication is the design.
    int64_t playhead_cursor_sample = 0;
    double  zoom_level             = kWorkingZoomLevel;
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
    float   playback_speed          = 0.7f;

    // Looping audition (trim set, launch-captured at toggle_playback).
    // `playback_loop_start_sample` is the domain-coordinate loop start decided
    // ONCE at the play launch (-1 = this session does not loop); the
    // click-keep-alive reseek threads it back through play() so a mid-session
    // trim edit cannot alter the running session's loop verdict.
    // `playback_loop_wrap_seen` is the last loop_wrap_seq() the per-redraw
    // drive observed; a change means the audio callback wrapped and the drive
    // resyncs the predictor. It is never reset (loop_wrap_seq is monotonic and
    // never reset either), so it stays valid across sessions.
    int64_t  playback_loop_start_sample = -1;
    uint64_t playback_loop_wrap_seen    = 0;

    // GUI-wide monospace text size in points (the font_size setting; 6..72,
    // default 11). A display preference, not engine input and not authoring
    // state: persisted on Ctrl+S like playback_speed, applied at file load
    // and set through the settings editor (`:font_size=`, no hotkey), and
    // pushed to the renderer's file-scope state via set_gui_font_size_pt at
    // each of those application points.
    double  font_size               = 11.0;

    // GUI-kind launch preference: the external audio player the `l`
    // ("Listen to renders") command spawns with the rendered wavs. The
    // "audacious" default lives in exactly two places: this member initializer
    // (the pre-load state) and the first-open template's audio_player line
    // (settings_io.cpp) — the key is required in every `.settings`, so a load
    // always assigns this field from the file. A BLANK value
    // (`audio_player=`) is the deliberate opt-out, and `l` then reports
    // "no audio_player set" and does nothing. Persisted on Ctrl+S. The one
    // GUI-kind key with NO gesture: the settings editor
    // (`:audio_player=<path>`) is its sole authoring surface; consumed only
    // by the `l` launcher.
    std::string audio_player = "audacious";

    // Render-environment attestation: the STORED per-library stat-identity
    // digests (16 lowercase hex digits each, env_fingerprint.h) the loaded
    // `.settings` recorded at its last save. Pre-load default is empty — never
    // written as empty: the four keys are required, so a load always assigns
    // them, and the first-open template stamps the four CURRENT hashes (a fresh
    // project starts matched, no prompt). Compared against compute_render_env_hashes()
    // once at source load; any mismatch opens the env-hash prompt, whose [o]k
    // — the sole response — stamps all four LIVE hashes to the current
    // environment's (no dismiss-without-ack path exists). The settings editor
    // (`:libm_hash=<16hex>` etc.) is the manual authoring surface. These are
    // history-less, no-dirty GUI-kind state: a restamp (prompt 'o' / editor
    // commit) mutates only these live fields and never marks the file dirty;
    // the new quartet persists on the next ordinary save (save_ops writes the
    // live values verbatim), and a save-less session simply re-modals the
    // mismatch on the next load by design (self-healing). Stored identity, not
    // recipe: the render fingerprint never reads these.
    std::string libm_hash;
    std::string libmvec_hash;
    std::string fftw3_hash;
    std::string fftw3_threads_hash;

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
    // the render hotkeys (Ctrl+Alt+R / Ctrl+Alt+I) and
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

    // Parsed phase reset markers. Authored by the GUI and compiled by the
    // parser (derive_phase_reset_frame_map) into engine input on every render.
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

    // Monotonic command-adjacency counter, bumped once per discrete user
    // command at the three command-dispatch entry points (GuiInputHandler's
    // on_key, on_button_press, on_wheel). The rapid-gesture undo-coalesce guard
    // (Undo::coalesce_gesture) records it at each eligible commit and merges a
    // later eligible press only when its command is the immediately-next one
    // (command_seq == recorded + 1) — so ANY intervening command (a click, Tab,
    // paste, save, undo/redo, tab/column switch, or an
    // unhandled key) advances the counter an extra step and breaks the burst.
    // A rapid same-gesture burst is, by definition, consecutive presses with no
    // other command between them, so adjacency alone captures it. Session-only,
    // never serialized.
    uint64_t      command_seq = 0;

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
    // interpretation flips on toggle. Target view was
    // formerly read-only; the target-render audio subsystem makes target
    // view playable with live engine output.
    char active_audio_view = 'S';

    // Memoized target-view warp_frame_map (see warp_frame_map_view.h). Mutable: consulted and
    // refreshed from const hit-test paths.
    mutable TargetWarpFrameMapCache target_warp_frame_map_cache;

    // The warp_frame_map the LAST COMMITTED frame's target-view item pixels
    // (marker/trim stems, flags, chips) were painted with — the geometry the
    // user is currently looking at, to commit granularity. This is the PROMOTED
    // half of a two-phase commit: the item-cache rebuilds stage a value
    // (staged_displayed_*, below), and the paint pass promotes it here at the
    // frame that blits those cache surfaces (GuiPaintHandler::on_redraw), so the
    // hit map advances exactly when the on-screen items commit — not at the
    // offscreen rebuild. Lifecycle: WRITTEN by the paint-pass promotion (target
    // view) / cleared-value promotion (source view, mapless items); CLEARED
    // (with the staged value) at source load and `'` adopt (through
    // apply_settings_engine_and_prefs) and at a view toggle
    // (handle_active_audio_view_toggle). Shutdown is terminal — no teardown
    // clear. The item hit tests read it through displayed_or_live_target_map so
    // what you grab is what you see (event-synchronized hit geometry — the
    // ruling at that selector). Empty = cold (no target frame has committed its
    // items yet); the selector then falls back to the live display map. The
    // irreducible remaining seam is commit-to-scanout plus human reaction —
    // input is always a response to the previously presented frame — recorded
    // as accepted at the selector.
    std::vector<WarpFrameMapSegment> displayed_target_warp_frame_map;

    // Staging half of the two-phase commit above. The item-cache rebuilds
    // (maybe_rebuild_stem_cache / maybe_rebuild_flag_cache) write the map they
    // baked here — or an empty clear in source view — and set the valid flag;
    // GuiPaintHandler::on_redraw promotes it into displayed_target_warp_frame_map
    // once, at the next committed frame (which blits those caches), then clears
    // the flag (idle frames with no staged value do nothing). A COPY, not a
    // reference to wf_cache.fp_warp_frame_map, so a worker completion between
    // the rebuild and the paint cannot mutate what the committed items were
    // built against. Cleared alongside displayed_target_warp_frame_map at every
    // clear site (a stale staged map surviving a view flip would promote wrong
    // geometry at the next paint). staged_displayed_valid distinguishes a
    // staged CLEAR (empty map, source view) from no-stage.
    std::vector<WarpFrameMapSegment> staged_displayed_target_warp_frame_map;
    bool staged_displayed_valid = false;

    // Alt+drag state. Not reset across file loads — explicitly cleared
    // there and on button release / Escape.
    DragState     drag;

    // Playhead drag state (plain / Shift left-button). Cleared on button
    // release, Escape, and file load.
    PlayheadDragState playhead_drag;

    // Alt drag of a trim boundary stem/chip. Cleared on button
    // release, Escape, and file load.
    TrimDragState trim_drag;

    // Plain left-drag on a live strip row (zoom/pan navigation). Cleared on
    // button release and file load.
    StripDragState strip_drag;

    // Mouse drag-to-select inside the active text editor. Cleared on
    // button release, on a lost button mid-drag, and on file load.
    EditorTextDragState editor_text_drag;

    // Hover-popup state. See HoverPopupState above.
    HoverPopupState   hover_popup;

    // Cursor screen position from the last on_motion event. Used by
    // recompute_hover_at_cursor() to re-evaluate hover after a viewport
    // mutation (when the cursor is stationary but rects have shifted). -1
    // means "no motion seen yet".
    int               last_mouse_x = -1;
    int               last_mouse_y = -1;

    // Undo/redo history for marker mutations. The dirty flags below are
    // derived from it (Undo::recompute_dirty walks saved_distance against
    // each persistence-affecting entry's op_mode). Save/load reshape the
    // saved reference rather than touching the flags directly.
    UndoHistory history;

    // True if any authoring-class file has changes since the last save.
    // app.dirty = warp_dirty || phase_reset_dirty || settings_dirty,
    // recomputed after every persistent push/undo/redo by walking
    // saved_distance against each persistence-affecting entry's op_mode.
    // Drives both the unsaved-work dialog and the dirty-dot.
    //
    // Authoring-class settings — the six engine-block keys (title, scale,
    // bpm, notes, url, cover; editor commits carry undo history) — participate
    // in dirty via settings_dirty. View-state keys — the GUI-kind keys
    // (viewport/zoom/playhead per tab, follow, active_audio_view,
    // active_markers_view, active_tab_view, playback_speed, trim, read_only,
    // font_size, audio_player, and the four *_hash env-attestation keys) — do
    // NOT participate: they are silently persisted on Ctrl+S and not tracked as
    // dirty, so quitting without saving simply drops them. Trim is
    // gesture-owned, excluded from undo/redo history, and render-affecting but
    // deliberately treated as transient view state.
    bool        warp_dirty           = false;
    bool        phase_reset_dirty    = false;
    bool        settings_dirty       = false;
    bool        dirty                = false;

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
    // playback bind as GuiPlayback's domain offset, stamped at production time
    // by GuiTargetRender from the trim values the produced samples embody and
    // carried to the completion rebind.

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
    // commits, .settings file load, the BPM-sweep scale commit, and the
    // `'` render-commit (adopt_render_entry, a full engine-settings
    // adopt) all mutate fields of this struct directly. Carried by
    // RenderRequest at dispatch; serialized to .settings on Ctrl+S.
    // Default-constructed before any source load.
    EngineSettings engine_settings;

    // Live working copy of the active tab's trim state. The per-tab
    // backing store lives in ViewState::trim. Excluded from undo/redo.
    // Mirrored to/from the active tab's ViewState slot at the tab-swap
    // boundary in active_views.cpp (same pattern as viewport/zoom/playhead).
    // Trim is a region authored purely by the Alt pointer drags (single-bound
    // stem/chip, chip-row inter-chip pair), the bare-x set/clear, and the
    // Alt+wheel chip-row end-move — it is NOT part of the selection system (no
    // bound selection, no Tab stop, no Delete arm).
    TrimState trim;

    // Bottom-strip command prompt. Active only when a close / re-detect
    // gesture fires while a confirmation is required. Originally
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

    // Settings-prompt editor. Opens on `:`, accepts a single `key=value`
    // line, writes to engine_settings on commit. Lives in the bottom
    // strip; separate from top_flag_editor so the two paint regions stay
    // independent (the in-practice mutual exclusion comes from the flag
    // editor swallowing all keys while active).
    text_editor::State settings_editor;
    bool settings_editor_blink_last = false;

    // Render-commit prompt editor. Opens on bare `'` from an authoring view,
    // takes a render entry's identifier relative to renders/
    // (`<batch_dir>/<basename>` or a globally-unique bare basename), and on
    // Enter adopts that render's frozen sidecar recipe as the new authoring
    // baseline (GuiInputHandler::adopt_render_entry). A bottom-strip modal like
    // the settings editor; separate State so the two paint regions stay
    // independent.
    text_editor::State commit_editor;
    bool commit_editor_blink_last = false;

    // Tick backstop bookkeeping: last live-domain total observed by the
    // on_tick clamp (see main.cpp). 0 = not yet observed.
    int64_t last_tick_live_total = 0;

    // Archival render dispatch state. `queue_running` is true while any
    // archival render dispatch is in flight (the Ctrl+Alt+R one-shot and the
    // iteration/BPM sweep batch runs). The Esc handler checks it to scope the
    // cancel binding away from normal interaction. `queue_cancel_requested`
    // is set by Esc during a run and read between batch entries.
    bool queue_running           = false;
    bool queue_cancel_requested  = false;

    // Non-interactive bottom-strip status text, giving the user visual
    // feedback while no other UI is updating. Driven by the shared batch
    // runner (the iteration/BPM sweeps), startup loading, Ctrl+Alt+R, and
    // target-preview updates — not a manual queue. Empty means "no status —
    // render the timestamp normally." Mutually exclusive with prompt.active in
    // practice (these updates can't fire while a prompt is up).
    std::string queue_progress_text;

    // Transient one-line status message shown in the bottom strip after
    // the dirty dot, alongside the S/T·W/P·A/B indicators. Set by a
    // command that wants to report a non-fatal outcome (e.g. phase-reset
    // state-paste divergence); cleared on the next keyboard press in
    // on_key. Empty = nothing to show. General-purpose: not specific to
    // any one command, so future commands can reuse it.
    std::string transient_status_message;

    // One-slot pending archival render command. An archival dispatch
    // (Ctrl+Alt+R / the iteration or bpm sweep) that finds the
    // worker busy kills the running render (the Esc pair: request_cancel +
    // queue_cancel_requested) and parks its fully built request set here;
    // the worker-idle pump (GuiTargetRender::maybe_dispatch_pending, via
    // GuiInputHandler::dispatch_pending_archival_command) dispatches it once
    // the cooperative cancellation drains — never a synchronous wait on the
    // GUI thread. A newer command replaces an older parked one wholesale.
    // Esc during the drain disarms the slot (Esc means stop rendering, and
    // the parked command would otherwise resurrect a render right after the
    // cancel lands).
    struct PendingArchivalCommand {
        bool armed = false;
        // Ctrl+Alt+R shape (single): reqs holds exactly one request,
        // dispatched through dispatch_single_archival_render. Batches
        // (armed && !single) go through start_render_batch with
        // `batch_label`.
        bool                       single = false;
        std::vector<RenderRequest> reqs;
        std::string                batch_label;
        // A parked Ctrl+Alt+Shift+R command late-binds its output folder/cell
        // at the worker-idle pump, not at command time: a command-time scan can
        // be invalidated by the very render this command kills (that render
        // may still publish into renders/ during its cancellation drain,
        // after the scan but before the cancel flag lands, stealing the
        // scanned cell name). Set only by the Ctrl+Alt+Shift+R park site; the
        // pump allocates the folder/cell here. Because Esc disarming the slot
        // (the wholesale `pending_archival = {}` reset) clears this flag with
        // everything else, a parked-then-abandoned misc command creates no
        // folder at all — the allocation never runs.
        bool                       miscellaneous = false;
    };
    PendingArchivalCommand pending_archival;

    // Phase reset propagate (W-mode Ctrl+P / Ctrl+Alt+P). Single-slot
    // session-only clipboard cleared on app exit. `pending_paste_anchor`
    // is the destination warp-marker index captured when the paste
    // confirmation prompt opens; consumed by the prompt response.
    PhaseResetClipboard phase_reset_clipboard;
    int                pending_paste_anchor = -1;

    // Internal text clipboard (session-only, lost on app close). The GUI has
    // no outside-world clipboard: hover-copy (Ctrl+C over a hover popup) and
    // the bottom-strip editors' copy/cut/paste all round-trip through this
    // one string, so a hover value pastes into a flag/settings text box.
    std::string        text_clipboard;

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

    // One entry in the flat list of valid renders under
    // <source_parent>/renders/, produced by
    // GuiRendersDir::enumerate_render_entries. Just the three path fields;
    // a render entry's sidecar set (.warpmarkers / .phaseresetmarkers /
    // .settings) is written ONCE at queue/dispatch and never touched again.
    // Consumed by the `l` listen-to-renders launcher and the `'` commit
    // editor (adopt_render_entry).
    struct RenderEntry {
        std::filesystem::path batch_folder;     // <source_parent>/renders/<i>_<tag>
        std::string           basename;         // e.g. "01" (no extension)
        std::filesystem::path wav_path;         // batch_folder / (basename + ".wav")
    };
};

// Geometry helpers — definitions live at file scope in main.cpp. Declared
// here so viewport.cpp can call them.
int     strip_h(const AppState& a);
GuiRect waveform_area(const AppState& a);
GuiRect top_strip_area(const AppState& a);
GuiRect bottom_strip_area(const AppState& a);
// One shared row-rect helper for every strip row (see the layout contract at
// its definition in main.cpp). row_from_window_edge indexes from the strip's
// window edge, 0 = edge-most. The named row accessors below delegate to it.
GuiRect strip_row_rect(const AppState& a, bool top_strip,
                       int row_from_window_edge);
GuiRect top_zoom_row_area(const AppState& a);
GuiRect top_upper_row_area(const AppState& a);
GuiRect bottom_upper_row_area(const AppState& a);
GuiRect bottom_lower_row_area(const AppState& a);
GuiRect bottom_pan_row_area(const AppState& a);
int64_t samples_visible(const AppState& a, const GuiAudio& audio);
double  current_samples_per_pixel(const AppState& a, const GuiAudio& audio);
// The explicit-domain form of current_samples_per_pixel: spp at a zoom level
// against a caller-chosen domain total. The level fully determines spp
// (total-independent — the total/width param pair is unused now that the
// fit-file mode is gone, kept for the shared signature). For callers that need
// a domain OTHER than the active display context's (e.g. the dispatch-time
// snapshot clamping queue-moment view keys against a cell's own map domain).
double  samples_per_pixel_at(double zoom_level,
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

// The single query for "some pointer gesture is in flight" — an Alt marker
// drag, a trim drag, a strip-row zoom/pan drag, a playhead drag, or an editor
// text drag. Consumed by the wheel_context predicate (on_wheel's
// completed-detent gate and the platform's per-frame sub-detent
// accumulator probe both route through it), the gate that must never fire
// mid-gesture: the "nothing pops mid-gesture" boundary.
inline bool any_pointer_gesture_active(const AppState& app) {
    return app.drag.active || app.trim_drag.active ||
           app.strip_drag.active || app.playhead_drag.active ||
           app.editor_text_drag.active;
}

// SelectionSnapshot movers: capture the live marker selection at drag begin,
// restore it wholesale at cancellation. One place enumerates the fields, so a
// future selection field cannot be forgotten.
inline SelectionSnapshot capture_selection_snapshot(const AppState& app) {
    SelectionSnapshot s;
    s.selected_markers     = app.selected_markers;
    s.last_selected_marker = app.last_selected_marker;
    return s;
}

inline void restore_selection_snapshot(AppState& app,
                                       const SelectionSnapshot& s) {
    app.selected_markers     = s.selected_markers;
    app.last_selected_marker = s.last_selected_marker;
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
// "length of the timeline currently being viewed" — the viewport clamp and
// the effective zoom-out ceiling. Declared here so any TU touching the
// viewport math can reach it.
int64_t live_total_frames(const AppState& a, const GuiAudio& audio);

// Live-domain playhead clamp — the single spelling of the playhead domain
// ruling: the playhead rests in [0, total - 1] of its LIVE view's domain,
// everywhere, after any gesture. All authored positions — both marker columns
// and both trim bounds — wall at total - 1, so every sync onto one is in-domain
// by construction; the clamp is the load-lenient runtime guard for persisted
// scratch values, not a source-view authored-position concern. Every playhead write
// funnels through here: Viewport::move_playhead_to (the gesture route),
// and the non-gesture live-ization routes a persisted or stashed value
// takes into the live fields — the source load's tab snapshots, the
// Ctrl+Tab restore, and the render-entry adopt's tab bands — so an
// arbitrary non-negative persisted int64 (the settings schema is
// load-lenient on view scratch) rests in-domain BEFORE any translation
// arithmetic (the S/T toggle's double->int64 conversion, Alt+Space's
// launch offset) can consume it. The clamp reads live_total_frames — the
// active display context's domain total, source-frame total in source view
// and target-frame total (cached at `t`-toggle) in target view — so it
// always matches the domain the value will live in. Clamping IS the
// load-lenient ruling, never a refusal: a value already in [0, total - 1]
// passes through unchanged. An empty live domain (total <= 0 — unreachable
// once audio is loaded, zero-frame sources refuse) has no in-domain frame
// and clamps to 0.
inline int64_t clamp_playhead_to_live_domain(int64_t frame,
                                             const AppState& a,
                                             const GuiAudio& audio) {
    const int64_t total = live_total_frames(a, audio);
    if (total <= 0) return 0;
    if (frame < 0) return 0;
    if (frame >= total) return total - 1;
    return frame;
}

// The playhead position that landing on an authored frame produces: the
// authored source frame forward-mapped into the active (displayed) domain, then
// clamped into the playhead's [0, total - 1] live domain. Every equality or
// landing compare between a STORED AUTHORED position (a marker or trim bound)
// and the playhead MUST go through this image — the forward/inverse map pair is
// not a round trip on compressed target segments, so comparing raw or
// inverse-mapped frames is the recurring defect class this helper kills.
inline int64_t playhead_image_of_authored_frame(const AppState& a,
                                                const GuiAudio& audio,
                                                int64_t authored_frame) {
    return clamp_playhead_to_live_domain(source_frame_to_active_domain(
        a, audio, authored_frame), a, audio);
}

double  effective_max_zoom_level(int waveform_width_px,
                                 int64_t total_frames,
                                 int sample_rate);
std::pair<long long, long long> compute_trim_samples(
    const AppState& a, long long total_frames);
GuiRect timestamp_invalidate_rect(const AppState& a);
GuiRect playhead_invalidate_rect(const GuiRect& area, double px_x);
bool    rects_intersect(GuiRect a, GuiRect b);
GuiRect union_rect(GuiRect a, GuiRect b);

// Free-function form of GuiActiveViews::active_view_state(): the active A/B
// tab's view-state slot. The renderer / the b/e/u handlers don't have access
// to GuiActiveViews but need to reach the active tab's view-state from an
// AppState reference alone.
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
// hit_test_marker_line: scan the active list (phase resets in 'P' mode; warp
// markers otherwise) and return the index whose pixel column is within
// kMarkerHitHalfPx of `mouse_x`, or -1 if no marker line is within reach.
int hit_test_marker_line(const AppState& app, const GuiAudio& audio,
                         int mouse_x);

// hit_test_flag: scan the active chip rects in the top strip and return the
// marker index under (mouse_x, mouse_y), or -1. Rects cover every visible chip
// and may overlap; the walk resolves an overlap to the topmost-painted chip.
// Mirror of the painters' z-order (render_flags / render_phase_reset_flags plus
// the live editor flag). When a FlagPayload editor is active in W view its
// target flag paints LAST — above the whole stack, selected included
// (render_one_editor_flag runs after the cache blit) and sized to the
// variable-width PENDING text — so a pass ZERO resolves the editor target
// first: its rect wins if it contains the point, before the rest. Then the
// two-pass z-order over the remaining rects: the SELECTED chips paint above the
// unselected, and within each class the leftmost paints on top. So the walk
// runs twice — first the first-containing rect whose marker is selected, else
// the first-containing rect unconditionally (rects are emitted ascending-x, so
// each pass resolves to that class's leftmost = topmost). Priority overall:
// topmost = editor target > selected leftmost > leftmost. Deliberately visible
// to every consumer (selection clicks, Alt+drag grabs, editor caret clicks, the
// hover popup's target): the topmost-painted chip is what the user sees, so it
// is what a click grabs (WYSIWYG). Works in both 'W' and 'P' authoring views
// (each column's own chip list).
int hit_test_flag(const AppState& app, const GuiAudio& audio,
                  int mouse_x, int mouse_y);

// Which trim boundary, if any, a waveform-area click lands on.
enum class TrimHit { None, Begin, End };

// hit_test_trim_boundary: return which trim boundary's painted
// column is within kMarkerHitHalfPx of `mouse_x` AND lies within the visible
// strip, or None. Early-outs to None unless the FULL pair is set — the sole
// consumer routes here only then (a lone bound is gesture-inert), so both
// bounds are guaranteed present. AUTHORING views — the active tab's live pair.
// Visible columns only (mirroring hit_test_marker_line): a bound painted
// outside the strip is not a candidate; the halo governs reach around a visible
// column. Walks the same warp_frame_map as hit_test_marker_line in target view
// so the hit lands on the visually-drawn stem. Trim is project-level, and
// applies in both 'W' and 'P' views. When both bounds are within reach, the
// nearer wins. Consumed by the Alt trim routing's waveform single-drag arm.
TrimHit hit_test_trim_boundary(const AppState& app, const GuiAudio& audio,
                               int mouse_x);

// hit_test_trim_chip: like hit_test_trim_boundary, but tests the press against
// each bound's painted CHIP RECT in the upper top row rather than the stem
// column. Same both-bounds-required early-out as hit_test_trim_boundary (the
// sole consumer routes here only with the full pair set; a lone bound is
// gesture-inert). The chip glyph ("b"/"e") is drawn hl_pad to the right of the bound's
// column, so a column-only test misses clicks on the visible chip. The rect
// mirrors regular-flag hit geometry (flag_chip_rect, the shared chip-rect
// helper): x = round(text_left), w = round(glyph_advance +
// 2*flag_pad_x_px()) + 2*kChipOutlinePx (the outline ring is part of the rect),
// with y/h from the row metrics; the same rect the renderers fill, so paint
// and hit cannot drift. Tests both mouse_x and mouse_y. Used for upper-row
// presses; the stem elsewhere still routes through hit_test_trim_boundary.
TrimHit hit_test_trim_chip(const AppState& app, const GuiAudio& audio,
                           int mouse_x, int mouse_y);

// displayed_or_live_target_map: the warp_frame_map the item hit tests decide
// against — the map the aimed-at item pixels (marker/trim stems, flags, chips)
// were painted with on the LAST COMMITTED frame, so a grab lands on what is
// drawn (WYSIWYG grabs). In target view with a non-empty displayed map
// (app.displayed_target_warp_frame_map, promoted at the frame commit that blits
// the item caches — see the two-phase stage/promote at that member) it returns
// that map; otherwise the live display context's map (source view = the live
// context's identity/empty map, unchanged semantics; target-view cold = the
// live map until the first committed target frame).
//
// EVENT-SYNC RULING: hit DECISIONS read the committed frame's map, so hit
// geometry flips at the exact instant the on-screen items flip — the FRAME
// COMMIT that blits the stem/flag caches, not the offscreen item rebuild (which
// only stages) and not the earlier plate publish. Gesture MECHANICS — anchors,
// walls, motion translation, the release snap, the x-coincidence images — stay
// on the LIVE map: the display converges to live within the rebuild+commit
// interval, and once the pick is made everything downstream is uniformly live.
// Synchronizing by TIME (a delay) was rejected — the true lag varies through
// zero with worker load and refresh rate, so any constant would invert the skew
// in the common fast-commit case. The remaining seams are ALL accepted:
// (a) commit-to-scanout plus human reaction — irreducible for any GUI, since
// input is always a response to the previously PRESENTED frame; (b) the
// cold-state fallback (first paint / view toggle / just-after-load, live map
// until the first committed target frame); (c) the column-based
// playhead-placement clicks (out of scope by ruling — a far subtler seam).
const std::vector<WarpFrameMapSegment>&
displayed_or_live_target_map(const AppState& app, const GuiAudio& audio);

// Promoted from a lambda in main(). True iff the warp marker
// at `idx` is hover-popup-eligible — i.e. its rect doesn't already
// display a numeric tempo (pass markers and label_ref markers qualify;
// owning markers don't). Requires warp view with iteration mode off.
// Always false in phase reset view (no pass concept).
bool popup_eligible_marker(const AppState& app, int idx);
