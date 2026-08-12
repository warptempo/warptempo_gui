#include "input_handler.h"

#include "gui_display_context.h"
#include "warp_frame_map_view.h"
#include "marker_drag.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

// GuiInputHandler pointer-gesture handlers (on_button_press,
// on_button_release, on_motion) and the editor-text drag finalizer
// (finalize_editor_text_drag), lifted
// verbatim from input_handler.cpp. The methods are declared on
// GuiInputHandler in input_handler.h, and the platform layer's pointer
// callbacks dispatch to them unchanged, so this is a pure definition move.
//
// The file-local ActiveEditorText / active_editor_text /
// set_editor_caret_from_x helpers live here because the press / motion /
// release editor-text drag-select is the only consumer; press, motion, and
// release all resolve active_editor_text so they agree on the editor's text
// origin and which strip to repaint.
//
// apply_editor_clipboard is intentionally NOT here — it is a keyboard
// clipboard helper used only by on_key, and stays in input_handler.cpp.

// F2.1: mouse drag-to-select for the three text editors. The selection
// highlight is already painted from the editor State's selection_anchor /
// cursor_pos, so the whole gesture is input-side: a press sets the anchor
// and arms the drag, motion moves cursor_pos (extending the highlight),
// release finalizes.
//
// ONE CLICK-TO-BYTE MAPPING (row 7, 2026-08-01). Every editor in the product is
// PROPORTIONAL now, so there is no advance to divide by anywhere: each takes an
// origin plus the shaped run's per-byte boundaries from ITS OWN painter's
// publication — the flag editor's FlagEditorBox, the four bottom-strip
// editors' BottomEditorText — and click-to-byte is the same nearest-boundary
// search over both. The monospace arm (a char-0 origin times one cell advance)
// died with the face; ActiveEditorText carries the one pair.
namespace {

// monotonic_ms() (the press-driven CLOCK_MONOTONIC ms time base for double-click
// detection) is now the shared reader declared in app_state.h — one owner, no
// per-TU clock copy.

// THE PRESS CLAIM'S HALF OF THE BUTTON ROSTER (the roster itself is
// RedesignButton, app_state.h): the KEYBOARD CHORD each button on rows 1
// through 4 and row 8 fires. The
// painter's label/icon table (paint_handler.cpp) is the other half; both key off
// the same ids. TWO roster entries are absent (re-derived 2026-08-06 by walking
// RedesignButton against the table below): row 1's SETTINGS and NAVIGATION
// anchors, each of whose action is a POPUP TOGGLE — not a chord at all, since no
// keyboard chord opens or closes a dropdown. Both are spelled at their own claim.
//
// The `shift` column is each button's OWN chord (only Redo's is set). It is not
// the whole shift story: the two SHIFT-ADMITTING buttons OR a shift-exact press
// into this field to reach their twins, and which buttons those are lives at
// redesign_button_shift_admits (app_state.h), which the tooltip's own table is
    // static_asserted against.
struct ToolbarChord {
    RedesignButton id;
    GuiKey         key;
    bool           ctrl;
    bool           shift;
    bool           alt;
    // (WHICH BUTTONS ADMIT SHIFT is NOT a column here: it is
    // redesign_button_shift_admits in app_state.h, because the TOOLTIP's SHIFT
    // LINE must appear exactly where a shift press does something — a
    // static_assert beside that table enforces it. One fact, two readers.)
    //
    // RADIO: this button reports a state it can only ever turn ON, so a press
    // while it is already selected is a CONSUMED NOTHING (there is nothing to
    // switch to, and its chord is a TOGGLE that would switch away from what the
    // user just clicked). The tab pair and the two view pairs are radios; the
    // follow and iteration buttons are TOGGLES and press through in both
    // directions, which is why this is a flag and not `selected` alone.
    //
    // THE VIEW BAR'S THREE ARE RADIOS FOR A DIFFERENT REASON, worth stating
    // because the toggle argument does not transfer: their chords are the
    // ABSOLUTE selectors 1/2/3, which are IDEMPOTENT — on_key's own handler
    // already makes a press on the current combination a no-op, so dispatching
    // would be harmless rather than wrong. The flag is set anyway, and for the
    // FACE: the crops give a selected face and a click face and nothing that is
    // both, so consuming at the claim keeps the pressed interior from ever
    // painting over a lit button. One rule, two justifications.
    bool           radio;
    // CLICK_FACE: rows 2, 4 and 8 and the view bar show a pressed interior;
    // row 1's
    // two left-floating buttons and row 3 have two faces by scope and show
    // nothing new on a press.
    bool           click_face;
    // REPEATS: a held press on this button synthesizes key repeats (row 8's
    // four cardinal arrows and nothing else, 2026-08-11) — the pointer twin of
    // holding the key. The press dispatches once and arms
    // AppState::transport_repeat; the tick fires the same chord with
    // GuiInputState::synthesized_repeat set, so the undo coalescing is the
    // repeat-identity rule the keyboard already has. Defaulted so the thirty
    // rows that do not repeat need no ninth column.
    bool           repeats = false;
};

// THE PRESS CLAIM'S HALF OF THE BUTTON ROSTER — every CHORD-DISPATCHING button
// in the redesign, rows 1 through 4 and row 8, in one table. The flags above
// are the
// only axes the rows differ on, so they share one dispatch body
// (dispatch_redesign_chord) instead of accumulating a special case per row.
//
// ROW 1'S TWO MENU BUTTONS ARE THE ABSENTEES, and the membership changed hands
// twice: Quit joined the table when Ctrl+Q was recognised as its chord, Settings
// left it when its action became a DROPDOWN TOGGLE (a popup open/close is not a
// chord at all — the bare `;` keyboard route still opens the editor directly,
// untouched), and Navigation arrived a menu button 2026-08-02. Everything else
// on rows 1 through 4 and row 8 is here.
constexpr ToolbarChord kToolbarChords[] = {
    // Row 1. QUIT IS A CHORD — Ctrl+Q — and joined the table when the architect
    // corrected the "two-call sequence" framing (2026-07-31): on_key's own
    // Ctrl+Q route performs EXACTLY finalize_active_drags() then
    // prompt.request_close() when a gesture is live (the drag-modal gate's one
    // hatch), and prompt.request_close() alone otherwise — which is the same
    // thing, finalize_active_drags being a no-op with nothing active. So the
    // dispatch is behaviourally identical to the hand-spelled pair in every
    // state, and the pair is gone.
    {RedesignButton::Quit,       GuiKeys::Q,   true,  false, false, false, true},   // Ctrl+Q
    // Row 1's RIGHT FLOAT — the view bar (2026-08-02). Bare 1/2/3, the ABSOLUTE
    // view selectors: S+W, T+P, T+W. Everything the digits own arrives by
    // construction through on_key's own handler — the audio-first-then-markers
    // order, the refused-target-entry abort of the whole press, the coincidence
    // auto-select, the read-only admission (they are navigation), the modal
    // swallow. There is no second route to keep in step.
    {RedesignButton::ViewSW,     GuiKeys::Digit1, false, false, false, true, true}, // bare 1
    {RedesignButton::ViewTP,     GuiKeys::Digit2, false, false, false, true, true}, // bare 2
    {RedesignButton::ViewTW,     GuiKeys::Digit3, false, false, false, true, true}, // bare 3
    // Row 2 — the toolbar.
    {RedesignButton::Save,       GuiKeys::S,   true,  false, false, false, true},   // Ctrl+S
    {RedesignButton::Undo,       GuiKeys::Z,   true,  false, false, false, true},   // Ctrl+Z
    {RedesignButton::Redo,       GuiKeys::Z,   true,  true,  false, false, true},   // Ctrl+Shift+Z
    {RedesignButton::Render,     GuiKeys::R,   true,  false, true,  false, true},   // Ctrl+Alt+R (+Shift)
    // Row 3 — the tabs. Both halves carry the SAME chord: with two tabs the
    // toggle IS the direct select, and the radio flag is what makes a press on
    // the already-selected half a consumed nothing rather than a switch away.
    // (The row carried two MORE slots for one day, 2026-08-07..08, when it was
    // the (walk source, reading) product; they never dispatched — the `h`
    // view's band claim routes every press in that row to the switch owner —
    // and they went with the reading, which is row 4's own toggle now.)
    {RedesignButton::TabA,       GuiKeys::Tab, true,  false, false, true,  false},  // Ctrl+Tab
    {RedesignButton::TabB,       GuiKeys::Tab, true,  false, false, true,  false},  // Ctrl+Tab
    // Row 4 — the icon row. The four view buttons are radios on the same two
    // toggling chords the tabs' pair models; the rest are plain dispatches.
    {RedesignButton::IconS,      GuiKeys::T,   false, false, false, true,  true},   // bare t
    {RedesignButton::IconT,      GuiKeys::T,   false, false, false, true,  true},   // bare t
    {RedesignButton::IconW,      GuiKeys::P,   false, false, false, true,  true},   // bare p
    {RedesignButton::IconP,      GuiKeys::P,   false, false, false, true,  true},   // bare p
    // THE TRIM BUTTON (2026-08-11, the trim surface arc): bare `x`, set trim
    // from region — momentary, click face, not a radio. Every rule the key has
    // the button has: no region / degenerate result = the arm's own consumed
    // refusals, read-only-legal (trim is band), consumed in the `h` view (the
    // derived partition greys it there). Shift is NOT admitted — Shift+X the
    // maximizer stays keyboard-only — so a shift press is the standing
    // consumed nothing.
    {RedesignButton::IconTrim,   GuiKeys::X,   false, false, false, false, true},   // bare x
    // (THE ZOOM PAIR — bare `-` and bare `=` — sat here from 2026-08-01 until
    // 2026-08-02, when the architect ruled out duplicate commands on the GUI and
    // the Navigation dropdown became those two commands' one pointer home. The
    // keys are untouched; the buttons are deleted whole.)
    {RedesignButton::IconCopy,   GuiKeys::P,   true,  false, false, false, true},   // Ctrl+P
    {RedesignButton::IconPaste,  GuiKeys::P,   true,  false, true,  false, true},   // Ctrl+Alt+P (+Shift)
    // BPM'S KEY IS BARE `m`, NOT `b` — the brief expected `b` and the code says
    // otherwise (the arm is at handle_mode_keys, input_key_dispatch.cpp). The
    // button is its chord, so it takes the chord the keyboard actually has.
    {RedesignButton::IconBpm,    GuiKeys::M,   false, false, false, false, true},   // bare m
    {RedesignButton::IconIter,   GuiKeys::I,   false, false, false, false, true},   // bare i
    {RedesignButton::IconFollow, GuiKeys::F,   false, false, false, false, true},   // bare f
    {RedesignButton::IconListen, GuiKeys::L,   false, false, false, false, true},   // bare l
    {RedesignButton::IconLoadInPlace,
     GuiKeys::Apostrophe, false, false, false, false, true},  // bare '
    // THE HISTORY MODE (2026-08-04), the row's twelfth and the table's
    // twenty-second: bare `h`, a TOGGLE like follow and iteration — its chord
    // opens the mode and closes it, and the button dispatches on both edges
    // because the icon row's band claim sits ABOVE the mode's pointer gate (the
    // rows' presses are covered by the KEYBOARD gate instead, which admits `h`
    // through handle_history_mode_key one line before the allowlist).
    {RedesignButton::IconHistory, GuiKeys::H, false, false, false, false, true},     // bare h
    // THE CUMULATIVE READING'S TOGGLE (2026-08-08), the row's thirteenth and the
    // table's twenty-third: bare `u` flips the history view's delta between
    // ITERATIVE (off) and CUMULATIVE (on). A TOGGLE like follow, iteration and
    // the history button — the selected face reads the live bit its own chord
    // flips — and like the three entries below it, its key is bound ONLY inside
    // the view, so it rests disabled and dispatches nothing outside one.
    {RedesignButton::IconCumulative, GuiKeys::U, false, false, false, false, true},  // bare u
    // THE REVERT ACT (2026-08-05), the table's twenty-fourth: CTRL+H applies the
    // view's SELECTED diff flags backwards into the live state and closes the
    // view. Momentary like the two below — not a radio, not a toggle, click face
    // only. It is the one entry here whose chord is NOT claimed by the mode's
    // own vocabulary: it dispatches from on_key's ordinary body, BELOW the
    // read-only gate, so a locked tab refuses the click exactly as it refuses
    // the key (the load-in-place's precedent, `'`).
    {RedesignButton::IconRevert, GuiKeys::H, true, false, false, false, true},       // Ctrl+H
    // THE WALK'S TWO STEPS (2026-08-05), the table's twenty-fifth and
    // twenty-sixth: bare `,` steps OLDER and bare `.` NEWER, through the same
    // dispatch and therefore through handle_history_mode_key's own arm — walls
    // clamped as consumed no-ops there, exactly as the keys behave. Neither is
    // a radio and neither is a toggle: they are momentary steps, so both flags
    // read like copy's and paste's, and only the CLICK face is set. Outside the
    // view they never dispatch at all, their enabled bit being the mode
    // (redesign_button_enabled), which is the one thing that makes the pair
    // safe to leave in a table whose keys are otherwise always bound.
    {RedesignButton::IconHistoryOlder,
     GuiKeys::Comma,  false, false, false, false, true},                             // bare ,
    {RedesignButton::IconHistoryNewer,
     GuiKeys::Period, false, false, false, false, true},                             // bare .
    // Row 8 — the transport row (architect-ratified 2026-08-11, the touch
    // arc's first surface). Eight chords, every one BARE, every one already
    // bound: the row adds no semantics anywhere — each button is its key,
    // through this one table like the rest of the roster, so the keyboard-
    // modal editor gate, the history-mode allowlists, the read-only gate and
    // every refusal apply by construction, a refusal being a consumed no-op on
    // click exactly as on key. (A ninth chord — bare Escape, the row's Cancel
    // button — shipped and was deleted the same day with its button; the
    // mid-render Cancel is the RENDER button's stateful face now, spelled in
    // this body below.)
    //
    // PLAY AND STOP SHARE THE ONE Space BINDING — two buttons over one chord,
    // the state-mirrored pair whose enabled split (redesign_button_enabled)
    // makes exactly one live at a time; neither is a radio (`radio` would
    // consume a press on a SELECTED button, and this pair's split is on the
    // ENABLED bit, which the disabled-press consume above already reads).
    //
    // THE FOUR ARROWS SET `repeats` — the table's only four: a held press
    // synthesizes the key's own repeats (tick_transport_arrow_repeat below).
    // The transport four are one-shot by ruling.
    {RedesignButton::TransportSkipBack,
     GuiKeys::Home,   false, false, false, false, true},                             // bare Home
    {RedesignButton::TransportPlay,
     GuiKeys::Space,  false, false, false, false, true},                             // bare Space
    {RedesignButton::TransportStop,
     GuiKeys::Space,  false, false, false, false, true},                             // bare Space
    {RedesignButton::TransportSkipForward,
     GuiKeys::End,    false, false, false, false, true},                             // bare End
    {RedesignButton::TransportLeft,
     GuiKeys::Left,   false, false, false, false, true, true},                       // bare Left
    {RedesignButton::TransportDown,
     GuiKeys::Down,   false, false, false, false, true, true},                       // bare Down
    {RedesignButton::TransportUp,
     GuiKeys::Up,     false, false, false, false, true, true},                       // bare Up
    {RedesignButton::TransportRight,
     GuiKeys::Right,  false, false, false, false, true, true},                       // bare Right
};

// THE ARROWS' HOLD-REPEAT CADENCE — the labwc-matching defaults, mirroring the
// target laptop's own compositor keyboard-repeat values (575 ms delay, then 25
// repeats per second) so a held arrow BUTTON walks at exactly the speed a held
// arrow KEY does. Named constants rather than a compositor query on purpose:
// the repeat is ours (the compositor repeats keys, not buttons), and these are
// its authored defaults.
constexpr int64_t kArrowRepeatDelayMs    = 575;
constexpr int64_t kArrowRepeatIntervalMs = 40;   // 1000 / 25

// THE TABLE IS TOTAL OVER THE ROSTER, ENFORCED AT COMPILE TIME (2026-08-06):
// every RedesignButton but the two menu anchors carries a chord here, so the
// table's length plus those two IS the roster. The check is not bookkeeping —
// history_mode_disables_button walks this table and DEFAULTS AN UNLISTED BUTTON
// TO LIVE, so a roster entry added without its row here would silently wear a
// live face in the `h` view while its press claimed nothing. This makes that
// drift a build error instead.
static_assert(std::size(kToolbarChords) + 2 ==
                  static_cast<std::size_t>(kRedesignButtonCount),
              "kToolbarChords must cover every RedesignButton except the "
              "Settings and Navigation anchors");

// THE MODAL EDITORS' COMMAND-CHORD ADMISSION, read without the act — the
// modal-trap fix's membership test (architect 2026-08-11, from the road: an
// accidental settings editor on GLASS, with no physical keyboard, was an
// EXIT-LESS STATE — the Quit button did nothing). The five editors' one modal
// contract admits exactly the editor's own keys plus bare Esc, Ctrl+S and
// Ctrl+Q (route_modal_editor_key / modal_editor_key_blocked,
// input_key_dispatch.cpp); this predicate is that contract's COMMAND tail —
// the chords that are commands rather than editor keys — spelled once so the
// roster's modal press claim can DERIVE its membership from the admission
// instead of hand-listing buttons. The two must move together: a chord added
// to the editors' admission belongs here in the same edit. Bare Esc is in the
// set because the contract admits it (the cancel); no roster button carries it
// today (row 8's Esc button died the day it shipped), so the Esc arm is the
// derivation's completeness, not a live consumer.
bool modal_editor_admits_command_chord(GuiKey key, bool ctrl, bool shift,
                                       bool alt) {
    if (key == GuiKeys::Escape && !ctrl && !shift && !alt) return true;
    if (key == GuiKeys::S && ctrl && !shift && !alt) return true;
    if (key == GuiKeys::Q && ctrl && !shift && !alt) return true;
    return false;
}

// Is (x, y) inside the PAINTED rect of a redesigned button? The rect is the
// painter's stash and nothing here re-shapes or re-measures, so the clickable
// region is exactly the drawn one. A zero rect (before that row's first paint)
// contains no point, which is the correct cold answer.
bool redesign_button_hit(const AppState& app, RedesignButton id, int x, int y) {
    return rect_contains(
        app.redesign_buttons[redesign_button_index(id)].rect, x, y);
}

// THE WAVEFORM'S HALF SPLIT, and the ONE expression of it. The plain left press
// splits by half — upper = the placement press, lower = the audition scrub — and
// since 2026-08-03 the pointer CURSOR marks that lower half too (the Scrub cue,
// pointer_cursor_kind), so the boundary has two readers and must have one owner:
// a painted-nothing boundary and a cursor boundary that could drift by a pixel
// would be worse than either alone.
//
// It is the same arithmetic the retired 1px channel-split line was drawn on
// (that line went 2026-08-03; the split it marked did not) — integer division,
// so an odd-height area gives the lower half the extra row, which is what the
// press has always done.
bool waveform_lower_half(const GuiRect& area, int y) {
    return y >= area.y + area.h / 2;
}

// Active-domain playhead frame at click column `col`. SOURCE view: the exact
// source grid (source_grid_position_at_column via painter q), matching marker
// commits so a drop-at-playhead lands where a drag/nudge would. TARGET view:
// the domain spp form — the source-frame commit routes through the inverse map,
// so there is no source-grid claim there.
int64_t playhead_frame_at_click_column(const AppState& app,
                                       const GuiAudio& audio, int col) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) {
        const double q = painter_samples_per_pixel(app, audio, waveform_area(app));
        if (q > 0.0)
            return static_cast<int64_t>(std::nearbyint(
                source_grid_position_at_column(app.viewport_start_sample, col, q)));
    }
    const double spp = current_samples_per_pixel(app, audio);
    return app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(static_cast<double>(col) * spp));
}

// The active editor's resolved text geometry, valid only while exactly one
// editor is active (and, for the flag editor, on-view). Press / motion /
// release all resolve this so they agree on origin and which strip to
// repaint.
struct ActiveEditorText {
    bool                valid        = false;
    text_editor::State* ed           = nullptr;  // the active editor
    double              text_left    = 0.0;       // byte-0 origin (px)
    // The painter's per-byte pen offsets for that editor's own shaped run.
    // Never null on a valid resolution — every editor is shaped since row 7.
    const std::vector<double>* byte_x = nullptr;
    bool                bottom_strip = false;      // which strip to repaint
};

ActiveEditorText active_editor_text(AppState& app, const GuiAudio& audio) {
    (void)audio;
    ActiveEditorText g;
    // THE FOUR BOTTOM-STRIP EDITORS share ONE publication — only one of them is
    // ever open, and paint_bottom_strip fills it from whichever branch actually
    // painted. An invalid publication (nothing painted yet, or an editor the
    // row's precedence chain is hiding) leaves this invalid, exactly as the flag
    // editor's does: what is not on screen takes no clicks.
    const AppState::BottomEditorText& be = app.bottom_editor_text;
    const bool bottom_open =
        text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.load_editor) ||
        text_editor::is_active(app.commit_title_editor) ||
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
    if (bottom_open) {
        if (!be.valid) return g;
        g.ed = text_editor::is_active(app.settings_editor)
                   ? &app.settings_editor
             : text_editor::is_active(app.load_editor)
                   ? &app.load_editor
             : text_editor::is_active(app.commit_title_editor)
                   ? &app.commit_title_editor
                   : &app.top_flag_editor;
        g.text_left    = be.text_origin_x;
        g.byte_x       = &be.byte_x;
        g.bottom_strip = true;
        g.valid        = true;
        return g;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // FlagPayload — the UNROLLED FLAG BOX. Its geometry is the painter's,
        // published at app.flag_editor_box (contract at FlagEditorBox,
        // render.h): the origin already carries the view offset and the
        // boundaries are the shaped run's own pen, so there is nothing to
        // re-derive and nothing that could disagree with the pixels. An invalid
        // publication (no box painted yet, or a target the store shrank past)
        // simply leaves this invalid — the same answer the old -1 origin gave.
        const FlagEditorBox& fb = app.flag_editor_box;
        if (!fb.valid) return g;
        g.ed        = &app.top_flag_editor;
        g.text_left = fb.text_origin_x;
        g.byte_x    = &fb.byte_x;
        g.valid     = true;
        return g;
    }
    return g;
}

// The ONE click-x -> byte owner (see ActiveEditorText): a nearest-boundary
// search over the painter's published per-byte pen offsets. Every caret and
// drag-select site funnels through it, and since row 7 there is one family of
// editors rather than two, so there is nothing left for them to drift apart on.
int editor_byte_index_at(const ActiveEditorText& g, int mouse_x) {
    return text_editor::byte_index_from_shaped_x(
        static_cast<double>(mouse_x), g.text_left, *g.byte_x);
}

void set_editor_caret_from_x(const ActiveEditorText& g, int mouse_x) {
    g.ed->cursor_pos = editor_byte_index_at(g, mouse_x);
}

// Region-drag end: dissolve a resting region whose on-screen span is under the
// arm gate. The press-becomes-drag gate (kDragMovedThresholdPx) latches once
// and never re-engages, so a hand-jitter drag that crosses the gate then
// releases near the press — or wanders back toward it — can rest a sliver
// region a pixel or two wide. That was never an intentional window: a
// sub-threshold rest reads as a click, so it dissolves exactly as a plain
// click's would, clearing the highlight (the recolored ground goes, which the
// same damage covers). The
// resting-region minimum floor SCALES with the gate (deliberate): reading
// kDragMovedThresholdPx here means the sliver floor rose to 8px when the
// architect unified the gate 2026-07-24, so the smallest region that can rest
// tracks the smallest press that becomes a drag. Called
// from both region-drag end points (release and button-lost). Only the REST is
// gated — the live mid-drag extension paints slivers freely. Under
// SELECTION-FLOWS-DOWNWARD-ONLY (architect 2026-07-23) the drag never touches
// the selection: the press's deselect/drop was the committed act, and the
// drag holds no selection to clear — so this dissolve drops the region alone.
// IT DROPS NO PLAYHEAD EITHER: the drag carried the cursor to its last column
// (architect 2026-07-30) and a sliver release leaves it there, the same
// what-stands-stands rule the gesture family holds everywhere.
void end_region_drag_min_size_check(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport) {
    if (!app.region.active) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;   // no geometry -> leave the region as-is
    const double span_px =
        std::abs(static_cast<double>(app.region.a_frame - app.region.b_frame)) /
        spp;
    if (span_px < kDragMovedThresholdPx) {
        app.region = RegionState{};
        viewport.invalidate_waveform_area();
    }
}

} // namespace

// THE `h` HISTORY VIEW'S DEAD SET — the roster's ONE mode-scoped disabled-face
// partition, and THE AUTHORITATIVE INVENTORY of it (every other site carries a
// pointer here). The architect's principle, 2026-08-04: while the view stands,
// EVERY BUTTON WHOSE ACT THE VIEW CONSUMES WEARS ITS ROW'S DISABLED FACE and
// ignores the pointer, and the ones that stay lit are exactly the ones that
// still work. It is the mode-scoped exception to the icon row's never-grey rule
// and to rows 1 and 3 having no disabled face, on the view bar's own precedent
// (a whole surface wearing a state that is not the enabled bit).
//
// IT IS DERIVED, NOT LISTED. Each roster button but two IS a chord
// (dispatch_redesign_chord synthesizes it and calls on_key), so "the view
// consumes this button's act" is exactly "the view's keyboard gate consumes this
// button's chord" — this walks kToolbarChords and asks that gate. So the faces
// cannot drift from the allowlist: admit a chord there and its button lights on
// the next frame with nothing to remember here.
//
// THE SETTINGS ANCHOR IS THE HAND ENTRY — one entry, not two, since 2026-08-08.
// The two anchors are the roster's only NON-chord actions, so there is no chord
// to ask the gate about and each has to be answered here; what changed is the
// answer for one of them. SETTINGS stays DEAD because toggle_dropdown still
// refuses that menu while the mode stands (its first line): its six items open
// the settings editor, a modal the view has no place for. NAVIGATION is LIVE
// because the architect ruled its menu open in the view — the toggle no longer
// refuses it — and every one of its seven rows is a chord that meets the mode's
// own gates through on_key, so nothing about it is a hand answer beyond this
// function's silence on it. The one row the view consumes greys at the ITEM
// (dropdown_item_enabled, app_state.h), a surface this partition does not reach:
// it answers about BUTTONS, and a menu row is not one.
//
// THE TWO TABS NEEDED A SECOND HAND ENTRY FOR ONE DAY AND NO LONGER DO
// (2026-08-05). While it stands row 3 is the WALK SELECTOR — Remote and Local,
// one axis since 2026-08-08 — with a mode-local press route at the tab row's own
// band claim; the pair shipped with no hotkey at all, so their chord was
// consumed while their buttons were live and only a hand entry could say so.
// Ctrl+Tab BECAME the cycle later that day, claimed by the mode's own vocabulary
// (history_mode_owns_key), so the derivation now answers LIVE for them on its
// own and the exception is gone. Ctrl+Shift+Tab joined it as the REVERSE cycle
// on 2026-08-07 and changes nothing here — no roster entry carries a shifted Tab
// — and the lock slots still go with the tabs' old meaning (the painter draws no
// padlock and publishes no lock rect in the mode).
//
// THE MODE'S OWN KEYS ARE ASKED FIRST, and that is not a detail: the allowlist
// never sees the mode's own vocabulary — handle_history_mode_key consumes it one
// line above — so asking the allowlist alone would call bare `h` blocked and
// grey out THE VERY BUTTON THAT LEAVES THE VIEW. history_mode_owns_key is that
// line's own predicate, shared rather than re-spelled, and its membership is
// re-derived at its own definition.
// THE PARTITION DID NOT MOVE WHEN THAT VOCABULARY GREW, any of the three times
// (2026-08-05 — first the diff-flag cycle, Home/End and `c`, then the compare
// toggle; 2026-08-08 — bare `u`, the reading's own toggle). None of the bare
// shapes is a roster chord EXCEPT `u` (nothing in kToolbarChords dispatches bare
// Tab, Home, End or `c`), the ONE older roster chord — the tabs' Ctrl+Tab — was
// already answered LIVE by the hand entry the claim replaced, and `u` needs no
// entry either: it IS a roster chord, so this walk finds it, and the mode owning
// it is exactly what makes the walk answer LIVE — the same derivation the walk's
// two arrows and Revert already ride.
//
// THREE ENTRIES ARE A FUNCTION OF THE SESSION, not of the chord alone
// (2026-08-05 for the first two, 2026-08-08 for the third), which is why this
// takes the state rather than only a button. The
// allowlist admits Ctrl+S — the checkpoint act's chord since 2026-08-08 — only
// while there is something to checkpoint AND no
// checkpoint is in flight, so the Save-and-Commit-faced SAVE button GREYS when
// the session's authoring content already matches the newest checkpoint
// (AppState::HistoryMode::head_delta_empty) and again while the worker is
// publishing one (AppState::history_checkpoint_in_flight, 2026-08-07 — the
// second bit is why this now takes the whole AppState rather than the mode
// struct); and it admits CTRL+H only while a diff flag is selected, so the
// REVERT button greys with an empty subject; and it admits bare `'` only while
// THE ACTIVE WALK CARRIES A MEMBER — one term for both walks since 2026-08-09,
// when the empty Remote walk became a legal standing state — so the
// load-in-place button greys over a blank lane, which the Remote tab reaches
// whenever a piece has no eligible checkpoint and a live Local tab cannot reach
// at all. The derivation carries all of it
// for free — this function restates no term of any of them. They differ in
// cadence and that is the honest difference: the head delta is measured once
// (at the entry, or at the drain that answers it) and is static for the visit,
// while the active walk's count grows as the prefetch streams, the revert
// subject moves with every click and the in-flight bit falls when the worker
// reports.
//
// THE PARTITION THIS PRODUCES, in full (verified against the roster both ways,
// 2026-08-04, re-verified 2026-08-05):
//   LIVE — Quit (Ctrl+Q, admitted), the view bar's ViewSW/ViewTP/ViewTW (bare
//   1/2/3, the admitted view selectors), Save (Ctrl+S, which in this mode IS the
//   save-and-commit checkpoint act and wears the "Save and Commit" face — LIVE
//   ONLY WITH A NON-EMPTY HEAD DELTA AND NO CHECKPOINT IN FLIGHT, and greyed
//   rather than relabelled in either case; it was RENDER's chord and RENDER's
//   face until 2026-08-08, when the act moved onto the save it begins with),
//   the icon row's S/T + W/P radios (bare `t` / `p`, admitted with the view
//   switches), the load-editor opener (bare `'`, which in this mode loads THE
//   VIEWED WALK'S MEMBER in place — the commit's sidecars on the Remote tab,
//   the timeline state on the Local one since 2026-08-08, and live on both:
//   the THIRD session-dependent entry, dead on a walk with no member — the
//   Remote tab's empty one, since 2026-08-09), and the history button itself
//   (bare `h`, the
//   mode's own key,
//   selected while it stands),
//   and BOTH TABS since 2026-08-05 — live as the WALK SELECTOR rather than
//   as tabs (Ctrl+Tab, the mode's own cycle, so they come out of the walk
//   like any other admitted chord), with their padlocks not drawn at all (the
//   mode's tabs are not tabs, so there is no lock state to show and no lock rect
//   published for bare `o` to be refused through),
//   and THE WALK'S TWO STEPS since 2026-08-05 — older (bare `,`) and newer
//   (bare `.`), the mode's own vocabulary again, so this walk answers LIVE for
//   them with nothing hand-listed,
//   and THE CUMULATIVE TOGGLE since 2026-08-08 (bare `u`, the same vocabulary
//   and the same free answer). Those three plus Revert are the roster's FOUR
//   RESTING-DISABLED buttons, which is the OTHER predicate's fact rather than
//   this one's: their keys are bound nowhere outside the view, so
//   redesign_button_enabled greys them there and this function is what says they
//   act in here. The arrows never grey at a walk WALL either — a step past the
//   oldest or newest member is a consumed no-op, which is the same nothing every
//   other refusal in this partition is.
//   and THE REVERT ACT since 2026-08-05 (Ctrl+H) — the SECOND session-dependent
//   entry, and the only button that is resting-disabled AND conditionally grey
//   inside the view: the allowlist admits its chord only while a diff flag is
//   selected (history_mode_revert_subject_standing), so this walk answers DEAD
//   with an empty subject and LIVE the moment a click selects one. Both facts
//   come from the same admission with nothing restated here, exactly as
//   Save's head-delta grey does.
//   and THE NAVIGATION ANCHOR since 2026-08-08 (architect) — the SECOND of the
//   two hand entries, flipped: its menu opens in the view and its commands act
//   there, so a dead face would be a lie about a working button. It is the one
//   LIVE entry that is not a chord's admission, which is why it is spelled in the
//   body rather than derived.
//   DEAD — Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z); RENDER since 2026-08-08
//   (Ctrl+Alt+R, which left the allowlist with its shifted twin when the act
//   moved onto Ctrl+S — so the button wears its ordinary Render face over this
//   partition's dead one, and the walk says so with nothing hand-listed);
//   copy phase (Ctrl+P), paste
//   phase (Ctrl+Alt+P), the BPM
//   opener (bare `m`), iteration mode (bare `i`), follow (bare `f`), listen
//   (bare `l`); and the SETTINGS anchor — alone here since 2026-08-08, when
//   NAVIGATION moved to the LIVE column above with its menu.
//
// TWO THINGS IT DELIBERATELY DOES NOT SAY. (1) The base chord decides the face,
// which since 2026-08-08 has nothing left to arbitrate on row 2: the ONE button
// whose shifted twin the mode consumed while its base chord stood — Render —
// is dead on both shapes now, and Save admits no shift press at all. (Save's
// own base chord is what this walk asks about, its shift column being false in
// the table and in redesign_button_shift_admits alike.) (2) A
// button the READ-ONLY tab bit refuses is not this function's business: that
// refusal is the lock's, it applies inside the view exactly as outside it, and
// row 4's never-grey rule still answers for it (the `'` button stays lit on a
// locked tab, in the view as out of it). Only the VIEW's own consumption greys
// anything here.
bool history_mode_disables_button(const AppState& app, RedesignButton b) {
    if (b == RedesignButton::Settings) return true;
    if (b == RedesignButton::Navigation) return false;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (tc.id != b) continue;
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift;
        chord.alt   = tc.alt;
        if (history_mode_owns_key(tc.key, chord)) return false;
        return history_mode_key_blocked(tc.key, chord, app);
    }
    // Not in the table and not an anchor: nothing to consume. Unreachable today
    // (the table plus the two anchors is the whole roster) and stated rather
    // than asserted, so a future button defaults to LIVE — the face it already
    // had — instead of greying on a chord nobody has written yet.
    return false;
}

// THE MARKER LANE OWNS THE PLAYHEAD (architect 2026-07-28) — the rule this
// helper serves, stated ONCE here; the other landing sites carry only their own
// class plus a pointer back to this comment. With a non-empty selection the bare
// horizontal arrows move the FOCUSED MARKER rather than the playhead column (the
// lane model, GuiInputHandler::playhead_in_marker_lane), so:
//   * any route that changes WHICH marker is focused while the lane is active
//     LANDS the playhead on the new focus — otherwise the arrows would move a
//     marker while playhead_cursor_sample rested somewhere else entirely, and
//     Space would play from that stale spot;
//   * any route that LEAVES the lane (empties the selection) leaves the playhead
//     exactly where it is — Home/End clear rather than land for precisely that
//     reason.
// THE CURSOR PLAYHEAD IS ALWAYS PAINTED (architect 2026-07-30), which is what
// makes the rule legible rather than merely correct: the land is VISIBLE, so a
// nudge shows the cursor riding the marker it moves. (The rule used to be argued
// from a SUPPRESSION — the cursor stopped painting under a selection and the
// focused flag's ink triangle "was" the playhead — and that argument is retired
// with the suppression itself. The behaviour it justified is unchanged.)
// A LAND IS ALWAYS ON THE FOCUS (architect 2026-07-28, closing the rule): the
// three MULTI-SELECT clicks used to land at the earliest selected while focusing
// elsewhere, and no longer do — the shift-range click lands on the clicked range
// END, and both ctrl-toggle arms land on whichever marker the toggle leaves
// focused. So there is no such thing as a focus set whose land went somewhere
// else: the "towed" category is empty, and the nudge/drag follow tows nothing.
// The landing sites are enumerated below (they are this function's callers, plus
// the image-moving gestures that re-land through move_playhead_to at their own
// commits: the two position nudges, and both arms of the Up/Down cent step in
// their target-view re-warp tails).
//
// THE LAND IS A PURE PLAYHEAD WRITE: it has no region side effect whatsoever.
// The REGION is TRIM SCRATCH (its contract is at RegionState, app_state.h) and
// every command that moves the playhead or replaces the selection clears it at
// its OWN site, unconditionally — never gated on whether the playhead actually
// moved. The one authoritative clear-site enumeration lives at
// clear_region_highlight (input_handler.h); do not restate it here or anywhere
// else. What is worth stating at the land is only this: the clear is never a land
// side effect, and the land never decides it.
//
// LANDS the playhead exactly onto marker `hit` (active column's store), with
// NO viewport move — the sole difference from Tab (which recenters) and `c`
// (which re-zooms and recenters), so the view holds perfectly still while the
// playhead seats. THE CALLER INVENTORY, THE ONE AUTHORITATIVE ENUMERATION,
// re-derived by grep 2026-07-29 (other sites state their own class and point
// here; do not copy this list, re-derive it):
//   * THE POINTER CLICKS — the plain marker click (input_pointer.cpp), the
//     shift RANGE click, and the ctrl TOGGLE click. Each lands on the FOCUS its
//     own path just set: the clicked marker, the clicked range end, the
//     toggled-in marker, or the focus repaired after a toggle-out (an empty
//     post-toggle selection lands nothing). The plain click's FOUR deferred
//     completions left this list 2026-07-29 with the group drag: every marker
//     press single-selects and lands at PRESS time now (horizontal movement is a
//     focus act — the doctrine at the head of position_nudge.h);
//   * THE KEYBOARD FOCUS-COLLAPSE COMMANDS, which collapse a 2+ selection to its
//     focus and land there — the Ctrl+N inherit toggle (warpmarkers_ops.cpp) and,
//     since 2026-07-29, BOTH POSITION NUDGES through their shared prologue
//     (position_nudge.cpp). Esc's singleton rung was in this class until
//     2026-07-29, when the whole Esc ladder was deleted: bare Esc lands nothing
//     now, because it does nothing at all outside the editors, the prompts, the
//     drag swallow and the render cancel;
//   * EVERY TEXT-EDITOR OPEN — flag_editor.cpp's enter_text_edit, the one
//     chokepoint of the three open routes;
//   * THE RESTORES, which hand the lane a focus it did not have: BOTH undo/redo
//     marker arms (undo.cpp's visual tail — the singleton lands on its touched
//     marker, the group on its focus, which IS the earliest touched member) and
//     the propagate paste's CREATED-SET arm, on the FIRST created reset
//     (phase_reset_propagate.cpp — its no-created arm lands nothing now, and the
//     `p` swap lands nothing either: both used to be cleaning up a restored
//     P-column selection, and the parked slots died 2026-07-29);
//   * THE VIEW / TAB SWITCHES, which re-express a focus into a new domain: the
//     S<->T flip (input_handler.cpp), landing only on a
//     NON-EMPTY selection — with no lane the cursor is the playhead in its own
//     right and keeps its own value. The flip CARRIES a 2+ selection across
//     since 2026-07-30 (its collapse died with the SPAN FORM), so the selection
//     here may be a GROUP, and the land seats the cursor on its focus exactly as
//     it does for a singleton. THE `h` HISTORY VIEW'S EXIT RESTORE joined this
//     class 2026-08-08 (close_history_mode, input_key_dispatch.cpp): its
//     restore re-spells the flip's column-preserving translation whenever the
//     visit left the entry audio view, so it re-spells the flip's land too, on
//     the same non-empty-selection gate and in that TRANSLATION ARM ALONE — an
//     unflipped visit restores the parked cursor bit-exact and has no round trip
//     to repair. Ctrl+Tab left this class
//     when the parked
//     selections died: it restores its tab's stored cursor VERBATIM, hands the
//     lane nothing, and its only land is the auto-select's below;
//   * THE COINCIDENCE AUTO-SELECT (auto_select_marker_at_playhead, this file) at
//     its entry chokepoints (the inventory is at its declaration,
//     input_handler.h). A provable NO-OP by
//     construction (its selection predicate IS this function's equality test), and
//     it is in the list because the adjacency is the rule, not because it moves
//     anything;
//   * (THE MAP CHANGERS ARE GONE FROM THIS LIST, architect 2026-07-29:
//     the settings engine-commit and the settings-only 'S' undo/redo arm each
//     landed here, target view only, because the rebuilt map moved the focused
//     marker's image out from under a resting cursor — and both now CLEAR THE
//     SELECTION at their own tails instead, so there is no lane and no focus left
//     to re-land. The map-change re-land SHAPE survives in exactly one place, the
//     Up/Down cent step's target-view tail, which re-lands through move_playhead_to
//     rather than here because it wants the keep-visible scroll.)
// The two-step placement
// basis the Tab family lands with (source_frame_to_active_domain then
// clamp_playhead_to_live_domain), against the active column's store, so the
// placement is exactly coincident for a subsequent nudge/drag ride. Direct
// cursor write mirroring jump_playhead_to_focused_marker's non-recenter part —
// NOT move_playhead_to, whose keep-visible edge-align could scroll for a
// half-offscreen flag, and the ruling is NO viewport write of any kind (the
// playhead may rest at a slightly offscreen column when the clicked flag hung
// half off the edge — accepted). Read-only allowed (selection + playhead are
// navigation). Some callers stop playback first (each marker click owns that
// stop at its own site, Tab-family symmetry; the restores and the bpm-editor
// open stop too); the others — the bare-Return flag-editor open,
// the `p` swap, the Ctrl+N collapse — may land DURING playback, safe because the
// land is a direct RESTING-cursor write and a live scanner is untouched by
// cursor writes (move_playhead_to's scanner-inactive
// convention). External linkage (declared in input_handler.h) so undo.cpp and
// the ops/views TUs can reach it.
// THE STORE LOOKUP IS ALL THIS FUNCTION ADDS to the one below it. Resolving the
// index to an authored frame is the marker-shaped half; the two-step placement
// basis and the damage are the frame-shaped half, hoisted into
// land_playhead_on_source_frame so a caller holding a frame that belongs to NO
// store entry — the `h` history mode's diff flags, whose removed lines exist in
// no store at all — lands through the identical expression instead of a second
// copy of it. Everything the list above says about WHEN a land happens and what
// it must not touch governs both halves alike.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit) {
    int64_t src_frame = 0;
    bool valid = true;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phaseresetmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(tv.size())) valid = false;
        else src_frame = tv[hit].time_frame;
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(mv.size())) valid = false;
        else src_frame = mv[hit].time_frame;
    }
    if (valid) land_playhead_on_source_frame(app, audio, viewport, src_frame);
}

// The frame-shaped half, above. Its own two decisions:
void land_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                   Viewport& viewport, int64_t src_frame) {
    int64_t sample = source_frame_to_active_domain(app, audio, src_frame);
    sample = clamp_playhead_to_live_domain(sample, app, audio);
    // IDEMPOTENCE ONLY, carrying no semantics: a land onto the sample the
    // playhead already holds writes the same value and moves no pixel, so
    // there is nothing to damage. It decides nothing about the region — the
    // caller's own clear (when the caller is a point command) runs either
    // way, before or after this call. Compared AFTER the clamp, because the
    // clamp is what decides where the land actually seats.
    if (sample == app.playhead_cursor_sample) return;
    app.playhead_cursor_sample = sample;
    // FULL WAVEFORM-AREA DAMAGE (architect 2026-07-30, replacing the narrow
    // old/new column pair computed on the LIVE viewport): the cursor's
    // pixels are PLATE-registered, and this free helper takes no
    // GuiPaintHandler, so it widens rather than adding one — a land is a
    // discrete command and a full-area invalidate cannot ride the wrong
    // epoch. Rule and per-site shape table at playhead_pixel_x
    // (app_state.h).
    viewport.invalidate_waveform_area();
    viewport.invalidate_clock_area();
}

// COINCIDENCE AUTO-SELECT (architect 2026-07-29, the entry half of THE SELECTION
// IS NEVER PARKED). Nothing stashes a selection any more, so a route that ENTERS
// a column or a tab re-acquires one from the PLAYHEAD: scan the active column's
// store and single-select the marker the playhead is already sitting exactly on.
// The payoff is the lane model — with that marker selected, the brightened flag
// and the cursor standing on it say the same thing, which is precisely the
// state the entry's stored cursor
// describes — and the price is nothing, because a selection recovered this way is
// derived from live data instead of remembered from a store that has since moved.
//
// THE TEST IS THE LAND'S OWN FORMULA, EXACTLY: clamp_playhead_to_live_domain(
// source_frame_to_active_domain(time_frame)) == app.playhead_cursor_sample,
// reusing land_playhead_on_marker's two helpers rather than re-deriving the
// mapping (a second spelling of the same conversion would drift). It is an EXACT
// int64 compare with no tolerance: the auto-select must fire only where a land
// would leave the playhead unmoved, and the whole point of the two-step basis is
// that a landed playhead is bit-exactly a marker's image. So this reads "is the
// playhead standing on a marker", not "is it near one" — a cursor one frame away
// selects nothing, which is right, since selecting it would move nothing while
// silently arming the marker lane.
// FIRST-IN-STORE WINS on a tie: markers may coincide exactly (legal in both
// stores) and in target view distinct source frames can share one target image
// under a compressing segment, so the scan is ordered and stops at the first
// match — an arbitrary but total rule, and the stores are time-ordered, so
// "first" is the earliest-authored of the coincident group.
// THE LAND AFTERWARD IS THE ORDINARY ADJACENCY, and it is a provable no-op here:
// the predicate that selected the marker is the land's own equality test, so the
// land early-returns on `sample == app.playhead_cursor_sample` and writes nothing.
// It stays because the marker lane owns the playhead — a route that hands the lane
// a focus pays the land, and this route paying it in the degenerate case is what
// keeps the rule exceptionless.
// NO REGION WORK: every caller has already cleared any resting scratch span
// before reaching here, which is the whole of the argument (the "a region rests
// only beside an EMPTY selection anyway" belt holds too — the `h` view's spans
// are view-local — but it was never load-bearing here).
// Read-only allowed (selection and playhead are navigation). Bounds-safe by
// construction — the index comes from the scan itself.
void auto_select_marker_at_playhead(AppState& app, const GuiAudio& audio,
                                    Selection& selection, Viewport& viewport) {
    const auto scan = [&](const auto& markers) {
        for (size_t i = 0; i < markers.size(); ++i) {
            const int64_t sample = clamp_playhead_to_live_domain(
                source_frame_to_active_domain(app, audio, markers[i].time_frame),
                app, audio);
            if (sample == app.playhead_cursor_sample) return static_cast<int>(i);
        }
        return -1;
    };
    const int hit = (app.active_markers_view == 'P')
        ? scan(app.phaseresetmarkers.markers())
        : scan(app.warpmarkers.markers());
    if (hit < 0) return;
    selection.set_single_selection(hit);
    land_playhead_on_marker(app, audio, viewport, hit);
}

// One scrub ACT: STOP, THEN START ON THE NEXT CLICK (architect 2026-07-27,
// superseding the kill-and-revive of 2026-07-23). A click on a scrub surface
// WHILE AUDIO PLAYS is a pure STOP — it does not relaunch, so the audition
// ends where the user interrupted it. The NEXT click then lands on a stopped
// session and launches a fresh one from wherever it fell, re-capturing its
// end_sample at that launch — so a scrub after a mid-session
// trim edit auditions the NEW window instead of riding a stale capture. The
// audition then plays ONCE to that end and stops; there is no looping anywhere
// in the product.
// The old exact-same-frame skip is GONE with the relaunch it existed to
// avoid: it kept an in-place audition uninterrupted, and the playing case now
// always stops, so there is no in-place audition left to preserve — and it
// cannot migrate to the stopped case, whose scanner fields are stale by
// contract (a stopped scanner is deactivated immediately; no non-playing
// validity window exists). A refused launch (out-of-window frame; target
// update in flight) leaves playback stopped, silently — the "nothing to
// audition" family; a later click at a launchable frame launches.
void GuiInputHandler::scrub_act_at(int64_t frame) {
    if (playback.is_playing()) {
        // The pure stop, through the standing stop machinery — side-effect-
        // clean here (the scrub never moved the cursor) and the owner of the
        // scanner's visible-identity teardown. `frame` is deliberately unused
        // on this arm: a click over a live session says only "stop", never
        // where to play from.
        playback_lifecycle.stop_playback_if_playing();
        return;
    }
    // Outer is_updating gate, mirroring the two Space handlers: a NEW launch
    // while a target update is in flight would audition the stale target
    // buffer, which Space refuses — so the scrub launch refuses it too,
    // silently.
    if (!(app.active_audio_view == 'T' && target_render.is_updating()))
        playback_lifecycle.scrub_launch_at(frame);
}

// The scanner scrub press body. TWO CALLERS, both in on_button_press and both
// re-derived by grepping this function 2026-08-01: the waveform LOWER-HALF PLAIN
// LEFT press, and the BARE RIGHT press anywhere in the waveform area (architect
// 2026-08-01 — full height, so it overlaps the left entry's half and extends over
// the upper half's placement press). The marker-text lane's empty-spot scrub is
// DELETED (architect 2026-07-27; that lane touches playback in neither direction
// now). Each caller owns only its own gate — the half test, the modifier
// exactness, the in-flight-gesture guard — and everything below is shared, which
// is why the second entry copied no recipe. See the declaration for
// the full contract. ONE-SHOT per click (architect 2026-07-23, the Ableton
// model): derive the clicked column's frame and run one scrub act — the press
// arms NOTHING, a held press does nothing further, and motion over the scrub
// surface is inert (the scrub drag is removed; each click pays scrub_act_at's
// stop quiescence fence AT MOST once — a stopped session's launch pays none —
// and the per-column fence cadence class is structurally gone). The caller
// keeps playback alive across the press (no waveform press stops playback, and
// the top-strip stops belong to the top-strip claims), so the
// scrub act sees the LIVE session — load-bearing for the stop-then-start
// ruling: a caller that let the session die before the act would turn the
// interrupting click into a launch, which is precisely the behaviour the
// ruling removed.
void GuiInputHandler::scrub_press_at(int click_rel_x) {
    const GuiRect area = waveform_area(app);
    // Gutter / invalid column: no launch position exists, silent no-op.
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    const int64_t frame = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    scrub_act_at(frame);
}

// THE POINTER CURSOR'S ZONE MAP. The full contract — the one caller, the zone
// table with the press branch each is taken from, what it is deliberately blind
// to, the hover-only rule and its one named exception (the live trim gesture),
// and the accepted staleness — is at the declaration in input_handler.h.
//
// The refusals below are the press's OWN gates, in the press's order, each one
// re-read out of on_button_press rather than remembered, and each applying to
// EVERY kind (this is what makes the cues hover-only — with the trim gesture's
// one named exception, stated at its arm):
//   1. the prompt swallow (the first line of the handler);
//   2. the three BOTTOM-STRIP modal editors, which return without acting — the
//      shared predicate is modal_bottom_strip_editor_active, whose second caller
//      this is;
//   3. the open dropdown, which owns the pointer and consumes every press over
//      the pixels it floats above;
//   4. the loading / empty-audio return, above the whole waveform band. The four
//      REDESIGNED ROWS are claimed ABOVE it and stay live through a load — they
//      carry no cue of their own, so they need no arm here and take the Arrow
//      from the tail like every other unnamed surface;
//   5. any live pointer gesture — the press's own `drag`/`trim_drag` guards
//      widened to any_pointer_gesture_active, the one authoritative "some
//      pointer gesture is in flight" predicate. A gesture is not a swallow but
//      it is a lie: mid-drag the button is already down and no new press can
//      start anything. ONE gesture is excepted, ahead of the refusal: a live
//      trim gesture owns the cursor (the arm below, architect 2026-08-03).
GuiCursorKind GuiInputHandler::pointer_cursor_kind(int x, int y,
                                                   GuiInputState mods) const {
    if (app.prompt.active) return GuiCursorKind::Arrow;
    if (modal_bottom_strip_editor_active()) return GuiCursorKind::Arrow;
    if (app.dropdown.open()) return GuiCursorKind::Arrow;
    if (app.loading || audio.total_frames() <= 0) return GuiCursorKind::Arrow;

    // A LIVE TRIM GESTURE OWNS THE CURSOR (architect 2026-08-03) — the ONE
    // exception to the uniform live-gesture refusal below, and the reason it
    // can be one: on this gesture alone the thing being dragged is the thing
    // the cursor names, so the cue stays TRUE for the whole drag. The kind is
    // read from the drag's own record of what it grabbed — the bridge (both)
    // keeps the bar's TrimResize, a single-bound drag keeps its own bound's
    // edge shape — never re-derived from the pointer's position: dragging a
    // bound is exactly the act of taking the pointer off the band, and the cue
    // must not flicker through the band map's answers on the way.
    // THE PENDING ARM IS THE SAME ARM, not a second one deciding differently:
    // sub-threshold the pointer still rests on the geometry it pressed (the
    // endcap, the bridge, or the ctrl click's set bound), so the pending's
    // record and the hover map name the same cue — reading the record here
    // just keeps one owner across the whole press-to-release span. (The modal
    // gates above are structurally inert mid-drag — no press or key opens a
    // prompt, editor or dropdown while the button is held — so their rank
    // costs nothing.)
    if (app.trim_drag.active || app.pending_trim_drag.active) {
        const bool both     = app.trim_drag.active
                                  ? app.trim_drag.both
                                  : app.pending_trim_drag.both;
        const bool is_begin = app.trim_drag.active
                                  ? app.trim_drag.is_begin
                                  : app.pending_trim_drag.is_begin;
        if (both) return GuiCursorKind::TrimResize;
        return is_begin ? GuiCursorKind::TrimBoundBegin
                        : GuiCursorKind::TrimBoundEnd;
    }
    if (any_pointer_gesture_active(app)) return GuiCursorKind::Arrow;

    // The waveform BAND, spelled exactly as the press spells it: full window
    // width (top.w), not the effective width, so the <=15px inert right gutter
    // counts as waveform by the user's lights on both surfaces alike. A press
    // there is a silent no-op rather than a launch, which is a degenerate
    // off-deployment case (the gutter is 0px at 1920/2560/3840) and not worth a
    // second, narrower band that would then disagree with the press.
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < top.x + top.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top = rect_contains(top, x, y);
    // THE MERGED TRIM SURFACE, spelled from the ONE geometry owner the press
    // sites read (top_trim_surface_area — trim bar + ruler + their gap since
    // the trim surface arc, 2026-08-11) and derived once because FOUR
    // modifier arms below need
    // it — plain (the endcap grabs), alt (the bridge drag, the fourth glass
    // session's home), ctrl (the BEGIN
    // bound-set) and ctrl+shift (the END bound-set). The presses gate on the
    // top strip first and so does this.
    const GuiRect trim_band = top_trim_surface_area(app);
    const bool in_trim_surface = inside_top &&
                                 y >= trim_band.y &&
                                 y < trim_band.y + trim_band.h;
    // THE `h` HISTORY MODE CONSUMES THIS BAND'S TRIM GESTURES, which is why the
    // mode enters the map HERE rather than as a fourth blanket return above. The
    // mode is PER-ZONE, and since 2026-08-07 it is the ONLY per-zone consumer
    // left — read-only was the other, and its trim refusals are deleted with the
    // ruling that trim is band rather than authored content. Under the mode the
    // Pan and the Zoom stay live — they are its navigation
    // vocabulary — while
    // the endcap drags, the alt bridge drag and the two bound-set clicks are
    // consumed
    // no-ops, so their cues must go. This term is what takes them: the alt arm
    // falls to the waveform's own Pan-or-Arrow question, the ctrl arm
    // to its Zoom-or-Arrow twin, the ctrl+shift arm
    // to the Arrow it already returns everywhere else, and the plain arm takes
    // the
    // Arrow below. THE SCRUB IS THE MODE'S SECOND ZONE-SCOPED CUE, taken at its
    // own arm at the tail of this function (2026-08-05, when playback left the
    // view whole).
    // THE MODE'S OWN TRIM-BAR ACT IS A DOUBLE-CLICK (architect 2026-08-05,
    // superseding the single click and the Zoom cue it wore for a day), so it
    // adds NO cue here: a double-click has no cursor promise anywhere in the
    // product — the live band's span framing is one too, and the band shows the
    // shapes of its drags, never that.
    const bool trim_write_gestures_live =
        in_trim_surface && !app.history_mode.active;

    // ALT-EXACT: two claims since the fourth glass session (2026-08-11, the
    // architect's vocabulary ruling — ctrl = zoom, ALT = move/pan). Over the
    // MERGED TRIM SURFACE's inter-cap BRIDGE span alt is the BRIDGE (pair)
    // drag — ALT+DRAG MOVES THE TRIM WINDOW — so the span answers the
    // bridge's own TrimResize (the cue names the DRAG the hold promises);
    // the modifier swapped off ctrl, the geometry test unchanged. Over the
    // waveform alt is the captured grab-pan, `inside_waveform` alone — either
    // half, no band split. Everywhere else alt claims nothing and shows the
    // Arrow.
    if (mods.alt && !mods.ctrl && !mods.shift) {
        if (trim_write_gestures_live &&
            point_in_trim_bridge_span(app, audio, x, y))
            return GuiCursorKind::TrimResize;
        return inside_waveform ? GuiCursorKind::Pan : GuiCursorKind::Arrow;
    }
    // CTRL-EXACT: two claims, and the press path's own order between them.
    // Over the MERGED TRIM SURFACE ctrl is the BOUND-SET PAIR'S vocabulary
    // alone since the bridge drag moved to alt (the fourth glass session,
    // 2026-08-11): the BEGIN set — deferred to the motionless release inside
    // the bridge span, at-press-then-arm off it, ONE cue for both shapes —
    // so the band answers the BEGIN cap's own cue where the click would act
    // and the Arrow where the strictly-inside guard would consume it (the
    // in-span deferral changes no answer: an in-span column is strictly
    // inside by geometry). Over the
    // waveform it is the dual-axis strip drag, `inside_waveform` alone —
    // the gesture's ONE entry since the ruler's zoom arm died with the merge.
    // Ctrl's other top-strip claim is the marker membership toggle, which is
    // not a drag and has no cue.
    if (mods.ctrl && !mods.alt && !mods.shift) {
        if (trim_write_gestures_live) {
            return trim_bound_click_frame(/*is_begin=*/true, x)
                       ? GuiCursorKind::TrimBoundBegin : GuiCursorKind::Arrow;
        }
        return inside_waveform ? GuiCursorKind::Zoom : GuiCursorKind::Arrow;
    }
    // CTRL+SHIFT-EXACT: the MERGED TRIM SURFACE is its ONE claim in the whole
    // product — the
    // END bound set, the begin set's mirror — so it takes the END cap's cue there
    // and the Arrow everywhere else.
    if (mods.ctrl && mods.shift && !mods.alt) {
        if (trim_write_gestures_live)
            return trim_bound_click_frame(/*is_begin=*/false, x)
                       ? GuiCursorKind::TrimBoundEnd : GuiCursorKind::Arrow;
        return GuiCursorKind::Arrow;
    }
    // Every other combination — shift, and every mixed pair the press path
    // discards at its strict-modifier gate — is unnamed. Shift's waveform press
    // is the deliberate one: it is the placement press (2026-08-05, when the
    // region former's anchor moved to the click), and the placement press has no
    // cue on either half. THAT HOLDS IN THE `h` VIEW UNCHANGED, and needed no
    // mode arm: the view's own shift press is the same gesture, and this line
    // answers Arrow above every mode-scoped term below it.
    if (mods.ctrl || mods.alt || mods.shift) return GuiCursorKind::Arrow;

    // PLAIN-EXACT from here, and the top strip splits by band exactly as the
    // press does — the merged trim surface, then the marker lane below it.
    if (inside_top) {
        // (THE RULER'S ZOOM CUE IS DELETED with its strip-drag entry — the trim
        // surface arc, 2026-08-11: the ruler rows are the merged trim surface's
        // now, answered below, and zoom's one pointer entry is the
        // ctrl+waveform press, whose cue is the ctrl arm's above.)
        //
        // THE MERGED TRIM SURFACE, RESOLVED THROUGH THE ROUTER'S OWN OWNER
        // (architect 2026-08-03 for the by-construction rule; the band merged
        // and the plain bridge cue died 2026-08-11): the plain press arms only
        // on an ENDCAP — everywhere else on the band it is the REGION FORMER,
        // which carries no cue, exactly as the waveform placement press
        // carries none — so the cue asks exactly hit_test_trim_endcap, the
        // predicate route_trim_bar_press calls, and a point off the caps shows
        // the Arrow. Cue and gesture therefore agree BY CONSTRUCTION rather
        // than by proximity, which is the whole rule this map is written to.
        //
        // AN ENDCAP IS NOT THE BRIDGE: a cap moves ONE bound, so the caps take
        // the boundary-extension shapes (begin left_side, end right_side); the
        // bridge's ew-resize is ALT vocabulary now (the fourth glass
        // session's swap), named in the alt arm
        // above where the drag actually lives.
        //
        // READ-ONLY NO LONGER REFUSES ANYTHING HERE (architect 2026-08-07): the
        // band's read-only return is deleted, trim being band rather than
        // authored content, so a locked tab runs the endcap and bridge drags and
        // this arm must promise them. The term that used to answer Arrow on the
        // read-only bit is gone with the gesture refusal it mirrored — cue and
        // gesture stay one decision, which is why nothing replaced it.
        if (in_trim_surface) {
            // THE `h` HISTORY MODE IS THE ONE THING THAT STILL TAKES THIS BAND'S
            // CUES: the drags its shapes promise are consumed in there, and its
            // one live gesture is a DOUBLE-click, which no cue in the product
            // names. The Arrow, over the whole band.
            if (app.history_mode.active) return GuiCursorKind::Arrow;
            switch (hit_test_trim_endcap(app, audio, x, y)) {
                case TrimHit::Begin: return GuiCursorKind::TrimBoundBegin;
                case TrimHit::End:   return GuiCursorKind::TrimBoundEnd;
                case TrimHit::None:  break;
            }
            return GuiCursorKind::Arrow;
        }
        return GuiCursorKind::Arrow;
    }
    // THE WAVEFORM'S LOWER HALF: the audition scrub, through the press's own half
    // expression. The upper half is the placement press, which carries no cue.
    // THE `h` HISTORY VIEW ANSWERS ARROW HERE (architect 2026-08-05): playback
    // is removed from the view whole, so the lower half is the PLACEMENT press
    // in there like the upper half — there is no scrub left to promise, and a
    // crosshair over a surface that auditions nothing would be a cue that lies.
    if (inside_waveform && waveform_lower_half(area, y) &&
        !app.history_mode.active)
        return GuiCursorKind::Scrub;
    return GuiCursorKind::Arrow;
}

// THE POINTER CURSOR'S WHOLE APPLIER, called once per run-loop iteration from
// the platform's settled-state hook and from nowhere else (the contract, and why
// the twenty-three per-site pushes are gone, are at the declaration).
void GuiInputHandler::refresh_pointer_cursor(GuiInputState mods) {
    // The remembered coordinates name a point INSIDE the window even after the
    // pointer has left, so the flag is what keeps this from resolving a cue for
    // a pointer that is elsewhere. It also covers the cold session in which no
    // pointer ever entered: the flag starts false and is written true only by
    // on_motion, which seeds last_mouse_x/y in the same breath.
    if (!app.pointer_in_window) return;
    gui.set_cursor_kind(
        pointer_cursor_kind(app.last_mouse_x, app.last_mouse_y, mods));
}

// Button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::apply_strip_drag_at(int x, int y, bool final_event) {
    // Dual-axis strip drag, INCREMENTAL (the v6 model). Every event reads the
    // LIVE zoom level and viewport and applies its own dx/dy on top — there is no
    // press baseline to go stale across composed pan/zoom phases (the earlier
    // axis-lock model died of exactly that staleness). The song anchor
    // (anchor_sample) is the zoom focus; the pan re-derives its drifted column
    // each event and the edge trick rebinds it when it leaves the screen.
    StripDragState& sd = app.strip_drag;

    // (1) Per-event deltas from the previous motion position. The crossing event
    // folds the whole accumulated delta since the press (last_x/last_y were
    // seeded there and no sub-threshold event advanced them).
    const double dx = static_cast<double>(x - sd.last_x);
    const double dy = static_cast<double>(y - sd.last_y);
    sd.last_x = x;
    sd.last_y = y;

    // (2) The old spp is read from the LIVE level (never stored).
    const double spp_old = current_samples_per_pixel(app, audio);
    const GuiRect wf_area = waveform_area(app);
    const double W = static_cast<double>(wf_area.w);
    const int64_t total = live_total_frames(app, audio);

    // (3) Pan at the old level, in the double domain: grab sign — drag right
    // (dx>0) reveals earlier content, so the viewport moves left. The result is
    // WALL-CLAMPED here, at the old level, to the SAME right wall the downstream
    // clamp_viewport_start rests at — the shared max_viewport_start_grid owner
    // (the level mid-gesture is the live level, so it reads exactly the state the
    // chokepoint would). step (5) derives the anchor column and rebinds
    // anchor_sample from vp, so both must see the viewport that will actually
    // REST. The earlier `total − W·spp_old` form sat up to a pixel short of the
    // legal grid rest — pressing at the flush-right rest first pulled vp back to
    // that off-grid wall, the anchor column clamped at W−1, and the edge rebind
    // PERMANENTLY rewrote anchor_sample, so the at-wall no-op proof failed exactly
    // at the legal rest; sharing the wall makes vp derive at the true rest and the
    // no-op proof hold. The grid snap of arbitrary INTERIOR vp values is
    // deliberately NOT reproduced (the sub-pixel residue there self-heals on the
    // following event, exactly as step (5)'s live re-read does — only the WALL had
    // to be exact because the edge rebind is a lasting mutation).
    double vp = static_cast<double>(app.viewport_start_sample) - dx * spp_old;
    const double vp_lo = 0.0;
    const double vp_hi = static_cast<double>(max_viewport_start_grid(app, audio));
    if (vp < vp_lo) vp = vp_lo;
    if (vp > vp_hi) vp = vp_hi;

    // (4) Zoom INCREMENTALLY off the live level: this event's dy applies to the
    // current level (drag DOWN, dy>0, lowers the level → zooms in). No press
    // baseline, so a wall reversal responds immediately — the older absolute-dy
    // formula had a dead zone after a clamp (dy had to unwind all the way back
    // before the level moved); this incremental form has none.
    double new_level = app.zoom_level - dy / kZoomStripPxPerLevel;
    const double max_l = effective_max_zoom_level(
        W, total, audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // (5) The anchor's drifted column under the wall-clamped post-pan viewport,
    // with the Ableton EDGE TRICK: clamp the column into [0, W-1] (the effective
    // waveform width), and when the clamp engages REBIND anchor_sample to that
    // edge pixel's frame — the zoom focus never leaves the screen; a pan that
    // pushes it to an edge PINS it there and it becomes the edge's content.
    // Deriving the column from the wall-clamped vp is what makes pure pan an
    // exact identity (see below) even at a saturated wall, and reading the live
    // viewport each event is what lets the sub-pixel grid snap self-heal on the
    // following event.
    double anchor_col = (sd.anchor_sample - vp) / spp_old;
    const double col_max = W > 0.0 ? W - 1.0 : 0.0;
    double clamped_col = anchor_col;
    if (clamped_col < 0.0)     clamped_col = 0.0;
    if (clamped_col > col_max) clamped_col = col_max;
    if (clamped_col != anchor_col) {
        sd.anchor_sample = vp + clamped_col * spp_old;
        anchor_col = clamped_col;
    }

    // Drive the capture's release-restore x to the anchor stem's surface x — the
    // identical column->x math render_strip_anchor_stem paints at (area.x +
    // col + 0.5), so on release the cursor lands dead on the stem rather than at
    // its raw traveled position (which the edge rebind leaves past a pinned
    // stem). Fired every event; the last before release wins. A motionless
    // strip press-release never reaches here, so its override stays unset and it
    // restores at the press point.
    if (set_strip_capture_restore_x)
        set_strip_capture_restore_x(
            static_cast<double>(wf_area.x) + anchor_col + 0.5);

    // (6) Apply: place anchor_sample at anchor_col under the new level's spp and
    // clamp. IDENTITY PROOFS: pure pan (dy=0) is EXACT — new_level == old, so
    // apply reproduces vp = anchor_sample - anchor_col·spp_old bit-for-bit (the
    // column was derived from that same vp), and the level-unchanged dispatch
    // takes the same synchronous full rebuild. Off the walls the pan arithmetic
    // is unchanged, so the identity holds as before; AT a wall the clamped vp
    // equals the viewport that will rest, the anchor column re-derives against it
    // consistently, and apply reproduces the wall value — a saturated pan is a
    // true no-op. Pure zoom (dx=0) leaves the anchor's current (possibly
    // edge-pinned) column fixed and pivots the rescale around it. A both-unchanged
    // event (level and viewport identical after the clamp) is a true no-op the
    // entry point skips.
    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample, anchor_col,
                                   final_event);
}

// ARM THE DUAL-AXIS STRIP DRAG at (x, y) — ONE body, ONE entry since the trim
// surface arc (architect 2026-08-11): the CTRL-EXACT WAVEFORM PRESS. The
// gesture's entry history, kept because each step was a ruling: a dedicated
// zoom LANE (deleted 2026-07-31), the ctrl press alone (which proved the
// deletion cost no capability), a second entry on the RULER band from row 5
// (2026-08-01), and the ruler entry DELETED 2026-08-11 when the ruler merged
// into the trim surface — zoom is already well supported on the waveform
// (ctrl-drag, the keys, touch two-finger), his words, so the band went to
// trim instead. The zoom-stem paint-from-press bookkeeping is the ctrl
// entry's alone now.
//
// The anchor is the SONG position under the press column, which is what makes
// the zoom pivot on the pixel the user grabbed.
//
// THE ANCHOR STEM PAINTS FROM THE PRESS (architect 2026-08-05, moving its start
// off the slack crossing): the headless zoom stem stands at the press column for
// the whole life of the gesture, so the press itself shows where the zoom will
// pivot instead of the affordance appearing only once the drag is under way.
// Same stem, same painter, no head — only its first frame moved, which is why
// the arm now owes the damage (the paint gate is strip_drag.active alone;
// paint_strip_drag_anchor, paint_handler.cpp).
//
// THE PLAYHEAD IS NOT TOUCHED HERE, and the gesture is NAVIGATION-CLASS as it
// always was (architect 2026-08-06, ROLLING BACK the strip-drag playhead feature
// of 2026-08-05..06 — it was buggy while audio played and it made the history
// view's ctrl press read as broken). The stem-on-mousedown above is the ONE part
// of that arc that survives; the press jump, the release-side click and the
// anchor-follow re-seat are all gone, and SHIFT+CLICK is where "move the
// playhead here" lives (the waveform placement press, region-scrub-esc.md).
void GuiInputHandler::arm_strip_drag_at(int x, int y) {
    const double spp = current_samples_per_pixel(app, audio);
    app.strip_drag = StripDragState{};
    app.strip_drag.active  = true;
    app.strip_drag.press_x = x;
    app.strip_drag.press_y = y;
    app.strip_drag.last_x  = x;
    app.strip_drag.last_y  = y;
    app.strip_drag.anchor_sample =
        static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(x) * spp;
    // ZOOM IS THE GESTURE'S CUE — the ctrl-waveform press is exactly the zone
    // map's one Zoom surface (the ruler's died with its entry, 2026-08-11) —
    // so this is
    // the kind the capture release restores. Passing it is what makes the restore
    // independent of what was on screen when the press landed (contract at
    // GuiPlatform::begin_pointer_capture).
    begin_strip_pointer_capture(GuiCursorKind::Zoom);
    // THE PRESS OWES THE STEM'S FIRST FRAME. A press is a DISCRETE command, so
    // the shape is full waveform-area damage rather than a narrow column (the
    // rule and the per-site table are at playhead_pixel_x, app_state.h); every
    // later frame of the stem rides the drag's own synchronous rebuild.
    viewport.invalidate_waveform_area();
}

// THE TOUCH NAVIGATION BODY — two-finger pan+zoom frames and the phone
// model's single-finger pan frames (dist_ratio 1.0, the zoom term inert)
// land here alike; contract, delivery-shape
// justification and refusal rationale at the declaration (input_handler.h).
// One delivered frame = one placement through the strip-drag family's own
// application chokepoint.
void GuiInputHandler::apply_touch_nav_update(int x, int y, double dx,
                                             double dist_ratio) {
    // The refusal answer, per frame: the wheel's own routing predicate at the
    // current centroid. <= 0 covers both the modal refusals (-1) and the
    // outside-both-areas 0 that handle_wheel itself no-ops on — the gesture
    // navigates exactly the wheel's two surfaces.
    if (wheel_context(x, y) <= 0) return;
    // Defensive only: the platform guarantees a positive ratio (a degenerate
    // finger distance delivers 1.0).
    if (!(dist_ratio > 0.0)) dist_ratio = 1.0;

    const GuiRect wf_area = waveform_area(app);
    const double  W       = static_cast<double>(wf_area.w);
    const int64_t total   = live_total_frames(app, audio);
    if (W <= 0.0 || total <= 0) return;

    // An applied navigation frame moves content between two taps, so a
    // pending double-click candidate must not survive it (the C8 rule the
    // wheel applies at on_wheel's top).
    app.double_click = DoubleClickCandidate{};

    // The content under the PREVIOUS centroid column (x - dx) is what the
    // fingers hold; the anchor column convention is arm_strip_drag_at's own
    // (window x against the live viewport — the waveform starts at the window
    // edge, and no clamp into [0, W-1] is needed: there is no persistent
    // anchor for an off-area column to corrupt, and the placement below runs
    // through the viewport chokepoint's own clamps either way).
    const double spp_old = current_samples_per_pixel(app, audio);
    const double anchor_sample =
        static_cast<double>(app.viewport_start_sample) +
        (static_cast<double>(x) - dx) * spp_old;

    // The distance ratio maps to the level LOGARITHMICALLY — spreading the
    // fingers by 2x is one level in (spp halves, so the content between the
    // fingers scales with the finger gap; no feel constant). Pre-clamped into
    // the same [kMinZoom, effective ceiling] window clamp_viewport_start
    // re-applies, exactly as apply_strip_drag_at pre-clamps — the chokepoint's
    // level_changed compare requires a real request (its contract names both
    // callers).
    double new_level = app.zoom_level - std::log2(dist_ratio);
    const double max_l =
        effective_max_zoom_level(W, total, audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // One placement does both axes: the anchor lands at the CURRENT centroid
    // column under the new level, so the centroid delta pans and the ratio
    // zooms about the centroid. Everything downstream is the strip drag's own
    // — level clamp, viewport clamp, the synchronous per-frame rebuild, the
    // either-axis follow suppression, and the mid-gesture true-no-op skip.
    viewport.apply_strip_drag_zoom(new_level, anchor_sample,
                                   static_cast<double>(x),
                                   /*final=*/false);
}

void GuiInputHandler::end_touch_nav() {
    // Any end commits, and every applied frame already rebuilt synchronously;
    // the one deferred piece is the playback predictor (mid-gesture frames
    // skip the resync exactly as the strip drag's do) — the grab-pan release's
    // own tail.
    if (playback.is_playing()) playback.resync_predictor();
}

// The pan-zone query's body (contract at the declaration): the waveform area,
// geometry only, on the tree's one containment convention.
bool GuiInputHandler::touch_point_in_pan_zone(int x, int y) const {
    return rect_contains(waveform_area(app), x, y);
}

// The trim-band zone query's body (the fourth glass session, 2026-08-11;
// contract at the declaration): the merged trim surface, geometry only —
// the same one accessor every trim gesture reads, so the touch hold and the
// pointer arms agree on the band by construction. Refusals stay in
// begin_touch_trim_move, the pan-zone split exactly.
bool GuiInputHandler::touch_point_in_trim_band_zone(int x, int y) const {
    return rect_contains(top_trim_surface_area(app), x, y);
}

// The flag editor's guard-free close, shared by the left and right press arms
// (contract at the declaration, input_handler.h). The box is the painter's
// published rect, the same one the F2.1 caret block tests, so "outside" means
// the same thing on every press path.
void GuiInputHandler::close_top_flag_editor_for_outside_press(int x, int y) {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (rect_contains(app.flag_editor_box.box, x, y)) return;
    flag_editor.exit_top_flag_edit_no_commit();
}

// THE TRIM BAR'S DOUBLE-CLICK TEST, hoisted for its SECOND consumer: the live
// band's span framing (in on_button_press below) and, since 2026-08-05, the
// history mode's framing of the viewed checkpoint's diff span
// (handle_history_mode_press). The two run different commands on the same
// gesture, so the gesture itself is asked in one place — surface tag, window and
// slack on both axes, exactly as the seed records them at the motionless
// release. It takes the candidate SNAPSHOT rather than reading app.double_click,
// because the press clears that field at its top: only the snapshot still holds
// the previous click.
static bool trim_bar_double_click_at(const DoubleClickCandidate& dc,
                                     int x, int y) {
    return dc.surface == DoubleClickSurface::TrimBar &&
           monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
           std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
           std::abs(y - dc.press_y) <= kDoubleClickSlackPx;
}

void GuiInputHandler::on_button_press(GuiMouseButton button, int x, int y,
                                      GuiInputState mods) {
    // ANY PRESS HIDES THE TOOLTIP, above every gate — it has said what it had to
    // say, and a hint left floating over the thing the user just clicked is
    // noise. It also resets the dwell, so a fresh hover starts a fresh wait.
    // Placed here rather than at the release because the hint's job ends the
    // moment the user acts on it, not when they let go.
    hide_shift_tooltip();
    // ANY PRESS ENDS THE MENU ROW'S MODE, beside it and for a related reason: the
    // ruling ends the mode on every ordinary dismissal, and with no popup open a
    // press is the only pointer act there is — the press on the anchor, the press
    // on a view-bar button, the press on the waveform underneath. THIS NEEDS NO
    // EXCEPTION LIST because the one press that must KEEP the mode re-arms
    // immediately through toggle_dropdown's open path a few lines below, which is
    // the mode's one producer; so "any press ends it" costs exactly nothing and
    // cannot be forgotten by a press route added later.
    // It is gated inside disarm_menu_row: with a popup OPEN this is inert, and
    // the press then belongs to the popup, whose own routes decide the mode.
    disarm_menu_row();
    // ANY POINTER PRESS ENDS THE TRANSPORT ARROWS' HOLD-REPEAT — the pointer
    // member of the three-edge disarm list at AppState::transport_repeat's
    // declaration (the platform layer-1 mirror; the undo-adjacency reasoning
    // is at the on_key member). Unconditional on the disarm_menu_row
    // precedent: the one press that must keep a hold — an arrow's own — re-arms
    // through dispatch_redesign_chord a few lines later, and every other press
    // (a right button mid-hold included) is an intervening event.
    app.transport_repeat.owner = -1;
    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate (TrimBar / EditorText at a motionless release,
    // Marker / EmptyLane at the press). One closed instrumentation point — the clear covers
    // every non-consuming press (a strip/region/trim arm, a modal swallow)
    // without a clear scattered on each path. It sits ABOVE the prompt swallow
    // (2026-08-11), which is what makes the "a modal swallow" clause fully
    // true — the prompt swallow was the one press that did not clear. (The
    // row-8 claim that briefly stood between this clear and the prompt gate is
    // gone: it existed for the row's Cancel button, which the architect
    // deleted the same day, and the transport claim is back in the band block
    // below with its four siblings.)
    const DoubleClickCandidate dc_at_press = app.double_click;
    app.double_click = DoubleClickCandidate{};

    // Prompt-modal input handling: while the bottom-strip prompt is
    // active, all mouse events are swallowed. Responses go through
    // the keyboard.
    if (app.prompt.active) return;

    // THE MODAL-TRAP FIX (architect 2026-08-11, with the trim surface arc; the
    // defect was the road's: an accidentally opened settings editor on GLASS,
    // with no physical keyboard, was an EXIT-LESS STATE — the Quit button did
    // nothing). The bottom-strip modal editors admit Ctrl+Q and Ctrl+S as
    // CHORDS, but their pointer swallows below refuse the roster wholesale —
    // so the QUIT and SAVE buttons violated button-is-its-chord exactly where
    // the chord is admitted. FIXED BY DERIVATION, not new semantics: while a
    // bottom-strip modal editor stands, a plain left press on a roster button
    // whose chord the editors' modal contract ADMITS AS A COMMAND
    // (modal_editor_admits_command_chord above — membership derived from the
    // admission, never hand-listed; today that resolves to Quit and Save)
    // dispatches through the ordinary one dispatch body, and every other
    // press stays refused at the swallows below. The dispatched chord then
    // meets the KEYBOARD gate's own routing — Ctrl+S saves with the editor
    // open, Ctrl+Q runs the teardown and the close route — so the button is
    // its chord end to end. Faces: the two admitted buttons already wear
    // their enabled faces (the modal gates are deliberately absent from
    // redesign_button_enabled — a modal that greyed the chrome would be a
    // fourth face nobody asked for, the standing note there), so face and
    // press agree where it matters and the un-admitted rest keep the standing
    // swallowed-press model. A MODIFIED press stays swallowed: the band
    // claims refuse ctrl/alt and non-admitting shift anyway, and under a
    // modal the refusal's home is the swallow. THE FLAG EDITOR IS NOT IN
    // THIS PATH and needs nothing: it is pointer-transparent — it never
    // blocked these presses, whose chords already dispatch into the keyboard
    // gate through the ordinary band claims below.
    if (button == GuiMouseButton::Left && !mods.ctrl && !mods.alt &&
        !mods.shift && modal_bottom_strip_editor_active()) {
        for (const ToolbarChord& tc : kToolbarChords) {
            if (!redesign_button_hit(app, tc.id, x, y)) continue;
            if (modal_editor_admits_command_chord(tc.key, tc.ctrl, tc.shift,
                                                  tc.alt)) {
                dispatch_redesign_chord(x, y, mods);
                return;
            }
            break;   // hit a non-admitted button: the modal swallow answers
        }
    }

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's text region places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the settings /
    // BPM bottom-strip editors too. A press outside the active editor's
    // region falls through: the bottom-strip editors stay modal and swallow
    // it, while the top flag editor closes guard-free below and the press
    // then acts normally.
    if (button == GuiMouseButton::Left) {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            bool in_region = false;
            if (g.bottom_strip) {
                // THE STATUS LANE, not the whole bottom strip: the strip is
                // TWO lanes since row 8 (2026-08-11), and the editors' text
                // lives in the status lane alone — the transport row above it
                // is the button rows' surface, whose band claim sits with the
                // other rows' below. Claiming the whole strip
                // here would map a transport-lane press to an editor byte.
                in_region = rect_contains(bottom_row_area(app), x, y);
            } else {
                // FlagPayload: the editable text lives IN THE UNROLLED FLAG BOX.
                // The claim is the whole published BOX, pads included, not just
                // the glyph run's extent — the box is the field, and clicking
                // its padding should place the caret at the nearest end exactly
                // as clicking a text field's margin does (the nearest-boundary
                // search gives that for free). Any press OUTSIDE the box is a
                // non-caret click, which closes the editor below and then routes
                // normally (the guard-free lifecycle).
                in_region = rect_contains(app.flag_editor_box.box, x, y);
            }
            if (in_region) {
                // Double-click: a second click within the window on this
                // editor's text selects the RUN of the clicked character class
                // (word / punctuation / whitespace) under the click — select_
                // word_at's own classifier, not just a word — arming no drag.
                // The surface tag keeps it from consuming a marker / trim-bar
                // candidate.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::EditorText &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                    std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                    text_editor::select_word_at(
                        *g.ed, editor_byte_index_at(g, x));
                    if (g.bottom_strip) viewport.invalidate_status_row_area();
                    else                viewport.invalidate_top_strip();
                    return;
                }
                set_editor_caret_from_x(g, x);
                // Collapsed anchor — extends to a real selection only if the
                // pointer then moves.
                g.ed->selection_anchor = g.ed->cursor_pos;
                app.editor_text_drag.active = true;
                if (g.bottom_strip) viewport.invalidate_status_row_area();
                else                viewport.invalidate_top_strip();
                return;
            }
            // A bottom-strip editor stays modal: a press outside its row is
            // swallowed without arming. A flag-editor press that isn't on the
            // lane text falls through to the guard-free close below.
            if (g.bottom_strip) return;
        }
    }

    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.load_editor)) return;
    if (text_editor::is_active(app.commit_title_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a bottom-strip modal owner (like the settings
        // editor). Mouse input does not interact with it beyond its own
        // click-to-cursor region; the session ends only through Esc or the
        // Enter dispatch path (`m` is just a typed character now). Swallow
        // the press so it cannot drive a region drag / marker click / or
        // tear the editor down through the top-strip flag-edit routine
        // below.
        return;
    }
    // THE OPEN DROPDOWN OWNS THE POINTER, claimed above every band because it
    // FLOATS over them: a press on one of its items runs that item, and a press
    // anywhere else closes it and is CONSUMED so nothing underneath acts. The
    // presses it does not swallow are those on the MENU BUTTONS themselves,
    // which fall through to the band claim below: on this menu's own button the
    // toggle closes it — the same gesture that opened it, closing it — and on the
    // OTHER menu's button the toggle switches, closing this one as it opens that
    // one (one popup state, so the switch is free).
    //
    // SINCE THE HOVER SWITCHES TOO (2026-08-03, on_motion), that second case is
    // now the rare one from a real pointer: crossing onto the other button has
    // already switched the menu, so the press landing there finds that menu open
    // and TOGGLES IT CLOSED — the ordinary menu-bar answer for pressing the
    // button whose menu is up. Neither route changed; they simply meet here, and
    // a press that arrives with no motion before it still takes the switch
    // through this claim.
    //
    // It sits BELOW the modal gates like every other pointer target, which is
    // half of why a popup and an editor are never open together: the popup opens
    // only from a press, and while one of the three BOTTOM-STRIP editors is up
    // every press dies at those gates (the MODAL-TRAP block above lifts ONLY
    // roster buttons whose chord the editors admit — Quit and Save today — and
    // the two menu ANCHORS carry no chord, so no press can open a popup under
    // an editor through it). The other half is not here — the
    // pointer-transparent FLAG editor swallows nothing, so a press does reach
    // the menu buttons with an edit open, and toggle_dropdown's open path ENDS
    // that edit (the rule is stated there). Two mechanisms, one claim. (The
    // reverse direction is closed by the keyboard gate: while the popup is open,
    // `;` is swallowed, so the editor cannot open under it either.)
    if (app.dropdown.open()) {
        // OWNING THE POINTER MEANS EVERY BUTTON, not just the left one
        // (2026-08-01, with the right-click scrub below): only LEFT carries
        // claims inside the popup, so any other button is CONSUMED INERT here
        // rather than falling through to the bands underneath — the popup floats
        // over the waveform, and a right press landing on the pixels it covers
        // must not scrub the audio behind it. Cheapest correct arm: the popup
        // stays open (a non-Left press is not one of its two answers, item-arm
        // or dismiss) and nothing acts.
        if (button != GuiMouseButton::Left) return;
        const AppState::Dropdown& pop = app.dropdown;
        const bool on_menu_button =
            redesign_button_hit(app, RedesignButton::Settings, x, y) ||
            redesign_button_hit(app, RedesignButton::Navigation, x, y);
        if (!on_menu_button) {
            const int hit = dropdown_item_at(x, y);
            // A MODIFIED press inside the popup closes it and does nothing else:
            // no item carries a modified binding, and leaving the popup open
            // under a press it refused would be the worse answer.
            const bool plain = !mods.ctrl && !mods.shift && !mods.alt;
            if (hit >= 0 && plain) {
                // A DISABLED ITEM ARMS NOTHING AND DISMISSES NOTHING (2026-08-08,
                // with the menus' first per-item disabled state): the press is
                // consumed where it lands and the MENU STAYS UP, kdenlive's own
                // answer for a greyed row — pressing one is a nothing, not a
                // dismissal. That is why this returns rather than falling into
                // the close below, which is the answer for the separator, the
                // chrome and the box's outside: those are the popup's DEAD SPACE,
                // and a greyed item is a row that is simply not for you.
                // The predicate is the painter's own (dropdown_item_enabled,
                // app_state.h), so the grey face and the inert press are one fact
                // read twice — the roster's disabled-press rule, one surface out.
                if (!dropdown_item_enabled(app, pop.menu, hit)) return;
                // ITEMS ACT ON RELEASE — this press only ARMS one. The whole
                // redesign fires its buttons on press; a MENU is the exception,
                // because that is the universal convention (press, slide, release
                // on what you meant) and because it is what makes the pressed
                // face worth painting at all: a press-to-act item would show its
                // accent fill for a single frame.
                //
                // THE ARM DOES NOT STAY HERE: from this press until the button
                // comes up it FOLLOWS THE POINTER (recompute_dropdown_hover),
                // so the slide of "press, slide, release on what you meant" is
                // literally the FACE arm moving — but the release does not act
                // on wherever it ended: it DERIVES the acted-on item from its
                // own coordinates under the claim (finish_dropdown_release),
                // which the arm only paints. The bit is what tells that walk the
                // held button belongs to this popup's gesture — it has a SECOND
                // producer since
                // 2026-08-03, the anchor press that opened the menu, whose drag
                // into the box is the same gesture arriving from outside — and it
                // is set OUTSIDE the transition test below, which is about
                // damage.
                app.dropdown.press_began_on_item = true;
                if (pop.pressed_item != hit) {
                    app.dropdown.pressed_item = hit;
                    viewport.invalidate_top_strip();
                    viewport.invalidate_rect(pop.rect);
                }
                return;
            }
            // Anywhere else inside the popup, or a modified press: close and
            // consume, so nothing underneath acts.
            close_dropdown();
            return;
        }
    }

    // THE FIVE REDESIGNED BUTTON ROWS (top lanes 0..3 plus the bottom strip's
    // transport row, whose claim closes the block), claimed ABOVE the
    // loading/empty guard below so their buttons stay live while a file loads
    // and on a blank state — they are the surfaces that have nothing to do with
    // the loaded audio. They sit BELOW the modal gates on purpose: a press while
    // a prompt or a bottom-strip editor is up is swallowed there, exactly as it
    // is for every other pointer target (a modal owns the pointer; these buttons
    // are no exception, and every one of their chords reaches the same route
    // from the keyboard anyway). (Row 8's claim stood ABOVE the modal gates for
    // part of 2026-08-11, for the sake of its Cancel/Esc button — the one
    // button that had to reach the keyboard-modal Esc bindings; the architect
    // deleted that button the same day and the claim came home to this block
    // with its justification.)
    //
    // ONE BAND-CLAIM SHAPE FOR ALL FIVE ROWS: the exact half-open row band, a
    // press carrying CTRL or ALT is a strict consumed no-op, a SHIFT press binds
    // only where the chord table admits one, and any press in the band that is
    // not on a button is a consumed nothing. Each band differs ONLY in its rect
    // and (row 1) in the dropdown toggle of its TWO non-chord buttons, Settings
    // and Navigation, so the dispatch is ONE body, dispatch_redesign_chord,
    // driven by the table's per-button flags.
    //
    // A BUTTON's rect is the painter's stash (app.redesign_buttons, published by
    // paint_menu_row / paint_toolbar_row / paint_tab_row / paint_icon_row /
    // paint_transport_row) —
    // never re-shaped here, so the clickable rect is the painted one. The action
    // FIRES ON PRESS: nothing on these rows drags, so there is no arm, no
    // threshold and no release body, and no double-click surface either. The one
    // thing a RELEASE does carry is the click face, cleared in
    // clear_redesign_button_press. Nothing here reads keyboard state, so the
    // bare-`e` mouse key reaches them as an ordinary left press through the
    // platform translation.
    {
        const GuiRect menu_row = top_menu_row_area(app);
        if (rect_contains(menu_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) {
                // SETTINGS AND NAVIGATION ARE THE ROSTER'S TWO NON-CHORD
                // BUTTONS, so they are spelled here rather than in the table:
                // each action is a POPUP TOGGLE, which no keyboard chord
                // performs. Their menus lead to routes the keyboard already has
                // — the bare `;` still opens the settings editor DIRECTLY, and
                // every navigation item is a key you can press instead — so a
                // dropdown is a pointer affordance for an existing road, never a
                // second one. Shift-exact is refused like every other
                // non-admitting button.
                //
                // THE ANCHOR PAIR IS WALKED rather than spelled twice — the same
                // shape on_motion's two anchor walks take, so
                // dropdown_anchor_button stays the one place that knows which
                // button emits which menu — and the walk is what gives the CLAIM
                // below exactly ONE site instead of one per branch.
                DropdownMenu anchored = DropdownMenu::None;
                if (!mods.shift) {
                    for (const DropdownMenu m : {DropdownMenu::Settings,
                                                 DropdownMenu::Navigation}) {
                        if (!redesign_button_hit(app, dropdown_anchor_button(m),
                                                 x, y)) continue;
                        anchored = m;
                        break;
                    }
                }
                if (anchored != DropdownMenu::None) {
                    toggle_dropdown(anchored);
                    // AN ANCHOR PRESS THAT OPENS A MENU CLAIMS THE HELD BUTTON
                    // FOR THE POPUP (architect 2026-08-03): press the button,
                    // hold, drag down into the menu that came up and release on
                    // an item, and that item fires — the desktop menu bar's one
                    // continuous gesture. This ONE assignment is the whole
                    // feature. Everything past it is the item press's own
                    // machinery, reused unchanged: the arm FOLLOWS THE POINTER
                    // from here (recompute_dropdown_hover, whose live-press test
                    // is the platform's button state AND this bit), the
                    // separator / the chrome / the box's outside / this very
                    // button arm nothing because none of them is an item, and
                    // the release derives the acted-on item from its own
                    // coordinates under the claim rather than reading the
                    // recorded arm (finish_dropdown_release).
                    //
                    // ITS VALUE IS THE TOGGLE'S OUTCOME READ BACK, never
                    // predicted: the toggle's other half CLOSES the menu whose
                    // anchor was pressed, and a press that put a menu AWAY claims
                    // nothing — there is no popup left for a gesture to belong
                    // to, and the release that follows must find the claim false.
                    // A SWITCH (this anchor pressed while the OTHER menu stood)
                    // is an open like any other and claims like one.
                    //
                    // THE CLAIM IS THIS PRESS SITE'S TO RECORD, NOT
                    // toggle_dropdown'S, and the reason is that owner's other
                    // callers: the menu-row hover open and the hover switch carry
                    // NO held button at all, so a claim written inside the toggle
                    // would be a lie on those routes. Its open path also RESETS
                    // the popup struct, which is exactly what makes a mid-hold
                    // hover switch onto the other anchor drop claim and arm
                    // together — the recorded rule, kept.
                    app.dropdown.press_began_on_item = app.dropdown.open();
                } else {
                    dispatch_redesign_chord(x, y, mods);
                }
            }
            return;
        }
    }
    {
        const GuiRect toolbar = top_toolbar_row_area(app);
        if (rect_contains(toolbar, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) dispatch_redesign_chord(x, y, mods);
            return;
        }
    }
    {
        const GuiRect tab_row = top_tab_row_area(app);
        if (rect_contains(tab_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            // THE ROW IS THE WALK SELECTOR WHILE THE `h` VIEW STANDS (architect
            // 2026-08-05 for the repurposing, 2026-08-08 for the axis), so its
            // presses are routed HERE and never reach the chord table below:
            // these SELECT a walk source, which is not what a chord dispatch
            // would do — the tabs' chord, Ctrl+Tab, became the mode's own CYCLE
            // and would step past whichever slot was clicked.
            //
            // ONE SWITCH OWNER for all five routes into the (source, reading)
            // pair — these two slots, the keyboard cycle's two directions
            // (Ctrl+Tab, Ctrl+Shift+Tab) and the `u` reading toggle, all through
            // set_history_reading — and it is IDEMPOTENT, which is where the
            // live tabs' radio rule comes from: a press on the tab already lit
            // is a consumed no-op because the owner returns, not because this
            // site tests for it.
            //
            // THE READING IS NOT ON THIS ROW since 2026-08-08 (it was, as two
            // more slots and then as two labelled groups, for one day): a press
            // here passes the CURRENT reading through unchanged, so switching
            // walk keeps how you were reading it.
            //
            // THE READ-ONLY LOCK DOES NOT APPLY, deliberately: the gate that
            // refuses on a locked tab is on_key's, and nothing here dispatches a
            // key. A lock means hands off the piece's authored state, and the
            // history view is neither authored nor per-tab — refusing it would
            // stop a locked session from READING its own history, which is the
            // one thing the view is for. The padlock itself is not even drawn in
            // here (paint_tab_row), so there is no second target to test for.
            //
            // Shift-exact, like every other non-shift-admitting button on these
            // rows: a shift press is a consumed nothing rather than the plain
            // act.
            if (app.history_mode.active) {
                if (button == GuiMouseButton::Left && !mods.shift) {
                    // EACH SLOT NAMES ITS OWN WALK, in painted order — which is
                    // what a DIRECT selector is, against the keyboard's cycle.
                    // A press that lands on neither (the row's empty tail past
                    // the last tab) falls out of the walk having claimed
                    // nothing, the band claim above having already consumed it.
                    struct TabWalk {
                        RedesignButton       id;
                        GuiHistoryWalkSource source;
                    };
                    static constexpr TabWalk kTabWalks[] = {
                        {RedesignButton::TabA, GuiHistoryWalkSource::Commit},
                        {RedesignButton::TabB, GuiHistoryWalkSource::Local},
                    };
                    for (const TabWalk& t : kTabWalks) {
                        if (!redesign_button_hit(app, t.id, x, y)) continue;
                        set_history_reading(t.source, app.history_compare());
                        break;
                    }
                }
                return;
            }
            if (button == GuiMouseButton::Left) {
                // THE ACTIVE TAB'S PADLOCK IS A SECOND TARGET INSIDE ITS TAB,
                // and the row's only one — spelled here rather than in the chord
                // table for the reason Settings is: its action is NOT A CHORD
                // the table can carry. It is BARE `o`, which toggles
                // active_view_state(app).read_only and nothing else, so the
                // padlock is that key's pointer affordance on the tab whose
                // state it names. Dispatched through on_key like every other
                // redesign button, so every keyboard gate applies identically
                // and no second writer of read_only exists.
                //
                // The rect is published ONLY for the active tab (contract at
                // AppState::tab_lock_rect), and since the slot became permanent
                // (2026-08-01) it is non-zero on every painted frame — so this
                // test is "the user clicked the active tab's lock", and the
                // dispatch TOGGLES: it locks a writable tab and unlocks a
                // read-only one, which is exactly what bare `o` does. Everything
                // else in the row — the inactive tab's whole box, its lock
                // included — falls to the chord table's Ctrl+Tab.
                // Shift-exact is refused like every other non-admitting button.
                const GuiRect& lk = app.tab_lock_rect;
                if (!mods.shift && lk.w > 0 && lk.h > 0 &&
                    rect_contains(lk, x, y)) {
                    GuiInputState chord{};
                    on_key(GuiKeys::O, chord);
                } else {
                    dispatch_redesign_chord(x, y, mods);
                }
            }
            return;
        }
    }
    {
        const GuiRect icon_row = top_icon_row_area(app);
        if (rect_contains(icon_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) dispatch_redesign_chord(x, y, mods);
            return;
        }
    }
    // ROW 8 — THE TRANSPORT ROW (2026-08-11), the block's fifth member on the
    // block's own terms: the band is the BOTTOM strip's lane 1, between the
    // waveform and the status row, and everything else is the shape above —
    // below the modal gates (a prompt or a bottom-strip editor swallows the
    // press; the pointer-transparent flag editor does not, and its KEYBOARD
    // modality then answers the dispatched chord exactly as it answers the
    // key), above the loading/empty guard, ctrl/alt strict no-ops, and every
    // press in the band that is not on a button a consumed nothing. The
    // arrows' hold-repeat arm lives inside the shared dispatch body
    // (ToolbarChord::repeats), not here.
    {
        const GuiRect transport_row = bottom_transport_row_area(app);
        if (rect_contains(transport_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) dispatch_redesign_chord(x, y, mods);
            return;
        }
    }
    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    // The waveform BAND spans the full window width (top.w), not the effective
    // width (area.w): the <=15 px inert right gutter counts as a waveform click
    // by the user's lights, so a plain press there still reaches the waveform
    // branch — the upper half clears the selection (it has no column to seat a
    // playhead, so that is all it does), the lower (scrub) half returns
    // silently (no launch position exists, and a scrub press touches no
    // selection anyway). The gutter is 0 px at the deployment widths
    // (1920/2560/3840 are multiples of 16), so this only matters off-deployment.
    const bool inside_waveform =
        x >= area.x && x < top.x + top.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top = rect_contains(top, x, y);
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Defensive: a second press during a drag is ignored (left button
    // should still be held down for a drag to exist).
    if (app.drag.active) return;
    if (app.trim_drag.active) return;

    // THE `h` HISTORY MODE's pointer gate — the sibling of the on_key allowlist,
    // and the second and last of the mode's two gates.
    //
    // PLACED BELOW THE FOUR REDESIGNED ROWS' BAND CLAIMS ON PURPOSE. Those rows
    // dispatch their buttons as synthesized CHORDS through on_key, so they are
    // already covered by the keyboard gate — Save, Undo, Redo, Render and the
    // view bar drop there exactly as their keys do, with no second membership to
    // keep in step — and letting them through here is what keeps that single
    // coverage true. TWO PRESS ROUTES IN THESE ROWS DISPATCH NO CHORD (re-derived
    // 2026-08-06): the Settings and Navigation anchors, which have none and are
    // shut at toggle_dropdown instead, and — WHILE THIS MODE STANDS — the A/B TAB
    // PAIR, which the tab row's own band claim intercepts above and turns into
    // set_history_reading directly (the walk selector, deliberately not a
    // chord: the keyboard twin is Ctrl+Tab, claimed a line above the allowlist,
    // and the pair's own chord is the A/B switch the mode consumes). Both
    // exceptions are refusals or acts decided ABOVE this gate, so neither leaves
    // the mode uncovered.
    // The double-click SNAPSHOT is handed in because the mode has a double-click
    // act of its own (the trim bar's framing) and this function's own field was
    // cleared at the top of the press; nothing else about the call is special.
    if (app.history_mode.active &&
        handle_history_mode_press(button, x, y, mods, dc_at_press)) {
        return;
    }

    // RIGHT-CLICK ANYWHERE ON THE WAVEFORM IS A SCRUB (architect 2026-08-01) —
    // the right button's FIRST and ONLY binding in the product. The lower-half
    // LEFT scrub is untouched; this adds the same one act over the waveform's
    // FULL HEIGHT, so the upper half's placement press and its region drag keep
    // the left button while the right one auditions without disturbing them.
    //
    // ONE OWNER: it calls scrub_press_at, the derive-column-and-act body the
    // left press already calls — the gutter no-op, the clamped column, the one
    // scrub_act_at, and "arms nothing" all come from there, so the two entries
    // cannot drift.
    //
    // BARE-EXACT (strict modifier validation): ctrl, shift and alt carry real
    // waveform bindings for the LEFT button (the strip drag, the region former,
    // the grab-pan) and none of them has a right-button meaning, so any modified
    // right press stays the standing no-op.
    //
    // NO GESTURE MAY BE IN FLIGHT. The two guards above cover only the marker
    // and trim drags; the strip drag, the grab-pan, the region drag, the editor
    // text drag and the two pendings are all reachable with the left button held
    // while a right button goes down — and a captured strip drag keeps
    // delivering button events under the pointer lock. So this gate is
    // any_pointer_gesture_active (app_state.h), the one authoritative "some
    // pointer gesture is in flight" predicate, rather than a second hand-written
    // list. The scrub itself is not a gesture and never appears in it.
    //
    // GATE PROFILE — IDENTICAL TO THE LEFT SCRUB'S, by position: everything that
    // stops the left press before it reaches scrub_press_at has already run
    // above (the prompt swallow, the four bottom-strip modal editors, the open
    // dropdown, the five redesigned rows' band claims, the loading / empty-audio
    // return). Read-only tabs scrub as ever (playback is navigation), the
    // selection / cursor / region / follow are untouched, and no double-click
    // candidate is seeded.
    // THE EDITOR LIFECYCLE RUNS ON BOTH BUTTONS (architect 2026-08-01, closing
    // the one deliberate divergence this arm shipped with — a right press used
    // to leave an open flag editor standing): a right press outside the box
    // tears the edit down exactly as a left press does, through the SAME owner
    // (close_top_flag_editor_for_outside_press), so there is no second recipe to
    // drift. It runs FIRST in the arm — ahead of this arm's own modifier and
    // in-flight-gesture gates and ahead of the scrub, so a modified right press
    // closes the edit exactly as a modified left press does and an audition
    // starts with the editor already gone. The two arms sit at the SAME depth
    // otherwise: both are below the modal, popup and band claims, so a right
    // press swallowed up there closes nothing, and neither does a left one. A
    // right press INSIDE the box is still nothing at all: the box is the field,
    // and the right button has no meaning on it.
    if (button == GuiMouseButton::Right) {
        close_top_flag_editor_for_outside_press(x, y);
        if (ctrl || shift || alt) return;
        if (any_pointer_gesture_active(app)) return;
        if (!inside_waveform) return;
        scrub_press_at(x - area.x);
        return;
    }

    // Mouse authoring is home-view gated like the keyboard: placement arming is
    // gated by active_column_authoring_allowed, off-home selecting and landing but
    // arming NOTHING — with no exception anywhere, since 2026-07-29. W+target used
    // to arm the TEMPO drag on an eligible marker instead of the reposition drag
    // (the pointer half of the home-view binding's tempo exception); that whole
    // gesture is deleted (see marker_drag.h), so a W+target flag press now selects
    // and lands like any other off-home press and arms nothing at all. The
    // click-playhead / region-drag family below is
    // navigation, not authoring, and stays view-independent.

    if (button == GuiMouseButton::Left) {
        // Editor lifecycle, guard-free — THE SHARED OWNER, called from the right
        // press arm above too. A press in the editor's rendered lane text
        // already repositioned the caret / armed the text drag above (the F2.1
        // block) and returned; ANY other left press with the top flag editor
        // open CLOSES it without committing, and then FALLS THROUGH so the press
        // acts normally (arm a strip drag, select a marker, arm a marker drag,
        // land, place the playhead, ...). Placed ahead of every claim below so
        // the close really is unconditional. Consequence: a double-click on the
        // open editor's own marker is close-then-reopen — the first click
        // closes + selects + seeds a Marker candidate (+ arms the pending drag
        // on the flag part, writable), and the second consumes into a fresh
        // open. That IS the documented "double-click opens the editor"; there
        // is no own-marker special case.
        close_top_flag_editor_for_outside_press(x, y);

        // The marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item and that item is now its FLAG BOX alone
        // (hit_test_flag against the painter's stash — the rendered lane run
        // that used to be its second half died with the marker-text lane, and
        // with it the MarkerHit pair and its shared resolver marker_hit_at).
        // The TOP-STRIP hit feeds the plain/Shift/Ctrl
        // marker-click branches (plain = single-select + land the playhead on
        // the marker + double-click seed / consume + arm the pending marker
        // drag on the flag part, Shift = file-manager inclusive RANGE select
        // from the interaction's anchor (shift-held, else the adopted focus)
        // to the clicked marker + land on that range END, Ctrl = the individual
        // membership toggle + land on the resulting focus), so it is resolved
        // once here — every one of the three lands on its own focus.
        // The WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + region-drag arm; lower:
        // the scanner scrub, which touches no selection at all), and a Shift
        // press FORMS a region waveform-wide (from the playhead, or a marker
        // DROP that clears the selection; see the waveform block below) — so
        // no marker scan runs on the waveform at all (the invisible stem is
        // not a grab target). The plain DRAG never selects markers either
        // (SELECTION FLOWS DOWNWARD ONLY, architect 2026-07-23 — the region no
        // longer selects its contents; it leaves the selection empty).
        // Trim bounds are grabbed only by their top-strip endcaps /
        // the inter-endcap bridge on a PLAIN trim-bar press (route_trim_bar_press
        // below); a click over a bound's waveform stem is an ordinary waveform
        // click (the stem grab retired), so no trim hit test runs on the
        // waveform at all. Resolved ONCE here, ahead of every branch that
        // consumes it: the ctrl-exact membership toggle, the plain / Shift
        // marker click, and the empty-top-strip fallthrough (mh_index < 0 is
        // what makes a spot EMPTY) all read this one hit.
        int mh_index = -1;
        if (inside_top) mh_index = hit_test_flag(app, audio, x, y);
        // THE STEM IS A SECOND SURFACE OF THE SAME ITEM (architect 2026-08-01):
        // a press within a few px of an ENABLED marker's stem column, in the
        // WAVEFORM'S UPPER HALF, resolves to that marker and routes through the
        // very same click bodies its flag does — select / land / arm / open,
        // nothing restated. It is resolved HERE, beside the flag hit, so exactly
        // one `mh_index` reaches every branch below and the two surfaces cannot
        // answer differently. The full contract (why upper-half only, why the
        // painter's stash, the tie rule) is at hit_test_marker_stem, app_state.h.
        //
        // THE STEM SURFACE IS PLAIN-EXACT (architect 2026-08-01, second pass):
        // the SHIFT range select and the CTRL membership toggle bind to the FLAG
        // ALONE, because both modifiers already mean something else ON THE
        // WAVEFORM — ctrl is the strip drag and shift is the region former — and
        // the stem lives in the waveform area, standing over pixels those two
        // gestures own. So a modified press near a stem is NOT a marker hit at
        // all: it falls through to the waveform's own ctrl / shift gesture at
        // that column, which is the answer the surface underneath promises. The
        // FLAG keeps all three clicks (it is in the top strip, where neither
        // waveform gesture reaches), and the plain stem click is untouched —
        // select, land, arm the pending drag, seed the double-click.
        //
        // ONE HIT OWNER STILL: the gate is on this single resolution site, not a
        // second hit function, and the short-circuit leaves `mh_index` at -1 for
        // a modified press, so every branch below sees "no marker" from the one
        // index they all read.
        //
        // `stem_click` is what widens the marker branches' own gate from
        // "inside_top" to "inside_top OR a stem": the trim-bar and empty-lane
        // arms inside those branches are y-band tests that a waveform press
        // fails, so they fall through untouched.
        const bool stem_click =
            !inside_top && inside_waveform && !alt && !ctrl && !shift &&
            (mh_index = hit_test_marker_stem(app, x, y)) >= 0;

        // A top-strip press stops playback WHEN IT CLAIMS SOMETHING, never
        // merely because it landed in the strip (architect 2026-07-27). The
        // stop is the price of an authoring or a navigation act — a marker
        // select, a trim bound set, a trim-bar consume — and continuing audio
        // during authoring / text editing is the wrong default, so each of
        // those acts calls stop_playback_if_playing ITSELF at its own site
        // below. THE STOP IS INTENTIONAL, NOT POSITIONAL: a press that claims
        // NOTHING changes no state at all, so there is nothing for a stop to
        // protect and a live audition survives it. A claim that can still
        // REFUSE goes one step further (architect 2026-07-27): its stop sits
        // INSIDE the refusal gate, at the latest point before the mutation, so
        // a claimed-but-refused press (a bound set over a
        // degenerate audio/geometry state, or at a column not STRICTLY INSIDE the
        // partner bound — the 2026-08-01 guard)
        // is as playback-inert as an unclaimed one. That covers every modified
        // combination the branches below reject (alt on a marker, ctrl or alt
        // on an empty flag/triangle spot, ctrl+alt, shift+alt, ...), a
        // SHIFT-exact trim-bar press (trim is transparent to shift), a
        // SHIFT-exact empty flag/triangle-lane press (shift binds nothing on
        // that lane — the waveform's shift region former does not extend to
        // it), every empty marker-text-lane spot, and the inter-lane gaps — all
        // of which end at the inert top-strip return far below.
        // The EMPTY FLAG/TRIANGLE lane's PLAIN press is the one ACTING press
        // that still does not stop: it is the waveform-upper-half's twin (the
        // empty flag/triangle-lane parity press, architect 2026-07-23), and a
        // live session RESEEKS there
        // rather than dying, through place_playhead_and_arm_region. That
        // exemption is stated at the branch itself and is PLAIN-ONLY by
        // construction — the branch matches no modified press at all, so shift
        // sits with ctrl and alt in the inert list above and stops nothing
        // there either.
        // Waveform clicks keep playback alive as ever — the per-press reseek to
        // the click sample happens at the playhead-drag press site below, gated
        // on was_playing && sample != playhead_at_entry. Capture the entry
        // state up front, AHEAD OF EVERY STOP below, so all the downstream
        // branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // Alt-exact left press: on the waveform it arms the captured grab-pan;
        // on the MERGED TRIM SURFACE's inter-cap BRIDGE span it arms the
        // BRIDGE (pair) drag — ALT+DRAG MOVES THE TRIM WINDOW since the
        // fourth glass session (architect 2026-08-11, his vocabulary ruling:
        // ctrl = zoom, ALT = move/pan — alt-drag pans the waveform, so
        // alt-drag MOVES the trim window; the drag lived one session on
        // ctrl+drag, whose motionless-release BEGIN set stays ctrl's,
        // untouched below). Alt-exact anywhere else (a top-strip marker
        // included) does nothing further HERE — the land now lives on the
        // PLAIN marker click below. On a markerless off-span spot that
        // "nothing" is a STRICT no-op, playback included: alt claims no other
        // top-strip gesture, so no stop runs over it and a live audition
        // keeps playing; alt+drag from a marker is inert by ruling.
        //
        // The waveform grab-pan: continuous 1:1 pan of the viewport by the
        // per-event pixel delta (see on_motion). It CAPTURES the pointer
        // (begin_strip_pointer_capture, the same cursor-hide + lock the zoom
        // strip uses) so the pan travels infinitely under unbounded virtual
        // coordinates while the viewport clamps at the song walls — pan-only, no
        // zoom axis and no anchor stem (the stem is the zoom pivot affordance,
        // gated on strip_drag.active). Navigation-class: allowed in read-only,
        // never touches the playhead or selection. It deliberately does NOT
        // override follow — a pan during playback moves the view along with the
        // audio rather than signaling a stop, unlike the marker / trim / playhead
        // gestures. A motionless Alt press-release commits nothing but the brief
        // cursor hide/reappear (the scroll happens on motion).
        if (alt && !ctrl && !shift) {
            // THE BRIDGE (pair) DRAG — alt's one top-strip claim (2026-08-11,
            // the modifier swapped off ctrl at the fourth glass session):
            // inside the inter-cap span, arm the same pending an endcap press
            // arms (both=true; point_in_trim_bridge_span self-gates on the
            // merged band's y). Nothing mutates until the threshold — a
            // motionless alt release is a consumed nothing (the pending's
            // release arm disarms; alt carries NO deferred click, the BEGIN
            // set staying ctrl's own vocabulary). No stop here — the drag
            // stops at its first accepted bound change, like every trim drag.
            if (inside_top) {
                if (point_in_trim_bridge_span(app, audio, x, y)) {
                    arm_pending_trim_drag(/*is_begin=*/true, /*both=*/true,
                                          x, y);
                }
                return;
            }
            if (inside_waveform) {
                app.scroll_drag = ScrollDragState{};
                app.scroll_drag.active = true;
                app.scroll_drag.last_x = x;
                // PAN IS THIS GESTURE'S CUE (the alt-over-waveform zone), and the
                // capture release hands it back — see arm_strip_drag_at's twin
                // above and the contract at GuiPlatform::begin_pointer_capture.
                begin_strip_pointer_capture(GuiCursorKind::Pan);
            }
            return;
        }

        // Ctrl-exact left press splits by surface. On a top-strip MARKER it is
        // the individual membership toggle + land on the resulting focus (the
        // marker claim below). On the WAVEFORM it arms the dual-axis strip drag
        // (StripDragState / apply_strip_drag_at) — THE GESTURE'S ONE ENTRY
        // since the trim surface arc (2026-08-11) deleted the ruler entry row
        // 5 had given it (the ruler is trim surface now; this press was
        // already the sole entry once before, between the zoom lane's deletion
        // and row 5, which is what proved one entry costs no capability): this
        // press has the gesture's full reach — the
        // cursor capture ("swallow"), the anchor stem, the edge clamp, and
        // dual-axis zoom+pan. The waveform strip-drag is
        // navigation-class: allowed in read-only, never touches the playhead or
        // selection — and a MOTIONLESS ctrl+waveform press-release commits
        // nothing at all (the ctrl+waveform selection clear is RETIRED,
        // architect 2026-07-23: ctrl is purely the zoom modifier on the
        // waveform; the 2026-08-05..06 playhead jump that briefly stood here was
        // ROLLED BACK 2026-08-06 and only its anchor stem survives, so this
        // press is once again the drag and nothing else).
        // Ctrl-exact on a MARKERLESS top-strip spot is a strict no-op except
        // the trim bar's BEGIN bound set (the claim below).
        if (ctrl && !alt && !shift) {
            // Ctrl-exact on a top-strip MARKER is the individual membership
            // toggle (the former shift behavior) — this AMENDS the "Ctrl keeps
            // only the letter chords" rule for the one marker surface (architect
            // 2026-07-23). It arms no drag, seeds/consumes no double-click, opens
            // no editor. Whether the toggle ADDED or REMOVED, the playhead lands
            // on the FOCUS the toggle leaves behind (architect 2026-07-28,
            // replacing the earliest-selected land): an ADD focuses the clicked
            // marker, a REMOVE of the focused member repairs the focus to the
            // largest remaining index, and a REMOVE of any other member leaves
            // the focus alone — so app.last_selected_marker is the one expression
            // for all three, and it is always a live member here (a non-empty
            // selection always carries a focus after either arm). ANY RESTING
            // REGION CLEARS, unconditionally and whatever the land did or did
            // not move: the region is trim scratch, never a selection visual, so
            // there is no result-size split here any more (the >=2 arm's extent
            // write died with the SPAN FORM, architect
            // 2026-07-30). Read-only allowed
            // (selection + playhead are navigation).
            //
            // THE FLAG IS THE WHOLE SURFACE HERE — `inside_top` alone, not the
            // stem's widened gate (architect 2026-08-01): the ctrl-exact
            // WAVEFORM press is the strip drag, and the stem stands ON the
            // waveform, so ctrl over a stem belongs to the drag. The gate is
            // spelled `inside_top` rather than `inside_top || stem_click`
            // because stem_click is FALSE for any modified press by its own
            // definition above; saying so directly keeps this branch from
            // reading as though a stem could still reach it. A markerless
            // top-strip ctrl press claims only the trim bar (BEGIN bound set,
            // next block) and is a strict no-op on every other lane — no-op in
            // the playback sense too, since only a CLAIM stops playback.
            if (inside_top && mh_index >= 0) {
                // The toggle is an act, so it owns its stop: selecting while a
                // session plays is the authoring case the top-strip stop
                // exists for. It runs AHEAD of the toggle and the land, like
                // every other claim's stop — and it stays at the TOP of the
                // branch because this claim cannot refuse: read-only is allowed
                // (selection is navigation), the hit index is >= 0 by the gate
                // above (so the mutator's idx < 0 guard is unreachable here),
                // and every path below changes membership.
                playback_lifecycle.stop_playback_if_playing();
                selection.toggle_selection_membership(mh_index);
                if (!app.selected_markers.empty())
                    land_playhead_on_marker(app, audio, viewport,
                                            app.last_selected_marker);
                // A marker click is a POINT command, so it takes any resting
                // scratch span with it (the clear-site list is at
                // clear_region_highlight, input_handler.h).
                clear_region_highlight(app, viewport);
                return;
            }
            // Markerless top-strip ctrl-exact press: the MERGED TRIM SURFACE is
            // ctrl's trim-bound vocabulary in TWO SHAPES since the trim
            // surface arc (architect 2026-08-11):
            //   * INSIDE the inter-cap BRIDGE span, the ctrl+CLICK bound-set
            //     DEFERS to the MOTIONLESS RELEASE (the press arms a pending
            //     purely as that deferral's vehicle, via
            //     PendingTrimDrag::set_begin_bound_on_release; a lost button
            //     disarms without setting, not being a clean click). THE
            //     BRIDGE (pair) DRAG NO LONGER LIVES ON THIS PRESS: it moved
            //     to ALT+DRAG at the fourth glass session (the architect's
            //     vocabulary ruling — ctrl = zoom, alt = move/pan; the alt
            //     branch above is its home), so a ctrl press that crosses the
            //     threshold in-span now DISARMS and drags nothing (the
            //     click-vs-drag split stands; only the drag's meaning left).
            //     The deferral itself STAYS — an at-press set would fire on
            //     every in-span press, and the release keeps the click's
            //     observable shape identical across the swap.
            //   * OFF the span, the click sets the BEGIN bound at press and
            //     arms the single-bound drag on it (REINSTATED architect
            //     2026-08-01 — ctrl is BEGIN and ctrl+shift is END;
            //     set_trim_bound_at_click refuses any value not STRICTLY
            //     INSIDE its partner and, being a SETTER, deselects past its
            //     refusals. IT NO LONGER REFUSES A READ-ONLY TAB: trim is
            //     band, not authored content, 2026-08-07).
            // EVERY other lane is a
            // strict no-op, falling through to the return below (the ctrl-click
            // clear on an empty marker spot is RETIRED, architect 2026-07-23:
            // ctrl-click in Ableton is just click, and ctrl stays the zoom
            // modifier here; the PLAIN empty marker-lane press below is the
            // surviving lane gesture — the waveform's own parity press, not a
            // bare clear). The four redesigned rows (lanes 0..3) were claimed far
            // above and never reach here.
            //
            // THE BAND IS THE CURRENT GEOMETRY OWNER, top_trim_surface_area —
            // the exact merged band the plain endcap grabs, the region former
            // and the span-framing
            // double-click claim, so paint, hit and every trim gesture read ONE
            // accessor and cannot drift.
            if (inside_top) {
                const GuiRect trim_band = top_trim_surface_area(app);
                if (y >= trim_band.y && y < trim_band.y + trim_band.h) {
                    if (point_in_trim_bridge_span(app, audio, x, y)) {
                        // The deferred click-set's vehicle: nothing mutates
                        // until a clean motionless release performs the BEGIN
                        // set; a threshold crossing DISARMS (the bridge drag
                        // is alt's now — the flag's contract, app_state.h).
                        // both=false so the pending's cursor cue names the
                        // set this press promises, not a drag it no longer
                        // carries. No stop here — the set carries its own,
                        // past its refusals.
                        arm_pending_trim_drag(/*is_begin=*/true,
                                              /*both=*/false, x, y);
                        app.pending_trim_drag.set_begin_bound_on_release = true;
                        return;
                    }
                    // NO stop here: the bound set has its own refusals
                    // (a degenerate audio/geometry state, a value not
                    // strictly inside its partner), and a refused press changes
                    // nothing, so there is nothing for a stop to protect. The
                    // stop lives INSIDE set_trim_bound_at_click, past every
                    // refusal and immediately ahead of the bound write.
                    // Set the BEGIN bound AND arm the single-bound drag on it, so
                    // motion drags it live (a motionless release rests the set;
                    // a refused set arms nothing).
                    set_trim_bound_at_click_then_arm_drag(/*is_begin=*/true, x, y);
                    return;
                }
            }
            if (inside_waveform) arm_strip_drag_at(x, y);
            return;
        }

        // Ctrl+Shift-exact: the MERGED TRIM SURFACE is its ONE claim — set the
        // END trim
        // bound at the click (ctrl is BEGIN, ctrl+shift is END; the same
        // reinstated pair, architect 2026-08-01, the band grown to the merged
        // surface 2026-08-11. set_trim_bound_at_click refuses
        // any value not strictly inside its partner — the adjust-only pair gate
        // died with the unset state 2026-07-30, a full pair always resting, and
        // the read-only refusal died 2026-08-07 with trim's reclassification as
        // band — and deselects as a SETTER past its
        // refusals). Ctrl+shift keeps its at-press set everywhere on the band —
        // no arm shares its shape there (the bridge drag is alt-exact and the
        // deferred BEGIN set ctrl-exact), so
        // it needs no deferral. Everywhere else Ctrl+Shift stays a strict
        // no-op, playback
        // included, falling to the return below.
        if (ctrl && shift && !alt && inside_top) {
            const GuiRect trim_band = top_trim_surface_area(app);
            if (y >= trim_band.y && y < trim_band.y + trim_band.h) {
                // NO stop here either: like the BEGIN set above, the stop sits
                // inside set_trim_bound_at_click past that act's refusals, so a
                // refused END set leaves a live audition alone.
                // Set the END bound AND arm the single-bound drag on it.
                set_trim_bound_at_click_then_arm_drag(/*is_begin=*/false, x, y);
                return;
            }
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's ENDCAP drag on the plain trim-bar press (the
        // bridge drag is the alt branch's, far above), so
        // every remaining modified combination — Ctrl+Alt (a strict no-op),
        // Ctrl+Shift off the trim bar (its one claim is the END bound set
        // above), Shift+Alt, Ctrl+Alt+Shift, ... — no-ops
        // here. Only a plain
        // or Shift-on-the-top-strip base press proceeds (Shift adjusts the
        // marker selection). Alt is POINTER-ONLY vocabulary — the Alt+wheel
        // stepped pan, the Alt+drag captured grab-pan and, since the fourth
        // glass session (2026-08-11), the alt bridge drag that MOVES the trim
        // window — all claimed at the alt branch above, never here. On the
        // keyboard alt survives only in the FOUR Ctrl+Alt
        // render / propagate chords (Ctrl+Alt+R, Ctrl+Alt+Shift+R,
        // Ctrl+Alt+P, Ctrl+Alt+Shift+P) — every other alt keybinding was retired
        // 2026-07-28, so nothing here defers to one.
        // Discarding a press here is TOTAL: it claimed
        // nothing, so it stopped no playback on the way down either — the stops
        // live at the claims above and below, never on the route to this gate.
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area a plain press splits by
        // HALF: the UPPER half clears the marker selection (deselect-all),
        // places the playhead at the clicked column, and arms the
        // region-select drag; the LOWER half is the scrub surface (one scrub
        // act, nothing else) — neither ever SELECTS a marker. A Shift press on
        // the waveform instead FORMS a region waveform-wide
        // (the former / marker drop, one-shot — see the waveform block). In the top strip a plain
        // TRIM-BAR press arms a trim ENDCAP drag (claimed ahead of the
        // marker select; the bridge drag is alt's, above); otherwise a marker click — its FLAG BOX, the marker's
        // one pointer item — is the whole selection interface, BOTH views. Plain
        // click: single-select, LAND the playhead on the marker (below), and ARM
        // a pending marker drag (moves the marker if the pointer crosses the
        // threshold, else a pure click). Shift+click: a
        // file-manager INCLUSIVE RANGE select from the interaction's anchor
        // (shift-held, else the adopted focus) to the
        // clicked marker (the range end = FOCUS), which LANDS the playhead THERE.
        // The individual membership TOGGLE moved to Ctrl+click (the ctrl-exact
        // marker claim in the earlier branch; whether it adds or removes it lands
        // on the focus it leaves, an empty selection landing nothing). The
        // plain / shift / ctrl land makes every such marker click a land route
        // alongside the Tab family and `c` (which additionally recenter /
        // re-zoom), and all three land ON THEIR FOCUS (architect 2026-07-28), so
        // the playhead and the focused flag are coincident before any subsequent
        // drag or nudge — nothing is towed.
        if (inside_top || stem_click) {
            // A PLAIN STEM CLICK ENTERS HERE TOO (2026-08-01). Its y is in the
            // waveform, so every band test inside this branch — the merged
            // trim surface below, the empty-marker-lane parity
            // press — simply fails for it, and it lands on the one arm it is
            // for: `mh_index >= 0`, the marker click. Nothing in those bodies
            // knows or needs to know which surface resolved the index.
            //
            // A SHIFT PRESS NEVER ARRIVES BY THE STEM: stem_click is plain-exact
            // (its definition above), so shift over a stem leaves both halves of
            // this gate false and falls through to the waveform block, where
            // shift is the region former. Shift reaches the marker RANGE select
            // through the FLAG BOX alone, which is `inside_top`.
            //
            // THE MERGED TRIM SURFACE (top_trim_surface_area — trim bar +
            // ruler + their gap, the trim surface arc, architect 2026-08-11:
            // "make the timestamp lane the same as the trim") is claimed
            // BEFORE the marker single-select; it and the marker lane (lane 6)
            // are disjoint y-bands, so
            // this contends with nothing: a marker-part press falls to the marker
            // handling below. The PLAIN click consumes the span-framing
            // double-click, else arms an ENDCAP drag, else ARMS THE BAND'S
            // REGION FORMER — a plain drag anywhere off a cap DRAWS THE REGION
            // ("let's experiment with making the region draw a drag on the
            // trim bar"), a motionless release staying the standing CONSUMED
            // NOTHING (plus the double-click seed); the bound-set clicks
            // are the ctrl / ctrl+shift
            // claims above and the bridge drag is the ALT branch's, further
            // above. THE RULER'S STRIP-DRAG ZOOM ENTRY IS DELETED with
            // the merge (its block stood here) — zoom lives on the waveform:
            // ctrl-drag, the keys, touch two-finger. A SHIFT-exact band press
            // still claims nothing —
            // trim is transparent to it, and the shift fall-through below is
            // inert here (the flag boxes' y-band excludes the band), so it
            // ends at the inert top-strip return, having touched nothing at
            // all: it is the only press that reaches the band without
            // claiming it (ctrl and alt were discarded at the gate above), and
            // it stops no playback — nor does the plain press: the band's
            // stop belongs to the DRAG's first accepted bound change
            // (input_trim.cpp), and the region former stops nothing anywhere.
            const GuiRect trim_bar = top_trim_surface_area(app);
            const bool in_trim_surface =
                (y >= trim_bar.y && y < trim_bar.y + trim_bar.h);
            if (!shift && in_trim_surface) {
                // Plain merged-band press. An endcap hit ARMS the trim drag,
                // IN EITHER TAB since 2026-08-07 — the
                // band's read-only return is deleted below. Either
                // way the band CONSUMES the press — it never falls to the
                // marker handling.
                // A PLAIN BAND CLICK THAT NEVER BECOMES A DRAG IS A
                // CONSUMED NOTHING (architect 2026-07-30). Its entire act was
                // PUBLISHING the trim window as a region highlight, and that
                // coupling is retired outright with the SPAN FORM — so the two
                // highlight-only arms that stood here (the read-only press's
                // direct sync and the writable UNCLAIMED-spot sync) are gone, and
                // so is the pre-route stop that served them: keeping either would
                // make a no-op click stop an audition and destroy a selection for
                // nothing. The trim drag keeps both — it takes the setter's
                // deselect
                // and the trim-mutation stop at its FIRST ACCEPTED bound change
                // (input_trim.cpp), which is where a trim commit actually happens.
                //
                // THE SPAN-FRAMING DOUBLE-CLICK, rehomed onto the trim band from
                // the deleted zoom lane (architect 2026-07-31) and grown to the
                // MERGED band with the arc: the whole surface —
                // endcaps, bar and ruler rows alike — carries it, since it
                // frames rather than grabs. The CONSUME runs FIRST, ahead of
                // every arm: a candidate from the previous motionless press-
                // release on this band, inside kDoubleClickMs and the slack on
                // both axes, spends this press on the framing command and returns
                // — no drag armed, no bound touched, playhead and selection
                // untouched, and allowed in read-only for its own reason — it is
                // pure navigation, which it was before the whole band became
                // read-only-legal and still is (all modal gates sit far above).
                // The surface tag is what keeps
                // a marker or editor candidate from consuming here, and the TEST
                // is shared with the history mode's own band double-click
                // (trim_bar_double_click_at) so the two cannot drift on the
                // gesture while running different commands on it. It DIVERGES
                // from the bare `0` key, which only ever reaches the whole song
                // (and, from there, `c`); this frames the region, else a proper
                // trim sub-window, else the whole song.
                if (trim_bar_double_click_at(dc_at_press, x, y)) {
                    run_span_framing_command();
                    return;
                }
                // SEEDING is a RELEASE act (only the release knows the press
                // stayed still), so the press records its point and the release
                // decides — see TrimBarPressSeed. Recorded for BOTH plain arms
                // below: the endcap pending and the region former alike stay a
                // seeding click if motionless (the release's own !moved /
                // !trim_drag terms are what refuse a moved drag's seed).
                app.trim_bar_press = TrimBarPressSeed{
                    .active = true, .press_x = x, .press_y = y};
                // THE BAND'S READ-ONLY RETURN IS DELETED (architect 2026-08-07):
                // it was the sole read-only defense for the whole trim band,
                // and the ruling removed the thing it was defending — read-only
                // protects the AUTHORED MUSICAL CONTENT (the marker stores and
                // the engine settings), while trim is BAND, no more locked than
                // the viewport or the zoom beside it in ViewState. So a locked
                // tab arms the endcap drag and the region former here exactly
                // as a
                // writable one does, route_trim_bar_press below still carries no
                // read-only check (it never did), and the trim CURSOR cues over
                // this band follow by construction, reading those same routers.
                // The full ruling is at read_only_key_blocked
                // (input_key_dispatch.cpp).
                // THE ENDCAP OUTRANKS, as it always outranked (the router's own
                // order): a cap hit arms the single-bound pending and the press
                // is done.
                if (route_trim_bar_press(x, y)) return;
                // ELSE THE BAND'S REGION FORMER (the trim surface arc's second
                // half): anchor at the press column, the drag's moving end
                // following with the playhead riding it — the waveform
                // placement press's region arm re-expressed on this band
                // through the ONE arm (arm_region_drag_at, band variant). NO
                // press-time act: no playhead seat, no deselect, no dissolve
                // of a resting span — the band's motionless click must stay a
                // consumed nothing, so those defer to the threshold crossing
                // (RegionDragState::band, the motion path's crossing act). A
                // gutter press (no column) arms nothing at all.
                {
                    int rel = x - area.x;
                    if (rel >= 0 && rel < area.w) {
                        const int64_t anchor = clamp_playhead_to_live_domain(
                            playhead_frame_at_click_column(app, audio, rel),
                            app, audio);
                        arm_region_drag_at(anchor, x, y, /*band=*/true);
                    }
                }
                return;
            }
            if (mh_index >= 0) {
                // A marker click — plain or Shift — is
                // an act (select, land, arm, open), so it owns the stop for the
                // whole branch: selecting or editing under a live audition is
                // the case the top-strip stop was written for. One site ahead
                // of BOTH arms below (the range select, and the plain
                // single-select + land + editor open),
                // and it belongs at the TOP because NEITHER arm refuses:
                // read-only still selects and lands, the hit is >= 0 by the
                // gate (the mutators' idx < 0 guards are unreachable), and the
                // plain arm always single-selects and lands. Only the editor
                // OPEN can decline (read-only / P view / off home), and that is
                // a second act layered on a click that already committed.
                playback_lifecycle.stop_playback_if_playing();
                const int hit = mh_index;
                if (shift) {
                    // Shift+click is a file-manager INCLUSIVE RANGE select
                    // (architect 2026-07-23): the click ranges from the
                    // interaction's anchor — a LIVE anchor, else the ADOPTED
                    // FOCUS (architect 2026-07-23: plain-click A
                    // then shift+click B selects A..B; with nothing focused the
                    // click anchors on its own marker, selection {hit}). THE
                    // ANCHOR IS NOT KEYED TO THE PHYSICAL SHIFT HOLD (architect
                    // 2026-07-29): it SURVIVES a shift release and dies at the
                    // next membership replace, so a shift interaction re-started
                    // after a release ranges from the SURVIVING anchor, not from
                    // the focus — A..B, release, re-press, shift-click C gives
                    // A..C, the accepted delta of the falling-edge hook's
                    // deletion. The full contract and clear list live at
                    // app.shift_range_anchor's declaration (app_state.h). Each
                    // successive shift-click replaces the selection with the
                    // inclusive index range between that anchor and
                    // hit. The clicked marker becomes the range end = FOCUS
                    // (last_selected), and the playhead LANDS THERE (architect
                    // 2026-07-28, replacing the earliest-member land): focus and
                    // land no longer diverge on any click, so nothing is towed
                    // onto the focus by a later nudge. On an anchoring
                    // focus-less first click the selection is {hit} and hit is
                    // the focus, so the land is unchanged. Ctrl+click is the
                    // individual membership toggle (above, landing on its own
                    // resulting focus). It arms no drag, seeds/consumes no
                    // double-click, opens no editor. Allowed in read-only (selection + playhead are
                    // navigation). ANY RESTING REGION CLEARS whatever the
                    // result size (architect 2026-07-30): the region is trim
                    // scratch, not this selection's extent, so the >=2 arm's
                    // extent write died with the SPAN FORM and the split with
                    // it.
                    selection.select_range_from_anchor(hit);
                    // A range leaving exactly one selected shows its always-on stem;
                    // select_range_from_anchor owns the subject-change damage.
                    // The land target is the FOCUS the mutator just set, which is
                    // `hit` on both its arms — spelled as last_selected_marker so
                    // the three multi-select clicks read as one rule.
                    if (!app.selected_markers.empty())
                        land_playhead_on_marker(app, audio, viewport,
                                                app.last_selected_marker);
                    // A marker click is a POINT command and takes any resting
                    // scratch span with it.
                    clear_region_highlight(app, viewport);
                } else {
                    // ONE PLAIN MARKER PRESS, NO SPECIAL CASE FOR A SELECTED
                    // MEMBER (architect 2026-07-29, HORIZONTAL MOVEMENT IS A FOCUS
                    // ACT — the doctrine is at the head of position_nudge.h):
                    // a press on a member of a 2+ selection single-selects and
                    // lands IMMEDIATELY like a press on any other marker, and the
                    // drag it arms is an ordinary singleton drag. The two
                    // file-manager DEFERRALS that used to sit here — one per drag
                    // surface, each holding the click's committed act back so the
                    // drag could seed the intact group —
                    // died with the group drag itself; groups are never moved by
                    // any route, so nothing needs the selection to survive the
                    // press. (The second of those surfaces, the tempo drag, is
                    // gone outright — see marker_drag.h.)
                    // Plain marker click single-selects (both views; W's
                    // click-to-edit is retired — the editor now opens on Enter or
                    // this double-click). Selection is navigation, allowed in
                    // read-only.
                    selection.set_single_selection(hit);
                    // The clicked marker's flag BRIGHTENS here —
                    // set_single_selection damages the top strip, where the
                    // flags live. No stem work is
                    // owed on any arm, the click-an-already-selected no-op
                    // included: stems are class-colored and always on, so a
                    // membership change never creates, moves or recolors one.
                    // Covers the double-click-consume path
                    // below too (this single-select ran first).
                    // ...and LANDS the playhead exactly onto the marker (shared
                    // helper; see land_playhead_on_marker). Runs on EVERY plain
                    // marker click — the double-click-consume path below (whose
                    // first click already landed, leaving the second land a
                    // same-sample no-op) and the plain-select path.
                    land_playhead_on_marker(app, audio, viewport, hit);
                    // THE CLICK OWNS ITS CLEAR (architect 2026-07-29): a
                    // single-select click moves the playhead onto a marker, so
                    // any resting scratch span ends here — unconditionally, and
                    // whether or not the land moved anything. A
                    // re-click of the already-selected marker therefore clears
                    // a resting highlight too; that is the
                    // ruling and not an accident (the click says "the playhead is
                    // HERE, at this point"). The double-click-consume path rides
                    // this same site, so an editor open through it finds no span.
                    clear_region_highlight(app, viewport);
                    // Double-click: a Marker candidate for the SAME index within
                    // the window opens the flag editor, exactly like Enter on the
                    // focused marker (the click above already single-selected it).
                    // The surface + target tag prevents any trim-bar / editor
                    // candidate from consuming here, and a candidate seeded on
                    // one part consumes on the other — one surface. Read-only,
                    // P view (phase resets have no per-flag editor), and the
                    // off-home column (active_column_authoring_allowed false —
                    // the warp editor is source-view-only) refuse silently,
                    // matching Enter's allowlist / view refusal — the candidate
                    // is cleared and the press stays a plain second select. On a
                    // consumed open nothing is armed and no fresh candidate
                    // seeds (the editor now owns input).
                    bool opened_editor = false;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::Marker &&
                        dc.target == hit &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        if (app.active_markers_view != 'P' &&
                            !active_view_state(app).read_only &&
                            active_column_authoring_allowed(app)) {
                            // Every open route opens fully SELECTED (open-
                            // selected), so there is no clicked-glyph caret to
                            // seat — a press on the flag BOX and a press on the
                            // marker's STEM are the same open. A specific caret
                            // spot is a click inside
                            // the already-open editor (the F2.1 path).
                            flag_editor.enter_top_flag_edit(hit);
                            opened_editor = true;
                        }
                    }
                    if (!opened_editor) {
                        // SEED a Marker candidate at this PRESS — the one seed
                        // timing for the whole marker surface (the release-time
                        // seeding is retired). Press-seeding is safe for the
                        // flag part because a press that becomes a real drag
                        // drops the candidate at the threshold crossing (see
                        // on_motion's pending-marker-drag branch).
                        app.double_click = DoubleClickCandidate{
                            .surface = DoubleClickSurface::Marker,
                            .time_ms = monotonic_ms(),
                            .press_x = x, .press_y = y,
                            .target  = hit};
                        // A writable tab arms the pending REPOSITION drag on a
                        // plain marker press IN THE COLUMN'S HOME VIEW only —
                        // from EITHER surface, the flag box or its stem
                        // (2026-08-01): the drag tracks the pointer's x from the
                        // press point, which is the same gesture whichever the
                        // press started on. The old on_flag half of this test
                        // went with the marker-text lane's run in row 5;
                        // read-only selects but never arms (marker
                        // mutation refused). ONE DRAG, ONE GATE since 2026-07-29:
                        // the home-view split that used to arm the TEMPO drag
                        // instead in W view + TARGET view exactly — the pointer
                        // half of the home-view binding's tempo exception, with its
                        // predecessor-eligibility walk — is DELETED with that whole
                        // gesture (see marker_drag.h). So EVERY off-home flag press
                        // (W+target and P+source alike) selects and LANDS the
                        // playhead but arms nothing at all — the silent
                        // navigation-class refusal, marker motion being authoring.
                        if (!active_view_state(app).read_only &&
                            active_column_authoring_allowed(app)) {
                            app.pending_marker_drag = PendingMarkerDrag{};
                            app.pending_marker_drag.active  = true;
                            app.pending_marker_drag.marker  = hit;
                            app.pending_marker_drag.press_x = x;
                            app.pending_marker_drag.press_y = y;
                        }
                    }
                }
            } else {
                // Empty top-strip spot — no marker flag under the point (the
                // trim bar already returned above; mh_index < 0 here).
                // ONE lane to test: the flag and triangle lanes became the
                // single marker lane in row 5.
                const GuiRect marker_lane = top_marker_row_area(app);
                const bool in_flag_or_tri =
                    y >= marker_lane.y && y < marker_lane.y + marker_lane.h;
                if (in_flag_or_tri && !shift) {
                    // The empty marker-lane parity press (architect
                    // 2026-07-23): the empty lane works like the waveform upper half. A
                    // DOUBLE-CLICK consume creates a marker at the clicked
                    // position — the AUGMENTED drop, the same and only drop bare
                    // `s` performs (architect 2026-07-28: the lane double-click
                    // reuses the keyboard's machinery, so it follows it); the
                    // FIRST press seeds the candidate AND runs the placement body
                    // (deselect + playhead + region arm).
                    // PLAIN ONLY: a SHIFT press on the lane claims nothing at all
                    // and falls to the inert return below, exactly like the
                    // SHIFT-exact trim-bar press — shift has no meaning here (the
                    // waveform's shift region former does not extend to the lane),
                    // so it seeds nothing, places nothing, and leaves a shift+drag
                    // inert. Ctrl and alt never reach this branch (discarded at the
                    // strict-modifier gate above).
                    // NO STOP HERE, deliberately, and this is the one ACTING
                    // top-strip press that omits one: PARITY is the whole reason —
                    // a live session RESEEKS to the placed playhead
                    // (place_playhead_and_arm_region's was_playing arm) exactly as
                    // it does on the waveform upper half, so a double-click drop
                    // lands over a live session without cutting it off. Do not add
                    // a stop to make this branch look like its siblings — the
                    // omission IS the ruling.
                    const int click_rel_x = x - area.x;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::EmptyLane &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        create_marker_at_empty_lane(click_rel_x);
                        return;
                    }
                    // SEED an EmptyLane candidate at this PRESS (position-keyed;
                    // target unused). Cleared at the region-drag threshold
                    // crossing so a moved drag never carries one (on_motion).
                    app.double_click = DoubleClickCandidate{
                        .surface = DoubleClickSurface::EmptyLane,
                        .time_ms = monotonic_ms(),
                        .press_x = x, .press_y = y,
                        .target  = -1};
                    place_playhead_and_arm_region(
                        click_rel_x, x, y, was_playing, playhead_at_entry);
                    return;
                }
                // Every other empty top-strip spot: NOTHING AT ALL — no
                // playhead, no marker, no selection or region change, and no
                // playback effect either, because this press claimed nothing
                // and the stops all live at the claims. That covers the whole
                // marker lane outside every flag box — a box under the point is
                // a marker hit and never reaches this branch. The inter-lane
                // gaps, the SHIFT-exact trim-bar press and the SHIFT-exact
                // marker-lane press land here too, equally inert.
                return;
            }
            return;
        }

        // Waveform-area press: marker-blind for SELECTION (it never SELECTS a
        // hit marker — the stem is not a grab target; hit_test_flag runs only
        // for top-strip presses). ONLY THE PLAIN PRESS SPLITS BY HALF
        // (architect 2026-07-23): the UPPER half is the placement press — CLEARS
        // the selection (the deselect-all: a waveform click dismisses the marker
        // selection, the Ableton behaviour), drops the playhead at the clicked
        // column (no marker snap — the 3px marker-snap magnet already died with
        // the retired plain-drag scrub), reseeks a live scanner to it, overrides
        // follow, and arms the region drag — which also DISSOLVES any resting
        // highlight at this mouse-down (arm_region_drag_at clears it), so the
        // highlight vanishes on press whether the gesture becomes a click or a
        // fresh drag. The LOWER half is the SCRUB surface (the branch below):
        // the press runs one scrub act — stopping a live session, else starting
        // a fresh SCANNER session from the clicked frame — arming nothing (the
        // one-shot Ableton model) and touching nothing else.
        //
        // A SHIFT PRESS IS THAT SAME PLACEMENT PRESS, WAVEFORM-WIDE (architect
        // 2026-08-05, THE FORMER'S RESHAPE): the region now ANCHORS AT THE
        // CLICKED COLUMN rather than at the playhead or at the furthest selected
        // marker, so shift's press does exactly what the plain upper-half press
        // does — deselect, seat the playhead at the column, arm the drag from
        // there — and the drag then extends the span with the moving endpoint
        // carrying the cursor, which is what makes the PLAYHEAD LAND WHERE THE
        // MOUSE RELEASES. A MOTIONLESS shift click-release lands the playhead at
        // that column and rests NO region (the arm dissolved the old span at
        // mouse-down and nothing rebuilt one — the min-size dissolve's shape
        // reached by never forming a span at all), and a GUTTER press deselects
        // and does nothing else, landing no playhead because there is no column.
        // ONE RULE, BOTH HALVES, THE WHOLE WAVEFORM: shift's only remaining job
        // is to reach the placement press on the LOWER half, where the plain
        // press is the scrub. What died with the old anchoring is the whole of
        // it — the playhead-anchor read, the furthest-selected-marker argmax and
        // the one-act span it formed (form_region_span_to_click and its
        // non-dissolving arm went with them).
        // ctrl/alt already claimed their waveform-wide gestures above.
        {
            const int click_rel_x = x - area.x;
            if (!shift && waveform_lower_half(area, y)) {
                // THE PLAIN LOWER HALF IS THE SCRUB SURFACE, and the half test
                // is first on this path — before the deselect, because a scrub
                // press must not deselect. waveform_lower_half is the SCRUB
                // CURSOR's zone predicate too — one owner, so the cue and the
                // gesture cannot disagree about where the surface starts. The
                // press drives the SCANNER (not the cursor) — the scanner fields
                // are meaningful only while active (the standing contract), and
                // this gesture is exactly the launches-the-scanner-independently-
                // of-the-cursor consumer that contract anticipated. Every scrub
                // act is STOP-THEN-START-ON-THE-NEXT-CLICK (scrub_act_at, architect
                // 2026-07-27): a click over a LIVE session just stops it, and the
                // following click launches a fresh audition from the frame it lands
                // on — never a positional seek inside an old session. A refused
                // launch is a silent no-op, exactly Space's conventions. The scrub
                // is ONE-SHOT per click (architect 2026-07-23, the Ableton model):
                // the press arms NOTHING, a held press does nothing further, and
                // motion over the surface is inert — each click pays at most one
                // stop quiescence fence. It touches
                // NOTHING else: no selection change, no region change, no cursor
                // write, no follow override, no double-click seed, no drag
                // arm. Read-only allowed (playback is navigation). The gutter
                // (click_rel_x outside [0, area.w)) returns silently — no launch
                // position exists there, unlike the placement press's
                // deselect-then-return.
                //
                // The scrub press body (scrub_press_at): gutter no-op, clamped
                // frame from the column, one scrub act (stop a live session,
                // else launch), nothing armed. This is the LEFT button's entry
                // — the waveform lower half; the bare right press over the full
                // waveform height is the body's other caller (its own comment
                // is above, in this same handler).
                scrub_press_at(click_rel_x);
                return;
            }
            // Everything else on the waveform — the plain UPPER half and a
            // SHIFT-exact press at either height — is the placement press body,
            // shared verbatim with the empty flag/triangle-lane parity press
            // (place_playhead_and_arm_region): deselect-all, drop the playhead
            // at the clicked column, reseek a live session, override follow, arm
            // the region drag. The clear runs FIRST inside the helper, so an
            // inert-gutter click still deselects. ctrl/alt returned earlier, so
            // the shift arrival here is shift-exact — a shift+modified
            // combination never reaches this branch.
            place_playhead_and_arm_region(click_rel_x, x, y,
                                          was_playing, playhead_at_entry);
        }
    }
    // Wheel events no longer reach on_button_press; they arrive coalesced
    // per pointer frame through on_wheel -> handle_wheel.
}

void GuiInputHandler::finalize_editor_text_drag() {
    const ActiveEditorText g = active_editor_text(app, audio);
    if (g.valid) {
        // A press that never moved leaves a plain caret and no selection,
        // matching the existing click-to-caret.
        if (g.ed->selection_anchor == g.ed->cursor_pos)
            g.ed->selection_anchor = -1;
        if (g.bottom_strip) viewport.invalidate_status_row_area();
        else                viewport.invalidate_top_strip();
    }
    app.editor_text_drag.active = false;
}

void GuiInputHandler::arm_region_drag_at(int64_t anchor_frame, int x, int y,
                                         bool band) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
    app.region_drag.band         = band;
    // Clear any resting region immediately at press: a plain upper-half
    // waveform press dissolves an existing highlight on mouse-down (the plain
    // canvas ground repaints back now, not at release; a lower-half scrub press never
    // reaches here — it arms no region drag and leaves the region alone). A
    // moved drag rebuilds a fresh region live; a
    // motionless press-release simply leaves it cleared, and an Esc mid-drag
    // changes nothing at all (no cancel) — the dissolve at mouse-down is final
    // either way. Same dissolve shape as
    // the navigation clears, so it shares clear_region_highlight.
    // THE BAND ARM DEFERS THE DISSOLVE (the trim surface arc, 2026-08-11): the
    // merged trim surface's motionless click is a CONSUMED NOTHING by standing
    // rule, and a resting span is the aiming scratch the user may be about to
    // FRAME with this very band's double-click — so its press dissolves
    // nothing, and the dissolve runs with the deselect at the threshold
    // crossing instead (the motion path's crossing act, gated on
    // RegionDragState::band, whose comment carries the divergence).
    if (!band) clear_region_highlight(app, viewport);
}

int64_t GuiInputHandler::place_playhead_at_click_column(
        int click_rel_x, bool was_playing, int64_t playhead_at_entry) {
    // THE PLACEMENT PRESS'S PLAYHEAD HALF — the column-to-cursor recipe alone,
    // with no selection and no region in it, so the `h` history mode can seat a
    // playhead by exactly the arithmetic and exactly the playback regime the
    // live press uses (the mode's own arm is in handle_history_mode_press; it
    // clears the MODE's focus where the live body clears the store selection,
    // and arms the same drag). Returns the seated frame, or -1 in the gutter.
    const GuiRect area = waveform_area(app);
    if (click_rel_x < 0 || click_rel_x >= area.w) return -1;
    // Clamp the click column's frame into the live domain ONCE and hand that
    // same clamped value back to the caller, which passes it to the region arm:
    // move_playhead_to clamps internally, but the region former stored the raw
    // value. At a fractional flush-right zoom the painter-quantized wall
    // (q = nearbyint(spp*W)/W) differs from the click conversion's
    // current_samples_per_pixel, so the last visible column's frame can compute
    // to domain_total — one past [0, domain_total-1], which the display-state
    // validator would clear wholesale — so both formers clamp. The clamp also
    // makes -1 a sentinel no seated frame can collide with.
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    if (was_playing && sample != playhead_at_entry)
        playback_lifecycle.reseek_keeping_alive(sample);
    // SUPPRESS THE CHASE for this session: the user placed the cursor
    // deliberately, so follow must not page the viewport away from it. One of
    // two producer classes (the other being any viewport pan); the inventory and
    // the clearing rule live at the flag's declaration, app_state.h.
    if (was_playing) app.follow_overridden_for_session = true;
    return sample;
}

void GuiInputHandler::place_playhead_and_arm_region(int click_rel_x, int x,
                                                    int y, bool was_playing,
                                                    int64_t playhead_at_entry) {
    // The waveform placement press body — THREE CALLERS, re-derived by grep:
    // the plain UPPER-HALF waveform press, the SHIFT-exact waveform press at
    // EITHER height (2026-08-05, when the former's anchor moved to the click and
    // shift became this same press), and the empty flag/triangle-lane parity
    // press. The clear runs FIRST, before the shared body's gutter early-return,
    // so an inert-gutter click (no column to seat a playhead) still deselects.
    selection.clear_selection();
    const int64_t sample = place_playhead_at_click_column(
        click_rel_x, was_playing, playhead_at_entry);
    if (sample < 0) return;
    arm_region_drag_at(sample, x, y);
}

void GuiInputHandler::create_marker_at_empty_lane(int click_rel_x) {
    // The empty flag/triangle-lane double-click marker create: the bare-`s` drop
    // equivalent, and like bare `s` it is the AUGMENTED drop in both columns —
    // the copy-previous owner in W, the lead-in reset in P. Gated exactly like
    // the keyboard `s`: home-view (active_column_authoring_allowed) and read-only
    // refuse SILENTLY. Place the playhead on the clicked column first — the
    // double-click's first press already moved it there, so this is a harmless
    // same-value repeat that also covers any first press whose placement was
    // undone in between. The standing _at_playhead drops then author at that
    // playhead, taking the full create path (walls, undo, selection, and the
    // point command's own region collapse at the drop chokepoint) — the
    // phase-reset lead-in additionally offset N/2 before the playhead and landing
    // the playhead per that drop's rule.
    const GuiRect area = waveform_area(app);
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    if (active_view_state(app).read_only) return;
    if (!active_column_authoring_allowed(app)) return;
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    // active_column_authoring_allowed guarantees the column is in its HOME audio
    // view: W -> source (warp drops legal), P -> target (phase-reset drops legal,
    // the lead-in's target requirement satisfied), so the view dispatch below
    // needs no extra audio-view guard.
    if (app.active_markers_view == 'P')
        phase_resets.drop_phase_reset_lead_in_at_playhead();
    else
        warpops.drop_copy_previous_at_playhead();
}

// NO ARM BELOW TOUCHES THE POINTER CURSOR, and that is the 2026-08-03 ruling
// rather than an omission. Every arm here ends a gesture, and the zone map
// refuses every cue while one is live (the trim gesture's named exception keeps
// its own kind), so the moment an arm clears its state the kind on screen
// becomes a claim about a gesture that no longer exists. It used to be each
// arm's job to re-resolve, strictly after its own teardown; the cursor now has
// ONE owner — the run loop's per-iteration tail — and a release is dispatched
// inside an iteration, so the correction lands at that iteration's boundary,
// past every arm's teardown by construction. What was an ordering rule repeated
// at eight sites is now a property of where the owner sits.
// THE CAPTURED GESTURES NEEDED NO SPECIAL CASE THEN AND NEED NONE NOW, because
// THE GUI IS NOT THE PLACE THAT KNOWS. A capture makes app.last_mouse_x/y the
// unbounded VIRTUAL travel, a point the pointer does not occupy, so a kind
// derived from it would not be a stale cue but a WRONG one — and the platform
// DROPS it, unrecorded, for as long as it has no real position
// (GuiPlatform::set_cursor_kind; the span outlasts the lock and ends at the
// compositor's next absolute event). What comes back at the restore is the kind
// the GESTURE ITSELF STAMPED when the capture began — Zoom for the strip drag,
// Pan for the alt-pan, the cue it wears by identity — and the drop above is what
// protects it: nothing named from a virtual position is ever recorded over the
// stamp (the full story at GuiPlatform::begin_pointer_capture).
// Without a capture — the two optional protocols absent — nothing is virtual and
// nothing is dropped: the pointer is where the GUI thinks it is, the cursor was
// never hidden, and the loop's next tail is the ordinary end-of-gesture
// re-resolve that puts the true cue back.
// `mods` IS UNNAMED HERE because nothing in this handler reads it any more: the
// per-arm cursor refresh was its only reader (it wanted the modifier truth the
// platform delivered WITH this release), and the owner that replaced those calls
// is handed the live state by the platform. The parameter stays in the signature
// — it is the platform's release callback shape — so an arm that ever needs the
// modifiers just names it again.
void GuiInputHandler::on_button_release(GuiMouseButton button, int x,
                                        int y, GuiInputState /*mods*/) {
    // THE DROPDOWN'S RELEASE, above every gate: while it is open it owns the
    // pointer, and its items are the redesign's one act-on-release surface. It
    // takes THIS RELEASE'S coordinates because it derives the acted-on item from
    // them under a live press claim, rather than trusting an arm that may have
    // been resolved before the item rects were published (the full argument is at
    // the definition).
    if (button == GuiMouseButton::Left && app.dropdown.open()) {
        if (finish_dropdown_release(x, y)) return;
    }
    // THE CLICK FACE ENDS WITH THE PHYSICAL HOLD, above every gate below: a
    // prompt opened by the press (or any other early return) must not strand a
    // lit interior on a button nobody is pressing any more. Nothing else about
    // the redesigned rows happens at a release — they have no release body.
    if (button == GuiMouseButton::Left) clear_redesign_button_press();
    if (app.prompt.active) return;
    // F2.1: a left release ending an editor-text drag finalizes the
    // selection (or collapses to a caret) before the modal swallow below.
    if (button == GuiMouseButton::Left && app.editor_text_drag.active) {
        const ActiveEditorText g = active_editor_text(app, audio);
        finalize_editor_text_drag();
        // Double-click seeding: a MOTIONLESS release (a pure click that left a
        // caret, no selection) seeds an editor-text candidate so a second click
        // within the window selects the clicked character class's run (word /
        // punctuation / whitespace). A drag that made a selection seeds nothing.
        if (g.valid && !text_editor::has_selection(*g.ed)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.load_editor)) return;
    if (text_editor::is_active(app.commit_title_editor)) return;
    // NON-LEFT RELEASES END HERE, and nothing is owed: every release body below
    // finishes something a LEFT press armed, and no other button arms anything.
    // The bare RIGHT press bound 2026-08-01 is a one-shot scrub act — it arms no
    // drag, seeds no double-click candidate and lights no button face — so its
    // release is a pure no-op by construction, not by an omission to fix later.
    if (button != GuiMouseButton::Left) return;

    // THE TRIM-BAR FRAMING DOUBLE-CLICK'S SEED, resolved for every left release
    // because only the release can tell a click from a drag. The press recorded
    // the band point (TrimBarPressSeed); this seeds the candidate when the
    // pointer never left the slack AND no trim drag went live AND the band's
    // region drag never crossed its threshold (the trim surface arc's third
    // term, 2026-08-11: the band's plain off-cap press arms the region former
    // now, and a moved region drag that wanders back inside the slack is a
    // DRAG, not a click) — the spellings
    // of "it stayed a click", equal by construction (kDoubleClickSlackPx ==
    // kDragMovedThresholdPx). A moved endcap or region drag therefore seeds
    // nothing
    // and, its own press having cleared any candidate at the top-of-frame, leaves
    // none behind. The record is consumed either way. (A WAVEFORM region drag
    // never recorded a seed, so the region term never fires there.)
    {
        const TrimBarPressSeed seed = app.trim_bar_press;
        app.trim_bar_press = TrimBarPressSeed{};
        if (seed.active && !app.trim_drag.active &&
            !(app.region_drag.active && app.region_drag.moved) &&
            std::abs(x - seed.press_x) <= kDoubleClickSlackPx &&
            std::abs(y - seed.press_y) <= kDoubleClickSlackPx) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::TrimBar,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
    }

    if (app.strip_drag.active) {
        // Terminating event: if the drag moved, run the final apply with
        // final=true and the one synchronous rebuild (resync + kick_waveform_sync,
        // inside apply_strip_drag_zoom's final path) so the rest state is exact. A
        // motionless press-release finalizes nothing — and commits nothing
        // either: the ctrl-waveform selection clear is RETIRED (architect
        // 2026-07-23), the press being purely the strip drag. A drag that MOVED
        // additionally clears any pending double-click candidate, so a drag can
        // never supply the second click of one.
        //
        // WHAT A MOTIONLESS RELEASE DOES OWE IS THE ANCHOR STEM'S ERASE, and
        // that is the ONE cost of the stem painting from the press (2026-08-05,
        // the one part of the rolled-back playhead arc that survives): full
        // waveform-area damage, the discrete shape. A MOVED drag gets it from
        // its final apply instead.
        const bool moved = app.strip_drag.moved;
        if (moved) {
            apply_strip_drag_at(x, y, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        }
        app.strip_drag = StripDragState{};
        if (!moved) viewport.invalidate_waveform_area();
        // Reappear the cursor at the anchor-stem column (y frozen at the press
        // row) — the restore x override the drag set each event — as the kind
        // this drag STAMPED at capture begin (Zoom), which is not necessarily the
        // kind that was showing when the press landed.
        end_strip_pointer_capture();
        return;
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan end: the pan applied incrementally during motion, so
        // there is nothing to finalize but the predictor. The continuous pan
        // deferred per-event resyncs, so re-anchor the predictor once here. The
        // pan captured the pointer at its arm, so end the capture (reappear the
        // cursor at the raw traveled virtual_pointer_x_, y frozen at the press
        // row — the pan sets no anchor-stem override); idempotent, so a degraded
        // compositor that never captured is unharmed.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        end_strip_pointer_capture();
        return;
    }
    // (No scrub branch: the scrub is one act at the PRESS — it arms nothing,
    // so its release is an ordinary fall-through.)
    if (app.region_drag.active) {
        // The region is extended live during the drag (see on_motion); a drag
        // that moved rests the region at its final extent. A MOTIONLESS
        // press-release (never crossed the threshold) needs no collapse here:
        // the press already cleared any resting highlight at mouse-down (see
        // arm_region_drag_at), so a plain click leaves the region cleared and
        // there is nothing to do at release but disarm. A jitter drag that
        // crossed the gate but rests a sub-threshold sliver dissolves like a
        // click (end_region_drag_min_size_check), and only a MOVED drag runs
        // that check — a motionless release left the region cleared at arm
        // (arm_region_drag_at, the one arm since 2026-08-05), so the check would
        // early-return anyway and the gate is what says so without asking.
        // Capture `moved` BEFORE the state reset (the reset zeroes it).
        const bool moved = app.region_drag.moved;
        app.region_drag = RegionDragState{};
        if (moved) end_region_drag_min_size_check(app, audio, viewport);
        return;
    }
    // (No tempo-drag arms here: the target-view tempo drag and its pending are
    // DELETED, architect 2026-07-29 — the tempo surface is the bare Up/Down cent
    // step alone. See marker_drag.h.)
    if (app.trim_drag.active) {
        commit_trim_drag();
        return;
    }
    if (app.pending_trim_drag.active) {
        // The pending trim drag never crossed the threshold: a motionless
        // endcap/bridge press, which is a CONSUMED NOTHING (architect
        // 2026-07-30). Its whole act was publishing the trim window as a region
        // highlight, and that coupling retired with the SPAN FORM; a deselect
        // and a playback stop left behind would be pure cost on a click that
        // commits nothing. Just disarm. (A crossed pending became app.trim_drag
        // and commits through the branch above, where the setter's deselect and
        // the trim-mutation stop live.)
        // ONE EXCEPTION SINCE THE TRIM SURFACE ARC (2026-08-11): the CTRL
        // in-span press DEFERRED its ctrl+CLICK bound-set to exactly this
        // moment — the clean motionless release IS the click (the flag's
        // contract at PendingTrimDrag::set_begin_bound_on_release; a lost
        // button, the force-end and — since the bridge drag moved to alt —
        // the threshold crossing all disarm without setting). It acts at the
        // PRESS column — a click sets where it was aimed, and the release sits
        // within the 8px slack by construction — and the setter carries its
        // own stop, refusals and deselect, so nothing is restated here. No
        // drag is armed after: the button is up.
        const bool set_begin = app.pending_trim_drag.set_begin_bound_on_release;
        const int  press_x   = app.pending_trim_drag.press_x;
        app.pending_trim_drag = PendingTrimDrag{};
        if (set_begin) set_trim_bound_at_click(/*is_begin=*/true, press_x);
        return;
    }
    if (app.pending_marker_drag.active) {
        // The pending marker drag never crossed the threshold: a pure flag click.
        // The press already single-selected its marker, landed the playhead,
        // collapsed any resting span AND seeded the Marker double-click candidate
        // (press-time seeding), so there is nothing to commit — just disarm. The
        // DEFERRED arm that used to complete a held-back click here died with the
        // group drag (architect 2026-07-29: horizontal movement is a focus act, so
        // a press on a selected member single-selects at press time like any other
        // — the doctrine is at the head of position_nudge.h).
        // (A crossed pending became app.drag — dropping the candidate at the
        // threshold crossing — and commits through the branch below.)
        app.pending_marker_drag = PendingMarkerDrag{};
        return;
    }
    if (!app.drag.active) return;
    // The marker reposition drag's own end.
    marker_drag.commit_drag();
}

// END every in-flight pointer gesture THROUGH ITS OWN RELEASE BODY. The one
// force-end route in the product: the Ctrl+Q hatch in the drag-modal gate
// (input_handler.cpp, where the no-cancel rule is stated in full) and main.cpp's
// resize / WM-close callbacks. It replaced cancel_active_drags — POINTER GESTURES
// HAVE NO CANCEL (architect 2026-07-29), so there is nothing to restore anywhere
// here: every live gesture COMMITS what stands, and undo is the mitigation.
// Homed beside on_button_release deliberately — these are the same bodies in the
// same order, and the region arm needs this TU's file-local min-size helper.
// The gestures are mutually exclusive in practice, so this reads as a chain of
// no-ops around the one that is live.
// NO CALLER OWES THE CURSOR ANYTHING, and the three that used to are the reason
// the model changed: every one of them does MORE after this returns — the two
// prompt routes raise the unsaved-work box (which the zone map answers with the
// Arrow) and the resize rebuilds the layout geometry the map measures against —
// so each carried a re-resolve at its own tail, ordered by hand against
// whatever else it did. The per-iteration owner (main.cpp's loop-settled hook)
// makes that ordering structural: a force-end reached from a dispatched key, a
// configure or a WM close all settle before their iteration's boundary, and a
// fourth caller inherits the same for free.
// A FORCE-END MID-CAPTURE ends the capture through the two navigation arms
// below, and the owner's next re-resolve then reads coordinates that are still
// the drag's VIRTUAL travel — which is why the platform drops a kind named while
// it has no real position (GuiPlatform::set_cursor_kind). What the release put
// back is the cue the GESTURE STAMPED at capture begin (Zoom or Pan), and it
// stands until the pointer moves; nothing here needs to test for it.
void GuiInputHandler::finalize_active_drags() {
    // Editor text-selection drag: FINALIZE (collapse a no-motion anchor to a
    // caret), the same act its release performs — selection-only, nothing to
    // revert. It seeds NO double-click candidate here: a force-end is not a click
    // (the clean release owns that seeding). The keyboard's own escape hatch for
    // this drag lives in on_key and is editor modality, not a gesture cancel.
    if (app.editor_text_drag.active) finalize_editor_text_drag();
    // The marker reposition drag commits its PROPOSED positions (the overlay
    // becomes the store) and pushes the one undo entry iff the drag netted a
    // change — the release path exactly, so Ctrl+Z reverts it.
    if (app.drag.active) marker_drag.commit_drag();
    // (The tempo drag's finalize arm went with the gesture, 2026-07-29 — see
    // marker_drag.h.)
    // The trim drag keeps its live bounds and runs the full commit tail (the
    // release column-snap, auto_clear_crossed_trim, the repaint/trigger, the
    // setter's deselect). TRIM IS HISTORY-LESS BY RULING, so these bounds are not
    // undoable — that is the standing trim gap (every trim gesture commits
    // outside undo), not something this force-end introduces.
    if (app.trim_drag.active) commit_trim_drag();
    if (app.region_drag.active) {
        // The region was extended live; the release rests it where it is, with the
        // sliver dissolve applying to a MOVED drag only (see the release arm).
        const bool moved = app.region_drag.moved;
        app.region_drag = RegionDragState{};
        if (moved) end_region_drag_min_size_check(app, audio, viewport);
    }
    if (app.strip_drag.active) {
        // Navigation gestures applied their motion continuously, so ending is just
        // ending: the one synchronous rebuild + predictor resync a moved drag's
        // release performs, then the capture release (idempotent). No end of this
        // gesture commits an ACT — it never had one — but each owes the ANCHOR
        // STEM'S ERASE, the stem having painted from the press since 2026-08-05,
        // so this arm, the clean release and the button-lost arm all spell it.
        if (app.strip_drag.moved) {
            if (playback.is_playing()) playback.resync_predictor();
            viewport.kick_waveform_sync();
        }
        app.strip_drag = StripDragState{};
        viewport.invalidate_waveform_area();
        end_strip_pointer_capture();
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan: incremental too, so re-anchor the predictor once and
        // end the capture, its release's whole body.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        end_strip_pointer_capture();
    }
    // THE PENDINGS DISARM, and that is not a cancel: a pending has committed
    // NOTHING of its own — the marker pending's click committed at the
    // PRESS, and the bound-set trim pending's bound was written at the press and
    // stands (the CTRL IN-SPAN arm's DEFERRED set fires only on a clean
    // motionless release, so disarming here rightly drops it — a force-end is
    // not a click; 2026-08-11). There is no release here (the button is still
    // held), so nothing is
    // owed; a pending otherwise resolves only by the threshold crossing or a real
    // release / button loss. (TWO pendings, not three, since the tempo drag's
    // deletion — 2026-07-29, see marker_drag.h.)
    // THE ASYMMETRY WITH THE DRAGS ABOVE — live gestures COMMIT here while pendings
    // merely DISARM — is ARCHITECT-ACCEPTED (2026-07-29: "not a real use case - do
    // whatever is easiest to code and has least loopholes"). It is also the form
    // with the fewest loopholes: a live gesture has produced state that must land
    // somewhere, and a pending has produced none, so each does the only thing it
    // can. Do not add a completion arm here to make the two look alike.
    app.pending_marker_drag = PendingMarkerDrag{};
    app.pending_trim_drag   = PendingTrimDrag{};
    // A force-end is not a clean click sequence, so no candidate may survive to
    // pair with a later click (the standing rule at every non-release gesture
    // end) — and neither may the trim bar's press record, which would otherwise
    // seed one at the next release.
    app.double_click   = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
}

// THE REDESIGNED BUTTONS' HOVER, in ONE transition writer over the whole roster
// (row 1's Quit / Navigation / Settings and the view bar's three, row 2's
// four, row 3's two tabs, row 4's seventeen and row 8's eight — 37, the enum's
// own count at kRedesignButtonCount — the stash is
// AppState::redesign_buttons).
// A face changes only when its boolean does, and a motion that changes ANY of
// them pays exactly ONE damage call PER STRIP TOUCHED — the strip idiom (no
// narrow rects; the playhead columns' carve-out stays the sole exception),
// forked per row-8's home since 2026-08-11: a transport face damages its own
// bottom-strip lane, every other face the top strip, and the common transition
// (leaving one button for its neighbour, two booleans flipping) still costs
// one damage when both live in one strip. The rects are the
// painter's stashes, so a hovered region is the painted button and nothing is
// measured here.
void GuiInputHandler::clear_redesign_button_hover() {
    // Row 8's pixels live in the bottom strip, so a cleared transport face
    // damages its own lane where every other face damages the top strip — the
    // row's standing damage fork, per changed face (each strip pays only when
    // one of its own faces moved).
    bool changed_top       = false;
    bool changed_transport = false;
    for (int i = 0; i < kRedesignButtonCount; ++i) {
        AppState::RedesignButtonFace& f = app.redesign_buttons[i];
        if (!f.hovered) continue;
        f.hovered = false;
        if (redesign_button_in_transport_row(static_cast<RedesignButton>(i)))
            changed_transport = true;
        else
            changed_top = true;
    }
    if (changed_top)       viewport.invalidate_top_strip();
    if (changed_transport)
        viewport.invalidate_rect(bottom_transport_row_area(app));
}

void GuiInputHandler::recompute_redesign_button_hover() {
    // IT REFUSES WHILE THE POINTER IS OUTSIDE THE WINDOW — the same first line
    // and the same reason as recompute_dropdown_hover's, the boundary's other
    // pointer-derived face: the remembered coordinates name a point INSIDE the
    // window even after the pointer has left, so this walk has no honest answer
    // to give while it is out. Its per-TICK caller (main.cpp) keeps running after
    // a leave, and the refusal is what makes that call inert in both directions —
    // it can neither re-light a face the pointer-leave hook dropped nor CLEAR the
    // one that hook deliberately KEEPS when an ORDINARY leave went out through
    // row 1 with the menu row's mode armed (the rule is at that hook, the band
    // predicate at point_in_menu_row_band). That the refusal would equally
    // prevent REPAIRING a kept face is why the hook keeps none on the hard
    // capability-loss edge, where no return motion exists to repair it. Nothing
    // else can move a face out there: the only writer of `hovered = true` is
    // this walk, and the only other writers of false are the leave hook itself
    // and the dropdown open edge, which no out-of-window event can reach.
    // THE GUARD LIVES HERE, NOT AT THE WIRING, because this function is the
    // faces' one derivation — on_motion, the other caller, writes
    // app.pointer_in_window true at its top and seeds the coordinates in the same
    // breath, so it is unaffected by construction, and a future third caller
    // inherits the rule instead of having to remember it.
    // ACCEPTED, and it is the leave hook's cost rather than this line's: a kept
    // face is FROZEN for as long as the pointer stays out, so a row-1 button that
    // changed its enabled or selected bit meanwhile keeps the hovered bit it left
    // with until the re-entry motion re-derives the whole roster. The paint reads
    // both bits, and row 1's faces are the ones this can reach at all.
    if (!app.pointer_in_window) return;
    const int mx = app.last_mouse_x;
    const int my = app.last_mouse_y;
    // NO DWELL RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT — read before the
    // walk because the walk below is what stamps it; the rule is stated at the
    // stamp itself.
    const bool modal_owns_the_keyboard =
        app.prompt.active || keyboard_modal_editor_active();
    bool changed_top       = false;
    bool changed_transport = false;
    int  hovered_tip = -1;
    for (int i = 0; i < kRedesignButtonCount; ++i) {
        AppState::RedesignButtonFace& f = app.redesign_buttons[i];
        const RedesignButton id = static_cast<RedesignButton>(i);
        // A zero-width stash (before that row's first paint) contains no point,
        // and the pre-motion (-1, -1) cursor is outside every rect, so both cold
        // states resolve to "not hovered" without a special case.
        //
        // THE ZONE IS THE SECOND TERM (redesign_button_hover_zone, app_state.h):
        // an open dropdown and the SELECTED tab answer nothing to the pointer at
        // all, and both refusals live in that one predicate rather than as
        // conditions here or in the painter. There is no in-window term: the
        // whole walk refused above.
        const bool under_pointer = rect_contains(f.rect, mx, my) &&
                                   redesign_button_hover_zone(app, id);
        // THE FACE ADDS THE ENABLED TERM AND THE HINT DOES NOT (architect
        // 2026-08-07): a disabled button keeps its dead face under the pointer
        // and still explains itself, kdenlive's own behaviour. This is the ONE
        // place the two consumers of a hover part company, which is why both are
        // resolved in this single walk.
        const bool inside =
            under_pointer &&
            redesign_button_enabled(app, audio.total_frames(), id);
        if (f.hovered != inside) {
            f.hovered = inside;
            if (redesign_button_in_transport_row(id))
                changed_transport = true;
            else
                changed_top = true;
        }
        if (hovered_tip < 0 && under_pointer && !modal_owns_the_keyboard &&
            redesign_button_tooltip(app, id).line1 != nullptr)
            hovered_tip = i;
    }
    // Row 8's damage fork: a transport face damages its own bottom-strip lane,
    // every other face the top strip — each strip pays only for its own.
    if (changed_top)       viewport.invalidate_top_strip();
    if (changed_transport)
        viewport.invalidate_rect(bottom_transport_row_area(app));

    // THE TOOLTIP'S DWELL STAMP, written here because this is the one place that
    // knows a hover STARTED. EVERY roster button but ROW 1'S carries a tooltip,
    // and redesign_button_tooltip's stateful overload owns both that membership
    // and the three things state moves in it (re-derived from that one function):
    // MEMBERSHIP moves exactly ONCE — row 3's WALK-SELECTOR tabs drop their hint
    // entirely while the `h` view stands, on the view bar's own reasoning — while
    // the TEXT follows state on two buttons, SAVE (a publishing checkpoint first,
    // then the history view's "Save and Commit") and RENDER (the iteration bit).
    // The walk above covers the whole roster either way: a newly hovered
    // one stamps the clock, and moving between two of them hides and re-stamps,
    // so a fresh dwell begins on each arrival. The run
    // loop's tick compares the stamp against kTooltipDelayMs and flips
    // `visible` — no timer is created and nothing here decides visibility.
    //
    // NO DWELL RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT, and this refusal
    // is what makes "a tooltip never floats over a modal" hold rather than merely
    // start out true. The HOVER PILL deliberately stays live under those surfaces
    // (the standing ruling: button hover is a pointer fact, and a lit pill
    // advertising a swallowed press is an accepted cost) — and both of the
    // branches that keep it live call this function, so without this line every
    // motion under a prompt or an editor would stamp a fresh dwell and the tick
    // would raise a FLOATING hint over the modal 700ms later. A hint is not a
    // face: it is a second surface, it hangs past the strip, and the chord it
    // names is exactly what the modal gate is swallowing. Forcing "no owner" here
    // rather than gating the tick keeps the stamp and the hide in one place — the
    // resolution below then also hides whatever was already up, so the modal's
    // OPEN edge needs nothing beyond its own hide (on_key's, for the case where
    // no motion and no tick follow).
    // THE DROPDOWN NEEDS NO TERM OF ITS OWN: redesign_button_hover_zone refuses
    // the whole roster while a popup is up, so the walk finds no owner by itself
    // — the two floating surfaces cannot coexist by construction.
    if (hovered_tip < 0) {
        hide_shift_tooltip();
    } else if (app.redesign_tooltip.owner != hovered_tip) {
        // A DIFFERENT tooltip button (or the first one) — hide whatever was up
        // and start this button's dwell from zero. Keying on the id is what
        // makes a direct Render->Paste motion wait the full delay again instead
        // of inheriting the dwell that was already running.
        hide_shift_tooltip();
        app.redesign_tooltip.owner    = hovered_tip;
        app.redesign_tooltip.hover_ms = monotonic_ms();
    }
}

// THE OPEN DROPDOWN'S OWN HOVER AND ITS ARMED ITEM, the pointer's only hover
// while it is up. One transition writer like the roster's, damaging the strip
// and the popup box on a change; the rects are the painter's published item
// boxes, so a highlighted item is exactly the box that lights and exactly the
// box a click hits. The closed menu's rects are zero and contain no point, so
// the walk needs no membership test beyond the open check.
//
// A DISABLED ITEM RESOLVES TO NO ITEM, which is what keeps the two faces honest
// with one line rather than a term in the painter: neither face can name a row
// the press and the release refuse. The predicate is the same one they and the
// painter read (dropdown_item_enabled, app_state.h).
//
// TWO CALLERS, AND EACH ANSWERS A QUESTION THE OTHER CANNOT (2026-08-03).
// PER DELIVERED MOTION, from on_motion's open-dropdown branch: the settled hook
// does NOT run between the events one wl_display_dispatch_pending delivers, so a
// batch carrying a motion onto item 2 and then a PAINT would light item 1 on the
// frame that batch produces if this call moved to the loop tail. THIS WALK SERVES
// THE FACES, and the faces are consumed by the painter, which can run inside the
// batch. The RELEASE no longer depends on it being last: finish_dropdown_release
// re-reads the arm's own definition at the release's coordinates, because the
// walk's INPUTS can move at that same in-batch paint (the whole argument, and the
// equivalence that makes it safe, are at that function).
// PER RUN-LOOP ITERATION, from main.cpp's settled hook: the INPUTS of this walk
// move with no pointer event under them. The item rects are PAINTER-PUBLISHED
// and are zero from the open until paint_dropdown publishes them, so a motion
// delivered before that paint resolves nothing — and a pointer that then RESTS
// has no next motion to be self-corrected by, which left the anchor-press
// gesture (press Settings, slide onto an item within the opening frame, stop,
// release) permanently missing its item. The iteration that paints the box ends
// by resolving against it, so the face lands on the next painted frame with no
// pointer event of any kind required. Same cure as the pointer cursor's, same
// boundary, one class: a pointer-derived face whose inputs can settle on their
// own.
//
// THE ARM FOLLOWS THE POINTER while a press is live inside the popup (architect
// 2026-08-03): slide from one item onto the next and the pressed face travels
// with the pointer, slide onto the separator, the chrome or off the box and
// NOTHING is lit though the press is still live, slide back on and it re-arms.
// That is why the two answers are resolved in ONE walk from ONE hit: a menu
// shows exactly one item in a distinguished state, and which FACE that item
// wears is only the question of whether the button is down. An arm that stayed
// where it went down lit the accent fill there while this hover lit the tint
// under the pointer — the two lit items this rule exists to prevent.
//
// WHY THE ARM CANNOT ANSWER "IS A PRESS LIVE" ANY MORE: it is -1 both before
// any press and while a live press stands over a separator, so `pressed_item >=
// 0` as the liveness test would strand the drag at the first separator
// crossing. The two facts are read from their own owners instead — WHETHER the
// button is still down is the PLATFORM's tracking, threaded in with the motion
// (GuiInputState::primary_button_held, the same field every button-lost arm in
// on_motion reads, so there is no second copy to desync), and WHERE it went
// down is the popup's own bit.
//
// THAT SECOND TERM WAS THE SCOPE LINE AND THE SCOPE WIDENED (architect
// 2026-08-03): a press on the ANCHOR button (Settings or Navigation) followed by
// a drag into the popup is the other half of the standard menu gesture, and it
// now works — the anchor press that OPENS a menu records the same claim an item
// press does, so this walk serves both halves with nothing added here. The bit
// was scoped for exactly that, and the widening cost the one line at the press
// site it was designed to cost. What the term still buys is the two routes that
// must NOT arm: an anchor press that CLOSED a menu (there is no popup to belong
// to), and any held button whose press this popup never saw at all.
void GuiInputHandler::recompute_dropdown_hover(GuiInputState mods) {
    if (!app.dropdown.open()) return;
    // THE REMEMBERED COORDINATES NAME A POINT INSIDE THE WINDOW even after the
    // pointer has left, and a LEAVE IS NOT A DISMISSAL — the menu stays up — so
    // without this the per-iteration caller would find the last on-surface
    // position still inside an item rect and RE-LIGHT, on every wakeup, exactly
    // the two faces the pointer-leave / capability-loss hook just dropped
    // (clear_dropdown_pointer_state, which is the only thing that can drop them
    // while the pointer is outside).
    // THE GUARD LIVES HERE, NOT AT THE HOOK WIRING, because this function is the
    // two faces' one derivation: on_motion writes app.pointer_in_window true at
    // its top and seeds the coordinates in the same breath, so the motion caller
    // is unaffected by construction, and a future third caller inherits the rule
    // instead of having to remember it. Same first line and same reason as
    // refresh_pointer_cursor's, the boundary's other consumer.
    if (!app.pointer_in_window) return;
    // THE GEOMETRY IS dropdown_item_at'S, asked at the remembered coordinates —
    // one walk over the published rects for this face derivation, for the press
    // claim and for the release's derive, so "which item is here" cannot mean
    // three slightly different things.
    const int raw = dropdown_item_at(app.last_mouse_x, app.last_mouse_y);
    // A DISABLED ITEM IS NEITHER HOVERED NOR ARMED (2026-08-08): resolving it to
    // "no item" is the whole rule, and it covers both faces at once — nothing
    // lights under the pointer, and an arm sliding across a greyed row drops
    // exactly as it does over the separator or the chrome, then re-arms on the
    // next live row. So the painter never has to ask whether a lit item is also
    // a dead one.
    const int hit =
        (raw >= 0 && !dropdown_item_enabled(app, app.dropdown.menu, raw))
            ? -1 : raw;
    const bool press_live =
        mods.primary_button_held && app.dropdown.press_began_on_item;
    const int armed = press_live ? hit : app.dropdown.pressed_item;
    if (app.dropdown.hovered_item == hit &&
        app.dropdown.pressed_item == armed) return;
    app.dropdown.hovered_item = hit;
    app.dropdown.pressed_item = armed;
    // ONE DAMAGE PAIR FOR BOTH ITEMS. The popup's WHOLE published box is
    // invalidated, so a frame in which the arm leaves one item and lands on
    // another erases and repaints both with no per-item rect arithmetic — the
    // same path the hover move already took, and the reason the following arm
    // needed no second invalidation of its own.
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(app.dropdown.rect);
}

// THE ONE CHORD-DISPATCH BODY for every redesigned button whose action IS a
// chord — rows 1 through 4 and row 8, driven entirely by kToolbarChords'
// per-button flags
// so no row carries a special case of its own. Returns true when a button's rect
// claimed the press, whether or not anything was dispatched (a refusal is still
// a consumed nothing, which is what the band claims want).
//
// THE BUTTON IS ITS CHORD, dispatched through on_key: the action is not merely
// the same FUNCTION the key calls, it is the same ROUTE — every gate the chord
// passes on the keyboard (the loading/blank return, the keyboard-modal editor
// gate, the read-only allowlist, the arm's own refusals) applies here, in the
// same order, with nothing restated and nothing that can drift. It is the exact
// inverse of the platform's bare-`e`-as-left-button translation: one vocabulary
// expressed on the other's surface, at a boundary.
//
// The caller has already refused ctrl and alt at the band; shift arrives live
// and is decided per button here.
bool GuiInputHandler::dispatch_redesign_chord(int x, int y, GuiInputState mods) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (!redesign_button_hit(app, tc.id, x, y)) continue;
        // A SHIFT PRESS ON A BUTTON WITH NO SHIFTED CHORD is a consumed nothing
        // — never the unshifted action, which would be a silent lie about what
        // the modifier did (the flag's rationale is at its declaration).
        if (mods.shift && !redesign_button_shift_admits(tc.id)) return true;
        // A DISABLED BUTTON'S PRESS IS A CONSUMED NOTHING: the chord is not
        // dispatched at all, and a SHIFT press is swallowed exactly like the
        // plain one (one predicate, both routes — a greyed Render is greyed for
        // both of its chords). The predicate is the painter's
        // (redesign_button_enabled, app_state.h), so the greyed face and the
        // inert press are the same fact read twice and the press cannot slip
        // through on a frame the paint disagreed with. Dispatching anyway would
        // be harmless — every one of these chords refuses on its own — but it
        // would leave the disabled face lying about what a click does. Rows 1, 3
        // and 4 have no disabled face of their own, so the predicate is simply
        // true there — EXCEPT while the `h` history view stands, which greys
        // every button whose act it consumes across all the rows and is
        // therefore the one state in which this line consumes a row-1, row-3 or
        // row-4 press (history_mode_disables_button, above). ROW 8 adds the one
        // RESTING consumer outside that mode: the play/stop pair's state
        // mirror, whose dead half this line consumes so the pair's faces and
        // presses stay one fact (redesign_button_enabled, app_state.h).
        if (!redesign_button_enabled(app, audio.total_frames(), tc.id))
            return true;
        // A RADIO ALREADY SELECTED HAS NOTHING TO SWITCH TO, and its chord is a
        // TOGGLE — dispatching would switch AWAY from what the user just
        // clicked. So the press is a consumed nothing, which also makes "no
        // button was hit" and "the selected half was hit" the same silent
        // outcome. Toggles (follow, iteration) are NOT radios and press through
        // in both directions.
        if (tc.radio && redesign_button_selected(app, tc.id)) return true;
        // THE CLICK FACE ARMS BEFORE THE ACTION RUNS, so a chord whose route
        // repaints the strip (undo, save, a view switch) paints the pressed
        // interior on the very frame it produces. It is cleared by the release /
        // the pointer-leave hook, never by the action. A SHIFT press takes it
        // too — it is the same physical hold, and the face tracks the hold.
        if (tc.click_face &&
            app.redesign_pressed != redesign_button_index(tc.id)) {
            app.redesign_pressed = redesign_button_index(tc.id);
            // DAMAGE FOLLOWS THE ROW'S HOME STRIP (row 8, 2026-08-11): the
            // transport row's pixels live in the BOTTOM strip, so its click
            // face damages its own lane where every other row damages the top
            // strip — the same fork every face writer takes.
            if (redesign_button_in_transport_row(tc.id))
                viewport.invalidate_rect(bottom_transport_row_area(app));
            else
                viewport.invalidate_top_strip();
        }
        // THE RENDER BUTTON IS CANCEL WHILE A RENDER IS LIVE (architect
        // 2026-08-11) — THE ROSTER'S ONE RULED EXCEPTION TO "THE BUTTON IS ITS
        // CHORD": while app.render_cancel_face stands (the painted face's own
        // bit — the mirror of cancel_archival_session's predicate, contract at
        // its declaration) a press here runs THE CANCEL ACT ITSELF, the Esc
        // arm's own body, and dispatches no chord at all. The divergence is
        // his ruling, both halves: the KEYBOARD keeps Ctrl+Alt+R's own
        // semantics unchanged (a dispatch kills the running render and starts
        // a new one — the render-dispatch rule), while the BUTTON "doesn't
        // need to exist while nothing's rendering" and so becomes the cancel;
        // and the dispatched chord could not be bare Esc either, because Esc
        // ranks the region clear ABOVE the render cancel — a Cancel button
        // that cleared a resting region instead of cancelling would be a lie.
        // A SHIFT press cancels too: one face, one act — while the button IS
        // Cancel, letting shift slip through to the miscellaneous render would
        // start a render from a button that says Cancel. The click face above
        // is already armed, correctly: this is a press acting.
        if (tc.id == RedesignButton::Render && app.render_cancel_face) {
            // BOTH HALVES OF THE FACE-MIRRORS-THE-ACT HONESTY (the contract is
            // at the bit's declaration): the CLAIM reads the painted bit, so a
            // press on a painted Cancel never dispatches a render; the ACT is
            // gated on the LIVE explicit-act bit, so on the stale edge it is a
            // consumed no-op and can never reach a PREVIEW session through
            // cancel_archival_session's wider is_busy branch — the face never
            // advertised one.
            if (app.queue_running) cancel_archival_session();
            return true;
        }
        // The shift term ORs the table's own (Redo's Ctrl+Shift+Z) with the
        // pointer's — well-defined because no row sets both (see shift_admits),
        // so this one expression spells both members of each shifted pair.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift || mods.shift;
        chord.alt   = tc.alt;
        // THE ARROWS' HOLD-REPEAT ARM, in three ordered pieces (codex round 3,
        // 2026-08-11). ELIGIBILITY IS JUDGED UNDER THE PRESS-TIME CONTEXT, and
        // it is the KEYBOARD'S OWN PREDICATE SHARED, not mirrored:
        // repeat_eligible is exactly what the platform's arming probe asks for
        // the physical key, so the button's hold arms in precisely the
        // contexts a held key would (and refuses in the ones it refuses — a
        // press the flag editor's gate consumes must not arm a burst that
        // starts firing when the editor closes under a held button). Asked
        // BEFORE on_key, which is what "press-time" means — the dispatch below
        // can change the context (open an editor, close one), and the
        // platform's probe is likewise evaluated pre-dispatch. The ARM itself
        // then lands AFTER on_key, because on_key's top disarms the transport
        // hold on every non-synthesized key arrival (the M2 mirror, below) and
        // must not eat the arm it is part of establishing.
        const bool arm_repeat = tc.repeats && repeat_eligible(tc.key, chord);
        on_key(tc.key, chord);
        // The schedule is the keyboard's own: one dispatch above, the first
        // synthesized repeat kArrowRepeatDelayMs later, then the interval
        // (tick_transport_arrow_repeat, which owns every firing condition).
        if (arm_repeat) {
            app.transport_repeat.owner = redesign_button_index(tc.id);
            app.transport_repeat.next_due_ms =
                monotonic_ms() + kArrowRepeatDelayMs;
        }
        return true;
    }
    return false;
}

// WHICH ITEM IS AT (x, y), or -1. The rects are the painter's published item
// boxes, so a hit is exactly the box that lights; a closed popup has zero rects
// and therefore contains no point, which is the correct cold answer.
//
// PURE GEOMETRY, DELIBERATELY: a DISABLED item keeps its rect and answers here
// like any other, because it keeps its row on screen too. Whether the row may be
// hovered, armed or activated is dropdown_item_enabled's question, asked by each
// of this function's three callers on its own side — one geometric answer, one
// enablement answer, neither hiding inside the other.
int GuiInputHandler::dropdown_item_at(int x, int y) const {
    for (int i = 0; i < dropdown_item_count(app.dropdown.menu); ++i) {
        const GuiRect& r = app.dropdown.item_rects[static_cast<size_t>(i)];
        if (rect_contains(r, x, y)) return i;
    }
    return -1;
}

// THE DROPDOWN'S RELEASE — the one place in the redesign where an action fires
// on the button coming UP. Returns true when the release belonged to the popup
// and the caller must stop.
//
// IT ACTS ON THE ITEM UNDER THE POINTER, which is the arm's own DEFINITION
// (recompute_dropdown_hover): the armed item IS the item under the pointer and
// is the ONE item lit, so "the release runs what is lit" and "the release runs
// the item under the pointer" are one sentence — and where the two clauses can
// part company, this body reads the second (the derive, below). NOTHING UNDER
// THE POINTER — a claimed press (on an item, or on the ANCHOR that opened the
// menu) standing over the separator, the chrome, the anchor button itself or off
// the box — runs nothing, is consumed, and LEAVES THE POPUP OPEN: that is the
// escape hatch for a press that landed on the wrong row, and it is the same act
// the user sees, since nothing was lit to release onto.
//
// THE TWO GESTURES MEET HERE, which is why the dismissal rule is stated at this
// one site: DISMISSAL IS A PRESS ACT IN THIS PRODUCT and a release never
// dismisses. A press outside the popup closes and consumes; a release that
// armed nothing leaves the menu standing, whichever gesture it belonged to. So
// the classic CLICK on the anchor — press and release without moving — opens
// the menu and leaves it up by construction rather than by exception: the
// anchor is not an item, so the release finds nothing armed and takes the
// return below. The two rules cannot overlap either — a press outside never
// arms anything, so no release can be owed there.
//
// THE POSITION COMPARE THIS BODY ONCE CARRIED (the item under the release point
// against the armed one, REFUSING when they differed) IS GONE AND STAYS GONE: it
// was deleted as structurally dead under the premise "the arm is recomputed at
// EVERY delivered motion", and with an item armed it could not be true. What
// stands here now is the opposite act on the same coordinates — a DERIVE, which
// ACTS on the freshest truth where the compare refused on disagreement.
//
// WHY THE DERIVE (2026-08-03, from codex): that premise has exactly ONE
// exception, and it is not about motion at all. The item rects are
// PAINTER-PUBLISHED and are zero from the open until paint_dropdown publishes
// them, so they can move — from nothing to real — at a PAINT with no pointer
// event between the publish and the release. paint_one_frame runs from the frame
// callback, which wl_display_dispatch_pending delivers out of the same batch as
// the pointer events, and the loop's settled tail runs only after the WHOLE
// batch: one batch can therefore carry [frame done → rects published] and then
// [the release], with nothing resolving the arm in between. Press the anchor,
// flick onto an item and release inside the frame the menu came up in, and the
// arm the release read was still the pre-paint -1 — the activation LOST, the
// release consumed, the menu standing. Reading the definition at delivery time is
// the premise's honest completion.
//
// IT CAN NEVER DISAGREE WITH A CORRECT ARM, which is what makes it a completion
// rather than the compare's return: whenever the arm IS current, the recompute
// set it from dropdown_item_at at app.last_mouse_x/y against the same published
// rects, and the platform delivers a release AT THOSE COORDINATES — absolute
// motion is delivered synchronously (on_pointer_motion) and the release carries
// the platform's own pointer_x_/pointer_y_, while the one deferred kind, a
// captured drag's coalesced relative motion, is flushed immediately BEFORE any
// button event with the held bit crossing on the pre-release side so that flushed
// motion still reads the button as down (flush_deferred_motion,
// platform_wayland.cpp). Same expression, same coordinates, same rects: the two
// answers are equal by construction wherever the arm is resolvable at all, and
// where they differ the ARM is the stale one. So nothing that fires today stops
// firing, and the stale-rect window stops swallowing an activation.
//
// THE ACCEPTED COSMETIC RESIDUE, stated rather than papered over: in exactly that
// window the publish and the release are sub-frame apart, so the item ACTS
// without its pressed face ever having been painted. A face nobody had time to
// see is strictly better than an activation nobody gets.
//
// The release's own consumed no-op case is still the armed-nothing return below,
// which the compare never owned either: it predates all of this and answers a
// press that armed nothing at all.
bool GuiInputHandler::finish_dropdown_release(int x, int y) {
    if (!app.dropdown.open()) return false;
    // THE CLAIM IS THE GATE, NOT THE ARM. Only a press this popup owns can
    // activate anything, so with no claim nothing is derived and the recorded arm
    // answers — which is -1 in every reachable unclaimed state. Not because the
    // item press is the arm's only raising writer: recompute_dropdown_hover can
    // also raise it, from -1, for either press origin (the item press or the
    // anchor press whose drag lands on an item). The true producer invariant is
    // this: the item press raises the arm while SETTING the claim; the recompute
    // may raise the arm only while the claim AND the platform's held bit are both
    // live; and every clearer of the claim (the pointer-leave / capability-loss
    // edge, every close's struct reset, and THIS RELEASE ITSELF, a few lines
    // below) drops the arm with it. The release is the third clearer, and it
    // takes both its outcomes: an activating release clears the arm explicitly
    // (`pressed_item = -1`) before the close, and a consumed no-item release can
    // return above that assignment only because the derive equivalence above
    // already makes the recorded arm `-1` there too — `dropdown_item_at(x, y)`
    // answering `-1` when `press_began_on_item` is true, or `pressed_item`
    // already reading `-1` when it is not. So an unclaimed press can never
    // observe a raised arm — the recompute that could have raised one needs the
    // claim to run at all. Deriving here without the claim would be the one
    // behavior change this round refuses: a held button this popup never saw
    // must not fire an item it happens to come up over.
    int armed = app.dropdown.press_began_on_item
                    ? dropdown_item_at(x, y)
                    : app.dropdown.pressed_item;
    // A DISABLED ITEM ACTIVATES NOTHING AND CLOSES NOTHING (2026-08-08). The
    // recorded arm can never name one — the hover walk resolves a greyed row to
    // "no item" — but the DERIVE reads raw geometry, so the gate belongs on this
    // side of it too, and answering -1 is what routes the release into the
    // armed-nothing return below: consumed, menu still up, which is the same
    // answer the press over that row already gave.
    if (armed >= 0 &&
        !dropdown_item_enabled(app, app.dropdown.menu, armed)) armed = -1;
    // The press claim ends here whatever it armed, so nothing the pointer does
    // afterwards can move an arm this button-down no longer owns.
    app.dropdown.press_began_on_item = false;
    if (armed < 0) return true;   // nothing was lit; consumed, menu stays up
    const DropdownMenu menu = app.dropdown.menu;
    app.dropdown.pressed_item = -1;
    // CLOSE FIRST, THEN ACT — the popup is gone before anything the item does
    // runs, so a modal it opens never overlaps the menu even for a frame, and a
    // COMMAND it dispatches is not swallowed by the popup's own keyboard gate.
    // The two menus' actions differ in kind and each stays with its own table.
    if (menu == DropdownMenu::Navigation) {
        // THE ITEM IS ITS KEY, dispatched through on_key exactly as a redesigned
        // button dispatches its chord: every gate the keyboard route passes
        // (loading/blank, the modal gates, the read-only allowlist, the arm's own
        // refusals) applies identically, so an item whose command cannot act
        // right now simply does nothing — the buttons-never-grey rule, one
        // surface further out. No stop, no modal, nothing restated here.
        // THAT IS ALSO HOW THE `h` HISTORY VIEW ANSWERS THIS MENU (2026-08-08):
        // per item, at the mode's own two gates — its allowlist for the zoom and
        // framing rows, history_mode_owns_key for the rest — with the one row
        // that would mean something else in there greyed above and never reaching
        // this dispatch at all.
        const NavigationPopupItem& it =
            kNavigationPopupItems[static_cast<size_t>(armed)];
        close_dropdown();
        GuiInputState chord{};
        chord.ctrl  = it.ctrl;
        chord.shift = it.shift;
        chord.alt   = it.alt;
        on_key(it.key, chord);
        return true;
    }
    // SETTINGS: the editor's open is its own ordinary route, prefilled through
    // the one recall serializer.
    // IT CAN REFUSE SINCE 2026-08-07, and this site says nothing about it: the
    // editor is disabled on a read-only ACTIVE tab (it authors the engine
    // settings), the refusal living at GuiSettingsEditor::open — the one
    // chokepoint — together with the modal playback stop, which moved off this
    // line to sit past that gate. So a locked tab's item click closes the menu
    // and does nothing else: no editor, no stopped audition. THE ITEM IS NOT
    // GREYED, deliberately — the never-grey rule for these items is the
    // standing ruling, and their commands' own refusals answer, exactly as the
    // Navigation menu's do.
    const char* key = kSettingsPopupItems[static_cast<size_t>(armed)].key;
    close_dropdown();
    settings_editor.open_prefilled(key);
    return true;
}

// THE DROPDOWN'S TWO WRITERS. Both damage the same pair of rects —
// the top strip AND the popup's own published box — because the popup hangs
// BELOW the strip and overlaps the rows and the waveform under it, so strip
// damage alone would leave its overhang stale on the close. On the OPEN the box
// has not been painted yet (its rect is still zero), so the strip damage is what
// schedules the frame that paints it and publishes the rect; on the CLOSE the
// rect from the last paint is exactly the region to erase, which is why the
// close reads it BEFORE zeroing the state.
//
// EVERY CLOSE THROUGH HERE ALSO DISARMS THE MENU ROW
// (AppState::Dropdown::menu_row_armed): bare Esc, an item activating, the anchor
// click that closes its own menu, a press anywhere else, the wheel, Ctrl+Q, the
// WM close and a resize all end the mode through this one owner, because a
// dismissal the user MEANT must end the mode — or Esc would put away a menu that
// the next pointer twitch reopens.
// THE CLEAR SITS ABOVE THE "NOTHING IS OPEN" RETURN, and that placement is the
// whole point rather than a detail: the mode's defining state is menu CLOSED and
// row ARMED (what a row-1 hover close leaves behind), so a dismissal arriving in
// it finds nothing to close and must still go cold. Riding the whole-struct
// reset alone would have made every one of those routes a no-op in exactly the
// state the mode exists for. Below the return the reset re-clears the bit, which
// costs nothing and keeps the struct one initializer.
// THE ONE CLOSE THAT KEEPS THE MODE is the row-1 hover close in on_motion —
// sliding onto Quit or the view bar is a step ACROSS the bar, not a dismissal —
// and it re-arms on the line after its call to this. It is the only site in the
// tree that writes that bit true outside toggle_dropdown's open.
// THE `h` HISTORY MODE's POINTER ALLOWLIST and its acts. True = the press is
// CONSUMED here (refused outright, or handled as one of the mode's own acts);
// false = the press is one of the navigation gestures the mode leaves alone, and
// on_button_press proceeds with it untouched.
//
// THE VIEW IS SILENT (architect 2026-08-05): NO PRESS IN HERE STARTS AUDIO. The
// two scrub entries the mode used to pass through — the bare RIGHT press over
// the whole waveform and the PLAIN press in the LOWER HALF — are consumed with
// everything else, because the mode's full-song trim forces a full target
// preview render for an audition the view never needed. Playback removal is
// whole: Space left the keyboard allowlist in the same ruling, and the entry
// owner stops a session that was already running.
//
// WHAT PASSES THROUGH, the whole list — the mode's navigation vocabulary, the
// pointer half of what history_mode_key_blocked admits on the keyboard:
//   * ALT-exact over the waveform — the captured grab-pan.
//   * CTRL-exact over the waveform — the dual-axis strip drag, its one entry
//     since the ruler entry died with the trim surface merge (2026-08-11; the
//     ruler rows are the consumed merged band's now). Ctrl+Shift is NOT
//     admitted anywhere: over the waveform it is already a no-op, and over the
//     merged band it sets the end bound, which is a write.
//
// THE ACTS, all pure navigation (2026-08-05, when the waveform's upper half
// joined the marker lane and the trim bar gained its framing gesture):
//   * a DOUBLE-CLICK anywhere on the MERGED TRIM SURFACE (trim bar + ruler +
//     their gap since 2026-08-11) ZOOMS TO THE VIEWED
//     CHECKPOINT'S DIFF SPAN — the span that band is already displaying —
//     through the framing act the mode's edges stopped running in 2026-08-05
//     (frame_viewed_commit_diff_span, input_key_dispatch.cpp). Since 2026-08-08
//     it is the ONLY framing gesture a standing view has, the internal edges
//     writing no viewport at all and the window being the user's for the whole
//     visit. It moves the viewport and nothing else, and a SINGLE click on that
//     band stays the consumed nothing a motionless trim-bar click is everywhere.
//   * a press on a DIFF FLAG in the MARKER LANE takes the mode's focus (at most
//     one, painted in its class's selected pair) and LANDS THE PLAYHEAD on that
//     flag's authored frame, through the same owner every marker land uses. It
//     touches NOTHING else: no store selection, no live focus, no auto-select,
//     no playback stop. It DOES take a resting region with it since 2026-08-06
//     (the no-region clause is dead — it predated the view's ability to form a
//     region at all): a click that lands the playhead is a POINT command, the
//     live clear-site rule's own shape. On empty lane it clears the focus, lands
//     nothing, and still clears the region — the live marker click's
//     unconditional clear.
//   * a PLAIN press within the stem grab tolerance of a PAINTED DIFF FLAG'S
//     STEM, in the waveform's upper half, IS THAT FLAG'S CLICK — the same body
//     at a different pixel, so it inherits that minimalism whole. It is asked
//     BEFORE the placement press below, the live press's own order between the
//     two surfaces. PLAIN ONLY since the symmetry ruling below.
//   * the MARKER LANE's two MODIFIED clicks, and that lane alone: SHIFT takes
//     the contiguous range from the focus, CTRL toggles one flag's membership,
//     and both then focus the clicked flag and land the playhead on it (the live
//     selection model over the mode's own list; the store selection stays as
//     untouched as the plain click leaves it).
//   * every other press on the WAVEFORM — plain at EITHER HEIGHT, or SHIFT-exact
//     at either height — LANDS THE PLAYHEAD at its column AND ARMS THE REGION
//     DRAG: the regular views' placement press as the mode's analog (architect
//     2026-08-05, when playback left the view and the lower half stopped being
//     the scrub). It is the live recipe and the live playback regime through the
//     two shared bodies (place_playhead_at_click_column then arm_region_drag_at),
//     so a motionless release is just the placement (the arm dissolved the old
//     span at mouse-down and nothing rebuilt one), and motion past the shared
//     gate grows a region with the playhead riding the moving endpoint. The
//     region is LISTENING SCRATCH no more — `x` is consumed in here, so a span
//     drawn in the view is a reading mark, and it is VIEW-LOCAL: the exit, every
//     `,` / `.` step and every compare switch clear it (the mode edges'
//     own rule, close_history_mode and set_history_reading). SHIFT ADDS NOTHING
//     but reaching the same press, exactly as it does outside; it is kept
//     because the muscle memory is the live former's.
//     It clears the mode focus and its selection, the analog of the live body's
//     deselect (the playhead is leaving the flag), which is why the SHIFT arm is
//     not the "deliberately independent" former it was for one day — a press
//     that lands the playhead cannot leave a flag claiming to be it. THAT IS THE
//     ARCHITECT'S RULING NOW, ratified 2026-08-05 rather than left as the
//     coder's reading: EVERY REGION FORMER DROPS THE SELECTION ITS SURFACE OWNS,
//     the live entries the store selection and this one the mode's pair — the
//     family rule, stated in full at RegionState (app_state.h). There is
//     no waveform double-click surface to inherit (DoubleClickSurface has none),
//     so a second click is simply another placement press.
//
// THE SYMMETRY RULING (architect 2026-08-06) is why the acts read as they do:
// THE HISTORY VIEW AND THE REGULAR VIEWS ANSWER A WAVEFORM CLICK IDENTICALLY,
// and the regular views' standing model is the model. MODIFIED CLICKS ON THE
// WAVEFORM IGNORE STEMS everywhere — the 2026-08-01 live-view precedent, now
// universal — so a waveform modifier is GESTURE vocabulary (ctrl the strip drag,
// shift the placement press and its former) while SELECTION is LANE vocabulary
// (the flag boxes' plain, shift and ctrl clicks). PLAIN clicks keep the stem hit
// in both views, that being the one surface where a flag and the waveform
// overlap without a modifier to spend. What this DELETED is the mode's
// stem-based multi-select — the shift range and the ctrl toggle over stems in
// the waveform's upper half, which stood for one day — and nothing else: the
// LANE's shift and ctrl clicks are untouched.
//
// EVERYTHING ELSE IS A CONSUMED NO-OP — the RIGHT button whole, the marker
// clicks and their drag arm, the empty-lane marker drop, the trim bar's three
// WRITING routes (the endcap and bridge drags and the two ctrl bound-set
// clicks, none of which the framing double-click above touches — it is the
// band's SECOND click, exactly as outside), and every unbound modifier
// combination that is not one of the LANE's two modified flag claims above (a
// modified press over the lane that hits no flag included: only the bare and
// shift-exact arms reach the waveform's placement press, and ctrl over the
// waveform leaves for the strip drag rather than being consumed at all).
bool GuiInputHandler::handle_history_mode_press(
        GuiMouseButton button, int x, int y, GuiInputState mods,
        const DoubleClickCandidate& dc_at_press) {
    // THE RIGHT BUTTON IS CONSUMED WHOLE (architect 2026-08-05): its one binding
    // in the product is the full-height waveform scrub, and no press in this
    // view starts audio.
    if (button != GuiMouseButton::Left) return true;

    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // The waveform BAND, spelled as on_button_press spells it (the inert right
    // gutter counts as waveform by the user's lights).
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < top.x + top.w &&
        y >= area.y && y < area.y + area.h;

    // THE MULTI-SELECTION'S TWO MODIFIED CLICKS (architect 2026-08-05), and they
    // are asked FIRST because they are the only modified presses in this mode
    // that hit an ITEM: shift takes the contiguous range from the focus, ctrl
    // toggles one flag's membership, and both then focus the clicked flag and
    // land the playhead on it — the live selection model re-expressed over the
    // mode's own list, with the store selection as untouched as the plain
    // click leaves it.
    //
    // MULTI-SELECT IS FLAG-LANE-ONLY (architect 2026-08-06, THE SYMMETRY RULING
    // — see the header above): the MARKER LANE is the whole surface these two
    // clicks have, and the claim there is unconditional, a modified lane press
    // that hits no flag being the consumed nothing it was before the arm
    // existed. THE WAVEFORM IS NOT A SELECTION SURFACE in either view: a
    // modified press there is the GESTURE the modifier names — ctrl the
    // dual-axis strip drag, shift the placement press and its region former —
    // so the STEM-BASED range select and toggle this arm carried for one day
    // are DELETED, and the mode now matches the regular views' 2026-08-01
    // "modified clicks ignore stems" rule exactly. Both fall through from here.
    if ((shift != ctrl) && !alt) {
        const GuiRect lane = top_marker_row_area(app);
        if (y >= lane.y && y < lane.y + lane.h) {
            const int hit = hit_test_flag(app, audio, x, y);
            if (hit >= 0) select_history_diff_flags_modified(hit, shift);
            return true;
        }
    }

    // A SHIFT-EXACT PRESS ON THE WAVEFORM IS THE PLACEMENT PRESS (architect
    // 2026-08-05, the former's reshape read into the view): outside, shift now
    // anchors the region AT THE CLICKED COLUMN, which makes it the placement
    // press with a modifier on it — so in here it carries no body of its own and
    // simply falls through to the waveform arm below, the mode's own press being
    // that same shape already.
    //
    // THE WHOLE WAVEFORM, BOTH HALVES, STEMS IGNORED (architect 2026-08-06, the
    // symmetry ruling): shift over the waveform resolves no diff flag anywhere,
    // so there is no half to except and no stem to test — a shift click lands
    // the playhead at its column and collapses the mode's focus + selection, and
    // a shift drag maps out the region from that column with the playhead riding
    // the moving endpoint. That is the regular views' sentence verbatim, which
    // is the point of stating it here in the same words.
    const bool shift_placement_press =
        shift && !ctrl && !alt && inside_waveform;

    if (alt && !ctrl && !shift) return !inside_waveform;
    if (ctrl && !shift && !alt) return !inside_waveform;
    if ((ctrl || shift || alt) && !shift_placement_press) return true;

    // PLAIN FROM HERE, plus the shift-exact waveform press above, which reaches
    // only the waveform arm at the bottom: the top-strip bands are tested by y
    // alone and the waveform starts where the top strip ends (waveform_area,
    // main.cpp), so no shift press can land in one of them. Each band lies
    // inside the top strip, which spans the full window width, exactly as their
    // own claims test them.
    // (THE RULER'S FALL-THROUGH IS DELETED with the trim surface arc,
    // 2026-08-11: it existed to reach the live strip-drag arm — the mode's
    // admitted zoom — and that ruler entry is deleted product-wide, the ruler
    // rows being the merged trim surface's now. In-view zoom keeps its other
    // three roads: the ctrl-waveform drag, the keys, the touch gestures.)
    // THE BAND'S DOUBLE-CLICK ZOOMS TO THE DIFF SPAN (architect 2026-08-05,
    // SUPERSEDING the single click this act shipped with earlier that day): the
    // mode's fourth plain act, and it is the REGULAR VIEWS' GESTURE EXACTLY, on
    // the same band and through the same machinery. So a SINGLE plain click here
    // is the consumed nothing a motionless trim-bar click is everywhere in the
    // product (architect 2026-07-30), and only the second click
    // inside the window frames. (The comparison this used to draw was to a
    // LOCKED TAB, whose trim drags refused while its framing double-click
    // navigated; that model is retired — read-only stopped refusing trim on
    // 2026-08-07 — and the mode is the only per-zone consumer of the band left.) The band is showing that span already
    // (paint_trim's display-only substitution while the view stands), which is
    // what makes the gesture read as "zoom to what the bar shows" — and the
    // empty-delta case falls to the framer's whole-song arm, which is harmless
    // where the edges have already left the view. The WHOLE band, endcaps
    // included: those endcaps are painted geometry with no gesture in here, so
    // splitting the band would be a distinction nothing acts on. Read-only does
    // not refuse it — framing is navigation, exactly as the pan and the zoom
    // above are.
    //
    // THE MACHINERY IS THE LIVE BAND'S, UNCHANGED: consume-before-arm through
    // the shared test (trim_bar_double_click_at, which reads the snapshot the
    // press took before clearing the field), then the seed RECORD for the
    // release to resolve — TrimBarPressSeed, whose release-side owner needs no
    // mode arm of its own, since it seeds on "the pointer never left the slack
    // and no trim drag went live" and no trim drag can go live in here at all.
    // Only the COMMAND differs: the viewed checkpoint's diff span instead of the
    // region / trim / whole-song ladder.
    {
        // THE MERGED TRIM SURFACE, whole (2026-08-11): the mode consumes the
        // band's trim vocabulary and its region former alike — a plain press
        // anywhere on it is the consumed nothing that seeds, and the
        // double-click frames, over the band's full height. The view's own
        // region former stays the waveform press below; the band does not draw
        // regions in here, the mode's band being a viewer's.
        const GuiRect trim_bar = top_trim_surface_area(app);
        if (y >= trim_bar.y && y < trim_bar.y + trim_bar.h) {
            if (trim_bar_double_click_at(dc_at_press, x, y)) {
                frame_viewed_commit_diff_span();
                return true;
            }
            app.trim_bar_press = TrimBarPressSeed{
                .active = true, .press_x = x, .press_y = y};
            return true;
        }
    }
    const GuiRect lane = top_marker_row_area(app);
    if (y >= lane.y && y < lane.y + lane.h) {
        // hit_test_flag SERVES THE MODE UNCHANGED: while the mode stands the
        // painter's stash holds the diff flags' rects, published by the same
        // pass that built app.history_mode.flags, so the index it returns is an
        // index into that list — a double-width changed pair claiming as the one
        // rect it is painted as. A cold stash answers -1, which is the
        // empty-lane answer and is correct: nothing is clickable that is not
        // drawn.
        focus_history_diff_flag(hit_test_flag(app, audio, x, y));
        return true;
    }

    // THE WAVEFORM, FULL HEIGHT, reached by elimination: the top-strip bands
    // above all returned, so what is left is the surface the live placement
    // press owns. Both halves take the same act since playback left the view —
    // the lower half was the scrub's until 2026-08-05.
    if (inside_waveform) {
        // THE STEM CLAIM FIRST — the live press's own order between the two
        // surfaces of one item (on_button_press resolves the stem beside the
        // flag hit, ahead of every branch that consumes it). hit_test_marker_stem
        // is unchanged and needs to be: it reads the painter's stash, which the
        // mode's own lane pass publishes for the diff flags (render_history_diff_-
        // flags, the sole producer while the mode stands), so `marker_index` is
        // an index into app.history_mode.flags exactly as the flag rects' is, a
        // changed pair carries the ONE stem its one ordinal names, and the
        // clickable stems are the DRAWN stems by construction — no second
        // predicate decides which diff classes stem. The half test and the
        // grab tolerance come from that owner too.
        // PLAIN ONLY, and this gate is now the WHOLE of the symmetry ruling on
        // this surface (architect 2026-08-06): a modified click over the
        // waveform never resolves a stem in either view, so the shift press
        // that reaches here skips the claim and goes straight to the placement
        // press beneath it. Ctrl never arrives at all (it left for the strip
        // drag far above).
        if (!shift) {
            const int stem_hit = hit_test_marker_stem(app, x, y);
            if (stem_hit >= 0) {
                focus_history_diff_flag(stem_hit);
                return true;
            }
        }
        // THE PLACEMENT PRESS. The focus clear runs FIRST, ahead of the shared
        // body's own gutter return, exactly where the live body's deselect runs
        // — this is that deselect's mode analog, and the clearer inventory at
        // AppState::HistoryMode::focus carries the reason. It takes the SELECTION
        // with it through the one clearer, the pair going together everywhere.
        if (clear_history_mode_focus(app.history_mode)) {
            // A DISCRETE COMMAND, so full-window damage for the face swap — the
            // same shape the focus click emits on a move.
            viewport.invalidate_all();
        }
        // The live recipe whole: the clamped column frame, the cursor write, the
        // live session's reseek and the follow override — AND the region arm
        // (architect 2026-08-05), which is what makes this the regular views'
        // press rather than a press that is complete at the press. The arm
        // dissolves any resting highlight at mouse-down exactly as it does
        // outside, so the live press's own region clear is inherited rather than
        // spelled here (the clear-site list is at clear_region_highlight,
        // input_handler.h), and past the gutter return as there: a click with no
        // column to seat a playhead at commits nothing.
        const int64_t sample = place_playhead_at_click_column(
            x - area.x, playback.is_playing(), app.playhead_cursor_sample);
        if (sample >= 0) arm_region_drag_at(sample, x, y);
        return true;
    }

    return true;
}

// THE MODE'S PLAIN FOCUS CLICK, one body for its two pointer surfaces — the diff
// flag's box in the marker lane and its STEM in the waveform's upper half. `hit`
// is an index into app.history_mode.flags (-1, or anything the list no longer
// holds, being the empty-lane answer: clear the focus and land nothing).
//
// IT CLEARS THE MULTI-SELECTION (architect 2026-08-05), which is the live
// model's own shape rather than a rule of the mode's: a plain click REPLACES the
// selection with what it hit, so the focus alone is then a selection of one, and
// the revert act's subject falls back to that focus with nothing to remember.
//
// AND IT CLEARS ANY RESTING REGION, UNCONDITIONALLY (architect 2026-08-06, from
// his live pass: sweep a span in the view, then click a diff flag, and the span
// used to stay). The old "it touches no region" minimalism was written when the
// view could not FORM a region at all; the placement press gave it one the same
// day the flag click was ruled, and this click lands the playhead, so the live
// clear-site rule reaches it — a flag click is a POINT command and takes the
// scratch with it. UNCONDITIONAL is the live marker click's own shape, not a
// looser reading of it: all three live marker clicks clear whatever the land did
// or did not move, so the EMPTY-LANE click (`hit` < 0, which lands nothing)
// clears too, exactly as the live empty-lane press does through its region arm.
// The rest of the minimalism STANDS: no store selection, no live focus, no
// auto-select, no playback stop.
void GuiInputHandler::focus_history_diff_flag(int hit) {
    const int was = app.history_mode.focus;
    const bool had_selection = !app.history_mode.selection.empty();
    // clear_region_highlight owns its own damage and is a no-op with no span
    // resting, so this needs no gate of its own.
    clear_region_highlight(app, viewport);
    // THROUGH THE ONE PAIR CLEARER (2026-08-06, closing the one route that
    // cleared the pair inline): the EMPTY-LANE answer below is a clear of both
    // fields, so it goes through the owner like every other clearer, and the
    // hit arm simply re-seats the focus over it. The return is deliberately
    // unread — the damage decision below is finer than "anything stood".
    clear_history_mode_focus(app.history_mode);
    if (hit >= 0 && hit < static_cast<int>(app.history_mode.flags.size()))
        app.history_mode.focus = hit;
    if (app.history_mode.focus >= 0) {
        land_playhead_on_source_frame(
            app, audio, viewport,
            app.history_mode.flags[
                static_cast<std::size_t>(app.history_mode.focus)].time_frame);
    }
    // A DISCRETE COMMAND: full-window damage when the focus actually moved
    // (the flag's colour swaps and its stem stays put), and none when it did
    // not — a re-click on the focused flag re-lands a playhead that is
    // already there, and the land owner is itself idempotent. A DROPPED
    // SELECTION is that same face swap over more flags, so it damages too.
    if (was != app.history_mode.focus || had_selection) viewport.invalidate_all();
}

// THE MODE'S TWO MODIFIED CLICKS, one body — `extend` true for SHIFT, false for
// CTRL — and `hit` is an ordinal into app.history_mode.flags that the caller has
// resolved from the flag's LANE BOX, the only surface these two clicks have
// (architect 2026-08-06, the symmetry ruling: selection is lane vocabulary in
// both views, and the stem-based pair this body briefly also served is gone). It
// is the LIVE selection model re-expressed over the mode's own list
// (selection-model.md):
//
//   SHIFT extends: the contiguous ordinal RANGE from the focus to the clicked
//   flag, inclusive, REPLACING whatever stood — the list is frame-sorted, so an
//   ordinal range IS the visible span between the two flags. With NO focus
//   standing there is no anchor to span from, so it selects the clicked flag
//   alone, which is what the live range press does from an empty selection.
//
//   CTRL toggles: the clicked flag's membership alone, everything else untouched
//   — the one gesture that can leave an arbitrary subset standing, which is why
//   the set is a set.
//
// BOTH THEN FOCUS THE CLICKED FLAG AND LAND THE PLAYHEAD ON IT, the live model's
// "a modified press lands on the focus it sets" applied here, and both leave the
// STORE selection exactly as untouched as the plain click does — the mode owns no
// live marker.
//
// A CTRL PRESS THAT REMOVES THE LAST MEMBER therefore rests on the focus alone,
// with that flag still lit and still the act's subject. That is the recorded
// consequence rather than an oversight, and it is the SAME state a plain click
// leaves ("focus = a selection of one"): the live column repairs its focus onto
// another member when a toggle removes the focused one, and this mode has no
// such repair to run — its focus is where the playhead just landed, which the
// removal did not take back.
//
// BOTH ALSO CLEAR ANY RESTING REGION (architect 2026-08-06), the plain click's
// own rule at the same strength: a click that lands the playhead takes the
// scratch span with it, and all three live marker clicks — the single-select and
// this modified pair — clear unconditionally. PAST THE RANGE GUARD BELOW, so a
// call that changes nothing clears nothing, which is where the live toggle's own
// clear sits too (inside its hit gate).
//
// DAMAGE IS THE FOCUS CLICK'S: full-window, unconditional here, because either
// arm changes at least one flag's face (ctrl always flips the clicked one's
// membership; shift always writes a set containing it) — and where it would not,
// a repaint of the strip is the same cost the plain click pays.
void GuiInputHandler::select_history_diff_flags_modified(int hit, bool extend) {
    const int n = static_cast<int>(app.history_mode.flags.size());
    if (hit < 0 || hit >= n) return;
    clear_region_highlight(app, viewport);
    std::set<int>& sel = app.history_mode.selection;
    if (extend) {
        const int anchor = (app.history_mode.focus >= 0 &&
                            app.history_mode.focus < n)
                               ? app.history_mode.focus : hit;
        const int lo = anchor < hit ? anchor : hit;
        const int hi = anchor < hit ? hit    : anchor;
        sel.clear();
        for (int i = lo; i <= hi; ++i) sel.insert(i);
    } else {
        const auto it = sel.find(hit);
        if (it != sel.end()) sel.erase(it);
        else                 sel.insert(hit);
    }
    app.history_mode.focus = hit;
    land_playhead_on_source_frame(
        app, audio, viewport,
        app.history_mode.flags[static_cast<std::size_t>(hit)].time_frame);
    viewport.invalidate_all();
}

void GuiInputHandler::close_dropdown() {
    app.dropdown.menu_row_armed = false;
    if (!app.dropdown.open()) return;
    const GuiRect painted = app.dropdown.rect;
    app.dropdown = AppState::Dropdown{};
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

void GuiInputHandler::toggle_dropdown(DropdownMenu menu) {
    // THE SETTINGS MENU AND THE `h` HISTORY MODE ARE NEVER UP TOGETHER, and this
    // is the half of that rule the mode cannot enforce from its own gates: the
    // mode refuses to OPEN while a popup stands (the entry sits below on_key's
    // dropdown gate), and this line refuses THAT menu while the MODE stands. The
    // guard belongs here for the same reason the flag-editor teardown below
    // does — this is the ONE route every open passes, the anchor click, the
    // hover switch and the armed re-open alike.
    //
    // IT IS ALSO WHAT CLOSES THE MODE'S ONE POINTER BYPASS, and that bypass is
    // exactly what the SETTINGS half is scoped to. Every other route out of row 1
    // dispatches a synthesized chord through on_key and so meets the mode's
    // keyboard allowlist; the two anchors do not, and a Settings item opens the
    // settings editor by a DIRECT call (finish_dropdown_release), reaching no
    // gate at all. Refusing the menu is one line where covering that path per
    // item would be several.
    //
    // THE NAVIGATION MENU NEEDS NO SUCH COVER AND IS LIVE IN THE VIEW (architect
    // 2026-08-08): it has no direct call to shut — every one of its seven rows is
    // a CHORD, dispatched through on_key exactly as a redesigned button's is, so
    // the mode answers PER ITEM at the same two gates a key meets (the allowlist
    // admits zoom in / out / overview; history_mode_owns_key claims
    // center-on-focus and the two marker steps as re-expressions over the diff
    // flags). The one row whose chord means something ELSE in here — "Walk both
    // tabs", the mode's reverse walk-tab cycle — greys at the item instead
    // (dropdown_item_enabled, app_state.h), which is the only thing a chord
    // dispatch cannot answer for: the command runs, it is simply not the one the
    // label names.
    //
    // THE GUARD STAYS ABOVE THE CLOSE BELOW, and now that matters rather than
    // being trivially safe: with Navigation open in the mode, a hover switch onto
    // the dead Settings anchor arrives here, and returning from ABOVE the close is
    // what makes it a nothing — it neither puts Navigation away nor opens
    // Settings, so the pointer crossing a dead anchor leaves the standing menu
    // exactly as it was.
    if (app.history_mode.active && menu == DropdownMenu::Settings) return;
    // ONE STATE, SO ONE MENU: a press on the OPEN menu's own button closes it
    // (the gesture that opened it, closing it), and a press on the OTHER menu's
    // button switches — the close below runs first, damaging the box that is
    // leaving, and the open then proceeds. "Two dropdowns are never open
    // together" needs no rule beyond this: the field holds one value.
    const bool same = (app.dropdown.menu == menu);
    close_dropdown();
    if (same) return;
    // OPENING A MENU ENDS AN ACTIVE FLAG EDIT, discarding it — exactly what a
    // press anywhere outside the editor's box already does. It is what keeps "a
    // popup and an editor are never open together" true, and the FLAG editor is
    // the one class that needs it: the three BOTTOM-STRIP modal editors (the
    // settings and load editors and the BPM bracket, the membership
    // modal_bottom_strip_editor_active names) swallow every press at the top of
    // on_button_press, above the row-1 band claim, so a menu button is not even
    // reachable while one of them is up — but the FlagPayload editor is
    // pointer-TRANSPARENT by ruling and swallows nothing, so its edit would
    // otherwise stand under the open menu. That is not merely untidy: a settings
    // item opens the settings editor, on_key tests the flag editor FIRST, and
    // the typing would land in the flag buffer while the settings editor is what
    // looks focused.
    //
    // IT SITS ON THE OPEN PATH RATHER THAN AT THE BAND CLAIM because this is the
    // ONE route every open passes — the anchor click, the hover switch and the
    // armed re-open all arrive here — while the band claim carries only the
    // click. The hover routes are arguably out of reach with an edit open (the
    // row arms only through a click, which would itself have closed the edit),
    // but a rule that is impossible by construction is worth more here than one
    // that is unreachable by argument. A toggle that CLOSES a menu discards
    // nothing: it returned above.
    //
    // The teardown is called DIRECTLY rather than through
    // close_top_flag_editor_for_outside_press, whose job includes the
    // is-the-press-inside-the-box test. There is no press here at all on the
    // hover routes, and the rect test is not merely skipped but INAPPLICABLE: a
    // row-1 button cannot be inside the flag editor's box, which lives in the
    // marker lane below the whole top strip's button rows.
    //
    // THE REST OF ROW 1 IS DELIBERATELY OUT OF SCOPE. Quit needs nothing: its
    // Ctrl+Q is one of the three chords the keyboard-modal gate admits, so it
    // tears the edit down through its own route. The view bar's bare 1/2/3 drop
    // at that gate as consumed nothings — the modality ruling working as
    // intended — and ending an edit there would be a behavior change nobody
    // asked for.
    if (text_editor::is_active(app.top_flag_editor)) {
        flag_editor.exit_top_flag_edit_no_commit();
    }
    app.dropdown.menu         = menu;
    app.dropdown.hovered_item = -1;
    // OPENING A MENU ARMS THE ROW — the mode's ONE producer, and it sits here
    // because this is the ONE route that opens either menu: the anchor click, the
    // hover switch, and the armed hover re-open all arrive through it, and no
    // keyboard chord opens a dropdown at all. So "a menu is open" implies "the
    // row is armed" by construction, with no second producer to keep in step.
    // The bit's contract — what the mode does and what ends it — is at the field
    // (AppState::Dropdown::menu_row_armed).
    app.dropdown.menu_row_armed = true;
    // THE OPEN EDGE DAMAGES THE BOX BEFORE THE BOX EXISTS. Its rect is not
    // published until paint_dropdown runs, and a redraw is CLIPPED to the
    // damage it was handed — so strip damage alone would clip away whatever the
    // popup hangs past the strip. At 100% the settings menu happens to fit
    // (175px of popup inside a 217px strip); at 200% it overhangs by ~40px and
    // the bottom band would never paint.
    //
    // The HEIGHT is derivable without painting (dropdown_h_px — item count
    // times item height, plus the separator blocks and the borders), so the
    // damage is exact vertically. The WIDTH is not (it needs the widest shaped
    // label), so this damages FULL WIDTH from the button's top down — a band,
    // not a guess, and cheap because it happens once per open.
    //
    // THE TOP EDGE IS THE BUTTON'S BOTTOM, the same expression paint_dropdown
    // places the box at — the dropdown hangs off the thing that opened it
    // (architect 2026-08-02), which since row 1 gained its 1px margin-bottom is
    // one pixel above the lane's own bottom. Both sites read the SAME stashed
    // rect through the SAME anchor owner, so the damaged band and the painted
    // box start on the same row of pixels; a band anchored a pixel lower would
    // leave the popup's own top border unpainted.
    {
        const GuiRect& btn =
            app.redesign_buttons[redesign_button_index(
                dropdown_anchor_button(menu))].rect;
        viewport.invalidate_rect(
            GuiRect{0, btn.y + btn.h, app.width, dropdown_h_px(menu)});
    }
    // THE TOOLTIP GOES DOWN ON THE OPEN EDGE — the two floating surfaces cannot
    // coexist (paint_handler.h states the pair), and this is the one line that
    // makes that structural rather than a reachability argument about which
    // routes reach an open. It is NOT the roster clear's doing:
    // clear_redesign_button_hover writes the faces' `hovered` bits and nothing
    // else, the tooltip's dwell and visibility living in their own state
    // (AppState::redesign_tooltip) with hide_shift_tooltip as their one hide
    // owner. The hover recompute would reach the same answer on the NEXT motion
    // (no roster button hovers under a popup, so the dwell walk finds no owner
    // and hides), but "next motion" is not a property — an open reached with the
    // pointer standing still has no next motion.
    hide_shift_tooltip();
    // THE ROSTER UNHOVERS AT THE OPEN: the pointer belongs to the popup, and
    // redesign_button_hoverable refuses the WHOLE roster while a dropdown is up.
    // A press carries no motion of its own and the pointer may then stand still
    // indefinitely ("next motion" is not a property, as the tooltip hide above
    // says), so a face lit at the moment of the open
    // would otherwise stay lit under a surface that has taken the pointer from
    // it. THE TOOLTIP STAYS DOWN for as long as the popup is up on the same
    // predicate's OTHER half: redesign_button_hover_zone (the term hoverability
    // and the dwell still share — the enabled term is the one they parted on)
    // refuses every button while a menu is open, so the dwell writer
    // (recompute_redesign_button_hover) can never stamp a new one.
    //
    // ROW 1 IS CLEARED HERE TOO AND STAYS CLEAR while the menu is up; it
    // re-resolves at the next motion or the next tick (main.cpp runs the same
    // recompute per frame, gesture-gated only) once the popup is down. That
    // costs nothing visible: the only row-1 button the pointer can be on at an
    // open edge is the one just pressed, and that is the popup's ANCHOR, whose
    // pill the paint condition below keeps regardless of the hover bit.
    //
    // IT NO LONGER DECIDES THE MENU BUTTON'S PILL, and the inversion is worth
    // stating because this line used to be what darkened it: since 2026-08-02
    // the pill paints on the popup's own ANCHOR as well as on hover (kdenlive's
    // behaviour, argued at the paint site), so the button whose menu is up stays
    // blue for exactly as long as it is up. This clear now serves the OTHER
    // roster buttons and the tooltip; the anchor's face is the paint condition's
    // business.
    clear_redesign_button_hover();
    viewport.invalidate_top_strip();
}

// THE MENU ROW'S MODE, EXIT HALF — "the pointer left row 1, go cold", which is
// what keeps the mode from outliving the visit: wander down to the waveform and
// Settings needs a click again. The band is point_in_menu_row_band (app_state.h),
// which wraps top_menu_row_area — the press claim's own rect, so "on the row"
// means one thing to the claim, to this exit and to the pointer-leave hook, the
// predicate's second consumer.
//
// IT RUNS WHEREVER MOTION IS SEEN — on_motion's very top, above every branch —
// and that is the half's whole design. The OPEN half below has a guard list (it
// must not spring a menu open under a modal or mid-gesture); this half has none,
// because a modal owning the pointer is a reason not to OPEN a menu and no
// reason at all to forget that the pointer left the row. Sitting in the
// no-gesture tail with the open half is exactly what it must not do: a prompt,
// an editor, an editor text drag or any live gesture returns above that tail, so
// the pointer could leave row 1 unnoticed and carry an invisible armed mode out
// of the visit.
// RUNNING IT DURING A GESTURE COSTS NOTHING AND IS NOT A CHANGE OF RULE: a press
// already ended the mode (on_button_press's top), so a live gesture's motion
// finds the bit false and this is a compare. What it buys is the modal branches.
// WITH A MENU OPEN IT MUST NOT FIRE — the popup hangs below the row, so the
// pointer leaves the band the moment it moves into it — and it cannot:
// disarm_menu_row carries that gate for all of its callers.
void GuiInputHandler::update_menu_row_exit(int mouse_x, int mouse_y) {
    if (!app.dropdown.menu_row_armed) return;
    if (point_in_menu_row_band(app, mouse_x, mouse_y)) return;
    disarm_menu_row();
}

// THE MENU ROW'S MODE, OPEN HALF (architect 2026-08-03): once a menu has been
// opened from row 1, the two anchors open on the POINTER ALONE — the menu-bar
// behaviour every desktop has, and the completion of the row-1 hover close,
// which puts a menu away and now leaves the row able to bring one back. The bit
// and its whole contract are at AppState::Dropdown::menu_row_armed.
//
// ITS PLACEMENT IS ITS GUARD LIST. It is called from on_motion's no-gesture
// tail and nowhere else, so the conditions the re-open must not fire under are
// the branches that already return above it — an open dropdown (which owns the
// motion outright), the prompt, the editor text drag, the two bottom-strip
// keyboard-modal editors, and every live gesture and pending — PLUS THE ONE
// condition the call site restates (codex round 2): a HELD PRIMARY BUTTON,
// which does not return above (a held motion that armed no gesture reaches the
// tail — the touch resolution burst's pre-press entry motion, and a mouse
// press-hold sliding along the armed row; the producers are recorded at the
// call). With that guard the hover opens a menu in precisely the states in
// which a click opens one — a held-button motion could never be a fresh press
// anyway. (The pointer-transparent FLAG editor gates
// neither route, by its own ruling — see the press claim; the open it leads to
// ENDS that edit, which is toggle_dropdown's business and not restated here.)
// IT TESTS NO BAND. Every anchor rect lies inside row 1 by construction, so a
// hit IS "on the row"; the exit half above owns the band question, at the one
// placement that can answer it for every branch.
void GuiInputHandler::open_menu_row_anchor_on_hover(int mouse_x, int mouse_y) {
    if (!app.dropdown.menu_row_armed) return;
    // ON AN ANCHOR, OPEN ITS MENU — through toggle_dropdown, the same owner the
    // CLICK uses, so the anchor expression, the open edge's damage and the roster
    // clear are one route with nothing restated. The walk covers every menu that
    // HAS an anchor rather than naming the pair, so dropdown_anchor_button stays
    // the one place that knows which button emits which menu. Anywhere ELSE on
    // the row — Quit, the view bar, the ground between them — is simply not an
    // anchor: the row stays armed and nothing opens.
    for (const DropdownMenu m : {DropdownMenu::Settings,
                                 DropdownMenu::Navigation}) {
        if (!redesign_button_hit(app, dropdown_anchor_button(m),
                                 mouse_x, mouse_y)) continue;
        toggle_dropdown(m);
        break;
    }
}

// THE MODE'S END, the one gated writer every route that is not close_dropdown
// goes through. Its callers, re-derived by grep: the band exit above, the
// pointer-leave hook (main.cpp, beside the row's other face clears — a pointer
// that has left the window has left the VISIT, the band exit's own reason at a
// coarser edge, NOT a claim that no motion can follow: a re-entry synthesizes
// one, and it finds a cold row that takes a click again, which is the intent —
// and that call is CONDITIONAL since 2026-08-08: an ORDINARY leave whose last
// position was inside row 1's band is a step onto the titlebar, not out of the
// visit, so the hook skips this call entirely there — while a pointer-CAPABILITY
// loss, the hard end of the stream, always makes it), ANY pointer press (on_button_press's
// top) and ANY key press (on_key's top). It damages nothing: the mode is
// invisible, painting no face of its own; what it changes is what the NEXT
// motion does.
//
// THE "NO MENU OPEN" GATE IS THIS FUNCTION'S REASON TO EXIST rather than four
// inline writes. Leaving the window is NOT a dismissal — the popup stays up, as
// clear_dropdown_pointer_state beside it states (only the popup's
// POINTER-DERIVED faces go) — and a menu still standing is still the
// mode, so re-entering over the other anchor must SWITCH rather than find a cold
// row (and the row-1 hover close, which re-arms, must not be resurrecting a mode
// something else meant to end). The same answer serves the two press callers for
// a second reason: while a popup is up, a press or a chord belongs to the POPUP,
// whose own routes (close_dropdown, the toggle) decide the mode — so these
// blanket disarms cannot get in front of them. A dismissal with a menu open
// therefore always ends the mode through close_dropdown, and a dismissal with
// none open ends it here.
void GuiInputHandler::disarm_menu_row() {
    if (app.dropdown.open()) return;
    app.dropdown.menu_row_armed = false;
}

// THE POPUP'S POINTER-DERIVED STATE, DROPPED AT THE ONE HOOK FIRED ON BOTH
// the pointer-leave AND capability-loss edges (codex 2026-08-03: the two are
// not the same). Capability loss ends that pointer stream outright — no
// motion or release for it will ever arrive again. An ordinary leave has no
// event only WHILE the pointer stays outside; it may re-enter (a synthesized
// motion) and a still-held button may release normally afterward. Both faces
// are dropped here regardless, and BOTH FACES GO, one function rather than
// two, because BOTH are answers to "where is the pointer" and the pointer is
// gone:
//   * the ARMED item — the pressed face would otherwise stay lit under a
//     pointer that has left; on capability loss no release ever comes to
//     un-press it, and on an ordinary leave a later release lands on nothing
//     because clearing the press claim below leaves it unowned (below);
//   * the HOVERED item, with no motion to follow while the pointer stays
//     outside — the painter lights an item whenever `hovered_item` names it,
//     with no pointer_in_window term of its own, so a flick out of the
//     window whose last on-surface motion was still over an item leaves that
//     item lit until a RETURN motion recomputes it (capability loss has no
//     return to wait for; this branch's actual audience is the ordinary
//     leave). Nothing else clears it: recompute_dropdown_hover REFUSES while
//     the pointer is outside the window — that guard is precisely what stops
//     its per-iteration caller (main.cpp's settled hook) from re-lighting from
//     the remembered coordinates what this function drops — and
//     close_dropdown's struct reset needs a dismissal this edge is not.
// THE MENU ITSELF STAYS OPEN — leaving the window is not a dismissal — and the
// row's armed mode is the leave hook's own question, asked beside this call:
// with a menu open disarm_menu_row is inert (its no-menu-open gate), and with
// none open the hook skips it altogether when an ORDINARY leave went out THROUGH
// row 1's band, so the mode survives that leave the way the standing menu
// survives this one. On CAPABILITY LOSS neither survives on this reasoning: the
// popup does stay up, having no pointer-derived existence at all, but nothing
// pointer-derived is kept there — the leave reason gates the exception.
// THE PRESS CLAIM GOES ABOVE THE TRANSITION GATE: this is the button-LOST edge,
// so a re-entry that still reports the button down must not resurrect an arm no
// release will ever be attributed to. Coming back takes a fresh press, exactly
// as it did before the arm followed the pointer.
// ONE DAMAGE PAIR FOR BOTH FACES — the strip plus the popup's whole published
// box, recompute_dropdown_hover's own shape, which is what lets a frame that
// drops a hover on one item and an arm on another erase both with no per-item
// arithmetic. It is transition-gated like the roster clears beside it; with the
// menu closed both indices are already -1 (the struct reset) and this is a
// compare, which is also why the zero rect is never handed to the damage.
void GuiInputHandler::clear_dropdown_pointer_state() {
    app.dropdown.press_began_on_item = false;
    if (app.dropdown.hovered_item < 0 && app.dropdown.pressed_item < 0) return;
    app.dropdown.hovered_item = -1;
    app.dropdown.pressed_item = -1;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(app.dropdown.rect);
}

// THE HOVER TOOLTIP'S HIDE. Its callers, re-derived by grep: the hover
// recompute (the hover ended, or moved to another tooltip-bearing button), ANY
// pointer press and ANY key press (the hint's job ends the moment the user
// acts), any wheel, the dropdown's OPEN edge (the two floating surfaces never
// coexist), and TWO main.cpp hooks — the pointer-leave / capability-loss edge
// and the compositor close, the one modal opener that arrives with no key press
// to carry the key-press hide's own rule.
// THAT LAST ONE IS WHY THE RECOMPUTE IS NOT ENOUGH BY ITSELF. It damages the
// strip AND the box's last painted rect, and the box hangs BELOW the strip — so
// an edge that clears the hover bits without hiding here leaves the paint with
// no owner to draw, which publishes a zero rect and returns, and the overhang
// below the strip then has nothing left to erase it. clear_redesign_button_hover
// has exactly TWO callers, re-derived by grep, and both are covered: the
// dropdown's open edge hides two lines above its clear, and the leave hook now
// hides beside its own.
// Showing is the tick's job (the dwell); this is only the hide, plus the stamp
// reset that makes the next hover start its dwell from zero.
void GuiInputHandler::hide_shift_tooltip() {
    app.redesign_tooltip.hover_ms = 0;
    app.redesign_tooltip.owner    = -1;
    if (!app.redesign_tooltip.visible) return;
    const GuiRect painted = app.redesign_tooltip.rect;
    app.redesign_tooltip.visible = false;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

// THE CLICK FACE, dropped. The face rides the PHYSICAL button hold, so the
// two edges that end the pointer's claim on a hold both come here: the left
// release (on_button_release, at its very top so a modal or an early return
// cannot strand a lit button) and the pointer-leave / capability-loss hook.
// THAT SECOND EDGE IS "BUTTON-LOST" FOR THE FACE, not for the event stream:
// capability loss really does end the stream with no release to come, while an
// ordinary leave keeps the held state and delivers the release normally once it
// happens — but the face is a statement about where the pointer IS, so it goes
// either way, and without this a pointer that slides out of the window mid-press
// would leave the interior filled for the whole absence. The later release is
// then a no-op on this transition gate, which is what makes the early clear safe
// rather than any inability of the release to arrive. Transition-gated
// like the hover clear beside it, and one invalidate_top_strip when it fires.
void GuiInputHandler::clear_redesign_button_press() {
    // THE ARROWS' HOLD-REPEAT ENDS WITH THE SAME HOLD (row 8, 2026-08-11), so
    // its disarm lives here, ABOVE the face's own early return: the repeat and
    // the click face ride one physical hold, and every edge that ends the hold
    // for the face — the release, the pointer leave, capability loss — ends
    // the repeat with it, with no second edge list to keep in step. (The face
    // gate below cannot cover it: a disabled-mid-hold button could drop its
    // face while the schedule stood.)
    app.transport_repeat.owner = -1;
    if (app.redesign_pressed < 0) return;
    const bool transport = redesign_button_in_transport_row(
        static_cast<RedesignButton>(app.redesign_pressed));
    app.redesign_pressed = -1;
    if (transport)
        viewport.invalidate_rect(bottom_transport_row_area(app));
    else
        viewport.invalidate_top_strip();
}

// THE ARROWS' HOLD-REPEAT, fired from the run loop's tick (row 8, 2026-08-11):
// while one of the transport row's four cardinal arrows is physically held,
// synthesize its chord on the keyboard repeat's own cadence — the labwc-
// matching delay and rate above — stamped GuiInputState::synthesized_repeat,
// so the undo side sees exactly a held key: the physical press pushed the
// entry (dispatch_redesign_chord, synthesized_repeat false), every repeat
// coalesces by repeat identity, and no new undo semantics exist.
//
// THE HOLD'S EDGES ARE THE PLATFORM KEY-REPEAT CONTRACT'S, SHARED AND
// MIRRORED — the authoritative inventory is at AppState::transport_repeat's
// declaration; this body carries only its own members. THE HOLD'S RECORD IS
// THE CLICK FACE (app.redesign_pressed): the arm rides
// the same physical hold, and clear_redesign_button_press — the release, the
// pointer leave and capability loss — disarms both together, so "the face is
// standing on the armed button" IS "the hold is standing". A repeat fires only
// with the pointer STILL ON the button (the scrollbar-button rule): sliding
// off pauses the schedule silently and sliding back on resumes it, the hold
// standing throughout — a leave that exits the WINDOW ends the hold outright
// through the hook, and a re-entry mid-hold does not re-arm (no new press).
// Each fire re-checks the press-time eligibility predicate (layer 2's mirror,
// at the check below) and the enabled bit — the disabled-press consume's
// mirror,
// so a button that went dead under the hold (the history view opening, say)
// pauses exactly as the claim would refuse — and dispatches through on_key,
// where every gate answers as it would for the held key itself. One fire per
// due tick, the next scheduled from NOW rather than accumulated, so a stalled
// frame yields one repeat and no burst.
void GuiInputHandler::tick_transport_arrow_repeat() {
    AppState::TransportArrowRepeat& rep = app.transport_repeat;
    if (rep.owner < 0) return;
    if (app.redesign_pressed != rep.owner) {   // hold gone under the schedule
        rep.owner = -1;
        return;
    }
    const int64_t now = monotonic_ms();
    if (now < rep.next_due_ms) return;
    rep.next_due_ms = now + kArrowRepeatIntervalMs;
    const AppState::RedesignButtonFace& f =
        app.redesign_buttons[static_cast<size_t>(rep.owner)];
    if (!app.pointer_in_window ||
        !rect_contains(f.rect, app.last_mouse_x, app.last_mouse_y))
        return;                                // paused off the button
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_index(tc.id) != rep.owner) continue;
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift;
        chord.alt   = tc.alt;
        // LAYER 2's MIRROR (codex round 3): each fire re-checks the press-time
        // predicate, exactly as the platform's maybe_fire_repeat re-checks its
        // own eligibility per fire. The three event-edge disarms cover every
        // context change an INPUT causes; this covers the ones none does — the
        // WM close raising the quit prompt is the live case — and it DISARMS
        // rather than pauses: a context that revoked the burst's eligibility
        // ends the burst, it does not park it.
        if (!repeat_eligible(tc.key, chord)) {
            rep.owner = -1;
            return;
        }
        if (!redesign_button_enabled(app, audio.total_frames(), tc.id)) return;
        chord.synthesized_repeat = true;
        on_key(tc.key, chord);
        return;
    }
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a pending
// marker drag); with no gesture it recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the arming plain click already landed the playhead on the marker, so the
// drag tows it by construction).
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    // Record latest cursor coords so viewport mutators can re-evaluate hover
    // at the cursor's last position.
    app.last_mouse_x = mouse_x;
    app.last_mouse_y = mouse_y;
    app.pointer_in_window = true;
    // THE MENU ROW'S MODE ENDS WHEN THE POINTER LEAVES ROW 1, and that half is
    // resolved HERE, above every branch, because it is the only placement that
    // sees every motion: the modal branches and every live gesture return before
    // the no-gesture tail where the mode's OPEN half lives, and a mode that could
    // not notice the pointer leaving under a prompt or an editor would be carried
    // invisibly out of the visit. A modal owning the pointer is a reason not to
    // OPEN a menu, not a reason to forget where the pointer went. The two halves
    // and their asymmetric guard lists are at their definitions
    // (update_menu_row_exit / open_menu_row_anchor_on_hover).
    update_menu_row_exit(mouse_x, mouse_y);
    // (THE POINTER CURSOR IS NOT RESOLVED HERE, 2026-08-03. A push stood at this
    // spot — above every gesture branch, so that each early return below still
    // left the right cue up — and it went with the other twenty-two: the cursor
    // has ONE owner now, the run loop's per-iteration tail hook, which runs in
    // the SAME iteration that dispatched this event and reads the two lines
    // above. Recording the position is therefore all a motion owes the cursor.)
    // THE BUTTON HOVER IS A POINTER FACT AND FOLLOWS THE POINTER, under every
    // modal surface (architect 2026-07-31, fixing a stale Settings pill). It is
    // recomputed in the two MODAL branches that return before the no-gesture
    // tail, here and at the bottom-strip editors below, because a modal freezing
    // it leaves a lit pill under a pointer that has moved away — visible as a
    // button still coloured after Esc/Enter closes the editor, clearing only at
    // the next motion. THE ARGUMENT IS THAT HOVER IS A SEPARATELY MAINTAINED
    // POINTER FACT — "the pointer is over this rect" is true or false regardless
    // of who owns the keyboard — and NOT that these surfaces let pointer input
    // through: only the top-strip FlagPayload editor is pointer- and
    // wheel-transparent, while the settings editor, the load editor and
    // the BpmBracket kind (modal_bottom_strip_editor_active, which is also the
    // wheel swallow) take every press outside their own text row and drop it. The
    // prompt takes the same answer for the same reason: it suppresses no other
    // pointer affordance either (the marker hover POPUP's suppression right here
    // is the one ruled exception, and it is a different fact: a resolved marker
    // readout, not "the pointer is over this rect"). What the recompute must NOT
    // do under those surfaces is start a TOOLTIP dwell — a floating hint is not a
    // face, and that refusal lives in the recompute itself.
    // What DOES still freeze the hover is an active pointer GESTURE — the
    // branches below all return without this call, exactly as before.
    //
    // NO CLOSE-EDGE HOOK EXISTS OR IS NEEDED: with the recompute live through
    // the whole modal, the stash is already correct when the editor closes. A
    // pointer resting ON the button at the close keeps its lit pill, and that is
    // hover, not staleness.
    //
    // AN OPEN DROPDOWN REFUSES THE WHOLE ROSTER (redesign_button_hoverable,
    // app_state.h), so its branch below recomputes for one reason only: the frame
    // on which a row-1 button CLOSES the menu must also light that button, and
    // running the recompute after the close is what makes it one frame rather than
    // two. While the menu stays up the walk re-derives all-false, which costs a
    // pass of rect compares and no damage.
    // The ANCHOR of the open menu lights through the paint condition
    // (paint_menu_row) rather than through the hover bit — that is what keeps its
    // pill through the open edge's clear and what makes a switch visible on the
    // frame it happens.
    //
    // THE OPEN DROPDOWN TAKES THE MOTION, above every gate: it owns the pointer,
    // so no gesture can be live under it (its own press consumed everything).
    if (app.dropdown.open()) {
        // A NON-MENU ROW-1 BUTTON CLOSES THE OPEN MENU (architect 2026-08-03,
        // from kdenlive: only ONE button in that row is lit at a time, so sliding
        // from Navigation onto Quit must put Navigation's menu away rather than
        // leave it hanging under a second lit button). This REVERSES the earlier
        // "a menu bar keeps its menu up while the pointer crosses the rest of the
        // bar" reading — the bar's other buttons are not menu titles here, they
        // are commands, and a command button lighting beside an open menu is the
        // state the row does not have.
        //
        // THE MEMBERSHIP IS redesign_button_in_menu_row (app_state.h), walked
        // rather than named, so Quit and the view bar's three are covered by the
        // fact "row 1" and a new row-1 button inherits the rule by existing. An
        // ANCHOR is skipped: the OPEN menu's own does nothing at all (no re-open,
        // no close) and the OTHER one SWITCHES through the walk below, both
        // unchanged. The close goes through close_dropdown, the one close owner,
        // which carries the popup's damage.
        //
        // IT RUNS BEFORE THE ROSTER RECOMPUTE so the frame that closes the menu is
        // the frame the button lights on: with the popup already gone,
        // redesign_button_hoverable's dropdown refusal no longer applies and the
        // button under the pointer resolves to hovered in the very same call.
        //
        // THE FAMILY RULE, stated here at its first member in this branch (codex
        // rounds 2-3 built it one member at a time): NO ROW-1 HOVER ACT FIRES
        // WHILE THE PRIMARY BUTTON IS HELD — the armed hover-OPEN (on_motion's
        // no-gesture tail), the open-menu anchor-SWITCH (the walk below), and
        // this hover CLOSE, its last unguarded member. A held button is not a
        // resting hover at any of the three. For the TOUCH resolution burst's
        // held entry motion the tap's outcome is unchanged by guarding the
        // close: press-anywhere-closes at on_button_press already closes the
        // menu on the burst's own press, so the tap nets the same result by the
        // cleaner path. For the MOUSE, a mid-press slide across a non-anchor
        // keeps the popup's live press claim exactly as the switch guard keeps
        // it. The unheld hover close is untouched.
        if (!mods.primary_button_held) {
            for (int i = 0; i < kRedesignButtonCount; ++i) {
                const RedesignButton id = static_cast<RedesignButton>(i);
                if (!redesign_button_in_menu_row(id)) continue;
                if (id == dropdown_anchor_button(DropdownMenu::Settings) ||
                    id == dropdown_anchor_button(DropdownMenu::Navigation))
                    continue;
                if (!redesign_button_hit(app, id, mouse_x, mouse_y)) continue;
                close_dropdown();
                // THE MODE SURVIVES THIS ONE CLOSE (architect 2026-08-03, the
                // other half of the same behaviour): sliding onto Quit puts the
                // menu away but leaves the row ARMED, so sliding BACK onto
                // Settings or Navigation opens that menu again with no click.
                // This is a step across the bar, not a dismissal, and
                // close_dropdown disarms by default — so the exception is
                // spelled here, at the only site that needs it.
                app.dropdown.menu_row_armed = true;
                break;
            }
        }
        // The roster's own faces. While a popup is up this re-derives false for
        // the WHOLE roster (redesign_button_hoverable refuses every button then —
        // the pointer belongs to the popup), and after the close above it resolves
        // the row-1 button the pointer landed on normally, which is the one case
        // that needs it here.
        recompute_redesign_button_hover();
        // HOVERING THE OTHER MENU'S BUTTON SWITCHES TO IT (architect
        // 2026-08-03) — the menu bar's standing behaviour: open one menu, slide
        // onto the next button without pressing, and that button's menu is what
        // is up. The switch goes through toggle_dropdown, the same owner the
        // CLICK uses, so the close-then-open, the anchor expression and the open
        // edge's damage are one route with nothing restated here; a menu that is
        // not the open one makes that toggle's same-menu test false, which is
        // exactly the switch. The button it switches ONTO lights through the
        // painter's own anchor condition (paint_menu_row), not through the hover
        // bit, which is why the order of these two blocks is set by the CLOSE rule
        // above and not by the switch.
        //
        // The walk covers every menu that HAS an anchor instead of naming the
        // pair, so the anchor owner stays the one place that knows which button
        // emits which menu. Hovering the OPEN menu's own anchor is skipped
        // outright — no re-open, no close, no damage. A row-1 button owning no
        // dropdown never reaches here at all: the close rule above consumed it
        // and the menu is already down.
        //
        // THE SWITCH REFUSES UNDER A HELD PRIMARY BUTTON (codex round 3) — the
        // family rule's second member (the three-member statement is at the
        // close walk above; the armed hover-open is the first) — a held
        // button is not a resting hover here either, on the same two producers:
        // the TOUCH resolution burst's pre-press entry motion (with a menu OPEN
        // and the other anchor tapped, that motion reached this walk, switched
        // menus, and the burst's press then found its own menu already open and
        // toggle-closed it — the tap closed the popup instead of switching to
        // it; guarded, the switch is the PRESS's own toggle_dropdown), and the
        // MOUSE's own latent case, which this guard fixes too: press-hold an
        // item (or the anchor press's drag-into-the-box claim) and slide across
        // the OTHER anchor — the switch fired mid-press, and toggle_dropdown's
        // close wipes the whole Dropdown struct, destroying the popup's live
        // press claim (pressed_item / press_began_on_item) out from under the
        // held button, so the coming release acted on a menu the press never
        // touched. The ordinary unheld hover-switch is unchanged.
        if (!mods.primary_button_held) {
            for (const DropdownMenu m : {DropdownMenu::Settings,
                                         DropdownMenu::Navigation}) {
                if (m == app.dropdown.menu) continue;
                if (!redesign_button_hit(app, dropdown_anchor_button(m),
                                         mouse_x, mouse_y)) continue;
                toggle_dropdown(m);
                break;
            }
        }
        // The popup's own item hover AND its armed item, beside row 1's — the
        // two are the whole hover answer while a menu is up, and they cannot
        // collide, since the box starts below the row. AFTER
        // A SWITCH (and after any open) the new menu's item rects have not been
        // published yet — the painter publishes them — so this call resolves to
        // no item, and nothing here reaches into the painter for geometry to
        // avoid that. WHAT FINISHES THE JOB IS THE SETTLED BOUNDARY, not the next
        // motion: main.cpp's per-iteration hook calls this same walk again, so the
        // iteration that paints the box ends by resolving against it and the item
        // lights on the next painted frame even if the pointer never moves again
        // (the standing "self-corrects on the next motion" reading was true only
        // while the pointer kept moving, and it is retired).
        // THIS CALL STAYS ANYWAY, and per DELIVERED MOTION rather than per
        // iteration: a dispatch batch can carry a motion and then a PAINT with no
        // loop tail in between, and the faces this walk writes are what that paint
        // reads. The RELEASE is no longer among its dependants — it derives the
        // item from its own coordinates (the full argument is at the definition).
        // `mods` carries the platform's button state, which is what lets the arm
        // follow the pointer under a live press (the rule is at the definition).
        recompute_dropdown_hover(mods);
        return;
    }
    if (app.prompt.active) {
        recompute_redesign_button_hover();
        return;
    }
    // F2.1: editor-text drag motion. Handled before the settings swallow
    // (which returns) so the gesture reaches the bottom-strip editors, and
    // before the trim / playhead branches. A lost button finalizes like
    // release, mirroring those handlers.
    if (app.editor_text_drag.active) {
        if (!mods.primary_button_held) {
            // THE BUTTON-LOST ARMS END A GESTURE AND OWE THE CURSOR NOTHING,
            // exactly like the clean-release arms they mirror: the teardown
            // below invalidates the map's live-gesture answer, and this
            // iteration's tail re-derives it from the position the top of this
            // function just recorded. (The rule, and what becomes of it while a
            // capture's position is virtual, are at on_button_release's header.)
            finalize_editor_text_drag();
            return;
        }
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            // The anchor set at press stays put; moving cursor_pos extends
            // the selection.
            set_editor_caret_from_x(g, mouse_x);
            if (g.bottom_strip) viewport.invalidate_status_row_area();
            else                viewport.invalidate_top_strip();
        }
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        return;
    }
    if (text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.load_editor) ||
        text_editor::is_active(app.commit_title_editor)) {
        // The button hover stays live under a keyboard-modal editor — the
        // rationale is at the prompt branch above, and THIS is the branch the
        // reported staleness came through (the Settings button opens the
        // settings editor, whose gate is right here).
        recompute_redesign_button_hover();
        return;
    }
    // Dual-axis strip drag (the incremental v6 model; see apply_strip_drag_at).
    // Each event pans by its dx at the live level and zooms by its dy off the
    // live level, pivoting the zoom around the (edge-rebindable) song anchor. The
    // repaint is SYNCHRONOUS (final_event=false): one full rebuild whenever the
    // level or the viewport moved, a true
    // no-op when neither did — affordable because the platform coalesces captured
    // motion to one event per pointer frame. The release runs the one synchronous
    // rebuild plus the predictor resync. A lost button finalizes like release.
    if (app.strip_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.strip_drag.moved) {
                apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/true);
            }
            // A motionless press ends with NO click action (the ctrl-waveform
            // selection clear is RETIRED — a motionless ctrl+waveform
            // press-release commits nothing on the clean release either, so this
            // abnormal end matches it for free). It owes the same ANCHOR STEM
            // ERASE the clean release does — full waveform-area damage, the
            // discrete shape — the stem having painted from the press.
            app.strip_drag = StripDragState{};
            viewport.invalidate_waveform_area();
            // An abnormal termination (button lost, not a clean release) seeds
            // no double-click candidate and drops any pending one.
            app.double_click = DoubleClickCandidate{};
            end_strip_pointer_capture();
            return;
        }
        // Sub-pixel capture jitter must not promote a click to a drag: while the
        // press has not yet become a drag, apply nothing until the pointer has
        // travelled at least the Chebyshev threshold from the press, leaving the
        // drag armed but unmoved. This gate decides only WHETHER the press
        // becomes a drag — once moved it never re-engages, so dragging back near
        // the press mid-drag has no dead zone. last_x/last_y stay at the press
        // until the crossing, so the crossing event folds the whole accumulated
        // delta since the press and no travel is lost.
        if (!app.strip_drag.moved &&
            std::max(std::abs(mouse_x - app.strip_drag.press_x),
                     std::abs(mouse_y - app.strip_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.strip_drag.moved = true;
        apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/false);
        return;
    }
    // Alt+drag grab-pan (continuous 1:1). The viewport snaps to whole pixels in
    // clamp_viewport_start (reached through scroll_viewport), so a per-event pan
    // re-anchored by that snap tracks the cursor 1:1 without drift — no carried
    // sample remainder. scroll_viewport renders the plate synchronously, so
    // per-event work is one full-width render — the cost zoom already paid per
    // pointer frame, and the reason a panning plate looks identical to a resting
    // one (architect 2026-07-26). A
    // lost button ends it like release (re-anchor the predictor once). The
    // wheel keeps its quantized detent step; only the drag is continuous.
    if (app.scroll_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (playback.is_playing()) playback.resync_predictor();
            app.scroll_drag = ScrollDragState{};
            end_strip_pointer_capture();     // reappear the cursor (idempotent)
            return;
        }
        const double spp = current_samples_per_pixel(app, audio);
        const int    dx  = mouse_x - app.scroll_drag.last_x;
        app.scroll_drag.last_x = mouse_x;
        const int64_t delta =
            static_cast<int64_t>(std::nearbyint(static_cast<double>(dx) * spp));
        if (delta != 0) {
            // Grab-pan: drag right (dx>0) reveals earlier content, viewport
            // moves left. The mid-gesture guarantee this gesture needed — never
            // painting over a plate from an older basis, a staleness mechanism
            // the async deferral was once convicted of — now comes free: every
            // scroll renders synchronously and drains a busy worker, so it no
            // longer has to ask for a distinct pan mode.
            viewport.scroll_viewport(-delta, /*continuous=*/true);
        }
        return;
    }
    // (No scrub motion branch: the scrub is a ONE-SHOT act at the press — the
    // held-drag per-column re-scrub is REMOVED (architect
    // 2026-07-23, the Ableton model), so motion over the scrub surfaces is
    // inert and the per-column stop-fence cadence is structurally gone.)
    // Trim-boundary drag motion. Handled before the marker-drag branch;
    // active in BOTH views (begin_trim_drag has no view gate, and
    // update_trim_drag / commit_trim_drag carry the target-view cached-map
    // machinery). A lost button commits at the current position, mirroring the
    // marker-drag motion handler.
    if (app.trim_drag.active) {
        if (!mods.primary_button_held) {
            commit_trim_drag();
            return;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Pending trim drag (armed by a plain trim-bar press): the trim reposition
    // begins only once the pointer travels past the shared Chebyshev threshold.
    // A lost button before the crossing ends it as a motionless click (nothing
    // committed). Placed after the trim_drag branch above: on the crossing this
    // begins the drag AND applies its first update inline, so it does not fall
    // back into that branch this event.
    if (app.pending_trim_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // The motionless endcap/bridge press commits NOTHING (architect
            // 2026-07-30, the clean-release twin above): its highlight publish is
            // retired, so the same physical click rests identically whichever
            // path ended it — as a consumed nothing. THE CTRL BRIDGE ARM'S
            // DEFERRED BOUND-SET DIES HERE TOO (2026-08-11): a lost button is
            // not a clean click, so set_begin_bound_on_release fires only on
            // the real release — the seed's own rule, one line down.
            app.pending_trim_drag = PendingTrimDrag{};
            // THE TRIM-BAR SEED DIES WITH IT. A button lost mid-press is not a
            // clean click sequence, so it may not leave a seed behind for an
            // unrelated later release to consume into a TrimBar double-click
            // candidate — the same rule the force-end finalizer follows, and the
            // lifetime the seed's own declaration states (app_state.h). Only the
            // CLEAN release is allowed to seed.
            app.trim_bar_press = TrimBarPressSeed{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_trim_drag.press_x),
                     std::abs(mouse_y - app.pending_trim_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // THE DEFERRED-CLICK PENDING DIES AT THE CROSSING (2026-08-11, the
        // fourth glass session): the ctrl in-span press is the BEGIN set's
        // deferral vehicle and NOTHING else — the bridge drag it used to
        // become moved to ALT+DRAG — so a moved ctrl press is not a click and
        // sets nothing, exactly as a lost button doesn't (the flag's contract
        // at PendingTrimDrag::set_begin_bound_on_release). Disarm and done; no
        // seed was recorded by that arm, so there is nothing else to clear.
        if (app.pending_trim_drag.set_begin_bound_on_release) {
            app.pending_trim_drag = PendingTrimDrag{};
            return;
        }
        // Threshold crossed: begin the trim drag anchored at the PRESS column so
        // the bound(s) track from the grab, this first update folding the whole
        // press->crossing delta (the marker-pending / strip catch-up pattern).
        // begin_trim_drag captures the anchor at press_x now — exact, since
        // nothing mutated the trim store between press and crossing — and sets
        // app.trim_drag.active.
        // THREE FIELDS CROSS, and that is the whole transfer (2026-07-29): the
        // pending's set_click / pre-press pair / selection+region snapshots were an
        // Esc-restore origin, and pointer gestures have no cancel (the rule at the
        // drag-modal gate in input_handler.cpp), so the cross-struct data path they
        // needed is gone. A bound-set-armed drag is now indistinguishable from a
        // plain one here — correctly: its click-set already committed, and
        // begin_trim_drag's orig_* capture (the click-set values) is exactly the
        // basis the drag mechanics want, both for the rigid pair delta and for
        // commit's moved-bound test.
        const bool is_begin = app.pending_trim_drag.is_begin;
        const bool both     = app.pending_trim_drag.both;
        const int  press_x  = app.pending_trim_drag.press_x;
        app.pending_trim_drag = PendingTrimDrag{};
        begin_trim_drag(is_begin ? TrimHit::Begin : TrimHit::End, press_x, both);
        if (!app.trim_drag.active) {
            // A REFUSED BEGIN IS A GESTURE END TOO (no pair / no audio): the
            // pending was cleared a line above and nothing took its place, so
            // the trim cue it was holding goes with it at the loop's tail, the
            // same route every other end takes.
            return;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Motion just continues whatever the press already armed — the
    // home-view gate (active_column_authoring_allowed, the whole gate now that the
    // tempo drag and its eligibility check are gone) ran once at arm time in
    // on_button_press, so nothing here re-checks view or column; the
    // region drag below is navigation, not authoring, and was never
    // gated. Per-site translation (drag anchor capture, motion delta
    // conversion, hit tests) lives in the handlers below.
    if (app.region_drag.active) {
        // Left button must still be held; if not, the release was lost —
        // end the gesture, resting the region at its current extent (as a
        // clean release would). Modifier changes mid-drag are ignored. A
        // sub-threshold sliver rest dissolves like a click, exactly as the
        // clean release branch does (end_region_drag_min_size_check) — and
        // identically gated on `moved`, a motionless arm having left the region
        // cleared at mouse-down. Capture `moved` before the reset zeroes it.
        if (!mods.primary_button_held) {
            const bool moved = app.region_drag.moved;
            app.region_drag = RegionDragState{};
            // THE TRIM-BAR SEED DIES WITH A LOST BUTTON (the band arm records
            // one at its press since the trim surface arc): a button lost
            // mid-press is not a clean click sequence, so no seed may survive
            // for an unrelated later release to consume — the pending trim
            // drag's own rule, one branch up. A waveform arm never recorded
            // one, so the clear is free there.
            app.trim_bar_press = TrimBarPressSeed{};
            if (moved)
                end_region_drag_min_size_check(app, audio, viewport);
            return;
        }
        const GuiRect area = waveform_area(app);
        if (area.w <= 0) return;
        // Sub-threshold: the press has not yet become a drag. Below the shared
        // Chebyshev gate nothing extra happens — the press already did the
        // click and cleared any resting region at mouse-down (the band arm did
        // neither, and does neither here — its acts land at the crossing
        // below). Once a drag,
        // always a drag (moved never re-engages).
        if (!app.region_drag.moved) {
            if (std::max(std::abs(mouse_x - app.region_drag.press_x),
                         std::abs(mouse_y - app.region_drag.press_y)) <
                    kDragMovedThresholdPx) {
                return;
            }
            // THE BAND ARM'S CROSSING ACT (the trim surface arc, 2026-08-11):
            // the merged trim surface's press deferred its dissolve and its
            // deselect so a motionless band click stays a consumed nothing —
            // this crossing is the moment the gesture stops being a click, so
            // the resting span dissolves and the store selection drops HERE,
            // the latest moment before the fresh span exists. That keeps the
            // family invariant (a region rests only beside an empty selection)
            // exactly: no event between this line and the install below can
            // observe a span beside a selection. The placement-press arms did
            // both at mouse-down and take neither branch.
            if (app.region_drag.band) {
                clear_region_highlight(app, viewport);
                selection.clear_selection();
            }
        }
        app.region_drag.moved = true;
        // A moved region drag drops any double-click candidate: this press became
        // a drag, not the first click of a double-click. Only the empty
        // flag/triangle-lane press seeds a candidate before arming this drag (a
        // waveform press seeds none), so this keeps a lane drag from later
        // consuming as an EmptyLane marker-create double-click — the standing
        // moved-drag clear route.
        app.double_click = DoubleClickCandidate{};
        // Far endpoint at the pointer column, through the same click->frame
        // basis as the anchor, clamped to the visible strip like the other
        // drags' live tracking. Endpoints are active-domain frames, so the
        // span survives pan/zoom mid-drag and at rest. Also clamped into the
        // live domain: at a fractional flush-right zoom the painter-quantized
        // wall differs from the click conversion, so the last visible column's
        // frame can land at domain_total — one past [0, domain_total-1] — which
        // the display-state validator would clear wholesale (same rule as the
        // press-site anchor).
        int rel = mouse_x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        const int64_t far_frame = clamp_playhead_to_live_domain(
            playhead_frame_at_click_column(app, audio, rel), app, audio);
        // Column-change gate: the span changes only when the far endpoint moves
        // to a new frame. A same-frame motion event (sub-pixel jitter within one
        // column) is a no-op — skip the repaint. The anchor is fixed for the
        // gesture, so the far endpoint alone decides the span. THE THRESHOLD-
        // CROSSING EVENT NEEDS NO BYPASS OF THIS TEST (2026-08-05, when the
        // non-dissolving arm died with the click-anchored former): every arm is
        // arm_region_drag_at now, which clears the region AT MOUSE-DOWN — or,
        // for the BAND arm, at the crossing act just above (2026-08-11) — so at
        // the crossing `app.region.active` is false either way and the install
        // proceeds whatever column the pointer is over.
        if (app.region.active && far_frame == app.region.b_frame)
            return;
        app.region.active     = true;
        app.region.a_frame    = app.region_drag.anchor_frame;
        app.region.b_frame    = far_frame;
        // SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): highlighting a
        // region does NOT select the markers it contains (the reverse coupling —
        // a region selecting its contents — was tried and retired; do not
        // re-propose) — the press (or, for the band arm, the crossing act
        // above) already deselected all and the drag
        // leaves the selection EMPTY throughout. That is the whole story for the
        // LIVE formers — the plain upper-half press, the shift press at
        // either height and the empty flag/triangle-lane parity press deselect
        // at press through the one placement body, and the merged trim
        // surface's band arm (2026-08-11) at its crossing — so a span
        // drawn in an
        // ordinary view rests beside an EMPTY selection. The `h` view's own
        // press rides this same motion path and deselects nothing, which costs
        // that invariant nothing since 2026-08-05: the view's regions are
        // VIEW-LOCAL, cleared at its exit and at every step and compare switch,
        // so a span formed in there can never rest in the editor (the inventory
        // is at RegionState, app_state.h).
        //
        // THE DRAG CARRIES THE PLAYHEAD (architect 2026-07-30, live-test
        // refinement: "i'd prefer the playhead move along with the drag for
        // region highlight - more intuitive"). The cursor rides the MOVING
        // endpoint — far_frame, already clamped playable by the conversion above,
        // so the write needs no clamp of its own — while the anchor stays put as
        // the span's other bound. EVERY ARM rides this one motion path, and
        // since the former's anchor moved to the clicked column (2026-08-05)
        // they no longer differ at the press either: each seats the playhead at
        // its click and the pointer carries the cursor from here on, in the view
        // as outside it.
        // DIRECT CURSOR WRITE, not move_playhead_to: a keep-visible edge-align
        // would scroll the viewport out from under a live gesture, and the span's
        // endpoints are painted against the viewport the drag started in.
        // PLAYBACK IS UNTOUCHED per motion: the plain press's at-press
        // reseek-keeping-alive stands as the whole playback story of this
        // gesture, and a per-column reseek would re-cue the audio on every pixel.
        // The column-change short-circuit above keeps this to ONE write per
        // CHANGED column. The waveform invalidate below repaints the cursor's
        // HEAD AND STEM with the ground — its rect runs from the window top
        // down through the waveform, so the marker-lane head is inside it (the
        // triangle this used to name retired with its lane in row 5); the
        // TIMESTAMP invalidate is owed
        // separately because the bottom-strip readout shows this cursor whenever
        // no scanner is active, and it lives outside the waveform area.
        // A SLIVER RELEASE LEAVES THE PLAYHEAD WHERE THE DRAG PUT IT: the
        // release-time min-size check dissolves the span but writes no playhead,
        // which is the same "what stands stands" rule every pointer gesture has
        // (there is no cancel) — accepted.
        app.playhead_cursor_sample = far_frame;
        viewport.invalidate_waveform_area();
        viewport.invalidate_clock_area();
        return;
    }
    // (No tempo-drag motion arms: the target-view tempo drag and its pending are
    // DELETED, architect 2026-07-29 — see marker_drag.h. W+target has no pointer
    // authoring gesture at all now.)
    // Pending marker drag (armed by a plain flag press): usually the marker was
    // single-selected at press; the reposition begins only once the pointer
    // travels past the shared Chebyshev threshold (kDragMovedThresholdPx, now
    // one generic 8px gate for every press-becomes-drag surface). Handled before the hover
    // fallthrough below and after the other drag branches (a pending drag and
    // any other pointer gesture are mutually exclusive — the arming press does
    // no other work). A lost button before the crossing ends it as a plain
    // click.
    if (app.pending_marker_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // A lost button before the crossing IS the click, matching the release
            // path: the press already committed the whole click (single-select,
            // land, span collapse), so this just disarms (the deferred completion
            // died with the group drag — see the release branch).
            app.pending_marker_drag = PendingMarkerDrag{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_marker_drag.press_x),
                     std::abs(mouse_y - app.pending_marker_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the drag anchored at the PRESS column so the
        // marker tracks the pointer 1:1, this first apply folding the whole
        // press->crossing delta (the strip/region catch-up pattern). begin_drag
        // captures the pre-drag snapshot / selection / walls now — exact, since
        // nothing mutated the store between press and crossing — and sets
        // app.drag.active. Fall through (no return) so this same motion event
        // applies the first delta through the marker-drag branch below.
        const int marker  = app.pending_marker_drag.marker;
        const int press_x = app.pending_marker_drag.press_x;
        app.pending_marker_drag = PendingMarkerDrag{};
        // A moved marker drag drops any double-click candidate: this press is a
        // drag, not a click of a marker double-click. The arming press seeded a
        // Marker candidate (press-time seeding), so this clear is what keeps a
        // moved drag from carrying one — the load-bearing half of seeding at
        // the press.
        app.double_click = DoubleClickCandidate{};
        if (!marker_drag.begin_drag(marker, press_x)) {
                // Begin refused (bad index / no audio): the gesture is DROPPED,
                // its pending already cleared above, so this is a gesture end
                // like any other and takes the loop tail's re-resolve like one.
                return;
        }
        // No follow override needed: the marker drag always begins from a
        // top-strip flag press, which already stopped playback (the marker
        // click owns that stop), so there is no live playhead to chase.
    }
    if (!app.drag.active) {
        // The redesigned rows' own hover, resolved in the same no-gesture tail
        // and through its own state (a button is not a marker, so it has no
        // place in the hover-popup machinery below). An ACTIVE GESTURE FREEZES
        // IT — the branches above all returned — which is consistent and
        // harmless: a gesture that ends over a button re-resolves on the next
        // motion, and a press cannot reach one mid-gesture anyway.
        // The redesigned rows' hover is the ONLY hover left: the marker hover
        // popup and its whole recompute machinery died with the marker-text lane
        // (row 5), so the no-gesture tail has nothing else to resolve.
        //
        // THE MENU ROW'S MODE, OPEN HALF, resolves here beside the faces and for
        // the same reason: opening a menu at a pointer is a thing only a
        // gesture-free, modal-free pointer may do, and every branch that must
        // forbid it has returned above. With the row ARMED, this opens an
        // anchor's menu on the pointer alone. Its EXIT half is deliberately not
        // here — it runs at the top of this function, where every branch is still
        // ahead of it. This call runs BEFORE the recompute so an open it
        // performs is already standing when the faces resolve — toggle_dropdown
        // clears them, and the recompute then re-derives the whole roster false
        // under the new popup, which is the correct answer for a pointer the
        // popup has taken.
        // ONE CONDITION IS RESTATED FOR THE OPEN (codex round 2): a HELD PRIMARY
        // BUTTON refuses the hover-open. This is the FIRST MEMBER of the row-1
        // hover-act family rule — no row-1 hover act fires while the primary
        // button is held; the three-member statement lives at the open-dropdown
        // branch's close walk, beside its siblings (the anchor-switch and the
        // hover close, codex round 3). A held button is not a resting hover,
        // and it does NOT return above — a held motion with no armed gesture
        // reaches this tail, on two producers: the TOUCH resolution burst's
        // pre-press entry motion (the platform raises the touch hold before
        // delivering it, so this guard is what stops that motion hover-opening
        // an armed anchor's menu one event before the press toggle-closes it —
        // the tap then opens the menu through the press path, as intended), and
        // a MOUSE press that armed no gesture sliding along row 1 (press-hold an
        // anchor — its menu opens armed — slide onto a non-anchor row-1 button,
        // whose close rule re-arms with the menu down, then back onto an anchor:
        // springing a menu open under a held button would hand the coming
        // release to an item that was never pressed). The mode itself is
        // untouched — the row stays armed; only the OPEN waits for a free
        // button, and the next resting motion performs it.
        if (!mods.primary_button_held)
            open_menu_row_anchor_on_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // Left button must still be held down — otherwise release was lost.
    if (!mods.primary_button_held) {
        marker_drag.commit_drag();
        return;
    }
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    // The delta handed to apply_drag_motion is an ACTIVE-domain frame delta:
    // mouse_frame is the pointer's plain active-domain position — one
    // expression, both views, no inverse map anywhere in its derivation.
    // The displayed-map hops that carry the delta into the source domain
    // live inside apply_drag_motion, which anchors the proposal in the
    // DISPLAYED target domain so the painted flag tracks the pointer 1:1.
    const double mouse_frame = static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(mouse_x - area.x) * spp;
    marker_drag.apply_drag_motion(mouse_frame - app.drag.anchor_mouse_time_frame);
    // Playhead rule: the playhead follows the dragged marker through the drag
    // inside apply_drag_motion (the arming click landed it on the marker, so
    // the drag tows it by construction — the DragState ruling). The selection
    // re-assert already ran at the THRESHOLD CROSSING in begin_drag — a no-op,
    // the arming press having single-selected the marker, and unconditional so a
    // wall-saturated drag still names what it grabbed. apply_drag_motion here only
    // writes the proposal and slides the playhead. Nothing further tracks here.
}
