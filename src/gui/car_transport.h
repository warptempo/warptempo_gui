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
// PREVIOUS IS UNDO and NEXT IS REDO (Ctrl+Z and Ctrl+Shift+Z
// whole, 2026-09-17 — the console steps the piece's history and reads the
// position back off its own display) — under the same dummy-track trick as
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
// and its distance from the save ("5_2", "3_0", "1_-2" —
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
//     other tab's lock, the restrict-undo lamp).
//   THE PLAY TAKES BARE SPACE'S GATES
//     (GuiInputHandler::car_play_refused_by_key_gates) — the gates alone,
//     because the ACT is the car's own loop rather than toggle_playback: under
//     a pointer drag it cards `Keys are ignored during a drag` as Space does,
//     and UNDER A TOP-STRIP FLAG, BOUND OR LEVEL EDITOR IT IS CONSUMED IN
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
// it, 0 at it, negative behind it — which is −saved_distance under
// UndoHistory's sign convention (the saved state is d steps from the live one:
// 0 at the save, negative when the save lies |d| undos back, positive when it
// lies d redos ahead; every push, pop, restore and eviction moves it with the
// stacks). PROJECT OPEN COUNTS AS THE SAVE until the first save: `saved_distance`
// starts at 0 with `saved_valid` true, so an untouched fresh project reads
// "1_0". WITH NO SAVE IN REACH (saved_valid false: a push that orphaned a save
// lying on the redo side, the cap's eviction of the state it named, or a
// coalesced burst's net-zero pop at the save) the distance reads `?`, the
// index still standing.
//
// SPELLING: to_string's own — a negative carries its minus, a zero is bare
// "0", a positive carries no sign. HIS EXAMPLE: open, drop two, save, drop two
// -> "5_2"; Previous twice -> "3_0"; Previous twice more -> "1_-2".
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
    //     stream makes the call a no-op.
    //     THE ACCEPTED COST: a PLAY key can no longer start
    //     playback from any remote — it costs nothing while the dummy display
    //     stands, because a remote that believes PLAYING sends PAUSE, and the
    //     earbuds with the GUI in front behave as the car does. NOT A TIMING
    //     GESTURE: no timer and no window after the connect; the rule is the
    //     key's kind alone.
    //   Previous -> car_previous(): run_undo_redo_without_key(false), UNDO
    //     WHOLE — Ctrl+Z's head gates, refusals and cards, then the restore
    //     (which stops a live session, the car's loop included).
    //   Next -> car_next(): run_undo_redo_without_key(true), REDO WHOLE,
    //     Ctrl+Shift+Z's.
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
    void on_media_command(GuiMediaCommand cmd);

    // THE PUBLISHER (the head comment's comparator), called from main.cpp's
    // on_tick EVERY tick, above the render player's fork, so it runs whether
    // or not the player stands: with the player active it remembers the
    // hand-over and returns (the player owns the wire then); otherwise it
    // derives the state and pushes iff the player just came down or a field
    // but the position differs from the last push.
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
};
