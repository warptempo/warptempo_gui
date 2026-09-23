#pragma once

#include "app_state.h"
#include "audio.h"
#include "gui_media.h"
#include "platform.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "target_render.h"
#include "viewport.h"

#include <cstdint>
#include <string>

struct GuiInputHandler;

// THE CAR DRIVES THE PROJECT TRANSPORT WHILE THE RENDER PLAYER IS CLOSED
// (architect 2026-09-17). The head unit's buttons reach the product over
// Bluetooth as GuiMediaCommand values through main.cpp's hook, and that hook
// FORKS ON THE PLAYER'S MODE BIT — the one partition of the head unit between
// its two owners: with the player standing every command is the player's
// (GuiRenderPlayer::on_media_command, render-player.md's car section), and
// with it closed every command is THIS cluster's. The console's Bluetooth
// face is exactly three buttons — rewind (Previous), play/pause, fast-forward
// (Next) — and his spec for them: the middle button is the play transport
// (PAUSE and PLAYPAUSE, the two kinds a console's own button sends against a
// display that says PLAYING; a bare PLAY starts nothing at all and is the
// audio route's reopen, the arm and its measured reason at the table below),
// PREVIOUS IS UNDO and NEXT IS REDO AND THEN A PLAY (Ctrl+Z and Ctrl+Shift+Z
// whole, 2026-09-17 — the console steps the piece's history and reads the
// position back off its own display — the play tail added 2026-09-18, the
// block "THE SKIPS PLAY WHAT THEY STEPPED TO" below) — under the same dummy-track trick as
// the player (the session says PLAYING always, its clock always in motion),
// with ONE DIFFERENCE from the GUI's own transport: A PLAY STARTED FROM THAT
// BUTTON LOOPS THE TRIM FOREVER, in every view, ALWAYS FROM THE TRIM'S BEGIN
// (the resting playhead is no term of it; a restart is the button itself —
// pause, then play). PAUSE IS THE GUI'S STOP: the
// console's pause runs the one stop body exactly as Space's stop does — no
// pause semantics, no resume point — because the transport is to be as close
// to the GUI's as the car allows, and with the trim narrowed to what he is
// working on he goes back to the beginning anyway. The loop lives in the
// engine (GuiPlayback::play_loop) and the launch in the lifecycle
// (GuiPlaybackLifecycle::car_toggle_playback); this cluster is the table, the
// gate and the publisher.
//
// THE SESSION STANDS FOR THE APP'S LIFE. On Android the MediaSession goes
// ACTIVE at the first tick of the first project and stays active until the
// activity's onDestroy releases it: this cluster's tick publishes with
// `session_active` true always, the player's close no longer pushes STOPPED
// (GuiRenderPlayer::publish_media_state's inactive arm returns without
// pushing), and the player's open is simply the other owner taking the wire
// for a while — this tick hands over while the player stands and takes the
// wire back on the first tick after it comes down. A REOPEN re-publishes the
// new project on its first tick (this object is per project, built inside
// run_project; last_pushed_ starts empty). On Wayland the platform's push is
// a no-op and the hook never fires, so nothing here has an effect there.
//
// THE THREE LINES, with the player closed (architect 2026-09-17): the ALBUM
// (the console's dim top line) is the project's name, the ARTIST is THE TAB
// AND THEN THE VIEW ("A) T+W" — the active A/B tab's letter, a close
// parenthesis and a space, then view_pair_label, the view bar's one speller,
// which carries no tab term of its own), and the TITLE, the big line, is
// WHERE THE SESSION STANDS, SPELLED AS A BATCH CELL'S BASENAME IS —
// "<index>_<distance>", the live state's number in the session walk's counting
// and its distance from the save ("5_+2", "3_+0", "1_-2" —
// car_transport_title_line, the formula and the spelling at its declaration) —
// so a Previous or Next reads back on the console as both numbers stepping
// together. ONE COMPOSER PER LINE, so a respelling is one edit.
//
// THE CLOCK IS THE LOOP'S, AND SO IS THE LENGTH. The POSITION is the cursor
// less the trim's begin while a transport session is live, and 0 at rest; the
// DURATION is THE TRIM WINDOW'S OWN LENGTH under the SAME gate — the trim is
// the whole lap, so the console's bar fills through it and refills at each
// wrap, the way a media file's does (architect 2026-09-17, from the car) —
// and -1, unknown, at rest, because the session says PLAYING at speed 1.0 at
// all times and a length published at rest would run the console's own clock
// into the end of a track that is not sounding. The one gate and the
// audition's inherited approximation are stated at derive().
//
// THE PUBLISHER IS A PER-TICK COMPARATOR, NOT AN EDGE INVENTORY, and that is
// a deliberate departure from the player's shape: the player pushes at the
// edges where its display changes, an inventory it can keep because its
// axes are its own few writers; the transport's strings are functions of
// axes written at MANY chokepoints — the audio view, the column, every push,
// pop, restore, save and load of the history, the play and stop edges, the
// loop wrap — so an edge inventory here would be the very drift the roster's
// faces avoid by repainting through main.cpp's per-tick comparator with no
// call at any mutator. So tick() derives the state every tick and pushes
// only when a field but the position differs from the last push (or the
// player just came down, or a loop wrap bumped the epoch): a handful of
// short strings compared per tick, and a binder call only on change.
//
// WHERE THE CAR ACTS AND WHERE IT IS DROPPED — admits(), one predicate: a
// command is DROPPED under a prompt (a question on screen is answered there,
// the player's own rule), while the folder overlay stands (the Open project
// picker and the AV Sync Stats panel — the player is forked ahead at the
// hook, so its owner tag never reaches this test), under a DIALOG modal
// editor (modal_dialog_editor_active: the settings editor, the commit-title
// editor and the BPM bracket editor, the surfaces whose open stopped
// playback and whose keys are theirs alone) and in the `h` history view
// (playback is removed from the view whole and bare Space is consumed
// there).
//
// PAST admits(), ALL THREE BUTTONS ARE THEIR KEYS (architect 2026-09-17:
// "look at how undo/redo and play work in the GUI under an open flag editor
// or a drag, and follow that rule"), so each meets the head gates on_key asks
// ahead of its own chord, through the same verdicts and the same sentences:
//   PREVIOUS AND NEXT ARE Ctrl+Z AND Ctrl+Shift+Z WHOLE
//     (GuiInputHandler::run_undo_redo_without_key) — under a flag editor or a
//     pointer drag the chord is swallowed with the key's card, a read-only tab
//     cards the chord, the iteration lock cards undo's own sentence — and then
//     they meet the command's own refusals and cards (the empty stack, the
//     other tab's lock, the restrict-undo lamp). Each of those refusals ends
//     the press where it ends today and plays NOTHING (the block below).
//   THE PLAY TAKES BARE SPACE'S GATES
//     (GuiInputHandler::car_play_refused_by_key_gates) — the gates alone,
//     because the ACT is the car's own loop rather than toggle_playback: under
//     a pointer drag it cards `Keys are ignored during a drag` as Space does,
//     and UNDER A TOP-STRIP FLAG OR BOUND EDITOR IT IS CONSUMED IN
//     SILENCE, which is what Space does there too (Space is printable, so the
//     field takes it as a typed character and the transport never sees it;
//     the car has no character to type, so it borrows no sentence). A
//     read-only tab and the iteration lock stay LEGAL for it, as they are for
//     Space.
// So the play no longer acts under a flag editor or a pointer drag, and what
// it still acts under is nothing the keyboard would refuse either.
//
// THE INPUT HANDLER IS AN ACT OWNER'S AND A GATE OWNER'S BACK-POINTER, NOT
// THE DELETED KEY ROAD: Previous and Next call
// GuiInputHandler::run_undo_redo_without_key, the one body Ctrl+Z's arm
// shares, and the play calls car_play_refused_by_key_gates for Space's own
// head gates — no key is pressed, no modal ring is touched, no dispatch runs —
// each as a DELIBERATE press (a synthesized repeat's silent empty-stack wall
// is the held key's alone; the wheel does not repeat). A restore stops a live
// session exactly as the key's does (the restore body's own stop), the car's
// loop included.
//
// THE SKIPS PLAY WHAT THEY STEPPED TO (architect 2026-09-18; the pending play
// approved 2026-09-19). His complaint from the car: Previous and Next stepped
// the history and then he had to press play after every step, and that was
// the cumbersome part. The project's undo stack is small and bounded and
// HEARING THE STATE YOU JUST STEPPED TO IS THE ACT, so each skip runs its
// chord whole and then THE CAR'S PLAY — the loop of the trim from its begin,
// GuiPlaybackLifecycle::car_play_playback, the toggle's play arm reached
// without its fork (the reason the two entries exist is at that declaration).
// THE RENDER PLAYER'S OWN Previous / Next DELIBERATELY DO NOT (a RECORDED
// ASYMMETRY, not an oversight): a sweep folder holds hundreds of files and
// walking one must not make noise, so with the player standing a skip AT REST
// stays a silent walk of the band and only a skip over a SOUNDING item
// changes what sounds (GuiRenderPlayer::car_previous / car_next).
//
// TWO RULES, and the first is the whole safety of it:
//   1. A REFUSED STEP PLAYS NOTHING, and leaves a running loop running.
//      Nothing was stepped to, so there is nothing new to hear — an empty
//      stack, the other tab's lock, the restrict-undo lamp, the iteration
//      lock, a read-only tab, an open editor, a pointer drag: every one ends
//      the press exactly where it ends today, with today's card and nothing
//      else. The bool that carries the answer runs all the way from
//      Undo::do_undo / do_redo through run_undo_redo_command to
//      run_undo_redo_without_key, false at every refusal arm on the way.
//   2. THE PENDING CAR PLAY. A restore triggers the target preview, which
//      with an idle worker dispatches SYNCHRONOUSLY and often resolves on the
//      render cache's reuse rung (an A->B->A walk of the history is exactly
//      its shape), so in target view the preview is usually ready the instant
//      the restore returns and the play fires at once. It is NOT ready for a
//      state never previewed this session, which is what a POSITION edit
//      leaves behind: drops, drags and nudges author in SOURCE view, where a
//      trigger bumps the generation and dispatches nothing. Without a wait
//      the feature would be dead in `A) T+W`, the view his console shows. So
//      a skip whose play is refused FOR READINESS ALONE ARMS A ONE-SHOT and
//      the tick fires it the moment the preview settles (PendingCarPlay,
//      below, where the clears are enumerated). Nothing is carded for that
//      wait: the refusal is the silent one-dimensional class row 8 already
//      answers with `Updating...` (messaging.md), and the latch turns that
//      silence into a wait rather than a refusal. It is asynchronous and it
//      makes SOUND, not a popup — the 2026-09-15 no-async-popups ruling is
//      about popups and stands.

// THE TITLE'S ONE COMPOSER: WHERE THE SESSION STANDS, SPELLED AS A BATCH
// CELL'S BASENAME IS (architect 2026-09-17, replacing the three-number line of
// that morning) — the leading index, an underscore, then the payload:
//
//     <index>_<distance>
//
// <index> IS THE LIVE STATE'S NUMBER in the session walk's counting, the same
// number the `h` view's Local walk shows for the state on screen: N − live
// index = (U + R + 1) − R = U + 1 with U = undo_stack.size() and
// R = redo_stack.size(). It is that ONE ARITHMETIC here rather than a call
// into HistoryMode::member_number, because the walk is bound only while the
// view stands and this line composes on every tick. Bare decimal, no padding:
// a batch cell pads to its folder's width, and a session has no set to pad
// against.
//
// <distance> IS HOW FAR THE LIVE STATE STANDS FROM THE SAVE — positive after
// it, zero at it, negative behind it — which is −saved_distance under
// UndoHistory's sign convention (the saved state is d steps from the live one:
// 0 at the save, negative when the save lies |d| undos back, positive when it
// lies d redos ahead; every push, pop, restore and eviction moves it with the
// stacks). PROJECT OPEN COUNTS AS THE SAVE until the first save: `saved_distance`
// starts at 0 with `saved_valid` true, so an untouched fresh project reads
// "1_+0". WITH NO SAVE IN REACH (saved_valid false: a push that orphaned a save
// lying on the redo side, the cap's eviction of the state it named, or a
// coalesced burst's net-zero pop at the save) the distance reads `?`, the
// index still standing.
//
// SPELLING: ALWAYS SIGNED (architect 2026-09-22), as a sweep cell's hop entry
// is (format_signed_hops spells `+2`) — "+2" after the save, "+0" at it, "-2"
// behind it. HIS EXAMPLE: open, drop two, save, drop two -> "5_+2"; Previous
// twice -> "3_+0"; Previous twice more -> "1_-2".
std::string car_transport_title_line(const UndoHistory& history);

struct GuiCarTransport {
    AppState&              app;
    const GuiAudio&        audio;
    GuiPlatform&           gui;
    GuiPlayback&           playback;
    GuiPlaybackLifecycle&  playback_lifecycle;
    Viewport&              viewport;
    // Read for Space's own target-view gate (preview_ready), asked ahead of
    // the launch as on_key asks it ahead of toggle_playback.
    const GuiTargetRender& target_render;
    // The undo / redo act body's owner (the head comment: a back-pointer onto
    // an act, never a key road).
    GuiInputHandler&       input_handler;

    GuiCarTransport(AppState&              app_,
                    const GuiAudio&        audio_,
                    GuiPlatform&           gui_,
                    GuiPlayback&           playback_,
                    GuiPlaybackLifecycle&  playback_lifecycle_,
                    Viewport&              viewport_,
                    const GuiTargetRender& target_render_,
                    GuiInputHandler&       input_handler_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          playback_lifecycle(playback_lifecycle_),
          viewport(viewport_),
          target_render(target_render_),
          input_handler(input_handler_) {}

    // A HEAD UNIT'S BUTTON WITH THE PLAYER CLOSED — THE TABLE, each arm gated
    // by admits() EXCEPT the Play arm, which is ungated for the reason stated
    // there (the head comment owns the gate's terms):
    //   Pause / PlayPause -> car_toggle(): ONE BODY FOR THE TWO, the player's
    //     reason — the display is a dummy that says PLAYING, so the console's
    //     one button sends whichever verb it believes and both of them mean
    //     "the other one"; the body is the car's Space
    //     (GuiPlaybackLifecycle::car_toggle_playback — the stop arm the one
    //     stop body, the play arm the loop of the trim), behind bare Space's
    //     own head gates and then its target-view readiness gate.
    //   Play -> GuiPlayback::ensure_device_available_for_play() AND NOTHING
    //     ELSE: PLAY MEANS REOPEN THE STREAM AND START NOTHING (architect
    //     2026-09-18, measured in the car). The Accord sends a plain
    //     KEYCODE_MEDIA_PLAY about a second after the Bluetooth link comes up
    //     and that IS its autoplay — there is no separate signal — while every
    //     HUMAN press arrives as Pause, the dummy display leaving the console
    //     nothing else to send. The link coming up also KILLS the AAudio
    //     stream (AAUDIO_ERROR_DISCONNECTED at the connect), and nothing but a
    //     press reopens it, so this arm reopens it and starts it SILENT right
    //     there: the Bluetooth audio link comes up under the car's own
    //     fade-in, where the connect's crackle is spent, and the constant
    //     stream this car design rests on begins at the connect. A healthy
    //     stream makes the call a no-op. THE RENDER PLAYER'S OWN ARM ANSWERS
    //     AHEAD OF ITS PROMPT GUARD on the same reason (architect 2026-09-18,
    //     "we want symmetry as much as possible"), so both car roads answer a
    //     connect alike whatever stands on the screen.
    //     THE ACCEPTED COST: a PLAY key can no longer start
    //     playback from any remote — it costs nothing while the dummy display
    //     stands, because a remote that believes PLAYING sends PAUSE, and the
    //     earbuds with the GUI in front behave as the car does. NOT A TIMING
    //     GESTURE: no timer and no window after the connect; the rule is the
    //     key's kind alone.
    //   Previous -> car_previous(): run_undo_redo_without_key(false), UNDO
    //     WHOLE — Ctrl+Z's head gates, refusals and cards, then the restore
    //     (which stops a live session, the car's loop included) — AND THEN
    //     THE CAR'S PLAY (car_play_after_step, the tail and its two rules
    //     below).
    //   Next -> car_next(): run_undo_redo_without_key(true), REDO WHOLE,
    //     Ctrl+Shift+Z's, and the same play tail.
    //   Stop -> the one stop body (stop_playback_if_playing): pause IS stop by
    //     ruling, and a console with a Stop button gets the same act.
    //   FocusLost / FocusLostTransient -> the one stop body iff a transport
    //     session is live (Android's one imposed interrupt, the player's arm's
    //     shape); a loss with nothing sounding writes nothing.
    //   FastForward / Rewind / SeekTo / FocusGained -> consumed no-ops: the
    //     project transport has no seek vocabulary from the console (the
    //     Accord's wheel sends Previous / Next; SeekTo has no scrub to answer
    //     on a dummy display that counts up from zero), and NOTHING RECOVERS
    //     BY ITSELF (the AAudio posture; the user presses play).
    // EVERY COMMAND CLEARS THE PENDING CAR PLAY AT THIS HEAD, ahead of the
    // switch (the latch's contract at its declaration): a Pause, a Play, a
    // Stop, a seek or a focus loss from the console cancels a wait, which is
    // his "any other transport act clears it". The two skips re-arm in their
    // own tails if their new step is refused for readiness again.
    void on_media_command(GuiMediaCommand cmd);

    // THE PUBLISHER (the head comment's comparator), called from main.cpp's
    // on_tick EVERY tick, above the render player's fork, so it runs whether
    // or not the player stands: with the player active it remembers the
    // hand-over and returns (the player owns the wire then); otherwise it
    // derives the state and pushes iff the player just came down or a field
    // but the position differs from the last push. IT ALSO RUNS THE PENDING
    // CAR PLAY, ahead of the comparator, so the skip's sound lands on the
    // first tick after the preview settles and the push that follows already
    // carries the play (run_pending_play, the rules at the latch).
    void tick();

    // A LOOP WRAP HAPPENED — called by main.cpp's tick right after the wrap's
    // predictor resync (the one consumer of GuiPlayback::consume_loop_wrap,
    // which must be consumed once). Bumps the comparator's epoch so the next
    // tick pushes with the position back near 0 and the console's clock
    // re-starts at the trim's start on every lap — the one freedom the dummy
    // clock has: truthful while a loop plays, a running dummy at rest.
    void note_loop_wrap();

private:
    bool admits() const;
    void car_toggle();
    void car_previous();
    void car_next();
    // THE SKIPS' PLAY TAIL, reached only after a step that actually restored
    // (the head comment's rule 1): the car's play, or the wait that stands in
    // for it in target view. It asks NOTHING ELSE — in particular it does NOT
    // re-ask the play's own key gates (car_play_refused_by_key_gates), and
    // that is load-bearing rather than an economy: by construction they
    // cannot refuse here, because a drag or an open keyboard-modal editor
    // already stopped the STEP one line above with the undo chord's own card,
    // and re-asking them could raise a SECOND card for one console press.
    void car_play_after_step();
    // The latch's tick body (the rules and every clear at its declaration).
    void run_pending_play();
    GuiMediaState derive() const;

    // THE COMPARATOR'S RECORD: the last pushed state, plus the wrap epoch it
    // was pushed under (a field of this record and not of GuiMediaState — the
    // wire carries a position, and the epoch is what makes the tick re-send
    // one). `valid` is false until the first push of this project.
    struct Published {
        bool          valid      = false;
        GuiMediaState state;
        int64_t       wrap_epoch = 0;
    };
    Published last_pushed_;
    int64_t   wrap_epoch_       = 0;
    // The player stood on the previous tick: the falling edge pushes whatever
    // the transport's state is, so the wire is taken back within one tick of
    // the player's close (up to one tick of the player's last picture,
    // accepted).
    bool      handed_to_player_ = false;

    // THE PENDING CAR PLAY (architect 2026-09-19) — armed by a skip whose
    // play was refused FOR THE PREVIEW'S READINESS ALONE (car_play_after_step;
    // the measured reason it exists is rule 2 of the head comment), and fired
    // or dropped by tick(). The three fields are THE AXES THAT DECIDE WHAT
    // WOULD BE HEARD: the AUDIO VIEW picks the buffer, the TAB picks the trim
    // and the marker stores, and the STATE ID names the authored state — so a
    // later step, an S/T switch, a tab switch or a save makes this wait stale
    // and the tick drops it rather than playing something he has already left.
    //
    // `state_id` IS AN IDENTITY AND NOT A DISPLAY STRING, which is why it is
    // not called `title`: it is spelled by car_transport_title_line, whose
    // `<index>_<distance>` line HAPPENS TO SPELL exactly the identity this
    // latch needs — the live state's own number, which every undo and redo
    // moves — and a retune of that line's spelling (it changed on 2026-09-17
    // and again on 2026-09-22) must be read as a change to a wire string and never as a
    // silent change to this staleness test.
    //
    // WHY THE COMPOSITE AND NOT THE BARE UNDO DEPTH: undo, then author a new
    // edit, and undo_stack.size() comes back to the number it already had on
    // a DIFFERENT state, so a depth alone would call a stale wait fresh. The
    // distance half moves on that push and tells the two apart.
    //
    // THE ONE OVER-CLEAR IT BUYS, said plainly rather than left implied: the
    // distance half also moves on a SAVE, so a save while the preview settles
    // clears a pending play. Harmless — the play is one press away — and
    // nearly unreachable from the console, which has no Ctrl+S.
    //
    // THE MARKER COLUMN IS DELIBERATELY NOT AMONG THEM (2026-09-19). W, P and
    // M choose which flags are authored and painted and change NOTHING about
    // the sound this latch is waiting for: the same buffer, the same trim, the
    // same state. Pressing `2` to look at the phase resets while the preview
    // settles would cost him the play, and the play he lost would have sounded
    // identical to the one he gets by pressing it again. A RESTORE IS ALREADY
    // COVERED: an undo entry carries all three view tags and its restore
    // writes them, so any restore that moved the column moved the STATE ID
    // with it and this wait is stale on that field — a column that moves with
    // the state id standing still can only be a deliberate view-selector press.
    //
    // THE FOURTH FIELD IS NOT AN AXIS OF THE SOUND BUT OF WHO IS ASKING:
    // `gui_press_count` snapshots AppState::gui_transport_press_count, which
    // a project-transport LAUNCH press bumps AT ITS ROAD'S HEAD — bare
    // Space's arm, Shift+Space's arm and the waveform scrub's one act, never
    // the bottom-row Home/End skips (the owner and its writer inventory are
    // at the field, app_state.h ~4743). A GUI TRANSPORT ACT SUPERSEDES THE CAR'S
    // DEFERRED PLAY, WHETHER IT PLAYED OR WAS REFUSED (architect 2026-09-21,
    // on Sol's finding): a scrub or a Space refused at the preview's own
    // readiness gate sounds nothing, so the "something is already sounding"
    // clear below never saw it, and the wait fired the car's loop from the
    // trim when the preview settled — over a press that had asked for
    // something else. The count is a snapshot compare, the same shape as the
    // other three, so the latch needs no hook into the input cluster.
    struct PendingCarPlay {
        bool        armed      = false;
        char        audio_view = '\0';
        char        tab        = '\0';
        std::string state_id;
        uint64_t    gui_press_count = 0;
    };
    // THE CLEARS, ALL OF THEM, AND NOT ONE OF THEM IS A DURATION (nothing in
    // this product decides a wait by a timer, and every state below is one
    // the tick can read):
    //   * THE PLAYER TOOK THE WIRE (tick's own early arm) — the head unit has
    //     another owner now and this wait is not its business.
    //   * !admits() — a prompt, the picker or the stats panel, a dialog
    //     editor, the `h` view: the wait is over.
    //   * THE SNAPSHOT MOVED — any of the three fields above.
    //   * A GUI TRANSPORT PRESS LANDED — the fourth field above: bare Space,
    //     Shift+Space (and the Play button's two presses, which are those
    //     chords) and the waveform scrub, played OR REFUSED. This is the
    //     rule's one statement; the writers point here.
    //   * SOMETHING IS ALREADY SOUNDING — he started it himself at the glass.
    //   * THE PREVIEW SETTLED (!is_updating) — the wait is over one way or
    //     the other: clear, and play iff preview_ready(). A render that
    //     settled with NO buffer (the failed preview, which cards on its own)
    //     plays nothing.
    //   * ANY CONSOLE COMMAND, at on_media_command's head (its own comment).
    // It is per-project state and needs no clear at a reopen: this object is
    // built inside run_project and dies with the project.
    PendingCarPlay pending_play_;
};
