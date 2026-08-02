#include "save_ops.h"

#include "settings_io.h"

#include <clocale>
#include <cstdio>
#include <cstring>

bool GuiSaveOps::save() {
    // Startup's locale_check.h tripwire covers launch; this catches a
    // dynamically loaded module changing numeric locale mid-session so refusal
    // preserves authored sidecars instead of writing corrupted numbers.
    const char* lc = std::setlocale(LC_NUMERIC, nullptr);
    if (!lc || std::strcmp(lc, "C") != 0) {
        std::fprintf(stderr,
            "warptempo_gui: numeric locale is '%s', not 'C'; refusing to save "
            "(sidecar numbers would be written corrupted)\n",
            lc ? lc : "(null)");
        return false;
    }

    if (app.warpmarkers_path.empty()) return false;
    // Capture the active tab's current values before any writes — both
    // the .warpmarkers and .settings paths see a consistent snapshot.
    active_views.refresh_active_tab_view_from_app();

    // The three sidecars publish sequentially, each individually atomic
    // (tmp + fsync + rename). This is deliberately NOT a cross-file
    // transaction: if a later write fails (disk full, an I/O error) after an
    // earlier one has renamed, disk holds a mixed-generation set until the
    // next save. Accepted — the non-adversarial single user reaches this only
    // on a real disk/IO fault, which is non-silent (the failing write prints
    // its path and `dirty` survives below, so the unsaved-work prompt offers
    // retry). A true fix would need one combined project file, a frozen-parser
    // change out of scope for this tool.
    const bool ok = app.warpmarkers.save(app.warpmarkers_path);
    if (!ok) {
        std::fprintf(stderr,
            "warptempo_gui: save failed: %s\n",
            app.warpmarkers_path.c_str());
        return false;
    }

    // .phaseresetmarkers is the companion authoring sidecar to .warpmarkers.
    // Empty phase-reset lists serialize as empty files, keeping Ctrl+S
    // symmetric and preventing stale on-disk resets without making absence
    // carry meaning.
    if (!app.phaseresetmarkers_path.empty()) {
        if (!app.phaseresetmarkers.save(app.phaseresetmarkers_path)) {
            std::fprintf(stderr,
                "warptempo_gui: phase_reset save failed: %s\n",
                app.phaseresetmarkers_path.c_str());
            return false;
        }
    }

    // .settings carries settings/state that Ctrl+S persists, so a failed
    // write fails the save: dirty must survive so the unsaved-work dialog can
    // offer retry instead of silently baselining in-memory-only state.
    if (!app.settings_path.empty()) {
        const NonEngineSettingsSnapshot gui{
            app.tab_a, app.tab_b,
            app.follow_mode,
            app.active_audio_view,
            app.active_markers_view,
            app.active_tab_view,
            app.playback_speed,
            app.gui_scale,
            app.audio_player,
            app.libm_hash,
            app.libmvec_hash,
            app.fftw3_hash,
            app.fftw3_threads_hash};
        if (!write_settings_file(app.settings_path, gui,
                                 app.engine_settings)) {
            std::fprintf(stderr,
                "warptempo_gui: settings save failed: %s\n",
                app.settings_path.c_str());
            return false;
        }
    }

    // Save rebinds the saved reference to the current timeline position
    // without touching either stack — undo still reverts the last op.
    //
    // NO DAMAGE ON THE CLEAN EDGE (2026-08-01): the dirty state's only display
    // was the bottom row's dot, and the dot moved to the WINDOW TITLE, which the
    // compositor repaints on its own. recompute_dirty pushes the new flag to the
    // title itself, so the repaint this used to request has nothing left to
    // redraw.
    app.history.mark_saved();
    undo.recompute_dirty();
    return true;
}
