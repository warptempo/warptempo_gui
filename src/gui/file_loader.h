#pragma once

#include "app_state.h"
#include "audio.h"
#include "paint_handler.h"   // for GuiPaintHandler
#include "playback.h"
#include "platform_wayland.h"
#include "viewport.h"

#include <string>

struct GuiTargetRender;
struct Selection;

// File-lifecycle operations, extracted from main.cpp's inline
// lambdas. Owns the audio-load → markers-parse → settings-parse →
// playback-init pipeline (load_file), the sole loader, invoked once from
// the startup tick: the GUI has no in-session file open or drag-and-drop,
// so a failed load exits rather than reverting to a blank window.
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

// Apply a parsed settings file's engine block and the scalar session prefs
// (follow, active_audio_view, active_markers_view, active_tab_view,
// playback_speed, gui_scale, waveform_magnification_level, audio_player,
// projects_repo) into `app`. VALUES
// ONLY — no side
// effects: the caller runs set_speed / set_gui_scale_percent / on_resize itself,
// owning its own side-effect timing. THE SOURCE LOAD IS THE ONLY CALLER since
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
