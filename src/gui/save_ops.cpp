#include "save_ops.h"

#include "settings_io.h"

#include <clocale>
#include <cstdio>
#include <cstring>

bool GuiSaveOps::save() {
    // NO SAVE WHILE A CHECKPOINT IS PUBLISHING (architect 2026-08-08). The
    // checkpoint worker writes the three sidecars into projects/<id>/ on its own
    // thread, and that folder is the one the loaded SOURCE sits in — the
    // coincident workflow when this landed, and since 2026-08-09 the only shape
    // a checkpointable piece has, the source's folder BEING the project
    // directory — so those are EXACTLY the three paths this function writes,
    // through the same fixed `<path>.tmp` temp name. Two writers on one
    // temp name is a torn temp or a rename of captured-older bytes over a newer
    // save, so the window is closed by refusing the save rather than by naming
    // the temp files per-writer: with saves refused for the act's duration there
    // is no concurrent writer of those three paths at all (a second checkpoint
    // act is already refused, single-in-flight).
    //
    // ONE TERM, EVERY CALLER: the Ctrl+S dispatch, the editors' own Ctrl+S
    // admission (route_modal_editor_key), the picker router's Ctrl+S and the
    // close prompt's Save answer all funnel through here, so the lockout needs
    // no second spelling.
    //
    // THE CLOSE PROMPT'S Save answer IS REACHABLE HERE, and it answers with the arm
    // it already has: the act saves before it dispatches, so the session is
    // CLEAN at that moment and only an edit made DURING the publish can raise
    // that prompt at all — and then the refusal below takes the prompt's
    // existing "Retry the failed save?" / Retry rung, where one retry a moment
    // later
    // succeeds (the bit falls when the worker reports; quit's own join, which
    // blocks on the act, happens after the prompt is answered rather than
    // before it). Accepted as it stands: the window is seconds wide, the state
    // is never lost, and Discard (the Del key) still quits — a second failure vocabulary for
    // "busy, try again" would be a new surface for a self-correcting wait.
    //
    // THE ACT'S OWN PRELUDE SAVE IS EXEMPT BY ORDERING, not by a flag:
    // run_history_commit calls this BEFORE it sets the bit (input_key_dispatch.
    // cpp — save, capture, close, dispatch, in that order), so the save that is
    // part of the act runs while nothing is in flight.
    //
    // SILENT, and the face is the message: the Save button reads "Committing..."
    // and is disabled off this same bit (redesign_button_enabled, app_state.h),
    // so a refusal here is a consumed nothing the user was already told about.
    if (app.history_checkpoint_in_flight) return false;

    // Startup's locale_check.h tripwire covers launch; this catches a
    // dynamically loaded module changing numeric locale mid-session so refusal
    // preserves authored sidecars instead of writing corrupted numbers.
    const char* lc = std::setlocale(LC_NUMERIC, nullptr);
    if (!lc || std::strcmp(lc, "C") != 0) {
        std::fprintf(stderr,
            "warptempo_gui: Numeric locale is '%s', not 'C'; refusing to save "
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
            "warptempo_gui: Save failed: %s\n",
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
            app.centered_mode,
            app.active_audio_view,
            app.active_markers_view,
            app.active_tab_view,
            app.waveform_magnification_level};
        if (!write_settings_file(app.settings_path, gui,
                                 app.engine_settings)) {
            std::fprintf(stderr,
                "warptempo_gui: Settings save failed: %s\n",
                app.settings_path.c_str());
            return false;
        }
    }

    // Save rebinds the saved reference to the current timeline position
    // without touching either stack — undo still reverts the last op — AND
    // ENDS THE UNDO TAP WINDOW (Undo::note_saved, the one tail, 2026-09-02):
    // the reference move and the coalescing stamp's clear are one act at the
    // undo owner, so a nudge tap after this save opens its own entry instead
    // of merging into the entry the reference now rests on and reading clean
    // over a store that differs from the file.
    //
    // NO DAMAGE ON THE CLEAN EDGE (2026-08-01): the dirty state's only display
    // was the bottom row's dot, and the dot moved to the WINDOW TITLE, which the
    // compositor repaints on its own. recompute_dirty (note_saved's tail) pushes
    // the new flag to the title itself, so the repaint this used to request has
    // nothing left to redraw.
    undo.note_saved();
    return true;
}
