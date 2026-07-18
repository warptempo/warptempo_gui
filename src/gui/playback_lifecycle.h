#pragma once

#include "app_state.h"
#include "audio.h"
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

// Playback-orchestration operations, extracted from main.cpp's inline lambdas.
// Owns the four GUI-level wrappers around GuiPlayback's mechanism: stop on
// gesture, restore the visible playhead at end-of-play, toggle play/stop, and
// apply a new speed. AppState, Viewport, GuiPlatform, and GuiAudio are
// captured directly. GuiPlayback stays a pure mechanism class — these
// operations live one layer up.
struct GuiPlaybackLifecycle {
    AppState&         app;
    const GuiAudio&   audio;
    GuiPlatform&      gui;
    GuiPlayback&      playback;
    Viewport&         viewport;

    GuiPlaybackLifecycle(AppState&         app_,
                         const GuiAudio&   audio_,
                         GuiPlatform&      gui_,
                         GuiPlayback&      playback_,
                         Viewport&         viewport_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          viewport(viewport_) {}

    void stop_playback_if_playing();
    void hold_natural_end_scanner(int64_t endpoint_sample);
    void restore_playhead_to_lsp();
    // launch_offset shifts the SCANNER's launch position (and the play() launch
    // bound) forward in the active paint domain WITHOUT moving the resting
    // cursor, so stop snaps the scanner back onto the unmoved cursor. Non-zero
    // only for the target-view Alt+Space audition (start from cursor + N/2);
    // the default 0 keeps plain Space and every other caller byte-identical.
    // The offset is applied only in the target-view branch; the offset launch
    // is re-validated against the target buffer's domain, so an offset landing
    // at or past the buffer end is a silent no-op.
    void toggle_playback(int64_t launch_offset = 0);
    void set_playback_speed(float s);

    // Reseek the active playback session to a new starting sample, keeping
    // audio alive. The sample is expressed in the active playhead domain
    // (source-domain in source view; target-domain in target view). Handles
    // the target-view target_buffer translation internally. Caller is
    // responsible for the entry-state
    // check (was_playing AND sample != playhead_at_entry); this function
    // unconditionally reseeks when called. For target view, samples
    // outside the target buffer's range fall back to playback.stop() —
    // keep-alive intent is well-defined for in-range positions only.
    void reseek_keeping_alive(int64_t sample);

    // Set follow mode to `desired`. Shared by the bare-`f` toggle (which passes
    // !app.follow_mode) and the settings editor's `follow=` commit (which passes
    // the parsed value) so the two stay one implementation. An off→on edge
    // during live playback clears the manual-pan suppression and resyncs so
    // follow resumes paging, not just the one initial jump; with playback
    // stopped (the settings editor is modal, so its open stopped playback) the
    // edge branch is inert and this is a plain field set.
    void set_follow_mode(bool desired);
};
