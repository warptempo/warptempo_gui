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
// with it closed every command is THIS cluster's. His spec: the buttons act on
// the GUI's own transport "like the transport keys" — PLAY / PAUSE /
// PLAYPAUSE are the play transport, PREVIOUS is the playhead to the TRIM
// START (Home's landing), NEXT is the playhead to the TRIM END (End's
// landing) — under the same dummy-track trick as the player (the session says
// PLAYING always, its clock always in motion), with ONE DIFFERENCE from the
// GUI's own transport: A PLAY ISSUED FROM THE CONSOLE LOOPS THE TRIM FOREVER,
// in every view, "even if it's played from the last frame of the trim, as it
// would be after the user presses Next". PAUSE IS THE GUI'S STOP: the
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
// THE THREE LINES, with the player closed: the ALBUM is the project's name
// (his ruling — the project stays on the console's dim top line), the TITLE
// is THE TAB AND THE VIEW ("Tab A, T+W" — car_transport_title, the tab
// letter as the tab row spells it and the views as the view bar spells them
// through the one speller view_pair_label), and the ARTIST is THE TRIM SPAN
// — the two times Home and End would land on, spelled as the row-8 clock
// spells a position ("00:12.000 - 00:47.500", car_transport_artist; with a
// full trim window it reads from zero to the piece's end). The title's and
// the artist's spellings are the planner's choice, to be ruled on by the
// architect on his console; each is one composer, so a respelling is one
// edit. The DURATION is unknown (-1: the console counts up with no end to
// run into, the silence track's shape) and the POSITION is the loop clock
// while a transport session is live — the cursor less the trim's begin — and
// 0 at rest.
//
// THE PUBLISHER IS A PER-TICK COMPARATOR, NOT AN EDGE INVENTORY, and that is
// a deliberate departure from the player's shape: the player pushes at the
// edges where its display changes, an inventory it can keep because its
// axes are its own few writers; the transport's strings are functions of
// axes written at MANY chokepoints — the tab switch, the audio view, the
// column, the trim (every writer of the pair), the play and stop edges, the
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
// there). It ACTS under a top-strip flag editor (chords-only modality: the
// editor stops no playback and the car is not a key) and under a pointer
// drag (the player's own rule: the drag swallow is the key road's, and a
// direct act never enters it). A read-only tab and the iteration lock are
// LEGAL, as Space and Home / End are.
//
// THE INPUT HANDLER IS AN ACT OWNER'S BACK-POINTER, NOT THE DELETED KEY ROAD:
// Previous and Next compose GuiInputHandler::run_playhead_end_jump exactly
// as the bare Home and End keys compose it — no key is pressed, no modal ring
// is touched, no dispatch runs — so the head unit's skips stop a live
// session (the car's loop included), clear the selection, land at the trim's
// bound and refuse silently where the form would change nothing (the act's
// own rule); the next car Play launches from where they landed, and after
// Next — the trim's LAST frame — that launch finds fewer than two frames to
// the loop's end and starts at the trim's begin (car_toggle_playback's rule),
// so Next-then-Play is "from the top", his stated intent.

// THE TITLE'S ONE COMPOSER: "Tab <letter>, <audio>+<column>" — the active
// tab's letter (the tab row's own spelling, whose label is the letter itself;
// kTabs, paint_handler.cpp) and the active views through view_pair_label.
std::string car_transport_title(const AppState& app);

// THE ARTIST'S ONE COMPOSER: "<begin> - <end>", the two times Home and End
// would land on (playhead_skip_landing_frame's bare pair, so the string and
// the skips agree by construction), each spelled by format_timestamp at the
// project source's rate — the row-8 clock's own spelling of a position.
std::string car_transport_artist(const AppState& app, const GuiAudio& audio);

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
    // The Home / End act body's owner (the head comment: a back-pointer onto
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
    // by admits() (the head comment owns the gate's terms):
    //   Play / Pause / PlayPause -> car_toggle(): ONE BODY FOR ALL THREE, the
    //     player's reason — the display is a dummy that says PLAYING, so the
    //     console sends whichever verb it believes and every one of them
    //     means "the other one"; the body is the car's Space
    //     (GuiPlaybackLifecycle::car_toggle_playback — the stop arm the one
    //     stop body, the play arm the loop of the trim), behind Space's own
    //     target-view readiness gate.
    //   Previous -> car_previous(): run_playhead_end_jump(false, false), HOME
    //     WHOLE — it stops a live session (the car's loop included; his model
    //     is "the transport keys": Previous / Next MOVE the playhead and the
    //     next Play launches from it), clears the selection, lands at the
    //     trim's start, and refuses silently where the form would change
    //     nothing.
    //   Next -> car_next(): run_playhead_end_jump(true, false), END WHOLE,
    //     landing on the trim's LAST frame — so the next car Play, finding
    //     fewer than two frames to the loop's end, starts at the trim's begin:
    //     Next-then-Play plays from the top.
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
