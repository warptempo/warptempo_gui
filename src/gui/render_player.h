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

// -- THE MODE'S SHARED REFUSAL SENTENCES ARE RETIRED WHOLE -------------------
//
// They were spelled here rather than inside the operations file from
// 2026-08-30's strictness ruling, each being said from SEVERAL acts and two of
// them from a POINTER site as well — the play-scrub's press, which lives with
// the other pointer routers (input_pointer.cpp). The family is silent now and
// the header keeps only its record. Every refusal with a single site still
// keeps its literal where it fires, and the DECODE's own sentences — which
// report a fact no surface shows — still take GuiRenderPlayer::status.
//
// THE IDLE FAMILY WENT SILENT LAST (architect 2026-08-31, R5, the
// one-dimensional rule): kNoPlayerItem ("No render is loaded to play") — Play
// with no item, the two folder-end jumps (which walk the TRANSPORT ITEM's
// folder and so have nothing to walk without one), and every seek road, Home
// and End among them — and kSeekWhileIdle ("Start playback before seeking"),
// which the two seek keys, bare Home, bare End, the car's absolute seek and
// the scrub's own press all met. A BENIGN REFUSAL ALREADY AT ITS STATE SAYS
// NOTHING: the modal row IS the state both name — an empty transport and an
// idle one rest with the clock at zero and the slider at its left end — so the
// row is the answer and a sentence only repeats it. THE REFUSALS THEMSELVES
// ARE UNCHANGED: an idle seek still refuses (R41's dead slider), which is what
// keeps `resume_frame` 0 at every idle rest by construction.
//
// THE ITEM FOLDER'S TWO ENDS went silent earlier that same day, on the same
// reasoning: NOTHING LOOPS, so an end is still an end, but the highlighted row
// rests at the listing's first or last line and one glance answers it. The
// pair kFirstInFolder / kLastInFolder ("This is the first/last render in the
// folder") is deleted with its two raises in first_in_item_folder /
// last_in_item_folder; their other producers, the item's two neighbour walks,
// had already left the product earlier that day when the skips became Home and
// End.

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
// bottom row (folder_overlay.h) lists `tmp/` — its batch folders and their
// cells, THE PLAYER'S WHOLE SUBJECT since 2026-09-01 (the ruling below) — and
// the bottom row's modal carries the transport (THE MAIN WINDOW'S OWN
// TRANSPORT TRIPLE since 2026-09-01 — Home / Play-Pause / End, the same three
// acts on the same three keys as the roster's — then the play-scrub, the
// clock, the Repeat one lamp, the UP button, and Load in place / Close flush
// right — the row's order and faces are the painter's, R25/R36).
//
// THE PLAYER LIVES INSIDE `tmp/` AND NEVER RISES ABOVE IT (architect
// 2026-09-01). HIS RATIONALE: the deliverable in `render/` is a
// NAMING-FOR-SHARING CONVENIENCE OUTSIDE THE GUI'S WORKFLOW — the tablet's
// engine differs from the laptop's by ULPs, so it is never driven from the
// glass, and it carries no sidecars, so it cannot be loaded in place (R15) —
// while `tmp/`'s cells are what the workflow auditions on both hosts. So
// `tmp/` IS THE ROOT: the two-branch root that listed `render` and `tmp` as
// folder rows, the deliverable listing under it and the question that fed them
// (deliverable_wav) are deleted, `AppState::RenderPlayer::Folder` is two
// places, and the prune's LISTING trigger retires with them (its publish
// trigger stays — prune_render_folder, renders_dir.h). AND THE `..` ROW LEAVES
// THE LISTINGS: going up is a BUTTON on the modal row beside Repeat one, its
// act up(), its key twin Backspace unchanged, greying at the root through the
// wall's one owner (render_player_up_actionable). Neither the deliverable's
// PUBLISH road nor the SYNCHRONIZE mirror is touched — the mirror still ships
// `render/`'s contents beside every batch folder; only the PLAYER stops
// looking at it.
// The state it moves is AppState::render_player and AppState::folder_overlay
// (app_state.h, where every field is described); this struct owns the acts.
//
// THE PLAYER'S STOP IS RETIRED WHOLE (architect 2026-09-01, reversing his R8
// keep of 2026-08-31 — "it sounds like stop is actually useful" — once the
// row's symmetry with the main window's transport became the goal): the
// button, its act body, its bare `v` key, its face arm and its tooltip are
// all deleted, and the row is the triple above. It was R36's own (2026-08-28,
// "one button that's either play or pause, and the other one is stop"), and
// what makes it superfluous is what replaced it: Home puts the playing file
// back at its beginning, and Space reads the highlight first (R6) so any file
// starts from its start in one press.
//
// THE TRANSPORT HAS THREE STATES AND THE BUTTONS ANSWER THEM (architect
// 2026-08-28, R36). THE STATE IS STORED, in
// `AppState::RenderPlayer::transport` (that field's block owns the reasons,
// the writer set and the readers) — IDLE (nothing to resume: no item, or an
// item a natural end or a fresh open left resting at its start — NO USER ACT
// PRODUCES IT since the Stop retired), LIVE,
// PAUSED (an item the transport parked, AT WHATEVER FRAME — a pause at frame 0
// is PAUSED, which no reading of the resume point could say) — and the table
// is at play_button_act.
//
// THE MODEL (R1, R2, revised 2026-08-29): the listing is navigated THE
// REGULAR WAY and A CLICK ACTIVATES — a click or tap on a folder row ENTERS
// it, on a wav row PLAYS IT FROM ITS START. GOING UP IS NOT A ROW since
// 2026-09-01: it is the modal row's Up button, with Backspace as its key on
// plastic (the `..` row stood at the top of every non-root listing until
// then). The click's act rides
// the motionless LIFT (the press still arms, the same press being the band's
// possible scroll drag) and the highlight moves onto the row first; Enter is
// the keyboard's own click on the highlight and Up/Down walk the band without
// opening anything. THE PLAY BUTTON READS THE HIGHLIGHT FIRST AND THE
// TRANSPORT SECOND (architect 2026-08-31, R6, narrowing R40's "it never reads
// the highlight, in any state" — the table is at play_button_act). THE
// TRANSPORT'S ITEM is separate from the highlight: it keeps playing while
// the highlight walks and while a folder is entered — up() is the one
// navigation that pauses it, and the reason is at that body — it wears the
// transport glyph on its row, and AUTO-ADVANCE, HOME'S PREVIOUS-TRACK WINDOW
// and the two Shift+Home / Shift+End ENDS walk ITS
// FOLDER'S wav list as it was listed when the item was played — never another
// folder and never a wrap. Every listing is built when its folder is entered
// and never kept fresh.
//
// THE HIGHLIGHT FOLLOWS THE TRANSPORT'S ITEM (architect 2026-08-28, R38,
// superseding the design's "Previous and Next never move the highlight"): at
// every item change THE TRANSPORT MAKES ON ITS OWN — Home's previous-track
// window / the folder's ends / auto-advance — the band moves onto
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
// start, never the folder's next wav. It is the
// whole of the exception: nothing else in the product plays anything twice by
// itself, and the state is session-only (false at every open, serialized
// nowhere).
//
// AT THE FOLDER'S LAST WAV, with the lamp off, the transport stops with the
// item resting at its start AND THAT IS ALL IT MEANS — the next Play replays
// that last file (architect 2026-08-31, R7:
// "we simplify — play on last file means play last file"). THE FOLDER-END
// RESTART IS RETIRED: from 2026-08-28 the bit `ended_at_folder_end` said the
// transport was resting THERE and turned the next Play — the car's at the end
// of a playlist above all — into "start the folder's FIRST wav" (R27); the
// bit, its one writer at the natural end, its one reader in play_button_act
// and its seven clears are all deleted, and the car's Play at that rest now
// replays the last track, which is also the row the band is resting on.
//
// THE ITEM IS A WAV PLAYED AS IT IS: decoded through the in-tree WAV reader
// (wav_read_full, audio_io — called, never changed) after the PROBE has
// confirmed it matches THE PROJECT SOURCE'S rate and channel count (`GuiAudio`,
// which is also what the audio device was opened at, so the two coincide — but
// the test reads the source and that is the one to state; the engine
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
// scanner flag's mirror). THE STOP IS STILL THE ONE STOP BODY: every pause —
// the transport's own, the tick's dead-device arm and, since 2026-09-04, the
// Up act — every natural end, every close and the rebind ahead of the next
// item takes GuiPlaybackLifecycle::stop_playback_if_playing, which carries the
// player's fork inside it (the fence, then the transport moved to PAUSED and
// the modal row damaged instead of the scanner teardown), so the keyboard stop
// rule and the fence-before-rebind ordering hold by construction.
//
// ENTER AND LEAVE. open() is the ONE opener — bare `l`, bare `'` outside the
// `h` view and, since 2026-09-01, ONE icon-row button (Play renders) all reach
// it through on_key; the LOAD IN PLACE button was the second until that day,
// when the architect moved it to the history group, its press out here having
// been Play renders' act under a second name (the record is at its roster
// entry, app_state.h). The KEYS are unchanged, an alias being a keyboard fact
// and the no-second-road doctrine a pointer one — and
// it refuses with "Nothing to play: no renders under tmp/" when
// `tmp/` holds no cell; its callers refuse the modal
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
// road — so the ordinary on_key dispatch runs and there is no second dispatch
// road for keys. THREE COMMAND FAMILIES ACT DIRECT instead of pressing
// anything, and the table at the declaration owns why: SeekTo (no keysym
// carries an absolute position), the DIRECTION-NAMED play/pause family, which
// since R6 must reach the transport past Space's highlight fork
// (transport_toggle_act), and — since the player's Stop key retired
// 2026-09-01 — STOP, which composes that same toggle with a seek to the top;
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
    // it, a wav row plays from its start — the two kinds a listing carries
    // since the `..` row left it (2026-09-01). Its
    // producers are the row click's motionless lift and Enter on the
    // highlight, both through the overlay's one row-act fork.
    void open_row(int index);
    // One folder up — THE MODAL ROW'S UP BUTTON AND BACKSPACE, one act, since
    // the `..` row retired (2026-09-01); a SILENT consumed no-op at the root,
    // which is `tmp/` (the wall's one owner is render_player_up_actionable,
    // app_state.h, which the button's face reads too). Past that wall it
    // pauses a live transport before it enters the root, through the one stop
    // body and with the resume point read as toggle_pause reads it; the
    // reasoning is at the body and nowhere else.
    void up();
    // The widget's three mechanics with the player's damage on top
    // (folder_overlay.h owns the clamps and the scroll-into-view; the
    // overlay's other two contents drive the same mechanics through their own
    // damage): move the highlight by
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
    // THE HIGHLIGHT LEADS AND THE TRANSPORT FOLLOWS (architect 2026-08-31,
    // R6, narrowing R40's "it never reads the highlight, in any state" of two
    // days before). ONE FORK IN FOUR ARMS, in this order:
    //
    //   HIGHLIGHT on a FOLDER row               -> open it (the row's own act).
    //   HIGHLIGHT on a WAV that is NOT the
    //     transport's item                     -> play that row, live or
    //                                             paused or idle alike.
    //   otherwise the TRANSPORT, whose three states are STORED in
    //   AppState::RenderPlayer::transport — LIVE the sounding one, PAUSED an
    //   item the transport parked AT WHATEVER FRAME (a pause that caught the
    //   cursor at 0 answers PAUSED here and resumes its own item), IDLE
    //   everything with nothing to resume:
    //     LIVE    -> pause it.
    //     PAUSED  -> resume it.
    //     IDLE with an item -> the item from ITS START, ALWAYS (architect
    //                        2026-08-29 ~01:40, Audacious: Play when idle
    //                        starts literally) — the folder's end included
    //                        since R7. A seek while idle is a consumed no-op
    //                        (seek_to's own head, the one owner), so
    //                        `resume_frame` is always 0 at an idle rest and
    //                        every rest the transport's own acts leave it in —
    //                        a natural end, a fresh bind — plays from
    //                        frame 0.
    //     IDLE with no item -> nothing.
    //
    // THE THREE ROW ARMS RUN open_row, the row click's and Enter's own body,
    // so the acts have ONE owner and this fork walks no listing of its own;
    // the question "which row would Space open" is
    // render_player_highlight_act_row (app_state.h), shared with the button's
    // face and its glyph.
    // R40'S BUG CANNOT RETURN, which is what makes the narrowing safe: it was
    // a band left BEHIND the transport (architect 2026-08-28, R36 — a Next
    // advanced the item but not the band, and the live button then PLAYED the
    // row still highlighted behind it instead of pausing what sounded), and
    // THE BAND FOLLOWS THE ITEM since R38, so a highlight anywhere else is one
    // the user walked there deliberately.
    // ENTER AND SPACE DIFFER IN EXACTLY ONE CASE: on the transport's own item
    // with a session standing, Enter (the click act) restarts it from 0 while
    // Space toggles it; everywhere else the two agree.
    void play_button_act();
    // THE TRANSPORT'S OWN TOGGLE — play_button_act's tail PAST the highlight
    // fork, in a body of its own since 2026-08-31 (the round-B conversion). It
    // is the three-arm table above (LIVE pause / PAUSED resume / IDLE the item
    // from its start, and nothing with no item) and reads no highlight at all.
    //
    // TWO CALLERS, and the split between them is by NAME: play_button_act
    // calls it as its tail, so Space, the Play button and the car's undivided
    // PlayPause keep the whole highlight-driven act; and on_media_command's
    // DIRECTION-NAMED arms — Play, Pause, FocusLost, FocusLostTransient —
    // call it DIRECTLY instead of synthesizing Space, because a direction is a
    // claim about the transport alone. Without that split R6's highlight arm
    // would have let a focus loss START a walked-to row instead of pausing
    // what sounds (R40's bug from the car's side). It is a direct act like
    // SeekTo, and like SeekTo it clears no modal ring — the ring clear belongs
    // to the key-synthesis lambda, whose membership is every kind that presses
    // a key and no other.
    void transport_toggle_act();
    // Pause a live transport (the resume point is the engine's own position)
    // or resume a paused one; a no-op with no item.
    void toggle_pause();
    // (STOP STOOD HERE, R36's own act — the transport to IDLE with the item
    // resting at its start, the resume point cleared to 0 and the item itself
    // untouched, a consumed no-op with no item and on an item already there.
    // It is RETIRED WHOLE, architect 2026-09-01; the record is at the head of
    // this file. Its three roads — the row's button, bare `v` and the head
    // unit's Stop — went with it, the last of them becoming the pause-then-
    // home composition at on_media_command's own arm.)
    //
    // THE ITEM FOLDER'S ENDS (R37, Shift+Home / Shift+End since 2026-08-31 and
    // the two skip buttons' shift-click or long press): the first / last wav of
    // `item_folder`, played from its start on the play road — never
    // a wrap, and a SILENT refusal with no item AND on an item already at that
    // end (both went silent on 2026-08-31, the end's own first and the no-item
    // arm with the idle family that evening — the record is at the header's
    // retired sentences). They are the folder walk's whole surface now: the item's two
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
    // item's own start and takes every refusal seek_to owns, the no-item arm
    // and the idle arm alike — both silent since 2026-08-31 (R5). It never
    // reports a folder wall either: at the first
    // entry the restart IS the act. THE FORK IS ONE OWNER since 2026-09-01,
    // render_player_home_takes_previous (app_state.h), which the button's
    // hint reads too ("Previous file" / "Go to start").
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
    // takes it from there, unaltered (advance where a next entry exists, an
    // ordinary idle rest on the item at the folder's last, a replay under a
    // lit Repeat
    // one); a PAUSED one moves its rest, which the resume arm reads as at-or-
    // past the end and replays from the start; an IDLE one meets seek_to's own
    // silent refusal, exactly as Home does.
    void end();
    // Left / Right's step: 5 s at the project source's rate (R6).
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
    // membership exactly "every command kind that synthesizes a key" — the
    // DIRECT acts do not take it (SeekTo, the directional family's
    // transport toggle since the round-B conversion, and Stop since the
    // player's stop key retired 2026-09-01), pressing no button
    // because they press no key, and a kind that presses nothing (FocusGained,
    // a state-gated no-op) damages nothing. The clear
    // is dispatch_modal_dialog_editor_act's own three writes: the one owner
    // GuiInputHandler::clear_modal_dialog_key_press for an armed key press (a
    // focus that moves cancels the arm — the rule at
    // AppState::modal_dialog_key_pressed), then modal_dialog_focus = -1 and
    // modal_dialog_focus_active = false, damaging the modal box.
    //
    // AN UNDIVIDED COMMAND TAKES THE WHOLE ACT, A DIRECTION-NAMED ONE TAKES
    // THE TRANSPORT ALONE (2026-08-31, the round-B conversion). Until R6 the
    // two were the same thing, Space having been a transport-only toggle; R6
    // put the highlight in front of it, so a Pause or a focus loss sent as a
    // synthesized Space would open a folder or start a walked-to row instead
    // of pausing what sounds. PlayPause, which says "the other one", still
    // takes the key and inherits every act Space has; Play, Pause and the two
    // focus losses call transport_toggle_act DIRECT — the transport's own
    // three-arm tail, past the highlight — joining SeekTo as a direct act with
    // no keysym behind it and, like SeekTo, taking no ring clear.
    //
    // THE TABLE (design §3, R6): PlayPause -> Space UNCONDITIONALLY (the
    // undivided toggle key, which the sliver maps itself rather than letting
    // the framework split it — gui_media.h; Space is play_button_act whole, so
    // the key and the act say the same thing and no gate belongs between
    // them); Play -> THE TRANSPORT TOGGLE ONLY WITH THE TRANSPORT DOWN; Pause,
    // FocusLost and FocusLostTransient -> THE TRANSPORT TOGGLE ONLY WITH THE
    // TRANSPORT LIVE (a focus loss pauses, ALWAYS, Android's one imposed
    // interrupt); STOP -> PAUSE AND THEN HOME, direct (architect 2026-09-01,
    // with the player's own Stop act retired: a live transport takes the
    // directional pause, and a session standing after it takes seek_to(0) —
    // never bare Home's act, whose previous-track window would change TRACKS
    // on a head unit's Stop. Audibly it is the old Stop: silence now, the top
    // on the next Play; what differs is the state left behind, PAUSED rather
    // than IDLE, so the scrub stays live under it. R36 had mapped this to the
    // player's stop KEY, and before R36 it was a plain pause);
    // Previous / Next -> Home / End (architect 2026-08-31 —
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
    // play/pause family is a SEMANTIC PRE-FILTER, not a second dispatch: the
    // transport toggle is a toggle, and a head unit saying "play" to a live
    // transport must not pause it, so the gate decides whether the act runs.
    //
    // THE ROAD'S DIRECT ACTS: the directional family's transport toggle
    // above, Stop's composition of that toggle with seek_to(0), and SeekTo,
    // which calls seek_to(frame) DIRECT because no keysym
    // carries an absolute position — the seeks the keys
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
    // re-derived by grep at each retell (SEVEN call sites across SIX
    // functions; it was eight across seven until GuiRenderPlayer::stop() went
    // with the player's Stop on 2026-09-01): open() —
    // active, no item, stopped; play_wav's tail and toggle_pause's resume arm
    // — the two writers of the LIVE state, playing; THE STOP BODY'S PLAYER FORK
    // (GuiPlaybackLifecycle::stop_playback_if_playing, through its
    // back-pointer) — the one place every player stop passes, paused, which
    // covers the pause, the Up act, the natural end's last-wav rest, the dead
    // device and the rebind ahead of the next item; seek_to — both arms, so
    // the head unit's clock stays honest (which is also what publishes the
    // car Stop's seek to the top, that command being a pause and then this
    // seek); and
    // close() — inactive. NO PER-TICK PUSH: a
    // playing position advances on the head unit's own clock from the last
    // push at speed 1.0. Title = the item's path relative to the project
    // folder (`tmp/3_bpm/01.wav` — the player lists `tmp/` alone), artist = the
    // project's name, duration and position in milliseconds at the project
    // source's rate (the item is at that rate by the decode's own equality).
    void publish_media_state();

private:
    // Rebuild the listing for the live folder: rows, scroll 0, the highlight
    // on the transport's item's row if it is here else row 0, hover and press
    // cleared. Damages the band.
    void rebuild_rows();
    // Enter a folder: the Root, which IS `tmp/`, or the batch at `dir`.
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
    // folder, else the ordinary idle rest at the item's start (R7 — the
    // folder's end is no longer a state of its own).
    // THE LAMP'S ARM IS TERMINAL: it returns on a REFUSED replay too, so a
    // lit lamp never falls through to the advance.
    // A NATURAL
    // END IS THE CURSOR REACHING THE END, NOT THE ABSENCE OF A DEVICE
    // (2026-08-28): a Bluetooth drop, a headphone pull and an audio device
    // that never came up all leave the session word's playing bit down, and the
    // tick asks GuiPlayback::device_unavailable FIRST so every one of them
    // pauses in place instead of arriving here and advancing to a wav
    // nothing can play — a whole folder at tick rate, in the never-came-up
    // case.
    void on_natural_end();
    // The wav rows of the live listing, in listing order.
    std::vector<AppState::FolderOverlayRow> listing_wavs() const;
    // Whether any playable wav exists — the opener's refusal.
    bool has_playable_render() const;
    // (THE DELIVERABLE'S QUESTION STOOD HERE — deliverable_wav, the CURRENT
    // TITLE'S wav in `render/`, which PRUNED the folder before it answered
    // (architect 2026-08-29: "player should only list a file if it matches the
    // current title, and delete the rest also") and made `render/` a ONE-FILE
    // FOLDER in the player. IT IS DELETED WHOLE with its three callers'
    // reasons, architect 2026-09-01: THE PLAYER LIVES INSIDE `tmp/` and never
    // lists `render/` at all — the ruling and his rationale are at the head of
    // this file and at the listing itself. The PRUNE stays with its other
    // trigger, the deliverable's publish.)
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
    // samples"), "No audio device to play the wav" and the opener's
    // "Nothing to play: no renders under tmp/" — every one a
    // sentence answering an act, none a state. Kept as a thin call rather than
    // deleted so the player's refusals stay one grep.
    void status(const std::string& line);
    // The decode road's refusals go through this one instead (2026-09-02):
    // the same card, plus the stderr line naming the wav's FULL path beside
    // the reader's words — the two clauses of one failure (failure.h). The
    // contract is at the definition.
    void refuse_decode(const std::filesystem::path& path,
                       const std::string&           words);
};
