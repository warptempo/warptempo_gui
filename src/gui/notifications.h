#pragma once

// THE NOTIFICATION CARDS (architect design 2026-08-29; THIS HEADER IS THE
// RULING WHOLE — the split, the two classes, the inventory of what is and is
// not notified, and the words on a card). The product's surface for EVENTS:
// something happened that answers an act, or that the user was not watching
// — and ONE STATE WORN AS A CARD, the struck warp markers' (the critical
// class below, 2026-11-19).
// A small card stacked at the window's top right, notifyd's seat, over
// whatever stands there (the card seat, notification_stack_bound), newest on
// top, EVERY CARD IN THE STACK VISIBLE, UP TO kNotificationMaxLines lines of
// the one sans.
//
// THE LOOK IS XFCE4-NOTIFYD'S BUBBLE UNDER ARC (architect 2026-11-19:
// "Agree" to "normal cards the grey notifyd bubble, standing cards Arc's
// red", and "good to build" on the approved picture): A ROUNDED BOX
// (kNotificationRadiusU), NO BORDER, NO SHADOW (notifyd draws none,
// measured), ITS FILL A CARRIED FILL AT ITS
// OPACITY OVER WHAT LIES UNDER IT (gui_theme.h's THE THREE CARRIED FILLS; the
// canvas's pixels composited by the GPU, canvas_sink.h's THE TRANSPARENCY
// INVARIANT) — A NORMAL (timed) CARD notifyd's grey `card_fill` (arc_dark
// 353945 at .95) with its words and glyph in `sidebar_text` (BAC3CF), A
// STANDING (critical) CARD Arc's destructive red `card_standing_fill`
// (F04A50 at .95) with its words
// and glyph in `card_text`, the print on a lit face (FFFFFF). INSIDE IT
// notifyd's one padding (kNotificationPadU, 14 U on all four sides), THE CLASS GLYPH in a
// kNotificationGlyphBoxU drawing box at the left —
// icons.h's: DialogInformation on a normal card, DialogError on a standing
// one, each the mark cut out of its shape (the i and the ! cut out of a
// disc), the mark showing the card's face through one color — then the same padding again, then THE CLAUSE,
// Regular 12 U, one line box (14 U) a line, a single line centered on the
// glyph's box (its baseline +26 U), wrapped lines from the top pad (the
// first baseline +25 U). THE WHOLE CARD IS ONE BUTTON (architect 2026-10-01,
// "whole card dismisses, X gone"): a click or tap anywhere on the card
// dismisses it at the lift (the hit section below). A sentence too long for
// one line WRAPS DOWNWARD (architect 2026-08-30): the card grows taller and
// nothing reflows. Two classes and nothing else — no remaining-time bar, no
// actions, no title/body split, no sound (the architect is "not a big fan of
// notifications": minimal, and the inventory below is closed).
//
// THE SPLIT, STATE AND EVENTS (2026-08-29), which decides the surface: STATE
// is what is true right now, replaced as it changes, never timed out and
// never cleared by a key press — the render's progress line and the `h`
// walk's line, which live in THE STATUS BAR, the window's last lane under
// the bottom icon row (architect 2026-11-19: the process line "a
// must-have" there; render.h's status bar block, paint_handler.cpp's
// status_bar_text the one composer and paint_status_bar the painter: its first
// field, the selection count and the history view's GitHub word beside it) — A
// BATCH'S PROGRESS IS THE PROGRESS MODAL'S (2026-10-14; the progress page:
// "Rendering...", the bar and "X of Y" in the modal family's box from the
// Render press to the batch's terminal, AppState::BatchProgress), THE STATUS
// BAR BLANK UNDER IT (architect 2026-11-20: the page says what the
// sweep's own line, "Rendering N of M <label>...", would; status_bar_text);
// on the main page the line is a single
// render's "Rendering..." and a preview's "Updating..."; an
// EVENT is an act answered with a sentence, or a background act that
// finished badly — a CARD. The state surface has NO TIMEOUTS because nothing
// in it is a claim about a past moment, so nothing there can go stale, and
// NO CARD RETIRES INTO IT (architect 2026-11-19: "No, I decline both of
// those").
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
// lift of a press on it, at a BUMP, or at the whole stack's dismissal (a
// long press on any card).
// THE INVENTORY, by family and owner (each
// family's sentences are literals at their raisers; re-grep `notify(` before
// restating a count):
//   * THE GATES' OWN CARDS, the refused press answered by the state that
//     refused it, each asked only for a chord the product BINDS IN THE
//     STANDING MODE (chord_is_bound, gui_input.h): kKeysDuringDrag
//     (input_handler.cpp: the car's two roads, a head unit's press that lands
//     while a pointer gesture holds the glass — no key can, the touch window
//     having one owner, architect 2026-10-30), read_only_act_card below
//     (the read-only lock, its one producer the car's undo / redo road
//     on a locked tab — the roster's refused chords wear the grey) and
//     kIterationLockCard (the iteration lock, reached by the lit walk keys'
//     holds, the two marches Ctrl+Tab and Ctrl+Shift+Tab, architect
//     2026-11-24, and the car); the lock's own composer
//     authoring_lock_card; kIterationLockUndoCard / kIterationLockRedoCard
//     (app_state.h). A BOUND CHORD UNDER ANY MODAL IS
//     SILENT (architect 2026-10-25: "chords are used with the laptop, and
//     I'm not using the laptop for authoring … silence") — the prompt, the
//     progress modal and every editor, the editor text drag with them. The
//     `h` view raises no gate card (architect 2026-10-30): every chord its
//     allowlist refuses greys its button, so no refused chord arrives. NONE
//     of them names a key (architect 2026-10-30): the read-only card names
//     the ACT, by the roster's own word for it. A GATE'S MEMBERSHIP IS THE CHORD'S ALONE
//     (architect 2026-09-01: "look for instances where it lies by saying it's
//     not allowed when, in fact, it's only contextually not allowed") — a
//     chord the state owns is admitted and its ACT answers the contextual
//     refusal with the true reason. A card naming the act refused says what
//     happened, never what to press instead — it is not a gesture hint. (A
//     card, a label or a tooltip never names a key (architect 2026-10-30,
//     redesign_button_tooltip's head, app_state.h); the one key text in the
//     product is the on-screen keyboard's own caps.)
//   * THE VERBS' OWN REFUSALS, naming a subject or a view, never a chord: the
//     home-view binding ("Markers are placed in source view", "Markers are
//     moved in source view", Shift+S's "Already in phase reset view",
//     2026-11-10); the warp drop's occupied place (kMarkerDropOccupiedCard
//     below, 2026-11-11); the
//     subject refusals ("Select a marker to …", "Select a warp marker to …");
//     the column ("Phase resets carry no tempo to inherit"); the value facts
//     (the GROUP step's "One of the selected markers cannot take this tempo
//     change", the singleton's "That marker coincides with another" and
//     "A label reference has no tempo of its own"); the clipboard chords'
//     (kSelectOneRun and its siblings, input_key_dispatch.cpp); the value
//     pair's (payload_eligibility — Ctrl+Shift+J and Ctrl+J); undo and redo's
//     ("There is nothing to undo" / "…redo", "That step belongs to the other
//     tab, which is read-only"); the `h` view's
//     kHistoryViewMovesNoMarkersCard; the off-edge pair below; bare `m`'s
//     NINE-arm BPM gate, one card per press carrying the first failure (the
//     tenth arm, enter_bpm_mode's own bail, is unreachable and silent).
//   * THE MODES: kCheckpointPublishing (below — bare `h`, the publish
//     chord, the save owner, the picker, Revert); "History
//     is unavailable[: <reason>]" (the press that asks for git where the
//     remote walk could not bootstrap — bare `g` at set_history_delta and
//     the main page's publish chord, SAVE's long press, at
//     run_publish_chord — plus the failed-scan arrival; the entry opens on
//     the local walk and raises none); playback's kPlaybackDeviceUnavailableCard
//     (playback_lifecycle.h — the one launch body, the two pre-launch gates
//     and the A/B audition's own tick) and the audition's "One of the two
//     tabs has nothing to play from here"; the marches' "Tab B has no next
//     marker" / "Tab A has no previous marker" (GuiInputHandler::
//     refuse_march, architect 2026-11-24: on the main page a march runs only
//     where both tabs have a stop that way, and names the tab that has none;
//     the `h` view runs no march and its holds raise nothing);
//     kNoRenderRunningCard (the painted
//     Cancel with no render running); the render player's (GuiRenderPlayer::
//     status and its load act's `refuse`, render_player.cpp /
//     input_key_dispatch.cpp: "Nothing to play: no renders under tmp/",
//     "Render player is unavailable while rendering", "A folder carries no
//     recipe to load in place", "Cannot load in place while a render is
//     running", the decode's rate / channel, empty and no-device sentences
//     and the frozen reader's own words, "There is no folder to delete",
//     the row keys' taps with no row lit, "No render is selected" / "No
//     folder is selected" (Delete, and Load in place's stale face), and
//     every `Load in place refused: …`); the trim presets list's Render tap
//     with no row lit, "No preset is selected" (architect 2026-11-27,
//     trim_presets_button_enabled; Delete's stale face too, beside its
//     "There is no preset to delete"); the `h` view's `'` and revert
//     refusals (`Load in place refused: …`, `Revert refused: …`, the
//     duplicate-label grammar's `Revert refused: label '<l>' is already
//     defined at another marker`); the Open project picker's three refusals
//     and Revert's three, the same words for the same reasons.
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
//   * EVERY REFUSED FIELD'S REASON (2026-08-30): the flag-editor cluster's
//     "Edit rejected: …", "Range bound rejected: …", the flag editor's
//     extra keys' "The flag is not well formed: …" / "The bound is not well
//     formed: …" (UP, DOWN and the pass key on a text the commit's parse
//     refuses, architect 2026-11-20 / 21; their kind refusals the
//     main page's step's own sentences, GuiFlagEditor::step_editor_text),
//     and the BPM form's
//     "BPM edit rejected: …" — its line's three (the row's key, the beats
//     grammar, the low / high grammar: commit_bpm_field) and RENDER's two
//     bracket walls (bpm_sweep_bracket_admits), flag_editor.cpp; the settings editor's
//     four "Settings edit rejected: …" composers (settings_editor.cpp:
//     commit_gui_setting's, apply_device_setting's, and the engine arm's
//     invalid value and source-clobber guard); the text editor's two capacity refusals, reported by text_editor.cpp and
//     carded by the dispatch ("The pasted text is too long for this field",
//     "This field is full" at route_modal_editor_key — the settings recall's
//     own replace_selection call staying silent, that text being the
//     product's). The card drops the offending text, which stands on screen
//     in the refused field as it was (text_editor::refuse, architect
//     2026-11-27; the field changes no colour, architect 2026-10-03, so the
//     card is the refusal's one statement).
//   * THE PROPAGATE REPORTS: the phase-reset pastes' "Stopped at …",
//     kNothingMatched (propagate_blocks.h, two readers) and the two
//     "nothing changed" sentences (architect 2026-11-18, "E. Agree."):
//     kPhaseResetStateAlreadyMatches, PASTE's hold that compared resets and
//     flipped none, and kPhaseResetsAlreadyMatch, the tap's byte-equal paste
//     whose blocks held resets (its wording the planner's, pending his
//     word) — blocks that held no reset on either side say nothing; a paste
//     that wrote no
//     block skips the switch to target view,
//     the card being the whole answer, while a paste that paired blocks
//     lands even byte-equal.
//   * THE CLIPBOARD WRITES, the one class of success that cards (nothing
//     paints a clipboard): "Copied the resolved value '<payload>'" (Ctrl+Shift+J)
//     and "Copied the selected markers' phase resets" (bare P); and the
//     phase copy that captured nothing, "No labeled, enabled markers are
//     selected, so nothing was copied". No clipboard write refuses (the copy
//     stays in the program's process, platform_android.h's
//     clipboard_set_text), so no refusal cards. THE EDITORS' OWN Ctrl+C
//     and Ctrl+V are silent: an editor is its own world with that world's
//     conventions.
//   * THE GITHUB CARDS (2026-09-27), the publish chord, Ctrl+Shift+S, in
//     either view (SAVE's long press on the main page, Save and Commit; the
//     `h` view's Save key, Force Save and Commit) forking on the status
//     (run_publish_chord), each the KEY'S — in the `h` view the Save face
//     greys on the same status, so there these answer a status that moved
//     inside the painted frame, while on the main page the face is the
//     dirty mark alone: "GitHub is still being checked", "GitHub
//     refused this device", "This device and GitHub have both moved" (stderr
//     names the fix), "GitHub has not been checked"; the pull's synchronous
//     refusals "Pull refused: this device has moved", "Pull refused:
//     GitHub's checkpoint of this piece would not load" and "Pull failed:
//     nothing was changed"; and the checkpoint's clean ending, "Nothing to
//     commit" (architect 2026-11-17: the worker found the sidecars
//     clean and nothing owed, on_history_checkpoint_complete). UNDER
//     `offline` THE KEY ASKS GITHUB AGAIN (architect 2026-09-28) and cards
//     nothing in the view, the status bar's walk line carrying the answer,
//     and "Asking GitHub again" on the main page, whose status bar carries
//     no GitHub word.
//     The GitHub CHECK itself raises nothing — its failing readings print one
//     stderr line each and the status word is state.
//
// CRITICAL (STANDING) — never TIMED OUT and never BUMPED (architect
// 2026-08-30: "critical cards keep standing"): no clock, and a later success
// clears nothing. Down only by a DELIBERATE dismissal: a click or tap on it,
// or the whole stack's dismissal (a long press on any card) — THE ONE
// EXCEPTION the struck card below, which also clears by itself. THE
// PRODUCERS, and nothing else (re-greped 2026-11-19): the three
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
// which); and THE STRUCK CARD below.
//
// THE STRUCK CARD, STATE WORN AS A CARD (architect 2026-11-19: "I
// lean towards giving them a red card. There's never a reason to leave a
// struct marker, so the red card is the permanent one ... the card could be
// cleared if the user undoes it or deletes the struct marker. That way the
// user is alerted immediately, not by a little status bar that they may or
// may not look at"; its words and its return "Agree"): ONE STANDING CARD while
// any warp marker of the open piece is STRUCK BY COINCIDENCE — a member of a
// coincident run, the parser's verdict (warp_coincident_members, read through
// warp_red_flag_set_cached's `collapsed`, warp_frame_map_view.h; disabled rows
// counted). The label-reference strikes (`red`'s other members) are no
// coincidence and raise nothing.
// THE STRUCK CARD SAYS WHERE (architect 2026-11-19: "The red
// coincide card should say where the coincident markers are"; put back as
// "'… coincide at <time>', up to three places, then 'and N more'": "Number
// six. Agree."). ITS WORDS, one clause, no key named, no period (the cards'
// style):
//     "2 warp markers coincide at 00:12.345"
//     "4 warp markers coincide at 00:12.345 and 01:23.456"
//     "6 warp markers coincide at 00:12.345, 01:23.456 and 02:05.000"
//     "10 warp markers coincide at 00:12.345, 01:23.456, 02:05.000 and 2 more"
//   * THE COUNT: the members' count (a run has two or more, so it is 2 or
//     more and the plural always holds);
//   * THE PLACES: ONE A COINCIDENT RUN, the run's FIRST member's frame, in
//     the music's order (warp_red_flag_set_cached's coincident_run_firsts,
//     grouped in the verdict's own pass by the classifier's own pair test,
//     so a run here is the parser's run); the first three named, ", "
//     between them and " and " before the last; past three, " and K more",
//     K the runs not named;
//   * EACH PLACE AS THE VIEW ON SCREEN READS IT, BARE
//     (format_active_domain_time, phase_reset_propagate.h — the one body the
//     propagate family's labeled register is built on): in source view the
//     source time, in target view the frame's image under the live target
//     map; MM:SS.mmm at the nearest millisecond, the register's own spelling
//     (format_timestamp). No " source time" / " target time" tail: the card
//     follows the view, and the ruler beside it says which (the bare place
//     and its spelling architect 2026-11-20: "agreed").
//     Two runs' first members stand at least one millisecond_frames
//     apart in source frames — 44 frames at 44.1 kHz, 0.998 ms, 48 at 48
//     kHz, exactly one — so two places CAN spell alike at the millisecond:
//     two first members that close can round into one millisecond (the
//     bin's width at 44.1 kHz, a tie rounded to even at 48), and in target
//     view a fast tempo can pack two runs' images into one. Each is what the
//     view's time reads there at that precision, and it stands.
// ITS ONE DERIVATION is GuiNotifications::sync_struck_card, on the run
// loop's tick (main.cpp, beside the cards' clock), from the warp store as it
// stands, ON ONE KEY — everything the words read: the warp store's
// generation (the members, the runs, their frames); the audio view
// (active_audio_view: which domain the places read in); in target view the
// tempo scale (engine_settings.scale, read as it stands whatever act wrote
// it — with the store, the live target map's inputs,
// target_view_warp_frame_map_cached); and the loaded audio's rate and
// length (the coincidence's millisecond and the map's envelope). NOT THE
// TAB (both tabs share the store, the scale and the view) and NOT THE TRIM
// (the target view's map is the whole song's, trim being render-time). A
// tick whose key has not moved answers with six compares:
//   * RAISED the moment the count leaves zero after AN EDIT — any change of
//     the authored warp markers (their `.warpmarkers` bytes,
//     format_warpmarkers_text: a drop, a drag, a nudge, a paste, an undo or
//     redo, a load in place or a revert) — AND AT THE PROJECT'S OPEN, the
//     loaded piece's first look counting as one;
//   * RE-WORDED IN PLACE while it stands, its count and its places following
//     the store, and its places the view's reading of them (an S/T flip, a
//     scale change in target view) — the same card, its id and its seat
//     kept; never a second card. A VIEW FLIP OR A MAP CHANGE IS NO EDIT: it
//     re-words a standing card and never brings back a tapped-away one;
//   * CLEARED BY ITSELF the moment no warp marker coincides — the one
//     critical card a state takes down (it says what is true now, so a
//     state that stopped being true leaves no message about the past);
//   * TAPPED AWAY (the lift on it, or the whole stack's dismissal) it STAYS
//     AWAY until THE NEXT EDIT AFTER WHICH THE COUNT IS NON-ZERO, or the
//     next project's open (a fresh AppState), and then stands again.
// It follows THE PIECE, not a tab: both tabs read the one warp store. THE
// HISTORY VIEW authors nothing and leaves the live store as it stands, so
// the card stands or not as the live piece is; the diff's own struck items
// are a commit's, not the piece's, and raise nothing.
//
// WHAT IS NOT A CARD, by ruling — these and no others:
//   * A BENIGN REFUSAL OF A ONE-DIMENSIONAL COMMAND ALREADY AT ITS STATE
//     (architect 2026-08-31): a refusal cards UNLESS it alters no output and
//     refuses a command that moves ONE THING IN ONE PLACE, where one glance
//     at that place answers whether anything happened — the Home / End jumps
//     (both forms and the `h` view's), the marker walk at an empty store or a
//     wall, the singleton tempo step at its bracket's end, Space with nothing
//     left to play, the history walk's walls and the diff cycle's ends,
//     Reset Trim over a full window, the picker's row on the project already
//     open, the render player's folder-end skips, Backspace at the root and
//     its idle family (no render loaded, seeking before playback), the
//     position nudge at its wall in both columns, a clean session's plain Ctrl+S
//     (architect 2026-10-05, GuiSaveOps::save_from_key) and its Save and
//     Commit on the main page (architect 2026-11-24, run_publish_chord),
//     and a held
//     Ctrl+Z / Ctrl+Shift+Z whose repeat runs out of history. THE COUNTER-CLASS IS THE TEST: a command whose effect
//     would have spread across the screen keeps its card even where its
//     button greys, "because they can be subtle and required me to check the
//     whole screen" — undo / redo over an empty stack on a deliberate press,
//     the GROUP tempo step, Shift+S in the P column, the propagate reports.
//     The act is unchanged where its sentence went; each site carries a
//     one-line comment naming this rule, and a reverse is per item.
//   * A BOUND KEY'S REFUSAL WHOSE REASON THE STATUS BAR IS ALREADY SAYING
//     (architect 2026-09-04): the target preview's two gates, Space's play edge and the
//     waveform scrub's launch, silent under the `Updating...` process line
//     with the Play face greyed. It is the ONE RECORDED EXCEPTION to the
//     deduction below: a silent press in target view under `Updating...` is
//     this refusal, not an unbound chord.
//   * A SUCCESS WHOSE RESULT IS ON SCREEN (architect 2026-08-31): a success
//     cards only where nothing shows (the clipboard writes above) or where
//     what shows would MISLEAD (kTrimFallbackCard) — and in the second case
//     the misleading thing is fixed where it can be. So: a render's
//     completion ("that would get annoying") and a cancelled one — a
//     complete BATCH's answer is the render player opening on its folder,
//     a cancelled one's the progress modal closing (2026-10-14); THE SAVE,
//     the dirty mark going out being its answer (architect 2026-08-30: "the
//     disc writes — there is something that paints"), the Save button
//     greying (2026-10-05, plain_save_actionable); a clean
//     propagate walk that changed the store, its landing selecting what
//     changed (a clean paste that changed nothing cards, above); a
//     landed checkpoint or pull; and the crossed-trim reset
//     (auto_clear_crossed_trim — the endcaps snapping to the song's edges is
//     the cue, and a full window changes no output).
//   * A CHORD THIS PRODUCT BINDS NOWHERE IN THE STANDING MODE, wherever it is
//     pressed and whatever swallowed it (architect 2026-08-30, THE DEDUCTION
//     RULE: "bound keys either show an effect or a card, so an unbound key is
//     identified by its silence"); the `h` view's seven shapes are bound only
//     while the view stands (2026-09-01), the render player's and the picker's
//     keys only inside their routers. THE UNBOUND POINTER PRESS goes with it: a
//     modified press the waveform, the top strip or the folder overlay's
//     band (pad, gaps and rows) binds nothing for says nothing, and the
//     render player's modified press on the scrub track likewise.
//   * THE T+W POINTER AUTHORING PAIR — the FLAG DRAG and the EMPTY LANE'S
//     DOUBLE-TAP DROP (the warp column's alone), because a gesture that
//     never begins is its own answer; the double-tap drop's LOCK arm and the
//     flag's double-tap open's refusals are silent on the same ground. Their
//     keyboard twins card.
//   * A GREYED BUTTON'S LIFT: the grey IS the message,
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
//     git is what cards); and what is TRUE NOW — the two state strings
//     (the struck warp markers the one state that cards, above).
//
// THE WORDS ON A CARD (the product's text rules are stated once at
// paint_handler.cpp's text-rules block; a card is a DESCRIPTION, sentence case):
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
//   * A CARD NEVER NAMES A KEY (architect 2026-10-30): a refused act is named
//     by the roster's own word for it (read_only_act_card below).
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
// the wav" card) but the loading/blank guard's, which nothing raises a card
// ahead of (the open's hold drops every press until the load, platform.h's
// head). It reads monotonic_ms() — the one clock every software
// deadline in the product is stamped on — and nothing here schedules anything.
//
// THE HIT (architect 2026-10-01, "whole card dismisses, X gone"): THE CARD
// IS ONE BUTTON,
// the chrome's own model. A press anywhere on it ARMS it
// (AppState::ChromePress's Card kind) and dismisses nothing; THE LIFT inside
// the same card dismisses it; a lift outside the card it pressed dismisses
// nothing (the chrome's slide-away cancel). A LONG PRESS
// DISMISSES EVERY CARD, criticals included — THE CHROME SHIFT LONG PRESS,
// one term at the lift: a hold past chrome_shift_hold_ms(), with no visual
// announcement (finish_notification_release, input_pointer.cpp). THE
// REASON: the pen was ticking the stack down card by card. It is THE ONE
// WHOLE-STACK DISMISSAL (architect 2026-10-30). The card wears no
// hover face and no pressed face. ONE RULE FOR FINGER AND PEN:
// the router cannot fork on tap versus click (no origin bit rides a press;
// GuiInputState carries modifiers alone).
//
// THE DISMISSAL IS THE LIFT'S — the ONE STATED EXCEPTION to "every dismissal
// stays at the press" (AppState::ChromePress's head): a card is an EVENT the
// user closes, never the side effect of a press aimed elsewhere — its
// dismissal is the press's own act, and an act lives at the lift, the only
// place a long press can be known. A HELD CARD'S CLOCK IS SUSPENDED (architect
// 2026-10-01: "holding turns off the timer, releasing re-enables"): from the
// press to the lift the pressed card's remaining life sits in the HOVER'S ONE
// BANK, the held press being a second reason the same card stands still and
// never a second mechanism (GuiNotifications::press_hold_edge); a slide-away
// lift re-arms it, and a card already due at the press is not banked (the
// hover's rule: a pause does not resurrect). A held card can still be BUMPED by
// a push; the lift re-asks the live stack and then lands nothing.
//
// THE WHOLE-STACK DISMISSAL READS NO CLASS — a critical card is dismissed
// like any other — so what takes a critical card down is always a
// deliberate act, never the CLOCK (a critical card has none). That asymmetry
// is deliberate and is the record a reversal would start from: if the
// criticals should survive the whole-stack dismissal, dismiss_all is the one
// line to change.
//
// THE PRESS IS CONSUMED WHOLE, whatever the button: it
// arms nothing else, moves nothing, lands no playhead and reaches nothing
// underneath (a non-left press is consumed and arms no card). The claim
// ranks ABOVE EVERY VEIL (the prompt's, the progress modal's, the player's,
// the picker's, the editors'), and THE RELEASE IS TAKEN AT THE SAME RANK,
// above every veil's — because a card is not a reach into the veiled surface:
// it is the message about the act the veil stands over, and it must be
// dismissable under any of them.
//
// THE GEOMETRY A CARD IS HIT BY IS THE GEOMETRY IT WAS PAINTED WITH: the
// painter publishes each visible card's rect into
// AppState::Notifications::painted, and the router and the
// hover walk read that publication and then ask the live stack whether the
// id still stands (published geometry may only SELECT; live state decides).
// The owners below are pure functions of the window and the scale — the
// stack's ROOM and the ONE-LINE card height — and neither knows a sentence:
// a card's real height needs its line count, the line count needs a shaped
// run, and shaping needs the paint's own font, so the painter is the only
// place that can answer it — and the damage owner asks the painter (its
// layout pass in a measure form, GuiPaintHandler::
// notification_stack_laid_out; architect 2026-11-16, the beats), the room
// staying the painter's clip.
//
// NO DOUBLE SPACE, ANYWHERE THE PRODUCT WRITES TEXT: prompt text and its
// options join with single spaces, and no card, prompt or stderr line carries
// two spaces in a row.

#include "app_state.h"
#include "viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

class GuiAudio;

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
// (an engine key is the piece, and the lock governs the keys, not the
// surface (2026-09-04), so the commit owes the answer itself — the account
// is at GuiSettingsEditor::open), the render player's Load in place, and
// the `h` view's bare `v`, whose admission composes the subject with this
// same lock. THE FIRST TWO READ IT THROUGH authoring_lock_card BELOW — the
// lock's second reason is grid iterations, and those two acts refuse under
// both — while the
// `h` view's fork keeps the literal, the view being unreachable under the
// second reason. THE LOCK GATE IS
// NOT A READER: it says "<Act> is not available on a read-only tab" through
// the composer below instead, naming what was pressed, which is what a user
// who just pressed it is looking for. Its predicate is the
// complement of an allowlist and so drops an unbound chord with a bound
// authoring one, which is why that gate asks chord_is_bound first and answers
// the unbound half with silence. The full reasoning is at that gate
// (input_handler.cpp).
inline constexpr const char* kTabReadOnlyCard = "This tab is read-only";

// THE PASTE'S OVER-CAPACITY SENTENCE (architect 2026-08-30, the head's
// refused-field list): the one incoming filter refused a paste that would
// grow the field past its byte cap (text_editor::replace_selection's false).
// TWO READERS, one sentence: the editors' paste
// (GuiInputHandler::apply_editor_clipboard) and the label paste, the EDIT
// key's hold (GuiFlagEditor::paste_payload_onto_selection).
inline constexpr const char* kPasteTooLongCard =
    "The pasted text is too long for this field";

// THE SAME LOCK'S SENTENCE WITH THE ACT IN IT, composed once — "<Act> is
// not available on a read-only tab" (architect 2026-10-30: a card never
// names a key, so the refused press is named by its ACT, the roster's own
// word for it, redesign_button_tooltip's line 1 — "Undo is not available on
// a read-only tab"; the reasoning for naming the press rather than the tab is
// at the gate, input_handler.cpp).
//
// ONE CALLER, the lock's verdict-and-card (authoring_lock_refuses_chord), and
// ONE PRODUCER, the car's undo / redo road on a locked tab — on_key's road
// never meets the sentence, the roster's refused chords greying on the same
// lock — whose subject is the ACTIVE tab's bit (architect 2026-10-13: the
// lamp's exclusion is the active tab's like every other read-only refusal,
// authoring_locked, app_state.h).
//
// THE TAIL IS A CONSTANT IN app_state.h (kReadOnlyActCardSuffix), this header
// including app_state.h and not the reverse.
inline std::string read_only_act_card(std::string_view act) {
    return std::string(act) + kReadOnlyActCardSuffix;
}

// THE ITERATION LOCK'S SENTENCE IS kIterationLockCard AND IT LIVES IN
// app_state.h, beside the sweep's verdict cards: this header includes
// app_state.h and not the reverse, so the literal is reachable where it
// stands. It is the read-only sentence's sibling all the same, and the
// composer below is where the two meet.

// THE ONE COMPOSER THE LOCK'S CARD FORKS AT, and READ-ONLY RANKS FIRST: the
// two locks compose (2026-10-13; authoring_locked, app_state.h — the
// lamp lit on a writable tab, a switch under it onto a locked one), and
// there the active tab's lock is the reason the user can fix where he
// stands, so its sentence is the one said.
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
// because the save owner raises it from its own translation unit. ONE FACT, ONE
// WORDING, and its readers are: the SAVE OWNER's in-flight refusal
// (GuiSaveOps::save, save_ops.cpp), which every plain Ctrl+S road reaches —
// on_key, the editors' modal contract, the picker's router, the render player's
// fall-through — and the close prompt's Save answer; bare `h`'s entry refusal
// (which also prints it on stderr); and the Open project picker's open act and
// the Revert act (revert_project); and the commit acts' own fork
// (run_publish_chord), the publish chord, reached from the main page's SAVE
// hold (no `h` view stands while one publishes). All of them meet
// AppState::history_checkpoint_in_flight.
// ONE CLAUSE (architect 2026-09-01, the capitalization sweep's sentence
// shape): the sentence is the instruction.
inline constexpr const char* kCheckpointPublishing =
    "Wait for the checkpoint to finish publishing";

// THE PROJECTS PATH COMMIT'S SENTENCE (2026-09-02): a `projects_path=` commit
// from the settings editor rewrites the device config and changes nothing on
// screen — the open project stays open on its absolute paths — while Open
// project and the next launch read the new folder at once (the whole
// account is at GuiSettingsEditor::apply_device_setting,
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
// untrimmed piece (the window's purpose is at TrimState, app_state.h). The trim bar
// goes on painting the hairline window the user drew, so the
// screen says "this span" while the audio is the whole movement — the shape
// the strictness ruling's "what shows would mislead" test cards. A MINIMUM
// TRIM SIZE IS REJECTED (2026-08-19, write_trim_from_sweep's record: a floor
// quantized the sweep unpleasantly) — the sweep authors
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
// either column and Shift+S's lead-in drop; the empty lane's double tap,
// through the same bodies, meets it only at the right wall's last half px,
// create_marker_at_empty_lane), and by
// Shift+S's group drop where it refuses whole, the edge having skipped every
// place (GuiInputHandler::drop_phase_reset_crossing, 2026-11-10). "At this
// zoom" is the sentence's truth: zooming in brings the column on screen.
inline constexpr const char* kMarkerNudgeOffEdgeCard =
    "The marker would move past the edge at this zoom";
inline constexpr const char* kMarkerDropOffEdgeCard =
    "The marker would sit past the edge at this zoom";

// THE OCCUPIED PLACE'S SENTENCE (architect 2026-11-11, THE DROP COINCIDENCE
// RULE: "coincident markers are always unintentional"; the test's owner
// warp_marker_in_vicinity_of, warp_frame_map_view.h, over the parser's
// warp_frames_coincide): a warp marker, a disabled one included, already
// coincides with the playhead's frame — the same frame or under one
// millisecond (architect 2026-11-17). ONE RAISER: the W column's drop body
// through its reason channel
// (GuiWarpMarkersOps::drop_copy_previous_at_playhead). THE PHASE-RESET
// COLUMN HAS NO OCCUPIED PLACE (architect 2026-11-17: "let's do
// anything-goes for phase"), so it has no sentence. Where both this and the
// painted edge would refuse a drop, this is the sentence: no zoom frees an
// occupied place. The Drop key greys where neither of its forms would act
// (redesign_button_enabled), so a lift reaches this only while its other
// form is live.
inline constexpr const char* kMarkerDropOccupiedCard =
    "A marker already stands here";

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
// statement of the product's text rules at paint_handler.cpp's text-rules
// block). A sentence composed as "<Act> refused: <reason>" is ONE sentence, so
// its tail does not start a second one; a producer whose string is ever used
// WHOLE is a sentence in its own right and capitalizes at that producer.
//
// ITS FIRST FAMILY IS THE ONE THAT IS BOTH — the past-EOF wall
// defects (first_past_eof_wall_defect, src/parser/marker_store_validate.cpp),
// which are used WHOLE by the picker's dry-run card and by the CLI's stderr
// and APPENDED by four GUI seams: "Revert refused: ", the render entry load's
// own `refuse`, the `h` view's "Load in place refused: " and the loader's
// "Source load aborted: ". The producer is under the parser's
// PERMANENT HARD FREEZE, so the case moves at the APPENDING seams instead of
// at the producer, which is also what keeps the two whole-message consumers
// right without a second edit.
//
// SIX CALLERS ACROSS THREE PRODUCER FAMILIES (re-grep `lowercase_initial(`
// before restating it): beside the wall defects' four, the loader's
// sidecar-set refusal (file_loader.cpp, "Source load aborted: " over
// sidecar_set_presence's diagnostic) and the FAILED RENDER'S CARD
// (input_render_dispatch.cpp), which appends `GuiFailure`'s display clause
// after "Render failed: ". Those two families compose their own text and
// take the helper for the same reason, an appended reason is lowercase, and
// for no other.
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
// text-rules block): the first prose word after the program-name prefix
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

// THE CARD'S WIDTH IS ITS CONTENT'S, UNDER THE CEILING BELOW AND NO FLOOR
// (architect 2026-11-19: "agree" — each card its text's width, notifyd's
// way): the box is the chrome plus the
// whole sentence's shaped width, ceiled to the whole px that contains it
// (paint_notifications), clamped only by notification_card_max_w_px. Two
// sentences of different widths are two widths of card ("There is nothing to
// undo" and "…to redo" a few px apart included).
//
// THE CARD'S CEILING, AUTHORED AND SCALED (architect 2026-08-31): a
// CANONICAL width — the same card at every window and, through `scaled_px`,
// the same card in millimetres at every gui_scale; a fraction of the window
// would be DEVICE PIXELS and starve the tablet, three words to a line. IT
// MAY COVER BUTTONS ON THE TABLET and that is accepted in his own words —
// "it's okay if it covers up some buttons": a card is a sentence to read and
// it leaves on its own. A length, so it scales; the life beside it
// (kNotificationMs) is a duration and does not. 698 U (architect
// 2026-11-05: 697.5 RULED TO 698, a painted length being a whole U), 1396
// device px on the tablet.
inline constexpr double kNotificationMaxWidthPx = 698.0;

// THE GAP BETWEEN TWO CARDS IN THE STACK: kNotificationGapU, 6 U
// (notifyd's stack, measured; architect 2026-11-19), 12 device px
// on the tablet, scaled_px floored at one so the cards never touch; read by
// the painter (paint_notifications) and the room's count
// (notification_capacity) and by nothing else.
inline constexpr double kNotificationGapU = 6.0;
int notification_gap_px();

// HOW MANY LINES A SENTENCE MAY TAKE (architect 2026-08-30, "a few"): a card
// whose sentence does not fit its room GROWS DOWNWARD to this many lines and
// no further — the last permitted line takes the WHOLE remainder as one run
// and clips at the right edge, so no word is ever silently dropped, only cut
// where the eye can see the cut. A COUNT, not a length: it does not scale.
inline constexpr int kNotificationMaxLines = 3;

// -- Geometry the painter, the damage owner and the hit share ---------------

// NOTIFYD'S BUBBLE'S LENGTHS (architect 2026-11-19; THE PADDING notifyd's
// ONE NUMBER, the same day: "the padding
// around the font seems inconsistent - just ask remote to make sure it's
// arc's canonical padding" — xfce4-notifyd's `#define DEFAULT_PADDING 14.0`,
// xfce-notify-window.c, the content box's border on all four sides AND the
// icon's margin_end, the summary and body packed with no margin), each a
// whole U through scaled_px, rounded at the element:
//   THE PADDING — kNotificationPadU, 14 U, USED FOUR WAYS AND ONLY HERE, so
//     no site can disagree: the box's edge to the content above, below, at
//     the left of the glyph's box and at the right of the text's room — AND
//     the glyph's box to the text's pen (notifyd's icon margin_end);
//   THE GLYPH'S DRAWING BOX — 16 U square, OURS (notifyd's icon is 48 px;
//     ours is the 16 U drawing the lists and keys wear, the class drawing's
//     whole canvas scaled into it, icons.h's THE BOX), its top on the top
//     pad (+14 U) on every card;
//   THE CONTENT'S HEIGHT — THE TALLER OF THE GLYPH'S BOX AND THE TEXT, as
//     GTK's row box sizes it: one line (14 U) is shorter than the glyph, so
//     a one-line card is 14 + 16 + 14 = 44 U (88 device rows on the tablet)
//     and its line box stands CENTERED ON THE GLYPH'S ROWS (+15 U, the
//     baseline +26 U); two or three lines (28 / 42 U) are taller, so the
//     card is 14 + the lines + 14 U, the text from +14 U (the first baseline
//     +25 U) and the glyph's box kept at +14 U (the text grows downward
//     beside it, nothing reflows);
//   THE RADIUS — 2 U at each corner (Arc's);
//   THE SEAT — notifyd's `#define SPACE 16`, off the screen's top and right
//     (his panel is at the bottom; measured on his desktop): the stack's
//     first card's top 16 U under the window's first row and every card's
//     right edge 16 U inside the window's last column
//     (notification_stack_bound).
inline constexpr double kNotificationPadU       = 14.0;
inline constexpr double kNotificationGlyphBoxU  = 16.0;
inline constexpr double kNotificationRadiusU    = 2.0;
inline constexpr double kNotificationSeatU      = 16.0;

// THE CARD'S LINE PITCH, device px: the body face's LINE BOX (the line-box
// rule, gui_font_line_box_u — 11 + 3 = 14 U at Regular 12 U), whole: a
// line box stacked on a line box, Pango's lines in notifyd's one label, 28
// device px on the tablet.
int notification_line_px();

// A card of `lines` lines' height (the padding's block above): the padding,
// the taller of the glyph's box and the lines' line boxes, the padding —
// each part its own rounded U. On the tablet one line 28 + 32 + 28 = 88
// device rows (44 U), two 28 + 56 + 28 = 112 (56 U), three 140 (70 U). THE
// ONE-LINE CARD (the default) is what the room's count reads
// (notification_capacity); a card's line count needs a shaped run, which
// only the painter has, so its real height is the painter's to ask.
int notification_card_h_px(int lines = 1);

// THE TEXT'S TOP in a card of `lines` lines, device px under the card's top:
// the padding, plus — where the lines are shorter than the glyph's box (one
// line) — half the difference in whole U, GTK's centering of the label on
// the row box: +15 U for one line (30 device rows on the tablet), +14 U for
// more. The first baseline is this plus the line box's ascent (11 U).
int notification_text_top_px(int lines);

// The card's largest possible width at this window and scale: the scaled
// ceiling above, and — where the window cannot even hold that — the window
// itself less the stack's two side seats (kNotificationSeatU each side). THE
// WINDOW TERM IS A SAFETY AND NOT A DESIGN (architect 2026-08-31): the
// ceiling is what the design says a card is wide, and the clamp only keeps a card inside
// a window too narrow for it (the tablet's portrait panel, a contrived
// window). The painter clamps a card's content width under this, and THE
// ROOM BELOW IS THIS SAME NUMBER, so every painted card lies inside the rect
// that clips and publishes it (no floor; never below zero).
int notification_card_max_w_px(const AppState& a);

// THE STACK'S ROOM: the rect the stack may occupy — the maximum card width,
// its right edge kNotificationSeatU (16 U) inside the window's last column,
// from THE CARD SEAT — kNotificationSeatU under the window's first row,
// OVER THE TITLE BAR AND THE TOP ICON ROW, WHATEVER STANDS THERE (architect
// 2026-11-19: "in xfce the card appears at the top right of the
// screen without regard for what it obstructs - it can always be dismissed
// with a tap. consider having ours do that, simply using the xfce
// notification margin"; notifyd's SPACE 16 off the screen's top and right,
// the block above) — DOWN TO THE PANEL'S PAD OF AIR (folder_overlay::pad_px,
// one U) ABOVE THE BOTTOM ROW'S LANE, or above THE ON-SCREEN KEYBOARD'S TOP
// LINE while it stands, whichever is higher (architect 2026-11-20: the
// keyboard stands over the bottom icon row, onscreen_keyboard.h's
// placement block; the cards paint after it, so the room's foot is what
// keeps a card off its keys). On the tablet: rows 32 .. 1307 (the bottom
// icon row's first row 1310 less the 2-row pad; 32 .. 1039 under the
// keyboard, its top line on 1042), the right edge on x 2271. NOTHING ELSE MOVES FOR IT: the title
// bar and the top icon row paint whole under a card, its .95 fill over
// them; a press on a card is the card's (claim_notification_press ranks
// above every claim and veil), so a key or any chrome under it answers
// nothing while it stands there, and no key
// under it starts a tooltip (recompute_redesign_button_hover's under_card).
// WORKED ON THE TABLET (2304 x 1440 at 200 %, the key 28 U tall; the advances
// MEASURED off fonts/LiberationSans-Regular.ttf at 24 px the product's way —
// unhinted hmtx advances and the GPOS `kern` pairs under the digits' rule,
// each to 1/64 px, which
// gives "2 warp markers coincide" its recorded 260.1 — the boxes
// DERIVED from them by the painter's arithmetic): THE STRUCK CARD of one
// run, "2 warp markers coincide at 00:12.345" — its sentence's advance 400.2
// device px under 116 px of chrome (28 + 32 + 28 + 28) — is ceil(516.2) =
// 517 wide, x 1755 .. 2271 and rows 32 .. 119 (88): its glyph's box x
// 1783 .. 1814, rows 60 .. 91; its pen x 1843, its line box rows 62 .. 89,
// the baseline on row 84; over the title bar's rows 32 .. 55 and the top
// icon row's 56 .. 119 (the keys' faces 70 .. 125 covered to row 119). Every
// digit has one advance (13.34 px; the colon, the point and the comma 6.67)
// and no digit kerns against a digit (text_shape.h's THE DIGITS' RULE), so
// a place spells 106.75 px whatever it reads: the card's width moves only
// with the number of places named and the digits of the two counts, never
// with a place's own value. THE LONGEST FORM, three
// places and the rest, "10 warp markers coincide at 00:12.345, 01:23.456,
// 02:05.000 and 2 more", advances 781.8 px: 898 wide (449 U), x 1374 ..
// 2271, against the ceiling's 1396 (698 U) — a sentence's room of 1280 px,
// which no count a piece can hold reaches (each further digit of a count
// 13.34 px), so THE STRUCK CARD NEVER WRAPS; its wrap, its seat (16 U off
// the window's top and right) and its padding the cards' own, untouched.
// (Two places, "4 … at 00:12.345 and 01:23.456", 560.3 px: 677 wide;
// three, "6 … and 02:05.000", 680.4 px: 797 wide.) A SECOND ONE-LINE CARD
// under it stands 12 rows lower, rows 132 .. 219 (glyph 160 .. 191,
// baseline 184), over the icon row's foot pad, the trim lane (140 .. 167),
// the ruler lane (168 .. 201) and the marker lane's first rows (202 .. 219);
// a third, rows 232 .. 319, is the first to reach the canvas (from row 236).
// THE SEAT READS THE WINDOW'S EDGES, NO LANE (architect 2026-11-19:
// notifyd seats a card off the screen's corner "without regard for what
// it obstructs"): the top and the right are notifyd's SPACE in from the
// window's first row and last column, so no lane's height or relayout moves
// the stack; the foot alone reads a lane, the bottom icon row's.
//
// IT IS THE ROOM, NOT A TIGHT BOUND (2026-08-30, with the wrap): no pure
// function of the window can know a card's line count — that needs a shaped
// run. So the answer is the whole space the stack has to grow into,
// and the painter CLIPS to it: a stack that outgrows the room paints on down
// and is cut at the room's foot, so no card ever paints over the bottom row,
// and the painter publishes its rects CLIPPED TO THE ROOM TOO, so nothing
// under that foot is ever claimed by a card. (A stack that tall is the
// contrived case this design declines to cater for.)
//
// A change to the stack damages the cards it changed, not this rect
// (architect 2026-11-16, the beats; Viewport::invalidate_notification_stack:
// the cards as painted against the cards as the painter will lay them out —
// the room is that owner's answer only where it cannot ask the painter).
// The painted cards lie inside the room by construction. A window with no
// room between the card seat and the bottom row answers a zero height and
// paints nothing — in the contrived class.
GuiRect notification_stack_bound(const AppState& a);

// HOW MANY CARDS THE ROOM HOLDS, and so what a push bumps past (architect
// 2026-08-30, the uncapped stack): the number of ONE-LINE cards that fit the
// room above, `floor((room + gap) / (card + gap))`, at least one.
//
// IT IS A COUNT OF ONE-LINE CARDS AND NOT A PIXEL BUDGET, deliberately: no
// pure function of the window can know a card's line count — that needs a
// shaped run, and the model layer this serves has no font to shape with (the
// same reason the damage owner asks the painter for the cards' rects). So
// the cap counts the cards the room WOULD hold at one line each; a stack of
// wrapped cards can pass the room's foot while inside this count, and the
// painter's clip is what answers that, exactly as it answers a stack of
// criticals that will not be bumped.
//
// A pure function of the window and the scale, like the room. On the tablet
// (the whole 2304 x 1440 panel; the one-line card 88 device rows, the gap
// 12, the room rows 32 .. 1307, 1276 rows) it is 12 (1288 over 100): every
// window this product runs in holds more cards than the architect will ever
// stack.
int notification_capacity(const AppState& a);

// Whether `id` names a card that is IN THE LIVE STACK — which is the same as
// "on screen", no card waiting unseen (2026-08-30): a push makes a card
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
// put there, which is what the user is about to see. No pointer-owning
// surface paints above the cards.
//
// THE READERS ARE THE CARD'S OPACITY, re-greped at this declaration: the
// press claim (claim_notification_press) and its lift
// (finish_notification_release, which asks it for the SAME card at the
// release's own coordinates), the play-key test at the press head
// (on_button_press's audition rule), the card hover walk
// (GuiNotifications::update_hover), the touch pan zone (touch_point_in_pan_zone) and the roster's
// pointer walk (recompute_redesign_button_hover), which answers "nothing
// under the pointer" so no button beneath a card starts a tooltip.
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

    // THE CARD'S LIFT (architect 2026-10-01), and the
    // pointer's alone: remove the named card whatever its class and state,
    // and drop the hover if it was this card's. The card is the one the press
    // armed, re-hit at the lift's coordinates, and the live test below is
    // asked of that argument.
    void dismiss(uint64_t id);

    // THE WHOLE STACK'S DISMISSAL (architect 2026-10-30): the LONG PRESS's
    // lift on any card (finish_notification_release, its one caller) — the
    // whole stack, CRITICALS INCLUDED, and the hover with it. An empty stack is
    // a silent nothing — no damage, no card. The reasoning and the reversal
    // record are at the hit section above.
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

    // THE STRUCK CARD'S ONE DERIVATION (the head's THE STRUCK CARD; architect
    // 2026-11-19), on the run loop's tick once a piece is
    // loaded: when THE WORDS' KEY has moved since the last look (the head's
    // list: the warp store's generation, the audio view, in target view the
    // tempo scale, and the loaded audio's rate and length), the struck count
    // and the runs' places (warp_red_flag_set_cached's `collapsed` and
    // `coincident_run_firsts`, each place spelled by
    // format_active_domain_time) and, when the store moved, whether the
    // authored markers changed (their file bytes against the last look's,
    // the piece's first look counting as a change) — then the card raised,
    // re-worded in place or cleared as the head says. `audio` the loaded
    // piece (the strike's cache key and the places' domain). A tick whose
    // key has not moved asks nothing more than six compares.
    void sync_struck_card(const GuiAudio& audio);

    // The hover walk, from the motion handler and the tick: which card the
    // pointer rests on. Entering a visible normal card banks its remaining
    // life; leaving re-arms it unless a press still holds it. A card whose
    // deadline has ALREADY passed is not banked — hover pauses a clock, it
    // does not resurrect one — so it retires on the next fire_if_due as an
    // unhovered one would. It paints nothing and damages nothing: the card
    // wears no hover face.
    void update_hover(int x, int y);
    // The pointer-left hook's half: no card is hovered, every bank the hover
    // held re-armed (a card a press still holds keeps its bank).
    void clear_hover();

private:
    void set_hover(uint64_t id);
    // THE ONE BANK'S ONE WRITER: re-answer card `id`'s pause from its two
    // reasons — the pointer resting on it (`hovered_id`) and a press holding
    // it (AppState::chrome_press's Card arm). Banks a running normal card's
    // remaining life when the first reason begins, re-arms it from now when
    // the last one ends, and banks nothing for a card already due.
    void rebank(uint64_t id, int64_t now);
    AppState::Notification* find(uint64_t id);
};
