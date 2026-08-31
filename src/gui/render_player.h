#pragma once

#include "notifications.h"
#include "app_state.h"
#include "audio.h"
#include "gui_media.h"
#include "platform.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "renders_dir.h"
#include "target_render.h"
#include "viewport.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// THE CAR KEYS' STABLE-CODE BASE (recorded beside the painted keyboard's own,
// onscreen_keyboard::kStableCodeBase): GuiRenderPlayer::on_media_command
// synthesizes the player's keys through GuiPlatform::synthesize_key, whose
// stable code is the core's per-key identity — what the repeat cancel and the
// synthesized-left hold end compare against — and must be non-zero, stable
// per key and unlike any other producer's. The keyboard's codes are [1, 97];
// these start at 1000 and are the GuiKey's own value added to it, so two car
// keys differ and no car key can meet a painted one.
inline constexpr uint32_t kCarStableCodeBase = 1000;

// -- THE MODE'S SHARED REFUSAL SENTENCES ------------------------------------
//
// (architect 2026-08-30, the strictness ruling.) Each is said from SEVERAL
// acts, and two of them from a POINTER site as well — the play-scrub's press,
// which lives with the other pointer routers (input_pointer.cpp) — so they
// are spelled here rather than inside the operations file. Every refusal with
// a single site keeps its literal where it fires. The acts raise these
// through GuiRenderPlayer::status, the thin road the decode refusals already
// take; the scrub's press raises them on the ordinary notify, having no
// status of its own.
//
// NOTHING IS LOADED: Play with no item, the two folder-end jumps (which walk
// the TRANSPORT ITEM's folder and so have nothing to walk without one), and
// every seek road — Home and End among them, both of which reach seek_to.
inline constexpr const char* kNoPlayerItem = "No render is loaded to play";

// THE ITEM FOLDER'S TWO ENDS: the first-jump at the first wav, the last-jump
// at the last. NOTHING LOOPS, so an end is an end. (Their producers were the
// two neighbour walks as well until 2026-08-31, when the skips became Home
// and End and the neighbour steps left the product — Home's previous-track
// window is the one step that remains, and it does not report a wall: at the
// first entry it restarts the track instead.)
inline constexpr const char* kFirstInFolder =
    "This is the first render in the folder";
inline constexpr const char* kLastInFolder =
    "This is the last render in the folder";

// A SEEK WHILE THE TRANSPORT IS IDLE (R41's dead slider): the two seek keys,
// bare Home, bare End, the car's absolute seek and the scrub's own press all
// meet it.
inline constexpr const char* kSeekWhileIdle = "Start playback before seeking";

// THE PREVIOUS-TRACK WINDOW (architect 2026-08-31, decision 88 — "agree on
// <3s"): bare Home, its skip button and the head unit's Previous all run ONE
// act, and inside the item's first three seconds that act is THE PREVIOUS
// ENTRY rather than this track's start. It is the iPod / car-head-unit
// convention, and it is POSITION-BASED RATHER THAN PRESS-TIMED on purpose:
// the wheel's buttons are slower than any double-press window (kHoldBeatMs
// included), and a position window makes a SECOND Home "previous" at any
// press speed at all, since the first one landed the cursor at 0.
//
// IT IS AN AUTHORED DURATION AND NEVER SCALES — gui_scale is a LENGTH axis
// (the scaled_px family) and no duration in the product reads it. It is
// converted to frames at the DEVICE's rate where it is used, the item being
// at that rate by the decode's own equality.
inline constexpr int64_t kPlayerPreviousThresholdMs = 3000;

// The input handler is reached through a back-pointer below (the ring clear
// on_media_command owes), and it holds this cluster by reference, so the
// include cannot run both ways: input_handler.h includes this header and
// render_player.cpp includes that one.
struct GuiInputHandler;

// THE RENDER PLAYER (architect design 2026-08-28, the in-app player for the
// car) — the operations cluster for the MODE that plays the project's own
// renders through the one playback engine: the folder overlay above the
// bottom row (folder_overlay.h) lists the project's OUTPUT FOLDERS — `render/`
// with the deliverable, which is the CURRENT TITLE'S ONE WAV and nothing else
// (the listing prunes the folder to it, prune_render_folder in renders_dir.h),
// `tmp/` with its batch folders and their cells — and
// the bottom row's modal carries the transport (the two skips around
// Play-Pause and Stop — Home / Play-Pause / Stop / End since 2026-08-31 —
// the play-scrub, the clock, the Repeat one lamp, and Load in place /
// Close flush right — the row's order and faces are the painter's, R25/R36).
// The state it moves is AppState::render_player and AppState::folder_overlay
// (app_state.h, where every field is described); this struct owns the acts.
//
// THE TRANSPORT HAS THREE STATES AND THE BUTTONS ANSWER THEM (architect
// 2026-08-28, R36 — PLAY/PAUSE PLUS STOP, "if the user is playing a track and
// wants to go back to the beginning of that track, they can hit stop and then
// play again. This is different from the live transport, because there we have
// the scrub, and a pause wouldn't make sense — the scrub always returns to the
// playhead when not playing"). THE STATE IS STORED, in
// `AppState::RenderPlayer::transport` (that field's block owns the reasons,
// the writer set and the readers) — IDLE (nothing to resume: no item, or an
// item a STOP, a natural end or a fresh open left resting at its start), LIVE,
// PAUSED (an item the transport parked, AT WHATEVER FRAME — a pause at frame 0
// is PAUSED, which no reading of the resume point could say) — and the table
// is at play_button_act.
//
// THE MODEL (R1, R2, revised 2026-08-29): the listing is navigated THE
// REGULAR WAY and A CLICK ACTIVATES — a click or tap on a folder row ENTERS
// it, on the `..` row at the top of every non-root listing goes UP (Backspace
// on plastic), on a wav row PLAYS IT FROM ITS START. The click's act rides
// the motionless LIFT (the press still arms, the same press being the band's
// possible scroll drag) and the highlight moves onto the row first; Enter is
// the keyboard's own click on the highlight and Up/Down walk the band without
// opening anything. THE PLAY BUTTON DOES NOT READ THE HIGHLIGHT AT ALL — it
// answers the transport (the table at play_button_act). THE TRANSPORT'S ITEM
// is separate from the highlight: it keeps playing while
// the listing is navigated elsewhere, it wears the transport glyph on its
// row, and AUTO-ADVANCE, HOME'S PREVIOUS-TRACK WINDOW and the two
// Shift+Home / Shift+End ENDS walk ITS
// FOLDER'S wav list as it was listed when the item was played — never another
// folder and never a wrap. Every listing is built when its folder is entered
// and never kept fresh.
//
// THE HIGHLIGHT FOLLOWS THE TRANSPORT'S ITEM (architect 2026-08-28, R38,
// superseding the design's "Previous and Next never move the highlight"): at
// every item change THE TRANSPORT MAKES ON ITS OWN — Home's previous-track
// window / the folder's ends / auto-advance / the folder-end restart — the
// band moves onto
// the new item's row and scrolls it into view WHEN THAT ROW IS IN THE LIVE
// LISTING, and stays where it is when the user has navigated elsewhere. Its
// one owner is play_wav, which is the one place the item changes, so the rule
// covers the user's own plays for free (the row is already the highlight
// there) and needs no membership list. A USER'S OWN HIGHLIGHT MOVES ARE
// UNTOUCHED — nothing here fights the band back onto the item, which is why
// THE SEAT IS GATED ON THE CHANGE and not run at every call: the REPEAT ONE
// replay re-enters play_wav on the item already resting there at every
// natural end, and an ungated seat would drag the band off the row a user
// walked to under a lit lamp — repeatedly, and under his next Load in place.
//
// NOTHING LOOPS, WITH ONE SANCTIONED EXCEPTION — REPEAT ONE (architect
// 2026-08-28, R26): the player's lamp is a two-state toggle, off or repeat
// the ONE item ("the user can just press play once the playlist finishes...
// repeat one is much more useful", which is why there is no repeat-all), and
// while it stands THE NATURAL END REPLAYS THE ITEM FROM ITS START through the
// player's own play road instead of advancing — AND IT OUTRANKS THE ADVANCE
// WHETHER THE REPLAY SOUNDS OR REFUSES: a replay that cannot decode (the file
// deleted or republished in another shape while it played) leaves its own
// words on a notification card and the transport resting on that item at its
// start, never the folder's next wav and never the folder-end bit. It is the
// whole of the exception: nothing else in the product plays anything twice by
// itself, and the state is session-only (false at every open, serialized
// nowhere).
//
// AT THE FOLDER'S LAST WAV, with the lamp off, the transport stops with the
// item resting at its start — and THE NEXT PLAY STARTS THE FOLDER'S FIRST
// WAV rather than replaying that last one (architect 2026-08-28, R27: the
// car's Play at the end of a playlist). `ended_at_folder_end` is the one bit
// that says the transport is resting THERE, cleared by every play, resume,
// seek, row open, open, close and STOP; `play_button_act` is its one reader
// and so the one owner of the act, which the car's Play reaches through the
// same key as every other Play.
//
// THE ITEM IS A WAV PLAYED AS IT IS: decoded through the in-tree WAV reader
// (wav_read_full, audio_io — called, never changed) after the PROBE has
// confirmed it matches the device's own rate and channel count (the engine
// never re-inits and nothing in the tree resamples; a render of this project
// is at the source's rate by construction, so the equality check is a
// refusal, never a conversion) and the allocation owner has passed the shape
// (checked_audio_sample_count) — every refusal is its own words on the status
// line and the item does not change. The decoded buffer is bound through
// GuiPlayback::rebind_buffer after the fence (the target preview's own road).
// No render is dispatched by the player, ever; a running render continues in
// the background (a preview that completes meanwhile does NOT rebind under
// the player — GuiTargetRender::complete_successful_buffer's guard — and the
// close's re-express binds it).
//
// THE SECOND LAUNCH BODY. GuiPlaybackLifecycle::launch_playback_window is the
// product's ONE launch body for the PROJECT'S audio, and this cluster does not
// use it, deliberately: that body's whole seed — the A/B audition clear, the
// playable gate against the project's domain, the waveform scanner, the
// follow-scroll, the waveform damage — belongs to the project's WAVEFORM,
// which the player does not display. The project's resting playhead does not
// move while the player plays, its scanner never runs, and the item's domain
// is the decoded buffer's own [0, frames). So play_item / resume / seek call
// playback.play directly against that domain, and the state that says the
// transport is live is the player's own (`transport`'s LIVE value, the
// scanner flag's mirror). THE STOP IS STILL THE ONE STOP BODY: every pause,
// natural end and close takes GuiPlaybackLifecycle::stop_playback_if_playing,
// which carries the player's fork inside it (the fence, then the transport
// moved to PAUSED and the modal row damaged instead of the scanner teardown),
// so the keyboard stop rule and the fence-before-rebind ordering hold by
// construction.
//
// ENTER AND LEAVE. open() is the ONE opener — bare `l`, bare `'` outside the
// `h` view and their two icon-row buttons all reach it through on_key — and
// it refuses with "Nothing to play: no renders under render/ or tmp/" when
// neither `render/` holds the current title's wav nor `tmp/` a cell; its callers refuse the modal
// states (a prompt, an
// editor, the `h` view, loading, no source) before it is asked. The open
// takes the modal-open stop, the mode bit, a fresh modal session, the root
// listing and a whole-window damage. close() takes the stop body, clears
// the mode, rebinds THE VIEW'S buffer through the S/T flip's own tail fork
// verbatim (ensure_ready in target view, rebind_to_source in source view),
// and only THEN frees the item's buffer: the engine may hold the pointer
// until the rebind.
//
// THE LOAD ROAD is not here: the Load in place button (bare `'` inside the
// player) and its confirmation live on GuiInputHandler, which owns the shared
// act (load_render_entry_in_place); this cluster answers only which entry is
// highlighted (highlighted_entry) and closes on the act's success.
//
// THE CAR (design §3, 2026-08-28) reaches this cluster through the seam's two
// members (platform_wayland.h owns their contracts): the head unit's buttons
// arrive as GuiMediaCommand values through main.cpp's hook into
// on_media_command, which turns each into THE PLAYER'S OWN KEYS through
// GuiPlatform::synthesize_key — press and release, the on-screen keyboard's
// road — so the ordinary on_key dispatch runs and there is no second road;
// and publish_media_state is the ONE owner of what the head unit shows,
// called at every edge where that changes. The platform is held for exactly
// those two calls.
struct GuiRenderPlayer {
    AppState&             app;
    const GuiAudio&       audio;
    GuiPlatform&          gui;
    GuiPlayback&          playback;
    GuiPlaybackLifecycle& playback_lifecycle;
    Viewport&             viewport;
    GuiTargetRender&      target_render;
    GuiRendersDir&        renders_dir;
    // The player's refusals are notification cards (2026-08-29); status()
    // below is the thin road onto the one push chokepoint.
    GuiNotifications&     notifications;

    // Back-pointer to the input handler, wired in main.cpp after both are
    // constructed (the handler takes this cluster by reference, so it cannot
    // be a constructor argument) — the prompt's own shape (GuiPrompt::input).
    // ITS ONE READER is on_media_command's ring clear, which needs the one
    // owner of the modal keyboard arm (clear_modal_dialog_key_press). Null
    // until main.cpp wires it, and the reader tolerates null.
    GuiInputHandler*      input = nullptr;

    GuiRenderPlayer(AppState&             app_,
                    const GuiAudio&       audio_,
                    GuiPlatform&          gui_,
                    GuiPlayback&          playback_,
                    GuiPlaybackLifecycle& playback_lifecycle_,
                    Viewport&             viewport_,
                    GuiTargetRender&      target_render_,
                    GuiRendersDir&        renders_dir_,
                    GuiNotifications&     notifications_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          playback_lifecycle(playback_lifecycle_),
          viewport(viewport_),
          target_render(target_render_),
          renders_dir(renders_dir_),
          notifications(notifications_) {}

    // THE OPENER (the contract above). Returns whether the mode opened; a
    // refusal has already raised its card or has nothing to say.
    bool open();
    // THE CLOSER (the contract above). A no-op when the mode is down.
    //
    // ITS CAUSES, FIVE CALL SITES (re-grepped 2026-08-28): the modal row's
    // Close button; bare Esc and bare `l` in the mode's key router; the `l`
    // toggle the icon row's Play renders button shares with it; the load
    // road's success; and THE CLOSE ROAD — GuiPrompt::request_close, which
    // takes the mode down at its head, so Ctrl+Q AND the compositor's
    // title-bar X (main.cpp's set_on_close, the same road with no key behind
    // it) both leave the ordinary window standing before the unsaved-work
    // question is asked, and a Cancel answers over no player.
    void close();

    // -- The listing --------------------------------------------------------

    // THE OPEN ACT on row `index` of the live listing: a folder row enters
    // it, the up row goes to the parent, a wav row plays from its start. Its
    // producers are the row click's motionless lift and Enter on the
    // highlight, both through the overlay's one row-act fork.
    void open_row(int index);
    // One folder up; a consumed no-op at the root.
    void up();
    // The widget's three mechanics with the player's damage on top
    // (folder_overlay.h owns the clamps and the scroll-into-view; the picker
    // drives the same mechanics through its own damage): move the highlight by
    // `delta` rows, seat it on `index` — the click's first half, the open
    // being its second — and scroll the band by `rows` rows, the wheel's
    // detent step.
    void move_highlight(int delta);
    void set_highlight(int index);
    void scroll_rows(int rows);
    // The highlighted row's render entry — non-null exactly when the highlight
    // is a LOAD-CAPABLE wav (a batch cell). The load road's one question.
    const AppState::RenderEntry* highlighted_entry() const;

    // -- The transport ------------------------------------------------------

    // THE PLAY BUTTON'S ACT (and Space's, and the car's Play / PlayPause).
    // IT NEVER READS THE HIGHLIGHT, IN ANY STATE (architect 2026-08-29,
    // Audacious: Play when idle plays the current track). The three states it
    // forks on are STORED in AppState::RenderPlayer::transport, which owns
    // them: LIVE is the sounding transport, PAUSED an item the transport
    // parked AT WHATEVER FRAME (a pause that caught the cursor at 0 answers
    // PAUSED here and resumes its own item), IDLE everything with nothing to
    // resume.
    //
    //   LIVE    -> pause it.
    //   PAUSED  -> resume it.
    //   IDLE with an item -> at the folder's END the item folder's FIRST wav
    //                        (R27, the bit's one reader), else the item from
    //                        ITS START, ALWAYS (architect 2026-08-29 ~01:40,
    //                        Audacious: Play when idle starts literally). A
    //                        seek while idle is a consumed no-op (seek_to's
    //                        own head, the one owner), so `resume_frame` is
    //                        always 0 at an idle rest and every rest the
    //                        transport's own acts leave it in — a STOP, a
    //                        natural end, a fresh bind — plays from frame 0.
    //   IDLE with no item -> nothing.
    //
    // THE ROW ACTS ARE THE ROWS' OWN since the same ruling: a click on a
    // folder opens it and a click on a wav plays it, so this button no longer
    // carries the car-stereo OK/Play convention R17 gave it (the highlight's
    // folder open and its play-another-wav arm are both gone).
    // THE BUG THAT PUT THE TRANSPORT AHEAD OF THE HIGHLIGHT IN THE FIRST
    // PLACE (architect 2026-08-28, R36) is subsumed rather than superseded: a
    // Next while a track played advanced the item but not the band, and the
    // live button — wearing Pause — then PLAYED the row still highlighted
    // behind it instead of pausing what was sounding.
    void play_button_act();
    // Pause a live transport (the resume point is the engine's own position)
    // or resume a paused one; a no-op with no item.
    void toggle_pause();
    // STOP (R36): the transport goes idle with the item resting at ITS START —
    // the resume point cleared to 0 and the folder-end bit with it, THE ITEM
    // ITSELF UNTOUCHED, so a following Play replays it from the beginning.
    // That is the whole difference from a pause, and it is why the row carries
    // both. A consumed no-op with no item and on an item already resting at
    // its start.
    void stop();
    // THE ITEM FOLDER'S ENDS (R37, Shift+Home / Shift+End since 2026-08-31 and
    // the two skip buttons' shift-click or long press): the first / last wav of
    // `item_folder`, played from its start on the play road — never
    // a wrap, and a CARDED refusal with no item AND on an item already at that
    // end. They are the folder walk's whole surface now: the item's two
    // NEIGHBOURS were `previous()` / `next()` on bare `,` / `.` and the two
    // skips' plain acts until 2026-08-31, when the skips became Home and End
    // and the step back moved inside Home as its previous-track window (the
    // constant above) — the step FORWARD has no producer left and is gone, the
    // natural end's own advance never having gone through it.
    void first_in_item_folder();
    void last_in_item_folder();
    // REPEAT ONE (architect 2026-08-28, R26) — the row's one lamp, flipped by
    // its button and by bare `r`: while it stands the natural end replays the
    // item from its start instead of advancing. Session-only state
    // (AppState::RenderPlayer::repeat_one, false at every open), and this is
    // its ONE writer past that reset; it damages the row for the lamp and
    // touches no transport.
    void toggle_repeat_one();
    // Seek by `delta_frames` from the current position, clamped into the
    // item; a no-op with no item. A live transport reseeks in place, a paused
    // one moves its resume point, and an IDLE one is a consumed no-op
    // (seek_to's own head, architect 2026-08-29 ~01:40 — Audacious's stopped
    // slider).
    void seek_by(int64_t delta_frames);
    void seek_to(int64_t frame);
    // HOME — the left skip's plain act, bare Home's, and the head unit's
    // Previous (architect 2026-08-31). TWO ARMS OVER ONE POSITION TEST: with
    // the item inside its first kPlayerPreviousThresholdMs AND a previous
    // entry in `item_folder`, this plays THAT ENTRY from its start (the
    // previous-track window at the constant); anywhere else — past the
    // window, at the folder's first entry, with no folder — it seeks the
    // item's own start and takes every refusal seek_to owns, the no-item card
    // and the idle card alike. It never reports a folder wall: at the first
    // entry the restart IS the act.
    void home();
    // THE ITEM'S END (architect 2026-08-30) — Home's twin, the RIGHT SKIP's
    // plain act and the head unit's Next since 2026-08-31, and NOT the
    // folder's
    // walk: it seeks to `frames`, the very position the scrub's right end
    // writes, and does nothing else — the item and the folder are untouched.
    // IT TOOK NO WINDOW OF ITS OWN when Home took one: at the end of a track
    // "next" is what the NATURAL END already does, and this act reaches it by
    // playing the last frames out.
    // A LIVE transport therefore plays the last frames out and the NATURAL END
    // takes it from there, unaltered (advance where a next entry exists,
    // `ended_at_folder_end` at the folder's last, a replay under a lit Repeat
    // one); a PAUSED one moves its rest, which the resume arm reads as at-or-
    // past the end and replays from the start; an IDLE one meets seek_to's own
    // carded refusal, exactly as Home does.
    void end();
    // Left / Right's step: 5 s at the device's rate (R6).
    int64_t seek_step_frames() const;

    // The item position the clock and the scrub read: the engine's cursor
    // while live, the resume point otherwise. 0 with no item.
    int64_t position() const;
    // A frame's x on the published scrub track, and the inverse — the one
    // mapping the painter's marker and the press router's seek share, over the
    // stashed track rect.
    int     scrub_x_of(int64_t frame) const;
    int64_t scrub_frame_at(int x) const;

    // THE TICK (main.cpp's on_tick, forked at its head onto this while the
    // mode stands): a live transport with NO DEVICE TO PLAY ON pauses (the
    // no-device arm below, asked first — gone away or never there, one
    // answer); one the audio thread has ended takes the natural-end branch; a
    // still-live one damages the clock cell and the scrub track once per
    // position change.
    void tick();

    // -- The car ------------------------------------------------------------

    // A HEAD UNIT'S BUTTON, translated into the player's own keys (the
    // contract at the head of this file). With the mode down every command
    // is DROPPED — the session is inactive then and the head unit's buttons
    // reach nothing, so this is belt and braces against a command queued
    // before the close drained — and so is every command while a PROMPT
    // stands over the player (the load confirmation): the car's buttons are
    // the player's keys, not a question's answer, and a Space that landed on
    // the prompt's focused OK would load a recipe from the wheel.
    //
    // A CAR BUTTON IS NOT A KEYBOARD WALKING A RING, and that is the second
    // pre-filter here beside the state gate below. The player's modal row
    // carries the focus ring, and a bare Space or Enter with a button focused
    // is THAT BUTTON'S press (route_modal_dialog_focus_key claims it before
    // the player's own vocabulary sees it), so a "play" from the wheel would
    // press whatever the ring stood on — Close, and the player would come
    // down. The focus can stand on a button with nobody having walked to it:
    // THE FEINT assigns it passively, a finger pressed on a button and slid
    // off (update_modal_dialog_hover). So the ring is CLEARED before any key
    // is synthesized, in the synthesis road itself, which is what makes the
    // membership exactly "every command kind that synthesizes a key" — SeekTo
    // does not take it, being the direct act below, and a kind that presses
    // nothing (FocusGained, a state-gated no-op) damages nothing. The clear
    // is dispatch_modal_dialog_editor_act's own three writes: the one owner
    // GuiInputHandler::clear_modal_dialog_key_press for an armed key press (a
    // focus that moves cancels the arm — the rule at
    // AppState::modal_dialog_key_pressed), then modal_dialog_focus = -1 and
    // modal_dialog_focus_active = false, damaging the modal box.
    //
    // THE TABLE (design §3, R6): PlayPause -> Space UNCONDITIONALLY (the
    // undivided toggle key, which the sliver maps itself rather than letting
    // the framework split it — gui_media.h; Space is play_button_act's own
    // toggle, so the key and the act say the same thing and no gate belongs
    // between them); Play -> Space ONLY WITH THE TRANSPORT DOWN; Pause,
    // FocusLost and FocusLostTransient -> Space ONLY WITH THE TRANSPORT LIVE
    // (a focus loss pauses, Android's one imposed interrupt); STOP -> THE STOP
    // KEY, unconditionally (R36 gave the player a real stop, so the head
    // unit's stop is no longer a pause; the key names an act rather than a
    // toggle, so no state gate belongs on it and the act's own idle refusal is
    // the answer) — the key being BARE `v` since 2026-08-30, which the one
    // road simply follows: the table names the player's STOP KEY, whatever
    // letter that is; Previous / Next -> Home / End (architect 2026-08-31 —
    // THE PLAYER'S OWN TRANSPORT PAIR, whatever the head unit calls its
    // buttons: the road stays "the car button presses the player's key", and
    // Home's PREVIOUS-TRACK WINDOW is what gives the wheel a real previous-
    // track act, its first three seconds stepping back a file and everything
    // past them restarting this one — the behaviour of the architect's own
    // car. They were Period / Comma from 2026-08-30 and PageDown / PageUp
    // before that);
    // FastForward / Rewind
    // -> Right / Left (5 s per press, nothing depending on repeat);
    // FocusGained -> nothing (NOTHING RECOVERS BY ITSELF — the AAudio
    // posture; the user presses play). The state gate on the DIRECTIONAL
    // play/pause family is a SEMANTIC PRE-FILTER, not a second dispatch:
    // Space is the toggle, and a head unit saying "play" to a live transport
    // must not pause it, so the gate decides whether the toggle is sent and
    // the act still runs through the key road.
    //
    // THE ROAD'S ONE RECORDED ASYMMETRY: SeekTo calls seek_to(frame) DIRECT,
    // because no keysym carries an absolute position — the seeks the keys
    // bind are relative (Left / Right / Home). Its milliseconds are clamped
    // to the item's own length BEFORE the conversion to frames, the arriving
    // position being any int64 a head unit cares to send (the rule at the
    // site).
    //
    // EACH KEY IS A PRESS AND A RELEASE, synthesized back to back: the release
    // is what cancels the core's repeat arm for the repeat-eligible keys
    // (Left / Right — the seeks alone since 2026-08-31, Home and End being
    // absolute and one-shot), exactly as the on-screen keyboard
    // owes its key-up. The stable code is kCarStableCodeBase + the GuiKey;
    // the codepoint is 0 for every transport key.
    void on_media_command(GuiMediaCommand cmd);

    // THE ONE OWNER OF WHAT THE HEAD UNIT SHOWS: builds GuiMediaState from
    // app.render_player and the one position reader (render_player_position)
    // and hands it to GuiPlatform::publish_media_state. THE EDGE INVENTORY,
    // re-derived by grep (EIGHT call sites across SEVEN functions): open() —
    // active, no item, stopped; play_wav's tail and toggle_pause's resume arm
    // — the two writers of the LIVE state, playing; THE STOP BODY'S PLAYER FORK
    // (GuiPlaybackLifecycle::stop_playback_if_playing, through its
    // back-pointer) — the one place every player stop passes, paused, which
    // covers the pause, the natural end's last-wav rest, the dead device and
    // the rebind ahead of the next item; GuiRenderPlayer::stop()'s OWN push in
    // its not-sounding arm (R36) — a transport that has already passed that
    // fork moves its rest to frame 0, which the head unit's clock must see, and
    // the arm that DOES sound needs no second push because the fork just made
    // one; seek_to — both arms, so the head unit's clock stays honest; and
    // close() — inactive. NO PER-TICK PUSH: a
    // playing position advances on the head unit's own clock from the last
    // push at speed 1.0. Title = the item's path relative to the project
    // folder (`tmp/3_bpm/01.wav`, `render/<title>.wav`), artist = the
    // project's name, duration and position in milliseconds at the device's
    // rate (the item is at the device's rate by the decode's own equality).
    void publish_media_state();

private:
    // Rebuild the listing for the live folder: rows, scroll 0, the highlight
    // on the transport's item's row if it is here else row 0, hover and press
    // cleared. Damages the band.
    void rebuild_rows();
    // Enter a folder (Root, Deliverable, Batches, or the batch at `dir`).
    void enter(AppState::RenderPlayer::Folder folder,
               const std::filesystem::path& dir);
    // Decode `path` under the vocabulary above; on success bind it as the
    // item and play it from its start. `folder_wavs` / `index` name the
    // item's folder list and its place in it. Returns whether it played; a
    // refusal has raised its card and changed nothing.
    bool play_wav(const std::filesystem::path& path,
                  const std::vector<AppState::FolderOverlayRow>& folder_wavs,
                  int index);
    // The natural end: the fence through the one stop body, then — with the
    // REPEAT ONE lamp lit — the item again from its start (the sanctioned
    // exception at the head of this file), else the next wav of the item's
    // folder, else the rest at the item's start with the folder-end bit set.
    // THE LAMP'S ARM IS TERMINAL: it returns on a REFUSED replay too, so a
    // lit lamp never falls through to the advance.
    // A NATURAL
    // END IS THE CURSOR REACHING THE END, NOT THE ABSENCE OF A DEVICE
    // (2026-08-28): a Bluetooth drop, a headphone pull and an audio device
    // that never came up all leave the same `playing` flag false, and the
    // tick asks GuiPlayback::device_unavailable FIRST so every one of them
    // pauses in place instead of arriving here and advancing to a wav
    // nothing can play — a whole folder at tick rate, in the never-came-up
    // case.
    void on_natural_end();
    // The wav rows of the live listing, in listing order.
    std::vector<AppState::FolderOverlayRow> listing_wavs() const;
    // Whether any playable wav exists — the opener's refusal.
    bool has_playable_render() const;
    // THE DELIVERABLE — the CURRENT TITLE'S wav in `render/`, present iff it
    // is there as a regular file, and the folder holds nothing else by the
    // time this answers: IT PRUNES FIRST (prune_render_folder, renders_dir.h,
    // whose declaration carries the ruling and the refusals). So `render/` is
    // a ONE-FILE FOLDER in the player — the root row stands iff this answers,
    // the Deliverable listing is `..` plus at most that one row, and the
    // folder's play order, the two ends and Home's previous-track window are
    // the degenerate
    // one-item case of the walks they already were, needing no arm of their
    // own (architect 2026-08-29: "player should only list a file if it matches
    // the current title, and delete the rest also").
    std::optional<std::filesystem::path> deliverable_wav() const;
    // Damage helpers: the band, the modal row.
    //
    // ONE BAND DAMAGE since R35 (2026-08-28): the panel's height is fixed at
    // the slot's ceiling whatever the listing is (folder_overlay.h), so the
    // band a rebuild must erase IS the band every other damage covers — a
    // shorter listing can no longer leave departed rows standing above it. The
    // second helper this file carried for exactly that case went with the
    // growing band, and with it folder_overlay::band_damage_rect, its one
    // reader.
    void damage_band();
    void damage_row();
    // THE PLAYER'S ONE VOICE: a NORMAL notification card (2026-08-29; it was
    // the status chain's transient tier until then). Its callers are the
    // decode road's refusals (the probe's, the allocation ceiling's, the
    // read's, the rate-and-channel equality twice, "This wav holds no
    // samples"), "No audio device; the wav cannot be played" and the opener's
    // "Nothing to play: no renders under render/ or tmp/" — every one a
    // sentence answering an act, none a state. Kept as a thin call rather than
    // deleted so the player's refusals stay one grep.
    void status(const std::string& line);
};
