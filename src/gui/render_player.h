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

// -- THE MODE'S BENIGN REFUSALS ARE SILENT ------------------------------------
//
// A BENIGN REFUSAL ALREADY AT ITS STATE SAYS NOTHING (architect 2026-08-31,
// the one-dimensional rule). THE IDLE ONES — Play with no item, the two
// folder-end jumps (which walk the TRANSPORT ITEM's folder and so have
// nothing to walk without one), and every seek road (the car's seek, the
// car's absolute seek, the scrub's own press, bare Home's reseek to the
// item's own start past the previous-track window): the modal row IS the
// state — an idle transport, which is an empty one (IDLE is no item bound,
// the invariant at AppState::RenderPlayer::transport), rests with the clock
// at zero and the slider at its left end — so the row is the answer and a
// sentence would only repeat it. THE REFUSAL ITSELF STANDS: a seek with no
// item refuses (the dead slider). THE ITEM FOLDER'S TWO ENDS, on the same
// reasoning: NOTHING LOOPS, so an end is an end, and the highlighted row
// resting at the listing's first or last line answers it at a glance.
// Every refusal with a single site keeps its literal where it fires, and
// the DECODE's own sentences — which report a fact no surface shows — take
// GuiRenderPlayer::status.

// THE PREVIOUS-TRACK WINDOW (architect 2026-08-31: "agree on
// <3s"): bare Home and its skip button reach it,
// and inside the item's first three seconds the act is THE PREVIOUS ENTRY
// rather than this track's start. It is the iPod / car-head-unit convention,
// and it is POSITION-BASED RATHER THAN PRESS-TIMED on purpose: a position
// window makes a SECOND press "previous" at any press speed at all, since the
// first one landed the cursor at 0, where a double-press window (kHoldBeatMs
// included) would be outrun by a slow press.
//
// IT IS ASKED IN EVERY TRANSPORT STATE by home() — at IDLE there is no item
// and so no file before it (the invariant at AppState::RenderPlayer::
// transport), and the seek arm under it is seek_to's silent no-item
// refusal. Its one owner is render_player_inside_previous_window
// (app_state.h), which render_player_home_takes_previous composes for the
// file step. THE CAR'S PREVIOUS DOES NOT REACH IT: while an item sounds that
// press is the car's seek (architect 2026-11-25, car_previous()).
//
// IT IS AN AUTHORED DURATION AND NEVER SCALES — gui_scale is a LENGTH axis
// (the scaled_px family) and no duration in the product reads it. It is
// converted to frames at the DEVICE's rate where it is used, the item being
// at that rate by the decode's own equality.
inline constexpr int64_t kPlayerPreviousThresholdMs = 3000;

// THE RENDER PLAYER (architect design 2026-08-28, the in-app player for the
// car) — the operations cluster for the MODE that plays the project's own
// renders through the one playback engine: the folder overlay — THE MODAL
// FAMILY'S BOX (folder_overlay.h, modal_box.h: from the trim lane's top row
// down, its first row under the list's head pad at the view's
// top, no folder-name header)
// — lists `tmp/` — its batch folders and their
// cells, THE PLAYER'S WHOLE SUBJECT (the ruling below) — and
// the family's CONTROLS ROW under it (the bottom icon row's rows, its
// outermost faces flush with the pane's outer columns, the well's) carries
// the transport (THE MAIN WINDOW'S OWN
// TRANSPORT TRIPLE — Home / Play-Pause / End, the same three
// keys as the roster's, the right one THE NEXT TRACK (home() /
// next_track(), the pair's contract at their declarations)) FLUSH RIGHT
// (architect 2026-11-10: the transport keys
// to the right, everything else to the left) — the Repeat one lamp, the
// highlight pair, the UP button, Load in place and Delete at the row's left
// (both in both lists, architect 2026-11-25; no
// Close: architect 2026-11-11, the lit PLAYER key is the
// way out), then the play-scrub and the
// clock — the row's order and faces are the painter's, ruled 2026-08-28).
//
// THE PLAYER LIVES INSIDE `tmp/` AND NEVER RISES ABOVE IT (architect
// 2026-09-01). HIS RATIONALE: the deliverable in `render/` is a
// NAMING-FOR-SHARING CONVENIENCE OUTSIDE THE GUI'S WORKFLOW — the tablet's
// engine differs from the laptop's by ULPs, so it is never driven from the
// glass, and it carries no sidecars, so it cannot be loaded in place
// (2026-08-28) — while `tmp/`'s cells are what the workflow auditions. So
// `tmp/` IS THE ROOT: `AppState::RenderPlayer::Folder` is two places, and
// NO LISTING CARRIES A `..` ROW: going up is a BUTTON on the modal row
// beside Repeat one, its act up(), its key twin Backspace, greying at the
// root through the wall's one owner (render_player_up_actionable). The
// deliverable's PUBLISH road and its prune (prune_render_folder,
// renders_dir.h) are the archival render's; the PLAYER does not look at
// `render/`. The state it
// moves is AppState::render_player and AppState::folder_overlay (app_state.h,
// where every field is described); this struct owns the acts.
//
// THE PLAYER HAS NO STOP (architect 2026-09-01, the row's symmetry with the
// main window's transport the goal): the row is the triple above. Home puts
// the playing file back at its beginning, and Space reads the highlight
// first (2026-08-31) so any file starts from its start in one press.
//
// THE TRANSPORT HAS THREE STATES AND THE BUTTONS ANSWER THEM (architect
// 2026-08-28). THE STATE IS STORED, in
// `AppState::RenderPlayer::transport` (that field's block owns the reasons,
// the writer set and the readers) — IDLE (nothing to resume: no item bound,
// the invariant at that field, a fresh open's or an unload's — THE UP ACT IS
// THE ONE USER ROAD INTO IT, 2026-09-04), LIVE,
// PAUSED (an item the transport parked, AT WHATEVER FRAME — a pause at frame 0
// is PAUSED, which no reading of the resume point could say — the natural
// end's rest at the item's end included) — and the table is at
// play_button_act.
//
// THE MODEL (2026-08-28, 2026-08-29): the listing is navigated THE
// REGULAR WAY and A CLICK ACTIVATES — a click or tap on a folder row ENTERS
// it, on a wav row PLAYS IT FROM ITS START. GOING UP IS NOT A ROW
// (2026-09-01): it is the modal row's Up button. The click's act rides
// the motionless LIFT (the press still arms, the same press being the band's
// possible scroll drag) and the highlight moves onto the row first; the
// row's Highlight Next / Highlight Previous buttons (architect 2026-10-10)
// walk the band without opening anything, move_highlight(+1 / -1). THE PLAY BUTTON READS THE HIGHLIGHT FIRST AND THE
// TRANSPORT SECOND (architect 2026-08-31 — the table is at
// play_button_act). THE
// TRANSPORT'S ITEM is separate from the highlight: it keeps playing while the
// highlight walks and while a folder is entered — up() is the one navigation
// that unloads it, and the reason is at that body — it wears the transport
// glyph on its row, and AUTO-ADVANCE, THE NEXT-TRACK SKIP, HOME'S
// PREVIOUS-TRACK WINDOW and the two Shift+Home / Shift+End ENDS walk ITS
// FOLDER'S wav list as it was listed when the item was played — never another
// folder and never a wrap — less any render the player's own Delete has
// removed since (delete_render strikes it, so nothing walks into a file that
// is gone). Every listing is built when its folder is entered or a delete
// changes it, and never kept fresh otherwise.
//
// THE HIGHLIGHT FOLLOWS THE TRANSPORT'S ITEM (architect 2026-08-28): at
// every item change THE TRANSPORT MAKES ON ITS OWN — Home's previous-track
// window / the folder's ends / auto-advance — the band moves onto
// the new item's row and scrolls it into view WHEN THAT ROW IS IN THE LIVE
// LISTING, and stays where it is when the user has navigated elsewhere. Its
// one owner is play_wav, which is the one place the item changes, so the rule
// covers the user's own plays for free (the row is already the highlight
// there; Play on the controls row brings it into view if it was dragged out)
// and needs no membership list — BUT THE ROW'S TAP, whose lift has already
// seated the band on the tapped row and whose play seats nothing and scrolls
// nothing (THE PRESS NEVER SCROLLS, architect 2026-11-21; play_wav's
// `tapped`). A USER'S OWN HIGHLIGHT MOVES ARE
// UNTOUCHED — nothing here fights the band back onto the item, which is why
// THE SEAT IS GATED ON THE CHANGE and not run at every call: the REPEAT ONE
// replay re-enters play_wav on the item already resting there at every
// natural end, and an ungated seat would drag the band off the row a user
// walked to under a lit lamp — repeatedly, and under his next Load in place.
//
// AND THE UP ACT SEATS THE BAND ON THE FOLDER IT JUST LEFT (architect
// 2026-09-11): the root listing a press of Up builds holds no item, so the
// seat rule's second arm takes it — "I'm pressing Up on a specific folder,
// and I expect that folder to be highlighted", which is the standing gesture
// and not a remembered place. ENTERING a folder names nothing to seat on and
// stays memory-less either way (the arms are at rebuild_rows).
//
// NOTHING LOOPS, WITH TWO SANCTIONED EXCEPTIONS — this one, REPEAT ONE
// (architect 2026-08-28), and the car's loop of the trim on the PROJECT
// transport with this player closed (GuiCarTransport, architect 2026-09-17,
// which reaches no player audio): the player's lamp is a two-state toggle, off or repeat
// the ONE item ("the user can just press play once the playlist finishes...
// repeat one is much more useful", which is why there is no repeat-all), and
// while it stands THE NATURAL END REPLAYS THE ITEM FROM ITS START through the
// player's own play road instead of advancing — AND IT OUTRANKS THE ADVANCE
// WHETHER THE REPLAY SOUNDS OR REFUSES: a replay that cannot decode (the file
// deleted or republished in another shape while it played) leaves its own
// words on a notification card and the transport resting on that item at its
// start, never the folder's next wav. It is the
// whole of the player's exception: nothing else in the product plays anything
// twice by itself but the car's loop, and the state is session-only (LIT at
// every open, architect 2026-09-11 — his default is repeat the one item,
// the button's name and glyph kept rather than inverting the lamp into a
// "play through" — and
// serialized nowhere; the open is its one reset, so it forgets at the close
// and survives everything in between). THE LAMP GOVERNS THE NATURAL END
// ALONE: a deliberate Next (next_track) walks to the folder's next wav under
// a lit lamp exactly as under a dark one, because a press is not a file
// reaching its end (architect 2026-09-04).
//
// AT THE END IT CONTINUES IF THERE IS A NEXT TRACK AND STOPS DEAD WHERE IT IS
// OTHERWISE (architect 2026-10-26, "advance to the next one if
// possible"; the score viewer's one rule): AT THE FOLDER'S LAST WAV, with the
// lamp off, the transport stops PAUSED with the item resting AT ITS END — the
// clock at its length, the thumb at the right — AND THAT IS ALL IT MEANS: the
// next Play replays that last file from its start (architect 2026-08-31: "we
// simplify — play on last file means play last file"; resume()'s end rule,
// which continues to the next wav where one exists, a paused seek to the end
// being the other road to that rest). A scrub dropped at the end of a live
// item reaches the natural end too (the lift resumes at the drop through the
// live seek's clamp, never the PLAY key's end rule — resume_at,
// input_pointer.cpp's play-scrub block). The car's Play at that rest
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
// launch page-in, the waveform damage — belongs to the project's WAVEFORM,
// which the player does not display. The project's resting playhead does not
// move while the player plays, its scanner never runs, and the item's domain
// is the decoded buffer's own [0, frames). So play_item / resume / seek call
// playback.play directly against that domain, and the state that says the
// transport is live is the player's own (`transport`'s LIVE value, the
// scanner flag's mirror). THE STOP IS STILL THE ONE STOP BODY: every pause —
// the transport's own and the tick's dead-device arm — every natural end,
// every close, the Up act's unload and the rebind ahead of the next
// item takes GuiPlaybackLifecycle::stop_playback_if_playing, which carries the
// player's fork inside it (the fence, then the transport moved to PAUSED and
// the modal row damaged instead of the scanner teardown), so the keyboard stop
// rule and the fence-before-rebind ordering hold by construction.
//
// ENTER AND LEAVE. open() is the ONE opener — bare `l` and ONE top icon row
// button (Play renders) both reach
// it through on_key, and A COMPLETE BATCH reaches it at its
// terminal: the progress page that stood from the Render press is replaced
// by the player on the batch's folder — the newest, which is the open's own
// seat below — with the held press dropped first
// (GuiInputHandler::swap_batch_progress_for_player; architect 2026-10-14).
// The LOAD IN PLACE button lives in the history group (its roster
// entry, app_state.h). It refuses with "Nothing to play: no renders under tmp/" when
// `tmp/` holds no cell and (2026-09-15) with
// kRenderPlayerWhileRenderingCard while a render run stands (the close's
// re-express would kill the run — app_state.h's load_in_place_render_blocked);
// its callers refuse the modal
// states (a prompt, an
// editor, the `h` view, loading, no source) before it is asked. THE OPEN
// ENTERS THE NEWEST BATCH FOLDER (architect 2026-09-15): the highest
// leading-index folder under tmp/ (max_renders_batch_index, renders_dir.h —
// never a filesystem timestamp), the root listing the fallback when there is
// none or the newest holds no playable wav; either way NO ROW IS LIT
// (folder_overlay.h's head, architect 2026-11-27), entering being
// memory-less, and the listing rests at its top. The open takes the
// modal-open stop, the mode bit, a fresh modal session, that one listing and
// a whole-window damage. close() takes the panel down around
// unload_item(), which is THE ONE OWNER of the ordering the engine's pointer
// demands — the stop body's fence, THE VIEW'S buffer rebound through the S/T
// flip's own tail fork verbatim (ensure_ready in target view, rebind_to_source
// in source view), and only THEN the item's fields cleared and its buffer
// freed. The mode bit travels inside that body, because the two ends of it
// want opposite answers: the fence needs the player active and the close's
// re-express needs it down, so the unload takes a required tail
// (UnloadTail::Up / ::Close) and lowers the bit between them. up() calls the
// same body past its root wall with the Up tail, which is why going up leaves
// the player standing with nothing loaded, exactly as an open leaves it.
//
// THE LOAD ROAD is not here: the Load in place button and its confirmation
// live on GuiInputHandler, which owns the shared
// act (load_render_entry_in_place); this cluster answers only which entry is
// highlighted (highlighted_entry) and closes on the act's success.
//
// THE CAR IS ITS OWN INTERFACE (architect 2026-09-12, from the car). It
// reaches this cluster through the seam's two members (platform_android.h owns
// their contracts): the head unit's buttons arrive as GuiMediaCommand values
// through main.cpp's hook into on_media_command, and EVERY ONE OF THEM IS A
// DIRECT ACT ON THIS CLUSTER — no key is synthesized and no dispatch is run.
// The car's vocabulary is THE CAR'S, not a replay of the tablet's keys: three
// buttons on the wheel and the console — rewind, play/pause, fast-forward —
// where the wheel's outer two arrive as Previous and Next, so THE ONE BUTTON
// IS A TOGGLE between the item and silence (car_toggle) and THE OUTER TWO ARE
// A SEEK while an item sounds and THE PLAYLIST WALK with the up-a-folder exit
// at rest (car_previous / car_next; architect 2026-11-25). The table at
// on_media_command owns each arm.
//
// AND THE DISPLAY IS A DUMMY: the session says PLAYING with a running clock
// whenever the player stands, because a console that hears the silent
// Bluetooth link believes it is playing and overrides a PAUSED session —
// publish_media_state is the owner of what the head unit shows WHILE THIS
// PLAYER STANDS, called at every edge where that changes, and its title rule
// is at that declaration. It is ONE OF TWO OWNERS (2026-09-17): with the
// player closed the head unit belongs to GuiCarTransport, which drives the
// project's own transport and publishes its three lines from a per-tick
// comparator, and main.cpp's hook forks the commands between the two on this
// player's mode bit. The platform is held for exactly those two calls.
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
    // refusal has already raised its card or has nothing to say. Its roads:
    // on_key's (bare `l`, the Play renders lift) and a complete
    // batch's terminal (swap_batch_progress_for_player, 2026-10-14).
    bool open();
    // THE CLOSER (the contract above). A no-op when the mode is down.
    //
    // ITS CAUSES, FOUR CALL SITES (the modal row has no Close button:
    // architect 2026-11-11):
    // bare `l` in the mode's key router (route_render_player_key), which the
    // top icon row's Play renders lift spells (the player's one chord, architect
    // 2026-10-30, its one way out on glass); the load road's success;
    // a folder delete leaving no folder
    // (delete_batch_folders, architect 2026-10-01, which a render's delete
    // reaches for a folder's last render); and THE CLOSE ROAD — GuiPrompt::request_close, which
    // takes the mode down at its head, so the platform's close
    // (Back; main.cpp's set_on_close)
    // leaves the ordinary window standing before the unsaved-work
    // question is asked, and a Cancel answers over no player.
    void close();

    // -- The listing --------------------------------------------------------

    // THE OPEN ACT on row `index` of the live listing: a folder row enters
    // it, a wav row plays from its start — the two kinds a listing carries
    // (2026-09-01). Its
    // producers are the row tap's motionless lift,
    // through the overlay's one row-act fork (folder_overlay_open_row), and
    // the Play button's two row arms (play_button_act, below; the car's
    // toggle reaching them past its LIVE arm). `tapped` says which: true
    // from the row's tap alone, whose play leaves the list's scroll where the
    // tap found it (play_wav's band seat).
    void open_row(int index, bool tapped);
    // One folder up — THE MODAL ROW'S UP BUTTON (2026-09-01), and THE CAR'S
    // OWN PREVIOUS AT REST: car_previous() composes this body where its
    // origin is the first row (car_origin_row), so the act has TWO roads on ONE body (the button and
    // car_previous()'s rest arm; its live arm is a seek, architect
    // 2026-11-25). The highlight pair is the
    // plain band walk and the Home button the previous-track act, so leaving
    // a folder is the Up button's act and the car's (the architect's reasons
    // are at car_previous()).
    // A SILENT consumed no-op at the root, which is `tmp/` (the wall's one
    // owner is render_player_up_actionable, app_state.h, which the button's
    // face reads too). Past that wall it UNLOADS THE ITEM (unload_item) and
    // re-enters the root, so the player stands as a fresh open leaves it —
    // nothing bound, nothing sounding — but for the band, which it seats on
    // the folder it just left (rebuild_rows' seat rule, architect
    // 2026-09-11). The reasoning is at the body and nowhere else.
    void up();
    // A FOLDER DELETE, ANSWERED (architect 2026-09-29; from inside a folder
    // 2026-11-25) — the body the RENDER_PLAYER_DELETE_CONFIRM prompt's Delete
    // runs over parked folders, through
    // GuiInputHandler::confirm_render_player_delete, and the body a render's
    // delete hands a folder's last render to (delete_render). `folders` are
    // the paths the raise parked off the listing's own rows: the root's
    // Folder rows, or the listed folder itself from inside it. A BOUND ITEM
    // LIVING IN A FOLDER ABOUT TO GO IS UNLOADED FIRST (unload_item's Up
    // tail, up()'s own road), so no transport, scrub, repeat or car act
    // reads a file that is gone — from inside a folder the item, when one is
    // bound, always lives there (play_wav binds from the listed folder alone,
    // and up() unloads on the way out); at the root none is bound. Each
    // folder is removed
    // whole, BUT ONLY A DIRECTORY STANDING DIRECTLY UNDER THIS PROJECT'S
    // `tmp/` (project_batch_root) — anything else is refused on stderr and
    // left alone, so nothing outside `tmp/` and never `tmp/` itself can go;
    // a filesystem failure cards CRITICAL naming the folder by its name, with
    // the full path and the system's words on stderr (failure.h's two
    // clauses), and the walk goes on to the next folder. The ROOT listing is
    // entered and rebuilt from disk after, whatever failed, NO ROW LIT
    // (architect 2026-11-27: "None" — after a delete, no selection;
    // rebuild_rows' seat names no folder), the folder list's scroll kept
    // where it stood (clamped) for a delete made there, the list at its top
    // for one made from inside a folder. A success is silent: the rows
    // leaving the listing are its answer — and when the rebuilt root lists
    // no folder at all, the player closes itself through close(), its own
    // close act (architect 2026-10-01; the rule is at the body).
    void delete_batch_folders(const std::vector<std::filesystem::path>& folders);
    // ONE RENDER'S DELETE, ANSWERED (architect 2026-11-25: "need way to delete
    // single render in the player") — the body the
    // RENDER_PLAYER_DELETE_CONFIRM prompt's Delete runs over a parked render,
    // through GuiInputHandler::confirm_render_player_delete. `entry` is the
    // highlighted row's own entry in the listed folder's render list.
    // THE FOLDER'S LAST RENDER IS THE FOLDER'S DELETE: with one render listed
    // (render_player_batch_render_count) the body is delete_batch_folders on
    // the entry's folder, the emptied folder going with it and the player
    // going up to the folder list (or closing with no folder left).
    // OTHERWISE: A BOUND ITEM THAT IS THIS RENDER IS UNLOADED FIRST
    // (unload_item's Up tail, up()'s road), so nothing reads the file that
    // goes; a bound item that is ANOTHER render stays bound, paused as the
    // question's rise left it, and the removed render is struck from its
    // item folder (`item_folder` / `item_index`), so no next track, folder
    // end or auto-advance walks into it. The render's whole file set goes
    // (batch_cell_file_set, renders_dir.h, the cell's one enumeration): a
    // file already missing is silent, a removal that fails cards CRITICAL
    // naming the render by its entry id with the system's words, the full
    // path on stderr (failure.h's two clauses), once for the render. The
    // folder's listing is rebuilt from disk after, whatever failed, and NO
    // ROW IS LIT (architect 2026-11-27: "None" — after a delete, no
    // selection, as a file manager leaves it), the list's scroll kept where
    // it stood, clamped onto the shorter listing.
    void delete_render(const AppState::RenderEntry& entry);
    // The widget's two mechanics with the player's damage on top
    // (folder_overlay.h owns the clamps and the scroll-into-view; the
    // overlay's other five contents drive the same mechanics through their own
    // damage): move the highlight by
    // `delta` rows, and seat it on `index` — the click's first half, the open
    // being its second, the list not scrolled (folder_overlay::
    // seat_tapped_row: the press never scrolls, architect 2026-11-21).
    void move_highlight(int delta);
    void set_highlight(int index);
    // The highlighted row's render entry — non-null exactly when the highlight
    // is a LOAD-CAPABLE wav (a batch cell). The load road's one question.
    const AppState::RenderEntry* highlighted_entry() const;

    // -- The transport ------------------------------------------------------

    // THE PLAY BUTTON'S ACT (and the car's toggle past its LIVE arm; no key
    // reaches it, the player's one chord being its opener's, architect
    // 2026-10-30).
    // THE HIGHLIGHT LEADS AND THE TRANSPORT FOLLOWS (architect 2026-08-31).
    // ONE FORK, in this order:
    //
    //   HIGHLIGHT on a FOLDER row               -> open it (the row's own act).
    //   HIGHLIGHT on a WAV that is NOT the
    //     transport's item                     -> play that row, live or
    //                                             paused or idle alike.
    //   NO ROW LIT inside a folder             -> the same two arms on the
    //                                             bound item's row where it
    //                                             is listed (the transport's
    //                                             own: the toggle below, the
    //                                             row lit first,
    //                                             seat_band_on_listed_item),
    //                                             else on the listing's FIRST
    //                                             RENDER
    //                                             (render_player_band_row,
    //                                             architect 2026-11-27).
    //   otherwise the TRANSPORT, whose three states are STORED in
    //   AppState::RenderPlayer::transport — LIVE the sounding one, PAUSED an
    //   item the transport parked AT WHATEVER FRAME (a pause that caught the
    //   cursor at 0 answers PAUSED here and resumes its own item), IDLE no
    //   item bound (the invariant at the field):
    //     LIVE    -> pause it.
    //     PAUSED  -> resume it — at the item's end (the natural end's rest)
    //                the next wav of its folder where one exists, this item
    //                from its start otherwise or under a lit REPEAT ONE
    //                (resume()'s end rule, architect 2026-10-26).
    //     IDLE    -> nothing.
    //
    // THE THREE ROW ARMS RUN open_row, the row tap's own body, so the acts
    // have ONE owner and this fork walks no listing of its own; the question
    // "which row would Play open" is render_player_highlight_act_row
    // (app_state.h), shared with the button's face and its glyph.
    // THE BAND FOLLOWS THE ITEM (architect 2026-08-28), so a highlight
    // anywhere else is one the user walked there deliberately — never a row
    // left behind the transport that the live button would play instead of
    // pausing what sounds.
    // THE ROW TAP AND PLAY DIFFER IN EXACTLY ONE CASE: on the transport's own
    // item with a session standing, the row's tap (open_row) restarts it from
    // 0 while Play toggles it; everywhere else the two agree.
    void play_button_act();
    // THE TRANSPORT'S OWN TOGGLE — play_button_act's tail PAST the highlight
    // fork, in a body of its own (2026-08-31). It
    // is the three-arm table above (LIVE pause / PAUSED resume / IDLE
    // nothing) and reads no highlight at all.
    //
    // THREE CALLERS: play_button_act calls it as its
    // tail, so Space, the Play button and the car's own toggle past its LIVE
    // arm keep the whole highlight-driven act; and on_media_command's two
    // IMPOSED-INTERRUPT arms — FocusLost / FocusLostTransient — and Stop's
    // leading pause call it DIRECTLY, because a claim that something must STOP
    // sounding is a claim about the transport alone. Without that split the
    // highlight arm would let a focus loss START a walked-to row instead of
    // pausing what sounds.
    void transport_toggle_act();
    // THE ITEM'S ROW LIT WHERE NO ROW IS (architect 2026-11-27): with NO ROW
    // LIT and a bound item (live or paused) listed in the folder the band
    // stands in — the state a render's delete leaves (delete_render: no row
    // lit after a delete, the surviving item paused by the question) — seat
    // the band on the item's row, the playing render's row being a seat that
    // reflects something (folder_overlay.h's head), with the keep-visible
    // scroll and the band's and the row's damage. Otherwise nothing. ONE
    // CALLER, play_button_act's transport tail (the Play button and the car's
    // toggle past its LIVE arm), ahead of the toggle that resumes the item;
    // the imposed interrupts call transport_toggle_act direct and light
    // nothing, a pause reflecting no choice.
    void seat_band_on_listed_item();
    // Pause a live transport (the resume point is the engine's own position)
    // or resume a paused one; a no-op with no item. ITS TWO ARMS ARE BODIES
    // OF THEIR OWN (architect 2026-10-26, the score viewer's shape),
    // each a silent no-op with no item and in the other's state: pause() —
    // LIVE parks at PAUSED through the one stop body, the resume point the
    // engine's position — and resume() — any rest plays from the resume
    // point, and A POINT IN THE ITEM'S LAST FRAME OR AT ITS END (THE PLAY KEY
    // AT THE END, architect 2026-10-26: "next track if there is one,
    // otherwise play from the start") continues to the folder's next wav
    // where one exists with the REPEAT ONE lamp dark, and replays this item
    // from its start otherwise — THAT END RULE IS THE PLAY KEY'S ALONE. The
    // scrub (THE SOUND PAUSES WHILE THE SCRUB IS HELD, input_pointer.cpp's
    // play-scrub block) pauses a live item at its press through pause() and
    // plays it on at the lift through resume_at(frame): the item the press
    // paused LIVE again from the dropped frame, clamped as seek_to's live arm
    // clamps (a target in the last frame stops one short of it, architect
    // 2026-10-27), and never the key's end rule — so a drop at the end
    // reaches the natural end at once, and a grab taken as the item finished
    // never walks to the next wav before the drop is read. A no-op with no
    // item, where LIVE, and on an item shorter than two frames, as resume()
    // is.
    void toggle_pause();
    void pause();
    void resume();
    void resume_at(int64_t frame);
    // (THE PLAYER HAS NO STOP ACT: architect 2026-09-01, the reasoning at
    // the head of this file; the head unit's Stop is the pause-then-home
    // composition at on_media_command's own arm.)
    //
    // THE ITEM FOLDER'S ENDS (2026-08-28, the two skip buttons' long press,
    // 2026-08-31): the
    // first / last wav of `item_folder`, played from its start on the play road
    // — never a wrap, and a SILENT refusal with no item AND on an item already
    // at that end (the header's silent refusals). The step back to the
    // item's previous neighbour lives inside Home as its previous-track
    // window (the constant above); the step FORWARD is next_track, the right
    // skip's own plain act, which shares the natural
    // end's walk rather than carrying a body of its own.
    void first_in_item_folder();
    void last_in_item_folder();
    // REPEAT ONE (architect 2026-08-28) — the row's one lamp, flipped by
    // its button and by bare `r`: while it stands the natural end replays the
    // item from its start instead of advancing. Session-only state
    // (AppState::RenderPlayer::repeat_one, LIT at every open,
    // 2026-09-11), and this is its ONE writer past that reset; it damages the
    // row for the lamp and touches no transport.
    void toggle_repeat_one();
    // Seek by `delta_frames` from the current position, clamped into the
    // item; a no-op with no item. A live transport reseeks in place and a
    // paused one moves its resume point; an IDLE one has no item (the
    // invariant at AppState::RenderPlayer::transport), so the no-item no-op
    // is its consumed answer
    // (architect 2026-08-29 — Audacious's stopped slider).
    void seek_by(int64_t delta_frames);
    void seek_to(int64_t frame);
    // -- THE TWO SKIPS (the modal row's outer transport buttons) -------------
    //
    // TWO ROADS EACH: the KEY (bare Home, bare End) and the SKIP BUTTON'S
    // PLAIN PRESS (the modal row's Home and NextTrack arms). THEY ARE THE
    // TABLET'S ACTS AND THE TABLET'S ALONE — the car has its own pair below,
    // and neither road reaches the other (architect 2026-09-12, from the car:
    // "the car is a separate interface").
    //
    // home() — THE PREVIOUS-TRACK ACT, TWO ARMS OVER ONE POSITION TEST
    //   (architect 2026-08-31): inside the item's first
    //   kPlayerPreviousThresholdMs WITH a previous entry in `item_folder`, it
    //   plays THAT ENTRY from its start (the window at the constant above, the
    //   fork's one owner render_player_home_takes_previous, app_state.h, which
    //   the button's hint reads too); everywhere else it seeks the item's own
    //   start and takes every refusal seek_to owns. AT IDLE there is no item
    //   (the invariant at AppState::RenderPlayer::transport), so there is no
    //   previous entry and the
    //   seek is seek_to's silent no-item refusal. IT NEVER LEAVES A FOLDER:
    //   going up is the Up button, Backspace and the car's Previous, and
    //   nothing else.
    // next_track() — THE NEXT TRACK (architect 2026-09-04, "Next should always
    //   skip to the next song. The home/end analogy doesn't quite work — this
    //   isn't a playhead, this is audio playback"): the next wav of the
    //   TRANSPORT ITEM'S OWN FOLDER as that folder was listed when the item was
    //   played, from its start, live or paused alike (IDLE has no item, the
    //   wall's first term), through
    //   advance_to_next_in_item_folder — the body the natural end's
    //   auto-advance shares. IT OUTRANKS REPEAT ONE and reads the lamp nowhere:
    //   the lamp governs what a file does when it REACHES ITS END, and a press
    //   is not a natural end. At the folder's last wav it is a silent walled
    //   no-op, nothing looping, and the button greys on that same wall
    //   (render_player_next_track_actionable).
    //
    // THE SHIFTED TWINS ARE THEIR OTHER HALF (the buttons' long press): the
    // ITEM folder's first and last
    // wav in every transport state, silent with no item (2026-08-31).
    void home();
    void next_track();

    // -- THE CAR'S OWN ACTS (architect 2026-09-12, from the car) -------------
    //
    // THE CAR IS A SEPARATE INTERFACE, so these are the head unit's vocabulary
    // and not a replay of the keys above: the wheel and the console carry
    // REWIND, PLAY/PAUSE and FAST-FORWARD, the outer two arriving as Previous
    // and Next, and the first three bodies are what those buttons mean, the
    // fourth the seek they share. Their callers are on_media_command's arms,
    // which own the table.
    //
    // car_toggle() — THE CONSOLE'S ONE PLAY/PAUSE BUTTON, undivided:
    //   Pause and PlayPause both reach this one body, because the session
    //   tells the console it is PLAYING whatever the transport is doing (the
    //   dummy display at publish_media_state), so the console's press is a
    //   TOGGLE whichever of the two verbs it happens to send. A bare Play is
    //   a head unit's autoplay at the connect rather than a press of that
    //   button and goes to the stream's reopen instead (the table at
    //   on_media_command). A LIVE transport takes
    //   toggle_pause — the item pauses and keeps its resume point, and the
    //   silence takes over the display on the publish — and anything else
    //   takes play_button_act WHOLE, the tablet's Play button: a highlighted
    //   folder opens, a highlighted other wav plays, and the item's own row
    //   resumes.
    // THE TWO STEPS AT REST START FROM THE CAR'S ORIGIN (car_origin_row,
    //   architect 2026-11-27): the lit row, or WITH NO ROW LIT
    //   (folder_overlay.h's head) the row the car's title names, as if it
    //   were lit — the car has no display for the highlight — else the
    //   first row; the tablet's walk pair is not the car's.
    // car_previous() — REWIND: WHILE LIVE it is car_seek() back (architect
    //   2026-11-25: "render player in car should also do ... skip back/forth
    //   when audio is playing"). At REST (paused and idle alike) it walks the
    //   band UP one row from the origin, and AT THE FIRST ROW it goes UP A
    //   FOLDER (up() whole: the item unloaded, the sound stopped, the band
    //   seated on the folder just left); at the ROOT's first row it is a
    //   silent walled no-op.
    // car_next() — FAST-FORWARD: WHILE LIVE it is car_seek() forward; at REST
    //   it walks the band DOWN one row from the origin — from none in a fresh
    //   folder onto the SECOND render — clamped at the listing's last row,
    //   a silent wall from a lit last row (from none on a one-row listing
    //   the step lights that row).
    // car_toggle()'s Play from NO ROW LIT is the tablet's: inside a folder
    //   the bound item listed there, resumed with its row lit, else the
    //   listing's first render (render_player_band_row), at the root
    //   nothing.
    //   Changing track from the car is therefore pause, step, play (architect
    //   2026-11-25, accepted).
    // car_seek() — THE CAR'S ONE SEEK (architect 2026-11-25), the live arm of
    //   the two above and the whole of the FastForward / Rewind arms:
    //   seek_by GuiCarTransport::kCarSeekSeconds (the project transport's car
    //   seek, one number for both car roads) of the item as it sounds, back or
    //   forward. ITS WALLS ARE seek_by's: back inside the item's first
    //   kCarSeekSeconds it lands on the item's start; forward inside its last
    //   kCarSeekSeconds a live item lands on its last frames (seek_to's live
    //   clamp), so it plays out at once and THE NATURAL END answers — Repeat One's replay
    //   of this item, else the next wav of its folder, else the rest paused at
    //   the item's end — the same answer the item reaching its end by itself
    //   gives, never a wrap of the folder. A paused item's resume point moves;
    //   an idle player has no item and the seek is seek_to's silence.
    //
    // THE ASYMMETRY IS DELIBERATE — only the car's PREVIOUS ever leaves a
    // folder, and only at rest: the listener always knows which file is a
    // folder's FIRST and never how many it holds, so walking off the TOP is a
    // known, intended act while walking off the END must never be an
    // accidental exit. TIMING GESTURES ARE REJECTED ("Previous twice
    // quickly"): these are transport- and band-based, never press-timed.
    //
    // NO WALL PREDICATE STANDS IN FRONT OF THEM: each refusal is the silent
    // return of the body composed here — the step's clamp (car_step_to,
    // nothing where the landing is the lit row), up()'s root return,
    // seek_to's for the seek — and with a constant
    // PLAYING published there is no drifted display for a walled press to
    // correct, so no wall is consulted ahead of them.
    void car_toggle();
    void car_previous();
    void car_next();
    void car_seek(bool forward);

    // The item position the clock and the scrub read: the engine's cursor
    // while live, the resume point otherwise. 0 with no item.
    int64_t position() const;
    // A frame's exact x on the published scrub track (a real place: the
    // thumb's center, render_player_scrub_x_of), and the inverse — the one
    // mapping the painter's thumb and the press router's seek share, over the
    // stashed track rect.
    double  scrub_x_of(int64_t frame) const;
    int64_t scrub_frame_at(double x) const;

    // THE TICK (main.cpp's on_tick, forked at its head onto this while the
    // mode stands): a live transport with NO DEVICE TO PLAY ON pauses (the
    // no-device arm below, asked first — gone away or never there, one
    // answer); one the audio thread has ended takes the natural-end branch; a
    // still-live one damages the scrub track at every position change and the
    // clock cell when its label's text changes.
    void tick();

    // -- The car ------------------------------------------------------------

    // A HEAD UNIT'S BUTTON, ACTED ON DIRECTLY (architect 2026-09-12, from the
    // car: "the car is a separate interface"). Every command runs one of this
    // cluster's own bodies; NOTHING IS SYNTHESIZED AND NOTHING IS DISPATCHED:
    // a direct act presses no key and no button, so nothing on the screen
    // can take a car command for its own press.
    //
    // WITH THE MODE DOWN every command is DROPPED HERE, and (2026-09-17)
    // that is the BELT and not the partition: main.cpp's hook forks on the
    // mode bit ahead of this body, so a command arriving with the player
    // closed is the CAR TRANSPORT'S (GuiCarTransport::on_media_command, which
    // drives the project's own transport), and this guard catches only a
    // command queued before the close drained. It is the player's own
    // statement about the state it needs — the same kind as the PROMPT guard
    // behind it, which drops every command BUT PLAY while a question stands
    // over the player (the load confirmation): the car's buttons are the
    // player's acts, not a question's answer, while Play is not an act on the
    // player at all (its arm below).
    //
    // A CAR COMMAND ACTS UNDER A POINTER DRAG exactly as the modal row's own
    // button would, and that is deliberate: the keyboard's drag swallow
    // ("Keys are ignored during a drag", on_key's player block) belongs to the
    // KEY road, and a direct act never enters it. Nothing here is unsafe under
    // one — the band's row press arms nothing a walk can break, and the
    // scrub's thumb drag commits its seek against the LIVE item at the
    // release (clamped into it by the mapping), so the one visible
    // consequence is that a toggle which started another wav mid-drag has that
    // release land inside the wav it started. The scrub's press pauses a live
    // item for the hold (input_pointer.cpp's play-scrub block), so a toggle
    // mid-drag resumes or starts the sound under the held thumb, and the
    // release then finds the transport LIVE and takes seek_to's live arm
    // alone, its resume_at skipped (finish_player_scrub_release).
    //
    // THE TABLE. The Accord's three buttons are REWIND, PLAY/PAUSE and
    // FAST-FORWARD, the outer two arriving as Previous and Next:
    //   Pause / PlayPause -> car_toggle(), ONE BODY FOR THE TWO. The
    //     published state says PLAYING whenever the player stands (the dummy
    //     display at publish_media_state), so the console's one button sends
    //     whichever of these two verbs its own display believes and both of
    //     them mean "the other one": LIVE pauses (toggle_pause), anything else
    //     takes the tablet's Play whole (play_button_act). No state gate: a
    //     constant PLAYING leaves no drifted display to correct.
    //   Play -> GuiPlayback::ensure_device_available_for_play() AND NOTHING
    //     ELSE: PLAY MEANS REOPEN THE STREAM AND START NOTHING (architect
    //     2026-09-18, measured in the car; GuiCarTransport::on_media_command's
    //     table owns the measurement and the accepted cost). A plain
    //     KEYCODE_MEDIA_PLAY about a second after the Bluetooth link comes up
    //     IS the head unit's autoplay, while every HUMAN press arrives as
    //     Pause against the dummy display; the connect kills the AAudio stream
    //     and nothing but a press reopens it, so the arm reopens it and starts
    //     it SILENT under the car's own fade-in, where the crackle is spent.
    //     No transport starts, nothing is published, and a failed reopen
    //     raises NO CARD — a console key is not a deliberate press at the
    //     glass, and the next real press cards at the launch gates. IT IS
    //     ANSWERED AHEAD OF THE PROMPT GUARD, at the head of that body rather
    //     than in its switch, as the car transport's own arm stands ahead of
    //     admits(): the reopen writes no authored, transport, modal or
    //     published state and its purpose holds whatever stands on the screen,
    //     so a question over the player has nothing to protect from it and
    //     BOTH CAR ROADS ANSWER A CONNECT ALIKE. Only the mode belt stands in
    //     front of it.
    //   Previous / Next -> car_previous() / car_next(): while an item sounds
    //     the car's seek, at rest the playlist walk with the up-a-folder exit
    //     (architect 2026-11-25; their contract is at the declarations).
    //   Stop -> PAUSE AND THEN HOME, two direct acts in order (architect
    //     2026-09-01; the player has no Stop act of its own): a LIVE transport
    //     takes transport_toggle_act — the transport's own tail, past the
    //     highlight — and a session standing after it takes seek_to(0), never
    //     the left skip's act, whose previous-track window would change TRACKS
    //     on a Stop. Audibly it is a Stop: silence now, the top on the
    //     next Play; what differs is the state left behind, PAUSED rather than
    //     IDLE, so the scrub stays live under it. The Accord has no stop
    //     button; the arm stays for any console that has one.
    //   FastForward / Rewind -> car_seek() DIRECT, in every transport state
    //     and every folder: the car has ONE SEEK in the player (architect
    //     2026-11-25), the Accord's wheel reaching it through Previous /
    //     Next's live arms and a console that sends these kinds reaching it
    //     here, a paused item's resume point moving as any paused seek's does.
    //   SeekTo -> seek_to DIRECT; its milliseconds are clamped to the item's
    //     own length BEFORE the conversion to frames, the arriving position
    //     being any int64 a head unit cares to send (the rule at the site).
    //   FocusLost / FocusLostTransient -> transport_toggle_act with the
    //     transport LIVE, Android's one imposed interrupt, which always pauses
    //     and must reach the transport past Space's highlight fork.
    //   FocusGained -> nothing. NOTHING RECOVERS BY ITSELF — the AAudio
    //     posture; the user presses play.
    void on_media_command(GuiMediaCommand cmd);

    // THE ONE OWNER OF WHAT THE HEAD UNIT SHOWS: builds GuiMediaState from
    // app.render_player, THE BAND and the one position reader
    // (render_player_position) and hands it to
    // GuiPlatform::publish_media_state.
    //
    // THE DISPLAY IS A DUMMY AND SAYS PLAYING WHENEVER THE PLAYER STANDS
    // (architect 2026-09-12, from the car). The console's PAUSE stuck every
    // time: it reads the still-streaming Bluetooth link as playing and
    // OVERRIDES a session that says PAUSED, so a paused state could never win.
    // So the session tells it what it already believes — `session_active` true
    // MEANS the published state is PLAYING, the sliver's fork being
    // "inactive -> STOPPED, else PLAYING" at speed 1.0, a fork nothing in the
    // running app takes the STOPPED side of (2026-09-17: both owners publish
    // `session_active` true) —
    // and every console
    // press becomes a plain toggle against a display that never contradicts
    // the link. THE CLOCK RUNS ON, always, never ending, because a console
    // shown a stopped clock goes back to its own idle picture. `playing` stays
    // the TRUE transport bit (active && Live) and is read on the consuming
    // side for AUDIO FOCUS ALONE. THE TABLET STAYS TRUTHFUL: the modal row's
    // glyph, clock and scrub say exactly what the transport is doing, and this
    // fiction lives on the wire and nowhere else.
    //
    // THE THREE LINES ARE THE PROJECT, THE FOLDER AND THE NAME (architect
    // 2026-09-12, from the car, on the console's own picture: it lays the
    // metadata out as ALBUM above the title, dim, and ARTIST below it — so
    // the dim top line, the
    // LEAST important of the three, takes the project, and the bottom line
    // takes the folder). ONE FUNCTION OF (transport, item, highlight) fills
    // all three, stated once in the body and read nowhere else, IN TWO ARMS:
    //   ALBUM — the project's name, in both arms, the one line that does not
    //     move while the player is walked.
    //   LIVE with an item — the ITEM: its file name as the title, ITS OWN
    //     FOLDER's name as the artist (not the band's: the listener may have
    //     walked elsewhere, and the sounding item's home is what the display
    //     is about), its own duration, and render_player_position.
    //   EVERYTHING ELSE (Paused and Idle alike, an item bound or not) — THE
    //     SILENCE TRACK, which is what plays while the listener is at the top
    //     level walking folders: the artist is THE FOLDER THE BAND IS IN,
    //     `tmp` at the root and the batch folder's own name inside one, and
    //     the title is THE NAME OF THE ROW PLAY WOULD ACT ON
    //     (render_player_band_row: the highlighted row, or with no row lit
    //     inside a folder the bound item's row or the first render;
    //     architect 2026-11-27) — a wav's or a folder's,
    //     bare and with no trailing slash, the artist beneath it already
    //     saying where that name lives — or, where that names no row (the
    //     root with none lit, an empty listing), the LISTED FOLDER naming
    //     itself, the same word the artist
    //     carries. AT POSITION 0 WITH THE DURATION UNKNOWN (duration_ms = -1;
    //     the sliver puts METADATA_KEY_DURATION only for a value above 0), so
    //     the console counts up from zero with no length to run into. A
    //     PAUSED ITEM'S OWN CLOCK IS NOT PUBLISHED: a PLAYING state
    //     at speed 1.0 over a real duration would run the console's clock
    //     into the track's end. The tablet's row is where a paused item's
    //     clock lives.
    // Title and album are never empty on any push this body makes — a row has
    // a name, or with no row named the listed folder names itself into
    // the title, and the project always has a name — AND THERE IS NO EMPTY
    // PAIR AND NO STOPPED STATE ANYWHERE (architect 2026-09-17): close()
    // pushes nothing, the mode-down arm
    // returns without pushing, and the session stands for the app's life with
    // the car transport publishing while this player is closed. THE
    // SILENCE IS METADATA AND NEVER A FILE: a silent wav on disk would be
    // listed by the player and played by the auto-advance.
    //
    // THE EDGE INVENTORY (NINE call sites across EIGHT functions; no car
    // arm re-publishes, a constant PLAYING leaving no drifted display to
    // correct):
    // play_wav's tail and play_live_from (the one launch of resume() —
    // toggle_pause's resume arm — and resume_at, the scrub's lift)
    // — the two writers of the LIVE state; THE STOP BODY'S PLAYER FORK
    // (GuiPlaybackLifecycle::stop_playback_if_playing, through its
    // back-pointer) — the one place every player stop passes, which
    // covers the pause, the natural end's last-wav rest, the dead
    // device, the rebind ahead of the next item and the Up act's own stop;
    // THE BAND'S FOUR WRITERS — move_highlight, set_highlight, rebuild_rows
    // and one render's delete's seat of none after its rebuild (delete_render,
    // architect 2026-11-27) — EACH GATED ON `transport != Live`, since the
    // highlight is the title only where the item is not: the two wrappers
    // carry the router's Up/Down walk and the pointer's row lift, and
    // rebuild_rows carries EVERY entry, which is why open() and up() have no
    // tail push of their own — open()'s listing publishes the
    // session active on its silence track, and up()'s root entry is THE LAST
    // WORD OVER THAT FORK, whose push still carries the item the unload
    // ahead of it has since dropped; seek_to — both arms, so
    // the head unit's clock is re-seated where a seek moved the item (which is
    // also what publishes the car Stop's seek to the top, that command being a
    // pause and then this seek).
    // CLOSE() IS NOT ONE OF THEM: it pushes nothing (2026-09-17), the wire
    // passing to
    // GuiCarTransport::tick, which takes it back on its first tick after the
    // close. NO PER-TICK PUSH HERE, the band's edges included: the
    // published position advances on the head unit's own clock from the last
    // push at speed 1.0. Duration and position are milliseconds at the project
    // source's rate (the item is at that rate by the decode's own equality).
    void publish_media_state();

private:
    // Which tail the unload is running. It is required, with no default,
    // because the two roads want the mode bit in different places and a third
    // caller must choose rather than inherit one of them:
    //   Up    — the bit stays up. The player goes on standing with nothing
    //           loaded, and a preview dispatched by the re-express below still
    //           owes its rebind to the close's own re-express later
    //           (complete_successful_buffer's guard is what defers it).
    //   Close — the bit comes down before the re-express. The target arm's
    //           ensure_ready can resolve synchronously on a cache or artifact
    //           rung, and that same guard refuses to rebind while the player
    //           is active: with the bit still up the mode would end in T view
    //           with is_dirty_ cleared and the engine on source.wav, one Space
    //           from playing source audio under the target picture.
    // The bit cannot move earlier on either road: the stop body's player fork
    // asks it, and a mode taken down ahead of the fence would send the stop
    // down the project's arm and leave the engine unfenced.
    enum class UnloadTail { Up, Close };
    // THE ONE OWNER OF THE UNLOAD ORDERING, shared by close() and up() — and
    // by the two deletes, delete_batch_folders and delete_render, which take
    // up()'s tail ahead of their own listing rebuild (architect 2026-11-25): the
    // stop body's fence, then the mode bit
    // where the tail wants it, then THE VIEW'S buffer rebound, then the item's
    // fields cleared to their open() values and its buffer freed. The order
    // is load-bearing — the engine may hold the item's pointer until the
    // rebind — and the callers add only what is theirs (close() the panel
    // teardown, up() and the deletes the listing entry whose rebuild
    // publishes). It publishes nothing itself: the fork inside the stop body
    // pushes a state still carrying the item, which every caller supersedes.
    void unload_item(UnloadTail tail);
    // Rebuild the listing for the live folder: rows, scroll 0, hover and
    // press cleared, and the highlight through THE SEAT RULE'S THREE ARMS —
    // the transport's item's row if it is here, else `seat_folder`'s row if
    // the caller named one and it is here, else NONE (a row is lit only
    // where it reflects something, folder_overlay.h's head, architect
    // 2026-11-27) — a seated row
    // centered in the band (folder_overlay::scroll_row_centered, 2026-10-26),
    // none leaving the listing at its top.
    // Damages the band. ONE CALLER RESEATS AFTER IT: one render's delete
    // (delete_render), which lights no row over a bound item's and keeps the
    // list's scroll (architect 2026-11-27: no row lit after a delete).
    // `seat_folder` IS REQUIRED AND IS EMPTY EVERYWHERE BUT UP: entering a
    // folder is memory-less by ruling, so a caller must state that it names
    // none rather than inherit a default (the rule and the architect's words
    // are at the seat block, render_player.cpp).
    void rebuild_rows(const std::filesystem::path& seat_folder);
    // Enter a folder: the Root, which IS `tmp/`, or the batch at `dir`;
    // `seat_folder` is passed straight to the seat rule above.
    void enter(AppState::RenderPlayer::Folder folder,
               const std::filesystem::path& dir,
               const std::filesystem::path& seat_folder);
    // Decode `path` under the vocabulary above; on success bind it as the
    // item and play it from its start. `folder_wavs` / `index` name the
    // item's folder list and its place in it. `tapped` is true on the row
    // tap's own play alone (open_row's), which seats no band and scrolls
    // nothing; every other play brings the item's row into view. Returns
    // whether it bound the item — true also where the device refused the
    // launch and the item rests paused at its start under that card
    // (rest_if_launch_refused); a decode refusal has raised its card and
    // changed nothing.
    bool play_wav(const std::filesystem::path& path,
                  const std::vector<AppState::FolderOverlayRow>& folder_wavs,
                  int index, bool tapped);
    // THE FOLDER'S FORWARD STEP, one body for its three callers (2026-09-04):
    // the natural end's auto-advance below, the deliberate next_track above
    // and resume()'s end rule (the PLAY key at the end). The next wav of the item's folder as listed, played from its
    // start; never across folders and never a wrap, the wall being
    // render_player_next_track_actionable. Returns whether it bound the next
    // wav (play_wav's answer) — a refused decode has raised its own card and
    // left the item untouched.
    bool advance_to_next_in_item_folder();
    // THE RESUME ROAD'S ONE LAUNCH: the item LIVE from `from` to its end, the
    // row damaged and the head unit told. Its two callers have already
    // chosen `from` and passed their guards: resume() through the PLAY key's
    // end rule, resume_at() through the live seek's clamp. Over a refused
    // device it rests PAUSED at `from` instead (rest_if_launch_refused).
    void play_live_from(int64_t from);
    // A launch the device refused, rested PAUSED at `from` under the card,
    // never left LIVE; answers whether it rested (the rule at the
    // definition, architect 2026-11-26).
    bool rest_if_launch_refused(int64_t from);
    // The natural end: the fence through the one stop body, then — with the
    // REPEAT ONE lamp lit — the item again from its start (the sanctioned
    // exception at the head of this file), else the shared forward step
    // above, else THE REST AT THE ITEM'S END, PAUSED (architect 2026-10-26:
    // it continues if there is a next track and stops dead where it
    // is otherwise; the folder's end is no state of its own, 2026-08-31).
    // THE LAMP'S ARM IS TERMINAL: it returns on a REFUSED replay too, so a
    // lit lamp never falls through to the advance.
    // A NATURAL
    // END IS THE CURSOR REACHING THE END, NOT THE ABSENCE OF A DEVICE
    // (2026-08-28): a Bluetooth drop or a headphone pull under a sounding
    // item leaves the session word's playing bit down, and the tick asks
    // GuiPlayback::device_unavailable FIRST so it pauses in place instead of
    // arriving here and advancing to a wav nothing can play. A launch the
    // device refuses — the device that never came up included — is never
    // left LIVE at all (rest_if_launch_refused, architect 2026-11-26).
    void on_natural_end();
    // The wav rows of the live listing, in listing order.
    std::vector<AppState::FolderOverlayRow> listing_wavs() const;
    // Whether any playable wav exists — the opener's refusal.
    bool has_playable_render() const;
    // Damage helpers: the band, the modal row.
    //
    // ONE BAND DAMAGE (2026-08-28): the panel's height is fixed at
    // the slot's ceiling whatever the listing is (folder_overlay.h), so the
    // band a rebuild must erase IS the band every other damage covers — a
    // shorter listing leaves no departed rows standing above it.
    void damage_band();
    void damage_row();
    // THE CAR'S ORIGIN — THE ROW ITS TWO STEPS START FROM at rest
    // (car_previous / car_next; architect 2026-11-27: "I think the implicit
    // rule, if not the explicit rule, should be that the car still has the
    // first row highlight feature. It's just the car doesn't actually have a
    // display for that." — "Agree."): the lit row; WITH NO ROW LIT the row
    // the car's title names, as if it were lit (render_player_band_row: the
    // bound item's row where listed, else the folder's first render), else
    // the listing's first row (the folder list, where the band names none);
    // -1 for an empty listing. ONE OWNER, read by both steps. THE CAR ALONE:
    // it has no display for the highlight, and its title already names this
    // row, so a step from it is a step from what the listener was shown; the
    // tablet's walk pair keeps the glass's own rule (from none the first row,
    // folder_overlay::move_highlight_target), the glass showing that no row
    // is lit.
    int  car_origin_row() const;
    // THE CAR'S STEP'S LANDING: `row` lit as the walk lights its landing —
    // folder_overlay::set_highlight (the keep-visible scroll), the band's and
    // the row's damage, the resume-abandon rule
    // (abandon_paused_resume_if_band_left_item) and the title's republish off
    // a non-live transport — move_highlight's own tail on a landing the car's
    // origin names. Nothing where the highlight already stands there.
    void car_step_to(int row);
    // A PAUSED ITEM'S RESUME POINT, ABANDONED WHEN THE BAND LEAVES ITS ROW
    // (architect 2026-09-15): while paused, `resume_frame` rests where the
    // pause left it ONLY WHILE THE HIGHLIGHT STANDS ON THE ITEM'S OWN ROW — a
    // highlighted OTHER wav PLAYS (render_player_highlight_act_row), so a row
    // the band has walked away from must not leave a stale resume point
    // sitting under it for a highlight that may never walk back. Does nothing
    // unless a paused item is standing there to abandon
    // (`transport == Paused && resume_frame != 0`); when it fires it clears
    // the point to 0 and damages the row so the clock and scrub repaint at
    // zero (seek_to's Paused arm's own damage — position() reads
    // `resume_frame` direct, so nothing else needs a nudge). ONE OWNER, called
    // from BOTH highlight writers below AFTER their own write — the car's rest
    // walk composes move_highlight and needs no arm of its own — and NOWHERE
    // ELSE NEEDS IT: rebuild_rows' two other seat writers are up()'s (which
    // unloads the item, and with it the point, before it ever reseats) and
    // every other entry's (memory-less, and reached only with no item bound —
    // a bound item's own folder is the band's, by construction, so `Root`
    // never carries one) but ONE RENDER'S DELETE's (delete_render), whose
    // rebuild may find ANOTHER render bound and whose seat after it lights no
    // row (architect 2026-11-27): the band stood on the deleted render at
    // the press, off the item's row, so no band leaves the item's row there
    // and the resume point stays as the question's rise left it (architect
    // 2026-11-25: the item stays bound and paused), Play resuming it there
    // with its row lit (seat_band_on_listed_item, which lands ON the item's
    // row); and play_wav's own seat
    // lands ON the item's row it
    // just bound, fresh, never paused. COMING BACK CHANGES NOTHING: the
    // abandon is one-way, and a paused item the band never leaves resumes
    // where it paused.
    void abandon_paused_resume_if_band_left_item();
    // THE PLAYER'S ONE VOICE: a NORMAL notification card (2026-08-29). Its
    // callers are the
    // decode road's refusals (the probe's, the allocation ceiling's, the
    // read's, the rate-and-channel equality twice, "This wav holds no
    // samples"), "No audio device to play the wav" and the opener's
    // "Nothing to play: no renders under tmp/" — every one a
    // sentence answering an act, none a state. A thin call, so the player's
    // refusals stay one grep.
    void status(const std::string& line);
    // The decode road's refusals go through this one instead (2026-09-02):
    // the same card, plus the stderr line naming the wav's FULL path beside
    // the reader's words — the two clauses of one failure (failure.h). The
    // contract is at the definition.
    void refuse_decode(const std::filesystem::path& path,
                       const std::string&           words);
};
