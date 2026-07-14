#pragma once

#include "app_state.h"

#include <filesystem>
#include <vector>

// Renders-folder enumeration. What remains of the retired render view: the
// directory scan of <source_parent>/renders/<batch>/<basename>.wav and the
// per-entry .settings path helper. Both are consumed by the `l`
// listen-to-renders launcher and the Shift+. commit editor (adopt_render_entry
// in input_key_dispatch.cpp). Reads only AppState (the source path); holds no
// audio, playback, or view state.
struct GuiRenderView {
    AppState& app;

    explicit GuiRenderView(AppState& app_) : app(app_) {}

    std::vector<AppState::RenderViewEntry> enumerate_render_view_list();
    std::filesystem::path settings_path(
        const AppState::RenderViewEntry& e);
};
