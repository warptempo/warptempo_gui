#include "input_handler.h"
#include "notifications.h"

#include "gui_display_context.h"
#include "warp_frame_map_view.h"
#include "marker_drag.h"
#include "folder_overlay.h"
#include "onscreen_keyboard.h"
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
// the same ids. FOUR roster entries are absent — row 1's FILE, EDIT, SERIES and
// SETTINGS anchors, each of whose action is a POPUP TOGGLE, not a chord at all,
// since no keyboard chord opens or closes a dropdown. All four are spelled at
// their own claim, which walks kDropdownMenus rather than naming them.
// THE ABSENTEES ARE NAMED IN ONE PLACE ONLY: the static_assert below this
// table, which is what makes "the table's length plus those four IS the
// roster" a build-time fact rather than a remembered list of names.
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
    // (WHICH BUTTONS ADMIT A MODIFIER is NOT a column here: it is
    // redesign_button_shift_admits and, since 2026-08-24,
    // redesign_button_ctrl_admits, both in app_state.h, because the TOOLTIP's
    // MODIFIER LINE must appear exactly where a modified press does something —
    // a static_assert beside that table enforces it. One fact, two readers.)
    //
    // RADIO: this button reports a state it can only ever turn ON, so a press
    // while it is already selected is a CONSUMED NOTHING (there is nothing to
    // switch to, and its chord is a TOGGLE that would switch away from what the
    // user just clicked). The tab pair and the two view pairs are radios; the
    // follow, read-only, history and Cumulative buttons are TOGGLES
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
    // REPEATS: a held press on this button synthesizes its own chord over and
    // over — the pointer twin of holding the key (the bottom row's four
    // cardinal arrows from 2026-08-16, joined by the waveform magnification
    // pair on 2026-08-26 — see below). The press arms the burst
    // on the ChromePress itself and tick_chrome_press_repeat fires it with
    // GuiInputState::synthesized_repeat set, so the undo coalescing is the
    // repeat-identity rule the keyboard already has, and a fired burst
    // suppresses the lift's own act. Defaulted, so the rows that do not repeat
    // need no eighth column. SIX ROWS CARRY IT since 2026-08-26, when the
    // WAVEFORM MAGNIFICATION PAIR joined the four arrows — a ladder
    // step is a continuous step gesture like an arrow, and the pair is the
    // touch panel's only road to the setting. Those two push no undo entry at
    // all (the key is history-less), so the opener flip below is vacuous for
    // them.
    bool           repeats = false;
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
// hands five times: Quit joined the table when Ctrl+Q was recognised as its
// chord, Settings left it when its action became a DROPDOWN TOGGLE (a popup
// open/close is not a chord at all — the bare `;` keyboard route still opens the
// editor directly, untouched), Navigation arrived a menu button 2026-08-02 and
// left with its whole menu 2026-08-15 (deleted: every one of its seven rows had
// grown a button), EDIT arrived a menu button 2026-08-20 with the propagate
// relocation (which deleted two chord rows from this table in the same act,
// IconCopy's and IconPaste's), and
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
    // THESE TWO ROWS DISPATCH IN EVERY STATE SINCE 2026-08-18, the `h` view
    // included: the mode's own band claim over this row is deleted with the
    // walk selector, so a press on a tab arms and lifts as an ordinary roster
    // press and Ctrl+Tab switches the A/B tab in there exactly as it does
    // outside (the architect's ruling; the record is at RedesignButton::TabA).
    // (The row carried two MORE slots for one day, 2026-08-07..08, when it was
    // the (walk source, reading) product; they never dispatched — the mode's
    // band claim owned the row then — and they went with the reading, which is
    // row 4's own toggle now.)
    {RedesignButton::TabA,       GuiKeys::Tab, true,  false, false, true,  false},  // Ctrl+Tab
    {RedesignButton::TabB,       GuiKeys::Tab, true,  false, false, true,  false},  // Ctrl+Tab
    // Row 4 — the icon row. The four view buttons are radios on the same two
    // toggling chords the tabs' pair models; the rest are plain dispatches.
    {RedesignButton::IconS,      GuiKeys::T,   false, false, false, true,  true},   // bare t
    {RedesignButton::IconT,      GuiKeys::T,   false, false, false, true,  true},   // bare t
    {RedesignButton::IconW,      GuiKeys::P,   false, false, false, true,  true},   // bare p
    {RedesignButton::IconP,      GuiKeys::P,   false, false, false, true,  true},   // bare p
    // THE TRIM GROUP — the Show trim region button, alone in it since the scissors
    // were deleted on 2026-08-18 (the scissors opened the group in 2026-08-11
    // and led it until the architect's 2026-08-16 reorder). THIS TABLE DOES NOT
    // DECIDE PAINTED ORDER — every one of its readers matches by id or by published
    // rect, never by position — but it is kept in the row's order so nobody
    // reads a mismatch here as the layout's truth; that truth is the painter's
    // kIconRowButtons, with the group's leader at
    // redesign_button_opens_icon_group (app_state.h).
    //
    // THE SHOW TRIM REGION BUTTON (architect 2026-08-16 for the button, named
    // on 2026-08-19): its chord is BARE `[` since 2026-08-24, when the architect
    // moved the whole trim family onto the bracket — the key it left "is too
    // easy to hit accidentally instead of `c`, and it can mess up the viewport",
    // this act's show half framing the trim span. `[` looks like the begin-trim
    // endcap, which is the mnemonic he chose. THE TWO CHORDS IT HAS LEFT BEHIND
    // — Ctrl+Shift+X (2026-08-16 to 2026-08-18) and bare `x` (2026-08-18 to
    // 2026-08-24) — are UNBOUND and answer nothing anywhere under the
    // strict-modifier rule.
    //
    // A TOGGLE, click face, not a radio: it reads and flips the overlay's
    // VISIBILITY, and the lamp the 2026-08-16 momentary ruling refused is safe
    // on that bit (the record is at the roster entry, app_state.h).
    //
    // AND IT ADMITS SHIFT, which is what closes the glass hole the trim
    // scissors left when their button was deleted earlier the same day: its
    // twin is `Shift+[` the MAXIMIZER, so a SHIFT-CLICK or a LONG PRESS at
    // kChromeShiftHoldMs resets the trim to the whole song with no keyboard.
    // The scissors carried exactly this admission for exactly this reason; the
    // act moved to the button that survived. The row's own shift bit stays
    // FALSE, the admission and the table bit being mutually exclusive by the
    // shift term's construction (finish_chrome_press_release).
    {RedesignButton::IconShowRegion,
     GuiKeys::BracketLeft,
     false, false, false, false, true},                                            // bare [
    // (THE TRIM SCISSORS' ROW IS DELETED — 2026-08-18, with its button: the
    // architect's roster relayout retired the "set trim from region" icon
    // whole, and the ACT went with it the same day when the region became the
    // trim. Its chord did not die with it — it was REPOINTED onto the
    // button above — and neither did its shift admission, which moved to that
    // button for the reason it was written: without it a keyboardless panel
    // could set a trim window and never get back out of it.)
    // THE ZOOM GROUP (2026-08-12, the grand relayout — SUPERSEDING the
    // 2026-08-02 no-duplicate-commands deletion of the old zoom pair for
    // these four: the Navigation dropdown kept its rows beside them, the
    // buttons being the glass rig's pointer home; that menu is deleted whole
    // as of 2026-08-15 and these four are the commands' pointer home outright):
    // four momentary navigation chords, no
    // radio, no shift admission, click face like the rest of the row. All
    // four stay LIVE in the `h` view — Ctrl+`=`, Ctrl+`-` and `0` are on the
    // mode's allowlist and `c` is its own vocabulary — which the derived
    // partition answers with nothing hand-listed.
    //
    // THE STEPPING PAIR'S CHORDS CARRY CTRL SINCE 2026-08-27 — this table's own
    // column, a different thing from ADMITTING a ctrl press, which neither does
    // (redesign_button_ctrl_admits names only the two skips), so a ctrl or
    // shift click is still refused at the band gate. NEITHER REPEATS, which the
    // spelling swap did not change: the `repeats` column is untouched on all
    // four rows, and the pair that carries it is the magnification one below.
    {RedesignButton::IconZoomIn,
     GuiKeys::Equal,  true,  false, false, false, true},                            // Ctrl+=
    {RedesignButton::IconZoomOut,
     GuiKeys::Minus,  true,  false, false, false, true},                            // Ctrl+-
    {RedesignButton::IconZoomFitBest,  GuiKeys::Digit0, false, false, false, false, true}, // bare 0
    {RedesignButton::IconZoomOriginal, GuiKeys::C,      false, false, false, false, true}, // bare c
    // THE WAVEFORM MAGNIFICATION PAIR (2026-08-26), the zoom group's last: the
    // picture's VERTICAL gain, on the same two keys as the horizontal zoom
    // beside it, one modifier LESS since 2026-08-27 — bare is vertical, ctrl is
    // horizontal, and the two pairs swapped their spellings whole that day. No
    // radio, no shift or ctrl admission, click face like the rest of the row,
    // and BOTH REPEAT: a held button walks the ladder at the compositor's own
    // rate, exactly as a held key does; the `repeats` column is where that
    // membership lives and there is no second list. Both are live in the `h`
    // view — the chords are on that mode's allowlist, the plate in there being
    // the same plate.
    //
    // (THE MAGNIFICATION RESET'S ROW IS DELETED — 2026-08-27, with its button:
    // the architect retired the third member when the pair moved onto the bare
    // keys, its Ctrl+0 chord going with it rather than standing beside two
    // spellings it no longer matched. The settings editor's
    // `waveform_magnification_level=0` is the reset road now.)
    {RedesignButton::IconWaveformMagnify,
     GuiKeys::Equal,  false, false, false, false, true,  true},                     // bare =
    {RedesignButton::IconWaveformReduce,
     GuiKeys::Minus,  false, false, false, false, true,  true},                     // bare -
    // FOLLOW — the ZOOM GROUP'S LAST MEMBER since 2026-08-27, and the last
    // survivor of the mass-marker category. Bare `f`, a TOGGLE with a lamp,
    // consumed by the `h` view and greyed in there.
    // (THE COPY AND PASTE ROWS ARE DELETED — 2026-08-20, with their buttons:
    // the architect's propagate relocation gave all FIVE propagate commands the
    // new EDIT MENU as their one pointer home, so Ctrl+P and Ctrl+Alt+P reach
    // the pointer as MENU ITEMS now. Their chords did not die with the rows —
    // a menu item dispatches through on_key exactly as a button's chord does,
    // which is why the relocation needed no second body anywhere. The shift
    // admission IconPaste carried for Ctrl+Alt+Shift+P went too, that chord
    // being a menu row of its own now; the trim scissors' deletion above is the
    // same shape.)
    // (THE BPM AND ITERATION ROWS ARE DELETED — 2026-08-27, with their
    // buttons, and it is the propagate relocation's shape exactly: the
    // architect's Series relocation gave bare `m` and bare `i` the new SERIES
    // MENU as their one pointer home, so both reach the pointer as MENU ITEMS
    // now. Neither chord died with its row — a menu item dispatches through
    // on_key exactly as a button's chord does — and neither carried a modifier
    // admission to move. BPM'S KEY IS BARE `m`, NOT `b`, which is worth
    // keeping here now that the row is gone: the arm is at handle_mode_keys,
    // input_key_dispatch.cpp, and the menu ROW spells the key the keyboard
    // actually has.)
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
    // THE TWO WALK RADIOS (architect 2026-08-18) — which walk the `h` view's
    // lane reads: GIT is the committed checkpoint history, SESSION this
    // session's own undo/redo timeline. ONE CHORD FOR THE PAIR, BARE `g`, which
    // was free, and the `radio` flag on both rows is what the shape needs: the
    // chord is a TOGGLE over the two walks, so a press on the half already lit
    // would switch AWAY from what the user just clicked — the same reason the
    // tabs' Ctrl+Tab and the S/T and W/P pairs' bare `t` and `p` carry it. The
    // consume is the generic one, keyed on this flag plus the lamp.
    //
    // BOTH ARE BOUND ONLY INSIDE THE VIEW, like the four companions below, and
    // what keeps them from dispatching outside one is their ENABLED bit: they
    // paint in every state on this row, and outside the view they wear the dead
    // face and the press is consumed at arm_redesign_press's disabled line.
    // Even reached, bare `g` is bound in handle_history_mode_key alone.
    {RedesignButton::HistoryWalkGit,
     GuiKeys::G,      false, false, false, true,  true},                             // bare g
    {RedesignButton::HistoryWalkSession,
     GuiKeys::G,      false, false, false, true,  true},                             // bare g
    // THE HISTORY COMPANIONS — the icon row's last group behind the opener
    // again since 2026-08-18 (they were this row's from 2026-08-04, the bottom
    // row's swapped cluster from 2026-08-14, and back here with the architect's
    // roster relayout; nothing about their chords or gates moved on either
    // trip — only their FACE at rest, which is the icon row's own mode rule).
    //
    // THE CUMULATIVE READING'S TOGGLE (2026-08-08): bare `u` flips the history
    // view's delta between ITERATIVE (off) and CUMULATIVE (on). A TOGGLE like
    // follow, iteration and the history button — the selected face reads the
    // live bit its own chord flips — and like the three entries below it, its
    // key is bound ONLY inside the view. WHAT KEEPS THE FOUR FROM DISPATCHING
    // OUTSIDE ONE IS THEIR ENABLED BIT AGAIN since 2026-08-18: they paint in
    // every state on this row, and outside the view they wear the dead face and
    // the press is consumed at arm_redesign_press's disabled line (the arm and
    // its reasoning are at redesign_button_enabled). It was the ZERO RECT from
    // 2026-08-14 to 2026-08-18, while the bottom row painted the four ARROWS in
    // their slots and published an empty rect for these — no point is inside a
    // zero rect — with a plain `true` behind it since 2026-08-15. The enabled
    // bit was the stated safeguard from 2026-08-05 and is again.
    {RedesignButton::HistoryCumulative,
     GuiKeys::U,      false, false, false, false, true},                             // bare u
    // THE REVERT ACT (2026-08-05): CTRL+H applies the view's SELECTED diff flags
    // backwards into the live state and closes the view. Momentary like the two
    // below — not a radio, not a toggle, click face only. It is the one entry
    // here whose chord is NOT claimed by the mode's own vocabulary: it
    // dispatches from on_key's ordinary body, BELOW the read-only gate, so a
    // locked tab refuses the click exactly as it refuses the key (the
    // load-in-place's precedent, `'`). BOTH OF ITS IN-VIEW REFUSALS ARE
    // FACELESS since 2026-08-15 — the lock's and the mode's empty-subject one —
    // the architect having reversed the second as a per-selection blink; the
    // click is a consumed no-op in either case, which is the roster's standing
    // shape for a refusal. OUTSIDE the view the button is dead like its three
    // neighbours (2026-08-18) and there is no click to consume at all.
    {RedesignButton::HistoryRevert,
     GuiKeys::H,      true,  false, false, false, true},                             // Ctrl+H
    // THE WALK'S TWO STEPS (2026-08-05): bare `,` steps OLDER and bare `.`
    // NEWER, through the same dispatch and therefore through
    // handle_history_mode_key's own arm — walls clamped as consumed no-ops
    // there, exactly as the keys behave. Neither is a radio and neither is a
    // toggle: they are momentary steps, so both flags read like copy's and
    // paste's, and only the CLICK face is set. Outside the view they never
    // dispatch at all — the dead face's consumed press, per the Cumulative
    // entry's note above; and even reached, bare `,` and `.` are bound in
    // handle_history_mode_key alone, so there is nothing for them to fire.
    {RedesignButton::HistoryOlder,
     GuiKeys::Comma,  false, false, false, false, true},                             // bare ,
    {RedesignButton::HistoryNewer,
     GuiKeys::Period, false, false, false, false, true},                             // bare .
    // The BOTTOM ROW (the transport half architect-ratified 2026-08-11 as the
    // touch arc's first surface; the marker-walk group added 2026-08-15, the
    // four SINGLE-MARKER VERBS moved down from the icon row 2026-08-18, and
    // ADD TO SELECTION landed behind them later that day; the MARKER MEASURE
    // joined the verb group 2026-08-19).
    // EIGHTEEN
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
    // EXACTLY here: the lift dispatches bare Space, Space toggles, and the
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
    // THE FOUR ARROWS TAKE THE TABLE'S `repeats` COLUMN (architect
    // 2026-08-16, reversing his own 2026-08-13 deletion of the same gesture,
    // which he finds did not hold up in practice; the column is the whole
    // membership and no list restates it — the magnification pair joined on
    // 2026-08-26): the touch panel has no
    // keyboard beside the synthetic one, so a HELD ARROW BUTTON is the panel's
    // only nudge run, and the substitutes the deletion counted on — dragging
    // the marker, typing the tempo in the editor — do not cover it.
    //
    // THE CADENCE IS THE KEYBOARD'S, not a constant of this table's: the first
    // fire one kHoldBeatMs after the press (the product's ONE hold beat, so the
    // button hold and the key hold cross their threshold on the same beat) and
    // every later fire at THE COMPOSITOR'S OWN advertised key-repeat interval,
    // read per fire — a desktop with key repeat switched off gets no button
    // repeat either. The burst rides the armed ChromePress and its whole edge
    // inventory is at that struct (app_state.h); the firing body is
    // tick_chrome_press_repeat below. The physical arrow KEYS are untouched
    // throughout, repeat_eligible included — the arm SHARES that predicate
    // rather than mirroring it.
    // THE TWO SKIPS ADMIT CTRL (architect 2026-08-24), the roster's first
    // ctrl-click: a CTRL-CLICK dispatches Ctrl+Home / Ctrl+End, the WHOLE-PIECE
    // jump, through redesign_button_ctrl_admits (app_state.h). They admit no
    // shift, which is what keeps the LONG PRESS — glass's held shift — off the
    // act by construction. The rows below carry the PLAIN chord, as every row
    // does; the admission and its modifier are the roster's, never a column
    // here.
    {RedesignButton::TransportSkipBack,
     GuiKeys::Home,   false, false, false, false, true},                             // bare Home
    {RedesignButton::TransportPlayStop,
     GuiKeys::Space,  false, false, false, false, true},                             // bare Space
    {RedesignButton::TransportSkipForward,
     GuiKeys::End,    false, false, false, false, true},                             // bare End
    // THE SINGLE-MARKER VERBS (2026-08-12), the right block's first group
    // since the architect moved them down here on 2026-08-18: drop, delete,
    // disable toggle, inherit/collapse — authoring chords whose refusals
    // (read-only, home view, empty selection, occupied frame) are the keys' own
    // consumed no-ops, inherited whole through on_key. NOTHING IN THIS ROW
    // CHANGED WITH THE LANE: the `h` view consumes all four outright and greys
    // them through the derived partition, and the READ-ONLY lock greys them
    // too (2026-08-15) — both are the BUTTONS' gates rather than the row's, so
    // they are what makes this row's otherwise-unconditional face policy have
    // an exception at all.
    {RedesignButton::IconMarkerDrop,    GuiKeys::S,      false, false, false, false, true}, // bare s
    {RedesignButton::IconMarkerDelete,  GuiKeys::Delete, false, false, false, false, true}, // Delete
    {RedesignButton::IconMarkerDisable, GuiKeys::D,      true,  false, false, false, true}, // Ctrl+D
    {RedesignButton::IconMarkerInherit, GuiKeys::N,      true,  false, false, false, true}, // Ctrl+N
    // THE EDIT FLAG BUTTON (architect 2026-08-27, on glass), the verb group's
    // FIFTH and the flag editor's THIRD ROAD: BARE Enter, which the keyboard
    // has opened the editor with all along. It exists because the other two
    // roads are a double-click and a key, and glass has neither reliably — the
    // architect drove a taller flag hit rect for one evening chasing the missed
    // double tap and retired it the same night for this button: "get rid of
    // double tapping as the ONLY way to enter the editor... its tooltip hotkey
    // says Enter, because that is what triggers the editor".
    //
    // BUTTON-IS-ITS-CHORD HOLDS LITERALLY: the press dispatches bare Return
    // through on_key at the LIFT like every other chrome button, while the KEY
    // acts at the press like every other hotkey. The ACT is on_key's own Return
    // arm (input_handler.cpp) and there is no second body — so the editor's
    // gates, the P-view refusal, the nothing-focused no-op, the read-only drop
    // and the modal consume are all inherited whole. THE KEYPAD'S Enter is the
    // same arm's second keysym and is NOT a second row here: a row spells the
    // chord the dispatch synthesizes, and Return is the spelling every reader
    // accepts (the marker walk's IsoLeftTab precedent).
    //
    // IT IS NOT A RADIO AND CARRIES NO LAMP (an act, not a mode), it does NOT
    // repeat (an editor opener, and openers never repeat — the Measure's own
    // line), and it admits NEITHER shift NOR ctrl, so a modified press is
    // refused at the band gate and the long press reaches nothing.
    //
    // THE BUTTON IS ENTER IN EVERY STATE, A STANDING EDITOR INCLUDED
    // (architect 2026-08-27, naming the button by its hotkey: "just say Enter,
    // because that's what triggers the editor"). A button that IS a key does
    // what the key does, so a press while the flag or the measure editor stands
    // COMMITS that editor on valid text and RED-FLASHES it on invalid — exactly
    // what the keyboard's Enter does there, and what the on-screen keyboard's
    // own Enter key does. No second road and no special case: the bottom row's
    // band claim sits ABOVE the outside-press closer in on_button_press, so the
    // editor is never torn down under this press, and the lift's dispatched
    // Return arrives at the editor's own key route like any other Return.
    // THE FLAG DOUBLE-CLICK DIFFERS, AND THE DIFFERENCE IS THE POINT: it stays
    // close-then-reopen, because a double-click on a flag names a MARKER — the
    // outside-press closer runs on its first press and the second press opens
    // the editor on the marker under the finger — while this button names the
    // KEY, which has no marker under it at all.
    //
    // ITS GATES ARE THE MEASURE'S but one exception narrower: the `h` view
    // consumes bare Return and greys it with the verbs, and so does the
    // READ-ONLY lock (the canonical line is serialized content). Where it
    // differs from the Measure is the P VIEW — phase resets have no per-flag
    // editor, so the act refuses there — and that refusal GREYS the button
    // since 2026-08-30 (flag_editor_open_actionable, the Return arm's own
    // predicate, under the truthful-buttons ruling; it was a consumed no-op
    // with a live face under the 2026-08-15 no-blink ruling until then).
    {RedesignButton::IconMarkerEditFlag,
     GuiKeys::Return, false, false, false, false, true},                            // bare Enter
    // THE MARKER MEASURE (architect 2026-08-19), the verb group's sixth since
    // 2026-08-27 and its fifth before that, with the Copy resolved value
    // button seated immediately behind it since 2026-08-29: BARE
    // `/`, which was free. It opens the measure editor on the focused marker,
    // and button-is-its-chord holds literally — the press dispatches bare `/`
    // through on_key at the LIFT like every other chrome button, while the KEY
    // acts at the press like every other hotkey.
    //
    // IT IS NOT A RADIO AND CARRIES NO LAMP (an act, not a mode), and it does
    // NOT repeat: an editor opener, and openers never repeat.
    //
    // ITS GATES: the `h` view consumes bare `/` and greys it with the four
    // verbs above, and so does the READ-ONLY lock (a measure is serialized
    // content). It differs from those four in the one place that matters — no
    // HOME-VIEW gate, measures being the fourth ruled exception to the
    // home-view binding — but that refusal was never a face anyway.
    {RedesignButton::IconMarkerMeasure,
     GuiKeys::Slash,  false, false, false, false, true},                             // bare /
    // THE COPY RESOLVED VALUE BUTTON (architect 2026-08-29), the verb group's
    // SEVENTH and the value pair's pointer home: BARE `j`, which was free
    // (greped at the landing). Its plain lift copies the focused marker's
    // resolved value to the system clipboard, exactly as the key does —
    // button-is-its-chord literally, the press dispatching bare `j` through
    // on_key at the LIFT while the KEY acts at the press.
    //
    // AND IT ADMITS SHIFT: its twin is SHIFT+`j`, THE JUMP to the marker that
    // value came from, on the other A/B tab — so a shift-click or a LONG PRESS
    // at kChromeShiftHoldMs puts a reference and its definition one Ctrl+Tab
    // apart with no keyboard, which is the whole reason the admission exists
    // on glass. The row's own `shift` bit stays FALSE, the admission and the
    // table bit being mutually exclusive by the shift term's construction
    // (finish_chrome_press_release); the membership is
    // redesign_button_shift_admits (app_state.h) and the tooltip's second line
    // is bound to it by that predicate's static_assert.
    //
    // NOT A RADIO AND NO LAMP (an act, not a mode), and it does NOT repeat: a
    // copy repeats onto itself and a jump has one destination, so the key is
    // one-shot at repeat_eligible and the button carries no `repeats`. ITS
    // GATES ARE NOT THE VERBS': the READ-ONLY lock ADMITS both chords (a
    // clipboard write, a tab switch, a playhead move and two camera frames
    // author nothing), so the button stays lit on a locked tab, while the `h`
    // view consumes bare `j` and greys it with the group through the derived
    // partition.
    {RedesignButton::IconCopyValue,
     GuiKeys::J,      false, false, false, false, true},                             // bare j
    // ADD TO SELECTION (architect 2026-08-18), the verb group's EIGHTH since
    // 2026-08-29 (its seventh from 2026-08-27 and its sixth before that):
    // BARE `k`, which was free — he picked it over `n`
    // (already reading as INHERIT) and over `a` (too easy to hit by accident).
    // It is a MODE toggle, so the button's lamp reads the same bit the key
    // flips and button-is-its-chord holds literally: the press dispatches bare
    // `k` through on_key at the LIFT like every other chrome button, while the
    // KEY acts at the press like every other hotkey — both fall out of the
    // machinery, with no timing code of its own anywhere.
    //
    // ITS GATES DIVERGE FROM THE FOUR ABOVE IT and that is the point: the `h`
    // view consumes bare `k` and greys the button with them, but the READ-ONLY
    // lock ADMITS it (read_only_key_blocked's allowlist) — a selection is
    // navigation, not authored content.
    {RedesignButton::IconAddToSelection,
     GuiKeys::K,      false, false, false, false, true},                             // bare k
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
    // ALL THREE ARE LIVE INSIDE THE `h` VIEW and the derived partition says so
    // with nothing hand-listed — history_mode_owns_key claims bare Tab and
    // Shift+Tab as the diff-flag cycle forward and back, and Ctrl+Shift+Tab
    // (2026-08-18) as the march that composes that cycle with the A/B switch.
    // WALK BOTH TABS greyed there for the hours between the walk selector
    // leaving row 3 and that ruling.
    {RedesignButton::TransportWalkPrev,
     GuiKeys::Tab,    false, true,  false, false, true},                             // Shift+Tab
    {RedesignButton::TransportWalkNext,
     GuiKeys::Tab,    false, false, false, false, true},                             // bare Tab
    {RedesignButton::TransportWalkBoth,
     GuiKeys::Tab,    true,  true,  false, false, true},                             // Ctrl+Shift+Tab
    // The arrows, in their painted order since 2026-08-14 (the architect's:
    // down, up, left, right, replacing the row's original vim order). The
    // lookup is by id, so this order is for the reader alone. The eighth
    // column is `repeats`, which these four set — as does the waveform
    // magnification pair above since 2026-08-26; the column IS the
    // membership (the contract is at ToolbarChord::repeats).
    {RedesignButton::TransportDown,
     GuiKeys::Down,   false, false, false, false, true, true},                       // bare Down
    {RedesignButton::TransportUp,
     GuiKeys::Up,     false, false, false, false, true, true},                       // bare Up
    {RedesignButton::TransportLeft,
     GuiKeys::Left,   false, false, false, false, true, true},                       // bare Left
    {RedesignButton::TransportRight,
     GuiKeys::Right,  false, false, false, false, true, true},                       // bare Right
};

// THE TABLE IS TOTAL OVER THE ROSTER, ENFORCED AT COMPILE TIME (2026-08-06):
// every RedesignButton but the FOUR menu anchors carries a chord here — 49
// rows against the roster's 53 since 2026-08-29'S COPY VALUE BUTTON, one pure
// addition inside an existing group (a chord, so the pair moved together): the
// bottom row's verb group gained a FIFTH box on bare `j`, the value pair's
// pointer home, and no separator or group boundary moved. It was 48 against 52
// from 2026-08-27'S EDIT FLAG BUTTON, one pure
// addition inside an existing group (a chord, so the pair moved together): the
// bottom row's verb group gained a fifth box on bare Enter, the flag editor's
// third road, and no separator or group boundary moved. It was 47
// against 51 earlier that day, at the SERIES RELOCATION, which
// moved BOTH numbers in one act: the two mass-marker rows (bare `m`, bare `i`)
// were deleted with their buttons and the SERIES ANCHOR joined row 1 carrying
// no chord, so the table lost two and the roster lost one. Its two commands
// are untouched on the keyboard — they are MENU ITEMS for the pointer now, and
// an item dispatches through on_key exactly as a row here does. It was 49
// against 52 earlier that day, when the MAGNIFICATION RESET
// was deleted with its Ctrl+0 chord (one row, so the pair moved together);
// the same ruling swapped the zoom and magnification SPELLINGS, which moved
// neither number. It was 50 against 53 from 2026-08-26, when the WAVEFORM
// MAGNIFICATION arrived in the zoom group: the stepping pair first, then the
// RESET later the same day with the ladder's
// retune (three chords, so the pairs moved together). It was 47 against 50
// from 2026-08-20, when the EDIT anchor joined
// row 1 and IconCopy and IconPaste were deleted from row 4 in one act (one
// non-chord entry gained, two chord rows lost). It was 49 against 51 from
// 2026-08-19, when the MARKER MEASURE
// arrived on bare `/` (a chord, so the pair moved together). It was 48 against
// 50 from 2026-08-18's THIRD ruling, when the two
// WALK RADIOS arrived on bare `g` (two chords, so the pairs moved together;
// the same ruling took the walk selector off row 3, which moved neither number
// — the tabs were always two rows here and still are, on Ctrl+Tab). It was 46
// against 48 earlier that day, when ADD TO
// SELECTION arrived on bare `k` (a chord, so the pair moved together), and
// 45 against 47 before that, when the TRIM SCISSORS left
// both (they carried a chord, so the pair moved together; the trim region
// toggle's own chord is untouched by that). The same relayout moved eight buttons BETWEEN ROWS and this
// table did not feel it: it is keyed by id and every reader matches by id or
// by published rect, so the row order it is kept in is for the reader alone.
// It was 46 against 48 from 2026-08-16's SHOW TRIM REGION addition (its chord
// has been Ctrl+Shift+X, then bare `x`, and is bare `[` since 2026-08-24; a
// repointing moves no count), and 45 against 47 from
// 2026-08-15's Navigation deletion; earlier
// that day it was 45 against 48: the marker walk's three in, the collapsed
// play/stop pair's second row out, and the Navigation ANCHOR carried no chord,
// so its removal moved the roster and not this table — so the
// table's length plus those two IS the roster. The check is not bookkeeping —
// history_mode_disables_button walks this table and DEFAULTS AN UNLISTED BUTTON
// TO LIVE, so a roster entry added without its row here would silently wear a
// live face in the `h` view while its press claimed nothing. This makes that
// drift a build error instead. (It was + 2 until 2026-08-13, when the Quit
// button became the File menu's one item: the roster's total did not move, the
// split did.)
static_assert(std::size(kToolbarChords) + 4 ==
                  static_cast<std::size_t>(kRedesignButtonCount),
              "kToolbarChords must cover every RedesignButton except the "
              "File, Edit, Series and Settings anchors");

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
//
// THE NOTIFICATION CARDS ARE HIT ABOVE THE VEIL, BY RULING, AND ARE NOT AN
// EXCEPTION TO IT (architect 2026-08-29): a card is not a reach into the
// veiled surface — it is the message about the act the veil stands over —
// so its X must answer under a prompt, the player, the picker and every
// dialog editor alike. The claim sits at on_button_press's head, ahead of
// every gate (the rule at notifications.h).

// Is (x, y) inside the PAINTED rect of a redesigned button? The rect is the
// painter's stash and nothing here re-shapes or re-measures, so the clickable
// region is exactly the drawn one. A zero rect (before that row's first paint)
// contains no point, which is the correct cold answer.
bool redesign_button_hit(const AppState& app, RedesignButton id, int x, int y) {
    return rect_contains(
        app.redesign_buttons[redesign_button_index(id)].rect, x, y);
}

// THE FOUR BAND CLAIMS' ONE MODIFIER GATE — "does this modified press spell a
// roster chord at all", asked before the arm so a press that spells nothing
// stays the strict consumed no-op it has always been. One body for all four
// rows, the band-claim shape's own rule rather than a row's.
//
// ALT IS REFUSED EVERYWHERE: the roster has no alt chord, and alt's whole
// pointer vocabulary is the WHEEL's stepped pan (2026-08-27), which no press
// can spell (conventions.md).
//
// CTRL BINDS ONLY WHERE redesign_button_ctrl_admits SAYS SO (app_state.h) — the
// two SKIPS since 2026-08-24, whose ctrl-click is Ctrl+Home / Ctrl+End — which
// is why the gate asks the BUTTON under the pointer rather than the band: the
// admission is the roster's, so a ctrl press anywhere else, on the bare ground
// of any row, or on one of the four dropdown anchors (which carry no chord row
// at all) is refused exactly as it always was. The walk is kToolbarChords in the
// arm's own order, so the button this answers about is the button that would
// arm.
//
// CTRL+SHIFT TOGETHER SPELL NO ROSTER CHORD on any button — strict modifier
// validation, and Ctrl+Shift+Home is unbound on the keyboard too — so the pair
// dies here and the lift's chord build never sees it.
//
// A SHIFT press is deliberately NOT judged here: its admission is a press-time
// refusal of the arm body's (arm_redesign_press), where a non-admitting button
// consumes the press rather than letting the band answer for it.
bool chrome_band_modifiers_refused(const AppState& app, int x, int y,
                                   GuiInputState mods) {
    if (mods.alt) return true;
    if (!mods.ctrl) return false;
    if (mods.shift) return true;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_hit(app, tc.id, x, y))
            return !redesign_button_ctrl_admits(tc.id);
    }
    return true;
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

// Active-domain playhead frame at click column `col`: the single-rounding
// display grid (displayed_grid_position_at_column via painter q) in BOTH views,
// so a click, a 1px step and the marker commits agree on ONE lattice per view
// and a drop-at-playhead lands where a drag or a nudge would. The target branch
// used to take the domain-spp form on the reasoning that the source-frame commit
// routes through the inverse map and so carried no source-grid claim; the
// target-domain lattice is an authoring lattice in its own right (the
// phase-reset drop commits the playhead's sample, and authored_frame_at_column's
// target arm rides this same grid), and an unanchored landing relabels by a
// frame across a pan or a zoom round trip.
// The fallback covers degenerate geometry only (no strip width / no zoom), where
// there is no painted grid to land on.
int64_t playhead_frame_at_click_column(const AppState& app,
                                       const GuiAudio& audio, int col) {
    const double q = painter_samples_per_pixel(app, audio, waveform_area(app));
    if (q > 0.0)
        return static_cast<int64_t>(std::llrint(
            displayed_grid_position_at_column(app.viewport_start_sample, col, q)));
    return app.viewport_start_sample;
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
    // true = one of the four DIALOG editors (settings / commit-title /
    // measure paste-offset / BPM, painting in the BOTTOM ROW'S modal since
    // 2026-08-13 — centered for the one day from 2026-08-12, and the field
    // was `bottom_strip` while they lived on the status lane); false = the
    // top-strip flag editor. Selects the claim
    // region and the repaint owner.
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
        text_editor::is_active(app.commit_title_editor) ||
        text_editor::is_active(app.measure_offset_editor) ||
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
    if (dialog_open) {
        if (!be.valid) return g;
        g.ed = text_editor::is_active(app.settings_editor)
                   ? &app.settings_editor
             : text_editor::is_active(app.commit_title_editor)
                   ? &app.commit_title_editor
             : text_editor::is_active(app.measure_offset_editor)
                   ? &app.measure_offset_editor
                   : &app.top_flag_editor;
        g.text_left    = be.text_origin_x;
        g.byte_x       = &be.byte_x;
        g.dialog       = true;
        g.valid        = true;
        return g;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // THE MARKER LANE'S OWN FIELD — the UNROLLED FLAG BOX under the payload
        // editor, the BLUE MEASURE BOX under the measure editor. KIND-BLIND
        // deliberately: both publish through the one painter into the one
        // stash, so this reads the published geometry and never asks which kind
        // made it (contract at FlagEditorBox, render.h). The origin already
        // carries the view offset and the boundaries are the shaped run's own
        // pen, so there is nothing to re-derive and nothing that could disagree
        // with the pixels. An invalid publication (no box painted yet, or a
        // target the store shrank past) simply leaves this invalid — the same
        // answer the old -1 origin gave.
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
// two, since 2026-08-15 (one of three from 2026-08-08).
// The anchors are the roster's only NON-chord actions, so there is no chord
// to ask the gate about and each has to be answered here. SETTINGS is DEAD
// because toggle_dropdown refuses that menu while the mode stands (its first
// line): its six items open the settings editor, a modal the view has no place
// for. FILE (2026-08-13) is LIVE: its one item is Ctrl+Q, which the mode
// admits, so its menu works in there.
// (NAVIGATION was a third entry, LIVE from 2026-08-08 — the architect ruled its
// menu open in the view, the toggle stopped refusing it, and every one of its
// seven rows was a chord that met the mode's own gates through on_key, so
// nothing about it was a hand answer beyond this function's silence on it. The
// one row the view consumed greyed at the ITEM instead, a surface this
// partition does not reach: it answers about BUTTONS, and a menu row is not
// one. The anchor and its menu are deleted 2026-08-15, and the item-grey
// predicate went producer-less with them.)
//
// THE TWO TABS ARE DERIVED LIKE EVERYTHING ELSE, and have been since
// 2026-08-05 — through two different facts. From then until 2026-08-18 the row
// was the WALK SELECTOR and Ctrl+Tab was the mode's own walk CYCLE, claimed by
// history_mode_owns_key, so this walk answered LIVE for the pair on its own;
// since 2026-08-18 the row is the A/B tabs again and Ctrl+Tab is on the mode's
// own ALLOWLIST (the architect's ruling that a tab switch works normally in the
// view), so the same walk answers LIVE from the other predicate. THE ONE DAY
// THAT NEEDED A HAND ENTRY was the walk selector's first, when the pair shipped
// with no hotkey at all: their chord was consumed while their buttons were
// live, and only a hand entry could say so. No lock rides this row in any state
// since 2026-08-14, the padlock having moved to the icon row's own read-only
// button.
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
// struct); and it admits CTRL+H only while a diff flag is selected on a
// writable tab (history_revert_actionable), so this walk answers DEAD for
// REVERT with an empty subject or a locked tab — AN ANSWER THE FACE READS
// AGAIN SINCE 2026-08-30 (it read nothing from 2026-08-15 until then, when
// redesign_button_enabled lifted the four history companions over this
// partition for the architect's blink reasoning; the truthful-buttons ruling
// deleted the lift, and decision 58 composed the lock in — the record is at
// the companions' arm in redesign_button_enabled, app_state.h, and the note
// below on the revert act says the same where the entry is spelled); and bare `'` only while
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
// hand-answered with the ONE other anchor, and the Ctrl+Q admission it rested
// on is unchanged; the hand entries were three until the Navigation anchor left
// with its menu on 2026-08-15):
//   LIVE — the view bar's ViewSW/ViewTP/ViewTW (bare
//   1/2/3, the admitted view selectors), Save (Ctrl+S, which in this mode IS the
//   save-and-commit checkpoint act and wears the "Save and Commit" face — LIVE
//   ONLY WITH A NON-EMPTY HEAD DELTA AND NO CHECKPOINT IN FLIGHT, and greyed
//   rather than relabelled in either case; it was RENDER's chord and RENDER's
//   face until 2026-08-08, when the act moved onto the save it begins with),
//   the icon row's S/T + W/P radios (bare `t` / `p`, admitted with the view
//   switches), THE ZOOM GROUP's four since the 2026-08-12 relayout (Ctrl+`=`,
//   Ctrl+`-` and bare `0` are the allowlist's own zoom admissions and bare `c`
//   is the mode's vocabulary — pure navigation, live with nothing hand-listed),
//   the load-editor opener (bare `'`, which in this mode loads THE
//   VIEWED WALK'S MEMBER in place — the commit's sidecars on the Remote tab,
//   the timeline state on the Local one since 2026-08-08, and live on both:
//   the THIRD session-dependent entry, dead on a walk with no member — the
//   Remote tab's empty one, since 2026-08-09), and the history button itself
//   (bare `h`, the
//   mode's own key,
//   selected while it stands),
//   and BOTH TABS since 2026-08-05 — live as the walk selector until
//   2026-08-18 and as ORDINARY A/B TABS since (Ctrl+Tab is on the allowlist,
//   so they come out of the walk like any other admitted chord), with no
//   padlock drawn on either in any state since 2026-08-14,
//   and THE TWO WALK RADIOS since 2026-08-18 (bare `g`, the mode's own
//   vocabulary),
//   and THE WALK'S TWO STEPS since 2026-08-05 — older (bare `,`) and newer
//   (bare `.`), the mode's own vocabulary again, so this walk answers LIVE for
//   them with nothing hand-listed,
//   and TWO OF THE BOTTOM ROW'S MARKER-WALK GROUP since 2026-08-15 (bare Tab
//   and Shift+Tab) — the mode's own vocabulary once more, so both answer LIVE
//   for free and step the DIFF-FLAG cycle forward and back — WITH THE GROUP'S
//   THIRD SINCE 2026-08-18 (Ctrl+Shift+Tab, the mode's own vocabulary too now:
//   it marches the pair over that same diff-flag cycle, "tab, ctrl+tab, tab"
//   read against the lane in here; it ran the reverse WALK-SOURCE cycle from
//   2026-08-07 until the walk moved to the icon row's own radio pair, and was
//   blocked for the hours between),
//   and THE BOTTOM ROW'S SKIPS and THE ZOOM-ORIGINAL button on the same terms
//   (bare Home / End are the mode's absolute jumps, bare `c` its own centring),
//   and THE CUMULATIVE TOGGLE since 2026-08-08 (bare `u`, the same vocabulary
//   and the same free answer). Those three plus Revert are the roster's
//   RESTING-DISABLED family — four of them, joined by the TWO WALK RADIOS on
//   2026-08-18 for six, all six gated at rest on `history_mode.active` by their
//   own arm in redesign_button_enabled (which carries the succession, the plain
//   `true` of 2026-08-15..18 included). This walk's answers for them have never
//   changed and this paragraph is still what says they ACT in here; what the
//   companions' own arm does is answer the RESTING face this partition cannot
//   reach and, since 2026-08-30, the walk's two WALLS for Older / Newer
//   (which this partition cannot see either, both chords being the mode's own
//   vocabulary) — the lift that kept the four over this partition INSIDE the
//   view from 2026-08-15 is deleted (see the note on Revert below).
//   and THE REVERT ACT since 2026-08-05 (Ctrl+H) — the SECOND session-dependent
//   entry: the allowlist admits its chord only while a diff flag is selected
//   (history_mode_revert_subject_standing), so this walk answers DEAD with an
//   empty subject and LIVE the moment a click selects one, from the same
//   admission with nothing restated here, exactly as Save's head-delta grey
//   does. ITS FACE READS THAT ANSWER AGAIN SINCE 2026-08-30: from 2026-08-15
//   the button was the one place a per-SELECTION fact reached a chrome glyph,
//   and redesign_button_enabled lifted the four companions over this
//   partition entirely for the blink rather than the logic; the
//   truthful-buttons ruling ("Any time a button would be a no-op, grey it")
//   deleted that lift, so the chord's refusal on an empty subject and the
//   button's grey are one decision, this walk's.
//   and THE FILE ANCHOR since 2026-08-13 — THE ONE LIVE ENTRY THAT IS NOT A
//   CHORD'S ADMISSION, which is why it is spelled in the body rather than
//   derived: an anchor has no chord to ask about, so the four anchor arms
//   answer ONE criterion by hand — an anchor is dead iff every row of its menu
//   is dead — and File is the one that answers it the other way. ITS MENU IS
//   THREE ROWS NOW AND ALL THREE ARE LIVE IN THE VIEW (architect 2026-08-29,
//   "admit both"): Ctrl+Q always was, Ctrl+O joined the mode's allowlist that
//   day, and Synchronize is the menu's one CHORDLESS row, which meets that
//   allowlist nowhere and answers inside its own act. Two of the three were
//   consumed nothings from their 2026-08-28 landing until that ruling, the state
//   this arm's own criterion would have greyed the anchor for had an anchor
//   been derivable at all. (THE NAVIGATION ANCHOR was the
//   other live one, from 2026-08-08 on that same reasoning, and it left
//   the roster with its menu on 2026-08-15 — so the LIVE hand entries went from
//   two to one and the hand entries in total from three to two.)
//   DEAD — Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z); RENDER since 2026-08-08
//   (Ctrl+Alt+R, which left the allowlist with its shifted twin when the act
//   moved onto Ctrl+S — so the button wears its ordinary Render face over this
//   partition's dead one, and the walk says so with nothing hand-listed);
//   copy phase (Ctrl+P), paste
//   phase (Ctrl+Alt+P), the BPM
//   opener (bare `m`), iteration mode (bare `i`), follow (bare `f`), listen
//   (bare `l`); the FOUR MARKER VERBS since
//   the 2026-08-12 relayout (bare `s`, Delete, Ctrl+D, Ctrl+N — authoring,
//   consumed like the rest, and unmoved by their 2026-08-18 change of ROW:
//   this walk asks about a chord, never about a lane); ADD TO SELECTION (bare
//   `k`, since 2026-08-18); and the
//   SETTINGS anchor
//   — the only anchor here
//   since 2026-08-08, when NAVIGATION moved to the LIVE column above with its
//   menu (FILE has never been in this column: it landed live, 2026-08-13).
//   (THE TRIM SCISSORS were here on their own chord until their button was deleted on
//   2026-08-18; the key is still consumed in the view and there is simply no
//   button left to grey.)
//
// TWO THINGS IT DELIBERATELY DOES NOT SAY. (1) The base chord decides the face,
// which since 2026-08-08 has nothing left to arbitrate on row 2: the ONE button
// whose shifted twin the mode consumed while its base chord stood — Render —
// is dead on both shapes now, and Save admits no shift press at all. (Save's
// own base chord is what this walk asks about, its shift column being false in
// the table and in redesign_button_shift_admits alike.) (2) A
// button the READ-ONLY tab bit refuses is not this function's business: that
// refusal is the lock's, and it applies inside the view exactly as outside it.
// SINCE 2026-08-15 THE LOCK GREYS ITS OWN SET (redesign_button_enabled's
// read-only arm — the architect's second MODE statement, which owns that
// membership; no count is restated here), so the two greys can
// now land on the same button and simply agree; but they are still two facts
// with two owners, and neither reaches into the other. (The clause this
// paragraph used to end on — "the `'` button stays lit on a locked tab, in the
// view as out of it" — was true under the never-grey rule and is superseded by
// that ruling: `'` is one of the ten, and it greys on a locked tab in either
// state now.) Only the VIEW's own consumption greys anything HERE.
//
// EVERY DEAD ANSWER IS PAINTED (architect 2026-08-14, "no more
// hiding/showing icons in top icon row"): the mode-collapsing roster of
// 2026-08-12, narrowed on 2026-08-13 and deleted whole on 2026-08-14, used to
// take part of this partition's DEAD column out of the icon row's walk
// entirely. It does not any more — this walk's verdict is the grey face
// everywhere, on every row. THE PRODUCT NOW HIDES NOTHING AT ALL (2026-08-18):
// the bottom row's cluster swap was the last hiding mechanism left, and it went
// with the four history companions when they returned to the icon row — the
// arrows paint unconditionally and every roster button publishes a real rect in
// every state.
bool history_mode_disables_button(const AppState& app, RedesignButton b) {
    if (b == RedesignButton::Settings) return true;
    // THE EDIT ANCHOR IS DEAD IN THE VIEW (2026-08-20), on the Settings
    // pattern and for a different reason: Settings is named here because its
    // items reach the settings editor by a DIRECT call that meets no gate,
    // while EVERY Edit item is a CHORD the mode's allowlist already drops — so
    // the menu would open, and every row in it would do nothing. That is the
    // face promising more than the keys deliver, which is what this partition
    // exists to prevent. It is HAND-NAMED rather than derived because an anchor
    // carries no chord for the walk below to ask about, and the walk's default
    // for an unlisted button is LIVE.
    if (b == RedesignButton::Edit) return true;
    // AND SO IS THE SERIES ANCHOR (2026-08-27), on the Edit arm's own
    // criterion rather than on a new one: both of its rows — bare `m` and bare
    // `i` — are chords the mode's allowlist drops, so the menu would open onto
    // nothing. Hand-named for the same reason Edit is (an anchor carries no
    // chord for the walk below to ask about, and the walk's default for an
    // unlisted button is LIVE), and its half of the pair is at
    // toggle_dropdown, which refuses to open it at all.
    if (b == RedesignButton::Series) return true;
    // FILE IS THE ANCHOR THE CRITERION ANSWERS LIVE (2026-08-29): all three of
    // its rows act in the view — Ctrl+Q and Ctrl+O through the allowlist,
    // Synchronize being chordless — so the menu opens onto three working rows
    // and a dead face would be a lie. Hand-named for the anchors' shared
    // reason (no chord for the walk below to ask about), and kept as its own
    // arm rather than left to the walk's unlisted default so that the tail's
    // "not in the table and not an anchor" claim stays true.
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
    // (the table plus the four anchors is the whole roster) and stated rather
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
// THE LAND HIDES THE TRIM REGION OVERLAY, and since 2026-08-19 that is the
// land's own act rather than every caller's. The REGION IS THE TRIM (its
// contract is at RegionState, app_state.h) and the overlay hides when the
// playhead's position in the music changes — so the two functions that change it
// are the two that hide, this one and Viewport::move_playhead_to. The rule, its
// exemptions and the RESEAT entry point for the callers whose write is not a
// movement all live at clear_region_highlight (input_handler.h); do not restate
// them here or anywhere else. What is worth stating at the land is only this:
// the hide sits ABOVE the write, so it is unconditional even where the seat
// early-returns.
//
// LANDS the playhead exactly onto marker `hit` (active column's store), with
// NO viewport move — the sole difference from Tab (which recenters) and `c`
// (which re-zooms and recenters), so the view holds perfectly still while the
// playhead seats. THE CALLER INVENTORY, THE ONE AUTHORITATIVE ENUMERATION,
// re-derived by grep 2026-07-29 (other sites state their own class and point
// here; do not copy this list, re-derive it):
//   * THE POINTER CLICKS — the plain marker click, the shift RANGE click and
//     the ctrl TOGGLE click, ALL THREE THROUGH ONE SITE since 2026-08-15
//     (run_marker_click_act, this file: the click acts AT THE PRESS again
//     since 2026-08-17, so the one body runs where the three arms used to).
//     Each lands on
//     the FOCUS its own arm just set: the clicked marker, the clicked range
//     end, the toggled-in marker, or the focus repaired after a toggle-out (an
//     empty post-toggle selection lands nothing). The plain click's FOUR
//     deferred completions left this list 2026-07-29 with the group drag —
//     horizontal movement is a focus act, the doctrine at the head of
//     position_nudge.h;
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
//     class 2026-08-08 (close_history_mode, input_key_dispatch.cpp) and LEFT IT
//     on 2026-08-18: its restore re-spelled the flip's column-preserving
//     translation whenever a visit left the entry audio view, so it re-spelled
//     the flip's land too, and the whole restore is deleted with the view's
//     navigation-state ownership — the `h` view lands nothing at its exit now.
//     Ctrl+Tab left this class
//     when the parked
//     selections died: it restores its tab's stored cursor VERBATIM, hands the
//     lane nothing, and its only land is the auto-select's below;
//   * THE COINCIDENCE AUTO-SELECT (auto_select_marker_at_playhead, this file) at
//     its entry chokepoints (the inventory is at its declaration,
//     input_handler.h). A provable NO-OP by
//     construction (its selection predicate IS this function's equality test), and
//     it is in the list because the adjacency is the rule, not because it moves
//     anything — WHICH IS WHY IT TAKES THE RESEAT rather than this entry point
//     since 2026-08-19: a no-op write must not carry the land's hide;
//   * (THE MAP CHANGERS ARE GONE FROM THIS LIST, architect 2026-07-29:
//     the settings engine-commit and the settings-only 'S' undo/redo arm each
//     landed here, target view only, because the rebuilt map moved the focused
//     marker's image out from under a resting cursor — and both now CLEAR THE
//     SELECTION at their own tails instead, so there is no lane and no focus left
//     to re-land. The map-change re-land SHAPE lives entirely off this entry
//     point — its whole membership is the caller list at
//     Viewport::reseat_playhead_to (viewport.cpp), which is where it re-lands
//     instead, for the keep-visible scroll and, since 2026-08-19, for the second
//     reason too: a re-land onto a marker whose IMAGE moved is a translation,
//     not a movement, so it must not hide the overlay. The undo/redo restore
//     rejoined that family on 2026-08-28 — through the RESEAT, on the cursor's
//     own instant rather than on a focus, so nothing about this list changes.)
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
// the ops/views TUs can reach it. THE `p` SWAP LEFT THIS LIST 2026-08-19 with
// its own overlay hide: a column switch lands nothing (the swap empties the
// selection, so there is no focus to re-express) and hides nothing.
// THE STORE LOOKUP IS ALL THIS FUNCTION ADDS to the one below it. Resolving the
// index to an authored frame is the marker-shaped half; the two-step placement
// basis and the damage are the frame-shaped half, hoisted into
// seat_playhead_on_source_frame so a caller holding a frame that belongs to NO
// store entry — the `h` history mode's diff flags, whose removed lines exist in
// no store at all — lands through the identical expression instead of a second
// copy of it, and so the land / reseat pair cannot drift either. Everything the list above says about WHEN a land happens and what
// it must not touch governs both halves alike.
// The shared write, defined below the two entry points that share it.
static void seat_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport, int64_t src_frame);

void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit) {
    // THE HIDE IS THE LAND'S, not the caller's, since 2026-08-19 — this is the
    // second of the rule's two movement owners (the rule and its exemptions are
    // at clear_region_highlight, input_handler.h). It sits ABOVE the store
    // lookup and above the frame half's idempotence return, so a land that
    // seats nothing still hides: an unconditional hide is the marker click's own
    // recorded shape and the Home/End skip buttons' too.
    clear_region_highlight(app, viewport);
    // THE A/B AUDITION ENDS ON THE SAME MOVEMENT, by the same membership — a
    // land is the marker-shaped spelling of "the playhead's position in the
    // music changes", and the act's premise is a resting cursor that cannot
    // move under it. Beside the hide rather than in the shared seat below,
    // because reseat_playhead_on_marker shares that seat and its land is a
    // TRANSLATION or a provable no-op, neither of which is a movement. A class
    // statement: the complete clearing-owner inventory is at
    // GuiAuditionSequence (app_state.h).
    clear_audition_sequence(app);
    reseat_playhead_on_marker(app, audio, viewport, hit);
}

// THE MARKER RESEAT — the same store lookup and the same write with NO hide, for
// the callers whose land is not a movement (2026-08-19). RE-DERIVED BY GREP —
// two:
//   * THE S/T AUDIO-VIEW FLIP's re-express of a surviving focus
//     (switch_active_audio_view_to, input_handler.cpp). A TRANSLATION IS NOT
//     A MOVEMENT: the flip maps the same musical instant into the other domain
//     and this call is the accurate spelling of that map for a focused marker,
//     the generic double round trip being the inaccurate one. The user has not
//     turned to other work; the work has changed domain.
//   * THE COINCIDENCE AUTO-SELECT (auto_select_marker_at_playhead, below), whose
//     land is a PROVABLE NO-OP — its selection predicate IS the frame half's
//     equality test — so there is no movement to answer for. Its callers are the
//     tab and column entries, which are restores and switches and hide nothing
//     of their own.
// Named rather than spelled as a flag on the land: a parameter meaning "skip the
// rule this time" is the hand-listed inventory in disguise.
void reseat_playhead_on_marker(AppState& app, const GuiAudio& audio,
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
    if (valid) seat_playhead_on_source_frame(app, audio, viewport, src_frame);
}

// The frame-shaped half, above — the MOVEMENT owner's frame form: the hide and
// the audition's end, then the seat. Every caller of this one is a command whose
// subject is the playhead (the `h` mode's Tab cycle, its `c` and its two
// diff-flag click bodies), so none of them wants the reseat.
void land_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                   Viewport& viewport, int64_t src_frame) {
    clear_region_highlight(app, viewport);
    // The A/B audition's end, this entry point's half of it — the argument is
    // at the marker form above, the inventory at GuiAuditionSequence.
    clear_audition_sequence(app);
    seat_playhead_on_source_frame(app, audio, viewport, src_frame);
}

// The write itself, shared by the two above. Its own two decisions:
static void seat_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport,
                                          int64_t src_frame) {
    int64_t sample = source_frame_to_active_domain(app, audio, src_frame);
    sample = clamp_playhead_to_live_domain(sample, app, audio);
    // IDEMPOTENCE ONLY, carrying no semantics: a land onto the sample the
    // playhead already holds writes the same value and moves no pixel, so
    // there is nothing to damage. It decides nothing about the region — the
    // hide is the LAND's, one level up, and runs above this return either way.
    // Compared AFTER the clamp, because the clamp is what decides where the land
    // actually seats.
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
// NO REGION WORK, AND THAT IS NOW A RULING RATHER THAN AN OBSERVATION
// (2026-08-19): the callers are ENTRIES — the A/B tab switch, the `p` column
// swap, the loads — and an entry is a restore or a switch, neither of which
// moves the playhead in the music. So this route reseats rather than lands, and
// the overlay it may find standing is the entering tab's own trim, which is
// exactly what the reader wants to keep seeing. (The "a region rests only beside
// an EMPTY selection anyway" belt is RETIRED, 2026-08-18: the overlay's
// visibility is bare `[`'s alone and that key writes no selection, so a shown
// overlay may rest beside any selection. It was never load-bearing here.)
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
    // THROUGH THE RESEAT, NOT THE LAND (2026-08-19): the land HIDES the trim
    // region overlay and this route must not, its callers being the tab and
    // column ENTRIES — restores and switches, not movements. The exemption is
    // free of judgement here: this land is a PROVABLE NO-OP (the predicate that
    // selected the marker IS the write's own equality test), so there is no
    // movement for the rule to answer. The rule and the reseat's two callers
    // are at clear_region_highlight and reseat_playhead_on_marker,
    // input_handler.h.
    reseat_playhead_on_marker(app, audio, viewport, hit);
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
// update in flight; a dead device) leaves playback stopped AND SAYS WHY since
// 2026-08-30 — the target gate's own kTargetPreviewNotReadyCard at this act,
// and the launch body's two sentences under it, this scrub being the one
// launch road with no outer gate of its own; a later click at a launchable
// frame launches.
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
    // buffer, which Space refuses — so the scrub launch refuses it too, AND
    // SAYS SO SINCE 2026-08-30, in Space's own sentence
    // (kTargetPreviewNotReadyCard, notifications.h). The two gates read
    // different predicates — preview_ready there, is_updating here — and
    // report one fact, which is why the words are shared rather than
    // spelled twice.
    if (app.active_audio_view == 'T' && target_render.is_updating()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kTargetPreviewNotReadyCard);
        return;
    }
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
//   0. a notification card under the pointer (2026-08-29) — the press's own
//      first claim, above every veil, and opaque: the Arrow, whatever lies
//      under the card;
//   1. the prompt's veil (the top of the handler — its dialog buttons are the
//      one thing a press can reach, and a button carries no cursor cue);
//   2. the five DIALOG modal editors' veil, which consumes every press
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
    // THE NOTIFICATION CARDS FIRST (2026-08-29), ahead of every refusal
    // below as their press claim is ahead of every gate: a card is opaque
    // to the pointer — no zone underneath it can promise anything — and its
    // own two surfaces, the body and the X, are a consumed press and a
    // button, which carry no cue anywhere in the product.
    if (notification_card_at(app, x, y) != 0) return GuiCursorKind::Arrow;
    if (app.prompt.active) return GuiCursorKind::Arrow;
    // THE RENDER PLAYER IS THE ARROW EVERYWHERE (2026-08-28): its three
    // pointer surfaces — the overlay's rows, the scrub, the modal row's
    // buttons — are buttons and a list, which carry no cue anywhere in the
    // product, and every other zone is behind its veil.
    if (app.render_player.active) return GuiCursorKind::Arrow;
    // AND SO IS THE PICKER (2026-08-28): its two pointer surfaces — the
    // overlay's rows and the modal row's one Cancel button — are a list and a
    // button, and it has no field to name the I-beam for.
    if (app.picker.active) return GuiCursorKind::Arrow;
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
    // IT SERVES BOTH TRIM SURFACES since 2026-08-18: the 9 px bar's endcaps and
    // bridge, and the waveform OVERLAY'S bounds and interior, which arm these
    // very drags (the region became the trim). The overlay's own live-cue arm —
    // a separate record with a separate kind from 2026-08-15 — is deleted with
    // the separate gesture, and this one names the same three shapes it did.
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
    // the same rule and naming exactly the same bound. (TrimBoundSet is that
    // record's ONLY kind since 2026-08-17, when the other four went back to
    // acting at the press, so there is no other-kind arm to fall through here.)
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
    // keep. (The record only ever holds a real drag: an OUTSIDE press arms
    // this same Pan after running its teleport at the press, so it needs no
    // kind of its own, and the Pending kind that carried the 2026-08-15 lift
    // deferral — with its Arrow arm — is deleted.)
    if (app.overview_drag.active) {
        switch (app.overview_drag.kind) {
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
    // THE PENDING IS PLAIN BY CONSTRUCTION since 2026-08-17: the flag's shift
    // and ctrl clicks act at the press and arm nothing (they have no drag to
    // become), so an armed marker pending always names the drag it may become
    // and the shift/ctrl fields it briefly carried are gone with their
    // producers.
    if (app.drag.active || app.pending_marker_press.active)
        return GuiCursorKind::TrimResize;
    // (THE REGION EDITOR'S OWN LIVE ARM STOOD HERE FROM 2026-08-15 TO
    // 2026-08-18 and is deleted with the gesture: the overlay's move and bound
    // drags ARE the trim bridge and endcap drags now, so the trim arm at the
    // top of this map — which reads trim_drag / pending_trim_drag's own record
    // — keeps their cue for the whole gesture, on that same live-gesture
    // exception and with one member fewer to remember.)
    if (any_pointer_gesture_active(app)) return GuiCursorKind::Arrow;

    // THE OPEN FLAG EDITOR'S BOX IS EDITABLE TEXT, so it wears the I-beam
    // (architect 2026-08-13, with the Text kind: it showed the navigation
    // surface's PAN before, the marker lane being nav surface under it, and a
    // hand over a text field is simply wrong). ABOVE THE MODIFIER ARMS,
    // because that is where the press path puts the claim: the caret / text-drag
    // block in on_button_press tests this same published rect
    // (app.flag_editor_box.box, the painter's stash) for ANY left press,
    // before any modifier is looked at, so ctrl over the open box still places
    // a caret and shift EXTENDS THE SELECTION to the clicked byte (architect
    // 2026-08-30) — both of them caret work, and the cue must say so. The
    // editor's pointer-TRANSPARENCY is untouched — that is about what a press OUTSIDE
    // the box reaches, and outside is exactly where this arm stops. A cold or
    // closed editor publishes a zero rect, which contains no point. It covers
    // the MEASURE editor's field too, which publishes the same rect through the
    // same painter — and covers the measure PAD deliberately NOT: that rect is
    // painted ink beside the field, not editable text, so an I-beam over it
    // would promise a caret the press does not seat.
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
    // one trim bound — and EVERYWHERE ELSE ON THE LANE IT IS TrimResize
    // ("left/right arrows like on plain trim hover", the architect's words),
    // because everywhere else the plain drag is the same one: the
    // box-follows-pointer pan, an x-only move-the-whole-span gesture, the trim
    // bridge's own shape. THE CUE NAMES THE DRAG, NOT THE CLICK, which is the
    // marker flag box's own rule (it wears TrimResize while its plain press
    // also runs a click act) — so the outside point wears it too, its press
    // teleporting and then arming that pan (2026-08-18).
    // THE BAND-WIDE ANSWER IS BACK AND ITS PREMISE IS NEW: it was true until
    // 2026-08-15 because an outside press extended the nearer BOUND, went to
    // the Arrow at codex round 19 under the map's standing rule that a point
    // arming nothing shows the Arrow, and returns now because the outside
    // point arms the PAN. Keeping the Arrow would also have made the cue
    // CHANGE at the drag's crossing — the live-drag arm above answers
    // TrimResize for a Pan — which is exactly the mid-slide flip the architect
    // closed on 2026-08-13.
    // The inside/outside question no longer reaches the cue at all, so the box
    // span is not asked here; degenerate geometry (no box) is the same answer,
    // its press being an outside press that teleports and pans like any other.
    // Ctrl/shift/alt/mixed presses on the lane bind nothing,
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
            return GuiCursorKind::TrimResize;
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
// offset. The full record is at GuiInputCore::notional_pointer_x_.
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
        // (GuiInputCore::notional_pointer_x_ carries the wrap's record).
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
    // window exactly as every apply_strip_drag_zoom caller pre-clamps. The
    // divisor is the RESOLVED rate — device px per level at the live gui_scale
    // — and never the authored constant; why the rate scales is at
    // nav_zoom_px_per_level(), app_state.h.
    double new_level = app.zoom_level - dx / nav_zoom_px_per_level();
    const double max_l = effective_max_zoom_level(wf_area.w, total,
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
// follow_overridden_for_session, app_state.h). IT RUNS AT THE PRESS since
// 2026-08-17 (CONTENT ACTS THE MOMENT ITS IDENTITY IS CERTAIN: an outside
// press can only mean the teleport, so there is nothing for a lift to
// disambiguate; the two-day lift deferral of 2026-08-15 — and the Pending kind
// that carried it — are deleted). The touch consequence that
// moved it to the lift is answered by the DISAMBIGUATION WINDOW rather than by
// deferral: the synthesized press is delivered only when the window resolves
// to ONE finger, and a pair landing inside the window goes straight to Nav
// with no press ever delivered, so a fast two-finger landing cannot fire this.
// ONE caller: the press router's outside-the-box arm, which hands
// it THE PRESS column — the point the user aimed at — and then FALLS THROUGH
// into the box pan's arm, so the teleport is the head of a gesture rather than
// the whole of one (2026-08-18). THE CENTERING ARITHMETIC is
// center_viewport_on_playhead's own
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

// THE TRIM REGION OVERLAY'S HIT TEST — the ONE spelling of "inside the shown
// overlay", read by the plain waveform press claim, the cursor map and the
// touch pan zone (the three-consumer contract is at the declaration,
// input_handler.h; the model is at RegionState, app_state.h).
//
// THE BOUNDS COME FROM THE PAINTER'S OWN OWNER — region_columns on the PLATE
// basis, the very call paint_region_ground makes, itself derived from the trim
// through trim_overlay_span — so a grabbed bound is exactly a painted one, by
// construction rather than by two derivations agreeing. Nothing is re-derived
// here. BoundLo IS THE TRIM BEGIN and BoundHi the END: a resting pair is
// ordered by construction, so the painted left/right and the authored
// begin/end are one distinction, which is what lets the press claim map a
// verdict straight onto that bound's own endcap drag.
//
// THE GRAB BAND IS trim_endcap_grab_px() PER SIDE, THE SAME 10 px ON PURPOSE:
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
    if (!app.region.shown) return RegionHit::None;
    if (audio.total_frames() <= 0) return RegionHit::None;
    // THE `h` VIEW ANSWERS NONE: trim is FROZEN in there — the toggle's chord is
    // off the mode's allowlist and its button greys — and the view's press
    // router forks far above this arm, so a verdict here could only make the
    // cursor and the touch zone promise a gesture that does not run.
    if (app.history_mode.active) return RegionHit::None;
    // Y-GATED TO THE WAVEFORM RECT ALONE (the architect's ruling): the RULER
    // and the MARKER LANE stay plain navigation surface even where the span
    // covers their columns, which is what keeps a pan and a zoom reachable
    // while the overlay covers the waveform entirely.
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

// (THE REGION EDITOR'S COLUMN->FRAME CONVERSION AND ITS MOTION BODY STOOD HERE
// FROM 2026-08-15 TO 2026-08-18 and are DELETED WHOLE, not moved: the overlay's
// move and bound drags are the TRIM BRIDGE and ENDCAP drags now, armed from
// this surface and running trim's own machinery — anchor capture, partner
// clamp, viewport clamp, release column-snap and the shared commit tail — so
// there is no second gesture to keep in step with them. The codex-round-20
// displayed-basis rule they carried lives on in that machinery's own dispatch
// freeze, which names trim_drag: no waveform job may publish a new basis while
// a trim drag is held.)

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
    // AND THE FOLDER OVERLAY TAKES NONE EITHER (2026-08-28): the wheel's
    // context answers 4 over the band under EITHER content — the LIST's
    // scroll, a wheel act with no two-finger meaning — and every other
    // position -1, so this one line is what keeps a pinch over the band from
    // reaching the waveform behind the veil. IT ASKS THE STANDING PREDICATE
    // AND NOT THE PLAYER'S BIT: the question is whether the BAND is there, and
    // the pickers raise the same band under their own mode — asking the
    // player alone let two fingers on the picker's list pan and zoom the
    // waveform behind it (this gesture skips the press road entirely, so no
    // veil refuses it).
    if (folder_overlay_stands(app)) return;
    // AND THE ON-SCREEN KEYBOARD TAKES NONE EITHER (2026-08-29), the overlay
    // clause's twin and the same hole one tenant over: the keyboard paints
    // over the waveform's lower part and `wheel_context` answers 1 there (the
    // band sits inside waveform_area and the wheel carries no keyboard term),
    // so two thumbs landing on keys inside the disambiguation window became a
    // Nav pinch and zoomed the waveform BEHIND the keyboard, about a column
    // the user cannot see. The pan zone already yields under this same rect
    // (touch_point_in_pan_zone's second clause) — this is the two-finger half
    // of that answer, and it asks the same two owners, so the two cannot
    // disagree. A key is not a navigation surface at any finger count.
    if (onscreen_keyboard::stands(app, gui) &&
        rect_contains(onscreen_keyboard::surface_rect(app), f.x, f.y))
        return;
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
    // THE WHEEL'S ROUTING OWNER and the wheel stays LIVE on these lanes: all
    // three of its arms work there and are no part
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
        effective_max_zoom_level(wf_area.w, total, audio.sample_rate());
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
// the region hold reaches anywhere on it, and a motionless tap on the
// lower half is the tap-at-lift burst whose motionless press-release IS the
// deferred scrub act — the mouse's own machinery, inherited with no touch code.
bool GuiInputHandler::touch_point_in_pan_zone(int x, int y) const {
    // THE ZONE YIELDS UNDER A NOTIFICATION CARD (2026-08-29), for the
    // keyboard clause's reason below: the cards stack over the waveform's
    // upper right, the whole waveform is the pan zone, and a finger landing
    // on a card's X inside a pan zone would become the phone-model pan and
    // NEVER DELIVER A PRESS. Answering false lets the finger resolve to the
    // pointer translation and reach the card's own press claim, where the X
    // acts. The card is opaque to the pointer everywhere else too (the
    // cursor map, the hover walk), so the zone agrees with them.
    if (notification_card_at(app, x, y) != 0) return false;
    // AND THE ZONE YIELDS INSIDE THE SHOWN TRIM REGION OVERLAY (2026-08-15).
    // Without this the feature would be unreachable on the exact surface it
    // exists for: the whole waveform is the pan zone, so a finger landing
    // inside the overlay would become the phone-model pan and NEVER DELIVER A
    // PRESS, and the overlay's move / bound drags — the trim bridge and endcap
    // drags since 2026-08-18 — live on the pointer. Answering false here lets
    // the finger resolve to the pointer translation and reach them, which is
    // the whole point of the large surface on glass: the 9 px bar is what a
    // fingertip cannot hit. The flag box carve-out's exact shape, one level up.
    // ONE SPELLING OF "INSIDE THE OVERLAY": this asks the same owner the
    // mouse press claim and the cursor map ask, so the three cannot disagree
    // (that owner is also what keeps the RULER and the MARKER LANE out of it —
    // they answer None there, so an overlay covering their columns still pans).
    // THE REGION HOLD'S ANSWER IS RECORDED AT THE DECLARATION and is accepted:
    // the hold-beat hold is a pan-zone gesture, so a hold INSIDE the shown
    // overlay no longer sweeps a new window — a hold started outside it, or a
    // tap to hide the overlay first, both still do.
    if (region_manipulation_hit(x, y) != RegionHit::None) return false;
    // AND IT YIELDS UNDER THE ON-SCREEN KEYBOARD (2026-08-27), for the overlay
    // clause's reason exactly: the keyboard paints over the waveform's lower
    // part, the whole waveform is the pan zone, and a finger landing on a key
    // inside a pan zone would become the phone-model pan and NEVER DELIVER A
    // PRESS — so every key would need a hold beat to type one character.
    // Answering false lets the finger resolve to the pointer translation and
    // reach the keyboard's own press claim, which is where a key acts.
    // ONE SPELLING OF "ON THE KEYBOARD": this asks the same owner the press
    // claim asks, so the two cannot disagree.
    if (onscreen_keyboard::stands(app, gui) &&
        rect_contains(onscreen_keyboard::surface_rect(app), x, y))
        return false;
    // AND IT YIELDS UNDER THE FOLDER OVERLAY'S BAND (2026-08-28), for the two
    // clauses above's reason exactly and for one more: the panel paints over
    // the waveform's lower part, the whole waveform is the pan zone, and
    // BOTH of the band's gestures live on the POINTER — the row press, whose
    // act is at the lift, and the band's own SCROLL DRAG, which is that same
    // arm past the vertical gate. Left in the zone, a finger crossing the
    // slop on a row would resolve to the phone-model pan and a held one to
    // the region former (which every content refuses), so the list would
    // answer a tap and nothing else. ONE SPELLING OF "ON THE BAND": the same
    // rect the press claim reads.
    if (folder_overlay::stands(app) &&
        rect_contains(folder_overlay::surface_rect(app), x, y))
        return false;
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
// no threshold wait at the begin — the hold beat already disambiguated,
// so the begin runs the former's press half directly — and no second former
// anywhere.

void GuiInputHandler::begin_touch_region(int x, int y) {
    // The press path's own gates, restated because this gesture never passes
    // through on_button_press (the shift former's claim sits below every one
    // of these): a prompt or modal editor owns the input, an open dropdown
    // owns the pointer, unloaded audio has no columns to span, and a live
    // pointer gesture must not be torn by a second writer. THE EDITOR GATE
    // IS THE SIX-EDITOR PREDICATE, the flag editor and the measure editor
    // deliberately included though both are pointer-transparent: every
    // pointer press CLOSES an open flag or measure editor before any claim
    // runs, so no region gesture can begin under one — and this begin, which
    // skips the press path, must not become the first (the declaration
    // carries the full argument). The `h`
    // HISTORY VIEW IS DELIBERATELY NOT REFUSED — unlike the dead trim-move
    // begin's list — because the view ADMITS the region former as its own
    // view-local vocabulary (its shift former), so the begin FORKS on the
    // mode below exactly as the shift press forks at its two claims. A
    // refused begin arms nothing — the update/end hooks then no-op on the
    // drag's own !active guard, so the refused stream is dead rather than a
    // fallback pointer drag (the pan gestures' model).
    if (app.prompt.active) return;
    // THE RENDER PLAYER'S AND THE PICKER'S VEILS (2026-08-28), restated here
    // for the gesture that skips the press path: a held finger on the band or
    // the waveform under either mode begins no sweep.
    if (app.render_player.active || app.picker.active) return;
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
        // the view's own standing rule — AND NO TRIM: the motion path carves
        // this view out of the sweep's trim write, the view promising the trim
        // window is untouched throughout.
        if (clear_history_mode_focus(app.history_mode)) {
            // A discrete command: full-window damage for the face swap, the
            // shift arm's own shape.
            viewport.invalidate_all();
        }
        const int64_t sample = place_playhead_at_click_column(
            x - area.x, playback.is_playing(), app.playhead_cursor_sample);
        if (sample >= 0) arm_region_drag_at(x - area.x, x, y);
        return;
    }
    // THE LIVE FORMER — the shift press's own body whole (deselect-all,
    // playhead at the down column, live-session reseek, the drag arm; the
    // overlay comes up at the stroke's first accepted trim write, not here).
    // The playback readings are taken here at the begin, the
    // formers' press-entry capture (the placement body's contract). THE HOLD IS
    // THE FINGER'S ONLY ROUTE TO A SWEEP — a finger has no modifier — and it
    // survives in full for that reason (architect 2026-08-18).
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
    // drops the staged final frame, its record): a moved sweep runs the shared
    // trim commit tail through the gesture's one end owner; a MOTIONLESS end
    // wrote no trim and leaves the playhead where the begin seated it, which is
    // what makes a long-press-then-lift a placement. The owner is self-guarded
    // like the update.
    commit_region_sweep();
}

// The flag editor's guard-free close — the LEFT press's, its one caller since
// the right button's unbinding (2026-08-12; contract at the declaration,
// input_handler.h). The box is the painter's
// published rect, the same one the F2.1 caret block tests, so "outside" means
// the same thing on every press path.
void GuiInputHandler::close_top_flag_editor_for_outside_press(int x, int y) {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (rect_contains(app.flag_editor_box.box, x, y)) return;
    // AND THE MEASURE PAD COUNTS AS INSIDE — the marker's measure box, painted
    // by the same publisher at the unrolled box's right edge while the payload
    // editor stands (contract at FlagEditorBox::measure_pad, render.h). It is
    // the editor's OWN painted surface, so a press on it is not an outside
    // press. THE CONSUME IS THE CALLER'S, one line past this call: this owner
    // only refuses to close, and on_button_press returns on the same test so
    // the press acts on nothing at all (it is not the FIELD either, so no caret
    // is seated). Empty under the measure editor and when the marker carries no
    // measure, and an empty rect contains no point.
    if (rect_contains(app.flag_editor_box.measure_pad, x, y)) return;
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
            // AND THE LIST'S RING BIT GOES WITH IT (2026-08-29): on the two
            // list-bearing owners the ring's stops are [list, buttons…], and
            // a feint REPLACES whatever focus the dialog had, of EITHER
            // STRENGTH — the ruling's own words. Without this the band kept
            // its accent outline beside the button's new passive face, so the
            // ring looked like it was in two places at once. Paint-only, but
            // the outline is the ring's whole cue.
            if (app.folder_overlay.list_focused) {
                app.folder_overlay.list_focused = false;
                viewport.invalidate_rect(folder_overlay::surface_rect(app));
            }
        }
        if (app.modal_dialog.valid)
            viewport.invalidate_rect(app.modal_dialog.box);
    }
    // THE PLAY-SCRUB'S HANDLE HAS A HOVERED OUTLINE AND NO BIT (2026-08-28):
    // the painter re-answers it every frame from the remembered pointer
    // position against the published item, because the handle MOVES under a
    // stationary pointer while the transport plays and a stored answer would
    // be stale in both directions between motions. What this walk owes is the
    // DAMAGE — one small rect while the player's stash stands, so the frame
    // that re-answers is drawn — and it is unconditional rather than
    // change-driven for the same reason there is no bit: this walk cannot know
    // the answer it would be comparing against without keeping one.
    if (app.render_player.active && app.modal_dialog.valid &&
        app.modal_dialog.scrub.w > 0 && app.modal_dialog.scrub.h > 0) {
        viewport.invalidate_rect(app.modal_dialog.scrub);
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
    app.modal_dialog_press_shift  = false;
    app.modal_dialog_press_ms     = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// THE KEYBOARD ARM'S HARD END, the twin of the one above and on the twin edge:
// the platform's keyboard-intent cancellation (keyboard leave, keyboard-
// capability loss, a Super-swallowed press — the fire classes are at
// set_keyboard_intent_cancel_hook, input_core.h). On the two focus edges
// the release this arm waits for can never be delivered, and an act that has
// not happened yet must not be left waiting for it; on the Super-swallowed
// press the drop is the conservative one — that press is an intervening key
// arrival, the release path itself being ungated on Super. Transition-gated,
// damaging the stashed box when it fires; the contract is at
// AppState::modal_dialog_key_pressed.
void GuiInputHandler::clear_modal_dialog_key_press() {
    if (app.modal_dialog_key_pressed < 0) return;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// A dialog button's PRESS: arm the index and paint it, dispatching nothing.
// Shared by the prompt claim, the player's, the picker's and the editor claim
// — they differ in what their RELEASE runs, not in what their press does.
// Returns true when a button was hit (the claim then consumes the press; the
// veil consumes it either way).
//
// `shift` IS THE PRESS-TIME MODIFIER (R37), and only the player's claim ever
// passes true — every other claim admits a plain press alone. A SHIFT PRESS ON
// A BUTTON WITH NO SHIFTED TWIN IS A CONSUMED NOTHING, never the unshifted
// act: the roster's own rule (arm_redesign_press), and a silent unshifted act
// would be a lie about what the modifier did. THE CLOCK IS STAMPED FOR EVERY
// ARM, a press having a time whatever it landed on; what the lift makes of it
// is modal_dialog_press_shifted's.
bool GuiInputHandler::arm_modal_dialog_press(int x, int y, bool shift) {
    const int hit = modal_dialog_button_hit(x, y);
    if (hit < 0) return false;
    // A DISABLED PLAYER BUTTON'S PRESS IS A CONSUMED NOTHING (architect
    // 2026-08-30: the transport keys are their own class) — the roster's
    // arm_redesign_press rule on this surface: nothing arms, no face paints,
    // no card (the grey is the message). The bit read is the STASH'S, which
    // may only SELECT — a press that slipped through on a stale stash still
    // dies at the dispatch's live re-ask. Every other owner's buttons
    // publish enabled=true, so this line is inert off the player.
    if (!app.modal_dialog.buttons[static_cast<size_t>(hit)].enabled)
        return true;
    if (shift &&
        !player_button_shift_admits(
            app.modal_dialog.buttons[static_cast<size_t>(hit)].player_act))
        return true;
    if (app.modal_dialog_pressed != hit || !app.modal_dialog_press_inside) {
        app.modal_dialog_pressed = hit;
        // A press is inside what it hit, by construction — the feint's bit
        // starts true and only the hover walk can turn it over.
        app.modal_dialog_press_inside = true;
        viewport.invalidate_rect(app.modal_dialog.box);
    }
    app.modal_dialog_press_shift = shift;
    app.modal_dialog_press_ms    = monotonic_ms();
    return true;
}

// THE SHIFTED-TWIN VERDICT for the arm as it stands (R37) — the roster lift's
// own term over this surface: the CARRIED press-time shift ORed with a press
// HELD past kChromeShiftHoldMs, so a physical Shift+click and a long press
// reach the same dispatch and holding a shift-clicked button changes nothing.
// It is measured at the LIFT against the arm's own stamp — no timer, no tick —
// and it asks nothing about WHICH button: the dispatch reads it only where
// player_button_shift_admits says so, which is what lets a button with no twin
// be held for as long as you like and still get its plain act. THE HOLD IS THE
// GLASS HALF of the pair and passes silently, the roster's ruling (tooltips do
// not show on the panel the gesture exists for).
bool GuiInputHandler::modal_dialog_press_shifted() const {
    if (app.modal_dialog_pressed < 0) return false;
    return app.modal_dialog_press_shift ||
           monotonic_ms() - app.modal_dialog_press_ms >= kChromeShiftHoldMs;
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
    // The arm's two shifted-twin fields die with it (the verdict is read
    // BEFORE this call, modal_dialog_press_shifted).
    app.modal_dialog_press_shift  = false;
    app.modal_dialog_press_ms     = 0;
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
    // THE LIVE OWNER'S CLASS, in the painter's own precedence (prompt over
    // player over picker over editor — paint_modal_dialog's fork; the lower
    // three never stand together, so their order is free).
    const AppState::ModalDialogOwner live =
        app.prompt.active         ? AppState::ModalDialogOwner::Prompt
      : app.render_player.active  ? AppState::ModalDialogOwner::Player
      : app.picker.active         ? AppState::ModalDialogOwner::Picker
                                  : AppState::ModalDialogOwner::Editor;
    if (dlg.owner != live) return false;
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
bool GuiInputHandler::dispatch_modal_dialog_button(int index, bool shifted) {
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
    // THE RENDER PLAYER'S BUTTONS (2026-08-28): the act the stash names,
    // decided against the live player — each act's own body re-asks its own
    // state (an item to pause, a neighbour to skip to, a load-capable
    // highlight), so a stale stash can select at worst a consumed nothing.
    if (app.render_player.active) {
        // THE LIVE ENABLED RE-ASK (architect 2026-08-30): a greyed button's
        // act is consumed with no card — the grey is the message — whichever
        // road armed it (the pointer's lift, the ring's Enter/Space
        // release). The KEYS never come through here, so their own cards and
        // R36's ruled silence at Stop's rest are untouched, and the head
        // unit's road (on_media_command -> synthesize_key) arrives as keys
        // and bypasses this line by construction.
        if (!render_player_button_enabled(app, b.player_act)) return true;
        switch (b.player_act) {
            // THE TWO SKIPS ARE THE ROW'S SHIFT-ADMITTING PAIR (R37): their
            // shifted twin — a Shift+click on plastic, a long press on glass,
            // ONE term either way — is the item folder's END rather than its
            // neighbour, the keys' own Shift+Page Up / Shift+Page Down. THE
            // ARMS THAT READ `shifted` ARE THE ADMISSION'S OWN MEMBERS
            // (player_button_shift_admits, app_state.h): every other act below
            // ignores the bit, so a held Play or Close is its plain act — the
            // roster's rule that a button with no twin may be held for as long
            // as you like.
            case AppState::PlayerButtonAct::Previous:
                if (shifted) render_player.first_in_item_folder();
                else         render_player.previous();
                return true;
            case AppState::PlayerButtonAct::PlayPause:
                render_player.play_button_act();
                return true;
            case AppState::PlayerButtonAct::Stop:
                render_player.stop();
                return true;
            case AppState::PlayerButtonAct::Next:
                if (shifted) render_player.last_in_item_folder();
                else         render_player.next();
                return true;
            case AppState::PlayerButtonAct::RepeatOne:
                render_player.toggle_repeat_one();
                return true;
            case AppState::PlayerButtonAct::LoadInPlace:
                render_player_load_in_place();
                return true;
            case AppState::PlayerButtonAct::Close:
                render_player.close();
                return true;
            case AppState::PlayerButtonAct::None:
                return false;
        }
        return false;
    }
    // THE PICKER'S ONE BUTTON (architect 2026-08-29): its row is **Cancel**
    // alone, because a click on a row is the open act now — so this arm reads
    // no bit and runs the one close body. (Enter on the list is still the open
    // act, through picker_open_highlight, the keyboard's own click.)
    if (app.picker.active) {
        close_picker();
        return true;
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
    } else if (text_editor::is_active(app.commit_title_editor)) {
        handle_commit_title_editor_key(key, mods);
    } else if (text_editor::is_active(app.measure_offset_editor)) {
        handle_measure_offset_editor_key(key, mods);
    }
}

// THE TRIM BAR'S DOUBLE-CLICK TEST, hoisted for its SECOND consumer: the live
// band's span framing (in on_button_press below) and the `h` history view's own
// arm over the same band (handle_history_mode_press). Both run the SAME command
// since 2026-08-18 — the view's used to frame the viewed checkpoint's diff span
// instead — so what the hoist still buys is one spelling of the GESTURE: surface
// tag, window and
// slack on both axes, exactly as the seed records them at the motionless
// release. It takes the candidate SNAPSHOT rather than reading app.double_click,
// because the press clears that field at its top: only the snapshot still holds
// the previous click.
static bool trim_bar_double_click_at(const DoubleClickCandidate& dc,
                                     int x, int y) {
    return dc.surface == DoubleClickSurface::TrimBar &&
           monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
           std::abs(x - dc.press_x) <= double_click_slack_px() &&
           std::abs(y - dc.press_y) <= double_click_slack_px();
}

// THE MARKER CLICK ACT — the whole flag click, AT THE PRESS (architect
// 2026-08-17: CONTENT ACTS THE MOMENT ITS IDENTITY IS CERTAIN — a flag press
// can only mean one thing, so nothing here waits for the lift; the one-day
// lift deferral of 2026-08-15 is inverted, its reasoning at
// PendingMarkerPress, app_state.h). ONE owner with TWO call sites, both in
// on_button_press's marker claims: the ctrl-exact toggle branch and the
// plain / shift branch. BOTH ARE FLAG HITS AND NOTHING ELSE (re-grepped
// 2026-08-27): each sits behind a resolved `mh_index >= 0` inside the top
// strip, so the empty marker lane — its plain click, its double-click create —
// never reaches this body, and the `h` view's diff flags are a different press
// router with its own mode-local multi-selection. THE TOGGLE ARM HAS TWO
// PRODUCERS since 2026-08-18: a real ctrl press, and a plain press while the
// ADD TO SELECTION mode stands (the fold, and the shift rule that goes with
// it, are at the `toggle` term below).
// It runs the stop, the three-way selection fork, the
// land, the region hide and — plain only — the double-click consume-open,
// and then ARMS the pending for the two things that genuinely belong to a
// later edge: the reposition DRAG a plain press may become (the crossing
// begins it; the two authoring gates live there) and the double-click SEED
// only a motionless release may write.
//
// THE HIT INDEX IS THE PRESS'S OWN live hit test — the act runs in the same
// event that resolved it, so nothing can be stale. The DOUBLE-CLICK verdict
// reads the press-time candidate SNAPSHOT (dc_at_press), because
// on_button_press's top-of-frame clear has already emptied the shared field.
//
// THE CONSUME PREEMPTS THE DRAG ARM: a consumed open returns before the arm,
// so no drag can begin under the editor it just opened (the editor owns input;
// it is pointer-transparent, and a second press that then moves is the
// editor's problem, not a marker drag). A consumed open seeds nothing either —
// the family rule.
void GuiInputHandler::run_marker_click_act(int hit, int x, int y, bool shift,
                                           bool ctrl,
                                           const DoubleClickCandidate&
                                               dc_at_press) {
    if (hit < 0) return;
    // The stop leads on every shape: selecting or editing under a live
    // audition is the case the top-strip stop exists for, and no arm below
    // refuses (read-only still selects and lands, and the index came from a
    // live hit test).
    playback_lifecycle.stop_playback_if_playing();
    // ADD TO SELECTION IS THE TOGGLE ARM'S SECOND PRODUCER (architect
    // 2026-08-18): while the mode stands, a PLAIN press on a flag is a ctrl
    // press in every respect — same branch, same land, same nothing-armed
    // tail — because the mode's whole definition is "run the ctrl branch".
    // Folding it into the term rather than growing a fourth arm is what makes
    // that true by construction instead of by two bodies agreeing.
    //
    // `&& !shift` IS THE SHIFT RULE AND IT IS LOAD-BEARING (the architect:
    // "Shift+click needs no rule here, because it has its own gesture"). The
    // fork below is `if (toggle) ... else if (shift)`, so a bare fold onto
    // ctrl would let a lit mode SWALLOW a held shift and turn a range select
    // into a membership toggle. A real ctrl+click is unaffected either way —
    // `ctrl` is already true — and ctrl+shift never reaches this owner at all
    // (the ctrl call site is ctrl-EXACT, and the plain/shift one passes
    // ctrl=false), so this term reads on the mode's arm alone.
    const bool toggle = ctrl || (app.add_to_selection && !shift);
    if (toggle) {
        // The individual membership TOGGLE. Whether it ADDED or REMOVED, the
        // playhead lands on the FOCUS the toggle leaves behind (architect
        // 2026-07-28, replacing the earliest-selected land): an ADD focuses the
        // clicked marker, a REMOVE of the focused member repairs the focus to
        // the largest remaining index, and a REMOVE of any other member leaves
        // the focus alone — so app.last_selected_marker is the one expression
        // for all three, and it is always a live member on a non-empty
        // selection.
        selection.toggle_selection_membership(hit);
        if (!app.selected_markers.empty())
            land_playhead_on_marker(app, audio, viewport,
                                    app.last_selected_marker);
    } else if (shift) {
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
        selection.select_range_from_anchor(hit);
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
        selection.set_single_selection(hit);
        land_playhead_on_marker(app, audio, viewport, hit);
    }
    // THE CLICK OWNS ITS HIDE, and this is the RULE'S SECOND CLAUSE rather than
    // a leftover call site (architect 2026-08-19: the overlay hides when the
    // playhead's position in the music changes AND WHEN A MARKER IS TOUCHED —
    // the rule is at clear_region_highlight, input_handler.h). The land the arms
    // above run hides already, but it cannot cover this on its own: a
    // ctrl-toggle that empties the selection lands NOTHING and must still hide,
    // because touching a flag is an act on the timeline whether or not the
    // cursor was already sitting on it. Unconditional, on all three arms. A
    // re-click of the already-selected marker therefore hides a shown overlay
    // too; that is the ruling and not an accident, and it discards nothing —
    // the trim stands and a later `[` re-shows the same overlay.
    clear_region_highlight(app, viewport);
    // THE TWO MODIFIED CLICKS END HERE: neither has a double-click meaning,
    // neither has a drag to become, and their click has just committed whole —
    // so they arm NOTHING, exactly as they did before the one-day lift model
    // (an armed marker pending is plain by construction).
    // THE MODE REACHES THIS RETURN THROUGH `toggle`, and that is INTENDED
    // rather than tolerated (2026-08-18): while Add to Selection stands, a
    // plain press is a ctrl press in every respect, so it must arm neither the
    // reposition DRAG nor the double-click SEED — a marker cannot be dragged
    // and a flag editor cannot be double-clicked open while the mode is
    // accumulating a selection, exactly as neither can be under a held ctrl.
    // Turning the mode off restores both in the same press.
    if (shift || toggle) return;
    // THE PLAIN ARM'S DOUBLE-CLICK CONSUME, AT THIS PRESS (2026-08-17 — the
    // architect on the deferred open: "a tad slow compared to the Enter key"):
    // a candidate for the SAME index within the window opens the flag editor,
    // exactly like Enter on the focused marker (the fork above already
    // single-selected it). THE GATES ARE READ LIVE AT THIS PRESS, which IS
    // live state: read-only and the P view (phase resets have no per-flag
    // editor) refuse SILENTLY — the gesture class's own answer, and the one
    // place this road parts from bare Enter, whose two refusals card since
    // 2026-08-30 (the lock at the key gate, the subject at the arm) — and
    // a refused consume stays a plain second select that seeds afresh at its
    // release. THE OFF-HOME COLUMN NO LONGER REFUSES (architect 2026-08-24):
    // the payload editor edits a marker's VALUES and never its position, so it
    // is a member of the fifth ruled exception to the home-view binding and
    // opens in W+target too (the inventory is at
    // active_column_authoring_allowed, app_state.h).
    // A CONSUMED OPEN ARMS NOTHING AND SEEDS NOTHING: the editor owns input,
    // and the return ahead of the arm below is what makes "nothing arms a
    // marker drag after a consumed open" structural rather than policed.
    //
    // AND THE SPAN DECIDES WHICH EDITOR (2026-08-19). The seed carries which
    // half of the box the FIRST press landed on (MarkerClickSpan), so the two
    // halves of one box open two different editors and a pair straddling the
    // seam opens the one the first click named. The gates differ with them:
    // the PAYLOAD editor keeps read-only and the P view, while the MEASURE
    // editor asks read-only ALONE — measures are the fourth ruled exception to
    // the home-view binding, so the phase column's measure double-click is
    // that column's FIRST pointer authoring gesture, measure-scoped and
    // nothing wider. Since 2026-08-24 the two no longer differ about the AUDIO
    // view either: the payload editor is the fifth exception's member and
    // opens off warp's home as well.
    if (dc_at_press.surface == DoubleClickSurface::Marker &&
        dc_at_press.target == hit &&
        monotonic_ms() - dc_at_press.time_ms <= kDoubleClickMs &&
        std::abs(x - dc_at_press.press_x) <= double_click_slack_px() &&
        std::abs(y - dc_at_press.press_y) <= double_click_slack_px() &&
        !active_view_state(app).read_only) {
        if (dc_at_press.span == MarkerClickSpan::Measure) {
            // Every open route opens fully SELECTED (open-selected); the seed
            // is the marker's own measure, which is the whole of what a
            // measure is — nothing inherits.
            flag_editor.enter_measure_edit(app.active_markers_view, hit);
            return;
        }
        if (app.active_markers_view != 'P') {
            // Every open route opens fully SELECTED (open-selected), so there
            // is no clicked-glyph caret to seat. A specific caret spot is a
            // click inside the already-open editor (the F2.1 path).
            flag_editor.enter_top_flag_edit(hit);
            return;
        }
    }
    // ARM THE PENDING — the drag the plain press may become (the crossing
    // begins it, the two authoring gates there) and the SEED its motionless
    // release owes. UNCONDITIONAL: even a locked tab or an off-home column
    // arms, because the release still seeds — only the DRAG is gated, at the
    // crossing (a locked tab still selects and lands; read-only protects the
    // authored musical content, and a selection is navigation).
    app.pending_marker_press = PendingMarkerPress{};
    app.pending_marker_press.active  = true;
    app.pending_marker_press.marker  = hit;
    app.pending_marker_press.press_x = x;
    app.pending_marker_press.press_y = y;
    // WHICH HALF OF THE BOX THIS PRESS LANDED ON, stamped now from the
    // painter's published boundary and carried to the seed at the motionless
    // release. Only the double-click reads it; the drag this may become is the
    // same gesture from either half.
    app.pending_marker_press.span = hit_test_flag_span(app, audio, x, y);
}

// ARM THE ONE SURVIVING DEFERRED CLICK — the trim bar's ctrl (BEGIN) /
// ctrl+shift (END) bound set (the contract, and why it alone still defers, is
// at PendingClickAct, app_state.h: its press IS the endcap drag's arm, the one
// click with genuine press ambiguity — the 2026-08-17 ruling took the other
// four kinds back to the press). It writes the pending and does NOTHING ELSE:
// the act is the lift's, or the crossing's.
//
// THE ARM IS UNCONDITIONAL BY SHAPE. Every gate the set meets — the
// strictly-inside refusal, the degenerate-geometry returns — lives INSIDE the
// act and is therefore re-asked LIVE at the lift, which is the chrome lift's
// own rule: a gate may change under a held button and the LIFT decides.
// Nothing is carried but the press POINT and which bound the click writes.
void GuiInputHandler::arm_pending_click_act(int x, int y, bool is_begin) {
    app.pending_click = PendingClickAct{};
    app.pending_click.kind     = PendingClickKind::TrimBoundSet;
    app.pending_click.press_x  = x;
    app.pending_click.press_y  = y;
    app.pending_click.is_begin = is_begin;
}

// RUN THE ARMED ACT — the motionless lift's whole body. Its ONE caller is
// on_button_release (the THRESHOLD CROSSING runs its act inline instead,
// because there it is the drag's own prologue rather than a click — the fork
// is stated at that site).
//
// THE ACT RUNS ON THE ARMED SUBJECT — the PRESS COLUMN — and never on a re-hit
// at the release's coordinates (touch's down-point press, and the press point
// is what the user aimed at, sub-threshold travel being jitter).
//
// THE PENDING IS TAKEN BY VALUE so the caller can follow the release bodies'
// STANDING SHAPE — read the fields, DISARM, then act, so the act runs with no
// gesture live. The set writes viewport, trim and playhead state that other
// code reads through the live-gesture predicates, which is exactly what that
// shape exists for.
void GuiInputHandler::run_pending_click_act(PendingClickAct press) {
    if (press.kind != PendingClickKind::TrimBoundSet) return;
    // The ctrl (BEGIN) / ctrl+shift (END) bound set, WHOLE AND
    // UNSPLIT: set_trim_bound_at_click owns every refusal (a degenerate
    // audio/geometry state, and above all the STRICTLY-INSIDE guard —
    // a click landing a bound on or past its partner writes nothing,
    // deselects nothing and stops nothing), the playback stop that sits
    // past those refusals, the write, the commit tail (the crossed
    // reset, the playhead park at the new trim start, the repaint and the
    // target trigger — but NO overlay hide, trim writes being that
    // inventory's one excluded class since 2026-08-18) and the setter's
    // deselect.
    // Moving the act meant moving that unit, never a piece of it.
    set_trim_bound_at_click(press.is_begin, press.press_x);
}

// -- THE ON-SCREEN KEYBOARD'S TWO EDGES (2026-08-27) -------------------------
//
// Contracts, the placement rule and the act-at-the-press ruling are at the
// declarations (input_handler.h); the layout table, the geometry walk and the
// two lamps are at onscreen_keyboard.h. What is here is the press's own
// dispatch and the release's owed key-up.

// THERE IS NO CURSOR ZONE FOR THIS SURFACE, and that is a decision rather than
// an omission (pointer-hit-testing.md's zone map is derived from the press
// routers, so a new press zone would ordinarily earn one). The map's whole job
// is that THE CURSOR PROMISES THE GESTURE — and this surface exists only where
// wants_onscreen_keyboard() is true, which is only on glass, where there is no
// cursor to promise with; on the one platform that HAS a cursor the surface can
// never stand, so an arm there would answer for a rect that is permanently empty. An
// arm with no producer is residue, so there is none: `pointer_cursor_kind` is
// untouched by this feature.
bool GuiInputHandler::claim_onscreen_keyboard_press(GuiMouseButton button,
                                                    int x, int y) {
    if (!onscreen_keyboard::stands(app, gui)) return false;
    const GuiRect surf = onscreen_keyboard::surface_rect(app);
    if (!rect_contains(surf, x, y)) return false;

    // CONSUMED FROM HERE, whatever it lands on and whichever button it was —
    // the surface is opaque to the pointer, and a non-left press has no meaning
    // on a key (the right button is unbound product-wide).
    if (button != GuiMouseButton::Left) return true;

    // THE SESSION-CHANGE OWNER RUNS FIRST, ahead of the hit test and ahead of
    // every read below (the contract is at its declaration): a close and a
    // reopen, or a flag-editor RETARGET, can both complete inside one drained
    // input batch with no tick between them, and this press must not be routed
    // against lamps the previous edit armed. It damages the band on a real
    // change and does nothing at all otherwise.
    onscreen_keyboard::reconcile_session(app, gui, viewport);

    // The lamps, read ONCE: the layer decides which key is under the finger and
    // the shift arm decides what that key types, so both must be the same
    // answer the last paint used.
    const AppState::OnscreenKeyboard& kb = app.onscreen_keyboard;
    const bool symbol_layer = kb.symbol_layer;
    const bool shift_armed  = kb.shift_armed;

    onscreen_keyboard::KeyDef def{};
    const int hit = onscreen_keyboard::key_at(app, symbol_layer, x, y, def);
    if (hit < 0) return true;   // a gap, the margin: consumed, no key

    using Role = onscreen_keyboard::Role;

    // THE TWO LAMP KEYS ACT ON THE SURFACE AND SYNTHESIZE NOTHING. They still
    // take the held index (so the finger sees the click face) with a keysym of
    // 0, which is what the release reads as "this key owed no key-up".
    if (def.role == Role::Shift || def.role == Role::LayerToggle) {
        app.onscreen_keyboard.pressed_key    = hit;
        app.onscreen_keyboard.pressed_keysym = 0;
        if (def.role == Role::Shift) {
            // ONE-SHOT, and a second tap while armed clears it — a toggle, not
            // a latch (there is no caps lock). Inside one edit the arm has
            // exactly two clearers — this tap and the letter it capitalizes,
            // below; the edit ENDING is the third, and belongs to the
            // session-change owner (onscreen_keyboard.h).
            app.onscreen_keyboard.shift_armed = !shift_armed;
        } else {
            // THE LAYER TOGGLE LEAVES A PENDING CAPITAL STANDING, like every
            // other key that types no letter (the rule is stated whole at the
            // ordinary key's spend, below). It costs nothing to keep: the only
            // thing an arm can ever change is a LETTER, and every letter is on
            // the page this key came from, so a round trip to the symbols and
            // back finds the arm exactly where it was left — with the lamp lit
            // again the moment the shift key is painted again.
            app.onscreen_keyboard.symbol_layer = !symbol_layer;
        }
        // BOTH KEYS REPAINT THE WHOLE SURFACE: shift moves every letter cap's
        // case and the layer moves every key on three rows.
        viewport.invalidate_rect(surf);
        return true;
    }

    // AN ORDINARY KEY. Resolve what it types from the layout table's own two
    // derivations — never a second list — and hand it to the platform's key
    // door with the key's PLACE as the core's stable per-key identity.
    GuiKey   keysym      = 0;
    uint32_t codepoint   = 0;
    // Did this key SPEND the arm? Only a letter can (see the spend below).
    bool     capitalized = false;
    switch (def.role) {
        case Role::Character: {
            const char typed = onscreen_keyboard::shifted_char(def.ch,
                                                               shift_armed);
            capitalized = (typed != def.ch);
            // The keysym is the LOWERCASE base (GuiKey is ASCII case-folded);
            // the CASE travels in the codepoint, which is what the editors'
            // printable classification reads. The reasoning is at keysym_of.
            keysym    = onscreen_keyboard::keysym_of(def.ch);
            codepoint = static_cast<uint32_t>(
                static_cast<unsigned char>(typed));
            break;
        }
        case Role::Backspace: keysym = GuiKeys::BackSpace; break;
        case Role::Enter:     keysym = GuiKeys::Return;    break;
        case Role::Escape:    keysym = GuiKeys::Escape;    break;
        // A bare Tab, no modifier: the product's prompts complete on it (the
        // one autocomplete model, route_modal_editor_key) and the ring walks
        // on it where there is nothing to complete.
        case Role::Tab:       keysym = GuiKeys::Tab;       break;
        case Role::Shift:
        case Role::LayerToggle: return true;   // handled above; unreachable
    }

    app.onscreen_keyboard.pressed_key    = hit;
    app.onscreen_keyboard.pressed_keysym = keysym;
    // The click face, before the act: the act can close the editor and take the
    // surface with it, and a damage queued after that would name a rect nothing
    // paints. One key's rect — the discrete-command rule applies to the SHOW
    // and HIDE of the whole surface (the tick comparator's, main.cpp), not to a
    // key changing face.
    viewport.invalidate_rect(
        onscreen_keyboard::key_rect(app, symbol_layer, hit));

    // A SHIFT ARM IS SPENT BY THE LETTER IT CAPITALIZED AND BY NOTHING ELSE.
    // ONE-SHOT SHIFT MEANS THE NEXT LETTER (planner ruling 2026-08-27), so a
    // key that types no capital leaves the arm standing: the comma and the
    // period, the space bar, backspace, Enter, Esc, the layer toggle and every
    // digit and symbol on the other page. Shift, comma, `q` types `,Q`. The
    // test is the layout table's own case derivation moving this key's
    // character (`capitalized`, set at the Character arm above) rather than a
    // list of exempt roles — a list would be a second statement of which keys
    // have a capital form, and shifted_char is already the one.
    //
    // THE REPEAT KEEPS THE CASE IT WAS PRESSED WITH, and that is the ruling
    // rather than a leak: the PRESS'S CODEPOINT IS THE KEY EVENT'S IDENTITY, so
    // a held `q` pressed with the arm up repeats `Q` for the whole hold,
    // exactly as a physical Shift+Q hold repeats Q with the shift key still
    // down. The arm clears at the PRESS, not at the repeats — the lamp is dark
    // from that first press onward — and the backend's codepoint table
    // (platform_android.cpp's synthesize_key) is what re-answers each
    // synthesized repeat, deliberately not re-derived against the live lamp.
    //
    // The whole surface repaints because every other letter cap drops back to
    // lowercase with it. Done BEFORE the act for the reason the damage above
    // is: after Enter there may be no surface left to talk about.
    if (capitalized) {
        app.onscreen_keyboard.shift_armed = false;
        viewport.invalidate_rect(surf);
    }

    // THE ACT, at the press. Everything downstream is the ordinary key path.
    gui.synthesize_key(keysym, static_cast<uint32_t>(hit), /*pressed=*/true,
                       codepoint);
    return true;
}

bool GuiInputHandler::finish_onscreen_keyboard_release() {
    const int held = app.onscreen_keyboard.pressed_key;
    if (held < 0) return false;
    const GuiKey keysym = app.onscreen_keyboard.pressed_keysym;
    app.onscreen_keyboard.pressed_key    = -1;
    app.onscreen_keyboard.pressed_keysym = 0;

    // Un-press the face, on the layer the INDEX names rather than the live one:
    // the layer toggle's own press moved the live layer, and its key would be
    // looked up on a page it is not on (the reason is at layer_of_key_index).
    // The surface may be GONE altogether (the press's own Enter or Esc closed
    // the editor), in which case the rect is empty and this is a no-op — the
    // show/hide damage is the tick comparator's, not this path's.
    if (onscreen_keyboard::stands(app, gui)) {
        viewport.invalidate_rect(onscreen_keyboard::key_rect(
            app, onscreen_keyboard::layer_of_key_index(held), held));
    }

    // The owed key-up. A lamp key synthesized nothing and owes nothing; every
    // other key owes exactly this, and the core's repeat cancel is the reason
    // it may not be skipped (contract at GuiInputCore::key_event: the arm dies
    // on the matching stable code and on nothing else this path can reach).
    // A release carries no character, exactly as a physical one does not.
    if (keysym != 0) {
        gui.synthesize_key(keysym, static_cast<uint32_t>(held),
                           /*pressed=*/false, /*codepoint=*/0);
    }
    return true;
}

// -- THE FOLDER OVERLAY'S POINTER HALF (2026-08-28) ---------------------------
//
// The panel's geometry and its one row walk are folder_overlay.h's; the row
// table and the press arm are AppState::folder_overlay's, where every field
// is described. THE ROWS ARE CHROME (conventions.md's third clause): the
// press ARMS because the same press may still become the band's scroll drag,
// so its identity is not certain at the press; THE MOTIONLESS LIFT HIGHLIGHTS
// THE ROW AND OPENS IT — a click activates (architect 2026-08-29), which is
// why the panel has no double-click surface any more: the act is at the one
// edge that can tell a click from a drag, and a second press has nothing
// left to mean.
//
// ONE PRESS ROUTER, TWO CONTENTS. The claim below stands ABOVE the two
// mode veils (the player's and the picker's), each of which admits exactly
// the band and its own modal row, and BELOW the prompt gate, which outranks
// every surface as it always has. What the OPEN means is the owner's, and the
// two forks below are the whole of it: nothing else in this
// file asks which content fills the rows.

// THE MOTIONLESS LIFT'S FIRST HALF: the band moves, under every owner — the
// picker has no field beside it (architect R22), so the band IS what Enter
// opens.
void GuiInputHandler::folder_overlay_highlight_row(int index) {
    switch (app.folder_overlay.owner) {
        case AppState::FolderOverlay::Owner::None:
            return;
        case AppState::FolderOverlay::Owner::Player:
            render_player.set_highlight(index);
            return;
        case AppState::FolderOverlay::Owner::ProjectPicker:
            picker_set_highlight(index);
            return;
    }
}

// THE OPEN ACT (the row click's own second half and Enter on the highlight).
// Under the player a folder enters, the up row goes to the parent and a wav
// plays; under the project picker the row's project is reopened — one body
// per content, which the click and Enter both reach.
void GuiInputHandler::folder_overlay_open_row(int index) {
    switch (app.folder_overlay.owner) {
        case AppState::FolderOverlay::Owner::None:
            return;
        case AppState::FolderOverlay::Owner::Player:
            render_player.open_row(index);
            return;
        case AppState::FolderOverlay::Owner::ProjectPicker:
            open_project_commit(index);
            return;
    }
}

bool GuiInputHandler::claim_folder_overlay_press(
        int x, int y, GuiMouseButton button, GuiInputState mods) {
    if (!folder_overlay::stands(app)) return false;
    if (button != GuiMouseButton::Left) return false;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (!rect_contains(surf, x, y)) return false;
    // CONSUMED FROM HERE: the band is opaque to the pointer. A MODIFIED
    // press is a consumed no-op — nothing on a row reads shift or ctrl, and
    // nothing on a row reads kHoldBeatMs either: the load is the modal row's
    // own button (strict modifier validation's answer, and the roster's
    // shift-hold is elsewhere). A press on the pad or a gap between rows arms
    // nothing.
    //
    // A MODIFIED PRESS ON A ROW SAYS SO (architect 2026-08-30): the ctrl-click
    // and the shift-click a file manager teaches are exactly what a list
    // invites, and the answer is the rule — a row opens on a plain click, one
    // sentence for both contents, since the act is the same act under either
    // owner. THE ROW TEST COMES FIRST so the pad and the gaps stay SILENT: a
    // modified press on empty band is not a press on a row, and it is answered
    // by the band doing nothing, exactly as a plain one there is.
    const int hit = folder_overlay::row_at(app, x, y);
    if (mods.ctrl || mods.shift || mods.alt) {
        if (hit >= 0)
            notifications.notify(AppState::NotificationClass::Normal,
                                 "Rows open on a plain click");
        return true;
    }
    if (hit < 0) return true;
    // THE PRESS ONLY ARMS — a click activates, but the act rides the
    // MOTIONLESS LIFT because this same press may still become the band's
    // scroll drag (architect 2026-08-29; the row press is chrome, and past the
    // drag gate it IS the drag).
    AppState::FolderOverlayPress& press = app.folder_overlay.press;
    press.armed           = true;
    press.row             = hit;
    press.press_x         = x;
    press.press_y         = y;
    press.scroll_at_press = app.folder_overlay.scroll_px;
    press.inside          = true;
    press.scrolling       = false;
    viewport.invalidate_rect(folder_overlay::row_rect(app, hit));
    return true;
}

void GuiInputHandler::update_folder_overlay_press_motion(int x, int y) {
    AppState::FolderOverlayPress& press = app.folder_overlay.press;
    if (!press.armed) return;
    if (!press.scrolling) {
        // THE DRAG GATE, the product's one crossing threshold, spelled here
        // exactly as at every other pending press: CHEBYSHEV from the press
        // (max(|dx|,|dy|)) against drag_moved_threshold_px() — 8 authored px
        // through scaled_px, the touch slop's own number — and the crossing is
        // `>=`, not `>`, because the core resolves a touch into a drag at `>=`
        // its slop and the two gates must not disagree by one pixel (the
        // twin-gate invariant, input_core.h: a slop-crossing resolution
        // delivers its crossing motion in the same burst as the press, and
        // that motion has to clear this gate BY CONSTRUCTION).
        //
        // IT CROSSES ON EITHER AXIS even though the scroll reads dy alone. A
        // sideways slide over a row is a drag, not a click: on glass a finger
        // that has slid 20 px across a full-width row has been classified as a
        // drag by the touch layer already, and on plastic a wobbling click is
        // the same event. Past the gate the arm IS the band's scroll drag and
        // its act is gone — once a drag, always a drag, on both axes — so a
        // purely sideways crossing simply scrolls nothing and ends with no act,
        // which is the answer Plasma's single-click and GNOME's touch both give
        // (the ruling's own references, R39).
        if (std::max(std::abs(x - press.press_x),
                     std::abs(y - press.press_y)) >=
                drag_moved_threshold_px()) {
            press.scrolling = true;
            press.inside    = false;
            viewport.invalidate_rect(folder_overlay::surface_rect(app));
        } else {
            const bool inside = folder_overlay::row_at(app, x, y) == press.row;
            if (inside != press.inside) {
                press.inside = inside;
                viewport.invalidate_rect(
                    folder_overlay::row_rect(app, press.row));
            }
            return;
        }
    }
    // THE SCROLL DRAG: the content follows the finger — a pointer moving
    // down pulls earlier rows into view — measured from the press, so the
    // offset cannot accumulate drift across the events.
    const int before = app.folder_overlay.scroll_px;
    app.folder_overlay.scroll_px = press.scroll_at_press - (y - press.press_y);
    folder_overlay::clamp_scroll(app);
    if (app.folder_overlay.scroll_px != before)
        viewport.invalidate_rect(folder_overlay::surface_rect(app));
}

bool GuiInputHandler::finish_folder_overlay_release(int x, int y) {
    AppState::FolderOverlayPress press = app.folder_overlay.press;
    if (!press.armed) return false;
    app.folder_overlay.press = AppState::FolderOverlayPress{};
    if (press.row >= 0)
        viewport.invalidate_rect(folder_overlay::row_rect(app, press.row));
    // A scroll drag ends here with nothing else owed.
    if (press.scrolling) return true;
    // A MOTIONLESS LIFT ON THE ARMED ROW HIGHLIGHTS IT AND THEN OPENS IT — A
    // CLICK ACTIVATES (architect 2026-08-29: in the player a wav plays from
    // its start, a folder is entered and `..` goes up; in the Open project
    // picker the row's project opens, refusals and all, exactly as Enter's
    // does). The band moves first, so what the act runs on is what the user
    // can see it named. A lift elsewhere is a consumed nothing.
    // NOTHING IS SEEDED HERE ANY MORE: the row's double-click surface went
    // with the ruling, no second meaning being left for a second press.
    if (folder_overlay::row_at(app, x, y) != press.row) return true;
    folder_overlay_highlight_row(press.row);
    folder_overlay_open_row(press.row);
    return true;
}

void GuiInputHandler::update_folder_overlay_hover(int x, int y) {
    if (!folder_overlay::stands(app)) return;
    // A NOTIFICATION CARD IS OPAQUE TO THE POINTER (notifications.h), the
    // roster walk's own term one surface over: the stack grows DOWN from row
    // 1 and the band's ceiling is the waveform's midpoint, so on a short
    // window at a large scale the two do overlap, and a row under a card must
    // neither light nor promise the press the card's claim will consume.
    const int hit = notification_card_at(app, x, y) != 0
                        ? -1
                        : folder_overlay::row_at(app, x, y);
    if (hit == app.folder_overlay.hovered_row) return;
    const int old = app.folder_overlay.hovered_row;
    app.folder_overlay.hovered_row = hit;
    if (old >= 0) viewport.invalidate_rect(folder_overlay::row_rect(app, old));
    if (hit >= 0) viewport.invalidate_rect(folder_overlay::row_rect(app, hit));
}

void GuiInputHandler::clear_folder_overlay_hover() {
    const int old = app.folder_overlay.hovered_row;
    if (old < 0) return;
    app.folder_overlay.hovered_row = -1;
    if (folder_overlay::stands(app))
        viewport.invalidate_rect(folder_overlay::row_rect(app, old));
}

// -- THE NOTIFICATION CARDS' POINTER HALF (2026-08-29) -------------------------
//
// The rule and its reasons are at notifications.h and at the claim's site in
// on_button_press. The three bodies here are thin: geometry is the painter's
// publication, the act is GuiNotifications' own.

bool GuiInputHandler::claim_notification_press(GuiMouseButton button, int x,
                                               int y) {
    // The hit owner has already asked the LIVE stack (notifications.h): a
    // published rect whose card has LEFT that stack — by its expiry, by its X
    // or by the bump — answers 0 here (a live card that is merely clipped is
    // selected wherever it has a published rect, and past the room's foot it
    // has none), so this press is claimed by NOTHING and falls through to
    // the surface underneath — the pixels the user is about to see there at
    // the next paint, which is the honest place for it to land.
    const uint64_t id = notification_card_at(app, x, y);
    if (id == 0) return false;
    // Consumed from here, whatever the button: the card is opaque.
    if (button != GuiMouseButton::Left) return true;
    // THE X, AND ONLY THE X, DISMISSES (architect 2026-08-29); a press on the
    // body is the consumed nothing above. dismiss re-asks the live stack, so
    // a stale published rect lands nothing.
    if (notification_close_at(app, id, x, y)) notifications.dismiss(id);
    return true;
}

void GuiInputHandler::update_notification_hover(int x, int y) {
    notifications.update_hover(x, y);
}

void GuiInputHandler::clear_notification_hover() {
    notifications.clear_hover();
}

void GuiInputHandler::clear_folder_overlay_press() {
    AppState::FolderOverlayPress& press = app.folder_overlay.press;
    if (!press.armed) return;
    const int row = press.row;
    press = AppState::FolderOverlayPress{};
    if (folder_overlay::stands(app) && row >= 0)
        viewport.invalidate_rect(folder_overlay::row_rect(app, row));
}

// -- THE PLAY-SCRUB'S POINTER HALF (2026-08-28) --------------------------------
//
// Over the painter's published track (AppState::ModalDialogGeometry::scrub,
// the whole SLIDER ITEM — zero under every owner but the player and read only
// through a stash that names the live player session). The mapping between a
// column and a frame is the one the painter's handle uses
// (render_player_scrub_x_of / _frame_at, app_state.h), which owns the handle
// box's inset at both ends. A press on the HANDLE'S OWN BOX — its 20 px, the
// one grab band, through the one test both this router and the painter's
// hovered outline ask (render_player_scrub_handle_hit; it took over from the
// trim endcaps' 10 px band on 2026-08-28, when the scrub became a Breeze
// slider and grew a handle with a size of its own) — arms the marker drag: the
// handle's painted x follows the pointer while the sound continues where it
// was, and the RELEASE commits the seek, the product's deferred-click shape
// (the same press could have been a tap on the track under the handle, whose
// meaning is the seek at the press; on the handle the identity is not certain
// until the lift). A press on the track ELSEWHERE seeks at the press — its
// identity is certain — and arms nothing.

// THE DRAG'S CARRIED COLUMN, clamped onto the HANDLE'S TRAVEL — the one
// expression its two writers (the press's arm, the motion) share, so the
// painted handle sits inside its own track at every point of a drag and the
// release's seek reads the same span the mapping does.
int GuiInputHandler::clamp_player_scrub_marker_x(int x) const {
    const GuiRect track = app.modal_dialog.scrub;
    const int x0   = track.x + scrub_handle_box_px() / 2;
    const int span = render_player_scrub_usable_span(track);
    if (span <= 0) return x0;
    return x < x0 ? x0 : (x > x0 + span ? x0 + span : x);
}

bool GuiInputHandler::claim_player_scrub_press(int x, int y,
                                               GuiInputState mods) {
    if (!app.render_player.active) return false;
    if (!modal_dialog_stash_current()) return false;
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w <= 0 || track.h <= 0) return false;
    if (!rect_contains(track, x, y)) return false;
    // Consumed from here; a modified press does nothing on the track. That
    // one stays SILENT (a modified press on the band's rows is silent too —
    // the overlay's ruled pad-and-gap silence, messaging.md): the answer is
    // that the plain press works, and a chord on a slider is not an act
    // anyone spelled.
    if (mods.ctrl || mods.shift || mods.alt) return true;
    // THE TWO STATE REFUSALS SAY WHAT THE ACTS SAY (architect 2026-08-30):
    // they are the seek's own two, met here instead of at seek_to because the
    // press must not ARM the handle drag either — so the words are the mode's
    // shared ones (render_player.h) rather than a second wording of one fact.
    if (app.render_player.frames <= 0 || app.render_player.item.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kNoPlayerItem);
        return true;
    }
    // THE SCRUB RESTS WHILE THE TRANSPORT IS IDLE (architect 2026-08-29,
    // Audacious's own slider — dead while stopped): the press seeks nothing
    // and arms no handle drag, and the painter keeps drawing the handle at
    // the resting point. LIVE and PAUSED are unchanged.
    if (app.render_player.transport ==
        AppState::RenderPlayer::Transport::Idle) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kSeekWhileIdle);
        return true;
    }
    const int marker_x =
        render_player_scrub_x_of(app, render_player_position(app, playback));
    if (render_player_scrub_handle_hit(track, marker_x, x, y)) {
        app.render_player.scrub.armed    = true;
        // THE CARRIED x IS A HANDLE CENTRE, so it is clamped onto the handle's
        // OWN TRAVEL and not onto the item — the painter draws the circle at
        // it, and a centre past either inset would hang the handle off its
        // track (the travel is the mapping's, one owner:
        // render_player_scrub_usable_span).
        app.render_player.scrub.marker_x = clamp_player_scrub_marker_x(x);
        viewport.invalidate_rect(track);
        return true;
    }
    render_player.seek_to(render_player_scrub_frame_at(app, x));
    return true;
}

void GuiInputHandler::update_player_scrub_motion(int x) {
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return;
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w <= 0) return;
    const int mx = clamp_player_scrub_marker_x(x);
    if (mx == drag.marker_x) return;
    drag.marker_x = mx;
    viewport.invalidate_rect(track);
}

bool GuiInputHandler::finish_player_scrub_release(int x, int y) {
    (void)y;
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return false;
    drag = AppState::RenderPlayer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
    // THE COMMIT: the seek to the column the lift is at (clamped onto the
    // track by the mapping), whatever the drag's own last x said.
    render_player.seek_to(render_player_scrub_frame_at(app, x));
    return true;
}

void GuiInputHandler::clear_player_scrub_drag() {
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return;
    drag = AppState::RenderPlayer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
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
    // nothing; MARKER joined 2026-08-15 — and KEPT the release-time seed when
    // its click went back to the press, 2026-08-17: only the release knows the
    // press stayed still, whatever the click's own timing).
    // THE MARKER SEED IS DELIBERATELY SPLIT ACROSS THE TWO EDGES, which is
    // unusual enough to state here: the press consumes against this snapshot
    // (it is gone by the release), while the seed written
    // at the lift carries the PRESS coordinates with the RELEASE timestamp — the
    // position looking back so the spatial pairing stays press-to-press, the
    // stamp being the seed's own so the window is measured release-to-press as it
    // is for the other three. The full reasoning is at the seed itself
    // (on_button_release's marker-pending arm). One closed instrumentation
    // point — the clear covers
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

    // THE NOTIFICATION CARDS, ABOVE EVERY GATE AND EVERY VEIL (architect
    // 2026-08-29): a press on a published card is consumed whole — arms
    // nothing, moves nothing, lands no playhead, opens no drag, reaches
    // nothing underneath — and the LEFT press on the card's X box dismisses
    // it. THE X, AND ONLY THE X: the router cannot fork on tap versus click
    // (no origin bit rides a press; GuiInputState carries modifiers alone),
    // so the rule is one for both hosts, and the X's box is the icon row's
    // 32 px button box, already the product's glass target, which is why no
    // finger-fattened body target exists. Any other button over a card is
    // consumed in the veil's own manner. It ranks above the veils because a
    // card is the message ABOUT the act a veil stands over, not a reach into
    // the veiled surface (the record at the retired reach-through's site
    // above). The act is the PRESS'S — content acts the moment its identity
    // is certain — so the release owes nothing and no arm is left standing:
    // this return precedes every arm below, and on_button_release's claims
    // (the keyboard's key-up, the dropdown's, the chrome arm, the modal's,
    // the overlay's) each test their own armed state, none of which a card
    // press set. ON GLASS the one-finger translation delivers this press only
    // once the disambiguation window has resolved to the pointer (a tap, or
    // an off-zone hold or crossing — input_core.cpp), which is what keeps a
    // two-finger landing off it; the pan zone answers false on a card
    // (touch_point_in_pan_zone), so a finger landing on one resolves to this
    // press rather than to the phone-model pan. The geometry read here is the
    // last paint's publication, and dismiss asks the live stack whether the
    // id still stands — a card that left between paint and press lands
    // nothing.
    if (claim_notification_press(button, x, y)) return;

    // THE ON-SCREEN KEYBOARD, ABOVE EVERY GATE (2026-08-27). While it stands
    // its rect belongs to no other surface, so there is nothing below to
    // arbitrate with — and it MUST outrank the dialog editors' veil further
    // down, which would otherwise swallow the press that types into the very
    // editor raising it. It stands only where a platform asks for one
    // (false forever on the laptop, at GuiPlatform::wants_onscreen_keyboard),
    // so this is one platform query and one integer compare everywhere else.
    // Its rect is opaque: a press inside it never reaches the waveform's
    // gestures underneath, key or gap. (Contract at the declaration; the act
    // runs at the PRESS, inside.)
    if (claim_onscreen_keyboard_press(button, x, y)) return;

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

    // THE FOLDER OVERLAY'S BAND, claimed for EVERY content and ranked here —
    // under the prompt gate above (a prompt outranks every surface; the
    // player's load confirmation and the reopen's unsaved-tab question paint
    // over their own rows) and above the two mode veils below, the player's
    // and the picker's, each of which admits the band and its own modal row
    // and consumes the rest. The claim is opaque and owns its own button
    // gate, so a non-left press on the band falls to whichever veil stands
    // and is consumed there.
    if (claim_folder_overlay_press(x, y, button, mods)) return;

    // THE RENDER PLAYER'S VEIL (2026-08-28), under the prompt gate — its load
    // confirmation is a prompt and paints over it — and above everything
    // else: while the mode stands the pointer has THREE targets, the folder
    // overlay's rows (claimed above), the play-scrub (a seek at the press
    // on the track, the marker drag on its band) and the modal row's buttons
    // (the arm every dialog button takes), and EVERY OTHER PRESS IS CONSUMED
    // — the tabs, the flags, the waveform, the menu anchors, the dead roster.
    // The whole rule is stated at render_player_active (input_handler.h).
    // THE ONE ROW THAT ADMITS SHIFT (2026-08-28, R37): the player's two skips
    // carry a shifted twin — the item folder's ends — so a SHIFT press reaches
    // the arm here and the lift dispatches that twin, the roster's own
    // shift-click over this surface. The arm body applies the admission (a
    // shift press on a button with no twin is a consumed nothing, never the
    // plain act); ctrl and alt spell nothing on any modal row and stay
    // consumed by the veil below.
    if (app.render_player.active) {
        if (button != GuiMouseButton::Left) return;
        if (claim_player_scrub_press(x, y, mods)) return;
        if (!mods.ctrl && !mods.alt && modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y, mods.shift);
        }
        return;
    }

    // THE PICKER'S VEIL (2026-08-28), the player's shape one mode over: while
    // a picker stands the pointer has TWO targets, the overlay's rows
    // (claimed above) and the modal row's one Cancel button (the arm every
    // dialog button takes), and EVERY OTHER PRESS IS CONSUMED. A ROW CLICK IS
    // THE OPEN ACT, which is why the row carries no OK beside that Cancel. There is no field
    // and so no caret claim and no text drag — the picker has nothing to
    // type into. The whole rule is stated at picker_active (input_handler.h).
    if (app.picker.active) {
        if (button != GuiMouseButton::Left) return;
        if (!mods.ctrl && !mods.shift && !mods.alt &&
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
    // THAT ITSELF now: all five editor dialogs publish real OK and CANCEL
    // buttons, the claim below admits a press on them, and Cancel dispatches
    // the session's own Esc. With Quit's button gone to the File menu the
    // membership had already derived down to Save, and a convenience chord is
    // not worth an exception to the veil. SO THE VEIL HAS NO EXCEPTION: while
    // a dialog editor stands every press outside the modal is consumed — the
    // notification cards' claim at the head of this function is hit ABOVE
    // the veil by ruling and is not one (a card is the message about the
    // veiled act, not a reach into the surface). The
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
                // THE MARKER LANE'S FIELD: the editable text lives IN THE
                // PUBLISHED BOX — the unrolled flag under the payload editor,
                // the blue measure box under the measure editor, one rect from
                // one painter either way.
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
                // SHIFT+CLICK EXTENDS THE SELECTION (architect 2026-08-30) —
                // the pointer's FIRST shift binding on an editor field, and
                // exactly what Shift+Left/Right and Ctrl+Shift+Left/Right do
                // from the keyboard: the standing anchor is kept (or the old
                // caret becomes one, the keyboard's own rule for a shifted
                // motion off a bare caret) and the CLICKED byte becomes the
                // cursor. A plain click still places the caret and drops the
                // selection, below.
                // SHIFT-EXACT, so Ctrl+click and Ctrl+Shift+click stay the
                // plain caret placement they have always been here — this
                // claim carries no modifier gate of its own and never did,
                // and nothing above it consumes a modified press over the
                // FIELD (the dialog buttons' claim is plain-exact, but its
                // rect is disjoint from the field's).
                // IT IS AHEAD OF THE DOUBLE-CLICK TEST because a shifted
                // second press is a shift-click, not a word select; and the
                // arm below is the plain press's own, so a drag that follows
                // keeps extending from the SAME anchor (the motion moves
                // `cursor_pos` alone). The `shift_extend` bit rides the arm
                // for the release's seed alone (EditorTextDragState).
                // PLASTIC ONLY, recorded rather than fixed: the touch
                // translation carries `current_mods()` like every other
                // delivery (touch.md), and the on-screen keyboard's Shift is
                // ONE-SHOT FOR THE NEXT CHARACTER KEY — it produces a capital
                // codepoint through shifted_char and sets no modifier bit — so
                // a tablet with no physical shift key has no road onto this
                // act, and none is built. Glass extends a selection by
                // dragging the field, which it already could.
                if (mods.shift && !mods.ctrl && !mods.alt) {
                    if (g.ed->selection_anchor < 0)
                        g.ed->selection_anchor = g.ed->cursor_pos;
                    g.ed->cursor_pos = editor_byte_index_at(g, x);
                    app.editor_text_drag.active       = true;
                    app.editor_text_drag.shift_extend = true;
                    if (g.dialog) viewport.invalidate_modal_dialog_area();
                    else          viewport.invalidate_top_strip();
                    return;
                }
                // Double-click: a second click within the window on this
                // editor's text selects the RUN of the clicked character class
                // (word / punctuation / whitespace) under the click — select_
                // word_at's own classifier, not just a word — arming no drag.
                // The surface tag keeps it from consuming a marker / trim-bar
                // candidate.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::EditorText &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= double_click_slack_px() &&
                    std::abs(y - dc.press_y) <= double_click_slack_px()) {
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
                app.editor_text_drag.active       = true;
                app.editor_text_drag.shift_extend = false;
                if (g.dialog) viewport.invalidate_modal_dialog_area();
                else          viewport.invalidate_top_strip();
                return;
            }
            // A dialog editor stays modal — THE VEIL: a press outside the
            // box's field and buttons is CONSUMED, closing nothing (the
            // architect's words: "once I've done that pop-up modal, I can't
            // do anything else in the window behind it"; the dialog closes
            // only by its own buttons and keys). THE VEIL HAS NO EXCEPTION
            // AGAIN since 2026-08-28: the folder overlay's band was its one
            // admitted surface beyond the box for the afternoon the Open
            // project prompt kept a field under the picker, and the picker
            // is a mode of its own now, with a veil of its own above. A
            // flag-editor press that isn't on the lane text falls through to
            // the guard-free close below.
            if (g.dialog) return;
        }
    }

    // THE VEIL'S LAST WORD: any DIALOG editor still standing here swallows the
    // press. It asks the membership's own predicate rather than spelling the
    // four surfaces again — the set is NAMED once (modal_dialog_editor_active
    // over AppState::dialog_editor_session, whose declaration says so) so it
    // cannot drift, and a fifth dialog editor would be veiled here by existing
    // rather than by an edit. What the swallow buys is the same for all four,
    // and the BPM editor is the case that names it: mouse input does not
    // interact with a dialog beyond its own field and buttons, claimed above,
    // and the session ends only through Esc / the Enter dispatch path / the
    // dialog's Cancel and OK (`m` is just a typed character now) — so the
    // press must not drive a region drag or a marker click, nor tear the
    // editor down through the top-strip flag-edit routine below.
    if (modal_dialog_editor_active()) return;
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
    // every press outside the box's own field and buttons dies at the veil,
    // which has had NO EXCEPTION since 2026-08-13 (the retired reach-through's
    // record is at its own site above) — so the menu anchors are never
    // reached and no press can open a popup under an editor.
    // The other half is not here — the
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
                // (A DISABLED ITEM ARMED NOTHING AND DISMISSED NOTHING,
                // 2026-08-08 to 2026-08-15: the press was consumed where it
                // landed and the MENU STAYED UP, kdenlive's own answer for a
                // greyed row — pressing one is a nothing, not a dismissal — so
                // that arm RETURNED rather than falling into the close below,
                // which is the answer for the separator, the chrome and the
                // box's outside: those are the popup's DEAD SPACE, and a greyed
                // item is a row that is simply not for you. The predicate was
                // the painter's own, so the grey face and the inert press were
                // one fact read twice, the roster's disabled-press rule one
                // surface out. It went producer-less with the Navigation menu
                // — no surviving item can grey — and the arm is deleted with
                // it; the record is at kFilePopupItems, app_state.h.)
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
    // ONE BAND-CLAIM SHAPE FOR ALL FOUR ROWS: the exact half-open row band, the
    // modifier gate above (ALT refused outright, CTRL only on the buttons the
    // roster admits it on, CTRL+SHIFT never), a SHIFT press binding only where
    // the roster admits one, and any press in the band that is not on a button
    // a consumed nothing. Each band differs ONLY in its rect
    // and (row 1) in the dropdown toggle of its TWO non-chord buttons, File
    // and Settings, so the press is ONE arm body, arm_redesign_press, driven
    // by the table's per-button flags — and the act one release body,
    // finish_chrome_press_release, in on_button_release.
    //
    // A BUTTON's rect is the painter's stash (app.redesign_buttons, published by
    // paint_menu_row / paint_tab_row / paint_icon_row /
    // paint_bottom_row_buttons_and_clock; every roster member publishes a real
    // rect on every frame since 2026-08-18, the bottom row's cluster swap —
    // whose unpainted four stashed a zero rect that contains no point, and
    // which was the product's last hiding after the icon row's collapse rule
    // was deleted on 2026-08-14 — having gone with the history companions'
    // return to row 4; a MODAL's yield is the one place the shape survives) —
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
            if (chrome_band_modifiers_refused(app, x, y, mods)) return;
            if (button == GuiMouseButton::Left) {
                // FILE, EDIT AND SETTINGS ARE THE ROSTER'S THREE
                // NON-CHORD BUTTONS, so they are spelled here rather than in the
                // table: each action is a POPUP TOGGLE, which no keyboard chord
                // performs. Their menus lead to routes the keyboard already has
                // — the bare `;` still opens the settings editor DIRECTLY,
                // File's one item is Ctrl+Q, and Edit's five are the propagate
                // chords — so a
                // dropdown is a pointer affordance for an existing road, never a
                // second one. (A THIRD anchor, Navigation, was spelled here from
                // 2026-08-02 until 2026-08-15, and its deletion is what makes
                // that principle load-bearing rather than decorative: every one
                // of its items was a key you could press instead, and once every
                // one of those keys had a BUTTON too the menu was a third road
                // to the same place and went. THE EDIT MENU OF 2026-08-20 IS
                // THAT RUN IN REVERSE and satisfies the same doctrine: its five
                // propagate rows would have been a second road beside IconCopy
                // and IconPaste, so those two BUTTONS were deleted with the
                // menu's arrival rather than left standing beside it — the
                // architect choosing which road survives, never keeping both.)
                // Shift-exact is refused like every
                // other non-admitting button.
                //
                // THE ANCHORS ARE WALKED rather than spelled one by one — the
                // same shape on_motion's two anchor walks take, over the one
                // menu list (app_state.h), so
                // dropdown_anchor_button stays the one place that knows which
                // button emits which menu — and the walk is what gives the CLAIM
                // below exactly ONE site instead of one per branch. It is also
                // what made File a one-row addition here and the Navigation
                // menu's deletion a one-row removal there: the walk's length
                // moved twice and this body did not change either time.
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
            if (chrome_band_modifiers_refused(app, x, y, mods)) return;
            // THE ROW CARRIES NO TEXT BUT ITS TABS since 2026-08-29, when
            // the STATUS CHAIN that had painted here from 2026-08-13 was
            // deleted for the status bar (whose own state text folded onto
            // row 8 that evening). It was pointer-inert the whole time
            // — it published no rect, so a press over its text was the band's
            // own consumed nothing — and the empty stretch it left answers the
            // same way the tail past the last tab always did.
            // THE TABS ARE THIS ROW'S ONLY TARGETS
            // since 2026-08-14: the active tab's padlock was a second one
            // until the read-only toggle moved into the icon row.
            //
            // THE ROW IS THE A/B TABS IN EVERY STATE SINCE 2026-08-18, the
            // `h` history view included (architect: "ctrl+tab should work as
            // normal in history view"). It was that view's WALK SELECTOR from
            // 2026-08-05 — a claim right here routed every press in the band to
            // set_history_reading, each slot naming its own walk, arming a
            // HistoryWalkTab press whose lift selected rather than dispatching
            // a chord — because the tabs' own Ctrl+Tab had become the mode's
            // walk cycle and would have stepped past whichever slot was
            // clicked. THE WALK HAS ITS OWN RADIO PAIR IN THE ICON ROW NOW
            // (bare `g`), so the chord means what it says again and this row
            // needs no mode branch at all: the claim, the arm kind and the
            // switch owner's pointer call site are deleted together.
            //
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
            if (chrome_band_modifiers_refused(app, x, y, mods)) return;
            if (button == GuiMouseButton::Left) arm_redesign_press(x, y, mods);
            return;
        }
    }
    // THE UNIFIED BOTTOM ROW (row 8's claim since 2026-08-11; the whole
    // merged lane since the 2026-08-12 unification), the block's fifth member
    // on the block's own terms: the band is the bottom strip's ONE lane, on
    // the window's foot, and everything else is the shape above —
    // below the modal gates (a prompt or a dialog editor swallows the
    // press; the pointer-transparent flag editor does not, and its KEYBOARD
    // modality then answers the dispatched chord exactly as it answers the
    // key), above the loading/empty guard, ctrl/alt strict no-ops, and every
    // press in the band that is not on a button a consumed nothing — which
    // since the unification includes the clock cell and the bare ground
    // beside it, the lane's pointer-inert span (the status chain that shared
    // that ground until 2026-08-13 took no clicks either, and took none away
    // with it) — and the STATE CELL right of the clock since 2026-08-29 is that
    // same inert ground, a text cell with no rect of its own to test. THE LANE
    // RESTS ON THE WINDOW'S FOOT (from the relayout's commit B, apart from the
    // one day a STATUS BAR stood under it), so there is nothing below it at
    // all, and ABOVE it lies GAP 2's blank window ground — outside every band
    // here and falling through to the tail's consumed nothing, as window ground
    // by the vertical rule (main.cpp).
    // (The OVERVIEW STRIP sat under this lane for the afternoon it landed and
    // is a top-strip lane now; its own claim is further down, past the gesture
    // guards — the endcap / teleport-pan / ctrl-zoom vocabulary.)
    {
        const GuiRect bottom_row = bottom_row_area(app);
        if (rect_contains(bottom_row, x, y)) {
            if (chrome_band_modifiers_refused(app, x, y, mods)) return;
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
    //   * PLAIN left press OUTSIDE the box TELEPORTS AT THE PRESS AND THEN
    //     ARMS THE SAME BOX PAN (2026-08-18 — "overview teleport should
    //     transition into drag immediately if finger/pointer drags"): the
    //     teleport centers the box on the press column, and the pointer that
    //     keeps moving keeps panning from there, so the outside press falls
    //     THROUGH into the inside-box arm below rather than seating a second
    //     copy of it. A motionless release is then the pan's own consumed
    //     nothing. (The act still runs AT THE PRESS — content acts the moment
    //     its identity is certain, 2026-08-17 — because acting and arming a
    //     drag are not the deferred-click case; the reasoning is at the
    //     contract, OverviewDragState.)
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
            // THE TELEPORT, AT THIS PRESS, AND THEN THE FALL-THROUGH: an
            // outside press centers the box on the press column — the point
            // the user aimed at — and then arms the pan below on exactly the
            // terms an inside press does, so a pointer or finger that keeps
            // moving keeps panning. No `return` and no second seat: after the
            // teleport the press column IS inside the box, which is what makes
            // the one arm below correct for both entries.
            if (!inside_box) run_overview_teleport(x);
            app.overview_drag = OverviewDragState{};
            app.overview_drag.active  = true;
            app.overview_drag.kind    = OverviewDragKind::Pan;
            app.overview_drag.press_x = x;
            app.overview_drag.press_y = y;
            // The grab-point offset: pointer's whole-song position minus
            // the viewport center, both in the active domain, so the
            // grabbed spot under the box stays under the pointer. READ AFTER
            // THE TELEPORT, deliberately: for an outside press the teleport
            // has already moved the viewport, so this measures the box where
            // it now IS. The result is near zero by construction (the teleport
            // centered on this very column) but NOT exactly zero — the
            // centering rounds the position, halves an integer span and then
            // takes clamp_viewport_start's grid snap and wall clamp, so what
            // rests here is that residue, and at a WALL, where the teleport
            // saturated, it is the whole leftover. Measuring it instead of
            // assuming zero is what keeps the first motion event from jumping
            // the box.
            const double pos =
                overview_anchor_sample_at_x(app, audio, x);
            const double center =
                static_cast<double>(app.viewport_start_sample) +
                static_cast<double>(samples_visible(app, audio)) / 2.0;
            app.overview_drag.grab_offset = pos - center;
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
    // coverage true. ONE PRESS ROUTE IN THESE ROWS DISPATCHES NO CHORD
    // (re-derived 2026-08-06, again 2026-08-15 when the Navigation anchor left,
    // again 2026-08-18 when the walk selector did, and again 2026-08-20 when
    // the EDIT anchor arrived, and again 2026-08-27 with SERIES): the four menu
    // anchors,
    // which have none and are
    // shut at toggle_dropdown instead. (The A/B TAB PAIR was a second WHILE
    // THIS MODE STOOD, from 2026-08-05 to 2026-08-18: the tab row's band claim
    // intercepted it and armed set_history_reading at the lift, the walk
    // selector being deliberately not a chord. The walk has its own radio pair
    // in the icon row now, so the tabs dispatch Ctrl+Tab in here like
    // everywhere else.) That exception is a refusal decided ABOVE this gate, so
    // it leaves the mode uncovered nowhere.
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
    // the press acts and arms unconditionally (its click is navigation and the
    // release still owes the seed), so the gate
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
        // is unconditional. A DISMISSAL is press-time by standing rule
        // product-wide — the menu row's any-press end, press-anywhere-closes,
        // this close, the veil.
        // Consequence: a double-click on the open editor's own marker is
        // close-then-reopen — the first press closes, selects and arms, its
        // motionless LIFT seeds a Marker candidate, and the second press
        // consumes into a fresh open at that press (2026-08-17). That IS the
        // documented "double-click opens the
        // editor"; there is no own-marker special case.
        //
        // THE MEASURE PAD IS THE ONE PRESS THIS OWNER LEAVES STANDING AND THE
        // ONE THIS FRAME CONSUMES OUTRIGHT: the marker's measure box, painted
        // by the editor's own publisher at the unrolled box's right edge while
        // the payload editor stands (FlagEditorBox::measure_pad, render.h). It
        // is the editor's own painted surface, so it is not an "outside" press
        // and must not close the session — and it is not the FIELD either, so
        // it must not seat a caret. A press there therefore does NOTHING and
        // ends here, rather than falling through to arm a nav press on lane
        // pixels the editor is currently occupying. Empty under the measure
        // editor and on a measureless marker, and an empty rect contains no
        // point.
        const bool on_measure_pad =
            text_editor::is_active(app.top_flag_editor) &&
            rect_contains(app.flag_editor_box.measure_pad, x, y);
        close_top_flag_editor_for_outside_press(x, y);
        if (on_measure_pad) return;

        // The marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item and that item is now its FLAG BOX alone
        // (hit_test_flag against the painter's stash — the rendered lane run
        // that used to be its second half died with the marker-text lane, and
        // with it the MarkerHit pair and its shared resolver marker_hit_at).
        // The TOP-STRIP hit feeds the plain/Shift/Ctrl marker-press branches,
        // all three of which run the CLICK ACT AT THE PRESS (2026-08-17 —
        // content acts the moment its identity is certain; the acts are at
        // run_marker_click_act: plain = single-select + land + the double-click
        // consume-open, Shift = the file-manager inclusive RANGE select from
        // the interaction's anchor to the clicked marker + land on that range
        // END, Ctrl = the individual membership toggle + land on the resulting
        // focus), so it is resolved once here — every one of the three lands on
        // its own focus.
        // The WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + the pending click; lower:
        // the scanner scrub, which touches no selection at all), and a Shift
        // press SWEEPS a trim window waveform-wide (from the playhead, or a marker
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
        // waveform is purely trim / playhead / pan / zoom. (Modified
        // presses never resolved a stem anyway — the 2026-08-01 plain-exact
        // gate, made universal by the 2026-08-06 symmetry ruling — so this
        // ruling only moved the PLAIN stem click.) The stems still PAINT
        // exactly as before: class-colored, always on, disabled-no-stem.
        // The plain DRAG never selects markers either
        // (SELECTION FLOWS DOWNWARD ONLY, architect 2026-07-23 — the region no
        // longer selects its contents; it leaves the selection empty).
        // Trim bounds are grabbed by their top-strip endcaps /
        // the inter-endcap bridge on a PLAIN trim-bar press (route_trim_bar_press
        // below) AND, since 2026-08-18, by the shown TRIM REGION OVERLAY'S own
        // bounds and interior on the waveform, which arm those very drags
        // (region_manipulation_hit, far below in the waveform block); a click
        // over a bound's waveform stem is still an ordinary waveform click (the
        // stem grab retired). Resolved ONCE here, ahead of every branch that
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
        // THE MARKER'S STOP LEADS ITS CLICK ACT, which runs AT THE PRESS again
        // since 2026-08-17 (run_marker_click_act — the one-day lift model of
        // 2026-08-15 is inverted), so a marker press stops on the way down as
        // it always had before that day.
        // THE STOP IS INTENTIONAL, NOT POSITIONAL: a press that claims
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

        // (NO ALT ARM: alt binds no PRESS anywhere — the grab-pan it carried
        // until 2026-08-12 is the PLAIN drag on the navigation surface now, and
        // its one surviving pointer form is the ALT+WHEEL stepped pan, which
        // came back to the modifier on 2026-08-27 when the plain wheel became
        // the waveform magnification step. An alt-exact press falls to the
        // strict-modifier discard below, a consumed no-op like every other
        // unbound combination; on the keyboard alt survives only inside the
        // five Ctrl+Alt chords.)

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
            // selection always carries a focus after either arm). THE TRIM
            // REGION OVERLAY HIDES, unconditionally and whatever the land did
            // or did not move — turning to marker work is turning to other work
            // — and the hide discards nothing, the trim standing untouched
            // behind it. There is no result-size split here any more (the >=2
            // arm's extent write died with the SPAN FORM, architect
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
                // THE PRESS ACTS (2026-08-17): the toggle, its stop, its land
                // and its region hide are the CLICK, run here through the one
                // act owner (run_marker_click_act). A ctrl click has no
                // gesture to become and no double-click meaning, so nothing is
                // armed — the act's own modified-shape return.
                run_marker_click_act(mh_index, x, y, /*shift=*/false,
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
                    // THE PRESS ONLY ARMS — THE ONE SURVIVING DEFERRED CLICK
                    // (2026-08-17; contract at PendingClickAct, app_state.h —
                    // this press IS the endcap drag's arm, the genuine press
                    // ambiguity the deferral exists for): the BEGIN bound set,
                    // its refusals, its stop, its commit tail and its deselect
                    // are the CLICK, and the click runs at the MOTIONLESS LIFT
                    // through the one act owner (run_pending_click_act), at
                    // the PRESS column. A
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
                    // NO OVERLAY RAISE HERE: the band's press claims stopped
                    // showing the waveform overlay on 2026-08-20, and the
                    // family's record sits at the plain trim-bar press below.
                    arm_pending_click_act(x, y, /*is_begin=*/true);
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
                // then hands over to that bound's endcap drag (the one
                // surviving deferred click, 2026-08-17).
                // NO stop here either: like the BEGIN set, the stop sits
                // inside set_trim_bound_at_click past that act's refusals, so a
                // refused END set leaves a live audition alone.
                // AND NO OVERLAY RAISE HERE EITHER, the band's rule again (the
                // record is at the plain trim-bar press below).
                arm_pending_click_act(x, y, /*is_begin=*/false);
                return;
            }
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's endcap/bridge drags on the plain trim-bar press, so
        // every remaining modified combination — ALT-exact (alt spells no
        // PRESS anywhere; its one pointer form is the wheel), Ctrl+Alt,
        // Ctrl+Shift off the
        // trim bar (its one claim is the END bound set above), Shift+Alt,
        // Ctrl+Alt+Shift, ... — no-ops here. Only a plain or Shift base press
        // proceeds. ALT survives ONLY in the FIVE keyboard Ctrl+Alt
        // render / propagate chords (Ctrl+Alt+R, Ctrl+Alt+Shift+R,
        // Ctrl+Alt+P, Ctrl+Alt+Shift+P and, since 2026-08-20, the measure
        // propagate's paste Ctrl+Alt+/) — every other alt keybinding was retired
        // 2026-07-28, and both of its pointer forms moved onto the PLAIN forms
        // with the eighth glass ruling; the alt+wheel STEPPED PAN came back to
        // the modifier on 2026-08-27, and it is a wheel and not a press, so no
        // press path anywhere defers to alt.
        // Discarding a press here is TOTAL: it claimed
        // nothing, so it stopped no playback on the way down either — the stops
        // live at the claims above and below, never on the route to this gate.
        //
        // AND IT SAYS SO (architect 2026-08-30, the strictness ruling): the
        // card names the MODIFIER COMBINATION rather than a chord, because on
        // this surface that is the whole of what was pressed — the same
        // spelling owner the keyboard's cards use, minus the key
        // (spell_modifiers, gui_input.h). NO BOUND GESTURE PASSES THIS LINE,
        // re-verified at this edit: the ctrl-EXACT branch above returns on
        // every one of its paths (the marker toggle, the BEGIN bound set, the
        // nav zoom arm, its own fall-through), Ctrl+Shift's one claim — the
        // END bound set — returns inside the trim band above, and ALT SPELLS
        // NO PRESS ANYWHERE. So what reaches here is Ctrl+Shift off the trim
        // bar and every alt-carrying press, and nothing else. The reach is the
        // waveform and the top strip alone (the lane test above returns for
        // everything else) under a LEFT button (this whole block's gate), with
        // every chrome, modal and overlay surface claimed far above.
        if (ctrl || alt) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 spell_modifiers(mods) +
                                     "+click is not bound here");
            return;
        }

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
        // click: single-select and LAND the playhead on the marker, both AT
        // THE PRESS (2026-08-17 — content acts the moment its identity is
        // certain), the press also arming the pending that becomes the
        // reposition drag past the threshold. Shift+click: a
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
        // seat the playhead at the clicked column, hide the overlay,
        // arm the drag — the drag then writes the trim with the playhead
        // riding the moving endpoint, landing where the mouse releases; a
        // motionless shift click lands the playhead and writes no trim. The
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
            // double-click AT THE PRESS (2026-08-17) and then falls through to
            // the same cap/bridge arm every plain press takes, else — on an
            // unclaimed spot — is a CONSUMED NOTHING; the bound-set clicks
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
                // THE BAND RAISES NO OVERLAY, and this is the family's one
                // record (architect 2026-08-20, partly reversing his own
                // 2026-08-19 "touching the trim shows the trim"): "touching the
                // tiny lane means I'm on the laptop, and the region exists
                // mostly for the touchscreen." A 9 px band under a POINTER is
                // already its own display of the trim window, and the big
                // waveform surface exists for glass, where that lane is
                // unusable. So all three of the band's press claims — this
                // plain one and the two Ctrl / Ctrl+Shift bound sets above —
                // left show_trim_region_overlay's call-site inventory that day,
                // leaving THE SWEEP its one caller — at the sweep's first
                // accepted trim write since 2026-08-21 (input_handler.h).
                // Bare `[` and its Show trim region button still raise the
                // overlay on demand, which is how a laptop press gets the big
                // surface when it wants one.
                // Plain trim-bar press. An endcap/bridge hit ARMS the trim drag
                // and commits nothing at the press: only the threshold crossing
                // begins it, and a MOTIONLESS release here runs NO act at all —
                // this surface leaves PendingTrimDrag::waveform_click_act false,
                // which is exactly the consumed nothing stated in the paragraph
                // below (the waveform overlay's arms are the ones that set it
                // and fall to the ordinary click act). Armed IN EITHER TAB since
                // 2026-08-07 — the band's read-only return is deleted below. Either
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
                // (and, from there, `c`); this frames a proper trim sub-window,
                // else the whole song (the region arm above those two died with
                // the separate region state on 2026-08-18 — the overlay and the
                // trim window are one span now).
                //
                // THE CONSUME ACTS AT THIS PRESS (2026-08-17, reverting the
                // one-day verdict-before-arm deferral of 2026-08-15: a
                // recognized second press acts immediately on every
                // double-click surface — "if I'm double clicking specifically
                // to do the double click action, I would never double click
                // into a drag"). A consumed press FRAMES here and then FALLS
                // THROUGH to the band's ordinary cap/bridge arm below, seeding
                // nothing (a consumed press never seeds — the family rule, so
                // the double-click cadence stays second-press-only).
                //
                // THE ACCEPTED COST, recorded because trim has no undo: a
                // second press that then crosses into a cap/bridge drag
                // proceeds FROM THE FRAMED VIEW — the framing changed the
                // viewport under the held button, and the drag that follows
                // moves a bound with nothing to take it back. The architect's
                // ruling is that the drift-into-drag double-click is a
                // nonexistent use case; the frame-then-drag, where it happens,
                // is the user's own two-act gesture.
                const bool framed = trim_bar_double_click_at(dc_at_press, x, y);
                if (framed) run_span_framing_command();
                // SEEDING is a RELEASE act (only the release knows the press
                // stayed still), so the press records its point and the release
                // decides — see TrimBarPressSeed. A consumed press records no
                // seed (above); every other plain band press does.
                if (!framed) {
                    app.trim_bar_press = TrimBarPressSeed{
                        .active = true, .press_x = x, .press_y = y};
                }
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
                // THE MARKER CLICK ACTS AT THE PRESS (architect 2026-08-17:
                // CONTENT ACTS THE MOMENT ITS IDENTITY IS CERTAIN — a flag
                // press can only mean one thing, so the one-day lift deferral
                // of 2026-08-15 is inverted; the double-click open in
                // particular was "a tad slow compared to the Enter key", and
                // the deferral's only defense there — a double-click's second
                // press becoming a drag — is a nonexistent use case). ONE act
                // owner, run_marker_click_act: the stop, the three-way fork,
                // the land, the region hide and the plain consume-open, which
                // then arms the pending that becomes the reposition drag past
                // the threshold. The two AUTHORING gates (read-only, home
                // view) guard the DRAG and live at the crossing, never here: a
                // locked tab and an off-home column still select and still
                // land. The contract is at PendingMarkerPress (app_state.h).
                run_marker_click_act(mh_index, x, y, shift, /*ctrl=*/false,
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
                    // serves both routes). THE CREATE RUNS AT THIS SECOND
                    // PRESS (2026-08-17, with the whole double-click family —
                    // a recognized second press acts immediately; the one-day
                    // lift deferral of 2026-08-15 is deleted): the press
                    // spends itself on the create and arms NO pending, so it
                    // can never become a pan and motion after it is DEAD — the
                    // create is undoable, and undo is its recovery.
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
                        std::abs(x - dc.press_x) <= double_click_slack_px() &&
                        std::abs(y - dc.press_y) <= double_click_slack_px()) {
                        create_marker_at_empty_lane(x - area.x);
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
        // the placement also DESELECTS, HIDES the trim region overlay and
        // overrides follow for the session, while the scrub act touches no
        // selection, no region, no cursor and no follow state at all (that is
        // what makes it the overlay's PREVIEW gesture — click inside the span
        // to audition it and the overlay stays up).
        //
        // WHAT THE LOWER HALF GAINS BY BEING A PENDING: for the press's whole
        // life it is a live pointer gesture like the upper half's — the wheel
        // and every chord are swallowed, the follow chase is paused, and the
        // cursor holds the uniform Arrow — which is exactly the symmetry the
        // ruling asked for and not a new rule of its own.
        //
        // BUT A SHOWN TRIM REGION OVERLAY IS ASKED FIRST (2026-08-15 for the
        // claim; since 2026-08-18 the overlay IS the trim and its drags are
        // trim's own — the model is at RegionState, app_state.h). A hit arms
        // the TRIM bound / bridge drag INSTEAD of the nav drag; None falls
        // straight through and nothing about the pan changed. THE WAVEFORM
        // ALONE ASKS — the ruler and the marker lane never reach this arm and
        // stay plain navigation surface, which is what keeps a pan reachable
        // while the overlay covers the waveform entirely.
        //
        // SHIFT AND CTRL BYPASSED IT ENTIRELY (both were claimed far above) and
        // keep their meanings: ctrl always zooms, and SHIFT ALWAYS SWEEPS a new
        // trim window even inside the overlay — the architect's reason being
        // that a shift press is still a press, so "shift always draws" costs
        // nothing and gives a way to re-sweep without hiding first.
        //
        // NOTHING IS COMMITTED AT THE PRESS, exactly as the nav press commits
        // nothing: a MOTIONLESS press-release on the overlay is NOT a
        // manipulation but the waveform's ORDINARY CLICK ACT, run by the
        // release arm (which states which half hides and which does not). That
        // is the escape hatch a full-window overlay needs — it covers both
        // halves, so the upper half's click is always reachable — and the
        // architect ruled the fall-through deliberately.
        {
            const RegionHit rh = region_manipulation_hit(x, y);
            if (rh != RegionHit::None) {
                // THE OVERLAY'S THREE MOTIONS ARE TRIM'S OWN TWO DRAGS
                // (2026-08-18): a BOUND is the single-bound endcap drag on
                // that bound — BoundLo is the trim BEGIN and BoundHi the END,
                // a resting pair being ordered by construction — and the
                // INSIDE is the BRIDGE (pair) drag. Nothing is re-derived
                // here: this arms the SAME pending an endcap or bridge press
                // on the 9 px bar arms, so the threshold crossing,
                // begin_trim_drag's anchor capture, the partner clamp, the
                // first-accepted-change deselect and stop, the release
                // column-snap and the shared commit tail are all the drag's
                // own rules, unchanged and unforked.
                //
                // waveform_click_act MARKS THE SURFACE: a motionless lift on
                // the bar commits nothing (the consumed nothing of
                // 2026-07-30), but here it falls to the waveform's ORDINARY
                // CLICK ACT — which HIDES the overlay and places the playhead
                // — and that is what keeps a full-window overlay dismissable
                // by clicking, the architect's own escape hatch kept through
                // the model change.
                arm_pending_trim_drag(rh != RegionHit::BoundHi,
                                      /*both=*/rh == RegionHit::Move, x, y,
                                      /*waveform_click_act=*/true);
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
        // matching the existing click-to-caret. IT COVERS THE SHIFT+CLICK'S
        // OWN degenerate case too (architect 2026-08-30): an extend that
        // landed on the caret's own byte has nothing to add — has_selection
        // is anchor != cursor by definition — so it collapses to a caret here
        // like every other empty span.
        if (g.ed->selection_anchor == g.ed->cursor_pos)
            g.ed->selection_anchor = -1;
        if (g.dialog) viewport.invalidate_modal_dialog_area();
        else          viewport.invalidate_top_strip();
    }
    app.editor_text_drag = EditorTextDragState{};
}

void GuiInputHandler::arm_region_drag_at(int anchor_col, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    // THE ANCHOR IS AUTHORED HERE, at the one arm, from the press COLUMN: the
    // whole source frame through the sweep's one column->trim route, beside
    // the active-domain frame the caller's placement just seated the playhead
    // at (which nothing on the trim side reads — the two-values-per-column
    // rule at the field's declaration).
    app.region_drag.anchor_source_frame = sweep_trim_frame_at_column(anchor_col);
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
    // THIS ARM RAISES NOTHING (architect 2026-08-21, on his first drive of the
    // press-time raise: "on shift+click on empty waveform, current region is
    // highlighted ... the first part is incorrect"). The overlay is DERIVED from
    // the RESTING trim, so a raise at the press could only ever show the OLD
    // window for the moments before the stroke wrote its own — the region the
    // user is about to replace, flashed up as if it were the one he is drawing.
    // The raise moved to the sweep's FIRST ACCEPTED TRIM WRITE
    // (apply_region_drag_motion), where the span on screen is the stroke's own
    // from its very first frame.
    //
    // THE SUCCESSION, in three days: the arm HID here until 2026-08-19 on the
    // letter of the point-command rule (a press deselects and seats the
    // playhead, and point commands hide — clear_region_highlight,
    // input_handler.h), which left the sweep writing the trim under a surface it
    // had just put away; the raise then came here, at the press, for every trim
    // touch; it narrowed to the sweep alone on 2026-08-20 (the trim bar's three
    // band presses leaving the inventory — the record is at the plain trim-bar
    // press, on_button_press); and it moved off the press entirely on
    // 2026-08-21. What survives whole is the principle: THE SURFACE YOU ARE
    // DRAWING ON SHOULD BE VISIBLE WHILE YOU DRAW IT, and nothing before that.
    //
    // THE ORDER STILL TELLS THE STORY, and it is why this gesture needs no
    // suppression anywhere: the live former's press SEATS the playhead
    // (place_playhead_and_arm_region -> place_playhead_at_click_column ->
    // move_playhead_to), a real movement that HIDES through the movement owner —
    // and now nothing re-raises until the stroke's first accepted write. A
    // motionless shift click therefore shows nothing at all. The sweep's
    // per-motion trim writes and its release park never pass an owner — they
    // write the cursor direct — so nothing WITHIN the stroke puts the overlay
    // back down; the stroke's END does, by commit_region_sweep's own ruling.
    //
    // (The one-day RULER arm's deferred dissolve — RegionDragState::ruler and
    // the motion path's crossing act — died 2026-08-12 with the ruler former
    // itself, superseded by pan-primary: the deferral pattern lives on in the
    // PLAIN pending click, ScrollDragState, which arms no sweep at all.)
}

// THE SWEEP'S ONE END OWNER (contract at the declaration). Every end path calls
// it — the clean release, on_motion's button-lost arm, the touch hook's end and
// the force-end finalizer — so the disarm and the commit cannot fork, which is
// what the four hand-copied end bodies this replaced could not promise.
//
// SELF-GUARDED, so the touch hooks' refused-begin streams and the force-end's
// unconditional call are all free.
void GuiInputHandler::commit_region_sweep() {
    if (!app.region_drag.active) return;
    const bool wrote = app.region_drag.wrote_trim;
    app.region_drag = RegionDragState{};
    // THE STROKE'S END COLLAPSES THE OVERLAY (architect 2026-08-20): "shift+drag
    // or longpress+drag on the waveform should collapse the region as soon as it
    // is set — this method usually indicates 'this exact region'." The sweep's
    // raise is therefore BRACKETED rather than sticky: the big surface goes up
    // at the stroke's first accepted trim write (apply_region_drag_motion) and
    // is put away the moment the span is settled, where it would only be noise
    // over a window the user has just declared.
    // BARE `[` (and its Show trim region button) IS THE RECALL — hiding
    // discards nothing, the trim persisting and re-showing identical.
    //
    // UNCONDITIONAL, at every end path this one owner serves, and each case is
    // wanted: the written stroke, the request's own; the DEGENERATE stroke
    // collapsed onto its anchor, whose trim resets to the whole song below (a
    // whole-song overlay is semantically the old unset state and owed no
    // surface); the motionless shift press-release that wrote nothing, which
    // raised nothing either and so finds the bit already down (the hide is a
    // guarded no-op, not a second act);
    // the touch region hold and the force-end finalizer, which share this owner;
    // and the `h` VIEW'S CARVED-OUT PLAYHEAD SWEEP, whose end now collapses an
    // overlay the user carried into the view. That last one is DELIBERATE: the
    // sweep moves the playhead's position in the music, which is exactly what
    // the hide rule hides for (clear_region_highlight, input_handler.h) — the
    // former escaped the rule only by writing the cursor direct.
    clear_region_highlight(app, viewport);
    // THE COMMIT TAIL IS THE ENDCAP DRAG'S, verbatim and for its own reason:
    // auto_clear_crossed_trim, the repaints, the target-render trigger, and the
    // PLAYHEAD PARKED AT THE COMMITTED TRIM START — at the end only, a
    // per-frame cursor chase being a cursor fighting the gesture that is moving
    // the bounds.
    //
    // AND THAT TAIL IS WHERE THE SWEEP'S DEGENERATE CASE IS ANSWERED (architect
    // 2026-08-19, retiring the sweep's minimum width floor): a stroke that ends
    // where it began rests begin == end, which auto_clear_crossed_trim's
    // `end <= begin` compare resets to the WHOLE SONG — the endcap drag's own
    // escape, made global by pointing the sweep at it rather than by copying it
    // (the rule at auto_clear_crossed_trim, input_trim.cpp). So the shared tail
    // is not merely convenient here; it is the sweep's whole answer to a
    // collapsed span, and nothing in this function tests a width.
    //
    // A sweep that WROTE NOTHING owes none of THAT TAIL, unchanged (the hide
    // above is outside this gate and runs for it too): a motionless
    // press-release, a stroke refused by degenerate geometry, and the `h`
    // view's carved-out former all end here with the trim exactly as they found
    // it, so no playhead parks and no render triggers behind them.
    if (wrote) commit_trim_mutation();
}

// ARM THE NAVIGATION SURFACE'S PLAIN PRESS — the pending click / grab-pan
// (contract at ScrollDragState, app_state.h). The press records its point and
// its surface facts and does NOTHING ELSE: no capture (that begins at the
// threshold crossing, so a click never blinks the cursor), no playhead, no
// deselect, no hide, NO SCRUB — nothing pops at press. The three surface
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
//   LIVE arm: deselect-all, then the placement body — playhead to the column,
//   live-session reseek, follow override (place_playhead_at_click_column).
//   THE TRIM REGION OVERLAY HIDES INSIDE THAT BODY since 2026-08-19, at the
//   movement owner move_playhead_to (the rule at clear_region_highlight): a
//   placement moves the playhead's position in the music, which is the rule
//   itself. A GUTTER column deselects and seats nothing, so it hides nothing
//   either — the placement body's own shape carried through honestly.
//   `h`-VIEW arm: the mode's land — clear the mode focus + selection (the
//   pair clearer, the deselect's mode analog; store selection untouched),
//   then the same placement body, whose seat hides the overlay the same way.
//   The empty-lane and ruler
//   stretches take this too since they are the extension: a click anywhere on
//   the surface moves the playhead, in the view as outside it.
//   SCRUB arm (2026-08-13, the waveform's LOWER half): ONE scrub act at the
//   press column and NOTHING ELSE — the act the lower half used to run at
//   mouse-down, moved here whole so that nothing on this surface pops at a
//   press any more. It is deliberately the FIRST arm and returns ahead of the
//   other two: the scrub selects nothing, hides nothing, moves no cursor
//   and overrides no follow, which is what keeps it the overlay's PREVIEW
//   gesture and is the halves' ONE difference (two, read honestly — the
//   omissions are the second). It cannot coincide with the `h` arm (that view
//   has no scrub half), and the scrub's own gutter no-op lives inside
//   scrub_press_at.
// NO SWEEP ARM on any path — the sweep is SHIFT's, and a click is not a drag.
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
        place_playhead_at_click_column(press_x - area.x, playback.is_playing(),
                                       app.playhead_cursor_sample);
        return;
    }
    selection.clear_selection();
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
    // same clamped value back to the caller (the region formers read it as the
    // gutter sentinel alone — the arm authors its own trim anchor from the
    // COLUMN, arm_region_drag_at — and the sweep's motion path clamps its
    // cursor carry by this same rule): move_playhead_to clamps internally, but
    // at a fractional flush-right zoom the painter-quantized wall
    // (q = nearbyint(spp*W)/W) differs from the click conversion's
    // current_samples_per_pixel, so the last visible column's frame can compute
    // to domain_total — one past [0, domain_total-1], which the display-state
    // validator would clear wholesale. The clamp also
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
    // THE SEAT BELOW HIDES THE OVERLAY and nothing here puts it back: the press
    // really does move the playhead, so the movement owner is right to hide,
    // and the sweep's raise waits for its FIRST ACCEPTED TRIM WRITE
    // (apply_region_drag_motion) so that what comes up is the stroke's own
    // region rather than the resting one it is replacing. A gutter press seats
    // nothing, hides nothing and arms nothing.
    selection.clear_selection();
    const int64_t sample = place_playhead_at_click_column(
        click_rel_x, was_playing, playhead_at_entry);
    if (sample < 0) return;
    arm_region_drag_at(click_rel_x, x, y);
}

void GuiInputHandler::create_marker_at_empty_lane(int click_rel_x) {
    // The empty marker-lane double-click, AT THE SECOND PRESS (2026-08-17 —
    // a recognized second press acts immediately; its one caller is the lane's
    // consume in on_button_press, which passes the press column and arms
    // nothing after the create).
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
    // overlay hide the drop's own playhead seat carries) — the
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
    // THE ON-SCREEN KEYBOARD'S OWED KEY-UP, above every gate and above even the
    // dropdown's release — the press's mirror. It is guarded on the held key
    // index alone, which only that surface's own press ever sets, so it claims
    // nothing that is not its; and it fires even when the press's act closed
    // the editor under it, because a delivered key-down owes its pair whatever
    // became of the surface (contract at the declaration).
    if (button == GuiMouseButton::Left && finish_onscreen_keyboard_release())
        return;
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
    // THE FOLDER OVERLAY'S ROW ARM ENDS HERE, the hoisted claim's mirror and
    // at the same rank: a motionless lift HIGHLIGHTS THE ROW AND OPENS IT (a
    // click activates, 2026-08-29), a scroll drag simply
    // ends. It is a no-op with no arm standing, so it costs the other
    // releases one test. The act may take the whole surface down under this
    // frame — a wav's play rebinds the engine, a project row's open closes the
    // picker and requests the reopen — which is why the arm is cleared at the
    // head of that body and this arm returns immediately after it.
    if (button == GuiMouseButton::Left && finish_folder_overlay_release(x, y))
        return;
    // THE RENDER PLAYER'S OTHER RELEASES (2026-08-28), the press block's
    // mirror: the scrub's marker drag (the seek commits here) and the modal
    // row's armed button through the one shared dispatch. Every other lift
    // is consumed — a chrome arm taken above dies here undispatched, the
    // veil's answer (no chrome can arm under the player anyway, its roster
    // being dead).
    if (app.render_player.active) {
        if (button == GuiMouseButton::Left) {
            if (finish_player_scrub_release(x, y)) return;
            // THE SHIFTED-TWIN VERDICT IS READ BEFORE THE TAKE, which clears
            // the arm the hold is measured against (R37; the term's two roads
            // are at modal_dialog_press_shifted).
            const bool shifted = modal_dialog_press_shifted();
            dispatch_modal_dialog_button(take_modal_dialog_release(x, y),
                                         shifted);
        }
        return;
    }
    // THE PICKER'S RELEASE (2026-08-28), its press block's mirror: the modal
    // row's armed Cancel through the one shared dispatch, and every
    // other lift consumed — the veil's answer.
    if (app.picker.active) {
        if (button == GuiMouseButton::Left) {
            dispatch_modal_dialog_button(take_modal_dialog_release(x, y));
        }
        return;
    }
    // THE EDITOR DIALOG'S ACT, the same shape over the other surface: the lift
    // on the armed OK / Cancel runs the session's own Enter / Esc through the
    // one modal key route. Above the text-drag branch because the two are
    // mutually exclusive by construction (a press on a button never reaches
    // the field claim), and above the four editor swallows below, which is
    // where an unarmed release still ends.
    if (button == GuiMouseButton::Left && modal_dialog_editor_active()) {
        if (dispatch_modal_dialog_button(take_modal_dialog_release(x, y)))
            return;
    }
    // THE CHROME ACT, the roster's own release body (2026-08-13): the lift on
    // the armed button runs its chord through the one release half, which
    // re-hits the target at
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
        // Read before the finalize, which clears the arm whole.
        const bool shift_extend = app.editor_text_drag.shift_extend;
        finalize_editor_text_drag();
        // Double-click seeding: a MOTIONLESS release (a pure click that left a
        // caret, no selection) seeds an editor-text candidate so a second click
        // within the window selects the clicked character class's run (word /
        // punctuation / whitespace). A drag that made a selection seeds nothing.
        // NEITHER DOES A SHIFT+CLICK (architect 2026-08-30): it is the
        // selection extend, not the first half of a word select, and a
        // shift-click that landed on the caret's own byte would otherwise seed
        // one by leaving no selection behind.
        if (!shift_extend && g.valid && !text_editor::has_selection(*g.ed)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_title_editor)) return;
    if (text_editor::is_active(app.measure_offset_editor)) return;
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
    // of "it stayed a click", equal by construction AT EVERY gui_scale
    // (double_click_slack_px() == drag_moved_threshold_px(): equal authored
    // constants through the one scaled_px conversion — the contracts are at the
    // two accessors, app_state.h). A moved endcap/bridge drag therefore seeds nothing
    // and, its own press having cleared any candidate at the top-of-frame, leaves
    // none behind. The record is consumed either way.
    {
        const TrimBarPressSeed seed = app.trim_bar_press;
        app.trim_bar_press = TrimBarPressSeed{};
        if (seed.active && !app.trim_drag.active &&
            std::abs(x - seed.press_x) <= double_click_slack_px() &&
            std::abs(y - seed.press_y) <= double_click_slack_px()) {
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
        // placement (deselect / mode-land, the overlay hide, playhead, reseek,
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
    if (app.overview_drag.active) {
        // The overview lane's box drag (contract at OverviewDragState). A
        // MOVED drag runs its final apply — the edge drag's terminating
        // apply_strip_drag_zoom (the one rebuild + predictor resync), the
        // pan's one deferred resync — and drops any double-click candidate,
        // the moved-drag rule.
        // A MOTIONLESS release completes NOTHING, the lane's v1 consumed
        // nothing — every act this lane owes has already run at its press (the
        // outside press's teleport since 2026-08-17, and the pan it arms after
        // it since 2026-08-18 commits per motion event), so no act is owed to
        // any lift here. No capture to end, no stem to erase, and the lane
        // seeds no double-click candidate of its own.
        const bool moved = app.overview_drag.moved;
        if (moved) {
            apply_overview_drag_at(x, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        }
        app.overview_drag = OverviewDragState{};
        return;
    }
    // (No scrub branch of its own: since 2026-08-13 the scrub has no drag
    // state — it rides ScrollDragState like every other act on the navigation
    // surface, and the branch above runs it, at the LOWER half's motionless
    // release, through the `scrub` flag the press stashed. Its press-time
    // dispatch is deleted; the contract is at ScrollDragState, app_state.h.)
    if (app.region_drag.active) {
        // THE SWEEP wrote the trim live during the drag (see on_motion); the
        // release runs the shared commit tail through the gesture's ONE end
        // owner, which every end path calls. A MOTIONLESS press-release (never
        // crossed the threshold) wrote nothing, so the owner just disarms —
        // the shift press was the placement and its own act is done.
        commit_region_sweep();
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
        // endcap/bridge press, which on THE TRIM BAR is a CONSUMED NOTHING
        // (architect 2026-07-30). Its whole act was publishing the trim window
        // as a region highlight, and that coupling retired with the SPAN FORM;
        // a deselect and a playback stop left behind would be pure cost on a
        // click that commits nothing. (A crossed pending became app.trim_drag
        // and commits through the branch above, where the setter's deselect and
        // the trim-mutation stop live.)
        //
        // ON THE WAVEFORM OVERLAY IT FALLS TO THE ORDINARY CLICK ACT
        // (2026-08-18): the same drag armed from the second surface, where a
        // motionless press-release is not a manipulation but exactly what a
        // press one pixel outside the overlay would have done — the UPPER
        // half's playhead placement, which HIDES the overlay like every other
        // point command, or the LOWER half's scrub, which deliberately does not
        // (the overlay's PREVIEW gesture). That is the escape hatch a
        // full-window overlay needs. The half is read from the PRESS point, the
        // pending click's own rule; the disarm leads the act, the release
        // bodies' standing shape.
        const bool click_act = app.pending_trim_drag.waveform_click_act;
        const int  press_x   = app.pending_trim_drag.press_x;
        const bool scrub     =
            waveform_lower_half(waveform_area(app),
                                app.pending_trim_drag.press_y);
        app.pending_trim_drag = PendingTrimDrag{};
        if (click_act)
            run_nav_click_act(press_x, /*history=*/false,
                              /*scrub_release=*/scrub);
        return;
    }
    if (app.pending_click.active()) {
        // THE MOTIONLESS LIFT OF THE ONE SURVIVING DEFERRED CLICK — the trim
        // bar's ctrl / ctrl+shift bound set (the contract is at
        // PendingClickAct, app_state.h; the 2026-08-17 ruling took the record's
        // other four kinds back to the press). The act runs on the PRESS
        // column, re-asking every gate live inside set_trim_bound_at_click.
        // THE RELEASE BODIES' STANDING SHAPE: read the pending, DISARM, then
        // act, so the act runs with no gesture live.
        // (A crossed pending is spent at the crossing — it ran its set and
        // became app.trim_drag, which commits through the branch above.)
        const PendingClickAct press = app.pending_click;
        app.pending_click = PendingClickAct{};
        run_pending_click_act(press);
        return;
    }
    if (app.pending_marker_press.active) {
        // THE FLAG'S MOTIONLESS LIFT OWES ONLY THE SEED — the click itself
        // acted at the press (2026-08-17; the contract is at
        // PendingMarkerPress, app_state.h), and only the release can tell a
        // click from a drag, so the next Marker double-click candidate is
        // written here and nowhere else.
        // THE POSITION IS THE PRESS'S, not this release's: it keeps the
        // SPATIAL pairing press-to-press, and it is the honest one for the
        // touch layer, whose synthesized release carries the finger's LAST
        // position while the press carries its down point. THE TIMESTAMP IS
        // THE SEED'S OWN, which is the release — the family's rule (TrimBar,
        // EditorText and EmptyLane all stamp their motionless release), so the
        // window is measured release-to-press here as it is everywhere else.
        // The split is deliberate: only the position has a reason to look back
        // at the press. (A consumed open armed nothing, so it cannot reach
        // this seed; a crossed pending became app.drag and seeds nothing, the
        // moved-drag rule.)
        const PendingMarkerPress press = app.pending_marker_press;
        app.pending_marker_press = PendingMarkerPress{};
        // THE SPAN IS THE PRESS'S TOO, for the position's own reason: the seed
        // describes the press, and the box may be repainted at a different
        // width before the second click arrives.
        app.double_click = DoubleClickCandidate{
            .surface = DoubleClickSurface::Marker,
            .time_ms = monotonic_ms(),
            .press_x = press.press_x, .press_y = press.press_y,
            .target  = press.marker,
            .span    = press.span};
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
// same order, each now a call to the gesture's own end owner.
// The gestures are mutually exclusive in practice, so this reads as a chain of
// no-ops around the one that is live.
// ITS MEMBERSHIP IS any_pointer_gesture_active's, EXACTLY (re-grepped
// 2026-08-28): the two lists must agree, because a caller's whole promise is
// that what follows lands on a gesture-free state, and that predicate is what
// "gesture-free" means to the keyboard, the wheel and the cursor. THE ELEVEN,
// in this body's order: the editor text drag, the marker reposition drag, the
// trim drag, the sweep (region_drag), the nav drag (scroll_drag), the overview
// drag, the three pendings (marker press, trim, deferred click) and THE RENDER
// PLAYER'S TWO ARMS — the folder overlay's row press and the play-scrub's
// marker drag, which joined this body 2026-08-28 through their own hard-end
// clears. WHAT EACH LEAVES DIFFERS AND EACH ARM SAYS SO: the drags COMMIT what
// stands, the pendings and the player's arms commit NOTHING (a force-end is
// not a click — the standing abnormal-end rule).
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
    // THE SWEEP wrote the trim live; the force-end COMMITS it exactly as the
    // release would, through the gesture's one end owner. Trim is history-less
    // by ruling, so what it wrote is not undoable — the standing trim gap, not
    // something this force-end introduces.
    commit_region_sweep();
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
        // DISARMS and commits nothing — a force-end is not a click, and the
        // outside press's own teleport already ran at the press, so nothing is
        // lost here. (No pending phase can be in flight: the record holds a
        // real drag or nothing, the two-day Pending teleport being deleted.)
        if (app.overview_drag.moved && playback.is_playing())
            playback.resync_predictor();
        app.overview_drag = OverviewDragState{};
    }
    // THE PENDINGS DISARM AND COMMIT NOTHING, which is not a cancel: there is
    // no release here (the button is still held), and a force-end is not a
    // click — the same rule the arms above state for their own unmoved
    // presses, and the same one the touch layer's ABNORMAL end (the
    // motionless-hold upgrade) delivers by leaving the button unheld. A pending
    // otherwise resolves only by the threshold crossing or a real release /
    // button loss.
    //
    // WHAT EACH PENDING LEAVES UNDONE DIFFERS, and stating it keeps this
    // honest (2026-08-17): the MARKER pending's click already committed at its
    // press — what dies here is only the drag it might have become and the
    // double-click SEED its clean release would have written, and neither is a
    // loss a force-end owes anyone (the committed click stands; undo is the
    // mitigation, the 2026-07-29 accepted answer for this exact shape). The
    // TRIM pendings — the endcap/bridge arm and the deferred BOUND-SET click,
    // the one act still lift-deferred — really do commit nothing: for the
    // bound set that is a real difference from its pre-2026-08-15 press-time
    // model, whose press had already written the bound. Trim is still
    // history-less; there is simply nothing to be history-less about on this
    // path. WHAT THE ENDCAP/BRIDGE ARM CAN STILL LOSE IS NOT A TRIM WRITE: an
    // arm taken on the WAVEFORM OVERLAY (waveform_click_act, 2026-08-18) would
    // have fallen to the ordinary click act at a clean motionless lift — a
    // playhead placement or a scrub — and a force-end runs neither, the same
    // abnormal-end rule the marker pending's seed takes above.
    app.pending_marker_press = PendingMarkerPress{};
    app.pending_trim_drag    = PendingTrimDrag{};
    app.pending_click        = PendingClickAct{};
    // THE RENDER PLAYER'S TWO ARMS END HERE TOO, through the same clears the
    // pointer-leave hook and the button-lost edge call — the row press with NO
    // act (its highlight and its open are the motionless LIFT's, and there is
    // no lift here; the row's double-click seed went with the surface itself
    // when a click became the open act) and the scrub's marker drag with NO seek
    // beyond the motion already applied (the seek is the release's, and the
    // sound never followed the marker). Each clear damages the face its arm
    // painted: the armed row's rect and the published scrub track, which the
    // resize caller then repaints under the new geometry anyway — the leave
    // hook's caller does not, which is why the damage lives in the clears.
    clear_folder_overlay_press();
    clear_player_scrub_drag();
    // A force-end is not a clean click sequence, so no candidate may survive to
    // pair with a later click (the standing rule at every non-release gesture
    // end) — and neither may the trim bar's press record, which would otherwise
    // seed one at the next release.
    app.double_click   = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
}

// THE REDESIGNED BUTTONS' HOVER, in ONE transition writer over the whole roster
// (row 1's four menu anchors and the view bar's three, row 3's two
// tabs, row 4's twenty-six — the toolbar four included since the 2026-08-12
// relayout, the history group's seven since 2026-08-18 — and the bottom row's
// eighteen since 2026-08-29: 53, the enum's
// own count at kRedesignButtonCount — the stash is
// AppState::redesign_buttons; only a MODAL's yield leaves a bottom-row member
// with a zero rect now, and it resolves unhovered with no arm here).
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
    // AND IT REFUSES UNDER A NOTIFICATION CARD, for the opacity rule rather
    // than for honesty: a card is opaque to the pointer (notifications.h), so
    // no button beneath one may wear a hover face or start a tooltip dwell —
    // a hover face is a PROMISE OF PRESSABILITY and the card's claim consumes
    // that press before any roster gate sees it. THE TERM LIVES HERE, at the
    // faces' one derivation, and not at the motion handler, because this walk
    // has TWO callers and the other is the TICK: a pointer RESTING on a card
    // over the icon row is exactly the case the review found, and a
    // motion-side guard would be undone by the next tick's recompute. It is
    // folded into `under_pointer` below rather than spelled as an early
    // return so the walk still writes `hovered = false` on whatever it lit
    // before the card slid up.
    const bool under_card = notification_card_at(app, mx, my) != 0;
    // NO DWELL RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT — read before the
    // walk because the walk below is what stamps it; the rule is stated at the
    // stamp itself.
    const bool modal_owns_the_keyboard =
        app.prompt.active || keyboard_modal_editor_active() ||
        app.render_player.active || app.picker.active;
    // THE DIALOG'S VEIL (2026-08-12): under a PROMPT or an EDITOR dialog the
    // WHOLE roster is refused — nothing behind the modal is pressable, so
    // nothing hovers. It was two rules until 2026-08-13, the editor half
    // admitting the reach-through's own buttons; the reach-through is retired
    // (the record is at its deleted predicate's site near the head of this
    // file) and the two collapsed into this one. The pointer-transparent FLAG
    // editor raises no veil: it is not a dialog and its roster presses were
    // never blocked.
    // THE RENDER PLAYER'S VEIL is the third term and THE PICKER'S the fourth
    // (2026-08-28): each mode's whole roster is dead through
    // redesign_button_enabled's first arm already, so neither term changes a
    // face — they are here so the walk's own statement of "nothing behind the
    // modal hovers" stays true by its own reading.
    const bool modal_veil =
        app.prompt.active || modal_dialog_editor_active() ||
        app.render_player.active || app.picker.active;
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
        const bool under_pointer = !modal_veil && !under_card &&
                                   rect_contains(f.rect, mx, my) &&
                                   redesign_button_hover_zone(app, id);
        // THE FACE ADDS THE ENABLED TERM AND THE HINT DOES NOT (architect
        // 2026-08-07): a disabled button keeps its dead face under the pointer
        // and still explains itself, kdenlive's own behaviour. This is the ONE
        // place the two consumers of a hover part company, which is why both are
        // resolved in this single walk.
        const bool inside =
            under_pointer &&
            redesign_button_enabled(app, audio, audio.total_frames(),
                                    playback, target_render, id);
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
            inside = rect_contains(
                app.redesign_buttons[static_cast<size_t>(
                                         app.chrome_press.index)].rect,
                mx, my);
            break;
        }
        if (inside != app.chrome_press.inside) {
            app.chrome_press.inside = inside;
            // Only an arm with a click face paints a pressed interior, and
            // its home strip pays — the row fork the face writers all
            // take. (Row 3's two-face tabs paint
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
    // and that membership is a CONSTANT: only the TEXT is stateful, never
    // whether a button has one. Re-derived from redesign_button_tooltip's
    // stateful overload, which moves the words on THREE buttons and takes no
    // hint away from any: SAVE (a publishing checkpoint first, then the history
    // view's "Save and Commit"), RENDER (the mid-render Cancel, then the
    // iteration bit) and, since 2026-08-15, THE BOTTOM ROW'S COLLAPSED
    // PLAY/STOP BUTTON (the live audition bit, the same condition its glyph
    // reads). (Row 3's tabs were the one membership move in the product, going
    // silent while the `h` view repurposed them as its walk selector; the
    // selector left the row on 2026-08-18 and they carry their ordinary hint in
    // every state again.)
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
// (A DISABLED ITEM RESOLVED TO NO ITEM from 2026-08-08 to 2026-08-15, which was
// what kept the two faces honest with one line rather than a term in the
// painter: neither face could name a row the press and the release refused. The
// predicate went producer-less with the Navigation menu — its "Walk both tabs"
// row inside the `h` view was the only item that ever greyed — and this walk's
// resolve line is deleted with it.)
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
// 2026-08-03): a press on ANY ANCHOR button (File, Edit or Settings) followed by
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
    const int hit = dropdown_item_at(app.last_mouse_x, app.last_mouse_y);
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
        // row-4 press (history_mode_disables_button, above) — row 4's history
        // group, Zoom out and the magnification pair aside, which carry
        // resting greys of their own. THE BOTTOM ROW HAS A RESTING CONSUMER
        // HERE FOR EVERY MEMBER BUT WALK BOTH TABS AND ADD TO SELECTION since
        // 2026-08-30 (the truthful-buttons ruling, reversing the 2026-08-15
        // scoped-truth ruling under which the row's members were lit outside
        // the `h` view whatever their chord would do): each arm at
        // redesign_button_enabled reads the predicate its act refuses on — the
        // verbs on the selection and the lock, the arrows on the tempo step,
        // the lane fork and the walls, the walk pair on the cycle's landing,
        // the skips on the jump's landing, Play on the launch, Copy value on
        // its eligibility — so this line consumes exactly the presses whose
        // chord would be a consumed no-op. Inside the view the derived
        // partition adds its own: the PLAY/STOP button, the FOUR ARROWS, the
        // verbs and ADD TO SELECTION (Space, the bare arrows and bare `k` are
        // consumed there), while the two skips — the mode's absolute jumps —
        // and the marker-walk THREE — its diff-flag cycle plus the march over
        // it — take no partition grey. The arrows joined the in-view list on
        // 2026-08-18 by being PAINTED in the view at all — the cluster swap
        // that hid them went with the history companions. ADD TO SELECTION
        // has no refusal to mirror at all: the lock does not carry it, a
        // selection being navigation.
        if (!redesign_button_enabled(app, audio, audio.total_frames(),
                                    playback, target_render, tc.id))
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
        // hold, and the carried bit is what the lift dispatches with. SO DOES A
        // CTRL press, which the band's modifier gate has already narrowed to
        // the buttons redesign_button_ctrl_admits names, so no second admission
        // is asked here — an unadmitted ctrl press never reaches this body. THE
        // PRESS'S CLOCK IS STAMPED HERE, unconditionally: the lift measures the
        // hold against kChromeShiftHoldMs to decide the SHIFT LONG PRESS, and
        // the stamp is taken for every button rather than for the
        // shift-admitting ones alone (redesign_button_shift_admits owns that
        // membership), a press having a time whatever it landed on.
        const int64_t now = monotonic_ms();
        app.chrome_press = AppState::ChromePress{
            AppState::ChromePress::Kind::Roster,
            redesign_button_index(tc.id), mods.shift, mods.ctrl, true, now};
        // THE HOLD-REPEAT'S ARM (architect 2026-08-16), for the rows that
        // carry `repeats`. ELIGIBILITY IS JUDGED UNDER THE PRESS-TIME CONTEXT
        // and it is the KEYBOARD'S OWN PREDICATE SHARED, not mirrored:
        // repeat_eligible is exactly what the platform's arming probe asks for
        // the physical key, so the button's hold arms in precisely the contexts
        // a held key would — and refuses in the ones it refuses, which is what
        // keeps a press some surface consumes from arming a burst that would
        // start firing when that surface closes under a held button. Nothing
        // has dispatched by this line and nothing will until the lift, so
        // "press-time" is simply the state this press found.
        //
        // The chord is the LIFT's minus its long-press term, which no repeating
        // row can reach (none of the six ADMITS shift, and a shift press
        // returned above the arm) and minus its CARRIED ctrl, which no
        // repeating row can carry either (none of the six admits ctrl, and the
        // band's modifier gate refuses an unadmitted ctrl press before the arm)
        // — so the predicate is asked about exactly the chord the burst will
        // fire. The zoom stepping pair's own ctrl comes from tc.ctrl,
        // this table's column, and is copied below like any other row's.
        if (tc.repeats) {
            GuiInputState chord{};
            chord.ctrl  = tc.ctrl;
            chord.shift = tc.shift || mods.shift;
            chord.alt   = tc.alt;
            if (repeat_eligible(tc.key, chord))
                app.chrome_press.repeat_due_ms = now + kHoldBeatMs;
        }
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
        // (The two-face rows paint none — no damage owed.)
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
    // THE VEIL, RE-ASKED ONCE FOR EVERY KIND: a chrome lift is a consumed
    // nothing while an editor dialog stands, whatever the arm is. It was a
    // membership test until 2026-08-13, admitting the modal-trap
    // reach-through's own buttons; with that retired the veil is blanket
    // again, and its whole job here is the editor OPENED MID-HOLD — a key
    // press deliberately does not end a pointer hold (main.cpp's set_on_key
    // hook), so Ctrl+S in the `h` view raises the commit-title editor under a
    // standing arm, and an arm taken before the dialog rose must not fire into
    // it. IT SITS ABOVE THE KIND SWITCH because the rule is every arm's and
    // none has an exception to it. (Carried per-branch it once was on the
    // roster's and missing from the deleted walk-tab kind's, so arming a
    // Remote/Local tab, raising the dialog mid-hold and lifting on the tab
    // switched the walk under the editor — which is why the term is stated
    // once, above the switch, rather than per branch.)
    // A PROMPT needs no term here for any kind: on_button_release's prompt
    // gate returns unconditionally above this call, so no arm reaches this
    // body while one stands — and neither does THE RENDER PLAYER's or THE
    // PICKER's, whose release blocks return above this call the same way; the
    // term below is the editor OPENED MID-HOLD's, and a player or a picker
    // opened mid-hold (bare `l`, Ctrl+O or `'` typed under a held button)
    // takes the same refusal through it.
    if (modal_dialog_editor_active() || app.render_player.active ||
        app.picker.active) return;
    switch (arm.kind) {
    case AppState::ChromePress::Kind::None:
        return;
    case AppState::ChromePress::Kind::Roster:
        break;
    }
    // A HOLD THAT ALREADY FIRED CONSUMES ITS OWN LIFT (architect 2026-08-16):
    // a tap gives exactly one act, at the lift, and a hold gives the stream
    // and nothing extra — the burst's fires already acted, so the lift adds
    // nothing on top of the run. The bit travels ON the arm, so it is asked
    // here before any of the roster's own gates (the veil above is asked
    // before it, for every kind) and dies with the arm the caller already took.
    if (arm.repeat_fired) return;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_index(tc.id) != arm.index) continue;
        // The lift must land on the armed button itself.
        if (!redesign_button_hit(app, tc.id, x, y)) return;
        // (The editor veil is re-asked once for every kind at the top of this
        // body — the roster's own copy lived here until the walk tab was found
        // to be missing it.)
        // The press-time refusals, re-asked against the live state — the
        // shift admission under the CARRIED shift, the enabled bit, the radio
        // rule. Each held at the press; any that no longer does makes the
        // lift a consumed nothing.
        if (arm.shift && !redesign_button_shift_admits(tc.id)) return;
        if (!redesign_button_enabled(app, audio, audio.total_frames(),
                                    playback, target_render, tc.id))
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
        // and it dispatches no Esc either: the act body is what the button
        // owes, not a key. (Until 2026-08-21 that was also a correctness
        // requirement — Esc ranked the region hide ABOVE the render cancel, so
        // a dispatched Esc would have hidden the trim region overlay instead of
        // cancelling. With the hide retired the key reaches the cancel in one
        // press whatever the overlay is doing, and running the act directly is
        // simply what the exception has always meant.)
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
        // pair — and it costs the desk nothing, the hold beat being well past
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
        //
        // AND IT REACHES NO CTRL-ADMITTING BUTTON, by construction rather than
        // by an exclusion (architect 2026-08-24): the two SKIPS admit CTRL for
        // the whole-piece jump and admit no shift at all, so this term's own
        // predicate answers false for them. The architect wants that jump off
        // glass, and a held skip gives the ordinary trim-bound jump exactly as
        // a tap does.
        const bool held_to_shift =
            redesign_button_shift_admits(tc.id) &&
            monotonic_ms() - arm.press_ms >= kChromeShiftHoldMs;
        // The shift term ORs the table's own (Redo's Ctrl+Shift+Z) with the
        // CARRIED press-time bit and the hold — well-defined because no row
        // sets both the table bit and the admission (see shift_admits), so this
        // one expression spells both members of each shifted pair however the
        // user asked for the shifted one. THE CTRL TERM IS THE SAME SHAPE ONE
        // AXIS OVER: the table's own ctrl bit ORed with the carried press-time
        // one, admitted where redesign_button_ctrl_admits says so — the band
        // gate's answer re-asked at the lift, the release's own second-wall
        // rule. The two carried bits never both stand: ctrl+shift is refused at
        // that gate.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl ||
                      (arm.ctrl && redesign_button_ctrl_admits(tc.id));
        chord.shift = tc.shift || arm.shift || held_to_shift;
        chord.alt   = tc.alt;
        on_key(tc.key, chord);
        return;
    }
}

// THE CHROME BUTTON HOLD-REPEAT, fired from the run loop's tick (architect
// 2026-08-16): while a press stands on a button whose chord row sets `repeats`
// — the bottom row's four cardinal arrows and the icon row's waveform
// magnification pair — synthesize that button's chord on the keyboard's own
// cadence, so a held BUTTON walks at the speed the held KEY does. It exists for the glass rig, which has no keyboard, and it
// works there with NO TOUCH-SPECIFIC CODE: the one-finger translation delivers
// an ordinary left press and an ordinary left release, so it arms and ends this
// arm exactly as a mouse does, and its motion deliveries keep the position this
// body re-hits honest.
//
// THE SCHEDULE'S TWO NUMBERS COME FROM DIFFERENT OWNERS ON PURPOSE. The FIRST
// fire is one kHoldBeatMs after the press — the product's own hold beat,
// matched to the architect's compositor delay so every deliberate hold in the
// product coincides (the readers' inventory is at that declaration). Every
// LATER fire is THE COMPOSITOR'S ADVERTISED KEY-REPEAT INTERVAL
// (GuiPlatform::key_repeat_period_ms), read PER FIRE because repeat_info may be
// re-sent at any time, and a compositor advertising rate 0 has key repeat
// DISABLED — so the buttons stop repeating there too, which is the honest
// mirror of the keyboard they stand in for.
//
// THE BURST'S FIRST FIRE IS ITS UNDO OPENER, this producer's own flip: a held
// KEY's burst opens with its physical press — the press acts, then the
// repeats merge behind it — but a held BUTTON's press dispatches nothing (its
// act is at the lift), so the first fire stands in for that press act. (The
// flip is VACUOUS for the magnification pair, whose setting is
// history-less: its bursts push no undo entry at all, so there is nothing to
// open or merge.
// The flag is set uniformly rather than forked on the row.) Fire
// one goes out with synthesized_repeat FALSE and pushes its own entry under
// the arrival-invalidate and the tap-window rules, and every fire behind it
// carries TRUE and merges by identity (the argument is at
// Undo::coalesce_gesture). Without the flip, a burst begun over a surviving
// foreign stamp would merge its first act into another subject's entry.
//
// THE HOLD'S RECORD IS THE ARM ITSELF (AppState::chrome_press), which is where
// the schedule and the fired bit live, so "the arm is standing" IS "the hold is
// standing" and every edge that drops the arm drops the burst. The
// authoritative inventory of the burst's ends is at that struct; this body owns
// only the four PER-FIRE questions below, which answer differently on purpose:
// off the button PAUSES (the scrollbar-button rule — sliding back on resumes,
// the hold standing throughout; a leave that exits the WINDOW ends the hold
// outright through the pointer-leave hook, and this body's first line sees the
// arm gone), a dead enabled bit PAUSES (the disabled-press consume's mirror —
// and since 2026-08-30 THE LADDER'S END for the magnification pair, whose
// face greys at its top and bottom rung (planner decision 53): a burst that
// walks the ladder to its end meets the dead bit on its next fire and rests
// there under the held pointer, greyed, nothing un-pausing it while the hold
// stands, and the lift that ends a fired burst is consumed), a
// rate of 0 PAUSES (stated at its own line), and lost eligibility DISARMS — a
// context that revoked the burst ends it
// rather than parking it. One fire per due tick, the next scheduled from NOW
// rather than accumulated, so a stalled frame yields one repeat and no burst.
void GuiInputHandler::tick_chrome_press_repeat() {
    AppState::ChromePress& arm = app.chrome_press;
    if (arm.kind != AppState::ChromePress::Kind::Roster) return;
    if (arm.repeat_due_ms == 0) return;
    const int64_t now = monotonic_ms();
    if (now < arm.repeat_due_ms) return;
    // A COMPOSITOR ADVERTISING RATE 0 HAS KEY REPEAT DISABLED, so these buttons
    // do not repeat either — the honest mirror of the keyboard they stand in
    // for. It PAUSES rather than disarms: repeat_info may be re-sent at any
    // time, and the schedule, already due, resumes on the next tick if it is.
    const int64_t period = gui.key_repeat_period_ms();
    if (period <= 0) return;
    arm.repeat_due_ms = now + period;
    const RedesignButton id = static_cast<RedesignButton>(arm.index);
    // PAUSED OFF THE BUTTON. The published rect re-hit at the pointer's
    // remembered position — the lift's own derive doctrine, not the arm's
    // `inside` bit, which serves the paint.
    if (!app.pointer_in_window ||
        !redesign_button_hit(app, id, app.last_mouse_x, app.last_mouse_y))
        return;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (tc.id != id) continue;
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift || arm.shift;
        chord.alt   = tc.alt;
        if (!repeat_eligible(tc.key, chord)) {
            arm.repeat_due_ms = 0;
            return;
        }
        if (!redesign_button_enabled(app, audio, audio.total_frames(),
                                    playback, target_render, tc.id)) return;
        // THE OPENER IS THE FIRST FIRE (the flip's statement is at this
        // function's head).
        chord.synthesized_repeat = arm.repeat_fired;
        arm.repeat_fired = true;
        // Through on_key, the same route the lift takes, so every gate the
        // chord passes on the keyboard applies per fire. (The physical-key
        // burst disarm lives UPSTREAM of on_key, at the platform delivery in
        // main.cpp — which is what lets this fire pass through on_key without
        // ending its own schedule.)
        on_key(tc.key, chord);
        return;
    }
}

// WHICH ITEM IS AT (x, y), or -1. The rects are the painter's published item
// boxes, so a hit is exactly the box that lights; a closed popup has zero rects
// and therefore contains no point, which is the correct cold answer.
//
// PURE GEOMETRY, DELIBERATELY, and since 2026-08-15 it is the whole answer: no
// item on either surviving menu can grey. (Whether a row could be hovered, armed
// or activated was a SEPARATE question, dropdown_item_enabled's, asked by each of
// this function's three callers on its own side — one geometric answer, one
// enablement answer, neither hiding inside the other. That predicate went
// producer-less with the Navigation menu; the separation is recorded because it
// is the shape a future disabled item takes, rather than a term folded in here.)
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
// motion is delivered synchronously (GuiInputCore::pointer_motion) and the
// release carries the input core's own pointer_x_/pointer_y_, while the one
// deferred kind, a captured drag's coalesced relative motion, is flushed
// immediately BEFORE any button event with the held bit crossing on the
// pre-release side so that flushed motion still reads the button as down
// (flush_deferred_motion, input_core.cpp). Same expression, same coordinates, same rects: the two
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
    // (A DISABLED ITEM ACTIVATED NOTHING AND CLOSED NOTHING, 2026-08-08 to
    // 2026-08-15. The recorded arm could never name one — the hover walk
    // resolved a greyed row to "no item" — but the DERIVE reads raw geometry, so
    // the gate belonged on this side of it too, and answering -1 routed the
    // release into the armed-nothing return below: consumed, menu still up,
    // the same answer the press over that row already gave. The predicate went
    // producer-less with the Navigation menu and this gate with it — which is
    // the whole reason the derive can now read geometry alone.)
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
    // a COMMAND menu (File since 2026-08-13, and Navigation from 2026-08-02
    // until its 2026-08-15 deletion) dispatches a chord, the SETTINGS one opens
    // the editor prefilled.
    if (dropdown_is_command_menu(menu)) {
        // THE ITEM IS ITS KEY, dispatched through on_key exactly as a redesigned
        // button dispatches its chord: every gate the keyboard route passes
        // (loading/blank, the modal gates, the read-only allowlist, the arm's own
        // refusals) applies identically, so an item whose command cannot act
        // right now simply does nothing — the buttons-never-grey rule, one
        // surface further out. No stop, no modal, nothing restated here. TWO OF
        // THE FILE MENU'S THREE ROWS RIDE THIS BODY WHOLE since 2026-08-28:
        // Ctrl+Q reaches on_key's own close route — the drag-modal hatch, the
        // dirty prompt, the WM-close ordering — with no second body anywhere,
        // which is the whole reason the Quit BUTTON could be retired for a menu
        // item without moving the act, and Ctrl+O reaches the Open project
        // prompt's one opener, whose own guards answer the states the keyboard
        // route does not.
        // THAT IS ALSO HOW THE `h` HISTORY VIEW ANSWERS THIS MENU (the ruling
        // is 2026-08-08's, made for the Navigation menu's seven rows): per item,
        // at the mode's own two gates — its allowlist carries Ctrl+Q, and
        // carried the zoom and framing rows, with history_mode_owns_key claiming
        // the rest — and the one Navigation row that would have meant something
        // else in there greyed above and never reached this dispatch at all.
        // Nothing greys now: that menu and its predicate are deleted.
        const CommandPopupItem& it = command_popup_item(menu, armed);
        close_dropdown();
        // THE ONE ROW WITH NO CHORD (the File menu's Synchronize to external
        // storage): no key binds it — the binding was refused rather than
        // deferred — so the release calls the act directly, and the act carries
        // the gates a chord would have met in its own body: the modal refusals,
        // the `h` view, the loading state (synchronize_to_external_storage's
        // declaration). Still CLOSE FIRST, THEN ACT, for the reason above.
        // (THE FILE MENU'S OPEN ROW LEFT THIS FORK on 2026-08-28, when the
        // architect bound the prompt to Ctrl+O: it is an ordinary chord row now
        // and rides the dispatch below like Quit, which is the standing model.)
        if (it.act == GuiPopupAct::SyncExternal) {
            synchronize_to_external_storage();
            return true;
        }
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
    // standing ruling (and since 2026-08-15 it has no exception anywhere; the
    // record is at kFilePopupItems), and their commands' own refusals answer.
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
//   * CTRL-exact over the navigation surface — the one nav drag's ZOOM entry
//     (arm_nav_zoom_press; the dual-axis strip drag this bullet used to name
//     died 2026-08-14 with the zoom's rotation onto the horizontal axis), on
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
//     hide the trim region overlay (a placement is a point command), seat the
//     playhead at the press column (run_nav_click_act's history arm — the
//     live recipe through the shared placement body, whose reseek cannot fire
//     in the silent view); a crossed drag is the captured pan, which moves no
//     playhead and clears nothing. "A click anywhere on the extension moves
//     the playhead" holds in here too: the ruler and the empty lane stretches
//     take the same pending, so their motionless clicks land the playhead
//     where the old empty-lane click only cleared the focus.
//   * a DOUBLE-CLICK anywhere on the TRIM BAR band FRAMES THE TRIM SPAN — the
//     span that band is displaying, the tab's own window in here as everywhere
//     else since 2026-08-18 — through the LIVE band's own framing owner
//     (run_span_framing_command). It zoomed to the viewed checkpoint's diff
//     span from 2026-08-05 until that day, when the bar stopped substituting
//     the delta for the real window. It is still the ONLY framing gesture a
//     standing view has, the internal edges
//     writing no viewport at all and the window being the user's for the whole
//     visit. It moves the viewport and nothing else, and a SINGLE click on that
//     band stays the consumed nothing a motionless trim-bar click is everywhere.
//   * a press on a DIFF FLAG in the MARKER LANE takes the mode's focus (at most
//     one, painted in its class's selected pair) and LANDS THE PLAYHEAD on that
//     flag's authored frame, through the same owner every marker land uses —
//     AT THE PRESS (2026-08-17: the mode has NO drag for the press to become
//     and the flag box claims the press whole, so the click's identity is
//     certain and the one-day lift deferral of 2026-08-15 is inverted with
//     the live flag clicks').
//     It touches NOTHING else: no store selection,
//     no live focus, no auto-select, no playback stop. It DOES take a resting
//     overlay with it (2026-08-06), through its LAND: a click that lands the
//     playhead moves the playhead's position in the music, which is the rule.
//   * the MARKER LANE's two MODIFIED clicks, on its FLAG BOXES: SHIFT takes
//     the contiguous range from the focus, CTRL toggles one flag's membership,
//     and both then focus the clicked flag and land the playhead on it (the live
//     selection model over the mode's own list; the store selection stays as
//     untouched as the plain click leaves it). A modified lane press that hits
//     NO flag is the GESTURE the modifier names, exactly as in the live views
//     since the lanes became the extension: shift the former, ctrl the zoom.
//   * SHIFT-exact on the navigation surface — THE SWEEP, CARVED OUT OF ITS OWN
//     TRIM WRITE: clear the mode focus + selection, seat the playhead at the
//     column, arm the drag (the live former's recipe with the mode's deselect
//     analog — EVERY FORMER DROPS THE SELECTION ITS SURFACE OWNS, the
//     family rule at RegionState, app_state.h). The drag rides the one motion
//     path, playhead on the moving endpoint, and WRITES NO TRIM: the view
//     promises the trim window is untouched throughout, and the carve-out lives
//     at the one site all three arms share (apply_region_drag_motion). So in
//     here the gesture is a playhead sweep and nothing more — the VIEW-LOCAL
//     reading span it drew until 2026-08-18 is gone with the separate region
//     state, there being no span store left to draw one in.
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
                // THE PRESS ACTS (2026-08-17: a diff-flag press has no drag to
                // become — nothing in this mode drags a marker and the flag
                // box claims the press whole — so its identity is certain and
                // the one-day lift deferral of 2026-08-15 is inverted): the
                // range / toggle, the focus move, the land and the region
                // clear run HERE, on the live hit, with the press's own
                // modifier shape (shift != ctrl by this branch's gate).
                select_history_diff_flags_modified(hit, shift);
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
        if (sample >= 0) arm_region_drag_at(x - area.x, x, y);
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
    // THE TRIM BAR'S DOUBLE-CLICK FRAMES THE TRIM SPAN — THE ORDINARY ACT
    // (architect 2026-08-18, with the diff-span substitution's deletion: the
    // bar shows the tab's real trim window in here, so the gesture that reads
    // "zoom to what the bar shows" must frame that window and nothing else).
    // It ran the VIEWED CHECKPOINT'S DIFF SPAN from 2026-08-05 until then,
    // which was the same gesture over a different command; there is no mode
    // command left at all now — the mode's fourth plain act is the LIVE act,
    // reached through the live band's own owner (run_span_framing_command).
    // A SINGLE plain click here
    // is the consumed nothing a motionless trim-bar click is everywhere in the
    // product (architect 2026-07-30), and only the second click
    // inside the window frames. (The comparison this used to draw was to a
    // LOCKED TAB, whose trim drags refused while its framing double-click
    // navigated; that model is retired — read-only stopped refusing trim on
    // 2026-08-07 — and the mode is the only per-zone consumer of the band left.)
    // The WHOLE band, endcaps
    // included: those endcaps are painted geometry with no gesture in here, so
    // splitting the band would be a distinction nothing acts on. Read-only does
    // not refuse it — framing is navigation, exactly as the pan and the zoom
    // above are.
    //
    // THE MACHINERY IS THE LIVE BAND'S, UNCHANGED: the consume ACTS AT THE
    // PRESS (2026-08-17, with the whole double-click family) through
    // the shared test (trim_bar_double_click_at, which reads the snapshot the
    // press took before clearing the field), then the seed RECORD for the
    // release to resolve — TrimBarPressSeed, whose release-side owner needs no
    // mode arm of its own, since it seeds on "the pointer never left the slack
    // and no trim drag went live" and no trim drag can go live in here at all.
    // A consumed press seeds nothing (the family rule) and, unlike the live
    // band's, falls through to NO cap/bridge arm — the mode has none — so it
    // frames and is done. NOTHING differs from the live band now but that
    // fall-through, which is why the framing itself is the live owner's call
    // rather than a mode arm.
    //
    // THE WAY TO SEE A WHOLE DELTA is to zoom out and come back: bare `0` is on
    // the mode's allowlist and the delta's flags are laid across the whole
    // piece, so the overview shows all of it — the product's own navigation
    // rather than a gesture that meant something different in one mode.
    {
        const GuiRect trim_bar = top_trim_row_area(app);
        if (y >= trim_bar.y && y < trim_bar.y + trim_bar.h) {
            if (trim_bar_double_click_at(dc_at_press, x, y)) {
                run_span_framing_command();
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
        // drawn. A FLAG runs the focus click AT THE PRESS (2026-08-17, like
        // the two modified clicks above — no drag to become, so nothing needs
        // the lift); an EMPTY STRETCH is
        // the navigation surface — the pending click / pan, whose motionless
        // release lands the playhead at the column through the mode's land
        // (the extension rule: this used to clear the focus and land nothing,
        // and now places like every other click on the surface). No EmptyLane
        // seed — the marker create is authoring, consumed in here.
        const int hit = hit_test_flag(app, audio, x, y);
        if (hit >= 0) {
            // THE PRESS ACTS (2026-08-17): the focus move, the land and the
            // region hide are the CLICK, run here on the live hit.
            focus_history_diff_flag(hit);
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
// `hit` is an index into app.history_mode.flags, RESOLVED AND ACTED ON AT THE
// PRESS (2026-08-17 — the mode has no drag for a flag press to become, so the
// click's identity is certain and the one-day lift deferral of 2026-08-15 is
// inverted): its ONE caller is handle_history_mode_press's plain flag claim.
// The router resolves a flag only since
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
// AND IT HIDES THE TRIM REGION OVERLAY (architect 2026-08-06, from his live
// pass: sweep a span in the view, then click a diff flag, and the span used to
// stay) — THROUGH ITS LAND since 2026-08-19, the land being one of the rule's
// two movement owners (clear_region_highlight, input_handler.h). Unconditional
// on the arm that matters, the owner hiding above its own idempotence return, so
// a re-click of the focused flag still hides. The out-of-range arm (`hit` < 0,
// tolerance the router cannot produce) lands nothing and so hides nothing, which
// is the rule read honestly rather than a hole: the empty-lane click the router
// DOES produce goes to the navigation surface's own act, which places the
// playhead and hides there.
// The rest of the minimalism STANDS: no store selection, no live focus, no
// auto-select, no playback stop.
void GuiInputHandler::focus_history_diff_flag(int hit) {
    const int was = app.history_mode.focus;
    const bool had_selection = !app.history_mode.selection.empty();
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
// BOTH RUN AT THE PRESS (2026-08-17 — the mode has no drag for either press to
// become, so nothing needs the lift; its one caller is the router's modified
// flag claim, with the press's own modifier shape). It
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
// BOTH ALSO HIDE THE TRIM REGION OVERLAY (architect 2026-08-06), the plain
// click's own rule at the same strength, and since 2026-08-19 through the LAND
// rather than at a site of their own: a click that lands the playhead takes the
// overlay with it — discarding nothing, the trim standing behind it. The land is
// past the range guard below, so a call that changes nothing hides nothing.
//
// DAMAGE IS THE FOCUS CLICK'S: full-window, unconditional here, because either
// arm changes at least one flag's face (ctrl always flips the clicked one's
// membership; shift always writes a set containing it) — and where it would not,
// a repaint of the strip is the same cost the plain click pays.
void GuiInputHandler::select_history_diff_flags_modified(int hit, bool extend) {
    const int n = static_cast<int>(app.history_mode.flags.size());
    if (hit < 0 || hit >= n) return;
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
    // A COMMAND MENU NEEDS NO SUCH COVER AND IS LIVE IN THE VIEW (File by
    // construction when it landed 2026-08-13, on the architect's 2026-08-08
    // ruling for the Navigation menu): it has no direct call to shut — its one
    // row is
    // a CHORD, dispatched through on_key exactly as a redesigned button's is, so
    // the mode answers PER ITEM at the same two gates a key meets, and File's
    // Ctrl+Q is on the allowlist. (Navigation's seven rows were answered the
    // same way — the allowlist admitted zoom in / out / overview,
    // history_mode_owns_key claimed center-on-focus and the two marker steps as
    // re-expressions over the diff flags — with the one row whose chord then
    // meant something ELSE in here, "Walk both tabs", greying at the item
    // instead (the chord marches the pair in the view too since 2026-08-18),
    // which is the only thing a chord dispatch cannot answer for: the command
    // runs, it is simply not the one the label names.)
    //
    // THE SCOPE IS RE-DERIVED RATHER THAN INHERITED (2026-08-15, with the
    // Navigation menu's deletion): the lockout was NARROWED to Settings alone on
    // 2026-08-08 for the express purpose of letting Navigation open in the view,
    // and that reason is gone — so the question "should this refuse a menu again"
    // is asked fresh, and the answer is NO. The scope's live half is FILE, whose
    // Ctrl+Q the mode admits, and shutting the whole row would refuse a menu
    // whose only item WORKS in there — the face promising less than the key
    // delivers, the read-only band ruling's own criterion. The narrowing survives
    // on its own merits rather than on the menu that occasioned it: what it names
    // is the ONE menu with a pointer bypass, and Settings is still that menu.
    //
    // THE GUARD STAYS ABOVE THE CLOSE BELOW, and that matters rather than being
    // trivially safe: with File open in the mode, a hover switch onto
    // the dead Settings anchor arrives here, and returning from ABOVE the close is
    // what makes it a nothing — it neither puts File away nor opens
    // Settings, so the pointer crossing a dead anchor leaves the standing menu
    // exactly as it was.
    //
    // THE EDIT MENU JOINED THE LOCKOUT ON 2026-08-20, and the scope re-derived
    // above admits it on its own terms rather than by widening back to the
    // whole row: Edit is a COMMAND menu, so it has no direct call to shut —
    // but unlike File, whose Ctrl+Q the mode ADMITS, every one of Edit's five
    // rows is a chord the mode's allowlist drops. The per-item answer that
    // makes a command menu safe in here is "the command runs and its own gate
    // refuses", and when that is the answer for EVERY row the menu is a box
    // that opens onto nothing. Refusing it is the same criterion the narrowing
    // used, read the other way: the face must not promise more than the keys
    // deliver. Its anchor greys beside this (history_mode_disables_button).
    //
    // THE SERIES MENU JOINED THE LOCKOUT ON 2026-08-27 on that same criterion,
    // with nothing re-argued: it is a COMMAND menu, and BOTH its rows (bare
    // `m` and bare `i`) are chords the mode's allowlist drops — the mass-marker
    // category was the one group the view dropped whole, and the relocation
    // moved where those two commands are reached without moving what the mode
    // does to them. Its anchor greys beside this one too.
    if (app.history_mode.active &&
        (menu == DropdownMenu::Settings || menu == DropdownMenu::Edit ||
         menu == DropdownMenu::Series)) {
        return;
    }
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
// opened from row 1, the anchors open on the POINTER ALONE — the menu-bar
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
// THE ARROWS' HOLD-REPEAT ENDS HERE TOO, and needs no line of its own: the
// schedule and its fired bit live ON the arm, so taking the arm takes the burst
// with it. That is the point of homing them there — one hold, one lifetime, no
// second edge list (the inventory is at AppState::ChromePress).
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
    // THE ON-SCREEN KEYBOARD'S HELD KEY IS THE FOURTH MEMBER (2026-08-27) and
    // it is the one that DELIVERS rather than merely clears: a key acts at the
    // press, so what its lift still owes is the KEY-UP — and the core's repeat
    // arm dies on that stable code and on nothing else this edge can reach, so
    // a key whose lift never came would repeat forever. The producer is real
    // and is the touch stream's own: the SECOND-FINGER UPGRADE delivers no
    // button release at all (the fork is at GuiInputCore::end_touch_left_hold),
    // only this unheld motion — so a second finger landing while a letter is
    // held is exactly the case, and typing on glass is where it happens.
    // Running the ordinary release body is the right end for it: that body is
    // the key-up plus the un-press damage, and neither depends on where the
    // finger was.
    if (app.onscreen_keyboard.pressed_key >= 0)
        finish_onscreen_keyboard_release();
    // THE RENDER PLAYER'S TWO ARMS ARE THE FIFTH AND SIXTH MEMBERS
    // (2026-08-28): the folder overlay's row press and the scrub's marker
    // drag both act at the lift, so a lift that never comes must drop them
    // with nothing committed — the chrome arm's own rule, on the same edge.
    clear_folder_overlay_press();
    clear_player_scrub_drag();
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
    // Sub-threshold: the press has not yet become a sweep. Below the shared
    // Chebyshev gate NOTHING HAS HAPPENED AT ALL beyond the press's own click —
    // no trim is written and no overlay stands, the arm having stopped raising
    // one on 2026-08-21 — so a motionless shift click is a pure placement with
    // nothing to put away (commit_region_sweep's unconditional hide still runs
    // at its release and is a guarded no-op there). Once a drag, always a drag
    // (moved never re-engages). (The
    // one-day RULER arm's crossing act — the deferred dissolve + deselect —
    // died 2026-08-12 with the ruler former, superseded by pan-primary.)
    if (!app.region_drag.moved) {
        if (std::max(std::abs(mouse_x - app.region_drag.press_x),
                     std::abs(mouse_y - app.region_drag.press_y)) <
                drag_moved_threshold_px()) {
            return;
        }
    }
    app.region_drag.moved = true;
    // A moved sweep drops any double-click candidate: this press
    // became a drag, not the first click of a double-click. No former
    // seeds a candidate itself since 2026-08-12 (the EmptyLane seed is
    // the plain pending click's motionless-release act now, and the
    // formers seed nothing), so this clear covers only a candidate a
    // PREVIOUS clean click left resting — the standing moved-drag clear
    // route.
    app.double_click = DoubleClickCandidate{};
    // The MOVING endpoint at the pointer column, clamped to the visible strip
    // like the other drags' live tracking. THE COLUMN YIELDS TWO VALUES, THE
    // DOMAINS KEPT APART (the rule at RegionDragState::anchor_source_frame):
    // far_frame is the ACTIVE-domain frame for the PLAYHEAD — the cursor carry
    // below, the same click->frame basis the press seated it on — and the
    // TRIM's moving end is the same column's whole SOURCE frame, taken
    // separately at the write below through the sweep's one column->trim
    // route (sweep_trim_frame_at_column), which needs no domain hop in the
    // writer. Also clamped into the
    // live domain: at a fractional flush-right zoom the painter-quantized
    // wall differs from the click conversion, so the last visible column's
    // frame can land at domain_total — one past [0, domain_total-1] — which
    // the display-state validator would clear wholesale (same rule as the
    // press seat, place_playhead_at_click_column).
    int rel = mouse_x - area.x;
    if (rel < 0) rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    const int64_t far_frame = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, rel), app, audio);
    // THE TRIM WRITE — the sweep IS a trim write (architect 2026-08-18): from
    // the drag's fixed ANCHOR to the pointer's column, ordered and clamped to
    // the song walls, through the one owner that carries all of that
    // (write_trim_from_sweep, input_trim.cpp). NO WIDTH RULE stands between the
    // two ends since 2026-08-19 (the retired floor's record is at that owner),
    // so a stroke authors whatever span it draws — including none at all, which
    // the release turns into the whole song. It owns its own same-pair
    // short-circuit, so a sub-pixel jitter event inside one column writes and
    // repaints nothing; `wrote_trim` latches for the release's commit gate.
    //
    // THE `h` VIEW IS CARVED OUT EXPLICITLY, at the one site all three arms
    // share: that view promises the trim window is untouched throughout, so its
    // sweep writes NO trim and is a playhead carry alone. (Its former had drawn
    // a VIEW-LOCAL reading span until 2026-08-18; there is no span state left
    // to draw one in, the overlay being the trim everywhere.)
    if (!app.history_mode.active &&
        write_trim_from_sweep(app.region_drag.anchor_source_frame,
                              sweep_trim_frame_at_column(rel))) {
        app.region_drag.wrote_trim = true;
        // AND THE FIRST ACCEPTED WRITE RAISES THE OVERLAY (architect
        // 2026-08-21, moving the raise off the press): INSIDE this branch on
        // purpose. The overlay derives from the resting trim, so raising it
        // anywhere earlier shows the OLD window — the `h` former writes no trim
        // and raises nothing, a refused write raises nothing, and what the user
        // sees is only ever a span this stroke itself authored, which since the
        // write runs anchor->pointer is the NEW one from its first frame. The
        // owner is a no-op when the bit is already set, so the later motions of
        // a stroke cost nothing. It carries the no-framing rule and the `h`
        // carve-out (show_trim_region_overlay, input_handler.h), and the lamp
        // needs nothing: the button reads app.region.shown. The bracket's other
        // end is commit_region_sweep, which hides unconditionally.
        show_trim_region_overlay(app, viewport);
    }
    // SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): sweeping a span
    // does NOT select the markers it contains (the reverse coupling — a region
    // selecting its contents — was tried and retired; do not re-propose) — the
    // press already deselected all and the trim write deselects again on the
    // setter rule, so the selection is EMPTY throughout. The `h` view's own
    // former entries ride this same motion path and deselect nothing (they
    // clear the MODE's pair instead), and write no trim either.
    //
    // THE DRAG CARRIES THE PLAYHEAD (architect 2026-07-30, live-test
    // refinement: "i'd prefer the playhead move along with the drag for
    // region highlight - more intuitive"). The cursor rides the MOVING
    // endpoint — far_frame, already clamped playable by the conversion above,
    // so the write needs no clamp of its own — while the anchor stays put as
    // the trim's other bound. EVERY ARM rides this one motion path: each
    // seats the playhead at
    // its click and the pointer carries the cursor from here on, in the view
    // as outside it, from the finger as from the mouse. THE RELEASE THEN PARKS
    // IT at the committed trim start (commit_region_sweep), which is the trim
    // family's own end-of-gesture rule and the reason the park is not run per
    // event here.
    // DIRECT CURSOR WRITE, not move_playhead_to: a keep-visible edge-align
    // would scroll the viewport out from under a live gesture, and the span's
    // endpoints are painted against the viewport the drag started in.
    // PLAYBACK IS UNTOUCHED per motion by THIS body: the press/begin's at-entry
    // reseek-keeping-alive and the trim write's own first-accepted-change STOP
    // are the whole playback story of this gesture, and a per-column reseek
    // would re-cue the audio on every pixel.
    // The waveform invalidate below repaints the cursor's
    // HEAD AND STEM with the ground — its rect runs from the window top
    // down through the waveform, so the marker-lane head is inside it (the
    // triangle this used to name retired with its lane in row 5); the
    // TIMESTAMP invalidate is owed
    // separately because the bottom row's CLOCK shows this cursor whenever
    // no scanner is active, and it lives outside the waveform area.
    // (THE SLIVER PARAGRAPH IS RETIRED with the release-time min-size check
    // (2026-08-18) and stayed retired when the minimum width floor went the
    // same way (2026-08-19): a jitter drag dissolves nothing and is widened to
    // nothing — it commits the sliver it drew, and Shift+[ is the way back.)
    app.playhead_cursor_sample = far_frame;
    viewport.invalidate_waveform_area();
    viewport.invalidate_clock_area();
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a PendingMarkerPress crossing the threshold —
// the flag's click acted at its PRESS since 2026-08-17, so the crossing only
// begins the drag); with no gesture it
// recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the ARMING PRESS's click act landed the playhead on the marker, so the drag
// tows it by construction).
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
    // THE NOTIFICATION CARDS' HOVER (2026-08-29), above every branch for the
    // menu-row exit's reason: the cards are hit above every veil, so their
    // hover must be answered under every modal too, and every branch below
    // returns.
    //
    // THE CARD'S OPACITY IS NOT SPELLED HERE, and deliberately not: the two
    // hover walks a card can stand over — the roster's
    // (recompute_redesign_button_hover) and the band's
    // (update_folder_overlay_hover) — each carry the term themselves, because
    // the roster's has a SECOND caller, the tick, and a pointer RESTING on a
    // card is exactly the case that must not light what is under it. An early
    // return here would be undone by the next tick and would freeze a live
    // gesture whose pointer merely crossed a card. The cursor map, the press
    // claim and the touch pan zone ask the same one owner
    // (notification_card_at) for the same reason.
    update_notification_hover(mouse_x, mouse_y);
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
        // from an anchor onto a view-bar button must put that anchor's menu away
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
        // remembered: THE VIEW BAR'S THREE — row 1's other two buttons are the
        // anchors, and an
        // ANCHOR is skipped (the OPEN menu's own does nothing at all — no
        // re-open, no close — and another one SWITCHES through the walk below,
        // both unchanged). It was "Quit and the view bar's three" until
        // 2026-08-13, when the Quit button became the File menu and joined the
        // skipped side; the Navigation anchor's 2026-08-15 deletion moved this
        // membership not at all, an anchor being skipped either way. The close goes through close_dropdown, the one close
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
    // THE FOLDER OVERLAY'S MOTION, for BOTH contents and at the press
    // claim's own rank: a standing row arm follows the pointer (the feint's
    // inside bit, or the band's scroll drag once the vertical gate is
    // crossed) and owns the motion whole, and with no arm standing the band
    // takes its row hover and the motion carries on below — which is what
    // hands the player's and the picker's own motion branches below their
    // hover work. A LOST BUTTON is the hard end: the arm drops and nothing
    // commits, the chrome arm's own rule.
    if (app.folder_overlay.press.armed) {
        if (!mods.primary_button_held) {
            clear_folder_overlay_press();
            return;
        }
        update_folder_overlay_press_motion(mouse_x, mouse_y);
        return;
    }
    update_folder_overlay_hover(mouse_x, mouse_y);
    // THE RENDER PLAYER'S MOTION (2026-08-28): a standing scrub drag moves
    // the marker, and otherwise the modal buttons' hover face and the roster
    // recompute (all-false under the veil term) run — nothing below this
    // branch does.
    if (app.render_player.active) {
        if (app.render_player.scrub.armed) {
            if (!mods.primary_button_held) {
                clear_player_scrub_drag();
                return;
            }
            update_player_scrub_motion(mouse_x);
            return;
        }
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // THE PICKER'S MOTION (2026-08-28): the modal buttons' hover face and the
    // roster recompute (all-false under the veil term), nothing else — no
    // field, no drag, no scrub; the overlay's own arm and hover ran above.
    if (app.picker.active) {
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
            // the selection. IT IS THE SAME MOTION FOR BOTH ARMS — the plain
            // press seats the anchor at the caret it just placed, the SHIFT
            // press keeps the anchor that was already standing, and neither
            // rewrites it from here — so a shift+click's drag goes on
            // extending from the original anchor (architect 2026-08-30).
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
    // click, the outside press's teleport having already run at its press.
    // (This state only ever holds a real drag: an outside press arms the same
    // Pan, and the Pending phase that deferred the teleport is deleted.)
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
                drag_moved_threshold_px()) {
            return;
        }
        app.overview_drag.moved = true;
        apply_overview_drag_at(mouse_x, /*final_event=*/false);
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
                    drag_moved_threshold_px()) {
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
    // THE ONE SURVIVING DEFERRED CLICK (PendingClickAct, app_state.h): the trim
    // bar's ctrl / ctrl+shift bound set. Nothing has been committed yet — the
    // set acts at the LIFT or here at the crossing — so this branch owns both
    // ways it can end early. Placed ABOVE the pending trim drag
    // below because the crossing HANDS OVER to it and falls
    // through, so the same motion event begins and applies the drag.
    if (app.pending_click.active()) {
        if (!mods.primary_button_held) {
            // A LOST BUTTON COMMITS NOTHING and simply disarms — the standing
            // rule for every lift-act surface, the same answer the force-end
            // finalizer gives, and the way the TOUCH layer's ABNORMAL end (the
            // motionless-hold upgrade) reaches this state for free: it delivers
            // a motion with the button unheld precisely so an unmoved press
            // commits nothing.
            app.pending_click = PendingClickAct{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_click.press_x),
                     std::abs(mouse_y - app.pending_click.press_y)) <
                drag_moved_threshold_px()) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // THE CROSSING SPENDS THE ARM. Read, disarm, then
        // act — the release bodies' standing shape.
        const PendingClickAct press = app.pending_click;
        app.pending_click = PendingClickAct{};
        // THE BOUND SET RUNS HERE AND THEN BECOMES THE DRAG — the act first,
        // at the PRESS column, then the
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
            // 2026-07-30): its highlight publish is retired, so a press that
            // never travelled leaves the trim exactly as it found it. A LOST
            // BUTTON IS NOT A CLICK, which is where this parts from the clean
            // release: since 2026-08-18 a clean motionless lift of a pending
            // armed from the WAVEFORM OVERLAY falls to the ordinary click act
            // (PendingTrimDrag::waveform_click_act, app_state.h), and this path
            // deliberately runs no act on either surface — the standing
            // abnormal-end rule, stated at that field.
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
                drag_moved_threshold_px()) {
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
    // SWEEP below writes TRIM, which is BAND rather than authored content, so
    // neither the home-view gate nor read-only reaches it and neither ever
    // did. Per-site translation (drag anchor capture, motion delta
    // conversion, hit tests) lives in the handlers below.
    if (app.region_drag.active) {
        // Left button must still be held; if not, the release was lost —
        // end the gesture, committing the trim it has written (as a clean
        // release would). Modifier changes mid-drag are ignored.
        // THIS ARM ALSO ENDS A LIVE TOUCH REGION GESTURE that a real mouse
        // motion interrupts (the hook gesture holds no logical button, so
        // primary_button_held reads false here) — the accepted cross-device
        // edge at begin_touch_region's declaration: the user's own
        // two-handed act, every end a commit.
        if (!mods.primary_button_held) {
            commit_region_sweep();
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
    // THE MARKER FLAG'S PENDING PRESS (armed by the PLAIN flag press alone —
    // its click already acted at the press; contract at PendingMarkerPress,
    // app_state.h). What is still open is the reposition DRAG and the
    // release-side double-click SEED, so this branch owns both ways the
    // gesture can end early. Handled before the hover
    // fallthrough below and after the other drag branches (this pending and any
    // other pointer gesture are mutually exclusive — the arming press does no
    // other work).
    if (app.pending_marker_press.active) {
        if (!mods.primary_button_held) {
            // A LOST BUTTON DISARMS AND SEEDS NOTHING — it is not a clean
            // click sequence. The CLICK is not taken back: it committed at the
            // press (2026-08-17), and the touch layer's ABNORMAL end (the
            // motionless-hold upgrade reaching here with the button unheld)
            // therefore no longer un-commits a flag press as it did under the
            // one-day lift model — the recorded cost of press-time acting,
            // with undo as the mitigation, exactly the 2026-07-29 accepted
            // answer for this shape.
            app.pending_marker_press = PendingMarkerPress{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_marker_press.press_x),
                     std::abs(mouse_y - app.pending_marker_press.press_y)) <
                drag_moved_threshold_px()) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // THE CROSSING SPENDS THE ARM into the drag. Read, disarm, then act —
        // the release bodies' standing shape. NO CLICK ACT RUNS HERE: it ran
        // at the press, so the stop, the select, the land and the region hide
        // all already stand — the select is what paints the dragged flag
        // BRIGHTENED, and the stop is why no follow override is needed below
        // (nothing can restart playback under the held button: the drag-modal
        // gate swallows every chord while this pending stands).
        const PendingMarkerPress press = app.pending_marker_press;
        app.pending_marker_press = PendingMarkerPress{};
        // THE TWO AUTHORING GATES LIVE HERE, not at the arm: they guard the
        // DRAG (marker motion is authoring), never the click, so a read-only
        // tab and an off-home column still selected and landed at the press
        // and simply refuse to move anything.
        // ONE DRAG, ONE GATE since 2026-07-29: the
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
        // NO DOUBLE-CLICK CLEAR IS OWED HERE: the seed is the motionless
        // release's alone, so a press that becomes a drag never seeded one,
        // and on_button_press's own
        // top-of-frame clear emptied the field before this press did anything —
        // with the button held, nothing can re-seed it in between.
        if (!marker_drag.begin_drag(press.marker, press.press_x)) {
                // Begin refused (bad index / no audio): the gesture is DROPPED,
                // its pending already cleared above, so this is a gesture end
                // like any other and takes the loop tail's re-resolve like one.
                return;
        }
        // No follow override needed: the arming press ran the click act's
        // stop, and nothing can have restarted playback since (the drag-modal
        // gate), so there is no live playhead to chase.
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
