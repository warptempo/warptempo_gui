#pragma once

#include "app_state.h"
#include "viewport.h"

#include <set>

class GuiAudio;
class GuiPlayback;

// Selection-cluster operations, extracted from main.cpp's inline
// lambdas. The struct holds references to the long-lived state the methods
// read and write; bodies are byte-identical to the originals modulo `this->`
// access on the captured references.
struct Selection {
    AppState&       app;
    const GuiAudio& audio;
    Viewport&       viewport;
    GuiPlayback&    playback;

    Selection(AppState&       app_,
              const GuiAudio& audio_,
              Viewport&       viewport_,
              GuiPlayback&    playback_)
        : app(app_),
          audio(audio_),
          viewport(viewport_),
          playback(playback_) {}

    void repair_last_selected();
    void set_single_selection(int idx);
    void clear_selection();
    bool toggle_selection_membership(int idx);
    void sanitize_selection_after_restore(int n);
    void cycle_selection(bool forward);
    void select_next_marker();
    void select_prev_marker();
    void prune_live_selection();
    void sync_playhead_to_last_selected();
    void jump_playhead_to(int64_t target_sample);
    // Focus a project-trim bound on undo/redo of a settings entry. Compares
    // the pre-restore trim (passed in) against the now-restored app.trim to
    // find the touched bound, selects it (group Trim), and jumps the playhead
    // to it, recentering if offscreen — no zoom, matching marker restore.
    // Both bounds changing anchors on begin (the x creation gesture). A pure
    // engine-settings edit changes no trim and is a no-op. When the restore
    // removed the focused bound, jumps to where it was with nothing selected,
    // mirroring marker-removal.
    void focus_restored_trim(bool before_has_begin, double before_begin_sec,
                             bool before_has_end,   double before_end_sec);
    // Select a project-trim bound ('B' or 'E') as the primary selection
    // (group Trim), dropping other marker selection but additionally selecting
    // a regular marker coincident with the bound if one exists. Used by Home /
    // End after they move the playhead to the trim region's bounds. No-op when
    // the bound is not set. Does not move the playhead — the caller does.
    void select_trim_bound_with_coincident(char which);
};
