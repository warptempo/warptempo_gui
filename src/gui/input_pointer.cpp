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
// publication — the flag editor's FlagEditorBox, the three dialog
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
// the same ids. THREE roster entries are absent — row 1's FILE, EDIT and
// SETTINGS anchors (re-greped 2026-09-09 against kDropdownMenus, the Help
// anchor having left with the top strip relayout), each of
// whose action is a POPUP TOGGLE,
// not a chord at all, since no keyboard chord opens or closes a dropdown. All
// three are spelled at their own claim, which walks kDropdownMenus rather than
// naming them.
// THE ABSENTEES ARE NAMED IN ONE PLACE ONLY: the static_assert below this
// table, which is what makes "the table's length plus those three IS the
// roster" a build-time fact rather than a remembered list of names.
//
// The `shift` column is each button's OWN chord — TWO rows set it: Redo's
// Ctrl+Shift+Z and, since 2026-09-29, PREVIOUS MARKER's Shift+Tab, which set
// it from 2026-08-15 until the walk pair merged into one button on
// 2026-09-22 and set it again when the dedicated button came back (the walk's
// own shift stays an ADMISSION beside it); Ctrl+Shift+Tab (walk both tabs)
// was a third until its button was deleted on 2026-09-14, the chord being
// Switch Tab's admitted shift now. It is not
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
    // redesign_button_shift_admits and redesign_button_ctrl_admits, both in
    // app_state.h, because the TOOLTIP's
    // MODIFIER LINE must appear exactly where a modified press does something —
    // a static_assert beside that table enforces it. One fact, two readers.)
    //
    // RADIO: this button reports a state it can only ever turn ON, so a LIFT
    // on it while it is already selected is a CONSUMED NOTHING — AT THE LIFT
    // ALONE since 2026-10-07 evening: the press ARMS like every enabled
    // button's (arm_redesign_press), the hold paints the pressed face over
    // the lit one (the toolbar case's CHECKED AND PRESSED, paint_toolbar_box),
    // and only the lift's act is the nothing (finish_chrome_press_release).
    // THE CLAIM'S OWN CONSUME IS GONE for the pen's sake (architect
    // 2026-10-07 ~22:15, on the tablet: "it blinks: goes dark for a moment,
    // goes back to white", at the down and again at the lift — "a
    // radio-button issue — the toggles don't do that"): the pen's HOVER_EXIT
    // before the tip's down and the touch translation's leave at the lift
    // each drop the hot face (main.cpp's leave hook), and with no arm
    // holding it off (the hover walk keeps hot at −1 only while a chrome
    // press is armed) the down's synthesized entry motion and the lift's
    // HOVER_ENTER lit it again around each drop. Armed, the selected radio
    // reads press, release, hover exactly as a toggle does. THE ICON ROW'S VIEW
    // GROUP is the flag's one user since the tab pair's deletion
    // (2026-10-01); the walk lamp, read-only, history and
    // Cumulative are TOGGLES and press through in both directions, which is why
    // this is a flag and not `selected` alone. (THE BOTTOM ROW'S PLAY / STOP
    // PAIR was a radio for hours on 2026-08-15, and the S/T, W/P and WALK pairs
    // were radios until 2026-09-04; all four collapsed into single stateful
    // buttons, which is the only reason any of them ever needed the flag: two
    // buttons over one chord. The consume itself is untouched and stayed
    // GENERIC throughout — keyed on the flag plus the lamp, with no id list
    // anywhere.)
    //
    // THE VIEW GROUP'S THREE ARE RADIOS for the LIFT'S SILENCE: their chords
    // are the ABSOLUTE selectors, which are IDEMPOTENT — on_key's own handler
    // already makes a press on the current combination a no-op, so dispatching
    // would be harmless rather than wrong. The flag keeps the lift on the lit
    // button a plain consumed nothing, the radio's own reading, rather than
    // leaning on the act's idempotence. (It once kept the CLAIM too, for a
    // face the bitmap crops could not show — a pressed interior over a lit
    // button; the faces are painted now and compose the two, paint_toolbar_box.)
    bool           radio;
    // (EVERY ROW PAINTS A PRESSED INTERIOR — the icon row's and the bottom
    // row's click face — so there is no click-face column: the tab row's two
    // tabs, the one surface that showed nothing new on a press, are deleted
    // (2026-10-01), and row 1's anchors carry no chord row at all. The face
    // tracks the pointer through the arm's `inside` bit — the paint reads
    // redesign_button_pressed_face, app_state.h.)
    // REPEATS: a held press on this button synthesizes its own chord over and
    // over — the pointer twin of holding the key.
    //
    // A HELD REPEAT OUTRANKS THE LONG-PRESS SHIFT — THE STANDING PRINCIPLE,
    // stated here, its one authoritative home (architect 2026-09-21, making
    // explicit what was already true: glass reaches no rung of a repeating
    // button's modifier ladder). A `repeats` row's long press IS ITS REPEAT,
    // never its shift: the hold that would read as Shift on a non-repeating
    // shift-admitting button (chrome_shift_hold_ms(), the hold delay) is the
    // burst's first fire here (at kHoldBeatMs, the fixed beat — two numbers
    // that differ, the hold delay the shorter), so the lift's
    // hold-as-shift term reads
    // `!tc.repeats` off this column (finish_chrome_press_release) and the
    // burst's chord never carries a held shift (arm_redesign_press). A
    // repeating button's shifted act is therefore plastic-only — a real
    // Shift-click — and a step whose unit needs to be reachable on glass must
    // be the BARE press's own (the P column's Left / Right hop, 2026-09-21,
    // horizontal_arrow_step in gui_input.h, is that shape). The admissions'
    // records point here (redesign_button_shift_admits, app_state.h).
    //
    // MEMBERSHIP (the bottom row's four
    // cardinal arrows from 2026-08-16, joined by UNDO AND REDO on 2026-09-13 —
    // see below; the two HOLD-COLUMN nudges, Ctrl+Left / Ctrl+Right, carried
    // it from 2026-09-22 until their deletion on 2026-09-23, and the waveform
    // magnification pair from 2026-08-26 until its deletion on 2026-09-14). The
    // press arms the burst
    // on the ChromePress itself and tick_chrome_press_repeat fires it with
    // GuiInputState::synthesized_repeat set, so the undo coalescing is the
    // repeat-identity rule the keyboard already has, and a fired burst
    // suppresses the lift's own act. Defaulted, so the rows that do not repeat
    // need no eighth column. SIX ROWS CARRY IT since 2026-09-23, re-counted
    // off the table when the two hold-column nudges left it: the four arrows
    // and UNDO / REDO (architect
    // 2026-09-13, "like the Left/Right nudge") — stepping through history is
    // the continuous step gesture their held chords already were at
    // repeat_eligible, and the buttons are glass's only road to it. Neither
    // pushes an undo entry of its own (a restore records nothing and clears
    // the coalesce stamp), so the opener flip below is vacuous for them.
    bool           repeats = false;
};

// THE PRESS CLAIM'S HALF OF THE BUTTON ROSTER — every CHORD-DISPATCHING button
// in the redesign — the icon row and the bottom row — in one table. The flags above
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
// the prompt all read exactly as before. Everything else on the icon row and
// the bottom row is here.
constexpr ToolbarChord kToolbarChords[] = {
    // The toolbar four — icon-row members since the 2026-08-12 relayout
    // dissolved row 2 (the chords, gates and flags are UNCHANGED by the move;
    // only the face and the band changed hands).
    //
    // UNDO AND REDO REPEAT (architect 2026-09-13): a held button walks the
    // history at the held Ctrl+Z's own cadence, the arm asking repeat_eligible
    // about the row's own ctrl (and Redo's shift) columns. Neither admits a
    // modifier, so the long-press exclusion at the lift costs them nothing.
    // A burst that runs out of history meets the GREYED face on its next fire
    // (history_step_actionable's empty-stack term, through
    // redesign_button_enabled) and rests there under the held pointer, the
    // retired magnification pair's ladder end in the same shape; the same pause holds
    // for the Restrict-undo lamp's verdict and the other tab's lock. The
    // held KEY's own wall is silent at the dispatch arm (input_handler.cpp).
    // OPEN PROJECT (architect 2026-10-07), the row's first button, a group
    // of its own: Ctrl+O, the project picker, the File menu row's own chord.
    // It admits SHIFT — Revert, the one translated twin (the lift's chord
    // build below) — and nothing else; a one-shot act, no radio, no repeat.
    {RedesignButton::OpenProject, GuiKeys::O,  true,  false, false, false},   // Ctrl+O (+Shift: Ctrl+Alt+O)
    {RedesignButton::Save,       GuiKeys::S,   true,  false, false, false},   // Ctrl+S
    {RedesignButton::Undo,       GuiKeys::Z,   true,  false, false, false,  true},   // Ctrl+Z
    {RedesignButton::Redo,       GuiKeys::Z,   true,  true,  false, false,  true},   // Ctrl+Shift+Z
    // THE COPY RESOLVED VALUE BUTTON (architect 2026-08-29), the value pair's
    // pointer home for the copy, BETWEEN REDO AND RENDER since 2026-09-29
    // (kdenlive's own order — Save | Undo, Redo, Copy, …): CTRL+C, which the
    // main window binds to nothing else (bare `c` is the centre key; the text
    // editors' own Ctrl+C is their router's).
    // Its lift copies the focused marker's resolved value to the system
    // clipboard, exactly as the key does — button-is-its-chord literally, the
    // press dispatching Ctrl+C through on_key at the LIFT while the KEY acts at
    // the press. The row carries the ctrl, Undo's shape, so a ctrl-click is the
    // consumed nothing the band's modifier gate makes it on any button that
    // does not admit ctrl, and the long press reaches nothing.
    //
    // NOT A RADIO AND NO LAMP (an act, not a mode), and it does NOT repeat: a
    // copy repeats onto itself, so the key is one-shot at repeat_eligible and
    // the button carries no `repeats`. ITS GATES ARE NOT THE VERBS': the
    // READ-ONLY lock ADMITS Ctrl+C (a clipboard write authors nothing), so
    // the button stays lit on a locked tab, while the `h` view consumes the
    // chord and greys it through the derived partition. (Bare `j` was its chord
    // until 2026-09-29 and binds nothing now.)
    {RedesignButton::IconCopyValue,
     GuiKeys::C,      true,  false, false, false},                             // Ctrl+C
    {RedesignButton::Render,     GuiKeys::R,   true,  false, true,  false},   // Ctrl+Alt+R (+Shift)
    // (THE EDIT FLAG BUTTON — bare Return on a button, the flag editor's third
    // road — stood in the bottom row's verb group from 2026-08-27 and in the
    // icon row for the evening of 2026-09-29, when the architect DELETED it:
    // tapping a flag and then moving the pen to a button is counterintuitive
    // when a double tap on the flag does it, and the double-click is safe since
    // the first click arms and waits and the grab gate doubled. The editor's two
    // roads, the Return key and the double-click on the flag, are unchanged.)
    // Row 4 — the icon row. (THE TWO VIEW LAMPS stood here from 2026-09-04,
    // the radio-pair collapse, to 2026-09-15 — one button per axis where
    // IconS/IconT and IconW/IconP were four radios over the bare `t`/`p`
    // chords, each a plain TOGGLE carrying NO radio flag. The architect
    // deleted the whole category with those two keys; the three view selectors
    // below are the axes' only keyboard road now.)
    // (THE SHOW TRIM REGION BUTTON'S ROW IS DELETED — 2026-09-22, with its
    // button and its bare `[` chord: the architect uses the tablet's pen,
    // which reaches the trim bar, so a toggle for the waveform overlay at rest
    // had nothing left to show. Its shift admission — Shift+[ the maximizer — went with the button;
    // Reset Trim is Shift+0 and rides Full zoom out's shift press. The trim
    // scissors' row
    // before it was deleted on 2026-08-18.) THIS TABLE DOES NOT DECIDE
    // PAINTED ORDER — every one of its readers matches by id or by published
    // rect, never by position — but it is kept in the row's order so nobody
    // reads a mismatch here as the layout's truth; that truth is the
    // painter's kIconRowButtons, with each group's leader at
    // redesign_button_opens_icon_group (app_state.h).
    // FULL ZOOM OUT (2026-08-12, the grand relayout): a momentary
    // navigation chord, the command's pointer home, no radio, click face like
    // the rest of the row. It stays LIVE in the `h` view — `0` is on the
    // mode's allowlist — which the derived partition answers with nothing
    // hand-listed. (CENTER, bare `c`, stood beside it as the zoom pair until
    // 2026-09-29, when its row moved to the bottom row's walk group below.)
    // THE STEPPED ZOOM BUTTONS
    // (bare `=` / `-`) WERE REMOVED 2026-09-25 (architect): zoom is on every
    // surface — the Ctrl+drag on the waveform, the two-finger pinch, the S
    // Pen's button-held drag. IT ADMITS A MODIFIER, SHIFT
    // (redesign_button_shift_admits), since 2026-09-22, its
    // shift-click or long press dispatching Shift+0, RESET TRIM. It does not
    // repeat: the `repeats` column is unset.
    {RedesignButton::IconZoomFitBest,  GuiKeys::Digit0, false, false, false, false}, // bare 0
    // WAVEFORM MAGNIFICATION (architect 2026-09-22) — the bare
    // backtick since 2026-09-23 (bare `]` for its first hours, then bare
    // `[`), a TOGGLE with
    // a lamp, right after Full zoom out. Live in both audio views, on a locked tab and
    // under the read-only lock (a display posture on that allowlist), and
    // DEAD in the `h` view, whose allowlist does not name it.
    {RedesignButton::IconWaveformMagnification,
     GuiKeys::Grave, false, false, false, false},                            // bare `
    // (THE WAVEFORM MAGNIFICATION PAIR'S ROWS ARE DELETED — 2026-09-14, with
    // their buttons and the setting they stepped, architect approval
    // 2026-09-14: the picture's gain varies over source time, the continuous
    // curve derived from the source since 2026-09-23.)
    // FOLLOW (architect 2026-09-23) — bare `f`, a TOGGLE with a lamp
    // (AppState::follow), behind the magnification lamp. Live on a locked tab
    // and under the grid-iterations lock (navigation), DEAD in the `h` view,
    // whose allowlist does not name it.
    {RedesignButton::IconFollow, GuiKeys::F,   false, false, false, false},   // bare f
    // (THE COPY AND PASTE ROWS ARE DELETED — 2026-08-20, with their buttons:
    // the architect's propagate relocation gave the propagate commands the
    // new EDIT MENU as their one pointer home, so Ctrl+P and Ctrl+Alt+P reach
    // the pointer as MENU ITEMS now. Their chords did not die with the rows —
    // a menu item dispatches through on_key exactly as a button's chord does,
    // which is why the relocation needed no second body anywhere. The shift
    // admission IconPaste carried for Ctrl+Alt+Shift+P went too, that chord
    // being a menu row of its own now; the trim scissors' deletion above is the
    // same shape.)
    // (THE BPM AND ITERATION ROWS SPENT 2026-08-27 TO 2026-09-04 OUT OF THIS
    // TABLE, with their buttons, and it was the propagate relocation's shape
    // exactly: the Series relocation gave bare `m` and bare `i` a MENU ROW
    // each. Neither chord died with its row — a menu item dispatches through
    // on_key exactly as a button's chord does — and neither carried a modifier
    // admission to move, which is why both rows come back below unchanged when
    // the architect deleted that menu. They are kept in the ROW'S OWN ORDER,
    // which puts them past the viewport group rather than here.)
    // RESTRICT UNDO TO CURRENT VIEW (architect 2026-09-04) — a lamp
    // closing the same group: bare `z`, a TOGGLE reading the live bit its own
    // chord flips. It shares its letter with the pair it governs, which is the
    // whole argument for the key, and it stood beside that pair in the toolbar
    // group for the hours of its first day before the architect moved it here
    // because it is a viewport gesture too. Live on a locked tab (the switch
    // authors nothing, so its chord is on the lock's allowlist even though
    // Ctrl+Z is not) and DEAD in the `h` view, where both undo stacks are
    // frozen and the mode's allowlist drops the chord, so the derived partition
    // greys this face in there.
    //
    // IT ADMITS CTRL AND THE PAIR (architect 2026-09-29): a modified press
    // spells the button's own key under those modifiers, so its ctrl-click is
    // CTRL+Z, UNDO, and its ctrl-shift press CTRL+SHIFT+Z, REDO — the keys'
    // own acts with every refusal they carry (the lock, the lamp's own
    // verdict, an empty stack), each carding where the key cards. It admits no
    // shift alone, Shift+Z binding nothing (redesign_button_ctrl_admits /
    // redesign_button_ctrl_shift_admits). On glass the ctrl is the S Pen's
    // side button, and the side button held through a long press is the redo.
    // The row does not repeat, so a held modified press acts once at the lift.
    {RedesignButton::IconRestrictUndo,
     GuiKeys::Z, false, false, false, false},                                // bare z
    // THE ITERATION GROUP (architect 2026-09-04) — two rows back from the menu
    // row, and their 2026-08-27 rows verbatim: the architect deleted the
    // Iterations dropdown once the icon row had room again, so both commands
    // are BUTTONS once more and this table is their chord half. BPM'S KEY IS
    // CTRL+B (architect 2026-09-15, moved off bare `m`): a Ctrl chord guards
    // against a stray bare press, as Ctrl+D and Ctrl+N do (the arm is at
    // handle_mode_keys, input_key_dispatch.cpp); the button is its chord, so
    // it admits the same modifier the keyboard actually has.
    //
    // NEITHER ADMITS A FURTHER MODIFIER and neither repeats: the BPM opener
    // raises a modal editor, which a repeat could only raise onto itself, and
    // the mode toggle is one bit. Both are TOGGLES-or-acts rather than radios
    // — the mode flag presses through in both directions — and both are
    // consumed by the `h` view, which greys them through the derived
    // partition below with nothing hand-listed, the answer the ITERATIONS
    // ANCHOR needed a hand-named arm for while it was a menu.
    {RedesignButton::IconBpm,  GuiKeys::B,   true,  false, false, false},     // Ctrl+B
    {RedesignButton::IconIter, GuiKeys::I,   false, false, false, false},     // bare i
    // FLATTEN CLOSES THE GROUP (architect 2026-09-19), up from the bottom
    // row's marker verbs the same day it landed there: CTRL+F, which clears
    // EVERY warp marker's tempo deviation terms. ITS SEAT IS A
    // CLASSIFICATION BY SUBJECT — a deviation term exists because a GRID SWEEP
    // APPENDED ONE to every cell it wrote, so the act that takes those terms
    // off again stands beside the mode that produces them, and both read the
    // piece rather than a selection (the full reasoning is at the roster
    // entry, app_state.h).
    //
    // BUTTON-IS-ITS-CHORD HOLDS LITERALLY: the press dispatches Ctrl+F
    // through on_key at the LIFT like every other chrome button, while the
    // KEY acts at the press like every other hotkey. The ACT is on_key's own
    // arm and there is no second body, so both locks, the W-column rule, the
    // nothing-to-flatten refusal and the card are all inherited whole — the
    // move between lanes touched none of them, this table being read by id
    // and never by position.
    //
    // AND IT ADMITS SHIFT, alone in this group: its twin is CTRL+SHIFT+F,
    // which COLLAPSES those
    // terms into their one sum instead of removing them — so a shift-click or
    // a LONG PRESS at chrome_shift_hold_ms() reaches the second act with no
    // keyboard. The row's own `shift` bit stays FALSE, the admission and the
    // table bit being mutually exclusive by the shift term's construction
    // (finish_chrome_press_release); the membership is
    // redesign_button_shift_admits (app_state.h) and the tooltip's second
    // line is bound to it by that predicate's static_assert.
    //
    // NOT A RADIO AND NO LAMP (an act, not a mode), and it does NOT repeat: a
    // flatten repeats onto itself, so the keys are one-shot at
    // repeat_eligible and this row carries no `repeats`.
    {RedesignButton::IconFlatten, GuiKeys::F,   true,  false, false, false},  // Ctrl+F
    // THE RENDER-ENTRY GROUP (architect 2026-08-14): listen, load in place, the
    // read-only toggle, the history opener. IT IS LISTEN AND THE PADLOCK since
    // 2026-09-01, the LOAD IN PLACE having moved to the history group below
    // (its row moved with it, the table being read by id and never by
    // position). Bare `l` is the player's one BUTTON now; bare `'` outside the
    // `h` view still opens the same player from the keyboard, which is why
    // listen stays lit in every state this row is live in but a running
    // render, which the open refuses (redesign_button_enabled).
    {RedesignButton::IconListen, GuiKeys::L,   false, false, false, false},   // bare l
    // THE READ-ONLY TOGGLE (2026-08-14, the padlock's move off the tabs): bare
    // `o` toggles the ACTIVE tab's read-only bit. A TOGGLE like
    // iteration — its selected face reads the live bit its own chord flips —
    // and NOT a radio: it presses through in both directions. The button is
    // its chord with no exception now, which is exactly why the padlock moved
    // here (the roster record is at RedesignButton::IconReadOnly).
    {RedesignButton::IconReadOnly, GuiKeys::O, false, false, false, false},   // bare o
    // SETTINGS (architect 2026-09-29), between the padlock and the
    // tooltip lamp since 2026-10-01: bare
    // `;`, the settings prompt, through on_key like every row, so the
    // read-only gate, the iteration gate and the `h` view refuse the lift
    // exactly as they refuse the key — and the face greys on all three first
    // (redesign_button_enabled). Momentary, no radio, no modifier, no repeat.
    {RedesignButton::IconSettings, GuiKeys::Semicolon, false, false, false, false}, // bare ;
    // ENABLE TOOLTIPS (architect 2026-09-29), the render-entry group's
    // last since 2026-10-01, behind Settings: the bare BACKSLASH, a TOGGLE with a lamp (AppState::
    // show_tooltips) and NOT a radio — it presses through both ways. Live on a
    // locked tab, under the grid-iterations lock and in the `h` view (all
    // three allowlists admit it, so the derived partition leaves it lit).
    // No modifier, no repeat.
    {RedesignButton::IconTooltips, GuiKeys::Backslash, false, false, false, false}, // bare backslash
    // THE HISTORY MODE (2026-08-04): bare `h`, a TOGGLE like
    // iteration — its chord opens the mode and closes it, and the button
    // dispatches on both edges because the icon row's band claim sits ABOVE the
    // mode's pointer gate (the rows' presses are covered by the KEYBOARD gate
    // instead, which admits `h` through handle_history_mode_key one line before
    // the allowlist). It stands right-anchored, one group gap left of the
    // view group, since 2026-10-06 (kIconRowHistoryOpener, paint_handler.cpp).
    {RedesignButton::IconHistory, GuiKeys::H, false, false, false, false},     // bare h
    // THE WALK LAMP (architect 2026-08-18 as a radio pair, one button since the
    // 2026-09-04 collapse) — which walk the `h` view's lane reads: Git is the
    // committed checkpoint history, Session this session's own undo/redo
    // timeline. BARE `g`, which was free, and NO radio flag: the chord is a
    // TOGGLE over the two walks and the button flips the same axis, so there is
    // no already-lit half to consume any more.
    //
    // IT IS BOUND ONLY INSIDE THE VIEW, like the four companions below, and
    // what keeps it from dispatching outside one is its ENABLED bit: it paints
    // in every state on this row, and outside the view it wears the dead face
    // and the press is consumed at arm_redesign_press's disabled line. Even
    // reached, bare `g` is bound in handle_history_mode_key alone.
    {RedesignButton::HistoryWalk,
     GuiKeys::G,      false, false, false, false},                             // bare g
    // THE HISTORY COMPANIONS — the icon row's last group behind the opener
    // again since 2026-08-18 (they were this row's from 2026-08-04, the bottom
    // row's swapped cluster from 2026-08-14, and back here with the architect's
    // roster relayout; nothing about their chords or gates moved on either
    // trip — only their FACE at rest, which is the icon row's own mode rule).
    //
    // THE CUMULATIVE READING'S TOGGLE (2026-08-08): bare `u` flips the history
    // view's delta between ITERATIVE (off) and CUMULATIVE (on). A TOGGLE like
    // iteration and the history button — the selected face reads the
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
     GuiKeys::U,      false, false, false, false},                             // bare u
    // THE REVERT ACT (2026-08-05): BARE `v` applies the view's SELECTED diff
    // flags backwards into the live state and closes the view. It rode CTRL+H
    // until 2026-09-01, when the architect moved the act onto the letter the
    // render player's retired Stop had just freed — the succession is at the
    // dispatch arm (input_handler.cpp). Momentary like the two
    // below — not a radio, not a toggle, click face only. It is one of the TWO
    // entries here whose chord is NOT claimed by the mode's own vocabulary
    // (the load-in-place at the group's tail is the other, since 2026-09-01):
    // it dispatches from on_key's ordinary body, BELOW the read-only gate, so
    // a locked tab refuses the click exactly as it refuses the key. BOTH OF ITS
    // IN-VIEW REFUSALS WEAR THE GREY since 2026-08-30 — the lock's and the
    // mode's empty-subject one, both composed into this entry's own admission
    // (history_revert_actionable) and reaching the face through the DERIVED
    // partition, which walks the chord through this very table — so the click
    // is consumed with no card and the grey is the message (the
    // truthful-buttons ruling, which retired the 2026-08-15 faceless pair the
    // architect had reversed as a per-selection blink).
    // OUTSIDE the view the button is dead like its three
    // neighbours (2026-08-18) and there is no click to consume at all.
    {RedesignButton::HistoryRevert,
     GuiKeys::V,      false, false, false, false},                             // bare v
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
     GuiKeys::Comma,  false, false, false, false},                             // bare ,
    {RedesignButton::HistoryNewer,
     GuiKeys::Period, false, false, false, false},                             // bare .
    // THE LOAD IN PLACE, the group's last since 2026-09-01 (architect: "move
    // the button to the history section"). The ROW IS UNCHANGED — it
    // synthesizes bare `'`, exactly as it did from the render-entry group —
    // and the chord's own fork is what makes the move safe: inside the view
    // `'` raises the confirmation on the viewed member, which is this button's
    // whole act, and outside it the press never reaches dispatch at all, the
    // face being dead there (arm_redesign_press's disabled line) exactly as
    // the six above. Momentary like Revert and the two steps: click face only.
    // It is the group's second entry whose chord is not the mode's own
    // vocabulary — `'` is an ALLOWLIST admission, so a locked tab's click is
    // refused below the mode gate exactly as the key is, Revert's own shape.
    {RedesignButton::IconLoadInPlace,
     GuiKeys::Apostrophe, false, false, false, false},  // bare '
    // THE VIEW GROUP, the icon row's last, flush at its right edge (architect
    // 2026-10-01): the ABSOLUTE view
    // selectors Source+Warp, Target+Warp, Target+Phase on bare 1 / 2 / 3, in
    // the painter's order (kIconRowViewGroup, paint_handler.cpp). Everything
    // the selectors own arrives by construction through on_key's own handler —
    // the audio-first-then-markers order, the refused-target-entry abort of
    // the whole press, the coincidence auto-select, the read-only admission
    // (they are navigation), the modal swallow. There is no second route to
    // keep in step. RADIOS, with the row's click face (the flag's
    // justification is at the struct's `radio` column).
    {RedesignButton::ViewSW,     GuiKeys::Digit1, false, false, false, true}, // bare 1
    {RedesignButton::ViewTW,     GuiKeys::Digit2, false, false, false, true}, // bare 2
    {RedesignButton::ViewTP,     GuiKeys::Digit3, false, false, false, true}, // bare 3
    // The BOTTOM ROW (the transport half architect-ratified 2026-08-11 as the
    // touch arc's first surface; the marker-walk group added 2026-08-15, the
    // four SINGLE-MARKER VERBS moved down from the icon row 2026-08-18, and
    // ADD TO SELECTION landed behind them later that day).
    // SEVENTEEN
    // rows (re-counted 2026-09-29 evening: the walk group grown to four that
    // morning, then Edit Flag and Copy Value up to the icon row — Edit Flag
    // deleted there the same evening — and Jump to Defining Marker in),
    // every chord already bound elsewhere: the row adds no semantics
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
    // consume is untouched — the view group's three use it.
    // bare Space, toggle_playback and playback_launch_playable are untouched
    // by construction, as they were under the radio.
    //
    // THE FOUR ARROWS TAKE THE TABLE'S `repeats` COLUMN (architect
    // 2026-08-16, reversing his own 2026-08-13 deletion of the same gesture,
    // which he finds did not hold up in practice; the column is the whole
    // membership and no list restates it — Undo and Redo joined on 2026-09-13,
    // the magnification pair's 2026-08-26 membership leaving with it on
    // 2026-09-14): the touch panel has no
    // keyboard beside the synthetic one, so a HELD ARROW BUTTON is the panel's
    // only nudge run, and the substitutes the deletion counted on — dragging
    // the marker, typing the tempo in the editor — do not cover it.
    //
    // THE CADENCE IS THE KEYBOARD'S, not a constant of this table's: the first
    // fire one kHoldBeatMs after the press (the product's FIXED beat — never
    // the hold delay, a repeat delay being a cadence and not a hold —
    // so the button hold and the key hold cross their threshold on the same
    // beat) and
    // every later fire at THE COMPOSITOR'S OWN advertised key-repeat interval,
    // read per fire — a desktop with key repeat switched off gets no button
    // repeat either. The burst rides the armed ChromePress and its whole edge
    // inventory is at that struct (app_state.h); the firing body is
    // tick_chrome_press_repeat below. The physical arrow KEYS are untouched
    // throughout, repeat_eligible included — the arm SHARES that predicate
    // rather than mirroring it.
    // THE TWO SKIPS ADMIT CTRL (architect 2026-09-26): a CTRL-CLICK
    // dispatches Ctrl+Home / Ctrl+End, the WHOLE-PIECE jump, through
    // redesign_button_ctrl_admits (app_state.h) — on glass the S Pen's side
    // button held on the skip. They admit no shift, which is what keeps the
    // LONG PRESS — glass's held shift — off the act by construction, so a held
    // skip is the trim-bound jump a tap is. The rows below carry the PLAIN
    // chord, as every row does; the admission and its modifier are the
    // roster's, never a column here.
    {RedesignButton::TransportSkipBack,
     GuiKeys::Home,   false, false, false, false},                             // bare Home
    {RedesignButton::TransportPlayStop,
     GuiKeys::Space,  false, false, false, false},                             // bare Space
    {RedesignButton::TransportSkipForward,
     GuiKeys::End,    false, false, false, false},                             // bare End
    // THE SINGLE-MARKER VERBS (2026-08-12), the right block's first group
    // since the architect moved them down here on 2026-08-18: drop, delete,
    // disable toggle and inherit/collapse (FLATTEN stood among them for the
    // hours of 2026-09-19 and its row is in the icon row's iteration group
    // now) — authoring
    // chords whose refusals (read-only, the home view, an empty selection, an
    // occupied frame) are the keys' own
    // consumed no-ops, inherited whole through on_key. NOTHING IN THESE ROWS
    // CHANGED WITH THE LANE, AND THE GROUP TAKES ONE PAIR OF GATES WHATEVER
    // ITS MEMBERSHIP: the `h` view consumes every one of these chords outright
    // — none of them is on history_mode_key_blocked's allowlist — and greys
    // them through the derived partition, and the READ-ONLY lock greys them
    // too (2026-08-15). Both are the BUTTONS'
    // gates rather than the row's, so they are what makes this row's
    // otherwise-unconditional face policy have an exception at all.
    {RedesignButton::IconMarkerDrop,    GuiKeys::S,      false, false, false, false}, // bare s
    {RedesignButton::IconMarkerDelete,  GuiKeys::Delete, false, false, false, false}, // Delete
    {RedesignButton::IconMarkerDisable, GuiKeys::D,      true,  false, false, false}, // Ctrl+D
    {RedesignButton::IconMarkerInherit, GuiKeys::N,      true,  false, false, false}, // Ctrl+N
    // JUMP TO DEFINING MARKER (architect 2026-09-29), the verb group's FIFTH
    // since Edit Flag and Copy Value went up to the icon row that day: CTRL+J,
    // the jump to the marker the focused value came from on the other A/B tab
    // (jump_to_value_source) — Copy Value's shifted twin, Shift+`j`, until that
    // day, a dedicated button now under the dissolved shift-twin rule. Its row
    // carries the ctrl as Ctrl+D's and Ctrl+N's do; it ADMITS NO MODIFIER, so a
    // modified press is refused at the band gate and the long press reaches
    // nothing. NOT A RADIO AND NO LAMP (an act), and it does NOT repeat (a jump
    // has one destination). ITS GATES ARE NOT THE VERBS': both locks admit the
    // chord (a tab switch, a select, a land and two `c`s author nothing), so it
    // is lit on a locked tab and under grid iterations wherever the jump would
    // land (jump_to_value_source_actionable), while the `h` view consumes Ctrl+J
    // and greys it through the derived partition.
    {RedesignButton::IconJumpToDefiningMarker,
     GuiKeys::J,      true,  false, false, false},                             // Ctrl+J
    // ADD TO SELECTION (architect 2026-08-18), the verb group's SIXTH and last
    // since 2026-09-29, behind Jump to Defining Marker (the group's count has
    // moved around it with every arrival and departure; the succession is in
    // git history):
    // BARE `k`, which was free — he picked it over `n`
    // (already reading as INHERIT) and over `a` (too easy to hit by accident).
    // It is a MODE toggle, so the button's lamp reads the same bit the key
    // flips and button-is-its-chord holds literally: the press dispatches bare
    // `k` through on_key at the LIFT like every other chrome button, while the
    // KEY acts at the press like every other hotkey — both fall out of the
    // machinery, with no timing code of its own anywhere.
    //
    // ITS GATES DIVERGE FROM THE FOUR ABOVE IT and that is the point: the
    // READ-ONLY lock ADMITS it (read_only_key_blocked's allowlist) — a
    // selection is navigation, not authored content — and SO DOES THE `h`
    // VIEW SINCE 2026-09-17 (history_mode_key_blocked's own admission), where
    // the four above it stay grey: the lamp produces that view's own
    // multi-selection now, so this button is lit in there while they are not.
    // The face needs no arm for either fact — the walk below asks each gate
    // about this row's chord.
    {RedesignButton::IconAddToSelection,
     GuiKeys::K,      false, false, false, false},                             // bare k
    // THE MARKER-WALK GROUP (architect 2026-08-15), the row's right cluster
    // behind a separator and ahead of the arrows — FOUR ROWS since 2026-09-29
    // (architect): PREVIOUS MARKER, NEXT MARKER (the walk), CENTER and SWITCH
    // TAB. It was the walk alone from 2026-09-23 (the two HOLD-COLUMN nudges
    // stood beside it from 2026-09-22 and were deleted with their chords).
    //
    // PREVIOUS MARKER'S ROW CARRIES SHIFT+TAB AS ITS OWN BASE CHORD, Redo's
    // shape: a Shift- or Ctrl-modified chord may have its own dedicated
    // button since 2026-09-29 (architect, "an artificial constraint; the
    // tablet is the only real development surface now" — this button is
    // Next Marker's Shift form, Switch Tab below its Ctrl form), the walk's
    // shift admission standing beside it. It ADMITS CTRL (redesign_button_ctrl_admits), which the lift ORs
    // into the row's own shift: its ctrl press dispatches Ctrl+Shift+Tab, the
    // paired march — the S Pen's side button with a tap on glass. A shift
    // press on it is the consumed nothing a shift on Redo is, and its long
    // press stays Shift+Tab (it admits no shift for the hold to reach). It
    // does not repeat, as the walk does not.
    {RedesignButton::TransportWalkPrevious,
     GuiKeys::Tab,    false, true,  false, false},                             // Shift+Tab
    // The declined double-click
    // rule's mechanical reason is recorded at the roster entry (every
    // double-click surface in this product acts on its FIRST click too).
    // WALK BOTH TABS was a member on Ctrl+Shift+Tab until the architect
    // deleted it on 2026-09-14: the march is Switch Tab's shifted press
    // (below) and the walk's own ctrl-shift press
    // (redesign_button_ctrl_shift_admits).
    //
    // THE WALK IS ONE ROW (architect 2026-09-22, merging Previous marker —
    // whose row carried Shift+Tab as its own base chord, Redo's shape — and
    // Next marker): the row is bare Tab and SHIFT IS AN ADMISSION
    // (redesign_button_shift_admits), so the shift-click and the long press
    // dispatch Shift+Tab, the reverse walk — beside Previous Marker's own
    // row above, the same chord's dedicated button since 2026-09-29, when
    // the rule that a modified form never has its own button was dissolved.
    // CTRL AND THE PAIR ARE ADMISSIONS TOO (architect
    // 2026-09-26, redesign_button_ctrl_admits /
    // redesign_button_ctrl_shift_admits): a modified press spells the
    // button's own key, so the ctrl-click dispatches Ctrl+Tab, the tab
    // switch, and the ctrl-shift press Ctrl+Shift+Tab, the paired march — on
    // glass the S Pen's side button with a tap and with a long press. Switch
    // Tab's roads to the same two chords stand beside these. The reverse cycle's other spelling, IsoLeftTab, is
    // deliberately NOT a row: the dispatch is synthesized, so it goes out in
    // the Tab spelling every reader accepts. It does NOT repeat: its long
    // press is its shift (a held repeat would outrank it, ToolbarChord::repeats).
    //
    // IT IS LIVE INSIDE THE `h` VIEW and the derived partition says so with
    // nothing hand-listed — history_mode_owns_key claims bare Tab and
    // Shift+Tab as the diff-flag cycle forward and back (and Ctrl+Shift+Tab as
    // the diff-flag march; Ctrl+Tab switches the tab there as everywhere).
    //
    // ITS CAMERA IS THE LANDING OWNER'S (Viewport::land_subject), the same in
    // both audio views, so one row reaches the walk on glass. (The
    // least-movement walk's own row, an Alt Tab row from 2026-09-22, was
    // deleted 2026-09-23.)
    {RedesignButton::TransportWalk,
     GuiKeys::Tab,    false, false, false, false},                             // bare Tab
    // CENTER (bare `c`), moved here from the icon row's zoom group on
    // 2026-09-29 (architect) with its row unchanged: a momentary navigation
    // chord, no radio, no repeat. LIVE in the `h` view, `c` being that mode's
    // own vocabulary. IT ADMITS CTRL (architect 2026-09-29,
    // redesign_button_ctrl_admits): a modified press spells the button's own
    // key under that modifier, so its ctrl-click dispatches CTRL+C, COPY
    // RESOLVED VALUE — the copy's own refusals and card, the icon row's Copy
    // Value button's act exactly (on glass the S Pen's side button with a
    // tap). A shift press, and the long press, reach nothing: Shift+C binds
    // nothing.
    {RedesignButton::IconZoomOriginal,
     GuiKeys::C,      false, false, false, false},                             // bare c
    // SWITCH TAB (architect 2026-09-29): Ctrl+Tab, the one-shot other-tab
    // act, its row carrying the ctrl — but NOT A RADIO:
    // the chord toggles, so every press switches. It ADMITS SHIFT
    // (redesign_button_shift_admits) and nothing else: the shift-click and
    // the long press OR the shift into the row's ctrl and dispatch
    // Ctrl+Shift+Tab, the paired march. A ctrl press on it is refused at the band gate (strict
    // modifier validation — the button's chord already carries its ctrl). It
    // does not repeat: its long press is its shift.
    {RedesignButton::TransportSwitchTab,
     GuiKeys::Tab,    true,  false, false, false},                             // Ctrl+Tab
    // (THE HOLD-COLUMN NUDGES' ROWS — Ctrl+Left and Ctrl+Right, 2026-09-22 —
    // ARE DELETED with their buttons and their chords, architect 2026-09-23:
    // the held column is a posture an explicit centring arms, read by the
    // bare arrows below, AppState::camera_hold.)
    // The arrows, in their painted order since 2026-08-14 (the architect's:
    // down, up, left, right, replacing the row's original vim order). The
    // lookup is by id, so this order is for the reader alone. The eighth
    // column is `repeats`, which these four set — as do Undo / Redo since
    // 2026-09-13; the column IS the membership (the contract is at
    // ToolbarChord::repeats).
    //
    // THEIR MODIFIER COLUMNS STAY FALSE and that is the split these four make
    // visible (2026-08-31, THE STEP LADDER): the table's `shift` and
    // `ctrl` columns spell a row's OWN base chord (Redo's Ctrl+Shift+Z, the
    // Undo's Ctrl+Z), while an ADMISSION is a press-time modifier the
    // lift moves into the dispatched chord — and the ladder is the second
    // kind. The bare row here is the one-unit step; on Up / Down a
    // Shift-click dispatches Shift+arrow (ten units, the long stride since
    // 2026-09-21) and a Ctrl-click Ctrl+arrow (three), both through redesign_button_shift_admits /
    // redesign_button_ctrl_admits (app_state.h) and both carried into the
    // HOLD-REPEAT's own chord so a held modified press repeats its own step.
    // LEFT / RIGHT ADMIT NEITHER since 2026-09-21 (the horizontal ladder
    // retired; their one step is the active column's unit, a hop on P), and
    // Ctrl+Left / Ctrl+Right bind nothing since 2026-09-23 — the camera their
    // step takes is the hold posture's (AppState::camera_hold).
    // Setting a column here instead would make the modified form the
    // button's ONLY act.
    {RedesignButton::TransportDown,
     GuiKeys::Down,   false, false, false, false, true},                       // bare Down
    {RedesignButton::TransportUp,
     GuiKeys::Up,     false, false, false, false, true},                       // bare Up
    {RedesignButton::TransportLeft,
     GuiKeys::Left,   false, false, false, false, true},                       // bare Left
    {RedesignButton::TransportRight,
     GuiKeys::Right,  false, false, false, false, true},                       // bare Right
};

// THE TABLE IS TOTAL OVER THE ROSTER, ENFORCED AT COMPILE TIME (2026-08-06):
// every RedesignButton but the THREE menu anchors carries a chord here — the
// table's length plus the three anchors is kRedesignButtonCount, and the
// static_assert below is the check, so no number is restated here
// (kRedesignButtonCount carries the split; the count's succession is in git
// history). It is keyed by id and every
// reader matches by id or by published rect, so the row order it is kept in
// is for the reader alone; a button moving BETWEEN ROWS moves no count, and
// an anchor carries no chord, so it moves the roster and not this table —
// the table's length plus THE THREE ANCHORS IS the roster. The check is not
// bookkeeping — history_mode_disables_button walks this table and DEFAULTS AN UNLISTED BUTTON
// TO LIVE, so a roster entry added without its row here would silently wear a
// live face in the `h` view while its press claimed nothing. This makes that
// drift a build error instead. (The addend has walked the anchor count: + 2
// until 2026-08-13, when the Quit button became the File menu's one item — the
// roster's total did not move, the split did — then + 3 with Edit
// 2026-08-20, + 4 with Iterations 2026-08-27, + 5 with Help 2026-09-03,
// back to + 4 on 2026-09-04, when the Iterations anchor was deleted and its
// two commands became chord rows here, and + 3 since 2026-09-09, when the
// Help anchor was deleted with no chord row moving.)
static_assert(std::size(kToolbarChords) + 3 ==
                  static_cast<std::size_t>(kRedesignButtonCount),
              "kToolbarChords must cover every RedesignButton except the "
              "File, Edit and Settings anchors");

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
// VEIL IS BEHAVIOURAL AND PAINTS NOTHING (architect 2026-08-12): no dim layer
// — the palette is fully opaque and kdenlive's own parent window stays
// undimmed under its dialogs — and the cursor is the Arrow over it. THE
// KEYBOARD CONTRACT IS UNTOUCHED — route_modal_editor_key still admits bare
// Esc, Ctrl+S and Ctrl+Q while an editor stands, which is where that pair's
// pointer-side mirror note used to point.)
//
// THE NOTIFICATION CARDS ARE HIT ABOVE THE VEIL, BY RULING, AND ARE NOT AN
// EXCEPTION TO IT (architect 2026-08-29): a card is not a reach into the
// veiled surface — it is the message about the act the veil stands over —
// so the card (its X until 2026-10-01) must answer under a prompt, the
// player, the picker and every dialog editor alike. The claim sits at
// on_button_press's head, ahead of every gate, and its lift at
// on_button_release's (the rule at notifications.h).

// Is (x, y) inside the PUBLISHED INTERACTION RECT of a redesigned button? The
// rect is the painter's stash and nothing here re-shapes or re-measures, so the
// pointer reads exactly what the painter published, which is the box it drew
// — and where the icon row's overflow rule clips a member under the view
// group, only the columns it painted (paint_icon_row). A zero rect (before
// that row's first paint, or a member the overflow hides whole) contains no
// point, which is the correct answer.
bool redesign_button_hit(const AppState& app, RedesignButton id, int x, int y) {
    return rect_contains(
        app.redesign_buttons[redesign_button_index(id)].rect, x, y);
}

// IS THIS PRESS ON A MENU ANCHOR THE STANDING MODE LEAVES LIVE? THE ONE
// EXEMPTION the folder overlay's two veils carry (architect 2026-09-03
// evening: "leave File open, because Quit should still be enabled —
// everything else like what we do with history ... Leave that for the player,
// the picker and the AV stats" — the AV Sync Stats panel, deleted
// 2026-09-30). While a content stands, the menu row shows
// above the band and File alone answers on it, so its press has to reach the
// menu-row claim at the foot of on_button_press instead of dying in the veil
// — and this predicate, ONE OWNER read by both veils, is where that is
// said. It walks kDropdownMenus like every other anchor road here and asks
// each anchor's PAINTED face (RedesignButtonFace::enabled, which
// publish_button_face stamps from menu_anchor_live) — the claim reads what is
// on screen (architect 2026-09-24, strictly as-painted), the per-tick
// comparator keeping it true — so the exemption cannot part from the FACE or
// from toggle_dropdown's guard, which reads the same bit: what is lit is
// pressable and what is grey is veiled, one enumeration behind both.
bool press_on_live_menu_anchor(const AppState& app, int x, int y) {
    for (const DropdownMenu m : kDropdownMenus) {
        const RedesignButton b = dropdown_anchor_button(m);
        if (!app.redesign_buttons[static_cast<size_t>(
                 redesign_button_index(b))].enabled) continue;
        if (redesign_button_hit(app, b, x, y)) return true;
    }
    return false;
}

// THE FOUR BAND CLAIMS' ONE MODIFIER GATE — "does this modified press spell a
// roster chord at all", asked before the arm so a press that spells nothing
// stays the strict consumed no-op it has always been. One body for all four
// rows, the band-claim shape's own rule rather than a row's.
//
// ALT IS REFUSED EVERYWHERE: the roster has no alt chord, and alt's pointer
// vocabulary is empty since 2026-09-14, when the Alt+wheel stepped pan went
// (the alt vocabulary at chord_is_bound, gui_input.h).
//
// CTRL BINDS ONLY WHERE redesign_button_ctrl_admits SAYS SO (app_state.h) — the
// two SKIPS, whose ctrl-click is Ctrl+Home / Ctrl+End, the Up / Down step
// ladder's Ctrl rung, the WALK's Ctrl+Tab, PREVIOUS
// MARKER's Ctrl+Shift+Tab (its own Shift+Tab under ctrl, 2026-09-29), CENTER's
// Ctrl+C and RESTRICT UNDO's Ctrl+Z (2026-09-29) — which is why
// the gate asks the BUTTON under the pointer rather than the band: the
// admission is the roster's, so a ctrl press anywhere else, on the bare ground
// of any row, or on one of the three dropdown anchors (which carry no chord row
// at all) is refused exactly as it always was. The walk is kToolbarChords in the
// arm's own order, so the button this answers about is the button that would
// arm.
//
// CTRL+SHIFT TOGETHER BIND ONLY WHERE redesign_button_ctrl_shift_admits SAYS
// SO (app_state.h) — THE WALK, whose ctrl-shift press is its own key under
// both modifiers, Ctrl+Shift+Tab, the paired march (architect 2026-09-26),
// and RESTRICT UNDO, whose ctrl-shift press is Ctrl+Shift+Z, redo (architect
// 2026-09-29). On every other button the pair spells no roster chord — strict modifier
// validation, and Ctrl+Shift+Up is unbound on the keyboard too — so it dies
// here and the lift's chord build never sees it there. The gate asks the
// roster, never a named button.
//
// A SHIFT press is deliberately NOT judged here: its admission is a press-time
// refusal of the arm body's (arm_redesign_press), where a non-admitting button
// consumes the press rather than letting the band answer for it. A CTRL+SHIFT
// press that passes here is the pair's admission and asks no shift-alone one:
// Restrict Undo admits the pair while Shift+Z binds nothing
// (redesign_button_shift_admits_with, app_state.h).
bool chrome_band_modifiers_refused(const AppState& app, int x, int y,
                                   GuiInputState mods) {
    if (mods.alt) return true;
    if (!mods.ctrl) return false;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_hit(app, tc.id, x, y))
            return mods.shift ? !redesign_button_ctrl_shift_admits(tc.id)
                              : !redesign_button_ctrl_admits(tc.id);
    }
    return true;
}

// Does the roster button at this index paint a pressed interior? Every chord
// row does (the struct's note), so the answer is "it has a chord row": false
// for the three anchors alone. The damage gate for the arm's writers (a face
// that is never painted owes no erase).
bool roster_index_click_face(int index) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (redesign_button_index(tc.id) == index) return true;
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

// THE TOP STRIP'S TWO PLACEMENT LANES — the RULER and the MARKER lane — the
// band owner (a point is in the lanes iff it is in the top strip AND in either
// lane's y-band). THEY LEFT THE NAVIGATION SURFACE ON BOTH DEVICES (architect
// 2026-09-25: "out of both — we want symmetry as much as possible; the
// horizontal zoom ... was designed to unify the motions for zoom on both
// devices"): off a flag, a motionless click or tap there places the playhead
// and the marker lane's empty-stretch double-click creates, and EVERY DRAG
// DOES NOTHING — no grab-pan, no ctrl zoom, no shift sweep. The FLAG BOXES
// carve themselves out (point_on_placement_lanes below, and the band walks'
// own flag claims), and the TRIM BAR is a disjoint y-band that never answers
// true here (it has its own claim and its own cue). Deliberately NOT the
// flexible GAP 1 band above the menu row — that ground is the row's own
// chrome, not surface, and stays inert.
bool point_in_placement_lanes(const AppState& app, int x, int y) {
    if (!rect_contains(top_strip_area(app), x, y)) return false;
    const GuiRect ruler = top_ruler_row_area(app);
    if (y >= ruler.y && y < ruler.y + ruler.h) return true;
    const GuiRect lane = top_marker_row_area(app);
    return y >= lane.y && y < lane.y + lane.h;
}

// THE PLACEMENT SURFACE, the navigation surface's sibling (architect
// 2026-09-25): the two placement lanes off any flag, the flag boxes carved out
// through the painter's published rects (a flag is lane vocabulary — select /
// range / toggle / drag). A press here arms the pending with placement_only
// (arm_placement_press): the motionless release places, a drag does nothing.
// TWO READERS, by grep 2026-09-25: the live press router's SHIFT claim and the
// `h` view's shift claim, each keeping the shift CLICK's placement on the
// lanes while the sweep stays the waveform's. The PLAIN arms reach the same
// pending through the band walks (the ruler band and the marker lane's empty
// stretch in on_button_press and handle_history_mode_press), because those
// also pick the lane double-click and the empty-lane seed; the CURSOR MAP
// needs no read — a click carries no cue anywhere, so the lanes fall to its
// top-strip Arrow; and the TOUCH PAN ZONE leaves them by construction, reading
// point_on_nav_surface.
bool point_on_placement_lanes(const AppState& app, const GuiAudio& audio,
                              int x, int y) {
    if (!point_in_placement_lanes(app, x, y)) return false;
    return hit_test_flag(app, audio, x, y) < 0;
}

// THE NAVIGATION SURFACE, THE ONE OWNER OF ITS GEOMETRY: THE WHOLE WAVEFORM
// AND NOTHING ELSE — both halves (architect 2026-08-13, THE WAVEFORM'S TWO
// HALVES BECOME ONE), in every view, the `h` history view's surface and the
// live views' being the same rect. The ruler and the marker lane's empty
// stretches were its lane members from 2026-08-12 until 2026-09-25, when they
// became placement surfaces (point_on_placement_lanes above). On it plain
// drag = grab-pan, motionless click = the half's act, shift+drag = the sweep,
// ctrl+drag = the zoom.
//
// The waveform BAND spans the FULL WINDOW WIDTH (top.w), not the effective
// width: the <=15 px inert right gutter counts as waveform by the user's
// lights, so a press there arms the pan and its click act deselects while
// seating nothing (the gutter is 0 px at 1920/2560/3840, so it only matters
// off-deployment). The TRIM BAR, the lanes and the flexible GAP band are
// outside it.
//
// FIVE READERS, re-derived by grep 2026-09-25: the press router's SHIFT sweep
// claim, its CTRL zoom claim, the pointer cursor map's Pan/Zoom zone, the `h`
// view's own press router, and the TOUCH PAN ZONE (touch_point_in_pan_zone,
// the one-finger pan surface by ruling, which must not drift from the
// mouse's — so a finger on the lanes resolves to the pointer translation,
// where a tap places and a drag does nothing). The plain press's own arm is
// the band walk in on_button_press rather than this predicate, because it
// also has to pick the release act.
bool point_on_nav_surface(const AppState& app, int x, int y) {
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    return x >= area.x && x < top.x + top.w &&
           y >= area.y && y < area.y + area.h;
}

// Active-domain playhead frame at click column `col`: the single-rounding
// display grid (displayed_grid_position_at_column via painter q) in BOTH views,
// so a click, a 1px step and the marker commits agree on ONE lattice per view
// and a drop-at-playhead lands where a drag or a nudge would. The target branch
// used to take the domain-spp form on the reasoning that the source-frame commit
// routes through the inverse map and so carried no source-grid claim; the
// target-domain lattice is an authoring lattice in its own right (the
// phase-reset drop commits the playhead's sample, and
// authored_frame_at_column_on_basis's target arm rides this same grid), and an
// unanchored landing relabels by a frame across a pan or a zoom round trip.
// THE VIEWPORT IS THE ITEM BASIS (architect 2026-09-24, strictly as painted —
// "reality to the face" reads the same for a click as for a drag):
// item_viewport_basis's integer vp_start_frame and its spp, the span the
// displayed items were painted on, so a click converts its column against the
// picture ON SCREEN and never against the live viewport a viewport-dispatched
// worker job in flight at the press (a wheel pan or a zoom gesture, then a quick
// click) has already moved ahead of the pixels. The integer start is the
// grid's own input, the shape trim_mouse_x_to_active_frame takes on the same
// basis. Cold, the basis is the live viewport by its own contract. This is
// the whole click-placement family's one conversion — the nav click act
// (place_playhead_at_click_column, run_nav_click_act), the sweep's PLAYHEAD
// half at the press and in motion (and so the touch region hold), the
// scrub click (scrub_press_at) and the empty-lane double-click create — so
// every one of them lands on the painted grid the gesture family's
// conversions ride; the 2026-07-25 live-viewport ruling for this family is
// repealed.
// The fallback covers degenerate geometry only (no strip width / no zoom), where
// there is no painted grid to land on.
int64_t playhead_frame_at_click_column(const AppState& app,
                                       const GuiAudio& audio, int col) {
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp > 0.0)
        return static_cast<int64_t>(std::llrint(
            displayed_grid_position_at_column(basis.vp_start_frame, col,
                                              basis.spp)));
    return basis.vp_start_frame;
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
    // true = one of the three DIALOG editors (settings / commit-title /
    // BPM, painting in the BOTTOM ROW'S modal since
    // 2026-08-13 — centered for the one day from 2026-08-12, and the field
    // was `bottom_strip` while they lived on the status lane); false = the
    // top-strip flag editor. Selects the claim
    // region and the repaint owner.
    bool                dialog       = false;
    // THE FIELD'S ONE RECT — where a press seats a caret: the dialog's inset
    // field (modal_dialog.field, the painter's published rect; the box
    // around it is chrome) or the marker lane's whole published box
    // (flag_editor_box.box — the unrolled flag under the payload editor, the
    // cell under a bound editor, pads included: the box is
    // the field, and a click in its padding puts the caret at the nearest
    // end as clicking a text field's margin does). The press claim and the
    // touch translation's editor-field query both read this, so "in the
    // field" has one spelling for the mouse and the finger.
    GuiRect             field{0, 0, 0, 0};
};

ActiveEditorText active_editor_text(AppState& app, const GuiAudio& audio) {
    (void)audio;
    ActiveEditorText g;
    // THE THREE DIALOG EDITORS share ONE publication — only one of them is
    // ever open, and paint_modal_dialog fills it from whichever editor it
    // actually painted. An invalid publication (nothing painted yet, or an
    // editor the dialog's precedence hides — a prompt is up) leaves this
    // invalid, exactly as the flag
    // editor's does: what is not on screen takes no clicks.
    const AppState::DialogEditorText& be = app.dialog_editor_text;
    const bool dialog_open =
        text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_title_editor) ||
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
    if (dialog_open) {
        if (!be.valid) return g;
        g.ed = text_editor::is_active(app.settings_editor)
                   ? &app.settings_editor
             : text_editor::is_active(app.commit_title_editor)
                   ? &app.commit_title_editor
                   : &app.top_flag_editor;
        g.text_left    = be.text_origin_x;
        g.byte_x       = &be.byte_x;
        g.dialog       = true;
        g.field        = app.modal_dialog.field;
        g.valid        = true;
        return g;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // THE MARKER LANE'S OWN FIELD — the UNROLLED FLAG BOX under the payload
        // editor, the CELL under a bound editor. KIND-BLIND
        // deliberately: all publish through the one painter into the one
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
        g.field     = fb.box;
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

// THE ONE POINTER SEAT: every press and every drag motion that puts the caret
// under the pointer comes through here — the plain press, the shift press, the
// mouse's selection sweep and the finger's caret drag (the touch trio's
// bodies below) — and it restarts the blink phase as the keyboard's own
// motion keys do, so a caret being dragged is lit where it lands rather than
// possibly caught in the dark half of a period. The one seat that is NOT a
// pointer byte — the double-click-drag's word boundary — goes through
// text_editor::extend_selection_by_words instead, which blinks for itself.
void set_editor_caret_from_x(const ActiveEditorText& g, int mouse_x) {
    g.ed->cursor_pos = editor_byte_index_at(g, mouse_x);
    text_editor::touch_blink(*g.ed);
}

// IS THIS PRESS THE EDITOR'S DOUBLE-CLICK'S SECOND HALF — a candidate on the
// editor's text, inside the double-click window and the scaled slack of the
// seeding release? ONE predicate for the two readers that must agree: the
// press arm's double-click test, and the touch translation's editor-field
// query, which asks it at a finger's down so the second press can be
// delivered on contact (the third clause: the seed makes the press certain).
bool editor_double_press_at(const DoubleClickCandidate& dc, int x, int y) {
    return dc.surface == DoubleClickSurface::EditorText &&
           monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
           std::abs(x - dc.press_x) <= double_click_slack_px() &&
           std::abs(y - dc.press_y) <= double_click_slack_px();
}

} // namespace

// THE `h` HISTORY VIEW'S DEAD SET — the roster's ONE mode-scoped disabled-face
// partition, and THE AUTHORITATIVE INVENTORY of it (every other site carries a
// pointer here). The architect's principle, 2026-08-04: while the view stands,
// EVERY BUTTON WHOSE ACT THE VIEW CONSUMES WEARS ITS ROW'S DISABLED FACE and
// ignores the pointer, and the ones that stay lit are exactly the ones that
// still work. It is the mode-scoped exception to the icon row's never-grey rule
// and to rows 1 and 3 having no disabled face.
//
// IT IS DERIVED, NOT LISTED. Each roster button but three IS a chord
// (finish_chrome_press_release synthesizes it and calls on_key at the lift),
// so "the view
// consumes this button's act" is exactly "the view's keyboard gate consumes this
// button's chord" — this walks kToolbarChords and asks that gate. So the faces
// cannot drift from the allowlist: admit a chord there and its button lights on
// the next frame with nothing to remember here.
//
// THE ANCHORS ARE THE HAND-ANSWERED ENTRIES, AND TWO OF THE THREE ARE DEAD
// (re-derived 2026-09-09, when the Help anchor was deleted; it was four of
// five from 2026-09-03, one dead of two until 2026-08-20, two of three until
// 2026-08-27, and one of three from 2026-08-08). The anchors are the
// roster's only NON-chord actions, so there is no chord to ask the gate
// about and each has to be answered — through the ONE owner the face is
// painted from (toggle_dropdown claiming on that painted face),
// menu_anchor_live (app_state.h), which is where the
// per-menu reasoning lives, and which redesign_button_enabled asks at its
// head, AHEAD of this partition (2026-09-24), so this walk is never asked
// about an anchor. In short:
// SETTINGS is dead because its rows reach a modal by a DIRECT call the view
// has no place for; EDIT because every one of its rows is a chord the view's
// allowlist drops (ITERATIONS and HELP answered the same way while they
// stood); FILE (2026-08-13) is LIVE — its three rows are Ctrl+O, Ctrl+Alt+O
// and Ctrl+Q, all admitted, so its menu works in there.
// (NAVIGATION was a third entry, LIVE from 2026-08-08 — the architect ruled its
// menu open in the view, the toggle stopped refusing it, and every one of its
// seven rows was a chord that met the mode's own gates through on_key, so
// nothing about it was a hand answer beyond this function's silence on it. The
// one row the view consumed greyed at the ITEM instead, a surface this
// partition does not reach: it answers about BUTTONS, and a menu row is not
// one. The anchor and its menu are deleted 2026-08-15, and the item-grey
// predicate went producer-less with them.)
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
// row's zoom-original (2026-08-12; the bottom row's walk group's since
// 2026-09-29), and bare Tab / Shift+Tab / Ctrl+Shift+Tab
// the bottom row's MARKER-WALK GROUP (2026-08-15; Shift+Tab a dedicated
// Previous Marker again since 2026-09-29). Every one of them is a chord
// the MODE OWNS, which is exactly what makes this walk answer LIVE for its
// button with nothing hand-listed — the same free derivation the walk's two
// arrows, the Cumulative toggle and Revert already ride. (The membership is
// re-greped here rather than inherited: this paragraph carried "nothing in
// kToolbarChords dispatches bare Tab, Home, End or `c`" for three roster
// growths after it stopped being true.)
//
// TWO ENTRIES ARE A FUNCTION OF THE SESSION, not of the chord alone
// (2026-08-05, both of them), which is why this takes the state rather than
// only a button.
// THEY WERE THREE UNTIL 2026-09-01: the allowlist admitted Ctrl+S — the
// checkpoint act's chord since 2026-08-08 — only while there was something to
// checkpoint and no checkpoint in flight, and the Save-and-Commit-faced SAVE
// button took its grey from that admission through this walk. The architect
// ruled a gate's membership the CHORD'S ALONE that day (a state term there made
// the gate answer the view's own save chord with "not available in the history
// view"), so Ctrl+S is admitted outright, this walk answers LIVE for Save, and
// the same two terms reach the face from redesign_button_enabled's own Save arm
// through history_checkpoint_actionable (app_state.h) — the identical grey off
// the act's own predicate. It admits BARE `v` — the revert's chord since
// 2026-09-01, Ctrl+H before it — only while a diff flag is selected on a
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
// cadence and that is the honest difference: the active walk's count grows as
// the prefetch streams and the revert subject moves with every click, while the
// terms that left this walk in 2026-09-01 sit at the other end of it — the head
// delta measured once (at the entry, or at the drain that answers it) and
// static for the visit, the in-flight bit falling when the worker reports.
//
// THE PARTITION THIS PRODUCES, in full (verified against the roster both ways,
// 2026-08-04, re-verified 2026-08-05, re-derived 2026-08-13 when the Quit button
// left the roster for the File menu — its LIVE entry is now the FILE ANCHOR's,
// hand-answered with the ONE other anchor, and the Ctrl+Q admission it rested
// on is unchanged; the hand entries were three until the Navigation anchor left
// with its menu on 2026-08-15):
//   LIVE — the icon row's view group, ViewSW/ViewTW/ViewTP (bare 1/2/3, the admitted
//   view selectors), Save (Ctrl+S, which in this mode IS the
//   save-and-commit checkpoint act and wears the "Save and Commit" face — LIVE
//   FROM THIS WALK SINCE 2026-09-01, when the chord's two session terms left the
//   allowlist for the act, its own arm in redesign_button_enabled greying it
//   with an empty head delta or a checkpoint in flight exactly as this walk used
//   to, and greyed rather than relabelled in either case; it was RENDER's chord
//   and RENDER's face until 2026-08-08, when the act moved onto the save it
//   begins with),
//   FULL ZOOM OUT AND CENTER since the 2026-08-12 relayout (bare `0` is the
//   allowlist's own zoom admission and bare `c` is the mode's vocabulary —
//   pure navigation, live with nothing hand-listed; Center on the bottom
//   row's walk group since 2026-09-29, beside Previous Marker and Switch Tab,
//   whose Shift+Tab and Ctrl+Shift+Tab are the mode's own and whose Ctrl+Tab
//   is on its allowlist),
//   the load-editor opener (bare `'`, which in this mode loads THE
//   VIEWED WALK'S MEMBER in place — the commit's sidecars on the Remote tab,
//   the timeline state on the Local one since 2026-08-08, and live on both:
//   a session-dependent entry, dead on a walk with no member — the
//   Remote tab's empty one, since 2026-08-09), and the history button itself
//   (bare `h`, the
//   mode's own key,
//   selected while it stands),
//   and THE WALK LAMP since 2026-08-18 (bare `g`, the mode's own vocabulary; a
//   radio pair until the 2026-09-04 collapse),
//   and THE WALK'S TWO STEPS since 2026-08-05 — older (bare `,`) and newer
//   (bare `.`), the mode's own vocabulary again, so this walk answers LIVE for
//   them with nothing hand-listed,
//   and TWO OF THE BOTTOM ROW'S MARKER-WALK GROUP since 2026-08-15 (bare Tab
//   and Shift+Tab) — the mode's own vocabulary once more, so both answer LIVE
//   for free and step the DIFF-FLAG cycle forward and back — WITH THE GROUP'S
//   THIRD SINCE 2026-08-18 (Ctrl+Shift+Tab, the mode's own vocabulary too now:
//   it marches the pair over that same diff-flag cycle, "tab, ctrl+tab, tab"
//   read against the lane in here, a round trip through the other tab since
//   2026-09-26; it ran the reverse WALK-SOURCE cycle from
//   2026-08-07 until the walk moved to the icon row's own radio pair, and was
//   blocked for the hours between) — AND SINCE 2026-09-29 THE GROUP'S
//   DEDICATED ROADS: PREVIOUS MARKER (Shift+Tab, the mode's own, its ctrl
//   press the mode's march) and SWITCH TAB (Ctrl+Tab, on the allowlist, its
//   shifted press the mode's march), both LIVE for free,
//   and THE BOTTOM ROW'S SKIPS and THE ZOOM-ORIGINAL button (Center, the
//   walk group's third since 2026-09-29) on the same terms
//   (bare Home / End are the mode's absolute jumps, bare `c` its own centring),
//   and THE BOTTOM ROW'S LEFT / RIGHT since 2026-09-26 (bare Left / Right, the
//   mode's own playhead step; their own arm greys them over a focused diff
//   flag and at the wall, horizontal_arrow_step_actionable),
//   and THE CUMULATIVE TOGGLE since 2026-08-08 (bare `u`, the same vocabulary
//   and the same free answer). Those three plus Revert are the roster's
//   RESTING-DISABLED family — four of them, joined by the WALK LAMP on
//   2026-08-18 (a radio pair then) for five, all five gated at rest on
//   `history_mode.active` by their
//   own arm in redesign_button_enabled (which carries the succession, the plain
//   `true` of 2026-08-15..18 included). This walk's answers for them have never
//   changed and this paragraph is still what says they ACT in here; what the
//   companions' own arm does is answer the RESTING face this partition cannot
//   reach and, since 2026-08-30, the walk's two WALLS for Older / Newer
//   (which this partition cannot see either, both chords being the mode's own
//   vocabulary) — the lift that kept the four over this partition INSIDE the
//   view from 2026-08-15 is deleted (see the note on Revert below).
//   and THE REVERT ACT since 2026-08-05 (bare `v`, Ctrl+H until 2026-09-01) —
//   the SECOND session-dependent
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
//   derived: an anchor has no chord to ask about, so the anchor arms
//   answer ONE criterion by hand — an anchor is dead iff every row of its menu
//   is dead — and File is the one that answers it the other way. ITS MENU IS
//   THREE ROWS NOW AND ALL THREE ARE LIVE IN THE VIEW (architect 2026-08-29,
//   "admit both"): Ctrl+Q always was, Ctrl+O joined the mode's allowlist that
//   day, and Revert's Ctrl+Alt+O beside it at its 2026-09-13 landing. Open was
//   a consumed nothing from its 2026-08-28 landing until that ruling, the state
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
//   opener (Ctrl+B), iteration mode (bare `i`), listen
//   (bare `l`); the FOUR MARKER VERBS since
//   the 2026-08-12 relayout (bare `s`, Delete, Ctrl+D, Ctrl+N — authoring,
//   consumed like the rest, and unmoved by their 2026-08-18 change of ROW:
//   this walk asks about a chord, never about a lane) — ADD TO SELECTION,
//   their row's sixth, is NOT here: bare `k` is on the view's allowlist
//   since 2026-09-17, the lamp producing the view's own multi-selection, so
//   it answers LIVE; the icon row's SETTINGS BUTTON (bare `;`, since
//   2026-09-29 — off the mode's allowlist, as the typed road always was); and the
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
// A DEAD ANSWER IS PAINTED GREY WHERE IT IS A STATE, AND NOT PAINTED WHERE
// IT IS THE CHORD'S (architect 2026-10-07, partly reversing 2026-08-14's "no
// more hiding/showing icons in top icon row"): a button this walk answers
// DEAD in EVERY session — its chord never admitted in the view — is absent
// from both toolbars while the view stands (history_mode_hides_button,
// below, the same walk asked about every state), and a button it answers
// dead only for THIS session (Revert, Load in Place — the two conditional
// admissions) wears the grey face. ONE SWAP STANDS beside it (architect
// 2026-10-05): in the `h` view the history companions stand in the icon
// row's slots of the two groups THIS WALK answers DEAD whole — Undo's (Undo,
// Redo, Copy Value, Render) and the iteration group — and outside it the
// companions do not stand (kIconRowHistoryStandIns, paint_handler.cpp,
// which names this walk as the membership's derivation). A change here that
// answers one of those seven LIVE in the view is a change to that table too.
//
// THE ONE CHORD WALK BOTH `h` ANSWERS TAKE (architect 2026-10-07): the grey
// (history_mode_disables_button) asks the allowlist about THIS session, the
// hide (history_mode_hides_button, below) about EVERY session
// (history_mode_key_blocked_in_every_state, input_key_dispatch.cpp, the
// allowlist's same body); the mode's own vocabulary answers LIVE for both.
static bool history_mode_refuses_button_chord(const AppState* state,
                                              RedesignButton b) {
    // THE THREE ANCHORS ARE NOT THIS PARTITION'S (2026-09-24): their one
    // verdict, menu_anchor_live (app_state.h), answers them at the head of
    // redesign_button_enabled, ahead of the `h` test that reaches this walk —
    // its mode clause is the partition this arm used to carry (Edit and
    // Settings dead in the view, File live), and the item clause joined it.
    // The arm stays so an anchor, which carries no chord for the walk below
    // to ask about, is never answered by that walk's LIVE default; it names
    // no set — and so no anchor is ever hidden either: the menu row is not a
    // toolbar, and a dead anchor greys as it always did.
    if (redesign_button_is_menu_anchor(b)) return false;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (tc.id != b) continue;
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift;
        chord.alt   = tc.alt;
        if (history_mode_owns_key(tc.key, chord)) return false;
        return state != nullptr
                   ? history_mode_key_blocked(tc.key, chord, *state)
                   : history_mode_key_blocked_in_every_state(tc.key, chord);
    }
    // Not in the table and not an anchor: nothing to consume. Unreachable today
    // (the table plus the three anchors is the whole roster) and stated rather
    // than asserted, so a future button defaults to LIVE — the face it already
    // had — instead of greying on a chord nobody has written yet.
    return false;
}

bool history_mode_disables_button(const AppState& app, RedesignButton b) {
    return history_mode_refuses_button_chord(&app, b);
}

// IS THIS BUTTON ABSENT FROM ITS TOOLBAR? (architect 2026-10-07: "a group
// stays if any of it is live in h; not every button must be live" — the ask
// being row 8's state line, which had no room for the walk line's text.) True
// iff the `h` view stands and the button's act is DEAD THERE BY
// CONSTRUCTION: its chord is neither the mode's own vocabulary nor admitted
// by the allowlist in ANY session. A button the view refuses only BY STATE —
// Revert with no diff flag selected, Load in Place over a memberless walk
// (the allowlist's two conditional admissions), and every arm of
// redesign_button_enabled's own (Older / Newer at the walk's walls, Save with
// nothing to commit, Left / Right at the wall, a skip at the end) — is
// painted and greys as it always did.
//
// THE SAME TABLE AS THE GREY, so the two can never disagree: hidden implies
// greyed (a chord refused in every state is refused in this one), a chord
// the view starts admitting shows its button, and one it stops admitting
// hides it, with no list here to keep. Read by both toolbars' walks
// (paint_icon_row, paint_bottom_row_buttons_and_clock), which publish the
// hidden button's EMPTY RECT — no point is inside it, so no press, hover or
// tooltip reaches it, and its face bits refresh unconditionally
// (publish_button_face) — close its neighbours up, and drop a group, with
// its separator and gap, only when every member is hidden. The icon row's
// two groups the view refuses whole are swapped for the history stand-ins
// rather than dropped (kIconRowHistoryStandIns, paint_handler.cpp); a
// stand-in's chord is the mode's own or an admission, so no stand-in is
// ever hidden. The keyboard is untouched: the chord is refused by the
// allowlist exactly as before. Outside the view nothing is hidden.
bool history_mode_hides_button(const AppState& app, RedesignButton b) {
    return app.history_mode.active &&
           history_mode_refuses_button_chord(nullptr, b);
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
// collapse. What a mode refuses GREYS, through `history_mode_disables_button`
// above and the enabled predicate that reads it, which is the convention
// every other row already used — save in the `h` view, where since
// 2026-10-07 a button refused in every session is not painted
// (history_mode_hides_button, above: one chord walk, no owed-separator
// machine). The group boundaries stayed — `redesign_button_opens_icon_
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
// THE LAND IS A MOVEMENT OWNER: it ENDS THE A/B AUDITION, and that is the
// land's own act rather than every caller's. The act ends when the playhead's
// position in the music changes — so the two functions that change it are the
// two that end it, this one and Viewport::move_playhead_to (the rule and its
// exemptions at Viewport::move_playhead_to's definition, viewport.cpp; the
// RESEAT entry points serve the callers whose write is not a movement). What is
// worth stating at the land is only this: the end sits ABOVE the write, so it
// is unconditional even where the seat early-returns. (It also hid the trim
// region overlay from 2026-08-19 until that overlay's resting form was deleted
// on 2026-09-22.)
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
//     since 2026-08-19: a no-op write must not carry the land's audition end;
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
//     not a movement, so it must not end an A/B audition. The undo/redo restore
//     rejoined that family on 2026-08-28 — through the RESEAT, on the cursor's
//     own instant rather than on a focus — and since 2026-09-22 takes the
//     reseat's no-scroll twin, Viewport::translate_playhead_to; nothing about
//     this list changes.)
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
// the ops/views TUs can reach it. THE `p` SWAP LEFT THIS LIST 2026-08-19: a
// column switch lands nothing (the swap empties the selection, so there is no
// focus to re-express).
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
    // THE A/B AUDITION ENDS ON THIS MOVEMENT — a land is the marker-shaped
    // spelling of "the playhead's position in the music changes", and the
    // act's premise is a resting cursor that cannot move under it. Here in
    // the entry point rather than in the shared seat below, because
    // reseat_playhead_on_marker shares that seat and its land is a
    // TRANSLATION or a provable no-op, neither of which is a movement. It sits
    // ABOVE the store lookup and the frame half's idempotence return, so a
    // land that seats nothing still ends the act. A class statement: the
    // complete clearing-owner inventory is at GuiAuditionSequence
    // (app_state.h).
    clear_audition_sequence(app);
    // AND THE HOLD POSTURE GOES OUT WHEN THE LAND MOVES THE PLAYHEAD
    // (architect 2026-09-26): one of its three movement-owner clears (the rule
    // and its two exemptions — the nudge's and the undo/redo restore's — are
    // at AppState::camera_hold). Unlike the audition's end it is gated on the
    // seat's own outcome, read after the frame half's clamp and idempotence
    // return: a land onto the sample the cursor already holds (an editor
    // opening on the marker under the playhead — Enter, the flag
    // double-click, a bound cell, `m`) leaves the column the centring asked
    // for where it stood, so the hold stands.
    const int64_t sample_before = app.playhead_cursor_sample;
    reseat_playhead_on_marker(app, audio, viewport, hit);
    if (app.playhead_cursor_sample != sample_before) app.camera_hold = false;
}

// THE MARKER RESEAT — the same store lookup and the same write with NO
// audition end, for
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
//     tab and column entries, which are restores and switches, not
//     movements.
// Named rather than spelled as a flag on the land: a parameter meaning "skip the
// rule this time" is the hand-listed inventory in disguise.
void reseat_playhead_on_marker(AppState& app, const GuiAudio& audio,
                               Viewport& viewport, int hit) {
    // Both columns through the one store selector pair (active_marker_count
    // / active_marker_time_frame, app_state.h).
    if (hit < 0 || hit >= active_marker_count(app)) return;
    seat_playhead_on_source_frame(app, audio, viewport,
                                  active_marker_time_frame(app, hit));
}

// The frame-shaped half, above — the MOVEMENT owner's frame form: the
// audition's end, then the seat. Every caller of this one is a command whose
// subject is the playhead (the `h` mode's Tab cycle, its `c` and its two
// diff-flag click bodies), so none of them wants the reseat.
void land_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                   Viewport& viewport, int64_t src_frame) {
    // The A/B audition's end, this entry point's half of it — the argument is
    // at the marker form above, the inventory at GuiAuditionSequence. The
    // hold posture's clear too, but only when the seat moves the cursor
    // (AppState::camera_hold).
    clear_audition_sequence(app);
    const int64_t sample_before = app.playhead_cursor_sample;
    seat_playhead_on_source_frame(app, audio, viewport, src_frame);
    if (app.playhead_cursor_sample != sample_before) app.camera_hold = false;
}

// The write itself, shared by the two above. Its own two decisions:
static void seat_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport,
                                          int64_t src_frame) {
    int64_t sample = source_frame_to_active_domain(app, audio, src_frame);
    sample = clamp_playhead_to_live_domain(sample, app, audio);
    // IDEMPOTENCE ONLY, carrying no semantics: a land onto the sample the
    // playhead already holds writes the same value and moves no pixel, so
    // there is nothing to damage. It decides nothing about the audition — its
    // end is the LAND's, one level up, and runs above this return either way.
    // The hold's clear is the land's too, one level up, and reads this
    // return's outcome: a land that returns here keeps camera_hold.
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
// NO MOVEMENT, AND THAT IS A RULING RATHER THAN AN OBSERVATION (2026-08-19):
// the callers are ENTRIES — the A/B tab switch, the `p` column swap, the
// loads — and an entry is a restore or a switch, neither of which moves the
// playhead in the music. So this route reseats rather than lands.
// THE SCAN IS SELECT-FIRST, AND IT STAYS THAT WAY (architect 2026-09-02): it
// walks the store in ORDER and takes the first
// marker on the frame, with no disabled term — so where a stack shares one
// frame it can land the focus on a member the marker WALK would skip (that
// walk skips disabled) and on the BOTTOM of a stack whose top a click would
// grab (hit_test_flag walks backwards). Neither is a defect to fix: a
// coincident stack is a state you delete down until it is not coincident, and
// any other behaviour there is adversarial and unsupported. No enabled-member
// preference was added.
// Read-only allowed (selection and playhead are navigation). Bounds-safe by
// construction — the index comes from the scan itself.
void auto_select_marker_at_playhead(AppState& app, const GuiAudio& audio,
                                    Selection& selection, Viewport& viewport) {
    // Both columns through the one store selector pair (active_marker_count
    // / active_marker_time_frame, app_state.h).
    const auto scan = [&]() {
        const int n = active_marker_count(app);
        for (int i = 0; i < n; ++i) {
            const int64_t sample = clamp_playhead_to_live_domain(
                source_frame_to_active_domain(app, audio,
                                              active_marker_time_frame(app, i)),
                app, audio);
            if (sample == app.playhead_cursor_sample) return i;
        }
        return -1;
    };
    const int hit = scan();
    if (hit < 0) return;
    selection.set_single_selection(hit);
    // THROUGH THE RESEAT, NOT THE LAND (2026-08-19): the land ENDS AN A/B
    // AUDITION and this route must not, its callers being the tab and column
    // ENTRIES — restores and switches, not movements (the audition's own two
    // tab switches among them). The exemption is free of judgement here: this
    // land is a PROVABLE NO-OP (the predicate that selected the marker IS the
    // write's own equality test), so there is no movement for the rule to
    // answer. The rule is at Viewport::move_playhead_to (viewport.cpp), the
    // reseat's two callers at reseat_playhead_on_marker (input_pointer.cpp).
    reseat_playhead_on_marker(app, audio, viewport, hit);
}

// One scrub ACT: THE SCRUB ALWAYS PLAYS (architect 2026-09-21, superseding
// the 2026-07-27 "stop, then start on the next click", which had itself
// superseded the kill-and-revive of 2026-07-23). EVERY scrub click launches
// from the clicked frame: a click over a LIVE session first ends it through
// the one stop body and then launches at the click IN THE SAME ACT, so the
// click never merely stops. Each launch re-captures its end_sample there — so
// a scrub after a mid-session trim edit auditions the NEW window instead of
// riding a stale capture — and plays ONCE to that end; no GUI launch loops
// (the two sanctioned exceptions are the render player's Repeat One and the
// car's loop of the trim, neither a road from here).
// WHAT THE STOP ENDS, stated here because the click now always goes on to
// launch:
//   * THE A/B AUDITION (Shift+Space) is one transport session, and the stop
//     body clears it ahead of its own guard (GuiAuditionSequence's inventory,
//     app_state.h), so a scrub over the act — mid-play or in a rest — ends
//     it and plays once from the click; the launch entry's own user-launch
//     clear would end it too.
//   * THE CAR'S LOOP OF THE TRIM (car_toggle_playback) is a property of its
//     launch and any GUI stop ends it: a scrub over a running car loop stops
//     the loop and plays ONCE from the click (launch_playback_from passes
//     kPlaybackNoLoop). The console's own buttons and bodies are untouched
//     (GuiCarTransport); its comparator simply publishes the new session, and
//     a pending car play dies at the click itself, launched or refused (the
//     press count at the body's head).
//   * FOLLOW: the scrub's launch pages while the lamp is lit, as every
//     project launch does, and starts unsuspended (the launch body's clear,
//     AppState::follow_suspended); the lamp itself is untouched.
//   * COST: the stop's quiescence fence is now paid on EVERY click over a
//     live session (at most one per click; a stopped session pays none).
// The stop body is called unconditionally: it is a no-op on a stopped
// session past its audition clear, which the launch entry makes anyway.
// The old exact-same-frame skip stays GONE: every click relaunches, so there
// is no in-place audition to preserve, and a stopped scanner's fields are
// stale by contract (no non-playing validity window exists). A refused launch
// (out-of-window frame; target update in flight; a dead device) ends the
// click STOPPED — a live session it interrupted is not revived — the launch
// body's own sentence answering the dead device under it (this scrub is the
// one launch road with no outer gate of its own), and a later click at a
// launchable frame launches. The target gate below carded from 2026-08-30
// until 2026-09-04 and is silent now, by the rule stated at that gate.
void GuiInputHandler::scrub_act_at(int64_t frame) {
    // A GUI TRANSPORT PRESS, counted FIRST so every ending of this act — the
    // launch, the is_updating refusal below, a launch the device refuses —
    // supersedes a pending car play (the rule at
    // GuiCarTransport::PendingCarPlay, car_transport.h; the count's writers at
    // AppState::gui_transport_press_count). Without it a refused scrub left
    // the wait armed, and the car's loop started from the trim when the
    // preview settled.
    ++app.gui_transport_press_count;
    // The stop half, through the standing stop machinery — side-effect-clean
    // here (the scrub never moved the cursor) and the owner of the scanner's
    // visible-identity teardown. It runs FIRST so scrub_launch_at's defensive
    // live-session guard always finds the session stopped.
    playback_lifecycle.stop_playback_if_playing();
    // Outer is_updating gate, mirroring the two Space handlers: a NEW launch
    // while a target update is in flight would audition the stale target
    // buffer, which Space refuses — so the scrub launch refuses it too, and
    // silently since 2026-09-04. Row 8's state cell already carries the
    // preview render's own `Updating...` for as long as that render runs,
    // which is the rule stated in notifications.h's silent list as the second
    // member of the one-dimensional class — a bound key's refusal is silent
    // when row 8's process line already names the reason. (The greyed Play
    // button is the other half of the answer here too: its face reads
    // preview_ready, which folds is_updating in (target_render.h), so the face
    // and this gate cannot disagree.) The same ruling took Space's twin card,
    // the shared kTargetPreviewNotReadyCard deleted with both raises. The
    // gate itself is unchanged: the click is still consumed, and
    // chord_is_bound and spell_chord are untouched, so nothing about this
    // silence says the road is unbound. It is asked AFTER the stop above
    // (architect 2026-09-21), so a click over a live session while the
    // preview updates ends STOPPED, as every refused launch below does.
    if (app.active_audio_view == 'T' && target_render.is_updating()) {
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
// per-column fence cadence class is structurally gone). The press claims
// nothing and stops nothing, and the drag-modal gate swallows every chord
// while the pending stands, so the act sees the session as it stands at the
// release; since the scrub always plays (architect 2026-09-21) that reading
// decides only whether the act pays the stop's fence, never whether it
// launches.
void GuiInputHandler::scrub_press_at(int click_rel_x) {
    const GuiRect area = waveform_area(app);
    // Gutter / invalid column: no launch position exists, silent no-op.
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    // The clicked column converts on the PAINTED viewport, the item basis
    // (playhead_frame_at_click_column; architect 2026-09-24, strictly as
    // painted), so the scrub launches at the frame the pixels under the
    // pointer showed; cold, the live viewport by the basis's own contract.
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
//   2. the THREE DIALOG modal editors' veil, which consumes every press
//      outside the dialog's own field and buttons — the
//      shared predicate is modal_dialog_editor_active, whose membership is
//      AppState::dialog_editor_session's three;
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
//      start anything. TWO gestures are excepted, each ahead of the refusal
//      and each on the same rule — the thing being dragged is the thing the
//      cursor names: a live trim gesture owns the cursor (the arm below,
//      architect 2026-08-03), and a live EDITOR TEXT DRAG owns the I-beam
//      (the arm at the top of the body, architect 2026-09-03).
GuiCursorKind GuiInputHandler::pointer_cursor_kind(int x, int y,
                                                   GuiInputState mods) const {
    // THE NOTIFICATION CARDS FIRST (2026-08-29), ahead of every refusal
    // below as their press claim is ahead of every gate: a card is opaque
    // to the pointer — no zone underneath it can promise anything — and the
    // card itself is ONE BUTTON since its X retired (2026-10-01), and a
    // button carries no cue anywhere in the product.
    if (notification_card_at(app, x, y) != 0) return GuiCursorKind::Arrow;
    // THE SIZING FRAME (architect 2026-10-05) names its resize, Windows'
    // double arrows by edge and corner, under every veil as its press is
    // claimed above every veil (claim_window_frame_press) — on the hover
    // alone: a live gesture crossing the frame keeps its own cue, the arms
    // below answering it.
    // A MAXIMISED WINDOW'S FRAME (cde's dtwm frame, standing there too,
    // 2026-10-08) sizes nothing: the arrow, its press a consumed nothing.
    if (const unsigned edges = window_frame_edges_at(app, x, y);
        edges != 0 && !any_pointer_gesture_active(app)) {
        if (gui.window_maximized()) return GuiCursorKind::Arrow;
        const bool top    = (edges & kGuiWindowEdgeTop) != 0;
        const bool bottom = (edges & kGuiWindowEdgeBottom) != 0;
        const bool left   = (edges & kGuiWindowEdgeLeft) != 0;
        const bool right  = (edges & kGuiWindowEdgeRight) != 0;
        if ((top && left) || (bottom && right))
            return GuiCursorKind::WindowSizeNWSE;
        if ((top && right) || (bottom && left))
            return GuiCursorKind::WindowSizeNESW;
        return (top || bottom) ? GuiCursorKind::WindowSizeNS
                               : GuiCursorKind::WindowSizeWE;
    }
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
    // AND THE COLOR PICKER (2026-10-07), with ONE cue: the I-beam over its
    // one field's interior as painted (the dialog field's own rule below),
    // the Arrow everywhere else — its sliders, its wheel, its swatches and
    // its buttons carry no cue, every other zone is behind its veil, and
    // THE TWO BUTTON ROWS, on under it (row 8 since 2026-10-07 evening, the
    // icon row since 2026-10-08), wear the Arrow they wear without it (a
    // button carries no cue anywhere).
    if (app.color_picker.active) {
        const AppState::ColorPicker::Stash& st = app.color_picker.stash;
        return st.valid && rect_contains(st.field_inner, x, y)
                   ? GuiCursorKind::Text : GuiCursorKind::Arrow;
    }
    // A LIVE EDITOR TEXT DRAG KEEPS THE I-BEAM (architect 2026-09-03: "the
    // cursor in flag editor becomes the pointer during drag-to-highlight — it
    // should remain the i-beam — that's what other editors do"), on the
    // live-gesture exception's own rule and in the same member shape as the
    // trim and marker drags below: the thing being dragged is the
    // thing the cursor names — a text selection — so the Text the field wears
    // at rest stays TRUE for the whole gesture, read from the drag's OWN
    // RECORD and never re-derived from the pointer's position. Position is
    // exactly what cannot answer here: a selection drag routinely leaves the
    // field's published rect (that is how one selects past the visible run),
    // and widening either field test to "or a drag is live" would be the
    // re-derivation this family forbids. The drag has ONE shape — a caret
    // sliding through a byte run — so being live is the whole record, and it
    // is capture-free, so there is a visible cursor to keep.
    // IT SITS AHEAD OF THE DIALOG EDITOR'S BLANKET rather than with those
    // three arms below, and that rank is what makes it ONE owner for BOTH
    // editor families: the blanket answers first while a dialog editor stands
    // and would decide its drag by position, while the marker lane's flag
    // box falls THROUGH the blanket (keyboard-modal, pointer- and
    // wheel-transparent — modal_dialog_editor_active, input_handler.h) to the
    // box's own hover arm far
    // below. One arm above both covers both. THE CARDS AND THE FOUR BLANKETS
    // ABOVE STILL WIN, the rank the trim and marker drags already take under
    // them: a card is opaque to the pointer and can be raised from a worker
    // mid-drag, and the rest are structurally inert while a button is held.
    if (app.editor_text_drag.active) return GuiCursorKind::Text;
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
    // return above it ever moved. THIS ARM IS THE HOVER'S ALONE since
    // 2026-09-03: a LIVE text drag never reaches it, the live-drag Text above
    // having answered, so a selection dragged past the field's edge keeps the
    // I-beam instead of flipping to the Arrow this arm would give it.
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
    // IT SERVES THE ONE TRIM SURFACE, the 10 px bar's endcaps and bridge (the
    // waveform overlay's bounds and interior armed these very drags from
    // 2026-08-18 until the resting overlay was deleted on 2026-09-22).
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
    // THE MARKER REPOSITION DRAG KEEPS ITS CUE TOO (architect 2026-08-14: "it
    // should remain left/right arrows during the drag, like trim ... drag
    // currently do"), on the same rule and in the same member shape as the
    // one above: the thing being dragged IS the thing the cursor names, so the
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
    // AND THE VALUE DRAG KEEPS ITS OWN, on the same rule (2026-09-10): the
    // thing being dragged is a VALUE, so the vertical resize the flag box
    // wears under the posture stays TRUE for the whole gesture. The record is
    // that it is live — one shape, a number sliding up and down — and there is
    // no position to re-derive from (this drag leaves the flag box by
    // definition, exactly as a bound drag leaves the band). Capture-free, so
    // there is a visible cursor to keep.
    if (app.value_drag.active) return GuiCursorKind::ValueDrag;
    // THE PENDING ARM IS THE SAME ARM FOR BOTH DRAGS, and where the view makes
    // the flag's plain drag the vertical one it has to ask the posture's
    // question (value_drag_posture, app_state.h): sub-threshold the pointer
    // still rests on the flag box it pressed, where the hover arm below
    // answers ValueDrag on an actionable cell and the ARROW on every other
    // one, so the pending reads the same predicates and the cue cannot flip at
    // the crossing. A marker drag cannot be live here at all while the posture
    // stands — the crossing begins one gesture or none — so `drag.active`
    // needs no term. (Nothing a pending press can outlive moves the posture:
    // the drag-modal gate swallows every chord that could switch a view.)
    if (value_drag_posture(app) && app.pending_marker_press.active) {
        return value_drag_target(app, audio, app.pending_marker_press.marker,
                                 app.pending_marker_press.cell)
                   ? GuiCursorKind::ValueDrag : GuiCursorKind::Arrow;
    }
    if (app.drag.active || app.pending_marker_press.active)
        return GuiCursorKind::TrimResize;
    // (THE REGION EDITOR'S OWN LIVE ARM STOOD HERE FROM 2026-08-15 TO
    // 2026-08-18 and is deleted with the gesture; the overlay's drags that
    // replaced it were deleted in turn on 2026-09-22.)
    // (THE EDITOR TEXT DRAG IS THIS FAMILY'S FOURTH MEMBER and is answered at
    // the top of the map rather than here — its own arm carries why.)
    if (any_pointer_gesture_active(app)) return GuiCursorKind::Arrow;

    // THE OPEN FLAG EDITOR'S BOX IS EDITABLE TEXT, so it wears the I-beam
    // (architect 2026-08-13, with the Text kind: it showed the navigation
    // surface's PAN before, the marker lane being nav surface under it then, and a
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
    // every marker-lane field alike, each publishing the same rect through the
    // same painter — and covers the RIDING BOXES deliberately NOT: that run is
    // the marker's own boxes painted beside the field, whatever the editor's
    // kind, not editable text, so an I-beam over it would promise a caret the
    // press does not seat. What the run promises instead is the MARKER LANE'S
    // OWN CUE, and it needs no arm here to get it: the flag arm far below reads
    // hit_test_flag, which resolves a riding box to its marker (the walk asks
    // the editor's published run first), so the pointer over one says exactly
    // what it says over a resting box — which is what the press now does
    // there.
    // IT IS THE HOVER'S ARM ALONE: a live text drag out of this box is
    // answered at the top of the map, by the drag's own record.
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
    // waveform — both halves, every view — and nothing else. The lower half
    // joined 2026-08-13 when the press-time scrub became the motionless
    // release's act, which left the halves differing in that act alone. It is
    // the plain drag's PAN surface and the ctrl drag's ZOOM surface, and both
    // cues cover it whole. THE RULER AND THE MARKER LANE WEAR THE ARROW
    // (architect 2026-09-25, the lanes leaving the surface on both devices):
    // they are PLACEMENT SURFACES now, a motionless click their one act and
    // no drag armed there, and a click carries no cue anywhere in this map —
    // so they fall to the top strip's plain Arrow below, with no term of
    // their own, as they do under ctrl (no zoom there any more).
    const bool on_nav_surface = point_on_nav_surface(app, x, y);

    // (ALT IS UNNAMED: its pointer vocabulary is EMPTY since 2026-08-12 — the
    // grab-pan it carried moved onto the plain drag and the alt press claims
    // nothing anywhere, so alt falls to the modified-combination Arrow below.)
    // CTRL-EXACT: three claims, and the press path's own order between them.
    // Over the TRIM BAR ctrl sets the BEGIN bound — at the LIFT since
    // 2026-08-15, its crossing then handing over to a single-bound drag on that
    // bound (set_trim_bound_at_click_then_arm_drag) — boundary extension by
    // another route, so it takes the BEGIN cap's own cue rather than the
    // Arrow (no
    // wheel zooms anywhere since 2026-09-14, every modified wheel a
    // swallowed no-op (GuiInputHandler::on_wheel), and the map answers what a
    // PRESS would do in any case, no wheel being cued anywhere);
    // over the
    // NAVIGATION SURFACE it is THE ONE NAV DRAG'S ZOOM MODIFIER (the
    // live-ctrl model, 2026-08-14 — ScrollDragState): the hover cue promises
    // exactly what a ctrl press or a mid-drag ctrl press buys, the zoom, on
    // the surface that covers BOTH waveform halves and nothing else. Ctrl's
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

    // PLAIN-EXACT from here.
    //
    // THE NAVIGATION SURFACE WEARS THE PAN — the cue promises the drag, which
    // is what the plain drag does there now; the motionless click needs no cue,
    // exactly as no click anywhere carries one, and that covers BOTH of the
    // halves' click acts (the upper half's playhead placement and the lower
    // half's audition scrub). That is the WHOLE waveform, in every view — "the
    // hand shows up in both the top and the bottom half" (architect
    // 2026-08-13) — and since 2026-09-25 nothing in the top strip: the ruler
    // and the marker lane's empty stretches are placement surfaces, whose
    // motionless click needs no cue for the same reason, and which arm no
    // drag for a cue to promise.
    if (on_nav_surface) return GuiCursorKind::Pan;
    if (inside_top) {
        // THE TRIM BAR BAND, RESOLVED THROUGH THE ROUTER'S OWN TWO OWNERS
        // (architect 2026-08-03, closing the band-wide cue this used to paint):
        // the plain press arms only on an ENDCAP or inside the inter-cap BRIDGE,
        // so the cue asks exactly hit_test_trim_endcap and
        // point_in_trim_bridge_span — the same predicates route_trim_bar_press
        // calls, in the same order, both reading the trim painter's stash
        // (AppState::trim_bar_hit) — and a point on the band that arms nothing
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
            switch (hit_test_trim_endcap(app, x, y)) {
                case TrimHit::Begin: return GuiCursorKind::TrimBoundBegin;
                case TrimHit::End:   return GuiCursorKind::TrimBoundEnd;
                case TrimHit::None:  break;
            }
            if (point_in_trim_bridge_span(app, x, y))
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
        // the drag in a live view at home; where the drag refuses (a
        // read-only tab) or does not exist (the `h` view's diff flags, which
        // take clicks alone) the box still wears it, one shape for the one
        // surface. THE EXCEPTIONS ARE THE VALUE DRAG'S POSTURE (2026-09-10,
        // the view's own since 2026-09-13 — which is also why the warp column's
        // off-home view no longer reaches this resting answer: target view on
        // that column IS the posture) and the ADD TO SELECTION lamp
        // (2026-09-12) — and they are exceptions because each changes WHICH
        // GESTURE the surface offers rather than merely whether it will
        // succeed: under either the horizontal move is off on every flag, so a
        // box that still promised it would promise a gesture that no longer
        // exists.
        // Those forks are at the arms below. It reads
        // hit_test_flag — the painter's published boxes, the same predicate the
        // press claims and the nav surface carves itself out with — so it
        // answers the LIVE marker lane and the `h` view's DIFF flags through
        // one term, that stash being whatever was drawn. It sits under the
        // modifier arms by their own rank (a ctrl or shift press on a flag is a
        // selection act, which carries no cue) and AHEAD of the strip's Arrow,
        // which is what used to answer here.
        // THE VALUE DRAG POSTURE TAKES THIS ARM WHILE IT STANDS (architect
        // 2026-09-10 for the gesture, 2026-09-13 for the posture being the
        // view's — value_drag_posture, app_state.h), and it is the map's
        // standing rule applied rather than an exception to it: there the
        // flag's plain drag is the VERTICAL one, so a box whose cell the drag
        // can act on wears the vertical resize — and a box it cannot act on
        // arms NOTHING at all (the horizontal move is off on every flag while
        // the posture stands), so it wears the Arrow, which is what a point
        // arming nothing shows everywhere in this map. The question is the gesture's own owner,
        // value_drag_target, asked with the cell the same published boundaries
        // resolve for a press (hit_test_flag_cell) — one answer for the cue
        // and for the crossing.
        const int flag_hit = hit_test_flag(app, audio, x, y);
        if (flag_hit >= 0) {
            // ADD TO SELECTION GOVERNS THE PAYLOAD BOX (architect 2026-09-12
            // for the arm, 2026-09-19 for the box it stops at): while the
            // sticky ctrl stands, a plain press on a flag's PAYLOAD takes
            // the membership-toggle branch, which acts at the press and arms
            // NOTHING (run_marker_click_act's `toggle` term, this file), so no
            // crossing can begin a drag of either axis there. That is
            // true whatever the value drag posture says, so the term is ranked
            // first and outside it: the Arrow is what a point arming nothing
            // wears everywhere in this map, and a box still promising the
            // horizontal move would promise a gesture the press cannot begin.
            //
            // THE TWO BOUND CELLS ARE OUTSIDE IT, on the press's own rule:
            // the lamp folds nothing there, so a plain press still arms
            // whatever the posture arms and the cue has to be the posture's.
            // The term asks the SAME hit test the press asks, and the pending
            // arm above asks the pending's own cell, so the cue cannot flip at
            // the press or at the crossing.
            //
            // AND IT IS THE `h` VIEW'S TERM TOO SINCE 2026-09-17 (architect):
            // in there the flags are the mode's DIFF flags, and the lamp is a
            // PRODUCER of the mode's multi-selection now — a plain diff-flag
            // press runs the mode's ctrl body
            // (handle_history_mode_press -> select_history_diff_flags_modified)
            // exactly as a plain live-flag press runs the toggle branch, and it
            // arms nothing either. So the box wears the Arrow in both views for
            // one reason, and the `!history_mode.active` term this arm carried
            // is deleted: it existed only while the lamp produced nothing in
            // the view, where the cursor had to keep promising the diff flag's
            // own click. The cursor promises the gesture, and the gesture now
            // exists there. The value drag's own term needs no such fork — its
            // posture answers false whole in the view, at its own declaration.
            // A DIFF FLAG PAINTS NO CELLS, so the box test below answers
            // Payload over the whole of one (hit_test_flag_cell, app_state.cpp)
            // and the lamp's Arrow covers that view exactly as it did.
            const MarkerCell cell = hit_test_flag_cell(app, audio, x, y);
            if (app.add_to_selection && cell == MarkerCell::Payload)
                return GuiCursorKind::Arrow;
            if (value_drag_posture(app)) {
                return value_drag_target(app, audio, flag_hit, cell)
                           ? GuiCursorKind::ValueDrag : GuiCursorKind::Arrow;
            }
            // Off the value drag's posture the flag's plain drag is the
            // HORIZONTAL move, so its box wears TrimResize — the cursor
            // promises the gesture.
            return GuiCursorKind::TrimResize;
        }
        // The rest of the strip: the button rows (claimed far above the
        // waveform in the press path, no cue of their own), the two
        // PLACEMENT LANES' empty stretches (a motionless click places, no drag
        // arms — 2026-09-25) and GAP 1's blank band — all Arrow.
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
// THAT THERE IS ONLY ONE POSITION IS THE POINT: a clamped
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
// site left (the nav drag's threshold crossing), so no two sites can
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
// above; that painter fills the line's pixels [col, col + t) through
// fill_waveform_line (render.h), its left edge on the column.
double GuiInputHandler::nav_stem_column_x() const {
    const GuiRect wf_area = waveform_area(app);
    const double  spp     = painter_samples_per_pixel(app, audio, wf_area);
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
    // A PLACEMENT LANE'S PENDING HAS NO ZOOM PHASE (2026-09-25): ctrl
    // arriving over a held ruler or marker-lane press seats no pivot and
    // paints no stem — the lanes arm no drag of either kind.
    if (!sd.active || sd.placement_only || mods.ctrl == sd.zooming) return;
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
                               painter_samples_per_pixel(
                                   app, audio, waveform_area(app));
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
    if (W <= 0.0 || spp <= 0.0) return;

    // The level: the rule's one owner (nav_drag_zoom_level, below), which
    // the one-finger touch zoom reads too.
    const double new_level = nav_drag_zoom_level(dx);

    // THE PIVOT'S COLUMN UNDER THE LIVE VIEWPORT, with the Ableton EDGE TRICK
    // — the one owner, rebind_zoom_pivot_into_waveform below, whose record
    // (the reversibility property and where it stops) is at its definition.
    const double anchor_col = rebind_zoom_pivot_into_waveform(sd.anchor_sample);

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

// THE NAV DRAG'S LEVEL RULE — one owner, two readers: the pointer nav drag's
// zoom phase (apply_nav_zoom_at) and the one-finger touch zoom
// (apply_touch_nav_update's ctrl arm, the S Pen's button, 2026-09-25: "port
// the fork, reuse the applier, no second zoom rule"). dx zooms plain linear
// off the LIVE level, RIGHT zooming in (`zoom_level - dx/rate` — the sign's
// derivation from the pinch is at the zoom phase's contract), pre-clamped
// into the chokepoint's own [effective floor, effective ceiling] window exactly as
// every apply_strip_drag_zoom caller pre-clamps. The divisor is the RESOLVED
// rate — device px per level at the live gui_scale — and never the authored
// constant; why the rate scales is at nav_zoom_px_per_level(), app_state.h.
double GuiInputHandler::nav_drag_zoom_level(double dx) const {
    const GuiRect wf_area = waveform_area(app);
    const int64_t total = live_total_frames(app, audio);
    double new_level = app.zoom_level - dx / nav_zoom_px_per_level();
    const int64_t column = audio.working_column();
    const double min_l = effective_min_zoom_level(column);
    const double max_l = effective_max_zoom_level(wf_area.w, total, column);
    if (new_level < min_l) new_level = min_l;
    if (new_level > max_l) new_level = max_l;
    return new_level;
}

// THE ZOOM PIVOT'S COLUMN UNDER THE LIVE VIEWPORT, with the Ableton EDGE
// TRICK — one owner, three readers: the nav drag's zoom phase, the pinch and
// the one-finger touch zoom (apply_touch_nav_update), each holding its own
// SONG-FRAME pivot (ScrollDragState::anchor_sample, TouchNavZoomState). The
// deleted strip drag's own step, minus its pan term: a zoom never moves the
// viewport itself, so the resting `viewport_start_sample` IS the viewport
// the zoom will pivot against and there is no local `vp` to clamp first. The
// pivot is a SONG FRAME, so its column is derived fresh every event; clamping
// it into [0, W-1] and REBINDING the anchor to that edge pixel's frame is
// what keeps the focus on screen once a wall has pushed it past an edge.
// WHAT THE REVERSIBILITY PROPERTY IS, stated exactly: THE ANCHORED FRAME IS
// INVARIANT FOR THE PHASE, so zooming out and back in by the same travel
// pivots about the SAME song position both ways and the gesture reverses into
// the section it came from. AWAY FROM THE WALLS that is the strict identity —
// the column re-derives to the value it was placed at, so the return event
// reproduces the earlier viewport. AT A SATURATED WALL the viewport cannot
// come back the same way (while `vp` is pinned the view is determined by the
// level alone), and what survives is the FOCUS: the stem's column slides
// left/right with the content and the frame under it never changes. That is
// the case the screen column got wrong — it held the COLUMN and let the song
// walk out from under it, so the way back zoomed into a later section
// entirely (the architect's own scenario, worked at ScrollDragState). WHERE
// THE PROPERTY STOPS: the edge REBIND, the one lasting mutation here — once
// the anchored frame has been pushed off the visible span the anchor becomes
// the edge pixel's content, and the return trip pivots about that instead.
// Returns the pivot's (clamped) column; the caller guards a live waveform
// and a positive spp.
double GuiInputHandler::rebind_zoom_pivot_into_waveform(
        double& anchor_sample) const {
    const GuiRect wf_area = waveform_area(app);
    const double  spp     = painter_samples_per_pixel(app, audio, wf_area);
    const double  vp      = static_cast<double>(app.viewport_start_sample);
    double anchor_col = (anchor_sample - vp) / spp;
    const double clamped_col = clamp_col_into_waveform(wf_area, anchor_col);
    if (clamped_col != anchor_col) {
        anchor_sample = vp + clamped_col * spp;
        anchor_col    = clamped_col;
    }
    return anchor_col;
}


// (THE REGION EDITOR'S COLUMN->FRAME CONVERSION AND ITS MOTION BODY STOOD HERE
// FROM 2026-08-15 TO 2026-08-18 and are DELETED WHOLE, not moved: the overlay's
// move and bound drags became the TRIM BRIDGE and ENDCAP drags armed from the
// waveform, and those arms were deleted with the resting overlay on
// 2026-09-22. The displayed-basis rule they carried lives on in
// the trim drag's own dispatch freeze, which names trim_drag: no waveform job
// may publish a new basis while a trim drag is held.)

// THE TOUCH NAVIGATION BODY — two-finger frames and the phone model's
// single-finger frames land here alike; contract, the ONE FINGER PANS,
// TWO FINGERS ZOOM ruling (and the one-finger zoom under ctrl), delivery-shape
// justification and refusal rationale at the declaration (input_handler.h).
// One delivered frame = at most one placement through the strip-drag family's
// own application chokepoint.
void GuiInputHandler::apply_touch_nav_update(const GuiTouchNavFrame& f) {
    // THE FRAME'S MEANING, three and only three: TWO fingers are the PINCH;
    // ONE finger under the ctrl bit is THE ONE-FINGER ZOOM (2026-09-25, the S
    // Pen's side button — the nav drag's own live-ctrl fork, ScrollDragState,
    // carried onto the glass; the frame's ctrl is ignored on two fingers); one
    // finger without it is the PAN. The fork keys on the bit, never the tool:
    // a fingertip simply has no ctrl to hold.
    const bool one_finger_zoom = !f.two_finger && f.ctrl;
    // THE SEATED PIVOT IS CLEARED BY ANY FRAME WHOSE MEANING IS NOT THE
    // SEAT'S — a pan frame, or a zoom of the other kind (the seat records its
    // kind, TouchNavZoomState::one_finger) — and that clear LEADS THE BODY:
    // it is the one thing here that happens above the refusal (contract at
    // TouchNavZoomState, app_state.h). THE TWO
    // HALVES SIT ON OPPOSITE SIDES OF THE REFUSAL DELIBERATELY: SEATING is a
    // navigation act and takes the refusal with everything else (the ordering
    // rule at the seat below), while CLEARING is bookkeeping — a frame of
    // another meaning means the zoom phase is OVER whether or not this frame
    // gets to navigate, and holding the anchor through a refused stretch of the
    // survivor's pan would let a later upgrade zoom about a song frame the
    // fingers had long since left behind. Refusing to navigate is not refusing
    // to notice that the pinch ended.
    // THE CLEAR OWES THE ERASE since the pinch became the anchor stem's third
    // producer (2026-08-14) — and that is exactly why it is a body with an
    // early return rather than an assignment here: this line runs on EVERY
    // one-finger frame, while the stem must be rubbed out once, on the frame
    // the seat actually dies (contract at clear_touch_zoom_seat).
    // AND THE DOWNGRADE REACHES THIS LINE BY CONSTRUCTION: the platform
    // delivers one single-finger frame at the two-to-one transition even when
    // both of its deltas are no-ops (the exemption at set_touch_nav_hooks'
    // update contract), so a survivor left standing still cannot keep the dead
    // pinch's pivot seated and its stem painted under one finger — which is
    // what let a later upgrade zoom about the OLD song point.
    // THE ONE-FINGER ZOOM REACHES THIS LINE AT BOTH CTRL EDGES BY
    // CONSTRUCTION TOO: the platform delivers one exempt frame carrying the
    // new bit at every ctrl edge under a live single-finger nav (the same
    // exemption's second clause), so a ctrl-up clears the seat and erases the
    // stem at the edge rather than at the next motion — the nav drag's own
    // edge behaviour (sync_nav_drag_mode).
    if ((!f.two_finger && !one_finger_zoom) ||
        app.touch_nav_zoom.one_finger != one_finger_zoom)
        clear_touch_zoom_seat(app, viewport);
    // A SEAT THE PEN'S LIFT RETAINED IS A LIVE GESTURE'S AGAIN from the first
    // frame that survives the clear above — which, for the pen's next
    // button-held landing, is its first one-finger zoom frame: the seat is of
    // its own kind, so nothing clears and nothing re-seats below, and the
    // zoom continues about the same song frame (contract at
    // TouchNavZoomState, app_state.h). Whether it outlives THIS gesture is
    // decided afresh at this gesture's end.
    app.touch_nav_zoom.retained = false;

    // The refusal answer, per frame: the wheel's own routing predicate at the
    // current centroid. <= 0 covers both the modal refusals (-1) and the
    // outside-both-areas 0 that handle_wheel itself no-ops on — the gesture
    // navigates exactly the wheel's two surfaces. A refused frame navigates
    // nothing AND SEATS NOTHING.
    // AND A SCROLLING POPUP LIST'S CONTEXT 6 TAKES NONE (2026-10-08): the
    // list's scroll is a wheel act with no two-finger meaning, and the
    // waveform behind the list is veiled (render.h's popup scroll block).
    const int nav_ctx = wheel_context(f.x, f.y);
    if (nav_ctx <= 0 || nav_ctx == 6) return;
    // (THE COLOR PICKER refuses the frame one line above, through the
    // wheel's context, which answers -1 under it, 2026-10-07: its veil
    // consumes every press off the card, and a pan is no exception.)
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
    // so two thumbs landing on keys inside the disambiguation window (a window
    // opened everywhere then) became a Nav pinch and zoomed the waveform
    // BEHIND the keyboard, about a column the user cannot see. Since
    // 2026-09-25 a pinch begins only from the pan zone's window, which yields
    // under the keyboard, so what this clause answers now is a pinch whose
    // centroid travels over the keys. The pan zone already yields under this same rect
    // (touch_point_in_pan_zone's keyboard clause) — this is the two-finger half
    // of that answer, and it asks the same two owners, so the two cannot
    // disagree. A key is not a navigation surface at any finger count.
    if (onscreen_keyboard::stands(app, gui) &&
        rect_contains(onscreen_keyboard::surface_rect(app), f.x, f.y))
        return;
    // Defensive only: the platform guarantees a positive ratio (a degenerate
    // finger distance delivers 1.0).
    double dist_ratio = f.dist_ratio;
    if (!(dist_ratio > 0.0)) dist_ratio = 1.0;

    // ONE FINGER PANS, TWO FINGERS ZOOM — architect 2026-08-14, the whole
    // gesture model on glass (the ruling and its friction argument at the
    // declaration, which is authoritative). The terms
    // are NEVER two at once: a two-finger frame's centroid travel is
    // discarded outright, which is what kills the accordion, and a
    // single-finger frame carries no distance to zoom by. The one-finger
    // side's 1.0 RESTATES the model rather than guarding — the platform
    // already pins the ratio there, one finger having no finger gap. UNDER
    // CTRL THE ONE FINGER'S TRAVEL IS THE ZOOM'S instead of the pan's
    // (zoom_dx), the nav drag's own rotation: the same horizontal travel, the
    // modifier deciding what it MEANS.
    const double eff_dx    = (f.two_finger || one_finger_zoom) ? 0.0 : f.dx;
    const double eff_ratio = f.two_finger ? dist_ratio : 1.0;
    const double zoom_dx   = one_finger_zoom ? f.dx : 0.0;

    // The geometry the pivot is measured in, HOISTED ABOVE THE NO-OP RETURN
    // for the seat below: cheap reads, and the guard is a validity gate (there
    // is no waveform to anchor a pivot in) rather than a policy about this
    // frame's deltas.
    const GuiRect wf_area = waveform_area(app);
    const double  W       = static_cast<double>(wf_area.w);
    const int64_t total   = live_total_frames(app, audio);
    if (W <= 0.0 || total <= 0) return;

    const double spp_old = painter_samples_per_pixel(app, audio, wf_area);
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
    // THE ONE-FINGER ZOOM SEATS ON THE SAME LINE, at the finger's point: the
    // PREVIOUS centroid column (x - dx), which is the finger's position when
    // the zoom began — on the ctrl edge's exempt frame (dx 0) the finger
    // itself, the pointer drag's "the pivot seats where the cursor is at the
    // ctrl-down"; on a single nav whose FIRST frame already carries the bit
    // (the button held at the down, then the slop crossed) the DOWN point,
    // the ctrl-armed press's own seat-at-the-press.
    if ((f.two_finger || one_finger_zoom) && !app.touch_nav_zoom.seated) {
        TouchNavZoomState& z = app.touch_nav_zoom;
        const double seat_col =
            static_cast<double>(f.x) - (one_finger_zoom ? f.dx : 0.0);
        z.anchor_sample = vp + seat_col * spp_old;
        z.seated        = true;
        z.one_finger    = one_finger_zoom;
        // THE SEAT OWES ITS FIRST FRAME'S DAMAGE, which is the mouse arm's own
        // rule (arm_nav_zoom_press) reaching the pinch —
        // the seat is the anchor stem's gate since 2026-08-14
        // (paint_strip_drag_anchor, paint_handler.cpp) and it is NOT free. A
        // seating frame is not even an APPLIED frame any more (it is exactly
        // the centroid-only frame above that this ordering rescued), and even
        // where it is, apply_strip_drag_zoom's own MID-GESTURE TRUE-NO-OP
        // return drops any frame whose post-clamp level AND viewport both
        // stand — every frame of a pinch that begins saturated at a wall
        // (pinching further out at full zoom-out, or further in at the floor),
        // which is precisely the edge the stem was asked for. Without this line
        // such a pinch would show no stem until it turned around. Once per
        // phase, and it merges with the apply's own damage on every frame that
        // does move.
        viewport.invalidate_waveform_area();
    }

    // A frame whose surviving delta is an exact no-op applies nothing: the
    // platform suppresses frames where BOTH raw deltas are no-ops, so a
    // two-finger frame carrying pure centroid travel arrives here and dies
    // here — having seated the pivot on its way past, which is the whole of
    // the ordering above. THE DOWNGRADE'S TRANSITION FRAME IS THE PLATFORM'S
    // ONE EXEMPTION FROM THAT SUPPRESSION and dies here too, which is exactly
    // what it is for: its whole errand is the CLEAR at the top of this body,
    // and it must apply nothing, seat nothing and consume no double-click
    // candidate on its way to that return. Below the seat, above the
    // double-click clear (the C8 rule covers APPLIED frames).
    if (eff_dx == 0.0 && eff_ratio == 1.0 && zoom_dx == 0.0) return;

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
    if (!f.two_finger && !one_finger_zoom) {
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
        // A ZOOM — TWO FINGERS, OR ONE UNDER CTRL — PIVOTS ABOUT THE POINT ON
        // THE WAVEFORM THE GESTURE GRABBED, held for the phase's life: seated
        // above (on the phase's FIRST unrefused frame, whether or not that
        // frame applies anything), then re-derived as a COLUMN against the
        // live viewport every frame here. The centroid's own travel is
        // discarded by the fork above (eff_dx is 0), so moving both fingers
        // together still applies nothing — it only seats — while the one
        // finger's travel is the zoom itself (zoom_dx).
        TouchNavZoomState& z = app.touch_nav_zoom;
        // THE EDGE TRICK, through its one owner (the nav drag's zoom phase
        // reads the same body): a column pushed outside [0, W-1] pins at the
        // edge pixel and REBINDS the held frame to that pixel's content, which
        // is what keeps the zoom's focus on screen exactly as it does for the
        // mouse.
        anchor_col    = rebind_zoom_pivot_into_waveform(z.anchor_sample);
        anchor_sample = z.anchor_sample;
    }

    // The distance ratio maps to the level LOGARITHMICALLY — spreading the
    // fingers by 2x is one level in (spp halves, so the content between the
    // fingers scales with the finger gap; no feel constant). Pre-clamped into
    // the same [effective floor, effective ceiling] window clamp_viewport_start
    // re-applies, exactly as every other caller pre-clamps — the chokepoint's
    // level_changed compare requires a real request (its contract names the
    // callers).
    // THE ONE-FINGER ZOOM TAKES THE NAV DRAG'S OWN LEVEL RULE instead
    // (nav_drag_zoom_level — linear in travel at the gui_scale-resolved rate,
    // RIGHT zooming in), so the pen's drag and the mouse's ctrl drag are one
    // rule, not two.
    double new_level = 0.0;
    if (one_finger_zoom) {
        new_level = nav_drag_zoom_level(zoom_dx);
    } else {
        new_level = app.zoom_level - std::log2(eff_ratio);
        const int64_t column = audio.working_column();
        const double min_l = effective_min_zoom_level(column);
        const double max_l = effective_max_zoom_level(wf_area.w, total, column);
        if (new_level < min_l) new_level = min_l;
        if (new_level > max_l) new_level = max_l;
    }

    // ONE placement carries whichever axis is live, and the fork above decided
    // both of its anchor terms: a ONE-FINGER frame places the content under the
    // previous centroid at the CURRENT centroid column, which is the pan, while
    // a TWO-FINGER frame places the HELD frame back at its own re-derived
    // column, which is a pure zoom about the grabbed point (the ratio is 1.0 on
    // the first and the centroid delta is 0 on the second, so the off term is a
    // literal no-op either way). Everything downstream is the strip drag's own
    // — level clamp, viewport clamp, the synchronous per-frame rebuild, the
    // camera chokepoint's compare on either axis, and the mid-gesture true-no-op
    // skip.
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
    // THE ONE END THAT KEEPS THE SEAT (architect 2026-09-27; contract at
    // TouchNavZoomState, app_state.h): the pen lifting with its side button
    // held leaves a ONE-FINGER seat standing, marked retained, so the next
    // button-held landing zooms on about the same anchor — the laptop's Ctrl
    // held while the mouse is lifted and set down. The pen facts are the
    // platform's and it answers them here, true only inside the pen's own
    // lift on the Android backend (GuiPlatform::pen_lift_keeps_zoom_anchor);
    // the Wayland backend answers false, so the laptop clears at every end
    // exactly as before. Nothing on screen changes at a retention, so it owes
    // no damage; the stem the seat gates simply stays.
    TouchNavZoomState& z = app.touch_nav_zoom;
    if (z.seated && z.one_finger && gui.pen_lift_keeps_zoom_anchor())
        z.retained = true;
    else
        clear_touch_zoom_seat(app, viewport);
    if (playback.is_playing()) playback.resync_predictor();
}

void GuiInputHandler::release_pen_zoom_anchor() {
    // A RETAINED SEAT ALONE: a seat a live gesture holds is that gesture's to
    // clear (its own frames and its end), so a release reaching one — a pen
    // report showing the button up mid-stroke, whose ctrl edge has already
    // cleared it, or a finger landing on a live pen stroke, which the
    // platform marks so that stroke's lift keeps nothing — changes nothing.
    // The erase is clear_touch_zoom_seat's.
    if (!app.touch_nav_zoom.retained) return;
    clear_touch_zoom_seat(app, viewport);
}

// The pan-zone query's body (contract at the declaration): THE NAVIGATION
// SURFACE, and since 2026-08-13 it DERIVES rather than restates — one call to
// the surface's own geometry owner, point_on_nav_surface, which is the same
// predicate the press router, the ctrl claim and the cursor map read. Until
// that ruling this body was a HAND COPY of the press router's derivation (its
// own inside_waveform spelling plus its own `!waveform_lower_half` term), so
// the lower half's arrival on the zone did NOT follow for free — it followed
// once the copy became a call. Surface geometry only, save the open menu (the
// first clause, 2026-10-01); every other refusal stays downstream (the update
// body's per-frame wheel_context answer, the region begin's gate list).
//
// SO THE WHOLE WAVEFORM IS THE PAN ZONE NOW (architect 2026-08-13, embracing
// the consequence: "currently finger down works in the upper half by waiting
// for the finger up, but on the lower half it immediately dispatches the
// scanner — so there's an asymmetry, and now that I understand it we should
// eliminate the asymmetry"). A one-finger drag pans anywhere on the waveform,
// the region hold reaches anywhere on it, and a motionless tap on the
// lower half is the tap-at-lift burst whose motionless press-release IS the
// deferred scrub act — the mouse's own machinery, inherited with no touch code.
//
// AND THE WAVEFORM IS THE WHOLE PAN ZONE (architect 2026-09-25, "out of
// both — we want symmetry as much as possible"): the ruler and the marker
// lane left the navigation surface on both devices, so they left this zone
// through the owner with no clause here. A finger landing on either resolves
// to the POINTER TRANSLATION, where the lanes' own press arms answer it: a
// tap places the playhead (a double tap on the empty marker lane creates)
// and a drag does nothing — the placement pending begins no pan.
bool GuiInputHandler::touch_point_in_pan_zone(int x, int y) const {
    // UNDER THE COLOR PICKER NO TOUCH IS A PAN (2026-10-07): a finger on
    // the card resolves to a press ON CONTACT — its sliders and its wheel
    // are drags — and a finger off it meets the veil as a press would (on
    // the icon row or row 8, the rows' own claim, as without the picker —
    // neither row was ever on the pan zone).
    if (app.color_picker.active) return false;
    // THE ZONE YIELDS WHOLE WHILE A MENU STANDS (architect 2026-10-01, "Good,
    // I agree"): with a dropdown open, every touch is the pointer on contact,
    // so the first touch outside the menu is a PRESS AT THE DOWN, reaches the
    // dropdown's claim, closes the menu and is consumed — exactly the mouse's
    // press (on_button_press's dropdown claim, "ONE PRESS, ONE ACT"); the drag
    // that follows starts on a closed menu, one wasted gesture after a menu,
    // the cost every desktop charges. THE DIVERGENCE IT CLOSES: on the tablet
    // a tap outside closed the menu at its lift, but a drag or a hold there
    // opened the zone's window — the pan and the region hold, which the
    // downstream refusals freeze under an open menu — and the menu stayed,
    // while the laptop's press-and-drag closes it and does not pan.
    // THE HISTORY THAT LED HERE: the same day this clause yielded under the
    // dropdown's BOX alone, from his glass pass (the Settings menu's device
    // rows lost their pressed face at the pen's down while every other row
    // took it) — a menu hanging over the waveform puts its lower rows on the
    // pan zone, and a contact there opened the window and DELIVERED NO PRESS
    // until the lift, the pen's HOVER_EXIT before the tip having already
    // dropped the hover face, so the row painted at rest from the down to the
    // lift. The whole-zone yield keeps that answer (a row's press arms it at
    // the down, as a mouse press does) and widens it to every point.
    // THE KINDS STAY DISTINCT: a dropdown is a POPUP — one press, one act, its
    // claim's rank (2026-09-03) untouched — while a card is a NOTICE that
    // claims its own rect only and is never a mode, so a drag beside a card
    // pans (the clause below yields under the card alone).
    if (app.dropdown.open()) return false;
    // AND WHILE THE SETTINGS CHOICE EDITOR'S LIST IS DOWN (2026-10-07
    // evening), for the dropdown's reason: it is a popup hanging over the
    // well, so its row's press must arm at the down and a touch elsewhere
    // must close it as a press does (claim_settings_choice_press).
    if (app.settings_choice_live() && app.settings_choice.list_open)
        return false;
    // THE ZONE YIELDS UNDER A NOTIFICATION CARD (2026-08-29), for the
    // keyboard clause's reason below: the cards stack over the waveform's
    // upper right, the whole waveform is the pan zone, and a finger landing
    // on a card inside a pan zone would become the phone-model pan and
    // deliver its press only at the lift, if at all. Answering false lets the
    // finger resolve to the pointer translation ON CONTACT and reach the
    // card's own press claim at the down — which the card needs since it
    // became one button acting at the lift (2026-10-01): the long press is
    // measured from the press, and the held card's clock stops there. The
    // card is opaque to the pointer everywhere else too (the cursor map, the
    // hover walk), so the zone agrees with them.
    if (notification_card_at(app, x, y) != 0) return false;
    // (THE ZONE YIELDED INSIDE THE SHOWN TRIM REGION OVERLAY from 2026-08-15,
    // so a finger could reach the overlay's endcap and bridge drags. Both are
    // DELETED with the resting overlay, architect 2026-09-22 — the pen reaches
    // the trim bar — so the clause went with them.)
    // (THE OPEN MARKER-LANE EDITOR'S BOX needs no clause: it paints in the
    // marker lane, which is off the zone since 2026-09-25 by the owner below.
    // It had one from 2026-09-04, when the lane was nav surface — architect,
    // on the tablet: "if I tap and drag inside the editor, instead of moving
    // the caret like I would expect on a touch screen, it drags the viewport
    // and the waveform". A finger in the field therefore reaches the
    // platform's editor-field query (touch_point_in_editor_field), which
    // routes a DRAG to the CARET DRAG while a tap and the double press reach
    // the field's own press — the third ruled divergence,
    // begin_touch_caret_drag's declaration — and a finger on a RIDING box
    // beside the field
    // keeps the flag box's pointer answer, hit_test_flag resolving a riding
    // box to its marker since 2026-09-05.)
    // AND IT YIELDS UNDER THE ON-SCREEN KEYBOARD (2026-08-27), for the flag
    // box carve-out's reason exactly: the keyboard paints over the waveform's lower
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
    // the waveform whole (its band runs from under the icon row to the
    // bottom row), the whole waveform is the pan zone, and
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
    return point_on_nav_surface(app, x, y);
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
    // IS THE SIX-EDITOR PREDICATE, the marker-lane editors
    // deliberately included though they are pointer-transparent: every
    // pointer press CLOSES an open marker-lane editor before any claim
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
    if (app.render_player.active || app.picker.active ||
        app.color_picker.active) return;
    if (keyboard_modal_editor_active()) return;
    if (app.dropdown.open()) return;
    if (app.loading || audio.total_frames() <= 0) return;
    if (any_pointer_gesture_active(app)) return;
    const GuiRect area = waveform_area(app);
    if (app.history_mode.active) {
        // THE VIEW-LOCAL FORMER — handle_history_mode_press's shift arm
        // re-expressed at the down point: clear the mode focus + selection
        // (the pair clearer, the deselect's mode analog — the family rule at
        // RegionDragState), seat the playhead at the down column through the
        // shared placement body, arm the drag. Store selection untouched,
        // the view's own standing rule — AND NO TRIM: the motion path carves
        // this view out of the sweep's trim write, the view promising the trim
        // window is untouched throughout.
        if (clear_history_mode_focus(app.history_mode)) {
            // A discrete command: full-window damage for the face swap, the
            // shift arm's own shape.
            viewport.invalidate_all();
        }
        const int64_t sample = place_playhead_at_click_column(x - area.x);
        if (sample >= 0) arm_region_drag_at(x - area.x, x, y);
        return;
    }
    // THE LIVE FORMER — the shift press's own body whole (deselect-all,
    // playhead at the down column with the placement's playback stop, the drag
    // arm). THE HOLD IS
    // THE FINGER'S ONLY ROUTE TO A SWEEP — a finger has no modifier — and it
    // survives in full for that reason (architect 2026-08-18).
    place_playhead_and_arm_region(x - area.x, x, y);
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

// --- The touch caret drag (a finger's drag in the editor's field) ---
//
// The third ruled divergence (architect 2026-09-05) — the contract, the
// drain rule and the one record the bodies keep (the editing session the
// stream began on) are at the declarations
// (input_handler.h), the machine at GuiInputCore::set_touch_nav_hooks.
// Every seat is THE ONE POINTER SEAT (set_editor_caret_from_x), which is
// what keeps the finger's caret and the mouse's on one byte rule and one
// blink rule; nothing here selects, commits or seeds.

GuiTouchEditorField GuiInputHandler::touch_point_in_editor_field(int x,
                                                                  int y) {
    const ActiveEditorText g = active_editor_text(app, audio);
    if (!g.valid || !rect_contains(g.field, x, y))
        return GuiTouchEditorField::Outside;
    // The seed is read LIVE: a finger's down delivers nothing, so the
    // top-of-press clear has not run and the candidate the last lift seeded
    // still stands — exactly what the press arm's own predicate will see
    // when the resolution delivers that press.
    return editor_double_press_at(app.double_click, x, y)
               ? GuiTouchEditorField::DoublePress
               : GuiTouchEditorField::Field;
}

void GuiInputHandler::begin_touch_caret_drag(int x, int y) {
    (void)y;
    // THE STREAM'S ONE RECORD: the editing session it begins on, so the
    // frames behind it can tell the editor under the finger from any other
    // editor a physical keyboard opens meanwhile (the contract at the
    // declaration; AppState::touch_caret_session). Written here and nowhere
    // else, and written UNCONDITIONALLY — a begin that finds no editor
    // leaves a dead stream (0, which no live session can equal since the ids
    // start at 1) rather than whatever the last one held.
    app.touch_caret_session = 0;
    const ActiveEditorText g = active_editor_text(app, audio);
    if (!g.valid) return;
    app.touch_caret_session = g.ed->session;
    // A FINGER IN THE FIELD TAKES THE FOCUS BACK, the press's own rule on the
    // glass's road (return_modal_focus_to_field); the seat below restarts the
    // blink.
    if (g.dialog) return_modal_focus_to_field();
    // A caret drag is motion between two taps, so the seed the last tap
    // left cannot become a double-click across it (the C8 rule the nav
    // frames apply); the stream's own end seeds nothing.
    app.double_click = DoubleClickCandidate{};
    g.ed->selection_anchor = -1;
    set_editor_caret_from_x(g, x);
    if (g.dialog) viewport.invalidate_modal_dialog_area();
    else          viewport.invalidate_top_strip();
}

void GuiInputHandler::update_touch_caret_drag(int x, int y) {
    (void)y;
    const ActiveEditorText g = active_editor_text(app, audio);
    // THE SESSION IS THE GATE, not "is an editor open": the editor this
    // stream began on may have been committed and a different one opened
    // from the physical keyboard while the finger stayed down, and this
    // finger has no business in that one's text.
    if (!g.valid || g.ed->session != app.touch_caret_session) return;
    // The I-beam follows the finger and nothing selects — the anchor is
    // dropped at every seat, so a selection a physical keyboard made
    // mid-stream cannot be dragged either.
    g.ed->selection_anchor = -1;
    set_editor_caret_from_x(g, x);
    if (g.dialog) viewport.invalidate_modal_dialog_area();
    else          viewport.invalidate_top_strip();
}

void GuiInputHandler::end_touch_caret_drag() {
    // The record dies here whatever the end is worth — the platform fires
    // this hook unconditionally once the drag began, on the lift and on both
    // hard ends, so this is the one road every stream leaves by.
    const uint64_t began_on  = app.touch_caret_session;
    app.touch_caret_session  = 0;
    const ActiveEditorText g = active_editor_text(app, audio);
    // A foreign editor gets nothing, not even the blink restart: the update
    // arm's reason, at the gesture's own end.
    if (!g.valid || g.ed->session != began_on) return;
    // THE DRAIN RULE: the caret stays where the last delivered frame seated
    // it, nothing selects, nothing commits (Enter remains the one commit
    // route), nothing seeds, and the editor stays open. What the end owes
    // is the modal focus ring's own courtesy — a blink restart, so the
    // resting caret is lit the instant the finger leaves rather than caught
    // in the dark half of a period the drag's last seat started.
    text_editor::touch_blink(*g.ed);
    if (g.dialog) viewport.invalidate_modal_dialog_area();
    else          viewport.invalidate_top_strip();
}

// The flag editor's guard-free close — the LEFT press's, its one caller since
// the right button's unbinding (2026-08-12; contract at the declaration,
// input_handler.h). The box is the painter's
// published rect, the same one the F2.1 caret block tests, so "outside" means
// the same thing on every press path.
void GuiInputHandler::close_top_flag_editor_for_outside_press(int x, int y) {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (rect_contains(app.flag_editor_box.box, x, y)) return;
    // AND THE RIDING BOXES ARE OUTSIDE — whichever of the marker's boxes stand
    // to the RIGHT of the open field, painted by the same publisher at that
    // field's right edge whatever its kind (contract at
    // FlagEditorBox::riding_cells, render.h). They wear the editor's paint
    // pass but they are the MARKER'S boxes, not the field, so a press on one
    // is an ordinary outside press: it closes this session without committing
    // and then falls through to act on the cell it landed on, exactly as a
    // press on a box standing at REST does (architect 2026-09-05 — the three
    // editors are transparent to each other for the pointer as they already
    // are graphically). This owner is KIND-BLIND and that is the point: it
    // tests the published FIELD and nothing else, so it serves the payload,
    // bound and level editors through one body. The refusal that stood here
    // for the payload editor's riding run, and the caller's consume that went
    // with it, are both gone; the FIELD is still the one thing this owner
    // refuses on, because a press there is caret work the F2.1 block already
    // claimed.
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

// THE DIALOG BUTTONS' POINTER WALK, run on every motion under a standing
// dialog. It answers three readers — the armed button's inside bit with the
// feint (below), whose pressed face and focus frame are painted and damage
// the stashed box; THE RENDER PLAYER'S HOT BUTTON (AppState::player_hot,
// architect 2026-10-06: the player's row is a flat toolbar, the one dialog
// surface with a hover face; no other dialog button wears one, the frozen
// design of 2026-10-02); and this surface's tooltip wait (the tail).
//
// IT TRACKS AN ARMED BUTTON — THE FEINT (architect 2026-08-13,
// SUPERSEDING this walk's own "sliding off cancels, and sliding back on does
// NOT re-arm" rule of hours earlier): "if the user feints — clicks a button
// and then drags away before the mouse goes up — then that button receives the
// passive focus as well." So THE ARM STAYS LIVE FOR THE WHOLE HOLD and this
// walk only answers whether the pointer is inside it, which is what makes
// sliding back on restore the pressed face and its release commit — nothing
// was cancelled, so nothing has to be re-armed. THE FOCUS IS NOT THIS
// WALK'S: the press already moved it onto the armed button (architect
// 2026-10-03, Windows' rule; arm_modal_dialog_press), so leaving the button
// leaves the focus where it is and the held-away face (the focus frame) is
// the painter's own composition rather than a case (paint_modal_dialog). The
// whole rule and the pair's read-as-one-fact contract are at
// AppState::modal_dialog_pressed.
void GuiInputHandler::update_modal_dialog_hover(int x, int y) {
    const int hit = modal_dialog_button_hit(x, y);
    const int  armed  = app.modal_dialog_pressed;
    const bool inside = armed >= 0 && armed == hit;
    if (app.modal_dialog_press_inside != inside) {
        app.modal_dialog_press_inside  = inside;
        if (app.modal_dialog.valid)
            viewport.invalidate_rect(app.modal_dialog.box);
    }
    // THE PLAYER'S HOT BUTTON: the hit on the player's row while no dialog
    // press is armed (comctl32 shows no hot item under capture), else none.
    set_player_hot(app.modal_dialog.valid &&
                           app.modal_dialog.owner ==
                               AppState::ModalDialogOwner::Player &&
                           armed < 0
                       ? hit
                       : -1);
    // AND IT OWNS THIS SURFACE'S TOOLTIP WAIT (2026-08-13, when the modal
    // buttons took hints instead of bracketed accelerators): the same writer
    // and the same tick the roster's walk uses, keyed on the Dialog half of
    // the owner's index space. EVERY dialog button has a hint, so there is no
    // membership term here — the hit alone decides, and the painter's stash
    // carries the text. A held press starts no wait here either (the writer's
    // held term — the press arm above is the same press), and the pointer
    // sliding off the box's button takes the box down at once.
    // THE OWNER IS STAMPED WITH THE STASH THE HIT WAS READ FROM (its owner
    // tag and session), so the wait belongs to this painted surface alone
    // and dies with it (the rule is at AppState::RedesignTooltip).
    // A ROSTER OWNER IS THE ROSTER WALK'S (2026-10-07 evening): with no
    // dialog button under the pointer and a roster wait or box standing,
    // this walk hands nothing — every caller runs the roster walk right
    // after this one, and that walk answers for its own owner (under the
    // color picker it finds the icon row's and row 8's hints there, both
    // rows staying on under the card — the one surface where both walks
    // find buttons; under every other dialog its veil or the no-wait rule
    // hands "none" itself). Without this, the dialog walk's "none" would
    // restart the rows' wait at every motion and no button-row hint would
    // ever ripen.
    const AppState::RedesignTooltip& t = app.redesign_tooltip;
    if (hit < 0 &&
        ((t.hovered.surface == AppState::RedesignTooltip::Surface::Roster &&
          t.hovered.index >= 0) ||
         (t.visible &&
          t.owner.surface == AppState::RedesignTooltip::Surface::Roster)))
        return;
    note_tooltip_hover({AppState::RedesignTooltip::Surface::Dialog, hit,
                        app.modal_dialog.owner, app.modal_dialog.session});
}

void GuiInputHandler::set_player_hot(int index) {
    const uint64_t session = index >= 0 ? app.modal_dialog.session : 0;
    if (index == app.player_hot && session == app.player_hot_session) return;
    app.player_hot         = index;
    app.player_hot_session = session;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// THE ARM'S HARD END — the pointer-leave / capability-loss hook (main.cpp),
// beside the roster's own clear_redesign_button_press and for its reason: a
// pointer that has left the window is on no button, and an act that has not
// happened yet must not be left waiting for a release that may never come.
// Transition-gated, damaging the stashed box when it fires.
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
// `shift` IS THE PRESS-TIME MODIFIER (2026-08-28), and only the player's claim
// ever passes true — every other claim admits a plain press alone. A SHIFT
// PRESS ON A BUTTON WITH NO SHIFTED TWIN IS A CONSUMED NOTHING, never the
// unshifted act: the roster's own rule (arm_redesign_press), and a silent
// unshifted act would be a lie about what the modifier did. THE CLOCK IS
// STAMPED FOR EVERY ARM, a press having a time whatever it landed on; what the
// lift makes of it is modal_dialog_press_shifted's.
bool GuiInputHandler::arm_modal_dialog_press(int x, int y, bool shift) {
    const int hit = modal_dialog_button_hit(x, y);
    if (hit < 0) return false;
    // A DISABLED PLAYER BUTTON'S PRESS IS A CONSUMED NOTHING (architect
    // 2026-08-30: the transport keys are their own class) — the roster's
    // arm_redesign_press rule on this surface: nothing arms, no face paints,
    // no card (the grey is the message). The bit read is the STASH'S — THE
    // CLAIM READS THE PAINTED FACE (architect 2026-09-24, strictly
    // as-painted), the per-tick comparator (main.cpp) keeping it honest, and
    // a face painted live dispatches, its act answering for itself. THE
    // PLAYER'S SEVEN ARE THE ONLY
    // BUTTONS THAT PUBLISH IT (re-greped 2026-09-30), so this line is inert
    // off every other owner's row.
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
    // THE PRESS TAKES THE KEYBOARD FOCUS (architect 2026-10-03, "as few
    // exceptions as possible to the Windows 95 rule"): Windows moves the
    // focus to the button the mouse presses, AT THE PRESS. It REPLACES
    // whatever focus the dialog had, of either strength, and it is PASSIVE —
    // the user pointed at this button, which is not the deliberate keyboard
    // walk that earns the active strength. Every dialog's buttons arm here —
    // the prompt's, the editors' OK / Cancel, the player's and the picker's
    // Cancel — so this is the rule's one site. A refused press (the disabled
    // player button and the unadmitted shift above) arms nothing and moves
    // nothing. A slide off the held button leaves the focus here and fires
    // nothing; a release away leaves the button passively focused.
    // THE COLOR PICKER'S PRESS TAKES NO FOCUS (2026-10-08, the architect's
    // report of typing that stopped reaching the field): the card HAS NO
    // FOCUS RING (handle_color_picker_field_key refuses the Tab family) and
    // paints none, so a focus its Copy, Paste or Close took would be
    // invisible — and route_modal_editor_key's wall swallows every key but
    // Esc while the focus is on a button, so a Save As, a Rename or a hex
    // edit opened after a press on Copy or Paste typed nothing until a
    // reopen minted a new session. Its index stays -1 for the picker's whole
    // session; a prompt raised over the card (Delete's) is the prompt's own
    // stash and takes the focus as every prompt does.
    if (app.modal_dialog.owner != AppState::ModalDialogOwner::ColorPicker &&
        (app.modal_dialog_focus != hit || app.modal_dialog_focus_active)) {
        // MOVING THE FOCUS CANCELS THE KEYBOARD ARM, the rule's second site
        // (AppState::modal_dialog_key_pressed): the two arms can stand
        // together — a pointer hold while the keyboard presses the focused
        // button — and this assignment takes the focus off whatever the
        // keyboard was holding, so that hold's release must commit nothing.
        clear_modal_dialog_key_press();
        app.modal_dialog_focus        = hit;
        app.modal_dialog_focus_active = false;
        // AND THE LIST'S RING BIT GOES WITH IT (2026-08-29): on the two
        // list-bearing owners the ring's stops are [list, buttons…], and the
        // press REPLACES whatever focus the dialog had. Without this the band
        // kept its focus frame beside the button's new one, so the ring
        // looked like it was in two places at once. Paint-only, but the frame
        // is the ring's whole cue.
        if (app.folder_overlay.list_focused) {
            app.folder_overlay.list_focused = false;
            viewport.invalidate_rect(folder_overlay::surface_rect(app));
        }
        viewport.invalidate_rect(app.modal_dialog.box);
    }
    app.modal_dialog_press_shift = shift;
    app.modal_dialog_press_ms    = monotonic_ms();
    return true;
}

// THE SHIFTED-TWIN VERDICT for the arm as it stands (2026-08-28) — the roster
// lift's own term over this surface: the CARRIED press-time shift ORed with a
// press HELD past chrome_shift_hold_ms(), so a physical Shift+click and a long
// press reach the same dispatch and holding a shift-clicked button changes
// nothing.
// It is measured at the LIFT against the arm's own stamp — no timer, no tick —
// and it asks nothing about WHICH button: the dispatch reads it only where
// player_button_shift_admits says so, which is what lets a button with no twin
// be held for as long as you like and still get its plain act. THE HOLD IS THE
// GLASS HALF of the pair and passes silently, the roster's ruling (tooltips do
// not show on the panel the gesture exists for).
bool GuiInputHandler::modal_dialog_press_shifted() const {
    if (app.modal_dialog_pressed < 0) return false;
    return app.modal_dialog_press_shift ||
           monotonic_ms() - app.modal_dialog_press_ms >= chrome_shift_hold_ms();
}

// A dialog button's RELEASE: the act runs iff the lift lands on the SAME
// button the press armed — which, since the FEINT made the arm survive the
// pointer wandering off, is exactly the `press_inside` bit the hover walk has
// been maintaining. Returns that button's index, or -1 — the caller owns the
// dispatch, because a prompt's buttons and an editor's mean different things.
// The arm is consumed either way: a release ends the hold whatever it lands
// on, and a release AWAY from the armed button leaves it passively focused,
// which the press already assigned (arm_modal_dialog_press).
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
    // player over picker over editor — paint_modal_dialog's fork; the two
    // list owners never stand together and neither stands beside an editor,
    // so their order is free). THIS IS ONE
    // OF THE THREE PLACES THE RANKING IS SPELLED and they must agree.
    const AppState::ModalDialogOwner live =
        app.prompt.active         ? AppState::ModalDialogOwner::Prompt
      : app.render_player.active  ? AppState::ModalDialogOwner::Player
      : app.picker.active         ? AppState::ModalDialogOwner::Picker
      : app.color_picker.active   ? AppState::ModalDialogOwner::ColorPicker
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
// THE ONE THING THE STASH DECIDES is a button's ENABLED bit (architect
// 2026-09-24, strictly as-painted): the claim reads the painted face, never
// the live predicate, and the act answers for itself (the player's arm below).
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
    // state (an item to pause, an item to seek inside, a load-capable
    // highlight) and answers its own refusal.
    if (app.render_player.active) {
        // THE CLAIM IS THE PAINTED FACE (architect 2026-09-24, strictly
        // as-painted, superseding 2026-08-30's live enabled re-ask here): a
        // button painted grey never arms — the pointer's press claim and the
        // ring's Enter/Space road both select on ModalDialogButton::enabled —
        // and the lift asks the same painted bit once more (a face the
        // comparator repainted grey under the hold is consumed, the roster
        // lift's rule), so what passes was painted live and DISPATCHES.
        // Where the act has become refused since that paint (at most one tick,
        // the per-tick comparator in main.cpp repainting the row on drift),
        // the act's own body answers exactly as its key does: carding where
        // the key cards (the load in place's three refusals), silent where
        // the silence is ruled (the folder walks' and the seeks' benign
        // refusals at their state, 2026-08-31). The KEYS never come through
        // here, and THE CAR never does either: on_media_command runs the
        // player's bodies DIRECT, so a head unit's button reaches neither this
        // line nor a button's face.
        if (!b.enabled) return true;
        switch (b.player_act) {
            // THE TWO SKIPS ARE THE ROW'S SHIFT-ADMITTING PAIR (2026-08-28):
            // their keys are Home and End since 2026-08-31 — the main window's
            // own transport pair, "just like the regular GUI" — with the plain
            // acts home() and next_track(), the right one a NEXT TRACK since
            // 2026-09-04; THIS IS THE SECOND OF EACH ACT'S TWO ROADS, the key
            // being the other (the car's own Previous and Next are the car's
            // acts and reach neither of these buttons — architect 2026-09-12,
            // "the car is a separate interface"; both plain acts walked the
            // band at rest for the one day before that ruling), and
            // their shifted twin — a Shift+click on plastic, a long press on glass,
            // ONE term either way — is the item folder's END, the keys' own
            // Shift+Home / Shift+End. THE
            // ARMS THAT READ `shifted` ARE THE ADMISSION'S OWN MEMBERS
            // (player_button_shift_admits, app_state.h): every other act below
            // ignores the bit, so a held Play or Close is its plain act — the
            // roster's rule that a button with no twin may be held for as long
            // as you like.
            case AppState::PlayerButtonAct::Home:
                if (shifted) render_player.first_in_item_folder();
                else         render_player.home();
                return true;
            case AppState::PlayerButtonAct::PlayPause:
                render_player.play_button_act();
                return true;
            // (STOP dispatched here between the two of them until 2026-09-01,
            // when the player's Stop retired whole with its button.)
            case AppState::PlayerButtonAct::NextTrack:
                if (shifted) render_player.last_in_item_folder();
                else         render_player.next_track();
                return true;
            case AppState::PlayerButtonAct::RepeatOne:
                render_player.toggle_repeat_one();
                return true;
            case AppState::PlayerButtonAct::Up:
                // THE `..` ROW'S ACT, ON THE ROW SINCE 2026-09-01: the same
                // body Backspace runs, with its own silent wall at the root.
                render_player.up();
                return true;
            case AppState::PlayerButtonAct::LoadInPlace:
                render_player_load_in_place();
                return true;
            // DELETE (architect 2026-09-29), the root's slot and the row's
            // third shift-admitting button: the plain press asks for the
            // highlighted batch folder, the shifted one (a Shift+click, a
            // long press on glass) for every batch folder at the root — the
            // Delete key's two spellings.
            case AppState::PlayerButtonAct::Delete:
                render_player_delete(/*all=*/shifted);
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
    // THE COLOR PICKER'S THREE (2026-10-07; ColorPickerButtonAct): the
    // painted enabled bit decides (Paste grays on an empty slot), the act
    // answers for itself.
    if (app.color_picker.active) {
        if (!b.enabled) return true;
        switch (b.color_picker_act) {
            case AppState::ColorPickerButtonAct::Copy:
                color_picker.copy_to_slot();
                return true;
            case AppState::ColorPickerButtonAct::Paste:
                color_picker.paste_from_slot();
                return true;
            case AppState::ColorPickerButtonAct::Close:
                close_color_picker();
                return true;
            case AppState::ColorPickerButtonAct::None:
                return false;
        }
        return false;
    }
    dispatch_modal_dialog_editor_act(b.editor_ok);
    return true;
}

// An editor dialog's OK / Cancel press, dispatched as the session's own
// Enter / Esc through the SAME per-editor key route the keyboard takes
// (handle_*_editor_key -> route_modal_editor_key) — button-is-its-chord, so
// the commit bodies, the refusals, the BPM sweep and the teardowns
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
// for the one act that does not close the dialog — a refusal leaves
// the user where the fix is typed — so this is the act's own semantics rather
// than a workaround for the ordering.
void GuiInputHandler::dispatch_modal_dialog_editor_act(bool ok) {
    const GuiKey        key = ok ? GuiKeys::Return : GuiKeys::Escape;
    const GuiInputState mods{};
    return_modal_focus_to_field();
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        handle_top_flag_editor_key(key, mods);
    } else if (text_editor::is_active(app.settings_editor)) {
        handle_settings_editor_key(key, mods);
    } else if (text_editor::is_active(app.commit_title_editor)) {
        handle_commit_title_editor_key(key, mods);
    }
}

// THE FOCUS GOES BACK TO THE FIELD (the declaration names the three roads).
// A PRESS ON THE FIELD TAKES THE FOCUS BACK (architect 2026-10-02, fix 3):
// Windows moves the focus to what is pressed, and the field is pressed — so a
// button that took the focus by a press (arm_modal_dialog_press, at the
// press since 2026-10-03; it keeps it through the slide-away cancel: press
// Cancel, slide off, lift) gives it back the moment the field is pressed or a
// finger drags in it, the caret returning with it. Until that day the press seated the caret's byte but left the
// focus on the button, so the caret, which paints only while the field has
// the focus (paint_modal_dialog), never came back — a double tap highlighted
// a word with no caret beside it. THE FOCUS IS PASSIVE AFTERWARDS: -1 carries
// no strength on an editor dialog (AppState::modal_dialog_focus_active).
// MOVING THE FOCUS CANCELS THE KEYBOARD ARM, the rule's third site
// (AppState::modal_dialog_key_pressed): the arm names the button the focus
// was on, and the focus is going back to the field. On the editor act's road
// the keyboard's own release has already consumed its arm before reaching
// here, so this is the POINTER path's due — a click on OK while Enter is held
// down on a focused button must not leave that Enter able to fire a second
// act at its release. A prompt has no field and never calls this.
bool GuiInputHandler::return_modal_focus_to_field() {
    if (app.modal_dialog_focus < 0) return false;
    clear_modal_dialog_key_press();
    app.modal_dialog_focus        = -1;
    app.modal_dialog_focus_active = false;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
    return true;
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

// THE PLAIN MARKER SELECT — the plain click's act, factored out of
// run_marker_click_act (architect 2026-09-14) for its SECOND caller, the plain
// WHEEL over a flag cell (run_flag_cell_wheel, below), which must select
// exactly as a plain click does and must NOT arm PendingMarkerPress or seed or
// consume a double-click. Its clauses are the plain click's, in the click's
// order, and their arguments stay at run_marker_click_act, which runs the same
// clauses on its two modified arms: the playback stop, the single-select, the
// land and the addressed cell. Read-only does
// not refuse it — a select is navigation, the click's own rule.
void GuiInputHandler::run_marker_plain_select(int hit, MarkerCell cell) {
    if (hit < 0) return;
    playback_lifecycle.stop_playback_if_playing();
    // ONE PLAIN MARKER CLICK, NO SPECIAL CASE FOR A SELECTED MEMBER
    // (architect 2026-07-29, HORIZONTAL MOVEMENT IS A FOCUS ACT — the
    // doctrine is at the head of position_nudge.h): a click on a member of a
    // 2+ selection single-selects and lands like a click on any other marker,
    // and the drag it may become is an ordinary singleton drag. The clicked
    // marker's flag BRIGHTENS here — set_single_selection damages the top
    // strip, where the flags live; stems are class-colored and always on, so
    // no stem work is owed.
    selection.set_single_selection(hit);
    // THE LAND IS THE MOVEMENT OWNER HERE, as it is on the modified arms.
    land_playhead_on_marker(app, audio, viewport, hit);
    // THE ADDRESSED CELL, written AFTER the select because the select resets
    // the axis to the payload as it seats the focus (Selection::seat_focus);
    // a changed axis damages the marker lane even where the selection stood
    // still.
    if (app.addressed_cell != cell) {
        app.addressed_cell = cell;
        viewport.invalidate_top_strip();
    }
}

// THE PLAIN WHEEL OVER A FLAG CELL (architect 2026-09-14) — contract at the
// declaration (input_handler.h). Reached from on_wheel's context 5, which
// wheel_context answers over a flag cell of the live marker lane (never in the
// `h` view, never over an open marker-lane editor's own field).
void GuiInputHandler::run_flag_cell_wheel(GuiMouseButton dir, int count,
                                          int x, int y) {
    // A MARKER-LANE EDITOR STANDING CLOSES FIRST, exactly as an outside press
    // closes it (no commit), and the wheel then acts on the cell under the
    // pointer — a riding box included, the one flag walk resolving it to its
    // marker and cell (topmost_flag_rect, app_state.cpp) off the last painted
    // frame's publication, which the close leaves standing until the next
    // paint. The FIELD is not a flag cell (wheel_context answers the stepped
    // pan there), and this owner refuses on it too.
    close_top_flag_editor_for_outside_press(x, y);
    const int hit = hit_test_flag(app, audio, x, y);
    if (hit < 0) return;  // belt: the context answered a flag cell
    const MarkerCell cell = hit_test_flag_cell(app, audio, x, y);
    // (a) THE SELECT — the plain click's own body: no prior selection needed,
    // every audio view and column, read-only-legal as the click is, arming no
    // drag and seeding no double-click.
    run_marker_plain_select(hit, cell);
    // (b) THE STEP — THE VALUE STEP, as Up / Down would run it on the cell
    // just addressed: one step per detent in the detent's direction (up =
    // increase), the burst's detents one signed delta. THE KEY'S LOCK GATES
    // ARE ASKED HERE because the wheel passes no keyboard gate of its own:
    // read-only blocks every value step, and the iteration lock admits the
    // bound axis alone — the same two predicates on_key asks of Up / Down,
    // reading the cell the select just addressed. EVERY REFUSAL IS SILENT (the
    // bodies' GuiOpRefusal is dropped): a pointer gesture's non-event is its
    // own answer, and the select above already happened as a click's would.
    // synthesized_repeat is false, so a wheel burst MERGES through the tap
    // window (kTapCoalesceMs) into one undo entry on the steps that record.
    const bool up = dir == GuiMouseButton::WheelUp;
    const GuiKey key = up ? GuiKeys::Up : GuiKeys::Down;
    const GuiInputState no_mods{};
    if (authoring_lock_drops_chord(app, key, no_mods)) return;
    const int64_t delta = (up ? +1 : -1) * static_cast<int64_t>(std::max(count, 1));
    switch (cell) {
    case MarkerCell::Payload:
        // A phase reset's payload is a POSITION with no value to step: there
        // the select was the whole act. THE WARP COLUMN STEPS ITS TEMPO through
        // the same body bare Up/Down runs.
        if (app.active_markers_view != 'W') return;
        (void)warpops.adjust_tempo_cents(delta, /*synthesized_repeat=*/false);
        return;
    case MarkerCell::Lower:
    case MarkerCell::Upper:
        if (app.active_markers_view == 'P') {
            (void)phase_resets.adjust_iter_bound_hops(cell,
                                                      static_cast<int>(delta));
            return;
        }
        (void)warpops.adjust_iter_bound_cents(cell, delta);
        return;
    }
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
//
// THE SELECTION MODIFIERS ACT ON THE FLAG BOX ALONE (architect 2026-09-10:
// the bound cells "are considered outside of undo, and so in many ways
// they're a separate system"; "iterations mode is by design targeting each
// marker individually"). THE PAYLOAD IS THE MEMBERSHIP SURFACE; EVERY OTHER
// BOX TAKES THE PLAIN PRESS ONLY — a ctrl or shift press on a Lower or
// Upper box is a SILENT NO-OP, the guard below returning ahead of the stop:
// no toggle, no range, no land, no address, no arm and no playback stop, the
// pointer's own non-event. A range or a membership toggle addressed off a
// bound cell would build the very 2+ selection with a bound axis the cells'
// single-marker model has no meaning for, and a range select is a MEMBERSHIP
// act whose subject is the marker rather than the box. The PLAIN press on
// those boxes is untouched — single-select, address the cell, land — which is
// what makes a cell reachable at all. AND THE `k` FOLD STOPS AT THE PAYLOAD
// BOX FOR THAT REASON (architect 2026-09-19): the sticky ctrl turns a plain
// press on the PAYLOAD into the toggle and leaves the two BOUND cells' plain
// press exactly as it is — single-select, address, land — so the mode's own
// authoring surface stays reachable by pointer with both lamps lit, which is
// what admits bare `k` under grid iterations at all. The lamp therefore
// cannot produce a `toggle` for the guard below to eat, and what that guard
// still eats is the REAL ctrl press and the shift press this paragraph is
// about. (Bare `i`'s ON edge still PUTS ADD TO SELECTION OUT — architect
// 2026-09-12, selection_consumed — because raising the cells is an act that
// ends a selecting pass, not because the cells would be unreachable under
// it.)
// It runs the stop, the three-way selection fork, the
// land and — plain only — the double-click consume-open,
// and then ARMS the pending for the two things that genuinely belong to a
// later edge: the reposition DRAG a plain press may become (the crossing
// begins it; its gates live there) and the double-click SEED
// only a motionless release may write.
//
// THE HIT INDEX IS THE PRESS'S OWN live hit test — the act runs in the same
// event that resolved it, so nothing can be stale. The DOUBLE-CLICK verdict
// reads the press-time candidate SNAPSHOT (dc_at_press), because
// on_button_press's top-of-frame clear has already emptied the shared field.
//
// THE CONSUME PREEMPTS THE DRAG ARM: a recognized second press returns before
// the arm whether its open ran or refused — a double-click is never a drag
// (architect 2026-09-22; the rule is at DoubleClickSurface, app_state.h) — so
// no drag can begin under the editor it just opened (the editor owns input;
// it is pointer-transparent, and a second press that then moves is the
// editor's problem, not a marker drag). It seeds nothing either — the family
// rule.
void GuiInputHandler::run_marker_click_act(int hit, int x, int y, bool shift,
                                           bool ctrl,
                                           const DoubleClickCandidate&
                                               dc_at_press) {
    if (hit < 0) return;
    // WHICH CELL THE PRESS LANDED ON, from the painter's published boundaries
    // (hit_test_flag_cell; the MarkerCell block at PendingMarkerPress,
    // app_state.h), asked ONCE for the press's THREE readers: the modified
    // press's own surface test just below, the addressed cell written under
    // the selection fork and the double-click seed the plain arm stamps at
    // the tail. It is a pure geometry read, which is what lets it stand ahead
    // of the stop.
    const MarkerCell cell = hit_test_flag_cell(app, audio, x, y);
    // ADD TO SELECTION IS THE TOGGLE ARM'S SECOND PRODUCER (architect
    // 2026-08-18): while the mode stands, a PLAIN press on a flag's PAYLOAD
    // BOX is a ctrl press in every respect — same branch, same land, same
    // nothing-armed tail — because the mode's whole definition is "run the
    // ctrl branch". Folding it into the term rather than growing a fourth arm
    // is what makes that true by construction instead of by two bodies
    // agreeing.
    //
    // AND IT STOPS AT THAT BOX (architect 2026-09-19): a plain press on a
    // LOWER or UPPER cell is a plain press however the mode stands, so a cell
    // keeps the single-select, the addressed cell and the land it has with the
    // lamp dark. The bound cells are a separate system, authored one marker at
    // a time, so a mode whose whole job is to BUILD a multi-marker selection
    // has nothing to say about them — and the narrowing is what lets this lamp
    // stand lit under grid iterations at all, the sticky ctrl no longer taking
    // away the plain press the cells are addressed by (the record of the
    // refusal it replaced is at iteration_lock_key_blocked,
    // input_key_dispatch.cpp). THE CURSOR MAP CARRIES THE SAME TERM
    // (pointer_cursor_kind, this file), so the cue on a cell promises what the
    // press will do there.
    //
    // `&& !shift` IS THE SHIFT RULE AND IT IS LOAD-BEARING (the architect:
    // "Shift+click needs no rule here, because it has its own gesture"). The
    // fork below is `if (toggle) ... else if (shift)`, so a bare fold onto
    // ctrl would let a lit mode SWALLOW a held shift and turn a range select
    // into a membership toggle. A real ctrl+click is unaffected either way —
    // `ctrl` is already true — and ctrl+shift never reaches this owner at all
    // (the ctrl call site is ctrl-EXACT, and the plain/shift one passes
    // ctrl=false), so this term reads on the mode's arm alone.
    const bool toggle = ctrl || (app.add_to_selection && !shift &&
                                 cell == MarkerCell::Payload);
    // THE PAYLOAD IS THE MEMBERSHIP SURFACE, and this is the whole of that
    // rule (the head of this body argues it): a MODIFIED press — the toggle
    // on either of its producers, or the shift range — landing on any box but
    // the flag is a silent no-op. It returns ABOVE THE STOP deliberately: a
    // press that does nothing must not stop the audition either, which is
    // what "the pointer's non-event" means everywhere else in this file. The
    // plain press falls straight through, so a cell keeps its select, its
    // address and its land.
    if ((toggle || shift) && cell != MarkerCell::Payload) return;
    // THE PLAIN ARM IS ITS OWN BODY since 2026-09-14 (run_marker_plain_select,
    // above — the stop, the single-select, the land and the addressed cell),
    // because the plain WHEEL over a flag cell selects exactly as this press
    // does without arming the drag or seeding the double-click; the two
    // modified arms keep the same four clauses here.
    if (!toggle && !shift) {
        run_marker_plain_select(hit, cell);
    } else {
        // The stop leads on every shape THAT ACTS: selecting or editing under a
        // live audition is the case the top-strip stop exists for, and no arm
        // below refuses (read-only still selects and lands, and the index came
        // from a live hit test).
        playback_lifecycle.stop_playback_if_playing();
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
        }
        // THE ADDRESSED CELL RIDES THE PRESS (architect 2026-09-04, the iteration
        // bound cells; every cell since 2026-09-05 — "light the colour of only
        // the flag that's clicked"): the cell the press landed on becomes the
        // focus's addressed cell — the bright one, the one the arrows step, the
        // one Enter opens — on ALL THREE click shapes, because the press already
        // selects and lands on every shape and the axis is one more thing the
        // press says. Written AFTER the fork, because every mutator above resets
        // the axis to the payload as it seats the focus (Selection::seat_focus),
        // and a press is one of the FOUR routes that name a cell — the plain
        // wheel over a cell (run_marker_plain_select), the bound editor's open
        // and the TAB WALK's cell step are
        // the others, each writing behind its own selection write; a focus reached
        // by none of them is addressed at its payload. Read-only does not refuse
        // it: the axis is navigation, as the selection is, and the act it
        // addresses meets the lock at its own gate. The bright cell moves with
        // it, so a changed axis damages the marker lane even where the selection
        // stood still (a re-press of the focused flag's other cell).
        if (app.addressed_cell != cell) {
            app.addressed_cell = cell;
            viewport.invalidate_top_strip();
        }
    }
    // THE TWO MODIFIED CLICKS END HERE: neither has a double-click meaning,
    // neither has a drag to become, and their click has just committed whole —
    // so they arm NOTHING, exactly as they did before the one-day lift model
    // (an armed marker pending is plain by construction).
    // THE MODE REACHES THIS RETURN THROUGH `toggle`, and that is INTENDED
    // rather than tolerated (2026-08-18): while Add to selection stands, a
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
    // a refused consume is still the double-click's second press, so it arms
    // nothing either (below). THE OFF-HOME COLUMN NO LONGER REFUSES (architect 2026-08-24):
    // the payload editor edits a marker's VALUES and never its position, so it
    // is a member of the fifth ruled exception to the home-view binding and
    // opens in W+target too (the inventory is at
    // active_column_authoring_allowed, app_state.h).
    // A RECOGNIZED SECOND PRESS ARMS NOTHING AND SEEDS NOTHING, OPENED OR
    // REFUSED — A DOUBLE-CLICK IS NEVER A DRAG (architect 2026-09-22; the rule
    // and its inventory are at DoubleClickSurface, app_state.h): the return
    // ahead of the arm below is what makes "nothing arms a marker drag after a
    // double-click" structural rather than policed. (Until that ruling a
    // REFUSED consume — the P column's payload, a locked tab — fell through
    // to the arm, so a double-click that then moved became a marker drag.)
    //
    // AND THE CELL DECIDES WHICH EDITOR (2026-08-19; every cell its own since
    // 2026-09-05 — "each should be like a mini flag with its own
    // double-click"). The seed carries which cell of the run the FIRST press
    // landed on (MarkerCell): the flag box opens the payload editor, a bound
    // cell its bound editor, and a pair
    // straddling a seam opens the one the first click named. The gates
    // differ with them: the PAYLOAD editor keeps the LOCK and the P view,
    // the BOUND editor read-only and the cell's own eligibility (a cell that
    // paints is eligible, and the open is a belt behind that) on EITHER column
    // since 2026-09-09; a phase-reset bound cell's
    // double-click authors a session-only render parameter rather than
    // content. Since 2026-08-24 the payload editor
    // no longer differs about the AUDIO view either: it is the fifth
    // exception's member and opens off warp's home as well. Every open route
    // opens fully SELECTED (open-selected), so there is no clicked-glyph
    // caret to seat; a specific caret spot is a click inside the already-open
    // editor (the F2.1 path).
    const bool double_click =
        dc_at_press.surface == DoubleClickSurface::Marker &&
        dc_at_press.target == hit &&
        monotonic_ms() - dc_at_press.time_ms <= kDoubleClickMs &&
        std::abs(x - dc_at_press.press_x) <= double_click_slack_px() &&
        std::abs(y - dc_at_press.press_y) <= double_click_slack_px();
    if (double_click &&
        // THE LOCK, WITH THE BOUND CELLS CARVED OUT (architect 2026-09-10).
        // Read-only refuses every one of the marker-lane editors, as it
        // always did. The ITERATION lock refuses the payload — it opens
        // over serialized content and pushes —
        // and ADMITS a bound cell, the
        // mode's own authoring surface, which is the keyboard's own delta at
        // iteration_lock_key_blocked (input_key_dispatch.cpp) written for the
        // pointer. Silent either way: a pointer gesture's non-event is its own
        // answer, and this surface has never carded (the keys do).
        !active_view_state(app).read_only &&
        (!app.iteration_mode_enabled ||
         dc_at_press.cell == MarkerCell::Lower ||
         dc_at_press.cell == MarkerCell::Upper)) {
        switch (dc_at_press.cell) {
        case MarkerCell::Payload:
            // THE PAYLOAD AXIS OPENS THE WARP column's canonical-line editor.
            // A phase reset authors no payload line at all, so the P column
            // opens nothing, silent — and arms nothing, the double-click
            // return below. (The bare-Return arm makes the same fork.)
            if (app.active_markers_view == 'W') {
                flag_editor.enter_top_flag_edit(hit);
                return;
            }
            break;
        case MarkerCell::Lower:
        case MarkerCell::Upper:
            // A bound cell paints on EITHER column's eligible markers under
            // a lit mode (2026-09-09 — the phase-reset column's own hop
            // bracket), so the open takes the ACTIVE column and its own belts
            // re-ask the eligibility.
            flag_editor.enter_iter_bound_edit(app.active_markers_view, hit,
                                              dc_at_press.cell);
            return;
        }
    }
    if (double_click) return;
    // ARM THE PENDING — the drag the plain press may become (the crossing
    // begins it, its gates there) and the SEED its motionless
    // release owes. UNCONDITIONAL past the double-click return above: even a
    // locked tab or an off-home column arms, because the release still seeds — only the DRAG is gated, at the
    // crossing (a locked tab still selects and lands; read-only protects the
    // authored musical content, and a selection is navigation).
    app.pending_marker_press = PendingMarkerPress{};
    app.pending_marker_press.active  = true;
    app.pending_marker_press.marker  = hit;
    app.pending_marker_press.press_x = x;
    app.pending_marker_press.press_y = y;
    // WHICH CELL THIS PRESS LANDED ON, resolved at the head and carried to
    // the seed at the motionless release AND, since 2026-09-10, to the VALUE
    // DRAG at the threshold crossing, whose subject IS the cell. The
    // horizontal marker drag still reads it nowhere: that gesture is the same
    // act from any cell.
    app.pending_marker_press.cell = cell;
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
// THE POINT'S BASIS DOES NOT MOVE UNDER IT: the armed record is a member of
// the displayed-basis freeze (displayed_basis_frozen, app_state.h —
// architect 2026-09-24), because the lift and the crossing both convert the
// stored press_x through the displayed map, the crossing arming the trim drag
// at it too — so the gates are live and the aim's geometry is the epoch the
// user pressed in, like every other aimed press.
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
    // target trigger) and the setter's deselect.
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
// an omission (pointer_cursor_kind's zone map is derived from the press
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

    // The lamps, read ONCE: the page decides which key is under the finger and
    // the shift arm decides what that key types, so both must be the same
    // answer the last paint used.
    const AppState::OnscreenKeyboard& kb = app.onscreen_keyboard;
    const onscreen_keyboard::Page page        = kb.page;
    const bool                    shift_armed = kb.shift_armed;

    onscreen_keyboard::KeyDef def{};
    const int hit = onscreen_keyboard::key_at(app, page, x, y, def);
    if (hit < 0) return true;   // a gap, the margin: consumed, no key

    using Role = onscreen_keyboard::Role;

    // THE THREE PAGE / STATE CONTROLS — Shift, the symbol-mode key and the
    // page key, the first two of which wear lamps (the page key's cap already
    // says its page) — ACT ON THE SURFACE AND SYNTHESIZE NOTHING. They still
    // take the held index (so the finger sees the click face) with a keysym of
    // 0, which is what the release reads as "this key owed no key-up".
    if (def.role == Role::Shift || def.role == Role::SymbolMode ||
        def.role == Role::SymbolPage) {
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
            // THE PAGE KEYS LEAVE A PENDING CAPITAL STANDING, like every other
            // key that types no letter (the rule is stated whole at the
            // ordinary key's spend, below). It costs nothing to keep: the only
            // thing an arm can ever change is a LETTER, and every letter is on
            // the letter page, so a round trip through the symbols and back
            // finds the arm exactly where it was left — with the lamp lit
            // again the moment the shift key is painted again. Where each key
            // goes is the layout's own answer (page_after).
            app.onscreen_keyboard.page =
                onscreen_keyboard::page_after(def.role, page);
        }
        // ALL THREE REPAINT THE WHOLE SURFACE: shift moves every letter cap's
        // case and a page change moves every key.
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
            const char32_t typed = onscreen_keyboard::shifted_char(def.ch,
                                                                   shift_armed);
            capitalized = (typed != def.ch);
            // The keysym is the LOWERCASE base (GuiKey is ASCII case-folded);
            // the CASE travels in the codepoint, which is what the editors'
            // printable classification reads — and what they insert, through
            // the one incoming filter, for ASCII and past it alike. The
            // reasoning is at keysym_of.
            keysym    = onscreen_keyboard::keysym_of(def.ch);
            codepoint = static_cast<uint32_t>(typed);
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
        case Role::SymbolMode:
        case Role::SymbolPage: return true;   // handled above; unreachable
    }

    app.onscreen_keyboard.pressed_key    = hit;
    app.onscreen_keyboard.pressed_keysym = keysym;
    // The click face, before the act: the act can close the editor and take the
    // surface with it, and a damage queued after that would name a rect nothing
    // paints. One key's rect — the discrete-command rule applies to the SHOW
    // and HIDE of the whole surface (the tick comparator's, main.cpp), not to a
    // key changing face.
    viewport.invalidate_rect(
        onscreen_keyboard::key_rect(app, page, hit));

    // A SHIFT ARM IS SPENT BY THE LETTER IT CAPITALIZED AND BY NOTHING ELSE.
    // ONE-SHOT SHIFT MEANS THE NEXT LETTER (planner ruling 2026-08-27), so a
    // key that types no capital leaves the arm standing: the comma and the
    // period, the space bar, backspace, Enter, Esc, Tab, the page keys and
    // every digit and symbol on the symbol pages. Shift, comma, `q` types
    // `,Q`. The
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

    // Un-press the face, on the page the INDEX names rather than the live one:
    // a page key's own press moved the live page, and its key would be looked
    // up on a page it is not on (the reason is at page_of_key_index).
    // The surface may be GONE altogether (the press's own Enter or Esc closed
    // the editor), in which case the rect is empty and this is a no-op — the
    // show/hide damage is the tick comparator's, not this path's.
    if (onscreen_keyboard::stands(app, gui)) {
        viewport.invalidate_rect(onscreen_keyboard::key_rect(
            app, onscreen_keyboard::page_of_key_index(held), held));
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
// is described. THE ROWS ARE CHROME (the timing doctrine at
// GuiInputHandler::on_key): the
// press ARMS because the same press may still become the band's scroll drag,
// so its identity is not certain at the press; THE MOTIONLESS LIFT HIGHLIGHTS
// THE ROW AND OPENS IT — a click activates (architect 2026-08-29), which is
// why the panel has no double-click surface any more: the act is at the one
// edge that can tell a click from a drag, and a second press has nothing
// left to mean.
//
// ONE PRESS ROUTER, TWO CONTENTS. The claim below stands ABOVE the two mode
// veils (the player's and the picker's), each of which admits exactly the
// band and its own modal row, and BELOW the prompt gate, which outranks every
// surface as it always has. What the OPEN means is the owner's, and the two
// forks below are the whole of it: nothing else in this file asks which
// content fills the rows.

// THE MOTIONLESS LIFT'S FIRST HALF: the band moves, under every owner — the
// picker has no field beside it (architect 2026-08-28), so the band IS what
// Enter opens.
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
    // (AN OPEN DROPDOWN OWNS THE POINTER, THIS BAND INCLUDED, and this claim
    // no longer says so itself: the popup's claim is RANKED above this call
    // since 2026-09-03, so a press while a menu stands never arrives here at
    // all — it selects an item or dismisses the menu, consumed whole. The line
    // that used to stand here, `if (app.dropdown.open()) return false;`, was
    // the band's own half of a rule the rank now owns for every surface under
    // the popup; the whole reasoning is at the claim, on_button_press.)
    if (button != GuiMouseButton::Left) return false;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (!rect_contains(surf, x, y)) return false;
    // CONSUMED FROM HERE: the band is opaque to the pointer. A MODIFIED
    // press is a consumed no-op — nothing on a row reads shift or ctrl, and
    // nothing on a row reads kHoldBeatMs either: the load is the modal row's
    // own button (strict modifier validation's answer, and the roster's
    // shift-hold is elsewhere). A press on the pad or on a gap between rows
    // arms nothing — the claim is this SURFACE rect, while row_at contains
    // against the content rect (the surface whole while the band owns no
    // line, folder_overlay.h), so the pad is claimed and inert by construction
    // at every scroll offset.
    //
    // A MODIFIED PRESS ON A ROW IS SILENT LIKE ONE ANYWHERE ELSE ON THE BAND
    // (architect 2026-08-30, the unbound-keys ruling read on the pointer:
    // "bound keys either show an effect or a card, so an unbound key is
    // identified by its silence" — a gesture nothing binds is answered the
    // same way). It carded "Rows open on a plain click" for the one day of
    // 2026-08-30, on the ROW alone; the row test the card needed is gone with
    // it, the modified press claiming the band and arming nothing wherever it
    // lands.
    if (mods.ctrl || mods.shift || mods.alt) return true;
    const int hit = folder_overlay::row_at(app, x, y);
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
        // THE DRAG GATE, the sweeps' and pans' crossing threshold (a flag and
        // the trim bar, the grab surfaces, read twice it), spelled here as at
        // every other pending press: CHEBYSHEV from the press
        // (max(|dx|,|dy|)) against drag_moved_threshold_px() — 6 Windows px
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
        // (the ruling's own references).
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

// -- THE NOTIFICATION CARDS' POINTER HALF (2026-08-29) -------------------------
//
// The rule and its reasons are at notifications.h and at the claim's site in
// on_button_press. The bodies here are thin: geometry is the painter's
// publication, the arm is AppState::ChromePress's Card kind, the act is
// GuiNotifications' own.

bool GuiInputHandler::claim_notification_press(GuiMouseButton button, int x,
                                               int y, GuiInputState mods) {
    // The hit owner has already asked the LIVE stack (notifications.h): a
    // published rect whose card has LEFT that stack — by its expiry, by a
    // dismissal or by the bump — answers 0 here (a live card that is merely
    // clipped is selected wherever it has a published rect, and past the
    // room's foot it has none), so this press is claimed by NOTHING and falls
    // through to the surface underneath — the pixels the user is about to see
    // there at the next paint, which is the honest place for it to land.
    const uint64_t id = notification_card_at(app, x, y);
    if (id == 0) return false;
    // Consumed from here, whatever the button: the card is opaque.
    if (button != GuiMouseButton::Left) return true;
    // THE CARD IS ONE BUTTON (architect 2026-10-01, "whole card dismisses, X
    // gone"): the press ARMS it and dismisses nothing — the act is the lift's
    // (finish_notification_release), the one stated exception to "every
    // dismissal stays at the press" (AppState::ChromePress's head). The arm
    // carries the press-time SHIFT and the press's stamp, the two roads to
    // the whole-stack dismissal, and nothing else: ctrl and alt bind nothing
    // on a card. No left arm can be standing here — a left press exists only
    // after the previous one came up, and every release takes the arm.
    app.chrome_press = AppState::ChromePress{
        .kind     = AppState::ChromePress::Kind::Card,
        .shift    = mods.shift,
        .press_ms = monotonic_ms(),
        .card_id  = id};
    // THE HELD CARD'S CLOCK STOPS AT THE PRESS ("holding turns off the
    // timer"), whatever the hover walk has or has not said: the pen's
    // HOVER_EXIT precedes every tip down, and a finger has no hover until the
    // touch translation's entry motion — which does hover the card just
    // before this press, so the hold is then the bank's second reason and
    // the one that survives a slide-away. Nothing paints: the card has no
    // pressed face.
    notifications.press_hold_edge(id);
    return true;
}

// THE CARD'S LIFT — the press claim's other half (architect 2026-10-01),
// called by on_button_release at the claim's own rank (above the dropdown's
// release and every veil's) with the arm already taken, so the card's clock
// has resumed before this runs (take_chrome_press) and a dismissal finds it
// running or gone. The lift dismisses iff it lands ON THE SAME CARD the
// press armed — re-hit at the release's own coordinates against the last
// paint's publication and the live stack (notification_card_at), the derive
// doctrine: a lift beside the card, on another card, or on a card bumped or
// cleared under the hold lands nothing (the chrome's slide-away cancel).
// THE TERM IS THE CHROME SHIFT LONG PRESS'S, read as at the roster's lift:
// the carried press-time shift ORed with a hold past chrome_shift_hold_ms(),
// measured here against the arm's stamp — no timer, no tick, no visual
// announcement — and with it the lift dismisses EVERY card, criticals
// included; without it, this card alone.
void GuiInputHandler::finish_notification_release(
        const AppState::ChromePress& arm, int x, int y) {
    if (arm.kind != AppState::ChromePress::Kind::Card) return;
    if (notification_card_at(app, x, y) != arm.card_id) return;
    const bool all =
        arm.shift || monotonic_ms() - arm.press_ms >= chrome_shift_hold_ms();
    if (all) notifications.dismiss_all();
    else     notifications.dismiss(arm.card_id);
}

// -- THE CAPTION AND THE SIZING FRAME (architect 2026-10-05) -------------------
//
// The contracts are at the declarations (input_handler.h); the geometry is the
// painter's publication (AppState::caption_buttons) and the frame's hit test
// (window_frame_edges_at); the window acts are the platform's verbs.

namespace {
// The caption button under (x, y) as painted, or -1.
int caption_button_at(const AppState& app, int x, int y) {
    for (int i = 0; i < kCaptionButtonCount; ++i)
        if (rect_contains(app.caption_buttons[static_cast<size_t>(i)].rect, x,
                          y))
            return i;
    return -1;
}
} // namespace

bool GuiInputHandler::claim_window_frame_press(GuiMouseButton button, int x,
                                               int y, GuiInputState mods) {
    const unsigned edges = window_frame_edges_at(app, x, y);
    if (edges == 0) return false;
    // Consumed from here, whatever the button and the modifiers: the
    // window's own chrome binds the bare left press alone, and a maximised
    // window's frame (cde's, which stands there too) binds nothing.
    if (button == GuiMouseButton::Left && !mods.ctrl && !mods.shift &&
        !mods.alt && !gui.window_maximized())
        gui.begin_window_resize(edges);
    return true;
}

bool GuiInputHandler::claim_caption_press(
        GuiMouseButton button, int x, int y, GuiInputState mods,
        const DoubleClickCandidate& dc_at_press) {
    if (!rect_contains(top_caption_row_area(app), x, y)) return false;
    // Consumed from here, whatever the button and the modifiers: the
    // window's own chrome binds the bare left press alone.
    if (button != GuiMouseButton::Left || mods.ctrl || mods.shift || mods.alt)
        return true;
    const int b = caption_button_at(app, x, y);
    if (b >= 0) {
        // AS PAINTED: a button painted disabled (the tablet's Restore) takes
        // its press as a consumed nothing, as a greyed roster button does.
        if (!app.caption_buttons[static_cast<size_t>(b)].enabled) return true;
        // UNDER CDE THE CLOSE SLOT IS THE WINDOW-MENU BUTTON (2026-10-08,
        // ruling 3; caption_button_rects' cde arm): it ACTS AT THE PRESS,
        // as the menu row's anchors do — the toggle's open half, the drag
        // into the menu and the lift on a verb being the one gesture (the
        // anchor press's record at the row-1 band claim) — and claims the
        // held button for the popup when a menu came up. A press while the
        // menu stands never arrives here: the open dropdown's claim, ranked
        // above, closes it (a press outside its items dismisses).
        if (static_cast<GuiCaptionButton>(b) == GuiCaptionButton::Close &&
            live_chrome_spec().vocabulary == GuiChromeVocabulary::Cde) {
            toggle_dropdown(DropdownMenu::Window);
            app.dropdown.press_began_on_item = app.dropdown.open();
            viewport.invalidate_rect(top_caption_row_area(app));
            return true;
        }
        app.chrome_press = AppState::ChromePress{
            .kind     = AppState::ChromePress::Kind::Caption,
            .index    = b,
            .press_ms = monotonic_ms()};
        viewport.invalidate_rect(top_caption_row_area(app));
        return true;
    }
    // THE CAPTION'S GROUND: only a window that can leave the maximised state
    // answers (the tablet's takes no drag and no double tap).
    if (!gui.window_restorable()) return true;
    // THE SECOND PRESS OF A DOUBLE CLICK toggles maximised and arms nothing
    // (the rule at DoubleClickCandidate; the candidate was cleared at the
    // press's head, dc_at_press keeping it).
    const int slack = double_click_slack_px();
    if (dc_at_press.surface == DoubleClickSurface::Caption &&
        monotonic_ms() - dc_at_press.time_ms <= kDoubleClickMs &&
        std::abs(x - dc_at_press.press_x) <= slack &&
        std::abs(y - dc_at_press.press_y) <= slack) {
        gui.toggle_window_maximized();
        return true;
    }
    // THE FIRST PRESS seeds the candidate here, at the press (the reason at
    // DoubleClickSurface::Caption's entry), then hands the drag to the
    // compositor — on a restored window only.
    app.double_click = DoubleClickCandidate{
        .surface = DoubleClickSurface::Caption,
        .time_ms = monotonic_ms(),
        .press_x = x,
        .press_y = y};
    if (!gui.window_maximized()) gui.begin_window_move();
    return true;
}

void GuiInputHandler::finish_caption_release(const AppState::ChromePress& arm,
                                             int x, int y) {
    if (arm.kind != AppState::ChromePress::Kind::Caption) return;
    // A PROMPT OR A DIALOG EDITOR RAISED UNDER THE HOLD (a key, the
    // compositor's close) outranks the arm: the lift does nothing, the veil's
    // rule for every chrome lift (finish_chrome_press_release's head).
    if (app.prompt.active || modal_dialog_editor_active()) return;
    if (caption_button_at(app, x, y) != arm.index) return;
    if (!app.caption_buttons[static_cast<size_t>(arm.index)].enabled) return;
    switch (static_cast<GuiCaptionButton>(arm.index)) {
    case GuiCaptionButton::Minimize: gui.minimize_window();         return;
    case GuiCaptionButton::Maximize: gui.toggle_window_maximized(); return;
    // (Under cde the Close slot is the window-menu button, which acts at
    // the press and arms nothing — claim_caption_press — so this arm is
    // the other two vocabularies' Close.)
    case GuiCaptionButton::Close:    on_window_close();             return;
    }
}

void GuiInputHandler::on_window_close() {
    // The window close (the compositor's, or the caption's Close) routes
    // through the unsaved-work dialog when dirty, same as Ctrl+Q. END any
    // in-flight pointer
    // gesture before the prompt goes up, matching the Ctrl+Q
    // hatch: while the prompt is up the pointer handlers swallow motion
    // and release, so a gesture left alive would commit on the next motion
    // if the user dismisses the prompt. ENDING IS COMMITTING (pointer
    // gestures have no cancel — the rule is at the drag-modal gate in
    // input_handler.cpp): each gesture runs its own release body here.
    // finalize_active_drags is a no-op when nothing is live, so the clean and
    // non-gesture close paths are unaffected — the caption's Close among
    // them, whose lift ended its own press. A live editor text-selection
    // drag is finalized there too (collapsed to a caret, selection-only,
    // nothing to revert), so there is no motion-free interval where it would
    // swallow keys until a later pointer motion noticed the lost button.
    finalize_active_drags();
    // THE HINT GOES DOWN WITH IT, and this is the SAME RULE AS on_key's
    // MODAL-OPENING END rather than a new one: no floating hint stands over a
    // modal. Every KEYBOARD opener meets it at on_key's every return and every
    // pointer opener at its press; the compositor's close is THE ONE modal
    // opener that arrives asynchronously — it carries no key and no pointer
    // event to hide with — so the rule needs its call here (a HARD end) or
    // the hint stands over the prompt until the tick's walk finds no owner
    // under it. (The checkpoint worker's
    // failure report was a second such opener from 2026-08-07 until
    // 2026-08-09, when it became the bottom row's paint-only critical slot and
    // stopped raising anything; it is a critical notification card since
    // 2026-08-29, which raises no modal either.)
    // Ordered ABOVE request_close so the box's published rect is
    // damaged before the prompt's own repaint, and beside the popup close for
    // the reason below — the two floating surfaces go down together.
    hide_shift_tooltip();
    // THE POPUP GOES DOWN BEFORE THE PROMPT GOES UP — including its armed
    // item, both being the one close owner's job. Without this the two would
    // stand together and ownership would
    // SPLIT: the prompt takes keys and presses (its gates are tested first),
    // but motion reaches the DROPDOWN branch, which sits above the prompt's,
    // and a left RELEASE reaches finish_dropdown_release, which sits above
    // the prompt gate in on_button_release — so an item pressed and still
    // HELD when the compositor close arrived would fire on release and raise
    // the settings editor UNDERNEATH the prompt. Closing here makes "the
    // prompt outranks the dropdown" structural in all four input channels
    // instead of an ordering accident in two of them.
    // THE RESIZE PATH DOES THE EQUIVALENT for the same class of reason (a
    // popup that cannot stay coherent through what follows), and Ctrl+Q needs
    // no line of its own: it reaches the popup's own keyboard gate first,
    // which closes the menu and only then lets the close route run. (The
    // checkpoint notice's opener owed this same pair from 2026-08-08 until
    // 2026-08-09; it raises no modal now, so these two routes are again the
    // whole list.)
    close_dropdown();
    // THE RENDER PLAYER, THE PICKER AND EVERY STANDING MODAL EDITOR GO
    // DOWN INSIDE request_close, not here: the close road owns those
    // steps for Ctrl+Q and for this road alike, so neither a mode
    // nor an editor — nor either overlay band — is left standing under
    // the unsaved-work prompt.
    prompt.request_close(GuiCloseTarget::Exit);
    // (The cursor re-resolve this road used to end with is gone for the
    // same reason the resize path's is — see there. The prompt this may have
    // just raised is one of the zone map's own refusals, and the map is asked
    // again at this iteration's tail, past everything above.)
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
// box's inset at both ends. THE THUMB DRAGS, WINDOWS' TRACKBAR (architect
// 2026-10-02 — the fix for "the thumb doesn't drag"; the drag itself stood
// since the player's birth, but only from the handle's own box, and a press
// on the track elsewhere seeked at the press and armed nothing): EVERY PRESS
// ON THE ITEM ARMS THE THUMB DRAG. On THE THUMB'S GRAB BAND — the painted
// thumb widened to its 14 Windows px box, through the one test
// (render_player_scrub_handle_hit) — the thumb is TAKEN WHERE IT IS, the grab
// offset kept so it does not jump under the pointer; ANYWHERE ELSE on the
// item's band the thumb JUMPS to the press's column and the same press
// continues as the drag. Either way the thumb's painted x follows the
// pointer while the sound continues where it was, and the RELEASE commits
// the seek to the thumb's carried column, the product's deferred-click shape
// — so a tap on the track seeks at the lift, and a motionless tap on the
// thumb itself seeks nothing (Windows: clicking the thumb moves nothing).

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
    // the overlay's ruled pad-and-gap silence, notifications.h): the answer is
    // that the plain press works, and a chord on a slider is not an act
    // anyone spelled.
    if (mods.ctrl || mods.shift || mods.alt) return true;
    // THE TWO STATE REFUSALS ARE SILENT (architect 2026-08-31): they are
    // the seek's own two, met here instead of at seek_to because the press
    // must not ARM the thumb drag either, and they went silent with seek_to's
    // — a slider resting at the left end under a zeroed clock IS the state
    // both name, so a sentence only repeated what the press was already
    // looking at (the retirement record is at render_player.h, where the two
    // shared sentences used to be spelled).
    if (app.render_player.frames <= 0 || app.render_player.item.empty())
        return true;
    // THE SCRUB RESTS WHILE THE TRANSPORT IS IDLE (architect 2026-08-29,
    // Audacious's own slider — dead while stopped): the press seeks nothing
    // and arms no thumb drag, and the painter keeps drawing the thumb at the
    // resting point. LIVE and PAUSED are unchanged.
    if (app.render_player.transport ==
        AppState::RenderPlayer::Transport::Idle)
        return true;
    // THE THUMB IS RESOLVED WHERE IT IS PAINTED (ON SCREEN IS AS PAINTED,
    // the displayed-not-live rule displayed_or_live_target_map states,
    // app_state.h): the painter's published column, never the live position,
    // which under a LIVE transport runs a tick ahead of the pixels. The live
    // read was older than the jump, but since every off-thumb press makes the
    // thumb JUMP (2026-10-02) a wrong verdict would move it. A stash with no
    // thumb (-1) has none to take, so the press jumps.
    const int marker_x = app.modal_dialog.scrub_thumb_x;
    // THE ARM, ON THE THUMB OR OFF IT (the block above): the grab offset is
    // the press's distance from the thumb's centre on the grab band and zero
    // elsewhere, so the thumb jumps to an off-thumb press and keeps its seat
    // under an on-thumb one.
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    const bool on_thumb =
        marker_x >= 0 && render_player_scrub_handle_hit(track, marker_x, x, y);
    drag          = AppState::RenderPlayer::ScrubDrag{};
    drag.armed    = true;
    drag.on_thumb = on_thumb;
    drag.grab_dx  = on_thumb ? x - marker_x : 0;
    // THE CARRIED x IS A THUMB CENTRE, so it is clamped onto the thumb's OWN
    // TRAVEL and not onto the item — the painter draws the thumb at it, and a
    // centre past either inset would hang the thumb off its channel (the
    // travel is the mapping's, one owner: render_player_scrub_usable_span).
    drag.marker_x = clamp_player_scrub_marker_x(x - drag.grab_dx);
    viewport.invalidate_rect(track);
    return true;
}

void GuiInputHandler::update_player_scrub_motion(int x) {
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return;
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w <= 0) return;
    const int mx = clamp_player_scrub_marker_x(x - drag.grab_dx);
    if (mx == drag.marker_x) return;
    drag.marker_x = mx;
    drag.moved    = true;
    viewport.invalidate_rect(track);
}

bool GuiInputHandler::finish_player_scrub_release(int x, int y) {
    (void)y;
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return false;
    const AppState::RenderPlayer::ScrubDrag ended = drag;
    drag = AppState::RenderPlayer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
    // A MOTIONLESS TAP ON THE THUMB SEEKS NOTHING: the thumb was taken where
    // it stood and never moved, and re-seeking its own column would round the
    // position to that column's frame.
    if (ended.on_thumb && !ended.moved) return true;
    // THE COMMIT: the seek to the thumb's column at the lift — the lift's x
    // less the grab offset, clamped onto the travel by the mapping, which is
    // the column the motion last painted the thumb at.
    render_player.seek_to(render_player_scrub_frame_at(app, x - ended.grab_dx));
    return true;
}

void GuiInputHandler::clear_player_scrub_drag() {
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return;
    drag = AppState::RenderPlayer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
}

// -- THE COLOR PICKER'S POINTER BODIES (architect 2026-10-07; the contract
//    is at the declarations, input_handler.h; the acts are GuiColorPicker's,
//    color_picker.h) ------------------------------------------------------

namespace {
// THE ROSTER'S CHORD SPAN (GuiInputHandler::roster_chord_in_flight_, whose
// declaration carries the rule): set for one roster button's dispatch, the
// previous value restored at the scope's end.
struct RosterChordScope {
    bool& flag;
    bool  was;
    explicit RosterChordScope(bool& flag_) : flag(flag_), was(flag_) {
        flag = true;
    }
    ~RosterChordScope() { flag = was; }
};
// The row of the chooser's list under (x, y) as painted, or -1.
int color_picker_list_hit(const AppState::ColorPicker::Stash& st, int x,
                          int y) {
    if (st.list.w <= 0 || st.list.h <= 0) return -1;
    for (std::size_t i = 0; i < st.list_items.size(); ++i)
        if (rect_contains(st.list_items[i], x, y)) return static_cast<int>(i);
    return -1;
}
// The row of the palette menu under (x, y) as painted, or -1.
int color_picker_menu_hit(const AppState::ColorPicker::Stash& st, int x,
                          int y) {
    if (st.menu.w <= 0 || st.menu.h <= 0) return -1;
    for (std::size_t i = 0; i < st.menu_rows.size(); ++i)
        if (rect_contains(st.menu_rows[i].rect, x, y))
            return static_cast<int>(i);
    return -1;
}
// THE POPUP LISTS' SCROLL BAR UNDER THE POINTER (render.h's popup scroll
// block, the rule's one owner; ONE BODY for the three lists): a press on the
// PUBLISHED bar `bar` is the bar's whatever its modifiers — a plain one
// scrolls a row (an arrow, held for its pressed face until the lift), a page
// (the track) or starts the thumb's drag (the press's offset on the thumb
// kept); a modified one is a consumed nothing. Returns whether the press was
// on the bar; the caller damages the box.
bool popup_scroll_press(PopupScroll& sc, const PopupScrollBar& bar, int x,
                        int y, bool plain) {
    if (!bar.present || !rect_contains(bar.bar, x, y)) return false;
    if (!plain) return true;
    const PopupScrollPart part = popup_scroll_hit(bar, x, y);
    sc.top = popup_scroll_step(bar, part);
    sc.held = PopupScrollPart::None;
    if (part == PopupScrollPart::Up || part == PopupScrollPart::Down)
        sc.held = part;
    if (part == PopupScrollPart::Thumb) {
        sc.held    = PopupScrollPart::Thumb;
        sc.grab_dy = y - bar.thumb.y;
    }
    return true;
}
// The thumb drag's carry: the rows follow the thumb under the pointer's row
// less the grab (popup_scroll_top_at_thumb on the published bar). Returns
// whether the shown rows moved.
bool popup_scroll_drag(PopupScroll& sc, const PopupScrollBar& bar, int y) {
    if (sc.held != PopupScrollPart::Thumb) return false;
    const int top = popup_scroll_top_at_thumb(bar, y - sc.grab_dy);
    if (top == sc.top) return false;
    sc.top = top;
    return true;
}
// The wheel's circles as painted, in the layout shape the reads take.
color_picker::Layout wheel_layout_of(const AppState::ColorPicker::Stash& st) {
    color_picker::Layout l;
    l.wheel   = st.wheel;
    l.cx      = st.wheel_cx;
    l.cy      = st.wheel_cy;
    l.outer_r = st.wheel_outer_r;
    l.inner_r = st.wheel_inner_r;
    return l;
}
} // namespace

void GuiInputHandler::close_color_picker() { color_picker.close(); }

void GuiInputHandler::confirm_color_picker_delete() {
    color_picker.confirm_delete();
}

void GuiInputHandler::cancel_color_picker_delete() {
    color_picker.cancel_delete();
}

void GuiInputHandler::set_color_picker_list_open(bool open) {
    AppState::ColorPicker& cp = app.color_picker;
    if (cp.chooser_open == open) return;
    // THE DAMAGE ON BOTH EDGES is the list's area: on the close the painted
    // list's rect (the stash's, what has to be erased), on the open the
    // rect the next paint will give it (the layout's — the one live
    // derivation here, for DAMAGE alone, never for a hit).
    // THE SCROLL STARTS AT THE HEAD, the shown element scrolled into view
    // (render.h's popup scroll block: a combo's one exception) — the
    // layout's shown count, derived here for that seat alone.
    if (!open) color_picker.damage_card();
    cp.chooser_open    = open;
    cp.chooser_hover   = open ? static_cast<int>(cp.element) : -1;
    cp.chooser_pressed = -1;
    cp.chooser_press_began_on_item = false;
    cp.chooser_scroll  = PopupScroll{};
    if (open) {
        const color_picker::Layout l =
            color_picker::layout(app, gui_font(GuiFace::Body));
        cp.chooser_scroll.top = popup_scroll_reveal(
            l.list.bar, 0, static_cast<int>(cp.element));
        viewport.invalidate_rect(l.list.box);
        color_picker.damage_card();
    }
}

void GuiInputHandler::set_color_picker_menu_open(bool open) {
    AppState::ColorPicker& cp = app.color_picker;
    if (cp.menu_open == open) return;
    // THE LIST'S DAMAGE RULE ONE SURFACE OVER (above), and THE LIT ROW
    // STARTS ON THE ACTIVE PRESET'S NAME (color_picker.h's THE PALETTE MENU:
    // the chooser's hover seed), found among the menu's rows — THE SCROLL
    // AT THE HEAD, the acts in view (render.h's popup scroll block: the
    // menu is no combo, so its lit name may lie below the shown rows).
    if (!open) color_picker.damage_card();
    cp.menu_open    = open;
    cp.menu_hover   = -1;
    cp.menu_pressed = -1;
    cp.menu_press_began_on_item = false;
    cp.menu_scroll  = PopupScroll{};
    if (open) {
        const color_picker::Layout l =
            color_picker::layout(app, gui_font(GuiFace::Body));
        const std::string_view active = color_picker::active_palette(app);
        for (std::size_t i = 0; i < l.menu_rows.size(); ++i)
            if (!l.menu_rows[i].is_act && l.menu_rows[i].name == active)
                cp.menu_hover = static_cast<int>(i);
        viewport.invalidate_rect(l.menu);
        color_picker.damage_card();
    }
}

void GuiInputHandler::color_picker_press(int x, int y, GuiInputState mods,
                                         const DoubleClickCandidate& dc_at_press) {
    AppState::ColorPicker& cp = app.color_picker;
    const AppState::ColorPicker::Stash& st = cp.stash;
    cp.field_caret_press = false;   // only the caret arm below sets it
    // PUBLISHED GEOMETRY MAY ONLY SELECT, and only the live session's: an
    // unpublished or stale stash contains no point (the owner-tag doctrine,
    // ModalDialogGeometry), so the press is the veil's consumed nothing.
    if (!st.valid || st.session != cp.session) return;

    // THE MENU FIRST, while it is down (the dropdown's own rank): a press
    // on a live row arms it for the lift, a press on a grayed row is a
    // consumed nothing with the menu standing (Windows' grayed menu item),
    // anywhere else closes the menu and is consumed.
    const bool plain = !mods.ctrl && !mods.shift && !mods.alt;
    if (cp.menu_open) {
        // ITS SCROLL BAR FIRST (render.h's popup scroll block): a press on
        // the bar scrolls and keeps the menu down.
        if (popup_scroll_press(cp.menu_scroll, st.menu_bar, x, y, plain)) {
            color_picker.damage_card();
            return;
        }
        const int hit = color_picker_menu_hit(st, x, y);
        if (hit >= 0 && plain) {
            if (st.menu_rows[static_cast<std::size_t>(hit)].enabled) {
                cp.menu_pressed = hit;
                cp.menu_hover   = hit;
                cp.menu_press_began_on_item = true;
                color_picker.damage_card();
            }
            return;
        }
        set_color_picker_menu_open(false);
        return;
    }
    // THE LIST, while it is down, the same rank: a press on a row arms it
    // for the lift, anywhere else closes the list and is consumed —
    // nothing underneath acts.
    if (cp.chooser_open) {
        if (popup_scroll_press(cp.chooser_scroll, st.list_bar, x, y, plain)) {
            color_picker.damage_card();
            return;
        }
        const int hit = color_picker_list_hit(st, x, y);
        if (hit >= 0 && plain) {
            cp.chooser_pressed = hit;
            cp.chooser_press_began_on_item = true;
            color_picker.damage_card();
            return;
        }
        set_color_picker_list_open(false);
        return;
    }

    // A STANDING EDIT ENDS AT ANY PRESS OUTSIDE ITS FIELD — the hex field or
    // the name ask abandoned, the flag editor's own rule for a press outside
    // its box (the field shows NEW again, or the hex field and the swatches
    // return); the press then goes on to what it landed on, AS PAINTED: the
    // frame it was painted from had the buttons gray and no swatches under a
    // name ask, so a press there arms nothing.
    const bool on_field = rect_contains(st.field, x, y);
    if (color_picker.field_active() && !on_field) color_picker.field_cancel();

    // A CHORD ON THE CARD IS A CONSUMED NOTHING (the scrub's rule: the plain
    // press works, and a modified press on a slider is not an act anyone
    // spelled); and off the card the veil consumes.
    if (mods.ctrl || mods.shift || mods.alt) return;
    if (!rect_contains(st.card, x, y)) return;

    if (on_field) {
        // THE DOUBLE TAP SELECTS (architect 2026-10-08: "double-tapping the
        // hex field doesn't allow me to select it as I would expect with an
        // input field"): every editor's road (the dialog editors' press arm,
        // editor_double_press_at and select_word_at) on the card's one
        // field, the hex field and the name ask alike — a second press
        // inside the double-click window and slack of the candidate a
        // caret-seating press's lift seeded (color_picker_release) selects
        // the run under it. On glass the press arrives at the down (a down
        // outside every dialog editor's field and off the pan zone is the
        // pointer's at once, input_core.cpp), so the double tap is this
        // same pair of presses. The OPEN keeps its own face: the first
        // press enters with the whole text selected (field_focus) and seeds
        // nothing, its selection standing. No drag by words: the card's
        // field has no drag road.
        if (color_picker.field_active() &&
            editor_double_press_at(dc_at_press, x, y)) {
            color_picker.field_select_word(x);
            return;
        }
        const bool seats_caret = color_picker.field_active();
        color_picker.field_focus(x);
        cp.field_caret_press = seats_caret;
        return;
    }
    if (rect_contains(st.chooser, x, y)) {
        set_color_picker_list_open(true);
        return;
    }
    // THE PALETTE MENU BUTTON drops its menu at the press, the chooser's
    // road, when it was painted live.
    if (rect_contains(st.menu_button, x, y)) {
        if (st.menu_button_enabled) set_color_picker_menu_open(true);
        return;
    }
    // A SLIDER'S TRACK: the thumb is resolved where it is painted (the
    // stash's column), the grab offset kept on the thumb and zero off it —
    // so the thumb jumps to an off-thumb press and keeps its seat under an
    // on-thumb one — and the first value applies at the press, LIVE.
    const int half = scrub_handle_box_px() / 2;
    for (int i = 0; i < color_picker::kChannelCount; ++i) {
        const AppState::ColorPicker::SliderStash& sl =
            st.sliders[static_cast<std::size_t>(i)];
        if (!rect_contains(sl.track, x, y)) continue;
        const bool on_thumb = sl.thumb_x >= 0 && std::abs(x - sl.thumb_x) <= half;
        cp.drag = AppState::ColorPicker::Drag{};
        cp.drag.kind    = AppState::ColorPicker::Drag::Kind::Slider;
        cp.drag.slider  = i;
        cp.drag.grab_dx = on_thumb ? x - sl.thumb_x : 0;
        const color_picker::Channel c = color_picker::channel_at(i);
        color_picker.set_channel(
            c, color_picker::slider_value_at(sl.track, x - cp.drag.grab_dx,
                                             color_picker::channel_max(c)));
        return;
    }
    // THE WHEEL: a press in the ring's annulus takes the hue, a press
    // inside the triangle takes (s, v) — GtkHSV's own two surfaces; the
    // square's corners and the gaps between take nothing.
    if (rect_contains(st.wheel, x, y)) {
        const color_picker::Layout l = wheel_layout_of(st);
        if (color_picker::wheel_in_ring(l, x, y)) {
            cp.drag = AppState::ColorPicker::Drag{};
            cp.drag.kind = AppState::ColorPicker::Drag::Kind::Hue;
            color_picker.set_hue(color_picker::wheel_hue_at(l, x, y));
            return;
        }
        if (color_picker::wheel_in_triangle(l, cp.hue_deg, x, y)) {
            cp.drag = AppState::ColorPicker::Drag{};
            cp.drag.kind = AppState::ColorPicker::Drag::Kind::Triangle;
            double s = 0.0, v = 0.0;
            color_picker::wheel_sv_at(l, cp.hue_deg, x, y, s, v);
            color_picker.set_sv(s, v);
            return;
        }
        return;
    }
    if (rect_contains(st.swatch_old, x, y)) {
        color_picker.revert_to_old();
        return;
    }
    // THE THREE PUSH BUTTONS, the dialogs' shared arm (the painted enabled
    // bit decides, the lift dispatches).
    if (modal_dialog_stash_current()) arm_modal_dialog_press(x, y);
}

void GuiInputHandler::color_picker_motion(int x, int y, GuiInputState mods) {
    AppState::ColorPicker& cp = app.color_picker;
    // A HELD SCROLL PART owns the motion (render.h's popup scroll block): the
    // thumb's drag carries the rows, a held arrow keeps its face to the
    // lift; the button lost has already ended either (on_motion's head,
    // clear_release_time_press_arms).
    if (cp.menu_scroll.live() || cp.chooser_scroll.live()) {
        if (popup_scroll_drag(cp.menu_scroll, cp.stash.menu_bar, y) ||
            popup_scroll_drag(cp.chooser_scroll, cp.stash.list_bar, y))
            color_picker.damage_card();
        return;
    }
    if (cp.drag.armed()) {
        if (!mods.primary_button_held) {
            clear_color_picker_drag();
            return;
        }
        const AppState::ColorPicker::Stash& st = cp.stash;
        switch (cp.drag.kind) {
            case AppState::ColorPicker::Drag::Kind::Slider: {
                const AppState::ColorPicker::SliderStash& sl =
                    st.sliders[static_cast<std::size_t>(cp.drag.slider)];
                const color_picker::Channel c =
                    color_picker::channel_at(cp.drag.slider);
                color_picker.set_channel(
                    c, color_picker::slider_value_at(
                           sl.track, x - cp.drag.grab_dx,
                           color_picker::channel_max(c)));
                return;
            }
            case AppState::ColorPicker::Drag::Kind::Hue:
                color_picker.set_hue(
                    color_picker::wheel_hue_at(wheel_layout_of(st), x, y));
                return;
            case AppState::ColorPicker::Drag::Kind::Triangle: {
                double s = 0.0, v = 0.0;
                color_picker::wheel_sv_at(wheel_layout_of(st), cp.hue_deg, x, y,
                                          s, v);
                color_picker.set_sv(s, v);
                return;
            }
            case AppState::ColorPicker::Drag::Kind::None:
                return;
        }
        return;
    }
    if (cp.chooser_open) {
        const int hit = color_picker_list_hit(cp.stash, x, y);
        if (hit != cp.chooser_hover) {
            cp.chooser_hover = hit;
            color_picker.damage_card();
        }
    }
    if (cp.menu_open) {
        // The lit row follows the pointer onto live rows alone (a grayed row
        // is never lit, the dropdown's gate) and goes dark off the rows —
        // the chooser's own walk.
        const int hit = color_picker_menu_hit(cp.stash, x, y);
        const int lit =
            hit >= 0 && !cp.stash.menu_rows[static_cast<std::size_t>(hit)].enabled
                ? -1 : hit;
        if (lit != cp.menu_hover) {
            cp.menu_hover = lit;
            if (cp.menu_pressed >= 0) cp.menu_pressed = lit;
            color_picker.damage_card();
        }
    }
    update_modal_dialog_hover(x, y);
    recompute_redesign_button_hover();
}

void GuiInputHandler::color_picker_release(int x, int y) {
    AppState::ColorPicker& cp = app.color_picker;
    // THE DOUBLE TAP'S SEED (2026-10-08; the press arm's rule): the lift of
    // a press that seated the field's caret seeds the editor-text candidate
    // while the edit stands with no selection — the dialog editors' own
    // release rule — and the second press reads it.
    if (cp.field_caret_press) {
        cp.field_caret_press = false;
        if (color_picker.field_active() &&
            !text_editor::has_selection(cp.field_editor)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        return;
    }
    if (cp.drag.armed()) {
        // Every step applied live; the lift only ends the gesture.
        cp.drag = AppState::ColorPicker::Drag{};
        return;
    }
    // A SCROLL PART'S LIFT ends its hold and selects nothing (render.h's
    // popup scroll block): the arrow's face comes up, the thumb's drag ends.
    if (cp.menu_scroll.live() || cp.chooser_scroll.live()) {
        clear_popup_scroll_holds();
        return;
    }
    if (cp.menu_open && cp.menu_press_began_on_item) {
        // THE MENU'S LIFT: the row under it as painted, when live, acts —
        // the menu closed first, so a Save As's field, a Delete's prompt or
        // a load's repaint meets the card with nothing floating over it.
        const int hit = color_picker_menu_hit(cp.stash, x, y);
        AppState::ColorPicker::MenuRowStash row;
        if (hit >= 0) row = cp.stash.menu_rows[static_cast<std::size_t>(hit)];
        set_color_picker_menu_open(false);
        if (hit < 0 || !row.enabled) return;
        if (!row.is_act) {
            color_picker.load_palette(row.name);
            return;
        }
        switch (color_picker::palette_act_at(row.act)) {
            case color_picker::PaletteAct::Save:
                color_picker.save_palette();
                return;
            case color_picker::PaletteAct::SaveAs:
                color_picker.begin_name_ask(
                    AppState::ColorPicker::NameAsk::SaveAs);
                return;
            case color_picker::PaletteAct::Rename:
                color_picker.begin_name_ask(
                    AppState::ColorPicker::NameAsk::Rename);
                return;
            case color_picker::PaletteAct::Delete:
                color_picker.raise_delete();
                return;
        }
        return;
    }
    if (cp.chooser_open && cp.chooser_press_began_on_item) {
        const int hit = color_picker_list_hit(cp.stash, x, y);
        cp.chooser_pressed = -1;
        cp.chooser_press_began_on_item = false;
        if (hit >= 0) color_picker.set_element(static_cast<std::size_t>(hit));
        set_color_picker_list_open(false);
        return;
    }
    dispatch_modal_dialog_button(take_modal_dialog_release(x, y));
}

void GuiInputHandler::clear_color_picker_drag() {
    AppState::ColorPicker& cp = app.color_picker;
    if (!cp.drag.armed()) return;
    cp.drag = AppState::ColorPicker::Drag{};
}

void GuiInputHandler::clear_popup_scroll_holds() {
    AppState::ColorPicker& cp = app.color_picker;
    if (cp.menu_scroll.live() || cp.chooser_scroll.live()) {
        cp.menu_scroll.held    = PopupScrollPart::None;
        cp.chooser_scroll.held = PopupScrollPart::None;
        color_picker.damage_card();
    }
    PopupScroll& ls = app.settings_choice.list_scroll;
    if (ls.live()) {
        ls.held = PopupScrollPart::None;
        viewport.invalidate_modal_dialog_area();
    }
}

// -- THE SETTINGS CHOICE EDITOR'S POINTER BODIES (2026-10-07 evening; the
//    contract at the declarations, input_handler.h; the design and the acts
//    at GuiSettingsEditor, settings_editor.h) -----------------------------

namespace {
// The dropped list's row under (x, y) as painted, or -1.
int settings_choice_list_hit(const AppState::ModalDialogGeometry& dlg, int x,
                             int y) {
    if (dlg.combo_list.w <= 0 || dlg.combo_list.h <= 0) return -1;
    for (std::size_t i = 0; i < dlg.combo_list_items.size(); ++i)
        if (rect_contains(dlg.combo_list_items[i], x, y))
            return static_cast<int>(i);
    return -1;
}
} // namespace

bool GuiInputHandler::claim_settings_choice_press(GuiMouseButton button,
                                                  int x, int y,
                                                  GuiInputState mods) {
    if (!app.settings_choice_live()) return false;
    const bool plain_left = button == GuiMouseButton::Left && !mods.ctrl &&
                            !mods.shift && !mods.alt;
    // PUBLISHED GEOMETRY MAY ONLY SELECT, and only the live session's (the
    // owner-tag doctrine, ModalDialogGeometry): a stale stash holds no row
    // and no combo.
    const bool current = modal_dialog_stash_current();
    if (app.settings_choice.list_open) {
        // ITS SCROLL BAR FIRST (render.h's popup scroll block), as painted:
        // a press on it scrolls and keeps the list down.
        if (current &&
            popup_scroll_press(app.settings_choice.list_scroll,
                               app.modal_dialog.combo_list_bar, x, y,
                               plain_left)) {
            viewport.invalidate_modal_dialog_area();
            return true;
        }
        const int hit = current ? settings_choice_list_hit(app.modal_dialog, x, y)
                                : -1;
        if (plain_left && hit >= 0) settings_editor.choice_arm_row(hit);
        else                        settings_editor.set_choice_list_open(false);
        return true;
    }
    if (plain_left && current && rect_contains(app.modal_dialog.combo, x, y)) {
        // THE COMBO TAKES THE FOCUS BACK AT THE PRESS, the field's own rule
        // (return_modal_focus_to_field), then drops its list.
        (void)return_modal_focus_to_field();
        settings_editor.set_choice_list_open(true);
        return true;
    }
    return false;
}

bool GuiInputHandler::finish_settings_choice_release(int x, int y) {
    if (!app.settings_choice_live()) return false;
    const AppState::SettingsChoice& ch = app.settings_choice;
    // A SCROLL PART'S LIFT ends its hold and selects nothing (render.h's
    // popup scroll block).
    if (ch.list_scroll.live()) {
        clear_popup_scroll_holds();
        return true;
    }
    if (!ch.list_open || !ch.list_press_began_on_item) return false;
    const int hit = modal_dialog_stash_current()
                        ? settings_choice_list_hit(app.modal_dialog, x, y)
                        : -1;
    if (hit >= 0) settings_editor.choice_commit(hit);
    else          settings_editor.set_choice_list_open(false);
    return true;
}

void GuiInputHandler::settings_choice_motion(int x, int y) {
    if (!app.settings_choice_live() || !app.settings_choice.list_open) return;
    // A HELD SCROLL PART owns the motion (the picker's rule, its button-lost
    // end at on_motion's head).
    if (app.settings_choice.list_scroll.live()) {
        if (popup_scroll_drag(app.settings_choice.list_scroll,
                              app.modal_dialog.combo_list_bar, y))
            viewport.invalidate_modal_dialog_area();
        return;
    }
    settings_editor.choice_hover(
        modal_dialog_stash_current()
            ? settings_choice_list_hit(app.modal_dialog, x, y) : -1);
}


void GuiInputHandler::on_button_press(GuiMouseButton button, int x, int y,
                                      GuiInputState mods) {
    // ANY PRESS IS THE TOOLTIP'S HARD END, above every gate — Windows hides a
    // tooltip at a click ("About Tooltip Controls"), and a hint left floating
    // over the thing the user just clicked is noise. The release is one too
    // (on_button_release); the press is the one that matters, because the
    // hint's job ends the moment the user acts on it, not when they let go;
    // and the held bit is recorded first, so NO WAIT STARTS UNDER THE HELD
    // PRESS (architect 2026-09-29 — AppState::RedesignTooltip). AND THE PRESS
    // SPENDS THE BUTTON UNDER THE POINTER, whether a box stood or a wait was
    // running (architect 2026-10-06, Windows': a clicked tool's hint does not
    // come back until the pointer has left it — the rule is at the model);
    // on no button `hovered` is no owner and nothing is spent.
    app.redesign_tooltip.button_held = mods.primary_button_held;
    hide_shift_tooltip();
    app.redesign_tooltip.spent = app.redesign_tooltip.hovered;
    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate — ALL FOUR at a motionless RELEASE now (TrimBar /
    // EditorText / EmptyLane, the empty lane joining that class 2026-08-12 with
    // its press becoming the placement lane's pending click (the navigation
    // surface's until 2026-09-25, when the empty lane left it — placement
    // lanes above), so only the release knows it stayed a click and a drag
    // that crossed the threshold seeds nothing; MARKER joined 2026-08-15 — and
    // KEPT the release-time seed when
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
    // 2026-08-29): a press on a published card is consumed whole — moves
    // nothing, lands no playhead, opens no drag, reaches nothing underneath —
    // and since 2026-10-01 THE CARD IS ONE BUTTON ("whole card dismisses, X
    // gone"): the LEFT press anywhere on it arms the card's own chrome arm
    // (AppState::ChromePress's Card kind) and its lift dismisses it, or every
    // card when shifted or held (the rule at notifications.h's hit section).
    // The router cannot fork on tap versus click (no origin bit rides a
    // press; GuiInputState carries modifiers alone), so the rule is one for
    // both hosts. Any other button over a card is consumed in the veil's own
    // manner and arms nothing. It ranks above the veils because a card is
    // the message ABOUT the act a veil stands over, not a reach into the
    // veiled surface (the record at the retired reach-through's site above),
    // and ABOVE THE OPEN DROPDOWN'S CLAIM, so a press on a card beside an open
    // menu arms the card and leaves the menu standing; the lift is taken at
    // the same rank in on_button_release, above the dropdown's release, which
    // would otherwise consume it. This return precedes every arm below, so
    // the card's is the only arm the press leaves. ON GLASS the pan zone
    // answers false on a card (touch_point_in_pan_zone), so a finger landing
    // on one resolves to this press ON CONTACT (the off-zone down,
    // input_core.cpp) rather than to the phone-model pan — which is what
    // lets the hold be measured from the contact and the clock stop there —
    // and a second finger beside it is ignored. The geometry read here is
    // the last paint's publication, and the lift asks the live stack whether
    // the id still stands — a card that left between paint and lift lands
    // nothing.
    if (claim_notification_press(button, x, y, mods)) return;

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

    // THE SIZING FRAME, ABOVE EVERY VEIL (architect 2026-10-05: a restored
    // window resizes "always"): a press on the frame lies outside every
    // surface the app paints (window_frame_edges_at), so nothing below can
    // claim it, and the resize it starts is the compositor's — the resize
    // path closes what cannot stay coherent through it (main.cpp's resize
    // callback) — so no prompt, menu or dialog editor needs to refuse it.
    if (claim_window_frame_press(button, x, y, mods)) return;

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

    // THE OPEN DROPDOWN OWNS THE POINTER, claimed above every band because it
    // FLOATS over them: a press on one of its items runs that item, and a press
    // anywhere else closes it and is CONSUMED so nothing underneath acts. The
    // presses it does not swallow are those on the MENU BUTTONS themselves,
    // which fall through to the band claim below: on this menu's own button the
    // toggle closes it — the same gesture that opened it, closing it — and on the
    // OTHER menu's button the toggle switches, closing this one as it opens that
    // one (one popup state, so the switch is free). WHILE A FOLDER-OVERLAY
    // CONTENT STANDS that fall-through reaches the band claim only for the LIVE
    // anchor; a DEAD one is consumed by the veil below instead, which is the
    // same nothing the band claim gave it (toggle_dropdown refuses a dead
    // anchor) — the popup stays up either way.
    //
    // SINCE THE HOVER SWITCHES TOO (2026-08-03, on_motion), that second case is
    // now the rare one from a real pointer: crossing onto the other button has
    // already switched the menu, so the press landing there finds that menu open
    // and TOGGLES IT CLOSED — the ordinary menu-bar answer for pressing the
    // button whose menu is up. Neither route changed; they simply meet here, and
    // a press that arrives with no motion before it still takes the switch
    // through this claim.
    //
    // THE CLAIM'S RANK IS THE FIX FOR THE STRANDED ARM (2026-09-03): it sits
    // BELOW the notification cards, the on-screen keyboard and the prompt gate
    // — the three surfaces that outrank every band — and ABOVE EVERYTHING ELSE
    // the pointer has: the folder overlay's band claim, its two mode veils
    // and the modal-row arm inside each of them, the dialog editors' field and
    // button claims and their veil. It stood under all of those until this
    // ruling, which is what let a press on the overlay's modal row while the
    // File menu was up reach arm_modal_dialog_press BEFORE this claim could
    // dismiss the popup: the release then ran finish_dropdown_release first,
    // and that body consumes every release the popup is open for without
    // taking or clearing the arm, so the button stayed visually pressed and a
    // LATER release over it acted with no press of its own. ONE PRESS, ONE
    // ACT is what the rank now says: a press that dismisses the popup arms no
    // modal button and opens no row. (Touch reaches this order too — the
    // one-finger translation delivers an ordinary press — so the rank answers
    // both hosts.)
    //
    // AND NO ARM CAN BE STANDING WHEN THE POPUP OPENS, which is why the
    // dismissal owes no clear of its own: every producer of an open popup is a
    // LEFT PRESS (the anchor press's toggle_dropdown at the menu-row claim
    // below, and the hover switch under that same held button — there is no
    // keyboard opener), a press exists only after the previous one came up,
    // and both arms a dismissal could strand are consumed at their own release
    // whatever it lands on (take_modal_dialog_release, take_chrome_press). The
    // anchor press moreover arms NOTHING — it toggles instead of calling
    // arm_redesign_press — so the very gesture that opens a menu leaves the
    // roster and the modal row clean.
    //
    // MOVING ABOVE THE EDITOR GATES MOVED NO BEHAVIOR: a popup and a dialog
    // editor are never open together. The popup opens only from a press, and
    // while a DIALOG editor is up every press outside the box's own field and
    // buttons dies at the veil, which has had NO EXCEPTION since 2026-08-13
    // (the retired reach-through's record is at its own site below) — so the
    // MENU ANCHORS are never reached and no press can open a popup under an
    // editor. That invariant is carried by the ANCHOR claim's rank at the foot
    // of this function, which is still under the veil, and never by this
    // claim's. The other half is not here — the
    // pointer-transparent FLAG editor swallows nothing, so a press does reach
    // the menu buttons with an edit open, and toggle_dropdown's open path ENDS
    // that edit (the rule is stated there). Two mechanisms, one claim. (The
    // reverse direction is closed by the keyboard gate: while the popup is open,
    // `;` is swallowed, so the editor cannot open under it either.)
    //
    // THE FOLDER OVERLAY'S BAND IS UNDER IT LIKE EVERY OTHER BAND: a FILE menu
    // can stand over the band since 2026-09-03 evening, and a press on a row
    // while it does closes the menu rather than opening the row — this claim is
    // what that press meets, and the band's own claim below no longer carries a
    // popup line of its own. What still reaches those veils is the LIVE ANCHOR
    // alone (menu_row_press_admitted, below), the menu row standing above the
    // band. THE PANEL STILL CANNOT RISE UNDER A POPUP, which is the other
    // direction and unchanged: every opener the two contents
    // have — bare `l`, bare `'`, Ctrl+O, the Play renders button's press and
    // the File menu's Open project row — either
    // arrives through on_key, whose popup gate admits Esc and Ctrl+Q and
    // swallows the rest, or is a menu row whose own release CLOSES the popup
    // BEFORE it acts. (This claim stood BELOW the mode veils for the evening
    // of 2026-09-03 alone, and below the editor gates from the redesign until
    // then; on 2026-09-02, with the panel's ceiling at row 1's foot, it stood
    // above the veils and no popup could reach the band at all for the hours
    // the anchors were all dead.)
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
            // A GREYED ITEM ARMS NOTHING AND DISMISSES NOTHING (architect
            // 2026-09-24, the truthful menus; it stood 2026-08-08..15 too): the
            // press is consumed where it landed and the MENU STAYS UP,
            // kdenlive's own answer for a greyed row — pressing one is a
            // nothing, not a dismissal — so this arm RETURNS rather than
            // falling into the close below, which is the answer for the
            // separator, the chrome and the box's outside: those are the
            // popup's DEAD SPACE, and a greyed item is a row that is simply not
            // for you. THE CLAIM READS THE PAINTED BIT (architect 2026-09-24,
            // strictly as-painted): the row's face as paint_dropdown last
            // published it (AppState::Dropdown::item_enabled), never the live
            // verdict, so the press does what the row shows; the per-tick
            // comparator (main.cpp) keeps the bit honest. No claim is set, so
            // the release that follows arms nothing either.
            if (hit >= 0 && plain &&
                !app.dropdown.item_enabled[static_cast<size_t>(hit)])
                return;
            if (hit >= 0 && plain) {
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

    // THE FOLDER OVERLAY'S BAND, claimed for EVERY content and ranked here —
    // under the prompt gate (a prompt outranks every surface; the
    // player's load confirmation and the reopen's unsaved-tab question paint
    // over their own rows), under the OPEN DROPDOWN'S claim just above (a
    // standing menu owns the press before any band does, so a row press
    // dismisses the menu instead of opening the row) and above the TWO mode
    // veils below — the player's and the picker's, each of which
    // admits the band and its own modal row
    // and consumes the rest. The claim is opaque and owns its own button
    // gate, so a non-left press on the band falls to whichever veil stands
    // and is consumed there.
    if (claim_folder_overlay_press(x, y, button, mods)) return;

    // THE ONE THING THE TWO VEILS BELOW LET THROUGH (architect 2026-09-03
    // evening): the LIVE MENU ANCHOR — File. The menu row stands above the
    // band with File lit and the other two anchors dead, so its press must
    // reach the menu-row claim at the foot of this function, which sits BELOW
    // these veils, and this one local is what carries it past them. ONE OWNER
    // (press_on_live_menu_anchor, near the head of this file — the same
    // enumeration the FACE and toggle_dropdown's guard read), so the exemption
    // cannot part from the face. What the exempted press meets on the way down
    // is inert — no editor can stand under the band, and the modal-row arm it
    // passes hits no button at row 1's height.
    //
    // IT IS THE ANCHOR HALF ALONE. It carried `app.dropdown.open() ||` until
    // 2026-09-03, when the OPEN POPUP was ranked above these veils and above
    // the band claim (the rank's own site above): an open menu no longer needs
    // the veils to let its presses through, because it has already answered
    // every press before they run. That blanket term was the stranded-arm
    // defect — with it, ANY coordinate was exempt while a menu stood, so a
    // press on the overlay's modal row armed a button under a popup that
    // should have swallowed the press whole.
    //
    // A COLOR PICKER LIST'S BOX OUTRANKS BOTH EXEMPTIONS (2026-10-08, ON
    // SCREEN IS AS PAINTED): the card's element list and its palette menu
    // scroll and may hang up over the menu row and the caption (an upward
    // palette menu at the tablet's 300 % reaches the caption's rows), and
    // the popup is what paints there (paint_modal_dialog after the caption).
    // So while one is down a press inside its published box
    // (color_picker_list_at, app_state.h — the list's box or the menu's,
    // its bar inside it) is admitted by neither exemption and reaches the
    // picker's veil below, whose claim (color_picker_press) answers it as the
    // popup's; a press outside the box meets the exemptions as ever.
    const bool press_in_picker_list = color_picker_list_at(app, x, y);
    const bool menu_row_press_admitted =
        !press_in_picker_list && press_on_live_menu_anchor(app, x, y);
    // AND THE CAPTION (architect 2026-10-05): the window's title bar is
    // reachable wherever the File menu's Quit is, so the two veils let it
    // through beside the live anchor; the claim stands under the dialog
    // editors' swallow below (claim_caption_press).
    const bool caption_press_admitted =
        !press_in_picker_list &&
        rect_contains(top_caption_row_area(app), x, y);
    // THE BUTTON ROWS' CLAIM — the icon row's and row 8's — ONE BODY for
    // their own band claims below and for the color picker's veil, which
    // lets both rows through (architect 2026-10-07 evening for row 8,
    // 2026-10-08 for the icon row).
    const auto claim_button_row_press = [&] {
        if (chrome_band_modifiers_refused(app, x, y, mods)) return;
        if (button == GuiMouseButton::Left) arm_redesign_press(x, y, mods);
    };

    // THE RENDER PLAYER'S VEIL (2026-08-28), under the prompt gate — its load
    // confirmation is a prompt and paints over it — and above everything
    // else: while the mode stands the pointer has FOUR targets, the folder
    // overlay's rows (claimed above), the play-scrub (every press on it arms
    // the thumb drag, the seek at the lift), the modal row's buttons
    // (the arm every dialog button takes) and, since 2026-09-03 evening, the
    // FILE ANCHOR above the band (the exemption above), and EVERY OTHER PRESS
    // IS CONSUMED — the flags, the waveform, the two dead anchors,
    // the dead roster. (File was a target under the panel on
    // 2026-09-02 too, when it stopped at row 1's foot; it was consumed here
    // for the hours between the gap's close and that evening's ruling.)
    // The whole rule is stated at render_player_active (input_handler.h).
    // THE ONE ROW THAT ADMITS SHIFT (2026-08-28): the player's two skips
    // carry a shifted twin — the item folder's ends — so a SHIFT press reaches
    // the arm here and the lift dispatches that twin, the roster's own
    // shift-click over this surface. The arm body applies the admission (a
    // shift press on a button with no twin is a consumed nothing, never the
    // plain act); ctrl and alt spell nothing on any modal row and stay
    // consumed by the veil below.
    if (app.render_player.active && !menu_row_press_admitted &&
        !caption_press_admitted) {
        if (button != GuiMouseButton::Left) return;
        if (claim_player_scrub_press(x, y, mods)) return;
        if (!mods.ctrl && !mods.alt && modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y, mods.shift);
        }
        return;
    }

    // THE PICKER'S VEIL (2026-08-28), the player's shape one mode over: while
    // a picker stands the pointer has THREE targets, the overlay's rows
    // (claimed above), the modal row's one Cancel button (the arm every
    // dialog button takes) and the live File anchor above the band (the
    // exemption above), and EVERY OTHER PRESS IS CONSUMED. A ROW CLICK IS
    // THE OPEN ACT, which is why the row carries no OK beside that Cancel. There is no field
    // and so no caret claim and no text drag — the picker has nothing to
    // type into. The whole rule is stated at picker_active (input_handler.h).
    if (app.picker.active && !menu_row_press_admitted &&
        !caption_press_admitted) {
        if (button != GuiMouseButton::Left) return;
        if (!mods.ctrl && !mods.shift && !mods.alt &&
            modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y);
        }
        return;
    }

    // THE COLOR PICKER'S VEIL (2026-10-07), the two list owners' shape: the
    // card's controls are its claims (color_picker_press, which reads the
    // published stash), the live File anchor and the caption pass as above,
    // and EVERY OTHER PRESS IS CONSUMED but THE TWO BUTTON ROWS'. The
    // on-screen keyboard's keys are claimed above every veil
    // (claim_onscreen_keyboard_press), so the hex field types on glass.
    // THE ICON ROW AND ROW 8 PASS (architect 2026-10-07 evening for row 8, "a
    // different type of modal"; 2026-10-08 for the icon row, "the fact that
    // the top row is disabled under the picker also needs to be updated" —
    // the asymmetry at modal_owns_bottom_row, paint_handler.cpp): a press on
    // either row reaches the rows' own claim, AS PAINTED, unless the card's
    // element list or palette menu is down — a popup owns the pointer whole
    // (the dropdown's ONE PRESS, ONE ACT), and where it hangs over a row the
    // pixel the user sees is the popup's, so the press is
    // color_picker_press's (a row's arm on its rows, the popup's dismissal
    // anywhere else). A standing edit ends at the row's press as at every
    // press outside its field (color_picker_press's rule), so the lift's
    // chord meets no keyboard-modal field. The menu row is not among them:
    // its live File anchor passes above, and its other anchors are dead
    // under the picker (menu_anchor_live) — and neither it nor the caption
    // passes where a list of the card's hangs over them: the two exemptions
    // are computed after the list's box (press_in_picker_list, above).
    if (app.color_picker.active && !menu_row_press_admitted &&
        !caption_press_admitted) {
        if ((rect_contains(top_icon_row_area(app), x, y) ||
             rect_contains(bottom_row_area(app), x, y)) &&
            !app.color_picker.chooser_open && !app.color_picker.menu_open) {
            if (color_picker.field_active()) color_picker.field_cancel();
            claim_button_row_press();
            return;
        }
        if (button != GuiMouseButton::Left) return;
        color_picker_press(x, y, mods, dc_at_press);
        return;
    }

    // (THE MODAL-TRAP REACH-THROUGH STOOD HERE, 2026-08-11..08-13, and is
    // RETIRED — architect: "we can drop the Save reach through". It let a
    // plain left press on a roster button whose chord the editors' modal
    // contract admits as a command arm through the ordinary press body while a
    // dialog editor stood, which is what gave a keyboard-less user on GLASS a
    // way out of an accidentally opened settings editor. THE MODAL ANSWERS
    // THAT ITSELF now: all three editor dialogs publish real OK and CANCEL
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
    // refusal, the BPM commit's render sweep and every teardown are
    // the keyboard's own bodies. A PROMPT's buttons are claimed in the prompt
    // gate above, not here.
    // THE STASH'S IDENTITY IS THIS CLAIM'S GATE (2026-08-13, exact since
    // 2026-08-14): in the one batch between an editor's OPEN and its first
    // paint the stash still belongs to whatever stood before it — a PROMPT,
    // which publishes editor_ok FALSE on every button and so used to CANCEL an
    // editor that had just opened, or ANOTHER EDITOR, whose OK could
    // commit at an unseen dialog. Refusing a stash that
    // does not name the live session closes both (the doctrine and the two
    // identity fields are at ModalDialogGeometry, app_state.h).
    // (The claim needs no modal_dialog_editor_active term of its own and lost
    // the one it carried: the prompt gate above has already returned, so a
    // CURRENT stash here is an editor's by construction.)
    // THE SETTINGS CHOICE EDITOR'S COMBO AND ITS LIST (2026-10-07 evening),
    // ahead of the buttons: a dropped list owns every press (the dropdown's
    // ONE PRESS, ONE ACT — even one on OK or Cancel only closes it), and
    // the combo's press drops it. Anything else goes on to the claims below.
    if (claim_settings_choice_press(button, x, y, mods)) return;
    if (button == GuiMouseButton::Left && !mods.ctrl && !mods.shift &&
        !mods.alt && modal_dialog_stash_current()) {
        if (arm_modal_dialog_press(x, y)) return;
    }

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's field places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the three dialog
    // editors too. ON GLASS ONLY A TAP AND THE DOUBLE PRESS ARRIVE HERE: a
    // finger's drag or hold in the field is the touch translation's caret
    // drag and delivers no press (the trio's bodies beside
    // begin_touch_region). A press outside the active editor's
    // region falls through: the dialog editors stay modal — the VEIL — and
    // swallow it, while the top flag editor closes guard-free below and the
    // press then acts normally.
    if (button == GuiMouseButton::Left) {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            // THE FIELD IS ONE RECT (ActiveEditorText::field): the dialog's
            // inset field — the box around it is chrome, and its buttons were
            // claimed above, so claiming more would map a chrome press to an
            // editor byte — or the marker lane's whole published box, pads
            // included. Any press OUTSIDE it is a non-caret click, which
            // closes the flag editor below and then routes normally (the
            // guard-free lifecycle) or is consumed by the dialog's veil.
            if (rect_contains(g.field, x, y)) {
                // THE FIELD TAKES THE FOCUS BACK AT THE PRESS (architect
                // 2026-10-02; the rule at return_modal_focus_to_field), ahead
                // of all three arms below, the caret's blink restarted so it
                // is lit at once.
                if (g.dialog && return_modal_focus_to_field())
                    text_editor::touch_blink(*g.ed);
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
                // delivery (current_mods, input_core.h), and the on-screen
                // keyboard's Shift is
                // ONE-SHOT FOR THE NEXT CHARACTER KEY — it produces a capital
                // codepoint through shifted_char and sets no modifier bit — so
                // a tablet with no physical shift key has no road onto this
                // act, and none is built. Glass selects by the double tap
                // and, from it, the double-tap-drag by words (below).
                if (mods.shift && !mods.ctrl && !mods.alt) {
                    if (g.ed->selection_anchor < 0)
                        g.ed->selection_anchor = g.ed->cursor_pos;
                    set_editor_caret_from_x(g, x);
                    app.editor_text_drag = EditorTextDragState{};
                    app.editor_text_drag.active       = true;
                    app.editor_text_drag.shift_extend = true;
                    if (g.dialog) viewport.invalidate_modal_dialog_area();
                    else          viewport.invalidate_top_strip();
                    return;
                }
                // Double-click: a second click within the window on this
                // editor's text selects the RUN of the clicked character class
                // (word / punctuation / whitespace) under the click — select_
                // word_at's own classifier, not just a word. The surface tag
                // keeps it from consuming a marker / trim-bar candidate. AND
                // IT ARMS THE DRAG BY WORDS (architect 2026-09-05): the
                // selected run is the anchor, and a drag from this press
                // extends the selection to the boundary of the word under the
                // pointer past it on either side — the desktop editors'
                // double-click-drag. On glass this press arrives at the
                // finger's DOWN (the translation delivers a second press on
                // contact once the seed makes it certain), so a double tap
                // that then drags takes this same arm. It is the one stated
                // exception to "a double-click is never a drag" (the rule at
                // DoubleClickSurface, app_state.h): selection inside an open
                // field, not a drag gesture on the canvas.
                if (editor_double_press_at(dc_at_press, x, y)) {
                    text_editor::select_word_at(
                        *g.ed, editor_byte_index_at(g, x));
                    app.editor_text_drag = EditorTextDragState{
                        .active     = true,
                        .by_words   = true,
                        .word_start = text_editor::selection_start(*g.ed),
                        .word_end   = text_editor::selection_end(*g.ed)};
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
                app.editor_text_drag = EditorTextDragState{};
                app.editor_text_drag.active = true;
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
    // three surfaces again — the set is NAMED once (modal_dialog_editor_active
    // over AppState::dialog_editor_session, whose declaration says so) so it
    // cannot drift, and a fourth dialog editor would be veiled here by existing
    // rather than by an edit. What the swallow buys is the same for all three,
    // and the BPM editor is the case that names it: mouse input does not
    // interact with a dialog beyond its own field and buttons, claimed above,
    // and the session ends only through Esc / the Enter dispatch path / the
    // dialog's Cancel and OK (`m` is just a typed character now) — so the
    // press must not drive a region drag or a marker click, nor tear the
    // editor down through the top-strip flag-edit routine below.
    if (modal_dialog_editor_active()) return;

    // THE CAPTION (architect 2026-10-05), the window's title bar, above
    // every lane of the app (the rank and the acts at claim_caption_press's
    // declaration).
    if (claim_caption_press(button, x, y, mods, dc_at_press)) return;

    // THE FOUR REDESIGNED BUTTON ROWS (top lanes 1..2 plus the bottom strip's
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
    // roster admits it on, CTRL+SHIFT only on the one the roster admits the
    // pair on), a SHIFT press binding only where the roster admits one, and
    // any press in the band that is not on a button
    // a consumed nothing. Each band differs ONLY in its rect
    // and (row 1) in the dropdown toggle of its THREE non-chord buttons — the
    // menu anchors File, Edit and Settings (re-greped
    // 2026-09-09 against kDropdownMenus) — so the press is ONE arm body,
    // arm_redesign_press, driven
    // by the table's per-button flags — and the act one release body,
    // finish_chrome_press_release, in on_button_release.
    //
    // A BUTTON's rect is the painter's stash (app.redesign_buttons, published by
    // paint_menu_row / paint_icon_row /
    // paint_bottom_row_buttons_and_clock; every roster member publishes a real
    // rect on every frame since 2026-08-18, the bottom row's cluster swap —
    // whose unpainted four stashed a zero rect that contains no point, and
    // which was the product's last hiding after the icon row's collapse rule
    // was deleted on 2026-08-14 — having gone with the history companions'
    // return to row 4; a MODAL's yield is the one place the shape survives) —
    // never re-shaped here, so the clickable rect is the painted one. THE ACT
    // IS AT THE RELEASE (architect 2026-08-13, the chrome-wide rule —
    // authoritative statement at AppState::ChromePress): the press ARMS through arm_redesign_press, carrying the
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
                // THE MENU ANCHORS ARE THE ROSTER'S NON-CHORD BUTTONS —
                // membership is kDropdownMenus (app_state.h) and this file's
                // ONE enumeration of it is the band-claim block above; nothing
                // is re-listed here. Each anchor's action is a POPUP TOGGLE,
                // which no keyboard chord performs, so they take the walk below
                // rather than a row in the chord table. Their ITEMS lead to
                // routes the keyboard already has — the bare `;` still opens
                // the settings editor DIRECTLY, and every File and Edit row
                // carries its own accelerator — so a
                // dropdown is a pointer affordance for an existing road, never a
                // second one (the Help menu's one row was that road's
                // exception for a few hours of 2026-09-03, and the menu itself
                // went on 2026-09-09; the record is at CommandPopupItem). (A THIRD
                // anchor, Navigation, was spelled here from
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
                // same shape on_motion's anchor walk takes, over the one
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
                    // caller: the hover switch carries NO held button at all, so
                    // a claim written inside the toggle would be a lie on that
                    // route. Its open path also RESETS
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
    // (THE ICON ROW'S BODY IS claim_button_row_press, above the veils: the
    // color picker's veil reaches it too.)
    if (rect_contains(top_icon_row_area(app), x, y)) {
        claim_button_row_press();
        return;
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
    // (THE BODY IS claim_button_row_press, above the veils: the color
    // picker's veil reaches it too.)
    if (rect_contains(bottom_row_area(app), x, y)) {
        claim_button_row_press();
        return;
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

    // THE `h` HISTORY MODE's pointer gate — the sibling of the on_key allowlist,
    // and the second and last of the mode's two gates.
    //
    // PLACED BELOW THE FOUR REDESIGNED ROWS' BAND CLAIMS ON PURPOSE. Those rows
    // dispatch their buttons as synthesized CHORDS through on_key, so they are
    // already covered by the keyboard gate — Save, Undo, Redo, Render and the
    // view group drop there exactly as their keys do, with no second membership to
    // keep in step — and letting them through here is what keeps that single
    // coverage true. ONE PRESS ROUTE IN THESE ROWS DISPATCHES NO CHORD
    // (re-derived 2026-08-06, again 2026-08-15 when the Navigation anchor left,
    // again 2026-08-18 when the walk selector did, and again 2026-08-20 when
    // the EDIT anchor arrived, and again 2026-08-27 with SERIES, and again
    // 2026-09-09 when the HELP anchor left): the three menu
    // anchors,
    // which have none and are
    // shut at toggle_dropdown instead. That exception is a refusal decided
    // ABOVE this gate, so
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

    // Mouse authoring is home-view gated like the keyboard: the marker DRAG
    // never moves a warp flag in T+W — selecting and landing but MOVING
    // NOTHING, the one off-home state left since the P column opened to both
    // audio views (2026-08-30; the column is target view only again since
    // 2026-09-21, where it is home) — the crossing's value-drag posture claiming
    // that state whole since 2026-09-13. W+target used
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
        // THE RIDING BOXES TAKE THAT SAME ROAD (architect 2026-09-05):
        // whichever of the marker's boxes stand to the RIGHT of the open field
        // — under the payload field its two bound cells, under
        // the lower bound's field the upper cell — are painted by the editor's own
        // publisher at that field's right edge, and they are the MARKER'S boxes
        // for the pointer as well as graphically, so a press on one is an
        // ordinary outside press: it closes the session here and falls through
        // to the marker claim below like every other press does. Nothing forks
        // for them on this path, and nothing forks on the editor's KIND: they
        // resolve through the one flag walk, which asks the editor's published
        // run (FlagEditorBox::riding_cells) ahead of the lane's stash, so
        // hit_test_flag answers the edited marker and hit_test_flag_cell
        // answers the pressed cell — a single click selects, lands and
        // addresses that cell, and a double click's second press opens that
        // cell's own editor through the consume-open road. The boxes standing
        // LEFT of the field never left the lane's own stash and are pressed
        // through it, as they always were. (The one press this owner still
        // leaves standing is a press in the FIELD, which the caret / text-drag
        // block far above already claimed and returned on.)
        close_top_flag_editor_for_outside_press(x, y);

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
        // below) alone — the trim region overlay's own bound and interior drags
        // on the waveform (2026-08-18) were deleted with its resting form on
        // 2026-09-22; a click over a bound's waveform stem is an ordinary
        // waveform click (the stem grab retired). Resolved ONCE here, ahead of every branch that
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
        // The plain presses on the EMPTY MARKER LANE, the ruler and the waveform
        // are PLACEMENT presses that stop nothing AT THE PRESS: each arms a
        // pending whose DEFERRED CLICK ACT (arm_placement_press since the lane
        // became a placement surface, 2026-09-25; arm_nav_press from the
        // pan-primary ruling of 2026-08-12) places the playhead at the lift,
        // and the stop is the PLACEMENT BODY's own
        // (place_playhead_at_click_column, architect 2026-10-06, the class at
        // stop_playback_if_playing's declaration), so a pan or a gutter click
        // stops nothing. A SHIFT press there is the same placement pending,
        // which is why shift is NOT in the inert list above: it acts here.

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // (NO ALT ARM: alt binds no PRESS anywhere — the grab-pan it carried
        // until 2026-08-12 is the PLAIN drag on the navigation surface now, and
        // its last pointer form, the ALT+WHEEL stepped pan (2026-08-27, while
        // the plain wheel was the waveform magnification step), moved back to
        // the PLAIN wheel on 2026-09-14 with the alt form deleted, so alt binds
        // nothing on the pointer at all. An alt-exact press falls to the
        // strict-modifier discard below, a consumed no-op like every other
        // unbound combination; on the keyboard alt survives only inside the
        // Ctrl+Alt chords, whose inventory is the alt vocabulary at
        // chord_is_bound, gui_input.h.)

        // Ctrl-exact left press splits by surface. On a top-strip MARKER it is
        // the individual membership toggle + land on the resulting focus (the
        // marker claim below). On the NAVIGATION SURFACE — the waveform, either
        // half, and nothing else (the RULER and the MARKER lane's empty
        // stretches were on it from 2026-08-12 and left it on both devices
        // 2026-09-25, a ctrl press there arming nothing) — it is THE ONE NAV
        // DRAG'S CTRL ENTRY since 2026-08-14
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
            // selection always carries a focus after either arm). There is no
            // result-size split here any more (the >=2
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
                // THE PRESS ACTS (2026-08-17): the toggle, its stop and its land
                // are the CLICK, run here through the one
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
                    arm_pending_click_act(x, y, /*is_begin=*/true);
                    return;
                }
            }
            // The zoom surface IS the navigation surface, through its one
            // geometry owner — the waveform, either half, and nothing else.
            // Anywhere else — the RULER and the MARKER lane's empty stretches
            // among them since 2026-09-25 (architect: the lanes left the
            // navigation surface on both devices; a ctrl press there arms no
            // drag, and a ctrl click there never had a click act, the ctrl
            // entry arming none), the gap band, the inter-lane seams — the
            // strict no-op below. (The `h` view's ctrl press falls through to
            // this same claim; the click act is not armed on this entry, so
            // the mode needs no arm of its own here.)
            if (point_on_nav_surface(app, x, y))
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
        // chords (Ctrl+Alt+R, Ctrl+Alt+Shift+R, Ctrl+Alt+P,
        // Ctrl+Alt+Shift+P and, since 2026-09-13, File → Revert's Ctrl+Alt+O;
        // re-derived by grep of chord_is_bound 2026-09-23)
        // — every other alt keybinding was retired
        // 2026-07-28, and both of its pointer forms moved onto the PLAIN forms
        // with the eighth glass ruling; the alt+wheel STEPPED PAN came back to
        // the modifier on 2026-08-27 and left it again on 2026-09-14 for the
        // plain wheel, so no pointer path anywhere defers to alt.
        // Discarding a press here is TOTAL: it claimed
        // nothing, so it stopped no playback on the way down either — the stops
        // live at the claims above and below, never on the route to this gate.
        //
        // AND IT SAYS NOTHING (architect 2026-08-30, the unbound-keys ruling
        // read on the pointer: "bound keys either show an effect or a card, so
        // an unbound key is identified by its silence"). NO BOUND GESTURE
        // PASSES THIS LINE, re-verified at this edit: the ctrl-EXACT branch
        // above returns on every one of its paths (the marker toggle, the
        // BEGIN bound set, the nav zoom arm, its own fall-through),
        // Ctrl+Shift's one claim — the END bound set — returns inside the trim
        // band above, and ALT SPELLS NO PRESS ANYWHERE. So what reaches here
        // is Ctrl+Shift off the trim bar and every alt-carrying press, and
        // nothing else — combinations this surface binds nowhere, answered by
        // the silence that identifies them. The reach is the waveform and the
        // top strip alone (the lane test above returns for everything else)
        // under a LEFT button (this whole block's gate), with every chrome,
        // modal and overlay surface claimed far above. It carded
        // "<modifier>+click is not bound here" through spell_modifiers for the
        // one day of 2026-08-30.
        if (ctrl || alt) return;

        // Plain or Shift press, under the PAN-PRIMARY vocabulary (architect
        // 2026-08-12, the eighth glass ruling) as amended 2026-08-13 (THE
        // WAVEFORM'S TWO HALVES BECOME ONE SURFACE). On the NAVIGATION SURFACE
        // — the WHOLE waveform and nothing else since 2026-09-25 — a PLAIN
        // press is a PENDING CLICK (arm_nav_press): a motionless
        // release runs THE HALF'S OWN ACT as the DEFERRED CLICK ACT (upper =
        // the playhead placement, lower = the audition scrub), and crossing the
        // 8px threshold is the GRAB-PAN in either half. A SHIFT press there is
        // the REGION FORMER, the one mouse region gesture (claimed just below,
        // ahead of the band walk), IN EITHER HALF TOO since the same ruling
        // ("shift plus drag to map out a region should also be allowed in the
        // lower half, for consistency"). On the two PLACEMENT LANES — the
        // RULER and the MARKER lane's empty stretches, off the surface on both
        // devices since 2026-09-25 — a plain or shift press is the PLACEMENT
        // PENDING (arm_placement_press): the motionless release places, a drag
        // does nothing. Neither ever SELECTS a marker. In the top strip a
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
        // navigation surface — the waveform alone since 2026-09-25 — WHICH
        // INCLUDES THE LOWER HALF — architect
        // 2026-08-13, superseding the eighth glass ruling's "no region sweep at
        // all in the lower half": the drag motions are the same in both halves
        // now, so the region former is too) so the band walk below is
        // plain-only except the flag range click. A shift press on a FLAG falls
        // through to the marker block (lane vocabulary); a shift press anywhere
        // else — the trim bar, the gap band, the inter-lane seams — is a
        // consumed nothing, shift binding nothing there. The former's body is
        // the one placement press (place_playhead_and_arm_region): deselect,
        // seat the playhead at the clicked column,
        // arm the drag — the drag then writes the trim with the playhead
        // riding the moving endpoint, landing where the mouse releases; a
        // motionless shift click lands the playhead and writes no trim. The
        // `h` view never reaches this claim (its gate consumed or forked far
        // above); its own shift former is handle_history_mode_press's.
        //
        // THE LANES KEEP THE SHIFT CLICK'S PLACEMENT AND LOSE THE SWEEP
        // (architect 2026-09-25, the lanes leaving the navigation surface on
        // both devices): a shift press on the ruler or the marker lane's empty
        // stretch arms the PLACEMENT pending (arm_placement_press), so a
        // motionless shift click deselects and places the playhead — the
        // former's own press half, run at the lift like every click on a
        // placement lane — and a shift drag there does nothing. No empty-lane
        // seed: the shift click was never the create's first half.
        if (shift && !(inside_top && mh_index >= 0)) {
            if (point_on_nav_surface(app, x, y)) {
                place_playhead_and_arm_region(x - area.x, x, y);
            } else if (point_on_placement_lanes(app, audio, x, y)) {
                arm_placement_press(x, y, /*history=*/false,
                                    /*seed_empty_lane=*/false);
            }
            return;
        }

        if (inside_top) {
            // TOP-STRIP ONLY (the stem's widened gate — `inside_top ||
            // stem_click` — died with the stem surface, architect 2026-08-12:
            // a plain waveform press over a stem column falls through to the
            // waveform block below and is the placement press there).
            //
            // The TRIM BAR (top_trim_row_area, lane 2) is trim's lane and is
            // claimed BEFORE the marker single-select. Row 5's three lanes —
            // the trim bar, the ruler (lane 3) and the marker lane (lane 4) —
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
            // THE RULER BAND'S PLAIN PRESS IS THE PLACEMENT PENDING
            // (architect 2026-09-25: the ruler and the marker lane left the
            // navigation surface on both devices — "out of both — we want
            // symmetry as much as possible"). The press arms ScrollDragState
            // with placement_only (arm_placement_press) and does NOTHING ELSE;
            // a motionless release runs the deferred click act (deselect +
            // playhead to the column), and a drag that crosses the slop does
            // nothing at all — no pan, and no ctrl edge turns it into a zoom.
            // No double-click surface here: the span-framing double-click
            // lives on the TRIM lane, and the marker-create one on the MARKER
            // lane's empty stretches — the ruler seeds nothing.
            //
            // THE BAND IS EXACTLY top_ruler_row_area AND NOTHING BELOW IT (the
            // claim reads the lane accessor and only the lane accessor). The
            // `h` VIEW never reaches this arm — its own gate armed the same
            // pending with the mode's deferred land far above. A GUTTER press
            // still arms; its motionless release's click act deselects and
            // seats no playhead, the placement body's own gutter shape.
            {
                const GuiRect ruler = top_ruler_row_area(app);
                if (y >= ruler.y && y < ruler.y + ruler.h) {
                    arm_placement_press(x, y, /*history=*/false,
                                        /*seed_empty_lane=*/false);
                    return;
                }
            }
            const GuiRect trim_bar = top_trim_row_area(app);
            const bool in_trim_bar =
                (y >= trim_bar.y && y < trim_bar.y + trim_bar.h);
            if (in_trim_bar) {
                // Plain trim-bar press. An endcap/bridge hit ARMS the trim drag
                // and commits nothing at the press: only the threshold crossing
                // begins it, and a MOTIONLESS release here runs NO act at all —
                // which is exactly the consumed nothing stated in the paragraph
                // below. Armed IN EITHER TAB since
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
                // gesture, and both run this one command. It DIVERGES
                // from the bare `0` key, which only ever reaches the whole song
                // (and, from there, `c`); this frames a proper trim sub-window,
                // else the whole song (the region arm above those two died with
                // the separate region state on 2026-08-18).
                //
                // THE CONSUME ACTS AT THIS PRESS AND ARMS NOTHING — A
                // DOUBLE-CLICK IS NEVER A DRAG (architect 2026-09-22; the rule
                // and its inventory are at DoubleClickSurface, app_state.h):
                // the second press frames and RETURNS, exactly as the `h`
                // view's copy of this gesture always did, so its release is
                // inert and no pending trim drag stands to freeze the
                // displayed basis under the new framing. It seeds nothing (a
                // consumed press never seeds — the family rule, so the cadence
                // stays second-press-only). (Retired: the fall-through into
                // the cap/bridge arm below and its "accepted cost" — the armed
                // pending held the staged viewport's promote until the
                // release, so the waveform zoomed at once and the trim bar
                // snapped to the framing a release, a tick and a frame later.)
                if (trim_bar_double_click_at(dc_at_press, x, y)) {
                    run_span_framing_command();
                    return;
                }
                // SEEDING is a RELEASE act (only the release knows the press
                // stayed still), so the press records its point and the release
                // decides — see TrimBarPressSeed.
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
                // THE MARKER CLICK ACTS AT THE PRESS (architect 2026-08-17:
                // CONTENT ACTS THE MOMENT ITS IDENTITY IS CERTAIN — a flag
                // press can only mean one thing, so the one-day lift deferral
                // of 2026-08-15 is inverted; the double-click open in
                // particular was "a tad slow compared to the Enter key", and
                // the deferral's only defense there — a double-click's second
                // press becoming a drag — is a nonexistent use case). ONE act
                // owner, run_marker_click_act: the stop, the three-way fork,
                // the land and the plain consume-open, which
                // then arms the pending that becomes the reposition drag past
                // the threshold. The drag's gates (the lock; the home view
                // through the value-drag posture) live at the crossing, never here: a
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
                    // The empty marker-lane stretch — a PLACEMENT SURFACE
                    // since 2026-09-25 (architect: the lanes left the
                    // navigation surface on both devices; the navigation
                    // surface's lane member from 2026-08-12, the press-time
                    // parity placement of 2026-07-23 before that). Plain by construction
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
                    // Otherwise the press is the PLACEMENT PENDING
                    // (arm_placement_press): nothing at press, the deferred
                    // click act at a motionless release — WHICH ALSO SEEDS the
                    // EmptyLane candidate there (the release-side owner; a
                    // crossed press seeds nothing) — and NOTHING past the
                    // threshold: a drag on the lane does nothing.
                    // THE STOP IS THE PLACEMENT BODY'S, NOT THIS BRANCH'S
                    // (architect 2026-10-06): the deferred click act runs
                    // place_playhead_at_click_column, which stops a live play
                    // before it seats the playhead — the one stop on every
                    // pointer and touch placement, as on the waveform's upper
                    // half — so a double-click drop's first click has already
                    // stopped any play by its second press. The press itself
                    // stops nothing (a pan or a crossed press leaves the play
                    // running); the audition scrub on the waveform's lower
                    // half keeps play-from-here.
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::EmptyLane &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= double_click_slack_px() &&
                        std::abs(y - dc.press_y) <= double_click_slack_px()) {
                        // A double-click is never a drag: the rule at
                        // DoubleClickSurface (app_state.h).
                        create_marker_at_empty_lane(x - area.x);
                        return;
                    }
                    arm_placement_press(x, y, /*history=*/false,
                                        /*seed_empty_lane=*/true);
                    return;
                }
                // Every other empty top-strip spot: NOTHING AT ALL — no
                // playhead, no marker, no selection or region change, and no
                // playback effect either, because this press claimed nothing
                // and the stops all live at the claims. That covers the
                // inter-lane gaps and any press in the FLEXIBLE GAP 1 band,
                // which since 2026-09-03 opens ABOVE the menu row (the
                // centering rule's remainder, main.cpp's vertical rule: inside
                // top_strip_area but in no lane, so it falls to exactly this
                // return with no code of its own, and the cursor map's plain
                // top-strip fall-through answers Arrow over it the same way —
                // the band was between the icon row and the trim lane when the
                // seventh ruling opened it, at the window's foot in between,
                // and between the menu row and the centered block from the
                // relayout's commit B until that day). A box under the point
                // is a marker hit and never reaches this branch.
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
        // the placement also DESELECTS and MOVES THE CURSOR, while the scrub
        // act touches no selection and no cursor.
        //
        // WHAT THE LOWER HALF GAINS BY BEING A PENDING: for the press's whole
        // life it is a live pointer gesture like the upper half's — the wheel
        // and every chord are swallowed, follow's page is paused, and the
        // cursor holds the uniform Arrow — which is exactly the symmetry the
        // ruling asked for and not a new rule of its own.
        //
        // (A SHOWN TRIM REGION OVERLAY WAS ASKED FIRST from 2026-08-15 until
        // 2026-09-22, a hit arming the trim endcap or bridge drag on the
        // waveform instead of the nav drag. The architect deleted its resting
        // form — the tablet's pen reaches the trim bar — and the claim went
        // with it.)
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
    // THIS ARM WRITES NOTHING ELSE: the trim is written by the motion path
    // from the first crossing on, and the trim bar, the sweep's picture
    // (RegionDragState, app_state.h), shows each write as it lands.
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
    // A sweep that WROTE NOTHING owes none of THAT TAIL: a motionless
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

// ARM A PLACEMENT LANE'S PRESS (architect 2026-09-25: the ruler and the marker
// lane left the navigation surface on both devices) — the same pending, the
// click act and the seed unchanged, with placement_only set so that no drag
// ever begins (contract at ScrollDragState::placement_only, app_state.h).
// FOUR CALLERS, by grep 2026-09-25: the live router's ruler band, its empty
// marker-lane stretch (the one that seeds), its shift claim off the waveform,
// and the `h` view's router (its ruler, its empty lane stretch and its shift
// claim through one arm each).
void GuiInputHandler::arm_placement_press(int x, int y, bool history,
                                          bool seed_empty_lane) {
    arm_nav_press(x, y, history, seed_empty_lane, /*scrub_release=*/false);
    app.scroll_drag.placement_only = true;
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
        nav_notional_col() *
            painter_samples_per_pixel(app, audio, waveform_area(app));
    viewport.invalidate_waveform_area();
}

// THE DEFERRED CLICK ACT — what a motionless navigation-surface press does at
// its RELEASE (the eighth glass ruling's deferral: the press could not know it
// was a click until the release said so, and a pan must move no playhead and
// clear nothing, so everything the old press-time placement did moved here
// whole). Runs at the PRESS column — sub-threshold travel is jitter, and the
// press point is what the user aimed at. A live play is stopped HERE, inside
// the placement body, never at the press (the press touched nothing, so a
// pan or a gutter click leaves the play running; architect 2026-10-06).
//   LIVE arm: deselect-all, then the placement body — playhead to the column,
//   the play stopped first (place_playhead_at_click_column).
//   The placement writes through the movement owner move_playhead_to, which
//   ends an A/B audition (a placement moves the playhead's position in the
//   music). A GUTTER column deselects and seats nothing.
//   `h`-VIEW arm: the mode's land — clear the mode focus + selection (the
//   pair clearer, the deselect's mode analog; store selection untouched),
//   then the same placement body, through the same movement owner.
//   The empty-lane and ruler
//   stretches take this too — PLACEMENT SURFACES since 2026-09-25, this act
//   their one act (placement_only) — so a click on the waveform's upper half
//   or on either lane moves the playhead, in the view as outside it.
//   SCRUB arm (2026-08-13, the waveform's LOWER half): ONE scrub act at the
//   press column and NOTHING ELSE — the act the lower half used to run at
//   mouse-down, moved here whole so that nothing on this surface pops at a
//   press any more. It is deliberately the FIRST arm and returns ahead of the
//   other two: the scrub selects nothing and moves no cursor, which is the
//   halves' ONE difference (two, read honestly — the
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
        place_playhead_at_click_column(press_x - area.x);
        return;
    }
    selection.clear_selection();
    place_playhead_at_click_column(press_x - area.x);
}

int64_t GuiInputHandler::place_playhead_at_click_column(int click_rel_x) {
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
    // the clamp here stays required because the conversion reads
    // ItemViewportBasis::spp, the PAINTED item epoch, which can extend past
    // the CURRENT live domain — a resize or a target-domain contraction after
    // the last item commit leaves the lagging painted basis's last column
    // computing to domain_total or beyond, one past [0, domain_total-1], which
    // the display-state validator would clear wholesale. The clamp also
    // makes -1 a sentinel no seated frame can collide with. THE COLUMN
    // CONVERTS ON THE PAINTED VIEWPORT, the item basis
    // (playhead_frame_at_click_column; architect 2026-09-24, strictly as
    // painted): the playhead seats at the frame the clicked pixels showed even
    // while a viewport job in flight has moved the live viewport ahead of
    // them; cold, the live viewport by the basis's own contract.
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    // A POINTER OR TOUCH PLACEMENT STOPS A LIVE PLAY (architect 2026-10-06; the
    // class is stated at stop_playback_if_playing's declaration): past the
    // gutter refusal above and ahead of the first write, so a click that seats
    // nothing stops nothing. This is the ONE site for every placement route
    // (the click act and both sweep formers); the sweep's motion then carries
    // the cursor with the play already stopped.
    playback_lifecycle.stop_playback_if_playing();
    viewport.move_playhead_to(sample);
    return sample;
}

void GuiInputHandler::place_playhead_and_arm_region(int click_rel_x, int x,
                                                    int y) {
    // THE REGION FORMER'S LIVE PRESS HALF — TWO CALLERS, re-derived by grep
    // 2026-08-12 at the touch half (the eighth glass ruling made the plain
    // presses the pending
    // pan, whose motionless release runs the placement WITHOUT the arm through
    // run_nav_click_act instead): the SHIFT-exact press on the navigation
    // surface — the waveform, both halves; the ruler and the marker lane's
    // empty stretches left it 2026-09-25 and take the placement pending
    // instead — claimed once for the whole y-gate in on_button_press, and
    // the touch region begin's live arm (begin_touch_region — the region
    // hold's expiry at the finger's down point, the same surface through the
    // pan-zone query). The
    // clear runs FIRST, before the shared body's gutter early-return,
    // so an inert-gutter click (no column to seat a playhead) still deselects.
    // THE SEAT BELOW IS A MOVEMENT (the press really does move the playhead),
    // and the press writes no trim: the motion path writes it from the first
    // crossing on (apply_region_drag_motion). A gutter press seats nothing and
    // arms nothing.
    selection.clear_selection();
    const int64_t sample = place_playhead_at_click_column(click_rel_x);
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
    // bare-`s` drop equivalent, and like bare `s` it is the AUGMENTED drop on
    // every column — the copy-previous owner in W, the lead-in reset in P, and
    // since 2026-09-15 the level-in-force copy in M.
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
    // movement the drop's own playhead seat carries) — the
    // phase-reset drop seeding per its own view fork (kN/2 before the
    // playhead in target view, at the playhead exactly in source view) and
    // landing the playhead per that drop's rule.
    const GuiRect area = waveform_area(app);
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    // THE LOCK, both reasons (authoring_locked, app_state.h): a drop is
    // authored content on a read-only tab and an undo entry under a lit grid
    // iterations lamp, and the iteration lock admits no drop at all. Silent,
    // as the read-only half has always been here — the pointer's non-event is
    // its answer.
    if (authoring_locked(app)) return;
    if (!active_column_authoring_allowed(app)) return;
    // The placement converts on the PAINTED viewport, the item basis
    // (playhead_frame_at_click_column; architect 2026-09-24, strictly as
    // painted), so the drop below authors at the column the double-click
    // pressed on screen; cold, the live viewport by the basis's own contract.
    // The drops themselves convert no column: they author at the playhead's
    // frame.
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    // active_column_authoring_allowed admits W only in source view (warp
    // drops legal there alone) and P wherever it exists, which is target view
    // alone (2026-09-21; both audio views from 2026-08-30 until then), so the
    // view dispatch below needs no extra audio-view guard on either column.
    // The drops' reason channel carries one sentence, the off-edge refusal
    // (kMarkerDropOffEdgeCard), which this road cannot meet: the playhead was
    // just seated on a painted column (< area.w on the painted viewport, so at
    // or left of it at the right wall), and the lead-in lies further left
    // still. Raised anyway, the channel's contract being that every caller
    // says what comes back.
    const GuiOpRefusal refusal =
        app.active_markers_view == 'P'
            ? phase_resets.drop_phase_reset_lead_in_at_playhead()
            : warpops.drop_copy_previous_at_playhead();
    if (refusal)
        notifications.notify(AppState::NotificationClass::Normal, *refusal);
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
// `mods` HAS ONE READER, the tooltip's held bit at the top (the per-arm cursor
// refresh that once read it wanted the modifier truth the platform delivered
// WITH this release, and the owner that replaced those calls is handed the
// live state by the platform).
void GuiInputHandler::on_button_release(GuiMouseButton button, int x,
                                        int y, GuiInputState mods) {
    // THE TOOLTIP'S HELD BIT, above every return: the platform's own answer
    // with this release (the left's release reads up; another button's reads
    // whatever the left still is), so a wait may start again — on the next
    // motion past the slop, never on the release's resting pointer, and not
    // on the button the press spent until the pointer has left it
    // (AppState::RedesignTooltip). AND ANY RELEASE IS THE TOOLTIP'S HARD END,
    // as any press is (Windows hides a tooltip at a click, "About Tooltip
    // Controls"): a box a non-primary press let stand goes with its release.
    app.redesign_tooltip.button_held = mods.primary_button_held;
    hide_shift_tooltip();
    // THE NOTIFICATION CARD'S LIFT (architect 2026-10-01), FIRST — the press
    // claim's mirror at the press claim's rank: the card's claim outranks
    // every veil and the open dropdown's claim, so its lift must outrank
    // their releases too, the dropdown's above all, which consumes every
    // release while a menu stands (a card pressed beside an open menu would
    // otherwise never be dismissed). Guarded on the Card arm alone, which
    // only that claim sets, so it claims nothing that is not its own; the
    // arm is taken whole here (take_chrome_press, which resumes the card's
    // clock) and the act is the lift's (finish_notification_release).
    if (button == GuiMouseButton::Left &&
        app.chrome_press.kind == AppState::ChromePress::Kind::Card) {
        finish_notification_release(take_chrome_press(), x, y);
        return;
    }
    // THE ON-SCREEN KEYBOARD'S OWED KEY-UP, above every gate and above even the
    // dropdown's release — the press's mirror. It is guarded on the held key
    // index alone, which only that surface's own press ever sets, so it claims
    // nothing that is not its; and it fires even when the press's act closed
    // the editor under it, because a delivered key-down owes its pair whatever
    // became of the surface (contract at the declaration).
    if (button == GuiMouseButton::Left && finish_onscreen_keyboard_release())
        return;
    // THE DROPDOWN'S RELEASE, above every gate but the card's lift (whose
    // claim outranks the dropdown's, above): while it is open it owns the
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
    // act-at-release conversion; a CARD arm was taken at the top of this
    // body): the hold ends with this release whatever
    // consumes it, so the un-pressed face cannot be stranded by an early
    // return — and whether the act RUNS is decided further down, after the
    // modal gates have had their say, so a prompt raised under the hold
    // swallows the lift exactly as it swallows every other pointer event.
    AppState::ChromePress chrome{};
    if (button == GuiMouseButton::Left) chrome = take_chrome_press();
    // THE CAPTION BUTTON'S LIFT (architect 2026-10-05), above the veils below:
    // its press was admitted under the render player's and the picker's, so
    // its lift must be too; the prompt and the dialog editors it re-asks
    // itself (finish_caption_release).
    if (chrome.kind == AppState::ChromePress::Kind::Caption) {
        finish_caption_release(chrome, x, y);
        return;
    }
    // THE PROMPT DIALOG'S ACT, the press claim's other half (2026-08-13): the
    // lift on the button the press armed activates that response. A lift
    // ANYWHERE ELSE consumes the arm and dispatches nothing, leaving that
    // button passively focused — the focus the press gave it (2026-10-03,
    // arm_modal_dialog_press) — instead of simply cancelling. The gates the act re-asks (the painted bit, the stash's
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
            // the arm the hold is measured against (2026-08-28; the term's two
            // roads are at modal_dialog_press_shifted).
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
    // THE COLOR PICKER'S RELEASE (2026-10-07), its press block's mirror:
    // the gesture's end, a list row's select, or the dialog buttons' shared
    // dispatch; every other lift consumed — but THE TWO BUTTON ROWS'
    // (architect 2026-10-07 evening for row 8, 2026-10-08 for the icon row):
    // a roster arm under the picker is an icon-row or row-8 arm by
    // construction (the press router admits no other), and its lift is the
    // chrome act's own body, which re-asks every gate.
    if (app.color_picker.active) {
        if (button != GuiMouseButton::Left) return;
        if (chrome.kind == AppState::ChromePress::Kind::Roster) {
            finish_chrome_press_release(chrome, x, y);
            return;
        }
        color_picker_release(x, y);
        return;
    }
    // THE EDITOR DIALOG'S ACT, the same shape over the other surface: the lift
    // on the armed OK / Cancel runs the session's own Enter / Esc through the
    // one modal key route. Above the text-drag branch because the two are
    // mutually exclusive by construction (a press on a button never reaches
    // the field claim), and above the four editor swallows below, which is
    // where an unarmed release still ends.
    // THE SETTINGS CHOICE EDITOR'S ROW LIFT (2026-10-07 evening), its press
    // claim's mirror: the armed list row's lift selects and commits, or
    // closes the list off the row. Ahead of the dialog's buttons, which no
    // press armed while the list was down.
    if (button == GuiMouseButton::Left && finish_settings_choice_release(x, y))
        return;
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
        const bool by_words     = app.editor_text_drag.by_words;
        finalize_editor_text_drag();
        // Double-click seeding: a MOTIONLESS release (a pure click that left a
        // caret, no selection) seeds an editor-text candidate so a second click
        // within the window selects the clicked character class's run (word /
        // punctuation / whitespace). A drag that made a selection seeds nothing.
        // NEITHER DOES A SHIFT+CLICK (architect 2026-08-30): it is the
        // selection extend, not the first half of a word select, and a
        // shift-click that landed on the caret's own byte would otherwise seed
        // one by leaving no selection behind. NOR THE DOUBLE-CLICK'S OWN
        // RELEASE (2026-09-05, the drag by words): it is the second click,
        // never the first half of a third, and an empty field's word select
        // leaves no selection to say so.
        if (!shift_extend && !by_words && g.valid &&
            !text_editor::has_selection(*g.ed)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
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
    // pointer never left the slack AND no trim drag went live. THE SLACK IS
    // THE STRICTER CLAUSE since 2026-09-29: it is the drag gate's length
    // (double_click_slack_px() == drag_moved_threshold_px()) while the trim
    // bar crosses into its drag at grab_moved_threshold_px(), twice it, so a
    // release inside the slack never follows a crossing, and a click that
    // rolled between the two is still a click (its act ran or will run as
    // one) that simply seeds no framing double-click — the contracts are at
    // the accessors, app_state.h. A moved endcap/bridge drag therefore seeds nothing
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
        // placement (deselect / mode-land, play stop, playhead)
        // or the lower half's audition SCRUB (2026-08-13),
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
        const bool placement = app.scroll_drag.placement_only;
        if (moved && zooming) {
            apply_nav_zoom_at(x, y, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        }
        app.scroll_drag = ScrollDragState{};
        // A PLACEMENT LANE'S CROSSED PRESS (2026-09-25) began no capture and
        // moved no viewport, so it owes nothing: no act (it was not a click),
        // no predictor re-anchor, no capture end.
        if (moved && placement) return;
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
        disarm_pending_trim_drag();
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
        // THE FLAG'S UNCROSSED LIFT OWES ONLY THE SEED — the click itself
        // acted at the press (2026-08-17; the contract is at
        // PendingMarkerPress, app_state.h), and only the release can tell a
        // click from a drag, so the next Marker double-click candidate is
        // written here and nowhere else. A lift that never crossed the grab
        // gate is a click however far short of it the press travelled
        // (grab_moved_threshold_px, app_state.h); the second press pairs with
        // this seed only within the drag gate's slack of it.
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
        // THE CELL IS THE PRESS'S TOO, for the position's own reason: the seed
        // describes the press, and the box may be repainted at a different
        // width before the second click arrives.
        app.double_click = DoubleClickCandidate{
            .surface = DoubleClickSurface::Marker,
            .time_ms = monotonic_ms(),
            .press_x = press.press_x, .press_y = press.press_y,
            .target  = press.marker,
            .cell    = press.cell};
        return;
    }
    if (app.value_drag.active) {
        // THE VALUE DRAG'S OWN END, beside the marker drag's below and on the
        // same rule: the release commits what the motion wrote — the tempo
        // arm's one undo entry, or nothing at all on a bound.
        value_drag.commit();
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
// 2026-09-16): the two lists must agree, because a caller's whole promise is
// that what follows lands on a gesture-free state, and that predicate is what
// "gesture-free" means to the keyboard, the wheel and the cursor. THE
// FIFTEEN, in this body's order: the editor text drag, the marker reposition
// drag, THE VALUE DRAG (2026-09-10, the flag's vertical one), the
// trim drag, the sweep (region_drag), the nav drag (scroll_drag),
// the three pendings (marker press, trim, deferred click), THE RENDER
// PLAYER'S TWO ARMS — the folder overlay's row press and the play-scrub's
// marker drag, which joined this body 2026-08-28 through their own hard-end
// clears — THE COLOR PICKER'S GESTURE, and THE THREE POPUP LISTS' SCROLL
// HOLDS (2026-10-08: the choice editor's list, the picker's element list and
// its palette menu, clear_popup_scroll_holds). WHAT EACH LEAVES DIFFERS AND
// EACH ARM SAYS SO: the drags COMMIT what stands, the pendings and the
// player's arms commit NOTHING (a force-end is not a click — the standing
// abnormal-end rule), and the picker's gesture and the scroll holds have
// applied every step live.
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
    // THE VALUE DRAG COMMITS TOO (2026-09-10) — the release path exactly, so
    // a resize, a WM close or the Ctrl+Q hatch leaves the value the hand had
    // reached and, on the tempo arm, the one undo entry that takes it back.
    if (app.value_drag.active) value_drag.commit();
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
        // A placement lane's crossed press began nothing, so it is the
        // disarm alone (ScrollDragState::placement_only).
        const bool zooming = app.scroll_drag.zooming;
        if (app.scroll_drag.moved && !app.scroll_drag.placement_only) {
            if (playback.is_playing()) playback.resync_predictor();
            if (zooming) viewport.kick_waveform_sync();
            end_strip_pointer_capture();
        }
        app.scroll_drag = ScrollDragState{};
        if (zooming) viewport.invalidate_waveform_area();
    }
    // THE PENDINGS DISARM AND COMMIT NOTHING, which is not a cancel: there is
    // no release here (the button is still held), and a force-end is not a
    // click — the same rule the arms above state for their own unmoved
    // presses, and the same one the touch layer's ABNORMAL end (a hard end —
    // cancel, capability or focus loss — on a live pointer translation,
    // hard_end_touch_stream) delivers by leaving the button unheld. A pending
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
    // path.
    app.pending_marker_press = PendingMarkerPress{};
    disarm_pending_trim_drag();
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
    // THE COLOR PICKER'S GESTURE (its slider or wheel drag applied every step
    // live) and THE POPUP LISTS' SCROLL HOLDS (2026-10-08: an arrow's face,
    // the thumb's drag, every row already scrolled) end with nothing to
    // commit.
    clear_color_picker_drag();
    clear_popup_scroll_holds();
    // A force-end is not a clean click sequence, so no candidate may survive to
    // pair with a later click (the standing rule at every non-release gesture
    // end) — and neither may the trim bar's press record, which would otherwise
    // seed one at the next release.
    app.double_click   = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
}

// THE ROSTER'S POINTER WALK over the whole roster (row 1's three menu
// anchors, the icon row's twenty-seven and the bottom row's seventeen: the
// enum's own count at kRedesignButtonCount — the stash is
// AppState::redesign_buttons; a MODAL's yield leaves a bottom-row member with
// a zero rect, as do the icon row's overflow rule and its history stand-ins a
// member that does not stand, and a zero rect contains no point). It answers
// three readers from the remembered position — the TOOLTIP's wait (the
// button a resting pointer's hint names, handed to note_tooltip_hover at the
// tail), the ARMED CHROME PRESS's inside bit (the pressed face, which is
// painted, and so pays its strip's damage) and THE HOT TOOLBAR BUTTON
// (AppState::roster_hot, the flat toolbar's one
// hover face, architect 2026-10-06; set_roster_hot pays its damage). The
// rects are the painter's stashes, so the button a hint names and the button
// that lights are the painted button and nothing is measured here.
// THE HOT FACE'S ONE SETTER (AppState::roster_hot): a change damages the
// old button's published rect and the new one's — the face is painted inside
// the case and nowhere else, and a stale stash's rect was already damaged by
// the relayout that moved it.
void GuiInputHandler::set_roster_hot(int index) {
    if (index == app.roster_hot) return;
    const auto damage = [&](int i) {
        if (i < 0) return;
        viewport.invalidate_rect(
            app.redesign_buttons[static_cast<size_t>(i)].rect);
    };
    damage(app.roster_hot);
    damage(index);
    app.roster_hot = index;
}

void GuiInputHandler::arm_pen_hot_latch() {
    // Armed and unanchored (the rule at pen_hot_latch_): the hook fires after
    // the lift's own delivery, whose restore motion walked the roster
    // unlatched, so this re-walk withdraws the hot face that motion lit
    // before the frame paints.
    pen_hot_latch_ = PenHotLatch{.armed = true, .anchored = false,
                                 .x = 0, .y = 0};
    recompute_redesign_button_hover();
}

void GuiInputHandler::clear_pen_hot_latch() {
    pen_hot_latch_.armed = false;
}

void GuiInputHandler::recompute_redesign_button_hover() {
    // IT REFUSES WHILE THE POINTER IS OUTSIDE THE WINDOW — the same first line
    // and the same reason as recompute_dropdown_hover's, the boundary's other
    // pointer-derived walk: the remembered coordinates name a point INSIDE the
    // window even after the pointer has left, so this walk has no honest answer
    // to give while it is out. Its per-TICK caller (main.cpp) keeps running after
    // a leave, and the refusal is what makes that call inert: it cannot start a
    // tooltip wait for a pointer the pointer-leave hook has already let go.
    // THE GUARD LIVES HERE, NOT AT THE WIRING, because this function is the
    // walk's one body — on_motion, the other caller, writes
    // app.pointer_in_window true at its top and seeds the coordinates in the same
    // breath, so it is unaffected by construction, and a future third caller
    // inherits the rule instead of having to remember it.
    if (!app.pointer_in_window) return;
    const int mx = app.last_mouse_x;
    const int my = app.last_mouse_y;
    // AND NO BUTTON IS UNDER THE POINTER BENEATH A NOTIFICATION CARD: a card is
    // opaque to the pointer (notifications.h), so no button beneath one may
    // start a tooltip dwell — the card's claim consumes the press before any
    // roster gate sees it, and a hint there would name a button the pointer
    // cannot reach. THE TERM LIVES HERE, at the walk's one body, and not at the
    // motion handler, because this walk has TWO callers and the other is the
    // TICK: a pointer RESTING on a card over the icon row would be re-answered
    // by the next tick's walk, undoing a motion-side guard. It is folded into
    // `under_pointer` below rather than spelled as an early return so the
    // walk's tail still runs.
    const bool under_card = notification_card_at(app, mx, my) != 0;
    // NO WAIT RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT — read before the
    // walk because the walk below is what finds the tooltip's button (the only
    // route that starts a roster wait); the rule is stated at the walk's tail.
    const bool modal_owns_the_keyboard = tooltip_dwell_suppressed();
    // THE DIALOG'S VEIL (2026-08-12): under a PROMPT or an EDITOR dialog the
    // WHOLE roster is refused — nothing behind the modal is pressable, so no
    // roster button is under the pointer for a hint either. The tail reads the
    // same term as the dialog surface's liveness. The pointer-transparent FLAG
    // editor raises no veil: it is not a dialog and its roster presses were
    // never blocked.
    // THE FOLDER OVERLAY'S TWO OWNERS ARE DELIBERATELY NOT TERMS HERE
    // (2026-09-03 evening): the menu row stands above the band with the FILE
    // ANCHOR LIVE, its press exempted from both veils; under them the hint is
    // refused by the no-wait rule (tooltip_dwell_suppressed) alone. NOR IS
    // THE COLOR PICKER (2026-10-07 evening; the icon row 2026-10-08): the two
    // button rows stay on under it, so its veil is the hover zone's term
    // (redesign_button_hover_zone: the icon row and row 8, none of either
    // under the card's list or menu, never the menu row's anchors), not this
    // blanket.
    const bool modal_veil =
        app.prompt.active || modal_dialog_editor_active();
    int  hovered_tip = -1;
    int  hot         = -1;
    for (int i = 0; i < kRedesignButtonCount; ++i) {
        const AppState::RedesignButtonFace& f = app.redesign_buttons[i];
        const RedesignButton id = static_cast<RedesignButton>(i);
        // A zero-width stash (before that row's first paint) contains no point,
        // and the pre-motion (-1, -1) cursor is outside every rect, so both cold
        // states resolve to "not under the pointer" without a special case.
        //
        // THE ZONE IS THE SECOND TERM (redesign_button_hover_zone, app_state.h):
        // an open dropdown answers nothing to the pointer at all, and the
        // refusal lives in that one predicate rather than as a condition here.
        // There is no in-window term: the whole walk refused above. AND NO
        // ENABLED TERM (architect 2026-08-07): a disabled button still explains
        // itself, kdenlive's own behaviour.
        const bool under_pointer = !modal_veil && !under_card &&
                                   rect_contains(f.rect, mx, my) &&
                                   redesign_button_hover_zone(app, id);
        // THE HOT CANDIDATE: the toolbar button under the pointer on the
        // same gates — never a menu anchor, the menu row having no hot face
        // (paint_menu_row) — and NO keyboard-modal term:
        // that refusal is the tooltip's dwell, while a button the flag editor
        // leaves pressable lights as it presses. The rects are disjoint, so
        // the first is the only one, and the tooltip's break below cannot
        // pass it by.
        if (under_pointer && hot < 0 && !redesign_button_is_menu_anchor(id))
            hot = i;
        // MEMBERSHIP IS THE CONSTANT TABLE'S (2026-09-01): this asks only
        // whether the button HAS a tooltip — a null line 1 — and that is the
        // menu-row exclusion the state-free table owns; the stateful overload
        // (whose every arm returns a non-null line 1) is the painter's, which
        // reads the words at paint time.
        if (under_pointer && !modal_owns_the_keyboard &&
            redesign_button_tooltip(id).line1 != nullptr) {
            hovered_tip = i;
            break;
        }
    }
    // THE HOT FACE'S ONE WRITE (AppState::roster_hot): the candidate in a
    // toolbar style with a hot face (both: Explorer's flat raised line and
    // GTK's prelit button, toolbar_style_has_hot_face) while no chrome press
    // is armed — comctl32 shows no hot item while a toolbar holds the
    // capture, and GTK prelights no other button under a grab — and none
    // otherwise, so a toolbar style with no hot face would never store one.
    // AND NONE WHILE THE PEN'S HOT-FACE LATCH STANDS (pen_hot_latch_, the
    // rule at its declaration, architect 2026-10-07): after a pen lift the
    // tapped button reads at rest until the pen moves off its anchor.
    set_roster_hot(toolbar_style_has_hot_face(
                           live_chrome_spec().toolbar_style) &&
                           app.chrome_press.kind ==
                               AppState::ChromePress::Kind::None &&
                           !pen_hot_latch_.armed
                       ? hot
                       : -1);
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
    // the bit answers only where the pointer is. A CARD ARM IS NOT WALKED
    // (2026-10-01): the card paints no pressed face, so the paint-only bit
    // has nothing to serve there, and its slide-away cancel is its lift's
    // own re-hit (finish_notification_release).
    // THE CAPTION KIND'S BIT (2026-10-05), the same feint over its own
    // published rect, its face the caption's.
    if (app.chrome_press.kind == AppState::ChromePress::Kind::Caption) {
        const bool inside = rect_contains(
            app.caption_buttons[static_cast<size_t>(
                                    app.chrome_press.index)].rect,
            mx, my);
        if (inside != app.chrome_press.inside) {
            app.chrome_press.inside = inside;
            viewport.invalidate_rect(top_caption_row_area(app));
        }
    }
    if (app.chrome_press.kind == AppState::ChromePress::Kind::Roster) {
        const bool inside = rect_contains(
            app.redesign_buttons[static_cast<size_t>(
                                     app.chrome_press.index)].rect,
            mx, my);
        if (inside != app.chrome_press.inside) {
            app.chrome_press.inside = inside;
            // Only an arm with a click face paints a pressed interior, and
            // its home strip pays — row 8's damage fork: a transport face
            // damages its own bottom-strip lane, every other face the top
            // strip.
            if (roster_index_click_face(app.chrome_press.index)) {
                if (redesign_button_in_transport_row(static_cast<RedesignButton>(
                        app.chrome_press.index)))
                    viewport.invalidate_rect(bottom_row_area(app));
                else
                    viewport.invalidate_top_strip();
            }
        }
    }

    // THE TOOLTIP'S DWELL STAMP, written here because this is the one place that
    // knows a hover STARTED. EVERY roster button but ROW 1'S carries a tooltip,
    // and that membership is a CONSTANT: only the TEXT is stateful, never
    // whether a button has one. Re-derived from redesign_button_tooltip's
    // stateful overload, whose every arm returns a non-null line 1 and so
    // takes no hint away from any button: it moved the words on THREE — SAVE
    // (a publishing checkpoint first, then the history view's "Save and
    // commit"), RENDER (the mid-render Cancel, then the iteration bit) and,
    // since 2026-08-15, THE BOTTOM ROW'S COLLAPSED PLAY/STOP BUTTON (the live
    // audition bit, the same condition its glyph reads) — until the
    // 2026-09-01 truthful-tooltips ruling forked a dozen more on the acts'
    // own predicates, membership untouched.
    // The walk above covers the whole roster either way, and it hands the
    // button it found (or none) to the wait's one writer, note_tooltip_hover,
    // which decides by the slop, the held button and the box's own owner; the
    // tooltip's clock (tick_tooltip) decides visibility. Nothing here does.
    //
    // NO WAIT RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT, and this refusal
    // is what makes "a tooltip never floats over a modal" hold rather than merely
    // start out true. The modal branches of on_motion still call this walk
    // (the armed press's inside bit and the dialog owner's liveness below are
    // pointer facts a modal does not freeze), so without this line a motion
    // under a prompt or an editor would start a wait and the tick would raise
    // a FLOATING hint over the modal. A hint is a second surface, it hangs
    // past the strip, and the chord it names is exactly what the modal gate
    // is swallowing. Forcing "no owner" here
    // rather than gating the tick keeps the wait's writer in one place; the
    // modal's OPEN edge is a hard end of its own (on_key's or the press's, or
    // the compositor close's), which takes any standing box down at once.
    // THE DROPDOWN NEEDS NO TERM OF ITS OWN: redesign_button_hover_zone refuses
    // the whole roster while a popup is up, so the walk finds no owner by itself
    // — the two floating surfaces cannot coexist by construction.
    //
    // A STANDING DIALOG'S WAIT IS THE DIALOG'S (2026-08-13, when the modal's
    // buttons took tooltips): this walk answers for the ROSTER's index space
    // only, IN EVERY STATE, so with no roster button under the pointer it
    // hands "none" to the writer (a soft end for a standing ROSTER box),
    // HARD-ENDS a DIALOG owner — the wait's or the standing box's — whose
    // surface is gone or replaced, and leaves a DIALOG wait whose surface
    // stands and still wears the stash that armed it: the modal's own walk
    // (update_modal_dialog_hover) owns that surface's wait. A DIALOG SURFACE
    // IS LIVE exactly where on_motion runs that walk, re-derived by grep
    // 2026-09-25: the prompt and the editor dialogs (the veil's two terms)
    // and the folder overlay's two owners (the player and the picker —
    // folder_overlay_stands). The liveness test reads the LIVE
    // surfaces, never the painted stash, so the frame a dialog closes on is
    // the frame this walk takes its hint down, whatever road closed it — the
    // two that carry no input event of their own (a modal button dispatched
    // by a KEY RELEASE, and the history prefetch's failure cancelling the
    // load-in-place prompt from a worker) included. A closed or replaced
    // surface is not a graze, so it is a hard end, not the grace.
    // AND THE OWNER MUST STILL NAME THE STASH THAT ARMED IT: a Dialog owner
    // whose stamp (AppState::RedesignTooltip) differs from the live stash's
    // owner and session is dead too. Liveness alone cannot see a surface
    // REPLACED under the pointer, and exactly one such road exists: the
    // load-in-place prompt standing over the render player, closed by a key
    // release while the pointer rested on one of its buttons — the player
    // stays live, so without the stamp the wait would ripen into the
    // PLAYER's button at the same index, a hint beside a pointer not on it.
    // The picker cannot host a prompt (open_project_commit runs close_picker
    // before request_close, and request_close takes the player and the
    // picker down before it asks anything). THE STASH LAGS THE CLOSE BY ONE
    // PAINT: the
    // frame that repaints the player's row republishes the stash,
    // paint_shift_tooltip refuses the mismatch on that same frame, and this
    // walk's hide lands on the tick after it, so no hint of the wrong surface
    // is ever painted.
    // Under the veil `hovered_tip` is -1 by construction (the veil and the
    // no-wait rule above), so the veil needs no branch of its own. Under the
    // FOLDER OVERLAY, whose two owners are not veil terms, `hovered_tip` is
    // -1 through the no-wait rule alone, and the "surface stands" arm is what
    // keeps the player's and the picker's button hints alive: handing "none" to the writer there would stop the dialog's wait
    // on every tick.
    // THE COLOR PICKER'S CARD IS A DIALOG SURFACE LIVE TOO (2026-10-07):
    // color_picker_motion runs the dialog buttons' walk for its Copy / Paste
    // / Close, so its wait stands here as the overlay owners' does — and
    // the roster walk finds the two button rows' hints beside it (row 8 since
    // 2026-10-07 evening, the icon row since 2026-10-08; the two walks'
    // handshake is at update_modal_dialog_hover's tail).
    if (hovered_tip < 0) {
        AppState::RedesignTooltip& t = app.redesign_tooltip;
        const bool dialog_surface_live =
            modal_veil || folder_overlay_stands(app) ||
            app.color_picker.active;
        const auto dialog_owner_dead =
            [&](const AppState::RedesignTooltip::Owner& o) {
                return o.surface == AppState::RedesignTooltip::Surface::Dialog &&
                       o.index >= 0 &&
                       (!dialog_surface_live ||
                        o.dialog_owner != app.modal_dialog.owner ||
                        o.dialog_session != app.modal_dialog.session);
            };
        if (dialog_owner_dead(t.hovered) ||
            (t.visible && dialog_owner_dead(t.owner))) {
            t.hovered = AppState::RedesignTooltip::Owner{};
            t.spent   = AppState::RedesignTooltip::Owner{};
            hide_shift_tooltip();
            return;
        }
        if (t.hovered.surface == AppState::RedesignTooltip::Surface::Dialog &&
            t.hovered.index >= 0)
            return;
        note_tooltip_hover(AppState::RedesignTooltip::Owner{});
        return;
    }
    // Keying on the id is what makes a direct Render->Paste motion a new
    // arrival with its own wait (the reshow once a hint has shown since the
    // last gap, the full wait otherwise) rather than a continuation of the
    // last one; the rule is at note_tooltip_hover.
    note_tooltip_hover({AppState::RedesignTooltip::Surface::Roster,
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
    // A GREYED ROW RESOLVES TO "NO ITEM" here (2026-09-24): it is never
    // hovered or armed, so it wears no face and the release can never find
    // it lit. GREYED MEANS PAINTED GREY — the as-painted bit, the one the
    // press and the release claim on (architect 2026-09-24, strictly
    // as-painted).
    int hit = dropdown_item_at(app.last_mouse_x, app.last_mouse_y);
    if (hit >= 0 && !app.dropdown.item_enabled[static_cast<size_t>(hit)])
        hit = -1;
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
// 2026-08-13; the rule's authoritative statement and the state's contract
// are at AppState::ChromePress) for every redesigned button whose action IS a chord —
// rows 1, 3 and 4 and the bottom row, driven entirely by kToolbarChords'
// per-button flags so no row carries a special case of its own. Returns true
// when a button's rect claimed the press, whether or not anything was armed (a
// refusal is still a consumed nothing, which is what the band claims want).
//
// THE PRESS DISPATCHES NOTHING. It applies the refusals that were always
// press-time — the shift admission and the disabled consume (the radio's
// consume is the LIFT's alone since 2026-10-07 evening, the `radio` column) —
// and then ARMS: the index, the PRESS-TIME SHIFT (the release deliberately
// does not re-read modifiers, the modal release's own rule — the
// shift-admitting buttons must see the shift held at the press), and the
// feint's inside bit. The lift runs the act through
// finish_chrome_press_release below, which re-asks every gate, the face's
// bits as painted (architect 2026-09-24).
bool GuiInputHandler::arm_redesign_press(int x, int y, GuiInputState mods) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (!redesign_button_hit(app, tc.id, x, y)) continue;
        // A SHIFT PRESS ON A BUTTON WITH NO SHIFTED CHORD is a consumed nothing
        // — never the unshifted action, which would be a silent lie about what
        // the modifier did (the flag's rationale is at its declaration). No
        // arm, no face: the release could only refuse it again. A shift
        // carried WITH a ctrl asks the pair's admission instead of the
        // shift-alone one (redesign_button_shift_admits_with): Restrict Undo
        // admits Ctrl+Shift+Z while Shift+Z binds nothing.
        if (mods.shift &&
            !redesign_button_shift_admits_with(tc.id, mods.ctrl)) return true;
        // A DISABLED BUTTON'S PRESS IS A CONSUMED NOTHING: nothing arms, so
        // nothing can dispatch at the lift, and a SHIFT press is swallowed
        // exactly like the plain one (one bit, both routes — a greyed
        // Render is greyed for both of its chords). THE CLAIM READS THE
        // PAINTED BIT (architect 2026-09-24, strictly as-painted): the press
        // asks the face the painter last published
        // (RedesignButtonFace::enabled), never the live predicate, so the
        // press does exactly what the screen shows. The per-tick comparator
        // (main.cpp) is what keeps that bit honest, holding it against
        // redesign_button_enabled and repainting on drift, so it is at most one
        // tick behind reality; and a face painted live whose act has become
        // refused in that window dispatches, the act's own body answering for
        // itself, a card preferred over a no-op on a face that advertised an
        // act. What follows names the predicate's arms the bit is painted
        // from (redesign_button_enabled, app_state.h). Rows 1, 3
        // and 4 have no disabled face of their own, so the predicate is simply
        // true there — EXCEPT in two states. The `h` history view greys every
        // button whose act it consumes across all the rows
        // (history_mode_disables_button, above) — row 4's history group
        // aside, which carries resting greys of its own. AND SINCE 2026-09-10 THE
        // ITERATION LOCK: the VIEW GROUP'S THREE answer false while grid
        // iterations stands (iteration_lock_greys, app_state.h), so the press
        // dies here, the icon row's disabled face shows it, and the KEY's card
        // carries the sentence (the account is at their arm in
        // redesign_button_enabled).
        // THE BOTTOM ROW HAS A RESTING CONSUMER
        // HERE FOR EVERY MEMBER — the last, ADD TO SELECTION, having joined
        // on 2026-09-10 (below) — since
        // 2026-08-30 (the truthful-buttons ruling, reversing the 2026-08-15
        // scoped-truth ruling under which the row's members were lit outside
        // the `h` view whatever their chord would do): each arm at
        // redesign_button_enabled reads the predicate its act refuses on — the
        // verbs on the selection and the lock, the arrows on the tempo step,
        // the lane fork and the walls, the walk pair on the cycle's landing,
        // the skips on the jump's landing, Play on the launch, Copy value on
        // its eligibility — so this line consumes exactly the presses whose
        // chord would be a consumed no-op. Inside the view the derived
        // partition adds its own: the PLAY/STOP button, UP and DOWN, the four
        // verbs and Jump to Defining Marker (Space, the bare verticals and
        // the verbs' chords are consumed there) — all of them dead by
        // construction, so since 2026-10-07 none of them is PAINTED in the
        // view (history_mode_hides_button: an empty rect, which no press
        // reaches) — while the two skips (the mode's absolute jumps), LEFT
        // and RIGHT (its own playhead step since 2026-09-26, greyed by their
        // own arm over a focused diff flag and at the wall) and the
        // marker-walk group's four (its diff-flag cycle, its centring, the
        // admitted tab switch) take no partition grey. ADD TO SELECTION IS
        // IN NEITHER LIST ANY MORE: the `h` view ADMITTED bare `k` on
        // 2026-09-17, the lamp producing that view's own multi-selection, and
        // its RESTING REFUSAL out of the iteration lock — carried from
        // 2026-09-10, when the sticky ctrl would have taken the bound cells'
        // plain press away — went on 2026-09-19 with the narrowing that leaves
        // that press alone (the VALUE DRAG was a second such refusal until
        // 2026-09-12, when the two lamps were allowed to stand lit together;
        // WALK BOTH TABS took the same refusal for the march until its
        // deletion on 2026-09-14). The READ-ONLY lock never carried it, a
        // selection being navigation.
        const AppState::RedesignButtonFace& face =
            app.redesign_buttons[static_cast<size_t>(
                redesign_button_index(tc.id))];
        if (!face.enabled) return true;
        // A RADIO ALREADY SELECTED ARMS LIKE ANY OTHER BUTTON (2026-10-07
        // evening): its consume is the lift's (the `radio` column says why the
        // claim's went — the pen's blink), and it repeats nothing (no view
        // group row carries `repeats`).
        // THE ARM. The pressed face paints from it on the very next frame
        // (redesign_button_pressed_face — Roster kind, inside true, and the
        // row's click_face column deciding whether a pressed interior exists
        // to paint at all); the damage below is skipped for the two-face rows
        // that paint none. A SHIFT press arms too — it is the same physical
        // hold, and the carried bit is what the lift dispatches with. SO DOES A
        // CTRL press, which the band's modifier gate has already narrowed to
        // the buttons redesign_button_ctrl_admits names (and a CTRL+SHIFT press
        // to the ones redesign_button_ctrl_shift_admits names), so no second
        // admission is asked here — an unadmitted ctrl press never reaches this
        // body. THE
        // PRESS'S CLOCK IS STAMPED HERE, unconditionally: the lift measures the
        // hold against chrome_shift_hold_ms() to decide the SHIFT LONG PRESS, and
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
        // THE CHORD IS THE LIFT'S MINUS ITS LONG-PRESS TERM, and it carries
        // BOTH press-time modifiers since 2026-08-31 (the step ladder):
        // Up / Down are `repeats` rows that admit shift AND ctrl, so a
        // Shift+hold walks ten units a fire and a Ctrl+hold three —
        // A HELD REPEAT CARRIES ITS MODIFIER, the burst continuing the gesture
        // the press began. Each carried bit is already narrowed to a button
        // that admits it (a shift press on a non-admitting button returned
        // above this arm; the band's modifier gate refuses an unadmitted ctrl
        // press before the arm ever runs), so no second admission is asked
        // here and the predicate is asked about exactly the chord the burst
        // will fire. Undo's and Redo's ctrl (and Redo's shift) come from tc.ctrl /
        // tc.shift, this table's columns, and are ORed in like any other
        // row's, so the arm asks about exactly Ctrl+Z / Ctrl+Shift+Z.
        //
        // THE LONG-PRESS TERM IS ABSENT: A HELD REPEAT OUTRANKS THE
        // LONG-PRESS SHIFT (the principle at ToolbarChord::repeats) — THE
        // LIFT EXCLUDES EVERY `repeats` ROW FROM THE HOLD-AS-SHIFT READING
        // outright, so the burst's chord and the lift's agree by construction
        // instead of by the two landing on the same beat.
        if (tc.repeats) {
            GuiInputState chord{};
            chord.ctrl  = tc.ctrl ||
                          (mods.ctrl &&
                           redesign_button_ctrl_admits(tc.id));
            chord.shift = tc.shift || mods.shift;
            chord.alt   = tc.alt;
            // The fixed beat, never the hold delay: the schedule's rule at
            // the tick's repeat body below. A ROSTER BUTTON'S PROBE is the
            // pointer's own under the color picker (roster_chord_in_flight_).
            const RosterChordScope roster_chord(roster_chord_in_flight_);
            if (repeat_eligible(tc.key, chord))
                app.chrome_press.repeat_due_ms = now + kHoldBeatMs;
        }
        // DAMAGE FOLLOWS THE ROW'S HOME STRIP (row 8, 2026-08-11): the
        // transport row's pixels live in the BOTTOM strip, so its click face
        // damages its own lane where every other row damages the top strip —
        // the same fork every face writer takes.
        if (redesign_button_in_transport_row(tc.id))
            viewport.invalidate_rect(bottom_row_area(app));
        else
            viewport.invalidate_top_strip();
        return true;
    }
    return false;
}

// THE ARM, TAKEN WHOLE — on_button_release's first act on every left release,
// armed or not, so a release consumed by any gate below it (the veil, an
// editor swallow) still ends the hold and un-presses the face. The caller owns
// what happens next; this owns only the state, the face's damage and — for a
// CARD arm (2026-10-01) — the held card's clock, which resumes here because
// the arm that held it is gone (every end of a card's hold comes through
// this body: its lift, and the button-lost and pointer-leave clears).
AppState::ChromePress GuiInputHandler::take_chrome_press() {
    const AppState::ChromePress arm = app.chrome_press;
    app.chrome_press = AppState::ChromePress{};
    if (arm.kind == AppState::ChromePress::Kind::Card) {
        notifications.press_hold_edge(arm.card_id);
        return arm;
    }
    // A CAPTION ARM'S PUSHED FACE is the caption's to erase.
    if (arm.kind == AppState::ChromePress::Kind::Caption && arm.inside)
        viewport.invalidate_rect(top_caption_row_area(app));
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
// opened by a key, the history view toggled), and an arm must never outrank
// the live state. A lift anywhere else, or any gate gone, dispatches nothing —
// the consumed-nothing the press would have been. THE ROSTER'S FACE BITS ARE
// RE-ASKED AS PAINTED, NOT LIVE (architect 2026-09-24, strictly as-painted:
// "the live painted face should correspond to reality, and reality to the
// face, with cards being preferred over no-ops on a face that falsely
// advertises an action"): a button disabled under the hold is consumed once
// the comparator has repainted it grey, and a face still painted live
// dispatches, the act's own body answering.
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
    // term below is the editor OPENED MID-HOLD's, and any of the three
    // opened mid-hold (bare `l`, Ctrl+O or `'` typed under a held button)
    // takes the same refusal through it. THE COLOR PICKER IS NO TERM
    // (architect 2026-10-07 evening for row 8, 2026-10-08 for the icon row:
    // the two button rows stay on under it — the asymmetry at
    // modal_owns_bottom_row, paint_handler.cpp): its release block hands this
    // body a roster arm, which its press router admits on those two rows
    // alone, and the lift answers as without the picker — a face the picker
    // makes a no-op is painted grey (redesign_button_enabled), so its lift
    // stops at the painted bit below.
    if (modal_dialog_editor_active() || app.render_player.active ||
        app.picker.active) return;
    switch (arm.kind) {
    case AppState::ChromePress::Kind::None:
        return;
    case AppState::ChromePress::Kind::Roster:
        break;
    case AppState::ChromePress::Kind::Card:
        // Never reaches here: the card's lift is taken and dispatched at the
        // head of on_button_release, at its claim's own rank above every
        // veil (finish_notification_release).
        return;
    case AppState::ChromePress::Kind::Caption:
        // Never reaches here either: taken beside the veils in
        // on_button_release (finish_caption_release).
        return;
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
        // The press-time refusals the lift re-asks — the shift admission
        // under the CARRIED shift, the pair's under the carried pair (one
        // constant question, redesign_button_shift_admits_with), then the
        // enabled bit and
        // the radio rule ON THE PAINTED FACE, never the live predicates
        // (architect 2026-09-24, strictly as-painted: the lift asks what the
        // screen shows). THE RADIO RULE IS NO RE-ASK BUT THE CONSUME ITSELF
        // since 2026-10-07 evening: the press on a lit radio armed and held
        // the pressed face, and this lift is where it becomes the consumed
        // nothing (the `radio` column's account). The press arms and damages the strip, so by the lift
        // the painted bits are at most one frame old, and the per-tick
        // comparator carries any drift under the hold onto the face. A face
        // painted grey is a consumed nothing; a face painted live dispatches,
        // and where its act has become refused inside that frame the act's
        // own body answers — carding where its key would card, silent only
        // where its silence is ruled — rather than the lift consuming a press
        // the screen advertised.
        const AppState::RedesignButtonFace& face =
            app.redesign_buttons[static_cast<size_t>(arm.index)];
        if (arm.shift &&
            !redesign_button_shift_admits_with(tc.id, arm.ctrl)) return;
        if (!face.enabled) return;
        if (tc.radio && face.selected) return;
        // THE RENDER BUTTON IS CANCEL WHILE A RENDER IS LIVE (architect
        // 2026-08-11) — THE ROSTER'S ONE RULED EXCEPTION TO "THE BUTTON IS ITS
        // CHORD": while the face is PAINTED as Cancel (its stashed
        // glyph, whose Render arm at redesign_button_glyph
        // is queue_running and holds the face's contract) the lift runs THE
        // CANCEL ACT ITSELF, the Esc
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
        // press, and running the act directly is simply what the exception has
        // always meant.)
        // A SHIFT press cancels too: one face, one act — while the button IS
        // Cancel, letting shift slip through to the miscellaneous render would
        // start a render from a button that says Cancel. THE SHIFT LONG PRESS
        // TAKES THE SAME ANSWER, and takes it by construction: this branch
        // returns ABOVE the chord build where the hold is measured, so a finger
        // held on a painted Cancel cancels exactly as a quick tap does. That is
        // the honest reading — the face says Cancel for the whole hold, and a
        // button that changed its act at some invisible mark while its label
        // stood still would be the lie this exception exists to prevent.
        if (tc.id == RedesignButton::Render && face.glyph != 0) {
            // BOTH HALVES OF THE FACE-MIRRORS-THE-ACT HONESTY (architect
            // 2026-09-24, strictly as-painted): the CLAIM reads the painted
            // glyph, so a lift on a painted Cancel never dispatches a render;
            // the ACT is gated on the LIVE explicit-act bit, so it can never
            // reach a PREVIEW session through cancel_archival_session's wider
            // is_busy branch — the face never advertised one. ON THE STALE
            // EDGE (the explicit render finished, the lift landed before the
            // comparator's repaint) the face advertised Cancel and there is
            // nothing to cancel, so the lift CARDS it (kNoRenderRunningCard)
            // rather than consuming a press the screen asked for. The reverse
            // edge needs no arm: a painted RENDER whose queue has gone live
            // dispatches the chord below, and a dispatch killing the running
            // render and starting anew is the keyboard's own semantics
            // (GuiTargetRender::trigger).
            if (app.queue_running)
                cancel_archival_session();
            else
                notifications.notify(AppState::NotificationClass::Normal,
                                     kNoRenderRunningCard);
            return;
        }
        // THE SHIFT LONG PRESS (architect 2026-08-13): a press HELD past
        // chrome_shift_hold_ms() on a shift-admitting button reaches that button's
        // shifted twin, the waveform region hold's shape on the roster's
        // surface. It exists for GLASS — the road rig has no keyboard, so
        // without it a finger could reach only the plain half of each shifted
        // pair — and it costs the desk nothing, the hold delay being well past
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
        // THE HOLD HAS NO VISUAL ANNOUNCEMENT (architect 2026-09-29): the hand
        // learns the hold delay, and nothing on screen marks the
        // instant this term starts answering true. The hover tooltip is not
        // its cue: no tooltip rises under a held press at all
        // (AppState::RedesignTooltip), the press being one of its hard ends.
        //
        // AND IT REACHES NO CTRL-ADMITTING BUTTON THAT ADMITS NO SHIFT, by
        // construction rather than by an exclusion: the two SKIPS admit CTRL
        // for the whole-piece jump and admit no shift at all, so this term's
        // own predicate answers false for them and a held skip gives the
        // trim-bound jump exactly as a tap does. Their ctrl road on glass is
        // the S Pen's side button held on the skip, a real ctrl bit carried
        // by the press like a held Ctrl key, never this hold.
        //
        // WITH A CARRIED CTRL IT COMPOSES ONLY WHERE THE PAIR IS ADMITTED
        // (redesign_button_shift_admits_with, which asks
        // redesign_button_ctrl_shift_admits under a carried ctrl; architect
        // 2026-09-26) — the band gate's pair rule, which a carried shift meets
        // at the press, asked here of the held shift. THE WALK IS WHERE IT
        // BITES: the S Pen's side button held through a long press on it
        // carries ctrl, the hold adds shift, and the lift dispatches
        // Ctrl+Shift+Tab, the paired march — the pen's road to it, the tab
        // row's long press being the fingertip's. Without the pen's button the
        // same hold is Shift+Tab, the previous marker, as before. RESTRICT
        // UNDO IS THE OTHER (architect 2026-09-29): the side button held
        // through a long press is Ctrl+Shift+Z, redo, while the bare hold
        // reaches nothing, the lamp admitting no shift alone. (Up / Down admit
        // both modifiers but not the pair, and they repeat, so the term below
        // excludes them twice over.)
        //
        // AND IT REACHES NO HOLD-REPEATING BUTTON EITHER: A HELD REPEAT
        // OUTRANKS THE LONG-PRESS SHIFT, the principle stated at
        // ToolbarChord::repeats, and this term is where it is read
        // (2026-08-31, the round-B conversion). A repeating row's burst and
        // this hold are measured AGAINST TWO DIFFERENT NUMBERS — the arm
        // schedules its first fire at press + kHoldBeatMs, the fixed beat
        // (600), while chrome_shift_hold_ms() reads the hold delay,
        // kHoldDelayMs (300; architect 2026-09-29) — and a fired burst
        // consumes its own lift above. THE HOLD DELAY BEING THE SHORTER, a
        // lift between the two reaches the shift term with no fire at all,
        // and even at one instant sharing a timestamp would not be an
        // ordering (a lift delivered just past the beat but before the next
        // tick finds repeat_fired still false), so without this term the release would
        // dispatch a SHIFT ten-step where the user was owed a plain one. So
        // the exclusion is
        // read off kToolbarChords' own `repeats` column — the arm's
        // membership, never a second list — which makes it guaranteed instead
        // of timing-dependent, for every `repeats` row alike (Undo / Redo and
        // Left / Right admit no shift anyway, so the term is load-bearing for
        // Up / Down). A NON-REPEATING
        // shift-admitting button is untouched: the hold is still its road to
        // its twin, on glass and on the desk.
        const bool held_to_shift =
            !tc.repeats && redesign_button_shift_admits_with(tc.id, arm.ctrl) &&
            monotonic_ms() - arm.press_ms >= chrome_shift_hold_ms();
        // The shift term ORs the table's own (Redo's Ctrl+Shift+Z) with the
        // CARRIED press-time bit and the hold — well-defined because no row
        // sets both the table bit and the admission (see shift_admits), so this
        // one expression spells both members of each shifted pair however the
        // user asked for the shifted one. THE CTRL TERM IS THE SAME SHAPE ONE
        // AXIS OVER: the table's own ctrl bit ORed with the carried press-time
        // one, admitted where redesign_button_ctrl_admits says so — the band
        // gate's answer re-asked at the lift, the release's own second-wall
        // rule. The carried pair, or a carried ctrl with the hold's shift,
        // reaches this build only on a button that admits the pair (the gate,
        // the re-ask above and the hold's own term), so the chord it spells
        // is that button's key under both modifiers — the walk's
        // Ctrl+Shift+Tab, Restrict Undo's Ctrl+Shift+Z.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl ||
                      (arm.ctrl && redesign_button_ctrl_admits(tc.id));
        chord.shift = tc.shift || arm.shift || held_to_shift;
        chord.alt   = tc.alt;
        // OPEN PROJECT'S TWIN TRANSLATES (architect 2026-10-07), the roster's
        // one exception to "a modified press spells its button's own key
        // under those modifiers": its shifted press — a carried Shift or the
        // long press, exactly as on every shift-admitting button — runs FILE →
        // REVERT, whose chord is Ctrl+Alt+O with no shift (is_revert_project_key,
        // gui_input.h, unchanged; Ctrl+Shift+O binds nothing). So the shift
        // becomes alt here and the keyboard's spelling stays as it is: the
        // icon's twin is the icon road's. The act's own prompt guards it and
        // its refusals card at revert_project, the menu row's road.
        if (tc.id == RedesignButton::OpenProject && chord.shift) {
            chord.shift = false;
            chord.alt   = true;
        }
        // A LIFT THAT SPELLS CTRL+C UNDER A STANDING MARKER-LANE EDITOR IS
        // REFUSED ON THE EDITOR'S OWN SWALLOW CARD (architect 2026-09-29): the
        // flag editor, in every kind, raises no veil, so the roster stays
        // pressable under it, and the chord would reach on_key as the
        // editor's own copy (text_editor's ctrl-exact Ctrl+C) — the field's
        // selection copied, or nothing, silently, behind a face named Copy
        // Resolved Value or Center. A face named for the resolved value never
        // copies something else, so its lift says what the same button said
        // while its chord was one the editor does not own. Both roads meet
        // here: Copy Resolved Value's plain lift and Center's ctrl press.
        // THE KEYBOARD IS UNTOUCHED: Ctrl+C typed in the field copies the
        // field's selection, the editor's key classifier being the keyboard's
        // owner, not this. The faces do not grey for it. The dialog editors
        // need no term: the veil at this body's head already refused the lift,
        // so a keyboard-modal editor standing here is the marker lane's.
        if (tc.key == GuiKeys::C && chord.ctrl && !chord.shift &&
            !chord.alt && keyboard_modal_editor_active()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 modal_editor_swallow_card(tc.key, chord));
            return;
        }
        // A ROSTER LIFT'S CHORD is the pointer's own under the color picker
        // (roster_chord_in_flight_).
        const RosterChordScope roster_chord(roster_chord_in_flight_);
        on_key(tc.key, chord);
        return;
    }
}

// THE CHROME BUTTON HOLD-REPEAT, fired from the run loop's tick (architect
// 2026-08-16): while a press stands on a button whose chord row sets `repeats`
// — the bottom row's four cardinal arrows and the icon row's Undo / Redo (the
// column is the membership) —
// synthesize that button's chord on the keyboard's own
// cadence, so a held BUTTON walks at the speed the held KEY does. It exists for the glass rig, which has no keyboard, and it
// works there with NO TOUCH-SPECIFIC CODE: the one-finger translation delivers
// an ordinary left press and an ordinary left release, so it arms and ends this
// arm exactly as a mouse does, and its motion deliveries keep the position this
// body re-hits honest.
//
// THE SCHEDULE'S TWO NUMBERS COME FROM DIFFERENT OWNERS ON PURPOSE. The FIRST
// fire is one kHoldBeatMs after the press — the product's FIXED beat, matched
// to the architect's compositor delay, and DECOUPLED FROM THE HOLD DELAY on
// both devices (architect 2026-09-29): a repeat delay measures the cadence of
// a stream of repeats, as the double-click window and the tap coalesce
// measure the gap between two presses, and none of them is a hand resting on
// a thing until it changes meaning, which is what kHoldDelayMs times (the
// readers' inventory is at kHoldBeatMs). Every
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
// flip is VACUOUS for UNDO / REDO: a restore pushes no entry
// through the coalescing — it pops one stack onto the other and clears the
// coalesce stamp (Undo::restore_history_entry) — so a first fire with the bit
// clear and the fires behind it with the bit set reach the same act.
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
// arm gone), a dead PAINTED enabled bit PAUSES, the claim's own bit since
// 2026-09-24 and the disabled-press consume's mirror (and since 2026-09-13
// THE HISTORY'S END for Undo / Redo: a burst that walks
// the history to its end meets the dead bit on its next fire and rests there
// under the held pointer, greyed, nothing un-pausing it while the hold
// stands, and the lift that ends a fired burst is consumed, their faces
// greying on an empty stack, a locked target tab and the Restrict-undo lamp's
// verdict through history_step_actionable and its companions), a
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
        // THE FIRE CARRIES THE ARM'S OWN MODIFIERS, the arm-time chord build's
        // twin (arm_redesign_press) and the same expression: a HELD REPEAT
        // CARRIES ITS MODIFIER (2026-08-31), so a Shift+hold on Up / Down
        // fires the ten-unit step and a Ctrl+hold the three-unit one, every
        // fire (Left / Right admit no modifier since 2026-09-21). Both bits
        // were narrowed at the press to a button that admits them, so this
        // restates no admission — it re-asks the ctrl one only
        // because the expression must be the arm's verbatim, and drift between
        // "what the burst was judged eligible for" and "what it fires" is
        // exactly what one expression in two places prevents.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl ||
                      (arm.ctrl && redesign_button_ctrl_admits(tc.id));
        chord.shift = tc.shift || arm.shift;
        chord.alt   = tc.alt;
        // A ROSTER BUTTON'S FIRE — its probe and its dispatch — is the
        // pointer's own under the color picker (roster_chord_in_flight_).
        const RosterChordScope roster_chord(roster_chord_in_flight_);
        if (!repeat_eligible(tc.key, chord)) {
            arm.repeat_due_ms = 0;
            return;
        }
        if (!app.redesign_buttons[static_cast<size_t>(arm.index)].enabled)
            return;
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
// PURE GEOMETRY, DELIBERATELY. Whether a row can be hovered, armed or
// activated is a SEPARATE question, the row's PAINTED enabled bit
// (AppState::Dropdown::item_enabled, architect 2026-09-24), asked by each of
// this function's three callers on its own side — one geometric answer, one
// enablement answer, neither hiding inside the other. (The separation was
// kept through 2026-08-15..09-24, when no item could grey, as the shape a
// disabled item takes; the truthful menus put it back to work.)
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
// WHY THE DERIVE (2026-08-03): that premise has exactly ONE
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
    // A GREYED ITEM ACTIVATES NOTHING AND CLOSES NOTHING (2026-09-24, and
    // 2026-08-08..15 before it). The recorded arm can never name one — the
    // hover walk resolves a greyed row to "no item" — but the DERIVE reads raw
    // geometry, so the gate belongs on this side of it too, and answering -1
    // routes the release into the armed-nothing return below: consumed, menu
    // still up, the same answer the press over that row already gave. THE
    // BIT IS THE PAINTED ONE (architect 2026-09-24, strictly as-painted),
    // asked AT THE RELEASE: a row the comparator has already repainted grey
    // under a held press does not fire, and a row still painted live
    // dispatches — the command's own body answering a refusal that landed
    // inside that one tick, carding where its key would card (the expensive
    // checks card there already).
    if (armed >= 0 && !app.dropdown.item_enabled[static_cast<size_t>(armed)])
        armed = -1;
    // The press claim ends here whatever it armed, so nothing the pointer does
    // afterwards can move an arm this button-down no longer owns.
    app.dropdown.press_began_on_item = false;
    if (armed < 0) return true;   // nothing was lit; consumed, menu stays up
    const DropdownMenu menu = app.dropdown.menu;
    app.dropdown.pressed_item = -1;
    // THE WINDOW MENU'S VERB (2026-10-08, kWindowPopupItems): close first,
    // then the caption's own act for the verb — Restore and Maximize the
    // maximized toggle (each greyed where it does not apply), Minimize the
    // platform's, Close the one close road (on_window_close: the dirty
    // prompt, the gesture end), exactly what the caption's buttons dispatch
    // at their lift.
    if (menu == DropdownMenu::Window) {
        const WindowPopupVerb verb =
            kWindowPopupItems[static_cast<size_t>(armed)].verb;
        close_dropdown();
        switch (verb) {
            case WindowPopupVerb::Restore:
            case WindowPopupVerb::Maximize: gui.toggle_window_maximized(); break;
            case WindowPopupVerb::Minimize: gui.minimize_window();         break;
            case WindowPopupVerb::Close:    on_window_close();             break;
        }
        return true;
    }
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
        // refusals) applies identically. An item whose command would refuse
        // cheaply never gets here — it is greyed and the derive above dropped
        // it (2026-09-24) — and an expensive refusal cards from the act. No
        // stop, no modal, nothing restated here.
        // ALL THREE OF THE FILE MENU'S ROWS RIDE THIS BODY WHOLE (Revert, on
        // Ctrl+Alt+O, since 2026-09-13, reaching its one act the way Open does):
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
        // That menu is deleted; since 2026-09-24 every row greys on its own
        // command's cheap refusals, the `h` view's allowlist among them.
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
    // IT OPENS ON A LOCKED TAB SINCE 2026-09-04, and this site says nothing
    // about the lock either way: the editor refused on a read-only ACTIVE tab
    // from 2026-08-07 to that date, and the lock now governs the KEYS at their
    // own commit arms instead — the four sidecar rows are engine keys and say
    // the lock's sentence when they commit, while the five device rows commit
    // regardless (the account is at GuiSettingsEditor::open). The modal
    // playback stop stays at that opener, where it moved off this line in
    // 2026-08-07. THE ITEMS GREY DURING A LOAD ALONE (2026-09-24,
    // dropdown_item_enabled): the opener refuses nothing cheap, and the
    // commit arms' refusals answer on the editor's own surfaces.
    const SettingsPopupItem& item =
        kSettingsPopupItems[static_cast<size_t>(armed)];
    close_dropdown();
    // THE MENU'S COMMAND ROWS (2026-10-07, SettingsPopupAct): Pick Colors
    // opens the color picker on the half opposite this lift's x; the
    // opener carries its own refusals.
    if (item.act == SettingsPopupAct::PickColors) {
        color_picker.open(x);
        return true;
    }
    // TRUE COLORS (2026-10-08): the conversion's toggle, its apply shape
    // whole at its one act.
    if (item.act == SettingsPopupAct::TrueColors) {
        toggle_true_colors();
        return true;
    }
    settings_editor.open_prefilled(item.key);
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
// A CLOSED MENU LEAVES NOTHING BEHIND (architect 2026-10-01): the row kept
// no "closed but armed" mode after its one producer — a non-menu row-1
// button's hover close — went with the view bar, and the mode was deleted
// whole, so every close is the struct reset and a cold row answers a click
// alone.
// THE `h` HISTORY MODE's POINTER ALLOWLIST and its acts. True = the press is
// CONSUMED here (refused outright, or handled as one of the mode's own acts);
// false = the press is one of the navigation gestures the mode leaves alone, and
// on_button_press proceeds with it untouched.
//
// THE VIEW MIRRORS THE LIVE VOCABULARY THROUGH ITS OWN GATES (re-derived
// 2026-08-12 under the eighth glass ruling, pan-primary — the view admits
// pan/zoom, consumes authoring): its NAVIGATION SURFACE is the WHOLE waveform
// (both halves — the view has no scrub since playback left it whole,
// 2026-08-05) and nothing else, and on it plain drag = the grab-pan,
// motionless plain click = THE MODE'S LAND at the column (deferred to the
// release like everywhere else), shift+drag = the view-local region former,
// ctrl+drag = the strip-drag zoom. The RULER and the MARKER lane's empty
// stretches are PLACEMENT SURFACES in here as outside (architect 2026-09-25,
// the lanes leaving the navigation surface on both devices): a motionless
// plain or shift click lands the playhead through the mode's land, and every
// drag there does nothing.
//
// THE VIEW IS SILENT (architect 2026-08-05): NO PRESS IN HERE STARTS AUDIO —
// the mode's full-song trim forces a full target preview render for an
// audition the view never needed. Playback removal is whole: Space left the
// keyboard allowlist in the same ruling, and the entry owner stops a session
// that was already running. (The mode's deferred click act runs the shared
// placement body, whose play stop is a no-op in here.)
//
// WHAT PASSES THROUGH, the whole list — the mode's navigation vocabulary, the
// pointer half of what history_mode_key_blocked admits on the keyboard:
//   * CTRL-exact over the navigation surface — the one nav drag's ZOOM entry
//     (arm_nav_zoom_press; the dual-axis strip drag this bullet used to name
//     died 2026-08-14 with the zoom's rotation onto the horizontal axis), on
//     the waveform (a ctrl press on a diff FLAG is the mode's membership
//     toggle, claimed below before this can fork; a ctrl press on the lanes'
//     empty stretches is consumed since 2026-09-25). Ctrl+Shift is NOT admitted anywhere on
//     this gate's content (the chrome's one pair admission, the walk
//     button's, is claimed at the band above this gate): over
//     the waveform it is already a no-op, and over the TRIM BAR it sets the
//     end bound, which is a write. ALT passes nowhere — its pointer
//     vocabulary is empty product-wide.
//
// THE ACTS, all pure navigation:
//   * a PLAIN press on the NAVIGATION SURFACE arms the mode's own PENDING
//     CLICK / GRAB-PAN (arm_nav_press with the history flag): nothing at
//     press; a motionless release runs THE MODE'S LAND — clear the mode focus
//     + selection (the deselect's mode analog, through the one pair clearer),
//     seat the playhead at the press column (run_nav_click_act's history arm — the
//     live recipe through the shared placement body, whose play stop has
//     nothing to stop in the silent view); a crossed drag is the captured pan, which moves no
//     playhead and clears nothing. The ruler and the empty lane stretches
//     take the same pending as PLACEMENT SURFACES (arm_placement_press,
//     2026-09-25), so their motionless clicks land the playhead and a drag
//     there does nothing.
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
//     ADD TO SELECTION FORKS THIS ACT since 2026-09-17 (architect): with the
//     lamp lit the very same press runs the CTRL click below instead — the
//     membership toggle over the mode's list — which is the sticky ctrl's own
//     meaning applied to this router, and the one road onto a two-flag
//     selection a finger has (AppState::add_to_selection).
//   * the MARKER LANE's two MODIFIED clicks, on its FLAG BOXES: SHIFT takes
//     the contiguous range from the focus, CTRL toggles one flag's membership,
//     and both then focus the clicked flag and land the playhead on it (the live
//     selection model over the mode's own list; the store selection stays as
//     untouched as the plain click leaves it). THE CTRL ARM HAS A SECOND
//     PRODUCER since 2026-09-17 — a PLAIN press while Add to selection stands,
//     the plain claim's own fork above. A modified lane press that hits
//     NO flag answers exactly as in the live views: shift is the placement
//     click (no sweep), ctrl a consumed nothing (no zoom) — the lanes being
//     placement surfaces since 2026-09-25.
//   * SHIFT-exact on the navigation surface (the waveform) — THE SWEEP, CARVED OUT OF ITS OWN
//     TRIM WRITE: clear the mode focus + selection, seat the playhead at the
//     column, arm the drag (the live former's recipe with the mode's deselect
//     analog — EVERY FORMER DROPS THE SELECTION ITS SURFACE OWNS, the
//     family rule at RegionDragState, app_state.h). The drag rides the one motion
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
    // waveform and nothing else (the lanes left it 2026-09-25). It has been
    // the full waveform height in here since playback left the view
    // (2026-08-05), and since 2026-08-13 the LIVE surface is the same rect —
    // the two halves became one out there too — so the mode's surface is no
    // longer a special case and there is nothing left for a mode term to say.
    const bool on_nav_surface = point_on_nav_surface(app, x, y);

    // THE MULTI-SELECTION'S TWO MODIFIED CLICKS (architect 2026-08-05), asked
    // FIRST because they are the only modified presses in this mode that hit
    // an ITEM: shift takes the contiguous range from the focus, ctrl toggles
    // one flag's membership, and both then focus the clicked flag and land the
    // playhead on it — the live selection model re-expressed over the mode's
    // own list, with the store selection as untouched as the plain click
    // leaves it. FLAG BOXES ONLY (the symmetry ruling, 2026-08-06): a
    // modified lane press that hits NO flag falls through to the claims
    // below, which answer it exactly as the live lanes do — shift the
    // placement click, ctrl a consumed nothing (the lanes being placement
    // surfaces since 2026-09-25).
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
    // claimed above). The entry arms NO click act, so the mode needs no arm
    // of its own; a mid-drag ctrl release pans, the pan being equally
    // admitted (the wheel class). Everywhere else — the trim bar's begin set
    // above all, and the placement lanes, which arm no drag — it is consumed.
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
        const int64_t sample = place_playhead_at_click_column(x - area.x);
        if (sample >= 0) arm_region_drag_at(x - area.x, x, y);
        return true;
    }
    // SHIFT-exact on a PLACEMENT LANE (2026-09-25): the click keeps its
    // placement — the mode's land, the shift former's own press half — at the
    // lift, and the drag does nothing (arm_placement_press; the live router's
    // shift claim is the same shape).
    if (shift && !ctrl && !alt && point_on_placement_lanes(app, audio, x, y)) {
        arm_placement_press(x, y, /*history=*/true, /*seed_empty_lane=*/false);
        return true;
    }
    // Every other modified combination — alt anything, ctrl+shift, shift off
    // both surfaces — is a consumed nothing.
    if (ctrl || shift || alt) return true;

    // PLAIN FROM HERE. The band walk mirrors the live press router's: the
    // ruler is a placement lane (arm_placement_press, 2026-09-25), the trim
    // bar keeps its framing double-click, the marker lane splits
    // flag-vs-stretch, and the waveform is the navigation surface.
    {
        const GuiRect ruler = top_ruler_row_area(app);
        if (y >= ruler.y && y < ruler.y + ruler.h) {
            arm_placement_press(x, y, /*history=*/true,
                                /*seed_empty_lane=*/false);
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
    // A consumed press seeds nothing (the family rule) and frames and is done,
    // arming nothing — a double-click is never a drag (architect 2026-09-22;
    // the rule is at DoubleClickSurface, app_state.h), which the live band
    // now obeys too, so NOTHING differs between the two and the framing
    // itself is the live owner's call rather than a mode arm.
    //
    // THE WAY TO SEE A WHOLE DELTA is to zoom out and come back: bare `0` is on
    // the mode's allowlist and the delta's flags are laid across the whole
    // piece, so a full zoom out shows all of it — the product's own navigation
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
        // a placement lane (2026-09-25) — the placement pending, whose
        // motionless release lands the playhead at the column through the
        // mode's land, a drag there doing nothing. No EmptyLane seed — the
        // marker create is authoring, consumed in here.
        const int hit = hit_test_flag(app, audio, x, y);
        if (hit >= 0) {
            // THE PRESS ACTS (2026-08-17): the focus move and the land are the
            // CLICK, run here on the live hit.
            //
            // ADD TO SELECTION FORKS IT (architect 2026-09-17): while the
            // sticky ctrl stands, a plain press on a diff flag runs the MODE'S
            // OWN CTRL BODY instead — the membership toggle, the focus and the
            // land, everything else left standing. THE LIVE MODEL'S SHAPE IS
            // RE-EXPRESSED OVER THE MODE'S LIST, exactly as
            // select_history_diff_flags_modified already re-expresses the live
            // selection model: out there the fold is
            // `ctrl || (add_to_selection && !shift && cell ==
            // MarkerCell::Payload)` at run_marker_click_act's one toggle term,
            // and in here BOTH of its extra halves cost nothing to spell,
            // each because the structure already guarantees it.
            // SHIFT CANNOT REACH THIS ARM — the router's
            // modified branch claims every shift and ctrl flag press above,
            // and the `if (ctrl || shift || alt) return true` line below it
            // eats the rest, so PLAIN is all this arm ever sees. And A DIFF
            // FLAG PAINTS NO CELLS: it publishes both cell boundaries at its
            // own rect's right edge, so hit_test_flag_cell answers Payload
            // over the whole of one and the payload half is vacuously true in
            // here. Adding either term would restate a guarantee the structure
            // already gives. THE EMPTY-LANE STRETCH IS UNTOUCHED, the lamp governing
            // FLAG presses alone as it does on the live lane
            // (AppState::add_to_selection's scope rule).
            if (app.add_to_selection)
                select_history_diff_flags_modified(hit, /*extend=*/false);
            else
                focus_history_diff_flag(hit);
        } else {
            arm_placement_press(x, y, /*history=*/true,
                                /*seed_empty_lane=*/false);
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
// inverted): its ONE caller is handle_history_mode_press's plain flag claim,
// which reaches it while ADD TO SELECTION IS DARK — with that lamp lit the
// same claim runs the mode's ctrl body instead (2026-09-17, the fork at the
// claim).
// The router resolves a flag only since
// 2026-08-12 (an empty lane stretch arms the PLACEMENT pending now —
// arm_placement_press, 2026-09-25; the navigation surface's pending click
// from the eighth glass ruling until then — whose deferred land clears the
// focus through the same pair clearer and then PLACES the playhead, where
// this body's out-of-range arm lands nothing);
// out-of-range tolerance kept, answering clear-and-land-nothing — and it is what
// makes an armed index that the walk somehow outran harmless.
//
// IT CLEARS THE MULTI-SELECTION (architect 2026-08-05), which is the live
// model's own shape rather than a rule of the mode's: a plain click REPLACES the
// selection with what it hit, so the focus alone is then a selection of one, and
// the revert act's subject falls back to that focus with nothing to remember.
//
// IT LANDS THROUGH THE MOVEMENT OWNER (land_playhead_on_source_frame). The
// out-of-range arm (`hit` < 0, tolerance the router cannot produce) lands
// nothing; the empty-lane click the router DOES produce goes to the placement
// pending's act (run_nav_click_act), which places the playhead there. (It hid the trim region
// overlay from 2026-08-06 until the resting overlay was deleted on
// 2026-09-22.)
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
    // (the flag's and its stem's colours swap; the stem's column stays put),
    // and none when it did
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
// become, so nothing needs the lift). TWO CALLERS SINCE 2026-09-17: the
// router's MODIFIED flag claim, with the press's own modifier shape, and its
// PLAIN flag claim while ADD TO SELECTION stands, which calls the ctrl arm
// (extend=false) — the sticky ctrl's whole meaning, and the only road onto
// this multi-selection a finger has, there being no ctrl key on glass
// (AppState::add_to_selection). It
// is the LIVE selection model re-expressed over the mode's own list
// (run_marker_click_act, AppState::add_to_selection):
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
// BOTH LAND THROUGH THE MOVEMENT OWNER, past the range guard below, so a call
// that changes nothing lands nothing. (Both hid the trim region overlay from
// 2026-08-06 until its resting form was deleted on 2026-09-22.)
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
    // does — this is the ONE route every open passes, the anchor click and
    // the hover switch alike.
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
    // same way — the allowlist admitted zoom in / out / full zoom out,
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
    // but unlike File, whose Ctrl+Q the mode ADMITS, every one of Edit's three
    // rows is a chord the mode's allowlist drops. The per-item answer that
    // makes a command menu safe in here is "the command runs and its own gate
    // refuses", and when that is the answer for EVERY row the menu is a box
    // that opens onto nothing. Refusing it is the same criterion the narrowing
    // used, read the other way: the face must not promise more than the keys
    // deliver. Its anchor greys beside this (menu_anchor_live, the face's read).
    //
    // (THE SERIES MENU JOINED THE LOCKOUT ON 2026-08-27 on that same
    // criterion and left it with its own deletion on 2026-09-04: both of its
    // rows — the BPM opener (bare `m` then, Ctrl+B since 2026-09-15) and
    // bare `i` — were chords the mode's allowlist drops,
    // and they still are. What changed is where the face lives: the two
    // commands are icon-row BUTTONS again, so the DERIVED walk below greys
    // them with nothing hand-listed, which is what a chord-bearing button buys
    // that an anchor cannot.)
    //
    // (THE HELP MENU JOINED THE LOCKOUT ON 2026-09-03 as the COMMAND menu
    // case Edit is refused under, with nothing re-argued — its single row an
    // ordinary chord row, Shift+L, a chord the view's allowlist refuses in
    // the mode's own sentence, so the one row was a box that opened onto
    // nothing; chord-less for its first hours it was the SETTINGS case — and
    // LEFT IT WITH ITS OWN DELETION on 2026-09-09, the top strip relayout;
    // the panel itself was deleted on 2026-09-30.)
    //
    // THE GUARD READS THE ANCHOR'S PAINTED FACE (architect 2026-09-24,
    // strictly as-painted): RedesignButtonFace::enabled, which
    // publish_button_face stamps from the shared owner (menu_anchor_live,
    // app_state.h) at redesign_button_enabled's head and the per-tick
    // comparator keeps true — so which menus open and which anchors look
    // live cannot drift apart, the open agreeing with the screen. Since
    // 2026-09-24 it carries a second clause beside the mode partition: an
    // anchor whose every item greys is dead too (during a load, Edit and
    // Settings), so no menu opens onto nothing but greyed rows. THE FOLDER OVERLAY IS IN THAT OWNER TOO and takes the same
    // partition (2026-09-03 evening: the menu row stands above the band with
    // File LIVE and the other anchors dead — two since the Help menu's
    // 2026-09-09 deletion — the architect's ruling at the
    // owner) — and here it is load-bearing rather than defensive, because the
    // HOVER SWITCH is live under the band: File's menu can be up over the
    // player, and crossing onto the dead Edit anchor arrives at this guard,
    // which returns from ABOVE the close so File stays exactly as it was.
    // (Every anchor was dead under the band for the hours between the gap's
    // close and that ruling, when no route reached this term at all; it was
    // File-exempt like this on 2026-09-02, when the panel stopped at row 1's
    // foot.)
    // THE WINDOW MENU'S ANCHOR IS THE CAPTION'S BUTTON (2026-10-08,
    // dropdown_hangs_from_caption): its painted bit, never a roster face.
    const bool anchor_live =
        dropdown_hangs_from_caption(menu)
            ? app.caption_buttons[static_cast<size_t>(GuiCaptionButton::Close)]
                  .enabled
            : app.redesign_buttons[static_cast<size_t>(
                  redesign_button_index(dropdown_anchor_button(menu)))].enabled;
    if (!anchor_live) return;
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
    // ONE route every open passes — the anchor click and the hover switch both
    // arrive here — while the band claim carries only the click. The hover
    // switch is arguably out of reach with an edit open (it needs a menu
    // already up, opened by a click, which would itself have closed the edit),
    // but a rule that is impossible by construction is worth more here than one
    // that is unreachable by argument. A toggle that CLOSES a menu discards
    // nothing: it returned above.
    //
    // The teardown is called DIRECTLY rather than through
    // close_top_flag_editor_for_outside_press, whose job includes the
    // is-the-press-inside-the-box test. There is no press here at all on the
    // hover switch, and the rect test is not merely skipped but INAPPLICABLE: a
    // row-1 button cannot be inside the flag editor's box, which lives in the
    // marker lane below the whole top strip's button rows.
    //
    // ROW 1 IS THE ANCHORS ALONE, so a menu's open is the whole of the row's
    // reach into an edit. (Quit needed nothing here while it was a button,
    // its Ctrl+Q being one of the three chords that gate admits; since
    // 2026-08-13 it is an ITEM of this menu, so the discard above covers it like
    // every other row.)
    if (text_editor::is_active(app.top_flag_editor)) {
        flag_editor.exit_top_flag_edit_no_commit();
    }
    // THE ITEMS OPEN COLD. close_dropdown's reset above (or the default
    // state, when nothing was open) left AppState::Dropdown::item_enabled all
    // false and item_rects all zero, and nothing else writes them before
    // paint_dropdown's first pass publishes both — so an open whose first
    // paint has not run yet offers nothing clickable, the stash doctrine
    // (nothing painted, nothing clickable), for at most one frame.
    app.dropdown.menu         = menu;
    app.dropdown.hovered_item = -1;
    // THE OPEN EDGE DAMAGES THE BOX BEFORE THE BOX EXISTS. Its rect is not
    // published until paint_dropdown runs, and a redraw is CLIPPED to the
    // damage it was handed — so strip damage alone would clip away whatever the
    // popup hangs past the strip. The settings menu (nine rows and a
    // separator) is taller than the four lanes below the menu lane at every
    // scale, so without the band its overhang would never paint.
    //
    // The HEIGHT is derivable without painting (dropdown_h_px — item count
    // times item height, plus the separator blocks and the borders), so the
    // damage is exact vertically. The WIDTH is not (it needs the widest shaped
    // label), so this damages FULL WIDTH from the button's top down — a band,
    // not a guess, and cheap because it happens once per open.
    //
    // THE TOP EDGE IS THE MENU LANE'S FOOT, the same expression paint_dropdown
    // places the box at — read from the SAME lane accessor there, so the
    // damaged band and the painted box start on the same row of pixels; a
    // band anchored anywhere else would leave a strip of the popup unpainted
    // at one end. It is the LANE and not the anchor's rect because the lane
    // is the one owner of that row: the anchor's published rect is its pill,
    // which IS the lane (render.h's menu-row block), so the two agree
    // exactly. The x is still the anchor's (the
    // dropdown hangs off the thing that opened it, architect 2026-08-02);
    // only this band's y reads the lane, and it damages FULL WIDTH anyway.
    // (The window menu hangs from the CAPTION lane's foot instead,
    // dropdown_hang_y's one expression for both readers.)
    viewport.invalidate_rect(
        GuiRect{0, dropdown_hang_y(app, menu), app.width, dropdown_h_px(menu)});
    // THE TOOLTIP GOES DOWN ON THE OPEN EDGE, A HARD END (a menu opening
    // hides the tip at once) — the two floating surfaces cannot coexist
    // (paint_handler.h states the pair), and this is the one line that makes
    // that structural rather than a reachability argument about which routes
    // reach an open. The roster walk alone would find no owner under the
    // popup only on its next pass — the next tick, after a frame may already
    // have painted the two together.
    hide_shift_tooltip();
    // THE TOOLTIP STAYS DOWN for as long as the popup is up:
    // redesign_button_hover_zone refuses every roster button while a menu is
    // open, so the roster walk (recompute_redesign_button_hover) can never
    // start a wait. The anchor's open frame is the painter's own open
    // condition (paint_menu_row), which the strip's damage repaints.
    viewport.invalidate_top_strip();
}

// THE POPUP'S POINTER-DERIVED STATE, DROPPED AT THE ONE HOOK FIRED ON BOTH
// the pointer-leave AND capability-loss edges (2026-08-03: the two are
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
// THE MENU ITSELF STAYS OPEN — leaving the window is not a dismissal — on
// every leave reason, capability loss included: the popup has no
// pointer-derived existence at all, and only its pointer-derived faces go.
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

// THE HOVER TOOLTIP'S BOX GOES DOWN — the one hide body, shared by the hard
// end, the pointer leaving the box's button (note_tooltip_hover) and the
// life's end (tick_tooltip). It damages the strip AND the box's last painted
// rect, and that is why every edge that takes a hint away must come through
// here rather than leave it to a hover walk: the box hangs OUTSIDE its strip
// (under the pointer, or above the bottom row), so a repaint that found no
// box to draw would publish a zero rect and return, and the overhang would
// have nothing left to erase it. Surface-agnostic: the painted rect is
// wherever that hint was drawn.
static void take_tooltip_box_down(AppState& app, Viewport& viewport) {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    t.expire_due_ms = 0;
    if (!t.visible) return;
    const GuiRect painted = t.rect;
    t.visible = false;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

// THE HARD END (the model and the callers' inventory are at
// AppState::RedesignTooltip and the declaration): the box down at once, the
// wait stopped and the reshow disarmed. `hovered`, the anchor, the seen
// position and `spent` STAND — the pointer has not moved, so the button under
// it is still the one it rests on, and a resting pointer starts nothing until
// a motion carries it past the slop (on a spent button, not even then). The
// press is the one hard end that spends, and it does so at its own site
// (on_button_press); this body spends nothing.
void GuiInputHandler::hide_shift_tooltip() {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    t.wake_due_ms = 0;
    t.reshow      = false;
    take_tooltip_box_down(app, viewport);
}

// THE POINTER LEAVING (the declaration carries the contract). The wait's
// button goes and the seen position is forgotten, so the re-entry's first
// walk is a motion onto whatever it lands on, and the hard end follows: the
// pointer leaving the window and the pen's hover leaving the plane both leave
// the tool, which hides a tooltip at once.
// A TRANSLATED CONTACT'S LIFT IS NO LEAVE FOR THE TOOLTIP (architect
// 2026-09-29) and returns before touching anything: the lift is the release
// of a press that was already the hard end (the box down, the reshow
// disarmed), and under that held press the button and the anchor followed
// the contact (note_tooltip_hover's held arm), so both stand at the lift
// point with the seen position beside them — the state a mouse's release
// leaves — and so does `spent`, the tap's press having spent the button.
// A hover that follows (the S Pen's after a lift that left a finger on the
// glass) arrives as an enter whose walk is the model's own: on the same
// button it starts no wait however far it moves (the button is spent until
// the hover leaves it), and onto another button it is an arrival. The pen's
// lift off an empty glass never comes here (architect 2026-10-07): the pen
// stays the pointer and the translation ends with a motion at the lift, the
// mouse's own case. A finger has no hover after its lift, and its next
// contact's entry motion already reads held, so nothing changes for it.
// THE HARD LEAVE forgets the spent button with the hovered one: leaving the
// window or the plane is leaving the tool.
void GuiInputHandler::end_tooltip_hover(TooltipHoverEnd end) {
    if (end == TooltipHoverEnd::ContactLift) return;
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    t.hovered = AppState::RedesignTooltip::Owner{};
    t.spent   = AppState::RedesignTooltip::Owner{};
    t.seen_x  = AppState::kTooltipUnseen;
    t.seen_y  = AppState::kTooltipUnseen;
    hide_shift_tooltip();
}

// THE WAIT'S ONE WRITER, shared by both hover walks (the roster's and the
// modal dialog's) so the model is one rule rather than two copies; the model
// is stated at AppState::RedesignTooltip. In order:
//   * THE LEAVE OF A SPENT BUTTON: any owner but the spent one (none, or
//     another button) clears `spent`, before every arm, so the held arm's
//     slide off the pressed button clears it as the unheld arrival does;
//   * LEAVING THE BOX'S BUTTON: while a box stands, the pointer anywhere but
//     on its own button takes it down at once;
//   * NO BUTTON: the wait stops and the reshow disarms (a gap);
//   * A HELD PRESS: the button and the anchor follow the pointer and no wait
//     runs, so the release's resting pointer arms nothing;
//   * A NEW BUTTON: re-anchored where the pointer is, and the wait starts if
//     this walk saw a motion — at the reshow when the walk before stood on
//     another button and a hint has shown since the last gap, at the full
//     wait otherwise. The owner is compared whole, across surfaces as well
//     as within one;
//   * THE SAME BUTTON: a motion past the hover slop from the anchor on either
//     axis re-anchors there (AOSP View's updateAnchorPos: within the slop is
//     stillness) and restarts the standing box's life, or, with no box, the
//     full wait — unless the button is SPENT, where it re-anchors and starts
//     nothing (a spent button never has a box: the life's end and the press
//     that spend it both took the box down, and no wait runs to show one).
//
// THE TOOLTIP LAMP IS ASKED FIRST (architect 2026-09-29, Enable Tooltips on
// bare backslash): while AppState::show_tooltips is dark this writer holds the
// wait's state clear — no hovered owner, no deadline — and returns, so no
// wait ever starts and tick_tooltip has nothing to ripen on either surface.
// It keeps only the SEEN position current, so the first walk after the lamp
// lights tells a motion from a resting pointer exactly as the lit model does
// (a lamp lit under a still pointer starts no wait until the pointer moves).
// The box itself went down at the dark edge (set_show_tooltips), so nothing
// here needs to hide one.
void GuiInputHandler::note_tooltip_hover(AppState::RedesignTooltip::Owner o) {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    if (!app.show_tooltips) {
        t.hovered     = AppState::RedesignTooltip::Owner{};
        t.spent       = AppState::RedesignTooltip::Owner{};
        t.wake_due_ms = 0;
        t.seen_x      = app.last_mouse_x;
        t.seen_y      = app.last_mouse_y;
        return;
    }
    const int64_t now = monotonic_ms();
    const int x = app.last_mouse_x;
    const int y = app.last_mouse_y;
    const bool moved = x != t.seen_x || y != t.seen_y;
    t.seen_x = x;
    t.seen_y = y;
    if (!(o == t.spent)) t.spent = AppState::RedesignTooltip::Owner{};
    if (t.visible && !(o.index >= 0 && o == t.owner))
        take_tooltip_box_down(app, viewport);
    if (o.index < 0) {
        t.hovered     = AppState::RedesignTooltip::Owner{};
        t.wake_due_ms = 0;
        t.reshow      = false;
        return;
    }
    const auto start_wait = [&](int64_t delay_ms) {
        t.anchor_x    = x;
        t.anchor_y    = y;
        t.wake_due_ms = now + delay_ms;
    };
    if (t.button_held) {
        t.hovered     = o;
        t.anchor_x    = x;
        t.anchor_y    = y;
        t.wake_due_ms = 0;
        return;
    }
    if (!(o == t.hovered)) {
        const bool direct = t.hovered.index >= 0;
        t.hovered = o;
        if (moved) {
            start_wait(direct && t.reshow ? kTooltipReshowMs
                                          : kTooltipInitialMs);
        } else {
            t.anchor_x    = x;
            t.anchor_y    = y;
            t.wake_due_ms = 0;
        }
        return;
    }
    const int slop = tooltip_hover_slop_px();
    if (std::abs(x - t.anchor_x) <= slop && std::abs(y - t.anchor_y) <= slop)
        return;
    if (t.visible) {
        t.anchor_x      = x;
        t.anchor_y      = y;
        t.expire_due_ms = now + kTooltipAutoPopMs;
    } else if (t.spent == o) {
        t.anchor_x = x;
        t.anchor_y = y;
    } else {
        start_wait(kTooltipInitialMs);
    }
}

// THE TOOLTIP'S CLOCK (the declaration carries the contract; the model is at
// AppState::RedesignTooltip). Two deadlines on a tick that already runs: the
// wait's ripening, then the life's end.
//
// THE SHOW EDGE seats the box under the pointer as it stands now (shown_x/y,
// the placement at tooltip_box_rect) but cannot know the box's own rect yet
// (the paint that publishes it is the frame this schedules), so it damages
// the band the box can hang into — the full-width band under the pointer, at
// most tooltip_damage_h_px() tall, and the band above the owner's button
// where a box that tall would cross the window's foot (tooltip_hang_bands,
// the bottom row's flip), one of which holds the box whole. No box stands
// at a ripening: an arrival took the old one down, and a motion on a
// standing box's own button restarts its life rather than a wait.
// THE LIFE'S END SPENDS THE BUTTON (the rule is at AppState::RedesignTooltip):
// the box stands only on the hovered button, so `spent` takes that one.
void GuiInputHandler::tick_tooltip() {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    const int64_t now = monotonic_ms();
    if (t.wake_due_ms != 0 && now >= t.wake_due_ms) {
        t.wake_due_ms = 0;
        if (t.hovered.index >= 0) {
            t.owner         = t.hovered;
            t.visible       = true;
            t.shown_x       = app.last_mouse_x;
            t.shown_y       = app.last_mouse_y;
            t.expire_due_ms = now + kTooltipAutoPopMs;
            t.reshow        = true;
            const TooltipHangBands bands = tooltip_hang_bands(app);
            viewport.invalidate_rect(bands.below);
            viewport.invalidate_rect(bands.above);
        }
    }
    if (t.visible && t.expire_due_ms != 0 && now >= t.expire_due_ms) {
        take_tooltip_box_down(app, viewport);
        t.spent = t.hovered;
    }
}

// NO DWELL RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT — the rule's one
// expression, read by the roster's hover walk. The two list owners are terms
// because each takes the keyboard whole; the pointer-transparent FLAG editor is
// a term through keyboard_modal_editor_active for the same reason, and it is
// the case the walk's veil cannot see: the roster stays hoverable under one
// (that editor raises no veil). THE COLOR PICKER IS NOT A TERM, though it
// takes the keyboard whole (architect 2026-10-07 evening for row 8,
// 2026-10-08 for the icon row): the two button rows stay on under it for
// the pointer, their tooltips included (modal_owns_bottom_row,
// paint_handler.cpp), and the hint names the act the pointer reaches; its
// veil over the menu row is the hover zone's term
// (redesign_button_hover_zone), and its one field, while it stands, is a
// term here through keyboard_modal_editor_active.
bool GuiInputHandler::tooltip_dwell_suppressed() const {
    return app.prompt.active || keyboard_modal_editor_active() ||
           app.render_player.active || app.picker.active;
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

// THE RELEASE-TIME ARMS' BUTTON-LOST END — the family's one owner (the
// contract, and why the family needed one, are at the declaration).
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
    // and is the touch stream's own: the TOUCH HARD END (a cancel, capability
    // or focus loss on a live pointer translation, hard_end_touch_stream)
    // delivers no button release at all (the fork is at
    // GuiInputCore::end_touch_left_hold), only this unheld motion — so a
    // system-taken touch while a letter is held is exactly the case, and
    // typing on glass is where it happens. (The second-finger upgrade was
    // its other producer until 2026-09-25, when a second contact on a live
    // translation came to be ignored.)
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
    clear_color_picker_drag();
    clear_popup_scroll_holds();
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
    // no trim is written — so a motionless shift click is a pure placement and
    // its release owes no commit tail. Once a drag, always a drag
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
    // live domain: the conversion reads ItemViewportBasis::spp, the PAINTED
    // item epoch, which can extend past the CURRENT live domain (a resize or
    // a target-domain contraction after the last item commit), so the
    // lagging painted basis's last column can land at domain_total — one past
    // [0, domain_total-1] — which the display-state validator would clear
    // wholesale (same rule as the press seat, place_playhead_at_click_column).
    // BOTH VALUES RIDE ONE
    // PAINTED VIEWPORT (architect 2026-09-24, strictly as painted): the
    // playhead's through playhead_frame_at_click_column and the trim's through
    // sweep_trim_frame_at_column both convert on the item basis (cold, the
    // live viewport by its own contract), so a job in flight parts neither.
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
    // to draw one in, the region being the trim everywhere.)
    if (!app.history_mode.active &&
        write_trim_from_sweep(app.region_drag.anchor_source_frame,
                              sweep_trim_frame_at_column(rel))) {
        app.region_drag.wrote_trim = true;
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
    // PLAYBACK IS UNTOUCHED per motion by THIS body: the press/begin's
    // placement stop (place_playhead_at_click_column) and the trim write's own
    // first-accepted-change STOP are the whole playback story of this gesture,
    // so no motion event stops anything.
    // The waveform invalidate below repaints the cursor's
    // HEAD AND STEM with the ground — its rect runs from the window top
    // down through the waveform, so the ruler-lane head is inside it (the
    // triangle this used to name retired with its lane in row 5); the
    // TIMESTAMP invalidate is owed
    // separately because the bottom row's CLOCK shows this cursor whenever
    // no scanner is active, and it lives outside the waveform area.
    // (THE SLIVER PARAGRAPH IS RETIRED with the release-time min-size check
    // (2026-08-18) and stayed retired when the minimum width floor went the
    // same way (2026-08-19): a jitter drag dissolves nothing and is widened to
    // nothing — it commits the sliver it drew, and Shift+0 is the way back.)
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
    // THE PEN'S HOT-FACE LATCH ANCHORS AT THE FIRST HOVER REPORT AND CLEARS
    // ON MOTION OFF THAT ANCHOR (pen_hot_latch_, the rule at its
    // declaration), here above every branch so the walk this motion runs
    // already re-lights the button under it. The latch arms after the lift's
    // own delivery, so the first motion met here unanchored is the pen's
    // first hover report: it sets the anchor and clears nothing.
    if (pen_hot_latch_.armed && !pen_hot_latch_.anchored) {
        pen_hot_latch_.anchored = true;
        pen_hot_latch_.x = mouse_x;
        pen_hot_latch_.y = mouse_y;
    } else if (pen_hot_latch_.armed &&
               std::max(std::abs(mouse_x - pen_hot_latch_.x),
                        std::abs(mouse_y - pen_hot_latch_.y)) >=
                   scaled_px(kPenHotRearmPx)) {
        pen_hot_latch_.armed = false;
    }
    // THE RELEASE-TIME ARMS END HERE ON THE BUTTON-LOST EDGE,
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
    // else, which is the distinction whose absence was the defect.
    if (!mods.primary_button_held) clear_release_time_press_arms();
    // THE TOOLTIP'S HELD BIT, at the same placement for the same reason (every
    // branch below returns, and the walks read it): the platform's button
    // state with this motion, so a button lost without a release (the touch
    // layer's abnormal end) cannot leave the wait refused
    // (AppState::RedesignTooltip).
    app.redesign_tooltip.button_held = mods.primary_button_held;
    // THE NOTIFICATION CARDS' HOVER (2026-08-29), above every branch: the cards
    // are hit above every veil, so their hover must be answered under every
    // modal too, and every branch below returns.
    //
    // THE CARD'S OPACITY IS NOT SPELLED HERE, and deliberately not: the
    // roster walk a card can stand over (recompute_redesign_button_hover)
    // carries the term itself, because it has a SECOND caller, the tick, and
    // a pointer RESTING on a card is exactly the case that must not start a
    // hint for what is under it. An early return here would be undone by the
    // next tick and would freeze a live gesture whose pointer merely crossed
    // a card. The cursor map, the press
    // claim and the touch pan zone ask the same one owner
    // (notification_card_at) for the same reason.
    update_notification_hover(mouse_x, mouse_y);
    // (THE POINTER CURSOR IS NOT RESOLVED HERE, 2026-08-03. A push stood at this
    // spot — above every gesture branch, so that each early return below still
    // left the right cue up — and it went with the other twenty-two: the cursor
    // has ONE owner now, the run loop's per-iteration tail hook, which runs in
    // the SAME iteration that dispatched this event and reads the two lines
    // above. Recording the position is therefore all a motion owes the cursor.)
    // THE ROSTER WALK STAYS LIVE UNDER EVERY MODAL SURFACE — the MODAL
    // branches that return before the no-gesture tail (the prompt's, the
    // player's, the picker's and the dialog editors' below) all call it —
    // because what it answers is not frozen by a modal: the armed chrome
    // press's inside bit, and the tail's hard end of a DIALOG tooltip owner
    // whose surface has gone. What it must NOT do under a modal is start a
    // roster TOOLTIP dwell, and that refusal lives in the walk itself (the
    // veil and the no-wait rule). The dialog's own buttons take their walk
    // through update_modal_dialog_hover in the same branches. What DOES
    // freeze the walk is an active pointer GESTURE — the branches below all
    // return without this call.
    //
    // AN OPEN DROPDOWN REFUSES THE WHOLE ROSTER (redesign_button_hover_zone,
    // app_state.h), so the walk in its branch below finds no hint owner while
    // the menu stays up. The ANCHOR of the open menu wears its open frame
    // through the paint condition (paint_menu_row), which is what makes a
    // switch visible on the frame it happens.
    //
    // THE OPEN DROPDOWN TAKES THE MOTION, above every gate: it owns the pointer,
    // so no gesture can be live under it (its own press consumed everything).
    if (app.dropdown.open()) {
        // The roster walk. While a popup is up it finds no hint owner on the
        // WHOLE roster (redesign_button_hover_zone refuses every button then —
        // the pointer belongs to the popup).
        recompute_redesign_button_hover();
        // HOVERING THE OTHER MENU'S BUTTON SWITCHES TO IT (architect
        // 2026-08-03) — the menu bar's standing behaviour: open one menu, slide
        // onto the next button without pressing, and that button's menu is what
        // is up. The switch goes through toggle_dropdown, the same owner the
        // CLICK uses, so the close-then-open, the anchor expression and the open
        // edge's damage are one route with nothing restated here; a menu that is
        // not the open one makes that toggle's same-menu test false, which is
        // exactly the switch. The button it switches ONTO wears the
        // painter's own anchor condition (paint_menu_row).
        //
        // The walk covers every menu that HAS an anchor instead of naming
        // them, so the anchor owner stays the one place that knows which button
        // emits which menu. Hovering the OPEN menu's own anchor is skipped
        // outright — no re-open, no close, no damage. Row 1 holds the anchors
        // alone, so every row-1 button the pointer meets here is one.
        //
        // THE SWITCH REFUSES UNDER A HELD PRIMARY BUTTON — this switch is row
        // 1's ONE HOVER ACT (the armed hover-open from a closed row was deleted
        // 2026-10-01, see input_handler.h's menu-row block), and a held button
        // is not a resting hover, on two producers: the TOUCH resolution burst's pre-press entry motion (with a menu OPEN
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
        // THE WINDOW MENU SWITCHES TO NO ROW-1 MENU (2026-10-08): it hangs
        // from the caption, not the menu row, and dtwm's menu does not slide
        // onto an application's menu bar; its press-and-drag stays its own.
        if (!mods.primary_button_held &&
            !dropdown_hangs_from_caption(app.dropdown.menu)) {
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
        // THE PROMPT DIALOG'S MOTION: the dialog buttons' walk, then the
        // roster walk (which under a prompt finds no hint owner through its
        // own veil term). The veil consumes the rest of the motion — nothing
        // below this branch runs.
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // THE FOLDER OVERLAY'S MOTION, for EVERY content and at the press
    // claim's own rank: a standing row arm follows the pointer (the feint's
    // inside bit, or the band's scroll drag once the vertical gate is
    // crossed) and owns the motion whole, and with no arm standing the
    // motion carries on below to the player's and the picker's own motion
    // branches (the band's rows wear no hover face, architect 2026-10-02). A
    // LOST BUTTON is the hard end: the arm drops and nothing commits, the
    // chrome arm's own rule.
    if (app.folder_overlay.press.armed) {
        if (!mods.primary_button_held) {
            clear_folder_overlay_press();
            return;
        }
        update_folder_overlay_press_motion(mouse_x, mouse_y);
        return;
    }
    // THE RENDER PLAYER'S MOTION (2026-08-28): a standing scrub drag moves
    // the marker, and otherwise the modal buttons' walk and the roster walk
    // run (no roster hint under the player — the no-wait rule — though THE
    // FILE ANCHOR stays live above the band, 2026-09-03 evening) — nothing
    // below this branch does.
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
    // THE PICKER'S MOTION (2026-08-28): the modal buttons' walk and the
    // roster walk (no roster hint, the no-wait rule), nothing else — no
    // field, no drag, no scrub; the overlay's own arm ran above.
    if (app.picker.active) {
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // THE COLOR PICKER'S MOTION (2026-10-07): its gesture's carry, else the
    // list's hover, the dialog buttons' walk and the roster walk.
    if (app.color_picker.active) {
        color_picker_motion(mouse_x, mouse_y, mods);
        return;
    }
    // F2.1: editor-text drag motion. Handled before the dialog-editor branch
    // (which returns) so the gesture reaches the three dialog editors' fields,
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
            const EditorTextDragState& d = app.editor_text_drag;
            if (d.by_words) {
                // THE DOUBLE-CLICK-DRAG: the double-clicked run stands as
                // the anchor and the moving end snaps to the boundary of the
                // run under the pointer, past the anchor on either side or
                // back to the anchor alone (the contract at
                // text_editor::extend_selection_by_words). No gate: the
                // second press already decided what this drag is.
                text_editor::extend_selection_by_words(
                    *g.ed, d.word_start, d.word_end,
                    editor_byte_index_at(g, mouse_x));
            } else {
                // The anchor set at press stays put; moving cursor_pos
                // extends the selection. IT IS THE SAME MOTION FOR BOTH ARMS
                // — the plain press seats the anchor at the caret it just
                // placed, the SHIFT press keeps the anchor that was already
                // standing, and neither rewrites it from here — so a
                // shift+click's drag goes on extending from the original
                // anchor (architect 2026-08-30). No gate either: the sweep
                // moves on every motion, the desk's own text vocabulary
                // (restored 2026-09-05 after one day under a press-age fork
                // — the glass's caret drag is the touch translation's own
                // stream and never arrives here).
                set_editor_caret_from_x(g, mouse_x);
            }
            if (g.dialog) viewport.invalidate_modal_dialog_area();
            else          viewport.invalidate_top_strip();
        }
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        return;
    }
    if (modal_dialog_editor_active()) {
        // THE EDITOR DIALOG'S MOTION — the three dialog editors in one branch
        // (the BPM bracket included since the dialog arc; it used to fall
        // through to the gesture branches, harmlessly, its presses all
        // swallowed): the dialog buttons' walk, then the roster walk, whose
        // veil term refuses the whole roster since the modal-trap
        // reach-through's retirement, so no roster hint starts under the
        // pointer. A choice editor's dropped list takes its hover first.
        settings_choice_motion(mouse_x, mouse_y);
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
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
            // A placement lane's crossed press began nothing (no capture, no
            // pan), so its moved end is the disarm alone.
            const bool moved   = sd.moved && !sd.placement_only;
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
            // A PLACEMENT LANE'S CROSSING ENDS THE CLICK AND BEGINS NOTHING
            // (architect 2026-09-25, the ruler and the marker lane leaving the
            // navigation surface on both devices): `moved` is what tells the
            // release the press was not a click, and no capture, pan or zoom
            // follows — the held button stays this pending's until the release.
            if (sd.placement_only) return;
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
            // after the begin: the
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
        if (sd.placement_only) return;  // past the crossing: nothing, ever
        if (sd.zooming) {
            // The ZOOM phase: dx off the live level about the seated pivot
            // (right zooms in), dy discarded, and the pointer's own notional x
            // held still by the freeze asserted above — the level spends the
            // lateral travel and the position must not spend it twice
            // (apply_nav_zoom_at; the two are different statements).
            apply_nav_zoom_at(mouse_x, mouse_y, /*final_event=*/false);
            return;
        }
        const double spp =
            painter_samples_per_pixel(app, audio, waveform_area(app));
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
            // finalizer gives, and the way the TOUCH layer's ABNORMAL end (a
            // hard end — cancel, capability or focus loss — on a live pointer
            // translation, hard_end_touch_stream) reaches this state for
            // free: it delivers a motion with the button unheld precisely so
            // an unmoved press commits nothing.
            app.pending_click = PendingClickAct{};
            return;
        }
        // THE GRAB GATE (grab_moved_threshold_px, app_state.h, 2026-09-29):
        // the trim bar is one surface with one gate, so this crossing and the
        // pending trim drag's below read the same distance and the set and
        // the drag it hands to resolve on one event.
        if (std::max(std::abs(mouse_x - app.pending_click.press_x),
                     std::abs(mouse_y - app.pending_click.press_y)) <
                grab_moved_threshold_px()) {
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
    // begins only once the pointer travels past the GRAB GATE
    // (grab_moved_threshold_px, twice the sweeps' and pans' drag gate — an
    // endcap or the bridge is a thing the press grabs; 2026-09-29).
    // A lost button before the crossing ends it as a motionless click (nothing
    // committed). Placed after the trim_drag branch above: on the crossing this
    // begins the drag AND applies its first update inline, so it does not fall
    // back into that branch this event.
    if (app.pending_trim_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // The motionless endcap/bridge press commits NOTHING (architect
            // 2026-07-30): its highlight publish is retired, so a press that
            // never travelled leaves the trim exactly as it found it.
            disarm_pending_trim_drag();
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
                grab_moved_threshold_px()) {
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
    // Motion just continues whatever the press already armed — the drag's
    // gates ran once at the threshold crossing, so nothing here re-checks view
    // or column; the
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
            // press (2026-08-17), and the touch layer's ABNORMAL end (a hard
            // end — cancel, capability or focus loss — on a live pointer
            // translation, hard_end_touch_stream, reaching here with the
            // button unheld) therefore no longer un-commits a flag press as
            // it did under the one-day lift model — the recorded cost of press-time acting,
            // with undo as the mitigation, exactly the 2026-07-29 accepted
            // answer for this shape.
            app.pending_marker_press = PendingMarkerPress{};
            return;
        }
        // THE GRAB GATE (grab_moved_threshold_px, app_state.h, 2026-09-29):
        // a flag is a thing the press grabs, so it rests at twice the drag
        // gate before it moves — and this one crossing is where BOTH of its
        // drags fork, the value drag and the horizontal reposition below.
        if (std::max(std::abs(mouse_x - app.pending_marker_press.press_x),
                     std::abs(mouse_y - app.pending_marker_press.press_y)) <
                grab_moved_threshold_px()) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // THE CROSSING SPENDS THE ARM into the drag. Read, disarm, then act —
        // the release bodies' standing shape. NO CLICK ACT RUNS HERE: it ran
        // at the press, so the stop, the select and the land
        // all already stand — the select is what paints the dragged flag
        // BRIGHTENED, and the stop is why no live playback needs handling
        // below (nothing can restart playback under the held button: the
        // drag-modal gate swallows every chord while this pending stands).
        const PendingMarkerPress press = app.pending_marker_press;
        app.pending_marker_press = PendingMarkerPress{};
        // THE DRAG'S GATES LIVE HERE, not at the arm: they guard the DRAG
        // (marker motion is authoring), never the click, so a LOCKED tab and
        // an off-home column still selected and landed at the press and simply
        // refuse to move anything. THE LOCK IS BOTH LOCKS since 2026-09-10
        // (authoring_locked, app_state.h): a drag writes a position and pushes
        // an entry, so grid iterations refuses it exactly as the read-only bit
        // does, and silently for the same reason. THE HOME-VIEW BINDING'S ONE
        // OFF-HOME FLAG DRAG — a WARP flag in target view (the tempo drag that
        // once armed there is deleted, see marker_drag.h; the P column drags
        // wherever it stands, which is T+P alone since 2026-09-21) — is the
        // posture fork's below.
        // THE VALUE DRAG FORKS AHEAD OF THE LOCK (architect 2026-09-10): wherever
        // the view makes it so — target view on the warp column, and the phase
        // column under grid iterations (value_drag_posture, app_state.h, where
        // the whole rule is stated, 2026-09-13) — the flag's plain drag is the
        // VERTICAL one, so the crossing begins that gesture or NONE — "we
        // never allow multi-axis dragging; flags move up and down or not at
        // all". The return is unconditional for that reason: a flag the value
        // drag cannot act on does not fall back to the horizontal move under
        // the posture, it simply does not drag. (Nothing is lost by it: T+W is
        // where the home-view binding refuses the horizontal warp drag, and
        // the lit lamp's lock refuses the horizontal phase-reset drag.) The
        // two AUTHORING GATES are not skipped, they are ASKED INSIDE the
        // target rule instead — value_drag_target reads
        // authoring_locked for the payload and the tab's read-only bit for a
        // bound cell, each cell answering for what it authors — and the
        // home-view gate has nothing to say here, this gesture moving no
        // marker at all. Silent either way, as this whole surface is.
        if (value_drag_posture(app)) {
            // THE TRAVEL COUNTS FROM THE CROSSING, NOT THE PRESS (architect
            // 2026-09-29): the origin handed to begin is THIS EVENT'S y, so
            // the grab gate's own travel is not spent on steps and the first
            // step lands one full kValueDragPxPerStep (scaled) past the
            // crossing — where a press-measured origin, the gate being twice
            // a step, would have landed two steps at once. The begin is still
            // followed by this event's own apply, the marker drag's
            // fall-through said as a call, and that apply now lands on step
            // zero and writes nothing: it is kept so the one motion body owns
            // every event from the crossing on.
            if (value_drag.begin(press.marker, press.cell, mouse_y))
                value_drag.apply_motion(mouse_y);
            return;
        }
        // No home-view test: T+W is the posture's, so S+W and T+P with grid
        // iterations dark (T+P the phase-reset column's only view since
        // 2026-09-21) reach here, both of them authoring views.
        if (authoring_locked(app)) return;
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
        // No live playback to handle: the arming press ran the click act's
        // stop, and nothing can have restarted playback since (the drag-modal
        // gate).
    }
    // THE VALUE DRAG'S LIVE ARM (2026-09-10), ahead of the marker drag's own
    // and in its shape exactly: a lost button ends the gesture through its
    // commit (every end of a pointer gesture commits what stands), and a held
    // one applies the motion. The two are mutually exclusive by construction —
    // the crossing above begins one or the other — so this arm's rank costs
    // the marker drag nothing.
    if (app.value_drag.active) {
        if (!mods.primary_button_held) {
            value_drag.commit();
            return;
        }
        value_drag.apply_motion(mouse_y);
        return;
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
        // (row 5), so the no-gesture tail has nothing else to resolve. With no
        // menu up a hover opens nothing (a cold anchor answers a click alone,
        // input_handler.h's menu-row block).
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
    // The delta handed to apply_drag_motion is an ACTIVE-domain frame delta:
    // mouse_frame is the pointer's plain active-domain position — one
    // expression, both views, no inverse map anywhere in its derivation.
    // The displayed-map hops that carry the delta into the source domain
    // live inside apply_drag_motion, which anchors the proposal in the
    // DISPLAYED target domain so the painted flag tracks the pointer 1:1.
    // The position is taken on the ITEM viewport basis, begin_drag's anchor's
    // own (architect 2026-09-24, strictly as painted — the rationale at that
    // anchor, marker_drag.cpp), so the delta is pure pointer travel on the
    // painted grid.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    const double mouse_frame =
        basis.vp_start + static_cast<double>(mouse_x - area.x) * basis.spp;
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
