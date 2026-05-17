#include "save_ops.h"

#include "settings_io.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

bool GuiSaveOps::save() {
    if (app.warpmarkers_path.empty()) return false;
    if (app.first_save_pending && app.warpmarkers.had_nonstandard_content()) {
        std::fprintf(stderr,
            "warptempo_gui: first save in this session will discard "
            "comments and freeform text from %s. Canonical format will "
            "be written.\n",
            app.warpmarkers_path.c_str());
    }
    // Capture the active tab's current values before any writes — both
    // the .warpmarkers and .settings paths see a consistent snapshot.
    active_views.refresh_active_tab_view_from_app();

    const bool ok = app.warpmarkers.save(app.warpmarkers_path);
    if (!ok) {
        std::fprintf(stderr,
            "warptempo_gui: save failed: %s\n",
            app.warpmarkers_path.c_str());
        return false;
    }

    // Phase resets sibling write. Empty list deletes the on-disk file so
    // a project never carries a stale empty .phaseresetmarkers.
    if (!app.phase_reset_markers_path.empty()) {
        if (app.phase_reset_markers.markers().empty()) {
            if (!app.phase_reset_markers.delete_file(app.phase_reset_markers_path)) {
                std::fprintf(stderr,
                    "warptempo_gui: failed to delete: %s\n",
                    app.phase_reset_markers_path.c_str());
            }
        } else {
            if (!app.phase_reset_markers.save(app.phase_reset_markers_path)) {
                std::fprintf(stderr,
                    "warptempo_gui: phase_reset save failed: %s\n",
                    app.phase_reset_markers_path.c_str());
                return false;
            }
        }
    }

    app.first_save_pending = false;
    // Save rebinds the saved reference to the current timeline position
    // without touching either stack — undo still reverts the last op.
    const bool was_dirty = app.dirty;
    app.history.mark_saved();
    undo.recompute_dirty();
    if (was_dirty != app.dirty) {
        viewport.invalidate_timestamp_area();
    }

    // Best-effort .settings write. Failure is logged but does not fail
    // the overall save — the .warpmarkers write is the primary target.
    if (!app.settings_path.empty()) {
        if (!write_settings_file(app.settings_path,
                                 app.tab_a, app.tab_b,
                                 app.follow_mode,
                                 app.active_audio_view,
                                 app.active_markers_view,
                                 app.active_tab_view,
                                 app.playback_speed,
                                 app.engine_settings)) {
            std::fprintf(stderr,
                "warptempo_gui: settings save failed: %s: %s\n",
                app.settings_path.c_str(),
                std::strerror(errno));
        }
    }
    return true;
}
