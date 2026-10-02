#pragma once

// THE NOTIFICATION CARDS (architect design 2026-08-29; THIS HEADER IS THE
// RULING WHOLE — the split, the two classes, the inventory of what is and is
// not notified, and the words on a card). The product's surface for EVENTS:
// something happened that answers an act, or that the user was not watching.
// A small dark card stacked top-right under row 1's view radios, newest on
// top, EVERY CARD IN THE STACK VISIBLE, UP TO kNotificationMaxLines lines of
// the one sans, a Breeze glyph at the left naming the class
// (dialog-information / dialog-error), and ONE PAD around both
// (notification_pad_px below — the card's chrome reads one number on all
// five of its distances). THE WHOLE CARD IS ONE BUTTON (architect
// 2026-10-01, "whole card dismisses, X gone"): the X that stood at its right
// from 2026-08-29 is retired, and a click or tap anywhere on the card
// dismisses it at the lift (the hit section below). A sentence too long for
// one line WRAPS DOWNWARD UNDER THE GLYPH (architect 2026-08-30): the card
// grows taller, the glyph stays at the first line's height, and nothing
// reflows beside it. Two classes and nothing else — no remaining-time bar, no
// actions, no title/body split, no sound (the architect is "not a big fan of
// notifications": minimal, and the inventory below is closed).
//
// THE SPLIT, STATE AND EVENTS (2026-08-29), which decides the surface: STATE
// is what is true right now, replaced as it changes, never timed out and
// never cleared by a key press — the render's progress line and the `h`
// walk's line, which live in ROW 8'S STATE CELL right of the clock
// (paint_bottom_row_buttons_and_clock, paint_handler.cpp, owns that cell); an
// EVENT is an act answered with a sentence, or a background act that
// finished badly — a CARD. The state surface has NO TIMEOUTS because nothing
// in it is a claim about a past moment, so nothing there can go stale.
//
// THE STRICTNESS RULING (architect 2026-08-30: "go very verbose — a card for
// every refusal / no-op that is silent today") and THE DELIBERATE-PRESS RULE
// beside it: every consumed no-op cards its sentence unless a ruled silence
// below covers it, and a deliberate press whose result nothing paints says
// its failure. ONE CARD PER PRESS: where a refusal is asked twice on one road
// the OUTERMOST site that has the reason raises it and the inner one stays a
// silent belt. The ops do not know about cards: the authoring cluster
// composes its sentence and RETURNS it (GuiOpRefusal, warpmarkers_ops.h) for
// the dispatch arm to raise. A card's sentence has ONE composer and, where
// the site printed one, TWO READERS — its stderr line (which keeps any
// offending text, and alone carries full paths) and the card (which never
// carries the `warptempo_gui: ` prefix).
//
// NORMAL — leaves on its own kNotificationMs after its push (gui_input.h; the
// pointer resting on it, or a press holding it, pauses the clock), at the
// lift of a press on it, at a BUMP, or at the whole stack's dismissal (a bare
// Esc that reaches the stack, or a Shift-click or long press on any card).
// THE INVENTORY, by family and owner (each
// family's sentences are literals at their raisers; re-grep `notify(` before
// restating a count):
//   * THE GATES' OWN CARDS, the swallowed press answered by the state that
//     swallowed it, each asked only for a chord the product BINDS IN THE
//     STANDING MODE (chord_is_bound, gui_input.h): "Close the editor first:
//     <chord> is ignored while it is open" (the keyboard-modal editor gate —
//     a typed character is the editor's own and says nothing),
//     kKeysDuringDrag (input_handler.cpp: the editor text drag, the pointer
//     gestures' drag-modal gate, the render player's and the picker's own
//     arms — those two, their routers being their vocabulary, do not ask
//     chord_is_bound), "No audio is loaded yet", "<chord> is not available
//     in the history view" (on_key's call site, never the predicate's) and
//     read_only_chord_card below; the lock's own composer
//     authoring_lock_card; kIterationLockUndoCard / kIterationLockRedoCard
//     (app_state.h). THREE of them name the chord through the one speller
//     spell_chord (gui_input.h). A GATE'S MEMBERSHIP IS THE CHORD'S ALONE
//     (architect 2026-09-01: "look for instances where it lies by saying it's
//     not allowed when, in fact, it's only contextually not allowed") — a
//     chord the state owns is admitted and its ACT answers the contextual
//     refusal with the true reason; the `h` allowlist's two state-conditional
//     admissions fork their own truthful sentences ("There is nothing to
//     load" for bare `'`, "Select a change to revert" / kTabReadOnlyCard for
//     bare `v`). A card naming the chord pressed says what happened, never
//     what to press instead — it is not a gesture hint.
//   * THE VERBS' OWN REFUSALS, naming a subject or a view, never a chord: the
//     home-view binding ("Markers are placed in source view", "Markers are
//     moved in source view", Shift+S's "Already in phase reset view"); the
//     subject refusals ("Select a marker to …", "Select a warp marker to …");
//     the column ("Phase resets carry no tempo to inherit"); the value facts
//     (the GROUP step's "One of the selected markers cannot take this tempo
//     change", the singleton's "That marker shares its frame with another" and
//     "A label reference has no tempo of its own"); the clipboard chords'
//     (kSelectOneRun and its siblings, input_key_dispatch.cpp); the value
//     pair's three (payload_eligibility — Ctrl+C and Ctrl+J); undo and redo's
//     ("There is nothing to undo" / "…redo", "That step belongs to the other
//     tab, which is read-only", the Restrict Undo lamp's "That undo would
//     switch the view" / "That redo …", ranked last); the `h` view's
//     kHistoryViewMovesNoMarkersCard; the off-edge pair below; bare `m`'s
//     NINE-arm BPM gate, one card per press carrying the first failure (the
//     tenth arm, enter_bpm_mode's own bail, is unreachable and silent).
//   * THE MODES: the picker's "Choose a project first" and its opener's
//     "Close the editor first"; kCheckpointPublishing (below — bare `h`, the
//     `h` view's Ctrl+S, the save owner, the picker, File → Revert); "History
//     is unavailable[: <reason>]" (the press that asks for git on a visit
//     that could not bootstrap the remote walk — bare `g` at set_history_delta
//     and Ctrl+S at open_history_commit_editor — plus the commit-title
//     editor's Enter into a closed mode and the failed-scan arrival; the
//     entry opens on the local walk and raises none); the trim bar's "A trim
//     bound must stay inside its partner" (the act's, never
//     trim_bound_click_frame's); playback's kPlaybackDeviceUnavailableCard
//     (playback_lifecycle.h — the one launch body, the two pre-launch gates
//     and the A/B audition's own tick) and the audition's "One of the two
//     tabs has nothing to play from here"; kNoRenderRunningCard (the painted
//     Cancel with no render running); the render player's (GuiRenderPlayer::
//     status and its load act's `refuse`, render_player.cpp /
//     input_key_dispatch.cpp: "Nothing to play: no renders under tmp/",
//     "Render player is unavailable while rendering", "A folder carries no
//     recipe to load in place", "Cannot load in place while a render is
//     running", the decode's rate / channel, empty and no-device sentences
//     and the frozen reader's own words, "There is no folder to delete", and
//     every `Load in place refused: …`); the `h` view's `'` and revert
//     refusals (`Load in place refused: …`, `Revert refused: …`, the
//     duplicate-label grammar's `Revert refused: label '<l>' is already
//     defined at another marker`); the Open project picker's three refusals
//     and File → Revert's three, the same words for the same reasons.
//   * THE RENDER ROAD: "Target render failed" (a cancelled preview says
//     nothing); the archival road's "Render failed: <reason>" (GuiFailure's
//     display clause through lowercase_initial) and a sweep's "Rendered N of
//     M" when it produced fewer cells than it counted and was not cancelled
//     (a failed CELL raises nothing of its own — one sweep, one answer);
//     kTrimFallbackCard below; the render chords' refusals ("Turn off grid
//     iterations to render one file", "A marker's iteration bracket runs
//     backwards", "No iteration ranges are authored", the sweep's two verdict
//     cards at iteration_sweep_plan, app_state.h) and
//     render_folder_creation_failure (renders_dir.h). THE TWO SWEEPS ARE NOT
//     CARDED FOR THE TRIM FALLBACK, a recorded asymmetry: a cell rewrites the
//     warp markers, so the live verdict is an assertion about maps it cannot
//     see, and a per-cell verdict would be up to 391 cards for one act.
//   * THE WRITES WHOSE RESULT NOTHING PAINTS: the save's failures at the one
//     save owner (GuiSaveOps::save — its three write arms, the numeric-locale
//     refusal and kCheckpointPublishing, every Ctrl+S road inheriting them);
//     the device config's write failure (write_device_config, device_config.h)
//     and kProjectsPathAppliesCard below on a successful persist.
//   * EVERY RED FLASH'S REASON (2026-08-30): the flag-editor cluster's
//     "Edit rejected: …", "Range bound rejected: …" and the BPM editor's
//     three "BPM edit rejected: …" (flag_editor.cpp); the settings editor's
//     five "Settings edit rejected: …" (settings_editor.cpp); the commit
//     title's "Enter a title for the checkpoint" (a card and no stderr line);
//     the text editor's two capacity refusals, reported by text_editor.cpp and
//     carded by the dispatch ("The pasted text is too long for this field",
//     "This field is full" at route_modal_editor_key — the settings recall's
//     own replace_selection call staying silent, that text being the
//     product's). The card drops the offending text, which stands on screen
//     in the red field.
//   * THE PROPAGATE REPORTS: the phase-reset pastes' "Stopped at …" and
//     kNothingMatched (propagate_blocks.h, two readers); a paste that wrote
//     no block skips the switch to target view, the card being the whole
//     answer, while a paste that paired blocks lands even byte-equal.
//   * THE CLIPBOARD WRITES, the one class of success that cards (nothing
//     paints a clipboard): "Copied the resolved value '<payload>'" (Ctrl+C)
//     and "Copied the selected markers' phase resets" (Ctrl+P); a refused
//     system-clipboard write cards through card_clipboard_refusal ("The
//     clipboard did not take the copy" / "…the cut" — the editors' Ctrl+X,
//     the one editor clipboard chord that cards, and only there); and the
//     phase copy that captured nothing, "No labeled, enabled markers are
//     selected, so nothing was copied". THE EDITORS' OWN Ctrl+C and Ctrl+V are
//     silent: an editor is its own world with that world's conventions.
//   * THE GITHUB CARDS (2026-09-27), Ctrl+S in the `h` view forking on the
//     status (open_history_commit_editor), each the KEY'S — the Save face
//     greys on the same status: "GitHub is still being checked", "GitHub
//     refused this device", "This device and GitHub have both moved" (stderr
//     names the fix), "GitHub has not been checked"; the pull's synchronous
//     refusals "Pull refused: this device has moved", "Pull refused:
//     '<piece>' has changes not committed", "Pull refused: GitHub's
//     checkpoint of this piece would not load" and "Pull failed: nothing was
//     changed". UNDER `offline` Ctrl+S CARDS NOTHING (architect 2026-09-28):
//     the press asks GitHub again and row 8's state cell carries the answer.
//     The GitHub CHECK itself raises nothing — its failing readings print one
//     stderr line each and the status word is state.
//
// CRITICAL — never TIMED OUT and never BUMPED (architect 2026-08-30:
// "critical cards keep standing"): no clock, and a later success clears
// nothing. Down only by a DELIBERATE dismissal: a click or tap on it, or the
// whole stack's dismissal (bare Esc, or a Shift-click or long press on any
// card). THE PRODUCERS, and nothing else (re-greped 2026-09-30): the three
// checkpoint failures ("Checkpoint failed: nothing was committed", "Checkpoint
// failed: files written but not committed", "Checkpoint committed but not
// pushed") and the three pre-commit refusals ("Checkpoint refused: GitHub has
// newer checkpoints" / "… cannot be reached" / "… refused this device"), all
// at the checkpoint completion (input_key_dispatch.cpp), critical because the
// act is asynchronous; the pull's one partial failure, "Pull failed: the
// files were updated but the branch did not move" (execute_history_pull — the
// clone left for the user to finish by hand); and the render player's
// "Could not delete '<folder>': <system words>" (render_player.cpp, one card
// per folder, since files may be half gone and the listing is all that says
// which).
//
// WHAT IS NOT A CARD, by ruling — these and no others:
//   * A BENIGN REFUSAL OF A ONE-DIMENSIONAL COMMAND ALREADY AT ITS STATE
//     (architect 2026-08-31): a refusal cards UNLESS it alters no output and
//     refuses a command that moves ONE THING IN ONE PLACE, where one glance
//     at that place answers whether anything happened — the Home / End jumps
//     (both forms and the `h` view's), the marker walk at an empty store or a
//     wall, the singleton tempo step at its bracket's end, Space with nothing
//     left to play, the history walk's walls and the diff cycle's ends,
//     Shift+0 over a full window, the picker's row on the project already
//     open, the render player's folder-end skips, Backspace at the root and
//     its idle family (no render loaded, seeking before playback), the
//     position nudge at its wall in both columns, the `h` view's Ctrl+S over
//     an empty head delta, and a held Ctrl+Z / Ctrl+Shift+Z whose repeat runs
//     out of history. THE COUNTER-CLASS IS THE TEST: a command whose effect
//     would have spread across the screen keeps its card even where its
//     button greys, "because they can be subtle and required me to check the
//     whole screen" — undo / redo over an empty stack on a deliberate press,
//     the GROUP tempo step, Shift+S in the P column, the propagate reports.
//     The act is unchanged where its sentence went; each site carries a
//     one-line comment naming this rule, and a reverse is per item.
//   * A BOUND KEY'S REFUSAL WHOSE REASON ROW 8 IS ALREADY SAYING (architect
//     2026-09-04): the target preview's two gates, Space's play edge and the
//     waveform scrub's launch, silent under the `Updating...` process line
//     with the Play face greyed. It is the ONE RECORDED EXCEPTION to the
//     deduction below: a silent press in target view under `Updating...` is
//     this refusal, not an unbound chord.
//   * A SUCCESS WHOSE RESULT IS ON SCREEN (architect 2026-08-31): a success
//     cards only where nothing shows (the clipboard writes above) or where
//     what shows would MISLEAD (kTrimFallbackCard) — and in the second case
//     the misleading thing is fixed where it can be. So: a render's
//     completion ("that would get annoying") and a cancelled one; THE SAVE,
//     the dirty mark going out being its answer (architect 2026-08-30: "the
//     disc writes — there is something that paints, the dirty dot goes
//     away"), row 8's `*` on both machines since 2026-09-09; a clean
//     propagate walk; a landed checkpoint or pull; and the crossed-trim reset
//     (auto_clear_crossed_trim — the endcaps snapping to the song's edges is
//     the cue, and a full window changes no output).
//   * A CHORD THIS PRODUCT BINDS NOWHERE IN THE STANDING MODE, wherever it is
//     pressed and whatever swallowed it (architect 2026-08-30, THE DEDUCTION
//     RULE: "bound keys either show an effect or a card, so an unbound key is
//     identified by its silence"), which retired the "<chord> is not bound"
//     class whole; the `h` view's seven shapes are bound only while the view
//     stands (2026-09-01), the render player's and the picker's keys only
//     inside their routers. THE UNBOUND POINTER PRESS goes with it: a
//     modified press the waveform, the top strip or the folder overlay's
//     band (pad, gaps and rows) binds nothing for says nothing, and the
//     render player's modified press on the scrub track likewise.
//   * TOP-LEVEL BARE ESC WITH AN EMPTY STACK — a retraction with nothing to
//     dismiss (the arm's other half clears the stack; the hit section below).
//     Esc inside a drag gate is NOT this case: Esc is bound, so the gate
//     cards it with every other bound key.
//   * EVERY CHORD UNDER AN OPEN MENU DROPDOWN (2026-09-02): dropdown_key_blocked
//     consumes everything but bare Esc and Ctrl+Q, bound chords included,
//     without asking chord_is_bound — a popup is a question on screen, the
//     standing prompt's own silence.
//   * THE T+W POINTER AUTHORING PAIR — the FLAG DRAG and the EMPTY-LANE
//     DOUBLE-CLICK DROP (the warp column's alone), because a gesture that
//     never begins is its own answer; the double-click drop's READ-ONLY arm is
//     silent on the same ground. Their keyboard twins card.
//   * A GREYED BUTTON'S LIFT and A GREYED DROPDOWN ITEM'S PRESS (the latter
//     consumed with the menu left open, 2026-09-24): the grey IS the message,
//     the card answering the keyboard; the grey is the PAINTED grey, so a
//     face painted live whose act refused inside the comparator's tick
//     dispatches and is answered as its key is. A tooltip states no reason
//     (2026-09-12: "cards for card-like info; tooltips for tooltips only;
//     disabled — or the lamp off — IS the message").
//   * THE TARGET-VIEW ENTRY GATE — one stderr line with the builder's words
//     (validate_target_view_entry): its refusals are unreachable from
//     program-written input, and a builder / resolver disagreement would
//     surface as "Target render failed".
//   * THE NO-PRODUCER BELTS, where an error arm would exist without a
//     producer (the type rule): the two drops' past-EOF walls, the
//     position nudge's leading state guards, and enter_bpm_mode's five-bail
//     recheck.
//   * The loader's fatal exits (the adversarial class: stderr and exit 1);
//     every QUESTION (the prompts and the dialog editors); the peaks-cache
//     rebuild lines (audio.cpp, stderr — they self-heal); the `h` entry's
//     fallback onto the local walk (its stderr line; the press that asks for
//     git is what cards); and what is TRUE NOW — the two state strings.
//
// THE WORDS ON A CARD (the product's text rules are stated once at
// paint_handler.cpp's menu-row block; a card is a DESCRIPTION, sentence case):
//   * ONE CLAUSE PER CARD, a statement, not a paragraph: no sentence-final
//     period, no second sentence, no instruction clause after the reason, no
//     number said twice. A PROMPT, by contrast, is always a QUESTION.
//   * NAMES AND PATHS ARE SINGLE-QUOTED ('…'), never backticked — one quoting
//     form across cards, prompts and stderr.
//   * A PATH NAMES THE FILE, never the full path (the basename rule and its
//     two-clause mechanism are GuiFailure's, failure.h).
//   * AN APPENDED REASON IS LOWERCASE (lowercase_initial below); THE SYSTEM'S
//     OWN WORDS (ec.message(), strerror) are appended as they arrive —
//     nothing lowercases libc.
//   * A KEY IS SPELLED BY spell_chord (gui_input.h) and nothing else.
//
// ONE PUSH CHOKEPOINT: GuiNotifications::notify. Every producer above calls
// it and nothing else writes a card.
//
// DELIBERATE PRESSES STACK THEIR DUPLICATES (architect 2026-09-01, retiring
// the 2026-08-30 unconditional dedup): each PHYSICAL press pushes its OWN
// card, so a wall hit three times shows three cards — his confirmation count,
// the same fact the roster's grey cannot give him because a key's refusal is
// what he is reading. THE ONE CARVE-OUT IS A HELD INPUT'S SYNTHESIZED
// REPEATS, which coalesce exactly as everything did before: a text identical
// to a card already in the stack in the same class removes that card and
// pushes it again at the TOP with a fresh clock, in both classes alike. The
// rule is "multiples are for distinct presses, not for a 30 Hz flood", and the
// bit that decides it is the key event's own
// (AppState::Notifications::held_repeat_dispatch, set from
// GuiInputState::synthesized_repeat at on_key's head — both of that bit's
// producers, the held KEY and the held BUTTON, dispatch through that body).
// A card raised off any other road — a worker's verdict, a pointer gesture —
// reads the bit false and stacks, which is the deliberate-press answer.
//
// WHY THE REPEAT MOVES ITS CARD rather than re-arming it where it stands: "in
// the stack" is not "on screen". An overflowing stack is clipped at the room's
// foot, so a live card can be wholly invisible, and the answer to the act the
// user is this moment performing must be visible. The top is also what a
// repeat means — the last time it happened.
//
// THE STACK IS UNCAPPED AND THE QUEUE IS RETIRED (architect 2026-08-30,
// "cards bump each other off the screen — newest on top, the oldest leaving;
// critical cards keep standing"). EVERY CARD IS VISIBLE FROM THE MOMENT IT IS
// PUSHED until it expires or is BUMPED — nothing waits unseen, so a normal
// card's clock always starts at its push and never at some later surfacing,
// and `notification_visible` is simply "the id is in the stack". What limits
// the stack is the ROOM, counted in one-line cards (notification_capacity):
// a push past that count removes THE OLDEST NORMAL CARD, walking from the
// back and SKIPPING every critical one, which is what "critical cards keep
// standing" means. A stack of criticals alone therefore keeps growing, and a
// burst of MULTILINE cards can exceed the room in pixels while sitting inside
// it in count; both overflow past the room's foot and the painter's clip is
// the answer, not a second rule.
//
// THE CLOCK RIDES THE RUN LOOP'S OWN DEADLINE TICK, polled: fire_if_due is
// called at the HEAD of main.cpp's on_tick, above every early return that
// body has — the startup load's, the render player's fork, the loading/blank
// guard and the playing-only guard — because a card is a message about
// something that has already happened, so nothing the tick does below can be
// a precondition for retiring one, while every one of those returns is a mode
// a card can stand over (the render player's own "No audio device to play
// the wav" card, the blank window's). It reads monotonic_ms() — the one clock every software
// deadline in the product is stamped on — and nothing here schedules anything.
//
// THE HIT (architect 2026-10-01, "whole card dismisses, X gone", reversing
// the 2026-08-29 rule that the X alone dismissed): THE CARD IS ONE BUTTON,
// the chrome's own model. A press anywhere on it ARMS it
// (AppState::ChromePress's Card kind) and dismisses nothing; THE LIFT inside
// the same card dismisses it; a lift outside the card it pressed dismisses
// nothing (the chrome's slide-away cancel). A SHIFT-CLICK OR A LONG PRESS
// DISMISSES EVERY CARD, criticals included — THE CHROME SHIFT LONG PRESS's
// own pairing, one term at the lift: the press-time shift ORed with a hold
// past chrome_shift_hold_ms(), with no visual announcement, on both machines
// (finish_notification_release, input_pointer.cpp). THE REASON IS THE
// TABLET'S: the laptop has bare Esc to clear every card at once and the
// tablet has no keyboard, so the pen was ticking the stack down X by X. The
// X went with the ruling, its hover fade with it, and the card wears no
// hover face and no pressed face. ONE RULE FOR FINGER AND MOUSE, as before:
// the router cannot fork on tap versus click (no origin bit rides a press;
// GuiInputState carries modifiers alone), and the whole card is a larger
// glass target than the X's 32 px box was.
//
// THE DISMISSAL IS THE LIFT'S — the ONE STATED EXCEPTION to "every dismissal
// stays at the press" (AppState::ChromePress's head): a card is an EVENT the
// user closes, not a popup the next press dismisses. A popup's dismissal is
// the side effect of a press aimed elsewhere, a card's is the press's own
// act, and an act lives at the lift — the only place a long press can be
// known. A HELD CARD'S CLOCK IS SUSPENDED (architect 2026-10-01: "holding
// turns off the timer, releasing re-enables"): from the press to the lift
// the pressed card's remaining life sits in the HOVER'S ONE BANK, the held
// press being a second reason the same card stands still and never a second
// mechanism (GuiNotifications::press_hold_edge); a slide-away lift re-arms
// it, and a card already due at the press is not banked (the hover's rule:
// a pause does not resurrect). A held card can still be BUMPED by a push or
// cleared by Esc; the lift re-asks the live stack and then lands nothing.
//
// BARE ESC IS THE KEYBOARD'S WHOLE-STACK DISMISSAL (since 2026-08-31; the
// whole stack, CRITICALS INCLUDED, since 2026-09-01), at the tail of its own
// ranking, and it lands on no card at all. THE POINTER AND THE KEY DIFFER IN
// RANK, deliberately: the card's claim sits ABOVE EVERY VEIL because a card
// must be dismissable under any modal, while Esc sits UNDER all of them —
// every other Esc place is earlier in the dispatch, so the key reaches the
// stack only when nothing modal stands and no render is in flight (the EIGHT
// places are enumerated at on_key, input_handler.cpp; the arm itself is
// handle_plain_bare_keys'). Neither road reads a class — a critical card is
// dismissed like any other — so what takes a critical card down is always a
// deliberate act, never the CLOCK (a critical card has none). That asymmetry
// is deliberate and is the record a reversal would start from: if the
// criticals should survive the whole-stack dismissal, dismiss_all is the one
// line to change.
//
// THE ESC ARM'S OWN SUCCESSION, in two rulings a day apart: it was born
// 2026-08-31 taking the stack's OLDEST card alone — clearing the top would
// have let the bottom card stick around preferentially, so the key emptied
// the stack from the back the way the clock does, one press per card — and a
// BULK FORM, Ctrl+Esc, was bound beside it at the head of on_key on the
// morning of 2026-09-01 to reach a whole stack from under any modal. THE
// CHORD RETIRED THE SAME EVENING and its act moved onto the bare key: "Esc
// should clear all notifications", one act wanting one chord rather than two
// roads onto it. Ctrl+Esc is unbound-silent now like every other modified
// Escape, and the bare key's rank is unchanged — it is still the LAST of the
// eight places, under every modal, so a standing surface takes the press for
// its own close and the stack waits.
//
// THE PRESS IS CONSUMED WHOLE, on both backends and whatever the button: it
// arms nothing else, moves nothing, lands no playhead and reaches nothing
// underneath (a non-left press is the consumed nothing it always was, and
// arms no card). The claim ranks ABOVE EVERY VEIL (the prompt's, the
// player's, the picker's, the dialog editors') and above the open dropdown's
// claim, and THE RELEASE IS TAKEN AT THE SAME RANK — above the dropdown's
// release, which would otherwise consume it, and above every veil's —
// because a card is not a reach into the veiled surface: it is the message
// about the act the veil stands over, and it must be dismissable under any
// of them.
//
// THE GEOMETRY A CARD IS HIT BY IS THE GEOMETRY IT WAS PAINTED WITH: the
// painter publishes each visible card's rect into
// AppState::Notifications::painted, and the router, the cursor map and the
// hover walk read that publication and then ask the live stack whether the
// id still stands (published geometry may only SELECT; live state decides).
// The owners below are pure functions of the window and the scale — the
// damage ROOM and the ONE-LINE card height — and neither knows a sentence:
// a card's real height needs its line count, the line count needs a shaped
// run, and shaping needs the paint's own font, so the painter is the only
// place that can answer it and the room is what the damage owner uses.
//
// NO DOUBLE SPACE, ANYWHERE THE PRODUCT WRITES TEXT: prompt text and its
// options join with single spaces, and no card, prompt or stderr line carries
// two spaces in a row (the WARPTEMPO_PROFILE instrumentation that was the
// sole exception is removed).

#include "app_state.h"
#include "viewport.h"

#include <cstdint>
#include <string>
#include <string_view>

// -- SENTENCES MORE THAN ONE TRANSLATION UNIT RAISES -------------------------
//
// A card's words live at the site that raises them. These two do not, because
// SEVERAL sites raise each and they must not drift: a sentence spelled twice
// is two sentences the moment one of them is edited. Everything else stays a
// literal where it fires (a sentence with ONE producer has nothing to agree
// with), and a family whose several sites share ONE translation unit keeps its
// constant there (kKeysDuringDrag; the two mode routers' catch-all tails were
// a second until their catch-alls went silent with the unbound-keys ruling). A THIRD HOME EXISTS for a sentence that
// belongs beside the VERDICT it spells rather than beside its one raiser: the
// grid-iteration sweep's three cards live at app_state.h next to
// iteration_sweep_plan, the owner whose refusal each of them names, because
// this header includes app_state.h and not the reverse. They are raised from
// one translation unit — the sweep's own dispatch — the Render button reading
// only the verdict, its grey being the roster's whole message since the
// refusal-reason tooltip class went on 2026-09-12. The third item below is
// not a sentence at all
// but the one COMPOSER several sites share, lowercase_initial, homed here for
// the same reason.
//
// THE LOCK'S SENTENCE — the read-only tab, said by the sites that KNOW THEIR
// ACT and so need no chord in it: the settings editor's ENGINE-KEY commit arm
// (an engine key is the piece; the arm took the sentence from the editor's
// opener on 2026-09-04, when the lock moved from the surface to the keys — the
// account is at GuiSettingsEditor::open, and the Settings menu's items grey
// on a load alone, so the commit owes the answer itself), the render
// player's Load in place, and the `h` view's bare `v`, whose admission
// composes the subject with this same lock. THE FIRST TWO READ IT THROUGH
// authoring_lock_card BELOW SINCE 2026-09-10 — the lock gained a second
// reason, grid iterations, and those two acts refuse under both — while the
// `h` view's fork keeps the literal, the view being unreachable under the
// second reason. THE KEYBOARD GATE IS
// NOT A READER (since 2026-08-30): it says "<chord> is not available on a
// read-only tab" through the speller and the composer below instead, naming
// what was pressed, which is what a user who just pressed it is looking for. Its predicate is the
// complement of an allowlist and so drops an unbound chord with a bound
// authoring one, which is why that gate asks chord_is_bound first and answers
// the unbound half with silence. The full reasoning is at that gate
// (input_handler.cpp).
inline constexpr const char* kTabReadOnlyCard = "This tab is read-only";

// THE SAME LOCK'S SENTENCE WITH THE CHORD IN IT, composed once — "<chord> is
// not available on a read-only tab", the words the keyboard gate has said
// since 2026-08-30 (the reasoning for naming the press rather than the tab is
// at that gate, input_handler.cpp). It takes the ALREADY-SPELLED chord rather
// than the key and its modifiers, so this header needs nothing of the
// speller's; every caller reaches spell_chord (gui_input.h) for itself.
//
// TWO CALLERS, and the second is why it is a composer at all: the keyboard
// gate (on_key), whose subject is the ACTIVE tab's bit, and BARE `i`'s OWN
// ARM (input_key_dispatch.cpp), whose subject is the PIECE — grid iterations
// refuse to light while EITHER tab is locked, the two A/B tabs sharing both
// marker stores (any_tab_read_only, app_state.h). The two subjects differ and
// the SENTENCE MUST NOT: one press of `i` says one thing whichever tab is the
// locked one, which is exactly what a second spelling of this English would
// have drifted away from.
//
// THE TAIL IS A CONSTANT IN app_state.h SINCE 2026-09-10
// (kReadOnlyChordCardSuffix), for that header's own third-home reason: a FACE
// read this English then — the Grid Iterations button's read-only line was `I`
// plus this very tail — so the words went where the faces are compiled. That
// second reader went on 2026-09-12 with the whole refusal-reason tooltip
// class, leaving this composer the tail's ONE reader; the constant stays where
// it is, this header including app_state.h and not the reverse.
inline std::string read_only_chord_card(const std::string& chord) {
    return chord + kReadOnlyChordCardSuffix;
}

// THE ITERATION LOCK'S SENTENCE IS kIterationLockCard AND IT LIVES IN
// app_state.h, beside the sweep's two verdict cards, on the reason this header
// states above — a FACE read it, the Toggle Marker Column button's hint being
// that sentence plus its accelerator — and it stays there now that no face
// does: the refusal-reason tooltips went on 2026-09-12, and this header
// includes app_state.h and not the reverse, so the literal is reachable where
// it stands. It is the read-only sentence's sibling all the same, and the
// composer below is where the two meet.

// THE ONE COMPOSER THE LOCK'S CARD FORKS AT, and the fork CHOOSES rather than
// ranks: the two locks are MUTUALLY EXCLUSIVE since 2026-09-10
// (authoring_locked, app_state.h — a lit lamp cannot be locked and a locked
// tab cannot be lit), so exactly one arm can ever be reached. It was ORDERED
// until then, read-only first, a tab being lockable while the lamp was already
// lit; the body did not change with the ruling, only what it means.
// Its readers are TWO of the three
// sites that KNOW THEIR ACT and so need no chord in the sentence — the
// settings editor's engine-key commit arm and the render player's Load in
// place; the third, the `h` view's bare `v`, keeps the bare literal because
// the view cannot stand under a lit lamp at all (its entry refuses with the
// iteration sentence), so a fork there would have one live arm. The keyboard
// gate composes its own, the read-only half of it carrying the spelled
// chord.
inline const char* authoring_lock_card(const AppState& a) {
    return active_view_state(a).read_only ? kTabReadOnlyCard
                                          : kIterationLockCard;
}

// THE CHECKPOINT-PUBLISHING SENTENCE, ONE LITERAL (2026-08-30), homed here
// since 2026-09-24, when the save owner became a raiser from its own
// translation unit. ONE FACT, ONE WORDING, and its readers are: the SAVE
// OWNER's in-flight refusal (GuiSaveOps::save, save_ops.cpp), which every
// plain Ctrl+S road reaches — on_key, the editors' modal contract, the
// picker's router, the render player's fall-through —
// and the close prompt's Save answer; bare `h`'s entry refusal (which also
// prints it on stderr); the Open project picker's open act and File > Revert;
// and the commit act's own opener (open_history_commit_editor), the `h`
// view's Ctrl+S. All of them meet AppState::history_checkpoint_in_flight.
// ONE CLAUSE (architect 2026-09-01, the capitalization sweep's sentence
// shape): the sentence is the instruction. It read "A checkpoint is still
// publishing; try again when it finishes" until that day.
inline constexpr const char* kCheckpointPublishing =
    "Wait for the checkpoint to finish publishing";

// THE PROJECTS PATH COMMIT'S SENTENCE (2026-09-02): a `projects_path=` commit
// from the settings editor rewrites the device config and changes nothing on
// screen — the open project stays open on its absolute paths — while File →
// Open project and the next launch read the new folder at once (the whole
// account is at GuiSettingsEditor::commit_device_setting,
// settings_editor.cpp). A press whose result nothing paints says where it
// applies; one clause. It is raised only when the config write succeeded
// (2026-09-04): the next launch reads the file, so a failed write makes that
// half of the sentence false, and the write's own failure card is then the
// press's whole answer.
inline constexpr const char* kProjectsPathAppliesCard =
    "Projects path applies at the next Open project and the next launch";

// THE TRIM FALLBACK'S SENTENCE (architect 2026-09-02) — a
// proper sub-window whose TARGET span rounds below one output sample, which
// plan_trim refuses and every orchestrator answers by rendering the FULL,
// untrimmed piece (the window's purpose is at TrimState, app_state.h). The trim bar and the
// waveform overlay go on painting the hairline window the user drew, so the
// screen says "this span" while the audio is the whole movement — the shape
// the strictness ruling's "what shows would mislead" test cards. A MINIMUM
// TRIM SIZE WAS THE ALTERNATIVE AND IS REJECTED (2026-08-19,
// write_trim_from_sweep's record: kMinTrimSpanFrames quantized the shift+drag
// unpleasantly and was retired one day after it landed) — the sweep authors
// exactly the span it draws and the outcome is announced instead.
//
// TWO TRANSLATION UNITS RAISE IT, which is why it is homed here: the preview's
// own dispatch (target_render.cpp, once per FALLBACK-SUBJECT edge — the whole
// record {fell_back, tab, trim begin, trim end} changing INTO a fallback, so a
// distinct tab or a distinct trim pair cards again under a verdict that stays
// true, and a non-fallback dispatch re-stamps fell_back = false so re-entry
// into the same tab and pair cards again; the contract is at
// TrimFallbackSubject, target_render.h) and the two archival commands
// (input_key_dispatch.cpp, once per press). It names
// the OUTCOME rather than either producer, so the one sentence covers both
// plan_trim refusals the verdict carries (only the sub-sample span is
// reachable from a resting store; the crossed pair is the breach mirror).
//
// AND IT IS PRESENT TENSE, because no producer raises it after the fact: the
// two archival commands card AT THE PRESS and the preview at its dispatch,
// both ahead of the synthesis, so "rendering untrimmed" is true at every raise
// while a past tense would assert work that has not run yet.
inline constexpr const char* kTrimFallbackCard =
    "Trim window too small to render; rendering untrimmed";

// THE OFF-EDGE MARKER'S TWO SENTENCES (architect 2026-09-26, strictly as
// painted: a marker may not be authored where its column would not paint).
// One owner decides the refusal, source_frame_off_right_edge
// (warp_frame_map_view.h — a frame in the song's last half-column when the
// song fills the window, whose column no viewport paints at this zoom: a
// marker there shows its flag's left border alone on the last column, no
// stem, and a border is not the marker's column); each act says it in its
// own verb.
// The NUDGE's is raised by the two nudge twins through their reason channel
// (the singleton's on the keyboard alone, its Right button greying there; a
// group's after its collapse); the
// DROP's by the two at-playhead drop bodies through theirs (bare `s` in
// either column and Shift+S's lead-in drop; the empty-lane double-click
// seats the playhead on a painted column first and cannot meet it). "At this
// zoom" is the sentence's truth: zooming in brings the column on screen.
inline constexpr const char* kMarkerNudgeOffEdgeCard =
    "The marker would move past the edge at this zoom";
inline constexpr const char* kMarkerDropOffEdgeCard =
    "The marker would sit past the edge at this zoom";

// THE `h` VIEW'S ARROW REFUSAL (architect 2026-09-26), the marker lane's
// sentence in the history view: with a diff flag focused (or selected) bare
// Left / Right would be the live nudge's press, and the view authors nothing,
// so the press refuses on this card — the T+W refusal's shape ("Markers are
// moved in source view", on_key's marker-lane branch) over the mode's own
// focus. ONE RAISER, handle_history_mode_key's Left / Right arm
// (input_key_dispatch.cpp); the Left / Right buttons grey on the same term
// (horizontal_arrow_step_actionable's `h` arm, app_state.h), so a lift never
// reaches it.
inline constexpr const char* kHistoryViewMovesNoMarkersCard =
    "The history view moves no markers";

// AN APPENDED REASON IS LOWERCASE (architect 2026-09-01, the capitalization
// sweep; the rule is stated once in this header's card section, over the one
// statement of the product's text rules at paint_handler.cpp's menu-row
// block). A sentence composed as "<Act> refused: <reason>" is ONE sentence, so
// its tail does not start a second one; a producer whose string is ever used
// WHOLE is a sentence in its own right and capitalizes at that producer.
//
// IT WAS WRITTEN FOR THE ONE FAMILY THAT IS BOTH — the past-EOF wall
// defects (first_past_eof_wall_defect, src/parser/marker_store_validate.cpp),
// which are used WHOLE by the picker's dry-run card and by the CLI's stderr
// and APPENDED by four GUI seams: "Revert refused: ", the render entry load's
// own `refuse`, the `h` view's "Load in place refused: " and the loader's
// "Source load aborted: ". The producer is under the parser's
// PERMANENT HARD FREEZE, so the case moves at the APPENDING seams instead of
// at the producer, which is also what keeps the two whole-message consumers
// right without a second edit.
//
// FIVE CALLERS ACROSS TWO PRODUCER FAMILIES (re-greped 2026-09-02; the
// inventory of four was the wall defects' alone): the fifth is the FAILED
// RENDER'S CARD (input_render_dispatch.cpp), which appends `GuiFailure`'s
// display clause after "Render failed: ". That family is not frozen and
// composes its own text — it takes the helper for the same reason, an
// appended reason is lowercase, and for no other.
//
// ONE OWNER, homed here because a card composer is what asks for it and
// notifications.h is the header every one of those seams already sees. ASCII
// ONLY, by construction: it lowers `A`-`Z` and touches nothing else, so a
// reason opening with a digit, a quote or a UTF-8 lead byte passes through
// unchanged (every producer it serves is ASCII prose).
//
// THE TERMINAL'S CASES (architect approval 2026-08-02, the terminal
// capitalization pass — text-only, otherwise byte-identical; stderr / stdout
// prose follows the product's one set of text rules, paint_handler.cpp's
// menu-row block): the first prose word after the program-name prefix
// capitalizes (`warptempo_gui:` / `warptempo_cli:` and routing tags such as
// `icons:` keep their spelling); a new sentence after ". " capitalizes; text
// after ":" or ";" stays lowercase; a message headed by an identifier, key
// name, path or expression keeps that token verbatim and starts
// uncapitalized (`tab_%c`, `projects_repo set:` — the underscore spelling is
// a key, the spaced one prose); the engine's dot-leader progress rows and
// bracketed status tags are STRUCTURED READOUT and unchanged; a feeder string
// printed whole anywhere carries its capital AT ITS DEFINITION, the embedded
// mid-clause capital being the accepted cost, while an embedded-ONLY clause
// vocabulary (the settings and parse rejection reasons, the flag editor's
// clauses) stays lowercase. BPM capitalizes always as an acronym, "iterations"
// never, and every bpm DATA token (the `<N>_bpm` folder tag, `bpm=`, the
// schema key) stays lowercase; flag labels are lowercase by grammar
// (is_valid_label_format).
inline std::string lowercase_initial(std::string_view s) {
    std::string out(s);
    if (!out.empty() && out[0] >= 'A' && out[0] <= 'Z')
        out[0] = static_cast<char>(out[0] - 'A' + 'a');
    return out;
}

// The card's width is its content's, clamped to [this, kNotificationMaxWidthPx
// below]. Authored px, scaled like every other length.
//
// THE FLOOR IS THE UNDO CARD'S OWN WIDTH (architect 2026-09-01): he steps undo
// and redo deliberately as a sanity check, and the two walls' cards — "There
// is nothing to undo" and "There is nothing to redo", the same sentence but
// for one letter — came out a few pixels apart and the difference read as a
// flicker between two presses of the same shape. Raising the floor above BOTH
// makes them one card: the clamp answers the floor for every sentence shorter
// than it, so the pair, and every other short refusal with them, paints at one
// width.
//
// THE MEASUREMENT, at 100 % in the card's own face (the one sans at
// redesign_font_size_px, 16 px): "There is nothing to undo" shapes to 172.73 px
// and "…to redo" to 169.16, and the card then added its chrome — four pads
// and two button boxes, 4 x 7 + 2 x 32 = 92 — for 265 px and 262 px.
// 272 was the next multiple of 8 above the wider of the two, which is the
// number this constant is: a clean authored round-up with 7 px of air over the
// sentence that set it, so a face retune of a pixel or two does not silently
// put the pair back at two widths. THE X'S RETIREMENT (2026-10-01) SHRANK THE
// CHROME AND MOVED NOT THE FLOOR: three pads and the glyph's one box,
// 3 x 7 + 32 = 53, put the pair at 226 px and 223 px, both under 272 by more
// than before, so the clamp still answers the floor for both and they still
// paint at one width; the width rule was not part of the ruling and the
// floor keeps the number it was measured at. AUTHORED PX, so the relation
// holds at every gui_scale — the sentence and the floor scale together
// through scaled_px.
//
// IT DOES NOT MEET THE CEILING: kNotificationMaxWidthPx is 640 authored px and
// notification_card_max_w_px already floors its window safety here, so the
// clamp's own precondition (floor <= ceiling) is untouched by the rise and a
// window too narrow for the floor keeps overhanging exactly as before.
inline constexpr double kNotificationMinWidthPx = 272.0;

// THE CARD'S CEILING IS THE LAPTOP'S OWN WIDTH, AUTHORED AND SCALED (architect
// 2026-08-31): 640 authored px is what the retired `window / 3` gave on the
// 1920 px laptop, so this constant is that width made CANONICAL — the same
// card at every window and, through `scaled_px`, the same card in millimetres
// at every gui_scale. The window fraction was DEVICE PIXELS and so starved the
// tablet: at 225 % its 1440 px panel gave a 480 px card for text shaped half
// again as large, three words to a line. IT MAY NOW COVER BUTTONS ON THE
// TABLET and that is accepted in his own words — "it's okay if it covers up
// some buttons": a card is a sentence to read and it leaves on its own.
// A length, so it scales; the life beside it (kNotificationMs) is a duration
// and does not.
inline constexpr double kNotificationMaxWidthPx = 640.0;

// HOW MANY LINES A SENTENCE MAY TAKE (architect 2026-08-30, "a few"): a card
// whose sentence does not fit its room GROWS DOWNWARD to this many lines and
// no further — the last permitted line takes the WHOLE remainder as one run
// and clips at the right edge, so no word is ever silently dropped, only cut
// where the eye can see the cut. A COUNT, not a length: it does not scale.
inline constexpr int kNotificationMaxLines = 3;

// -- Geometry the painter, the damage owner and the hit share ---------------

// A ONE-LINE card's height, and the height every card's FIRST line occupies:
// the icon row's content height (the 32 px button box plus its 7 px margins,
// one source) — the glyph sits in that box at the row's own inset,
// AT THE FIRST LINE'S HEIGHT WHATEVER THE LINE COUNT (architect 2026-08-30:
// the text grows downward under the icon, nothing reflows beside it; the X
// that sat beside the glyph at that height retired 2026-10-01).
// A card of `lines` lines is this plus (lines - 1) line spacings, which only
// the painter can know — a line count needs a shaped run, and shaping needs
// the paint's own font — so no pure function of the window states a card's
// real height and none is offered here.
int notification_card_h_px();

// THE CARD'S ONE PAD (architect 2026-08-30): the padding around the glyph
// and the text is ONE NUMBER, the box's own vertical margin — the
// centering the card's height already derives from the icon row (46 = 32 +
// 2 x 7). It is read for ALL FIVE of the card's distances: left edge ->
// glyph box, glyph box -> text, text -> right edge, top -> box, box ->
// bottom (six until the X's box retired, 2026-10-01: text -> X box and
// X box -> right edge became the one text -> right edge). Nothing is
// authored here: the number IS
// (notification_card_h_px() - the button box) / 2, so a retune of either
// moves all five together, and the painter reads no foreign constant (the
// icon row's lane pad and the folder overlay's icon-to-name gap both left it
// that day — the overlay's rows keep their gap, that being their surface).
// PARITY: where card_h - btn is odd the integer floor puts the extra pixel
// BELOW the box, exactly as the icon row's own centering does for its
// buttons — the same floor, not a second rule.
int notification_pad_px();

// The card's largest possible width at this window and scale: the scaled
// ceiling above, and — where the window cannot even hold that — the window
// itself less the stack's two side margins. THE WINDOW TERM IS A SAFETY AND
// NOT A DESIGN (architect 2026-08-31, retiring `window / 3`): the ceiling is
// what the design says a card is wide, and the clamp only keeps a card inside
// a window too narrow for it (the tablet's portrait panel, a contrived
// window). IT NEVER ANSWERS BELOW THE FLOOR: the painter clamps a card's
// content width into [floor, this] and THE ROOM BELOW IS THIS SAME NUMBER, so
// a bound under the floor would put painted pixels outside the rect that
// damages and publishes them. A window narrower than the floor itself
// therefore keeps the floor and lets a card overhang, exactly as the retired
// fraction did — the contrived window this file declines to cater for.
int notification_card_max_w_px(const AppState& a);

// THE STACK'S ROOM: the rect the stack may occupy — the maximum card width,
// right-aligned at kPanelPadPx from the window's right edge, from that same
// kPanelPadPx of air below row 1 (architect 2026-08-29: the two margins are
// one number; the right one was the icon row's 8 px pad for the cards' first
// day) DOWN TO THE SAME AIR ABOVE THE BOTTOM ROW'S LANE.
//
// IT IS THE ROOM AND NO LONGER A TIGHT BOUND (2026-08-30, with the wrap): it
// was "three cards of one line each", which a wrapped card outgrows, and no
// pure function of the window can know a card's line count — that needs a
// shaped run. So the answer is the whole space the stack has to grow into,
// and the painter CLIPS to it: a stack that outgrows the room paints on down
// and is cut at the room's foot, so no card ever paints over the bottom row,
// and the painter publishes its rects CLIPPED TO THE ROOM TOO, so nothing
// under that foot is ever claimed by a card. (A stack that tall is the
// contrived case this design declines to cater for.)
//
// Every change to the stack damages this rect
// (Viewport::invalidate_notification_stack): the painted cards lie inside it
// by construction, so it erases what stood and admits what comes without
// shaping a single glyph off the paint clock. A window with no room between
// row 1 and the bottom row answers a zero height and paints nothing — a
// window with no waveform at all, in the contrived class.
GuiRect notification_stack_bound(const AppState& a);

// HOW MANY CARDS THE ROOM HOLDS, and so what a push bumps past (architect
// 2026-08-30, the uncapped stack): the number of ONE-LINE cards that fit the
// room above, `floor((room + gap) / (card + gap))`, at least one.
//
// IT IS A COUNT OF ONE-LINE CARDS AND NOT A PIXEL BUDGET, deliberately: no
// pure function of the window can know a card's line count — that needs a
// shaped run, and the model layer this serves has no font to shape with (the
// same reason the damage owner takes the room rather than a tight bound). So
// the cap counts the cards the room WOULD hold at one line each; a stack of
// wrapped cards can pass the room's foot while inside this count, and the
// painter's clip is what answers that, exactly as it answers a stack of
// criticals that will not be bumped.
//
// A pure function of the window and the scale, like the room. At a 1080 px
// window and 100 % it is 20 (a 999 px room over 46 + 2); on the tablet at
// 200 % it is 13 (1278 over 92 + 4), its window being the whole 1440-tall
// panel since the activity went full screen (2026-10-01; the stacks are
// recorded at main.cpp's vertical-stack owner). Every window this product
// runs in holds more cards than the architect will ever stack.
int notification_capacity(const AppState& a);

// Whether `id` names a card that is IN THE LIVE STACK — which is the same as
// "on screen" since the queue retired (2026-08-30): a push makes a card
// visible at once and only an expiry, a dismissal or a bump removes it. THE ONE LIVE
// TEST every act on a published hit asks, and the one dismiss() asks of its
// argument — the publication is a paint old, and a card can expire or be
// bumped between that paint and the press, or between the press and its lift.
bool notification_visible(const AppState& a, uint64_t id);

// The card under (x, y), or 0. PUBLISHED GEOMETRY MAY ONLY SELECT, LIVE STATE
// DECIDES (the owner-tag doctrine, recorded at ModalDialogGeometry): the walk
// picks an id out of the painter's rects and then answers 0 unless
// notification_visible still holds for it, so a card that expired or was
// bumped between the paint and the event is hit by nothing,
// and a press on the stale rect falls through to whatever the NEXT paint will
// put there, which is what the user is about to see. A card under the OPEN
// DROPDOWN's box yields to it — the dropdown is the one pointer-owning
// surface that paints above the cards — so every reader agrees on the z-order
// in one place too.
//
// THE READERS ARE THE CARD'S OPACITY, re-greped at this declaration: the
// press claim (claim_notification_press) and its lift
// (finish_notification_release, which asks it for the SAME card at the
// release's own coordinates), the cursor map
// (pointer_cursor_kind), the card hover walk (GuiNotifications::update_hover),
// the WHEEL's routing predicate (wheel_context, which swallows a detent over
// a card), the touch pan zone (touch_point_in_pan_zone) and the two hover
// walks a card can stand over — the roster's
// (recompute_redesign_button_hover) and the folder overlay band's
// (update_folder_overlay_hover), each answering "nothing under the pointer"
// so no surface beneath a card wears a face or promises a press.
uint64_t notification_card_at(const AppState& a, int x, int y);

// -- The operations ---------------------------------------------------------

struct GuiNotifications {
    AppState& app;
    Viewport& viewport;

    GuiNotifications(AppState& app_, Viewport& viewport_)
        : app(app_), viewport(viewport_) {}

    // THE ONE PUSH. A card goes on TOP and is visible at once, its clock
    // started here; then THE BUMP brings the stack back inside
    // notification_capacity by removing the oldest NORMAL card, never a
    // critical one and never the card just pushed (the full argument is at the
    // site). A DUPLICATE STACKS (2026-09-01) unless this dispatch is a HELD
    // INPUT'S SYNTHESIZED REPEAT, in which case the matching card is removed
    // and re-pushed at the top with a fresh clock, in both classes alike — the
    // ruling and its one bit are at the site and at the head of this file.
    void notify(AppState::NotificationClass cls, std::string text);

    // THE CARD'S LIFT (architect 2026-10-01; the X's act until then), and the
    // pointer's alone: remove the named card whatever its class and state,
    // and drop the hover if it was this card's. The card is the one the press
    // armed, re-hit at the lift's coordinates, and the live test below is
    // asked of that argument.
    void dismiss(uint64_t id);

    // THE WHOLE STACK'S DISMISSAL: BARE ESC's act (2026-09-01, superseding the
    // 2026-08-31 arm that took the OLDEST card alone) and, since 2026-10-01,
    // the SHIFTED OR HELD LIFT on any card — the whole stack, CRITICALS
    // INCLUDED, and the hover with it. An empty stack is a silent nothing —
    // no damage, no card. The reasoning and the reversal record are at the
    // hit section above.
    void dismiss_all();

    // THE HELD PRESS'S EDGE (architect 2026-10-01, "holding turns off the
    // timer, releasing re-enables"): AppState::chrome_press's Card arm on
    // card `id` has just been raised (claim_notification_press) or dropped
    // (take_chrome_press — the lift, and the button-lost and pointer-leave
    // clears through it), so re-answer that card's bank. The arm IS the
    // record of the hold — nothing here keeps a second one — and the bank is
    // the hover's own: the hold is a second reason the card stands still,
    // never a second mechanism (rebank). A card already gone answers nothing.
    void press_hold_edge(uint64_t id);

    // THE CLOCK, on the run loop's deadline tick: retire every normal card
    // whose life has elapsed and is not paused, and re-derive the hover from
    // the remembered pointer (a card that slid up under a motionless pointer
    // pauses from this tick on).
    void fire_if_due();

    // The hover walk, from the motion handler and the tick: which card the
    // pointer rests on. Entering a visible normal card banks its remaining
    // life; leaving re-arms it unless a press still holds it. A card whose
    // deadline has ALREADY passed is not banked — hover pauses a clock, it
    // does not resurrect one — so it retires on the next fire_if_due as an
    // unhovered one would. It paints nothing and damages nothing: the card
    // wears no hover face since its X retired (2026-10-01).
    void update_hover(int x, int y);
    // The pointer-left hook's half: no card is hovered, every bank the hover
    // held re-armed (a card a press still holds keeps its bank).
    void clear_hover();

private:
    // (start_visible_clocks and is_visible_index went with the QUEUE on
    // 2026-08-30: no card waits unseen any more, so the only clock a push
    // starts is its own card's, written where that card is built, and no
    // index is "visible" or not.)
    void set_hover(uint64_t id);
    // THE ONE BANK'S ONE WRITER: re-answer card `id`'s pause from its two
    // reasons — the pointer resting on it (`hovered_id`) and a press holding
    // it (AppState::chrome_press's Card arm). Banks a running normal card's
    // remaining life when the first reason begins, re-arms it from now when
    // the last one ends, and banks nothing for a card already due.
    void rebank(uint64_t id, int64_t now);
    AppState::Notification* find(uint64_t id);
};
