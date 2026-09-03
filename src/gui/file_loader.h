#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for GuiPaintHandler
#include "playback.h"
#include "platform.h"
#include "project_model.h"   // GuiProjectSource (the load request)
#include "viewport.h"

#include <filesystem>
#include <optional>
#include <string>

struct GuiTargetRender;
struct Selection;

// File-lifecycle operations, extracted from main.cpp's inline
// lambdas. Owns the audio-load → markers-parse → settings-parse →
// playback-init pipeline (load_file), the sole loader, invoked once per
// project from the startup tick of gui_main's loop: a project is opened by
// REBUILDING the object set around its source (the reopen loop, main.cpp),
// never by loading into a standing one, so a failed load exits rather than
// reverting to a blank window — and since 2026-08-27 it can fail only by a
// change on disk between the Open project picker's dry-run below and the load.
struct GuiFileLoader {
    AppState&          app;
    GuiAudio&          audio;
    GuiPlatform&       gui;
    GuiPlayback&       playback;
    Viewport&          viewport;
    GuiTargetRender&   target_render;
    // Loading a source can change app.gui_scale; the load applies it
    // through GuiPaintHandler::on_resize, the same geometry-and-cache
    // rebuild path a window resize takes.
    GuiPaintHandler&   paint_handler;
    Selection&         selection;

    GuiFileLoader(AppState&          app_,
                  GuiAudio&          audio_,
                  GuiPlatform&       gui_,
                  GuiPlayback&       playback_,
                  Viewport&          viewport_,
                  GuiTargetRender&   target_render_,
                  GuiPaintHandler&   paint_handler_,
                  Selection&         selection_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_),
          viewport(viewport_),
          target_render(target_render_),
          paint_handler(paint_handler_),
          selection(selection_) {}

    // THE LOAD TAKES THE RESOLVED PROJECT, not a bare path: the project's NAME
    // is the folder's name under the projects path (project_model.h) and is a
    // fact the caller already holds, so the load is HANDED it rather than
    // deriving one from the source's parent — a derivation that answers a
    // different name whenever a link is in the way. The source is the same
    // object's `source`, so the pair can never be mismatched.
    bool load_file(const GuiProjectSource& project);
};

// THE DRY-RUN IS THE REAL LOAD'S FAILURE VOCABULARY MINUS THE DECODE
// (architect 2026-08-27, with the project model) — the rule, stated here once:
// EVERY DETERMINISTIC REFUSAL load_file CAN RAISE FROM WHAT THE PROBE AND THE
// SIDECARS ALREADY KNOW IS RAISED HERE FIRST. The Open project picker's open
// act asks this BEFORE anything is torn down, so a project that would refuse
// in load_file refuses in the picker instead — its first error on the status
// line, the picker still open and the old session still standing — and the
// reopened
// session's load can then fail only on a CHANGE ON DISK between this check and
// that load, which is the adversarial class and takes load_file's own fatal
// exit — WITH ONE STANDING GAP the dry-run cannot see (recorded 2026-09-02,
// the four-tier review's R-15, ADVERSARIAL by his rubric and left as it is):
// a NEW project whose folder is not writable passes here — nothing is
// created at this stage, and the load's own template creation is advisory —
// and then dies at load_file's strict read of the sidecar that was never
// written, whose sentence names the missing file rather than the failed
// create. An unwritable folder under his own projects path is not a
// supported state, so no writability probe was added. It writes nothing and keeps nothing: every parsed value is discarded.
// Returns the first refusal's one line, or nothing when the load would succeed.
//
// THE INVENTORY, walked in load_file's own order, each refusal through the same
// owner the load calls, so the words are the load's words — WITH ONE
// DELIBERATE EXCEPTION, the STEREO arm, whose card is a proper sentence
// ("The source is not stereo (N channels)") where the load's stderr line is
// the terminal's own "N channels (stereo sources only)": a card is prose and
// a diagnostic is not, the capitalization sweep's rule (recorded 2026-09-02;
// the verdict is identical, only the wording differs):
//
//   (The peaks-cache path refusal that opened the load's own walk is DELETED,
//   2026-09-02: it had no producer left once every source became the project
//   model's composed `<stem>.wav` by that model's exact-extension rule
//   (project_model.h), which is why it was never mirrored here either.)
//
//   * `audio_probe` — the container magic and the malformed-but-recognized WAV
//     diagnostics. Mirrored (the load's convert-once acquisition hint is the
//     terminal's alone).
//   * the 44100 sample-rate floor. Mirrored.
//   * the stereo-only channel count. Mirrored.
//   * `GuiAudio::load`, the full decode. Its ALLOCATION arm is mirrored — the
//     `checked_audio_sample_count` ceiling (wav_io.h) asked of the probed frame
//     and channel counts, a pure shape check needing no bytes. Its other arms
//     are THE DECODE ITSELF (an unstattable file, a truncated or unreadable
//     payload) and are deliberately not mirrored: a wav the probe accepts and
//     the decoder refuses is a corrupt-media fault of the class the load's own
//     exit answers.
//   * the two STRICT marker readers and the STRICT whole-file settings schema,
//     each on a companion that is PRESENT by the load's own presence predicate
//     (`sidecar_present`, settings_io.h — EXISTS, so a non-regular object at a
//     sidecar's name is a parse failure here exactly as it is there); a genuine
//     absence is what the load will template, so a new project passes
//     trivially, and a stat that fails is its own refusal in the system's
//     words. Mirrored.
//   * `render_output_source_collision`, the source-clobber predicate. Mirrored.
//   * `first_past_eof_wall_defect`, the six walls, against the probe's frame
//     count and rate (with no settings on disk the load's own full-window stamp
//     is what the walls see, which is inside them by construction). Mirrored.
//
// WHAT THE LOAD DOES THAT IS NOT A REFUSAL needs no arm here and has none: the
// template creation is advisory, a crossed trim pair NORMALIZES to the full
// window, a target-view restore that will not build forces source view
// silently, and a failed playback init only disables playback.
//
// EVERY REASON IT RETURNS IS TWO CLAUSES (GuiFailure, failure.h): the
// display is a NOTIFICATION CARD'S ONE LINE (the picker's third refusal), so
// a path in it names THE PROJECT FOLDER AND THE FILE and never the projects
// path — shown_project_path, device_config.h, the basename rule's one
// composer (messaging.md) — and the diagnostic, which the picker prints on
// stderr, names the full path.
std::optional<GuiFailure> source_load_dry_run(
    const std::filesystem::path& source);

// Apply a parsed settings file's engine block and the scalar session prefs
// (follow, active_audio_view, active_markers_view, active_tab_view,
// waveform_magnification_level) into `app`. VALUES ONLY — no side effects: the
// caller runs on_resize itself, owning its own side-effect timing. (The list
// lost four prefs 2026-08-27: playback_speed retired, and gui_scale,
// audio_player and projects_repo became per-DEVICE values gui_main reads before
// the window exists — device_config.h, where audio_player then retired whole
// 2026-08-28. A source load must not write any of them.) THE SOURCE LOAD IS THE ONLY CALLER since
// 2026-08-24, when a load in place narrowed to what its undo entry restores —
// the marker pair and the engine block — and so stopped applying a file's view
// keys, tab bands and session prefs at all (the rule is stated at
// GuiInputHandler::apply_recipe_in_place, input_handler.h). The routine stays a
// named routine rather than folding into load_file: it is the whole-file
// values-only apply, and that is one act worth reading as one. SettingsFile is
// visible here via app_state.h -> settings_file.h.
// THE VIEWPORT IS FOR ONE THING AND NOTHING ELSE: this routine
// is the product's ONLY wholesale write of the three active-view fields, so it
// carries the seated pinch's clear (clear_touch_zoom_seat, input_handler.h,
// whose declaration owns the membership) with its own damage. That is a
// lifecycle end rather than a side effect a caller could time differently, so
// the values-only contract above stands.
void apply_settings_engine_and_prefs(AppState& app, Viewport& viewport,
                                     const SettingsFile& sf);
