#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "playback_lifecycle.h"
#include "renders_dir.h"
#include "target_render.h"
#include "viewport.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// THE RENDER PLAYER (architect design 2026-08-28, the in-app player for the
// car) — the operations cluster for the MODE that plays the project's own
// renders through the one playback engine: the folder overlay above the
// bottom row (folder_overlay.h) lists the project's OUTPUT FOLDERS — `render/`
// with the deliverable, `tmp/` with its batch folders and their cells — and
// the bottom row's modal carries the transport (Previous / Play-Pause / Next /
// Load in place / Close, the clock, the play-scrub). The state it moves is
// AppState::render_player and AppState::folder_overlay (app_state.h, where
// every field is described); this struct owns the acts.
//
// THE MODEL (R1, R2, revised 2026-08-28 after the first pass): the listing is
// navigated THE REGULAR WAY — a highlighted folder OPENS on the double-click,
// Enter or the Play button, the `..` row at the top of every non-root listing
// goes up (Backspace on plastic) — and a single click only HIGHLIGHTS (the
// band is the list's keyboard focus; Up/Down walk it). A WAV PLAYS from its
// start when opened, or when it is highlighted and Play is pressed. THE
// TRANSPORT'S ITEM is separate from the highlight: it keeps playing while
// the listing is navigated elsewhere, it wears the transport glyph on its
// row, and AUTO-ADVANCE, Previous and Next walk ITS FOLDER'S wav list as it
// was listed when the item was played — never another folder, never a wrap,
// and NOTHING LOOPS: at the folder's last wav the transport stops with the
// item resting at its start, and a following Play replays it (a user act).
// Every listing is built when its folder is entered and never kept fresh.
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
// playback.play directly against that domain, and the bit that says the
// transport is live is the player's own (`transport_live`, the scanner
// flag's mirror). THE STOP IS STILL THE ONE STOP BODY: every pause, natural
// end and close takes GuiPlaybackLifecycle::stop_playback_if_playing, which
// carries the player's fork inside it (the fence, then the transport bit
// cleared and the modal row damaged instead of the scanner teardown), so the
// keyboard stop rule and the fence-before-rebind ordering hold by
// construction.
//
// ENTER AND LEAVE. open() is the ONE opener — bare `l`, bare `'` outside the
// `h` view and their two icon-row buttons all reach it through on_key — and
// it refuses with "No renders to play" when neither `render/` holds a wav
// nor `tmp/` a cell; its callers refuse the modal states (a prompt, an
// editor, the `h` view, loading, no source) before it is asked. The open
// takes the modal-open stop, the mode bit, a fresh modal session, the root
// listing and a whole-window damage. close() — Close, Esc, `l`, Ctrl+Q ahead
// of the quit road, and the load road's success — takes the stop body, clears
// the mode, rebinds THE VIEW'S buffer through the S/T flip's own tail fork
// verbatim (ensure_ready in target view, rebind_to_source in source view),
// and only THEN frees the item's buffer: the engine may hold the pointer
// until the rebind.
//
// THE LOAD ROAD is not here: the Load in place button (bare `'` inside the
// player) and its confirmation live on GuiInputHandler, which owns the shared
// act (load_render_entry_in_place); this cluster answers only which entry is
// highlighted (highlighted_entry) and closes on the act's success.
struct GuiRenderPlayer {
    AppState&             app;
    const GuiAudio&       audio;
    GuiPlayback&          playback;
    GuiPlaybackLifecycle& playback_lifecycle;
    Viewport&             viewport;
    GuiTargetRender&      target_render;
    GuiRendersDir&        renders_dir;

    GuiRenderPlayer(AppState&             app_,
                    const GuiAudio&       audio_,
                    GuiPlayback&          playback_,
                    GuiPlaybackLifecycle& playback_lifecycle_,
                    Viewport&             viewport_,
                    GuiTargetRender&      target_render_,
                    GuiRendersDir&        renders_dir_)
        : app(app_),
          audio(audio_),
          playback(playback_),
          playback_lifecycle(playback_lifecycle_),
          viewport(viewport_),
          target_render(target_render_),
          renders_dir(renders_dir_) {}

    // THE OPENER (the contract above). Returns whether the mode opened; a
    // refusal has already written its status line or has nothing to say.
    bool open();
    // THE CLOSER (the contract above). A no-op when the mode is down.
    void close();

    // -- The listing --------------------------------------------------------

    // THE OPEN ACT on row `index` of the live listing: a folder row enters
    // it, the up row goes to the parent, a wav row plays from its start.
    void open_row(int index);
    // One folder up; a consumed no-op at the root.
    void up();
    // Move the highlight by `delta` rows, clamped, scrolling the band to keep
    // it visible.
    void move_highlight(int delta);
    // Seat the highlight on `index` (clamped into the listing; -1 for an empty
    // one) — the motionless click's act. Damages the band.
    void set_highlight(int index);
    // Scroll the band by `rows` rows (the wheel's detent step), clamped.
    void scroll_rows(int rows);
    // The highlighted row's render entry — non-null exactly when the highlight
    // is a LOAD-CAPABLE wav (a batch cell). The load road's one question.
    const AppState::RenderEntry* highlighted_entry() const;

    // -- The transport ------------------------------------------------------

    // THE PLAY BUTTON'S ACT (and Space's): a highlighted wav that is not the
    // transport's item plays from its start; a highlighted folder or `..`
    // OPENS (the car-stereo OK/Play convention, so glass never needs a
    // double-tap to navigate); otherwise pause / resume the item.
    void play_button_act();
    // Pause a live transport (the resume point is the engine's own position)
    // or resume a paused one; a no-op with no item.
    void toggle_pause();
    // The item's neighbours within ITS folder; a consumed no-op at either end.
    void previous();
    void next();
    // Seek by `delta_frames` from the current position, clamped into the
    // item; a no-op with no item. A live transport reseeks in place, a paused
    // one moves its resume point.
    void seek_by(int64_t delta_frames);
    void seek_to(int64_t frame);
    // The item's start.
    void home();
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
    // mode stands): a live transport that the audio thread has ended takes
    // the natural-end branch; a still-live one damages the clock cell and the
    // scrub track once per position change.
    void tick();

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
    // refusal has written its status line and changed nothing.
    bool play_wav(const std::filesystem::path& path,
                  const std::vector<AppState::FolderOverlayRow>& folder_wavs,
                  int index);
    // The natural end: the fence through the one stop body, then the next
    // wav of the item's folder or the rest at the item's start.
    void on_natural_end();
    // The wav rows of the live listing, in listing order.
    std::vector<AppState::FolderOverlayRow> listing_wavs() const;
    // Whether any playable wav exists — the opener's refusal.
    bool has_playable_render() const;
    // The deliverable folder's regular `*.wav` files, byte order.
    std::vector<std::filesystem::path> deliverable_wavs() const;
    // Damage helpers: the band, the modal row.
    void damage_band();
    void damage_row();
    void status(const std::string& line);
};
