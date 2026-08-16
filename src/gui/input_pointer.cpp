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
// publication — the flag editor's FlagEditorBox, the four dialog
// editors' DialogEditorText — and click-to-byte is the same nearest-boundary
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
// The `shift` column is each button's OWN chord — THREE rows set it: Redo's
// Ctrl+Shift+Z and, since 2026-08-15, the marker walk's Shift+Tab (previous
// marker) and Ctrl+Shift+Tab (walk both tabs). It is not
// the whole shift story: the SHIFT-ADMITTING buttons OR a shift-exact press
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
    // follow, iteration, read-only, history and Cumulative buttons are TOGGLES
    // and press through in both directions, which is why this is a flag and
    // not `selected` alone. (THE BOTTOM ROW'S PLAY / STOP PAIR was a fourth
    // radio for hours on 2026-08-15 and is not one now — the architect
    // COLLAPSED that pair into one stateful button later the same day, which
    // is the only reason it ever needed the flag: two buttons over one chord.
    // The consume itself is untouched and stayed GENERIC throughout — keyed on
    // the flag plus the lamp, with no id list anywhere.)
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
    // CLICK_FACE: row 4, the bottom row and the view bar show a pressed
    // interior; row 1's
    // two left-floating buttons and row 3 have two faces by scope and show
    // nothing new on a press. (Since the act moved to the release the face
    // tracks the pointer through the arm's `inside` bit — the paint reads
    // redesign_button_pressed_face, app_state.h.)
    bool           click_face;
};

// THE PRESS CLAIM'S HALF OF THE BUTTON ROSTER — every CHORD-DISPATCHING button
// in the redesign — rows 1, 3 and 4 and the bottom row since the 2026-08-12
// relayout deleted row 2 — in one table. The flags above
// are the
// only axes the rows differ on, so they share one press body and one release
// body (arm_redesign_press / finish_chrome_press_release) instead of
// accumulating a special case per row.
//
// ROW 1'S THREE MENU BUTTONS ARE THE ABSENTEES, and the membership changed
// hands three times: Quit joined the table when Ctrl+Q was recognised as its
// chord, Settings left it when its action became a DROPDOWN TOGGLE (a popup
// open/close is not a chord at all — the bare `;` keyboard route still opens the
// editor directly, untouched), Navigation arrived a menu button 2026-08-02, and
// on 2026-08-13 QUIT LEFT THE TABLE WITH ITS BUTTON: row 1 paints no held face,
// so a button acting at the lift gave no feedback while it was down, and the
// architect moved the act into a THIRD MENU — File, one item, "Quit", dispatched
// as this chord through on_key like every other dropdown command (the roster
// record is at RedesignButton::File, app_state.h). The CHORD is untouched
// everywhere: the keyboard, the editors' modal admission, the close routing and
// the prompt all read exactly as before. Everything else on rows 1, 3 and 4 and
// the bottom row is here.
constexpr ToolbarChord kToolbarChords[] = {
    // Row 1's RIGHT FLOAT — the view bar (2026-08-02). Bare 1/2/3, the ABSOLUTE
    // view selectors: S+W, T+P, T+W. Everything the digits own arrives by
    // construction through on_key's own handler — the audio-first-then-markers
    // order, the refused-target-entry abort of the whole press, the coincidence
    // auto-select, the read-only admission (they are navigation), the modal
    // swallow. There is no second route to keep in step.
    {RedesignButton::ViewSW,     GuiKeys::Digit1, false, false, false, true, true}, // bare 1
    {RedesignButton::ViewTP,     GuiKeys::Digit2, false, false, false, true, true}, // bare 2
    {RedesignButton::ViewTW,     GuiKeys::Digit3, false, false, false, true, true}, // bare 3
    // The toolbar four — icon-row members since the 2026-08-12 relayout
    // dissolved row 2 (the chords, gates and flags are UNCHANGED by the move;
    // only the face and the band changed hands).
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
    // the button has: the degenerate-result refusal and the NO-REGION arm (a
    // SEED at the current trim window since 2026-08-15, not a no-op) are the
    // key's own, read-only-legal (trim is band), consumed in the `h` view (the
    // derived partition greys it there). SHIFT IS ADMITTED since 2026-08-15 —
    // the twin is Shift+X the maximizer, and the reason the keyboard-only
    // clause was dropped is at redesign_button_shift_admits (app_state.h).
    {RedesignButton::IconTrim,   GuiKeys::X,   false, false, false, false, true},   // bare x
    // THE ZOOM GROUP (2026-08-12, the grand relayout — SUPERSEDING the
    // 2026-08-02 no-duplicate-commands deletion of the old zoom pair for
    // these four: the Navigation dropdown keeps its rows, the buttons being
    // the glass rig's pointer home): four momentary navigation chords, no
    // radio, no shift admission, click face like the rest of the row. All
    // four stay LIVE in the `h` view — `=`, `-` and `0` are on the mode's
    // allowlist and `c` is its own vocabulary — which the derived partition
    // answers with nothing hand-listed.
    {RedesignButton::IconZoomIn,       GuiKeys::Equal,  false, false, false, false, true}, // bare =
    {RedesignButton::IconZoomOut,      GuiKeys::Minus,  false, false, false, false, true}, // bare -
    {RedesignButton::IconZoomFitBest,  GuiKeys::Digit0, false, false, false, false, true}, // bare 0
    {RedesignButton::IconZoomOriginal, GuiKeys::C,      false, false, false, false, true}, // bare c
    // THE SINGLE-MARKER VERBS (2026-08-12): drop, delete, disable toggle,
    // inherit/collapse — authoring chords whose refusals (read-only, home
    // view, empty selection, occupied frame) are the keys' own consumed
    // no-ops, inherited whole through on_key. The `h` view consumes all four
    // outright, and since 2026-08-13 that shows as the DEAD FACE rather than a
    // collapse — as it now does for everything in this row the view refuses,
    // the collapse rule being deleted whole (architect 2026-08-14).
    {RedesignButton::IconMarkerDrop,    GuiKeys::S,      false, false, false, false, true}, // bare s
    {RedesignButton::IconMarkerDelete,  GuiKeys::Delete, false, false, false, false, true}, // Delete
    {RedesignButton::IconMarkerDisable, GuiKeys::D,      true,  false, false, false, true}, // Ctrl+D
    {RedesignButton::IconMarkerInherit, GuiKeys::N,      true,  false, false, false, true}, // Ctrl+N
    // THE MASS-MARKER CATEGORY — five chords the `h` view consumes outright,
    // all five greyed in there.
    {RedesignButton::IconCopy,   GuiKeys::P,   true,  false, false, false, true},   // Ctrl+P
    {RedesignButton::IconPaste,  GuiKeys::P,   true,  false, true,  false, true},   // Ctrl+Alt+P (+Shift)
    // BPM'S KEY IS BARE `m`, NOT `b` — the brief expected `b` and the code says
    // otherwise (the arm is at handle_mode_keys, input_key_dispatch.cpp). The
    // button is its chord, so it takes the chord the keyboard actually has.
    {RedesignButton::IconBpm,    GuiKeys::M,   false, false, false, false, true},   // bare m
    {RedesignButton::IconIter,   GuiKeys::I,   false, false, false, false, true},   // bare i
    {RedesignButton::IconFollow, GuiKeys::F,   false, false, false, false, true},   // bare f
    // THE ROW'S LAST GROUP (architect 2026-08-14): listen, load in place, the
    // read-only toggle, the history opener. `'` is one of the `h` view's three
    // admitted mutators, so listen greys beside a live `'` in there.
    {RedesignButton::IconListen, GuiKeys::L,   false, false, false, false, true},   // bare l
    {RedesignButton::IconLoadInPlace,
     GuiKeys::Apostrophe, false, false, false, false, true},  // bare '
    // THE READ-ONLY TOGGLE (2026-08-14, the padlock's move off the tabs): bare
    // `o` toggles the ACTIVE tab's read-only bit. A TOGGLE like follow and
    // iteration — its selected face reads the live bit its own chord flips —
    // and NOT a radio: it presses through in both directions. The button is
    // its chord with no exception now, which is exactly why the padlock moved
    // here (the roster record is at RedesignButton::IconReadOnly).
    {RedesignButton::IconReadOnly, GuiKeys::O, false, false, false, false, true},   // bare o
    // THE HISTORY MODE (2026-08-04): bare `h`, a TOGGLE like follow and
    // iteration — its chord opens the mode and closes it, and the button
    // dispatches on both edges because the icon row's band claim sits ABOVE the
    // mode's pointer gate (the rows' presses are covered by the KEYBOARD gate
    // instead, which admits `h` through handle_history_mode_key one line before
    // the allowlist). It closes the row since 2026-08-14.
    {RedesignButton::IconHistory, GuiKeys::H, false, false, false, false, true},     // bare h
    // The BOTTOM ROW (the transport half architect-ratified 2026-08-11 as the
    // touch arc's first surface; the marker-walk group added 2026-08-15). TEN
    // chords, every one already bound elsewhere: the row adds no semantics
    // anywhere — each button is its key, through this one table like the rest
    // of the roster, so the keyboard-modal editor gate, the history-mode
    // allowlists, the read-only gate and every refusal apply by construction, a
    // refusal being a consumed no-op on click exactly as on key. (An eleventh
    // chord — bare Escape, the row's Cancel button — shipped on the row's first
    // day and was deleted that same day with its button; the mid-render Cancel
    // is the RENDER button's stateful face now, spelled in this body below.)
    //
    // PLAY AND STOP ARE ONE BUTTON OVER THE ONE Space BINDING since 2026-08-15
    // (architect, at his live look at the row), so BUTTON-IS-ITS-CHORD HOLDS
    // EXACTLY here: the press dispatches bare Space, Space toggles, and the
    // button's GLYPH and TOOLTIP swap on the live audition bit. There is no
    // wrong half to press, which is why this row carries neither a `radio` flag
    // nor a lamp.
    //
    // TWO SUPERSEDED SHAPES, both from earlier that same day. (1) THE ENABLED
    // SPLIT of the morning's whole-row honesty ruling — Play dead while an
    // audition ran and where a launch would refuse, Stop dead while none ran —
    // reversed with the rest of the row's honest arms (the ruling and his
    // reasoning are at redesign_button_enabled). (2) THE RADIO PAIR, his own
    // fix for what that reversal exposed: the enabled split had also been the
    // pair's DISAMBIGUATION, only ever leaving the meaningful half clickable,
    // so with the row always-on a press on Stop while stopped would have
    // STARTED playback — the glyph contradicting the act. `radio=true` on both
    // rows killed the wrong-direction press at the claim and again at the lift.
    // THE COLLAPSE REMOVES THE PROBLEM RATHER THAN SOLVING IT AGAIN: two
    // buttons over one chord was the whole difficulty, and one button has no
    // wrong half. The flag is deleted from these rows and the GENERIC radio
    // consume is untouched — the S/T and W/P rows and the tabs still use it.
    // bare Space, toggle_playback and playback_launch_playable are untouched
    // by construction, as they were under the radio.
    //
    // THE FOUR ARROWS DO NOT REPEAT (architect 2026-08-13, deleting the
    // hold-repeat that shipped with the row — the synthesized 575ms/25Hz
    // bursts, their AppState::transport_repeat arm, the tick's firing body
    // and the three-plus-one edge disarm lists all went with it; the physical
    // arrow KEYS keep their platform repeat, repeat_eligible untouched). HIS
    // REASONING, recorded: these four exist for the touch panel, which has no
    // keyboard beside the synthetic one, and each thing the repeat bought is
    // replaceable there — a marker nudge by dragging the marker and
    // fine-tuning with taps on these arrows, a tempo step by typing the value
    // in the editor. Like every other chrome button they are one act per
    // press-and-lift now.
    {RedesignButton::TransportSkipBack,
     GuiKeys::Home,   false, false, false, false, true},                             // bare Home
    {RedesignButton::TransportPlayStop,
     GuiKeys::Space,  false, false, false, false, true},                             // bare Space
    {RedesignButton::TransportSkipForward,
     GuiKeys::End,    false, false, false, false, true},                             // bare End
    // THE MARKER-WALK GROUP (architect 2026-08-15), the row's right cluster
    // behind a separator and ahead of the arrows. THREE BUTTONS, THREE CHORDS
    // — no hold, no double-click, no modifier gesture on the surface — and the
    // declined double-click rule's mechanical reason is recorded at the roster
    // entry (every double-click surface in this product acts on its FIRST
    // click too, so a double-click on "next" would step a marker AND THEN walk
    // both tabs).
    //
    // PREV'S SHIFT IS ITS OWN CHORD, not an admission: Shift+Tab is the
    // reverse marker cycle's own spelling, so the `shift` column carries it
    // exactly as Redo's carries Ctrl+Shift+Z, and this row is the SECOND
    // producer that column has ever had. A SHIFT press on the button is
    // therefore a consumed nothing (redesign_button_shift_admits says no), and
    // that is Redo's behaviour too. The reverse cycle's other spelling,
    // IsoLeftTab, is deliberately NOT a second row: the dispatch is
    // synthesized, so it goes out in the Tab spelling every reader accepts.
    //
    // THEY ARE LIVE INSIDE THE `h` VIEW and the derived partition says so with
    // nothing hand-listed — history_mode_owns_key claims all three shapes (the
    // diff-flag cycle forward and back, and the reverse walk-source cycle), so
    // this walk answers LIVE and each button does the mode's own thing.
    {RedesignButton::TransportWalkPrev,
     GuiKeys::Tab,    false, true,  false, false, true},                             // Shift+Tab
    {RedesignButton::TransportWalkNext,
     GuiKeys::Tab,    false, false, false, false, true},                             // bare Tab
    {RedesignButton::TransportWalkBoth,
     GuiKeys::Tab,    true,  true,  false, false, true},                             // Ctrl+Shift+Tab
    // The arrows, in their painted order since 2026-08-14 (the architect's:
    // down, up, left, right, replacing the row's original vim order). The
    // lookup is by id, so this order is for the reader alone.
    {RedesignButton::TransportDown,
     GuiKeys::Down,   false, false, false, false, true},                             // bare Down
    {RedesignButton::TransportUp,
     GuiKeys::Up,     false, false, false, false, true},                             // bare Up
    {RedesignButton::TransportLeft,
     GuiKeys::Left,   false, false, false, false, true},                             // bare Left
    {RedesignButton::TransportRight,
     GuiKeys::Right,  false, false, false, false, true},                             // bare Right
    // THE HISTORY COMPANIONS — the bottom row's right cluster while the `h`
    // view stands, in the arrows' own slots (architect 2026-08-14; they were
    // the icon row's history group until that day, and nothing about their
    // chords, gates or faces moved with them).
    //
    // THE CUMULATIVE READING'S TOGGLE (2026-08-08): bare `u` flips the history
    // view's delta between ITERATIVE (off) and CUMULATIVE (on). A TOGGLE like
    // follow, iteration and the history button — the selected face reads the
    // live bit its own chord flips — and like the three entries below it, its
    // key is bound ONLY inside the view. WHAT KEEPS THE FOUR FROM DISPATCHING
    // OUTSIDE ONE IS THE ZERO RECT, not their enabled bit: since 2026-08-15
    // they answer a plain `true` everywhere (the ruling is at
    // redesign_button_enabled), and outside the view the bottom row paints the
    // four ARROWS in these slots and publishes an empty rect for these, which
    // no point is inside. The resting-disabled bit had been the stated
    // safeguard from 2026-08-05; it was already redundant when the buttons
    // moved to this row on 2026-08-14 and stopped being painted outside the
    // view at all.
    {RedesignButton::HistoryCumulative,
     GuiKeys::U,      false, false, false, false, true},                             // bare u
    // THE REVERT ACT (2026-08-05): CTRL+H applies the view's SELECTED diff flags
    // backwards into the live state and closes the view. Momentary like the two
    // below — not a radio, not a toggle, click face only. It is the one entry
    // here whose chord is NOT claimed by the mode's own vocabulary: it
    // dispatches from on_key's ordinary body, BELOW the read-only gate, so a
    // locked tab refuses the click exactly as it refuses the key (the
    // load-in-place's precedent, `'`). BOTH OF ITS REFUSALS ARE FACELESS since
    // 2026-08-15 — the lock's and the mode's empty-subject one — because it is
    // a member of the bottom row's untruthful right cluster; the click is a
    // consumed no-op in either case, which is the roster's standing shape for a
    // refusal.
    {RedesignButton::HistoryRevert,
     GuiKeys::H,      true,  false, false, false, true},                             // Ctrl+H
    // THE WALK'S TWO STEPS (2026-08-05): bare `,` steps OLDER and bare `.`
    // NEWER, through the same dispatch and therefore through
    // handle_history_mode_key's own arm — walls clamped as consumed no-ops
    // there, exactly as the keys behave. Neither is a radio and neither is a
    // toggle: they are momentary steps, so both flags read like copy's and
    // paste's, and only the CLICK face is set. Outside the view they never
    // dispatch at all — through the empty published rect, per the Cumulative
    // entry's note above; and even reached, bare `,` and `.` are bound in
    // handle_history_mode_key alone, so there is nothing for them to fire.
    {RedesignButton::HistoryOlder,
     GuiKeys::Comma,  false, false, false, false, true},                             // bare ,
    {RedesignButton::HistoryNewer,
     GuiKeys::Period, false, false, false, false, true},                             // bare .
};

// THE TABLE IS TOTAL OVER THE ROSTER, ENFORCED AT COMPILE TIME (2026-08-06):
// every RedesignButton but the three menu anchors carries a chord here — 45
// rows against the roster's 48 since 2026-08-15 (the marker walk's three in,
// the collapsed play/stop pair's second row out) — so the
// table's length plus those three IS the roster. The check is not bookkeeping —
// history_mode_disables_button walks this table and DEFAULTS AN UNLISTED BUTTON
// TO LIVE, so a roster entry added without its row here would silently wear a
// live face in the `h` view while its press claimed nothing. This makes that
// drift a build error instead. (It was + 2 until 2026-08-13, when the Quit
// button became the File menu's one item: the roster's total did not move, the
// split did.)
static_assert(std::size(kToolbarChords) + 3 ==
                  static_cast<std::size_t>(kRedesignButtonCount),
              "kToolbarChords must cover every RedesignButton except the "
              "File, Settings and Navigation anchors");

// (THE MODAL-TRAP REACH-THROUGH IS RETIRED — architect 2026-08-13, "we can
// drop the Save reach through". From 2026-08-11 a plain left press on a roster
// button whose chord the editors' modal contract ADMITS AS A COMMAND was
// lifted over the veil and dispatched, membership derived from the admission
// through the chord table by `modal_editor_admits_command_chord`, with
// `modal_veil_admits_button` giving the hover walk the same answer. ITS REASON
// IS GONE: it existed because an accidentally opened settings editor on GLASS,
// with no physical keyboard, was an exit-less state — the Quit button did
// nothing — and the modal itself now answers that, every one of the four
// editor dialogs publishing real OK and CANCEL buttons the veil admits, with
// Cancel dispatching the session's own Esc. Quit's button had already left the
// roster with the File menu, so the membership had derived down to SAVE alone;
// the whole mechanism goes rather than surviving for one convenience chord.
// SO THE VEIL IS EXCEPTIONLESS AGAIN: while a dialog editor stands, EVERY
// press outside the modal is consumed, and the roster hovers nothing. THE
// KEYBOARD CONTRACT IS UNTOUCHED — route_modal_editor_key still admits bare
// Esc, Ctrl+S and Ctrl+Q while an editor stands, which is where that pair's
// pointer-side mirror note used to point.)

// Is (x, y) inside the PAINTED rect of a redesigned button? The rect is the
// painter's stash and nothing here re-shapes or re-measures, so the clickable
// region is exactly the drawn one. A zero rect (before that row's first paint)
// contains no point, which is the correct cold answer.
bool redesign_button_hit(const AppState& app, RedesignButton id, int x, int y) {
    return rect_contains(
        app.redesign_buttons[redesign_button_index(id)].rect, x, y);
}

// Does the roster button at this index paint a pressed interior? The chord
// table's click_face column by roster index — the damage gate for the arm's
// writers (a face that is never painted owes no erase). False for the two
// anchors, which carry no table row.
bool roster_index_click_face(int index) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_index(tc.id) == index) return tc.click_face;
    }
    return false;
}

// THE WAVEFORM'S HALF SPLIT, and the ONE expression of it — now with exactly
// ONE reader (architect 2026-08-13, THE TWO HALVES BECOME ONE SURFACE): the
// half no longer selects a SURFACE, it selects which act a MOTIONLESS release
// runs on the one navigation surface — upper = the playhead placement, lower =
// the audition scrub. Everything else about the two halves is now identical
// (plain drag = the grab-pan, shift = the region former, ctrl = the strip
// drag), so the half is read once, at the press, and stashed on the pending
// (ScrollDragState::scrub_release). The CURSOR no longer reads it at all: the
// Scrub kind is deleted and Pan covers the whole waveform, which is what the
// architect asked for — "get rid of the crosshairs but retain the scrub
// action".
//
// It is the same arithmetic the retired 1px channel-split line was drawn on
// (that line went 2026-08-03; the split it marked did not) — integer division,
// so an odd-height area gives the lower half the extra row, which is what the
// press has always done.
bool waveform_lower_half(const GuiRect& area, int y) {
    return y >= area.y + area.h / 2;
}

// THE TOP STRIP'S TWO NAVIGATION LANES — the RULER and the MARKER lane, the
// "extension of the upper half" (architect 2026-08-12, the eighth glass
// ruling: the waveform-height clamp put both lanes in easy reach, so they take
// the upper half's whole vocabulary — the plain pending click / grab-pan, the
// shift region former, the ctrl zoom drag). One band owner because the press
// router and the cursor map both ask it: a point is in the lanes iff it is in
// the top strip AND in either lane's y-band. The FLAG BOXES carve themselves
// out at each consumer (a flag hit is claimed first, lane vocabulary), and the
// TRIM BAR and the OVERVIEW STRIP are disjoint y-bands that never answer true
// here (the strip has its own claim and its own cue). Deliberately NOT the
// flexible GAP 1 band between the menu row and the centered block — that ground
// is chrome, not surface, and stays inert (the gap sat between the icon row and
// the trim lane for the seventh ruling's first hours, and at the window's foot
// until the relayout's commit B split it in two).
bool point_in_nav_lanes(const AppState& app, int x, int y) {
    if (!rect_contains(top_strip_area(app), x, y)) return false;
    const GuiRect ruler = top_ruler_row_area(app);
    if (y >= ruler.y && y < ruler.y + ruler.h) return true;
    const GuiRect lane = top_marker_row_area(app);
    return y >= lane.y && y < lane.y + lane.h;
}

// THE NAVIGATION SURFACE, THE ONE OWNER OF ITS GEOMETRY (architect 2026-08-13,
// THE WAVEFORM'S TWO HALVES BECOME ONE): the WHOLE waveform — both halves, in
// every view — plus the RULER and the MARKER lane's empty stretches. The lower
// half joined with the ruling that took the press-time scrub off the mouse-down
// ("we do everything on lift the finger or on mouse up, but the playhead scrub,
// we do right on mouse down... that should allow the dragging on the lower half
// of the waveform as well, the pan"), so the surface is MODE-INDEPENDENT now —
// the `h` history view's own surface, which was already the full height, and
// the live views' are the same rect, and this owner serves both.
//
// The waveform BAND spans the FULL WINDOW WIDTH (top.w), not the effective
// width: the <=15 px inert right gutter counts as waveform by the user's
// lights, so a press there arms the pan and its click act deselects while
// seating nothing (the gutter is 0 px at 1920/2560/3840, so it only matters
// off-deployment). The FLAG BOXES carve out through the painter's published
// rects — a flag is lane vocabulary (select / range / toggle / drag), never the
// surface. THE TRIM BAR and THE OVERVIEW STRIP are disjoint y-bands that never
// answer true here, and neither does the flexible GAP band: that ground is
// chrome, not surface.
//
// FIVE READERS, re-derived by grep 2026-08-13 and every one of them a
// derivation that used to be spelled by hand: the press router's SHIFT
// region-former claim, its CTRL strip-drag claim, the pointer cursor map's
// Pan/Zoom zone, the `h` view's own press router, and the TOUCH PAN ZONE
// (touch_point_in_pan_zone, which is the one-finger pan surface by ruling and
// so must not drift from the mouse's — it was a hand COPY until this owner
// existed, and so did not follow the lower half onto the surface for free).
// The plain press's own arm
// is the band walk in on_button_press rather than this predicate, because it
// also has to pick the lane double-click and the release act.
bool point_on_nav_surface(const AppState& app, const GuiAudio& audio,
                          int x, int y) {
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    if (x >= area.x && x < top.x + top.w &&
        y >= area.y && y < area.y + area.h)
        return true;
    if (!point_in_nav_lanes(app, x, y)) return false;
    return hit_test_flag(app, audio, x, y) < 0;
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
// release all resolve this so they agree on origin and which surface to
// repaint.
struct ActiveEditorText {
    bool                valid        = false;
    text_editor::State* ed           = nullptr;  // the active editor
    double              text_left    = 0.0;       // byte-0 origin (px)
    // The painter's per-byte pen offsets for that editor's own shaped run.
    // Never null on a valid resolution — every editor is shaped since row 7.
    const std::vector<double>* byte_x = nullptr;
    // true = one of the four DIALOG editors (settings / load / commit-title /
    // BPM, painting in the centered modal dialog since 2026-08-12 — the field
    // was `bottom_strip` while they lived on the status lane); false = the
    // top-strip flag editor. Selects the claim region and the repaint owner.
    bool                dialog       = false;
};

ActiveEditorText active_editor_text(AppState& app, const GuiAudio& audio) {
    (void)audio;
    ActiveEditorText g;
    // THE FOUR DIALOG EDITORS share ONE publication — only one of them is
    // ever open, and paint_modal_dialog fills it from whichever editor it
    // actually painted. An invalid publication (nothing painted yet, or an
    // editor the dialog's precedence hides — a prompt is up) leaves this
    // invalid, exactly as the flag
    // editor's does: what is not on screen takes no clicks.
    const AppState::DialogEditorText& be = app.dialog_editor_text;
    const bool dialog_open =
        text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.load_editor) ||
        text_editor::is_active(app.commit_title_editor) ||
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
    if (dialog_open) {
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
        g.dialog       = true;
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
// (finish_chrome_press_release synthesizes it and calls on_key at the lift),
// so "the view
// consumes this button's act" is exactly "the view's keyboard gate consumes this
// button's chord" — this walks kToolbarChords and asks that gate. So the faces
// cannot drift from the allowlist: admit a chord there and its button lights on
// the next frame with nothing to remember here.
//
// THE SETTINGS ANCHOR IS THE ONLY DEAD HAND ENTRY — one dead entry of the
// three, since 2026-08-08.
// The anchors are the roster's only NON-chord actions, so there is no chord
// to ask the gate about and each has to be answered here; what changed is the
// answer for one of them (and FILE, 2026-08-13, landed on the LIVE side: its one
// item is Ctrl+Q, which the mode admits, so its menu works in there exactly as
// Navigation's does). SETTINGS stays DEAD because toggle_dropdown still
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
// — and no lock rides this row in any state since 2026-08-14, the padlock
// having moved to the icon row's own read-only button.
//
// THE MODE'S OWN KEYS ARE ASKED FIRST, and that is not a detail: the allowlist
// never sees the mode's own vocabulary — handle_history_mode_key consumes it one
// line above — so asking the allowlist alone would call bare `h` blocked and
// grey out THE VERY BUTTON THAT LEAVES THE VIEW. history_mode_owns_key is that
// line's own predicate, shared rather than re-spelled, and its membership is
// re-derived at its own definition.
// THE PARTITION DID NOT MOVE WHEN THAT VOCABULARY GREW, any of the three times
// (2026-08-05 — first the diff-flag cycle, Home/End and `c`, then the compare
// toggle; 2026-08-08 — bare `u`, the reading's own toggle), and it did not have
// to move when the ROSTER later grew buttons for those same shapes either:
// bare Home / End became the bottom row's SKIPS (2026-08-11), bare `c` the icon
// row's zoom-original (2026-08-12), and bare Tab / Shift+Tab / Ctrl+Shift+Tab
// the bottom row's MARKER-WALK GROUP (2026-08-15). Every one of them is a chord
// the MODE OWNS, which is exactly what makes this walk answer LIVE for its
// button with nothing hand-listed — the same free derivation the walk's two
// arrows, the Cumulative toggle and Revert already ride. (The membership is
// re-greped here rather than inherited: this paragraph carried "nothing in
// kToolbarChords dispatches bare Tab, Home, End or `c`" for three roster
// growths after it stopped being true.)
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
// struct); and it admits CTRL+H only while a diff flag is selected, so this
// walk answers DEAD for REVERT with an empty subject — AN ANSWER NO FACE READS
// SINCE 2026-08-15, redesign_button_enabled lifting the four history companions
// over this partition entirely, so the chord still refuses while the button
// stays lit and a click on it is a consumed no-op, the roster's standing shape
// for a refusal (the record, with the architect's blink reasoning, is at that
// arm in redesign_button_enabled, app_state.h; the note below on the revert act
// says the same thing where the entry itself is spelled); and bare `'` only while
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
// 2026-08-04, re-verified 2026-08-05, re-derived 2026-08-13 when the Quit button
// left the roster for the File menu — its LIVE entry is now the FILE ANCHOR's,
// hand-answered with the other two, and the Ctrl+Q admission it rested on is
// unchanged):
//   LIVE — the view bar's ViewSW/ViewTP/ViewTW (bare
//   1/2/3, the admitted view selectors), Save (Ctrl+S, which in this mode IS the
//   save-and-commit checkpoint act and wears the "Save and Commit" face — LIVE
//   ONLY WITH A NON-EMPTY HEAD DELTA AND NO CHECKPOINT IN FLIGHT, and greyed
//   rather than relabelled in either case; it was RENDER's chord and RENDER's
//   face until 2026-08-08, when the act moved onto the save it begins with),
//   the icon row's S/T + W/P radios (bare `t` / `p`, admitted with the view
//   switches), THE ZOOM GROUP's four since the 2026-08-12 relayout (bare `=`,
//   `-` and `0` are the allowlist's own zoom admissions and bare `c` is the
//   mode's vocabulary — pure navigation, live with nothing hand-listed),
//   the load-editor opener (bare `'`, which in this mode loads THE
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
//   and THE BOTTOM ROW'S MARKER-WALK GROUP since 2026-08-15 (bare Tab,
//   Shift+Tab and Ctrl+Shift+Tab) — the mode's own vocabulary once more, so
//   all three answer LIVE for free and each does the mode's own thing in
//   here: the two bbox buttons step the DIFF-FLAG cycle forward and back, and
//   boost runs the reverse WALK-SOURCE cycle rather than the paired marker
//   march it runs outside,
//   and THE BOTTOM ROW'S SKIPS and THE ZOOM-ORIGINAL button on the same terms
//   (bare Home / End are the mode's absolute jumps, bare `c` its own centring),
//   and THE CUMULATIVE TOGGLE since 2026-08-08 (bare `u`, the same vocabulary
//   and the same free answer). Those three plus Revert were the roster's FOUR
//   RESTING-DISABLED buttons until 2026-08-15, when the architect's scoped-truth
//   ruling gave the whole cluster a plain `true` — the record is at their arm in
//   redesign_button_enabled. This walk's answers for them did not change and
//   this paragraph is still what says they ACT in here; what changed is that
//   nothing reads it for them any more (see the note on Revert below). The
//   arrows never greyed at a walk WALL either — a step past the oldest or
//   newest member is a consumed no-op, which is the same nothing every other
//   refusal in this partition is.
//   and THE REVERT ACT since 2026-08-05 (Ctrl+H) — the SECOND session-dependent
//   entry: the allowlist admits its chord only while a diff flag is selected
//   (history_mode_revert_subject_standing), so this walk answers DEAD with an
//   empty subject and LIVE the moment a click selects one, from the same
//   admission with nothing restated here, exactly as Save's head-delta grey
//   does. ITS FACE NO LONGER READS THAT ANSWER (architect 2026-08-15): the
//   button was the one place a per-SELECTION fact reached a chrome glyph, and
//   redesign_button_enabled now lifts the four companions over this partition
//   entirely, so the chord still refuses on an empty subject while the button
//   stays lit. The reasoning is the blink, not the logic, and it is recorded
//   there; this answer is kept exact because the KEY still reads it.
//   and THE NAVIGATION ANCHOR since 2026-08-08 (architect) — one of the
//   three hand entries, flipped: its menu opens in the view and its commands act
//   there, so a dead face would be a lie about a working button. It is one of
//   the two LIVE entries that are not a chord's admission, which is why they are
//   spelled in the body rather than derived,
//   and THE FILE ANCHOR joined it 2026-08-13 for the same reason: its menu opens
//   in the view too and its one item is Ctrl+Q, which the mode's allowlist
//   admits, so its face must be live.
//   DEAD — Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z); RENDER since 2026-08-08
//   (Ctrl+Alt+R, which left the allowlist with its shifted twin when the act
//   moved onto Ctrl+S — so the button wears its ordinary Render face over this
//   partition's dead one, and the walk says so with nothing hand-listed);
//   copy phase (Ctrl+P), paste
//   phase (Ctrl+Alt+P), the BPM
//   opener (bare `m`), iteration mode (bare `i`), follow (bare `f`), listen
//   (bare `l`); the TRIM SCISSORS (bare `x`) and the FOUR MARKER VERBS since
//   the 2026-08-12 relayout (bare `s`, Delete, Ctrl+D, Ctrl+N — authoring,
//   consumed like the rest); and the SETTINGS anchor — the only anchor here
//   since 2026-08-08, when NAVIGATION moved to the LIVE column above with its
//   menu (FILE has never been in this column: it landed live, 2026-08-13).
//
// TWO THINGS IT DELIBERATELY DOES NOT SAY. (1) The base chord decides the face,
// which since 2026-08-08 has nothing left to arbitrate on row 2: the ONE button
// whose shifted twin the mode consumed while its base chord stood — Render —
// is dead on both shapes now, and Save admits no shift press at all. (Save's
// own base chord is what this walk asks about, its shift column being false in
// the table and in redesign_button_shift_admits alike.) (2) A
// button the READ-ONLY tab bit refuses is not this function's business: that
// refusal is the lock's, and it applies inside the view exactly as outside it.
// SINCE 2026-08-15 THE LOCK GREYS ITS OWN TEN (redesign_button_enabled's
// read-only arm — the architect's second MODE statement), so the two greys can
// now land on the same button and simply agree; but they are still two facts
// with two owners, and neither reaches into the other. (The clause this
// paragraph used to end on — "the `'` button stays lit on a locked tab, in the
// view as out of it" — was true under the never-grey rule and is superseded by
// that ruling: `'` is one of the ten, and it greys on a locked tab in either
// state now.) Only the VIEW's own consumption greys anything HERE.
//
// EVERY DEAD ANSWER IS PAINTED AGAIN (architect 2026-08-14, "no more
// hiding/showing icons in top icon row"): the mode-collapsing roster of
// 2026-08-12, narrowed on 2026-08-13 and deleted whole on 2026-08-14, used to
// take part of this partition's DEAD column out of the icon row's walk
// entirely. It does not any more — this walk's verdict is the grey face
// everywhere, on every row, with the four history companions the one
// remaining thing the product hides and the BOTTOM ROW's cluster swap the
// mechanism (they are not this walk's business either: outside the view they
// publish no rect, and inside it their chords are the mode's own vocabulary,
// so this function answers LIVE for them).
bool history_mode_disables_button(const AppState& app, RedesignButton b) {
    if (b == RedesignButton::Settings) return true;
    if (b == RedesignButton::Navigation) return false;
    if (b == RedesignButton::File) return false;
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
    // (the table plus the three anchors is the whole roster) and stated rather
    // than asserted, so a future button defaults to LIVE — the face it already
    // had — instead of greying on a chord nobody has written yet.
    return false;
}

// (THE MODE-COLLAPSING ROSTER IS DELETED — architect 2026-08-14, "no more
// hiding/showing icons in top icon row". `redesign_button_collapsed` answered
// "does the icon row's walk SKIP this button" over two derived levels — the
// four history mode-companions at rest, and inside the `h` view a whole group
// to the RIGHT of the history opener that the mode consumed outright — and
// with it go `history_mode_consumes_outright`, the `icon_group_begin` /
// `icon_group_end` span walk, `redesign_button_mode_companion` (app_state.h)
// and `history_mode_admission_is_momentary` (input_key_dispatch.cpp), which
// existed only to keep Save's and Revert's moment-state greys out of the
// collapse. The row paints every member in every state now: what a mode
// refuses GREYS, through `history_mode_disables_button` above and the
// enabled predicate that reads it, which is the convention every other row
// already used. The group boundaries stayed — `redesign_button_opens_icon_
// group` is the painter's divider owner, its one reader.)

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
//   * THE POINTER CLICKS — the plain marker click, the shift RANGE click and
//     the ctrl TOGGLE click, ALL THREE THROUGH ONE SITE since 2026-08-15
//     (run_marker_click_act, this file: the click acts at the LIFT, or at the
//     threshold crossing when a plain arm becomes the drag, so the three arms
//     that used to land from on_button_press are one body now). Each lands on
//     the FOCUS its own arm just set: the clicked marker, the clicked range
//     end, the toggled-in marker, or the focus repaired after a toggle-out (an
//     empty post-toggle selection lands nothing). The plain click's FOUR
//     deferred completions left this list 2026-07-29 with the group drag —
//     horizontal movement is a focus act, the doctrine at the head of
//     position_nudge.h — and the DEFERRAL that stands here now is a different
//     thing entirely: not a click held back for a group, but the whole click
//     waiting for the lift;
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
// navigation). Some callers stop playback first (the marker click owns that
// stop at its one act site, Tab-family symmetry; the restores and the bpm-editor
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
// superseding the kill-and-revive of 2026-07-23). A scrub click
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

// The scanner scrub body. ONE CALLER, re-derived by grepping this
// function 2026-08-13: the DEFERRED CLICK ACT's scrub arm (run_nav_click_act),
// reached by a motionless release of a plain LOWER-HALF waveform press. The
// caller moved from the press to the release with the ruling that the two
// halves are one surface — "we do everything on lift the finger or on mouse
// up, but the playhead scrub, we do right on mouse down. We should remove
// that" — and the body did not change at all: the act is still one act per
// click. (The BARE RIGHT full-height entry of 2026-08-01 died 2026-08-12 with
// the right button's unbinding; the marker-text lane's empty-spot scrub was
// DELETED 2026-07-27 with that lane.) The caller owns only its own gate — the
// half, read at the press and stashed on the pending — and everything below is
// shared. See the declaration for the full contract. ONE-SHOT per click
// (architect 2026-07-23, the Ableton model): derive the clicked column's frame
// and run one scrub act — the press arms only the pending click, a held press
// does nothing further, and CROSSING the threshold cancels the act outright by
// making the gesture a pan (each click pays scrub_act_at's stop quiescence
// fence AT MOST once — a stopped session's launch pays none — and the
// per-column fence cadence class is structurally gone). NOTHING BETWEEN THE
// PRESS AND THE ACT CAN KILL THE SESSION: the press claims nothing and stops
// nothing, and the drag-modal gate swallows every chord while the pending
// stands, so the act still sees the LIVE session — load-bearing for the
// stop-then-start ruling, whose whole point is that the interrupting click is a
// stop rather than a launch.
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
//   1. the prompt's veil (the top of the handler — its dialog buttons are the
//      one thing a press can reach, and a button carries no cursor cue);
//   2. the four DIALOG modal editors' veil, which consumes every press
//      outside the dialog's own field and buttons — the
//      shared predicate is modal_dialog_editor_active;
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
    // THE VEIL'S ONE EXCEPTION IS THE FIELD (architect 2026-08-13, with the
    // Text kind). The blanket above it is unchanged in kind: a dialog editor
    // consumes every press outside its own box, so every zone this map would
    // otherwise answer is a lie while one stands. But the INSET FIELD is the
    // one place inside the veil that TAKES a pointer act — the click-to-caret
    // and the text drag claim exactly this published rect (on_button_press's
    // caret block reads app.modal_dialog.field, the painter's own stash) — so
    // naming the I-beam there is the map's own rule (the cue promises the
    // gesture), not an escape from the veil. The BUTTONS are the veil's other
    // reachable surface and take no cue, a button carrying none anywhere; a
    // PROMPT publishes a zero field, so it cannot reach this arm even if the
    // return above it ever moved.
    if (modal_dialog_editor_active()) {
        return rect_contains(app.modal_dialog.field, x, y)
                   ? GuiCursorKind::Text : GuiCursorKind::Arrow;
    }
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
    // THE PENDING ARMS ARE THE SAME ARM, not a second one deciding differently:
    // sub-threshold the pointer still rests on the geometry it pressed (the
    // endcap, the bridge, or the ctrl click's bound), so the pending's
    // record and the hover map name the same cue — reading the record here
    // just keeps one owner across the whole press-to-release span. (The modal
    // gates above are structurally inert mid-drag — no press or key opens a
    // prompt, editor or dropdown while the button is held — so their rank
    // costs nothing.)
    // TWO PENDINGS SINCE 2026-08-15: the ctrl / ctrl+shift BOUND SET no longer
    // writes its bound at the press and arms the endcap pending there — it arms
    // PendingClickAct and writes at the LIFT — so the cue that used to come from
    // the endcap pending for the whole hold now comes from this one, on exactly
    // the same rule and naming exactly the same bound. Its OTHER three kinds
    // are not trim gestures at all and fall to the live-gesture Arrow below,
    // which is what they should show: a framing or create double-click's second
    // press and an `h`-view flag press arm no drag, and no click carries a cue
    // anywhere in the product.
    // AND THE BOUND-SET PENDING ASKS THE ACT'S OWN DECIDER, at its PRESS column:
    // the arm is unconditional now (every refusal moved into the act, to be
    // re-asked at the lift), so without this a press the STRICTLY-INSIDE guard
    // will refuse would wear a bound cue for its whole hold — promising a set
    // that cannot happen, which is exactly what the hover map's own ctrl arm
    // refuses to do below. Read from the PRESS column, never the live pointer,
    // so the answer is fixed for the pending's life and the cue cannot flicker
    // as the hand moves — the live-trim-gesture rule's own requirement.
    const bool trim_bound_set_pending =
        app.pending_click.kind == PendingClickKind::TrimBoundSet &&
        trim_bound_click_frame(app.pending_click.is_begin,
                               app.pending_click.press_x).has_value();
    if (app.trim_drag.active || app.pending_trim_drag.active ||
        trim_bound_set_pending) {
        const bool both     = app.trim_drag.active
                                  ? app.trim_drag.both
                                  : (app.pending_trim_drag.active &&
                                     app.pending_trim_drag.both);
        const bool is_begin = app.trim_drag.active
                                  ? app.trim_drag.is_begin
                                  : (app.pending_trim_drag.active
                                         ? app.pending_trim_drag.is_begin
                                         : app.pending_click.is_begin);
        if (both) return GuiCursorKind::TrimResize;
        return is_begin ? GuiCursorKind::TrimBoundBegin
                        : GuiCursorKind::TrimBoundEnd;
    }
    // THE OVERVIEW BOX'S LIVE DRAGS KEEP THEIR CUE on the trim rule (the thing
    // dragged is the thing the cursor names, read from the drag's own record —
    // the lane rework, 2026-08-12), AND THE BOX PAN JOINED THEM 2026-08-13
    // (architect): the pan drags the box exactly as an edge drag drags an edge,
    // so its TrimResize stays true for the whole gesture and dropping to the
    // Arrow mid-slide was the odd one out. The gesture keeps no cursor of any
    // other kind — the pan is capture-free, so there is a visible cursor to
    // keep. THE PENDING (the outside press, 2026-08-15) takes THE ARROW since
    // codex round 19, which is again the same answer the hover map gives at the
    // point it is resting on — outside the box arms nothing to name — so the
    // whole press-to-release span still reads ONE kind, and it cannot change
    // kind mid-gesture either, the crossing resolving into
    // nothing (the outside-drag extension is deleted). That is why there is no
    // separate pending struct here as the trim and marker drags have — the
    // pending is a KIND of this record, so one arm covers the whole span.
    if (app.overview_drag.active) {
        switch (app.overview_drag.kind) {
            case OverviewDragKind::Pending:
                return GuiCursorKind::Arrow;
            case OverviewDragKind::Pan:
                return GuiCursorKind::TrimResize;
            case OverviewDragKind::EdgeBegin:
                return GuiCursorKind::TrimBoundBegin;
            case OverviewDragKind::EdgeEnd:
                return GuiCursorKind::TrimBoundEnd;
        }
    }
    // THE MARKER REPOSITION DRAG KEEPS ITS CUE TOO (architect 2026-08-14: "it
    // should remain left/right arrows during the drag, like trim and overview
    // drag currently do"), on the same rule and in the same member shape as the
    // two above: the thing being dragged IS the thing the cursor names, so the
    // ew-resize the flag box wears at rest stays TRUE for the whole gesture,
    // and falling to the uniform Arrow the moment the marker started moving was
    // the odd one out. The drag has exactly ONE shape — a marker slides side to
    // side and nothing else — so the record is simply that it is live; there is
    // no kind to read and no position to re-derive from (a marker drag takes
    // the pointer off the flag box by definition, exactly as a bound drag takes
    // it off the band). THE PENDING ARM IS THE SAME ARM, the trim pair's own
    // arrangement: sub-threshold the pointer still rests on the flag box it
    // pressed, where the hover map answers this same kind, so reading the
    // pending here just keeps one owner across the whole press-to-release span.
    // The gesture is capture-free, so there is a visible cursor to keep.
    // THE PLAIN ARM ONLY, since the flag's shift and ctrl presses started
    // arming the same pending (2026-08-15, the click moving to the lift): those
    // two can never become a drag, so promising a side-to-side slide for the
    // whole hold would be exactly the false promise the map's own rule forbids.
    // They fall to the live-gesture Arrow just below, which is what the hover
    // map already answers over a flag box under either modifier — cue and
    // gesture still agreeing by construction.
    if (app.drag.active ||
        (app.pending_marker_press.active &&
         !app.pending_marker_press.shift && !app.pending_marker_press.ctrl))
        return GuiCursorKind::TrimResize;
    // THE REGION'S OWN EDITOR KEEPS ITS CUE TOO (2026-08-15), on that same
    // rule and in that same member shape: it is a capture-free, visible-cursor
    // drag, and what it drags is the span (or one of its bounds), which is
    // exactly what the resting cue over that spot already named. Read from the
    // record's own `kind` like the overview drag's arm, so a bound drag keeps
    // its edge arrow for the whole slide and a move keeps the ew-resize.
    // A CROSSED BOUND SHOWS THE EDGE IT NOW IS (codex round 20): the motion
    // body rewrites `kind` when the grabbed point passes its partner, so this
    // arm needs no derivation of its own — one owner of "which edge is this",
    // read here and written there.
    if (app.region_edit_drag.active) {
        switch (app.region_edit_drag.kind) {
            case RegionEditKind::Move:     return GuiCursorKind::TrimResize;
            case RegionEditKind::BoundLo:  return GuiCursorKind::TrimBoundBegin;
            case RegionEditKind::BoundHi:  return GuiCursorKind::TrimBoundEnd;
        }
    }
    if (any_pointer_gesture_active(app)) return GuiCursorKind::Arrow;

    // THE OPEN FLAG EDITOR'S BOX IS EDITABLE TEXT, so it wears the I-beam
    // (architect 2026-08-13, with the Text kind: it showed the navigation
    // surface's PAN before, the marker lane being nav surface under it, and a
    // hand over a text field is simply wrong). ABOVE THE MODIFIER ARMS,
    // because that is where the press path puts the claim: the caret / text-drag
    // block in on_button_press tests this same published rect
    // (app.flag_editor_box.box, the painter's stash) for ANY left press,
    // before any modifier is looked at, so ctrl or shift over the open box
    // still places a caret and the cue must say so. The editor's
    // pointer-TRANSPARENCY is untouched — that is about what a press OUTSIDE
    // the box reaches, and outside is exactly where this arm stops. A cold or
    // closed editor publishes a zero rect, which contains no point.
    if (rect_contains(app.flag_editor_box.box, x, y))
        return GuiCursorKind::Text;

    const GuiRect top  = top_strip_area(app);
    const bool inside_top = rect_contains(top, x, y);
    // THE TRIM BAR BAND, spelled from the ONE geometry owner the press sites read
    // (top_trim_row_area) and derived once because THREE modifier arms below need
    // it — plain (the endcap / bridge drags), ctrl and ctrl+shift (the two
    // bound-set clicks). The presses gate on the top strip first and so does this.
    const GuiRect trim_bar_row = top_trim_row_area(app);
    const bool in_trim_bar = inside_top &&
                             y >= trim_bar_row.y &&
                             y < trim_bar_row.y + trim_bar_row.h;
    // THE `h` HISTORY MODE CONSUMES THIS BAND'S TRIM GESTURES, which is why the
    // mode enters the map HERE rather than as a fourth blanket return above. The
    // mode is PER-ZONE, and since 2026-08-07 it is the ONLY per-zone consumer
    // left — read-only was the other, and its trim refusals are deleted with the
    // ruling that trim is band rather than authored content. Under the mode the
    // Pan and the Zoom (the plain and ctrl drags on the mode's whole navigation
    // surface) stay live — they are its navigation
    // vocabulary — while
    // the endcap/bridge drags and the two ctrl bound-set clicks are consumed
    // no-ops, so their cues must go. This term is what takes them: the ctrl arm
    // falls to the surface's own Zoom-or-Arrow question and the ctrl+shift arm
    // to the Arrow it already returns everywhere else; the plain arm takes the
    // Arrow below. (The mode-scoped SCRUB cue this paragraph used to carry is
    // gone with the Scrub kind itself — 2026-08-13, the two halves becoming one
    // surface: Pan covers the whole waveform in every view, so there is no
    // crosshair left to scope.)
    // THE MODE'S OWN TRIM-BAR ACT IS A DOUBLE-CLICK (architect 2026-08-05,
    // superseding the single click and the Zoom cue it wore for a day), so it
    // adds NO cue here: a double-click has no cursor promise anywhere in the
    // product — the live band's span framing is one too, and the band shows the
    // shapes of its drags, never that.
    const bool trim_write_gestures_live =
        in_trim_bar && !app.history_mode.active;

    // THE NAVIGATION SURFACE, read from its one geometry owner: the WHOLE
    // waveform — both halves, every view — plus the two nav lanes (ruler +
    // marker lane) MINUS the flag boxes. The lower half joined 2026-08-13 when
    // the press-time scrub became the motionless release's act, which left the
    // halves differing in that act alone. It is the plain drag's PAN surface
    // and the ctrl drag's ZOOM surface, and both cues cover it whole. A FLAG
    // BOX is lane vocabulary (select / range / toggle), which carries no cue —
    // Arrow, through the owner's own carve-out.
    const bool on_nav_surface = point_on_nav_surface(app, audio, x, y);

    // (ALT IS UNNAMED: its pointer vocabulary is EMPTY since 2026-08-12 — the
    // grab-pan it carried moved onto the plain drag and the alt press claims
    // nothing anywhere, so alt falls to the modified-combination Arrow below.)
    // CTRL-EXACT: three claims, and the press path's own order between them.
    // Over the TRIM BAR ctrl sets the BEGIN bound — at the LIFT since
    // 2026-08-15, its crossing then handing over to a single-bound drag on that
    // bound (set_trim_bound_at_click_then_arm_drag) — boundary extension by
    // another route, so it takes the BEGIN cap's own cue rather than the
    // Arrow; over the OVERVIEW LANE it names NOTHING since 2026-08-15 — the
    // lane's Zoom magnifier went with the dual-axis strip drag the redesign
    // deleted, and ctrl binds nothing there now, so the lane falls to the
    // Arrow this arm returns off the navigation surface (the map's standing
    // rule: a point arming nothing shows the Arrow, cue and gesture agreeing
    // by construction — the same answer shift already gives on this lane. The
    // ctrl+WHEEL zoom step is still live there and is deliberately not cued:
    // the map answers what a PRESS would do, and no wheel is cued anywhere);
    // over the
    // NAVIGATION SURFACE it is THE ONE NAV DRAG'S ZOOM MODIFIER (the
    // live-ctrl model, 2026-08-14 — ScrollDragState): the hover cue promises
    // exactly what a ctrl press or a mid-drag ctrl press buys, the zoom, on
    // the surface that covers BOTH waveform halves and the two lanes. Ctrl's
    // other top-strip claim is the marker membership
    // toggle, which is not a drag and has no cue — the flag carve-out above.
    // The `h` view ADMITS the zoom (its navigation vocabulary), so the cue
    // stands in there over the view's own nav surface.
    // (MID-GESTURE the map never runs — the capture hides the cursor for the
    // drag's whole life, so a live ctrl edge shows nothing until the release,
    // whose restored kind the mode switches re-stamp; the
    // live-gesture-keeps-its-cue exception is for VISIBLE-cursor drags and
    // needed no revision.)
    if (mods.ctrl && !mods.alt && !mods.shift) {
        if (trim_write_gestures_live)
            return trim_bound_click_frame(/*is_begin=*/true, x)
                       ? GuiCursorKind::TrimBoundBegin : GuiCursorKind::Arrow;
        return on_nav_surface ? GuiCursorKind::Zoom : GuiCursorKind::Arrow;
    }
    // CTRL+SHIFT-EXACT: the TRIM BAR is its ONE claim in the whole product — the
    // END bound set, the begin set's mirror — so it takes the END cap's cue there
    // and the Arrow everywhere else.
    if (mods.ctrl && mods.shift && !mods.alt) {
        if (trim_write_gestures_live)
            return trim_bound_click_frame(/*is_begin=*/false, x)
                       ? GuiCursorKind::TrimBoundEnd : GuiCursorKind::Arrow;
        return GuiCursorKind::Arrow;
    }
    // Every other combination — shift, alt, and every mixed pair the press path
    // discards at its strict-modifier gate — is unnamed. Shift's arm is the
    // deliberate one: it is the REGION FORMER (the one mouse region gesture
    // since 2026-08-12), and the former carries no cue anywhere — in the `h`
    // view identically, its own shift former being the same gesture.
    if (mods.ctrl || mods.alt || mods.shift) return GuiCursorKind::Arrow;

    // PLAIN-EXACT from here, AND PLAIN IS NOW THE LANE'S WHOLE VOCABULARY (the
    // lane rework, 2026-08-12; the redesign of 2026-08-15, which deleted the
    // ctrl strip drag the Zoom magnifier this clause once wore had moved to):
    // the BOX ENDCAPS wear the trim
    // endcaps' own pair — TrimBoundBegin on the outline's left edge,
    // TrimBoundEnd on its right, through the shared hit test, since a plain
    // press there extends ONE viewport bound exactly as a trim cap extends
    // one trim bound — INSIDE THE BOX it is TrimResize ("left/right arrows
    // like on plain trim hover", the architect's words), which is honest
    // there: the drag is the box-follows-pointer pan, an x-only
    // move-the-whole-span gesture, the trim bridge's own shape — and OUTSIDE
    // THE BOX IT IS THE ARROW (codex round 19), which is the map's own
    // standing rule reasserted rather than a new exception: a point arming
    // nothing shows the Arrow. The band-wide TrimResize was true only while an
    // outside press extended the nearer BOUND, and that extension was deleted
    // on 2026-08-15 ("the threshold for the bounds is fine, the ten pixels on
    // either side works") — since then an outside press is a Pending whose
    // crossing commits nothing, so an ew-resize there promised a drag the lane
    // no longer has. What the outside press still does is TELEPORT at its
    // motionless lift, and a click carries no cue anywhere in the product.
    // The inside/outside question is asked through the painter's own span
    // owner (overview_box_span), in the press router's own order and off the
    // press router's own predicates, so cue and gesture agree by construction;
    // degenerate geometry publishes no box, which makes every press an outside
    // press and every point the Arrow — the right degenerate arm on both
    // sides. Ctrl/shift/alt/mixed presses on the lane bind nothing,
    // so they fell to the Arrow above; the `h` view keeps these cues — every
    // lane gesture is the mode's admitted navigation class. IT MUST STAY
    // ABOVE THE `inside_top` FALL-THROUGH below: the lane is a TOP-STRIP lane
    // since the relayout's commit B, so the strip's own Arrow would otherwise
    // take it.
    {
        const GuiRect ov = top_overview_row_area(app);
        if (rect_contains(ov, x, y)) {
            switch (hit_test_overview_endcap(app, audio, x, y)) {
                case TrimHit::Begin: return GuiCursorKind::TrimBoundBegin;
                case TrimHit::End:   return GuiCursorKind::TrimBoundEnd;
                case TrimHit::None:  break;
            }
            int bx0 = 0;
            int bx1 = 0;
            const bool have_box = overview_box_span(app, audio, &bx0, &bx1);
            const bool inside_box =
                have_box && x >= ov.x + bx0 && x < ov.x + bx1;
            return inside_box ? GuiCursorKind::TrimResize
                              : GuiCursorKind::Arrow;
        }
    }
    // A STANDING REGION'S OWN ZONES OUTRANK THE PAN, because inside them the
    // plain drag IS the region's editor rather than the pan (2026-08-15; the
    // press claim asks the same owner in the same order, so cue and gesture
    // agree by construction). INSIDE the span the drag is a horizontal slide of
    // the whole span, so the TRIM BRIDGE's cue is the honest one — the same
    // shape the overview box's interior takes for the same reason; ON A BOUND
    // it is the matching edge arrow, as the trim endcaps and the box edges
    // already do. It sits UNDER the modifier arms by their own rank (ctrl there
    // is the zoom drag and shift the region former, both of which bypass this
    // entirely) and ABOVE the Pan below.
    switch (region_manipulation_hit(x, y)) {
        case RegionHit::Move:    return GuiCursorKind::TrimResize;
        case RegionHit::BoundLo: return GuiCursorKind::TrimBoundBegin;
        case RegionHit::BoundHi: return GuiCursorKind::TrimBoundEnd;
        case RegionHit::None:    break;
    }
    // THE NAVIGATION SURFACE WEARS THE PAN — the cue promises the drag, which
    // is what the plain drag does there now; the motionless click needs no cue,
    // exactly as no click anywhere carries one, and that covers BOTH of the
    // halves' click acts (the upper half's playhead placement and the lower
    // half's audition scrub). That is the ruler, the marker lane's empty
    // stretches and the WHOLE waveform, in every view — "the hand shows up in
    // both the top and the bottom half" (architect 2026-08-13).
    if (on_nav_surface) return GuiCursorKind::Pan;
    if (inside_top) {
        // THE TRIM BAR BAND, RESOLVED THROUGH THE ROUTER'S OWN TWO OWNERS
        // (architect 2026-08-03, closing the band-wide cue this used to paint):
        // the plain press arms only on an ENDCAP or inside the inter-cap BRIDGE,
        // so the cue asks exactly hit_test_trim_endcap and
        // point_in_trim_bridge_span — the same predicates route_trim_bar_press
        // calls, in the same order — and a point on the band that arms nothing
        // (outside a trimmed-in window, either side of the bar) shows the Arrow.
        // Cue and gesture therefore agree BY CONSTRUCTION rather than by
        // proximity, which is the whole rule this map is written to.
        //
        // AN ENDCAP IS NOT THE BRIDGE: a cap moves ONE bound and the bridge moves
        // BOTH, so the caps take the boundary-extension shapes (begin left_side,
        // end right_side) and the bridge keeps ew-resize, the move.
        //
        // READ-ONLY NO LONGER REFUSES ANYTHING HERE (architect 2026-08-07): the
        // band's read-only return is deleted, trim being band rather than
        // authored content, so a locked tab runs the endcap and bridge drags and
        // this arm must promise them. The term that used to answer Arrow on the
        // read-only bit is gone with the gesture refusal it mirrored — cue and
        // gesture stay one decision, which is why nothing replaced it.
        if (in_trim_bar) {
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
            if (point_in_trim_bridge_span(app, audio, x, y))
                return GuiCursorKind::TrimResize;
            return GuiCursorKind::Arrow;
        }
        // A MARKER FLAG BOX WEARS TrimResize (architect 2026-08-13): markers
        // MOVE SIDE TO SIDE, which is exactly what ew-resize promises on the
        // trim bridge — the pair drag, the product's other move-me-horizontally
        // gesture — and the flag box is the marker's ONE pointer surface in
        // every view since stems went pointer-inert (the seventh glass ruling).
        // THE CUE NAMES THE SURFACE HERE, not one branch of it — the ruled
        // exception to this map's usual derive-one-arm-per-press-branch shape,
        // and the architect's: a flag box is the marker, and the marker is a
        // thing you slide along the timeline. The plain press's own gesture IS
        // the drag in a live view at home; where the drag refuses (off the
        // column's home view, a read-only tab) or does not exist (the `h`
        // view's diff flags, which take clicks alone) the box still wears it,
        // one shape for the one surface. It reads
        // hit_test_flag — the painter's published boxes, the same predicate the
        // press claims and the nav surface carves itself out with — so it
        // answers the LIVE marker lane and the `h` view's DIFF flags through
        // one term, that stash being whatever was drawn. It sits under the
        // modifier arms by their own rank (a ctrl or shift press on a flag is a
        // selection act, which carries no cue) and AHEAD of the strip's Arrow,
        // which is what used to answer here.
        if (hit_test_flag(app, audio, x, y) >= 0)
            return GuiCursorKind::TrimResize;
        // The rest of the strip: the button rows (claimed far above the
        // waveform in the press path, no cue of their own) and GAP 1's blank
        // band — all Arrow.
        return GuiCursorKind::Arrow;
    }
    // Below the top strip and off the waveform (the flexible gap, the bottom
    // row): the Arrow. THE WAVEFORM ITSELF NEVER REACHES HERE ANY MORE — it is
    // the navigation surface whole, answered by the Pan arm above. The lower
    // half's Scrub arm that stood here is DELETED with the Scrub kind
    // (2026-08-13): the scrub is still the lower half's click act, but a click
    // carries no cue anywhere in the product and the drag the cue must promise
    // is the pan.
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

// THE WAVEFORM'S COLUMN BOUNDS — the ONE clamp the notional column projection
// and the zoom pivot's re-derived column share, and the same bounds
// render_strip_anchor_stem draws the stem inside.
static double clamp_col_into_waveform(const GuiRect& wf_area, double col) {
    const double col_max =
        wf_area.w > 0 ? static_cast<double>(wf_area.w) - 1.0 : 0.0;
    if (col < 0.0)     col = 0.0;
    if (col > col_max) col = col_max;
    return col;
}

// THE POINTER'S NOTIONAL COLUMN — the zoom pivot SEAT's one source, and A
// PURE PROJECTION of the platform's notional pointer position into the
// waveform's own bounds. The pivot seats WHEREVER THE CURSOR IS at the
// ctrl-down, visible or invisible, and asks nothing about where the release
// will put it; what the seat then STORES is the song frame under this column
// (the ruling, the superseded seat that did ask, and why the held quantity is
// a frame rather than this column are at ScrollDragState::anchor_sample).
// NO STATE, NO ACCUMULATION, NO FORK ON WHETHER A CAPTURE IS LIVE:
// the platform's position already answers both cases (uncaptured nothing is
// virtual, so it simply IS the delivered position; captured it is the raw
// relative stream accumulated and clamped per event), so this only changes
// space — window x to waveform column — and re-clamps in the bounds THIS layer
// owns, the platform knowing nothing about the waveform.
// THAT THERE IS ONLY ONE POSITION IS THE POINT (codex round 17): a clamped
// column accumulated HERE advanced once per DELIVERED motion, on the net
// travel of a whole coalesced pointer frame, while the platform's advanced per
// RAW event — and the two answers differ at a wall (raw +20 then -8 at the
// right edge), permanently and silently, since interior motion preserves the
// offset. The full record is at GuiPlatform::notional_pointer_x_.
// Read on demand at each seat (the ctrl-armed press and every ctrl-down edge),
// so it is current by construction: under a capture the raw events of the
// frame being delivered have already been accumulated, and the settled-state
// tail sees everything.
// THE TWO CLAMPS COMPOSE, and the cost is bounded and correct: the platform
// pins into the WINDOW and this pins into the WAVEFORM, whose rect starts at
// x 0 and is the window width floored to a multiple of 16 — so the only span
// where they disagree is the inert right gutter, at most 15 px, and a pointer
// parked out there honestly has no waveform column of its own. The column
// therefore holds at the last one until the pointer comes back onto the
// waveform, which is what a projection of a real position means.
// SO THE STEM AND THE CURSOR RESTORE CAN DIFFER BY THAT GUTTER — the stem
// clamps into the WAVEFORM and the restore into the WINDOW — and NOTHING
// PROMISES THEY AGREE: the stem is simply where the cursor was, not a
// prediction of where it will go, so this is a difference and not an
// inconsistency. It is ZERO PIXELS at any window width that is a multiple of
// 16, which is every width either host runs (1920 and 1024, and 2560/3840
// besides), so it is reachable only under a hand resize to an odd width. It is
// NOT to be engineered around, and in particular the restore path takes no
// waveform clamp: the gutter is a real place on the window even though it is
// not a place on the waveform, and a pan-only release must be able to put the
// cursor back there.
double GuiInputHandler::nav_notional_col() const {
    const GuiRect wf_area = waveform_area(app);
    return clamp_col_into_waveform(
        wf_area, gui.notional_pointer_x() - static_cast<double>(wf_area.x));
}

// TELL THE CAPTURED POINTER ITS WRAP SPAN — the waveform's bounds, between
// which the hidden cursor folds EDGE TO EDGE when its travel would carry it
// past one. ONE OWNER, fired immediately after the begin at the ONE capture
// site left (the nav drag's threshold crossing; the overview lane's arm was the
// second until 2026-08-15), so no two sites can
// hand the platform different spans; the platform holds them for the capture's
// life and knows nothing about a waveform (contract at
// GuiPlatform::set_capture_wrap_span).
// THE BOUNDS ARE THE WAVEFORM'S RATHER THAN THE WINDOW'S on the architect's own
// reasoning — that this makes the behaviour identical at every resolution — and
// they are the same INCLUSIVE pair the column clamp above uses: the first and
// last painted columns, so a pointer resting exactly on either is inside and
// stays there.
// NOTHING ELSE IS PASSED: the fold runs bound to bound, so the centre column
// the one-commit centre form had to decide here is gone, and with it the
// even-width rounding question it existed to answer — an edge-to-edge fold has
// no middle, so that question is retired rather than settled.
void GuiInputHandler::tell_capture_wrap_span() const {
    const GuiRect wf = waveform_area(app);
    const double  lo = static_cast<double>(wf.x);
    const double  hi = lo + (wf.w > 0 ? static_cast<double>(wf.w) - 1.0 : 0.0);
    set_strip_capture_wrap_span(lo, hi);
}

// THE ZOOM STEM'S COLUMN X — its column's ORIGIN in surface coordinates, not
// a pixel centre, and the name says so because that distinction is the whole
// reason this owner is shaped the way it is. One owner, two callers: the zoom
// body's per-event restore stamp and the ctrl-up handover that hands this same
// column to the pointer's notional position (sync_nav_drag_mode).
// WHAT IS SHARED IS THE COLUMN, because that is the quantity that must never
// diverge — one derivation, so the stem and the cursor cannot name different
// columns. That is the sent-vs-stamped risk this owner exists to close, and it
// is a WHOLE-COLUMN risk (a second derivation could read a stale viewport, or
// the anchor before its edge rebind), never a sub-pixel one.
// WHAT IS NOT SHARED IS THE PIXEL CONVENTION. A cursor position wants the
// CENTRE of a pixel; a pointer position wants the coordinate. Each consumer
// adds what it needs, at its own site.
// RECOMPUTING FROM THE ANCHOR AFTER AN APPLY REPRODUCES THE COLUMN THAT APPLY
// PIVOTED AT, INCLUDING A REBOUND ONE: the edge trick writes
// anchor_sample = vp + clamped_col·spp, so this derivation inverts it exactly
// (up to the viewport's sub-pixel grid snap, which self-heals on the following
// event exactly as the apply's own live re-read does).
// IT READS THE LIVE VIEWPORT, while the PAINTER derives the same stem through
// displayed_column_at on the PLATE basis. That is the standing displayed-basis
// rule and NOT a divergence to fix: painted pixels ride the displayed basis,
// hit and restore geometry ride the live one.
// The column->x math is render_strip_anchor_stem's own origin term
// (area.x + col), clamped in the waveform's bounds through the one clamp body
// above; that painter adds its own +0.5 to centre a 1px cairo stroke in the
// pixel, which is the same convention split stated once more at a third
// consumer.
double GuiInputHandler::nav_stem_column_x() const {
    const GuiRect wf_area = waveform_area(app);
    const double  spp     = current_samples_per_pixel(app, audio);
    const double  col =
        spp > 0.0 ? (app.scroll_drag.anchor_sample -
                     static_cast<double>(app.viewport_start_sample)) / spp
                  : 0.0;
    return static_cast<double>(wf_area.x) +
           clamp_col_into_waveform(wf_area, col);
}

// THE NAV DRAG'S ZOOM/PAN MODE SYNC — one body, two callers (the contract and
// the reason there are two are at the declaration; both re-seat directions are
// at ScrollDragState, app_state.h). Within the live gesture CTRL ALONE is read
// — shift and alt bind nothing mid-drag and ride along inert.
void GuiInputHandler::sync_nav_drag_mode(GuiInputState mods) {
    ScrollDragState& sd = app.scroll_drag;
    if (!sd.active || mods.ctrl == sd.zooming) return;
    sd.zooming = mods.ctrl;
    if (sd.zooming) {
        // CTRL MEANS ONE THING: IT SEATS THE STEM WHERE THE CURSOR IS, FULL
        // STOP (architect 2026-08-14, from the rig, undoing his own ctrl-down
        // pop of hours earlier along with the whole teleport-on-clamp family:
        // "I want to undo that idea"). The pop existed because a runaway pan
        // used to leave the pointer PINNED at a wall, where a pivot can show
        // only half of what a zoom is for; the hidden cursor now WRAPS to the
        // waveform's opposite bound instead of pinning, so it is never out
        // there to be brought back and the edge has nothing left to do but seat
        // (GuiPlatform::notional_pointer_x_ carries the wrap's record).
        //
        // THE PIVOT SEATS AT THE POINTER, every ctrl-down (the withdrawn
        // persist-across-toggles experiment and its reason are recorded at
        // ScrollDragState::anchor_sample). The notional column IS the
        // pointer's clamped column, projected from the platform's one notional
        // position at this instant, so the seat needs nothing kept current for
        // it — and what is STORED is the song frame under that column, through
        // the apply's own conversion so the seat and this phase's first event
        // cannot disagree.
        sd.anchor_sample = static_cast<double>(app.viewport_start_sample) +
                           nav_notional_col() *
                               current_samples_per_pixel(app, audio);
        // The restore X is NOT stamped here: the stem override exists to land
        // the released cursor on a stem the edge-rebind has pinned, and the
        // zoom phase's own applies set it. Until one runs, the notional
        // position is still the honest restore.
        if (sd.moved) set_strip_capture_restore_kind(GuiCursorKind::Zoom);
    } else if (sd.moved) {
        // THE ZOOM PHASE'S DRIFT IS HANDED TO THE POINTER HERE, and that is
        // what makes the override's clear honest: the fallback it falls back
        // TO is now the stem. The phase froze the notional x at the ctrl-down
        // column while the stem's column slid with the song frame it holds
        // (every time clamp_viewport_start saturates), so clearing the
        // override alone left the two naming different pixels and the cursor
        // landed on whichever the user's release ORDER selected. Telling the
        // platform the position — the fifth member of the told-not-inferred
        // family, freeze-independent by class because it states a POSITION
        // rather than accumulating a delta (GuiPlatform::set_notional_pointer_x)
        // — makes both orders agree: with ctrl still held the release lands on
        // the stem through the OVERRIDE, after a ctrl-up it lands on the stem
        // through the NOTIONAL POSITION, and a pan that follows advances from
        // there.
        // NOT CONDITIONAL ON A CLAMP HAVING HAPPENED: where nothing saturated
        // the stem never left the notional column, so this writes the value
        // that was already there and costs nothing — asking would be a second
        // predicate over a quantity that already answers.
        // Ordering against the freeze release at this body's tail does not
        // matter, and is stated rather than relied on silently: this write is
        // not gated by the freeze (only the relative stream's accumulation is).
        // WHAT IS HANDED OVER IS THE COLUMN'S OWN COORDINATE, not the pixel
        // centre: a pointer position is not a pixel. So the value written back
        // here is exactly the value the next ctrl-down seat reads back through
        // nav_notional_col(), and the handover is idempotent under repeated
        // ctrl cycles inside one capture — that is why no ratchet exists,
        // rather than why one is tolerated.
        set_strip_capture_notional_x(nav_stem_column_x());
        clear_strip_capture_restore_x();
        set_strip_capture_restore_kind(GuiCursorKind::Pan);
    }
    // THE POINTER'S X FREEZES FOR THE ZOOM PHASE AND RESUMES FOR THE PAN
    // (architect 2026-08-14: the zoom locks the x position). Unconditional
    // here — the platform's own capture guard answers a sub-threshold edge,
    // and the crossing re-asserts what those edges could not reach. THE
    // GESTURE'S ARITHMETIC IS UNTOUCHED EITHER WAY, and that separation is the
    // whole reason a single bit can do this: both phases difference last_x off
    // the UNFROZEN TRAVEL LEDGER, so the zoom keeps its unlimited lateral
    // travel while the pointer's clamped NOTIONAL position simply stops
    // advancing — the level spends those pixels and the position must not
    // spend them again.
    set_strip_capture_notional_x_frozen(sd.zooming);
    // The stem's paint or erase: a mode switch is a discrete edge, so full
    // waveform-area damage (the arm's own shape). This is what makes the stem
    // vanish AT the ctrl-up rather than at the next motion.
    viewport.invalidate_waveform_area();
}

// THE NAV DRAG'S ZOOM PHASE, one event (the live-ctrl model — contract at
// ScrollDragState, app_state.h): dx zooms plain linear off the LIVE level
// about the seated pivot, and dy is DISCARDED — the same axis the pan phase
// reads, with the modifier deciding what horizontal travel MEANS rather than
// which axis is live (architect 2026-08-14, THE ROTATION).
// THE SIGN — RIGHT ZOOMS IN, LEFT ZOOMS OUT — is `zoom_level - dx/rate`, dx
// being positive to the right and a SMALLER level being deeper in. Its
// derivation is the PINCH this drag stands in for: take the dominant hand's
// finger as the one the mouse imitates, and spreading the fingers apart moves
// that finger RIGHT and zooms in. The pan-derived argument (dragging LEFT
// advances the view forward, a piece opens at full zoom out, so forward means
// in) is SUPERSEDED — it is outranked because the rotation itself came from
// the pinch, so a sign taken from the pan would have made the two surfaces
// disagree about the very thing the rotation existed to make agree. (Both
// arguments in full, and the cross-surface case for rotating at all, are at
// the contract; not restated here.)
// AND THE POINTER'S OWN X IS FROZEN WITH IT, which is a SEPARATE STATEMENT
// (architect 2026-08-14, from the rig: "I've been operating under the
// assumption that the zoom control would lock the x position"). The rotation
// makes it MORE necessary: this phase SPENDS its lateral travel on the level,
// and the pointer's notional position must not spend the same pixels a second
// time — nor could it, without capping a zoom at the window's width, since the
// notional position clamps into the surface where the travel ledger does not.
// The freeze is asserted at the mode edges (sync_nav_drag_mode) and lives in
// the platform, which owns the position; the ledger is untouched.
// The viewport itself never moves here (a pure zoom pivots about the anchor's
// column), so no wall clamp is needed on it — the resting viewport is already
// chokepoint-legal, and apply_strip_drag_zoom re-clamps downstream. last_x
// stays current in this phase exactly as in the pan phase, which is the
// ctrl-up switch's whole rebase: the first plain event after a switch
// measures its dx from the pointer's own position, so nothing can jump — both
// phases difference the SAME quantity, so the rebase holds on both sides of
// the edge.
// `y` IS UNREAD HERE, and deliberately: the rotation left this gesture no
// vertical term at all (the pan phase has none either). The parameter stays
// because its callers hand the motion event's pair straight through.
void GuiInputHandler::apply_nav_zoom_at(int x, int y, bool final_event) {
    (void)y;
    ScrollDragState& sd = app.scroll_drag;
    const double dx = static_cast<double>(x - sd.last_x);
    sd.last_x = x;

    const double spp = current_samples_per_pixel(app, audio);
    const GuiRect wf_area = waveform_area(app);
    const double W = static_cast<double>(wf_area.w);
    const int64_t total = live_total_frames(app, audio);
    if (W <= 0.0 || spp <= 0.0) return;

    // Incremental off the live level, pre-clamped into the chokepoint's own
    // window exactly as every apply_strip_drag_zoom caller pre-clamps.
    double new_level = app.zoom_level - dx / kNavZoomPxPerLevel;
    const double max_l = effective_max_zoom_level(W, total,
                                                  audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // THE PIVOT'S COLUMN UNDER THE LIVE VIEWPORT, with the Ableton EDGE TRICK
    // — the deleted strip drag's own step, minus its pan term: this
    // phase never moves the viewport, so the resting `viewport_start_sample`
    // IS the viewport the zoom will pivot against and there is no local `vp`
    // to clamp first. The pivot is a SONG FRAME (ScrollDragState), so its
    // column is derived fresh every event; clamping it into [0, W-1] and
    // REBINDING the anchor to that edge pixel's frame is what keeps the focus
    // on screen once a wall has pushed it past an edge.
    // WHAT THE REVERSIBILITY PROPERTY IS, stated exactly: THE ANCHORED FRAME
    // IS INVARIANT FOR THE PHASE, so zooming out and back in by the same dx
    // pivots about the SAME song position both ways and the drag reverses into
    // the section it came from. AWAY FROM THE WALLS that is the strict
    // identity — the column re-derives to the value it was placed at, so the
    // return event reproduces the earlier viewport. AT A SATURATED WALL the
    // viewport cannot come back the same way (while `vp` is pinned the view is
    // determined by the level alone), and what survives is the FOCUS: the
    // stem's column slides left/right with the content and the frame under it
    // never changes. That is the case the screen column got wrong — it held
    // the COLUMN and let the song walk out from under it, so the way back
    // zoomed into a later section entirely (the architect's own scenario,
    // worked at ScrollDragState). WHERE THE PROPERTY STOPS: the edge REBIND,
    // the one lasting mutation here — once the anchored frame has been pushed
    // off the visible span the anchor becomes the edge pixel's content, and
    // the return trip pivots about that instead.
    double anchor_col =
        (sd.anchor_sample - static_cast<double>(app.viewport_start_sample)) /
        spp;
    const double clamped_col = clamp_col_into_waveform(wf_area, anchor_col);
    if (clamped_col != anchor_col) {
        sd.anchor_sample =
            static_cast<double>(app.viewport_start_sample) + clamped_col * spp;
        anchor_col = clamped_col;
    }

    // Drive the capture's release-restore x to the stem, the strip drag's own
    // rule; a later pan phase clears it back to the notional x at its switch —
    // having first HANDED that switch this same column, through the one owner
    // both sites read (nav_stem_column_x, above). Recomputing there rather
    // than passing anchor_col along is what keeps the ctrl-up handover and this
    // stamp from drifting apart.
    // THE +0.5 IS ADDED HERE AND NOT IN THE OWNER: the cursor is sent to the
    // CENTRE of the stem's pixel, which is a convention belonging to the
    // cursor and not to the column.
    if (set_strip_capture_restore_x)
        set_strip_capture_restore_x(nav_stem_column_x() + 0.5);

    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample, anchor_col,
                                   final_event);
}

// --- The overview lane's own gestures (the lane rework, 2026-08-12; the
// box-is-the-subject redesign, 2026-08-15) ----------------------------------
//
// Contract at OverviewDragState (app_state.h). Two bodies: the TELEPORT and
// the ONE motion apply, forking on the drag's kind. The ends
// live in on_button_release, on_motion's button-lost arm and the force-end
// finalizer — absolute-position drags (the trim endcap model), so there is no
// capture to tear down and no stem to erase.

// THE CLICK-TELEPORT: center the viewport on the pressed column's whole-song
// position, zoom level UNCHANGED — a pure viewport move of the pan class (no
// playhead, no region, no selection touch; follow suppressed for the session
// by scroll_viewport's funnel exactly as any pan — the producer inventory at
// follow_overridden_for_session, app_state.h). IT RUNS AT THE LIFT since
// 2026-08-15 (the redesign; it ran at the press from its landing until then,
// which was the product's LAST press-time act — the act-at-lift sweep had
// excluded it by omission, and the touch consequence is what settled it: two
// fingers can never land on the same frame, so a press-time teleport
// necessarily fired on the first finger before any second-finger gesture could
// exist). ONE caller: on_button_release's motionless Pending arm, which hands
// it THE PRESS column — the point the user aimed at, the deferred click act's
// own rule. THE CENTERING ARITHMETIC is center_viewport_on_playhead's own
// over the lane's mapping: position = overview_anchor_sample_at_x at the
// lane-clamped column (the song walls by construction), start =
// nearbyint(position) − samples_visible/2, the delta handed to
// scroll_viewport (continuous=false — a discrete jump, one predictor
// resync), which clamps through clamp_viewport_start (grid snap + walls)
// and takes the synchronous full-render route.
void GuiInputHandler::run_overview_teleport(int x) {
    const GuiRect lane = top_overview_row_area(app);
    if (lane.w <= 0) return;
    const int cx = std::clamp(x, lane.x, lane.x + lane.w - 1);
    const double pos = overview_anchor_sample_at_x(app, audio, cx);
    const int64_t new_start =
        static_cast<int64_t>(std::nearbyint(pos)) -
        samples_visible(app, audio) / 2;
    viewport.scroll_viewport(new_start - app.viewport_start_sample,
                             /*continuous=*/false);
}

// THE EDGE DRAG'S SEAT — the kind and the FIXED (opposite) viewport bound,
// written onto the already-armed record. ONE owner, ONE caller: the press
// claim's ENDCAP hit. It had a second — the Pending outside press's threshold
// crossing, which picked the bound nearer the press column — and that gesture
// is DELETED (2026-08-15, the architect: "we can remove that, because the
// threshold for the bounds is fine, the ten pixels on either side works, it's a
// large enough threshold"), so a bound is dragged by its own grab band and
// nowhere else. It stays a body because it is the one place an edge drag's kind
// and its fixed partner are decided together. The fixed bound is the box's END
// edge for a grabbed BEGIN edge and its BEGIN edge for a grabbed END edge, both
// in the ACTIVE domain — it is the per-event zoom's anchor, held for the drag's
// life.
//
// IT IS THE PAINTED EDGE, NOT THE RAW VIEWPORT BOUND (2026-08-15, codex round
// 22): the seat reads overview_box_edge_samples, the same owner the painter's
// box span reads, because at the RIGHT WALL the two disagree — the ruled grid
// rest may legitimately put viewport_end_sample up to one waveform pixel past
// the song end (<1 px of inert past-EOF padding, max_viewport_start_grid) and
// the box is drawn from the clamped value. Seating the raw end fixed the
// drag's span and its anchor to an INVISIBLE endpoint: the whole span was
// biased long by the overshoot, so the level was a hair too low, and the
// downstream wall clamp could settle the grabbed edge a painted column left of
// the finger while the opposite edge stayed at EOF. The gesture's contract is
// that the grabbed edge tracks the finger and the one the user can see holds
// still, so both halves must be measured in painted terms.
//
// Returns false on degenerate geometry (no waveform width, no spp — the owner's
// own refusal), where the caller drops the arm and the press is the band's
// consumed nothing.
bool GuiInputHandler::seat_overview_edge_drag(bool grabbed_begin) {
    int64_t box_begin = 0;
    int64_t box_end   = 0;
    if (!overview_box_edge_samples(app, audio, &box_begin, &box_end))
        return false;
    app.overview_drag.kind = grabbed_begin ? OverviewDragKind::EdgeBegin
                                           : OverviewDragKind::EdgeEnd;
    app.overview_drag.fixed_edge_sample =
        static_cast<double>(grabbed_begin ? box_end : box_begin);
    return true;
}

// THE ONE MOTION APPLY, forking on the drag's kind. X ONLY, STRUCTURALLY:
// mouse_y is not even a parameter — the pan has no zoom axis to leak into
// ("no cross axis allowance for up/down", the architect's ruling: the pan
// has no zoom axis at all) and the edge drags'
// level is a pure function of the pointer COLUMN. The column clamps into the
// lane first (the song walls by construction), then maps through
// overview_anchor_sample_at_x per event, so every frame is domain-correct in
// target view — the END edge reading the column's FAR boundary, which is what
// makes the inverse agree with the box painter at BOTH walls (the invariant and
// its derivation are at the mapping call below).
void GuiInputHandler::apply_overview_drag_at(int x, bool final_event) {
    // A PENDING APPLIES NOTHING, EVER (2026-08-15, the outside-drag extension's
    // deletion): a press outside the box commits at its motionless LIFT or not
    // at all, so once it has crossed the threshold there is no act left for it
    // — it simply runs to the release. The guard lives here rather than at the
    // three call sites (the crossing, the release, the button-lost arm) because
    // "a pending applies nothing" is one statement about the kind.
    if (app.overview_drag.kind == OverviewDragKind::Pending) return;

    const GuiRect lane = top_overview_row_area(app);
    if (lane.w <= 0) return;
    const int cx = std::clamp(x, lane.x, lane.x + lane.w - 1);
    // THE RIGHT EDGE READS THE COLUMN'S FAR BOUNDARY, AND THAT IS WHAT MAKES
    // THE INVERSE AGREE WITH THE PAINTER AT BOTH WALLS (codex round 21). The
    // invariant: overview_box_span's span is HALF-OPEN [x0, x1), so a viewport
    // BEGIN at song position p paints at column round(p/spp) and a viewport END
    // at p paints its visible edge at round(p/spp) − 1. The inverse of the first
    // is column·spp — exact, which is why the left edge has never been wrong and
    // why its exactness is the proof rather than the convention. The inverse of
    // the second is (column + 1)·spp, and asking the ONE mapping at cx + 1 IS
    // that inverse: the far boundary of column cx is the near boundary of
    // cx + 1. Reading it at cx instead treated the last pixel as the ORIGIN of
    // the final bin while the painter treats it as the END wall, so a right edge
    // dragged fully right topped out at total − spp — one overview column short
    // of the song end — and pulling an already-full edge outward asked for that
    // shorter span and zoomed IN by a bin. The one-bin bias was there at every
    // column, not only at the wall; the wall is just where it could not be
    // absorbed. cx + 1 leaves the lane by design at the right wall, which is the
    // point — the function is a pure scale, not a hit test, and lane.w·spp is
    // exactly the song end. THE OTHER TWO CONSUMERS DELIBERATELY DO NOT MOVE:
    // the box PAN and the click-TELEPORT ask "what song position is under this
    // pixel", whose answer is the bin's origin (the pan's grab offset is taken
    // from the same reading, so it is exact by cancellation, and the teleport
    // must not be made to overshoot the end).
    const bool far_boundary =
        app.overview_drag.kind == OverviewDragKind::EdgeEnd;
    const double pos =
        overview_anchor_sample_at_x(app, audio, far_boundary ? cx + 1 : cx);

    if (app.overview_drag.kind == OverviewDragKind::Pan) {
        // THE BOX-FOLLOWS-POINTER PAN: center the viewport on (pointer's
        // whole-song position − grab offset) — ABSOLUTE placement per event,
        // so the box tracks the pointer with no accumulated drift and a
        // wall-saturated event is a plain no-op inside scroll_viewport's
        // changed guard. continuous=true defers the predictor resync to the
        // drag's end (the grab-pan's own contract); the terminating event
        // re-anchors it here, its release/button-lost/force-end callers all
        // passing final.
        const int64_t new_start =
            static_cast<int64_t>(
                std::nearbyint(pos - app.overview_drag.grab_offset)) -
            samples_visible(app, audio) / 2;
        viewport.scroll_viewport(new_start - app.viewport_start_sample,
                                 /*continuous=*/true);
        if (final_event && playback.is_playing()) playback.resync_predictor();
        return;
    }

    // THE EDGE DRAG: the dragged edge's whole-song position follows the
    // pointer column, the OPPOSITE bound stays fixed — a zoom anchored at
    // the far edge. The span between the fixed bound and the pointer maps to
    // a LEVEL by the fit formula (effective_max_zoom_level's own equation
    // solved for an arbitrary span: level = 1 + log2(span·1000/(0.625·sr·w))),
    // pre-clamped into [kMinZoom, effective ceiling] exactly as both other
    // apply_strip_drag_zoom callers pre-clamp. THE kMinZoom FLOOR IS THE
    // CANNOT-CROSS CLAMP: a pointer on or past the partner asks for a span
    // at or below zero, the min-span floor rests it at the max-zoom minimum
    // span flush against the fixed bound — the trim drag's inclusive
    // clamp-at-the-partner convention re-expressed in level space. Applied
    // through Viewport::apply_strip_drag_zoom with the FIXED bound as the
    // anchor at its own window column (area.w for a dragged LEFT edge whose
    // fixed partner is the viewport END, 0 for a dragged RIGHT edge whose
    // partner is the START), so that bound stays put bit-exactly and every
    // event takes the chokepoint's level clamp, viewport clamp, synchronous
    // full rebuild and either-axis follow suppression.
    // THIS ARITHMETIC LIVES HERE AGAIN (2026-08-15): it was hoisted into a
    // shared span application for one commit, to be shared with the lane's
    // two-finger bounds gesture, and that gesture is deleted — a helper whose
    // whole justification was its second caller goes back where its one caller
    // is.
    const GuiRect area = waveform_area(app);
    const int     sr   = audio.sample_rate();
    const int64_t total = live_total_frames(app, audio);
    if (area.w <= 0 || sr <= 0 || total <= 0) return;
    const bool begin = app.overview_drag.kind == OverviewDragKind::EdgeBegin;
    const double fixed = app.overview_drag.fixed_edge_sample;
    double span = begin ? fixed - pos : pos - fixed;
    const double min_span = samples_per_pixel_at(kMinZoom, sr) *
                            static_cast<double>(area.w);
    if (span < min_span) span = min_span;
    double level = 1.0 + std::log2(
        span * 1000.0 /
        (0.625 * static_cast<double>(sr) * static_cast<double>(area.w)));
    const double max_l = effective_max_zoom_level(area.w, total, sr);
    if (level < kMinZoom) level = kMinZoom;
    if (level > max_l)    level = max_l;
    viewport.apply_strip_drag_zoom(level, fixed,
                                   begin ? static_cast<double>(area.w) : 0.0,
                                   final_event);
}

// THE STANDING REGION'S HIT TEST — the ONE spelling of "inside a live region",
// read by the plain waveform press claim, the cursor map and the touch pan zone
// (the three-consumer contract is at the declaration, input_handler.h; the
// model is at RegionState, app_state.h).
//
// THE BOUNDS COME FROM THE PAINTER'S OWN OWNER — region_columns on the PLATE
// basis, the very call paint_region_ground makes — so a grabbed bound is
// exactly a painted one, by construction rather than by two derivations
// agreeing. Nothing is re-derived here.
//
// THE GRAB BAND IS trim_endcap_grab_px() PER SIDE, THE SAME 15 px ON PURPOSE:
// the trim endcaps and the overview box edges already take it, and this is the
// SAME GESTURE ON THE SAME SHAPE — a third member of that family, so it takes
// the family's constant rather than a number of its own.
//
// OVERLAP RESOLVES NEARER-BOUND-WINS WITH TIES TO THE LO BOUND — the shape of
// hit_test_overview_endcap, which is itself the trim sort's tie-break over two
// fixed candidates. It is what keeps BOTH bounds reachable down to a 1 px span,
// and there is deliberately no second rule for it.
//
// An OFFSCREEN bound is simply not grabbable: its column falls outside the
// waveform and no band answers there. Clamping it to the edge would manufacture
// a handle where nothing is painted, which is the one thing the shared-owner
// rule above exists to prevent — the Move zone still covers the visible
// stretch, exactly as the highlight does.
RegionHit GuiInputHandler::region_manipulation_hit(int x, int y) const {
    if (!app.region.active) return RegionHit::None;
    if (audio.total_frames() <= 0) return RegionHit::None;
    // THE `h` VIEW ANSWERS NONE. Its spans are VIEW-LOCAL reading marks — `x`
    // is consumed in there, so a region can commit nothing — and its press
    // router forks far above this arm, so a verdict here could only make the
    // cursor and the touch zone promise a gesture that does not run.
    if (app.history_mode.active) return RegionHit::None;
    // Y-GATED TO THE WAVEFORM RECT ALONE (the architect's ruling): the RULER
    // and the MARKER LANE stay plain navigation surface even where the span
    // covers their columns, which is what keeps a pan and a zoom reachable
    // while a region covers the waveform entirely.
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    if (area.w <= 0) return RegionHit::None;   // degenerate geometry: no zones
    if (y < area.y || y >= area.y + area.h) return RegionHit::None;
    // The x band is the waveform's own, the navigation surface's spelling
    // included (the <=15 px inert right gutter counts as waveform).
    if (x < area.x || x >= top.x + top.w) return RegionHit::None;

    const GuiPaintHandler::PlateViewportBasis basis =
        paint_handler.plate_viewport_basis();
    if (basis.spp <= 0.0) return RegionHit::None;
    const GuiPaintHandler::RegionColumns cols =
        paint_handler.region_columns(basis);
    const int lo_x = area.x + cols.lo_col;
    const int hi_x = area.x + cols.hi_col;
    const int grab = trim_endcap_grab_px();
    const int dl = std::abs(x - lo_x);
    const int dh = std::abs(x - hi_x);
    const bool on_lo = dl <= grab;
    const bool on_hi = dh <= grab;
    if (on_lo && on_hi)
        return dl <= dh ? RegionHit::BoundLo : RegionHit::BoundHi;
    if (on_lo) return RegionHit::BoundLo;
    if (on_hi) return RegionHit::BoundHi;
    // Inside the span but outside both bands: the MOVE zone. The half-open
    // [lo, hi) interval is paint_region_ground's own fill span.
    if (x >= lo_x && x < hi_x) return RegionHit::Move;
    return RegionHit::None;
}

// THE REGION EDITOR'S OWN COLUMN->FRAME CONVERSION, AND IT IS THE PLATE'S
// (codex round 20). The hit test resolves the span's bounds on the DISPLAYED
// (plate) basis through the painter's own owner, so the drag has to READ that
// same basis or the gesture hits one span and moves another: while the async
// worker rebuilds, the live viewport/spp already describe a plate that is not
// on screen yet, and a bound grabbed where it is PAINTED would jump to the live
// frame under that column on its first applied event. This is the project's
// standing rule applied to a gesture that was written against the live basis by
// omission — hit geometry and painted-pixel deciders ride the DISPLAYED basis,
// and damage follows the basis of the pixels it erases (playhead_pixel_x,
// app_state.h). Its TWO callers are the press arm (the Move's grab offset) and
// the motion body, so press and motion cannot disagree either.
//
// IT IS displayed_column_at RUN BACKWARDS on that basis — vp_start + col * spp,
// nearbyint once — which is exactly the inverse of what region_columns did to
// place the bound, so a grabbed bound converts back to the frame it was painted
// from. Deliberately NOT playhead_frame_at_click_column: that owner reads the
// LIVE viewport and, in source view, the source GRID, both of which are the
// live epoch's answers. The caller supplies its own validated area (w > 0); a
// non-positive spp cannot arm this gesture at all — region_manipulation_hit
// refuses one — and if it somehow arrived the multiply would simply return the
// viewport start, there being no division here to make it worse.
int64_t GuiInputHandler::region_edit_frame_at_column(const GuiRect& area,
                                                     int mouse_x) const {
    int rel = mouse_x - area.x;
    if (rel < 0) rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    const GuiPaintHandler::PlateViewportBasis basis =
        paint_handler.plate_viewport_basis();
    const double at = basis.vp_start + static_cast<double>(rel) * basis.spp;
    return clamp_playhead_to_live_domain(
        static_cast<int64_t>(std::nearbyint(at)), app, audio);
}

// THE REGION EDIT DRAG'S ONE MOTION BODY, forking on the kind. X ONLY,
// STRUCTURALLY: mouse_y is not even a parameter — every kind is a horizontal
// slide and there is no second axis to leak into (the overview box drag's own
// shape, borrowed rather than reinvented). ABSOLUTE per event, so the placement
// folds the whole press->crossing travel by construction and a wall-saturated
// event is a plain no-op.
//
// IT WRITES app.region AND NOTHING ELSE: no trim, no playhead, no selection, no
// viewport, and it never clears the span it is editing (contract at
// RegionEditDragState, app_state.h).
void GuiInputHandler::apply_region_edit_drag_at(int mouse_x) {
    if (!app.region.active) return;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    const int64_t total = live_total_frames(app, audio);
    if (total <= 0) return;
    const int64_t wall = total - 1;
    // The pointer's active-domain frame on the PAINTED basis (the owner above),
    // the column clamped into the visible strip first.
    const int64_t at = region_edit_frame_at_column(area, mouse_x);

    int64_t lo = 0;
    int64_t hi = 0;
    if (app.region_edit_drag.kind == RegionEditKind::Move) {
        // THE SPAN FOLLOWS THE POINTER, its grab offset preserved and its width
        // invariant — the trim bridge's and the overview box pan's rigid move.
        // THE WALL CLAMP SLIDES THE PAIR rather than squashing it: a bound that
        // would run past a wall stops there WITH THE SPAN INTACT, which is also
        // what makes a full-window region's Move a silent no-op.
        lo = at - app.region_edit_drag.grab_offset;
        const int64_t span = app.region_edit_drag.span_frames;
        if (lo < 0) lo = 0;
        if (lo + span > wall) lo = wall - span;
        if (lo < 0) lo = 0;   // a span wider than the song: rest at 0
        hi = lo + span;
        if (hi > wall) hi = wall;
    } else {
        // A BOUND FOLLOWS THE POINTER WHILE ITS PARTNER HOLDS — the trim endcap
        // drag's shape. The pair is normalized after the write, so pushing one
        // bound past the other simply swaps which side of the span it is: the
        // region is free scratch with no partner wall, unlike trim, because
        // nothing here commits and `x`'s own degenerate refusal is the wall
        // that matters.
        const int64_t fixed = app.region_edit_drag.fixed_frame;
        lo = std::min(at, fixed);
        hi = std::max(at, fixed);
        // AND THE CROSSING RE-NAMES THE GRABBED EDGE (codex round 20). The
        // normalization above means the bound under the hand IS the other one
        // the moment it passes its partner, and `kind` is the ONE owner of
        // "which edge is this" — the live cursor reads it and nothing
        // re-derives it — so it flips here rather than growing a second
        // answer. Written from the same compare that just normalized: the
        // grabbed point is the LO bound while it sits at or below the fixed
        // partner and the HI bound above it, so a zero-width rest keeps the
        // LO reading, which is region_manipulation_hit's own tie-break and
        // therefore the arrow the next press would honour. Above the
        // column-change gate deliberately: the cue is about the record, not
        // about the paint, and the cursor is resolved from this field once per
        // run-loop iteration, so a frame that repaints nothing must still
        // leave the naming true.
        app.region_edit_drag.kind = at <= fixed
            ? RegionEditKind::BoundLo : RegionEditKind::BoundHi;
    }
    if (lo == std::min(app.region.a_frame, app.region.b_frame) &&
        hi == std::max(app.region.a_frame, app.region.b_frame)) {
        // Column-change gate: a same-span motion event (sub-pixel jitter inside
        // one column, or a saturated wall) repaints nothing — the region
        // former's own short-circuit.
        return;
    }
    app.region.a_frame = lo;
    app.region.b_frame = hi;
    viewport.invalidate_waveform_area();
}

// THE TOUCH NAVIGATION BODY — two-finger frames and the phone model's
// single-finger pan frames land here alike; contract, the ONE FINGER PANS,
// TWO FINGERS ZOOM ruling, delivery-shape justification and refusal rationale
// at the declaration (input_handler.h). One delivered frame = at most one
// placement through the strip-drag family's own application chokepoint.
void GuiInputHandler::apply_touch_nav_update(const GuiTouchNavFrame& f) {
    // THE PINCH'S SEATED PIVOT IS CLEARED BY ANY FRAME THAT IS NOT TWO-FINGER,
    // and that clear LEADS THE BODY — it is the one thing here that happens
    // above the refusal (contract at TouchNavZoomState, app_state.h). THE TWO
    // HALVES SIT ON OPPOSITE SIDES OF THE REFUSAL DELIBERATELY: SEATING is a
    // navigation act and takes the refusal with everything else (the ordering
    // rule at the seat below), while CLEARING is bookkeeping — a one-finger
    // frame means the two-finger phase is OVER whether or not this frame gets
    // to navigate, and holding the anchor through a refused stretch of the
    // survivor's pan would let a later upgrade zoom about a song frame the
    // fingers had long since left behind. Refusing to navigate is not refusing
    // to notice that the pinch ended.
    // THE CLEAR OWES THE ERASE since the pinch became the anchor stem's third
    // producer (2026-08-14) — and that is exactly why it is a body with an
    // early return rather than an assignment here: this line runs on EVERY
    // one-finger frame, while the stem must be rubbed out once, on the frame
    // the seat actually dies (contract at clear_touch_zoom_seat).
    if (!f.two_finger) clear_touch_zoom_seat(app, viewport);

    // The refusal answer, per frame: the wheel's own routing predicate at the
    // current centroid. <= 0 covers both the modal refusals (-1) and the
    // outside-both-areas 0 that handle_wheel itself no-ops on — the gesture
    // navigates exactly the wheel's two surfaces. A refused frame navigates
    // nothing AND SEATS NOTHING.
    if (wheel_context(f.x, f.y) <= 0) return;
    // AND THE THIN LANES TAKE NO NAV GESTURE AT ALL — the OVERVIEW STRIP and
    // the TRIM BAR (architect 2026-08-15, from the rig: "get rid of all
    // two-finger gestures on the overview strip and on the trim bar; once one
    // finger is down, the second finger is completely ignored, which is what we
    // do with three-finger gestures on the waveform — which makes sense, because
    // the waveform is large and the overview and trim are small"). A lane whose
    // whole vocabulary is precise, thin and absolute has nothing a nav gesture
    // could mean, so THE WAVEFORM'S OWN THIRD-FINGER RULE APPLIES WHERE THE
    // SURFACE IS SMALL: on a large surface a second contact carries a distinct
    // meaning worth admitting, on a 26 px strip it carries nothing the strip's
    // own motions do not already do better. That is a difference in KIND, not an
    // exception to the two-finger model.
    // THE REFUSAL IS WHAT THE BIT IS FOR: without it a gesture begun on a strip
    // would fall THROUGH to the waveform's pinch below and zoom the view from a
    // lane the user was touching for another reason — a thin lane's positive
    // wheel context admits these frames, so nothing above stops them.
    // THE REFUSAL IS HERE AND NOT IN wheel_context BECAUSE THAT PREDICATE IS
    // THE WHEEL'S ROUTING OWNER and the wheel stays LIVE on these lanes: the
    // stepped pan and the ctrl+wheel zoom step both work there and are no part
    // of this, so refusing in the shared predicate would have taken them with
    // it.
    // IT READS THE FIRST FINGER'S DOWN POINT, NOT THE LIVE CENTROID: these
    // lanes are TWENTY-SIX PIXELS TALL and their drags are X-ONLY, so a finger
    // that grabbed a bound wanders vertically well off the strip while still
    // legitimately dragging it, and a second finger landing low would drop the
    // pair's centroid outside the band — a live geometric test cannot keep
    // answering this gesture's own geometry. A GESTURE'S SURFACE IS DECIDED
    // WHERE IT STARTED (the pinch's seat, the press-time act, the mode read at
    // the 8 px crossing all follow that rule), so the answer travels ON THE
    // FRAME, captured once at the `Idle` down and CONSTANT for the contact
    // stream (field contract at GuiTouchNavFrame, gui_input.h) — which is why
    // the refusal cannot change under a live gesture: no frame of a stream can
    // disagree with any other about it. THE MIRROR IS WANTED TOO: a pinch that
    // BEGINS on the waveform keeps the pinch even if its centroid crosses onto a
    // strip.
    // IT NEEDS NO clear_touch_zoom_seat: seating happens BELOW this return and
    // the bit is constant for the stream, so a refused gesture has never seated,
    // and a pinch begun on the waveform carries the bit FALSE and never reaches
    // here. THE ONE-FINGER CLEAR AT THE TOP OF THE BODY STAYS ABOVE THIS RETURN
    // and is deliberately not moved below it: it costs a thin-lane stream
    // nothing (that stream can never hold a seat, so the clear's own early
    // return fires) while every OTHER stream still gets the pinch's end noticed
    // on the frame it actually ends, which is that line's whole contract.
    // IT READS THE BIT ALONE — no two-finger term, and that is not an
    // over-reach. A plain single finger on these lanes never produces a nav
    // frame at all: the touch pan zone is the navigation surface and the lanes
    // are outside it, so one finger there resolves to the pointer translation
    // and the lanes' own drags. The only one-finger frames that CAN carry the
    // bit are DOWNGRADE SURVIVORS — a pair that landed on a lane and then lost a
    // finger — and those are exactly what must not pan. (The first shape of this
    // refusal was gated on `two_finger` as well and let precisely that survivor
    // through: the bit true, the count false, the test asking the wrong
    // question.) A live phone-model pan whose finger has drifted onto a lane is
    // untouched, and by the bit rather than by the count — it began in the pan
    // zone, so it carries the bit FALSE for its whole life.
    if (f.down_on_thin_lane) return;
    // Defensive only: the platform guarantees a positive ratio (a degenerate
    // finger distance delivers 1.0).
    double dist_ratio = f.dist_ratio;
    if (!(dist_ratio > 0.0)) dist_ratio = 1.0;

    // ONE FINGER PANS, TWO FINGERS ZOOM — architect 2026-08-14, the whole
    // gesture model on glass (the ruling and its friction argument at the
    // declaration; touch.md's two-finger section is authoritative). The two
    // terms are NEVER both live: a two-finger frame's centroid travel is
    // discarded outright, which is what kills the accordion, and a
    // single-finger frame carries no distance to zoom by. The one-finger
    // side's 1.0 RESTATES the model rather than guarding — the platform
    // already pins the ratio there, one finger having no finger gap.
    const double eff_dx    = f.two_finger ? 0.0 : f.dx;
    const double eff_ratio = f.two_finger ? dist_ratio : 1.0;

    // The geometry the pivot is measured in, HOISTED ABOVE THE NO-OP RETURN
    // for the seat below: cheap reads, and the guard is a validity gate (there
    // is no waveform to anchor a pivot in) rather than a policy about this
    // frame's deltas.
    const GuiRect wf_area = waveform_area(app);
    const double  W       = static_cast<double>(wf_area.w);
    const int64_t total   = live_total_frames(app, audio);
    if (W <= 0.0 || total <= 0) return;

    const double spp_old = current_samples_per_pixel(app, audio);
    const double vp      = static_cast<double>(app.viewport_start_sample);

    // THE SEAT — TAKEN THE MOMENT THE PINCH REGISTERS, NOT WHEN THE FINGER GAP
    // FIRST CHANGES (architect 2026-08-14, from the rig: "when the two-finger
    // touch is first registered, it picks the point on the waveform"). ITS
    // ORDERING RULE, the matched half of the clear's at the top of the body:
    // the seat sits ABOVE the exact-no-op return and BELOW the wheel refusal.
    // Seating is a NAVIGATION act, so it takes the refusal with everything else
    // — a pinch beginning off the wheel's surfaces anchors nothing, and the
    // gesture keeps its "it navigates exactly the wheel's two surfaces"
    // property. But "this frame's deltas apply nothing" is a statement about
    // the FRAME and says nothing about where the GESTURE is anchored, and
    // letting it decide was a real defect: the zoom-only ruling forces eff_dx to
    // a literal zero on every two-finger frame, so a pair landing and sliding
    // TOGETHER — pure centroid travel, gap unchanged — met the no-op return and
    // died above the seat. The pivot was then in truth taken at the first frame
    // whose finger DISTANCE changed, at whatever column the centroid had
    // drifted to by then rather than at the point the fingers grabbed, and no
    // stem appeared until then either. Nothing about the seat's VALUE changed
    // here — only when it is taken.
    if (f.two_finger && !app.touch_nav_zoom.seated) {
        TouchNavZoomState& z = app.touch_nav_zoom;
        z.anchor_sample = vp + static_cast<double>(f.x) * spp_old;
        z.seated        = true;
        // THE SEAT OWES ITS FIRST FRAME'S DAMAGE, which is the mouse arm's own
        // rule (arm_nav_zoom_press) reaching the pinch —
        // the seat is the anchor stem's gate since 2026-08-14
        // (paint_strip_drag_anchor, paint_handler.cpp) and it is NOT free. A
        // seating frame is not even an APPLIED frame any more (it is exactly
        // the centroid-only frame above that this ordering rescued), and even
        // where it is, apply_strip_drag_zoom's own MID-GESTURE TRUE-NO-OP
        // return drops any frame whose post-clamp level AND viewport both
        // stand — every frame of a pinch that begins saturated at a wall
        // (pinching further out at full zoom-out, or further in at kMinZoom),
        // which is precisely the edge the stem was asked for. Without this line
        // such a pinch would show no stem until it turned around. Once per
        // phase, and it merges with the apply's own damage on every frame that
        // does move.
        viewport.invalidate_waveform_area();
    }

    // A frame whose surviving delta is an exact no-op applies nothing: the
    // platform suppresses only frames where BOTH raw deltas are no-ops, so a
    // two-finger frame carrying pure centroid travel arrives here and dies
    // here — having seated the pivot on its way past, which is the whole of
    // the ordering above. Below the seat, above the double-click clear (the C8
    // rule covers APPLIED frames).
    if (eff_dx == 0.0 && eff_ratio == 1.0) return;

    // An applied navigation frame moves content between two taps, so a
    // pending double-click candidate must not survive it (the C8 rule the
    // wheel applies at on_wheel's top). It stays BELOW the no-op return
    // deliberately — a frame that applies nothing must not consume a pending
    // candidate — which is why the seat was hoisted around it rather than the
    // return moved.
    app.double_click = DoubleClickCandidate{};

    // THE PIVOT, and the two finger counts answer it DIFFERENTLY since
    // 2026-08-14 (the seated pinch; contract at TouchNavZoomState,
    // app_state.h).
    double anchor_sample = 0.0;   // active-domain song frame the pivot holds
    double anchor_col    = 0.0;   // its column under the LIVE viewport
    if (!f.two_finger) {
        // ONE FINGER — the phone model's pan, unchanged and stateless: the
        // content under the PREVIOUS centroid column (x - eff_dx) is what the
        // finger holds, placed at the CURRENT centroid. The anchor column
        // convention is the mouse zoom's own (window x against the live
        // viewport — the waveform starts at the window edge), and no clamp is
        // needed on a pan: nothing persists between frames for an off-area
        // column to corrupt, and the placement runs through the viewport
        // chokepoint's own clamps either way. The seat is already cleared at
        // the top of the body — which is what makes the DOWNGRADE clean: a
        // finger lifting from the pair continues as this pan, and the next
        // upgrade takes a FRESH pivot rather than inheriting the dead pinch's.
        anchor_sample = vp + (static_cast<double>(f.x) - eff_dx) * spp_old;
        anchor_col    = static_cast<double>(f.x);
    } else {
        // TWO FINGERS — THE PINCH'S PIVOT IS THE POINT ON THE WAVEFORM THE
        // GESTURE GRABBED, held for the phase's life: seated above (on the
        // phase's FIRST unrefused frame, whether or not that frame applies
        // anything), then re-derived as a COLUMN against the live viewport
        // every frame here. The centroid's own travel is discarded by the fork
        // above (eff_dx is 0), so moving both fingers together still applies
        // nothing — it only seats.
        TouchNavZoomState& z = app.touch_nav_zoom;
        // THE EDGE TRICK, apply_nav_zoom_at's pivot block mirrored. The
        // stateless model deliberately did without a clamp because there was no
        // persistent anchor for an off-area column to corrupt; there is one
        // now, so that sentence is superseded: a column pushed outside [0, W-1]
        // pins
        // at the edge pixel and REBINDS the held frame to that pixel's
        // content, which is what keeps the zoom's focus on screen exactly as it
        // does for the mouse.
        anchor_col = (z.anchor_sample - vp) / spp_old;
        const double clamped = clamp_col_into_waveform(wf_area, anchor_col);
        if (clamped != anchor_col) {
            z.anchor_sample = vp + clamped * spp_old;
            anchor_col      = clamped;
        }
        anchor_sample = z.anchor_sample;
    }

    // The distance ratio maps to the level LOGARITHMICALLY — spreading the
    // fingers by 2x is one level in (spp halves, so the content between the
    // fingers scales with the finger gap; no feel constant). Pre-clamped into
    // the same [kMinZoom, effective ceiling] window clamp_viewport_start
    // re-applies, exactly as every other caller pre-clamps — the chokepoint's
    // level_changed compare requires a real request (its contract names the
    // callers).
    double new_level = app.zoom_level - std::log2(eff_ratio);
    const double max_l =
        effective_max_zoom_level(W, total, audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // ONE placement carries whichever axis is live, and the fork above decided
    // both of its anchor terms: a ONE-FINGER frame places the content under the
    // previous centroid at the CURRENT centroid column, which is the pan, while
    // a TWO-FINGER frame places the HELD frame back at its own re-derived
    // column, which is a pure zoom about the grabbed point (the ratio is 1.0 on
    // the first and the centroid delta is 0 on the second, so the off term is a
    // literal no-op either way). Everything downstream is the strip drag's own
    // — level clamp, viewport clamp, the synchronous per-frame rebuild, the
    // either-axis follow suppression, and the mid-gesture true-no-op skip.
    viewport.apply_strip_drag_zoom(new_level, anchor_sample, anchor_col,
                                   /*final=*/false);
}

// THE SEATED PINCH'S CLEAR AND ITS ERASE (contract at the declaration,
// input_handler.h): the early return is what makes the damage fire exactly
// once per phase, and the damage is owed because a clear can land on a frame
// that applies nothing and so rebuilds nothing.
void clear_touch_zoom_seat(AppState& app, Viewport& viewport) {
    if (!app.touch_nav_zoom.seated) return;
    app.touch_nav_zoom = TouchNavZoomState{};
    viewport.invalidate_waveform_area();
}

void GuiInputHandler::end_touch_nav() {
    // Any end commits, and every applied frame already rebuilt synchronously;
    // the one deferred piece is the playback predictor (mid-gesture frames
    // skip the resync exactly as the strip drag's do) — the grab-pan release's
    // own tail.
    // AND THE PINCH'S SEATED PIVOT IS CLEARED HERE, the gesture's one GUI-side
    // record since 2026-08-14 (TouchNavZoomState, app_state.h — the old "every
    // frame is applied whole and forgotten" is retired with it). Every end
    // reaches this one body — a finger lift, wl_touch.cancel and
    // touch-capability loss alike — so no later gesture can inherit a dead
    // pinch's anchor; a fresh pair seats its own. It goes through
    // clear_touch_zoom_seat because the clear owes the STEM'S ERASE: an end
    // rebuilds nothing of its own, so without the damage a hard end would
    // leave the pivot mark painted over a settled view.
    clear_touch_zoom_seat(app, viewport);
    if (playback.is_playing()) playback.resync_predictor();
}

// The pan-zone query's body (contract at the declaration): THE NAVIGATION
// SURFACE, and since 2026-08-13 it DERIVES rather than restates — one call to
// the surface's own geometry owner, point_on_nav_surface, which is the same
// predicate the press router, the ctrl claim and the cursor map read. Until
// that ruling this body was a HAND COPY of the press router's derivation (its
// own inside_waveform spelling plus its own `!waveform_lower_half` term), so
// the lower half's arrival on the zone did NOT follow for free — it followed
// once the copy became a call. Surface geometry only; refusals stay downstream
// (the update body's per-frame wheel_context answer, the region begin's gate
// list).
//
// SO THE WHOLE WAVEFORM IS THE PAN ZONE NOW (architect 2026-08-13, embracing
// the consequence: "currently finger down works in the upper half by waiting
// for the finger up, but on the lower half it immediately dispatches the
// scanner — so there's an asymmetry, and now that I understand it we should
// eliminate the asymmetry"). A one-finger drag pans anywhere on the waveform,
// the ~500 ms region hold reaches anywhere on it, and a motionless tap on the
// lower half is the tap-at-lift burst whose motionless press-release IS the
// deferred scrub act — the mouse's own machinery, inherited with no touch code.
bool GuiInputHandler::touch_point_in_pan_zone(int x, int y) const {
    // AND THE ZONE YIELDS INSIDE A LIVE REGION (2026-08-15). Without this the
    // feature would be unreachable on the exact surface it exists for: the
    // whole waveform is the pan zone, so a finger landing inside a region would
    // become the phone-model pan and NEVER DELIVER A PRESS, and the region's
    // move / bound drags live on the pointer. Answering false here lets the
    // finger resolve to the pointer translation and reach them — the flag box
    // carve-out's exact shape, one level up.
    // ONE SPELLING OF "INSIDE A LIVE REGION": this asks the same owner the
    // mouse press claim and the cursor map ask, so the three cannot disagree
    // (that owner is also what keeps the RULER and the MARKER LANE out of it —
    // they answer None there, so a region covering their columns still pans).
    // THE REGION HOLD'S ANSWER IS RECORDED AT THE DECLARATION and is accepted:
    // the ~500 ms hold is a pan-zone gesture, so a hold INSIDE an existing
    // region no longer begins a new one — a hold started outside it, or a tap
    // to clear it first, both still do.
    if (region_manipulation_hit(x, y) != RegionHit::None) return false;
    return point_on_nav_surface(app, audio, x, y);
}

// The thin-lane query's body (contract at the declaration): the two member
// lanes' own rects and nothing else — the same rectangles their press routers,
// their cursor arms and their wheel contexts read, so there is one answer to
// "is this the overview strip" and one to "is this the trim bar", and this
// query is not a second spelling of either.
//
// WHAT MAKES A LANE A MEMBER, so the next one joins on a rule rather than a
// hunch: it is THIN (~26 px, a band a fingertip covers whole), and its whole
// vocabulary is its OWN precise, ABSOLUTE drags — a bound or an edge follows the
// finger to a column, one bound moving while its partner holds. Two members
// today, and they are the same shape twice over: THE OVERVIEW STRIP
// (top_overview_row_area — the box's two edge handles, the box pan, the
// click-teleport) and THE TRIM BAR (top_trim_row_area — the two endcaps, the
// bridge, the framing double-click). On a surface like that a second contact has
// nothing to mean: everything a nav gesture could offer, the lane's own motions
// already do better and more precisely. The WAVEFORM is deliberately not a
// member — it is large, so a second contact there carries a distinct meaning
// (the pinch) worth admitting, which is the same reasoning read the other way.
//
// Surface geometry only; nothing here decides a gesture. The answer is captured
// once at the first finger's down and carried onto every nav frame, where its
// two readers refuse — the GUI's own apply_touch_nav_update drops every frame
// carrying it, and the platform's second-finger fork ignores the second contact
// outright.
bool GuiInputHandler::touch_point_on_thin_lane(int x, int y) const {
    return rect_contains(top_overview_row_area(app), x, y) ||
           rect_contains(top_trim_row_area(app), x, y);
}

// --- The touch region former (the hold on the pan zone) --------------------
//
// Pan-primary's touch half (architect 2026-08-12, the eighth glass ruling) —
// the full contract, the refusal list and the accepted cross-device edge are
// at the declarations (input_handler.h). These three ARE the one region
// former's machinery driven from the platform's region hooks: no pending and
// no threshold wait at the begin — the ~500 ms hold already disambiguated,
// so the begin runs the former's press half directly — and no second former
// anywhere.

void GuiInputHandler::begin_touch_region(int x, int y) {
    // The press path's own gates, restated because this gesture never passes
    // through on_button_press (the shift former's claim sits below every one
    // of these): a prompt or modal editor owns the input, an open dropdown
    // owns the pointer, unloaded audio has no columns to span, and a live
    // pointer gesture must not be torn by a second writer. THE EDITOR GATE
    // IS THE FIVE-EDITOR PREDICATE, the flag editor deliberately included
    // though it is pointer-transparent: every pointer press CLOSES an open
    // flag editor before any claim runs, so no region gesture can begin
    // under one — and this begin, which skips the press path, must not
    // become the first (the declaration carries the full argument). The `h`
    // HISTORY VIEW IS DELIBERATELY NOT REFUSED — unlike the dead trim-move
    // begin's list — because the view ADMITS the region former as its own
    // view-local vocabulary (its shift former), so the begin FORKS on the
    // mode below exactly as the shift press forks at its two claims. A
    // refused begin arms nothing — the update/end hooks then no-op on the
    // drag's own !active guard, so the refused stream is dead rather than a
    // fallback pointer drag (the pan gestures' model).
    if (app.prompt.active) return;
    if (keyboard_modal_editor_active()) return;
    if (app.dropdown.open()) return;
    if (app.loading || audio.total_frames() <= 0) return;
    if (any_pointer_gesture_active(app)) return;
    const GuiRect area = waveform_area(app);
    if (app.history_mode.active) {
        // THE VIEW-LOCAL FORMER — handle_history_mode_press's shift arm
        // re-expressed at the down point: clear the mode focus + selection
        // (the pair clearer, the deselect's mode analog — the family rule at
        // RegionState), seat the playhead at the down column through the
        // shared placement body, arm the drag. Store selection untouched,
        // the view's own standing rule.
        if (clear_history_mode_focus(app.history_mode)) {
            // A discrete command: full-window damage for the face swap, the
            // shift arm's own shape.
            viewport.invalidate_all();
        }
        const int64_t sample = place_playhead_at_click_column(
            x - area.x, playback.is_playing(), app.playhead_cursor_sample);
        if (sample >= 0) arm_region_drag_at(sample, x, y);
        return;
    }
    // THE LIVE FORMER — the shift press's own body whole (deselect-all,
    // playhead at the down column, live-session reseek, dissolve-at-arm, the
    // drag arm). The playback readings are taken here at the begin, the
    // formers' press-entry capture (the placement body's contract).
    place_playhead_and_arm_region(x - area.x, x, y, playback.is_playing(),
                                  app.playhead_cursor_sample);
}

void GuiInputHandler::update_touch_region(int x, int y) {
    // The drag's ONE motion path (apply_region_drag_motion): the shared
    // Chebyshev gate from the down point, the span extension, the playhead
    // riding the moving end. Self-guarded on the drag's own active bit,
    // which is what makes a refused begin's stream free — and what covers
    // the accepted cross-device edge (a mouse end mid-gesture cleared it).
    if (!app.region_drag.active) return;
    apply_region_drag_motion(x, y);
}

void GuiInputHandler::end_touch_region() {
    // The release path's own body (any end commits — finger up,
    // wl_touch.cancel, capability loss; the platform's end split delivers or
    // drops the staged final frame, its record): a moved drag rests the span
    // under the sliver gate; a MOTIONLESS end rests nothing — the begin's
    // arm dissolved at the down — and leaves the playhead where the begin
    // seated it, the former's motionless-release rule, which is what makes a
    // long-press-then-lift a placement. Self-guarded like the update.
    if (!app.region_drag.active) return;
    const bool moved = app.region_drag.moved;
    app.region_drag = RegionDragState{};
    if (moved) end_region_drag_min_size_check(app, audio, viewport);
}

// The flag editor's guard-free close — the LEFT press's, its one caller since
// the right button's unbinding (2026-08-12; contract at the declaration,
// input_handler.h). The box is the painter's
// published rect, the same one the F2.1 caret block tests, so "outside" means
// the same thing on every press path.
void GuiInputHandler::close_top_flag_editor_for_outside_press(int x, int y) {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (rect_contains(app.flag_editor_box.box, x, y)) return;
    flag_editor.exit_top_flag_edit_no_commit();
}

// -- The modal dialog's pointer half (2026-08-12) ---------------------------
//
// The dialog itself is the painter's (paint_modal_dialog, which publishes
// AppState::modal_dialog every frame); these three are the pointer's readers
// and the buttons' dispatch. The full veil contract is at the press gates in
// on_button_press.

// The dialog button under (x, y), or -1 — the painter's stash, the roster
// model: a zero/invalid stash contains no point, the correct cold answer.
int GuiInputHandler::modal_dialog_button_hit(int x, int y) const {
    if (!app.modal_dialog.valid) return -1;
    for (size_t i = 0; i < app.modal_dialog.buttons.size(); ++i) {
        if (rect_contains(app.modal_dialog.buttons[i].rect, x, y))
            return static_cast<int>(i);
    }
    return -1;
}

// The dialog buttons' hover face — the pointer fact, written on every motion
// under a standing dialog; a change damages the stashed box (the painter
// reads the index back). The index resets with the stash in
// paint_modal_dialog's no-dialog arm, so it cannot go stale across dialogs.
//
// IT ALSO TRACKS AN ARMED BUTTON — THE FEINT (architect 2026-08-13,
// SUPERSEDING this walk's own "sliding off cancels, and sliding back on does
// NOT re-arm" rule of hours earlier): "if the user feints — clicks a button
// and then drags away before the mouse goes up — then that button receives the
// passive focus as well." So THE ARM STAYS LIVE FOR THE WHOLE HOLD and this
// walk only answers whether the pointer is inside it, which is what makes
// sliding back on restore the pressed face and its release commit — nothing
// was cancelled, so nothing has to be re-armed. Leaving the button is what
// assigns the PASSIVE FOCUS the ruling gives the feint, and the face that
// results (the focus fill under an accent outline, no halo) is the ladder's
// own composition rather than a case (paint_modal_dialog). The whole rule and
// the pair's read-as-one-fact contract are at AppState::modal_dialog_pressed.
void GuiInputHandler::update_modal_dialog_hover(int x, int y) {
    const int hit = modal_dialog_button_hit(x, y);
    // THE FIELD'S OWN HOVER FACE rides this same walk (2026-08-13, when the
    // field took the buttons' outline on hover and focus): one pointer fact
    // resolved beside the buttons', against the painter's published field
    // rect, damaging the same stashed box on the same transition. A prompt
    // publishes a zero field, so this is false there without a term.
    const bool in_field =
        app.modal_dialog.valid &&
        rect_contains(app.modal_dialog.field, x, y);
    const int  armed  = app.modal_dialog_pressed;
    const bool inside = armed >= 0 && armed == hit;
    // THE FEINT'S ASSIGNMENT, on the leave edge alone: the pointer has left the
    // button it armed while still holding it. It REPLACES whatever focus the
    // dialog had, of either strength, and it is PASSIVE — the user pointed at
    // this button, which is not the deliberate keyboard walk that earns the
    // active face.
    const bool feint = armed >= 0 && !inside &&
                       (app.modal_dialog_focus != armed ||
                        app.modal_dialog_focus_active);
    if (app.modal_dialog_hovered != hit || feint ||
        app.modal_dialog_press_inside != inside ||
        app.modal_dialog_field_hovered != in_field) {
        app.modal_dialog_hovered       = hit;
        app.modal_dialog_field_hovered = in_field;
        app.modal_dialog_press_inside  = inside;
        if (feint) {
            // MOVING THE FOCUS CANCELS THE KEYBOARD ARM, the rule's second
            // site (AppState::modal_dialog_key_pressed): the two arms can
            // stand together — a feint held with the mouse while the keyboard
            // presses the focused button — and this assignment takes the focus
            // off whatever the keyboard was holding, so that hold's release
            // must commit nothing.
            clear_modal_dialog_key_press();
            app.modal_dialog_focus        = armed;
            app.modal_dialog_focus_active = false;
        }
        if (app.modal_dialog.valid)
            viewport.invalidate_rect(app.modal_dialog.box);
    }
    // AND IT OWNS THIS SURFACE'S TOOLTIP DWELL (2026-08-13, when the modal
    // buttons took hints instead of bracketed accelerators): the same helper
    // and the same 700ms tick the roster's walk uses, keyed on the Dialog half
    // of the owner's index space. EVERY dialog button has a hint, so there is
    // no membership term here — the hit alone decides, and the painter's stash
    // carries the text.
    // IT IS INDEPENDENT OF THE PRESS ARM ABOVE: the dwell answers where the
    // pointer IS, so a held button keeps its hint running exactly as a hovered
    // one does, and losing the arm by sliding off is the same motion that
    // re-keys the dwell onto whatever is under the pointer now.
    arm_tooltip_dwell({AppState::RedesignTooltip::Surface::Dialog, hit});
}

// THE ARM'S HARD END — the pointer-leave / capability-loss hook (main.cpp),
// beside the roster's own clear_redesign_button_press and for its reason: a
// pointer that has left the window is on no button, and an act that has not
// happened yet must not be left waiting for a release that may never come.
// It deliberately does NOT clear the hovered index: a stale lit face is the
// standing accepted cost there (the next delivered motion re-runs the walk),
// while a stale ARM is a pending act. Transition-gated, damaging the stashed
// box when it fires.
void GuiInputHandler::clear_modal_dialog_press() {
    if (app.modal_dialog_pressed < 0) return;
    app.modal_dialog_pressed      = -1;
    app.modal_dialog_press_inside = false;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// THE KEYBOARD ARM'S HARD END, the twin of the one above and on the twin edge:
// the platform's keyboard-intent cancellation (keyboard leave, keyboard-
// capability loss, a Super-swallowed press — the fire classes are at
// set_keyboard_intent_cancel_hook, platform_wayland.h). The release this arm
// waits for can never be delivered across those edges, and an act that has not
// happened yet must not be left waiting for it. Transition-gated, damaging the
// stashed box when it fires; the contract is at
// AppState::modal_dialog_key_pressed.
void GuiInputHandler::clear_modal_dialog_key_press() {
    if (app.modal_dialog_key_pressed < 0) return;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// A dialog button's PRESS: arm the index and paint it, dispatching nothing.
// Shared by the prompt claim and the editor claim — the two differ in what
// their RELEASE runs, not in what their press does. Returns true when a
// button was armed (the claim then consumes the press; the veil consumes it
// either way).
bool GuiInputHandler::arm_modal_dialog_press(int x, int y) {
    const int hit = modal_dialog_button_hit(x, y);
    if (hit < 0) return false;
    if (app.modal_dialog_pressed != hit || !app.modal_dialog_press_inside) {
        app.modal_dialog_pressed = hit;
        // A press is inside what it hit, by construction — the feint's bit
        // starts true and only the hover walk can turn it over.
        app.modal_dialog_press_inside = true;
        viewport.invalidate_rect(app.modal_dialog.box);
    }
    return true;
}

// A dialog button's RELEASE: the act runs iff the lift lands on the SAME
// button the press armed — which, since the FEINT made the arm survive the
// pointer wandering off, is exactly the `press_inside` bit the hover walk has
// been maintaining. Returns that button's index, or -1 — the caller owns the
// dispatch, because a prompt's buttons and an editor's mean different things.
// The arm is consumed either way: a release ends the hold whatever it lands
// on, and a release AWAY from the armed button leaves it passively focused,
// which the walk already assigned when the pointer left it.
int GuiInputHandler::take_modal_dialog_release(int x, int y) {
    const int armed = app.modal_dialog_pressed;
    if (armed < 0) return -1;
    app.modal_dialog_pressed      = -1;
    app.modal_dialog_press_inside = false;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
    return modal_dialog_button_hit(x, y) == armed ? armed : -1;
}

// DOES THE PUBLISHED STASH NAME THE SURFACE THAT OWNS INPUT RIGHT NOW — the
// ONE comparison behind the doctrine at AppState::ModalDialogGeometry
// (published geometry may only SELECT; live state DECIDES), and the one owner
// of it: every site that reads the stash to act asks this and nothing spells
// it a second time (the two press claims, the focus ring's route, and the
// shared act below).
//
// TWO TERMS, ONE QUESTION. The OWNER TAG is the class the painter drew and is
// the cheap first refusal; THE SESSION IS THE EXACT ONE — an id names one raise
// of one surface for the life of the program (text_editor::next_session_id), so
// it answers what the tag cannot: a dialog EDITOR replaced by another dialog
// editor inside a single dispatch batch, which wears the same tag. A zero
// session is an unpublished stash and can never match.
bool GuiInputHandler::modal_dialog_stash_current() const {
    const AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    if (!dlg.valid || dlg.session == 0) return false;
    if (dlg.owner != (app.prompt.active ? AppState::ModalDialogOwner::Prompt
                                        : AppState::ModalDialogOwner::Editor)) {
        return false;
    }
    return dlg.session == app.modal_dialog_live_session();
}

// THE FOCUS RING'S LIVE READING — AppState::modal_dialog_focus, or -1 whenever
// the stash it indexes is not the live surface's. The index names a slot in the
// painter's published button list, so between a raise and its first paint it
// names the PREVIOUS dialog's buttons; every keyboard site that asks "is the
// focus on a button or in the field" reads it through here, so a stale index
// cannot swallow the new editor's first keystrokes. (The painter's own reset
// clears the field a frame later, which is too late for a queued burst.)
int GuiInputHandler::modal_dialog_focus_live() const {
    return modal_dialog_stash_current() ? app.modal_dialog_focus : -1;
}

// THE ACT ITSELF, and THE ONE PLACE THE TWO GATES ARE RE-ASKED — hoisted
// 2026-08-13 when the KEYBOARD grew a release of its own (Enter and Space on a
// focused button act at the lift, exactly as the pointer's press does), so the
// pointer's two release arms and the keyboard's one share a single body
// instead of the gate pair being spelled a third time.
//
// PUBLISHED GEOMETRY MAY ONLY SELECT; LIVE STATE DECIDES (the doctrine at
// ModalDialogGeometry, app_state.h). `index` names a slot in the painter's
// stash, and everything that DECIDES is read live here:
//   THE STASH MUST BE THE LIVE SURFACE'S — modal_dialog_stash_current above,
//   which is class AND session, so a stash a prompt painted never answers an
//   editor, a stash the PREVIOUS editor painted never answers the one that
//   replaced it, and an unpublished stash answers nothing;
//   PromptState::painted must stand for a prompt — an arm outlives its press
//   by definition and the surface under it may have been replaced (the
//   save-failed rung leaves live-keyed rects where the new box is not);
//   the RESPONSE KEY the stash names is validated against the LIVE response
//   set, a different question from the stash's and asked separately.
// Returns true iff something dispatched, so a caller can tell a consumed
// nothing from an act.
bool GuiInputHandler::dispatch_modal_dialog_button(int index) {
    const AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    if (index < 0 || index >= static_cast<int>(dlg.buttons.size()))
        return false;
    if (!modal_dialog_stash_current()) return false;
    const AppState::ModalDialogButton& b =
        dlg.buttons[static_cast<size_t>(index)];
    if (app.prompt.active) {
        if (!app.prompt.painted) return false;
        for (char live : app.prompt.response_keys) {
            if (b.response_key != 0 && b.response_key == live) {
                prompt.activate_response(b.response_key);
                return true;
            }
        }
        return false;
    }
    dispatch_modal_dialog_editor_act(b.editor_ok);
    return true;
}

// An editor dialog's OK / Cancel press, dispatched as the session's own
// Enter / Esc through the SAME per-editor key route the keyboard takes
// (handle_*_editor_key -> route_modal_editor_key) — button-is-its-chord, so
// the commit bodies, the red-flash refusals, the BPM sweep and the teardowns
// are all the keyboard's own, byte-identical. Bare mods: the session keys are
// bare-exact by the strict-modifier rule, and the only gesture that reaches
// here is a plain press's own lift (the press claim refuses every modifier and
// the release simply finishes what it armed). The focus ring's Enter reaches
// it too, for the same reason and with the same bareness. The editor fork
// mirrors the painter's precedence order,
// though only one dialog editor can be open at a time (each opener refuses
// while any editor owns the keyboard), so the order is free.
//
// THE FOCUS RETURNS TO THE FIELD FIRST, and it MUST: the key this synthesizes
// is the FIELD'S key, and route_modal_editor_key offers every key to the focus
// ring before the field sees it. With the focus left on a BUTTON the ring
// would claim the synthesized Return as that button's own press — the act
// feeding itself back into the surface that raised it — so a keyboard OK could
// never commit. (Before this line the ring ACTIVATED at the press and the same
// loop was unbounded recursion; it never fired in practice because the pointer
// path reaches here with the focus in the field, but the keyboard's Enter on an
// editor dialog's OK button had no other end. Found and closed 2026-08-13 with
// the act-at-release ruling.) Setting the field is also the right RESTING state
// for the one act that does not close the dialog — a red-flash refusal leaves
// the user where the fix is typed — so this is the act's own semantics rather
// than a workaround for the ordering.
void GuiInputHandler::dispatch_modal_dialog_editor_act(bool ok) {
    const GuiKey        key = ok ? GuiKeys::Return : GuiKeys::Escape;
    const GuiInputState mods{};
    if (app.modal_dialog_focus >= 0) {
        // MOVING THE FOCUS CANCELS THE KEYBOARD ARM, the rule's third site
        // (AppState::modal_dialog_key_pressed): the arm names the button the
        // focus was on, and the focus is going back to the field. The
        // keyboard's own release has already consumed its arm before reaching
        // here, so this is the POINTER path's due — a click on OK while Enter
        // is held down on a focused button must not leave that Enter able to
        // fire a second act at its release.
        clear_modal_dialog_key_press();
        app.modal_dialog_focus        = -1;
        app.modal_dialog_focus_active = false;
        if (app.modal_dialog.valid)
            viewport.invalidate_rect(app.modal_dialog.box);
    }
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        handle_top_flag_editor_key(key, mods);
    } else if (text_editor::is_active(app.settings_editor)) {
        handle_settings_editor_key(key, mods);
    } else if (text_editor::is_active(app.load_editor)) {
        handle_load_editor_key(key, mods);
    } else if (text_editor::is_active(app.commit_title_editor)) {
        handle_commit_title_editor_key(key, mods);
    }
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

// ARM THE MARKER FLAG'S PENDING CLICK — the ONE arm for all three of the flag
// box's presses (the ctrl-exact toggle, the shift range, the plain
// single-select), so no future shape can arm a fourth way. It writes the
// pending and does NOTHING ELSE: the whole click is the lift's
// (run_marker_click_act below; the contract is at PendingMarkerPress,
// app_state.h).
//
// UNCONDITIONAL BY SHAPE. The two authoring gates the plain press used to arm
// behind — read-only and active_column_authoring_allowed — guard the DRAG, not
// the click, and live at the crossing (on_motion): a locked tab and an off-home
// column still select, still land the playhead and still open no editor, which
// is read_only_key_blocked's own ruling (read-only protects the AUTHORED
// MUSICAL CONTENT — the marker stores and the engine settings — and a selection
// is navigation).
//
// THE DOUBLE-CLICK VERDICT IS DECIDED HERE, at the press, and only for the
// PLAIN shape (the two modified clicks have never had a double-click meaning).
// Deciding it at the press is not a press-time ACT — it commits nothing and
// changes no state; it records what the user's second press MEANT, at the
// moment the timing test is about, which the lift can no longer ask because
// on_button_press's own top-of-frame clear has already emptied the field. What
// the verdict then meets — the P view, read-only and the home column — is
// re-asked LIVE at the lift, the chrome lift's rule: a gate may change under a
// held button and the LIFT decides.
void GuiInputHandler::arm_marker_press(int hit, int x, int y, bool shift,
                                       bool ctrl,
                                       const DoubleClickCandidate& dc_at_press) {
    const bool plain = !shift && !ctrl;
    app.pending_marker_press = PendingMarkerPress{};
    app.pending_marker_press.active  = true;
    app.pending_marker_press.marker  = hit;
    app.pending_marker_press.press_x = x;
    app.pending_marker_press.press_y = y;
    app.pending_marker_press.shift   = shift;
    app.pending_marker_press.ctrl    = ctrl;
    app.pending_marker_press.double_click_consume =
        plain &&
        dc_at_press.surface == DoubleClickSurface::Marker &&
        dc_at_press.target == hit &&
        monotonic_ms() - dc_at_press.time_ms <= kDoubleClickMs &&
        std::abs(x - dc_at_press.press_x) <= kDoubleClickSlackPx &&
        std::abs(y - dc_at_press.press_y) <= kDoubleClickSlackPx;
}

// THE MARKER CLICK ACT — the whole of what a flag press used to commit at press
// time, in one owner with TWO call sites: the MOTIONLESS RELEASE runs it with
// `at_lift` true, and the plain arm's THRESHOLD CROSSING runs it with `at_lift`
// false, which drops only the double-click half (a drag is not a click of a
// double-click). Everything else — the stop, the three-way fork, the land and
// the region clear — is identical on both, which is what makes a drag's
// user-visible outcome byte-identical to the press-time model's.
//
// THE PENDING IS TAKEN BY VALUE so the callers can follow the release bodies'
// STANDING SHAPE — read the fields, DISARM the state, and only then act, so the
// act runs with no gesture live (ScrollDragState's and the overview lane's
// releases both say so at their sites). A reference into a field the caller is
// about to zero would make that shape unwritable.
//
// THE ARMED MARKER INDEX IS THE SUBJECT AND NOTHING IS RE-HIT-TESTED HERE. This
// is where the flag deliberately differs from the chrome roster, whose lift
// re-hits its target's published rect, and there are three independent reasons:
//   * THE TOUCH LAYER delivers the synthesized PRESS at the finger's DOWN point
//     and the release at its last position, so a re-hit would resolve a
//     different flag — or none — for a finger that drifted inside the 8px slop;
//   * THE FLAG HIT STASH (AppState::flag_hit_rects) may legitimately be
//     REPUBLISHED between press and release: the async worker's freeze list is
//     `app.drag.active || app.trim_drag.active || app.region_edit_drag.active`
//     and does not name this pending, so the basis under a held flag press is
//     not frozen;
//   * THE PRESS POINT IS WHAT THE USER AIMED AT — the navigation surface's
//     deferred click and the overview lane's teleport both act at the PRESS
//     COLUMN for exactly this reason, sub-threshold travel being jitter.
// A chrome button's rect is static, published by the roster, and its arm has no
// travel threshold at all; a flag's is neither.
//
// THE INDEX CANNOT GO STALE UNDER THE HELD BUTTON. The drag-modal gate
// (input_handler.cpp) swallows every key while this pending stands — Ctrl+Q
// hatched, and that route force-ends the pending before it does anything else —
// no pointer route can run with the button down, and no worker mutates a marker
// store. The mutators' own index guards (land_playhead_on_marker's store range
// test, the selection ops' idx < 0 returns, begin_drag's count test) are the
// backstop; nothing here re-derives.
void GuiInputHandler::run_marker_click_act(PendingMarkerPress press,
                                           bool at_lift) {
    if (press.marker < 0) return;
    // The stop leads on every shape, as it did at the press: selecting or
    // editing under a live audition is the case the top-strip stop exists for,
    // and no arm below refuses (read-only still selects and lands, and the
    // index came from a live hit test).
    playback_lifecycle.stop_playback_if_playing();
    if (press.ctrl) {
        // The individual membership TOGGLE. Whether it ADDED or REMOVED, the
        // playhead lands on the FOCUS the toggle leaves behind (architect
        // 2026-07-28, replacing the earliest-selected land): an ADD focuses the
        // clicked marker, a REMOVE of the focused member repairs the focus to
        // the largest remaining index, and a REMOVE of any other member leaves
        // the focus alone — so app.last_selected_marker is the one expression
        // for all three, and it is always a live member on a non-empty
        // selection.
        selection.toggle_selection_membership(press.marker);
        if (!app.selected_markers.empty())
            land_playhead_on_marker(app, audio, viewport,
                                    app.last_selected_marker);
    } else if (press.shift) {
        // Shift is a file-manager INCLUSIVE RANGE select (architect
        // 2026-07-23): the click ranges from the interaction's anchor — a LIVE
        // anchor, else the ADOPTED FOCUS (plain-click A then shift-click B
        // selects A..B; with nothing focused the click anchors on its own
        // marker, selection {hit}). THE ANCHOR IS NOT KEYED TO THE PHYSICAL
        // SHIFT HOLD (architect 2026-07-29): it SURVIVES a shift release and
        // dies at the next membership replace, so a shift interaction
        // re-started after a release ranges from the SURVIVING anchor rather
        // than from the focus — A..B, release, re-press, shift-click C gives
        // A..C, the accepted delta of the falling-edge hook's deletion. The
        // full contract and clear list are at app.shift_range_anchor
        // (app_state.h). The clicked marker becomes the range end = FOCUS
        // (last_selected) and the playhead LANDS THERE (architect 2026-07-28,
        // replacing the earliest-member land), so focus and land never diverge
        // and nothing is towed by a later nudge. On an anchoring focus-less
        // first click the selection is {hit} and hit is the focus, so the land
        // is unchanged. A range leaving exactly one selected shows its always-on
        // stem; select_range_from_anchor owns the subject-change damage.
        selection.select_range_from_anchor(press.marker);
        if (!app.selected_markers.empty())
            land_playhead_on_marker(app, audio, viewport,
                                    app.last_selected_marker);
    } else {
        // ONE PLAIN MARKER CLICK, NO SPECIAL CASE FOR A SELECTED MEMBER
        // (architect 2026-07-29, HORIZONTAL MOVEMENT IS A FOCUS ACT — the
        // doctrine is at the head of position_nudge.h): a click on a member of
        // a 2+ selection single-selects and lands like a click on any other
        // marker, and the drag it may become is an ordinary singleton drag. The
        // two file-manager DEFERRALS that used to sit on this path — one per
        // drag surface, each holding the click's act back so the drag could
        // seed the intact group — died with the group drag itself; groups are
        // never moved by any route. (The second of those surfaces, the tempo
        // drag, is gone outright — see marker_drag.h.)
        // The clicked marker's flag BRIGHTENS here — set_single_selection
        // damages the top strip, where the flags live. No stem work is owed on
        // any arm: stems are class-colored and always on, so a membership
        // change never creates, moves or recolors one.
        selection.set_single_selection(press.marker);
        land_playhead_on_marker(app, audio, viewport, press.marker);
    }
    // THE CLICK OWNS ITS CLEAR (architect 2026-07-29): a marker click is a
    // POINT command — it says "the playhead is HERE, at this point" — so any
    // resting scratch span ends here, unconditionally, on all three arms and
    // whether or not the land moved anything (the clear-site list is at
    // clear_region_highlight, input_handler.h). A re-click of the
    // already-selected marker therefore clears a resting highlight too; that is
    // the ruling and not an accident.
    clear_region_highlight(app, viewport);
    // THE DOUBLE-CLICK HALF IS THE LIFT'S ALONE and the PLAIN arm's alone. The
    // crossing skips it by construction: a press that became a drag is not a
    // click of a double-click, so it neither opens nor seeds — and it needs no
    // clear of its own either, on_button_press's top-of-frame clear having
    // already emptied the field with nothing able to re-seed it under the held
    // button.
    if (!at_lift || press.shift || press.ctrl) return;
    // A carried verdict for the SAME index within the window opens the flag
    // editor, exactly like Enter on the focused marker (the fork above already
    // single-selected it). THE THREE GATES ARE RE-ASKED LIVE, never carried:
    // read-only, the P view (phase resets have no per-flag editor) and the
    // off-home column (active_column_authoring_allowed — the warp editor is
    // source-view-only) refuse SILENTLY, matching Enter's allowlist / view
    // refusal, and the press stays a plain second select that seeds afresh.
    bool opened_editor = false;
    if (press.double_click_consume &&
        app.active_markers_view != 'P' &&
        !active_view_state(app).read_only &&
        active_column_authoring_allowed(app)) {
        // Every open route opens fully SELECTED (open-selected), so there is no
        // clicked-glyph caret to seat. A specific caret spot is a click inside
        // the already-open editor (the F2.1 path).
        flag_editor.enter_top_flag_edit(press.marker);
        opened_editor = true;
    }
    if (!opened_editor) {
        // SEED the next Marker candidate. THE POSITION IS THE PRESS'S, not
        // this release's: it keeps the SPATIAL pairing press-to-press, exactly
        // the comparison the press-time seed made, and it is the honest one for
        // the touch layer, whose synthesized release carries the finger's LAST
        // position while the press carries its down point. THE TIMESTAMP IS THE
        // SEED'S OWN, which is the release — the family's rule (TrimBar,
        // EditorText and EmptyLane all stamp their motionless release), so the
        // window is measured release-to-press here as it is everywhere else.
        // The split is deliberate: only the position has a reason to look back
        // at the press. On a consumed open nothing seeds: the editor now owns
        // input.
        app.double_click = DoubleClickCandidate{
            .surface = DoubleClickSurface::Marker,
            .time_ms = monotonic_ms(),
            .press_x = press.press_x, .press_y = press.press_y,
            .target  = press.marker};
    }
}

// ARM ONE OF THE ACT-AT-LIFT SWEEP'S LAST FOUR ACTS — the ONE arm for all four,
// so no future shape can arm a fifth way (2026-08-15; the contract, and why the
// four share one record rather than four, is at PendingClickAct, app_state.h).
// It writes the pending and does NOTHING ELSE: the act is the lift's.
//
// THE ARM IS UNCONDITIONAL BY SHAPE. Every gate any of the four acts meets —
// the trim set's strictly-inside refusal and its degenerate-geometry returns,
// the create's read-only and home-view refusals, the framing's audio test, the
// diff-flag bodies' range guards — lives INSIDE the act and is therefore re-asked
// LIVE at the lift, which is the chrome lift's own rule: a gate may change under
// a held button and the LIFT decides. Nothing is carried but the press POINT,
// the modifier SHAPE and the resolved flag INDEX.
void GuiInputHandler::arm_pending_click_act(PendingClickKind kind, int x, int y,
                                            bool is_begin, int flag,
                                            bool shift, bool ctrl) {
    app.pending_click = PendingClickAct{};
    app.pending_click.kind     = kind;
    app.pending_click.press_x  = x;
    app.pending_click.press_y  = y;
    app.pending_click.is_begin = is_begin;
    app.pending_click.flag     = flag;
    app.pending_click.shift    = shift;
    app.pending_click.ctrl     = ctrl;
}

// RUN THE ARMED ACT — the motionless lift's whole body, one owner over the four
// kinds. Its ONE caller is on_button_release (the TrimBoundSet kind's THRESHOLD
// CROSSING runs its act inline instead, because there it is the drag's own
// prologue rather than a click — the fork is stated at that site).
//
// EVERY ACT RUNS ON THE ARMED SUBJECT — the PRESS COLUMN, and the armed flag
// index — and never on a re-hit at the release's coordinates, for the three
// reasons recorded at run_marker_click_act above (touch's down-point press, the
// unfrozen hit stash, and the navigation surfaces' own press-point rule).
//
// THE PENDING IS TAKEN BY VALUE so the caller can follow the release bodies'
// STANDING SHAPE — read the fields, DISARM, then act, so the act runs with no
// gesture live. Several of these acts write viewport, trim and playhead state
// that other code reads through the live-gesture predicates, which is exactly
// what that shape exists for.
void GuiInputHandler::run_pending_click_act(PendingClickAct press) {
    switch (press.kind) {
        case PendingClickKind::None:
            return;
        case PendingClickKind::TrimBoundSet:
            // The ctrl (BEGIN) / ctrl+shift (END) bound set, WHOLE AND
            // UNSPLIT: set_trim_bound_at_click owns every refusal (a degenerate
            // audio/geometry state, and above all the STRICTLY-INSIDE guard —
            // a click landing a bound on or past its partner writes nothing,
            // deselects nothing and stops nothing), the playback stop that sits
            // past those refusals, the write, the commit tail (the crossed
            // reset, the playhead park at the new trim start, the region clear,
            // the repaint and the target trigger) and the setter's deselect.
            // Moving the act meant moving that unit, never a piece of it.
            set_trim_bound_at_click(press.is_begin, press.press_x);
            return;
        case PendingClickKind::TrimBarFraming:
            // ONE GESTURE ON ONE BAND, TWO COMMANDS, and the mode picks between
            // them HERE rather than at the press (2026-08-15): the `h` view
            // frames the VIEWED CHECKPOINT'S DIFF SPAN — the span the bar is
            // already showing — and every other state frames the live ladder
            // (region, else a proper trim sub-window, else the whole song).
            // Asking at the lift is the gates-are-re-asked-live rule; the answer
            // cannot actually have changed under the held button (the drag-modal
            // gate swallows every chord while this pending stands, and no
            // pointer route runs with the button down), so this is honesty about
            // WHERE the decision belongs rather than a behaviour change.
            if (app.history_mode.active) frame_viewed_commit_diff_span();
            else                         run_span_framing_command();
            return;
        case PendingClickKind::EmptyLaneCreate:
            // The empty marker lane's CREATE, at the PRESS column. Its body is
            // unchanged — the bare-`s` drop's own chokepoint, which single-selects
            // what it creates and re-seats the playhead — and its two silent
            // refusals (read-only, the home view) are inside it, so they are
            // re-asked here at the lift.
            create_marker_at_empty_lane(press.press_x - waveform_area(app).x);
            return;
        case PendingClickKind::HistoryDiffFlag:
            // The `h` view's three diff-flag clicks, forked on the CARRIED
            // modifiers: shift is the contiguous ordinal range from the mode's
            // focus, ctrl the membership toggle, and plain the focus click that
            // replaces the set. All three land the playhead on the clicked
            // flag's authored frame and take a resting region with them; none
            // touches the store selection. The two bodies carry their own range
            // guards, which is what makes a flag list that changed under the
            // hold harmless — and nothing can change it, the mode's own walk
            // being keyboard- and chrome-driven.
            if (press.shift != press.ctrl)
                select_history_diff_flags_modified(press.flag, press.shift);
            else
                focus_history_diff_flag(press.flag);
            return;
    }
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
    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate — ALL FOUR at a motionless RELEASE now (TrimBar /
    // EditorText / EmptyLane, the empty lane joining that class 2026-08-12 with
    // its press becoming the navigation surface's pending click, so only the
    // release knows it stayed a click and a pan that crossed the threshold seeds
    // nothing; MARKER joined 2026-08-15 when the flag click moved to the lift).
    // THE MARKER SEED IS DELIBERATELY SPLIT ACROSS THE TWO EDGES, which is
    // unusual enough to state here: the press computes only the consume VERDICT
    // (this snapshot is gone by the time the lift runs), while the seed written
    // at the lift carries the PRESS coordinates with the RELEASE timestamp — the
    // position looking back so the spatial pairing stays press-to-press, the
    // stamp being the seed's own so the window is measured release-to-press as it
    // is for the other three. The full reasoning is at the seed itself
    // (run_marker_click_act). One closed instrumentation point — the clear covers
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

    // Prompt-modal input handling: while a prompt dialog is up, its BUTTONS
    // are the pointer's only targets — and since 2026-08-13 a plain left press
    // on one only ARMS it (architect: "everything else acts on lift"). The
    // ACT is at the release, in on_button_release's mirror of this gate, which
    // is where activate_response is called and where the live-response-set
    // validation lives. Every other press — any button, any modifier,
    // anywhere — is swallowed: THE VEIL. Responses still answer from the
    // keyboard unchanged.
    // THE TWO GATES ARE ON BOTH EDGES, deliberately: the press cannot arm
    // without them and the release re-asks both before dispatching, so an arm
    // cannot survive a dialog that changed under it (the painter drops the arm
    // on exactly those edges too — AppState::modal_dialog_pressed).
    // THE PAINTED GATE (2026-08-13): the rects read here are the LAST PAINT'S
    // publication, so between a raise and its first paint they belong to the
    // previous dialog. A PROMPT REPLACING A PROMPT — the save-failed rung, the
    // one such route — leaves Discard and Cancel rects whose keys ARE live in
    // the new set, at coordinates the new layout (different text, different
    // button words, so different widths) no longer uses: a press there
    // answered destructively against a question that had not been painted.
    // Gated on the same bit the keyboard reads, so both halves of the answer
    // wait for the same frame. The veil is unchanged either way — the press is
    // still consumed by the `return` below, it just arms nothing.
    // AND THE STASH'S IDENTITY IS THE CLAIM'S OTHER HALF: a stash an EDITOR
    // painted never answers a prompt, whatever its keys say (published
    // geometry may only select; live state decides — the doctrine is at
    // ModalDialogGeometry, app_state.h, and the one comparison is
    // modal_dialog_stash_current). The live-response-set test at the release
    // stays: it is the KEY half, and the two are different questions.
    if (app.prompt.active) {
        if (app.prompt.painted && button == GuiMouseButton::Left &&
            !mods.ctrl && !mods.shift && !mods.alt &&
            modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y);
        }
        return;
    }

    // (THE MODAL-TRAP REACH-THROUGH STOOD HERE, 2026-08-11..08-13, and is
    // RETIRED — architect: "we can drop the Save reach through". It let a
    // plain left press on a roster button whose chord the editors' modal
    // contract admits as a command arm through the ordinary press body while a
    // dialog editor stood, which is what gave a keyboard-less user on GLASS a
    // way out of an accidentally opened settings editor. THE MODAL ANSWERS
    // THAT ITSELF now: all four editor dialogs publish real OK and CANCEL
    // buttons, the claim below admits a press on them, and Cancel dispatches
    // the session's own Esc. With Quit's button gone to the File menu the
    // membership had already derived down to Save, and a convenience chord is
    // not worth an exception to the veil. SO THE VEIL HAS NO EXCEPTION: while
    // a dialog editor stands every press outside the modal is consumed. The
    // KEYBOARD is untouched — Ctrl+S still saves with the editor open, through
    // the admission this block used to mirror. The full retirement record is at
    // the deleted predicate's site near the head of this file.)

    // THE DIALOG'S OWN BUTTONS, claimed while an editor dialog stands and
    // ahead of the field claim below (the rects are disjoint; the order only
    // states that a button press is a button press). A plain left press on OK
    // or Cancel ARMS it since 2026-08-13 and dispatches nothing; the LIFT on
    // that same button runs the editor's own Enter or Esc through the one
    // modal key route (dispatch_modal_dialog_editor_act, from
    // on_button_release's mirror of this gate) — button-is-its-chord, so a
    // red-flash refusal, the BPM commit's render sweep and every teardown are
    // the keyboard's own bodies. A PROMPT's buttons are claimed in the prompt
    // gate above, not here.
    // THE STASH'S IDENTITY IS THIS CLAIM'S GATE (2026-08-13, exact since
    // 2026-08-14): in the one batch between an editor's OPEN and its first
    // paint the stash still belongs to whatever stood before it — a PROMPT,
    // which publishes editor_ok FALSE on every button and so used to CANCEL an
    // editor that had just opened, or ANOTHER EDITOR, whose OK the round-15
    // finding showed could commit at an unseen dialog. Refusing a stash that
    // does not name the live session closes both (the doctrine and the two
    // identity fields are at ModalDialogGeometry, app_state.h).
    // (The claim needs no modal_dialog_editor_active term of its own and lost
    // the one it carried: the prompt gate above has already returned, so a
    // CURRENT stash here is an editor's by construction.)
    if (button == GuiMouseButton::Left && !mods.ctrl && !mods.shift &&
        !mods.alt && modal_dialog_stash_current()) {
        if (arm_modal_dialog_press(x, y)) return;
    }

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's text region places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the four dialog
    // editors too. A press outside the active editor's
    // region falls through: the dialog editors stay modal — the VEIL — and
    // swallow it, while the top flag editor closes guard-free below and the
    // press then acts normally.
    if (button == GuiMouseButton::Left) {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            bool in_region = false;
            if (g.dialog) {
                // THE DIALOG'S FIELD INTERIOR, the painter's published rect
                // (modal_dialog.field): the editable text lives in the inset
                // field alone — the box around it is dialog chrome, and its
                // buttons were claimed above. Claiming more would map a
                // chrome press to an editor byte.
                in_region = rect_contains(app.modal_dialog.field, x, y);
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
                    // The dialog editors' repaint owner is the bottom row's
                    // lane (invalidate_modal_dialog_area).
                    if (g.dialog) viewport.invalidate_modal_dialog_area();
                    else          viewport.invalidate_top_strip();
                    return;
                }
                set_editor_caret_from_x(g, x);
                // Collapsed anchor — extends to a real selection only if the
                // pointer then moves.
                g.ed->selection_anchor = g.ed->cursor_pos;
                app.editor_text_drag.active = true;
                if (g.dialog) viewport.invalidate_modal_dialog_area();
                else          viewport.invalidate_top_strip();
                return;
            }
            // A dialog editor stays modal — THE VEIL: a press outside the
            // box's field and buttons is CONSUMED, closing nothing (the
            // architect's words: "once I've done that pop-up modal, I can't
            // do anything else in the window behind it"; the dialog closes
            // only by its own buttons and keys, and since 2026-08-13 the veil
            // has no exception at all). A flag-editor press that
            // isn't on the lane text falls through to the guard-free close
            // below.
            if (g.dialog) return;
        }
    }

    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.load_editor)) return;
    if (text_editor::is_active(app.commit_title_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a dialog modal owner (like the settings
        // editor). Mouse input does not interact with it beyond its dialog's
        // own field and buttons, claimed above; the session ends only through
        // Esc / the Enter dispatch path / the dialog's Cancel and OK
        // (`m` is just a typed character now). Swallow
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
    // only from a press, and while a DIALOG editor is up
    // every press dies at the veil (the MODAL-TRAP block above lifts ONLY
    // roster buttons whose chord the editors admit — Save alone today — and
    // the three menu ANCHORS carry no chord, so no press can open a popup under
    // an editor through it). The other half is not here — the
    // pointer-transparent FLAG editor swallows nothing, so a press does reach
    // the menu buttons with an edit open, and toggle_dropdown's open path ENDS
    // that edit (the rule is stated there). Two mechanisms, one claim. (The
    // reverse direction is closed by the keyboard gate: while the popup is open,
    // `;` is swallowed, so the editor cannot open under it either.)
    if (app.dropdown.open()) {
        // OWNING THE POINTER MEANS EVERY BUTTON, not just the left one: only
        // LEFT carries
        // claims inside the popup, so any other button is CONSUMED INERT here
        // rather than falling through to the bands underneath. (The arm dates
        // from the right-click scrub, 2026-08-01..12; with the right button
        // unbound it defends nothing live, and stays as the popup's shape —
        // owning the pointer is owning every button.) The popup
        // stays open (a non-Left press is not one of its two answers, item-arm
        // or dismiss) and nothing acts.
        if (button != GuiMouseButton::Left) return;
        const AppState::Dropdown& pop = app.dropdown;
        // WALKED, NOT NAMED (the anchor membership is derived from the menu
        // list — app_state.h — so a menu added later needs no edit here).
        bool on_menu_button = false;
        for (const DropdownMenu m : kDropdownMenus) {
            if (redesign_button_hit(app, dropdown_anchor_button(m), x, y)) {
                on_menu_button = true;
                break;
            }
        }
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
                // ITEMS ACT ON RELEASE — this press only ARMS one. The items
                // were the redesign's FIRST act-on-release surface (the
                // universal menu convention: press, slide, release on what you
                // meant), the model the modal dialog buttons and then the
                // whole chrome roster took on 2026-08-13. What stays the
                // items' own is the SLIDE: the arm travels between items,
                // where a button's arm stays on the button it pressed.
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

    // THE FOUR REDESIGNED BUTTON ROWS (top lanes 0..2 plus the bottom strip's
    // transport row, whose claim closes the block — the toolbar row's own
    // claim died with its lane at the 2026-08-12 relayout, its four buttons
    // now inside the icon row's band), claimed ABOVE the
    // loading/empty guard below so their buttons stay live while a file loads
    // and on a blank state — they are the surfaces that have nothing to do with
    // the loaded audio. They sit BELOW the modal gates on purpose: a press while
    // a prompt or a dialog editor is up is swallowed there, exactly as it
    // is for every other pointer target (a modal owns the pointer; these buttons
    // are no exception, and every one of their chords reaches the same route
    // from the keyboard anyway). (Row 8's claim stood ABOVE the modal gates for
    // part of 2026-08-11, for the sake of its Cancel/Esc button — the one
    // button that had to reach the keyboard-modal Esc bindings; the architect
    // deleted that button the same day and the claim came home to this block
    // with its justification.)
    //
    // ONE BAND-CLAIM SHAPE FOR ALL FOUR ROWS: the exact half-open row band, a
    // press carrying CTRL or ALT is a strict consumed no-op, a SHIFT press binds
    // only where the chord table admits one, and any press in the band that is
    // not on a button is a consumed nothing. Each band differs ONLY in its rect
    // and (row 1) in the dropdown toggle of its TWO non-chord buttons, Settings
    // and Navigation, so the press is ONE arm body, arm_redesign_press, driven
    // by the table's per-button flags — and the act one release body,
    // finish_chrome_press_release, in on_button_release.
    //
    // A BUTTON's rect is the painter's stash (app.redesign_buttons, published by
    // paint_menu_row / paint_tab_row / paint_icon_row /
    // paint_bottom_row_buttons_and_clock; a COLLAPSED icon-row member's stash
    // is a zero rect, which contains no point — the mode-collapsing roster's
    // whole pointer story) —
    // never re-shaped here, so the clickable rect is the painted one. THE ACT
    // IS AT THE RELEASE (architect 2026-08-13, the chrome-wide rule —
    // authoritative statement in kdenlive-redesign.md's act-at-release
    // section): the press ARMS through arm_redesign_press, carrying the
    // press-time shift, and the lift on that same button dispatches through
    // finish_chrome_press_release (on_button_release). Nothing on these rows
    // drags — no threshold, no double-click surface — and the arm is the whole
    // press-state machinery (AppState::ChromePress). Nothing here reads
    // keyboard state, so the bare-`e` mouse key reaches them as an ordinary
    // left press through the platform translation.
    {
        const GuiRect menu_row = top_menu_row_area(app);
        if (rect_contains(menu_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) {
                // FILE, NAVIGATION AND SETTINGS ARE THE ROSTER'S THREE
                // NON-CHORD BUTTONS, so they are spelled here rather than in the
                // table: each action is a POPUP TOGGLE, which no keyboard chord
                // performs. Their menus lead to routes the keyboard already has
                // — the bare `;` still opens the settings editor DIRECTLY, every
                // navigation item is a key you can press instead, and File's one
                // item is Ctrl+Q — so a
                // dropdown is a pointer affordance for an existing road, never a
                // second one. Shift-exact is refused like every other
                // non-admitting button.
                //
                // THE ANCHORS ARE WALKED rather than spelled one by one — the
                // same shape on_motion's two anchor walks take, over the one
                // menu list (app_state.h), so
                // dropdown_anchor_button stays the one place that knows which
                // button emits which menu — and the walk is what gives the CLAIM
                // below exactly ONE site instead of one per branch. It is also
                // what made File a one-row addition here: the walk grew a menu
                // and this body did not change.
                DropdownMenu anchored = DropdownMenu::None;
                if (!mods.shift) {
                    for (const DropdownMenu m : kDropdownMenus) {
                        if (!redesign_button_hit(app, dropdown_anchor_button(m),
                                                 x, y)) continue;
                        anchored = m;
                        break;
                    }
                }
                if (anchored != DropdownMenu::None) {
                    // THE ANCHORS ACT AT THE PRESS — the chrome roster's ONE
                    // recorded exception to act-at-release (architect ruling
                    // 2026-08-13; everything else armed below and at the
                    // sibling bands). Deliberate, for three reasons that are
                    // the menus' own: (1) a menu OPENS ON PRESS on every
                    // desktop, because the press-drag-into-the-box-release
                    // gesture (the claim below) needs the box up while the
                    // button is still down — an anchor that opened at the lift
                    // could never carry it; (2) DISMISSAL IS A PRESS ACT in
                    // this product, and the toggle's close half is a
                    // dismissal — moving it to the lift would split the
                    // toggle's two halves across two edges; (3) an
                    // arm-then-toggle-at-lift would re-open the menu the
                    // press-anywhere-closes rule had just put away whenever
                    // the press landed on the open menu's own anchor — the
                    // open-on-press-close-on-release oscillation. The toggle
                    // is not a command dispatch, so nothing about
                    // button-is-its-chord is at stake.
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
                    arm_redesign_press(x, y, mods);
                }
            }
            return;
        }
    }
    {
        const GuiRect tab_row = top_tab_row_area(app);
        if (rect_contains(tab_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            // THE STATUS CHAIN ON THIS ROW IS POINTER-INERT (it moved here
            // 2026-08-13): it publishes no rect, so a press over its text is
            // the band's own consumed nothing, exactly as the empty tail past
            // the last tab already was. THE TABS ARE THIS ROW'S ONLY TARGETS
            // since 2026-08-14: the active tab's padlock was a second one
            // until the read-only toggle moved into the icon row.
            //
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
            // one thing the view is for. (The padlock was not drawn in here
            // even before it left the row, so this branch has never had a
            // second target to test for.)
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
                    // THE SELECT IS AT THE LIFT like every chrome act
                    // (2026-08-13): the press arms the tab's roster index and
                    // finish_chrome_press_release re-derives the walk source
                    // from it — the release-side twin of this walk.
                    for (const RedesignButton id :
                         {RedesignButton::TabA, RedesignButton::TabB}) {
                        if (!redesign_button_hit(app, id, x, y)) continue;
                        app.chrome_press = AppState::ChromePress{
                            AppState::ChromePress::Kind::HistoryWalkTab,
                            redesign_button_index(id), false, true,
                            monotonic_ms()};
                        break;
                    }
                }
                return;
            }
            // THE ROW HAS ONE TARGET PER TAB AGAIN (2026-08-14): the
            // padlock left this row for the icon row's own roster button
            // (RedesignButton::IconReadOnly, bare `o`), so a press anywhere in
            // a tab is that tab's Ctrl+Tab through the chord table like every
            // other roster press — there is no second target inside a tab and
            // no rect of its own to test first.
            if (button == GuiMouseButton::Left) arm_redesign_press(x, y, mods);
            return;
        }
    }
    {
        const GuiRect icon_row = top_icon_row_area(app);
        if (rect_contains(icon_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) arm_redesign_press(x, y, mods);
            return;
        }
    }
    // THE UNIFIED BOTTOM ROW (row 8's claim since 2026-08-11; the whole
    // merged lane since the 2026-08-12 unification), the block's fifth member
    // on the block's own terms: the band is the bottom strip's ONE lane, on the
    // window's foot since commit B, and everything else is the shape above —
    // below the modal gates (a prompt or a dialog editor swallows the
    // press; the pointer-transparent flag editor does not, and its KEYBOARD
    // modality then answers the dispatched chord exactly as it answers the
    // key), above the loading/empty guard, ctrl/alt strict no-ops, and every
    // press in the band that is not on a button a consumed nothing — which
    // since the unification includes the clock cell and the bare ground
    // beside it, the lane's pointer-inert span (the status chain that shared
    // that ground until 2026-08-13 took no clicks either, and took none away
    // with it). The
    // lane rests on the WINDOW'S FOOT
    // since the relayout's commit B, so NOTHING is below it; ABOVE it lies GAP
    // 2's blank window ground, outside every band and falling through to the
    // tail's consumed nothing (window ground by the vertical rule, main.cpp).
    // (The OVERVIEW STRIP sat under this lane for the afternoon it landed and
    // is a top-strip lane now; its own claim is further down, past the gesture
    // guards — the endcap / teleport-pan / ctrl-zoom vocabulary.)
    {
        const GuiRect bottom_row = bottom_row_area(app);
        if (rect_contains(bottom_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) arm_redesign_press(x, y, mods);
            return;
        }
    }
    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    // The waveform BAND spans the full window width (top.w), not the effective
    // width (area.w): the <=15 px inert right gutter counts as a waveform click
    // by the user's lights, so a plain press there still reaches the waveform
    // branch and arms the pending click like any other — a gutter PAN works
    // from any column, and the motionless release's act degenerates per half:
    // the upper half's placement clears the selection and seats nothing (no
    // column exists), and the lower half's scrub returns silently (no launch
    // position exists, and a scrub act touches no selection anyway). The
    // gutter is 0 px at the deployment widths
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
    // The region drag joins the pair since the touch half (2026-08-12): a
    // live TOUCH region gesture holds no logical button, so a mouse press —
    // or a bare-`e` synth press, which arrives with no prior motion to hit
    // the button-lost arm — could land mid-gesture and arm a second writer
    // over the same state. Consumed instead, the trim guard's own shape; the
    // gesture ends by its own hooks or the button-lost motion arm.
    if (app.region_drag.active) return;

    // THE OVERVIEW STRIP'S CLAIM — the whole lane (the lane rework,
    // 2026-08-12, SUPERSEDING the landing's plain-press strip-drag claim;
    // REDESIGNED 2026-08-15 so that THE BOX IS THE SUBJECT of every gesture
    // here and the zoom follows from the box's span — OverviewDragState,
    // app_state.h, owns the vocabulary's contract). THREE PLAIN arms and one
    // refusal:
    //   * PLAIN left press on a BOX ENDCAP — the outline's left/right edge,
    //     hit_test_overview_endcap, the claim that OUTRANKS everything else
    //     on the lane — arms the EDGE DRAG, the FIXED opposite
    //     viewport bound captured now (dragging the LEFT edge holds the
    //     viewport END, the RIGHT edge its START).
    //   * PLAIN left press INSIDE the box arms the BOX PAN with the grab-point
    //     offset preserved; a motionless release there is a consumed nothing
    //     (the lane's v1 rule standing).
    //   * PLAIN left press OUTSIDE the box ARMS AND ACTS ON NOTHING — the
    //     Pending kind, the navigation surface's own deferred-click shape for
    //     the identical reason. Its motionless RELEASE runs the teleport at the
    //     press column (on_button_release), and that is its ONLY act: a pending
    //     that crosses the drag threshold commits nothing at all (2026-08-15,
    //     the outside-drag extension's deletion; the ruling is at the contract).
    //   * EVERY OTHER press — ctrl, shift, alt, mixed, non-left — is a consumed
    //     nothing, the band-claim family's shape. CTRL is on that list since
    //     2026-08-15: it carried the dual-axis strip drag until the redesign
    //     deleted that gesture, and it binds nothing on this lane now (the
    //     cursor map's ctrl arm dropped the lane's Zoom with it).
    //
    // DELIBERATELY ABOVE THE `h` MODE'S GATE: every arm here is the mode's
    // own admitted navigation class (pure viewport moves — no playhead, no
    // region, no selection), and the lane behaves identically in and out of
    // the view (the box reads the mode's viewport by construction), so
    // claiming here keeps ONE body with no mode arm — the
    // band-claims-above-the-gate precedent, its "already covered" reasoning
    // met by the gestures being coverage-free navigation rather than chords.
    //
    // AND DELIBERATELY ABOVE THE TOP-STRIP BRANCH, which the lane joined at the
    // relayout's commit B: this claim's rect is the lane's own, so it wins the
    // band before the strip's flag / trim / nav-lane walk and its empty-spot
    // return can see it — the same position the claim held when the lane was a
    // bottom-strip surface, now load-bearing rather than incidental.
    {
        const GuiRect ov = top_overview_row_area(app);
        if (rect_contains(ov, x, y)) {
            if (button != GuiMouseButton::Left) return;
            if (ctrl || shift || alt) return;
            const TrimHit cap = hit_test_overview_endcap(app, audio, x, y);
            if (cap != TrimHit::None) {
                app.overview_drag = OverviewDragState{};
                app.overview_drag.active  = true;
                app.overview_drag.press_x = x;
                app.overview_drag.press_y = y;
                // The kind and its FIXED partner through the one seater, whose
                // only caller this is; degenerate geometry seats nothing and
                // the press is the band's consumed nothing.
                if (!seat_overview_edge_drag(cap == TrimHit::Begin))
                    app.overview_drag = OverviewDragState{};
                return;
            }
            // Inside-the-box test off the painter's own span (one owner —
            // overview_box_span; no box on degenerate geometry means every
            // press is an outside press, which is the right degenerate arm).
            int bx0 = 0;
            int bx1 = 0;
            const bool have_box = overview_box_span(app, audio, &bx0, &bx1);
            const bool inside_box =
                have_box && x >= ov.x + bx0 && x < ov.x + bx1;
            app.overview_drag = OverviewDragState{};
            app.overview_drag.active  = true;
            app.overview_drag.kind    = inside_box ? OverviewDragKind::Pan
                                                   : OverviewDragKind::Pending;
            app.overview_drag.press_x = x;
            app.overview_drag.press_y = y;
            if (inside_box) {
                // The grab-point offset: pointer's whole-song position minus
                // the viewport center, both in the active domain, so the
                // grabbed spot under the box stays under the pointer. The
                // Pending arm needs none — its one act is the teleport, which
                // centers on the press column itself, and a pending that drags
                // commits nothing at all.
                const double pos =
                    overview_anchor_sample_at_x(app, audio, x);
                const double center =
                    static_cast<double>(app.viewport_start_sample) +
                    static_cast<double>(samples_visible(app, audio)) / 2.0;
                app.overview_drag.grab_offset = pos - center;
            }
            return;
        }
    }

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
    // PAIR, which the tab row's own band claim intercepts above and arms for
    // set_history_reading at the lift (the walk selector, deliberately not a
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

    // THE RIGHT BUTTON IS FULLY UNBOUND (architect 2026-08-12, the eighth glass
    // ruling, deleting the 2026-08-01 full-height right-click scrub — "that
    // existed only to serve a very tall waveform, and we're shrinking the
    // waveform"): a right press, plain or modified, is a consumed nothing
    // everywhere — it falls through the Left-only blocks below and out of this
    // handler having touched nothing. The flag-editor outside-press close went
    // with the arm it was added for (2026-08-01's "editor lifecycle runs on
    // both buttons" served the scrub that could act under an open edit; with
    // the right button meaningless again, a right press closes nothing) — the
    // guard-free close is the LEFT press's, below.

    // Mouse authoring is home-view gated like the keyboard: the marker DRAG is
    // gated by active_column_authoring_allowed, off-home selecting and landing but
    // MOVING NOTHING — with no exception anywhere, since 2026-07-29. W+target used
    // to arm the TEMPO drag on an eligible marker instead of the reposition drag
    // (the pointer half of the home-view binding's tempo exception); that whole
    // gesture is deleted (see marker_drag.h), so a W+target flag click now selects
    // and lands like any other off-home click and can move nothing at all. THE
    // GATE LIVES AT THE THRESHOLD CROSSING since 2026-08-15, not at the press:
    // the press arms unconditionally and the click is the lift's, so the gate
    // sits where the drag actually begins (on_motion). The
    // click-playhead / region-drag family below is
    // navigation, not authoring, and stays view-independent.

    if (button == GuiMouseButton::Left) {
        // Editor lifecycle, guard-free — THE SHARED OWNER, called from the right
        // press arm above too. A press in the editor's rendered lane text
        // already repositioned the caret / armed the text drag above (the F2.1
        // block) and returned; ANY other left press with the top flag editor
        // open CLOSES it without committing, and then FALLS THROUGH so the press
        // acts normally (arm a nav press, arm a marker press, place the
        // playhead, ...). Placed ahead of every claim below so the close really
        // is unconditional. IT STAYS AT THE PRESS while the marker click moved
        // to the lift (2026-08-15): a DISMISSAL is press-time by standing rule
        // product-wide — the menu row's any-press end, press-anywhere-closes,
        // this close, the veil — and the act-at-lift sweep moved only the ACT.
        // Consequence: a double-click on the open editor's own marker is
        // close-then-reopen — the first press closes and arms, its LIFT selects
        // and seeds a Marker candidate, and the second press's lift consumes
        // into a fresh open. That IS the documented "double-click opens the
        // editor"; there is no own-marker special case.
        close_top_flag_editor_for_outside_press(x, y);

        // The marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item and that item is now its FLAG BOX alone
        // (hit_test_flag against the painter's stash — the rendered lane run
        // that used to be its second half died with the marker-text lane, and
        // with it the MarkerHit pair and its shared resolver marker_hit_at).
        // The TOP-STRIP hit feeds the plain/Shift/Ctrl marker-press branches,
        // all three of which now merely ARM the pending click (2026-08-15 — the
        // click acts at the LIFT; the acts themselves are at
        // run_marker_click_act: plain = single-select + land + the double-click
        // consume-or-seed, Shift = the file-manager inclusive RANGE select from
        // the interaction's anchor to the clicked marker + land on that range
        // END, Ctrl = the individual membership toggle + land on the resulting
        // focus), so it is resolved once here — every one of the three lands on
        // its own focus. THE ARMED INDEX IS WHAT THE LIFT ACTS ON: nothing is
        // re-hit-tested at the release (the three reasons are at
        // run_marker_click_act).
        // The WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + region-drag arm; lower:
        // the scanner scrub, which touches no selection at all), and a Shift
        // press FORMS a region waveform-wide (from the playhead, or a marker
        // DROP that clears the selection; see the waveform block below) — so
        // no marker scan runs on the waveform at all. THE MARKER STEMS ARE
        // POINTER-INERT (architect 2026-08-12, the seventh glass ruling — "I
        // definitely don't want to be concerned about accidentally touching a
        // marker"): the FLAG BOX is the marker's ONE pointer surface, in every
        // view. The stem-as-second-surface model of 2026-08-01 — the plain
        // upper-half press within a grab tolerance of a painted stem's column
        // routing through the flag's own click bodies — is DELETED WHOLE
        // (hit_test_marker_stem and kMarkerStemGrabPx with it), so a plain
        // upper-half press over a stem column is the ordinary placement press,
        // stem or no stem. The stems question stayed OPEN through the touch
        // arc and was answered "stems stay" while the scrollbar plan lived;
        // the waveform-height clamp (main.cpp's layout owner) keeps the flag
        // lane in easy reach on every display, which is what the removal was
        // waiting for — marker work happens in the flag lane, and the
        // waveform is purely region / playhead / pan / zoom. (Modified
        // presses never resolved a stem anyway — the 2026-08-01 plain-exact
        // gate, made universal by the 2026-08-06 symmetry ruling — so this
        // ruling only moved the PLAIN stem click.) The stems still PAINT
        // exactly as before: class-colored, always on, disabled-no-stem.
        // The plain DRAG never selects markers either
        // (SELECTION FLOWS DOWNWARD ONLY, architect 2026-07-23 — the region no
        // longer selects its contents; it leaves the selection empty).
        // Trim bounds are grabbed only by their top-strip endcaps /
        // the inter-endcap bridge on a PLAIN trim-bar press (route_trim_bar_press
        // below); a click over a bound's waveform stem is an ordinary waveform
        // click (the stem grab retired), so no trim hit test runs on the
        // waveform at all. Resolved ONCE here, ahead of every branch that
        // consumes it: the ctrl-exact arm, the plain / Shift arm, and the
        // empty-top-strip fallthrough (mh_index < 0 is what makes a spot EMPTY)
        // all read this one hit.
        int mh_index = -1;
        if (inside_top) mh_index = hit_test_flag(app, audio, x, y);

        // A top-strip gesture stops playback WHEN IT CLAIMS SOMETHING, never
        // merely because it landed in the strip (architect 2026-07-27). The
        // stop is the price of an authoring or a navigation act — a marker
        // select, a trim bound set, a trim-bar consume — and continuing audio
        // during authoring / text editing is the wrong default, so each of
        // those acts calls stop_playback_if_playing ITSELF at its own site.
        // THE MARKER'S SITE IS NO LONGER A PRESS SITE (2026-08-15): its stop
        // leads the click act, which runs at the LIFT or at the drag's
        // threshold crossing, so a marker press stops nothing on the way down —
        // the rule is unchanged, its subject simply moved with the act it
        // priced. THE STOP IS INTENTIONAL, NOT POSITIONAL: a press that claims
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
        // SHIFT-exact trim-bar press (trim is transparent to shift),
        // every empty marker-text-lane spot, and the inter-lane gaps — all
        // of which end at the inert top-strip return far below.
        // The EMPTY MARKER LANE's PLAIN press is the one ACTING press
        // that still does not stop: it is the waveform-upper-half's twin (the
        // empty-lane parity press, architect 2026-07-23), and a
        // live session RESEEKS there rather than dying — through arm_nav_press's
        // own DEFERRED CLICK ACT since the pan-primary ruling made that lane the
        // navigation surface's member (2026-08-12; the claim that this branch
        // acts "through place_playhead_and_arm_region" outlived that change and
        // is corrected here 2026-08-15 — that body is the SHIFT former's, and
        // the former's own y-gate does reach this lane, which is why shift is
        // NOT in the inert list above: it acts here, and it too only reseeks).
        // Waveform clicks keep playback alive as ever — the per-press reseek to
        // the click sample happens inside the deferred click act's placement
        // body (run_nav_click_act -> place_playhead_at_click_column; it was the
        // playhead-drag PRESS site until the eighth glass ruling deferred it),
        // gated
        // on was_playing && sample != playhead_at_entry. Capture the entry
        // state up front, AHEAD OF EVERY STOP below, so all the downstream
        // branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // (NO ALT ARM: alt's pointer vocabulary is EMPTY since 2026-08-12, the
        // eighth glass ruling — the grab-pan it carried is the PLAIN drag on
        // the navigation surface now, and the alt+wheel stepped pan is the
        // plain wheel. An alt-exact press falls to the strict-modifier discard
        // below, a consumed no-op like every other unbound combination; on
        // the keyboard alt survives only inside the four Ctrl+Alt chords.)

        // Ctrl-exact left press splits by surface. On a top-strip MARKER it is
        // the individual membership toggle + land on the resulting focus (the
        // marker claim below). On the NAVIGATION SURFACE — the waveform, either
        // half, plus the RULER and the MARKER lane's empty stretches since
        // 2026-08-12 (the eighth glass ruling: the lanes are the upper half's
        // extension) — it is THE ONE NAV DRAG'S CTRL ENTRY since 2026-08-14
        // (arm_nav_zoom_press; the live-ctrl model at ScrollDragState): the
        // same drag the plain press arms, opened in the ZOOM phase with the
        // pivot seated and the anchor stem painted at the press — ctrl is the
        // desk's second finger, live mid-gesture in both directions, so this
        // entry differs from the plain one only in its opening mode and in
        // arming NO click act. The gesture is
        // navigation-class: allowed in read-only, never touches the playhead or
        // selection — and a MOTIONLESS ctrl press-release commits
        // nothing at all (the ctrl+waveform selection clear is RETIRED,
        // architect 2026-07-23: ctrl is purely the zoom modifier on the
        // waveform; the 2026-08-05..06 playhead jump that briefly stood here was
        // ROLLED BACK 2026-08-06 and only its anchor stem survives, so this
        // press is once again the drag and nothing else).
        // Ctrl-exact on any OTHER top-strip spot is a strict no-op except
        // the trim bar's BEGIN bound set (the claim below).
        if (ctrl && !alt && !shift) {
            // Ctrl-exact on a top-strip MARKER is the individual membership
            // toggle (the former shift behavior) — this AMENDS the "Ctrl keeps
            // only the letter chords" rule for the one marker surface (architect
            // 2026-07-23). It becomes no drag, seeds/consumes no double-click,
            // opens no editor. Whether the toggle ADDED or REMOVED, the playhead lands
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
            // THE FLAG IS THE WHOLE SURFACE HERE — `inside_top` alone: the
            // ctrl-exact WAVEFORM press is the strip drag, and a stem stands
            // ON the waveform, so ctrl over a stem belongs to the drag (as it
            // always did — modified presses never resolved a stem, and since
            // 2026-08-12 no press does). A markerless
            // top-strip ctrl press claims only the trim bar (BEGIN bound set,
            // next block) and is a strict no-op on every other lane — no-op in
            // the playback sense too, since only a CLAIM stops playback.
            if (inside_top && mh_index >= 0) {
                // THE PRESS ONLY ARMS (2026-08-15): the toggle, its stop, its
                // land and its region clear are the CLICK, and the click runs
                // at the motionless lift through the one act owner
                // (run_marker_click_act). A ctrl arm has no gesture to become,
                // so a crossing spends it and commits nothing.
                arm_marker_press(mh_index, x, y, /*shift=*/false,
                                 /*ctrl=*/true, dc_at_press);
                return;
            }
            // Markerless top-strip ctrl-exact press: the TRIM BAR sets the BEGIN
            // trim bound at the click (REINSTATED architect 2026-08-01 — ctrl is
            // BEGIN and ctrl+shift is END, the pair's original shape, now homed
            // on the redesigned bar's whole band rather than the chip row it grew
            // up on; set_trim_bound_at_click refuses any value not STRICTLY
            // INSIDE its partner — the clicks ADJUST the window that always
            // rests, they never create one — and, being
            // a SETTER, deselects past its refusals. IT NO LONGER REFUSES A
            // READ-ONLY TAB: trim is band, not authored content, and that gate
            // was deleted 2026-08-07). EVERY other lane is a
            // strict no-op, falling through to the return below (the ctrl-click
            // clear on an empty marker spot is RETIRED, architect 2026-07-23:
            // ctrl-click in Ableton is just click, and ctrl stays the zoom
            // modifier here; the PLAIN empty marker-lane press below is the
            // surviving lane gesture — the waveform's own parity press, not a
            // bare clear). The three redesigned top rows (lanes 0..2 since the
            // 2026-08-12 relayout deleted the toolbar lane) were claimed far
            // above and never reach here.
            //
            // THE BAND IS THE CURRENT GEOMETRY OWNER, top_trim_row_area — the
            // exact band the plain endcap/bar drags and the span-framing
            // double-click claim, so paint, hit and every trim gesture read ONE
            // accessor and cannot drift.
            if (inside_top) {
                const GuiRect trim_band = top_trim_row_area(app);
                if (y >= trim_band.y && y < trim_band.y + trim_band.h) {
                    // THE PRESS ONLY ARMS (2026-08-15, the act-at-lift sweep's
                    // last four acts — contract at PendingClickAct,
                    // app_state.h): the BEGIN bound set, its refusals, its stop,
                    // its commit tail and its deselect are the CLICK, and the
                    // click runs at the MOTIONLESS LIFT through the one act
                    // owner (run_pending_click_act), at the PRESS column. A
                    // CROSSING runs that same set and then hands the gesture to
                    // the single-bound endcap drag on the bound it just wrote,
                    // so ctrl-press-and-drag is byte-for-byte the gesture it has
                    // always been.
                    // NO stop here, for the reason it was never here: the bound
                    // set has its own refusals (a degenerate audio/geometry
                    // state, a value not strictly inside its partner), and a
                    // refused click changes nothing, so there is nothing for a
                    // stop to protect. The stop lives INSIDE
                    // set_trim_bound_at_click, past every refusal and
                    // immediately ahead of the bound write.
                    arm_pending_click_act(PendingClickKind::TrimBoundSet, x, y,
                                          /*is_begin=*/true);
                    return;
                }
            }
            // The zoom surface IS the navigation surface, through its one
            // geometry owner — the waveform (either half, as ctrl always
            // covered) and the two navigation lanes. The flag hit was claimed
            // above, so the owner's own flag carve-out simply agrees here.
            // Anywhere else — the gap band, the inter-lane seams — the strict
            // no-op below. (The `h` view's ctrl press falls through to this
            // same claim; the click act is not armed on this entry, so the
            // mode needs no arm of its own here.)
            if (point_on_nav_surface(app, audio, x, y))
                arm_nav_zoom_press(x, y);
            return;
        }

        // Ctrl+Shift-exact: the TRIM BAR is its ONE claim — set the END trim
        // bound at the click (ctrl is BEGIN, ctrl+shift is END; the same
        // reinstated pair, architect 2026-08-01. set_trim_bound_at_click refuses
        // any value not strictly inside its partner — the adjust-only pair gate
        // died with the unset state 2026-07-30, a full pair always resting, and
        // the read-only refusal died 2026-08-07 with trim's reclassification as
        // band — and deselects as a SETTER past its
        // refusals). Everywhere else Ctrl+Shift stays a strict no-op, playback
        // included, falling to the return below.
        if (ctrl && shift && !alt && inside_top) {
            const GuiRect trim_band = top_trim_row_area(app);
            if (y >= trim_band.y && y < trim_band.y + trim_band.h) {
                // THE PRESS ONLY ARMS, the BEGIN set's own shape above: the END
                // set runs at the motionless lift and a crossing runs it and
                // then hands over to that bound's endcap drag (2026-08-15).
                // NO stop here either: like the BEGIN set, the stop sits
                // inside set_trim_bound_at_click past that act's refusals, so a
                // refused END set leaves a live audition alone.
                arm_pending_click_act(PendingClickKind::TrimBoundSet, x, y,
                                      /*is_begin=*/false);
                return;
            }
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's endcap/bridge drags on the plain trim-bar press, so
        // every remaining modified combination — ALT-exact (the pointer's alt
        // vocabulary is EMPTY since 2026-08-12), Ctrl+Alt, Ctrl+Shift off the
        // trim bar (its one claim is the END bound set above), Shift+Alt,
        // Ctrl+Alt+Shift, ... — no-ops here. Only a plain or Shift base press
        // proceeds. ALT survives ONLY in the FOUR keyboard Ctrl+Alt
        // render / propagate chords (Ctrl+Alt+R, Ctrl+Alt+Shift+R,
        // Ctrl+Alt+P, Ctrl+Alt+Shift+P) — every other alt keybinding was retired
        // 2026-07-28, and its last two pointer forms (the alt+drag grab-pan and
        // the alt+wheel stepped pan) moved onto the PLAIN forms with the
        // eighth glass ruling, so nothing anywhere defers to it.
        // Discarding a press here is TOTAL: it claimed
        // nothing, so it stopped no playback on the way down either — the stops
        // live at the claims above and below, never on the route to this gate.
        if (ctrl || alt) return;

        // Plain or Shift press, under the PAN-PRIMARY vocabulary (architect
        // 2026-08-12, the eighth glass ruling) as amended 2026-08-13 (THE
        // WAVEFORM'S TWO HALVES BECOME ONE SURFACE). On the NAVIGATION SURFACE
        // — the WHOLE waveform + the RULER + the MARKER lane's empty stretches
        // — a PLAIN press is a PENDING CLICK (arm_nav_press): a motionless
        // release runs THE HALF'S OWN ACT as the DEFERRED CLICK ACT (upper =
        // the playhead placement, lower = the audition scrub), and crossing the
        // 8px threshold is the GRAB-PAN in either half. A SHIFT press there is
        // the REGION FORMER, the one mouse region gesture (claimed just below,
        // ahead of the band walk), IN EITHER HALF TOO since the same ruling
        // ("shift plus drag to map out a region should also be allowed in the
        // lower half, for consistency"). Neither ever SELECTS a marker. In the top strip a
        // plain TRIM-BAR press arms a trim endcap/bridge drag (claimed ahead
        // of the marker select); a marker click — its FLAG BOX, the marker's
        // one pointer item — is the whole selection interface, BOTH views,
        // UNCHANGED by the ruling: plain
        // click: single-select and LAND the playhead on the marker, both AT THE
        // LIFT since 2026-08-15, with the press merely arming the pending click
        // that becomes the reposition drag past the threshold (which runs that
        // same act at the crossing). Shift+click: a
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

        // THE SHIFT REGION FORMER, claimed ONCE for its whole y-gate (the
        // navigation surface, WHICH NOW INCLUDES THE LOWER HALF — architect
        // 2026-08-13, superseding the eighth glass ruling's "no region sweep at
        // all in the lower half": the drag motions are the same in both halves
        // now, so the region former is too) so the band walk below is
        // plain-only except the flag range click. A shift press on a FLAG falls
        // through to the marker block (lane vocabulary); a shift press anywhere
        // else — the trim bar, the gap band, the inter-lane seams — is a
        // consumed nothing, shift binding nothing there. The former's body is
        // the one placement press (place_playhead_and_arm_region): deselect,
        // seat the playhead at the clicked column, dissolve any resting span,
        // arm the drag — the drag then extends the span with the playhead
        // riding the moving endpoint, landing where the mouse releases; a
        // motionless shift click lands the playhead and rests no region. The
        // `h` view never reaches this claim (its gate consumed or forked far
        // above); its own shift former is handle_history_mode_press's.
        if (shift && !(inside_top && mh_index >= 0)) {
            if (point_on_nav_surface(app, audio, x, y)) {
                place_playhead_and_arm_region(x - area.x, x, y,
                                              was_playing, playhead_at_entry);
            }
            return;
        }

        if (inside_top) {
            // TOP-STRIP ONLY (the stem's widened gate — `inside_top ||
            // stem_click` — died with the stem surface, architect 2026-08-12:
            // a plain waveform press over a stem column falls through to the
            // waveform block below and is the placement press there).
            //
            // The TRIM BAR (top_trim_row_area, lane 4) is trim's lane and is
            // claimed BEFORE the marker single-select. Row 5's three lanes —
            // the trim bar, the ruler (lane 5) and the marker lane (lane 6) —
            // are disjoint y-bands, so
            // this contends with nothing: a marker-part press falls to the marker
            // handling below. The PLAIN click consumes the span-framing
            // double-click, else arms an endcap/bridge drag, else — on an unclaimed
            // spot — is a CONSUMED NOTHING; the bound-set clicks
            // are the ctrl (BEGIN) / ctrl+shift (END)
            // claims above. A SHIFT-exact trim-bar press claims nothing —
            // trim is transparent to it, and the shift former's claim above
            // already consumed it (the band is outside the former's y-gate),
            // so this arm is plain by construction and stops no playback —
            // nor does the plain press: the trim bar's
            // stop belongs to the DRAG's first accepted bound change
            // (input_trim.cpp).
            // THE RULER BAND'S PLAIN PRESS IS THE PENDING CLICK / GRAB-PAN
            // (architect 2026-08-12, the eighth glass ruling — the lane is the
            // upper half's extension, so it takes the upper half's own plain
            // vocabulary; the one-day RULER REGION FORMER of that morning's
            // sixth ruling is SUPERSEDED, the region living on SHIFT now like
            // everywhere else). The press arms ScrollDragState and does
            // NOTHING ELSE; a motionless release runs the deferred click act
            // (deselect + playhead to the column — "a click anywhere on the
            // extension moves the playhead", because a flag click already
            // does), and a crossed drag is the captured pan. No double-click
            // surface here: the span-framing double-click lives on the TRIM
            // lane, and the marker-create one on the MARKER lane's empty
            // stretches — the ruler seeds nothing.
            //
            // THE BAND IS EXACTLY top_ruler_row_area AND NOTHING BELOW IT (the
            // claim reads the lane accessor and only the lane accessor). The
            // `h` VIEW never reaches this arm — its own gate armed the same
            // pending with the mode's deferred land far above. A GUTTER press
            // still arms (the pan works from any column); its motionless
            // release's click act deselects and seats no playhead, the
            // placement body's own gutter shape.
            {
                const GuiRect ruler = top_ruler_row_area(app);
                if (y >= ruler.y && y < ruler.y + ruler.h) {
                    arm_nav_press(x, y, /*history=*/false,
                                  /*seed_empty_lane=*/false,
                                  /*scrub_release=*/false);
                    return;
                }
            }
            const GuiRect trim_bar = top_trim_row_area(app);
            const bool in_trim_bar =
                (y >= trim_bar.y && y < trim_bar.y + trim_bar.h);
            if (in_trim_bar) {
                // Plain trim-bar press. An endcap/bridge hit ARMS the trim drag
                // (a motionless release then runs that same click action at
                // on_button_release), IN EITHER TAB since 2026-08-07 — the
                // band's read-only return is deleted below. Either
                // way the trim bar CONSUMES the press — it never falls to the
                // marker handling.
                // A PLAIN TRIM-BAR CLICK THAT NEVER BECOMES A DRAG IS NOW A
                // CONSUMED NOTHING (architect 2026-07-30). Its entire act was
                // PUBLISHING the trim window as a region highlight, and that
                // coupling is retired outright with the SPAN FORM — so the two
                // highlight-only arms that stood here (the read-only press's
                // direct sync and the writable UNCLAIMED-spot sync) are gone, and
                // so is the pre-route stop that served them: keeping either would
                // make a no-op click stop an audition and destroy a selection for
                // nothing. The drag keeps both — it takes the setter's deselect
                // and the trim-mutation stop at its FIRST ACCEPTED bound change
                // (input_trim.cpp), which is where a trim commit actually happens.
                //
                // THE SPAN-FRAMING DOUBLE-CLICK, rehomed onto this band from the
                // deleted zoom lane (architect 2026-07-31): the whole band —
                // endcaps, bridge and empty space alike — carries it, since it
                // frames rather than grabs. It is pure navigation and allowed in
                // read-only for that reason, which it was before the whole band
                // became read-only-legal and still is (all modal gates sit far
                // above); no bound is touched, and playhead and selection are
                // untouched. The surface tag is what keeps
                // a marker or editor candidate from consuming here, and the TEST
                // is shared with the history mode's own trim-bar double-click
                // (trim_bar_double_click_at) so the two cannot drift on the
                // gesture while running different commands on it. It DIVERGES
                // from the bare `0` key, which only ever reaches the whole song
                // (and, from there, `c`); this frames the region, else a proper
                // trim sub-window, else the whole song.
                //
                // CONSUME-BEFORE-ARM BECAME VERDICT-BEFORE-ARM (2026-08-15, the
                // act-at-lift sweep's last four acts): the framing ACT moved to
                // the motionless LIFT, but the VERDICT is still taken HERE,
                // FIRST, ahead of every arm — and that ordering is the whole
                // point rather than a leftover. A verdict is not an act: it
                // commits nothing and changes no state, it records what the
                // user's second press MEANT at the moment the timing test is
                // about (the candidate snapshot is gone by the lift, cleared at
                // the top of this frame). A TRUE verdict arms the FRAMING
                // PENDING ALONE and returns — no trim drag armed, no seed
                // recorded, exactly where the press-time consume returned.
                //
                // WHY THAT MATTERS MORE HERE THAN ANYWHERE ELSE: TRIM HAS NO
                // UNDO — it is excluded from the stacks by ruling. Today the
                // second press of a double-click physically cannot become a trim
                // drag, and if a true verdict also armed the endcap / bridge
                // drag, a double-click whose second press drifted 8 px would
                // silently move a bound with no way back. Preserving the
                // ordering preserves that guarantee exactly; a crossing on the
                // framing pending commits nothing.
                if (trim_bar_double_click_at(dc_at_press, x, y)) {
                    arm_pending_click_act(PendingClickKind::TrimBarFraming,
                                          x, y);
                    return;
                }
                // SEEDING is a RELEASE act (only the release knows the press
                // stayed still), so the press records its point and the release
                // decides — see TrimBarPressSeed. It was recorded above this
                // band's read-only return while that return existed, so that a
                // locked tab framed without arming; the return is gone and the
                // ordering no longer carries a rule.
                app.trim_bar_press = TrimBarPressSeed{
                    .active = true, .press_x = x, .press_y = y};
                // THE BAND'S READ-ONLY RETURN IS DELETED (architect 2026-08-07):
                // it was the sole read-only defense for the whole trim-bar band,
                // and the ruling removed the thing it was defending — read-only
                // protects the AUTHORED MUSICAL CONTENT (the marker stores and
                // the engine settings), while trim is BAND, no more locked than
                // the viewport or the zoom beside it in ViewState. So a locked
                // tab arms the endcap and bridge drags here exactly as a
                // writable one does, route_trim_bar_press below still carries no
                // read-only check (it never did), and the trim CURSOR cues over
                // this band follow by construction, reading those same routers.
                // The full ruling is at read_only_key_blocked
                // (input_key_dispatch.cpp).
                route_trim_bar_press(x, y);
                return;
            }
            if (mh_index >= 0) {
                // THE MARKER CLICK ACTS AT THE LIFT (architect 2026-08-15:
                // "all actions should be on mouse-up / finger-up" — the
                // act-at-lift sweep, which began at the chrome roster
                // 2026-08-13 and reached the overview lane's teleport and this
                // flag on the same day, reaching the surface it was never
                // scoped to). THE PRESS ARMS AND COMMITS NOTHING: no stop, no
                // selection change, no land, no region clear, no editor. All of
                // it moved into ONE act owner, run_marker_click_act, which the
                // MOTIONLESS RELEASE runs whole and the THRESHOLD CROSSING runs
                // minus its double-click half before beginning the drag — so a
                // drag's user-visible outcome is byte-identical to the
                // press-time model's, and only a click's TIMING changed.
                //
                // THE ARCHITECT'S OWN COUNTER-ARGUMENT IS SUPERSEDED, NOT
                // DELETED: "marker work keeps its immediacy: flag clicks act at
                // press time, unlike the navigation surface's deferred click"
                // was the recorded reason this surface stood outside the sweep,
                // and his later ruling overrides it. The contract, the carried
                // modifiers and the carried double-click verdict are at
                // PendingMarkerPress (app_state.h).
                //
                // ALL THREE SHAPES ARM through one owner — plain, Shift and the
                // ctrl-exact toggle far above — because each now has an act to
                // carry. The two AUTHORING gates (read-only, home view) guard
                // the DRAG and live at the crossing, never here: a locked tab
                // and an off-home column still select and still land.
                arm_marker_press(mh_index, x, y, shift, /*ctrl=*/false,
                                 dc_at_press);
            } else {
                // Empty top-strip spot — no marker flag under the point (the
                // trim bar already returned above; mh_index < 0 here).
                // ONE lane to test: the flag and triangle lanes became the
                // single marker lane in row 5.
                const GuiRect marker_lane = top_marker_row_area(app);
                const bool in_flag_or_tri =
                    y >= marker_lane.y && y < marker_lane.y + marker_lane.h;
                if (in_flag_or_tri) {
                    // The empty marker-lane stretch — the navigation surface's
                    // lane member (architect 2026-08-12, the eighth glass
                    // ruling; the press-time parity placement of 2026-07-23 is
                    // superseded by the deferred model). Plain by construction
                    // here: shift was the former's claim far above, ctrl and
                    // alt the strict-modifier discard. TWO acts:
                    // A DOUBLE-CLICK consume creates a marker at the clicked
                    // position, SELECTS it and LANDS the playhead on it — the
                    // AUGMENTED drop, the same and only drop bare `s` performs
                    // (architect 2026-07-28: the lane double-click reuses the
                    // keyboard's machinery, so it follows it — the drop itself
                    // single-selects and re-seats the playhead, so one body
                    // serves both routes). THE CREATE RUNS AT THE MOTIONLESS
                    // LIFT since 2026-08-15 (the act-at-lift sweep's last four
                    // acts — PendingClickAct, app_state.h), and its VERDICT is
                    // still taken HERE, ahead of the nav arm: verdict-before-arm,
                    // the trim bar's own double-click precedent read at the same
                    // strength, so the second press arms the CREATE PENDING
                    // ALONE and can still never become a pan. Letting a true
                    // verdict fall through to the nav arm would have been a
                    // second spelling of the one rule, and the pan is not worth
                    // one. A crossing on the create pending commits nothing.
                    // Otherwise the press is the PENDING CLICK / GRAB-PAN,
                    // exactly the upper half's: nothing at press, the deferred
                    // click act at a motionless release — WHICH ALSO SEEDS the
                    // EmptyLane candidate there (the release-side owner; a
                    // crossed pan seeds nothing) — and the captured pan past
                    // the threshold.
                    // NO STOP anywhere on this path, deliberately: a live
                    // session RESEEKS to the placed playhead at the deferred
                    // click (run_nav_click_act's placement body) exactly as it
                    // does on the waveform upper half, so a double-click drop
                    // lands over a live session without cutting it off. Do not
                    // add a stop to make this branch look like its siblings —
                    // the omission IS the ruling.
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::EmptyLane &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        arm_pending_click_act(PendingClickKind::EmptyLaneCreate,
                                              x, y);
                        return;
                    }
                    arm_nav_press(x, y, /*history=*/false,
                                  /*seed_empty_lane=*/true,
                                  /*scrub_release=*/false);
                    return;
                }
                // Every other empty top-strip spot: NOTHING AT ALL — no
                // playhead, no marker, no selection or region change, and no
                // playback effect either, because this press claimed nothing
                // and the stops all live at the claims. That covers the
                // inter-lane gaps and any press in the FLEXIBLE GAP 1 band
                // between the menu row and the centered block (the centering
                // rule's window ground, the relayout's commit B: inside
                // top_strip_area but in no lane, so it falls to exactly this
                // return with no code of its own, and the cursor map's plain
                // top-strip fall-through answers Arrow over it the same way —
                // the band was between the icon row and the trim lane when the
                // seventh ruling opened it and at the window's foot in
                // between). A box under the point is a marker hit and never
                // reaches this branch, and the OVERVIEW STRIP's lane was
                // claimed far above.
                return;
            }
            return;
        }

        // Waveform-area press: marker-blind — the waveform resolves NO marker
        // on any press (the stems are pointer-inert since 2026-08-12, and
        // hit_test_flag runs only for top-strip presses), so a press over a
        // stem column is the ordinary press for its half. PLAIN ONLY here by
        // construction: the SHIFT former claimed BOTH halves far above, and
        // ctrl/alt claimed or discarded earlier.
        //
        // THE TWO HALVES ARE ONE SURFACE (architect 2026-08-13, superseding the
        // press-time scrub: "the playhead scrub is an outlier. We do everything
        // on lift the finger or on mouse up, but the playhead scrub, we do right
        // on mouse down. We should remove that"). BOTH halves arm the same
        // PENDING CLICK / GRAB-PAN — nothing at press, a crossed drag is the
        // captured pan — and the HALF, read once here and stashed on the
        // pending, picks which act a MOTIONLESS release runs: the UPPER half's
        // playhead placement or the LOWER half's audition scrub. That act is
        // the ONLY difference between the halves now, and it is TWO differences
        // read honestly, both pre-existing and neither touched by this ruling:
        // the placement also DESELECTS, dissolves a resting region and overrides
        // follow for the session, while the scrub act touches no selection, no
        // region, no cursor and no follow state at all (that is what makes it
        // the region's PREVIEW gesture — click inside a span to audition it and
        // the span rests).
        //
        // WHAT THE LOWER HALF GAINS BY BEING A PENDING: for the press's whole
        // life it is a live pointer gesture like the upper half's — the wheel
        // and every chord are swallowed, the follow chase is paused, and the
        // cursor holds the uniform Arrow — which is exactly the symmetry the
        // ruling asked for and not a new rule of its own.
        //
        // BUT A STANDING REGION IS ASKED FIRST (2026-08-15 — the region becomes
        // the trim editor; the model is at RegionState, app_state.h). A hit
        // arms the region's own move / bound drag INSTEAD of the nav drag;
        // None falls straight through and nothing about the pan changed. THE
        // WAVEFORM ALONE ASKS — the ruler and the marker lane never reach this
        // arm and stay plain navigation surface, which is what keeps a pan
        // reachable while a region covers the waveform entirely.
        //
        // SHIFT AND CTRL BYPASSED IT ENTIRELY (both were claimed far above) and
        // keep their meanings: ctrl always zooms, and SHIFT ALWAYS DRAWS A NEW
        // REGION even inside one — the architect's reason being that a shift
        // press is still a press, which destroys the region anyway, so "shift
        // always draws" costs nothing and gives a way to redraw without
        // clearing first.
        //
        // NOTHING IS COMMITTED AT THE PRESS, exactly as the nav press commits
        // nothing: a MOTIONLESS press-release on the region is NOT a
        // manipulation but the waveform's ORDINARY CLICK ACT, run by the
        // release arm (which states which half clears and which does not). That
        // is the degenerate case's escape hatch — a full-window region has
        // nothing to pan and nothing to move until one click destroys it — and
        // the architect ruled it deliberately.
        {
            const RegionHit rh = region_manipulation_hit(x, y);
            if (rh != RegionHit::None) {
                app.region_edit_drag = RegionEditDragState{};
                app.region_edit_drag.active  = true;
                app.region_edit_drag.press_x = x;
                app.region_edit_drag.press_y = y;
                const int64_t lo =
                    std::min(app.region.a_frame, app.region.b_frame);
                const int64_t hi =
                    std::max(app.region.a_frame, app.region.b_frame);
                if (rh == RegionHit::Move) {
                    app.region_edit_drag.kind = RegionEditKind::Move;
                    // The grab offset: the pressed column's own frame back to
                    // the span's LO bound, so the grabbed spot stays under the
                    // pointer (the overview box pan's offset, in frames). ON
                    // THE PAINTED BASIS, through the drag's own conversion
                    // owner — the same one every motion event takes, so the
                    // offset and the tracking are measured in one epoch
                    // (region_edit_frame_at_column; its comment carries the
                    // rule). The geometry it needs is already established:
                    // region_manipulation_hit answered non-None just above,
                    // which requires a positive area width and a positive
                    // plate spp.
                    app.region_edit_drag.grab_offset =
                        region_edit_frame_at_column(area, x) - lo;
                    app.region_edit_drag.span_frames = hi - lo;
                } else {
                    // A bound drag holds its PARTNER for the gesture's life —
                    // the trim endcap drag's fixed-opposite shape.
                    const bool lo_bound = rh == RegionHit::BoundLo;
                    app.region_edit_drag.kind = lo_bound
                        ? RegionEditKind::BoundLo : RegionEditKind::BoundHi;
                    app.region_edit_drag.fixed_frame = lo_bound ? hi : lo;
                }
                return;
            }
        }
        arm_nav_press(x, y, /*history=*/false, /*seed_empty_lane=*/false,
                      /*scrub_release=*/waveform_lower_half(area, y));
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
        if (g.dialog) viewport.invalidate_modal_dialog_area();
        else          viewport.invalidate_top_strip();
    }
    app.editor_text_drag.active = false;
}

void GuiInputHandler::arm_region_drag_at(int64_t anchor_frame, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
    // Clear any resting region immediately at press: the former
    // dissolves an existing highlight at its press/begin (the plain
    // canvas ground repaints back now, not at release). A
    // moved drag rebuilds a fresh region live; a
    // motionless press-release simply leaves it cleared, and an Esc mid-drag
    // changes nothing at all (no cancel) — the dissolve at mouse-down is final
    // either way. Same dissolve shape as
    // the navigation clears, so it shares clear_region_highlight. (The
    // one-day RULER arm's deferred dissolve — RegionDragState::ruler and the
    // motion path's crossing act — died 2026-08-12 with the ruler former
    // itself, superseded by pan-primary: the deferral pattern lives on in the
    // PLAIN pending click, ScrollDragState, which arms no region at all.)
    clear_region_highlight(app, viewport);
}

// ARM THE NAVIGATION SURFACE'S PLAIN PRESS — the pending click / grab-pan
// (contract at ScrollDragState, app_state.h). The press records its point and
// its surface facts and does NOTHING ELSE: no capture (that begins at the
// threshold crossing, so a click never blinks the cursor), no playhead, no
// deselect, no dissolve, NO SCRUB — nothing pops at press. The three surface
// facts are the press's, because only the press knows where it landed:
// `history` marks the `h` view's arm (the deferred act is the mode's land);
// `seed_empty_lane` marks the marker lane's empty stretch (the motionless
// release seeds the marker-create double-click candidate beside its click
// act); `scrub_release` marks the waveform's LOWER half (the motionless
// release runs the audition scrub instead of the placement — 2026-08-13). The
// three are mutually exclusive by geometry: a lane is not the waveform, and
// the `h` view has no scrub half.
void GuiInputHandler::arm_nav_press(int x, int y, bool history,
                                    bool seed_empty_lane, bool scrub_release) {
    app.scroll_drag = ScrollDragState{};
    app.scroll_drag.active          = true;
    app.scroll_drag.press_x         = x;
    app.scroll_drag.press_y         = y;
    app.scroll_drag.last_x          = x;
    app.scroll_drag.history         = history;
    app.scroll_drag.seed_empty_lane = seed_empty_lane;
    app.scroll_drag.scrub_release   = scrub_release;
}

// THE CTRL ENTRY TO THE SAME ONE DRAG (2026-08-14, the live-ctrl model —
// contract at ScrollDragState, app_state.h): the ordinary nav press, opened
// in the ZOOM phase. `ctrl_entry` is the press-time record — the deferred
// click act is NOT armed, a ctrl click never having been the placement — and
// the pivot seats at the press column, where the anchor stem paints FROM THE
// PRESS (the stem-at-press ruling kept across the unification; the arm owes
// that first frame's full waveform-area damage, the discrete shape). The
// CAPTURE does not begin here: it begins at the 8px crossing whatever the
// mode, so a ctrl click never blinks the cursor — superseding the retired
// dedicated zoom drag's capture-at-press, the unification's own rule.
void GuiInputHandler::arm_nav_zoom_press(int x, int y) {
    arm_nav_press(x, y, /*history=*/false, /*seed_empty_lane=*/false,
                  /*scrub_release=*/false);
    app.scroll_drag.ctrl_entry = true;
    app.scroll_drag.zooming    = true;
    // The seat is the SONG FRAME under the pointer's notional column — the
    // same projection and the same conversion every later ctrl-down edge
    // makes, so the press is not a second recipe. No capture is live at a
    // press, so that position is the press point.
    app.scroll_drag.anchor_sample =
        static_cast<double>(app.viewport_start_sample) +
        nav_notional_col() * current_samples_per_pixel(app, audio);
    viewport.invalidate_waveform_area();
}

// THE DEFERRED CLICK ACT — what a motionless navigation-surface press does at
// its RELEASE (the eighth glass ruling's deferral: the press could not know it
// was a click until the release said so, and a pan must move no playhead and
// clear nothing, so everything the old press-time placement did moved here
// whole). Runs at the PRESS column — sub-threshold travel is jitter, and the
// press point is what the user aimed at. Playback state is read HERE, at the
// act: the press touched nothing, so the readings agree with a press-time
// capture, and a session that ended naturally under the hold reads honestly
// (the drag-modal keyboard gate swallows every chord while the pending
// stands, so no command can change the state in between).
//   LIVE arm: deselect-all, dissolve any resting span (a placement is a point
//   command — the clear-site rule at clear_region_highlight), then the
//   placement body — playhead to the column, live-session reseek, follow
//   override (place_playhead_at_click_column). A GUTTER column deselects and
//   dissolves but seats nothing, the placement body's own shape.
//   `h`-VIEW arm: the mode's land — clear the mode focus + selection (the
//   pair clearer, the deselect's mode analog; store selection untouched),
//   dissolve the span, then the same placement body. The empty-lane and ruler
//   stretches take this too since they are the extension: a click anywhere on
//   the surface moves the playhead, in the view as outside it.
//   SCRUB arm (2026-08-13, the waveform's LOWER half): ONE scrub act at the
//   press column and NOTHING ELSE — the act the lower half used to run at
//   mouse-down, moved here whole so that nothing on this surface pops at a
//   press any more. It is deliberately the FIRST arm and returns ahead of the
//   other two: the scrub selects nothing, dissolves nothing, moves no cursor
//   and overrides no follow, which is what keeps it the region's PREVIEW
//   gesture and is the halves' ONE difference (two, read honestly — the
//   omissions are the second). It cannot coincide with the `h` arm (that view
//   has no scrub half), and the scrub's own gutter no-op lives inside
//   scrub_press_at.
// NO REGION ARM on any path — the region former is SHIFT's, and a click is
// not a drag.
void GuiInputHandler::run_nav_click_act(int press_x, bool history,
                                        bool scrub_release) {
    const GuiRect area = waveform_area(app);
    if (scrub_release) {
        scrub_press_at(press_x - area.x);
        return;
    }
    if (history) {
        if (clear_history_mode_focus(app.history_mode)) {
            // A discrete command: full-window damage for the face swap, the
            // mode's placement-press shape.
            viewport.invalidate_all();
        }
        clear_region_highlight(app, viewport);
        place_playhead_at_click_column(press_x - area.x, playback.is_playing(),
                                       app.playhead_cursor_sample);
        return;
    }
    selection.clear_selection();
    clear_region_highlight(app, viewport);
    place_playhead_at_click_column(press_x - area.x, playback.is_playing(),
                                   app.playhead_cursor_sample);
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
    // THE REGION FORMER'S LIVE PRESS HALF — TWO CALLERS, re-derived by grep
    // 2026-08-12 at the touch half (the eighth glass ruling made the plain
    // presses the pending
    // pan, whose motionless release runs the placement WITHOUT the arm through
    // run_nav_click_act instead): the SHIFT-exact press on the navigation
    // surface — the upper waveform half, the ruler, the marker lane's empty
    // stretches — claimed once for the whole y-gate in on_button_press, and
    // the touch region begin's live arm (begin_touch_region — the region
    // hold's expiry at the finger's down point, the same surface through the
    // pan-zone query). The
    // clear runs FIRST, before the shared body's gutter early-return,
    // so an inert-gutter click (no column to seat a playhead) still deselects.
    selection.clear_selection();
    const int64_t sample = place_playhead_at_click_column(
        click_rel_x, was_playing, playhead_at_entry);
    if (sample < 0) return;
    arm_region_drag_at(sample, x, y);
}

void GuiInputHandler::create_marker_at_empty_lane(int click_rel_x) {
    // The empty marker-lane double-click, AT THE SECOND PRESS'S MOTIONLESS LIFT
    // since 2026-08-15 (the act-at-lift sweep's last four acts; its one caller is
    // run_pending_click_act, which passes the PRESS column). The body is
    // unchanged, and its two silent refusals below are therefore re-asked live
    // at the lift, which is the sweep's own rule.
    // CREATE the marker, SELECT it, LAND
    // the playhead on it (the architect's words, eighth glass ruling) — the
    // bare-`s` drop equivalent, and like bare `s` it is the AUGMENTED drop in
    // both columns — the copy-previous owner in W, the lead-in reset in P.
    // The select and the land are the DROP'S OWN acts (drop_marker /
    // drop_phase_reset_at_position single-select what they create and re-seat
    // the playhead), so one body serves the key and the click with no
    // divergence to record. Gated exactly like
    // the keyboard `s`: home-view (active_column_authoring_allowed) and read-only
    // refuse SILENTLY. Place the playhead on the clicked column first — the
    // first pair's deferred click act already moved it there at its release,
    // so this is a harmless same-value repeat that also covers any first
    // press whose placement was
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
// Pan for the grab-pan, the cue it wears by identity — and the drop above is what
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
    // pointer, and its items were the redesign's FIRST act-on-release surface
    // (the modal's dialog buttons joined it 2026-08-13, and the whole chrome
    // roster the same day). It
    // takes THIS RELEASE'S coordinates because it derives the acted-on item from
    // them under a live press claim, rather than trusting an arm that may have
    // been resolved before the item rects were published (the full argument is at
    // the definition).
    if (button == GuiMouseButton::Left && app.dropdown.open()) {
        if (finish_dropdown_release(x, y)) return;
    }
    // THE CHROME ARM IS TAKEN WHOLE, above every gate below (2026-08-13, the
    // act-at-release conversion): the hold ends with this release whatever
    // consumes it, so the un-pressed face cannot be stranded by an early
    // return — and whether the act RUNS is decided further down, after the
    // modal gates have had their say, so a prompt raised under the hold
    // swallows the lift exactly as it swallows every other pointer event.
    AppState::ChromePress chrome{};
    if (button == GuiMouseButton::Left) chrome = take_chrome_press();
    // THE PROMPT DIALOG'S ACT, the press claim's other half (2026-08-13): the
    // lift on the button the press armed activates that response. A lift
    // ANYWHERE ELSE consumes the arm and dispatches nothing — which, since the
    // FEINT, is what leaves that button passively focused instead of simply
    // cancelling. The gates the act re-asks (the painted bit, the stash's
    // identity, and the live response set) all live in the one shared dispatch
    // body, which the keyboard's own release shares; the painter drops the arm
    // on those same edges, so the body is the second wall rather than the only
    // one. The veil still swallows every release either way — a CHROME arm
    // taken above dies here undispatched, which is the veil's answer.
    if (app.prompt.active) {
        if (button == GuiMouseButton::Left) {
            dispatch_modal_dialog_button(take_modal_dialog_release(x, y));
        }
        return;
    }
    // THE EDITOR DIALOG'S ACT, the same shape over the other surface: the lift
    // on the armed OK / Cancel runs the session's own Enter / Esc through the
    // one modal key route. Above the text-drag branch because the two are
    // mutually exclusive by construction (a press on a button never reaches
    // the field claim), and above the three editor swallows below, which is
    // where an unarmed release still ends.
    if (button == GuiMouseButton::Left && modal_dialog_editor_active()) {
        if (dispatch_modal_dialog_button(take_modal_dialog_release(x, y)))
            return;
    }
    // THE CHROME ACT, the roster's own release body (2026-08-13): the lift on
    // the armed button runs its chord — or the lock's `o`, or the walk tabs'
    // select — through the one release half, which re-hits the target at
    // these coordinates and re-asks every press-time gate, the veil included —
    // which is why this sits BELOW the editor dialog's own release and ABOVE
    // the editor swallows further down. Mutually exclusive with every gesture branch
    // below by construction: the press that armed chrome claimed the press
    // whole and armed nothing else.
    if (button == GuiMouseButton::Left &&
        chrome.kind != AppState::ChromePress::Kind::None) {
        finish_chrome_press_release(chrome, x, y);
        return;
    }
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
    // finishes something a LEFT press armed, and no other button arms anything —
    // the RIGHT button is fully unbound (2026-08-12, the eighth glass ruling;
    // its one-shot scrub of 2026-08-01 is deleted), so its press and release
    // are both consumed nothings by construction.
    if (button != GuiMouseButton::Left) return;

    // THE TRIM-BAR FRAMING DOUBLE-CLICK'S SEED, resolved for every left release
    // because only the release can tell a click from a drag. The press recorded
    // the trim-bar point (TrimBarPressSeed); this seeds the candidate when the
    // pointer never left the slack AND no trim drag went live — the two spellings
    // of "it stayed a click", equal by construction (kDoubleClickSlackPx ==
    // kDragMovedThresholdPx). A moved endcap/bridge drag therefore seeds nothing
    // and, its own press having cleared any candidate at the top-of-frame, leaves
    // none behind. The record is consumed either way.
    {
        const TrimBarPressSeed seed = app.trim_bar_press;
        app.trim_bar_press = TrimBarPressSeed{};
        if (seed.active && !app.trim_drag.active &&
            std::abs(x - seed.press_x) <= kDoubleClickSlackPx &&
            std::abs(y - seed.press_y) <= kDoubleClickSlackPx) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::TrimBar,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
    }

    if (app.scroll_drag.active) {
        // The navigation surface's press resolves at its release (the one nav
        // drag — contract at ScrollDragState, app_state.h). A MOVED drag ends
        // in whichever PHASE it was in: the pan's end is one predictor
        // re-anchor (the continuous pan deferred per-event resyncs), the zoom
        // phase's end is the final apply (resync + the one synchronous
        // rebuild inside apply_strip_drag_zoom's final path — which is also
        // the stem's erase — plus the moved-drag double-click drop, since a
        // zoom moves content between two clicks); either way the capture,
        // begun at the crossing, ends here and the cursor reappears as the
        // kind the LAST mode stamped — the stem column after a zoom-phase
        // end, the notional x after a pan-phase one. No click act on any
        // moved end: the drag was navigation.
        // THE MODE IS NOT RE-ASKED HERE, and does not need to be: every ctrl
        // edge — the motionless one included — has already run the switch
        // through sync_nav_drag_mode, so the cached bit and the stamped
        // restore both name the phase the gesture is really in. What is left
        // is ONE DISPATCH BATCH wide (a ctrl edge and this release arriving
        // with no loop tail between them) and falls in the accepted
        // post-unlock stale-cursor class: the platform drops every cursor
        // answer while it has no real pointer position, and the compositor's
        // next absolute event resolves it.
        // A MOTIONLESS press is THE DEFERRED CLICK — run_nav_click_act at the
        // press column, running THE PRESSED HALF'S OWN ACT: the upper half's
        // placement (deselect / mode-land, region dissolve, playhead, reseek,
        // follow override) or the lower half's audition SCRUB (2026-08-13),
        // plus the EmptyLane double-click seed when the press was the marker
        // lane's empty stretch (release-side seeding, the TrimBar pattern: only
        // the release knows it stayed a click). No capture ever began, so
        // nothing to end. THE ACT IS PRESS-TIME: a ctrl-armed press
        // (ctrl_entry) runs NO act — a ctrl click was never the placement —
        // and owes only its press-painted stem's erase; a plain-armed press
        // runs its act even with ctrl down at the release (press-time
        // modifiers arm the act, live modifiers steer the gesture — the
        // scoping statement at AppState::ChromePress::shift).
        const bool moved     = app.scroll_drag.moved;
        const bool zooming   = app.scroll_drag.zooming;
        const bool ctrl_arm  = app.scroll_drag.ctrl_entry;
        const bool history   = app.scroll_drag.history;
        const bool seed_lane = app.scroll_drag.seed_empty_lane;
        const bool scrub     = app.scroll_drag.scrub_release;
        const int  press_x   = app.scroll_drag.press_x;
        if (moved && zooming) {
            apply_nav_zoom_at(x, y, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        }
        app.scroll_drag = ScrollDragState{};
        if (moved) {
            if (!zooming && playback.is_playing())
                playback.resync_predictor();
            end_strip_pointer_capture();
            return;
        }
        // The motionless zoom-phase press painted a stem from the press (or
        // from a sub-threshold ctrl edge) and owes its erase — full
        // waveform-area damage, the discrete shape.
        if (zooming) viewport.invalidate_waveform_area();
        if (ctrl_arm) return;
        run_nav_click_act(press_x, history, scrub);
        if (seed_lane) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EmptyLane,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target  = -1};
        }
        return;
    }
    if (app.region_edit_drag.active) {
        // THE STANDING REGION'S OWN EDITOR (contract at RegionEditDragState,
        // app_state.h). A MOVED drag applied its span absolutely per event, so
        // ending is just ending: the last applied event stands, nothing is
        // committed to trim, no playhead moved and no capture is owed. It drops
        // any double-click candidate, the standing moved-drag rule.
        //
        // A MOTIONLESS PRESS-RELEASE IS NOT A MANIPULATION — it runs THE
        // WAVEFORM'S ORDINARY CLICK ACT at the press column, exactly what a
        // press one pixel outside the region would have done, each half's own
        // and unchanged: the UPPER half's playhead placement, which DISSOLVES
        // the span like every other point command, and the LOWER half's scrub,
        // which deliberately does not (the region's PREVIEW gesture). So the
        // architect's escape hatch for the degenerate case is the UPPER half's
        // click — a full-window region covers both halves, so it is always
        // reachable. The half is read from the PRESS point, the pending click's
        // own rule.
        const bool    moved   = app.region_edit_drag.moved;
        const int     press_x = app.region_edit_drag.press_x;
        const bool    scrub   =
            waveform_lower_half(waveform_area(app),
                                app.region_edit_drag.press_y);
        app.region_edit_drag = RegionEditDragState{};
        if (moved) {
            app.double_click = DoubleClickCandidate{};
            return;
        }
        run_nav_click_act(press_x, /*history=*/false, /*scrub_release=*/scrub);
        return;
    }
    if (app.overview_drag.active) {
        // The overview lane's box drag (contract at OverviewDragState). A
        // MOVED drag runs its final apply — the edge drag's terminating
        // apply_strip_drag_zoom (the one rebuild + predictor resync), the
        // pan's one deferred resync — and drops any double-click candidate,
        // the moved-drag rule.
        // A MOTIONLESS release forks on the kind, which is the redesign's
        // whole press-to-lift move (2026-08-15): a PENDING press — one made
        // OUTSIDE the box — runs THE TELEPORT HERE, at the PRESS column (the
        // point the user aimed at, the deferred click act's own rule), while
        // an inside-the-box or endcap press-release completes NOTHING, the
        // lane's v1 consumed nothing. A pending that MOVED runs the final apply
        // like any other kind and that apply is its own no-op, so a press
        // outside the box that drags ends here having committed nothing. No
        // capture to end, no stem to erase, and the lane seeds no double-click
        // candidate of its own.
        const bool moved = app.overview_drag.moved;
        const bool teleport =
            !moved && app.overview_drag.kind == OverviewDragKind::Pending;
        const int  press_x = app.overview_drag.press_x;
        if (moved) {
            apply_overview_drag_at(x, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        }
        // Disarmed BEFORE the act, so the teleport's viewport write runs with
        // no gesture live — the release bodies' standing shape.
        app.overview_drag = OverviewDragState{};
        if (teleport) run_overview_teleport(press_x);
        return;
    }
    // (No scrub branch of its own: since 2026-08-13 the scrub has no drag
    // state — it rides ScrollDragState like every other act on the navigation
    // surface, and the branch above runs it, at the LOWER half's motionless
    // release, through the `scrub` flag the press stashed. Its press-time
    // dispatch is deleted; the contract is at ScrollDragState, app_state.h.)
    if (app.region_drag.active) {
        // The region is extended live during the drag (see on_motion); a drag
        // that moved rests the region at its final extent. A MOTIONLESS
        // press-release (never crossed the threshold) needs no collapse here:
        // the shift former's press already cleared any resting highlight at
        // mouse-down (arm_region_drag_at), so a motionless shift click leaves
        // the region cleared and
        // there is nothing to do at release but disarm. A jitter drag that
        // crossed the gate but rests a sub-threshold sliver dissolves like a
        // click (end_region_drag_min_size_check), and only a MOVED drag runs
        // that check — a motionless release left the region cleared at arm, so
        // the check would
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
        // endcap/bridge press, which is now a CONSUMED NOTHING (architect
        // 2026-07-30). Its whole act was publishing the trim window as a region
        // highlight, and that coupling retired with the SPAN FORM; a deselect
        // and a playback stop left behind would be pure cost on a click that
        // commits nothing. Just disarm. (A crossed pending became app.trim_drag
        // and commits through the branch above, where the setter's deselect and
        // the trim-mutation stop live.)
        app.pending_trim_drag = PendingTrimDrag{};
        return;
    }
    if (app.pending_click.active()) {
        // THE MOTIONLESS LIFT OF THE SWEEP'S LAST FOUR ACTS — the two trim bound
        // sets, the trim bar's framing double-click, the empty lane's create
        // double-click and the `h` view's three diff-flag clicks (2026-08-15;
        // the contract is at PendingClickAct, app_state.h). One act owner, which
        // forks on the armed KIND and runs each act on the PRESS column / the
        // ARMED flag index, re-asking every gate live.
        // THE RELEASE BODIES' STANDING SHAPE: read the pending, DISARM, then
        // act, so the act runs with no gesture live.
        // (A crossed pending is spent at the crossing — a TrimBoundSet one
        // having run its set and become app.trim_drag, which commits through the
        // branch above; the other three having committed nothing at all.)
        const PendingClickAct press = app.pending_click;
        app.pending_click = PendingClickAct{};
        run_pending_click_act(press);
        return;
    }
    if (app.pending_marker_press.active) {
        // THE FLAG'S MOTIONLESS LIFT — the click act, whole (2026-08-15, when
        // the marker click moved off the press; the contract is at
        // PendingMarkerPress, app_state.h). All three shapes resolve here: the
        // ctrl toggle, the shift range and the plain single-select, plus the
        // plain arm's double-click consume-or-seed, run by the one act owner on
        // the ARMED marker — never a re-hit at these coordinates, for the three
        // reasons stated at run_marker_click_act.
        // THE RELEASE BODIES' STANDING SHAPE: read the pending, DISARM, then
        // act, so the act runs with no gesture live.
        // (A crossed pending is spent at the crossing — a plain one having
        // become app.drag, which commits through the branch below; a shift or
        // ctrl one having committed nothing at all.)
        const PendingMarkerPress press = app.pending_marker_press;
        app.pending_marker_press = PendingMarkerPress{};
        run_marker_click_act(press, /*at_lift=*/true);
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
    if (app.scroll_drag.active) {
        // The one nav drag / pending click. A MOVED drag applied its motion
        // continuously in either phase, so ending is just ending: one
        // predictor re-anchor, one synchronous rebuild when the zoom phase
        // was live (there are no release coordinates here, so no final
        // apply — the strip arm's own force-end shape), and the capture end
        // (begun at the crossing). An UNMOVED press merely
        // DISARMS — a force-end is not a click, so the deferred click act does
        // NOT run, the same commit-vs-disarm asymmetry the two pendings below
        // hold (a pending has committed nothing, and there is nothing owed).
        // A zoom-phase stem — painted from a ctrl press or a ctrl edge —
        // owes its erase on every one of these ends.
        const bool zooming = app.scroll_drag.zooming;
        if (app.scroll_drag.moved) {
            if (playback.is_playing()) playback.resync_predictor();
            if (zooming) viewport.kick_waveform_sync();
            end_strip_pointer_capture();
        }
        app.scroll_drag = ScrollDragState{};
        if (zooming) viewport.invalidate_waveform_area();
    }
    if (app.overview_drag.active) {
        // The overview box drag applied its motion continuously (absolute
        // per-event placement through the clamp chokepoints), so ending is
        // just ending: a MOVED drag owes one predictor re-anchor — the pan's
        // per-event resyncs were deferred (continuous scroll) and the edge
        // drag's mid-gesture applies skipped them the same way — and nothing
        // else: no capture, no stem, no act. An UNMOVED press merely
        // DISARMS and commits nothing — a force-end is not a click, so a
        // PENDING outside press runs no teleport (2026-08-15, when that act
        // moved to the lift; it was a committed press-time move before, and
        // this arm said so). It is now exactly the pendings' own shape below.
        if (app.overview_drag.moved && playback.is_playing())
            playback.resync_predictor();
        app.overview_drag = OverviewDragState{};
    }
    // The region's own editor applied its span continuously and owes NOTHING at
    // an end — no capture, no stem, no predictor (it moves no viewport and no
    // playhead), and nothing committed to trim in any case. An UNMOVED press
    // merely DISARMS: a force-end is not a click, so its motionless-release
    // click act does not run, exactly the pendings' shape below.
    app.region_edit_drag = RegionEditDragState{};
    // THE PENDINGS DISARM AND COMMIT NOTHING, which is not a cancel: there is
    // no release here (the button is still held), and a force-end is not a
    // click — the same rule the four arms above state for their own unmoved
    // presses, and the same one the touch layer's ABNORMAL end (the
    // motionless-hold upgrade) delivers by leaving the button unheld. A pending
    // otherwise resolves only by the threshold crossing or a real release /
    // button loss. (TWO pendings, not three, since the tempo drag's deletion —
    // 2026-07-29, see marker_drag.h.)
    //
    // THE 2026-07-29 ASYMMETRY RECORDED HERE IS GONE, AND ITS PREMISE IS WHY IT
    // WAS RIGHT AT THE TIME. It read: live gestures COMMIT here while pendings
    // merely DISARM, because "a pending has committed NOTHING of its own — the
    // marker pending's click committed at the PRESS" — architect-accepted ("not
    // a real use case - do whatever is easiest to code and has least
    // loopholes") with an explicit instruction not to add a completion arm to
    // make the two look alike. THAT WAS TRUE OF THE PRESS-TIME CLICK AND IS
    // FALSE OF THE LIFT-TIME ONE: since 2026-08-15 the marker pending carries
    // the whole click, so the question is no longer "commit or disarm" but "did
    // the user finish the gesture", and a force-end is precisely the answer no.
    // The marker pending therefore ends exactly as every other pending does —
    // the asymmetry the old rule protected has no subject left, rather than
    // having been overruled. (The parenthetical that stood here — "the
    // bound-set trim pending is unchanged: its bound was written at the press
    // and stands, trim being history-less" — was true of the press-time bound
    // set and went stale on 2026-08-15, when that set moved to the lift too; the
    // sweep's own pending is handled a few lines below.)
    app.pending_marker_press = PendingMarkerPress{};
    app.pending_trim_drag    = PendingTrimDrag{};
    // THREE pendings since 2026-08-15, when the act-at-lift sweep's last four
    // acts joined this shape in ONE record (PendingClickAct): the trim bar's two
    // bound sets and its framing double-click, the empty lane's create
    // double-click and the `h` view's diff-flag clicks. Every one of them
    // commits NOTHING here for the reason stated above — a force-end is not a
    // click — and that is a real change of outcome for the bound sets, whose
    // press used to have WRITTEN the bound already (the sentence in parentheses
    // below was true of that model and is why it was there). Trim is still
    // history-less; what changed is that there is now nothing to be history-less
    // about on this path.
    app.pending_click        = PendingClickAct{};
    // A force-end is not a clean click sequence, so no candidate may survive to
    // pair with a later click (the standing rule at every non-release gesture
    // end) — and neither may the trim bar's press record, which would otherwise
    // seed one at the next release.
    app.double_click   = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
}

// THE REDESIGNED BUTTONS' HOVER, in ONE transition writer over the whole roster
// (row 1's File / Navigation / Settings and the view bar's three, row 3's two
// tabs, row 4's twenty-six — the toolbar four included since the 2026-08-12
// relayout — and the bottom row's fourteen: 48, the enum's
// own count at kRedesignButtonCount — the stash is
// AppState::redesign_buttons; an UNPAINTED bottom-row cluster member's zero
// rect resolves unhovered with no arm here).
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
        viewport.invalidate_rect(bottom_row_area(app));
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
    // THE DIALOG'S VEIL (2026-08-12): under a PROMPT or an EDITOR dialog the
    // WHOLE roster is refused — nothing behind the modal is pressable, so
    // nothing hovers. It was two rules until 2026-08-13, the editor half
    // admitting the reach-through's own buttons; the reach-through is retired
    // (the record is at its deleted predicate's site near the head of this
    // file) and the two collapsed into this one. The pointer-transparent FLAG
    // editor raises no veil: it is not a dialog and its roster presses were
    // never blocked.
    const bool modal_veil =
        app.prompt.active || modal_dialog_editor_active();
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
        const bool under_pointer = !modal_veil &&
                                   rect_contains(f.rect, mx, my) &&
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
    // THE ARM'S INSIDE BIT — the feint's chrome half (2026-08-13, the modal
    // arm's press_inside on this surface), maintained here because this walk
    // is the roster's one per-motion-and-per-tick derivation: with a chrome
    // press armed, answer whether the pointer is inside the armed target's
    // own published rect. The pressed face paints only while it is true
    // (redesign_button_pressed_face), so sliding off un-presses and sliding
    // back on re-presses, the arm itself standing for the whole hold; the
    // RELEASE re-hits the rect at its own coordinates and never reads this
    // bit. Raw geometry deliberately — no veil, no zone, no enabled term:
    // those gates belong to the arm's creation and to the lift's re-ask, and
    // the bit answers only where the pointer is.
    if (app.chrome_press.kind != AppState::ChromePress::Kind::None) {
        bool inside = false;
        switch (app.chrome_press.kind) {
        case AppState::ChromePress::Kind::None:
            break;
        case AppState::ChromePress::Kind::Roster:
        case AppState::ChromePress::Kind::HistoryWalkTab:
            inside = rect_contains(
                app.redesign_buttons[static_cast<size_t>(
                                         app.chrome_press.index)].rect,
                mx, my);
            break;
        }
        if (inside != app.chrome_press.inside) {
            app.chrome_press.inside = inside;
            // Only a Roster arm with a click face paints a pressed interior,
            // and its home strip pays — the row fork the face writers all
            // take. (HistoryWalkTab and the two-face rows paint
            // none; their flip costs nothing.)
            if (app.chrome_press.kind == AppState::ChromePress::Kind::Roster &&
                roster_index_click_face(app.chrome_press.index)) {
                if (redesign_button_in_transport_row(static_cast<RedesignButton>(
                        app.chrome_press.index)))
                    changed_transport = true;
                else
                    changed_top = true;
            }
        }
    }
    // Row 8's damage fork: a transport face damages its own bottom-strip lane,
    // every other face the top strip — each strip pays only for its own.
    if (changed_top)       viewport.invalidate_top_strip();
    if (changed_transport)
        viewport.invalidate_rect(bottom_row_area(app));

    // THE TOOLTIP'S DWELL STAMP, written here because this is the one place that
    // knows a hover STARTED. EVERY roster button but ROW 1'S carries a tooltip,
    // and redesign_button_tooltip's stateful overload owns both that membership
    // and the three things state moves in it (re-derived from that one function):
    // MEMBERSHIP moves exactly ONCE — row 3's WALK-SELECTOR tabs drop their hint
    // entirely while the `h` view stands, on the view bar's own reasoning — while
    // the TEXT follows state on THREE buttons: SAVE (a publishing checkpoint
    // first, then the history view's "Save and Commit"), RENDER (the
    // mid-render Cancel, then the iteration bit) and, since 2026-08-15, THE
    // BOTTOM ROW'S COLLAPSED PLAY/STOP BUTTON (the live audition bit, the same
    // condition its glyph reads).
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
    //
    // A STANDING DIALOG'S DWELL IS THE DIALOG'S (2026-08-13, when the modal's
    // buttons took tooltips): this walk answers for the ROSTER's index space
    // only, so under a dialog it neither stamps (`hovered_tip` is forced -1 by
    // the veil and the no-dwell rule above) nor hides — the modal's own walk
    // owns that surface's dwell. It DOES hide a ROSTER dwell caught by the
    // dialog's open, which is a backstop rather than the mechanism (the open
    // edge is a key press or a pointer press, and both hide already). The test
    // reads the LIVE surfaces, not the painted stash, so the frame a dialog
    // closes on is the frame this walk takes the dwell back — a stash-based
    // test would leave the last hint floating for one more paint.
    if (modal_veil) {
        if (app.redesign_tooltip.owner.surface ==
            AppState::RedesignTooltip::Surface::Roster) {
            hide_shift_tooltip();
        }
        return;
    }
    // Keying on the id is what makes a direct Render->Paste motion wait the
    // full delay again instead of inheriting the dwell that was already
    // running; the rule itself is at arm_tooltip_dwell.
    arm_tooltip_dwell({AppState::RedesignTooltip::Surface::Roster,
                       hovered_tip});
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

// THE CHROME PRESS'S ARM — the press half of act-at-release (architect
// 2026-08-13; the authoritative statement of the rule is
// kdenlive-redesign.md's act-at-release section, the state's contract at
// AppState::ChromePress) for every redesigned button whose action IS a chord —
// rows 1, 3 and 4 and the bottom row, driven entirely by kToolbarChords'
// per-button flags so no row carries a special case of its own. Returns true
// when a button's rect claimed the press, whether or not anything was armed (a
// refusal is still a consumed nothing, which is what the band claims want).
//
// THE PRESS DISPATCHES NOTHING. It applies the refusals that were always
// press-time — the shift admission, the disabled consume, the radio consume —
// and then ARMS: the index, the PRESS-TIME SHIFT (the release deliberately
// does not re-read modifiers, the modal release's own rule — the
// shift-admitting buttons must see the shift held at the press), and the
// feint's inside bit. The lift runs the act through
// finish_chrome_press_release below, which re-asks every gate.
bool GuiInputHandler::arm_redesign_press(int x, int y, GuiInputState mods) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (!redesign_button_hit(app, tc.id, x, y)) continue;
        // A SHIFT PRESS ON A BUTTON WITH NO SHIFTED CHORD is a consumed nothing
        // — never the unshifted action, which would be a silent lie about what
        // the modifier did (the flag's rationale is at its declaration). No
        // arm, no face: the release could only refuse it again.
        if (mods.shift && !redesign_button_shift_admits(tc.id)) return true;
        // A DISABLED BUTTON'S PRESS IS A CONSUMED NOTHING: nothing arms, so
        // nothing can dispatch at the lift, and a SHIFT press is swallowed
        // exactly like the plain one (one predicate, both routes — a greyed
        // Render is greyed for both of its chords). The predicate is the
        // painter's (redesign_button_enabled, app_state.h), so the greyed face
        // and the inert press are the same fact read twice and the press
        // cannot slip through on a frame the paint disagreed with. Rows 1, 3
        // and 4 have no disabled face of their own, so the predicate is simply
        // true there — EXCEPT while the `h` history view stands, which greys
        // every button whose act it consumes across all the rows and is
        // therefore the one state in which this line consumes a row-1, row-3 or
        // row-4 press (history_mode_disables_button, above). THE BOTTOM ROW HAS
        // NO RESTING CONSUMER HERE AT ALL since 2026-08-15: all ten of its
        // buttons are lit outside the `h` view by the architect's scoped-truth
        // ruling (the arrows for the per-selection blink, the two skips
        // because their key acts even on a no-op jump, PLAY / STOP last —
        // "the user is expected to know that with the playhead outside trim
        // it's not going to play in target view" — and the marker-walk three
        // on the row's settled policy when they landed), so this line fires
        // for them only inside that view, where the derived partition greys
        // the transport button ALONE (Space is consumed there; the walk three
        // are the mode's own cycles). Everywhere else their chords do their
        // own refusing and a refused click is a consumed no-op, which is the
        // roster's standing shape.
        if (!redesign_button_enabled(app, audio.total_frames(), tc.id))
            return true;
        // A RADIO ALREADY SELECTED HAS NOTHING TO SWITCH TO, and its chord is a
        // TOGGLE — dispatching would switch AWAY from what the user just
        // clicked. So the press is a consumed nothing, which also makes "no
        // button was hit" and "the selected half was hit" the same silent
        // outcome. Toggles (follow, iteration) are NOT radios and press through
        // in both directions.
        if (tc.radio && redesign_button_selected(app, tc.id)) return true;
        // THE ARM. The pressed face paints from it on the very next frame
        // (redesign_button_pressed_face — Roster kind, inside true, and the
        // row's click_face column deciding whether a pressed interior exists
        // to paint at all); the damage below is skipped for the two-face rows
        // that paint none. A SHIFT press arms too — it is the same physical
        // hold, and the carried bit is what the lift dispatches with. THE
        // PRESS'S CLOCK IS STAMPED HERE, unconditionally: the lift measures the
        // hold against kChromeShiftHoldMs to decide the SHIFT LONG PRESS, and
        // the stamp is taken for every button rather than for the four that can
        // use it, a press having a time whatever it landed on.
        app.chrome_press = AppState::ChromePress{
            AppState::ChromePress::Kind::Roster,
            redesign_button_index(tc.id), mods.shift, true, monotonic_ms()};
        if (tc.click_face) {
            // DAMAGE FOLLOWS THE ROW'S HOME STRIP (row 8, 2026-08-11): the
            // transport row's pixels live in the BOTTOM strip, so its click
            // face damages its own lane where every other row damages the top
            // strip — the same fork every face writer takes.
            if (redesign_button_in_transport_row(tc.id))
                viewport.invalidate_rect(bottom_row_area(app));
            else
                viewport.invalidate_top_strip();
        }
        return true;
    }
    return false;
}

// THE ARM, TAKEN WHOLE — on_button_release's first act on every left release,
// armed or not, so a release consumed by any gate below it (the veil, an
// editor swallow) still ends the hold and un-presses the face. The caller owns
// what happens next; this owns only the state and the face's damage.
AppState::ChromePress GuiInputHandler::take_chrome_press() {
    const AppState::ChromePress arm = app.chrome_press;
    app.chrome_press = AppState::ChromePress{};
    if (arm.kind == AppState::ChromePress::Kind::Roster && arm.inside &&
        roster_index_click_face(arm.index)) {
        // The pressed face is painted; erase it through the row fork.
        // (HistoryWalkTab and the two-face rows paint none — no
        // damage owed.)
        if (redesign_button_in_transport_row(
                static_cast<RedesignButton>(arm.index)))
            viewport.invalidate_rect(bottom_row_area(app));
        else
            viewport.invalidate_top_strip();
    }
    return arm;
}

// THE CHROME ACT, AT THE LIFT — the release half of act-at-release (architect
// 2026-08-13). The lift runs the act iff it lands ON the armed target — the
// published rect re-hit at the release's own coordinates, the derive doctrine
// (the arm's `inside` bit serves the paint alone) — and iff every press-time
// gate still holds, re-asked here exactly as the modal dialog's release
// re-asks its own: the surface may have changed under the hold (a dialog
// opened by a key, the history view toggled, a button disabled), and an arm
// must never outrank the live state. A lift anywhere else, or any gate gone,
// dispatches nothing — the consumed-nothing the press would have been.
//
// THE BUTTON IS ITS CHORD, dispatched through on_key: the action is not merely
// the same FUNCTION the key calls, it is the same ROUTE — every gate the chord
// passes on the keyboard (the loading/blank return, the keyboard-modal editor
// gate, the read-only allowlist, the arm's own refusals) applies here, in the
// same order, with nothing restated and nothing that can drift. It is the
// exact inverse of the platform's bare-`e`-as-left-button translation: one
// vocabulary expressed on the other's surface, at a boundary. WHAT MOVED is
// only WHEN it fires — the keyboard's own chords still dispatch on the key
// PRESS; button-is-its-chord is about what a button runs, not when.
void GuiInputHandler::finish_chrome_press_release(
        const AppState::ChromePress& arm, int x, int y) {
    switch (arm.kind) {
    case AppState::ChromePress::Kind::None:
        return;
    case AppState::ChromePress::Kind::HistoryWalkTab: {
        // The `h` view's walk selector: the lift on the armed tab selects its
        // walk, passing the CURRENT reading through unchanged (the one switch
        // owner's radio rule — idempotent on the lit tab). The mode gate is
        // re-asked: a view closed under the hold selects nothing.
        if (!app.history_mode.active) return;
        const RedesignButton id = static_cast<RedesignButton>(arm.index);
        if (!redesign_button_hit(app, id, x, y)) return;
        set_history_reading(id == RedesignButton::TabA
                                ? GuiHistoryWalkSource::Commit
                                : GuiHistoryWalkSource::Local,
                            app.history_compare());
        return;
    }
    case AppState::ChromePress::Kind::Roster:
        break;
    }
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_index(tc.id) != arm.index) continue;
        // The lift must land on the armed button itself.
        if (!redesign_button_hit(app, tc.id, x, y)) return;
        // THE VEIL, re-asked: under an editor dialog NO roster button may act.
        // It was a membership test until 2026-08-13, admitting the modal-trap
        // reach-through's own buttons; with that retired the veil is blanket
        // again and this line's whole job is the editor OPENED MID-HOLD — an
        // arm taken before the dialog rose must not fire into it. A PROMPT
        // never reaches this body at all — on_button_release's prompt gate
        // consumes the release above it.
        if (modal_dialog_editor_active()) return;
        // The press-time refusals, re-asked against the live state — the
        // shift admission under the CARRIED shift, the enabled bit, the radio
        // rule. Each held at the press; any that no longer does makes the
        // lift a consumed nothing.
        if (arm.shift && !redesign_button_shift_admits(tc.id)) return;
        if (!redesign_button_enabled(app, audio.total_frames(), tc.id))
            return;
        if (tc.radio && redesign_button_selected(app, tc.id)) return;
        // THE RENDER BUTTON IS CANCEL WHILE A RENDER IS LIVE (architect
        // 2026-08-11) — THE ROSTER'S ONE RULED EXCEPTION TO "THE BUTTON IS ITS
        // CHORD": while app.render_cancel_face stands (the painted face's own
        // bit — the mirror of cancel_archival_session's predicate, contract at
        // its declaration) the lift runs THE CANCEL ACT ITSELF, the Esc
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
        // start a render from a button that says Cancel. THE SHIFT LONG PRESS
        // TAKES THE SAME ANSWER, and takes it by construction: this branch
        // returns ABOVE the chord build where the hold is measured, so a finger
        // held on a painted Cancel cancels exactly as a quick tap does. That is
        // the honest reading — the face says Cancel for the whole hold, and a
        // button that changed its act at some invisible mark while its label
        // stood still would be the lie this exception exists to prevent.
        if (tc.id == RedesignButton::Render && app.render_cancel_face) {
            // BOTH HALVES OF THE FACE-MIRRORS-THE-ACT HONESTY (the contract is
            // at the bit's declaration): the CLAIM reads the painted bit, so a
            // lift on a painted Cancel never dispatches a render; the ACT is
            // gated on the LIVE explicit-act bit, so on the stale edge it is a
            // consumed no-op and can never reach a PREVIEW session through
            // cancel_archival_session's wider is_busy branch — the face never
            // advertised one.
            if (app.queue_running) cancel_archival_session();
            return;
        }
        // THE SHIFT LONG PRESS (architect 2026-08-13): a press HELD past
        // kChromeShiftHoldMs on a shift-admitting button reaches that button's
        // shifted twin, the waveform region hold's shape on the roster's
        // surface. It exists for GLASS — the road rig has no keyboard, so
        // without it a finger could reach only the plain half of each shifted
        // pair — and it costs the desk nothing, a 500ms hold being well past
        // any ordinary click.
        //
        // THE MEMBERSHIP IS redesign_button_shift_admits AND NOT A LIST: the
        // hold reaches a twin exactly where a shift press does, so a button
        // with no shifted chord is held for as long as you like and still gets
        // its plain act — which is also why the term must carry the predicate
        // itself rather than lean on the admission gate above, that gate asking
        // only about the CARRIED bit.
        //
        // AND IT COMPOSES WITH A REAL HELD SHIFT rather than competing with it:
        // both routes feed the ONE shift term below, so a physical Shift+click
        // is exactly what it always was and the hold is a second way to the
        // same dispatch — holding a shift-clicked button changes nothing.
        //
        // Measured at the LIFT against the arm's own press stamp: no timer, no
        // tick, nothing polled, and no state beyond the int64 the arm already
        // carries.
        //
        // NO FEEDBACK IS NEEDED WHILE THE HOLD RUNS — architect-RULED
        // 2026-08-13, closing the open question this arc left rather than
        // leaving it as a gap: the gesture exists for the TOUCH PANEL, and
        // TOOLTIPS DO NOT SHOW THERE, so the surface the hint would have to
        // ride is one the gesture's only user never sees. The beat passes
        // silently, the act shows at the lift, and none is to be built.
        const bool held_to_shift =
            redesign_button_shift_admits(tc.id) &&
            monotonic_ms() - arm.press_ms >= kChromeShiftHoldMs;
        // The shift term ORs the table's own (Redo's Ctrl+Shift+Z) with the
        // CARRIED press-time bit and the hold — well-defined because no row
        // sets both the table bit and the admission (see shift_admits), so this
        // one expression spells both members of each shifted pair however the
        // user asked for the shifted one.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift || arm.shift || held_to_shift;
        chord.alt   = tc.alt;
        on_key(tc.key, chord);
        return;
    }
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
    // The two KINDS of menu differ in kind and each stays with its own table:
    // the COMMAND menus (Navigation, and File since 2026-08-13) dispatch a
    // chord, the SETTINGS one opens the editor prefilled.
    if (dropdown_is_command_menu(menu)) {
        // THE ITEM IS ITS KEY, dispatched through on_key exactly as a redesigned
        // button dispatches its chord: every gate the keyboard route passes
        // (loading/blank, the modal gates, the read-only allowlist, the arm's own
        // refusals) applies identically, so an item whose command cannot act
        // right now simply does nothing — the buttons-never-grey rule, one
        // surface further out. No stop, no modal, nothing restated here. THE
        // FILE MENU'S ONE ROW RIDES THIS BODY WHOLE: Ctrl+Q reaches on_key's own
        // close route — the drag-modal hatch, the dirty prompt, the WM-close
        // ordering — with no second body anywhere, which is the whole reason the
        // Quit BUTTON could be retired for a menu item without moving the act.
        // THAT IS ALSO HOW THE `h` HISTORY VIEW ANSWERS THESE MENUS (2026-08-08):
        // per item, at the mode's own two gates — its allowlist for the zoom and
        // framing rows and for Ctrl+Q, history_mode_owns_key for the rest — with
        // the one row that would mean something else in there greyed above and
        // never reaching this dispatch at all.
        const CommandPopupItem& it = command_popup_item(menu, armed);
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
// sliding onto the view bar is a step ACROSS the bar, not a dismissal —
// and it re-arms on the line after its call to this. It is the only site in the
// tree that writes that bit true outside toggle_dropdown's open.
// THE `h` HISTORY MODE's POINTER ALLOWLIST and its acts. True = the press is
// CONSUMED here (refused outright, or handled as one of the mode's own acts);
// false = the press is one of the navigation gestures the mode leaves alone, and
// on_button_press proceeds with it untouched.
//
// THE VIEW MIRRORS THE LIVE VOCABULARY THROUGH ITS OWN GATES (re-derived
// 2026-08-12 under the eighth glass ruling, pan-primary — the view admits
// pan/zoom, consumes authoring): its NAVIGATION SURFACE is the WHOLE waveform
// (both halves — the view has no scrub since playback left it whole,
// 2026-08-05) plus the RULER and the MARKER lane's empty stretches, and on it
// plain drag = the grab-pan, motionless plain click = THE MODE'S LAND at the
// column (deferred to the release like everywhere else), shift+drag = the
// view-local region former, ctrl+drag = the strip-drag zoom.
//
// THE VIEW IS SILENT (architect 2026-08-05): NO PRESS IN HERE STARTS AUDIO —
// the mode's full-song trim forces a full target preview render for an
// audition the view never needed. Playback removal is whole: Space left the
// keyboard allowlist in the same ruling, and the entry owner stops a session
// that was already running. (The mode's deferred click act runs the shared
// placement body, whose reseek arm is structurally dead in here.)
//
// WHAT PASSES THROUGH, the whole list — the mode's navigation vocabulary, the
// pointer half of what history_mode_key_blocked admits on the keyboard:
//   * CTRL-exact over the navigation surface — the dual-axis strip drag, on
//     its full grown surface (waveform + ruler + the lane's empty stretches;
//     a ctrl press on a diff FLAG is the mode's membership toggle, claimed
//     below before this can fork). Ctrl+Shift is NOT admitted anywhere: over
//     the waveform it is already a no-op, and over the TRIM BAR it sets the
//     end bound, which is a write. ALT passes nowhere — its pointer
//     vocabulary is empty product-wide.
//
// THE ACTS, all pure navigation:
//   * a PLAIN press on the NAVIGATION SURFACE arms the mode's own PENDING
//     CLICK / GRAB-PAN (arm_nav_press with the history flag): nothing at
//     press; a motionless release runs THE MODE'S LAND — clear the mode focus
//     + selection (the deselect's mode analog, through the one pair clearer),
//     dissolve any resting span (a placement is a point command), seat the
//     playhead at the press column (run_nav_click_act's history arm — the
//     live recipe through the shared placement body, whose reseek cannot fire
//     in the silent view); a crossed drag is the captured pan, which moves no
//     playhead and clears nothing. "A click anywhere on the extension moves
//     the playhead" holds in here too: the ruler and the empty lane stretches
//     take the same pending, so their motionless clicks land the playhead
//     where the old empty-lane click only cleared the focus.
//   * a DOUBLE-CLICK anywhere on the TRIM BAR band ZOOMS TO THE VIEWED
//     CHECKPOINT'S DIFF SPAN — the span that band is already displaying —
//     through the framing act the mode's edges stopped running in 2026-08-05
//     (frame_viewed_commit_diff_span, input_key_dispatch.cpp). Since 2026-08-08
//     it is the ONLY framing gesture a standing view has, the internal edges
//     writing no viewport at all and the window being the user's for the whole
//     visit. It moves the viewport and nothing else, and a SINGLE click on that
//     band stays the consumed nothing a motionless trim-bar click is everywhere.
//   * a press on a DIFF FLAG in the MARKER LANE takes the mode's focus (at most
//     one, painted in its class's selected pair) and LANDS THE PLAYHEAD on that
//     flag's authored frame, through the same owner every marker land uses —
//     AT THE MOTIONLESS LIFT, on the ARMED flag index, since 2026-08-15 (the
//     act-at-lift sweep's last four acts — PendingClickAct, app_state.h). It
//     lagged the LIVE views' three flag clicks by hours, having been called out
//     the same day as a separate press-time family "pending an architect ruling
//     of its own"; the ruling came, and it is the sweep's own: none of these was
//     ever RULED a press-time exception, they were simply never in scope. The
//     mode has NO drag for the press to become, so a crossing commits nothing.
//     It touches NOTHING else: no store selection,
//     no live focus, no auto-select, no playback stop. It DOES take a resting
//     region with it (2026-08-06): a click that lands the playhead is a POINT
//     command, the live clear-site rule's own shape.
//   * the MARKER LANE's two MODIFIED clicks, on its FLAG BOXES: SHIFT takes
//     the contiguous range from the focus, CTRL toggles one flag's membership,
//     and both then focus the clicked flag and land the playhead on it (the live
//     selection model over the mode's own list; the store selection stays as
//     untouched as the plain click leaves it). A modified lane press that hits
//     NO flag is the GESTURE the modifier names, exactly as in the live views
//     since the lanes became the extension: shift the former, ctrl the zoom.
//   * SHIFT-exact on the navigation surface — the VIEW-LOCAL REGION FORMER:
//     clear the mode focus + selection, seat the playhead at the column, arm
//     the region drag (the live former's recipe with the mode's deselect
//     analog — EVERY REGION FORMER DROPS THE SELECTION ITS SURFACE OWNS, the
//     family rule at RegionState, app_state.h). The drag rides the one motion
//     path, playhead on the moving endpoint. The region is a READING MARK in
//     here — `x` is consumed, nothing auditions — and VIEW-LOCAL: the exit,
//     every `,` / `.` step and every compare switch clear it (the mode
//     edges' own rule, close_history_mode and set_history_reading).
//
// THE SYMMETRY RULING (architect 2026-08-06) is why the acts read as they do:
// THE HISTORY VIEW AND THE REGULAR VIEWS ANSWER A WAVEFORM CLICK IDENTICALLY,
// and the regular views' standing model is the model. THE WAVEFORM RESOLVES NO
// FLAG AT ALL — a waveform modifier is GESTURE vocabulary (ctrl the zoom,
// shift the former) while SELECTION is LANE vocabulary (the flag boxes' plain,
// shift and ctrl clicks), the stems pointer-inert in all contexts (the seventh
// glass ruling).
//
// EVERYTHING ELSE IS A CONSUMED NO-OP — the RIGHT button whole (unbound
// product-wide since the eighth ruling; consumed here as everywhere), the
// marker clicks' drag arm, the empty-lane marker-CREATE double-click (the
// EmptyLane candidate is never seeded in here — the mode's pending never sets
// seed_empty_lane — so the create cannot fire; authoring), the trim bar's
// three WRITING routes (the endcap and bridge drags and the two ctrl
// bound-set clicks, none of which the framing double-click above touches — it
// is the band's SECOND click, exactly as outside), and every unbound modifier
// combination that is not one of the claims above.
bool GuiInputHandler::handle_history_mode_press(
        GuiMouseButton button, int x, int y, GuiInputState mods,
        const DoubleClickCandidate& dc_at_press) {
    // A non-left press is a consumed nothing: the right button is unbound
    // product-wide, and no press in this view may start audio anyway.
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
    // THE MODE'S NAVIGATION SURFACE, from the ONE geometry owner: the whole
    // waveform plus the two nav lanes, flag boxes carved out. It has been the
    // full waveform height in here since playback left the view (2026-08-05),
    // and since 2026-08-13 the LIVE surface is the same rect — the two halves
    // became one out there too — so the mode's surface is no longer a special
    // case and there is nothing left for a mode term to say. The flag claims
    // below run before any arm that reads this, so the owner's own carve-out
    // simply agrees with them.
    const bool on_nav_surface = point_on_nav_surface(app, audio, x, y);

    // THE MULTI-SELECTION'S TWO MODIFIED CLICKS (architect 2026-08-05), asked
    // FIRST because they are the only modified presses in this mode that hit
    // an ITEM: shift takes the contiguous range from the focus, ctrl toggles
    // one flag's membership, and both then focus the clicked flag and land the
    // playhead on it — the live selection model re-expressed over the mode's
    // own list, with the store selection as untouched as the plain click
    // leaves it. FLAG BOXES ONLY (the symmetry ruling, 2026-08-06): a
    // modified lane press that hits NO flag falls through to the gesture the
    // modifier names — shift the former, ctrl the zoom — exactly as the live
    // lanes answer since they became the extension (2026-08-12; the
    // consumed-nothing it used to be predated the lanes carrying gestures).
    if ((shift != ctrl) && !alt) {
        const GuiRect lane = top_marker_row_area(app);
        if (y >= lane.y && y < lane.y + lane.h) {
            const int hit = hit_test_flag(app, audio, x, y);
            if (hit >= 0) {
                // THE PRESS ONLY ARMS (2026-08-15, the act-at-lift sweep's last
                // four acts): the range / toggle, the focus move, the land and
                // the region clear run at the MOTIONLESS LIFT on the ARMED
                // index, through the one act owner (run_pending_click_act). The
                // press-time MODIFIER SHAPE is carried, never re-read — the
                // modal release's own rule — and the mode has no drag for any of
                // the three to become, so a crossing commits nothing.
                arm_pending_click_act(PendingClickKind::HistoryDiffFlag, x, y,
                                      /*is_begin=*/false, /*flag=*/hit,
                                      shift, ctrl);
                return true;
            }
        }
    }

    // CTRL-exact on the navigation surface leaves for the live router's ctrl
    // claim — the one nav drag's ctrl entry (arm_nav_zoom_press), the mode's
    // admitted zoom, on the gesture's full grown surface (the flag toggle was
    // claimed above, so a lane arrival here is an empty stretch). The entry
    // arms NO click act, so the mode needs no arm of its own; a mid-drag
    // ctrl release pans, the pan being equally admitted (the wheel class).
    // Everywhere else — the trim bar's begin set above all — it is consumed.
    if (ctrl && !shift && !alt) return !on_nav_surface;
    // SHIFT-exact on the navigation surface is the VIEW-LOCAL REGION FORMER
    // (the header's act list): the mode-focus clear where the live former
    // deselects, then the shared placement body and the arm.
    if (shift && !ctrl && !alt && on_nav_surface) {
        if (clear_history_mode_focus(app.history_mode)) {
            // A DISCRETE COMMAND, so full-window damage for the face swap —
            // the same shape the focus click emits on a move.
            viewport.invalidate_all();
        }
        const int64_t sample = place_playhead_at_click_column(
            x - area.x, playback.is_playing(), app.playhead_cursor_sample);
        if (sample >= 0) arm_region_drag_at(sample, x, y);
        return true;
    }
    // Every other modified combination — alt anything, ctrl+shift, shift off
    // the surface — is a consumed nothing.
    if (ctrl || shift || alt) return true;

    // PLAIN FROM HERE. The band walk mirrors the live press router's: the
    // ruler is the navigation surface's lane member, the trim bar keeps its
    // framing double-click, the marker lane splits flag-vs-stretch, and the
    // waveform is the surface's floor.
    {
        const GuiRect ruler = top_ruler_row_area(app);
        if (y >= ruler.y && y < ruler.y + ruler.h) {
            arm_nav_press(x, y, /*history=*/true, /*seed_empty_lane=*/false,
                          /*scrub_release=*/false);
            return true;
        }
    }
    // THE TRIM BAR'S DOUBLE-CLICK ZOOMS TO THE DIFF SPAN (architect 2026-08-05,
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
        const GuiRect trim_bar = top_trim_row_area(app);
        if (y >= trim_bar.y && y < trim_bar.y + trim_bar.h) {
            if (trim_bar_double_click_at(dc_at_press, x, y)) {
                arm_pending_click_act(PendingClickKind::TrimBarFraming, x, y);
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
        // empty-stretch answer and is correct: nothing is clickable that is not
        // drawn. A FLAG arms the focus click (run at the lift, 2026-08-15, like
        // the two modified clicks above); an EMPTY STRETCH is
        // the navigation surface — the pending click / pan, whose motionless
        // release lands the playhead at the column through the mode's land
        // (the extension rule: this used to clear the focus and land nothing,
        // and now places like every other click on the surface). No EmptyLane
        // seed — the marker create is authoring, consumed in here.
        const int hit = hit_test_flag(app, audio, x, y);
        if (hit >= 0) {
            // ARM ONLY (2026-08-15, with the two modified clicks above): the
            // focus move, the land and the region clear are the CLICK and run at
            // the motionless lift on this armed index.
            arm_pending_click_act(PendingClickKind::HistoryDiffFlag, x, y,
                                  /*is_begin=*/false, /*flag=*/hit,
                                  /*shift=*/false, /*ctrl=*/false);
        } else {
            arm_nav_press(x, y, /*history=*/true, /*seed_empty_lane=*/false,
                          /*scrub_release=*/false);
        }
        return true;
    }

    // THE WAVEFORM, FULL HEIGHT, reached by elimination: the top-strip bands
    // above all returned, so what is left is the navigation surface's floor.
    // Both halves take the same pending since playback left the view — there
    // is no scrub half in here.
    if (inside_waveform) {
        // NO STEM CLAIM (the seventh glass ruling — stems pointer-inert in
        // all contexts): the press is the surface's own at EVERY column,
        // stems included. The diff flag's LANE BOX is its one pointer
        // surface, above.
        arm_nav_press(x, y, /*history=*/true, /*seed_empty_lane=*/false,
                      /*scrub_release=*/false);
        return true;
    }

    return true;
}

// THE MODE'S PLAIN FOCUS CLICK — the diff flag's box in the marker lane, the
// flag's ONE pointer surface (its waveform STEM surface, this body's second
// caller from 2026-08-05, died with the stems-inert ruling of 2026-08-12).
// `hit` is an index into app.history_mode.flags, ARMED AT THE PRESS AND ACTED ON
// AT THE MOTIONLESS LIFT since 2026-08-15 (the act-at-lift sweep's last four
// acts): its ONE caller is run_pending_click_act, which passes the index the
// press resolved and never a re-hit at the release's coordinates — the three
// reasons are at run_marker_click_act. The router resolves a flag only since
// 2026-08-12 (an empty lane stretch is the
// navigation surface's pending click now — the eighth glass ruling — whose
// deferred land clears the focus through the same pair clearer and then
// PLACES the playhead, where this body's out-of-range arm lands nothing);
// out-of-range tolerance kept, answering clear-and-land-nothing — and it is what
// makes an armed index that the walk somehow outran harmless.
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
// CTRL — and `hit` is an ordinal into app.history_mode.flags that the PRESS
// resolved from the flag's LANE BOX, the only surface these two clicks have
// (architect 2026-08-06, the symmetry ruling: selection is lane vocabulary in
// both views, and the stem-based pair this body briefly also served is gone).
// BOTH RUN AT THE MOTIONLESS LIFT since 2026-08-15, on that ARMED ordinal and
// under the PRESS-TIME modifier shape, which is carried and never re-read — the
// act-at-lift sweep's last four acts, contract at PendingClickAct
// (app_state.h). It
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
    // keyboard allowlist; the anchors do not, and a Settings item opens the
    // settings editor by a DIRECT call (finish_dropdown_release), reaching no
    // gate at all. Refusing the menu is one line where covering that path per
    // item would be several.
    //
    // THE COMMAND MENUS NEED NO SUCH COVER AND ARE LIVE IN THE VIEW (Navigation
    // by the architect 2026-08-08, File by construction when it landed
    // 2026-08-13): neither has a direct call to shut — every one of Navigation's
    // seven rows and File's one row is
    // a CHORD, dispatched through on_key exactly as a redesigned button's is, so
    // the mode answers PER ITEM at the same two gates a key meets (the allowlist
    // admits zoom in / out / overview; history_mode_owns_key claims
    // center-on-focus and the two marker steps as re-expressions over the diff
    // flags; File's Ctrl+Q is on that same allowlist). The one row whose chord
    // means something ELSE in here — "Walk both
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
    // (the gesture that opened it, closing it), and a press on ANOTHER menu's
    // button switches — the close below runs first, damaging the box that is
    // leaving, and the open then proceeds. "Two dropdowns are never open
    // together" needs no rule beyond this: the field holds one value, whatever
    // the menu count.
    const bool same = (app.dropdown.menu == menu);
    close_dropdown();
    if (same) return;
    // OPENING A MENU ENDS AN ACTIVE FLAG EDIT, discarding it — exactly what a
    // press anywhere outside the editor's box already does. It is what keeps "a
    // popup and an editor are never open together" true, and the FLAG editor is
    // the one class that needs it: the DIALOG modal editors (the settings,
    // load and commit-title editors and the BPM bracket, the membership
    // modal_dialog_editor_active names) veil every press at the top of
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
    // THE REST OF ROW 1 IS DELIBERATELY OUT OF SCOPE. The view bar's bare
    // 1/2/3 drop at the keyboard-modal gate as consumed nothings — the modality
    // ruling working as intended — and ending an edit there would be a behavior
    // change nobody asked for. (Quit needed nothing here while it was a button,
    // its Ctrl+Q being one of the three chords that gate admits; since
    // 2026-08-13 it is an ITEM of this menu, so the discard above covers it like
    // every other row.)
    if (text_editor::is_active(app.top_flag_editor)) {
        flag_editor.exit_top_flag_edit_no_commit();
    }
    app.dropdown.menu         = menu;
    app.dropdown.hovered_item = -1;
    // OPENING A MENU ARMS THE ROW — the mode's ONE producer, and it sits here
    // because this is the ONE route that opens any menu: the anchor click, the
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
    // redesign_button_hover_zone refuses the WHOLE roster while a dropdown is
    // up.
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
// motion outright), the prompt, the editor text drag, the dialog
// modal editors, and every live gesture and pending — PLUS THE ONE
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
    // HAS an anchor rather than naming them, so dropdown_anchor_button stays
    // the one place that knows which button emits which menu. Anywhere ELSE on
    // the row — the view bar, the ground between the floats — is simply not an
    // anchor: the row stays armed and nothing opens.
    for (const DropdownMenu m : kDropdownMenus) {
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
// reset that makes the next hover start its dwell from zero. It is
// surface-agnostic: the owner clears to "none" whichever surface it named, and
// the painted rect it damages is wherever that hint was drawn.
void GuiInputHandler::hide_shift_tooltip() {
    app.redesign_tooltip.hover_ms = 0;
    app.redesign_tooltip.owner    = AppState::RedesignTooltip::Owner{};
    if (!app.redesign_tooltip.visible) return;
    const GuiRect painted = app.redesign_tooltip.rect;
    app.redesign_tooltip.visible = false;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

// THE DWELL'S ONE ARMING ROUTE, shared by both hover walks (the roster's and
// the modal dialog's) so "a fresh dwell on each arrival" is one rule rather
// than two copies: a change of owner — ACROSS surfaces as well as within one,
// the owner being compared whole — hides whatever was up and starts the new
// button's wait from zero, an unchanged owner keeps the wait running, and no
// owner hides. The tick then decides visibility by comparing two numbers.
void GuiInputHandler::arm_tooltip_dwell(
        AppState::RedesignTooltip::Owner o) {
    if (o.index < 0) {
        hide_shift_tooltip();
        return;
    }
    if (app.redesign_tooltip.owner == o) return;
    hide_shift_tooltip();
    app.redesign_tooltip.owner    = o;
    app.redesign_tooltip.hover_ms = monotonic_ms();
}

// THE ARMED CHROME PRESS, dropped — the pointer-leave / capability-loss hook's
// clear (main.cpp), the arm's hard end. Since the act moved to the release
// (2026-08-13) this is sharper than a face clear: the arm is a pending ACT,
// and a pointer that has left the window is on no button, so the act must not
// be left waiting for a release that may never come. Capability loss really
// does end the stream with no release to come; an ordinary leave keeps the
// held state and delivers the release normally once it happens — that release
// then finds no arm and dispatches nothing, which is the intended answer (the
// pointer was outside the window; the same asymmetry the modal arm's clear
// beside this one carries). The RELEASE itself never comes here — it consumes
// the arm through take_chrome_press at on_button_release's top. Damage is
// owed only when a pressed face may be painted (a Roster arm with the pointer
// inside), through the row's home-strip fork.
// (THE ARROWS' HOLD-REPEAT DISARM lived at the top of this function until the
// repeat's deletion, 2026-08-13 — the record is at the arrows' chord-table
// rows above.)
void GuiInputHandler::clear_redesign_button_press() {
    if (app.chrome_press.kind == AppState::ChromePress::Kind::None) return;
    (void)take_chrome_press();
}

// THE RELEASE-TIME ARMS' BUTTON-LOST END — the family's one owner (codex round
// 20; the contract, and why the family needed one, are at the declaration).
// It calls the three arms' OWN clears rather than touching their fields, so
// each keeps its damage, its transition gate and its own reasoning; what this
// body adds is the QUESTION — is any of them standing — because two of those
// clears are unconditional in the leave hook's sense and one of them
// (clear_dropdown_pointer_state) also drops a HOVER face, which an ordinary
// unheld motion must not disturb. With an arm standing, dropping that face
// with it is right and not collateral: the pointer that lit it is the one whose
// press has just vanished, and any pointer still in the window re-derives the
// face on its very next motion.
void GuiInputHandler::clear_release_time_press_arms() {
    if (app.chrome_press.kind == AppState::ChromePress::Kind::None &&
        app.modal_dialog_pressed < 0 &&
        app.dropdown.pressed_item < 0 &&
        !app.dropdown.press_began_on_item)
        return;
    clear_redesign_button_press();
    clear_modal_dialog_press();
    clear_dropdown_pointer_state();
}

// THE REGION DRAG'S ONE MOTION PATH, hoisted 2026-08-12 (the touch half) for
// its TWO DRIVERS: on_motion's region branch below (mouse motion under the
// held button, past its own button-lost arm) and update_touch_region (the
// platform's per-frame region hook, which holds no button and has none to
// lose). Both callers guard on region_drag.active.
void GuiInputHandler::apply_region_drag_motion(int mouse_x, int mouse_y) {
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    // Sub-threshold: the press has not yet become a drag. Below the shared
    // Chebyshev gate nothing extra happens — the former's press/begin
    // already did the click and cleared any resting region at mouse-down.
    // Once a drag, always a drag (moved never re-engages). (The one-day
    // RULER arm's crossing act — the deferred dissolve + deselect — died
    // 2026-08-12 with the ruler former, superseded by pan-primary.)
    if (!app.region_drag.moved) {
        if (std::max(std::abs(mouse_x - app.region_drag.press_x),
                     std::abs(mouse_y - app.region_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
    }
    app.region_drag.moved = true;
    // A moved region drag drops any double-click candidate: this press
    // became a drag, not the first click of a double-click. No former
    // seeds a candidate itself since 2026-08-12 (the EmptyLane seed is
    // the plain pending click's motionless-release act now, and the
    // formers seed nothing), so this clear covers only a candidate a
    // PREVIOUS clean click left resting — the standing moved-drag clear
    // route.
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
    // arm_region_drag_at, which clears the region AT MOUSE-DOWN, so
    // at the crossing `app.region.active` is false and the
    // install proceeds whatever column the pointer is over.
    if (app.region.active && far_frame == app.region.b_frame)
        return;
    app.region.active     = true;
    app.region.a_frame    = app.region_drag.anchor_frame;
    app.region.b_frame    = far_frame;
    // SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): highlighting a
    // region does NOT select the markers it contains (the reverse coupling —
    // a region selecting its contents — was tried and retired; do not
    // re-propose) — the press already deselected all and the drag
    // leaves the selection EMPTY throughout. That is the whole story for
    // the LIVE former — the shift press and the touch begin's live arm both
    // deselect through the one placement body — so a span drawn in an
    // ordinary view rests beside an EMPTY selection. The `h` view's own
    // former entries ride this same motion path and deselect nothing (they
    // clear the MODE's pair instead), which costs
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
    // the span's other bound. EVERY ARM rides this one motion path: each
    // seats the playhead at
    // its click and the pointer carries the cursor from here on, in the view
    // as outside it, from the finger as from the mouse.
    // DIRECT CURSOR WRITE, not move_playhead_to: a keep-visible edge-align
    // would scroll the viewport out from under a live gesture, and the span's
    // endpoints are painted against the viewport the drag started in.
    // PLAYBACK IS UNTOUCHED per motion: the press/begin's at-entry
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
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a PendingMarkerPress crossing the threshold —
// the flag PRESS commits nothing since 2026-08-15, it only arms, so the
// crossing is where the click act runs and the drag begins); with no gesture it
// recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the CROSSING'S OWN click act lands the playhead on the marker a few lines
// before begin_drag, so the drag tows it by construction).
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    // Record latest cursor coords so viewport mutators can re-evaluate hover
    // at the cursor's last position.
    app.last_mouse_x = mouse_x;
    app.last_mouse_y = mouse_y;
    app.pointer_in_window = true;
    // THE RELEASE-TIME ARMS END HERE ON THE BUTTON-LOST EDGE (codex round 20),
    // and it sits at the very TOP because every branch below returns: an open
    // dropdown takes the motion whole, a modal branch returns, each live gesture
    // returns. The arms this drops belong to none of those branches — they are
    // claims on a release that the unheld button says can no longer come — so
    // the only placement that sees them all is above the lot. The FAMILY'S
    // ARGUMENT and the defect it answers are at clear_release_time_press_arms's
    // declaration (input_handler.h); what belongs HERE is the ordering: the
    // motion-driven gestures keep their OWN per-branch button-lost arms below,
    // where each ends the way its release would (a moved drag finalizes), while
    // these commit NOTHING — the two families share this one edge and nothing
    // else, which is the distinction whose absence was the round-19 miss.
    if (!mods.primary_button_held) clear_release_time_press_arms();
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
    // THE BUTTON HOVER RECOMPUTE STAYS LIVE UNDER EVERY MODAL SURFACE — the
    // two MODAL branches that return before the no-gesture tail (the prompt's
    // and the dialog editors' below) both call it — but WHAT IT DERIVES under
    // a modal changed with the dialog veil (2026-08-12, revising the
    // 2026-07-31 "hover follows the pointer everywhere" reading): the walk's
    // veil term refuses the whole roster under a PROMPT and everything but
    // the veil-admitted Save under an EDITOR dialog, because a hover
    // face is a PROMISE OF PRESSABILITY and the veil consumes those presses.
    // The recompute must still RUN in those branches for the original
    // ruling's reason inverted: the walk is the only writer of `hovered`
    // false as well as true, so a modal that skipped it would leave a pill
    // lit at the open frozen under the veil — the exact stale-pill defect the
    // 2026-07-31 ruling fixed, now fixed by re-deriving to the veiled answer.
    // The dialog's own buttons take their hover through
    // update_modal_dialog_hover in the same branches. What the recompute must
    // NOT do under a modal is start a TOOLTIP dwell — a floating hint is not
    // a face, and that refusal lives in the recompute itself.
    // What DOES still freeze the hover is an active pointer GESTURE — the
    // branches below all return without this call, exactly as before.
    //
    // NO CLOSE-EDGE HOOK EXISTS OR IS NEEDED: with the recompute live through
    // the whole modal, the veiled faces re-derive on the first motion or tick
    // after the close (the veil term reads the live editor state), which is
    // the roster's ordinary staleness window.
    //
    // AN OPEN DROPDOWN REFUSES THE WHOLE ROSTER (redesign_button_hover_zone,
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
        // from Navigation onto a view-bar button must put Navigation's menu away
        // rather than
        // leave it hanging under a second lit button). This REVERSES the earlier
        // "a menu bar keeps its menu up while the pointer crosses the rest of the
        // bar" reading — the bar's other buttons are not menu titles here, they
        // are commands, and a command button lighting beside an open menu is the
        // state the row does not have.
        //
        // THE MEMBERSHIP IS redesign_button_in_menu_row (app_state.h), walked
        // rather than named, so the rule covers whatever row 1 holds by the
        // fact "row 1" and a new row-1 button inherits it by existing. WHAT IT
        // COVERS TODAY, re-derived from the two predicates rather than
        // remembered: THE VIEW BAR'S THREE — row 1's other three buttons are the
        // anchors, and an
        // ANCHOR is skipped (the OPEN menu's own does nothing at all — no
        // re-open, no close — and another one SWITCHES through the walk below,
        // both unchanged). It was "Quit and the view bar's three" until
        // 2026-08-13, when the Quit button became the File menu and joined the
        // skipped side. The close goes through close_dropdown, the one close
        // owner, which carries the popup's damage.
        //
        // IT RUNS BEFORE THE ROSTER RECOMPUTE so the frame that closes the menu is
        // the frame the button lights on: with the popup already gone,
        // redesign_button_hover_zone's dropdown refusal no longer applies and
        // the button under the pointer resolves to hovered in the very same
        // call.
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
                if (redesign_button_is_menu_anchor(id)) continue;
                if (!redesign_button_hit(app, id, mouse_x, mouse_y)) continue;
                close_dropdown();
                // THE MODE SURVIVES THIS ONE CLOSE (architect 2026-08-03, the
                // other half of the same behaviour): sliding onto a view-bar
                // button puts the
                // menu away but leaves the row ARMED, so sliding BACK onto an
                // anchor opens that menu again with no click.
                // This is a step across the bar, not a dismissal, and
                // close_dropdown disarms by default — so the exception is
                // spelled here, at the only site that needs it.
                app.dropdown.menu_row_armed = true;
                break;
            }
        }
        // The roster's own faces. While a popup is up this re-derives false for
        // the WHOLE roster (redesign_button_hover_zone refuses every button
        // then — the pointer belongs to the popup), and after the close above
        // it resolves the row-1 button the pointer landed on normally, which is
        // the one case that needs it here.
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
        // The walk covers every menu that HAS an anchor instead of naming
        // them, so the anchor owner stays the one place that knows which button
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
            for (const DropdownMenu m : kDropdownMenus) {
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
        // THE PROMPT DIALOG'S MOTION: the dialog buttons' hover face, then
        // the roster recompute — which under a prompt re-derives ALL-FALSE
        // through that walk's own veil term, so a pill lit at the open goes
        // out on the next motion or tick. The veil consumes the rest of the
        // motion — nothing below this branch runs.
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // F2.1: editor-text drag motion. Handled before the dialog-editor branch
    // (which returns) so the gesture reaches the four dialog editors' fields,
    // and before the trim / playhead branches. A lost button finalizes like
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
            if (g.dialog) viewport.invalidate_modal_dialog_area();
            else          viewport.invalidate_top_strip();
        }
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        return;
    }
    if (modal_dialog_editor_active()) {
        // THE EDITOR DIALOG'S MOTION — the four dialog editors in one branch
        // (the BPM bracket included since the dialog arc; it used to fall
        // through to the gesture branches, harmlessly, its presses all
        // swallowed): the dialog buttons' hover face, then the roster
        // recompute, whose veil term refuses the whole roster since the
        // modal-trap reach-through's retirement, so every chrome face goes
        // dead under the pointer. The rationale for recomputing at all is the
        // one the old branch carried: hover is a separately maintained
        // pointer fact, and a modal freezing it left lit pills behind.
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // The overview lane's box drag — the box pan or an edge drag, one motion
    // body for both (apply_overview_drag_at, X ONLY; contract at
    // OverviewDragState,
    // app_state.h). Absolute-position placement per event through the family's
    // clamp chokepoints, no capture and no stem. A lost button ends like
    // release: a MOVED drag runs the final apply (the edge drag's final
    // apply_strip_drag_zoom, the pan's one deferred predictor resync); an
    // UNMOVED press just disarms and COMMITS NOTHING — a force-end is not a
    // click, so a Pending's teleport does not run (the pendings' own rule,
    // and since 2026-08-15 this lane's press-time act is gone, so the arm's
    // old "its teleport already ran and stands" clause is gone with it).
    if (app.overview_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.overview_drag.moved)
                apply_overview_drag_at(mouse_x, /*final_event=*/true);
            app.overview_drag = OverviewDragState{};
            return;
        }
        // Sub-threshold: still a click (the generic press-becomes-drag gate).
        // Nothing has been applied, so nothing happens here either; the
        // crossing's first apply is absolute, so it folds the whole
        // press->crossing travel by construction.
        if (!app.overview_drag.moved &&
            std::max(std::abs(mouse_x - app.overview_drag.press_x),
                     std::abs(mouse_y - app.overview_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        // A PENDING THAT CROSSES COMMITS NOTHING (2026-08-15, the outside-drag
        // extension's deletion — architect: "we can remove that, because the
        // threshold for the bounds is fine, the ten pixels on either side
        // works, it's a large enough threshold"): the crossing used to resolve
        // an outside press into a bound drag on the bound nearer the press
        // column, and now it resolves into nothing at all — a bound is dragged
        // by its own grab band and nowhere else. What the crossing still does
        // is mark the press MOVED, which is what takes the teleport off the
        // release (the act belongs to a motionless lift), so the gesture simply
        // runs to its end committing nothing, exactly as a force-ended pending
        // does. The apply below is the Pending arm's own no-op.
        app.overview_drag.moved = true;
        apply_overview_drag_at(mouse_x, /*final_event=*/false);
        return;
    }
    // THE STANDING REGION'S OWN EDITOR — the move and the two bound drags, one
    // motion body for all three (apply_region_edit_drag_at, X ONLY; contract at
    // RegionEditDragState, app_state.h). Absolute per-event placement, no
    // capture and no stem, and it writes app.region and nothing else. A lost
    // button ends like release EXCEPT that an UNMOVED press commits nothing: a
    // force-end is not a click, so the motionless release's click act does not
    // run here — the standing abnormal-end rule.
    if (app.region_edit_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            app.region_edit_drag = RegionEditDragState{};
            return;
        }
        // Sub-threshold: still a click (the generic press-becomes-drag gate).
        // Nothing has been applied, so nothing happens here either; the
        // crossing's first apply is absolute, so it folds the whole
        // press->crossing travel by construction.
        if (!app.region_edit_drag.moved &&
            std::max(std::abs(mouse_x - app.region_edit_drag.press_x),
                     std::abs(mouse_y - app.region_edit_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.region_edit_drag.moved = true;
        apply_region_edit_drag_at(mouse_x);
        return;
    }
    // THE ONE NAV DRAG: the pending click, and past the threshold the
    // grab-pan — or, while ctrl is held, the zoom (the live-ctrl model,
    // contract at ScrollDragState, app_state.h). The viewport snaps to whole
    // pixels in
    // clamp_viewport_start (reached through scroll_viewport), so a per-event pan
    // re-anchored by that snap tracks the cursor 1:1 without drift — no carried
    // sample remainder. scroll_viewport renders the plate synchronously, so
    // per-event work is one full-width render — the cost zoom already paid per
    // pointer frame, and the reason a panning plate looks identical to a resting
    // one (architect 2026-07-26). A lost button: a MOVED drag ends like release
    // (the zoom phase's final apply, or the pan's one predictor re-anchor,
    // then the capture end); an UNMOVED press is NOT
    // a clean click, so the deferred act does not run and no seed is left —
    // the standing abnormal-end rule. The
    // wheel keeps its quantized detent step; only the drag is continuous.
    if (app.scroll_drag.active) {
        ScrollDragState& sd = app.scroll_drag;
        if (!mods.primary_button_held) {     // button lost
            const bool moved   = sd.moved;
            const bool zooming = sd.zooming;
            if (moved && zooming)
                apply_nav_zoom_at(mouse_x, mouse_y, /*final_event=*/true);
            app.scroll_drag = ScrollDragState{};
            // The stem's erase, when the zoom phase painted one — the moved
            // final apply's rebuild covers it, so this is the unmoved
            // ctrl-armed press's owed frame (the strip arm's own shape).
            if (zooming && !moved) viewport.invalidate_waveform_area();
            if (moved) {
                if (!zooming && playback.is_playing())
                    playback.resync_predictor();
                end_strip_pointer_capture(); // reappear the cursor (idempotent)
            }
            return;
        }
        // THE LIVE MODE SYNC, ahead of the threshold gate so a pending press
        // tracks ctrl too. THIS CALLER IS THE PER-DELIVERED-MOTION ONE: the
        // settled-state tail already answers the motionless edge, but a
        // dispatch batch can carry the modifiers event and then a motion with
        // no loop tail between them, and the mode must be right BEFORE this
        // event's delta is applied — the same two-caller shape, and the same
        // argument, as the dropdown hover walk. Both reach the one body.
        sync_nav_drag_mode(mods);
        // Sub-threshold: still the pending click. The press did nothing, so
        // nothing happens here either — the fork IS the threshold. last_x
        // stays at the press until the crossing, which therefore folds the
        // whole press→crossing travel into its first applied event.
        if (!sd.moved) {
            if (std::max(std::abs(mouse_x - sd.press_x),
                         std::abs(mouse_y - sd.press_y)) <
                    kDragMovedThresholdPx) {
                return;
            }
            sd.moved = true;
            // THE DRAG BEGINS AT THE CROSSING, and so does its CAPTURE — not
            // at the press, or every motionless click would blink the cursor
            // away and back, the ctrl click included (the unification's own
            // rule). The MODE AT THE CROSSING is the gesture's cue, stamped
            // for the capture's release restore (the contract at
            // GuiPlatform::begin_pointer_capture); a later mode switch
            // re-stamps it through set_strip_capture_restore_kind above.
            begin_strip_pointer_capture(sd.zooming ? GuiCursorKind::Zoom
                                                   : GuiCursorKind::Pan);
            // AND THE CAPTURED POINTER IS TOLD ITS WRAP SPAN, immediately
            // after the begin exactly as the overview lane's arm does it: the
            // hidden cursor folds to the waveform's opposite bound instead of
            // pinning at the one it reached, uniformly for every capture
            // whatever the gesture or the mode (contract at
            // set_strip_capture_wrap_span, input_handler.h).
            tell_capture_wrap_span();
            // AND THE MODE AT THE CROSSING ALSO SETS THE LATERAL FREEZE. The
            // capture opens unfrozen, and the ctrl edges a sub-threshold press
            // took spoke to no capture at all (the setters are capture-
            // guarded), so a ctrl-armed drag would otherwise reach its zoom
            // phase with the pointer's x still advancing. This is the only
            // other site: from here every switch rides sync_nav_drag_mode.
            set_strip_capture_notional_x_frozen(sd.zooming);
        }
        if (sd.zooming) {
            // The ZOOM phase: dx off the live level about the seated pivot
            // (right zooms in), dy discarded, and the pointer's own notional x
            // held still by the freeze asserted above — the level spends the
            // lateral travel and the position must not spend it twice
            // (apply_nav_zoom_at; the two are different statements).
            apply_nav_zoom_at(mouse_x, mouse_y, /*final_event=*/false);
            return;
        }
        const double spp = current_samples_per_pixel(app, audio);
        const int    dx  = mouse_x - sd.last_x;
        sd.last_x = mouse_x;
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
    // (No scrub motion branch: the scrub is a ONE-SHOT act at the motionless
    // RELEASE (2026-08-13) — the held-drag per-column re-scrub is REMOVED
    // (architect 2026-07-23, the Ableton model), so motion over the lower half
    // is the ordinary grab-pan, which cancels the act rather than re-scrubbing,
    // and the per-column stop-fence cadence is structurally gone.)
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
    // THE SWEEP'S LAST FOUR PENDING ACTS (PendingClickAct, app_state.h): the two
    // trim bound sets, the trim bar's framing double-click, the empty lane's
    // create double-click and the `h` view's diff-flag clicks. Nothing has been
    // committed yet — all four act at the LIFT since 2026-08-15 — so this branch
    // owns both ways they can end early. Placed ABOVE the pending trim drag
    // below because the TrimBoundSet crossing HANDS OVER to it and falls
    // through, so the same motion event begins and applies the drag.
    if (app.pending_click.active()) {
        if (!mods.primary_button_held) {
            // A LOST BUTTON COMMITS NOTHING and simply disarms — the standing
            // rule for every lift-act surface, the same answer the force-end
            // finalizer gives, and the way the TOUCH layer's ABNORMAL end (the
            // motionless-hold upgrade) reaches these four for free: it delivers
            // a motion with the button unheld precisely so an unmoved press
            // commits nothing.
            app.pending_click = PendingClickAct{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_click.press_x),
                     std::abs(mouse_y - app.pending_click.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // THE CROSSING SPENDS THE ARM, whatever its kind. Read, disarm, then
        // act — the release bodies' standing shape.
        const PendingClickAct press = app.pending_click;
        app.pending_click = PendingClickAct{};
        if (press.kind != PendingClickKind::TrimBoundSet) {
            // THE OTHER THREE COMMIT NOTHING: none has a gesture to become —
            // the two double-click acts are the SECOND press of a pair and
            // deliberately arm nothing else (verdict-before-arm, whose whole
            // purpose is that they cannot become a drag or a pan), and the `h`
            // view has no marker drag at all. That is the overview Pending's own
            // rule, and on the trim bar it is the no-undo guarantee itself: an
            // 8 px drift on a framing second press moves no bound because there
            // is no bound gesture armed to move one.
            return;
        }
        // THE BOUND SET RUNS HERE AND THEN BECOMES THE DRAG, which is the marker
        // flag's crossing exactly: the act first, at the PRESS column, then the
        // gesture it was always the prologue to. set_trim_bound_at_click_then_
        // arm_drag is the one owner of that pair — its arm RIDES the set's
        // verdict, so a refused set (a degenerate audio/geometry state, a value
        // not strictly inside its partner) arms nothing and the crossing is a
        // consumed nothing, exactly as the motionless lift would have been.
        // A drag's outcome is therefore byte-for-byte what it was under the
        // press-time set: the bound is written at the press column, the drag
        // anchors there, and begin_trim_drag's orig_* capture is the click-set
        // value it always was.
        // FALL THROUGH (no return) so the pending trim drag armed just now
        // crosses on this same event and applies its first delta, folding the
        // whole press->crossing travel.
        set_trim_bound_at_click_then_arm_drag(press.is_begin, press.press_x,
                                              press.press_y);
    }
    // Pending trim drag (armed by a plain trim-bar press, or by the crossing of
    // a ctrl / ctrl+shift bound set just above): the trim reposition
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
            // path ended it — as a consumed nothing.
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
        // THIS ARM ALSO ENDS A LIVE TOUCH REGION GESTURE that a real mouse
        // motion interrupts (the hook gesture holds no logical button, so
        // primary_button_held reads false here) — the accepted cross-device
        // edge at begin_touch_region's declaration: the user's own
        // two-handed act, every end a commit.
        if (!mods.primary_button_held) {
            const bool moved = app.region_drag.moved;
            app.region_drag = RegionDragState{};
            if (moved)
                end_region_drag_min_size_check(app, audio, viewport);
            return;
        }
        // The drag's ONE motion path, shared with the touch region hook
        // (update_touch_region) — the body just above on_motion.
        apply_region_drag_motion(mouse_x, mouse_y);
        return;
    }
    // (No tempo-drag motion arms: the target-view tempo drag and its pending are
    // DELETED, architect 2026-07-29 — see marker_drag.h. W+target has no pointer
    // authoring gesture at all now.)
    // THE MARKER FLAG'S PENDING CLICK (armed by any of the flag box's three
    // presses; contract at PendingMarkerPress, app_state.h). Nothing has been
    // committed yet — the click is the lift's since 2026-08-15 — so this branch
    // owns both ways the gesture can end early. Handled before the hover
    // fallthrough below and after the other drag branches (this pending and any
    // other pointer gesture are mutually exclusive — the arming press does no
    // other work).
    if (app.pending_marker_press.active) {
        if (!mods.primary_button_held) {
            // A LOST BUTTON COMMITS NOTHING and simply disarms — the standing
            // rule for every lift-act surface, and the same answer the
            // force-end finalizer gives. It is also how the TOUCH layer's
            // ABNORMAL end reaches this state for free: the motionless-hold
            // upgrade delivers a motion with the button unheld precisely so an
            // unmoved press commits nothing (codex round 19's ruling), which is
            // an improvement the press-time click could not have — that model
            // had already acted and the upgrade could not take it back.
            app.pending_marker_press = PendingMarkerPress{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_marker_press.press_x),
                     std::abs(mouse_y - app.pending_marker_press.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // THE CROSSING SPENDS THE ARM, whatever its shape. Read, disarm, then
        // act — the release bodies' standing shape.
        const PendingMarkerPress press = app.pending_marker_press;
        app.pending_marker_press = PendingMarkerPress{};
        // A SHIFT OR CTRL ARM THAT CROSSES COMMITS NOTHING: neither has a
        // gesture to become (they arm no drag, today as before), so the arm is
        // simply spent — the overview Pending's own rule, "a Pending that
        // crosses marks moved and commits nothing". THE DELIBERATE COST,
        // recorded rather than special-cased: an 8px wobble during a
        // shift-click therefore selects nothing. That is one threshold rule
        // across all three arms, and it is exactly what the navigation surface
        // already does to a wobbled plain click.
        if (press.shift || press.ctrl) return;
        // THE PLAIN ARM RUNS ITS CLICK ACT HERE, minus the double-click half
        // (`at_lift` false), and THEN begins the drag. Two of those acts are
        // load-bearing for the drag itself, which is why the crossing runs the
        // act rather than dropping it:
        //   * THE STOP. The follow-override omission below depends on it: with
        //     the stop moved to the lift alone, a drag could begin under a live
        //     scanner with follow_overridden_for_session unset.
        //   * THE SELECT, for the PAINT and not for the arithmetic. begin_drag /
        //     apply_drag_motion / commit_drag never read app.selected_markers —
        //     and since 2026-08-15 never write it either, this act being its ONE
        //     owner at both act sites (begin_drag's duplicate re-assert was
        //     deleted there, with its reasoning) — but the dragged flag's
        //     BRIGHTENED face comes from the selection (the class ladder's
        //     brighter pair, damaged by set_single_selection's
        //     invalidate_top_strip), so without it the user drags an unmarked
        //     flag.
        // The land and the region clear ride along for parity: the press did
        // both before any drag under the old model, and commit_drag lands the
        // playhead again at the committed position, so keeping them here
        // changes nothing observable.
        run_marker_click_act(press, /*at_lift=*/false);
        // THE TWO AUTHORING GATES LIVE HERE, not at the arm: they guard the
        // DRAG (marker motion is authoring), never the click, so a read-only
        // tab and an off-home column still select, still land and simply refuse
        // to move anything — which is exactly what the press-time model did.
        // THE ORDER IS THE RULING: the acts run BEFORE the gate, matching
        // today's read-only flag press, which selected, landed and stopped and
        // had only its drag refused. ONE DRAG, ONE GATE since 2026-07-29: the
        // home-view split that used to arm the TEMPO drag instead in W+target
        // exactly — the pointer half of the home-view binding's tempo
        // exception, with its predecessor-eligibility walk — is DELETED with
        // that whole gesture (see marker_drag.h), so EVERY off-home flag drag
        // (W+target and P+source alike) is the silent navigation-class refusal.
        if (active_view_state(app).read_only ||
            !active_column_authoring_allowed(app))
            return;
        // Begin the drag anchored at the PRESS column so the marker tracks the
        // pointer 1:1, this first apply folding the whole press->crossing delta
        // (the strip/region catch-up pattern). begin_drag captures the pre-drag
        // snapshot and the wall math now — exact, since nothing mutated the
        // store between press and crossing — and sets app.drag.active. Fall
        // through (no return) so this same motion event applies the first delta
        // through the marker-drag branch below.
        // NO DOUBLE-CLICK CLEAR IS OWED HERE, and the clear that stood here is
        // DELETED rather than kept as a second owner (2026-08-15): the seed
        // moved to the motionless lift with the rest of the click, so a press
        // that becomes a drag never seeded one, and on_button_press's own
        // top-of-frame clear emptied the field before this press did anything —
        // with the button held, nothing can re-seed it in between.
        if (!marker_drag.begin_drag(press.marker, press.press_x)) {
                // Begin refused (bad index / no audio): the gesture is DROPPED,
                // its pending already cleared above, so this is a gesture end
                // like any other and takes the loop tail's re-resolve like one.
                return;
        }
        // No follow override needed: the marker drag always begins from this
        // crossing, which just ran the click act's stop, so there is no live
        // playhead to chase.
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
    // inside apply_drag_motion (the crossing's click act landed it on the
    // marker, so the drag tows it by construction — the DragState ruling). The
    // selection was named at the THRESHOLD CROSSING by that same CLICK ACT, which
    // is its ONE owner — begin_drag's duplicate re-assert was deleted 2026-08-15
    // (the reasoning is at the deletion) — and the act is unconditional there, so a
    // wall-saturated drag still names what it grabbed. apply_drag_motion here only
    // writes the proposal and slides the playhead. Nothing further tracks here.
}
