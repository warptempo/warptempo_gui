#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for GuiPaintHandler
#include "playback.h"
#include "platform.h"
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
// change on disk between the Open prompt's dry-run below and the load.
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

    bool load_file(const std::string& path);
};

// THE LOAD'S FAILURE ARMS, RUN WITHOUT LOADING (architect 2026-08-27, with the
// project model): the Open prompt's Enter asks this BEFORE anything is torn
// down, so a project that would refuse in load_file refuses in the prompt
// instead — with its first error on the status line and the prompt still open
// — and the reopen's real load can fail only by a change on disk in between,
// which is the adversarial class and takes load_file's own fatal exit. It runs
// exactly the checks load_file runs, in load_file's order, through the same
// owners: the audio probe (magic, the rate floor, stereo), the three STRICT
// whole-file readers on whichever sidecars EXIST (a missing one is what the
// load will template — a new project passes trivially), the source-clobber
// predicate, and the past-EOF walls against the probe's frame count. It writes
// nothing and keeps nothing: every parsed value is discarded. Returns the first
// refusal's one line, or nothing when the load would succeed. The full decode
// is deliberately not run — a wav the probe accepts and the decoder refuses is
// a corrupt-media fault of the class the load's own exit answers.
std::optional<std::string> source_load_dry_run(
    const std::filesystem::path& source);

// Apply a parsed settings file's engine block and the scalar session prefs
// (follow, active_audio_view, active_markers_view, active_tab_view,
// waveform_magnification_level) into `app`. VALUES ONLY — no side effects: the
// caller runs on_resize itself, owning its own side-effect timing. (The list
// lost four prefs 2026-08-27: playback_speed retired, and gui_scale,
// audio_player and projects_repo became per-DEVICE values gui_main reads before
// the window exists — device_config.h. A source load must not write any of
// them.) THE SOURCE LOAD IS THE ONLY CALLER since
// 2026-08-24, when a load in place narrowed to what its undo entry restores —
// the marker pair and the engine block — and so stopped applying a file's view
// keys, tab bands and session prefs at all (the rule is stated at
// GuiInputHandler::apply_recipe_in_place, input_handler.h). The routine stays a
// named routine rather than folding into load_file: it is the whole-file
// values-only apply, and that is one act worth reading as one. SettingsFile is
// visible here via app_state.h -> settings_file.h.
// THE VIEWPORT IS FOR ONE THING AND NOTHING ELSE (codex round 21): this routine
// is the product's ONLY wholesale write of the three active-view fields, so it
// carries the seated pinch's clear (clear_touch_zoom_seat, input_handler.h,
// whose declaration owns the membership) with its own damage. That is a
// lifecycle end rather than a side effect a caller could time differently, so
// the values-only contract above stands.
void apply_settings_engine_and_prefs(AppState& app, Viewport& viewport,
                                     const SettingsFile& sf);
