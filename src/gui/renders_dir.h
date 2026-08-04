#pragma once

#include "app_state.h"

#include <filesystem>
#include <vector>

// Renders-folder enumeration. The directory scan of
// <source_parent>/renders/<batch>/<basename>.wav and the per-entry .settings
// path helper. Both are consumed by the `l` listen-to-renders launcher and the
// `'` load editor (load_render_entry_in_place in input_key_dispatch.cpp).
// Reads
// only AppState (the source path); holds no audio, playback, or view state.
struct GuiRendersDir {
    AppState& app;

    explicit GuiRendersDir(AppState& app_) : app(app_) {}

    std::vector<AppState::RenderEntry> enumerate_render_entries();
    std::filesystem::path settings_path(
        const AppState::RenderEntry& e);
};
