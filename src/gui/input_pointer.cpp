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

// F2.1: the pointer road's drag-to-select for the text editors. The selection
// highlight is already painted from the editor State's selection_anchor /
// cursor_pos, so the whole gesture is input-side: a press sets the anchor
// and arms the drag, motion moves cursor_pos (extending the highlight),
// release finalizes.
//
// ONE CLICK-TO-BYTE MAPPING (row 7, 2026-08-01). Every editor in the product is
// PROPORTIONAL now, so there is no advance to divide by anywhere: each takes an
// origin plus the shaped run's per-byte boundaries from ITS OWN painter's
// publication — the flag editor's FlagEditorBox, the two dialog
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
// the same ids. NO ROSTER ENTRY IS ABSENT since 2026-10-21: row 1's FILE,
// EDIT and SETTINGS anchors, whose action was a POPUP TOGGLE and not a chord,
// went with the menu row, and the popups with them; the static_assert below
// this table makes "the table's length IS the roster" a build-time fact.
//
// The `ctrl` column likewise is each button's OWN chord (Save's Ctrl+S,
// PASTE's Ctrl+P and the rest): the pointer carries no ctrl into a chord
// since 2026-11-08 (the barrel is no chrome modifier, architect session 94;
// redesign_button_shift_admits' head). The `shift` column is each button's
// OWN chord — TWO rows set it: Redo's
// Ctrl+Shift+Z and, since 2026-09-29, PREVIOUS MARKER's Shift+Tab, which set
// it from 2026-08-15 until the walk pair merged into one button on
// 2026-09-22 and set it again when the dedicated button came back (the walk's
// own shift stays an ADMISSION beside it); Ctrl+Shift+Tab (walk both tabs)
// was a third until its button was deleted on 2026-09-14, the chord being
// Switch Tab's admitted shift now. It is not
// the whole shift story: the SHIFT-ADMITTING buttons OR the long press's shift
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
    // redesign_button_shift_admits, app_state.h, the hold's shift the one
    // admission, because the TOOLTIP's
    // MODIFIER LINE must appear exactly where a modified press does something —
    // a static_assert beside that table enforces it. One fact, two readers.)
    //
    // (NO RADIO COLUMN since 2026-10-23 night: a lift on a view pair's LIT
    // member was a consumed nothing from 2026-10-07 evening, and the
    // architect reversed it — "pushing a button in, I would expect always to
    // cause it to pop out … building a suction on the other one is the right
    // way to go". The lift dispatches the member's own bare digit like every
    // roster button, and the selector's arm in GuiInputHandler::on_key
    // resolves a press on the standing value to its axis's other value, so
    // the button and its key are one rule with no flag here. A hold on the
    // lit member keeps it lit, its accent face darkened by the press
    // ratio (materials.h's key_look, LitPress).)
    // REPEATS: a held press on this button synthesizes its own chord over and
    // over — the hold-repeat.
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
    // repeating button therefore has no shifted act at all (architect
    // 2026-10-30), and a step whose unit needs to be reachable on glass must
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
    // repeat-identity rule the platform's key repeat has, and a fired burst
    // suppresses the lift's own act. Defaulted, so the rows that do not repeat
    // need no sixth column. SIX ROWS CARRY IT since 2026-09-23, re-counted
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
// in the redesign — the icon row and the bottom row — in one
// table. The flags above
// are the
// only axes the rows differ on, so they share one press body and one release
// body (arm_redesign_press / finish_chrome_press_release) instead of
// accumulating a special case per row.
//
// EVERY BUTTON ON THE BAND AND ROW 8 IS HERE: the menu row and its anchors
// went 2026-10-21 and carried no chord row, and quit has one road, BACK (no
// chord spells it, architect 2026-10-30).
constexpr ToolbarChord kToolbarChords[] = {
    // The toolbar four — icon-row members since the 2026-08-12 relayout
    // dissolved row 2 (the chords, gates and flags are UNCHANGED by the move;
    // only the face and the band changed hands).
    //
    // UNDO AND REDO REPEAT (architect 2026-09-13): a held button walks the
    // history at the platform's repeat cadence, the arm asking repeat_eligible
    // about the row's own ctrl (and Redo's shift) columns. Neither admits a
    // modifier, so the long-press exclusion at the lift costs them nothing.
    // A burst that runs out of history meets the GREYED face on its next fire
    // (history_step_actionable's empty-stack term, through
    // redesign_button_enabled) and rests there under the held pointer, the
    // retired magnification pair's ladder end in the same shape; the same pause holds
    // for the other tab's lock. A
    // repeat's own wall is silent at the dispatch arm (input_handler.cpp).
    // OPEN PROJECT (architect 2026-10-07), the row's first button, a group
    // of its own: Ctrl+O, the project picker (the retired File menu's row's
    // chord until 2026-10-21).
    // It admits SHIFT — Revert, the one translated twin (the lift's chord
    // build below) — and nothing else; a one-shot act, no repeat.
    {RedesignButton::OpenProject, GuiKeys::O,  true,  false, false},   // Ctrl+O (+Shift: Ctrl+Alt+O)
    // SAVE admits SHIFT since 2026-10-11 (architect): its long press is
    // Ctrl+Shift+S, the publish chord (run_publish_chord) —
    // the row's own key under both modifiers, no translation.
    {RedesignButton::Save,       GuiKeys::S,   true,  false, false},   // Ctrl+S (+Shift, and in `h`: Ctrl+Shift+S, save_button_publishes)
    {RedesignButton::Undo,       GuiKeys::Z,   true,  false, false,  true},   // Ctrl+Z
    {RedesignButton::Redo,       GuiKeys::Z,   true,  true,  false,  true},   // Ctrl+Shift+Z
    // (COPY PHASE RESETS' row stood here 2026-10-25 to 2026-11-07, its key
    // between Redo and Render; it is row 8's marker verbs' tail since, its
    // row there below.)
    {RedesignButton::Render,     GuiKeys::R,   true,  false, true},   // Ctrl+Alt+R (+Shift)
    // (THE EDIT FLAG BUTTON — bare Return on a button, the flag editor's third
    // road — stood in the bottom row's verb group from 2026-08-27 and in the
    // icon row for the evening of 2026-09-29, when the architect DELETED it:
    // tapping a flag and then moving the pen to a button is counterintuitive
    // when a double tap on the flag does it, and the double-click is safe since
    // the first click arms and waits and the grab gate doubled. The editor's two
    // roads, the Return key and the double-click on the flag, are unchanged.)
    // Row 4 — the icon row. (The view axes' buttons are the two radio pairs
    // below, architect 2026-10-14, each member its own absolute chord; the
    // bare `t` / `p` toggles are gone, and the four selectors are the axes'
    // only road.)
    // (THE SHOW TRIM REGION BUTTON'S ROW IS DELETED — 2026-09-22, with its
    // button and its bare `[` chord: the architect uses the tablet's pen,
    // which reaches the trim bar, so a toggle for the waveform overlay at rest
    // had nothing left to show. Its shift admission — Shift+[ the maximizer — went with the button;
    // Reset Trim is Shift+T since 2026-11-08, the PRESETS key's hold (Zoom to
    // Fit's presses until then). The trim
    // scissors' row
    // before it was deleted on 2026-08-18.) THIS TABLE DOES NOT DECIDE
    // PAINTED ORDER — every one of its readers matches by id or by published
    // rect, never by position — but it is kept in the row's order so nobody
    // reads a mismatch here as the layout's truth; that truth is the
    // painter's kIconRowButtons, with each group's leader at
    // redesign_button_opens_icon_group (app_state.h).
    // ZOOM IN AND ZOOM OUT (architect 2026-10-27, the pair's return in
    // front of Zoom to Fit): bare `=` and bare `-`, the zoom step one level
    // about the viewport's centre (Viewport::zoom_step), two momentary
    // navigation chords. NEITHER ADMITS A MODIFIER since 2026-11-08
    // (architect, session 93: the hold's two-level Shift+= / Shift+- step
    // retired), and NEITHER REPEATS. Both stay LIVE in the `h` view and under
    // both locks — the chords are on all three allowlists (is_zoom_step_key)
    // — so the derived partition answers live with nothing hand-listed.
    {RedesignButton::IconZoomIn,
     GuiKeys::Equal,  false, false, false},                             // bare =
    {RedesignButton::IconZoomOut,
     GuiKeys::Minus,  false, false, false},                             // bare -
    // ZOOM TO FIT (2026-08-12, the grand relayout, as Full Zoom Out; renamed
    // 2026-10-24): a momentary
    // navigation chord, the command's pointer home, click face like
    // the rest of the row. It stays LIVE in the `h` view — `0` and Shift+0
    // are on the mode's allowlist — which the derived partition answers with nothing
    // hand-listed. (CENTER, bare `c`, stood beside it as the zoom pair until
    // 2026-09-29, when its row moved to the bottom row's, below.)
    // IT ADMITS SHIFT since 2026-10-24 (redesign_button_shift_admits; Zoom
    // to Fit since that day): its long press dispatches
    // Shift+0, FRAME THE TRIM (the trim bar's double-click). (RESET TRIM is
    // the PRESETS row's Shift+T, below.) It does not repeat: the `repeats` column is unset.
    {RedesignButton::IconZoomFitBest,  GuiKeys::Digit0, false, false, false}, // bare 0 (+Shift: Shift+0)
    // WAVEFORM MAGNIFICATION (architect 2026-09-22) — the bare
    // backtick since 2026-09-23 (bare `]` for its first hours, then bare
    // `[`), a TOGGLE with
    // a lamp, right after Zoom to Fit. Live in both audio views, on a locked tab and
    // under the read-only lock (a display posture on that allowlist), and
    // DEAD in the `h` view, whose allowlist does not name it.
    {RedesignButton::IconWaveformMagnification,
     GuiKeys::Grave, false, false, false},                            // bare `
    // (THE WAVEFORM MAGNIFICATION PAIR'S ROWS ARE DELETED — 2026-09-14, with
    // their buttons and the setting they stepped, architect approval
    // 2026-09-14: the picture's gain varies over source time, the continuous
    // curve derived from the source since 2026-09-23.)
    // FOLLOW (architect 2026-09-23) — bare `f`, a TOGGLE with a lamp
    // (AppState::follow), behind the magnification lamp. Live on a locked tab
    // and under the grid-iterations lock (navigation), DEAD in the `h` view,
    // whose allowlist does not name it.
    {RedesignButton::IconFollow, GuiKeys::F,   false, false, false},   // bare f
    // RESTRICT (architect 2026-10-29) — bare `r`, a TOGGLE with a lamp
    // (AppState::restrict), behind Follow, its radio partner. Live on a
    // locked tab and under the grid-iterations lock (navigation), DEAD in
    // the `h` view, whose allowlist does not name it. No modifier, no repeat.
    {RedesignButton::IconRestrict, GuiKeys::R, false, false, false},   // bare r
    // THE TRIM ARM LAMP (architect 2026-11-07, session 87) — bare `w`, a
    // TOGGLE with a lamp (AppState::trim_armed), behind Restrict: lit, the
    // next single-contact drag on the waveform is the trim sweep. Live on a
    // locked tab and under the grid-iterations lock (trim is band,
    // read_only_key_blocked), DEAD and hidden in the `h` view, whose
    // allowlist does not name it. No modifier, no repeat, no long-press act.
    {RedesignButton::IconTrimArm, GuiKeys::W, false, false, false},    // bare w
    // THE PRESETS KEY (architect 2026-10-28 night; PRESETS since 2026-11-07),
    // the zoom group's tail behind the TRIM lamp: bare `t`, the trim
    // presets list's opener through on_key
    // like every row, so the `h` view refuses the lift as it refuses the key
    // (the face hidden there by the derived partition) while a locked tab
    // and grid iterations admit both (read_only_key_blocked, trim being
    // band). Under the standing list the key is lit and its lift is the
    // picker router's close (route_picker_key). No repeat. IT ADMITS SHIFT
    // since 2026-11-08 (architect, session 93): its long press dispatches
    // SHIFT+T, RESET TRIM (handle_trim_maximize) — under the standing list
    // too, where the router runs the reset and the list stays.
    {RedesignButton::IconTrimPresets, GuiKeys::T, false, false, false}, // bare t (+Shift: Shift+T)
    // (THE COPY AND PASTE ROWS ARE DELETED — 2026-08-20, with their buttons:
    // the architect's propagate relocation gave the propagate commands the
    // new EDIT MENU as their one pointer home, so Ctrl+P and Ctrl+Alt+P reach
    // the pointer as MENU ITEMS now. Their chords did not die with the rows —
    // a menu item dispatches through on_key exactly as a button's chord does,
    // which is why the relocation needed no second body anywhere. The shift
    // admission IconPaste carried for Ctrl+Alt+Shift+P went too; the trim
    // scissors' deletion above is the same shape. SINCE 2026-10-21 the Edit
    // menu is gone with the menu row; the pair was CTRL+RETURN /
    // CTRL+SHIFT+RETURN, the Open Text Editor row's ctrl and ctrl+shift
    // presses, from that night (it rode Jump to Defining Marker's seat in the
    // phase-reset view for that evening's first landing), and since
    // 2026-10-24 the family is the letter P — since 2026-11-08 the COPY and
    // PASTE rows below, bare P, and Ctrl+P with the state paste Ctrl+Shift+P
    // on its hold.)
    // (THE BPM AND ITERATION ROWS SPENT 2026-08-27 TO 2026-09-04 OUT OF THIS
    // TABLE, with their buttons, and it was the propagate relocation's shape
    // exactly: the Series relocation gave bare `m` and bare `i` a MENU ROW
    // each. Neither chord died with its row — a menu item dispatches through
    // on_key exactly as a button's chord does — and neither carried a modifier
    // admission to move, which is why both rows come back below unchanged when
    // the architect deleted that menu. They are kept in the ROW'S OWN ORDER,
    // which puts them past the viewport group rather than here.)
    // THE ITERATION GROUP (architect 2026-09-04) — two rows back from the menu
    // row, and their 2026-08-27 rows verbatim: the architect deleted the
    // Iterations dropdown once the icon row had room again, so both commands
    // are BUTTONS once more and this table is their chord half. BPM'S KEY IS
    // CTRL+B (architect 2026-09-15, moved off bare `m`): a Ctrl chord guards
    // against a stray bare press, as Ctrl+D and Ctrl+N do (the arm is at
    // handle_mode_keys, input_key_dispatch.cpp); the button is its chord.
    //
    // NEITHER ADMITS A FURTHER MODIFIER and neither repeats: the BPM opener
    // raises a modal editor, which a repeat could only raise onto itself, and
    // the mode toggle is one bit. Both are TOGGLES-or-acts rather than radios
    // — the mode flag presses through in both directions — and both are
    // consumed by the `h` view, which greys them through the derived
    // partition below with nothing hand-listed, the answer the ITERATIONS
    // ANCHOR needed a hand-named arm for while it was a menu.
    {RedesignButton::IconBpm,  GuiKeys::B,   true,  false, false},     // Ctrl+B
    {RedesignButton::IconIter, GuiKeys::I,   false, false, false},     // bare i
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
    // through on_key at the LIFT like every other chrome button. The ACT is on_key's own
    // arm and there is no second body, so both locks, the W-column rule, the
    // nothing-to-flatten refusal and the card are all inherited whole — the
    // move between lanes touched none of them, this table being read by id
    // and never by position.
    //
    // AND IT ADMITS SHIFT, alone in this group: its twin is CTRL+SHIFT+F,
    // which COLLAPSES those
    // terms into their one sum instead of removing them — so a LONG PRESS at
    // chrome_shift_hold_ms() reaches the second act. The row's own `shift`
    // bit stays FALSE, the admission and the
    // table bit being mutually exclusive by the shift term's construction
    // (finish_chrome_press_release); the membership is
    // redesign_button_shift_admits (app_state.h) and the tooltip's second
    // line is bound to it by that predicate's static_assert.
    //
    // NOT A RADIO AND NO LAMP (an act, not a mode), and it does NOT repeat: a
    // flatten repeats onto itself, so the chords are one-shot at
    // repeat_eligible and this row carries no `repeats`.
    {RedesignButton::IconFlatten, GuiKeys::F,   true,  false, false},  // Ctrl+F
    // THE RENDER-ENTRY GROUP (architect 2026-08-14): listen, load in place, the
    // read-only toggle, the history opener. IT IS LISTEN AND THE PADLOCK since
    // 2026-09-01, the LOAD IN PLACE having moved to the history group below
    // (its row moved with it, the table being read by id and never by
    // position). Bare `l` is the player's one BUTTON now; bare `'` outside the
    // `h` view still opens the same player from the keyboard, which is why
    // listen stays lit in every state this row is live in but a running
    // render, which the open refuses (redesign_button_enabled).
    {RedesignButton::IconListen, GuiKeys::L,   false, false, false},   // bare l
    // THE READ-ONLY TOGGLE (2026-08-14, the padlock's move off the tabs): bare
    // `o` toggles the ACTIVE tab's read-only bit. A TOGGLE like
    // iteration — its selected face reads the live bit its own chord flips —
    // and NOT a radio: it presses through in both directions. The button is
    // its chord with no exception now, which is exactly why the padlock moved
    // here (the roster record is at RedesignButton::IconReadOnly).
    {RedesignButton::IconReadOnly, GuiKeys::O, false, false, false},   // bare o
    // THE SCORE (architect 2026-10-26), between the padlock and Settings
    // (architect 2026-11-10): bare `m`,
    // the score viewer's opener through on_key like every row, so the `h`
    // view refuses the lift as it refuses the key (the face hidden there by
    // the derived partition, Settings' own answer) while a locked tab and
    // grid iterations admit both (read_only_key_blocked). Under the standing
    // viewer the key is lit and its lift is the viewer's own router's close
    // (route_score_viewer_key). No modifier, no repeat.
    {RedesignButton::IconScore, GuiKeys::M, false, false, false},      // bare m
    // SETTINGS (architect 2026-09-29), between the Score key and the
    // tooltip lamp (architect 2026-11-10; ahead of the lamp since
    // 2026-10-01): bare
    // `;`, the settings page (2026-10-28), through on_key like every row, so
    // the `h` view refuses the lift exactly as it refuses the key, the face
    // greying there first (redesign_button_enabled); a locked tab and grid
    // iterations admit both since 2026-10-21 (the lock governs the keys, not
    // the editor — read_only_key_blocked). No repeat, NO MODIFIER (architect
    // 2026-10-28: "plain press only. retire long press"). The key sits lit
    // while the page or the value list it swaps in stands, its lift the
    // close (redesign_button_selected, route_picker_key).
    {RedesignButton::IconSettings, GuiKeys::Semicolon, false, false, false}, // bare ;
    // ENABLE TOOLTIPS (architect 2026-09-29), behind Settings since
    // 2026-10-01: the bare BACKSLASH, a TOGGLE with a lamp (AppState::
    // show_tooltips) and NOT a radio — it presses through both ways. Live on a
    // locked tab, under the grid-iterations lock and in the `h` view (all
    // three allowlists admit it, so the derived partition leaves it lit).
    // No modifier, no repeat.
    {RedesignButton::IconTooltips, GuiKeys::Backslash, false, false, false}, // bare backslash
    // (THE LEVEL CASE, IconNoiseLevel, behind the tooltip lamp, the third
    // run's last since 2026-11-05, HAS NO ROW: a slider, pressed by its own
    // arm, arm_level_case_press, not by a chord — the one roster member off
    // this table, which the static_assert below counts out; the AUDIO case
    // beside it, no row either, retired 2026-11-11. THE NOISE lamp's row stood here
    // 2026-10-28 to 2026-11-05; its letter is the TRIM arm lamp's since
    // 2026-11-07, above.)
    // THE HISTORY MODE (2026-08-04): bare `h`, a TOGGLE like
    // iteration — its chord opens the mode and closes it, and the button
    // dispatches on both edges because the icon row's band claim sits ABOVE the
    // mode's pointer gate (the rows' presses are covered by the KEYBOARD gate
    // instead, which admits `h` through handle_history_mode_key one line before
    // the allowlist). It stands right-anchored, the view pairs' run's first
    // case, since 2026-10-06 (kIconRowHistoryOpener, paint_handler.cpp).
    {RedesignButton::IconHistory, GuiKeys::H, false, false, false},     // bare h
    // THE WALK LAMP (architect 2026-08-18; reinstated 2026-10-26 as it stood
    // at its removal) — which walk the `h` view's lane reads: the committed
    // checkpoint history or this session's own undo/redo timeline. BARE `g`,
    // a TOGGLE over the two walks, the button flipping the same axis through
    // the same switch owner (set_history_delta).
    {RedesignButton::SessionHistory,
     GuiKeys::G,      false, false, false},                             // bare g
    // THE HISTORY COMPANIONS — the history group behind the opener since
    // 2026-08-18, standing only in the `h` view since 2026-10-05 (the
    // stand-ins, kIconRowHistoryStandIns and kTransportHistoryStandIns,
    // paint_handler.cpp).
    //
    // THE CUMULATIVE READING'S TOGGLE (2026-08-08; reinstated architect
    // 2026-10-26): bare `u` flips the history view's delta between ITERATIVE
    // (off) and CUMULATIVE (on). A TOGGLE like the history button — the
    // selected face reads the live bit its own chord flips. Its key, like the
    // walk lamp's and the three entries' below, is bound ONLY inside the
    // view; WHAT KEEPS THEM FROM DISPATCHING OUTSIDE ONE IS THEIR ENABLED BIT
    // (2026-08-05, again since 2026-08-18): outside the view they do not
    // stand, publishing empty rects, and a press is consumed at
    // arm_redesign_press's disabled line (the arm and its reasoning are at
    // redesign_button_enabled).
    {RedesignButton::HistoryCumulative,
     GuiKeys::U,      false, false, false},                             // bare u
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
    // OUTSIDE the view the button is dead like its
    // neighbours (2026-08-18) and there is no click to consume at all.
    {RedesignButton::HistoryRevert,
     GuiKeys::V,      false, false, false},                             // bare v
    // THE WALK'S TWO STEPS (2026-08-05): bare `,` steps OLDER and bare `.`
    // NEWER, through the same dispatch and therefore through
    // handle_history_mode_key's own arm — walls clamped as consumed no-ops
    // there, exactly as the keys behave. Neither is a radio and neither is a
    // toggle: they are momentary steps, so both flags read like copy's and
    // paste's, and only the CLICK face is set. Outside the view they never
    // dispatch at all — the dead face's consumed press, per the Cumulative
    // entry's note above; and even reached, bare `,` and `.` are bound in
    // handle_history_mode_key alone, so there is nothing for them to fire.
    //
    // NO HOLD-REPEAT, THOUGH THEY STAND IN THE ARROWS' SEATS (architect
    // 2026-10-11): in the `h` view the two take the Left / Right arrows' places
    // on row 8 (kTransportHistoryStandIns) but not the arrows' `repeats`
    // column — these rows set none. They are shift-admitting, and a
    // non-repeating button's long press IS its shifted press, the jump to the
    // walk's end; a hold-repeat would collide with that long press (the
    // standing principle above: a held repeat outranks the long-press shift).
    // A deliberate asymmetry with the arrows.
    {RedesignButton::HistoryOlder,
     GuiKeys::Comma,  false, false, false},                             // bare ,
    {RedesignButton::HistoryNewer,
     GuiKeys::Period, false, false, false},                             // bare .
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
     GuiKeys::Apostrophe, false, false, false},  // bare '
    // THE VIEW PAIRS, the icon row's last, flush at its right edge (architect
    // 2026-10-14): Source and Target on bare 1 / 2 (the audio view, the
    // markers view untouched), Warp and Phase Reset on bare 3 / 4 (the markers
    // view, the audio view untouched), in the painter's order
    // (kIconRowViewPairs, paint_handler.cpp). Everything
    // the selectors own arrives by construction through on_key's own handler —
    // each key's one axis, the refused target entry, the coincidence
    // auto-select, the read-only admission (they are navigation), the modal
    // swallow, and THE POP (2026-10-23 night): a press on the lit member
    // selects its pair's other member, the key's own resolution. There is no
    // second route to keep in step. The row's click face, no flag.
    {RedesignButton::ViewSource, GuiKeys::Digit1, false, false, false}, // bare 1
    {RedesignButton::ViewTarget, GuiKeys::Digit2, false, false, false}, // bare 2
    {RedesignButton::ViewWarp,   GuiKeys::Digit3, false, false, false}, // bare 3
    {RedesignButton::ViewPhase,  GuiKeys::Digit4, false, false, false}, // bare 4
    // The BOTTOM ROW (the transport half architect-ratified 2026-08-11 as the
    // touch arc's first surface; the marker-walk group added 2026-08-15, the
    // four SINGLE-MARKER VERBS moved down from the icon row 2026-08-18, and
    // ADD TO SELECTION landed behind them later that day).
    // TWENTY-ONE
    // ordinary rows (re-counted 2026-11-08, PASTE in; Older / Newer, the
    // history stand-ins, stand apart; the succession is in git history),
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
    // wrong half to press, which is why this row carries no lamp.
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
    // wrong half. (The generic radio consume the view pairs kept went with
    // their pop, 2026-10-23 night, the column with it.)
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
    // THE CADENCE IS THE PLATFORM'S KEY REPEAT, not a constant of this
    // table's: the first fire one kHoldBeatMs after the press (the product's
    // FIXED beat — never the hold delay, a repeat delay being a cadence and
    // not a hold — so the button hold and the on-screen keyboard's held key
    // cross their threshold on the same beat) and
    // every later fire at THE PLATFORM'S key-repeat interval
    // (key_repeat_period_ms), read per fire — a repeat rate of 0 would stop
    // the button repeat too. The burst rides the armed ChromePress and its whole edge
    // inventory is at that struct (app_state.h); the firing body is
    // tick_chrome_press_repeat below. The arm SHARES repeat_eligible, the
    // platform's own arming probe, rather than mirroring it.
    // The rows below carry the PLAIN chord, as every row does; the
    // admission and its modifier are the roster's, never a column here.
    {RedesignButton::TransportPlayStop,
     GuiKeys::Space,  false, false, false},                             // bare Space
    // GO TO START / GO TO END (architect 2026-10-24, back on row 8 as their
    // own run at its left after their removal of 2026-10-12): bare Home /
    // End, the trim bounds' jump (run_playhead_end_jump), the press
    // dispatching the key at the lift. THEY ADMIT SHIFT
    // (redesign_button_shift_admits): the long press
    // dispatches SHIFT+HOME / SHIFT+END, the whole piece's 0 and EOF — the
    // glass's road to them — and a ctrl press is refused at the band gate
    // (Ctrl+Home / Ctrl+End bind nothing since that day). No lamp, no repeat
    // (a jump has one destination, so the long press is free to be the
    // shift). Both locks admit both forms, and in the `h` view the mode owns
    // both (history_mode_owns_key), so the buttons stand there.
    {RedesignButton::TransportSkipBack,
     GuiKeys::Home,   false, false, false},                             // Home (+Shift: Shift+Home)
    {RedesignButton::TransportSkipForward,
     GuiKeys::End,    false, false, false},                             // End (+Shift: Shift+End)
    // THE SINGLE-MARKER VERBS (2026-08-12), the right block's first group
    // since the architect moved them down here on 2026-08-18: drop, delete,
    // disable toggle and inherit/collapse (FLATTEN stood among them for the
    // hours of 2026-09-19 and its row is in the icon row's iteration group
    // now) — authoring chords that OWN THEIR REFUSALS AND CARDS (read-only,
    // the home view, an empty selection, the drop's occupied place,
    // architect 2026-11-11), inherited whole through on_key; EACH BUTTON'S
    // PUBLISHED FACE IS ITS OWN ARM in redesign_button_enabled (app_state.h)
    // — the Drop key greying where neither of its forms would act, the
    // occupied place included. NOTHING IN THESE ROWS CHANGED WITH THE LANE:
    // the `h` view consumes every one of these chords outright — none of
    // them is on history_mode_key_blocked's allowlist — and answers their
    // faces dead through the derived partition, and the READ-ONLY lock greys
    // them too (2026-08-15), both the BUTTONS' gates rather than the row's.
    {RedesignButton::IconMarkerDrop,    GuiKeys::S,      false, false, false}, // bare s
    // DELETE AND DISABLE ADMIT SHIFT (architect 2026-10-29,
    // redesign_button_shift_admits): the long press
    // dispatches SHIFT+DELETE and CTRL+SHIFT+D, the label definition's
    // references carried with it. Their faces cannot fork per chord, so the
    // shifted lift with no definition selected reaches its arm's card.
    {RedesignButton::IconMarkerDelete,  GuiKeys::Delete, false, false, false}, // Delete (+Shift: Shift+Delete)
    {RedesignButton::IconMarkerDisable, GuiKeys::D,      true,  false, false}, // Ctrl+D (+Shift: Ctrl+Shift+D)
    {RedesignButton::IconMarkerInherit, GuiKeys::N,      true,  false, false}, // Ctrl+N
    // JUMP TO DEFINING MARKER (architect 2026-09-29), the verb group's FIFTH:
    // CTRL+J, the jump to the marker the focused value came from on the other
    // A/B tab (jump_to_value_source). Its row carries the ctrl as Ctrl+D's and
    // Ctrl+N's do. IT ADMITS SHIFT (architect 2026-10-14,
    // redesign_button_shift_admits), Toggle Inherit's shape: the long press
    // ORs the shift into the row's ctrl and dispatches
    // CTRL+SHIFT+J, COPY RESOLVED VALUE — the focused marker's resolved value
    // to the system clipboard (copy_focused_marker_value), the copy's own
    // refusals and card. A ctrl press is refused at the band gate (the row
    // already carries its ctrl). NOT A RADIO AND NO LAMP (an act), and it does
    // NOT repeat (a jump has one destination, a copy repeats onto itself), so
    // its long press is its shift. ITS GATES ARE NOT THE VERBS': both locks
    // admit both chords (a tab switch, a select, a land, two `c`s and a
    // clipboard write author nothing), so it is lit on a locked tab and under
    // grid iterations wherever either act would run (the twin rule at its
    // enabled arm), while the `h` view consumes both chords and greys it
    // through the derived partition.
    {RedesignButton::IconJumpToDefiningMarker,
     GuiKeys::J,      true,  false, false},                             // Ctrl+J (+Shift: Ctrl+Shift+J)
    // OPEN TEXT EDITOR (architect 2026-10-09: "resurrect the Open Text Editor
    // button. It should go between jump to defining marker and toggle add to
    // selection and the shortcut of course is enter"), the verb group's SIXTH:
    // BARE Return, the key that opens the addressed cell's editor on the
    // focused marker (input_handler.cpp's Return arm) — the on-screen
    // keyboard stands only under an editor, so this button and the double
    // tap on a flag are the opener's roads. THE PRESS
    // DISPATCHES BARE Return THROUGH on_key AT THE LIFT and the act is that
    // arm's exactly, ONE ROAD with no second editor-open body, so every gate
    // the chord meets is the button's by construction: the load, the modal
    // routes, the `h` view's allowlist (which does not name Return, so the
    // derived partition greys it there and the view hides it), the read-only
    // allowlist (which does not name it either — every editor is an
    // authoring surface), the grid-iterations gate (Return admitted on a bound
    // axis alone), the tie's card and flag_editor_open_actionable. NOT A
    // RADIO AND NO LAMP (an act whose editor session is the state), and it
    // does NOT repeat (an opener). IT ADMITS SHIFT since 2026-11-10
    // (redesign_button_shift_admits): the long press ORs the hold's shift
    // into this bare row, SHIFT+Return, THE LABEL PASTE — the clipboard's
    // text onto every selected marker's line, no editor opened (the arm
    // below bare Return's, input_handler.cpp). Its ctrl and ctrl+shift
    // presses, Copy / Paste Phase Resets on Ctrl+Return / Ctrl+Shift+Return
    // from 2026-10-21, went with the family's move to the letter P
    // 2026-10-24 (the COPY / PASTE rows, below).
    {RedesignButton::IconOpenTextEditor,
     GuiKeys::Return, false, false, false},                             // bare Return (+Shift: Shift+Return)
    // ADD TO SELECTION (architect 2026-08-18), the verb group's SEVENTH
    // since 2026-10-09, behind Open Text Editor (the group's count has
    // moved around it with every arrival and departure; the succession is in
    // git history):
    // BARE `k`, which was free — he picked it over `n`
    // (already reading as INHERIT) and over `a` (too easy to hit by accident).
    // It is a MODE toggle, so the button's lamp reads the same bit the chord
    // flips and button-is-its-chord holds literally: the press dispatches bare
    // `k` through on_key at the LIFT like every other chrome button, with no
    // timing code of its own anywhere.
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
     GuiKeys::K,      false, false, false},                             // bare k
    // TOGGLE VALUE DRAG (architect 2026-10-11 ~00:40), the verb group's
    // EIGHTH and last, immediately right of Add to Selection: BARE `[`, which
    // was free, the one-shot lamp that arms the flag's value drag, permanently
    // lit under grid iterations (AppState::value_drag_lamp, value_drag_armed;
    // the rule at value_drag_posture). Add to
    // Selection's shape exactly — the lamp reads the bit the chord flips, the
    // press dispatches the chord at the LIFT. ITS
    // GATES ARE THE CHORD'S BY CONSTRUCTION: the read-only allowlist does not
    // name it (the lock refuses both flag drags, so the lamp would arm
    // nothing), the iteration gate admits it (the bound cells are its
    // targets), and the `h` view's allowlist does not name it, so the derived
    // partition greys it there and the view hides it. The phase reset view
    // greys it unlit outside grid iterations at its enabled arm (2026-10-11
    // ~02:05, value_drag_armed). NOT A RADIO, NO REPEAT
    // (a toggle repeats onto itself), NO MODIFIER ADMITTED.
    {RedesignButton::IconValueDrag,
     GuiKeys::BracketLeft, false, false, false},                        // bare [
    // THE COPY / PASTE DUO (architect 2026-11-08, session 93), row 8's
    // leftmost run (kPhaseResetPropagateGroup, paint_handler.cpp; no reader
    // of this table partitions by lane). COPY (architect 2026-10-24): BARE P,
    // the propagate's copy (handle_mode_keys), ADMITTING NOTHING since
    // 2026-11-08 (Ctrl+P and Ctrl+Shift+P are PASTE's chords). PASTE: CTRL+P,
    // PASTE PHASE RESETS, its row carrying the ctrl (Switch Tab's shape), and
    // IT ADMITS SHIFT (redesign_button_shift_admits): the long press ORs the
    // shift into the row's ctrl and dispatches CTRL+SHIFT+P, PASTE PHASE
    // RESET STATE, through the propagate's own arms with their gates and
    // cards. No lamp, no repeat. THEIR GATES ARE THE CHORDS': the read-only
    // allowlist names none of the three (both faces grey on the lock), the
    // iteration gate admits the copy alone (PASTE greys under the lamp), and
    // the `h` view's allowlist names none, so the derived partition hides
    // both there.
    {RedesignButton::IconCopyPhaseResets,
     GuiKeys::P,      false, false, false},                             // bare P
    {RedesignButton::IconPastePhaseResets,
     GuiKeys::P,      true,  false, false},                             // Ctrl+P (+Shift: Ctrl+Shift+P)
    // THE MARKER-WALK GROUP'S OLD FOUR (architect 2026-08-15; four rows since
    // 2026-09-29): PREVIOUS MARKER, NEXT MARKER (the walk), CENTER and SWITCH
    // TAB, kept together here for the reader though row 8 paints them in two
    // groups since 2026-10-18 (the walk pair behind Down / Up, Switch Tab and
    // Center ahead of Left / Right — kTransportStepWalkGroup /
    // kTransportNudgePlayGroup, paint_handler.cpp; the lookup is by id). It
    // was the walk alone from 2026-09-23 (the two HOLD-COLUMN nudges stood
    // beside it from 2026-09-22 and were deleted with their chords).
    //
    // PREVIOUS MARKER'S ROW CARRIES SHIFT+TAB AS ITS OWN BASE CHORD, Redo's
    // shape: a Shift- or Ctrl-modified chord may have its own dedicated
    // button since 2026-09-29 (architect, "an artificial constraint; the
    // tablet is the only real development surface now" — this button is
    // Next Marker's Shift form, Switch Tab below its Ctrl form). IT ADMITS
    // NOTHING since 2026-11-08 (architect, session 93: the march,
    // Ctrl+Shift+Tab, is Switch Tab's hold alone). It
    // does not repeat, as the walk does not.
    {RedesignButton::TransportWalkPrevious,
     GuiKeys::Tab,    false, true,  false},                             // Shift+Tab
    // The declined double-click
    // rule's mechanical reason is recorded at the roster entry (every
    // double-click surface in this product acts on its FIRST click too).
    // WALK BOTH TABS was a member on Ctrl+Shift+Tab until the architect
    // deleted it on 2026-09-14: the march is Switch Tab's shifted press
    // (below).
    //
    // THE WALK IS ONE ROW (architect 2026-09-22, merging Previous marker and
    // Next marker; Previous Marker's own row above since 2026-09-29): bare
    // Tab, ADMITTING NOTHING since 2026-11-08 (architect, session 93:
    // Shift+Tab is PREV's row, Ctrl+Tab and Ctrl+Shift+Tab SWITCH's row and
    // hold). It does NOT repeat.
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
     GuiKeys::Tab,    false, false, false},                             // bare Tab
    // CENTER (bare `c`), moved here from the icon row's zoom group on
    // 2026-09-29 (architect) with its row unchanged: a momentary navigation
    // chord, no repeat. LIVE in the `h` view, `c` being that mode's
    // own vocabulary. IT ADMITS NO MODIFIER (2026-10-14: no modified `c`
    // binds in the main window), so a modified press is refused at the band
    // gate and the long press reaches nothing.
    {RedesignButton::IconZoomOriginal,
     GuiKeys::C,      false, false, false},                             // bare c
    // SWITCH TAB (architect 2026-09-29): Ctrl+Tab, the one-shot other-tab
    // act, its row carrying the ctrl — but NOT A RADIO:
    // the chord toggles, so every press switches. It ADMITS SHIFT
    // (redesign_button_shift_admits) and nothing else: the long press ORs
    // the shift into the row's ctrl and dispatches
    // Ctrl+Shift+Tab, the paired march — both chords' one pointer road since
    // 2026-11-08. A barrel press on it is the band gate's consumed no-op, as
    // on every key. It does not repeat: its long press is its shift.
    {RedesignButton::TransportSwitchTab,
     GuiKeys::Tab,    true,  false, false},                             // Ctrl+Tab
    // (THE HOLD-COLUMN NUDGES' ROWS — Ctrl+Left and Ctrl+Right, 2026-09-22 —
    // ARE DELETED with their buttons and their chords, architect 2026-09-23:
    // the held column is a posture an explicit centring arms, read by the
    // bare arrows below, AppState::camera_hold.)
    // The arrows, in the architect's order of 2026-08-14 (down, up, left,
    // right, replacing the row's original vim order), painted in two pairs
    // since 2026-10-18 — Down / Up at the step-and-walk group's head, Left /
    // Right in the nudge-and-play group (paint_handler.cpp). The
    // lookup is by id, so this order is for the reader alone. The eighth
    // column is `repeats`, which these four set — as do Undo / Redo since
    // 2026-09-13; the column IS the membership (the contract is at
    // ToolbarChord::repeats).
    //
    // THEIR MODIFIER COLUMNS STAY FALSE and that is the split these four make
    // visible (2026-08-31): the table's `shift` and
    // `ctrl` columns spell a row's OWN base chord (Redo's Ctrl+Shift+Z, the
    // Undo's Ctrl+Z), while an ADMISSION is the hold's shift the lift moves
    // into the dispatched chord. The bare row here is the one-unit step, and
    // THE ARROWS ADMIT NOTHING: their long press is their repeat (architect
    // 2026-10-30), and Ctrl+Up / Ctrl+Down bind nothing since 2026-11-08
    // (architect, session 93).
    // LEFT / RIGHT ADMIT NEITHER since 2026-09-21 (the horizontal ladder
    // retired; their one step is the active column's unit, a hop on P), and
    // Ctrl+Left / Ctrl+Right bind nothing since 2026-09-23 — the camera their
    // step takes is the hold posture's (AppState::camera_hold).
    // Setting a column here instead would make the modified form the
    // button's ONLY act.
    {RedesignButton::TransportDown,
     GuiKeys::Down,   false, false, false, true},                       // bare Down
    {RedesignButton::TransportUp,
     GuiKeys::Up,     false, false, false, true},                       // bare Up
    {RedesignButton::TransportLeft,
     GuiKeys::Left,   false, false, false, true},                       // bare Left
    {RedesignButton::TransportRight,
     GuiKeys::Right,  false, false, false, true},                       // bare Right
};

// THE TABLE IS TOTAL OVER THE ROSTER, ENFORCED AT COMPILE TIME (2026-08-06):
// every RedesignButton carries a chord here since the menu row's three
// anchors left (2026-10-21) — the table's length is kRedesignButtonCount, and
// the static_assert below is the check, so no number is restated here (the
// count's succession is in git history). It is keyed by id and every reader
// matches by id or by published rect, so the row order it is kept in is for
// the reader alone; a button moving BETWEEN ROWS moves no count. The check is not
// bookkeeping — history_mode_disables_button walks this table and DEFAULTS AN UNLISTED BUTTON
// TO LIVE, so a roster entry added without its row here would silently wear a
// live face in the `h` view while its press claimed nothing. This makes that
// drift a build error instead. THE LEVEL CASE IS THE ADDEND (architect
// 2026-11-05): a slider with no chord, pressed by its own arm
// (arm_level_case_press) and answered LIVE in the `h` view by an explicit
// arm of that walk (history_mode_refuses_button_chord), so the table covers
// the roster less exactly that one (redesign_button_is_level_case, the
// count's one owner; two from 2026-11-05 until the AUDIO case's retirement,
// 2026-11-11). (An addend walked the menu anchors' count, + 2 to
// + 5, from 2026-08-13 until they left with the menu row, 2026-10-21.)
constexpr int kLevelCaseCount = [] {
    int n = 0;
    for (int i = 0; i < kRedesignButtonCount; ++i)
        if (redesign_button_is_level_case(static_cast<RedesignButton>(i))) ++n;
    return n;
}();
static_assert(std::size(kToolbarChords) + kLevelCaseCount ==
                  static_cast<std::size_t>(kRedesignButtonCount),
              "kToolbarChords must cover every RedesignButton but the "
              "level case");

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
// roster, so the membership had derived down to SAVE alone;
// the whole mechanism goes rather than surviving for one convenience chord.
// SO THE VEIL IS EXCEPTIONLESS AGAIN: while a dialog editor stands, EVERY
// press outside the modal is consumed, and the roster hovers nothing. THE
// VEIL IS BEHAVIOURAL AND PAINTS NOTHING (architect 2026-08-12): no dim layer
// — the palette is fully opaque and kdenlive's own parent window stays
// undimmed under its dialogs. EVERY
// EDITOR TAKES THE VEIL since 2026-10-25, the lane editors with the dialogs
// (keyboard_modal_editor_active); the on-screen keyboard's contract is
// modal_editor_key_blocked's, Esc and the editor's own keys, NO HATCH.)
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
// — and where the icon row's overflow rule clips a member under the two view
// pairs (and the history opener seated with them), only the columns it
// painted (paint_icon_row). A zero rect (before
// that row's first paint, or a member the overflow hides whole) contains no
// point, which is the correct answer. THE PLAY KEY'S SECOND RECT, ROW 8'S
// CLOCK (architect 2026-10-27), answers here too (RedesignButtonFace's
// `contains`, the field's record), so the clock arms, lifts and passes the
// band's modifier gate as the key does, and the dispatch is the key's.
bool redesign_button_hit(const AppState& app, RedesignButton id, int x, int y) {
    return app.redesign_buttons[redesign_button_index(id)].contains(x, y);
}

// (press_on_live_menu_anchor — the folder overlay's two veils' one
// exemption, the menu row's live File anchor above the band, 2026-09-03 —
// went with the menu row, 2026-10-21.)

// THE FOUR BAND CLAIMS' ONE MODIFIER GATE — asked before the arm so a press
// that spells nothing stays the strict consumed no-op it has always been. One
// body for all four rows, the band-claim shape's own rule rather than a row's.
// THE POINTER'S ONE PRESS-TIME MODIFIER IS THE S PEN BARREL'S CTRL (architect
// 2026-10-30; the long press is Shift at the lift, never a press-time bit),
// AND ON CHROME IT SPELLS NOTHING: A CARRIED CTRL ON A CHROME PRESS IS
// CONSUMED, on every key and on the ground between them (architect
// 2026-11-08, session 94: "barrel taps are not supposed to be supported
// because they're difficult to do and pointless for the effort ... a long
// press will do just fine on the icons, whereas a barrel click is awkward";
// the ctrl and ctrl+shift admissions the gate asked from 2026-09-26 left with
// their last members, the record at redesign_button_shift_admits' head). A
// key whose chord carries a ctrl carries it in its own kToolbarChords row.
// THE SAME RULE HOLDS ON THE ON-SCREEN KEYBOARD (architect 2026-11-08,
// session 94: the barrel has no road on any key), at its own claim, which
// ranks above this gate (claim_onscreen_keyboard_press).
// THE BARREL'S WAVEFORM ROAD IS UNTOUCHED — "the zoom is worth it ... the
// most convenient way to have that, because a long press can't substitute":
// the one-finger ctrl zoom (GuiTouchNavFrame::ctrl), the trim bar's barrel
// bound set and every other waveform barrel rule stay at their owners, none
// of which reads this gate.
bool chrome_band_modifiers_refused(const AppState& app, int x, int y,
                                   GuiInputState mods) {
    (void)app;
    (void)x;
    (void)y;
    return mods.ctrl;
}

// THE WAVEFORM'S HALF SPLIT, and the ONE expression of it — now with exactly
// ONE reader (architect 2026-08-13, THE TWO HALVES BECOME ONE SURFACE): the
// half no longer selects a SURFACE, it selects which act a MOTIONLESS release
// runs on the one navigation surface — upper = the playhead placement, lower =
// the audition scrub. Everything else about the two halves is identical, so
// the half is read once, at the press, and stashed on the pending
// (ScrollDragState::scrub_release).
//
// THE HALVES ARE THE TWO WELLS (architect 2026-10-16, Cool Edit's stereo
// divider): the upper well's rows are the upper half, the lower well's —
// the odd spare row at the canvas's foot among them — the lower half, the
// rows read off waveform_well_rows (render.h). The divider between them is
// chrome and owns nothing: a press there never reaches this split
// (point_on_nav_surface refuses it, point_on_waveform_divider).
bool waveform_lower_half(const GuiRect& area, int y) {
    return y >= waveform_well_rows(area)[1].y0;
}

// THE PLACEMENT LANES block — THE TOP STRIP'S TWO PLACEMENT LANES, the RULER
// and the MARKER lane, each claimed by its band walk's own rect test (a
// point is in the lanes iff it is in either lane's RECT, BOTH
// AXES: a lane's membership is its rect's x bounds as well as its y bounds —
// the window's width since 2026-10-21, when the program frame's inert
// columns beside every program lane went (strip_row_rect) — the canvas
// column's inner margins, inside the rect, keeping their recorded answer in
// on_button_press's ruler band). THEY LEFT THE NAVIGATION SURFACE (architect
// 2026-09-25: "out of both — we want symmetry as much as possible; the
// horizontal zoom ... was designed to unify the motions for zoom on both
// devices"): off a flag, a motionless click or tap there places the playhead
// and the marker lane's empty-stretch double-click creates, and EVERY DRAG
// DOES NOTHING — no pan, no zoom, no sweep — WITH ONE
// EXCEPTION, THE PLAYHEAD'S HEAD (architect 2026-10-09 ~17:40, Cool Edit's
// cursor grabbed by its head): a plain press on the head as painted
// (point_on_playhead_head, below) is the HEAD DRAG, every motion past the
// slop the placement act at the pointer's column (ScrollDragState::head_drag). The
// press's order: a flag first (a cue's triangle or label over the head is
// a flag act, hit_test_flag), then the head, then the lane's placement. The FLAG BOXES
// carve themselves out (the band walks' own flag claims), and the TRIM BAR is a disjoint y-band that never answers
// true here (it has its own claim and its own cue). Deliberately NOT the
// flexible gap band (above main.cpp's lane 2, under the band) — that ground
// is chrome, not surface, and stays inert.

// THE PLAYHEAD'S HEAD AS PAINTED (architect 2026-10-09 ~17:40, the head drag;
// the lanes' one drag, above): the head's painted columns — the triangle and
// its shadow, cut to the waveform's columns — over its device rows of
// the ruler's, NO INVISIBLE HIT BOX beyond them ("I don't think we should
// get in the habit of making invisible hitboxes"), read from the ruler
// pass's own publication (AppState::playhead_head_hit, paint_ruler_row) as
// every flag rect is (ON SCREEN IS AS PAINTED). Its callers ask the flags
// first, so a cue standing over the head keeps the press. TWO READERS: the
// plain press's ruler band walk in on_button_press and in
// handle_history_mode_press (each arming arm_placement_press with the head
// drag).
bool point_on_playhead_head(const AppState& app, int x, int y) {
    return rect_contains(app.playhead_head_hit, x, y);
}

// THE NAVIGATION SURFACE, THE ONE OWNER OF ITS GEOMETRY: THE WHOLE WAVEFORM
// AND NOTHING ELSE — both halves (architect 2026-08-13, THE WAVEFORM'S TWO
// HALVES BECOME ONE), in every view, the `h` history view's surface and the
// live views' being the same rect. The ruler and the marker lane's empty
// stretches were its lane members from 2026-08-12 until 2026-09-25, when they
// became placement surfaces (the PLACEMENT LANES block above). On it a
// motionless click = the half's act; the finger's pan, pinch and hold are the
// touch road's (touch_point_in_pan_zone reads this owner).
//
// The waveform BAND is THE CANVAS'S INTERIOR, waveform_area exactly
// (2026-10-09, the canvas framed): the well's margins either side — the
// ground and the grid floor's leftover
// split between them — are the program's panel, not waveform, and answer nothing, as
// the rows of ground above and below do (the right gutter counted as waveform
// while the waveform spanned the window, 2026-08-13 to 2026-10-09). The TRIM
// BAR, the lanes and the flexible GAP band are outside it.
//
// LESS THE STEREO DIVIDER'S ROWS (architect 2026-10-16, "chrome is not
// waveform, for any surface"): the divider is drawn at the chrome's scale in
// the flat ground (render.h's stereo divider paragraph), so it is chrome, and its rows answer the pen and the
// finger exactly as the margins do: no claim, no pan zone
// (point_on_waveform_divider, render.h, read here for every claim). A gesture
// that began in a well runs on when it crosses them.
//
// AND ONLY WHERE THE WAVEFORM IS PAINTED (2026-10-10, ON SCREEN IS AS
// PAINTED): the extent is the waveform's painted rect,
// onscreen_keyboard::waveform_paint_area — waveform_area less the modal
// family's box (nothing at all while a modal stands, modal_box.h) or the
// keyboard's band (onscreen_keyboard.h's placement block). Those rows answer
// nothing: what lies under a surface is covered. ONE RECT FOR
// THE PIXELS AND THE PRESS, rather than a press rect of its
// own beside the paint rect: the press then cannot answer where the waveform
// is not painted, whatever later stands over it. The COORDINATES stay
// waveform_area's (every column conversion reads it, the half split
// included) — this rect is only where the surface ends.
//
// ITS READERS, re-derived 2026-10-30: the press router's band walk (its
// `inside_waveform`, the waveform arm at the band walk's foot, which arms the
// pending click and picks the release act), the `h` view's own press router,
// and the TOUCH PAN ZONE (touch_point_in_pan_zone, the one-finger pan surface
// by ruling, which must not drift from the press's — so a finger on the lanes
// resolves to the pointer translation, where a tap places and a drag does
// nothing).
bool point_on_nav_surface(const AppState& app, int x, int y) {
    return rect_contains(onscreen_keyboard::waveform_paint_area(app), x, y) &&
           !point_on_waveform_divider(waveform_area(app), y);
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
// worker job in flight at the press (a pan or a zoom gesture, then a quick
// click) has already moved ahead of the pixels. The integer start is the
// grid's own input, the shape trim_mouse_x_to_active_frame takes on the same
// basis. Cold, the basis is the live viewport by its own contract. This is
// the whole click-placement family's one conversion — the nav click act
// (place_playhead_at_click_column, run_nav_click_act), the
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
                                              basis.spp,
                                              basis.lattice_origin)));
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
    // true = the DIALOG editor's box (settings, which also hosts the
    // trim preset's name and the BPM form's field, painting in the
    // modal family's box since 2026-10-14 — in the bottom row's modal from
    // 2026-08-13, centered for the one day from 2026-08-12, and the field
    // was `bottom_strip` while they lived on the status lane); false = the
    // top-strip flag editor. Selects the claim
    // region and the repaint owner.
    bool                dialog       = false;
    // THE DIALOG EDITOR'S WRAPPED ROWS AS PAINTED (DialogEditorText's
    // contract, 2026-10-14), null for the flag editor: a dialog editor's
    // press picks the row under it before the boundary on it
    // (dialog_editor_byte_at), the flag editor's one row reads `text_left`.
    const AppState::DialogEditorText* wrap = nullptr;
    // THE FIELD'S ONE RECT — where a press seats a caret: the dialog editor's
    // edit control, the modal family's box's interior (modal_dialog.field,
    // the painter's published rect; the ground round it is chrome) or the
    // marker lane's whole published box
    // (flag_editor_box.box — the unrolled flag under the payload editor, the
    // cell under a bound editor, pads included: the box is
    // the field, and a click in its padding puts the caret at the nearest
    // end as clicking a text field's margin does). The press claim and the
    // touch translation's editor-field query both read this, so "in the
    // field" has one spelling for the pointer road and the finger.
    GuiRect             field{0, 0, 0, 0};
};

ActiveEditorText active_editor_text(AppState& app, const GuiAudio& audio) {
    (void)audio;
    ActiveEditorText g;
    // THE DIALOG LINE EDITOR's ONE PUBLICATION, which paint_modal_dialog
    // fills from the buffer it actually painted. An invalid publication
    // (nothing painted yet, or an editor the dialog's precedence hides — a
    // prompt is up) leaves this invalid, exactly as the flag editor's does:
    // what is not on screen takes no clicks.
    const AppState::DialogEditorText& be = app.dialog_editor_text;
    const bool dialog_open = text_editor::is_active(app.settings_editor);
    if (dialog_open) {
        if (!be.valid) return g;
        g.ed = &app.settings_editor;
        g.wrap         = &be;
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

// The ONE pointer -> caret stop owner (see ActiveEditorText): a
// nearest-boundary search over the painter's published per-byte pen offsets
// — on the flag editor's one row by x alone, on a dialog editor's wrapped
// line on the row under the pointer first, with the stop's row-end affinity
// at a hard-wrapped row's end (dialog_editor_byte_at and its rule at
// dialog_editor_row_last_stop, app_state.h, 2026-10-14). Every caret and
// drag-select site funnels through it, and since row 7 there is one family
// of editors rather than two, so there is nothing left for them to drift
// apart on.
DialogEditorStop editor_stop_at(const ActiveEditorText& g, int mouse_x,
                                int mouse_y) {
    if (g.wrap != nullptr)
        return dialog_editor_byte_at(*g.wrap, g.ed->pending,
                                     static_cast<double>(mouse_x), mouse_y);
    return {text_editor::byte_index_from_shaped_x(
                static_cast<double>(mouse_x), g.text_left, *g.byte_x),
            false};
}

// THE ONE POINTER SEAT: every press and every drag motion that puts the caret
// under the pointer comes through here — the plain press, the pointer road's
// caret drag (a tap's sub-slop motions) and the finger's caret drag (the
// touch trio's bodies below) — and it COLLAPSES ANY SELECTION (architect
// 2026-11-08, session 95: a tap seats the caret, a drag moves it, the
// selection is the keys' alone), restarts the blink phase as the keyboard's
// own motion keys do, so a caret being dragged is lit where it lands rather
// than possibly caught in the dark half of a period, then sets the stop's
// row-end affinity (a press past a hard-wrapped row's text keeps the caret
// painted at that row's end, 2026-10-14).
void set_editor_caret_from_x(const ActiveEditorText& g, int mouse_x,
                             int mouse_y) {
    const DialogEditorStop s = editor_stop_at(g, mouse_x, mouse_y);
    g.ed->selection_anchor = -1;
    g.ed->cursor_pos = s.byte;
    text_editor::touch_blink(*g.ed);
    g.ed->caret_row_end = s.row_end;
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
// IT IS DERIVED, NOT LISTED. Each roster button IS a chord
// (finish_chrome_press_release synthesizes it and calls on_key at the lift),
// so "the view
// consumes this button's act" is exactly "the view's keyboard gate consumes this
// button's chord" — this walks kToolbarChords and asks that gate. So the faces
// cannot drift from the allowlist: admit a chord there and its button lights on
// the next frame with nothing to remember here.
//
// (THE MENU ANCHORS WERE THE HAND-ANSWERED ENTRIES — the roster's only
// non-chord actions, answered through menu_anchor_live ahead of this walk:
// Settings and Edit dead, File live — until the menu row's removal,
// 2026-10-21. Every roster button is a chord since, and the walk answers
// the whole roster.)
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
// row's Center button (2026-08-12; the bottom row's since 2026-09-29, in the
// nudge-and-play group since 2026-10-18), and bare Tab / Shift+Tab /
// Ctrl+Shift+Tab the bottom row's walk buttons (2026-08-15; Shift+Tab a
// dedicated Previous Marker again since 2026-09-29; Previous / Next Marker in
// the step-and-walk group and Switch Tab, whose shifted press is the march,
// in the nudge-and-play group since 2026-10-18). Every one of them is a chord
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
// view"), so the act's chord is admitted outright (Ctrl+Shift+S since
// 2026-10-10), this walk answers LIVE for Save, and
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
// left the roster for the File menu, and since 2026-10-21 with no hand
// entries at all, the menu row's anchors gone):
//   LIVE — the icon row's view pairs, ViewSource/ViewTarget/ViewWarp/ViewPhase (bare 1/2/3/4, the admitted
//   view selectors), Save (its lift Ctrl+Shift+S in this mode since
//   2026-10-10, Ctrl+S before — save_button_publishes — the
//   save-and-commit checkpoint act, wearing the "Save and Commit" face — LIVE
//   FROM THIS WALK SINCE 2026-09-01, when the chord's two session terms left the
//   allowlist for the act, its own arm in redesign_button_enabled greying it
//   with an empty head delta or a checkpoint in flight exactly as this walk used
//   to, and greyed rather than relabelled in either case; it was RENDER's chord
//   and RENDER's face until 2026-08-08, when the act moved onto the save it
//   begins with),
//   ZOOM IN AND ZOOM OUT (2026-10-27; `=` / `-` and their shifted forms on
//   the allowlist through is_zoom_step_key),
//   THE LEVEL CASE (2026-11-05, in the NOISE lamp's seat; the lamp's
//   bare `w` was on the allowlist 2026-10-28 to then — device state the view
//   has no reason to refuse; the case carries no chord, so the walk below
//   answers it LIVE by an explicit arm),
//   ZOOM TO FIT (Full Zoom Out until 2026-10-24) AND CENTER since the
//   2026-08-12 relayout (bare `0` is the allowlist's own zoom admission,
//   Shift+0's frame beside it, and bare `c` is the mode's vocabulary —
//   pure navigation, live with nothing hand-listed; Center on the bottom
//   row since 2026-09-29, beside Switch Tab since 2026-10-18 — Previous
//   Marker's and Switch Tab's Shift+Tab and Ctrl+Shift+Tab being the mode's
//   own and Ctrl+Tab on its allowlist),
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
//   and THE BOTTOM ROW'S WALK since 2026-08-15 (bare Tab and Shift+Tab, Next
//   and Previous Marker in the step-and-walk group since 2026-10-18) — the
//   mode's own vocabulary once more, so both answer LIVE
//   for free and step the DIFF-FLAG cycle forward and back — WITH THE MARCH
//   SINCE 2026-08-18 (Ctrl+Shift+Tab, the mode's own vocabulary too now:
//   it marches the pair over that same diff-flag cycle, "tab, ctrl+tab, tab"
//   read against the lane in here, a round trip through the other tab since
//   2026-09-26; it ran the reverse WALK-SOURCE cycle from
//   2026-08-07 until the walk moved to the icon row's own radio pair, and was
//   blocked for the hours between) — AND SINCE 2026-09-29 THE WALK'S
//   DEDICATED ROADS: PREVIOUS MARKER (Shift+Tab, the mode's own, its ctrl
//   press the mode's march) and SWITCH TAB (Ctrl+Tab, on the allowlist, its
//   shifted press the mode's march; in the nudge-and-play group since
//   2026-10-18), both LIVE for free,
//   and THE BOTTOM ROW'S SKIPS and THE CENTER button (the
//   walk group's third from 2026-09-29 to 2026-10-18) on the same terms
//   (bare Home / End are the mode's absolute jumps, bare `c` its own centring),
//   and THE BOTTOM ROW'S LEFT / RIGHT CHORDS since 2026-09-26 (bare Left /
//   Right, the mode's own playhead step, admitted; the hide predicate leaves
//   them be, but kTransportHistoryStandIns seats Older / Newer in the arrow
//   BUTTONS' places in this view, the painted pair there),
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
//   (The menu row's anchors were hand-answered here until its removal,
//   2026-10-21; Ctrl+O and Ctrl+Alt+O stay admitted.)
//   DEAD — Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z); RENDER since 2026-08-08
//   (Ctrl+Alt+R, which left the allowlist with its shifted twin when the act
//   moved onto Ctrl+S — so the button wears its ordinary Render face over this
//   partition's dead one, and the walk says so with nothing hand-listed);
//   the BPM
//   opener (Ctrl+B), iteration mode (bare `i`), listen
//   (bare `l`); OPEN TEXT EDITOR (bare Return, admitting no modifier since
//   2026-10-24 — its ctrl presses were the phase-reset copy and paste from
//   2026-10-21 until then); COPY PHASE RESETS (bare P, its ctrl presses
//   Ctrl+P and Ctrl+Shift+P, the paste and the state paste, 2026-10-24 — the
//   propagate family, which had its own two buttons here as Ctrl+P /
//   Ctrl+Alt+P until the 2026-08-20 relocation; none of its three chords on
//   the view's allowlist, so the walk hides it there);
//   the FOUR MARKER VERBS since
//   the 2026-08-12 relayout (bare `s`, Delete, Ctrl+D, Ctrl+N — authoring,
//   consumed like the rest, and unmoved by their 2026-08-18 change of ROW:
//   this walk asks about a chord, never about a lane) — ADD TO SELECTION,
//   the verb group's seventh, is NOT here: bare `k` is on the view's allowlist
//   since 2026-09-17, the lamp producing the view's own multi-selection, so
//   it answers LIVE; and the icon row's SETTINGS BUTTON (bare `;`,
//   since 2026-09-29 — off the mode's allowlist). (The menu row's SETTINGS and EDIT anchors were hand-answered dead
//   here until the row's removal, 2026-10-21.)
//   (THE TRIM SCISSORS were here on their own chord until their button was deleted on
//   2026-08-18.)
//
// TWO THINGS IT DELIBERATELY DOES NOT SAY. (1) The base chord decides the face,
// which since 2026-08-08 has nothing left to arbitrate on row 2: the ONE button
// whose shifted twin the mode consumed while its base chord stood — Render —
// is dead on both shapes now, and Save's shifted press (admitted since
// 2026-10-11) is the very publish chord its plain lift spells in the view, so
// its two shapes are one answer here. (The walk asks Save's chord with the
// shift set, save_button_publishes below.) (2) A
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
// row's slots of the two groups THIS WALK answers DEAD whole — Revert and
// Load in Place in Undo's (Undo, Redo and Render; the copy among them
// 2026-10-25 to 2026-11-07), Cumulative and the walk lamp in the iteration
// group's (BPM, Grid, Flatten; again since 2026-10-26) — and outside it
// they do not stand (kIconRowHistoryStandIns, paint_handler.cpp, which
// names this walk as the membership's derivation). A change here that
// answers one of those seven LIVE in the view is a change to that table
// too.
// ROW 8 HAS ITS OWN SINCE 2026-10-11 (architect): in the view Older / Newer
// stand in the Left / Right arrows' seats (kTransportHistoryStandIns,
// paint_handler.cpp) — a seat ruling, not this walk's: the arrows' chords
// stay live in the view, only their buttons stand down there.
//
// THE ONE CHORD WALK BOTH `h` ANSWERS TAKE (architect 2026-10-07): the grey
// (history_mode_disables_button) asks the allowlist about THIS session, the
// hide (history_mode_hides_button, below) about EVERY session
// (history_mode_key_blocked_in_every_state, input_key_dispatch.cpp, the
// allowlist's same body); the mode's own vocabulary answers LIVE for both.
// THE `h` VIEW'S SAVE BUTTON SPELLS THE PUBLISH CHORD (architect 2026-10-10:
// "in history view the hotkey for save then becomes ctrl s and save and
// commit becomes ctrl shift s. however the icons in history remain the
// same"): while the view stands its lift is Ctrl+Shift+S (is_publish_key,
// gui_input.h) — the save-and-commit act, the pull or the check — and
// Ctrl+S, its table chord, is the plain save there, with no button of its
// own. Outside the view the button is that plain save. Asked by the lift's
// chord build (finish_chrome_press_release) and by the `h` chord walk below,
// so the face and the press read one chord. SAVE ADMITS SHIFT since
// 2026-10-11 (architect: the publish chord as its twin, Open's model): its
// long press spells Ctrl+Shift+S in EITHER view — outside
// the view the twin, inside it the same chord this term already spells, so
// the `h` view's tap and long press run one act. It admits no
// ctrl.
static bool save_button_publishes(RedesignButton id, bool history_view) {
    return id == RedesignButton::Save && history_view;
}

static bool history_mode_refuses_button_chord(const AppState* state,
                                              RedesignButton b) {
    // (THE THREE MENU ANCHORS, which carried no chord for this walk, answered
    // false here until the menu row went, 2026-10-21.)
    // THE LEVEL CASE ANSWERS LIVE OUTRIGHT (architect 2026-11-05, the
    // slider in the NOISE lamp's seat; the lamp's bare `w` was on the
    // view's allowlist 2026-10-28 to 2026-11-05, the planner's call the
    // architect agreed to, Tooltips' class — the letter is the TRIM arm
    // lamp's since 2026-11-07 and on no allowlist of the view): DEVICE
    // STATE the view has no reason to
    // refuse — the level touches nothing the view froze — and the case
    // carries no chord for the allowlist to walk, so the answer is stated here
    // rather than defaulted below: never greyed, never hidden, in every
    // session.
    if (redesign_button_is_level_case(b)) return false;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (tc.id != b) continue;
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift;
        chord.alt   = tc.alt;
        // Both readers ask only while the view stands (the grey from
        // redesign_button_enabled's `h` term, the hide on its own mode term).
        if (save_button_publishes(tc.id, /*history_view=*/true))
            chord.shift = true;
        if (history_mode_owns_key(tc.key, chord)) return false;
        return state != nullptr
                   ? history_mode_key_blocked(tc.key, chord, *state)
                   : history_mode_key_blocked_in_every_state(tc.key, chord);
    }
    // Not in the table: nothing to consume. Unreachable today (the table is
    // the whole roster since 2026-10-21, its static_assert) and stated rather
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
// nothing to commit, a skip at the end) — is painted and greys as it always
// did. (Left / Right are admitted by this predicate, their chords staying
// live, but kTransportHistoryStandIns seats Older / Newer in the arrow
// BUTTONS' places in the view, the painted, state-greyed pair there.)
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
    return app.history_mode.active && history_view_hides_button(b);
}
bool history_view_hides_button(RedesignButton b) {
    return history_mode_refuses_button_chord(nullptr, b);
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
// A LAND IS ALWAYS ON THE FOCUS (architect 2026-07-28, closing the rule): both
// ctrl-toggle arms land on whichever marker the toggle leaves focused. So
// there is no such thing as a focus set whose land went somewhere
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
//     empty post-toggle selection lands nothing). THE ADD SEL SWEEP joins
//     them (architect 2026-11-10, session 101, commit_region_sweep): at its
//     lift it lands on the focus its last swept member took, behind the
//     click's own stop — a sweep that added no member lands and stops
//     nothing. The plain click's FOUR
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
// seat_playhead_on_source_frame, whose domain-shaped tail
// (seat_playhead_on_domain_frame) is the write a caller holding a DISPLAYED
// frame that belongs to NO store entry — the `h` history mode's diff flags,
// whose removed lines exist in no store at all and whose nudged pair lands
// between its two mapped stems — seats through instead of a second copy of
// it, and so the land / reseat pair cannot drift either. Everything the list above says about WHEN a land happens and what
// it must not touch governs both halves alike.
// The shared write, defined below the two entry points that share it.
static void seat_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport, int64_t src_frame);
static void seat_playhead_on_domain_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport,
                                          int64_t domain_frame);

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
    // and its three exemptions — the nudge's, the undo/redo restore's and the
    // drop's — are at AppState::camera_hold). Unlike the audition's end it is gated on the
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
// diff-flag click bodies), so none of them wants the reseat. IT TAKES A
// DISPLAYED-DOMAIN FRAME (2026-10-11): its callers land a diff flag's cue
// where it is painted, its frame mapped into the displayed domain once by the
// one owner (history_diff_flag_landing_domain_frame, app_state.h).
void land_playhead_on_domain_frame(AppState& app, const GuiAudio& audio,
                                   Viewport& viewport, int64_t domain_frame) {
    // The A/B audition's end, this entry point's half of it — the argument is
    // at the marker form above, the inventory at GuiAuditionSequence. The
    // hold posture's clear too, but only when the seat moves the cursor
    // (AppState::camera_hold).
    clear_audition_sequence(app);
    const int64_t sample_before = app.playhead_cursor_sample;
    seat_playhead_on_domain_frame(app, audio, viewport, domain_frame);
    if (app.playhead_cursor_sample != sample_before) app.camera_hold = false;
}

// The write itself, shared by the marker entry points above: the forward map,
// then the domain tail below, whose two decisions follow.
static void seat_playhead_on_source_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport,
                                          int64_t src_frame) {
    seat_playhead_on_domain_frame(
        app, audio, viewport,
        source_frame_to_active_domain(app, audio, src_frame));
}

// The domain-shaped tail of the seat: the clamp, the idempotence return and
// the damage, shared by the source form above and the domain land.
static void seat_playhead_on_domain_frame(AppState& app, const GuiAudio& audio,
                                          Viewport& viewport,
                                          int64_t domain_frame) {
    const int64_t sample =
        clamp_playhead_to_live_domain(domain_frame, app, audio);
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
// The payoff is the lane model — with that marker selected, its stem (an
// enabled marker's; a disabled chip stays grey and publishes none) and the
// cursor standing on it say the same thing, which is precisely the
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
    // chord_is_bound still lists Space, so nothing about this
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
// nothing and stops nothing, and no key arrives while the pending stands
// (on_key's drag-modal statement), so the act sees the session as it stands at the
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

// THE WAVEFORM'S COLUMN BOUNDS — the ONE clamp the zoom pivot's held column
// goes through, at its seat (apply_touch_nav_update) and at its two re-seats
// (rebind_zoom_pivot_into_waveform), and the same bounds
// render_strip_anchor_stem draws the stem inside.
static double clamp_col_into_waveform(const GuiRect& wf_area, double col) {
    const double col_max =
        wf_area.w > 0 ? static_cast<double>(wf_area.w) - 1.0 : 0.0;
    if (col < 0.0)     col = 0.0;
    if (col > col_max) col = col_max;
    return col;
}

// THE ONE-FINGER ZOOM'S LEVEL RULE — read by apply_touch_nav_update's ctrl
// arm (the S Pen's button, 2026-09-25: "port the fork, reuse the applier, no
// second zoom rule"). dx zooms plain linear off the LIVE level, RIGHT zooming
// in (`zoom_level - dx/rate` — the sign's derivation from the pinch is at
// ScrollDragState's head, app_state.h), pre-clamped
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

// THE ZOOM PIVOT'S RE-SEAT: its column under the LIVE viewport, with the
// Ableton EDGE TRICK — one owner, read by the pinch and the one-finger touch
// zoom (apply_touch_nav_update) at their two RE-SEATS alone, never every
// frame: (A) after a placement the walls refused and (B) before a placement
// under a camera another writer moved (architect 2026-10-31; the seat holds
// the column with the frame, contract at TouchNavZoomState). The caller
// STORES the column it returns as the held one. A zoom never moves the
// viewport itself, so the resting `viewport_start_sample` IS the viewport
// the column is read against and there is no local `vp` to clamp first.
// Clamping the column into [0, W-1] and REBINDING the anchor to that edge
// pixel's frame is what keeps the focus on screen once a wall has pushed it
// past an edge.
// WHAT THE REVERSIBILITY PROPERTY IS, stated exactly: THE ANCHORED FRAME IS
// INVARIANT FOR THE PHASE, so zooming out and back in by the same travel
// pivots about the SAME song position both ways and the gesture reverses into
// the section it came from. AWAY FROM THE WALLS that is the strict identity
// BY CONSTRUCTION — the frame and its column are both held, so the return
// event places the same frame at the same column and reproduces the earlier
// viewport. AT A SATURATED WALL the viewport cannot
// come back the same way (while `vp` is pinned the view is determined by the
// level alone), and what survives is the FOCUS: the stem's column slides
// left/right with the content and the frame under it never changes. That is
// the case the screen column got wrong — it held the COLUMN at the wall too
// and let the song walk out from under it, so the way back zoomed into a later section
// entirely (TouchNavZoomState, app_state.h). WHERE
// THE PROPERTY STOPS: the edge REBIND, the one lasting mutation here — once
// the anchored frame has been pushed off the visible span the anchor becomes
// the edge pixel's content, and the return trip pivots about that instead.
// A KNOWN LIMIT, ACCEPTED (architect 2026-11-06, session 68, Astra's
// localization review): the column held after a wall re-seat is fractional
// (A / q, e.g. 100.5), and reversing the zoom, the camera's whole-frame
// rounding can carry the anchor frame's projection across that half-column
// threshold, so the painted anchor stem (strip_anchor_stem_column, which
// reprojects the frame each paint) may alternate by one device px. A held
// raster tie choice or a subframe camera phase would remove it and is NOT
// TAKEN: it needs a pan or zoom all the way out against a wall and back in.
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
    // THE A/B AUDITION IS HANDS-OFF (architect 2026-10-28; the rule at
    // end_audition_for_press's declaration): the pan and the pinch deliver no
    // button press, so their frames are a press entry of their own — the
    // first frame of a gesture ends a standing act, every later one finds it
    // ended (one enum compare).
    end_audition_for_press();
    // THE FRAME'S MEANING, three and only three: TWO fingers are the PINCH;
    // ONE finger under the ctrl bit is THE ONE-FINGER ZOOM (2026-09-25, the S
    // Pen's side button, a live fork mid-gesture; the frame's ctrl is ignored
    // on two fingers); one
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
    // stem at the edge rather than at the next motion.
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
    // THE PAN'S SEAT DIES AT EVERY ZOOM FRAME (2026-10-22; the seat's record
    // at the one-finger arm below): a zoom — two fingers, or one under ctrl —
    // moves the content under the finger by the level, refused or applied or
    // not, so the pan that follows it (the pinch's downgrade, the pen's ctrl
    // edge back to the pan) seats afresh on its own first frame, which is
    // what keeps the downgrade clean.
    if (f.two_finger || one_finger_zoom) app.touch_nav_zoom.pan_seated = false;

    // THE GESTURE'S SURFACE IS DECIDED AT ITS START (architect 2026-10-16:
    // "all gestures that start on waveform can continue on chrome"). Every
    // frame reaching this body belongs to a gesture that OPENED ON THE PAN
    // ZONE — the platform opens a window only on the zone's down answer, and
    // the pan, the pinch and the pen's pan and zoom are all born in that
    // window (touch_point_in_pan_zone; the touch state block, input_core.h) —
    // so where the frame's point lies now decides nothing: the finger, the
    // pair's centroid or the pen may travel over the frame's columns, the
    // stereo divider, the lanes, the toolbar, row 8, a card or the keys and
    // the gesture keeps navigating the waveform until the lift, exactly as
    // the pinch's zoom holds its seated pivot.
    // WHAT THE FRAME STILL ASKS IS STATE, NEVER PLACE — the modal
    // refusals, which can come to stand mid-gesture without the finger
    // choosing a surface: a prompt, the progress modal, a
    // live pointer gesture (a pen press — no two writers), a
    // dialog editor, loading or empty audio, and THE FOLDER OVERLAY, the
    // render player or the picker, standing whole (a complete batch swaps the
    // progress modal for the player, and the band's veil would otherwise be
    // punched by a pan from under it). A refused
    // frame navigates nothing AND SEATS NOTHING; the gesture resumes when the
    // state clears. The `h` view refuses nothing, entered before the
    // gesture or toggled under it (bare `h`): the same
    // waveform navigates either way. EVERY EDITOR REFUSES since 2026-10-25
    // (keyboard_modal_editor_active, the one predicate the armed sweep reads
    // too): the lane editors took the veil, so a pan or pinch no longer
    // punches through them as it did from 2026-07-28.
    // A REFUSED FRAME ALSO BREAKS THE PAN'S HOLD (2026-10-22): the finger
    // travels on while nothing navigates, so the first frame after the state
    // clears seats afresh where the finger then is and the pan resumes from
    // there — the content never jumps to catch up with travel the refusal
    // swallowed.
    if (app.prompt.active || app.batch_progress.active ||
        any_pointer_gesture_active(app) ||
        folder_overlay_stands(app) || score_viewer_stands(app) ||
        keyboard_modal_editor_active() ||
        app.loading || audio.total_frames() <= 0) {
        app.touch_nav_zoom.pan_seated = false;
        return;
    }
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
    // the seat sits ABOVE the exact-no-op return and BELOW the state refusal.
    // Seating is a NAVIGATION act, so it takes the refusal with everything else
    // — a pinch whose frame lands under a prompt, the progress modal, a
    // drop-down, the folder overlay, a dialog editor or a load anchors
    // nothing, and seats once the state clears. Where the pair lands is no
    // question here: the gesture's one surface answer was the pan zone's at
    // its down (architect 2026-10-16, the refusal's paragraph above), so a
    // second finger joining over a lane or the toolbar seats as it would over
    // the waveform. But "this frame's deltas apply nothing" is a statement about
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
    // itself; on a single nav whose FIRST frame already carries the bit
    // (the button held at the down, then the slop crossed) the DOWN point.
    if ((f.two_finger || one_finger_zoom) && !app.touch_nav_zoom.seated) {
        TouchNavZoomState& z = app.touch_nav_zoom;
        // The waveform's own column (window x less the area's x, the
        // canvas standing the column's margin in since 2026-10-09), CLAMPED
        // INTO THE WAVEFORM'S COLUMNS before the frame is read under it: the
        // second finger may join over chrome and the pen's previous point may
        // lie off the canvas, and the seat HOLDS this column for the phase
        // (architect 2026-10-31, contract at TouchNavZoomState). The camera
        // the seat was taken against is recorded for re-seat (B) below.
        const double seat_col = clamp_col_into_waveform(
            wf_area,
            static_cast<double>(f.x - wf_area.x) - (one_finger_zoom ? f.dx : 0.0));
        z.anchor_sample = vp + seat_col * spp_old;
        z.anchor_col    = seat_col;
        z.zoom_vp       = app.viewport_start_sample;
        z.zoom_origin   = app.viewport_lattice_origin;
        z.zoom_level    = app.zoom_level;
        z.seated        = true;
        z.one_finger    = one_finger_zoom;
        // THE SEAT OWES ITS FIRST FRAME'S DAMAGE —
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
    // pending double-click candidate must not survive it (the C8 rule, the
    // keyboard's own at on_key's top). It stays BELOW the no-op return
    // deliberately — a frame that applies nothing must not consume a pending
    // candidate — which is why the seat was hoisted around it rather than the
    // return moved.
    app.double_click = DoubleClickCandidate{};

    // THE PIVOT, and the two finger counts answer it DIFFERENTLY since
    // 2026-08-14 (the seated pinch; contract at TouchNavZoomState,
    // app_state.h).
    double anchor_sample = 0.0;   // active-domain song frame the pivot holds
    double anchor_col    = 0.0;   // the column the placement puts it at
    const bool pan = !f.two_finger && !one_finger_zoom;
    double pan_col = 0.0;   // the pan's placement column (fractional)
    if (pan) {
        // ONE FINGER — THE PAN, PLACED LIKE THE MARKER DRAG (architect
        // 2026-10-22: "I don't understand why the pan can't just use whatever
        // math the marker drag is using, because that's basically flawless").
        // THE STALL IT REPLACES, measured that day on the tablet over adb with
        // injected motion, finger and stylus sources alike: the pan was
        // STATELESS, placing the content under the previous centroid column
        // (x - dx) at the current one every frame, and the viewport's grid
        // snap (clamp_viewport_start) rounded each frame's travel to a whole
        // column and dropped the rest — steps of 0.3 px a frame moved
        // nothing, 0.6 px a frame moved a whole px each (an expected +45 px
        // measured +75), 1 and 2 px were exact. So a pan slower than half a
        // device px a frame (about 30 px/s at 60 Hz, 45 at 90) stood still,
        // and the S Pen's smooth 480-Hz stream showed it whenever he crept.
        // The marker drag never stalls because it is ABSOLUTE: it seats the
        // grabbed frame once and places by the travel from that seat, so its
        // snap rounds a cumulative value (marker_drag.cpp's anchor).
        // THE SEAT, TAKEN ONCE PER PAN PHASE: on the phase's first applied
        // frame, the content under the PREVIOUS centroid column (cx - dx) —
        // the latch folds the slop's travel into that frame's dx, so this is
        // the content under the DOWN point — and every frame after places
        // that one song frame at the LIVE FRACTIONAL centroid column
        // (GuiTouchNavFrame::cx), no delta arithmetic at all. THE RE-SEATS,
        // wherever the finger's hold on the content is broken: (1) every zoom
        // frame and every refused frame (above), so a pan after a pinch's
        // downgrade or a refused stretch seats afresh; (2) the gesture's end
        // (end_touch_nav); (3) A PLACEMENT THE WALLS REFUSED (below the
        // apply): the seat moves to the content now under the finger, so a
        // reversal at the song's ends follows the finger at once, the Android
        // way, no rubber band; and (4) a camera another writer moved under the
        // finger (a key, a view switch, a load — the camera the last pan frame
        // settled no longer stands), seated afresh exactly as on a first
        // frame. The column convention is the zooms' own (the
        // waveform's column, window x less the area's x, against the live
        // viewport), and no clamp is needed on the column: the placement runs
        // through the viewport chokepoint's own clamps, and (3) reads them.
        TouchNavZoomState& z = app.touch_nav_zoom;
        pan_col = f.cx - static_cast<double>(wf_area.x);
        const bool hold_stands =
            z.pan_seated &&
            z.pan_vp == app.viewport_start_sample &&
            z.pan_origin == app.viewport_lattice_origin &&
            z.pan_level == app.zoom_level &&
            z.pan_tab_view == app.active_tab_view &&
            z.pan_audio_view == app.active_audio_view;
        if (!hold_stands) {
            z.pan_anchor_sample = vp + (pan_col - eff_dx) * spp_old;
            z.pan_seated        = true;
        }
        anchor_sample = z.pan_anchor_sample;
        anchor_col    = pan_col;
    } else {
        // A ZOOM — TWO FINGERS, OR ONE UNDER CTRL — PIVOTS ABOUT THE POINT ON
        // THE WAVEFORM THE GESTURE GRABBED, held for the phase's life: the
        // song frame AND ITS COLUMN seated above (on the phase's FIRST
        // unrefused frame, whether or not that frame applies anything) and
        // placed together here, the held frame at the held column (architect
        // 2026-10-31, the pan's absolute seat the model; contract at
        // TouchNavZoomState). The centroid's own travel is discarded by the
        // fork above (eff_dx is 0), so moving both fingers together still
        // applies nothing — it only seats — while the one finger's travel is
        // the zoom itself (zoom_dx).
        TouchNavZoomState& z = app.touch_nav_zoom;
        // RE-SEAT (B), A CAMERA ANOTHER WRITER MOVED under the gesture: the
        // camera the seat or the last zoom frame settled no longer stands (a
        // following play paged the view under the pen's retained anchor, or
        // between the seat and the first applied frame), so the held column
        // would yank the view back to the section it left. The column is
        // re-read from the live viewport through the edge trick's one owner,
        // which pins a frame the page carried off the waveform at the edge
        // pixel and rebinds it to that pixel's content.
        if (z.zoom_vp != app.viewport_start_sample ||
            z.zoom_origin != app.viewport_lattice_origin ||
            z.zoom_level != app.zoom_level)
            z.anchor_col = rebind_zoom_pivot_into_waveform(z.anchor_sample);
        anchor_col    = z.anchor_col;
        anchor_sample = z.anchor_sample;
    }

    // The distance ratio maps to the level LOGARITHMICALLY — spreading the
    // fingers by 2x is one level in (spp halves, so the content between the
    // fingers scales with the finger gap; no feel constant). Pre-clamped into
    // the same [effective floor, effective ceiling] window clamp_viewport_start
    // re-applies, exactly as every other caller pre-clamps — the chokepoint's
    // level_changed compare requires a real request (its contract names the
    // callers).
    // THE ONE-FINGER ZOOM TAKES ITS OWN LEVEL RULE instead
    // (nav_drag_zoom_level — linear in travel at the gui_scale-resolved rate,
    // RIGHT zooming in).
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
    // both of its anchor terms: a ONE-FINGER frame places the pan's SEATED
    // frame at the CURRENT fractional centroid column, which is the pan, while
    // a ZOOM frame places its HELD frame at its HELD column, which is a pure
    // zoom about the grabbed point (the ratio is 1.0 on the first, and the
    // zoom's column moves only at its two re-seats). Everything downstream is the strip drag's own
    // — level clamp, viewport clamp, the synchronous per-frame rebuild, the
    // camera chokepoint's compare on either axis, and the mid-gesture true-no-op
    // skip.
    // THE LATTICE RULE (architect 2026-10-31; the rules at
    // AppState::viewport_lattice_origin): a frame that CHANGES THE LEVEL
    // places EXACTLY — the requested start is the rest, its lattice origin
    // derived from it, so the held frame sits at the held column to one frame
    // rather than within half a column — and a frame at an unchanged level (a
    // pan, or a zoom pinned at a level bound) KEEPS the camera's lattice, so a
    // pan is a pure index shift on it and a pan after a pinch does not jump.
    const ViewportLattice lattice = (new_level != app.zoom_level)
                                        ? ViewportLattice::Exact
                                        : ViewportLattice::Keep;
    viewport.apply_strip_drag_zoom(new_level, anchor_sample, anchor_col,
                                   /*final=*/false, lattice);

    if (pan) {
        // THE WALLS' REFUSAL RE-SEATS (re-seat (3) at the arm above): the
        // placement asked for the lattice point nearest the seat's start (on
        // the lattice the frame settled on, the live origin, through the one
        // snap owner — architect 2026-10-31), and a
        // settled start anywhere else is the chokepoint's wall (or the
        // whole-song park at 0). The seat then becomes the content under the
        // finger NOW, so the travel spent pushing past the end is forgotten
        // and a reversal moves the content on its first frame. The applier's
        // true-no-op skip and the camera compare are untouched by this: it
        // reads only where the start settled.
        TouchNavZoomState& z = app.touch_nav_zoom;
        const double spp_now = painter_samples_per_pixel(app, audio, wf_area);
        if (spp_now > 0.0) {
            const int64_t requested = static_cast<int64_t>(
                std::nearbyint(anchor_sample - anchor_col * spp_now));
            if (nearest_viewport_grid_point(requested, spp_now,
                                            app.viewport_lattice_origin) !=
                app.viewport_start_sample)
                z.pan_anchor_sample =
                    static_cast<double>(app.viewport_start_sample) +
                    pan_col * spp_now;
        }
        // The camera this frame settled, which re-seat (4) compares.
        z.pan_vp         = app.viewport_start_sample;
        z.pan_origin     = app.viewport_lattice_origin;
        z.pan_level      = app.zoom_level;
        z.pan_tab_view   = app.active_tab_view;
        z.pan_audio_view = app.active_audio_view;
    } else {
        // RE-SEAT (A), A PLACEMENT THE WALLS REFUSED — the pan's re-seat (3)
        // in the zoom's form, the same test: a settled start other than the
        // point nearest the requested one ON THE LATTICE THE FRAME SETTLED ON
        // (the live origin, through the one snap owner; under Exact the
        // requested start itself, architect 2026-10-31) is the chokepoint's
        // wall (or the whole-song park at 0). The viewport is pinned, so the held frame
        // slides across the glass with the content and THE HELD COLUMN
        // FOLLOWS IT, re-read from the settled viewport through the edge
        // trick's one owner, which pins a frame pushed off the waveform at
        // the edge pixel and rebinds both — the reversibility paragraph at
        // TouchNavZoomState: at a saturated wall the focus survives. Off the
        // walls nothing here fires and the column stays held. Every read is
        // valid on every frame: apply_strip_drag_zoom assigns the level and
        // the clamped start before its mid-gesture true-no-op return.
        TouchNavZoomState& z = app.touch_nav_zoom;
        const double spp_now = painter_samples_per_pixel(app, audio, wf_area);
        if (spp_now > 0.0) {
            const int64_t requested = static_cast<int64_t>(
                std::nearbyint(anchor_sample - anchor_col * spp_now));
            if (nearest_viewport_grid_point(requested, spp_now,
                                            app.viewport_lattice_origin) !=
                app.viewport_start_sample)
                z.anchor_col = rebind_zoom_pivot_into_waveform(z.anchor_sample);
        }
        // The camera this frame settled, which re-seat (B) compares.
        z.zoom_vp     = app.viewport_start_sample;
        z.zoom_origin = app.viewport_lattice_origin;
        z.zoom_level  = app.zoom_level;
    }
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
    // skip the resync).
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
    // button-held landing zooms on about the same anchor. The pen facts are the
    // platform's and it answers them here, true only inside the pen's own
    // lift (GuiPlatform::pen_lift_keeps_zoom_anchor), so every other end
    // clears. Nothing on screen changes at a retention, so it owes
    // no damage; the stem the seat gates simply stays.
    // THE PAN'S SEAT DIES AT EVERY END, the pen's retained lift included
    // (2026-10-22; its record at apply_touch_nav_update's one-finger arm): a
    // fresh landing grabs fresh content.
    TouchNavZoomState& z = app.touch_nav_zoom;
    z.pan_seated = false;
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
// predicate the press router and the ctrl claim read. Until
// that ruling this body was a HAND COPY of the press router's derivation (its
// own inside_waveform spelling plus its own `!waveform_lower_half` term), so
// the lower half's arrival on the zone did NOT follow for free — it followed
// once the copy became a call. Surface geometry only, save the open menu (the
// first clause, 2026-10-01); every other refusal stays downstream (the update
// body's per-frame state refusal, the armed sweep's begin gate list). The
// zone's answer at the down is the nav gesture's one surface answer
// (architect 2026-10-16): what it admits navigates until the lift wherever
// the point travels (apply_touch_nav_update).
//
// SO THE WHOLE WAVEFORM IS THE PAN ZONE NOW (architect 2026-08-13, embracing
// the consequence: "currently finger down works in the upper half by waiting
// for the finger up, but on the lower half it immediately dispatches the
// scanner — so there's an asymmetry, and now that I understand it we should
// eliminate the asymmetry"). A one-finger drag pans anywhere on the waveform
// (the armed sweep, under the lit TRIM lamp, reaches anywhere on it), and a
// motionless tap on the
// lower half is the tap-at-lift burst whose motionless press-release IS the
// deferred scrub act — the pointer road's own machinery, with no touch code.
//
// AND THE WAVEFORM IS THE WHOLE PAN ZONE (architect 2026-09-25, "out of
// both — we want symmetry as much as possible"): the ruler and the marker
// lane left the navigation surface, so they left this zone
// through the owner with no clause here. A finger landing on either resolves
// to the POINTER TRANSLATION, where the lanes' own press arms answer it: a
// tap places the playhead (a double tap on the empty marker lane creates)
// and a drag does nothing — the placement pending begins no pan.
bool GuiInputHandler::touch_point_in_pan_zone(int x, int y) const {
    // (A CLAUSE FOR THE SETTINGS CHOICE EDITOR'S DROPPED LIST stood here
    // 2026-10-07 to 2026-10-14, for the dropdown's reason, and went with the
    // list.)
    // THE ZONE YIELDS UNDER A NOTIFICATION CARD (2026-08-29), for the
    // keyboard clause's reason below: the cards stack over the waveform's
    // upper right, the whole waveform is the pan zone, and a finger landing
    // on a card inside a pan zone would become the phone-model pan and
    // deliver its press only at the lift, if at all. Answering false lets the
    // finger resolve to the pointer translation ON CONTACT and reach the
    // card's own press claim at the down — which the card needs since it
    // became one button acting at the lift (2026-10-01): the long press is
    // measured from the press, and the held card's clock stops there. The
    // card is opaque to the pointer everywhere else too (the hover walk), so
    // the zone agrees with it.
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
    // beside the field resolves to the pointer translation like any lane
    // press, where the editor's modal veil consumes it and closes nothing
    // (every editor a pure modal, architect 2026-10-29, on_button_press's
    // veil).)
    // AND IT YIELDS UNDER THE ON-SCREEN KEYBOARD (2026-08-27), for the flag
    // box carve-out's reason exactly: the keyboard paints over the waveform's lower
    // part, the whole waveform is the pan zone, and a finger landing on a key
    // inside a pan zone would become the phone-model pan and NEVER DELIVER A
    // PRESS — so every key would need a hold beat to type one character.
    // Answering false lets the finger resolve to the pointer translation and
    // reach the keyboard's own press claim, which is where a key acts.
    // ONE SPELLING OF "ON THE KEYBOARD": this asks the same rect the press
    // claim asks (claim_rect, the band), so the two cannot disagree.
    if (onscreen_keyboard::stands(app) &&
        rect_contains(onscreen_keyboard::claim_rect(app), x, y))
        return false;
    // AND IT YIELDS UNDER THE FOLDER OVERLAY'S BAND (2026-08-28), for the two
    // clauses above's reason exactly and for one more: the panel paints over
    // the waveform whole (its band is the modal family's panel, from the view
    // bar's top row to the controls row, modal_box.h, 2026-10-14), the whole
    // waveform is the pan zone, and
    // BOTH of the band's gestures live on the POINTER — the row press, whose
    // act is at the lift, and the band's own SCROLL DRAG, which is that same
    // arm past the vertical gate. Left in the zone, a finger crossing the
    // slop on a row would resolve to the phone-model pan (or, under a lit
    // sweep lamp, TRIM or ADD SEL, to the sweep every content refuses), so
    // the list would
    // answer a tap and nothing else. ONE SPELLING OF "ON THE BAND": the same
    // rect the press claim reads.
    if (folder_overlay::stands(app) &&
        rect_contains(folder_overlay::surface_rect(app), x, y))
        return false;
    // AND UNDER THE SCORE VIEWER'S PANEL (2026-10-26): the page covers the
    // waveform whole and takes no gesture, so a finger there resolves to the
    // pointer translation, whose press the viewer's veil consumes, rather
    // than to a pan or an armed sweep the viewer refuses.
    if (score_viewer_stands(app) &&
        rect_contains(modal_panel_rect(app, /*keyboard_h=*/0), x, y))
        return false;
    return point_on_nav_surface(app, x, y);
}

// --- The touch region formers (the TRIM and ADD SEL lamps' sweeps) ---------
//
// THE ARMED SWEEP (architect 2026-11-07, session 87, retiring the region hold
// of 2026-08-12) — the full contract and the refusal list are at the
// declarations (input_handler.h). These three ARE the region record's
// machinery driven from the platform's region hooks: no pending and no
// threshold wait at the begin — the lit lamp and the slop crossing already
// disambiguated, so the begin runs a former's press half directly. TWO
// FORMERS ON ONE RECORD since 2026-11-10 (architect, session 101: "can reuse
// trim's mechanism largely"): the TRIM sweep and THE ADD SEL SWEEP
// (RegionDragState::select), the begin choosing by the one lit lamp of the
// radio pair; one core phase, one motion path, one end owner.

bool GuiInputHandler::touch_sweep_armed() const {
    // EITHER LAMP ARMS THE DRAG (the radio pair, so at most one stands).
    // BOTH ARE INERT IN THE `h` VIEW: TRIM's key is hidden there (the history
    // allowlist naming no bare `w`) and the view cannot edit the trim; ADD
    // SEL's role in there is its diff-flag click alone (bare `k` on the
    // allowlist), so the drag stays the pan. The bits survive the view.
    return (app.trim_armed || app.add_to_selection) &&
           !app.history_mode.active;
}

void GuiInputHandler::begin_touch_region(int x, int y) {
    // THE A/B AUDITION IS HANDS-OFF (architect 2026-10-28; the rule at
    // end_audition_for_press's declaration): the armed sweep delivers no
    // button press, so its begin is a press entry of its own, ahead of the
    // gates below.
    end_audition_for_press();
    // The press path's own gates, restated because this gesture never passes
    // through on_button_press: a prompt or modal editor owns the input, unloaded audio has no columns to span, and a live
    // pointer gesture must not be torn by a second writer. THE EDITOR GATE
    // IS THE ONE PREDICATE FOR A KEYBOARD-TAKING MODAL
    // (keyboard_modal_editor_active), every editor a modal since 2026-10-25,
    // the touch navigation reading the same one. (The `h` view never reaches
    // here: touch_sweep_armed answers false there, so its drag is the pan.)
    // A refused begin arms nothing — the update/end hooks then no-op on the
    // drag's own !active guard, so the refused stream is dead rather than a
    // fallback pan, and the lamp stays lit.
    if (app.prompt.active) return;
    // THE RENDER PLAYER'S AND THE PICKER'S VEILS (2026-08-28), restated here
    // for the gesture that skips the press path: a drag on the waveform under
    // either mode begins no sweep.
    if (app.render_player.active || app.picker.active) return;
    if (app.score_viewer.active) return;
    // AND THE PROGRESS MODAL'S (2026-10-14), for the same gesture's reason.
    if (app.batch_progress.active) return;
    if (keyboard_modal_editor_active()) return;
    if (app.loading || audio.total_frames() <= 0) return;
    if (any_pointer_gesture_active(app)) return;
    // THE ADD SEL SWEEP'S PRESS HALF (architect 2026-11-10, session 101) when
    // that lamp is the lit one (the radio pair: TRIM lit wins only because it
    // is asked first, the two never standing together). NO DESELECT — the
    // sweep only adds — and nothing else at the down: the arm on a waveform
    // column, a gutter down point arming nothing (the lamp stays lit).
    if (!app.trim_armed && app.add_to_selection) {
        const GuiRect sel_area = waveform_area(app);
        const int sel_rel = x - sel_area.x;
        if (sel_rel < 0 || sel_rel >= sel_area.w) return;
        arm_selection_sweep_at(sel_rel, x, y);
        return;
    }
    // THE FORMER'S PRESS HALF at the DOWN point; the platform stages the
    // crossing position as the drag's first frame. THE ARMED DRAG IS THE ONE
    // ROUTE TO A SWEEP (architect 2026-11-07, session 87). The deselect runs
    // FIRST, ahead of the gutter refusal, so a gutter begin still deselects
    // but arms nothing. NO PLAYHEAD SEAT (architect 2026-11-07, session 88:
    // "Setting the trim should not move the playhead"; the rule at the head
    // of input_trim.cpp) and so NO PLAYBACK STOP HERE: the stop a placement
    // owed is the trim write's own, at the sweep's first accepted change
    // (write_trim_from_sweep), and a stroke that changes no bound stops
    // nothing, the one claim of every trim route.
    selection.clear_selection();
    const GuiRect area = waveform_area(app);
    const int rel = x - area.x;
    if (rel < 0 || rel >= area.w) return;
    arm_region_drag_at(rel, x, y);
}

void GuiInputHandler::update_touch_region(int x, int y) {
    // The drag's ONE motion path (apply_region_drag_motion): the shared
    // Chebyshev gate from the down point, the trim's moving end, no playhead
    // write. Self-guarded on the drag's own active bit,
    // which is what makes a refused begin's stream free — and what covers a
    // lost-button motion that ended it mid-gesture.
    if (!app.region_drag.active) return;
    apply_region_drag_motion(x, y);
}

void GuiInputHandler::end_touch_region() {
    // The release path's own body (any end commits — finger up,
    // wl_touch.cancel, capability loss; the platform's end split delivers or
    // drops the staged final frame, its record): the moved sweep runs the
    // shared trim commit tail through the gesture's one end owner, which puts
    // the TRIM arm lamp out — or, on the ADD SEL former, puts ADD SEL out and
    // lands the playhead on the swept focus. The owner is self-guarded like
    // the update.
    commit_region_sweep();
}

// --- The touch caret drag (a finger's drag in the editor's field) ---
//
// The third ruled divergence (architect 2026-09-05) — the contract, the
// drain rule and the one record the bodies keep (the editing session the
// stream began on) are at the declarations
// (input_handler.h), the machine at GuiInputCore::set_touch_nav_hooks.
// Every seat is THE ONE POINTER SEAT (set_editor_caret_from_x), which is
// what keeps the finger's caret and the press's on one byte rule and one
// blink rule; nothing here selects, commits or seeds (the field's whole
// vocabulary, a tap and a drag, at begin_touch_caret_drag's declaration).

GuiTouchEditorField GuiInputHandler::touch_point_in_editor_field(int x,
                                                                  int y) {
    const ActiveEditorText g = active_editor_text(app, audio);
    if (!g.valid || !rect_contains(g.field, x, y))
        return GuiTouchEditorField::Outside;
    // A NOTIFICATION CARD STANDING OVER THE FIELD OWNS THE GLASS UNDER IT
    // (2026-10-14): the cards paint above the modal family's box, and a
    // refusal card stands over the very editor that refused, so a point on
    // a card is the card's, never the field's — the same as-painted lookup
    // the pan zone yields to (touch_point_in_pan_zone). Answering Outside
    // lets the contact resolve to the pointer translation ON CONTACT and
    // reach the card's own press claim at the down, where its hold clock
    // starts (a held card dismisses the stack); answered Field, the contact
    // stayed pending to the lift, measured no hold, and a drifting one
    // dragged the editor's caret through the card.
    if (notification_card_at(app, x, y) != 0)
        return GuiTouchEditorField::Outside;
    return GuiTouchEditorField::Field;
}

void GuiInputHandler::begin_touch_caret_drag(int x, int y) {
    // The stream acts on the editor active now and keeps no record of it
    // (the contract at the declaration).
    const ActiveEditorText g = active_editor_text(app, audio);
    if (!g.valid) return;
    // A FINGER IN THE FIELD TAKES THE FOCUS BACK, the press's own rule on the
    // glass's road (return_modal_focus_to_field); the seat below restarts the
    // blink.
    if (g.dialog) return_modal_focus_to_field();
    // A caret drag is motion, so no seed standing can become a double-click
    // across it (the C8 rule the nav frames apply); the stream's own end
    // seeds nothing.
    app.double_click = DoubleClickCandidate{};
    set_editor_caret_from_x(g, x, y);
    if (g.dialog) viewport.invalidate_dialog_editor_box();
    else          viewport.invalidate_top_strip();
}

void GuiInputHandler::update_touch_caret_drag(int x, int y) {
    const ActiveEditorText g = active_editor_text(app, audio);
    // The editor active now is the one the stream began on: the drag owns
    // the one touch, so no road opens another meanwhile (architect
    // 2026-10-30; the contract at the declaration).
    if (!g.valid) return;
    // The I-beam follows the finger and nothing selects — the seat drops the
    // anchor at every frame.
    set_editor_caret_from_x(g, x, y);
    if (g.dialog) viewport.invalidate_dialog_editor_box();
    else          viewport.invalidate_top_strip();
}

void GuiInputHandler::end_touch_caret_drag() {
    // The platform fires this hook unconditionally once the drag began, on
    // the lift and on both hard ends, so this is the one road every stream
    // leaves by; it acts on the editor active now, the update's own rule.
    const ActiveEditorText g = active_editor_text(app, audio);
    if (!g.valid) return;
    // THE DRAIN RULE: the caret stays where the last delivered frame seated
    // it, nothing selects, nothing commits (Enter remains the one commit
    // route), nothing seeds, and the editor stays open. What the end owes
    // is the modal focus ring's own courtesy — a blink restart, so the
    // resting caret is lit the instant the finger leaves rather than caught
    // in the dark half of a period the drag's last seat started — the blink
    // alone, so the row-end affinity that seat set stands.
    text_editor::restart_blink(*g.ed);
    if (g.dialog) viewport.invalidate_dialog_editor_box();
    else          viewport.invalidate_top_strip();
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
        // The published rect is the PAINTED face (the ground round it is not
        // pressable, architect 2026-11-02, R5; point_on_key, materials.h).
        if (point_on_key(app.modal_dialog.buttons[i].rect, x, y))
            return static_cast<int>(i);
    }
    return -1;
}

// THE DIALOG BUTTONS' POINTER WALK, run on every motion under a standing
// dialog. It answers two readers — THE RENDER PLAYER'S HOT BUTTON
// (AppState::player_hot,
// architect 2026-10-06; never lit since the player's cases became the
// program's, 2026-10-09: Cool Edit's case has no hover face); and this
// surface's tooltip wait (the tail).
//
// AN ARMED BUTTON — THE FEINT (architect 2026-08-13): "if the user feints —
// clicks a button and then drags away before the mouse goes up — then that
// button receives the passive focus as well." So THE ARM STAYS LIVE FOR THE
// WHOLE HOLD, and this walk only answers whether the pointer is inside it —
// the armed button's inside bit (AppState::modal_dialog_press_inside), which
// the press face reads (paint_modal_dialog's row; the press arc, 2026-11-05),
// damaging the box on every crossing; the release re-hits the armed button
// at its own coordinates (take_modal_dialog_release), reading no bit. THE
// FOCUS IS NOT THIS WALK'S: the press already moved it onto the
// armed button (architect 2026-10-03, Windows' rule;
// arm_modal_dialog_press), so leaving the button leaves the focus where it
// is (read by a dialog editor's ring alone). The whole rule is at
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
    // THE PLAYER'S HOT BUTTON: the hit on the player's row while the
    // program's case has a hot face (program_spec.h's case_hot_face — none
    // since 2026-10-09, Cool Edit's) and no dialog press is armed (comctl32
    // shows no hot item under capture), else none —
    // AND NONE WHILE THE PEN'S HOT-FACE LATCH HOLDS (pen_hot_latch_holds,
    // the rule at pen_hot_latch_, 2026-10-09): the roster's own answer, the
    // tapped button at rest until the pen moves off its anchor.
    set_player_hot(kProgramSpec.case_hot_face && app.modal_dialog.valid &&
                           app.modal_dialog.owner ==
                               AppState::ModalDialogOwner::Player &&
                           armed < 0 && !pen_hot_latch_holds()
                       ? hit
                       : -1);
    // AND IT OWNS THIS SURFACE'S TOOLTIP WAIT (2026-08-13, when the modal
    // buttons took hints): the same writer
    // and the same tick the roster's walk uses, keyed on the Dialog half of
    // the owner's index space. THE TARGET IS THE BUTTON'S COLUMN, not the
    // face the press above reads (architect 2026-11-07, session 87;
    // tooltip_column_rect, render.h: the face, its legend's pad under it and
    // half the gap each side — every dialog button is a key with its legend
    // under it on a controls row). EVERY dialog button has a hint, so there is
    // no membership term here — the column alone decides, and the painter's
    // stash carries the text. A held press starts no wait here either (the
    // writer's held term — the press arm above is the same press), and the
    // pointer leaving the box's column takes the box down at once.
    // THE OWNER IS STAMPED WITH THE STASH THE HIT WAS READ FROM (its owner
    // tag and session), so the wait belongs to this painted surface alone
    // and dies with it (the rule is at AppState::RedesignTooltip).
    // A ROSTER OWNER IS THE ROSTER WALK'S (2026-10-07 evening), AND A
    // MARKER OWNER WITH IT (2026-11-09, the cues that walk answers for): with
    // no dialog button under the pointer and such a wait or box standing,
    // this walk hands nothing — every caller runs the roster walk right
    // after this one, and that walk answers for its own owner (under a
    // dialog its veil or the no-wait rule hands "none" itself — or, under
    // an overlay, the overlay's opener or the tooltip lamp where the pointer
    // is on one, 2026-11-11: the two walks' buttons are disjoint, the
    // roster's in the band and the dialog's on the controls row; the color
    // picker's card, 2026-10-07..14, was the one surface before it where
    // both walks found buttons). Without this, the dialog walk's "none" would
    // restart the rows' wait at every motion and no button-row hint would
    // ever ripen.
    int     tip = -1;
    GuiRect tip_column{0, 0, 0, 0};
    if (app.modal_dialog.valid) {
        for (size_t i = 0; i < app.modal_dialog.buttons.size(); ++i) {
            const GuiRect column =
                tooltip_column_rect(app.modal_dialog.buttons[i].rect);
            if (rect_contains(column, x, y)) {
                tip        = static_cast<int>(i);
                tip_column = column;
                break;
            }
        }
    }
    const AppState::RedesignTooltip& t = app.redesign_tooltip;
    if (tip < 0 &&
        ((t.hovered.surface != AppState::RedesignTooltip::Surface::Dialog &&
          t.hovered.index >= 0) ||
         (t.visible &&
          t.owner.surface != AppState::RedesignTooltip::Surface::Dialog)))
        return;
    note_tooltip_hover({AppState::RedesignTooltip::Surface::Dialog, tip,
                        app.modal_dialog.owner, app.modal_dialog.session},
                       tip_column);
}

void GuiInputHandler::set_player_hot(int index) {
    const uint64_t session = index >= 0 ? app.modal_dialog.session : 0;
    if (index == app.player_hot && session == app.player_hot_session) return;
    app.player_hot         = index;
    app.player_hot_session = session;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// THE ARM'S HARD END — the pointer-leave hook (main.cpp), beside the roster's
// own clear_redesign_button_press and for its reason: a pointer that has left
// the window is on no button, and an act that has not happened yet must not
// be left waiting for a release that may never come. The press face's
// un-press damages the box (2026-11-05).
void GuiInputHandler::clear_modal_dialog_press() {
    if (app.modal_dialog_pressed < 0) return;
    app.modal_dialog_pressed      = -1;
    app.modal_dialog_press_inside = false;
    app.modal_dialog_press_ms     = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// THE KEYBOARD ARM'S HARD END, the twin of the one above: an act that has not
// happened yet must not be left waiting for a release that will commit
// nothing (the focus moving off the held button, below). The press face's
// un-press damages the box (2026-11-05); the contract is at
// AppState::modal_dialog_key_pressed.
void GuiInputHandler::clear_modal_dialog_key_press() {
    if (app.modal_dialog_key_pressed < 0) return;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    if (app.modal_dialog.valid)
        viewport.invalidate_rect(app.modal_dialog.box);
}

// A dialog button's PRESS: arm the index, painting and dispatching nothing.
// Shared by the prompt claim, the player's, the picker's and the editor claim
// — they differ in what their RELEASE runs, not in what their press does.
// Returns true when a button was hit (the claim then consumes the press; the
// veil consumes it either way).
//
// THE CLOCK IS STAMPED FOR EVERY ARM, a press having a time whatever it
// landed on; what the lift makes of it — the long press as Shift on a button
// with a shifted twin — is modal_dialog_press_shifted's.
bool GuiInputHandler::arm_modal_dialog_press(int x, int y) {
    const int hit = modal_dialog_button_hit(x, y);
    if (hit < 0) return false;
    // A DISABLED PLAYER BUTTON'S PRESS IS A CONSUMED NOTHING (architect
    // 2026-08-30: the transport keys are their own class) — the roster's
    // arm_redesign_press rule on this surface: nothing arms, no card (the
    // grey is the message). The bit read is the STASH'S — THE
    // CLAIM READS THE PAINTED FACE (architect 2026-09-24, strictly
    // as-painted), the per-tick comparator (main.cpp) keeping it honest, and
    // a face painted live dispatches, its act answering for itself. THE
    // PLAYER'S, THE SCORE VIEWER'S, THE TRIM PRESETS LIST'S AND THE BPM
    // FORM'S BUTTONS ARE THE ONLY ONES THAT PUBLISH IT (re-grepped
    // 2026-10-29), so this line is inert off those four rows.
    const AppState::ModalDialogButton& hb =
        app.modal_dialog.buttons[static_cast<size_t>(hit)];
    if (!hb.enabled) return true;
    // THE ARM, and its press face's damage (2026-11-05): a press is inside
    // what it hit, by construction — the feint's bit starts true.
    if (app.modal_dialog_pressed != hit || !app.modal_dialog_press_inside) {
        app.modal_dialog_pressed      = hit;
        app.modal_dialog_press_inside = true;
        viewport.invalidate_rect(app.modal_dialog.box);
    }
    // THE PRESS TAKES THE KEYBOARD FOCUS (architect 2026-10-03, "as few
    // exceptions as possible to the Windows 95 rule"): Windows moves the
    // focus to the button the mouse presses, AT THE PRESS. It REPLACES
    // whatever focus the dialog had, of either strength, and it is PASSIVE —
    // the user pointed at this button, which is not the deliberate keyboard
    // walk that earns the active strength. Every dialog's buttons arm here —
    // the prompt's, the editors' OK / Cancel, the player's and the score
    // viewer's keys, the picker's one word (Cancel, kModalCancelLabel's
    // rule; none under the settings page) or, under the trim presets list,
    // its pair and three words, under the BPM form its two words — so this is the
    // rule's one site. ONLY A DIALOG EDITOR READS WHAT IT WRITES (the
    // on-screen keyboard's ring, AppState::modal_dialog_focus): on every
    // other surface the write is the shared road's and nothing reads it
    // (architect 2026-11-02, a prompt has no focus). A refused
    // press (the disabled player button above) arms nothing and moves
    // nothing. A slide off the held button leaves the focus here and fires
    // nothing; a release away leaves the button passively focused.
    if (app.modal_dialog_focus != hit || app.modal_dialog_focus_active) {
        // MOVING THE FOCUS CANCELS THE KEYBOARD ARM, the rule's second site
        // (AppState::modal_dialog_key_pressed): this assignment takes the
        // focus off whatever the on-screen keyboard's Enter or Space was
        // holding, so that hold's release must commit nothing.
        clear_modal_dialog_key_press();
        app.modal_dialog_focus        = hit;
        app.modal_dialog_focus_active = false;
        viewport.invalidate_rect(app.modal_dialog.box);
    }
    app.modal_dialog_press_ms = monotonic_ms();
    return true;
}

// THE SHIFTED-TWIN VERDICT for the arm as it stands (2026-08-28) — the roster
// lift's own term over this surface: a press HELD past chrome_shift_hold_ms(),
// the long press being Shift on glass (architect 2026-10-30: no pointer
// carries a shift bit).
// It is measured at the LIFT against the arm's own stamp — no timer, no tick —
// and it asks nothing about WHICH button: the dispatch reads it only where
// player_button_shift_admits or score_button_shift_admits says so (the
// player's admitted twins, and the score viewer row's two skips, whose
// shifted form is the 10 s skip — dispatch_modal_dialog_button's score arm;
// 2026-10-28, Sol's round), which is what lets a button with no twin be held
// for as long as you like and still get its plain act. THE HOLD IS THE
// GLASS HALF of the pair and passes silently, the roster's ruling (tooltips do
// not show on the panel the gesture exists for).
bool GuiInputHandler::modal_dialog_press_shifted() const {
    if (app.modal_dialog_pressed < 0) return false;
    return monotonic_ms() - app.modal_dialog_press_ms >= chrome_shift_hold_ms();
}

// A dialog button's RELEASE: the act runs iff the lift lands on the SAME
// button the press armed — the button re-hit at the lift's own coordinates,
// the FEINT having let the arm survive the pointer wandering off. Returns
// that button's index, or -1 — the caller owns the
// dispatch, because a prompt's buttons and an editor's mean different things.
// The arm is consumed either way: a release ends the hold whatever it lands
// on, and a release AWAY from the armed button leaves it passively focused,
// which the press already assigned (arm_modal_dialog_press).
int GuiInputHandler::take_modal_dialog_release(int x, int y) {
    const int armed = app.modal_dialog_pressed;
    if (armed < 0) return -1;
    app.modal_dialog_pressed      = -1;
    app.modal_dialog_press_inside = false;
    // The arm's shifted-twin stamp dies with it (the verdict is read BEFORE
    // this call, modal_dialog_press_shifted).
    app.modal_dialog_press_ms     = 0;
    // The press face's un-press (2026-11-05).
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
    // the progress modal over player over picker over editor —
    // paint_modal_dialog's fork; the progress modal stands beside none of
    // the lower three, ModalDialogOwner's block; the two
    // list owners never stand together and neither stands beside an editor,
    // so their order is free). THIS IS ONE
    // OF THE THREE PLACES THE RANKING IS SPELLED and they must agree.
    const AppState::ModalDialogOwner live =
        app.prompt.active         ? AppState::ModalDialogOwner::Prompt
      : app.batch_progress.active ? AppState::ModalDialogOwner::Progress
      : app.render_player.active  ? AppState::ModalDialogOwner::Player
      : app.picker.active         ? AppState::ModalDialogOwner::Picker
      : app.score_viewer.active   ? AppState::ModalDialogOwner::Score
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
//   the RESPONSE KEY the stash names is validated against the LIVE answers
//   (PromptState::answers), a different question from the stash's and asked
//   separately.
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
        for (const PromptAnswer& live : app.prompt.answers) {
            if (b.response_key != 0 && b.response_key == live.key) {
                prompt.activate_response(b.response_key);
                return true;
            }
        }
        return false;
    }
    // THE PROGRESS MODAL'S ONE BUTTON (2026-10-14): Cancel, and its act is
    // the batch's cancel road (cancel_batch_progress) — reached at the lift
    // of a press armed on it, the modal's one road (architect 2026-10-30).
    if (app.batch_progress.active) {
        cancel_batch_progress();
        return true;
    }
    // THE RENDER PLAYER'S BUTTONS (2026-08-28): the act the stash names,
    // decided against the live player — each act's own body re-asks its own
    // state (an item to pause, an item to seek inside, a load-capable
    // highlight) and answers its own refusal.
    if (app.render_player.active) {
        // THE CLAIM IS THE PAINTED FACE (architect 2026-09-24, strictly
        // as-painted, superseding 2026-08-30's live enabled re-ask here): a
        // button painted grey never arms — the pointer's press claim selects
        // on ModalDialogButton::enabled —
        // and the lift asks the same painted bit once more (a face the
        // comparator repainted grey under the hold is consumed, the roster
        // lift's rule), so what passes was painted live and DISPATCHES.
        // Where the act has become refused since that paint (at most one tick,
        // the per-tick comparator in main.cpp repainting the row on drift),
        // the act's own body answers: carding where it cards (the load in
        // place's three refusals), silent where the silence is ruled (the
        // folder walks' and the seeks' benign refusals at their state,
        // 2026-08-31). THE CAR never comes through here: on_media_command runs the
        // player's bodies DIRECT, so a head unit's button reaches neither this
        // line nor a button's face.
        if (!b.enabled) return true;
        switch (b.player_act) {
            // THE TWO SKIPS ARE THE ROW'S SHIFT-ADMITTING PAIR (2026-08-28):
            // their keys are Home and End since 2026-08-31 — the main window's
            // own transport pair, "just like the regular GUI" — with the plain
            // acts home() and next_track(), the right one a NEXT TRACK since
            // 2026-09-04; THIS IS EACH ACT'S ONE ROAD on the glass (the car's
            // own Previous and Next are the car's
            // acts and reach neither of these buttons — architect 2026-09-12,
            // "the car is a separate interface"; both plain acts walked the
            // band at rest for the one day before that ruling), and
            // their shifted twin — the long press — is the item folder's END.
            // THE
            // ARMS THAT READ `shifted` ARE THE ADMISSION'S OWN MEMBERS
            // (player_button_shift_admits, app_state.h): every other act below
            // ignores the bit, so a held Play or Load in Place is its plain act — the
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
            // THE HIGHLIGHT PAIR (architect 2026-10-10): the band walk and
            // nothing else — a silent wall
            // at either end of the listing, in every transport state.
            case AppState::PlayerButtonAct::HighlightDown:
                render_player.move_highlight(+1);
                return true;
            case AppState::PlayerButtonAct::HighlightUp:
                render_player.move_highlight(-1);
                return true;
            case AppState::PlayerButtonAct::Up:
                // THE `..` ROW'S ACT, ON THE ROW SINCE 2026-09-01, with its
                // own silent wall at the root.
                render_player.up();
                return true;
            case AppState::PlayerButtonAct::LoadInPlace:
                render_player_load_in_place();
                return true;
            // DELETE (architect 2026-09-29), the root's slot and the row's
            // third shift-admitting button: the plain press asks for the
            // highlighted batch folder, the shifted one (the long press) for
            // every batch folder at the root.
            case AppState::PlayerButtonAct::Delete:
                render_player_delete(/*all=*/shifted);
                return true;
            case AppState::PlayerButtonAct::None:
                return false;
        }
        return false;
    }
    // THE PICKER'S ONE BUTTON (architect 2026-08-29): its row is one button,
    // because a click on a row is the open act now — so this arm reads no
    // bit. Its word is what leaving does (kModalCancelLabel's rule,
    // app_state.h, architect 2026-10-28 night): Cancel under the project
    // picker and the value list; the settings page's row is EMPTY since
    // 2026-11-11 (architect, session 106: its Close left, the lit SETUP key
    // the way out), so no press reaches here under it (the trim presets
    // list's row is its own arm, below). Under a VALUE LIST it RETURNS TO THE SETTINGS PAGE, seated on
    // the list's key, nothing applied (return_to_settings_page, architect
    // 2026-10-28 night: the list is the page's sub-page); under the project
    // picker it runs the one close body.
    // THE TRIM PRESETS LIST'S ROW IS THE PLAYER'S SHAPE (architect
    // 2026-10-28 night): its published face decides as the player's does — a
    // key painted dead is consumed — and each act is the list's own: the
    // highlight pair the band walk alone (picker_move_highlight), Save,
    // Save As and Delete their bodies; no Close since 2026-11-11 (the lit
    // PRESETS key's lift is the list's close). No member admits a modifier.
    if (app.picker.active &&
        app.folder_overlay.owner == AppState::FolderOverlay::Owner::TrimPresets) {
        if (!b.enabled) return true;
        switch (b.trim_act) {
            case AppState::TrimPresetsButtonAct::HighlightDown:
                picker_move_highlight(+1);
                return true;
            case AppState::TrimPresetsButtonAct::HighlightUp:
                picker_move_highlight(-1);
                return true;
            case AppState::TrimPresetsButtonAct::Save:
                trim_presets_save();
                return true;
            case AppState::TrimPresetsButtonAct::SaveAs:
                trim_presets_save_as();
                return true;
            case AppState::TrimPresetsButtonAct::Delete:
                trim_presets_delete();
                return true;
            case AppState::TrimPresetsButtonAct::None:
                return false;
        }
        return false;
    }
    // THE BPM FORM'S ROW (architect 2026-10-29), the trim list's shape: its
    // published face decides — a key painted dead is consumed — RENDER its
    // body (bpm_form_render), CANCEL the form's close with the mode
    // (close_bpm_form). No member admits a modifier.
    if (app.picker.active &&
        app.folder_overlay.owner == AppState::FolderOverlay::Owner::BpmForm) {
        if (!b.enabled) return true;
        switch (b.bpm_act) {
            case AppState::BpmFormButtonAct::Render:
                bpm_form_render();
                return true;
            case AppState::BpmFormButtonAct::Cancel:
                close_bpm_form();
                return true;
            case AppState::BpmFormButtonAct::None:
                return false;
        }
        return false;
    }
    if (app.picker.active) {
        if (app.folder_overlay.owner ==
            AppState::FolderOverlay::Owner::ValueList)
            return_to_settings_page(app.folder_overlay.value_key);
        else
            close_picker();
        return true;
    }
    // THE SCORE VIEWER'S ROW (2026-10-26): the published face decides, as
    // the player's does — a key painted dead is consumed — and each act is
    // the viewer's own (score_viewer.h). THE SKIPS ARE THE ROW'S
    // SHIFT-ADMITTING PAIR (architect 2026-10-27, score_button_shift_admits):
    // `shifted` — a press held past the hold delay — is
    // the 10 s form; every other arm ignores it.
    if (app.score_viewer.active) {
        if (!b.enabled || score_viewer == nullptr) return true;
        switch (b.score_act) {
            case AppState::ScoreButtonAct::PrevPage: score_viewer->step(-1);     return true;
            case AppState::ScoreButtonAct::SkipBack:
                score_viewer->skip(/*forward=*/false, shifted);
                return true;
            case AppState::ScoreButtonAct::PlayPause: score_viewer->toggle_play(); return true;
            case AppState::ScoreButtonAct::SkipForward:
                score_viewer->skip(/*forward=*/true, shifted);
                return true;
            case AppState::ScoreButtonAct::NextPage: score_viewer->step(+1);     return true;
            case AppState::ScoreButtonAct::Reset:    score_viewer->raise_reset(); return true;
            case AppState::ScoreButtonAct::SetStart: score_viewer->set_start();   return true;
            case AppState::ScoreButtonAct::SetEnd:   score_viewer->set_end();     return true;
            case AppState::ScoreButtonAct::None:     return false;
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
    if (text_editor::is_active(app.settings_editor))
        handle_settings_editor_key(key, mods);
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
// run_marker_click_act (architect 2026-09-14); it arms no PendingMarkerPress
// and seeds or consumes no double-click (its caller does). Its clauses are the
// plain click's, in the click's order, and their arguments stay at
// run_marker_click_act, which runs the same clauses on its modified arm: the
// playback stop, the single-select, the land and the addressed cell.
// Read-only does not refuse it — a select is navigation, the click's own rule.
void GuiInputHandler::run_marker_plain_select(int hit, MarkerCell cell) {
    if (hit < 0) return;
    playback_lifecycle.stop_playback_if_playing();
    // ONE PLAIN MARKER CLICK, NO SPECIAL CASE FOR A SELECTED MEMBER
    // (architect 2026-07-29, HORIZONTAL MOVEMENT IS A FOCUS ACT — the
    // doctrine is at the head of position_nudge.h): a click on a member of a
    // 2+ selection single-selects and lands like a click on any other marker,
    // and the drag it may become is an ordinary singleton drag. The clicked
    // marker takes THE SELECTED LOOK here (its stem, render.h's THE STATES;
    // the land below puts the playhead on its frame, the focus's mark)
    // — set_single_selection publishes the change, the flag cache rebuilt
    // IN THIS EVENT with the waveform's damage
    // (Selection::publish_selection_change), so the new stem and the land's
    // playhead paint in one frame and no stem work is owed here.
    selection.set_single_selection(hit);
    // THE LAND IS THE MOVEMENT OWNER HERE, as it is on the modified arms.
    land_playhead_on_marker(app, audio, viewport, hit);
    // THE ADDRESSED CELL, written AFTER the select because the select resets
    // the axis to the payload as it seats the focus (Selection::seat_focus);
    // it paints nothing (2026-11-02), so it owes no damage.
    app.addressed_cell = cell;
}

// THE MARKER CLICK ACT — the whole flag click, AT THE PRESS (architect
// 2026-08-17: CONTENT ACTS THE MOMENT ITS IDENTITY IS CERTAIN — a flag press
// can only mean one thing, so nothing here waits for the lift; the one-day
// lift deferral of 2026-08-15 is inverted, its reasoning at
// PendingMarkerPress, app_state.h). ONE owner with TWO call sites, both in
// on_button_press's marker claims: the ctrl-exact toggle branch and the
// plain branch. BOTH ARE FLAG HITS AND NOTHING ELSE (re-grepped
// 2026-08-27): each sits behind a resolved `mh_index >= 0` inside the top
// strip, so the empty marker lane — its plain click, its double-click create —
// never reaches this body, and the `h` view's diff flags are a different press
// router with its own mode-local multi-selection. THE TOGGLE ARM HAS TWO
// PRODUCERS since 2026-08-18: a real ctrl press, and a plain press while the
// ADD TO SELECTION mode stands (the fold is at the `toggle` term below).
//
// THE SELECTION MODIFIER ACTS ON THE FLAG BOX ALONE (architect 2026-09-10:
// the bound cells "are considered outside of undo, and so in many ways
// they're a separate system"; "iterations mode is by design targeting each
// marker individually"). THE PAYLOAD IS THE MEMBERSHIP SURFACE; EVERY OTHER
// BOX TAKES THE PLAIN PRESS ONLY — a toggle press on a Lower or
// Upper box is a SILENT NO-OP, the guard below returning ahead of the stop:
// no toggle, no land, no address, no arm and no playback stop, the
// pointer's own non-event. A membership toggle addressed off a
// bound cell would build the very 2+ selection with a bound axis the cells'
// single-marker model has no meaning for. The PLAIN press on
// those boxes is untouched — single-select, address the cell, land — which is
// what makes a cell reachable at all. AND THE `k` FOLD STOPS AT THE PAYLOAD
// BOX FOR THAT REASON (architect 2026-09-19): the sticky ctrl turns a plain
// press on the PAYLOAD into the toggle and leaves the two BOUND cells' plain
// press exactly as it is — single-select, address, land — so the mode's own
// authoring surface stays reachable by pointer with both lamps lit, which is
// what admits bare `k` under grid iterations at all. The lamp therefore
// cannot produce a `toggle` for the guard below to eat, and what that guard
// still eats is the REAL ctrl press (the S Pen's barrel) this paragraph is
// about. (Bare `i`'s ON edge still PUTS ADD TO SELECTION OUT — architect
// 2026-09-12, selection_consumed — because raising the cells is an act that
// ends a selecting pass, not because the cells would be unreachable under
// it.)
// It runs the stop, the two-way selection fork, the
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
// no drag can begin under the editor it just opened (the editor owns input
// and veils the pointer, 2026-10-25, so the press's motion and lift are the
// veil's). It seeds nothing either — the family rule.
void GuiInputHandler::run_marker_click_act(int hit, int x, int y, bool ctrl,
                                           const DoubleClickCandidate&
                                               dc_at_press) {
    if (hit < 0) return;
    // WHICH CELL THE PRESS LANDED ON, from the painter's published boundaries
    // (hit_test_flag_cell; the MarkerCell block at PendingMarkerPress,
    // app_state.h), asked ONCE for the press's THREE readers: the toggle
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
    // input_key_dispatch.cpp).
    // (No shift reaches this owner: the core raises no shift on a pointer
    // event, architect 2026-10-30.)
    const bool toggle = ctrl || plain_marker_press_toggles(cell);
    // THE PAYLOAD IS THE MEMBERSHIP SURFACE, and this is the whole of that
    // rule (the head of this body argues it): a toggle press — on either of
    // its producers — landing on any box but the flag is a silent no-op. It
    // returns ABOVE THE STOP deliberately: a
    // press that does nothing must not stop the audition either, which is
    // what "the pointer's non-event" means everywhere else in this file. The
    // plain press falls straight through, so a cell keeps its select, its
    // address and its land.
    if (toggle && cell != MarkerCell::Payload) return;
    // THE PLAIN ARM IS ITS OWN BODY since 2026-09-14 (run_marker_plain_select,
    // above — the stop, the single-select, the land and the addressed cell);
    // the toggle arm keeps the same four clauses here.
    if (!toggle) {
        run_marker_plain_select(hit, cell);
    } else {
        // The stop leads on every shape THAT ACTS: selecting or editing under a
        // live audition is the case the top-strip stop exists for, and no arm
        // below refuses (read-only still selects and lands, and the index came
        // from a live hit test).
        playback_lifecycle.stop_playback_if_playing();
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
        // THE ADDRESSED CELL RIDES THE PRESS (architect 2026-09-04, the iteration
        // bound cells; every cell since 2026-09-05): the cell the press landed
        // on becomes the focus's addressed cell — the one the arrows step, the
        // one Enter opens — on BOTH click shapes, because the press already
        // selects and lands on every shape and the axis is one more thing the
        // press says. Written AFTER the fork, because every mutator above resets
        // the axis to the payload as it seats the focus (Selection::seat_focus),
        // and a press is one of the TWO routes that name a cell — the bound
        // editor's open is the other, writing behind its own selection write;
        // a focus reached by neither (the walk included, 2026-11-02) is
        // addressed at its payload. Read-only does not refuse
        // it: the axis is navigation, as the selection is, and the act it
        // addresses meets the lock at its own gate. The axis paints nothing
        // (2026-11-02, render.h's THE STATES), so it owes no damage.
        app.addressed_cell = cell;
    }
    // THE TOGGLE CLICK ENDS HERE: it has no double-click meaning and no drag
    // to become, and its click has just committed whole — so it arms
    // NOTHING, exactly as before the one-day lift model (an armed marker
    // pending is plain by construction).
    // THE MODE REACHES THIS RETURN THROUGH `toggle`, and that is INTENDED
    // rather than tolerated (2026-08-18): while Add to selection stands, a
    // plain press is a ctrl press in every respect, so it must arm neither the
    // reposition DRAG nor the double-click SEED — a marker cannot be dragged
    // and a flag editor cannot be double-clicked open while the mode is
    // accumulating a selection, exactly as neither can be under a held ctrl.
    // Turning the mode off restores both in the same press.
    if (toggle) return;
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

// ARM THE ONE SURVIVING DEFERRED CLICK — the trim bar's ctrl bound set (the
// contract, and why it alone still defers, is at PendingClickAct, app_state.h:
// its press IS the endcap drag's arm, the one click with genuine press
// ambiguity — the 2026-08-17 ruling took the other four kinds back to the
// press). It writes the pending and does NOTHING ELSE: the act is the lift's,
// or the crossing's.
//
// THE ARM IS UNCONDITIONAL BY SHAPE. Every gate the set meets — the
// on-its-own-cap no-op, the partner wall, the degenerate-geometry returns —
// lives INSIDE the
// act and is therefore re-asked LIVE at the lift, which is the chrome lift's
// own rule: a gate may change under a held button and the LIFT decides.
// Nothing is carried but the press POINT: which bound the click writes is
// decided where the act runs, the end cap nearest the press column on the
// painted columns
// (trim_bound_click, input_trim.cpp; architect 2026-11-10 — no clock is
// read, a long press meaning nothing on the trim bar).
// THE POINT'S BASIS DOES NOT MOVE UNDER IT: the armed record is a member of
// the displayed-basis freeze (displayed_basis_frozen, app_state.h —
// architect 2026-09-24), because the lift and the crossing both convert the
// stored press_x through the displayed map, the crossing arming the trim drag
// at it too — so the gates are live and the aim's geometry is the epoch the
// user pressed in, like every other aimed press.
void GuiInputHandler::arm_pending_click_act(int x, int y) {
    app.pending_click = PendingClickAct{};
    app.pending_click.kind     = PendingClickKind::TrimBoundSet;
    app.pending_click.press_x  = x;
    app.pending_click.press_y  = y;
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
    // The barrel bound set — the END CAP NEAREST THE PRESS COLUMN, the
    // act's own verdict (architect 2026-11-10) — WHOLE AND UNSPLIT:
    // set_trim_bound_at_click owns every refusal (a degenerate
    // audio/geometry state, a click exactly on its cap and the partner
    // wall, each of which writes
    // nothing, deselects nothing and stops nothing), the playback stop that sits
    // past those refusals, the write, the commit tail (the crossed
    // reset, the repaint and the target trigger; no playhead write) and the
    // setter's deselect.
    // Moving the act meant moving that unit, never a piece of it.
    (void)set_trim_bound_at_click(press.press_x);
}

// -- THE ON-SCREEN KEYBOARD'S TWO EDGES (2026-08-27) -------------------------
//
// Contracts, the placement rule and the act-at-the-press ruling are at the
// declarations (input_handler.h); the layout table, the geometry walk and the
// two lamps are at onscreen_keyboard.h. What is here is the press's own
// dispatch and the release's owed key-up.

bool GuiInputHandler::claim_onscreen_keyboard_press(int x, int y,
                                                    GuiInputState mods) {
    if (!onscreen_keyboard::stands(app)) return false;
    const GuiRect surf = onscreen_keyboard::surface_rect(app);
    // THE CLAIM IS THE BAND (onscreen_keyboard::claim_rect; with the taskbar
    // overlay's line above it, 2026-10-10 to 2026-10-14): row 8 and the
    // waveform under the band answer nothing. CONSUMED FROM HERE, whatever it
    // lands on — the surface is opaque to the pointer.
    if (!rect_contains(onscreen_keyboard::claim_rect(app), x, y)) return false;

    // THE SESSION-CHANGE OWNER RUNS FIRST, ahead of the hit test and ahead of
    // every read below (the contract is at its declaration): a close and a
    // reopen, or a flag-editor RETARGET, can both complete inside one drained
    // input batch with no tick between them, and this press must not be routed
    // against lamps the previous edit armed. It damages the band on a real
    // change and does nothing at all otherwise.
    onscreen_keyboard::reconcile_session(app, viewport);

    // THE BARREL HAS NO ROAD ON THE KEYBOARD (architect 2026-11-08, session
    // 94, extending the chrome ruling at chrome_band_modifiers_refused: "a
    // long press will do just fine ..., whereas a barrel click is awkward"):
    // A PRESS CARRYING THE BARREL'S CTRL IS CONSUMED BEFORE THE HIT TEST, on
    // every key — the letters, Bksp, Shift, the page key, Copy, Paste, Enter,
    // Esc, Tab, LEFT, RIGHT, the pass key, the space bar — exactly as a press
    // on a gap is: nothing typed,
    // no lamp toggled, no held key (so no press face and no key-up owed at
    // the lift), the band's refusal's shape. Copy and Paste keep their OWN
    // ctrl on an ordinary tap (`carried`, below), and that is the only ctrl
    // a key's press carries (a barrel edge under a held key's repeat only
    // disarms it: no ctrl spelling of a repeating key is the editors', so the
    // repeat's live re-probe fails, GuiInputCore::maybe_fire_repeat). The
    // waveform's barrel roads read the platform's bit at their own owners,
    // untouched.
    if (mods.ctrl) return true;

    // The lamps, read ONCE: the page decides which key is under the finger and
    // the shift latch decides what that key types, so both must be the same
    // answer the last paint used.
    const AppState::OnscreenKeyboard& kb = app.onscreen_keyboard;
    const onscreen_keyboard::Page page          = kb.page;
    const bool                    shift_latched = kb.shift_latched;

    onscreen_keyboard::KeyDef def{};
    const int hit = onscreen_keyboard::key_at(app, page, x, y, def);
    if (hit < 0) return true;   // a gap, the margin: consumed, no key

    using Role = onscreen_keyboard::Role;

    // THE TWO STATE CONTROLS — Shift and the page key, both wearing lamps —
    // ACT ON THE SURFACE AND SYNTHESIZE NOTHING. They still take the held
    // index with a keysym of 0, which is what the release reads as "this key
    // owed no key-up".
    if (def.role == Role::Shift || def.role == Role::SymbolMode) {
        app.onscreen_keyboard.pressed_key    = hit;
        app.onscreen_keyboard.pressed_keysym = 0;
        if (def.role == Role::Shift) {
            // A LATCH (architect 2026-11-08, sessions 95 / 96, retiring the
            // one-shot arm of 2026-08-27): a press lights it, a second press
            // puts it out, and NOTHING ELSE inside one edit touches it — no
            // letter spends it. Lit, the letters type capital (shifted_char)
            // and LEFT / RIGHT carry its shift (the selection's extend,
            // below). The edit ENDING clears it, and belongs to the
            // session-change owner (onscreen_keyboard.h).
            app.onscreen_keyboard.shift_latched = !shift_latched;
        } else {
            // THE PAGE KEY LEAVES THE LATCH STANDING: a round trip through
            // the symbols and back finds it exactly where it was left, and on
            // the symbols page it still extends the selection. Where the key
            // goes is the layout's own answer (page_after).
            app.onscreen_keyboard.page =
                onscreen_keyboard::page_after(def.role, page);
        }
        // BOTH REPAINT THE WHOLE SURFACE: the latch's edge moves every letter
        // cap's case and a page change re-letters every letter slot.
        viewport.invalidate_rect(surf);
        return true;
    }

    // AN ORDINARY KEY. Resolve what it types from the layout table's own two
    // derivations — never a second list — and hand it to the platform's key
    // door with the key's PLACE as the core's stable per-key identity.
    GuiKey         keysym    = 0;
    uint32_t       codepoint = 0;
    // The modifiers the key carries itself (GuiCarriedMods): Copy's and
    // Paste's ctrl, LEFT's and RIGHT's latched shift (below).
    GuiCarriedMods carried{};
    switch (def.role) {
        case Role::Character: {
            const char32_t typed = onscreen_keyboard::shifted_char(
                def.ch, shift_latched);
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
        // A bare Tab, no modifier: the dialog editors complete on it (the
        // one autocomplete model, route_modal_editor_key) and the editor's
        // ring walks on it where there is nothing to complete.
        case Role::Tab:       keysym = GuiKeys::Tab;       break;
        // COPY AND PASTE ARE THE EDITORS' OWN CHORDS (architect 2026-10-29):
        // the letter's keysym with the key's own ctrl riding the event
        // (synthesize_key's `carried`, GuiInputCore::key_event's contract),
        // so text_editor::handle_key's Ctrl+C and Ctrl+V arms answer them
        // exactly as they answer any other key's chord — a copy with no selection
        // copying nothing — and every gate and repeat rule on the road reads
        // the chord. A chord types no character, so the codepoint stays 0.
        case Role::Copy:
            keysym        = GuiKeys::C;
            carried.ctrl  = true;
            break;
        case Role::Paste:
            keysym        = GuiKeys::V;
            carried.ctrl  = true;
            break;
        // LEFT AND RIGHT ARE THE EDITORS' CARET KEYS (architect 2026-11-08,
        // sessions 95 / 96): GuiKeys::Left / Right on the ordinary road — the
        // keyboard-modal gate admits them under an editor, so no main-page
        // Left / Right binding can fire — CARRYING THE LATCHED SHIFT when the
        // lamp is lit (the selection's extend, text_editor::handle_key's
        // caret arm), the shift riding every repeat of a held key
        // (GuiInputCore::key_event's contract).
        case Role::Left:
            keysym        = GuiKeys::Left;
            carried.shift = shift_latched;
            break;
        case Role::Right:
            keysym        = GuiKeys::Right;
            carried.shift = shift_latched;
            break;
        // THE PASS KEY IS A KEY ON THE ONE ROAD (architect 2026-11-08,
        // sessions 95 / 96): a key event carries one codepoint, never a
        // string, so the key synthesizes its own keysym, GuiKeys::Pass, and
        // the EDITOR answers it with the text `pass` through the one incoming
        // filter (text_editor::handle_key's pass arm) — so it meets the same
        // modal gate, the same session, the same byte cap and card as every
        // other key, rather than the press router writing into an editor's
        // buffer behind the key path. (No editor's text is on the undo
        // stack, so there is no coalescing for it to join.) One-shot.
        case Role::Pass:      keysym = GuiKeys::Pass;      break;
        case Role::Shift:
        case Role::SymbolMode: return true;   // handled above; unreachable
    }

    // THE HELD KEY wears the press face until the lift (the press arc,
    // architect 2026-11-05, paint_onscreen_keyboard): the index is the
    // release's subject and the painter's held slot; its own rect damaged
    // here (the lamp keys above damage the whole band).
    app.onscreen_keyboard.pressed_key    = hit;
    app.onscreen_keyboard.pressed_keysym = keysym;
    viewport.invalidate_rect(onscreen_keyboard::key_rect(app, page, hit));

    // NO KEY SPENDS THE SHIFT LATCH (architect 2026-11-08, sessions 95 / 96):
    // a capital letter, a LEFT / RIGHT extend and every other key leave it
    // lit, so nothing here writes it or owes the band its damage — the
    // latch's own two edges are the Shift press above and the session change.
    //
    // THE REPEAT KEEPS THE CASE AND THE SHIFT IT WAS PRESSED WITH: the PRESS'S
    // CODEPOINT IS THE KEY EVENT'S IDENTITY, so a held `q` pressed with the
    // latch lit repeats `Q` for the whole hold, exactly as a physical Shift+Q
    // hold repeats Q with the shift key still down — the backend's codepoint
    // table (platform_android.cpp's synthesize_key) re-answers each
    // synthesized repeat, deliberately not re-derived against the live lamp —
    // and a held LEFT / RIGHT's carried shift rides every fire
    // (GuiInputCore::key_event's contract).

    // THE ACT, at the press. Everything downstream is the ordinary key path.
    gui.synthesize_key(keysym, static_cast<uint32_t>(hit), /*pressed=*/true,
                       codepoint, carried);
    return true;
}

bool GuiInputHandler::finish_onscreen_keyboard_release() {
    const int held = app.onscreen_keyboard.pressed_key;
    if (held < 0) return false;
    const GuiKey keysym = app.onscreen_keyboard.pressed_keysym;
    app.onscreen_keyboard.pressed_key    = -1;
    app.onscreen_keyboard.pressed_keysym = 0;
    // THE UN-PRESS DAMAGE (2026-11-05): the held key's own rect, looked up
    // on the page its index belongs to — the page key's own act has moved
    // the live page (onscreen_keyboard::page_of_key_index) — while the
    // keyboard still stands (a press whose act closed the editor leaves no
    // key to repaint; the band's teardown repaints the slot).
    if (onscreen_keyboard::stands(app)) {
        viewport.invalidate_rect(onscreen_keyboard::key_rect(
            app, onscreen_keyboard::page_of_key_index(held), held));
    }

    // The owed key-up. A lamp key synthesized nothing and owes nothing; every
    // other key owes exactly this, and the core's repeat cancel is the reason
    // it may not be skipped (contract at GuiInputCore::key_event: the arm dies
    // on the matching stable code and on nothing else this path can reach).
    // A release carries no character and no modifier, exactly as a physical
    // one does not (Copy's and Paste's ctrl, LEFT's and RIGHT's shift, were
    // the press's alone).
    if (keysym != 0) {
        gui.synthesize_key(keysym, static_cast<uint32_t>(held),
                           /*pressed=*/false, /*codepoint=*/0,
                           /*carried=*/GuiCarriedMods{});
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
// ONE PRESS ROUTER, SIX CONTENTS. The claim below stands ABOVE the two mode
// veils (the player's and the picker's, the picker's serving all five of
// its contents), each of which admits exactly the band and its own modal row, and
// BELOW the prompt gate, which outranks every surface as it always has. What
// the OPEN means is the owner's, and the two forks below are the whole of it
// for the rows: nothing else in this file asks which content fills them (the
// opener key's veil exemption asks is_folder_overlay_opener).

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
        case AppState::FolderOverlay::Owner::SettingsPage:
        case AppState::FolderOverlay::Owner::ValueList:
        case AppState::FolderOverlay::Owner::TrimPresets:
        case AppState::FolderOverlay::Owner::BpmForm:
            picker_set_highlight(index);
            return;
    }
}

// THE OPEN ACT (the row click's own second half and Enter on the highlight).
// Under the player a folder enters and a wav plays; under the project picker
// the row's project is reopened; under the settings page (2026-10-28) the
// page closes and the settings editor opens prefilled on the row's line (its
// OK and Cancel reopening the page on that row, handle_settings_editor_key),
// or the row of a key with a value list swaps that list in; under the value
// list (2026-10-25, four keys more 2026-10-28 night) the row's value is
// committed to the listed key and the settings page returns, seated on that
// key's row; under the trim presets list (2026-10-28 night) the row's preset
// becomes the active tab's trim, the list standing; under the BPM form
// (2026-10-29) the form closes and the line editor opens prefilled on the
// row's line (its OK and Cancel reopening the form on that row) — one body
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
        case AppState::FolderOverlay::Owner::SettingsPage:
            settings_page_commit(index);
            return;
        case AppState::FolderOverlay::Owner::ValueList:
            value_list_commit(index);
            return;
        case AppState::FolderOverlay::Owner::TrimPresets:
            trim_presets_apply(index);
            return;
        case AppState::FolderOverlay::Owner::BpmForm:
            bpm_form_commit(index);
            return;
    }
}

bool GuiInputHandler::claim_folder_overlay_press(
        int x, int y, GuiInputState mods) {
    if (!folder_overlay::stands(app)) return false;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (!rect_contains(surf, x, y)) return false;
    // CONSUMED FROM HERE: the band is opaque to the pointer. A MODIFIED
    // press is a consumed no-op — nothing on a row reads shift or ctrl, and
    // nothing on a row reads kHoldBeatMs either: the load is the modal row's
    // own button (strict modifier validation's answer, and the roster's
    // shift-hold is elsewhere). A press on the pad or on a gap between rows
    // arms nothing — the claim is this SURFACE rect (the modal family's
    // panel), while row_at contains against the content rect (the box's
    // interior inside its outer rows and columns of ground, folder_overlay.h),
    // so those, the margins,
    // the foot and the pad are claimed and inert by construction at every
    // scroll offset.
    //
    // A MODIFIED PRESS ON A ROW IS SILENT LIKE ONE ANYWHERE ELSE ON THE BAND
    // (architect 2026-08-30, the unbound-keys ruling read on the pointer:
    // "bound keys either show an effect or a card, so an unbound key is
    // identified by its silence" — a gesture nothing binds is answered the
    // same way). It carded "Rows open on a plain click" for the one day of
    // 2026-08-30, on the ROW alone; the row test the card needed is gone with
    // it, the modified press claiming the band and arming nothing wherever it
    // lands.
    if (mods.ctrl) return true;
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
    // THE ARM: the row wears the lists' press pair while the press stands
    // on it (paint_folder_overlay; the press arc, architect 2026-11-05) —
    // the row damaged here, at every inside crossing (the motion below) and
    // at the lift (finish_folder_overlay_release); an inert row's arm paints
    // nothing, the painter's own gate.
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
        // (max(|dx|,|dy|)) against drag_moved_threshold_px() — 9 W through
        // scaled_px, the touch slop's own number — and the crossing is
        // `>=`, not `>`, because the core resolves a touch into a drag at `>=`
        // its slop and the two gates must not disagree by one pixel (the
        // twin-gate invariant, input_core.h's set_touch_slop_px: a finger's
        // travel means one distance whichever side decides it).
        //
        // IT CROSSES ON EITHER AXIS even though the scroll reads dy alone. A
        // sideways slide over a row is a drag, not a click: on glass a finger
        // that has slid 20 px across a full-width row has been classified as a
        // drag by the touch layer already, and the pen's wobbling tap is the
        // same event. Past the gate the arm IS the band's scroll drag and
        // its act is gone — once a drag, always a drag, on both axes — so a
        // purely sideways crossing simply scrolls nothing and ends with no act,
        // which is the answer Plasma's single-click and GNOME's touch both give
        // (the ruling's own references).
        if (std::max(std::abs(x - press.press_x),
                     std::abs(y - press.press_y)) >=
                drag_moved_threshold_px()) {
            // The arm IS the scroll drag now: no row is pressed (the press
            // pair leaves it), the surface repainted whole.
            press.scrolling = true;
            press.inside    = false;
            viewport.invalidate_rect(folder_overlay::surface_rect(app));
        } else {
            // THE FEINT'S BIT (2026-11-05): the row under the press wears
            // the press pair while the pointer is on it, each crossing
            // damaging the row.
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
    // can see it named. A lift elsewhere is a consumed nothing, and so is a
    // lift on an INERT row (folder_overlay::row_is_inert, the settings page's
    // group rows, architect 2026-10-28, and its disabled `scale` and `bpm`
    // lines, 2026-10-29): neither half, the row being no choice.
    // NOTHING IS SEEDED HERE ANY MORE: the row's double-click surface went
    // with the ruling, no second meaning being left for a second press.
    if (folder_overlay::row_at(app, x, y) != press.row) return true;
    if (folder_overlay::row_is_inert(app, press.row)) return true;
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

bool GuiInputHandler::claim_notification_press(int x, int y) {
    // The hit owner has already asked the LIVE stack (notifications.h): a
    // published rect whose card has LEFT that stack — by its expiry, by a
    // dismissal or by the bump — answers 0 here (a live card that is merely
    // clipped is selected wherever it has a published rect, and past the
    // room's foot it has none), so this press is claimed by NOTHING and falls
    // through to the surface underneath — the pixels the user is about to see
    // there at the next paint, which is the honest place for it to land.
    const uint64_t id = notification_card_at(app, x, y);
    if (id == 0) return false;
    // Consumed from here: the card is opaque.
    // THE CARD IS ONE BUTTON (architect 2026-10-01, "whole card dismisses, X
    // gone"): the press ARMS it and dismisses nothing — the act is the lift's
    // (finish_notification_release), the one stated exception to "every
    // dismissal stays at the press" (AppState::ChromePress's head). The arm
    // carries the press's stamp, the long press's road to the whole-stack
    // dismissal (the core raises no shift on a pointer press, architect
    // 2026-10-30), and nothing else: ctrl binds nothing on a card. No arm can
    // be standing here — a press exists only after the previous one came up,
    // and every release takes the arm.
    app.chrome_press = AppState::ChromePress{
        .kind     = AppState::ChromePress::Kind::Card,
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
// called by on_button_release at the claim's own rank (above every veil's
// release) with the arm already taken, so the card's clock
// has resumed before this runs (take_chrome_press) and a dismissal finds it
// running or gone. The lift dismisses iff it lands ON THE SAME CARD the
// press armed — re-hit at the release's own coordinates against the last
// paint's publication and the live stack (notification_card_at), the derive
// doctrine: a lift beside the card, on another card, or on a card bumped or
// cleared under the hold lands nothing (the chrome's slide-away cancel).
// THE TERM IS THE CHROME SHIFT LONG PRESS'S: a hold past
// chrome_shift_hold_ms(), measured here against the arm's stamp — no timer,
// no tick, no visual announcement — and with it the lift dismisses EVERY
// card, criticals included; without it, this card alone.
void GuiInputHandler::finish_notification_release(
        const AppState::ChromePress& arm, int x, int y) {
    if (arm.kind != AppState::ChromePress::Kind::Card) return;
    if (notification_card_at(app, x, y) != arm.card_id) return;
    const bool all =
        monotonic_ms() - arm.press_ms >= chrome_shift_hold_ms();
    if (all) notifications.dismiss_all();
    else     notifications.dismiss(arm.card_id);
}

// (THE CAPTION AND THE SIZING FRAME's claims — claim_caption_press,
// finish_caption_release and claim_window_frame_press, the app's own title
// bar's three buttons, its drag and double click and the restored window's
// resize, 2026-10-05 — went 2026-10-24 with the caption and the frame.)

void GuiInputHandler::on_window_close() {
    // The window close (Android's Back, the one quit road) routes
    // through the unsaved-work dialog when dirty. END any
    // in-flight pointer
    // gesture before the prompt goes up: while the prompt is up the pointer
    // handlers swallow motion
    // and release, so a gesture left alive would commit on the next motion
    // if the user dismisses the prompt. ENDING IS COMMITTING (pointer
    // gestures have no cancel — the rule is at on_key's drag-modal statement
    // in input_handler.cpp): each gesture runs its own release body here.
    // finalize_active_drags is a no-op when nothing is live, so the clean and
    // non-gesture close paths are unaffected. A live editor text-selection
    // drag is finalized there too (collapsed to a caret, selection-only,
    // nothing to revert), so there is no motion-free interval where it would
    // swallow keys until a later pointer motion noticed the lost button.
    finalize_active_drags();
    // THE HINT GOES DOWN WITH IT, and this is the SAME RULE AS on_key's
    // MODAL-OPENING END rather than a new one: no floating hint stands over a
    // modal. Every KEYBOARD opener meets it at on_key's every return and every
    // pointer opener at its press; the platform's close is THE ONE modal
    // opener that arrives asynchronously — it carries no key and no pointer
    // event to hide with — so the rule needs its call here (a HARD end) or
    // the hint stands over the prompt until the tick's walk finds no owner
    // under it. (The checkpoint worker's
    // failure report was a second such opener from 2026-08-07 until
    // 2026-08-09, when it became the bottom row's paint-only critical slot and
    // stopped raising anything; it is a critical notification card since
    // 2026-08-29, which raises no modal either.)
    // Ordered ABOVE request_close so the box's published rect is
    // damaged before the prompt's own repaint.
    hide_shift_tooltip();
    // (THE POPUP WENT DOWN HERE BEFORE THE PROMPT WENT UP, close_dropdown,
    // until the Settings dropdown retired, 2026-10-21.)
    // THE RENDER PLAYER, THE PICKER AND EVERY STANDING MODAL EDITOR GO
    // DOWN INSIDE request_close, not here: the close road owns those
    // steps for every road into it, so neither a mode
    // nor an editor — nor either overlay band — is left standing under
    // the unsaved-work prompt.
    prompt.request_close(GuiCloseTarget::Exit);
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
// (render_player_scrub_x_of / _frame_at, app_state.h), whose travel runs the
// thumb's center from the channel's first column to its last (2026-10-25).
// THE THUMB DRAGS, WINDOWS' TRACKBAR (architect
// 2026-10-02 — the fix for "the thumb doesn't drag"; the drag itself stood
// since the player's birth, but only from the handle's own box, and a press
// on the track elsewhere seeked at the press and armed nothing): EVERY PRESS
// ON THE ITEM ARMS THE THUMB DRAG. On THE THUMB'S GRAB BAND — the painted
// thumb, its overhang past the channel's ends included, through the one test
// (render_player_scrub_handle_hit) — the thumb is TAKEN WHERE IT IS, the grab
// offset kept so it does not jump under the pointer; ANYWHERE ELSE on the
// item's band the thumb JUMPS to the press's column and the same press
// continues as the drag. Either way the thumb's painted x follows the
// pointer with nothing seeked, and the RELEASE commits the seek to the
// thumb's carried column, the product's deferred-click shape — so a tap on
// the track seeks at the lift, and a motionless tap on the thumb itself
// seeks nothing (Windows: clicking the thumb moves nothing).
// THE SOUND PAUSES WHILE THE SCRUB IS HELD — THE ONE RULE OF BOTH SCRUBS,
// this one and the score viewer's below (architect 2026-10-26 ~23:15: "we
// should stop playback when dragging the thumb, in part because it's less
// truthful. The thumb is no longer at that place"): a LIVE item is paused at
// the press (GuiRenderPlayer::pause, the key reading PLAY while held) and
// THE LIFT RESUMES IT AT THE DROPPED COLUMN (GuiRenderPlayer::resume_at,
// architect 2026-10-27): the item the press paused, LIVE again from the
// dropped frame, clamped as seek_to's live arm clamps (a target in the
// item's last frame stopping one short), and NEVER THROUGH THE PLAY KEY'S END
// RULE, which is the key's alone — so the sound plays on exactly where the
// thumb dropped, a grab taken as the item finished never walks to the next
// wav before the drop is read, and A DROP AT THE END REACHES THE NATURAL END
// at once, whose answer is the one rule's (architect 2026-10-26 ~23:55: the
// next track where there is one, else the rest dead at the item's end —
// never the PLAY key's replay from the start). A motionless tap on the thumb
// of a live item pauses at the press and resumes from the pause's own point
// at the lift, nothing seeked, a no-op in effect. A PAUSED item arms as it
// stands, the lift's seek moving its resume point, and stays paused; a hard
// end owes nothing, the item left PAUSED (clear_player_scrub_drag).
// THE TIMESTAMP FOLLOWS THE THUMB (architect 2026-11-10, both scrubs): while
// the drag stands the clock's position field shows THE TIME THE DROP WOULD
// LAND AT — the lift's own derivation (render_player_scrub_drop_frame,
// app_state.h, which the lift below and the painter both ask; the score's
// score_scrub_drop_frame), the item's position under a motionless grab of
// the thumb — and nothing else changes: no sound, no seek. EVERY EDGE THAT
// MOVES THAT ANSWER DAMAGES THE CLOCK'S RECT beside the slider's
// (invalidate_scrub_clock): the press (an off-thumb press's jump), each
// motion that moves the thumb, the lift, and the hard end, where the field
// returns to the item's position.

// THE CLOCK'S RECT AS PUBLISHED (AppState::ModalDialogGeometry::clock, the
// tick's own per-position damage; zero on the score's page road, whose row
// has no clock), damaged for the held scrub's timestamp (the block above).
static void invalidate_scrub_clock(Viewport& viewport, const AppState& app) {
    const GuiRect clock = app.modal_dialog.clock;
    if (clock.w > 0 && clock.h > 0) viewport.invalidate_rect(clock);
}

// THE DRAG'S CARRIED COLUMN, clamped onto the HANDLE'S TRAVEL — the one
// expression its two writers (the press's arm, the motion) share, so the
// painted handle sits inside its own track at every point of a drag and the
// release's seek reads the same span the mapping does.
int GuiInputHandler::clamp_player_scrub_marker_x(int x) const {
    const GuiRect track = app.modal_dialog.scrub;
    const int x0   = render_player_scrub_travel_x0(track);
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
    // Consumed from here; a ctrl press does nothing on the track. That
    // one stays SILENT (a modified press on the band's rows is silent too —
    // the overlay's ruled pad-and-gap silence, notifications.h): the answer is
    // that the plain press works, and a chord on a slider is not an act
    // anyone spelled.
    if (mods.ctrl) return true;
    // THE NO-ITEM REFUSAL IS SILENT (architect 2026-08-31): it is the seek's
    // own, met here instead of at seek_to because the press must not ARM the
    // thumb drag either, and it went silent with seek_to's — a slider resting
    // at the left end under a zeroed clock IS the state it names, so a
    // sentence only repeated what the press was already looking at (the
    // retirement record is at render_player.h, where the shared sentences
    // used to be spelled). The thumb STANDS there with no item since
    // 2026-10-25 (render.h's scrub block), painted at frame 0; a press on it
    // or on the channel is consumed here and seeks nothing. IT IS ALSO THE
    // SCRUB'S REST WHILE THE TRANSPORT IS IDLE (architect 2026-08-29,
    // Audacious's own slider — dead while stopped), IDLE being no item bound
    // (the invariant at AppState::RenderPlayer::transport).
    if (app.render_player.frames <= 0 || app.render_player.item.empty())
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
    // centre past either end of that travel would hang the thumb off its
    // channel (the
    // travel is the mapping's, one owner: render_player_scrub_usable_span).
    drag.marker_x = clamp_player_scrub_marker_x(x - drag.grab_dx);
    // THE SOUND PAUSES WHILE THE SCRUB IS HELD (the block head): a live item
    // paused here, its resume point the engine's position; a paused one arms
    // as it stands.
    drag.was_live = app.render_player.transport ==
                    AppState::RenderPlayer::Transport::Live;
    if (drag.was_live) render_player.pause();
    viewport.invalidate_rect(track);
    invalidate_scrub_clock(viewport, app);
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
    // The timestamp follows the thumb (the block head).
    invalidate_scrub_clock(viewport, app);
}

bool GuiInputHandler::finish_player_scrub_release(int x, int y) {
    (void)y;
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return false;
    const AppState::RenderPlayer::ScrubDrag ended = drag;
    drag = AppState::RenderPlayer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
    // The position field leaves the drop's time for the item's position
    // (the block head).
    invalidate_scrub_clock(viewport, app);
    // A MOTIONLESS TAP ON THE THUMB SEEKS NOTHING: the thumb was taken where
    // it stood and never moved, and re-seeking its own column would round the
    // position to that column's frame. THE COMMIT otherwise is the thumb's
    // column at the lift — the lift's x less the grab offset, clamped onto
    // the travel by the mapping, which is the column the motion last painted
    // the thumb at. BOTH ARE THE DROP'S ONE DERIVATION
    // (render_player_scrub_drop_frame, app_state.h), the answer the clock's
    // position field showed under the drag.
    const std::optional<int64_t> drop =
        render_player_scrub_drop_frame(app, ended, x - ended.grab_dx);
    // THE SOUND PLAYS ON FROM THE DROP where the press paused a live item
    // (the block head): resume_at, the item the press paused LIVE again from
    // the dropped frame — or from the pause's own point under a motionless
    // tap — and NEVER THE PLAY KEY'S END RULE (architect 2026-10-27): a grab
    // taken as the item finished paused it at its end, and the key's rule
    // would walk to the next wav before the drop was read; the drop decides
    // through the live seek's clamp instead (a target in the last frame
    // stops one short, so the natural end takes over — architect 2026-10-26
    // ~23:55). A CAR TOGGLE MID-DRAG that set the item sounding again
    // (render_player.h's car block) leaves it LIVE, and the drop is then
    // seek_to's live arm, the same clamp. Where the press found the item
    // paused, the seek moves the resume point (seek_to's PAUSED arm) and the
    // item stays paused. THE ASYMMETRY WITH THE SCORE'S LIFT IS THE CAR'S:
    // nothing sets the score's soundtrack sounding under a held scrub.
    if (ended.was_live && app.render_player.transport !=
                              AppState::RenderPlayer::Transport::Live) {
        render_player.resume_at(drop ? *drop
                                     : app.render_player.resume_frame);
    } else if (drop) {
        render_player.seek_to(*drop);
    }
    return true;
}

void GuiInputHandler::clear_player_scrub_drag() {
    AppState::RenderPlayer::ScrubDrag& drag = app.render_player.scrub;
    if (!drag.armed) return;
    // A LOST LIFT IS NOT A COMMIT (architect 2026-10-26 ~23:15, the score
    // scrub's own choice, the two scrubs one rule): nothing seeked and
    // nothing resumed — a live item the press paused stays PAUSED where the
    // press left it, the key reading PLAY, and the clock's position field
    // returns from the drop's time to the item's position (its rect damaged).
    drag = AppState::RenderPlayer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
    invalidate_scrub_clock(viewport, app);
}

// -- THE SCORE VIEWER'S SCRUB (2026-10-26; the contract at the declaration,
//    input_handler.h), ON ITS TWO FACES (score_viewer.h's head), ONE RULE:
//    THE PICTURE FOLLOWS THE THUMB, THE SOUND WAITS FOR THE LIFT (architect
//    2026-10-27 ~19:45: "the picture follows the thumb, the sound waits for
//    the lift" — the waveform's playhead drag's own rule). BOTH ARM AS THE
//    RENDER PLAYER'S SCRUB ARMS: the thumb taken where it stands on its grab
//    band, the grab offset kept, jumping to a press anywhere else, and
//    CARRIED by the motion (clamp_player_scrub_marker_x, the travel's one
//    owner), the painter drawing it at the carried column while armed. THE
//    PAGE UNDER THE CARRIED COLUMN IS SHOWN LIVE, DISPLAY ONLY
//    (score_scrub_page_under, the one mapping per face; GuiScoreViewer::
//    display_page): at an off-thumb press and at every motion, nothing
//    seeked, nothing written — a press on the thumb takes
//    it at the shown page and shows nothing new. UNDER THE SOUNDTRACK THE
//    CLOCK'S ELAPSED FIELD SHOWS THE TIME THE DROP WOULD LAND AT (architect
//    2026-11-10; score_scrub_drop_frame, the lift's own derivation, the
//    play-scrub's block head), the cursor untouched; the page road's cell
//    reads the shown page. On the page road the pages
//    are the steps (architect 2026-10-27: "the thumb follows the pen
//    continuously, the page under it changes in steps; the lift commits the
//    page under the thumb"); under the soundtrack the page is the one holding
//    the column's frame. THE LIFT COMMITS: on the page road the position
//    written once (write_page_position) where the shown page differs from the
//    page the press found, the thumb then resting at the shown page's column
//    (score_scrub_x_of) — the step; under the soundtrack THE SEEK AT THE LIFT
//    to the column the motion last painted the thumb at (architect
//    2026-10-26, "no scrubbing": audio jumping under the drag is not
//    helpful), the position written with it, the seek's page the page the
//    drag showed (the same column, the same frame, the same page), so
//    nothing changes at the drop. THE SOUND PAUSES WHILE THE SCRUB IS HELD
//    (architect 2026-10-26 ~23:00: "when I'm scrubbing, I would expect the
//    audio to stop playing because I'm no longer at the scrub position. And
//    then it wouldn't start playing until I commit by dropping the thumb"): a
//    LIVE transport is paused at the press (GuiScoreViewer::pause, the key
//    reading PLAY while held) and the lift PLAYS ON FROM THE DROPPED COLUMN
//    (GuiScoreViewer::play_from, architect 2026-10-27: the live seek's clamp,
//    never the PLAY key's end rule), so a drop at the range's end reaches
//    the natural end and rests there (architect 2026-10-26 ~23:55: "it stops
//    dead where it is"), never the PLAY key's replay from the start; a
//    motionless tap on the thumb of a live transport pauses at the press and
//    plays on from the pause's cursor at the lift, nothing seeked, a no-op in
//    effect. A HARD END COMMITS NOTHING on either face: the page the press
//    found is shown again, nothing written, seeked or played. NOTHING ELSE
//    MOVES THE PAGE UNDER A HELD SCRUB: the tick's page follow runs only
//    while LIVE, which the press ended, and the keyboard belongs to the
//    gesture (the score viewer's key gate, input_handler.cpp). THE ONE RULE
//    OF BOTH SCRUBS (architect 2026-10-26 ~23:15, the render player's scrub
//    taking it: "we should make them both the same"; the play-scrub's block
//    head) holds for the sound; the picture is the score's own. --

bool GuiInputHandler::claim_score_scrub_press(int x, int y,
                                              GuiInputState mods) {
    if (!app.score_viewer.active || score_viewer == nullptr) return false;
    if (!modal_dialog_stash_current()) return false;
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w <= 0 || track.h <= 0) return false;
    if (!rect_contains(track, x, y)) return false;
    // Consumed from here: a ctrl press does nothing on the track.
    if (mods.ctrl) return true;
    if (!app.score) return true;
    // THE THUMB AS PAINTED (ON SCREEN IS AS PAINTED): on its grab band the
    // press takes it where it stands, the grab offset kept; anywhere else on
    // the item the thumb jumps to the press.
    const int marker_x = app.modal_dialog.scrub_thumb_x;
    const bool on_thumb =
        marker_x >= 0 && render_player_scrub_handle_hit(track, marker_x, x, y);
    AppState::ScoreViewer::ScrubDrag& drag = app.score_viewer.scrub;
    const bool media = score_viewer_has_soundtrack(app);
    // A range of one page has no other page to show (the scrub dead).
    if (!media && score_viewer_range(app).count() <= 1) return true;
    drag          = AppState::ScoreViewer::ScrubDrag{};
    drag.armed    = true;
    drag.on_thumb = on_thumb;
    drag.grab_dx  = on_thumb ? x - marker_x : 0;
    drag.marker_x = clamp_player_scrub_marker_x(x - drag.grab_dx);
    if (media) {
        // THE SOUND PAUSES WHILE THE SCRUB IS HELD (the block head): a live
        // transport paused here, the cursor read into the resting position
        // and written as a pause writes it, its page shown; a paused one
        // arms as it stands.
        drag.was_live = app.score_viewer.transport ==
                        AppState::ScoreViewer::Transport::Live;
        if (drag.was_live) score_viewer->pause();
    }
    // THE PAGE THE PRESS FOUND — after the pause, which shows the page
    // holding the paused cursor — kept for the page road's lift comparison
    // and a lost lift's return on both faces. An off-thumb press then shows
    // the page under the column the thumb jumped to, display only (a press
    // on the thumb takes it at the shown page, nothing to show: under the
    // soundtrack its painted column's frame may round across a page edge
    // from the cursor's own).
    drag.press_page = app.score_viewer.page;
    if (!on_thumb)
        score_viewer->display_page(score_scrub_page_under(app, drag.marker_x));
    viewport.invalidate_rect(track);
    invalidate_scrub_clock(viewport, app);
    return true;
}

void GuiInputHandler::update_score_scrub_motion(int x) {
    AppState::ScoreViewer::ScrubDrag& drag = app.score_viewer.scrub;
    if (!drag.armed || score_viewer == nullptr) return;
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w <= 0) return;
    const int mx = clamp_player_scrub_marker_x(x - drag.grab_dx);
    if (mx == drag.marker_x) return;
    drag.marker_x = mx;
    drag.moved    = true;
    viewport.invalidate_rect(track);
    // The soundtrack's timestamp follows the thumb (the block head).
    invalidate_scrub_clock(viewport, app);
    // THE PAGE UNDER THE CARRIED COLUMN IS SHOWN (the block head), display
    // only on both faces — nothing written, nothing seeked until the lift; a
    // column inside the shown page changes nothing but the thumb.
    score_viewer->display_page(score_scrub_page_under(app, mx));
}

bool GuiInputHandler::finish_score_scrub_release() {
    AppState::ScoreViewer::ScrubDrag& drag = app.score_viewer.scrub;
    if (!drag.armed) return false;
    const AppState::ScoreViewer::ScrubDrag ended = drag;
    drag = AppState::ScoreViewer::ScrubDrag{};
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
    invalidate_scrub_clock(viewport, app);
    if (score_viewer == nullptr) return true;
    // THE PAGE ROAD'S LIFT COMMITS (architect 2026-10-27, the block head):
    // the page under the thumb's last column is already the shown page (the
    // motion showed it), so the lift writes its position — once, and only
    // where it differs from the page the press found; a motionless tap on
    // the thumb found the shown page and writes nothing.
    if (!score_viewer_has_soundtrack(app)) {
        if (app.score_viewer.page != ended.press_page)
            score_viewer->write_page_position();
        return true;
    }
    // A MOTIONLESS TAP ON THE THUMB SEEKS NOTHING (the render player's rule):
    // the thumb was taken where it stood and never moved, and re-seeking its
    // own column would round the position to that column's frame.
    // THE LIFT IS THE SEEK otherwise (architect 2026-10-26, "no scrubbing"):
    // to the column the motion last painted the thumb at (ON SCREEN IS AS
    // PAINTED; the painter draws the armed drag's marker_x), the position
    // written with it. THE DROP SHOWS NOTHING NEW: the seek's page follows
    // the frame at that column, which is the frame whose page the press or
    // the last motion showed (score_scrub_page_under) — play_from's clamp to
    // one short of the range's end moving it only inside the range's last
    // page — and a motionless tap showed nothing and seeks nothing. BOTH ARE
    // THE DROP'S ONE DERIVATION (score_scrub_drop_frame, score_viewer.h),
    // the answer the clock's elapsed field showed under the drag.
    const std::optional<int64_t> drop =
        score_scrub_drop_frame(app, ended, ended.marker_x);
    // THE SOUND PLAYS ON FROM THE DROP where the press paused a live
    // soundtrack (the block head): play_from, the soundtrack LIVE again from
    // the dropped frame — or from the pause's cursor under a motionless tap
    // — and NEVER THE PLAY KEY'S END RULE (architect 2026-10-27, the render
    // player's lift): the drop decides through the live seek's clamp (a
    // target in the range's last frame stops one short, so the natural end
    // takes over and rests at the end — architect 2026-10-26 ~23:55). Where
    // the press found it paused, the seek moves the cursor at rest. (The
    // render player's lift also asks whether a car toggle set its item
    // sounding mid-drag; the score has no car hook and nothing else acts
    // under a held scrub.)
    if (ended.was_live) {
        score_viewer->play_from(
            drop ? *drop : active_view_state(app).score_position);
    } else if (drop) {
        score_viewer->seek_to(*drop);
    }
    return true;
}

void GuiInputHandler::clear_score_scrub_drag() {
    AppState::ScoreViewer::ScrubDrag& drag = app.score_viewer.scrub;
    if (!drag.armed) return;
    // A LOST LIFT IS NOT A COMMIT (architect 2026-10-26 ~23:00, the
    // deliberate choice): on both faces the page the press found is shown
    // again, nothing written (2026-10-27); under the soundtrack nothing
    // seeked and nothing played — a live soundtrack the press paused stays
    // PAUSED where the press left it, the key reading PLAY, its page the one
    // the press found.
    const AppState::ScoreViewer::ScrubDrag ended = drag;
    drag = AppState::ScoreViewer::ScrubDrag{};
    if (score_viewer != nullptr) score_viewer->display_page(ended.press_page);
    const GuiRect track = app.modal_dialog.scrub;
    if (track.w > 0 && track.h > 0) viewport.invalidate_rect(track);
    // The clock's elapsed field returns to the cursor's time.
    invalidate_scrub_clock(viewport, app);
}


void GuiInputHandler::on_button_press(int x, int y, GuiInputState mods) {
    // ANY PRESS IS THE TOOLTIP'S HARD END, above every gate — Windows hides a
    // tooltip at a click ("About Tooltip Controls"), and a hint left floating
    // over the thing the user just clicked is noise. The release is one too
    // (on_button_release); the press is the one that matters, because the
    // hint's job ends the moment the user acts on it, not when they let go;
    // and the held bit is recorded first, so NO WAIT STARTS UNDER THE HELD
    // PRESS (architect 2026-09-29 — AppState::RedesignTooltip). THE PRESS
    // SPENDS NOTHING (architect 2026-11-07, the always-show model): it
    // forgets the column and breaks the chain, so after the lift a pen
    // hovering on a column waits the delay as an arrival.
    app.redesign_tooltip.button_held = mods.primary_button_held;
    hide_shift_tooltip();
    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate — ALL THREE at a motionless RELEASE now (TrimBar /
    // EmptyLane, the empty lane joining that class 2026-08-12 with
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

    // THE A/B AUDITION IS HANDS-OFF (architect 2026-10-28; the rule at
    // end_audition_for_press's declaration): a press ends a standing act
    // ABOVE EVERY CLAIM BELOW — the CLAIM ranks above every veil, so this
    // ranks above the claims, and no claim can turn the press into a gesture
    // under a standing act — every place on the window. THE ONE
    // EXCEPTION is a press that lands on the PLAY key (either rect, the
    // clock's field included) and not on a card over it: its lift dispatches
    // bare Space, or Shift+Space held, through on_key — the two
    // chords whose own dispatch forks on the act and takes its stop side — so
    // ending the act at this press would make that lift a launch or a restart.
    const bool play_key_press =
        notification_card_at(app, x, y) == 0 &&
        redesign_button_hit(app, RedesignButton::TransportPlayStop, x, y);
    if (!play_key_press) end_audition_for_press();

    // THE NOTIFICATION CARDS, ABOVE EVERY GATE AND EVERY VEIL (architect
    // 2026-08-29): a press on a published card is consumed whole — moves
    // nothing, lands no playhead, opens no drag, reaches nothing underneath —
    // and since 2026-10-01 THE CARD IS ONE BUTTON ("whole card dismisses, X
    // gone"): the press anywhere on it arms the card's own chrome arm
    // (AppState::ChromePress's Card kind) and its lift dismisses it, or every
    // card when held (the rule at notifications.h's hit section).
    // The router cannot fork on tap versus click (no origin bit rides a
    // press; GuiInputState carries modifiers alone), so the rule is one for
    // every input. It ranks above the veils because a card is
    // the message ABOUT the act a veil stands over, not a reach into the
    // veiled surface (the record at the retired reach-through's site above),
    // and the lift is taken at the same rank in on_button_release. This return precedes every arm below, so
    // the card's is the only arm the press leaves. ON GLASS the pan zone
    // answers false on a card (touch_point_in_pan_zone), so a finger landing
    // on one resolves to this press ON CONTACT (the off-zone down,
    // input_core.cpp) rather than to the phone-model pan — which is what
    // lets the hold be measured from the contact and the clock stop there —
    // and a second finger beside it is ignored. The geometry read here is
    // the last paint's publication, and the lift asks the live stack whether
    // the id still stands — a card that left between paint and lift lands
    // nothing.
    if (claim_notification_press(x, y)) return;

    // THE ON-SCREEN KEYBOARD, ABOVE EVERY GATE (2026-08-27). While it stands
    // its rect belongs to no other surface, so there is nothing below to
    // arbitrate with — and it MUST outrank the dialog editors' veil further
    // down, which would otherwise swallow the press that types into the very
    // editor raising it. It stands only where an editor owns it, so this is
    // one integer compare otherwise.
    // Its rect is opaque: a press inside it never reaches the waveform's
    // gestures underneath, key or gap. (Contract at the declaration; the act
    // runs at the PRESS, inside; a press carrying the barrel's ctrl is
    // consumed there with no act, architect 2026-11-08.)
    if (claim_onscreen_keyboard_press(x, y, mods)) return;

    // (THE SIZING FRAME's claim, above every veil, 2026-10-05 to 2026-10-24,
    // went with the app's frame.)

    // Prompt-modal input handling: while a prompt dialog is up, its BUTTONS
    // are the pointer's only targets — and since 2026-08-13 a plain press
    // on one only ARMS it (architect: "everything else acts on lift"). The
    // ACT is at the release, in on_button_release's mirror of this gate, which
    // is where activate_response is called and where the live-response-set
    // validation lives. Every other press — a ctrl press,
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
        if (app.prompt.painted && !mods.ctrl && modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y);
        }
        return;
    }

    // (THE OPEN DROPDOWN OWNED THE POINTER HERE, claimed above every band —
    // a press on an item armed it, a press anywhere else closed the menu and
    // was consumed, ONE PRESS, ONE ACT (2026-09-03) — until the Settings
    // popup, its last menu, retired 2026-10-21.)

    // THE FOLDER OVERLAY'S BAND, claimed for EVERY content and ranked here —
    // under the prompt gate (a prompt outranks every surface; the
    // player's load confirmation and the reopen's unsaved-tab question paint
    // over their own rows) and above the TWO mode
    // veils below — the player's and the picker's, each of which
    // admits the band and its own modal row
    // and consumes the rest. The claim is opaque.
    if (claim_folder_overlay_press(x, y, mods)) return;

    // (THE VEILS BELOW LET NOTHING THROUGH since 2026-10-24: the app's
    // caption, the window's title bar, was their one exemption from
    // 2026-10-05 and went with the caption; the menu row's live File anchor
    // the other, 2026-09-03 to 2026-10-21, gone with the row.)
    // THE BUTTON ROWS' CLAIM — the icon row's and row 8's — ONE BODY for
    // their own band claims below and for the three overlays' veils, which
    // let the icon row's press through to their opener (architect 2026-10-13
    // for the player, 2026-10-25 for the picker, 2026-10-26 for the score
    // viewer), to the tooltip lamp (architect 2026-11-10, live under every
    // overlay) and to the struck clip lamp (architect 2026-11-05 night).
    // THE LEVEL CASE IS THE CLAIM'S SECOND HALF (architect 2026-11-05):
    // where no chord row's face holds the press, the level case's face may —
    // the slider, armed by its own kind (arm_level_case_press). THE
    // STRUCK CLIP LAMP IS ITS THIRD (architect 2026-11-05 night, session 49:
    // "the strike + the tap"; render.h's THE TWO LAMPS' STRIKE): where
    // neither claimed it, a press on the clip lamp's square while the strike
    // stands arms the Lamp kind (arm_clip_lamp_press) — the logo lane is the
    // icon row's upper pad, inside this band. A ctrl press reaches none of
    // the three arms: the modifier gate consumes every press carrying the
    // barrel's ctrl on the band and row 8 (architect 2026-11-08, the barrel
    // no chrome modifier).
    const auto claim_button_row_press = [&] {
        if (chrome_band_modifiers_refused(app, x, y, mods)) return;
        if (arm_redesign_press(x, y)) return;
        if (arm_level_case_press(x, y)) return;
        arm_clip_lamp_press(x, y);
    };

    // THE RENDER PLAYER'S VEIL (2026-08-28), under the prompt gate — its load
    // confirmation is a prompt and paints over it — and above everything
    // else: while the mode stands the pointer has FOUR targets, the folder
    // overlay's rows (claimed above), the play-scrub (every press on it arms
    // the thumb drag, the seek at the lift), the modal row's buttons
    // (the arm every dialog button takes) and the icon row's band (below:
    // its own lit key, the tooltip lamp, architect 2026-11-10, and the
    // struck clip lamp's square, architect
    // 2026-11-05 night), and EVERY OTHER PRESS IS CONSUMED — the flags, the
    // waveform, the dead roster. (The menu row's File anchor was a target
    // 2026-09-03 to 2026-10-21, the app's caption 2026-10-05 to 2026-10-24.)
    // The whole rule is stated at render_player_active (input_handler.h).
    // THE PLAYER'S TWO SKIPS CARRY A SHIFTED TWIN (2026-08-28) — the item
    // folder's ends — which the LONG PRESS reaches at the lift
    // (modal_dialog_press_shifted); ctrl spells nothing on any modal row and
    // stays consumed by the veil below.
    if (app.render_player.active) {
        // THE PLAY RENDERS BUTTON TOGGLES THE PLAYER OFF (architect
        // 2026-10-13: "so the user can quickly toggle it on/off by pressing
        // the same button twice"): the icon row's press reaches the roster's
        // own arm, whose painted-bit gate consumes every button but the two
        // the face leaves live (redesign_button_enabled's overlay arm: this
        // key and the tooltip lamp), and the
        // lift is bare `l`, the player's own closer — the lamp's bare
        // backslash, the player's router's other arm. The same claim reaches
        // the struck clip lamp's arm, its lift the main page's reopen.
        if (rect_contains(top_icon_row_area(app), x, y)) {
            claim_button_row_press();
            return;
        }
        if (claim_player_scrub_press(x, y, mods)) return;
        if (!mods.ctrl && modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y);
        }
        return;
    }

    // THE PICKER'S VEIL (2026-08-28), the player's shape one mode over: while
    // a picker stands the pointer has TWO targets, the overlay's rows
    // (claimed above) and the modal row's buttons — Cancel under the project
    // picker and the value list, the trim presets list's and the BPM form's
    // clusters, none under the settings page since 2026-11-11 (the arm every
    // dialog button takes; the app's caption a third, 2026-10-05 to
    // 2026-10-24), beside the icon row's band (below: the opener key, the
    // tooltip lamp, architect 2026-11-10, and
    // the struck clip lamp's square, architect 2026-11-05 night), and EVERY
    // OTHER PRESS IS CONSUMED. A ROW CLICK IS
    // THE OPEN ACT, which is why the row carries no OK beside that button. There is no field
    // and so no caret claim and no text drag — the picker has nothing to
    // type into. The whole rule is stated at picker_active (input_handler.h).
    // THE OPENER KEY CLOSES IT (architect 2026-10-25, the player's own
    // Play Renders toggle one overlay over): the icon row's press reaches the
    // roster's arm, whose painted-bit gate consumes every button but the two
    // the face leaves live (redesign_button_enabled's overlay arm: the
    // opener and the tooltip lamp), and the
    // lift is that key's chord — Ctrl+O under the project picker, `;` under
    // the settings page and the value list — which the picker's router
    // answers with its close, the lamp's bare backslash with the lamp's
    // toggle. The same claim reaches the struck clip lamp's
    // arm, its lift the main page's reopen.
    if (app.picker.active) {
        if (rect_contains(top_icon_row_area(app), x, y)) {
            claim_button_row_press();
            return;
        }
        if (!mods.ctrl && modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y);
        }
        return;
    }

    // THE SCORE VIEWER'S VEIL (2026-10-26), the player's: the band's press
    // (its two live keys, the SCORE key and the tooltip lamp, architect
    // 2026-11-10, and the struck clip lamp's square,
    // architect 2026-11-05 night), the page scrub's, a press on a row
    // key (its skips' shifted twin the long press's at the lift, architect
    // 2026-10-27, score_button_shift_admits); every
    // other press — the page itself among them, which takes no gesture — is
    // consumed.
    if (app.score_viewer.active) {
        if (rect_contains(top_icon_row_area(app), x, y)) {
            claim_button_row_press();
            return;
        }
        if (claim_score_scrub_press(x, y, mods)) return;
        if (!mods.ctrl && modal_dialog_stash_current()) {
            arm_modal_dialog_press(x, y);
        }
        return;
    }

    // THE PROGRESS MODAL'S VEIL (architect 2026-10-14, AppState::
    // BatchProgress): while a batch's modal stands the pointer has ONE target
    // of its own, the controls row's Cancel (the arm every dialog button
    // takes, its act at the lift); EVERY OTHER PRESS IS CONSUMED —
    // the box, the lanes and the roster alike, the prompt's own veil. Ranked with the two list
    // owners' veils, none of which can stand beside it; the cards, the
    // keyboard and a prompt over it answer above.
    if (app.batch_progress.active) {
        if (!mods.ctrl && modal_dialog_stash_current()) {
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
    // THAT ITSELF now: both editor dialogs publish real OK and CANCEL
    // buttons, the claim below admits a press on them, and Cancel dispatches
    // the session's own Esc. With Quit's button gone the membership had
    // already derived down to Save, and a convenience chord is
    // not worth an exception to the veil. SO THE VEIL HAS NO EXCEPTION: while
    // a dialog editor stands every press outside the modal is consumed — the
    // notification cards' claim at the head of this function is hit ABOVE
    // the veil by ruling and is not one (a card is the message about the
    // veiled act, not a reach into the surface). The
    // keyboard has NO HATCH either since 2026-10-25: Ctrl+S, which the
    // block used to mirror, is consumed at the editors' key gate like every
    // other chord (modal_editor_key_blocked). The full retirement record is
    // at the deleted predicate's site near the head of this file.)

    // THE DIALOG'S OWN BUTTONS, claimed while an editor dialog stands and
    // ahead of the field claim below (the rects are disjoint; the order only
    // states that a button press is a button press). A plain left press on OK
    // or Cancel ARMS it since 2026-08-13 and dispatches nothing; the LIFT on
    // that same button runs the editor's own Enter or Esc through the one
    // modal key route (dispatch_modal_dialog_editor_act, from
    // on_button_release's mirror of this gate) — button-is-its-chord, so a
    // refusal, the BPM field's return to its form and every teardown are
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
    // (THE SETTINGS CHOICE EDITOR'S COMBO AND ITS LIST took their presses
    // here, ahead of the buttons, 2026-10-07 to 2026-10-14.)
    if (!mods.ctrl && modal_dialog_stash_current()) {
        if (arm_modal_dialog_press(x, y)) return;
    }

    // A PRESS ON THE ACTIVE EDITOR'S FIELD seats the caret, collapsing any
    // selection, and arms the pointer road's caret drag (EditorTextDragState:
    // the motions until the lift move the caret, the selection staying
    // collapsed). Resolved before the veil below so the gesture reaches every
    // editor. ONLY A TAP ARRIVES HERE (its press, its sub-slop motions and
    // its release in one burst at the lift): a drag in the field, pen or
    // finger, is the touch translation's caret drag and delivers no press
    // (the trio's bodies beside begin_touch_region). A press outside the
    // active editor's field is swallowed by THE VEIL, every editor being a
    // modal since 2026-10-25.
    {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            // THE FIELD IS ONE RECT (ActiveEditorText::field): the dialog
            // editor's edit control, the modal family's box's interior — the
            // ground round it is chrome, and its buttons were
            // claimed above, so claiming more would map a chrome press to an
            // editor byte — or the marker lane's whole published box, pads
            // included. Any press OUTSIDE it is consumed by the veil
            // below, closing nothing.
            if (rect_contains(g.field, x, y)) {
                // THE FIELD TAKES THE FOCUS BACK AT THE PRESS (architect
                // 2026-10-02; the rule at return_modal_focus_to_field), the
                // caret's blink restarted so it is lit at once (the blink
                // alone: the seat that follows places the caret).
                if (g.dialog && return_modal_focus_to_field())
                    text_editor::restart_blink(*g.ed);
                // NO PRESS SELECTS (architect 2026-11-08, session 95,
                // retiring the double tap's word select and its drag by
                // words): the seat collapses the selection, and the
                // selection is the keys' alone (Shift + LEFT / RIGHT).
                set_editor_caret_from_x(g, x, y);
                app.editor_text_drag = EditorTextDragState{};
                app.editor_text_drag.active = true;
                // The dialog editors' edit damage is the box's interior
                // (invalidate_dialog_editor_box, viewport.h).
                if (g.dialog) viewport.invalidate_dialog_editor_box();
                else          viewport.invalidate_top_strip();
                return;
            }
            // A PRESS OUTSIDE THE FIELD falls to THE VEIL'S LAST WORD below,
            // which consumes it and closes nothing.
        }
    }

    // THE VEIL'S LAST WORD: any editor still standing here swallows the
    // press — every button, every modifier, every surface: the band and row
    // 8 (their keys dead and ungreyed behind it), the view bar, the ruler,
    // the marker lane (no press on another flag), the waveform (no pan,
    // scrub, placement, marker or strip drag) — and CLOSES NOTHING. It asks
    // THE ONE PREDICATE for "a keyboard-taking modal stands"
    // (keyboard_modal_editor_active, input_handler.h, whose declaration
    // names its readers) rather than spelling the editors again, so a new
    // editor is veiled here by existing rather than by an edit (the
    // architect's words, 2026-10-25: "once I've done that pop-up modal, I
    // can't do anything else in the window behind it"). EVERY EDITOR IS A
    // PURE MODAL, the lane editors (the flag editor, the iteration bound
    // editor) with the dialogs: an editor ends through its own keys alone —
    // Esc, Enter — and a dialog's Cancel and OK: the on-screen keyboard's own
    // Esc and Enter, whose claim ranks above every gate, and no press outside
    // the field. The press inside the field keeps its act (the caret block
    // above). (A LANE
    // EDITOR'S ONE EXCEPTION, a left press in the lanes the keyboard left
    // visible — the view bar, the ruler, the marker lane, the waveform as
    // painted — closing it as Esc does, stood 2026-10-26 to 2026-10-29; it
    // is undone, architect 2026-10-29: "I think we can reverse that,
    // because the escape key should be at a very convenient position on the
    // keyboard" — the keyboard's Esc sits lower right since the compact
    // keyboard of the same day.)
    if (keyboard_modal_editor_active()) return;

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
    // modifier gate above (CTRL only on the buttons the roster admits it or
    // its pair on), and any press in the band that is not on a button
    // a consumed nothing. Each band differs ONLY in its rect (row 1's dropdown
    // toggle of its three non-chord menu anchors went with the menu row,
    // 2026-10-21, and the dropdown with the Settings popup) — so the press is ONE arm body,
    // arm_redesign_press, driven
    // by the table's per-button flags — and the act one release body,
    // finish_chrome_press_release, in on_button_release.
    //
    // A BUTTON's rect is the painter's stash (app.redesign_buttons, published by
    // paint_icon_row /
    // paint_bottom_row_buttons_and_clock; every roster member publishes a real
    // rect on every frame since 2026-08-18, the bottom row's cluster swap —
    // whose unpainted four stashed a zero rect that contains no point, and
    // which was the product's last hiding after the icon row's collapse rule
    // was deleted on 2026-08-14 — having gone with the history companions'
    // return to row 4; a MODAL's yield is the one place the shape survives) —
    // never re-shaped here, so the clickable rect is the painted one. THE ACT
    // IS AT THE RELEASE (architect 2026-08-13, the chrome-wide rule —
    // authoritative statement at AppState::ChromePress): the press ARMS through
    // arm_redesign_press, carrying the press-time ctrl, and the lift on that
    // same button dispatches through finish_chrome_press_release
    // (on_button_release). Nothing on these rows drags — no threshold, no
    // double-click surface — and the arm is the whole press-state machinery
    // (AppState::ChromePress).
    // (THE MENU ROW'S CLAIM — its titles the roster's non-chord buttons, each
    // a popup toggle acting at the PRESS, the press-drag-into-the-box gesture
    // claiming the held button — went with the row, 2026-10-21.)
    // (THE ICON ROW'S BODY IS claim_button_row_press, above the veils: the
    // render player's veil reaches it too.)
    if (rect_contains(top_icon_row_area(app), x, y)) {
        claim_button_row_press();
        return;
    }
    // THE UNIFIED BOTTOM ROW (row 8's claim since 2026-08-11; the whole
    // merged lane since the 2026-08-12 unification), the block's fifth member
    // on the block's own terms: the band is bottom lane 0 (bottom_row_area,
    // on the window's foot since the simple layout, 2026-10-22), and
    // everything else is the shape above — below the modal gates (a prompt,
    // any editor or the progress modal swallows the press — the lane
    // editors too since 2026-10-25, their row-8 presses having reached the
    // editor's key gate until then),
    // above the loading/empty guard, ctrl/alt strict no-ops, and every
    // press in the band that is not on a button a consumed nothing — which
    // includes the bare ground beside the clock, the lane's pointer-inert
    // span (the status chain that shared that ground until 2026-08-13 took
    // no clicks either, and took none away with it) — and the STATE CELL
    // right of the clock since 2026-08-29 is that same inert ground, a text
    // cell with no rect of its own to test, as is the active tab's name
    // beside the clock (2026-10-22). THE CLOCK'S FIELD IS ON A BUTTON: the
    // PLAY key's second rect (architect 2026-10-27,
    // RedesignButtonFace::field), so a press there is the key's. ABOVE it stand
    // the well's foot (inert chrome, a row of ground) and
    // GAP 2's blank window ground — outside every band here and falling
    // through to the tail's consumed nothing, the ground as window ground by
    // the vertical rule (main.cpp).
    // (THE BODY IS claim_button_row_press, above the veils.)
    if (rect_contains(bottom_row_area(app), x, y)) {
        claim_button_row_press();
        return;
    }
    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    // The waveform BAND is the navigation surface, point_on_nav_surface
    // exactly — the canvas's interior where the waveform is painted
    // (2026-10-09; the keyboard's band carved out 2026-10-10, inside the
    // frame since 2026-10-14): the column's margins beside it are the
    // program's panel, and a press there falls to the tail's consumed
    // nothing. `area` stays the coordinate origin.
    const bool inside_waveform = point_on_nav_surface(app, x, y);
    const bool inside_top = rect_contains(top, x, y);
    const bool ctrl = mods.ctrl;

    // Defensive: a second press during a drag is ignored (the button
    // should still be held down for a drag to exist).
    if (app.drag.active) return;
    if (app.trim_drag.active) return;
    // The region drag joins the pair since the touch half (2026-08-12): a
    // live TOUCH region gesture holds no logical button, so a press arriving
    // mid-gesture would arm a second writer over the same state. Consumed
    // instead, the trim guard's own shape; the gesture ends by its own hooks
    // or the button-lost motion arm.
    if (app.region_drag.active) return;

    // THE `h` HISTORY MODE's pointer gate — the sibling of the on_key allowlist,
    // and the second and last of the mode's two gates.
    //
    // PLACED BELOW THE FOUR REDESIGNED ROWS' BAND CLAIMS ON PURPOSE. Those rows
    // dispatch their buttons as synthesized CHORDS through on_key, so they are
    // already covered by the keyboard gate — Save, Undo, Redo, Render and the
    // view pairs drop there exactly as their keys do, with no second membership to
    // keep in step — and letting them through here is what keeps that single
    // coverage true. ONE PRESS ROUTE IN THESE ROWS DISPATCHES NO CHORD
    // (re-derived 2026-08-06, again 2026-08-15 when the Navigation anchor left,
    // again 2026-08-18 when the walk selector did, and again 2026-08-20 when
    // the EDIT anchor arrived, and again 2026-08-27 with SERIES, and again
    // 2026-09-09 when the HELP anchor left): the three menu
    // anchors, which had none — went with the menu row, 2026-10-21; every
    // roster button carries a chord since.
    // The double-click SNAPSHOT is handed in because the mode has a double-click
    // act of its own (the trim bar's framing) and this function's own field was
    // cleared at the top of the press; nothing else about the call is special.
    if (app.history_mode.active &&
        handle_history_mode_press(x, y, mods, dc_at_press)) {
        return;
    }

    // Pointer authoring is home-view gated like the keyboard: the marker DRAG
    // never moves a warp flag in T+W — selecting and landing but MOVING
    // NOTHING, the one off-home state left since the P column opened to both
    // audio views (2026-08-30, and again with S+P's return, 2026-10-12) — the
    // crossing's value-drag posture claiming
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

    {
        // (THE FLAG EDITOR'S GUARD-FREE CLOSE stood here, 2026-08-01 to
        // 2026-10-25: any left press outside an open marker-lane editor's
        // field closed it uncommitted and then acted, its riding boxes
        // included. Every editor is a modal since 2026-10-25, so the veil
        // above swallows that press and no editor stands past it.)

        // The marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item and that item is now its FLAG BOX alone
        // (hit_test_flag against the painter's stash — the rendered lane run
        // that used to be its second half died with the marker-text lane, and
        // with it the MarkerHit pair and its shared resolver marker_hit_at).
        // The TOP-STRIP hit feeds the plain and Ctrl marker-press branches,
        // both of which run the CLICK ACT AT THE PRESS (2026-08-17 — content
        // acts the moment its identity is certain; the acts are at
        // run_marker_click_act: plain = single-select + land + the double-click
        // consume-open, Ctrl = the individual membership toggle + land on the
        // resulting focus), so it is resolved once here — each lands on its
        // own focus.
        // The WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + the pending click; lower:
        // the scanner scrub, which touches no selection at all) — so no marker
        // scan runs on the waveform at all. THE MARKER STEMS ARE
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
        // waveform is purely trim / playhead / pan / zoom. The stem still
        // PAINTS on every selected marker (2026-11-05: the selection
        // stem), disabled-no-stem.
        // The plain DRAG never selects markers either
        // (SELECTION FLOWS DOWNWARD ONLY, architect 2026-07-23 — the region no
        // longer selects its contents; it leaves the selection empty).
        // Trim bounds are grabbed by their top-strip endcaps /
        // the inter-endcap bridge on a PLAIN trim-bar press (route_trim_bar_press
        // below) alone — the trim region overlay's own bound and interior drags
        // on the waveform (2026-08-18) were deleted with its resting form on
        // 2026-09-22; a click over a bound's waveform stem is an ordinary
        // waveform click (the stem grab retired). Resolved ONCE here, ahead of every branch that
        // consumes it: the ctrl arm, the plain arm, and the
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
        // degenerate audio/geometry state, on its own end cap or at its
        // partner wall)
        // is as playback-inert as an unclaimed one. That covers a ctrl press
        // off a flag and off the trim bar and the inter-lane gaps — all of
        // which end at an inert return below.
        // The plain presses on the EMPTY MARKER LANE, the ruler and the waveform
        // are PLACEMENT presses that stop nothing AT THE PRESS: each arms a
        // pending whose DEFERRED CLICK ACT (arm_placement_press since the lane
        // became a placement surface, 2026-09-25; arm_nav_press from the
        // pan-primary ruling of 2026-08-12) places the playhead at the lift,
        // and the stop is the PLACEMENT BODY's own
        // (place_playhead_at_click_column, architect 2026-10-06, the class at
        // stop_playback_if_playing's declaration), so a pan or a gutter click
        // stops nothing.

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // A CTRL PRESS — the S Pen's barrel, the pointer's one modifier —
        // splits by surface. On a top-strip MARKER it is the individual
        // membership toggle + land on the resulting focus (the marker claim
        // below); on the TRIM BAR it is the bound set (the claim below).
        // EVERYWHERE ELSE IT IS A CONSUMED NOTHING, stopping no playback: the
        // ruler, the marker lane's empty stretches and THE WAVEFORM (architect
        // 2026-10-30: the barrel tap there arms nothing — the one-finger ctrl
        // zoom is the touch road's, GuiTouchNavFrame::ctrl, and a motionless
        // ctrl press-release on the waveform commits nothing, as it always
        // did). It says nothing either: an unbound gesture is identified by
        // its silence (architect 2026-08-30).
        if (ctrl) {
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
            // THE FLAG IS THE WHOLE SURFACE HERE — `inside_top` alone: a stem
            // stands ON the waveform, where no press resolves one (since
            // 2026-08-12). A markerless
            // top-strip ctrl press claims only the trim bar (the bound set,
            // next block) and is a strict no-op on every other lane — no-op in
            // the playback sense too, since only a CLAIM stops playback.
            if (inside_top && mh_index >= 0) {
                // THE PRESS ACTS (2026-08-17): the toggle, its stop and its land
                // are the CLICK, run here through the one
                // act owner (run_marker_click_act). A ctrl click has no
                // gesture to become and no double-click meaning, so nothing is
                // armed — the act's own modified-shape return.
                run_marker_click_act(mh_index, x, y, /*ctrl=*/true, dc_at_press);
                return;
            }
            // Markerless top-strip ctrl press: the TRIM BAR sets a trim bound
            // at the click (REINSTATED architect 2026-08-01, homed on the
            // redesigned bar's whole band; THE END CAP NEAREST THE PEN,
            // architect 2026-11-10 — below);
            // set_trim_bound_at_click moves that cap to the click — inward
            // from inside the trim, outward from outside it, a click on the
            // cap itself, or one that would land it on or past its partner,
            // a no-op; the click ADJUSTS the window that always
            // rests, it never creates one — and, being
            // a SETTER, deselects past its refusals. IT NO LONGER REFUSES A
            // READ-ONLY TAB: trim is band, not authored content, and that gate
            // was deleted 2026-08-07). EVERY other lane is a
            // strict no-op, falling through to the return below (the ctrl-click
            // clear on an empty marker spot is RETIRED, architect 2026-07-23:
            // ctrl-click in Ableton is just click; the PLAIN empty marker-lane
            // press below is the
            // surviving lane gesture — the waveform's own parity press, not a
            // bare clear). The three redesigned top rows (lanes 0..2 since the
            // 2026-08-12 relayout deleted the toolbar lane) were claimed far
            // above and never reach here.
            //
            // THE BAND IS THE CURRENT GEOMETRY OWNER, top_trim_row_area — the
            // exact band the plain endcap/bar drags and the span-framing
            // double-click claim, so paint, hit and every trim gesture read ONE
            // accessor and cannot drift. THE BAND IS THE LANE'S RECT, BOTH
            // AXES (2026-10-10): the program frame's columns beside it are
            // inert ground and set no bound (the PLACEMENT LANES block, above point_on_playhead_head).
            if (inside_top) {
                const GuiRect trim_band = top_trim_row_area(app);
                if (rect_contains(trim_band, x, y)) {
                    // THE PRESS ONLY ARMS — THE ONE SURVIVING DEFERRED CLICK
                    // (2026-08-17; contract at PendingClickAct, app_state.h —
                    // this press IS the endcap drag's arm, the genuine press
                    // ambiguity the deferral exists for): the bound set, its
                    // refusals, its stop, its commit tail and its deselect
                    // are the CLICK, and the click runs at the MOTIONLESS LIFT
                    // through the one act owner (run_pending_click_act), at
                    // the PRESS column. A
                    // CROSSING runs that same set and then hands the gesture to
                    // the single-bound endcap drag on the bound it just wrote.
                    // THE BOUND IS THE NEAREST END CAP (architect 2026-11-10):
                    // whichever cap is nearest the press column on the
                    // painted columns, a tie the
                    // begin's but right of both caps, where it is the
                    // end's, decided where the set runs (trim_bound_click,
                    // input_trim.cpp); a held press means nothing more than
                    // a tap.
                    // NO stop here, for the reason it was never here: the bound
                    // set has its own refusals (a degenerate audio/geometry
                    // state, a click on its own cap, the partner wall), and a
                    // refused click changes nothing, so there is nothing for a
                    // stop to protect. The stop lives INSIDE
                    // set_trim_bound_at_click, past every refusal and
                    // immediately ahead of the bound write.
                    arm_pending_click_act(x, y);
                    return;
                }
            }
            return;
        }

        // The plain press, under the PAN-PRIMARY vocabulary (architect
        // 2026-08-12, the eighth glass ruling) as amended 2026-08-13 (THE
        // WAVEFORM'S TWO HALVES BECOME ONE SURFACE). On the NAVIGATION SURFACE
        // — the WHOLE waveform and nothing else since 2026-09-25 — a PLAIN
        // press is a PENDING CLICK (arm_nav_press): a motionless
        // release runs THE HALF'S OWN ACT as the DEFERRED CLICK ACT (upper =
        // the playhead placement, lower = the audition scrub), and a contact
        // crossing the slop is the touch layer's pan in either half before
        // this road sees a press (architect 2026-10-30; the region sweep is
        // the TRIM arm lamp's touch drag, begin_touch_region). On the two
        // PLACEMENT LANES — the RULER and the MARKER lane's empty stretches,
        // off the surface since 2026-09-25 — a plain press is the PLACEMENT
        // PENDING (arm_placement_press): the motionless release places, a drag
        // does nothing. Neither ever SELECTS a marker. In the top strip a
        // plain TRIM-BAR press arms a trim endcap/bridge drag (claimed ahead
        // of the marker select); a marker click — its FLAG BOX, the marker's
        // one pointer item — is the whole selection interface, BOTH views,
        // UNCHANGED by the ruling: plain
        // click: single-select and LAND the playhead on the marker, both AT
        // THE PRESS (2026-08-17 — content acts the moment its identity is
        // certain), the press also arming the pending that becomes the
        // reposition drag past the threshold. The individual membership
        // TOGGLE is Ctrl+click (the ctrl marker claim above; whether it adds
        // or removes it lands on the focus it leaves, an empty selection
        // landing nothing). The plain / ctrl land makes every such marker
        // click a land route alongside the Tab family and `c` (which
        // additionally recenter / re-zoom), and both land ON THEIR FOCUS
        // (architect 2026-07-28), so the playhead and the focused flag are
        // coincident before any subsequent drag or nudge — nothing is towed.

        if (inside_top) {
            // TOP-STRIP ONLY (the stem's widened gate — `inside_top ||
            // stem_click` — died with the stem surface, architect 2026-08-12:
            // a plain waveform press over a stem column falls through to the
            // waveform block below and is the placement press there).
            //
            // The TRIM BAR (the view bar, top_trim_row_area) is trim's lane
            // and is claimed BEFORE the marker single-select. The canvas
            // column's lanes — the trim bar, the ruler (top_ruler_row_area)
            // and the marker lane (top_marker_row_area), main.cpp's lane
            // table — are disjoint y-bands, so
            // this contends with nothing: a marker-part press falls to the marker
            // handling below. The PLAIN click consumes the span-framing
            // double-click AT THE PRESS (2026-08-17) and then falls through to
            // the same cap/bridge arm every plain press takes, else — on an
            // unclaimed spot — is a CONSUMED NOTHING; the bound-set click
            // is the ctrl claim above. The plain press stops no playback: the
            // trim bar's stop belongs to the DRAG's first accepted bound change
            // (input_trim.cpp).
            // THE RULER BAND'S PLAIN PRESS IS THE PLACEMENT PENDING
            // (architect 2026-09-25: the ruler and the marker lane left the
            // navigation surface — "out of both — we want
            // symmetry as much as possible"). The press arms ScrollDragState
            // (arm_placement_press) and does NOTHING ELSE; a motionless
            // release runs the deferred click act (deselect + playhead to the
            // column), and a drag that crosses the slop does nothing at all —
            // no pan, no zoom — but the head drag on the playhead's head.
            // No double-click surface here: the span-framing double-click
            // lives on the TRIM lane, and the marker-create one on the MARKER
            // lane's empty stretches — the ruler seeds nothing.
            //
            // THE BAND IS EXACTLY top_ruler_row_area AND NOTHING BELOW IT (the
            // claim reads the lane accessor and only the lane accessor), ITS
            // RECT ON BOTH AXES (2026-10-10): the program frame's columns
            // outside the lane's inset are inert ground and fall to the empty
            // spot's consumed nothing below (the PLACEMENT LANES block, above point_on_playhead_head).
            // The `h` VIEW never reaches this arm — its own gate armed the same
            // pending with the mode's deferred land far above. A press on the
            // well's MARGINS (the ground beside
            // the ruler's face, inside the lane, 2026-10-09) still
            // arms; its motionless release's click act deselects and seats no
            // playhead, the placement body's own off-column shape.
            // ON THE PLAYHEAD'S HEAD AS PAINTED the same pending is THE HEAD
            // DRAG (architect 2026-10-09 ~17:40; point_on_playhead_head,
            // ScrollDragState::head_drag): past the slop every motion places
            // the playhead at the pointer's column. The flags rank first
            // (mh_index, the one hit above — a cue never stands in the ruler,
            // and the order is stated rather than left to geometry).
            {
                const GuiRect ruler = top_ruler_row_area(app);
                if (rect_contains(ruler, x, y)) {
                    arm_placement_press(
                        x, y, /*history=*/false, /*seed_empty_lane=*/false,
                        /*head_drag=*/mh_index < 0 &&
                            point_on_playhead_head(app, x, y));
                    return;
                }
            }
            const GuiRect trim_bar = top_trim_row_area(app);
            const bool in_trim_bar = rect_contains(trim_bar, x, y);
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
                // read-only check (it never did). The full ruling is at
                // read_only_key_blocked
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
                run_marker_click_act(mh_index, x, y, /*ctrl=*/false,
                                     dc_at_press);
            } else {
                // Empty top-strip spot — no marker flag under the point (the
                // trim bar already returned above; mh_index < 0 here).
                // ONE lane to test: the flag and triangle lanes became the
                // single marker lane in row 5.
                const GuiRect marker_lane = top_marker_row_area(app);
                const bool in_flag_or_tri =
                    rect_contains(marker_lane, x, y);
                if (in_flag_or_tri) {
                    // The empty marker-lane stretch — a PLACEMENT SURFACE
                    // since 2026-09-25 (architect: the lanes left the
                    // navigation surface; the navigation
                    // surface's lane member from 2026-08-12, the press-time
                    // parity placement of 2026-07-23 before that). Plain by
                    // construction here: a ctrl press returned in its claim
                    // above. TWO acts:
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
                // inter-lane gaps (the program frame's top row and its two
                // columns, inert ground 2026-10-10 to 2026-10-22, are gone),
                // and any press in the FLEXIBLE GAP 1 band,
                // (the centering rule's remainder, main.cpp's vertical rule: inside
                // top_strip_area but in no lane, so it falls to exactly this
                // return with no code of its own —
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
        // construction: a ctrl press returned in its claim above.
        //
        // THE TWO HALVES ARE ONE SURFACE (architect 2026-08-13, superseding the
        // press-time scrub: "the playhead scrub is an outlier. We do everything
        // on lift the finger or on mouse up, but the playhead scrub, we do right
        // on mouse down. We should remove that"). BOTH halves arm the same
        // PENDING CLICK — nothing at press, a crossed press runs nothing (the
        // pan is the touch layer's, architect 2026-10-30: a waveform contact
        // past the slop is Nav before this road sees it) — and the HALF, read
        // once here and stashed on the
        // pending, picks which act a MOTIONLESS release runs: the UPPER half's
        // playhead placement or the LOWER half's audition scrub. That act is
        // the ONLY difference between the halves now, and it is TWO differences
        // read honestly, both pre-existing and neither touched by this ruling:
        // the placement also DESELECTS and MOVES THE CURSOR, while the scrub
        // act touches no selection and no cursor.
        //
        // WHAT THE LOWER HALF GAINS BY BEING A PENDING: for the press's whole
        // life it is a live pointer gesture like the upper half's — every
        // chord is swallowed and follow's page is paused — which is exactly
        // the symmetry the
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
}

void GuiInputHandler::finalize_editor_text_drag() {
    const ActiveEditorText g = active_editor_text(app, audio);
    if (g.valid) {
        // The caret stays where the last seat placed it, no selection
        // standing (every seat collapses it, set_editor_caret_from_x).
        if (g.dialog) viewport.invalidate_dialog_editor_box();
        else          viewport.invalidate_top_strip();
    }
    app.editor_text_drag = EditorTextDragState{};
}

void GuiInputHandler::arm_region_drag_at(int anchor_col, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    // THE ANCHOR IS AUTHORED HERE, at the one arm, from the press COLUMN: the
    // whole source frame through the sweep's one column->trim route (the
    // field's declaration). No playhead is seated (2026-11-07, session 88).
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

// THE ADD SEL SWEEP'S ARM (contract at the declaration): the record armed
// on its `select` former with the down column and the selection standing now.
void GuiInputHandler::arm_selection_sweep_at(int anchor_col, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active        = true;
    app.region_drag.select        = true;
    app.region_drag.anchor_col    = anchor_col;
    app.region_drag.press_x       = x;
    app.region_drag.press_y       = y;
    app.region_drag.prior_members = app.selected_markers;
    app.region_drag.prior_focus   = app.last_selected_marker;
}

// THE ADD SEL SWEEP — THE MEMBERSHIP RULE (architect 2026-11-10, session 101:
// "add sel should allow sweep like trim"; "sweep is in waveform area only";
// "2 agree" — live membership; "disagree - include. they might be the
// target"; "make the last selected in sweep the selection/focus. that's what
// file managers do"). Asked at every motion past the shared gate, from the
// snapshot, so the result is a pure function of the down column and the
// pen's column:
//   * THE MEMBERS are the selection standing at the down (the snapshot,
//     RegionDragState::prior_members) UNION every marker of the ACTIVE
//     COLUMN (the store the marker click selects in, W or P) whose PAINTED
//     COLUMN lies in the swept span — the down column to the pen's, both
//     inclusive, either direction. PULLING BACK drops only what this sweep
//     added, never a prior member; the sweep ADDS ONLY (removal stays the
//     marker click's toggle). DISABLED MARKERS ARE INCLUDED ("they might be
//     the target"; a disabled chip shows nothing of its selection — accepted
//     by the ruling).
//   * THE COLUMN IS THE PAINTER'S (ON SCREEN IS AS PAINTED): the marker's
//     source frame through painted_column_of_source_frame_on_basis on the
//     ITEM basis over the displayed map (displayed_or_live_target_map,
//     item_viewport_basis) — the flag iterator's own shape
//     (iterate_visible_flags_impl, render.cpp: the chip's apex column, the
//     stem's column, the column a tap on that chip resolves), so a marker
//     whose chip stands on a swept column is in, and one past either edge of
//     the waveform (a column outside [0, w), where the pen's column is
//     clamped) is never spanned.
//   * THE FOCUS is the last marker the sweep selected: the swept member that
//     was NOT a prior member nearest the pen's end of the span — rightward
//     (or on the down column) the largest column, leftward the smallest — a
//     tie on one column taking the largest store index (the topmost chip, the
//     one a tap there grabs, hit_test_flag), re-seated as the span recomputes.
//     With no such member the focus is the prior one (or none).
//   * THE WRITE IS THE SELECTION MUTATOR'S (Selection::replace_selection, its
//     publish the damage), and only when the membership or the focus moved,
//     so a motion inside one column republishes nothing. No other paint: the
//     stems appearing are the sweep's picture. The playhead and playback wait
//     for the lift (commit_region_sweep).
void GuiInputHandler::apply_selection_sweep_to_column(int pen_col) {
    RegionDragState& d = app.region_drag;
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.spp <= 0.0) return;
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const int lo = std::min(d.anchor_col, pen_col);
    const int hi = std::max(d.anchor_col, pen_col);
    const bool leftward = pen_col < d.anchor_col;
    std::set<int> members = d.prior_members;
    int swept_focus = -1;
    int focus_col   = 0;
    const int n = active_marker_count(app);
    for (int i = 0; i < n; ++i) {
        const int c = painted_column_of_source_frame_on_basis(
            app, audio, static_cast<double>(active_marker_time_frame(app, i)),
            dmap, basis.vp_start, basis.spp, basis.lattice_origin);
        if (c < lo || c > hi) continue;
        members.insert(i);
        if (d.prior_members.count(i)) continue;
        // Ascending i, so a tie replaces: the largest index on one column.
        if (swept_focus < 0 || (leftward ? c <= focus_col : c >= focus_col)) {
            swept_focus = i;
            focus_col   = c;
        }
    }
    d.swept_focus = swept_focus;
    const int focus = swept_focus >= 0 ? swept_focus : d.prior_focus;
    if (members == app.selected_markers && focus == app.last_selected_marker)
        return;
    selection.replace_selection(std::move(members), focus);
}

// THE SWEEP'S ONE END OWNER (contract at the declaration). Every end path calls
// it — the clean release, on_motion's button-lost arm, the touch hook's end and
// the force-end finalizer — so the disarm and the commit cannot fork, which is
// what the four hand-copied end bodies this replaced could not promise. BOTH
// FORMERS END HERE (the ADD SEL sweep's arm below the disarm).
//
// SELF-GUARDED, so the touch hooks' refused-begin streams and the force-end's
// unconditional call are all free.
void GuiInputHandler::commit_region_sweep() {
    if (!app.region_drag.active) return;
    const bool wrote = app.region_drag.wrote_trim;
    const bool moved = app.region_drag.moved;
    const bool select = app.region_drag.select;
    const int swept_focus = app.region_drag.swept_focus;
    app.region_drag = RegionDragState{};
    if (select) {
        // THE ADD SEL SWEEP IS A ONE-SHOT, TRIM'S RULE (architect 2026-11-10,
        // session 101: "drag once, it's spent"): a sweep that MOVED past the
        // slop puts ADD SEL out, whatever it spanned — a refused or gutter
        // begin arms nothing and never reaches here, the lamp lit. Its off
        // edge damages the lamp's bottom-row face in this frame, as bare `k`
        // does. This also ends the lamp's sticky-ctrl click posture (the
        // ruling's accepted consequence: press it again to keep adding).
        if (moved && app.add_to_selection) {
            app.add_to_selection = false;
            viewport.invalidate_rect(bottom_row_area(app));
        }
        // THE PLAYHEAD LANDS ON THE FOCUS AT THE LIFT, the way a marker click
        // lands it (the playhead is the focus's one mark), behind the click's
        // own playback stop — and only where the span holds a member this
        // sweep added: a sweep that selected nothing moves neither focus nor
        // playhead and stops nothing. The membership was written per motion,
        // so nothing selection-shaped is owed here.
        if (swept_focus >= 0 && app.last_selected_marker >= 0) {
            playback_lifecycle.stop_playback_if_playing();
            land_playhead_on_marker(app, audio, viewport,
                                    app.last_selected_marker);
        }
        return;
    }
    // A COMMITTED SWEEP PUTS THE TRIM ARM LAMP OUT (architect 2026-11-07,
    // session 87; AppState::trim_armed): a sweep that moved past the slop is
    // the act the lamp armed, whatever it wrote — the coincident stroke's
    // reset to the whole song included. A begin that never moved (a refused
    // or gutter begin arms nothing and never reaches here) leaves it lit.
    if (moved && app.trim_armed) set_trim_armed(false);
    // THE COMMIT TAIL IS THE ENDCAP DRAG'S, verbatim and for its own reason:
    // auto_clear_crossed_trim, the repaint, the target-render trigger — at the
    // end only. NO PLAYHEAD WRITE, at the end or before it (architect
    // 2026-11-07, session 88, the rule at the head of input_trim.cpp).
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
    // press-release and a stroke refused by degenerate geometry end here with
    // the trim exactly as they found it, so no render triggers behind them.
    if (wrote) commit_trim_mutation();
}

// ARM THE NAVIGATION SURFACE'S PLAIN PRESS — the pending click (contract at
// ScrollDragState, app_state.h). The press records its point and its surface
// facts and does NOTHING ELSE: no playhead, no deselect, no hide, NO SCRUB —
// nothing pops at press. The three surface
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
    app.scroll_drag.history         = history;
    app.scroll_drag.seed_empty_lane = seed_empty_lane;
    app.scroll_drag.scrub_release   = scrub_release;
}

// ARM A PLACEMENT LANE'S PRESS (architect 2026-09-25: the ruler and the marker
// lane left the navigation surface) — the same pending, the click act and the
// seed unchanged; a crossed press is the no-op every pending's crossing is
// (architect 2026-10-30, on_motion's branch), so a lane's only drag is the
// head drag. FOUR CALLERS, by grep 2026-10-30: the live router's ruler band
// and its empty marker-lane stretch (the one that seeds), and the `h` view's
// router's ruler and empty lane stretch. `head_drag` (architect 2026-10-09
// ~17:40) is the two ruler bands' alone, true where the plain press landed on
// the playhead's head as painted (point_on_playhead_head): the lanes' one
// drag, the playhead riding the pointer past the slop
// (ScrollDragState::head_drag).
void GuiInputHandler::arm_placement_press(int x, int y, bool history,
                                          bool seed_empty_lane,
                                          bool head_drag) {
    arm_nav_press(x, y, history, seed_empty_lane, /*scrub_release=*/false);
    app.scroll_drag.head_drag = head_drag;
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
//   their one click act (arm_placement_press) — so a click on the waveform's upper half
//   or on either lane moves the playhead, in the view as outside it.
//   SCRUB arm (2026-08-13, the waveform's LOWER half): ONE scrub act at the
//   press column and NOTHING ELSE — the act the lower half once ran at the
//   press, moved here whole so that nothing on this surface pops at a press. It is deliberately the FIRST arm and returns ahead of the
//   other two: the scrub selects nothing and moves no cursor, which is the
//   halves' ONE difference (two, read honestly — the
//   omissions are the second). It cannot coincide with the `h` arm (that view
//   has no scrub half), and the scrub's own gutter no-op lives inside
//   scrub_press_at.
// NO SWEEP ARM on any path — the sweep is the TRIM arm lamp's touch drag
// (begin_touch_region), and a click is not a drag. A tap under the lit lamp
// is this same act, the lamp staying lit (architect 2026-11-07, session 87).
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
            // The stems leave with the focus in this event (paint_marker_stems'
            // ONE-STATE RULE, 2026-11-05), before the playhead moves.
            viewport.refresh_flag_cache();
        }
        place_playhead_at_click_column(press_x - area.x);
        return;
    }
    selection.clear_selection();
    place_playhead_at_click_column(press_x - area.x);
}

void GuiInputHandler::place_playhead_at_click_column(int click_rel_x) {
    // THE PLACEMENT PRESS'S PLAYHEAD HALF — the column-to-cursor recipe alone,
    // with no selection in it, so the `h` history mode can seat a playhead by
    // exactly the arithmetic and exactly the playback regime the live click
    // uses (run_nav_click_act's mode arm clears the MODE's focus where the
    // live arm clears the store selection). A gutter column seats nothing.
    const GuiRect area = waveform_area(app);
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    // Clamp the click column's frame into the live domain: move_playhead_to
    // clamps internally, but the clamp here stays required because the
    // conversion reads ItemViewportBasis::spp, the PAINTED item epoch, which
    // can extend past the CURRENT live domain — a resize or a target-domain
    // contraction after the last item commit leaves the lagging painted
    // basis's last column computing to domain_total or beyond, one past
    // [0, domain_total-1], which the display-state validator would clear
    // wholesale. THE COLUMN CONVERTS ON THE PAINTED VIEWPORT, the item basis
    // (playhead_frame_at_click_column; architect 2026-09-24, strictly as
    // painted): the playhead seats at the frame the clicked pixels showed even
    // while a viewport job in flight has moved the live viewport ahead of
    // them; cold, the live viewport by the basis's own contract.
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    // A POINTER OR TOUCH PLACEMENT STOPS A LIVE PLAY (architect 2026-10-06; the
    // class is stated at stop_playback_if_playing's declaration): past the
    // gutter refusal above and ahead of the first write, so a click that seats
    // nothing stops nothing. This is the ONE site for every placement route,
    // the click act's (the sweep seats no playhead since 2026-11-07, session
    // 88; its stop is the trim write's own).
    playback_lifecycle.stop_playback_if_playing();
    viewport.move_playhead_to(sample);
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
    // drops legal there alone) and P in both audio views (S+P and T+P, each
    // with the lead-in, architect 2026-10-12), so the
    // view dispatch below needs no extra audio-view guard on either column.
    // The drops' reason channel carries two refusals. THE OCCUPIED PLACE
    // (architect 2026-11-11, the drop coincidence rule;
    // kPhaseResetDropOccupiedCard / kMarkerDropOccupiedCard) this road CAN
    // meet on the P column: the lead-in reset lands kN/2 output samples left
    // of the clicked column, so the stretch right of a reset is empty lane,
    // and a second double-click where the first just dropped asks for a
    // reset in the window that one now seeds in — refused, on its card, the
    // playhead left where the click seated it. On the W column a marker on
    // the clicked column's exact frame paints its cue on that column, so the
    // press is ordinarily the cue's and not the empty lane's. THE OFF-EDGE
    // REFUSAL (kMarkerDropOffEdgeCard) this road cannot meet: the playhead was
    // just seated on a painted column (< area.w on the painted viewport, so at
    // or left of it at the right wall), and the lead-in lies further left
    // still. Whatever comes back is raised, the channel's contract.
    const GuiOpRefusal refusal =
        app.active_markers_view == 'P'
            ? phase_resets.drop_phase_reset_lead_in_at_playhead()
            : warpops.drop_copy_previous_at_playhead();
    if (refusal)
        notifications.notify(AppState::NotificationClass::Normal, *refusal);
}

// `mods` HAS ONE READER, the tooltip's held bit at the top. No button is
// named: the touch translation's press is the one the core delivers
// (architect 2026-10-30, the mouse closed on the tablet).
void GuiInputHandler::on_button_release(int x, int y, GuiInputState mods) {
    // THE TOOLTIP'S HELD BIT, above every return: the platform's own answer
    // with this release (the release reads up), so a wait may start again.
    // AND ANY RELEASE IS THE TOOLTIP'S HARD END, as any press is (a click
    // hides the hint): the column forgotten, so the next walk with the pen
    // hovering on a column — its restore motion at the lift point — is an
    // arrival and waits the delay (AppState::RedesignTooltip).
    app.redesign_tooltip.button_held = mods.primary_button_held;
    hide_shift_tooltip();
    // THE NOTIFICATION CARD'S LIFT (architect 2026-10-01), FIRST — the press
    // claim's mirror at the press claim's rank: the card's claim outranks
    // every veil, so its lift must outrank their releases too. Guarded on the Card arm alone, which
    // only that claim sets, so it claims nothing that is not its own; the
    // arm is taken whole here (take_chrome_press, which resumes the card's
    // clock) and the act is the lift's (finish_notification_release).
    if (app.chrome_press.kind == AppState::ChromePress::Kind::Card) {
        finish_notification_release(take_chrome_press(), x, y);
        return;
    }
    // THE ON-SCREEN KEYBOARD'S OWED KEY-UP, above every gate — the press's
    // mirror. It is guarded on the held key
    // index alone, which only that surface's own press ever sets, so it claims
    // nothing that is not its; and it fires even when the press's act closed
    // the editor under it, because a delivered key-down owes its pair whatever
    // became of the surface (contract at the declaration).
    if (finish_onscreen_keyboard_release()) return;
    // (THE DROPDOWN'S RELEASE stood here, above every gate but the card's
    // lift, until the Settings popup retired, 2026-10-21.)
    // THE CHROME ARM IS TAKEN WHOLE, above every gate below (2026-08-13, the
    // act-at-release conversion; a CARD arm was taken at the top of this
    // body): the hold ends with this release whatever
    // consumes it, so the arm cannot be stranded by an early
    // return — and whether the act RUNS is decided further down, after the
    // modal gates have had their say, so a prompt raised under the hold
    // swallows the lift exactly as it swallows every other pointer event.
    const AppState::ChromePress chrome = take_chrome_press();
    // THE PROMPT DIALOG'S ACT, the press claim's other half (2026-08-13): the
    // lift on the button the press armed activates that response. A lift
    // ANYWHERE ELSE consumes the arm and dispatches nothing (a prompt has no
    // focus, architect 2026-11-02, so nothing else is left behind). The gates the act re-asks (the painted bit, the stash's
    // identity, and the live response set) all live in the one shared dispatch
    // body, which the keyboard's own release shares; the painter drops the arm
    // on those same edges, so the body is the second wall rather than the only
    // one. The veil still swallows every release either way — a CHROME arm
    // taken above dies here undispatched, which is the veil's answer.
    if (app.prompt.active) {
        dispatch_modal_dialog_button(take_modal_dialog_release(x, y));
        return;
    }
    // THE PROGRESS MODAL'S RELEASE (2026-10-14), its press block's mirror:
    // the controls row's armed Cancel through the one shared dispatch, every
    // other lift consumed — the veil's answer. (A lift that arrives AFTER THE
    // BATCH'S COMPLETION SWAPPED THE MODAL FOR THE RENDER PLAYER finds no arm:
    // the swap drops it, GuiInputHandler::swap_batch_progress_for_player.)
    if (app.batch_progress.active) {
        dispatch_modal_dialog_button(take_modal_dialog_release(x, y));
        return;
    }
    // THE FOLDER OVERLAY'S ROW ARM ENDS HERE, the hoisted claim's mirror and
    // at the same rank: a motionless lift HIGHLIGHTS THE ROW AND OPENS IT (a
    // click activates, 2026-08-29), a scroll drag simply
    // ends. It is a no-op with no arm standing, so it costs the other
    // releases one test. The act may take the whole surface down under this
    // frame — a wav's play rebinds the engine, a project row's open requests
    // the reopen (its question standing over the picker, or the session
    // ending at once on a clean tab) — which is why the arm is cleared at the
    // head of that body and this arm returns immediately after it.
    if (finish_folder_overlay_release(x, y)) return;
    // THE OVERLAYS' VEILS ADMIT TWO KINDS OF CHROME ARM, the one predicate
    // the three release blocks below read: A ROSTER ARM — THE OPENER'S (its
    // lift the overlay's close) OR THE TOOLTIP LAMP'S (architect 2026-11-10,
    // "tips should remain open in all situations": its lift bare backslash,
    // the overlay's router's own arm); the press-time painted-bit gate
    // already consumed every other roster key, which the overlay greys —
    // and THE STRUCK CLIP
    // LAMP'S (architect 2026-11-05 night: "we should make it respond under
    // the overlay" — the tap reopens the dead device there as on the main
    // page, nothing playing; render.h's THE TWO LAMPS' STRIKE). They lift
    // through the chrome act's own body, whose overlay term admits exactly
    // these (finish_chrome_press_release's head: overlay_admits_button for a
    // roster arm, the Lamp kind's exemption); any other chrome arm
    // dies in the block undispatched, the veil's answer. THE MODALS still
    // veil the lamp: a prompt and the progress modal return above, an editor
    // dialog is that body's own refusal.
    const bool overlay_admits_chrome =
        chrome.kind == AppState::ChromePress::Kind::Roster ||
        chrome.kind == AppState::ChromePress::Kind::Lamp;
    // THE RENDER PLAYER'S OTHER RELEASES (2026-08-28), the press block's
    // mirror: the scrub's marker drag (the seek commits here) and the modal
    // row's armed button through the one shared dispatch. Every other lift
    // is consumed — a chrome arm taken above dies here undispatched, the
    // veil's answer — but the chrome arms the veil admits
    // (overlay_admits_chrome): the Play Renders arm, the roster button
    // the player leaves lit (architect 2026-10-13), the tooltip lamp's
    // (architect 2026-11-10) and the struck clip
    // lamp's (architect 2026-11-05 night), which are lifted below.
    if (app.render_player.active) {
        // THE PLAY RENDERS BUTTON'S LIFT (architect 2026-10-13), THE
        // TOOLTIP LAMP'S (architect 2026-11-10) AND THE
        // STRUCK LAMP'S (architect 2026-11-05 night), the press block's
        // mirror: the chrome arms the player's veil admits dispatch
        // through the chrome act's own body.
        if (overlay_admits_chrome) {
            finish_chrome_press_release(chrome, x, y);
            return;
        }
        if (finish_player_scrub_release(x, y)) return;
        // THE SHIFTED-TWIN VERDICT IS READ BEFORE THE TAKE, which clears
        // the arm the hold is measured against (2026-08-28; the term's two
        // roads are at modal_dialog_press_shifted).
        const bool shifted = modal_dialog_press_shifted();
        dispatch_modal_dialog_button(take_modal_dialog_release(x, y),
                                     shifted);
        return;
    }
    // THE PICKER'S RELEASE (2026-08-28), its press block's mirror: the modal
    // row's armed Cancel through the one shared dispatch, and every
    // other lift consumed — the veil's answer — but the chrome arms the
    // veil admits (overlay_admits_chrome): the opener's (Open Project,
    // Settings under the settings page and the value list, Presets
    // under its list, BPM under its form), the roster button the picker
    // leaves live as its way out (2026-10-25), the tooltip lamp's
    // (architect 2026-11-10) and the struck clip lamp's (architect
    // 2026-11-05 night), lifted through the chrome act's own body as the
    // player's are.
    if (app.picker.active) {
        if (overlay_admits_chrome) {
            finish_chrome_press_release(chrome, x, y);
            return;
        }
        dispatch_modal_dialog_button(take_modal_dialog_release(x, y));
        return;
    }
    // THE SCORE VIEWER'S RELEASE (2026-10-26), its press block's mirror: the
    // chrome arms the veil admits (overlay_admits_chrome) — the SCORE
    // key's, the tooltip lamp's (architect 2026-11-10) and the struck clip
    // lamp's (architect 2026-11-05 night) —
    // through the chrome act's body, the page scrub's end, the armed row key
    // through the one shared dispatch; every other lift consumed.
    if (app.score_viewer.active) {
        if (overlay_admits_chrome) {
            finish_chrome_press_release(chrome, x, y);
            return;
        }
        if (finish_score_scrub_release()) return;
        // The shifted-twin verdict before the take, as the player's.
        const bool shifted = modal_dialog_press_shifted();
        dispatch_modal_dialog_button(take_modal_dialog_release(x, y),
                                     shifted);
        return;
    }
    // THE EDITOR DIALOG'S ACT, the same shape over the other surface: the lift
    // on the armed OK / Cancel runs the session's own Enter / Esc through the
    // one modal key route. Above the text-drag branch because the two are
    // mutually exclusive by construction (a press on a button never reaches
    // the field claim), and above the editor swallow below, which is
    // where an unarmed release still ends. THE DIALOG ALONE IS MEANT: these
    // are a dialog's own OK / Cancel, and a lane editor publishes none.
    if (modal_dialog_editor_active()) {
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
    if (chrome.kind != AppState::ChromePress::Kind::None) {
        finish_chrome_press_release(chrome, x, y);
        return;
    }
    // F2.1: a release ending an editor-text caret drag ends it before the
    // modal swallow below. IT SEEDS NO DOUBLE-CLICK (architect 2026-11-08,
    // session 95: the field's double tap is retired; a tap and a drag are
    // its whole vocabulary).
    if (app.editor_text_drag.active) {
        finalize_editor_text_drag();
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;

    // THE TRIM-BAR FRAMING DOUBLE-CLICK'S SEED, resolved for every release
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
        // The pending press resolves at its release (contract at
        // ScrollDragState, app_state.h). A MOVED press was no click and began
        // nothing, so it owes nothing: no act — the head drag's included
        // (2026-10-09), its every motion past the slop having run the act
        // already, so its lift ends it where the last motion placed it.
        // A MOTIONLESS press is THE DEFERRED CLICK — run_nav_click_act at the
        // press column, running THE PRESSED HALF'S OWN ACT: the upper half's
        // placement (deselect / mode-land, play stop, playhead)
        // or the lower half's audition SCRUB (2026-08-13),
        // plus the EmptyLane double-click seed when the press was the marker
        // lane's empty stretch (release-side seeding, the TrimBar pattern: only
        // the release knows it stayed a click). THE ACT IS PRESS-TIME: a
        // plain-armed press runs its act even with ctrl down at the release
        // (press-time modifiers arm the act — the statement at
        // AppState::ChromePress, THE RULE'S SCOPE; architect 2026-10-30).
        const bool moved     = app.scroll_drag.moved;
        const bool history   = app.scroll_drag.history;
        const bool seed_lane = app.scroll_drag.seed_empty_lane;
        const bool scrub     = app.scroll_drag.scrub_release;
        const int  press_x   = app.scroll_drag.press_x;
        app.scroll_drag = ScrollDragState{};
        if (moved) return;
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
        // THE SWEEP wrote the trim (or ADD SEL's, the selection) live during
        // the drag (see on_motion); the release runs the shared commit tail
        // through the gesture's ONE end owner, which every end path calls. A MOTIONLESS press-release (never
        // crossed the threshold) wrote nothing, so the owner just disarms —
        // the begin was the placement and its own act is done.
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
        // bar's ctrl bound set (the contract is at PendingClickAct,
        // app_state.h; the 2026-08-17 ruling took the record's other four
        // kinds back to the press). The act runs on the PRESS column,
        // re-asking every gate live inside set_trim_bound_at_click, where
        // the nearest end cap is chosen too (architect 2026-11-10).
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
        // THE SEED'S OWN, which is the release — the family's rule (TrimBar
        // and EmptyLane stamp their motionless release too), so the
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
// force-end route in the product: the window's close road (on_window_close),
// main.cpp's resize callback and the history prefetch's failed-scan closer
// (the no-cancel rule is stated in full at on_key's drag-modal statement,
// input_handler.cpp). It replaced cancel_active_drags — POINTER GESTURES
// HAVE NO CANCEL (architect 2026-07-29), so there is nothing to restore anywhere
// here: every live gesture COMMITS what stands, and undo is the mitigation.
// Homed beside on_button_release deliberately — these are the same bodies in the
// same order, each now a call to the gesture's own end owner.
// The gestures are mutually exclusive in practice, so this reads as a chain of
// no-ops around the one that is live.
// ITS MEMBERSHIP IS any_pointer_gesture_active's, EXACTLY (re-grepped
// 2026-09-16): the two lists must agree, because a caller's whole promise is
// that what follows lands on a gesture-free state, and that predicate is what
// "gesture-free" means to the keyboard and the touch navigation. THE
// TWELVE, in this body's order: the editor text drag, the marker reposition
// drag, THE VALUE DRAG (2026-09-10, the flag's vertical one), the
// trim drag, the sweep (region_drag), the pending click (scroll_drag),
// the three pendings (marker press, trim, deferred click), and THE RENDER
// PLAYER'S TWO ARMS — the folder overlay's row press and the play-scrub's
// marker drag, which joined this body 2026-08-28 through their own hard-end
// clears — and THE SCORE VIEWER'S PAGE SCRUB (2026-10-26), its own clear,
// which commits nothing: both faces show the press's page again, nothing
// written, and the soundtrack's face seeks nothing. (THE
// POPUP LIST'S SCROLL HOLD was a twelfth 2026-10-08 to
// 2026-10-14, the choice editor's list's, the color picker's gesture and its
// two lists' holds beside it until that morning.) WHAT EACH LEAVES DIFFERS
// AND EACH ARM SAYS SO: the drags COMMIT what stands, and the pendings and
// the player's arms commit NOTHING (a force-end is not a click — the
// standing abnormal-end rule).
void GuiInputHandler::finalize_active_drags() {
    // Editor text-selection drag: FINALIZE (collapse a no-motion anchor to a
    // caret), the same act its release performs — selection-only, nothing to
    // revert. It seeds NO double-click candidate here: a force-end is not a click
    // (the clean release owns that seeding). No key reaches on_key during
    // this drag (its gate there: the caret drag owns the one touch).
    if (app.editor_text_drag.active) finalize_editor_text_drag();
    // The marker reposition drag commits its PROPOSED positions (the overlay
    // becomes the store) and pushes the one undo entry iff the drag netted a
    // change — the release path exactly, so Ctrl+Z reverts it.
    if (app.drag.active) marker_drag.commit_drag();
    // THE VALUE DRAG COMMITS TOO (2026-09-10) — the release path exactly, so
    // a resize or a WM close leaves the value the hand had
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
    // something this force-end introduces. The ADD SEL sweep's selection
    // stands as written and its end runs as the release's (the lamp out, the
    // land), the same owner.
    commit_region_sweep();
    // The pending click MERELY DISARMS, moved or not — a force-end is not a
    // click, so the deferred click act does NOT run, and a crossed press began
    // nothing (ScrollDragState, app_state.h), the head drag's steps already
    // placed.
    if (app.scroll_drag.active) app.scroll_drag = ScrollDragState{};
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
    clear_score_scrub_drag();
    // (THE POPUP LIST'S SCROLL HOLD ended here 2026-10-08 to 2026-10-14 and
    // went with the settings choice editor's list.)
    // A force-end is not a clean click sequence, so no candidate may survive to
    // pair with a later click (the standing rule at every non-release gesture
    // end) — and neither may the trim bar's press record, which would otherwise
    // seed one at the next release.
    app.double_click   = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
}

// THE ROSTER'S POINTER WALK over the whole roster (row 1's three menu
// anchors, the icon row's and the bottom row's members: the painters'
// membership tables own the counts (app_state.h's bottom-row head), the
// enum's own count at kRedesignButtonCount — the stash is
// AppState::redesign_buttons; a MODAL's yield leaves a bottom-row member with
// a zero rect, as do the icon row's overflow rule and its history stand-ins a
// member that does not stand, and a zero rect contains no point). It answers
// two readers from the remembered position — the TOOLTIP's wait (the
// button whose column holds a hovering pointer, handed to note_tooltip_hover
// at the tail) and THE HOT TOOLBAR BUTTON
// (AppState::roster_hot; set_roster_hot pays its damage) — which stores none
// since the cases became the program's (2026-10-09: Cool Edit's toolbar has
// no hover face under any chrome, program_spec.h's case_hot_face). The
// rects are the painter's stashes, so the button a hint names is the
// painted button, its column read off the painted face
// (tooltip_column_rect, render.h).
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
    // the lift's own delivery, whose restore motion walked every hover face
    // unlatched, so the two toolbars' latched answer — none, the face painted
    // under the tap's capture, and since 2026-10-09 the program's cases' only
    // answer (no hover face under any chrome, so the rest face by
    // construction) — is installed here before the frame paints: the roster
    // by its own re-walk (whose hot write stores -1), the player's hot button
    // directly (its walk,
    // update_modal_dialog_hover, also owns the dialog's tooltip wait, which
    // the arm has no business re-noting). The lit rows need nothing: the
    // restore motion left them on the row the contact last lit, and their
    // walks hold it from here.
    pen_hot_latch_ = PenHotLatch{.armed = true, .anchored = false,
                                 .x = 0, .y = 0};
    recompute_redesign_button_hover();
    set_player_hot(-1);
}

void GuiInputHandler::clear_pen_hot_latch() {
    pen_hot_latch_.armed = false;
}

void GuiInputHandler::recompute_redesign_button_hover() {
    // IT REFUSES WHILE THE POINTER IS OUTSIDE THE WINDOW: the remembered coordinates name a point INSIDE the
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
    // NO CUE'S WAIT RUNS UNDER A MODAL OR AN OVERLAY — the no-wait rule asked
    // for an owner that is no roster key (tooltip_dwell_suppressed, the
    // rule's one expression; the tail's cue arm reads this). A KEY is asked
    // the same owner by its own index inside the walk, at the column the
    // pointer is on: under an overlay the keys it leaves live answer false.
    const bool cue_wait_refused =
        tooltip_dwell_suppressed(/*roster_key=*/-1);
    // THE MODALS' VEIL (2026-08-12; every editor since 2026-10-25): under a
    // PROMPT, ANY EDITOR or THE PROGRESS MODAL the WHOLE roster is refused —
    // nothing behind the modal is pressable, so no roster button is under
    // the pointer for a hint either (keyboard_modal_editor_active, the one
    // predicate for a keyboard-taking modal; the lane editors raised no veil
    // until then, their roster presses never blocked).
    // THE OVERLAYS ARE DELIBERATELY NOT TERMS HERE (2026-09-03 evening):
    // each leaves its opener key and the tooltip lamp live
    // (overlay_admits_button — redesign_button_enabled's overlay arm), and
    // under them a key's hint is the no-wait rule's to refuse or admit, key
    // by key (tooltip_dwell_suppressed: those two keep their hints, architect
    // 2026-11-11; every other key is dead and silent).
    const bool modal_veil =
        app.prompt.active || keyboard_modal_editor_active() ||
        app.batch_progress.active;
    int     hovered_tip = -1;
    GuiRect tip_column{0, 0, 0, 0};
    int     hot         = -1;
    for (int i = 0; i < kRedesignButtonCount; ++i) {
        const AppState::RedesignButtonFace& f = app.redesign_buttons[i];
        const RedesignButton id = static_cast<RedesignButton>(i);
        // A zero-width stash (before that row's first paint) contains no point,
        // and the pre-motion (-1, -1) cursor is outside every rect, so both cold
        // states resolve to "not under the pointer" without a special case.
        //
        // There is no in-window term: the whole walk refused above. AND NO
        // ENABLED TERM (architect 2026-08-07): a disabled button still explains
        // itself, kdenlive's own behaviour. (The hover zone's open-dropdown
        // term went with the popup, 2026-10-21.)
        // The face's `contains`: Play's second rect, row 8's clock, raises
        // the key's own tooltip (2026-10-27).
        const bool under_pointer = !modal_veil && !under_card &&
                                   f.contains(mx, my);
        // THE HOT CANDIDATE (stored by no write while the program's case has
        // no hot face, below): the toolbar button under the pointer on the
        // same gates — and NO keyboard-modal term:
        // that refusal is the tooltip's dwell, while a button the flag editor
        // leaves pressable lights as it presses. The rects are disjoint, so
        // the first is the only one, and the tooltip's break below cannot
        // pass it by.
        if (under_pointer && hot < 0) hot = i;
        // THE TOOLTIP'S TARGET IS THE KEY'S COLUMN (architect 2026-11-07,
        // session 87; tooltip_column_rect, render.h): the face, its legend's
        // pad and half the gap each side — wider than the face the press
        // reads — and Play's clock field beside it. The rect the pointer is
        // in goes to the writer with the owner (the bypass chain measures
        // from it at the leave). The columns of a run tile, so the walk's
        // first hit is the only one.
        if (modal_veil || under_card) continue;
        const GuiRect column = tooltip_column_rect(f.rect);
        const bool on_column = rect_contains(column, mx, my);
        if (!on_column && !rect_contains(f.field, mx, my)) continue;
        // THE NO-WAIT RULE, ASKED OF THE KEY THE POINTER IS ON (architect
        // 2026-11-11: under an overlay the live band keys show their
        // tooltips): refused under a modal — the veil above already said so
        // — and under an overlay for every key but the two it leaves live,
        // its opener and the tooltip lamp (tooltip_dwell_suppressed, on
        // overlay_admits_button — the face's, the lit bit's and the lift's
        // one membership, so the key that answers a press there is the key
        // that explains itself). Asked after the column test, so one key a
        // walk at most reaches it.
        if (tooltip_dwell_suppressed(i)) continue;
        // MEMBERSHIP IS THE CONSTANT TABLE'S (2026-09-01): this asks only
        // whether the button HAS a tooltip — a null line 1 (the menu row's
        // exclusion until 2026-10-21; every roster button has one since); the
        // stateful overload
        // (whose every arm returns a non-null line 1) is the painter's, which
        // reads the words at paint time.
        if (redesign_button_tooltip(id).line1 != nullptr) {
            hovered_tip = i;
            tip_column  = on_column ? column : f.field;
            break;
        }
    }
    // THE HOT FACE'S ONE WRITE (AppState::roster_hot): the candidate while
    // the program's case has a hot face and no chrome press is armed —
    // comctl32 shows no hot item while a toolbar holds the capture — and
    // none otherwise. COOL EDIT'S CASE HAS NONE (program_spec.h's
    // case_hot_face, false since 2026-10-09), so this stores none.
    // AND NONE WHILE THE PEN'S HOT-FACE LATCH HOLDS (pen_hot_latch_holds,
    // the rule at pen_hot_latch_, architect 2026-10-07; every hover-lit face
    // since 2026-10-09): after a pen lift the tapped button reads at rest —
    // the face painted under the tap's capture — until the pen moves off its
    // anchor.
    set_roster_hot(kProgramSpec.case_hot_face &&
                           app.chrome_press.kind ==
                               AppState::ChromePress::Kind::None &&
                           !pen_hot_latch_holds()
                       ? hot
                       : -1);
    // THE ARMED ROSTER PRESS'S INSIDE BIT (AppState::ChromePress::inside,
    // the press face's gate, redesign_button_pressed_face; back 2026-11-05
    // with the press look), maintained here because this walk is the one
    // body that runs on every motion AND every tick under a standing arm: is
    // the pointer inside the armed target's published face? Both of the
    // face's rects (`contains`): a press on row 8's clock holds the PLAY
    // key down while the pointer stays on the clock. The bit serves the
    // paint alone — the RELEASE re-hits the armed target's rect at its own
    // coordinates, so a slide off the key runs nothing at the lift without
    // this walk's help — and a crossing damages the key's home strip, the
    // lit flip's own damage (main.cpp's comparator; row 8's fork). A CARD
    // ARM IS NOT WALKED: the card paints no pressed face.
    if (app.chrome_press.kind == AppState::ChromePress::Kind::Roster) {
        const bool inside =
            app.redesign_buttons[static_cast<size_t>(app.chrome_press.index)]
                .contains(mx, my);
        if (inside != app.chrome_press.inside) {
            app.chrome_press.inside = inside;
            invalidate_roster_press_face(app.chrome_press.index);
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
    // button whose column it found (or none) to the wait's one writer,
    // note_tooltip_hover, which decides by the held button, the chain and the
    // box's own owner; the tooltip's clock (tick_tooltip) decides visibility.
    // Nothing here does. THE WALK RUNS ON EVERY HOVER MOTION, ON GROUND TOO
    // (on_motion's no-gesture tail and every modal branch), and on every tick
    // with no gesture standing (main.cpp), so the writer sees the pointer
    // leave a column and the chain's lapse is asked off the band as on it.
    //
    // NO WAIT RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT — NOR UNDER AN
    // OVERLAY BUT FOR THE KEYS IT LEAVES LIVE (its opener and the tooltip
    // lamp, architect 2026-11-11; tooltip_dwell_suppressed) — and this refusal
    // is what makes "a tooltip never floats over a modal" hold rather than merely
    // start out true. The modal branches of on_motion still call this walk
    // (the dialog owner's liveness below is a pointer fact a modal does not
    // freeze), so without this line a motion
    // under a prompt or an editor would start a wait and the tick would raise
    // a FLOATING hint over the modal. A hint is a second surface, it hangs
    // past the strip, and the chord it names is exactly what the modal gate
    // is swallowing. Forcing "no owner" here
    // rather than gating the tick keeps the wait's writer in one place; the
    // modal's OPEN edge is a hard end of its own (on_key's or the press's, or
    // the platform close's), which takes any standing box down at once.
    // (THE DROPDOWN needed no term of its own here until it retired,
    // 2026-10-21: the hover zone refused the whole roster under it.)
    //
    // A STANDING DIALOG'S WAIT IS THE DIALOG'S (2026-08-13, when the modal's
    // buttons took tooltips): this walk answers for the ROSTER's index space
    // and, since 2026-11-09, the marker lane's cues only, IN EVERY STATE, so
    // with neither under the pointer it hands "none" to the writer (a soft
    // end for a standing ROSTER or MARKER box),
    // HARD-ENDS a DIALOG owner — the wait's or the standing box's — whose
    // surface is gone or replaced, and leaves a DIALOG wait whose surface
    // stands and still wears the stash that armed it: the modal's own walk
    // (update_modal_dialog_hover) owns that surface's wait. A DIALOG SURFACE
    // IS LIVE exactly where on_motion runs that walk, re-derived by grep
    // 2026-10-25: the prompt, the editor dialogs and the progress modal (the
    // veil's terms — a lane editor among them publishes no dialog, so a
    // dialog owner has no surface to outlive there) and the folder overlay's
    // two owners (the player and the picker — folder_overlay_stands). The
    // progress modal joined 2026-10-25: without it the walk hard-ended its
    // Cancel's wait on every motion, so that hint never showed. The liveness test reads the LIVE
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
    // The picker hosts its own two questions the same way since 2026-10-25
    // (the reopen's and Revert's, standing over its list), so the stamp
    // answers there too: the prompt closed by a key release leaves the
    // picker live and a wait stamped with the prompt's owner dead. THE STASH LAGS THE CLOSE BY ONE
    // PAINT: the
    // frame that repaints the player's row republishes the stash,
    // paint_shift_tooltip refuses the mismatch on that same frame, and this
    // walk's hide lands on the tick after it, so no hint of the wrong surface
    // is ever painted.
    // Under the veil `hovered_tip` is -1 by construction (the veil and the
    // no-wait rule above), so the veil needs no branch of its own. Under AN
    // OVERLAY, which is no veil term, `hovered_tip` is -1 through the no-wait
    // rule wherever the pointer is not on one of the two keys the overlay
    // leaves live, and the "surface stands" arm is what
    // keeps the player's and the picker's button hints alive: handing "none" to the writer there would stop the dialog's wait
    // on every tick. ON ONE OF THOSE TWO KEYS the walk falls through to the
    // roster's own arrival below, as on the main page: the writer takes a
    // standing dialog box down (another owner) and the dialog walk, finding
    // none of its buttons under the pointer and a roster owner standing,
    // hands nothing (update_modal_dialog_hover's tail).
    if (hovered_tip < 0) {
        AppState::RedesignTooltip& t = app.redesign_tooltip;
        const bool dialog_surface_live =
            modal_veil || folder_overlay_stands(app) ||
            score_viewer_stands(app);
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
            hide_shift_tooltip();
            return;
        }
        if (t.hovered.surface == AppState::RedesignTooltip::Surface::Dialog &&
            t.hovered.index >= 0)
            return;
        // THE MARKER LANE'S CUES (architect 2026-11-09, session 100: "with
        // tooltips on, hover should show resolved value for marker. same
        // placement as for icons"; the Marker owner at AppState::
        // RedesignTooltip): with no key's column under the pointer, the cue
        // the press would take — the press's own walk and arbitration over
        // the published rects (marker_cue_hit) — is the owner when the value
        // copy would act on it (marker_tooltip_value non-empty), the rect
        // handed to the chain its face. Under the same gates as a key: no
        // veil, no card over it, the no-wait rule (a cue's under every
        // overlay, the lane covered there); the `h` view and the P
        // column answer no cue through the two owners. Asked only while the
        // TIPS lamp is lit — dark, the writer holds the wait clear anyway, so
        // the lane's hit and the value are not composed for nothing.
        if (app.show_tooltips && !modal_veil && !under_card &&
            !cue_wait_refused) {
            GuiRect face{0, 0, 0, 0};
            const int cue = marker_cue_hit(app, mx, my, &face);
            if (cue >= 0 && !marker_tooltip_value(app, audio, cue).empty()) {
                note_tooltip_hover(
                    {AppState::RedesignTooltip::Surface::Marker, cue}, face);
                return;
            }
        }
        note_tooltip_hover(AppState::RedesignTooltip::Owner{},
                           GuiRect{0, 0, 0, 0});
        return;
    }
    // Keying on the id is what makes a direct Render->Paste motion a new
    // arrival (at once while the chain holds, the delay otherwise) rather
    // than a continuation of the last one; the rule is at note_tooltip_hover.
    note_tooltip_hover({AppState::RedesignTooltip::Surface::Roster,
                        hovered_tip},
                       tip_column);
}

// THE CHROME PRESS'S ARM — the press half of act-at-release (architect
// 2026-08-13; the rule's authoritative statement and the state's contract
// are at AppState::ChromePress) for every redesigned button whose action IS a chord —
// rows 1, 3 and 4 and the bottom row, driven entirely by kToolbarChords'
// per-button flags so no row carries a special case of its own. Returns true
// when a button's rect claimed the press, whether or not anything was armed (a
// refusal is still a consumed nothing, which is what the band claims want).
//
// THE PRESS DISPATCHES NOTHING. It applies the refusal that was always
// press-time — the disabled consume — and then ARMS: the index and the press
// stamp the long press is measured against (no modifier: a press carrying
// the barrel's ctrl never reaches here, the band gate's consume, architect
// 2026-11-08) — and damages the key's home strip for its press face (the
// press arc, 2026-11-05; invalidate_roster_press_face). The lift runs the
// act through
// finish_chrome_press_release below, which re-asks every gate, the face's
// bits as painted (architect 2026-09-24).
bool GuiInputHandler::arm_redesign_press(int x, int y) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (!redesign_button_hit(app, tc.id, x, y)) continue;
        // A DISABLED BUTTON'S PRESS IS A CONSUMED NOTHING: nothing arms, so
        // nothing can dispatch at the lift, and a long press is swallowed
        // exactly like a tap (one bit, both routes — a greyed
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
        // ITERATION LOCK: the VIEW PAIRS' FOUR (Source / Target and Warp /
        // Phase reset, two radio pairs since 2026-10-14) answer false while grid
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
        // Play on its launch and A/B predicates, Jump to Defining Marker on
        // the jump's and its copy twin's — so this line consumes exactly the presses whose
        // chord would be a consumed no-op. Inside the view the derived
        // partition adds its own: the PLAY/STOP button, UP and DOWN, the four
        // verbs and Jump to Defining Marker (Space, the bare verticals and
        // the verbs' chords are consumed there) — all of them dead by
        // construction, so since 2026-10-07 none of them is PAINTED in the
        // view (history_mode_hides_button: an empty rect, which no press
        // reaches) — while the LEFT / RIGHT chords (its own playhead step since 2026-09-26, admitted;
        // the arrow buttons are not painted in the view, Older / Newer being
        // the painted, state-greyed pair) and the walk pair, Center and
        // Switch Tab (the diff-flag cycle, the centring, the admitted tab
        // switch) take no partition grey. ADD TO SELECTION IS
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
        // A VIEW PAIR'S LIT MEMBER ARMS LIKE ANY OTHER BUTTON, and its lift
        // dispatches like any other since 2026-10-23 night (the pop: its key
        // selects the pair's other member); it repeats nothing (no view pair
        // row carries `repeats`).
        // THE ARM, whose press face the band's and row 8's painters read
        // through redesign_button_pressed_face (the inside bit starts true:
        // a press is inside what it hit; materials.h's head, 2026-11-05).
        // NO CTRL PRESS ARMS: the band's modifier gate consumes every press
        // carrying the barrel's ctrl before this body (architect 2026-11-08,
        // the barrel is no chrome modifier — chrome_band_modifiers_refused),
        // so the arm's ctrl bit is false on every roster arm. THE
        // PRESS'S CLOCK IS STAMPED HERE, unconditionally: the lift measures the
        // hold against chrome_shift_hold_ms() to decide the SHIFT LONG PRESS, and
        // the stamp is taken for every button rather than for the
        // shift-admitting ones alone (redesign_button_shift_admits owns that
        // membership), a press having a time whatever it landed on.
        const int64_t now = monotonic_ms();
        // Designated: the record's fields are its contract, never a position.
        app.chrome_press = AppState::ChromePress{
            .kind     = AppState::ChromePress::Kind::Roster,
            .index    = redesign_button_index(tc.id),
            .press_ms = now};
        invalidate_roster_press_face(app.chrome_press.index);
        // THE HOLD-REPEAT'S ARM (architect 2026-08-16), for the rows that
        // carry `repeats`. ELIGIBILITY IS JUDGED UNDER THE PRESS-TIME CONTEXT
        // and it is the KEYBOARD'S OWN PREDICATE SHARED, not mirrored:
        // repeat_eligible is exactly what the platform's arming probe asks for
        // a held on-screen key, so the button's hold arms in precisely the
        // contexts a held key would — and refuses in the ones it refuses, which is what
        // keeps a press some surface consumes from arming a burst that would
        // start firing when that surface closes under a held button. Nothing
        // has dispatched by this line and nothing will until the lift, so
        // "press-time" is simply the state this press found.
        //
        // THE CHORD IS THE LIFT'S MINUS ITS LONG-PRESS TERM: the row's own
        // columns alone. Undo's and Redo's ctrl (and
        // Redo's shift) come from tc.ctrl / tc.shift, so the arm asks about
        // exactly Ctrl+Z / Ctrl+Shift+Z, the arrows about their bare keys.
        //
        // THE LONG-PRESS TERM IS ABSENT: A HELD REPEAT OUTRANKS THE
        // LONG-PRESS SHIFT (the principle at ToolbarChord::repeats) — THE
        // LIFT EXCLUDES EVERY `repeats` ROW FROM THE HOLD-AS-SHIFT READING
        // outright, so the burst's chord and the lift's agree by construction
        // instead of by the two landing on the same beat.
        if (tc.repeats) {
            GuiInputState chord{};
            chord.ctrl  = tc.ctrl;
            chord.shift = tc.shift;
            chord.alt   = tc.alt;
            // The fixed beat, never the hold delay: the schedule's rule at
            // the tick's repeat body below.
            if (repeat_eligible(tc.key, chord))
                app.chrome_press.repeat_due_ms = now + kHoldBeatMs;
        }
        return true;
    }
    return false;
}

// THE ARM, TAKEN WHOLE — on_button_release's first act on every left release,
// armed or not, so a release consumed by any gate below it (the veil, an
// editor swallow) still ends the hold. The caller owns what happens next;
// this owns only the state and — for a CARD arm (2026-10-01) — the held
// card's clock, which resumes here because
// the arm that held it is gone (every end of a card's hold comes through
// this body: its lift, and the button-lost and pointer-leave clears).
AppState::ChromePress GuiInputHandler::take_chrome_press() {
    const AppState::ChromePress arm = app.chrome_press;
    app.chrome_press = AppState::ChromePress{};
    if (arm.kind == AppState::ChromePress::Kind::Card) {
        notifications.press_hold_edge(arm.card_id);
        return arm;
    }
    // A ROSTER ARM'S ERASE (2026-11-05): a face painted pressed — the arm
    // inside its key — is un-pressed by the same damage the arm paid.
    if (arm.kind == AppState::ChromePress::Kind::Roster && arm.inside)
        invalidate_roster_press_face(arm.index);
    // A LEVEL ARM'S END (2026-11-05, the sliders; AppState::ChromePress's
    // Level kind): EVERY ROAD OUT OF THE ARM COMES THROUGH HERE — the lift,
    // the button-lost and the pointer-leave clears — and every one of them
    // is the drag's end, the level staying where the last motion put it:
    // the noise's -inf release and the one config write where the level
    // moved (settle_level_case). The lift's own act, the tap's toggle,
    // follows in finish_level_press_release on a press that moved nothing,
    // so the two writes are never both owed.
    if (arm.kind == AppState::ChromePress::Kind::Level)
        settle_level_case(arm.level_changed);
    return arm;
}

// THE PRESS FACE'S DAMAGE for the roster button at `index` — the arm, the
// lift and the walk's inside crossing (the press arc, architect 2026-11-05):
// the key's HOME STRIP, the damage the lit flip's comparator pays for the
// same pixels (main.cpp) — row 8's fork, a transport-row face damaging its
// own bottom-strip lane, every other face the top strip.
void GuiInputHandler::invalidate_roster_press_face(int index) {
    if (redesign_button_in_transport_row(static_cast<RedesignButton>(index)))
        viewport.invalidate_rect(bottom_row_area(app));
    else
        viewport.invalidate_top_strip();
}

// -- THE LEVEL CASE'S ARM (architect 2026-11-05; the contract at
//    AppState::ChromePress's Level kind, the declarations' comment,
//    input_handler.h) ---------------------------------------------------------

// THE PRESS: the band claim's second half (claim_button_row_press, after
// arm_redesign_press found no chord row's face under the press). A press on
// the level case's PAINTED face — the published rect, point_on_key through
// `contains` — arms the Level kind with the press's x, y, the level's travel
// fraction and the clock (the stamp every arm takes; this kind's lift
// measures no hold); A DISABLED CASE'S PRESS IS A CONSUMED NOTHING,
// read as the roster's is, off the painted bit (RedesignButtonFace::enabled,
// strictly as-painted). No face damage: a slider has no press look.
bool GuiInputHandler::arm_level_case_press(int x, int y) {
    const RedesignButton id = RedesignButton::IconNoiseLevel;
    const AppState::RedesignButtonFace& face =
        app.redesign_buttons[static_cast<size_t>(redesign_button_index(id))];
    if (!face.contains(x, y)) return false;
    if (!face.enabled) return true;
    app.chrome_press = AppState::ChromePress{
        .kind     = AppState::ChromePress::Kind::Level,
        .index    = redesign_button_index(id),
        .press_ms = monotonic_ms(),
        .x0       = x,
        .y0       = y,
        .t0       = level_travel_fraction(app.device_config->noise_level)};
    return true;
}

// THE DRAG (on_motion, under a standing Level arm, the button held): until a
// motion has left the hover slop on either axis (kHoverSlopW, render.h, its
// one reader this) the press is still a tap and moves
// nothing — a wobble under a resting pen must not nudge the level and leave
// the file behind the voice. Past it the press is a DRAG for good: on this
// and every later motion the level is the EXACT taper of t0 + (x − x0) / the
// travel, clamped to [0, 1] (level_db_from_travel, a double; y
// ignored), applied live where it differs from the level standing
// (apply_level_case), wherever the pointer is — on the face, off it, over
// the canvas: the arm runs to the lift, the scrub thumb's shape, no
// slide-off feint. `level_changed` records that some motion moved the
// level, which is what the end's one write is owed for.
void GuiInputHandler::update_level_press_motion(int x, int y) {
    AppState::ChromePress& arm = app.chrome_press;
    if (arm.kind != AppState::ChromePress::Kind::Level) return;
    if (!arm.moved) {
        const int slop = hover_slop_px();
        if (std::abs(x - arm.x0) <= slop && std::abs(y - arm.y0) <= slop)
            return;
        arm.moved = true;
    }
    const double travel = static_cast<double>(level_travel_px());
    double t = arm.t0 + static_cast<double>(x - arm.x0) / travel;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double db = level_db_from_travel(t);
    if (db == app.device_config->noise_level) return;
    apply_level_case(static_cast<RedesignButton>(arm.index), db);
    arm.level_changed = true;
}

// THE LIFT (finish_chrome_press_release's Level arm, under the veil's say):
// A PRESS THAT NEVER LEFT THE SLOP, LIFTED ON THE CASE'S FACE (the published
// rect, the derive doctrine), IS THE TAP, AND THE TAP TOGGLES THE CASE OFF
// AND BACK (architect 2026-11-10, "like the fit recall"; the rule at
// RedesignButton::IconNoiseLevel) — WHATEVER THE PRESS'S DURATION: the long
// press that set 0 dB (2026-11-05) is retired ("awkward on something that
// slides"), so no hold is measured here. ON THE CASE STANDING AT A FINITE
// LEVEL the tap REMEMBERS that level (GuiLevelRecall, the app's, through
// AppState::level_recall — the tap its one writer: a drag that ends at -inf
// remembers nothing and no drag clears it) and sets -inf; ON THE CASE STANDING
// AT -inf it sets the remembered level, 0 dB WITH NOTHING REMEMBERED (his
// ruling; empty at the launch). THE LEVEL GOES THE DRAG'S OWN ROAD:
// apply_level_case (the live struct, the voice, the face's damage), then
// settle_level_case with the level moved (one config write; the case's
// -inf frees the bed's bank there, as a drag's lift on -inf does, and its
// recall from -inf is GuiNoise::set_level_db's RISE — the loader started
// afresh on its own thread, the freed bank's state Idle, the bed fading in
// when the load lands; nothing on that road asks for a motion before it).
// The two levels always differ (one finite, one -inf), so the tap always
// writes. A moved press's level stands where the last motion put it,
// settled by the take.
void GuiInputHandler::finish_level_press_release(
        const AppState::ChromePress& arm, int x, int y) {
    if (arm.moved) return;
    const RedesignButton id = static_cast<RedesignButton>(arm.index);
    if (!redesign_button_hit(app, id, x, y)) return;
    const double standing = app.device_config->noise_level;
    std::optional<double>& remembered = app.level_recall->noise;
    double db = kLevelDbOff;
    if (standing != kLevelDbOff)
        remembered = standing;
    else
        db = remembered.value_or(0.0);
    apply_level_case(id, db);
    settle_level_case(/*level_changed=*/true);
}

// -- THE STRUCK CLIP LAMP'S ARM (architect 2026-11-05 night, session 49; the
//    contract at AppState::ChromePress's Lamp kind, the rule at render.h's
//    THE TWO LAMPS' STRIKE) ----------------------------------------------------

// THE PRESS: the band claim's third half (claim_button_row_press, after
// arm_redesign_press and arm_level_case_press found nothing). WHILE THE
// STRIKE STANDS, AND ONLY THEN (AppState::clip_lamp_struck, the tick's
// bit, so the press acts on the picture last painted), a press on the clip
// lamp's square — logo_lamp_rects(...).clip, the very rect the tick
// damages, so the hit and the picture agree — arms the Lamp kind with the
// clock; unstruck the lane stays inert and the press claims nothing. No
// face damage: a lamp is not a key and wears no press look. UNDER AN
// OVERLAY THE TAP ANSWERS AS ON THE MAIN PAGE (architect 2026-11-05 night:
// "we should make it respond under the overlay"): the render player's, the
// picker's and the score viewer's veils let the icon row's band through to
// this claim, and their release blocks lift the Lamp arm beside the
// opener's and the tooltip lamp's (overlay_admits_chrome, on_button_release; the overlay term's
// exemption at finish_chrome_press_release's head). THE MODALS ALONE VEIL
// IT: a prompt's or the progress modal's press gate returns above the band,
// and an editor dialog standing at the lift refuses it there.
bool GuiInputHandler::arm_clip_lamp_press(int x, int y) {
    if (!app.clip_lamp_struck) return false;
    const GuiRect r = logo_lamp_rects(app, app.logo_legend).clip;
    if (!rect_contains(r, x, y)) return false;
    app.chrome_press = AppState::ChromePress{
        .kind     = AppState::ChromePress::Kind::Lamp,
        .press_ms = monotonic_ms()};
    return true;
}

// THE LIFT (finish_chrome_press_release's Lamp arm, under the veil's say):
// ON THE SQUARE STILL (re-hit at the release's own coordinates, the derive
// doctrine: a lift slid off the square does nothing, the keys' feint) AND
// THE STRIKE STILL STANDING (a tick may have cleared it under the hold: no
// second touch can reopen, the touch window having one owner, but the car's
// media Play can, GuiCarTransport::on_media_command), the launch gates'
// device question,
// GuiPlayback::ensure_device_available_for_play: the dead stream closed
// (the noise voice's sequence reset first, close_dead_stream), reopened at
// the new route's rate and STARTED, so the bed resumes through the running
// callback and NOTHING PLAYS — no launch, no engine play, the tablet's
// silence between plays. A FALSE answer (the reopen refused, or a device
// that never came up, which no press opens) cards
// kPlaybackDeviceUnavailableCard as the gates do — a device asked for and
// none given; a true answer raises nothing, the next tick's refresh
// clearing the strike and damaging the square (refresh_logo_lamps).
void GuiInputHandler::finish_clip_lamp_press_release(int x, int y) {
    if (!app.clip_lamp_struck) return;
    const GuiRect r = logo_lamp_rects(app, app.logo_legend).clip;
    if (!rect_contains(r, x, y)) return;
    if (!playback.ensure_device_available_for_play())
        notifications.notify(AppState::NotificationClass::Normal,
                             kPlaybackDeviceUnavailableCard);
}

// THE LIVE APPLY, ONE BODY FOR THE DRAG AND THE TAP (the case is
// the level's one road: no Settings row since 2026-11-05 night,
// kSettingsEditorKeys): the live struct takes the level, the bed's voice
// takes it at the next fill — GuiNoise::set_level_db (the gain, the level
// plus the fixed attenuation, and the switch: a rise from -inf lights the
// voice and starts the loader, a fall to -inf fades it out; nothing freed
// here, noise.h) — and THE CASE'S FACE RECT IS DAMAGED, the
// published face (the number and the fill both inside it; the legend on the
// ground does not change): THE LEVEL'S ONE DAMAGE — the per-tick comparator
// watches enabled, selected, glyph and drawing, no level, so every edge of
// the case's value pays its own damage here, as the press arc's
// invalidate_roster_press_face pays the press's. The case not standing (an
// empty rect, no pixel to be stale) damages nothing.
void GuiInputHandler::apply_level_case(RedesignButton id, double db) {
    app.device_config->noise_level = db;
    if (app.noise != nullptr) app.noise->set_level_db(db);
    const GuiRect& face =
        app.redesign_buttons[static_cast<size_t>(redesign_button_index(id))]
            .rect;
    if (face.w > 0 && face.h > 0) viewport.invalidate_rect(face);
}

// THE END'S TAIL, every road's (take_chrome_press for the lift and the two
// clears, finish_level_press_release after the tap's apply): THE
// NOISE'S -inf RELEASE — the case left at -inf frees the bed's bank behind
// the fence (GuiNoise::release_if_silent, a no-op above -inf or with
// nothing loaded; never per motion, noise.h's head) — and ONE CONFIG WRITE
// where the level moved (persist_device_config, the device keys' one
// persist: a failure cards, the live value standing).
void GuiInputHandler::settle_level_case(bool level_changed) {
    if (app.noise != nullptr) app.noise->release_if_silent();
    if (level_changed) (void)settings_editor.persist_device_config();
}

// THE CHROME ACT, AT THE LIFT — the release half of act-at-release (architect
// 2026-08-13). The lift runs the act iff it lands ON the armed target — the
// published rect re-hit at the release's own coordinates, the derive doctrine
// (the arm's inside bit serves the paint alone, AppState::ChromePress) — and iff
// every press-time gate still holds, re-asked here exactly as the modal
// dialog's release
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
// same order, with nothing restated and nothing that can drift. WHAT MOVED is
// only WHEN it fires — the keyboard's own chords still dispatch on the key
// PRESS; button-is-its-chord is about what a button runs, not when.
void GuiInputHandler::finish_chrome_press_release(
        const AppState::ChromePress& arm, int x, int y) {
    // THE VEIL, RE-ASKED ONCE FOR EVERY KIND: a chrome lift is a consumed
    // nothing while an editor dialog stands, whatever the arm is. It was a
    // membership test until 2026-08-13, admitting the modal-trap
    // reach-through's own buttons; with that retired the veil is blanket
    // again, and its whole job here is a modal RAISED MID-HOLD — no key can
    // arrive under a held button (the touch window has one owner, main.cpp's
    // set_on_key hook), but an asynchronous raise can (a parked batch's
    // progress modal), and an arm taken before the surface rose must not fire
    // into it. IT SITS ABOVE THE KIND SWITCH because the rule is every arm's and
    // none has an exception to it. (Carried per-branch it once was on the
    // roster's and missing from the deleted walk-tab kind's, so arming a
    // Remote/Local tab, raising the dialog mid-hold and lifting on the tab
    // switched the walk under the editor — which is why the term is stated
    // once, above the switch, rather than per branch.)
    // A PROMPT needs no term here for any kind: on_button_release's prompt
    // gate returns unconditionally above this call, so no arm reaches this
    // body while one stands — and THE OVERLAYS' release blocks (the render
    // player's, the picker's, the score viewer's) return above this call the
    // same way for every arm but the ones their veils admit, which meet the
    // overlay term below. The terms below are the surface RAISED MID-HOLD's —
    // THE PROGRESS MODAL (2026-10-14) of a batch parked behind a killed
    // render's drain, which can rise under a held button — and every overlay
    // and editor takes the same refusal through them, though no key can open
    // one under a hold.
    // EACH OVERLAY ADMITS THREE ARMS, ITS OPENER'S, THE TOOLTIP LAMP'S AND
    // THE STRUCK LAMP'S, the
    // release blocks' overlay_admits_chrome read again here. THE OPENER'S
    // (architect 2026-10-13 for Play Renders under the player, 2026-10-25 for
    // Open Project under the project picker, 2026-10-28 for Settings under
    // the settings page and the value list it swaps in, that night for Trim
    // Presets under its list, 2026-10-29 for BPM under its form — the tag's
    // answer, is_folder_overlay_opener; 2026-10-26 for the SCORE key under
    // the score viewer, is_overlay_opener, the family): that button, lit
    // under its overlay, is a roster arm the veil admits and its lift
    // is the overlay's close. THE TOOLTIP LAMP'S (architect 2026-11-10,
    // "tips should remain open in all situations"): the other roster arm
    // every overlay admits, its lift bare backslash, answered by the
    // standing overlay's router with the lamp's one setter — the two roster
    // members one predicate, overlay_admits_button (app_state.h), the face's
    // own. THE STRUCK CLIP LAMP'S (architect 2026-11-05
    // night: "we should make it respond under the overlay"): the Lamp kind is
    // exempt from THE OVERLAY TERM ALONE — the editor and progress terms
    // still refuse it, the modals veiling the lamp as every arm — and the
    // kind is tested FIRST because a Lamp arm's index is -1, no button for
    // overlay_admits_button to read. Any other arm under an overlay — a
    // Level arm among them, a case being dead there — is the
    // mid-hold case above and stays refused.
    const bool overlay_refuses =
        (app.render_player.active || app.picker.active ||
         score_viewer_stands(app)) &&
        arm.kind != AppState::ChromePress::Kind::Lamp &&
        !overlay_admits_button(app, static_cast<RedesignButton>(arm.index));
    if (keyboard_modal_editor_active() || overlay_refuses ||
        app.batch_progress.active) return;
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
    case AppState::ChromePress::Kind::Level:
        // THE LEVEL CASE'S LIFT (2026-11-05): no chord to build — its one
        // act is the tap's toggle (2026-11-10), its own body; the veil
        // above has had its say (a modal raised mid-hold swallows the act,
        // the drag's level standing and already settled by the take).
        finish_level_press_release(arm, x, y);
        return;
    case AppState::ChromePress::Kind::Lamp:
        // THE STRUCK CLIP LAMP'S LIFT (2026-11-05 night): no chord — its one
        // act the device question, its own body; the veil above has had its
        // say (a modal raised mid-hold swallows it).
        finish_clip_lamp_press_release(x, y);
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
        // The press-time refusal the lift re-asks — the enabled bit ON THE
        // PAINTED FACE, never the live predicates (architect 2026-09-24,
        // strictly as-painted: the lift asks what the screen shows). (A lift on
        // a view pair's lit member was the consumed nothing here from
        // 2026-10-07 evening until its pop, 2026-10-23 night; it dispatches its
        // key now, ToolbarChord's NO RADIO COLUMN.) The press arms and damages
        // the strip, so by the lift the painted bits are at most one frame old,
        // and the per-tick comparator carries any drift under the hold onto the
        // face. A face painted grey is a consumed nothing; a face painted live
        // dispatches, and where its act has become refused inside that frame
        // the act's own body answers — carding where its key would card, silent
        // only where its silence is ruled — rather than the lift consuming a
        // press the screen advertised.
        const AppState::RedesignButtonFace& face =
            app.redesign_buttons[static_cast<size_t>(arm.index)];
        if (!face.enabled) return;
        // THE RENDER BUTTON IS CANCEL WHILE A RENDER IS LIVE (architect
        // 2026-08-11) — THE ROSTER'S ONE RULED EXCEPTION TO "THE BUTTON IS ITS
        // CHORD": while the face is PAINTED as Cancel (its stashed
        // glyph, whose Render arm at redesign_button_glyph
        // is queue_running and holds the face's contract) the lift runs THE
        // CANCEL ACT ITSELF (cancel_archival_session, the render cancel's one
        // body) and dispatches no chord at all. The divergence is
        // his ruling, both halves: Ctrl+Alt+R keeps its own
        // semantics unchanged (a dispatch kills the running render and starts
        // a new one — the render-dispatch rule), while the BUTTON "doesn't
        // need to exist while nothing's rendering" and so becomes the cancel —
        // the one road to a single render's cancel since bare Esc's went with
        // the hardware keyboard (architect 2026-10-30).
        // THE SHIFT LONG PRESS CANCELS TOO: one face, one act — while the
        // button IS Cancel, letting the hold slip through to the miscellaneous
        // render would start a render from a button that says Cancel. It
        // takes that answer by construction: this branch
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
        // shifted twin. IT IS THE POINTER'S ONE SHIFT (architect 2026-10-30: no
        // pointer carries a shift bit), so without it pen and finger could
        // reach only the plain half of each shifted pair.
        //
        // THE MEMBERSHIP IS redesign_button_shift_admits AND NOT A LIST, so a
        // button with no shifted chord is held for as long as you like and
        // still gets its plain act.
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
        // IT COMPOSES WITH NO CARRIED CTRL: the band gate consumes every press
        // carrying the barrel's ctrl before the arm (architect 2026-11-08,
        // the barrel is no chrome modifier — chrome_band_modifiers_refused),
        // so the hold's shift is the lift's one modifier. (From 2026-09-26 to
        // then a carried ctrl composed with it where the pair was admitted:
        // the walk's march, COPY's state paste and FIT's reset; each is a
        // key's own chord or hold now.)
        //
        // AND IT REACHES NO HOLD-REPEATING BUTTON: A HELD REPEAT
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
        // tick finds repeat_fired still false), so a repeating row admitting
        // a shift would dispatch its shifted twin where the user was owed a
        // plain step. So the exclusion is
        // read off kToolbarChords' own `repeats` column — the arm's
        // membership, never a second list — which makes it guaranteed instead
        // of timing-dependent, for every `repeats` row alike. NO `repeats` ROW
        // ADMITS SHIFT (architect 2026-10-30: Up / Down's shifted ten-step
        // went, the four arrows and Undo / Redo admitting none), so the term
        // is the principle held structurally rather than a live fork today.
        // A NON-REPEATING shift-admitting button is untouched: the hold is its
        // road to its twin.
        const bool held_to_shift =
            !tc.repeats && redesign_button_shift_admits(tc.id) &&
            monotonic_ms() - arm.press_ms >= chrome_shift_hold_ms();
        // The shift term ORs the table's own (Redo's Ctrl+Shift+Z, PREV's
        // Shift+Tab) with the hold — well-defined because no row sets both
        // the table bit and the admission (see shift_admits), so this one
        // expression spells both members of each shifted pair. THE CTRL IS
        // THE ROW'S OWN ALONE (Save's Ctrl+S, PASTE's Ctrl+P, whose hold is
        // then Ctrl+Shift+P): the pointer carries none into a chord.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift || held_to_shift;
        chord.alt   = tc.alt;
        // OPEN PROJECT'S TWIN TRANSLATES (architect 2026-10-07), the roster's
        // one exception to "a modified press spells its button's own key
        // under those modifiers": its shifted press — the long press, exactly
        // as on every shift-admitting button — runs
        // REVERT, whose chord is Ctrl+Alt+O with no shift (is_revert_project_key,
        // gui_input.h, unchanged; Ctrl+Shift+O binds nothing). So the shift
        // becomes alt here and the keyboard's spelling stays as it is: the
        // icon's twin is the icon road's. The act's own prompt guards it and
        // its refusals card at revert_project.
        if (tc.id == RedesignButton::OpenProject && chord.shift) {
            chord.shift = false;
            chord.alt   = true;
        }
        // THE `h` VIEW'S SAVE IS THE PUBLISH CHORD (save_button_publishes).
        if (save_button_publishes(tc.id, app.history_mode.active))
            chord.shift = true;
        on_key(tc.key, chord);
        return;
    }
}

// THE CHROME BUTTON HOLD-REPEAT, fired from the run loop's tick (architect
// 2026-08-16): while a press stands on a button whose chord row sets `repeats`
// — the bottom row's four cardinal arrows and the icon row's Undo / Redo (the
// column is the membership) —
// synthesize that button's chord on the keyboard's own
// cadence, so a held BUTTON walks at the speed the held KEY does. It works
// with NO TOUCH-SPECIFIC CODE: the one-finger translation delivers an
// ordinary press and an ordinary release, so it arms and ends this arm, and
// its motion deliveries keep the position this body re-hits honest.
//
// THE SCHEDULE'S TWO NUMBERS COME FROM DIFFERENT OWNERS ON PURPOSE. The FIRST
// fire is one kHoldBeatMs after the press — the product's FIXED beat, matched
// to the architect's laptop compositor delay (its source), and DECOUPLED FROM
// THE HOLD DELAY (architect 2026-09-29): a repeat delay measures the cadence of
// a stream of repeats, as the double-click window and the tap coalesce
// measure the gap between two presses, and none of them is a hand resting on
// a thing until it changes meaning, which is what kHoldDelayMs times (the
// readers' inventory is at kHoldBeatMs). Every
// LATER fire is THE PLATFORM'S ADVERTISED KEY-REPEAT INTERVAL
// (GuiPlatform::key_repeat_period_ms), read PER FIRE so the two holds cannot
// drift apart.
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
// only the three PER-FIRE questions below, which answer differently on purpose:
// off the button PAUSES (the scrollbar-button rule — sliding back on resumes,
// the hold standing throughout; a leave that exits the WINDOW ends the hold
// outright through the pointer-leave hook, and this body's first line sees the
// arm gone), a dead PAINTED enabled bit PAUSES, the claim's own bit since
// 2026-09-24 and the disabled-press consume's mirror (and since 2026-09-13
// THE HISTORY'S END for Undo / Redo: a burst that walks
// the history to its end meets the dead bit on its next fire and rests there
// under the held pointer, greyed, nothing un-pausing it while the hold
// stands, and the lift that ends a fired burst is consumed, their faces
// greying on an empty stack and a locked target tab through
// history_step_actionable and its companions), and lost eligibility DISARMS — a
// context that revoked the burst ends it
// rather than parking it. One fire per due tick, the next scheduled from NOW
// rather than accumulated, so a stalled frame yields one repeat and no burst.
void GuiInputHandler::tick_chrome_press_repeat() {
    AppState::ChromePress& arm = app.chrome_press;
    if (arm.kind != AppState::ChromePress::Kind::Roster) return;
    if (arm.repeat_due_ms == 0) return;
    const int64_t now = monotonic_ms();
    if (now < arm.repeat_due_ms) return;
    arm.repeat_due_ms = now + gui.key_repeat_period_ms();
    const RedesignButton id = static_cast<RedesignButton>(arm.index);
    // PAUSED OFF THE BUTTON. The published rect re-hit at the pointer's
    // remembered position — the lift's own derive doctrine (the arm's inside
    // bit serves the paint alone, AppState::ChromePress).
    if (!app.pointer_in_window ||
        !redesign_button_hit(app, id, app.last_mouse_x, app.last_mouse_y))
        return;
    for (const ToolbarChord& tc : kToolbarChords) {
        if (tc.id != id) continue;
        // THE FIRE'S CHORD IS THE ARM-TIME BUILD'S (arm_redesign_press), the
        // same expression — the row's own columns alone, so "what the burst
        // was judged eligible for" and "what it fires" cannot drift.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift;
        chord.alt   = tc.alt;
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
        // chord meets applies per fire.
        on_key(tc.key, chord);
        return;
    }
}

// (THE DROPDOWN'S ITEM HIT, ITS RELEASE AND ITS ACTIVATION — dropdown_item_at,
// finish_dropdown_release, activate_dropdown_item, the one act-on-release
// surface the chrome roster modeled itself on — RETIRED 2026-10-21 with the
// Settings popup.)

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
// release like everywhere else), the touch hold = the view-local region
// former. The RULER and the MARKER lane's empty
// stretches are PLACEMENT SURFACES in here as outside (architect 2026-09-25,
// the lanes leaving the navigation surface): a motionless
// plain click lands the playhead through the mode's land, and every
// drag there does nothing.
//
// THE VIEW IS SILENT (architect 2026-08-05): NO PRESS IN HERE STARTS AUDIO —
// the mode's full-song trim forces a full target preview render for an
// audition the view never needed. Playback removal is whole: Space left the
// keyboard allowlist in the same ruling, and the entry owner stops a session
// that was already running. (The mode's deferred click act runs the shared
// placement body, whose play stop is a no-op in here.)
//
// NO MODIFIED PRESS PASSES THROUGH (architect 2026-10-30: the barrel tap on
// the waveform arms nothing, live or here): a ctrl press is the diff flag's
// membership toggle or a consumed nothing — over the TRIM BAR it would set a
// bound, which is a write.
//
// THE ACTS, all pure navigation:
//   * a PLAIN press on the NAVIGATION SURFACE arms the mode's own PENDING
//     CLICK (arm_nav_press with the history flag): nothing at
//     press; a motionless release runs THE MODE'S LAND — clear the mode focus
//     + selection (the deselect's mode analog, through the one pair clearer),
//     seat the playhead at the press column (run_nav_click_act's history arm — the
//     live recipe through the shared placement body, whose play stop has
//     nothing to stop in the silent view); a crossed press runs nothing,
//     moving no playhead and clearing nothing (the pan is the touch layer's,
//     architect 2026-10-30). The ruler and the empty lane stretches
//     take the same pending as PLACEMENT SURFACES (arm_placement_press,
//     2026-09-25), so their motionless clicks land the playhead and a drag
//     there does nothing but the ruler's head drag.
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
//   * the MARKER LANE's CTRL click, on its FLAG BOXES: it toggles one flag's
//     membership, then focuses the clicked flag and lands the playhead on it
//     (the live selection model over the mode's own list; the store selection
//     stays as untouched as the plain click leaves it). IT HAS A SECOND
//     PRODUCER since 2026-09-17 — a PLAIN press while Add to selection stands,
//     the plain claim's own fork above. A ctrl lane press that hits NO flag
//     answers exactly as in the live views: a consumed nothing — the lanes
//     being placement surfaces since 2026-09-25.
//   * (NO SWEEP: the view cannot edit the trim, and its playhead-only former,
//     the touch hold's mode arm, retired with the hold 2026-11-07; the TRIM
//     arm lamp is inert in here, GuiInputHandler::touch_sweep_armed.)
//
// THE SYMMETRY RULING (architect 2026-08-06) is why the acts read as they do:
// THE HISTORY VIEW AND THE REGULAR VIEWS ANSWER A WAVEFORM CLICK IDENTICALLY,
// and the regular views' standing model is the model. THE WAVEFORM RESOLVES NO
// FLAG AT ALL — SELECTION is LANE vocabulary (the flag boxes' plain and ctrl
// clicks), the stems pointer-inert in all contexts (the seventh glass ruling).
//
// EVERYTHING ELSE IS A CONSUMED NO-OP — the marker clicks' drag arm, the empty-lane marker-CREATE double-click (the
// EmptyLane candidate is never seeded in here — the mode's pending never sets
// seed_empty_lane — so the create cannot fire; authoring), the trim bar's
// WRITING routes (the endcap and bridge drags and the ctrl bound-set click,
// none of which the framing double-click above touches — it is the band's
// SECOND click, exactly as outside), and every ctrl press that is not the
// claim above.
bool GuiInputHandler::handle_history_mode_press(
        int x, int y, GuiInputState mods,
        const DoubleClickCandidate& dc_at_press) {
    const bool ctrl = mods.ctrl;

    // THE MODE'S NAVIGATION SURFACE, from the ONE geometry owner: the whole
    // waveform and nothing else (the lanes left it 2026-09-25). It has been
    // the full waveform height in here since playback left the view
    // (2026-08-05), and since 2026-08-13 the LIVE surface is the same rect —
    // the two halves became one out there too — so the mode's surface is no
    // longer a special case and there is nothing left for a mode term to say.
    // It is also the plain press's waveform band at the walk's foot below
    // (2026-10-10, the live router's own reading): where the waveform is
    // painted, so the keyboard's rows over the canvas take no press.
    const bool on_nav_surface = point_on_nav_surface(app, x, y);

    // THE MULTI-SELECTION'S MODIFIED CLICK (architect 2026-08-05), the one
    // modified press in this mode that hits an ITEM: ctrl toggles one flag's
    // membership, then focuses the clicked flag and lands the playhead on it —
    // the live selection model re-expressed over the mode's own list, with the
    // store selection as untouched as the plain click leaves it. FLAG BOXES
    // ONLY (the symmetry ruling, 2026-08-06). EVERY OTHER CTRL PRESS IS A
    // CONSUMED NOTHING, as in the live views (architect 2026-10-30: the
    // barrel tap on the waveform arms nothing; the lanes being placement
    // surfaces since 2026-09-25).
    if (ctrl) {
        const GuiRect lane = top_marker_row_area(app);
        if (rect_contains(lane, x, y)) {
            const int hit = hit_test_flag(app, audio, x, y);
            if (hit >= 0) {
                // THE PRESS ACTS (2026-08-17: a diff-flag press has no drag to
                // become — nothing in this mode drags a marker and the flag
                // box claims the press whole — so its identity is certain and
                // the one-day lift deferral of 2026-08-15 is inverted): the
                // toggle, the focus move, the land and the region clear run
                // HERE, on the live hit, on the cue it hit (a nudged pair's
                // two each their own, 2026-10-26).
                select_history_diff_flags_modified(
                    hit, hit_test_history_diff_half(app, x, y));
            }
        }
        return true;
    }

    // PLAIN FROM HERE. The band walk mirrors the live press router's: the
    // ruler is a placement lane (arm_placement_press, 2026-09-25) whose
    // playhead head is the head drag in here as outside (2026-10-09 ~17:40,
    // the mode's land at every placement; the diff flags rank first), the
    // trim bar keeps its framing double-click, the marker lane splits
    // flag-vs-stretch, and the waveform is the navigation surface. Each lane
    // is its RECT on both axes, the live walk's (2026-10-10: the program
    // frame's columns beside the inset lanes are inert ground and fall to the
    // tail's consumed nothing — the PLACEMENT LANES block, above point_on_playhead_head).
    {
        const GuiRect ruler = top_ruler_row_area(app);
        if (rect_contains(ruler, x, y)) {
            arm_placement_press(
                x, y, /*history=*/true, /*seed_empty_lane=*/false,
                /*head_drag=*/hit_test_flag(app, audio, x, y) < 0 &&
                    point_on_playhead_head(app, x, y));
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
        if (rect_contains(trim_bar, x, y)) {
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
    if (rect_contains(lane, x, y)) {
        // hit_test_flag SERVES THE MODE UNCHANGED: while the mode stands the
        // painter's stash holds the diff flags' rects, published by the same
        // pass that built app.history_mode.flags, so the index it returns is an
        // index into that list — a same-frame pair claiming as the one rect it
        // is painted as, a nudged pair's two cues each the pair's index with
        // its own half (hit_test_history_diff_half, read beside it below). A
        // cold stash answers -1, which is the
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
            // `ctrl || (add_to_selection && cell == MarkerCell::Payload)` at
            // run_marker_click_act's one toggle term, and in here its extra
            // half costs nothing to spell, because the structure already
            // guarantees it: the router's ctrl branch claims every ctrl press
            // above, so PLAIN is all this arm ever sees, and A DIFF
            // FLAG PAINTS NO CELLS: it publishes both cell boundaries at its
            // own rect's right edge, so hit_test_flag_cell answers Payload
            // over the whole of one and the payload half is vacuously true in
            // here. THE EMPTY-LANE STRETCH IS UNTOUCHED, the lamp governing
            // FLAG presses alone as it does on the live lane
            // (AppState::add_to_selection's scope rule).
            const HistoryDiffHalf half = hit_test_history_diff_half(app, x, y);
            if (app.add_to_selection)
                select_history_diff_flags_modified(hit, half);
            else
                focus_history_diff_flag(hit, half);
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
    if (on_nav_surface) {
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
// IT LANDS THROUGH THE MOVEMENT OWNER (land_playhead_on_domain_frame). The
// out-of-range arm (`hit` < 0, tolerance the router cannot produce) lands
// nothing; the empty-lane click the router DOES produce goes to the placement
// pending's act (run_nav_click_act), which places the playhead there. (It hid the trim region
// overlay from 2026-08-06 until the resting overlay was deleted on
// 2026-09-22.)
// The rest of the minimalism STANDS: no store selection, no live focus, no
// auto-select, no playback stop.
void GuiInputHandler::focus_history_diff_flag(int hit, HistoryDiffHalf half) {
    const int was = app.history_mode.focus;
    const bool had_selection = !app.history_mode.selection.empty();
    // THROUGH THE ONE PAIR CLEARER (2026-08-06, closing the one route that
    // cleared the pair inline): the EMPTY-LANE answer below is a clear of both
    // fields, so it goes through the owner like every other clearer, and the
    // hit arm simply re-seats the focus over it. The return is deliberately
    // unread — the damage decision below is finer than "anything stood".
    clear_history_mode_focus(app.history_mode);
    if (hit >= 0 && hit < static_cast<int>(app.history_mode.flags.size())) {
        app.history_mode.focus      = hit;
        app.history_mode.focus_half = half;
    }
    // THE STEMS IN THIS EVENT (2026-11-05, paint_marker_stems' ONE-STATE
    // RULE): the mode's focus and selection are the diff lane's selected
    // look, its stems among it, so the flag cache is rebuilt here, before the
    // land below can paint the playhead beside the old stems
    // (Viewport::refresh_flag_cache, fingerprint-guarded).
    viewport.refresh_flag_cache();
    if (app.history_mode.focus >= 0) {
        // THE CUE THE PRESS HIT is the landing (a nudged pair's two cues each
        // their own, 2026-10-26).
        land_playhead_on_domain_frame(
            app, audio, viewport,
            history_diff_flag_landing_domain_frame(
                app, audio,
                app.history_mode.flags[
                    static_cast<std::size_t>(app.history_mode.focus)],
                app.history_mode.focus_half));
    }
    // A DISCRETE COMMAND: full-window damage when the focus actually moved
    // (the selected look moves — the stems, the chips keeping their fill,
    // render.h's THE STATES; the columns stay put), and none when it
    // did not — a re-click on the focused flag re-lands a playhead that is
    // already there, and the land owner is itself idempotent; a press on a
    // nudged pair's other cue moves focus_half alone, which paints nothing
    // (both cues wear the item's look, 2026-11-04) — its landing moves the
    // playhead, whose own damage repaints it. A DROPPED SELECTION is that
    // same stem change over more flags, so it damages too.
    if (was != app.history_mode.focus || had_selection) viewport.invalidate_all();
}

// THE MODE'S MODIFIED CLICK, the CTRL toggle — `hit` is an ordinal into
// app.history_mode.flags that the PRESS resolved from the flag's LANE BOX, the
// only surface this click has (architect 2026-08-06, the symmetry ruling:
// selection is lane vocabulary in both views). IT RUNS AT THE PRESS
// (2026-08-17 — the mode has no drag for the press to become, so nothing
// needs the lift). TWO CALLERS SINCE 2026-09-17: the router's ctrl flag
// claim, and its PLAIN flag claim while ADD TO SELECTION stands — the sticky
// ctrl's whole meaning, and a finger's road onto this multi-selection
// (AppState::add_to_selection). It is the LIVE selection model re-expressed
// over the mode's own list (run_marker_click_act, AppState::add_to_selection):
// the clicked flag's membership alone toggles, everything else untouched — the
// one gesture that can leave an arbitrary subset standing, which is why the
// set is a set. (No pointer range-extends here, architect 2026-10-30: no
// pointer carries a shift bit.)
//
// IT THEN FOCUSES THE CLICKED FLAG AND LANDS THE PLAYHEAD ON IT, the live
// model's "a modified press lands on the focus it sets" applied here, and it
// leaves the STORE selection exactly as untouched as the plain click does —
// the mode owns no live marker.
//
// A CTRL PRESS THAT REMOVES THE LAST MEMBER therefore rests on the focus alone,
// with that flag still lit and still the act's subject. That is the recorded
// consequence rather than an oversight, and it is the SAME state a plain click
// leaves ("focus = a selection of one"): the live column repairs its focus onto
// another member when a toggle removes the focused one, and this mode has no
// such repair to run — its focus is where the playhead just landed, which the
// removal did not take back.
//
// IT LANDS THROUGH THE MOVEMENT OWNER, past the range guard below.
//
// DAMAGE IS THE FOCUS CLICK'S: full-window, unconditional here, because the
// toggle always flips the clicked flag's face.
void GuiInputHandler::select_history_diff_flags_modified(int hit,
                                                         HistoryDiffHalf half) {
    const int n = static_cast<int>(app.history_mode.flags.size());
    if (hit < 0 || hit >= n) return;
    std::set<int>& sel = app.history_mode.selection;
    {
        const auto it = sel.find(hit);
        if (it != sel.end()) sel.erase(it);
        else                 sel.insert(hit);
    }
    app.history_mode.focus      = hit;
    app.history_mode.focus_half = half;
    // THE STEMS IN THIS EVENT (2026-11-05, paint_marker_stems' ONE-STATE
    // RULE): the mode's focus and selection are the diff lane's selected
    // look, its stems among it, so the flag cache is rebuilt here, before the
    // land below can paint the playhead beside the old stems
    // (Viewport::refresh_flag_cache, fingerprint-guarded).
    viewport.refresh_flag_cache();
    land_playhead_on_domain_frame(
        app, audio, viewport,
        history_diff_flag_landing_domain_frame(
            app, audio, app.history_mode.flags[static_cast<std::size_t>(hit)],
            half));
    viewport.invalidate_all();
}

// (THE DROPDOWN'S TWO WRITERS AND ITS POINTER-LEAVE DROP — close_dropdown,
// toggle_dropdown and clear_dropdown_pointer_state — RETIRED 2026-10-21 with
// the Settings popup.)

// THE HOVER TOOLTIP'S BOX GOES DOWN — the one hide body, shared by the hard
// end and the pointer leaving the box's column (note_tooltip_hover). It
// damages the strip AND the box's last painted
// rect, and that is why every edge that takes a hint away must come through
// here rather than leave it to a hover walk: the box hangs OUTSIDE its strip
// (above its owner's face — over the logo lane for a band key, over
// whatever stands above row 8 or a controls row for theirs), so a repaint
// that found no
// box to draw would publish a zero rect and return, and the overhang would
// have nothing left to erase it. Surface-agnostic: the painted rect is
// wherever that hint was drawn.
static void take_tooltip_box_down(AppState& app, Viewport& viewport) {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    if (!t.visible) return;
    const GuiRect painted = t.rect;
    t.visible = false;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

// THE HARD END (the model and the callers' inventory are at
// AppState::RedesignTooltip and the declaration): the box down at once, the
// wait stopped, THE CHAIN BROKEN and THE COLUMN FORGOTTEN — `hovered` goes to
// no owner, so the next walk that finds the pointer on a column (the pen's
// restore motion after its lift, the re-entry after a leave, a resting
// pointer's next tick after an activation flip) is an ARRIVAL and waits the
// delay. A finger's lift leaves the window (the pointer-leave hook, every
// reason alike), so no walk runs after it and nothing is waited.
void GuiInputHandler::hide_shift_tooltip() {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    t.hovered        = AppState::RedesignTooltip::Owner{};
    t.hovered_column = GuiRect{0, 0, 0, 0};
    t.wake_due_ms    = 0;
    t.left_ms        = 0;
    take_tooltip_box_down(app, viewport);
}

// THE SHOW — the one show body, shared by the wait's ripening (tick_tooltip)
// and the bypass's immediate arrival (note_tooltip_hover): the box takes the
// hovered owner, SEATED OFF THAT OWNER'S FACE ALONE — above it, its left edge
// on the face's (architect 2026-11-07, sessions 90 and 91; the placement at
// tooltip_box_rect) — and the chain is spent into it (`left_ms` 0: the box's
// own leave re-arms it). The pen's one say in the seat is taken here and held
// while the box stands: whether the pointer is on row 8's clock field, Play's
// second rect (`shown_on_field`, the face the box stands over), so motion
// between the
// clock and Play's key under a standing box moves nothing. The box's own
// rect cannot be known yet (the paint that publishes it is the frame this
// schedules), so it damages the band every seat of the box lies in — the
// full-width band of tooltip_damage_h_px() rows ending on the gap over the
// owner's face (tooltip_damage_band), which holds the box whole. No box
// stands at a show: an arrival took the old one down first.
static void show_tooltip_box(AppState& app, Viewport& viewport) {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    t.wake_due_ms = 0;
    t.left_ms     = 0;
    t.owner       = t.hovered;
    t.visible     = true;
    t.shown_on_field =
        t.owner.surface == AppState::RedesignTooltip::Surface::Roster &&
        t.owner.index >= 0 && t.owner.index < kRedesignButtonCount &&
        rect_contains(
            app.redesign_buttons[static_cast<size_t>(t.owner.index)].field,
            app.last_mouse_x, app.last_mouse_y);
    viewport.invalidate_rect(tooltip_damage_band(app));
}

// THE CHAIN'S LAPSE (the bypass's rule at AppState::RedesignTooltip): an
// armed chain is broken once BOTH hold — more than the delay since the leave
// AND the pointer more than tooltip_chain_reach_px() (one W, Chebyshev)
// outside the column it left. Asked by the wait's writer before its arrival
// test and by the clock on every tick, so a pen resting off the band breaks
// it on time with no motion.
static void break_lapsed_tooltip_chain(AppState& app, int64_t now) {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    if (t.left_ms == 0) return;
    if (now - t.left_ms <= app.device_config->tooltip_delay_ms) return;
    const GuiRect& c = t.left_column;
    const int x = app.last_mouse_x;
    const int y = app.last_mouse_y;
    const int dx = x < c.x ? c.x - x : x >= c.x + c.w ? x - (c.x + c.w - 1) : 0;
    const int dy = y < c.y ? c.y - y : y >= c.y + c.h ? y - (c.y + c.h - 1) : 0;
    if (std::max(dx, dy) > tooltip_chain_reach_px()) t.left_ms = 0;
}

// THE WAIT'S ONE WRITER, shared by both hover walks (the roster's and the
// modal dialog's) so the model is one rule rather than two copies; the model
// is stated at AppState::RedesignTooltip. `o` is the owner whose column holds
// the pointer (no owner off every column) and `column` the rect the walk found
// it in (tooltip_column_rect, or Play's clock field). In order:
//   * A HELD PRESS: no column is held and no wait runs (a box cannot stand —
//     the press took it down — but one would go down here), the chain broken;
//   * THE CHAIN'S LAPSE (break_lapsed_tooltip_chain), before any arrival
//     reads it;
//   * LEAVING THE BOX'S COLUMN: while a box stands, the pointer anywhere but
//     on its own owner's column takes it down at once AND ARMS THE CHAIN on
//     the column it stood on (`left_column`, `left_ms` now);
//   * NO COLUMN: the wait stops;
//   * THE SAME COLUMN: nothing — motion inside it restarts nothing; the rect
//     is refreshed (a relayout moves it under the pointer);
//   * AN ARRIVAL (another owner, compared whole, across surfaces as within
//     one): the box shows AT ONCE while the chain holds (show_tooltip_box),
//     else the wait starts, the delay from now.
// The delay is the live `tooltip_delay_ms` (device_config.h), read at every
// arrival and lapse test, so a Settings commit takes the next one.
//
// THE TOOLTIP LAMP IS ASKED FIRST (architect 2026-09-29, Enable Tooltips on
// bare backslash): while AppState::show_tooltips is dark this writer holds the
// wait's state clear — no hovered owner, no deadline, no chain — and returns,
// so no wait ever starts and tick_tooltip has nothing to ripen on either
// surface. The box itself went down at the dark edge (set_show_tooltips), so
// nothing here needs to hide one. The lamp lit under a pointer resting on a
// column, the next walk is an arrival like any other.
void GuiInputHandler::note_tooltip_hover(AppState::RedesignTooltip::Owner o,
                                         const GuiRect& column) {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    const auto forget_column = [&] {
        t.hovered        = AppState::RedesignTooltip::Owner{};
        t.hovered_column = GuiRect{0, 0, 0, 0};
        t.wake_due_ms    = 0;
    };
    if (!app.show_tooltips) {
        forget_column();
        t.left_ms = 0;
        return;
    }
    if (t.button_held) {
        forget_column();
        t.left_ms = 0;
        take_tooltip_box_down(app, viewport);
        return;
    }
    const int64_t now = monotonic_ms();
    break_lapsed_tooltip_chain(app, now);
    if (t.visible && !(o.index >= 0 && o == t.owner)) {
        t.left_column = t.hovered_column;
        t.left_ms     = now;
        take_tooltip_box_down(app, viewport);
    }
    if (o.index < 0) {
        forget_column();
        return;
    }
    if (o == t.hovered) {
        t.hovered_column = column;
        return;
    }
    t.hovered        = o;
    t.hovered_column = column;
    if (t.left_ms != 0) {
        show_tooltip_box(app, viewport);
        return;
    }
    t.wake_due_ms = now + app.device_config->tooltip_delay_ms;
}

// THE TOOLTIP'S CLOCK (the declaration carries the contract; the model is at
// AppState::RedesignTooltip). Two jobs on a tick that already runs: the
// chain's lapse (break_lapsed_tooltip_chain — the pointer resting off the
// column it left breaks it once the delay has passed, with no motion to
// run a walk), then the wait's ripening, the show (show_tooltip_box, whose
// body carries the damage). No box stands at a ripening: an arrival took the
// old one down. The box has no life: it goes down only when the pointer
// leaves its column or at a hard end.
void GuiInputHandler::tick_tooltip() {
    AppState::RedesignTooltip& t = app.redesign_tooltip;
    const int64_t now = monotonic_ms();
    break_lapsed_tooltip_chain(app, now);
    if (t.wake_due_ms != 0 && now >= t.wake_due_ms) {
        t.wake_due_ms = 0;
        if (t.hovered.index >= 0) show_tooltip_box(app, viewport);
    }
}

// THE NO-WAIT RULE — the rule's one expression, read by the roster's hover
// walk and on_key's modal-opening tooltip end, each for the owner it holds:
// `roster_key` the roster index of a key, or negative for an owner that is
// no key (a marker lane cue). TWO CLASSES OF SURFACE, re-derived 2026-11-11:
//   * THE MODALS VEIL EVERYTHING — the prompt, every editor
//     (keyboard_modal_editor_active, the one predicate for a keyboard-taking
//     modal, the lane editors among them) and the progress modal: no key's
//     and no cue's wait runs under one, an overlay standing behind it or
//     not.
//   * THE THREE OVERLAYS — the render player, the picker's contents and the
//     score viewer — REFUSE EVERY OWNER BUT THE KEYS THEY LEAVE LIVE
//     (architect 2026-11-11: under an overlay the live band keys show their
//     tooltips, "yes" — narrowing 2026-10-25's "overlays don't show hints"
//     to the keys that are dead there): the overlay's opener and the tooltip
//     lamp, overlay_admits_button's membership (app_state.h) and no second
//     list — the face's, the lit bit's and the lift's own, so a key explains
//     itself exactly where it answers. A cue is refused: the lane is covered.
// The `h` view keeps its hints: a view, not an overlay. The overlay term is
// the lift's own spelling (finish_chrome_press_release's overlay_refuses).
bool GuiInputHandler::tooltip_dwell_suppressed(int roster_key) const {
    if (app.prompt.active || keyboard_modal_editor_active() ||
        app.batch_progress.active)
        return true;
    if (!(app.render_player.active || app.picker.active ||
          app.score_viewer.active))
        return false;
    return roster_key < 0 || roster_key >= kRedesignButtonCount ||
           !overlay_admits_button(app,
                                  static_cast<RedesignButton>(roster_key));
}

// THE ARMED CHROME PRESS, dropped — the pointer-leave hook's clear (main.cpp),
// the arm's hard end. Since the act moved to the release (2026-08-13) this is
// sharper than a face clear: the arm is a pending ACT, and a pointer that has
// left the window is on no button, so the act must not be left waiting for a
// release that may never come. A release that does arrive then finds no arm
// and dispatches nothing, which is the intended answer (the pointer was
// outside the window; the modal arm's clear beside this one carries the
// same). The RELEASE itself never comes here — it consumes
// the arm through take_chrome_press at on_button_release's top. The press
// face's erase is take_chrome_press's own (2026-11-05).
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
// clears are unconditional in the leave hook's sense (the dropdown's pointer
// drop, which also took a HOVER face, was a third until the popup retired,
// 2026-10-21).
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
    // the key-up, which does not depend on where the finger was.
    if (app.onscreen_keyboard.pressed_key >= 0)
        finish_onscreen_keyboard_release();
    // THE RENDER PLAYER'S TWO ARMS ARE THE FIFTH AND SIXTH MEMBERS
    // (2026-08-28): the folder overlay's row press and the scrub's marker
    // drag both act at the lift, so a lift that never comes must drop them
    // with nothing committed — the chrome arm's own rule, on the same edge.
    // The score viewer's scrub drops with them, both owing nothing: a live
    // item or soundtrack the press paused stays PAUSED
    // (clear_player_scrub_drag, clear_score_scrub_drag).
    clear_folder_overlay_press();
    clear_player_scrub_drag();
    clear_score_scrub_drag();
    if (app.chrome_press.kind == AppState::ChromePress::Kind::None &&
        app.modal_dialog_pressed < 0)
        return;
    clear_redesign_button_press();
    clear_modal_dialog_press();
}

// THE REGION DRAG'S ONE MOTION PATH, hoisted 2026-08-12 (the touch half) for
// its TWO DRIVERS: on_motion's region branch below (pointer motion under the
// held button, past its own button-lost arm) and update_touch_region (the
// platform's per-frame region hook, which holds no button and has none to
// lose). Both callers guard on region_drag.active.
void GuiInputHandler::apply_region_drag_motion(int mouse_x, int mouse_y) {
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    // Sub-threshold: the press has not yet become a sweep. Below the shared
    // Chebyshev gate NOTHING HAS HAPPENED AT ALL beyond the begin's deselect —
    // no trim is written, and the release owes no commit tail (the armed
    // sweep begins past the slop, so its first frame crosses at once). Once
    // a drag, always a drag
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
    // like the other drags' live tracking: the column's whole SOURCE frame
    // through the sweep's one column->trim route (sweep_trim_frame_at_column,
    // on the painted item basis, architect 2026-09-24), so the writer needs no
    // domain hop.
    int rel = mouse_x - area.x;
    if (rel < 0) rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    // THE TRIM WRITE — the sweep IS a trim write (architect 2026-08-18): from
    // the drag's fixed ANCHOR to the pointer's column, ordered and clamped to
    // the song walls, through the one owner that carries all of that
    // (write_trim_from_sweep, input_trim.cpp). NO WIDTH RULE stands between the
    // two ends since 2026-08-19 (the retired floor's record is at that owner),
    // so a stroke authors whatever span it draws — including none at all, which
    // the release turns into the whole song. It owns its own same-pair
    // short-circuit, so a sub-pixel jitter event inside one column writes and
    // repaints nothing; `wrote_trim` latches for the release's commit gate.
    // Its own repaint is the waveform area's, the trim bar's lane inside it.
    //
    // (THE `h` VIEW WAS CARVED OUT HERE while its hold ran a playhead-only
    // sweep; that former retired with the hold, architect 2026-11-07,
    // session 87 — "history cannot edit trim" — and the armed sweep never
    // begins in the view, touch_sweep_armed.)
    //
    // THE ADD SEL FORMER TAKES THE SAME COLUMN AND WRITES THE SELECTION
    // INSTEAD (architect 2026-11-10, session 101; the rule at
    // apply_selection_sweep_to_column) — no trim write, so nothing below runs.
    if (app.region_drag.select) {
        apply_selection_sweep_to_column(rel);
        return;
    }
    if (write_trim_from_sweep(app.region_drag.anchor_source_frame,
                              sweep_trim_frame_at_column(rel))) {
        app.region_drag.wrote_trim = true;
    }
    // SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): sweeping a TRIM
    // span does NOT select the markers it contains (the reverse coupling — a
    // region selecting its contents — was tried and retired; do not
    // re-propose) — the begin already deselected all and the trim write
    // deselects again on the setter rule, so the selection is EMPTY
    // throughout. (Selecting a span's markers is ADD SEL's own sweep since
    // 2026-11-10, the branch above, which writes no trim.)
    //
    // THE PLAYHEAD STAYS WHERE IT IS (architect 2026-11-07, session 88:
    // "Setting the trim should not move the playhead — I usually use scrub
    // now, so moving playhead is just distracting"; the rule at the head of
    // input_trim.cpp), reversing 2026-07-30's carry on the moving end. So
    // nothing here writes the cursor or damages the clock. PLAYBACK IS
    // UNTOUCHED per motion by THIS body: the trim write's own first-accepted-
    // change STOP is the whole playback story of this gesture.
    // (THE SLIVER PARAGRAPH IS RETIRED with the release-time min-size check
    // (2026-08-18) and stayed retired when the minimum width floor went the
    // same way (2026-08-19): a jitter drag dissolves nothing and is widened to
    // nothing — it commits the sliver it drew, and Reset Trim is the way back.)
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a PendingMarkerPress crossing the threshold —
// the flag's click acted at its PRESS since 2026-08-17, so the crossing only
// begins the drag); with no gesture it
// recomputes hover at the pointer (the pen's hover, the one hover). The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the ARMING PRESS's click act landed the playhead on the marker, so the drag
// tows it by construction).
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    // Record the latest pointer coords so viewport mutators can re-evaluate
    // hover at the pointer's last position.
    app.last_mouse_x = mouse_x;
    app.last_mouse_y = mouse_y;
    app.pointer_in_window = true;
    // THE PEN'S HOT-FACE LATCH ANCHORS AT THE FIRST HOVER REPORT AND CLEARS
    // ON MOTION OFF THAT ANCHOR (pen_hot_latch_, the rule at its
    // declaration), here above every branch so the hover walks this motion
    // runs already answer at the pen — the hot button or the lit row under
    // it lights, damaged as usual. The latch arms after the lift's
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
    // and it sits at the very TOP because every branch below returns: a modal
    // branch returns, each live gesture returns. The arms this drops belong to none of those branches — they are
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
    // a card. The press
    // claim and the touch pan zone ask the same one owner
    // (notification_card_at) for the same reason.
    update_notification_hover(mouse_x, mouse_y);
    // A LEVEL CASE'S DRAG (architect 2026-11-05, the sliders; AppState::
    // ChromePress's Level kind): a standing Level arm owns the motion whole
    // — the slop crossing and the relative travel, the level applied live
    // wherever the pointer is (update_level_press_motion) — until the lift
    // or the button-lost clear above. It stands above the modal branches
    // because none of them can arm one (a case under a modal or an overlay
    // is dead and arms nothing), and a modal RAISED MID-DRAG leaves the drag
    // its pointer: the lift's act is what the veil swallows.
    if (app.chrome_press.kind == AppState::ChromePress::Kind::Level) {
        update_level_press_motion(mouse_x, mouse_y);
        return;
    }
    // THE ROSTER WALK STAYS LIVE UNDER EVERY MODAL SURFACE — the MODAL
    // branches that return before the no-gesture tail (the prompt's, the
    // player's, the picker's and the dialog editors' below) all call it —
    // because what it answers is not frozen by a modal: the tail's hard end
    // of a DIALOG tooltip owner whose surface has gone. What it must NOT do under a modal is start a
    // roster TOOLTIP dwell — but for the two keys an overlay leaves live
    // (2026-11-11) — and that refusal lives in the walk itself (the
    // veil and the no-wait rule). The dialog's own buttons take their walk
    // through update_modal_dialog_hover in the same branches. What DOES
    // freeze the walk is an active pointer GESTURE — the branches below all
    // return without this call.

    // (THE OPEN DROPDOWN TOOK THE MOTION here, above every gate, until the
    // Settings popup retired, 2026-10-21.)
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
    // claim's own rank: a standing row arm follows the pointer (the band's
    // scroll drag once the gate is crossed) and owns the motion whole, and with no arm standing the
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
    // run (a roster hint under the player for its two live keys alone, the
    // opener and the tooltip lamp — the no-wait rule, 2026-11-11) — nothing
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
    // THE SCORE VIEWER'S MOTION (2026-10-26): a standing scrub carries its
    // thumb (the page under it shown live on both faces, 2026-10-27), and
    // otherwise the player's two walks.
    if (app.score_viewer.active) {
        if (app.score_viewer.scrub.armed) {
            if (!mods.primary_button_held) {
                clear_score_scrub_drag();
                return;
            }
            update_score_scrub_motion(mouse_x);
            return;
        }
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // THE PICKER'S MOTION (2026-08-28): the modal buttons' walk and the
    // roster walk (a roster hint for the content's opener and the tooltip
    // lamp alone, the no-wait rule), nothing else — no
    // field, no drag, no scrub; the overlay's own arm ran above.
    if (app.picker.active) {
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // THE PROGRESS MODAL'S MOTION (2026-10-14): the picker's — the modal
    // buttons' walk and the roster walk, nothing else.
    if (app.batch_progress.active) {
        update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // F2.1: editor-text drag motion. Handled before the dialog-editor branch
    // (which returns) so the gesture reaches the dialog editor's field,
    // and before the trim / playhead branches. A lost button finalizes like
    // release, mirroring those handlers.
    if (app.editor_text_drag.active) {
        if (!mods.primary_button_held) {
            // THE BUTTON-LOST ARM ENDS THE GESTURE exactly like the
            // clean-release arm it mirrors.
            finalize_editor_text_drag();
            return;
        }
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            // THE CARET FOLLOWS, NOTHING SELECTS (architect 2026-11-08,
            // session 95: a drag in a field moves the caret): the one seat,
            // which collapses the selection, on every motion. No gate (a
            // tap's sub-slop motions are this road's only producer; a drag
            // past the slop is the touch translation's caret drag and never
            // arrives here).
            set_editor_caret_from_x(g, mouse_x, mouse_y);
            if (g.dialog) viewport.invalidate_dialog_editor_box();
            else          viewport.invalidate_top_strip();
        }
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        return;
    }
    if (keyboard_modal_editor_active()) {
        // THE EDITORS' MOTION — every editor in one branch since 2026-10-25
        // (the lane editors fell through to the surface under them until
        // then, its hover cues and gestures, their presses not veiled): the
        // dialog buttons' walk WHERE A DIALOG STANDS — a lane editor
        // publishes no dialog, so it has no buttons to walk — then the roster
        // walk, whose veil term refuses the whole roster, so no roster hint
        // starts under the pointer.
        if (modal_dialog_editor_active())
            update_modal_dialog_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // THE NAVIGATION SURFACE'S AND THE PLACEMENT LANES' PENDING CLICK
    // (contract at ScrollDragState, app_state.h): the press did nothing, and
    // the motionless release runs the deferred click act. PAST THE SLOP THE
    // PRESS IS NO CLICK AND BEGINS NOTHING (architect 2026-10-30: on glass a
    // waveform contact past the slop is the touch
    // layer's pan before the pointer road sees it, and the tap's drift is
    // delivered only inside the slop, resolve_touch_window_to_pointer), but
    // for the HEAD DRAG, whose every motion is a step. A lost button: an
    // UNMOVED press is NOT a clean click, so the deferred act does not run and
    // no seed is left — the standing abnormal-end rule; a moved one owes
    // nothing but the disarm.
    if (app.scroll_drag.active) {
        ScrollDragState& sd = app.scroll_drag;
        if (!mods.primary_button_held) {     // button lost
            app.scroll_drag = ScrollDragState{};
            return;
        }
        // THE HEAD DRAG'S ONE STEP (architect 2026-10-09 ~17:40; contract at
        // ScrollDragState::head_drag): THE CLICK ACT ITSELF at the pointer's
        // column — run_nav_click_act, the motionless release's own body, the
        // `h` view's mode land with it — the column clamped onto the
        // waveform's, so a pointer past either end of the canvas holds the
        // playhead at that end's column rather than meeting the act's gutter.
        // No second placement road: the play's stop, the deselect, the
        // movement owner and its damage are the act's.
        const auto head_drag_step = [&] {
            const GuiRect area = waveform_area(app);
            const int col =
                std::clamp(mouse_x - area.x, 0, std::max(0, area.w - 1));
            run_nav_click_act(area.x + col, sd.history,
                              /*scrub_release=*/false);
        };
        // Sub-threshold: still the pending click. The press did nothing, so
        // nothing happens here either — the fork IS the threshold.
        if (!sd.moved) {
            if (std::max(std::abs(mouse_x - sd.press_x),
                         std::abs(mouse_y - sd.press_y)) <
                    drag_moved_threshold_px()) {
                return;
            }
            // THE CROSSING ENDS THE CLICK AND BEGINS NOTHING: `moved` is what
            // tells the release the press was not a click — the held button
            // stays this pending's until the release.
            sd.moved = true;
        }
        // Past the crossing the pending does nothing, ever — but the head
        // drag, whose crossing is its first step and every motion after it
        // one more (2026-10-09).
        if (sd.head_drag) head_drag_step();
        return;
    }
    // (No scrub motion branch: the scrub is a ONE-SHOT act at the motionless
    // RELEASE (2026-08-13) — the held-drag per-column re-scrub is REMOVED
    // (architect 2026-07-23, the Ableton model), and the per-column
    // stop-fence cadence is structurally gone.)
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
    // bar's ctrl bound set. Nothing has been committed yet — the
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
        // verdict, so a refused set (a degenerate audio/geometry state, a
        // click on its own cap, the partner wall) arms nothing and the
        // crossing is a
        // consumed nothing, exactly as the motionless lift would have been.
        // The bound is written at the press column, the drag
        // anchors there, and begin_trim_drag's orig_* capture is the click-set
        // value.
        // WHICH BOUND IS THE NEAREST RULE'S, AT THE DOWN'S COLUMN (architect
        // 2026-11-10): the set picks the end cap nearest the press column
        // and the drag takes the bound the set moved, however long the press
        // was held before it crossed.
        // FALL THROUGH (no return) so the pending trim drag armed just now
        // crosses on this same event and applies its first delta, folding the
        // whole press->crossing travel.
        set_trim_bound_at_click_then_arm_drag(press.press_x, press.press_y);
    }
    // Pending trim drag (armed by a plain trim-bar press, or by the crossing of
    // a barrel bound set just above): the trim reposition
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
        // Esc-restore origin, and pointer gestures have no cancel (the rule at
        // on_key's drag-modal statement), so the cross-struct data path they
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
        // end the gesture, committing the trim (or, on the ADD SEL former,
        // the selection) it has written (as a clean release would). Modifier changes mid-drag are ignored.
        // THIS ARM ALSO ENDS A LIVE TOUCH REGION GESTURE that a pointer
        // motion interrupts (the hook gesture holds no logical button, so
        // primary_button_held reads false here), every end a commit.
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
        // all already stand — the select is what stands the dragged marker's
        // stem, and the stop is why no live playback needs handling
        // below (nothing can restart playback under the held button: no key
        // arrives while this pending stands, on_key's drag-modal statement).
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
        // in both audio views, S+P and T+P since 2026-10-12) — is the
        // view refusal's below, behind the lamp's fork.
        // THE VALUE DRAG FORKS AHEAD OF THE LOCK (architect 2026-09-10): while
        // the VALUE DRAG LAMP stands lit (value_drag_posture, app_state.h,
        // where the whole rule is stated — the lamp's since 2026-10-11, the
        // view's from 2026-09-13) the flag's plain drag is the VERTICAL one
        // in every view, so the crossing begins that gesture or NONE — "we
        // never allow multi-axis dragging; flags move up and down or not at
        // all". The return is unconditional for that reason: a flag the value
        // drag cannot act on does not fall back to the horizontal move under
        // the lamp, it simply does not drag, and the lamp stays lit (only a
        // drag that BEGAN spends it, at its commit). The
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
        // THE DARK LAMP WHERE THE VIEW REFUSES THE HORIZONTAL MOVE (architect
        // 2026-10-11): T+W, the home-view binding's one off-home flag state,
        // and T+P under grid iterations, whose lock refuses the move — the
        // drag is NOTHING there, silent as this whole surface is, the press's
        // click acts standing (horizontal_flag_drag_refused, app_state.h).
        // Until that day the view made the drag the value drag there, which
        // is how a sideways drag changed a tempo unnoticed.
        if (horizontal_flag_drag_refused(app)) return;
        // No home-view test: T+W returned above, so S+W, S+P (its drag
        // taking no lead-in offset and riding no overlay, architect
        // 2026-10-12) and T+P with grid iterations dark reach here, all of
        // them authoring views.
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
        // stop, and nothing can have restarted playback since (on_key's
        // drag-modal statement: no key arrives mid-gesture).
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
        // menu up a hover opens nothing. (The cue's hover tooltip, 2026-11-09,
        // is that same walk's: it asks the marker lane's cues where no key's
        // column holds the pointer.)
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
    // DISPLAYED target domain so the painted flag tracks the pointer 1:1 —
    // exactly one column per device px at a whole q in source view away from
    // the walls, within a one-device-px quantization at a fractional q and in
    // target view (the earlier frame rounding; the qualification at
    // apply_drag_motion's head, marker_drag.cpp).
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
