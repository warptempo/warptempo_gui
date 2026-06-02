#pragma once

#include "app_state.h"

#include <cstdint>
#include <utility>

class GuiAudio;
class GuiPlatform;
class GuiPlayback;

// X.7.1: viewport mutators and invalidation helpers, extracted from main.cpp's
// inline lambdas. The struct holds references to the long-lived state the
// methods read and write; bodies are byte-identical to the originals modulo
// `this->` access on the captured references.
struct Viewport {
    AppState&                       app;
    const GuiAudio&                 audio;
    GuiPlatform&                         gui;
    GuiPlayback&                    playback;

    Viewport(AppState&                       app_,
             const GuiAudio&                 audio_,
             GuiPlatform&                         gui_,
             GuiPlayback&                    playback_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_) {}

    // Trim helpers.
    std::pair<int64_t, int64_t> trim_range() const;
    int64_t                     trim_begin_sample() const;
    int64_t                     trim_end_sample() const;

    // Viewport mutators.
    void move_playhead_to(int64_t new_sample);
    void move_playhead_pixels(int delta_px);
    void apply_zoom_change(int new_zoom_level);
    void zoom_in();
    void zoom_out();
    void scroll_viewport(int64_t delta_samples);
    void center_viewport_on_playhead();
    void follow_scroll_if_needed();

    // Invalidation.
    void invalidate_waveform_area();
    void invalidate_timestamp_area();
    void invalidate_playhead_columns(double old_px, double new_px);
    void invalidate_top_strip();
    void invalidate_all();

    // Reset the hover popup state. If the popup was visible, invalidate
    // the top strip so the next paint erases it. Safe to call from any
    // path. Promoted from main.cpp's clear_hover_popup lambda.
    void clear_hover_popup();

    // Recompute the hover state at the cursor's last on_motion position.
    // Called from viewport mutators (so a scroll/zoom updates which
    // marker is under the cursor) and from the platform tick (so the
    // dwell-to-visible flip fires after delay). Body promoted from
    // main.cpp's recompute_hover_at_cursor lambda.
    void recompute_hover_at_cursor();
};
