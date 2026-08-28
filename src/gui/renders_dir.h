#pragma once

#include "app_state.h"

#include <filesystem>
#include <string>
#include <vector>

// THE PROJECT'S TWO OUTPUT FOLDERS (architect 2026-08-27), and this file is
// the one home of both their names and the composition of both their paths.
// A project folder holds its source, the source's sidecar set, `peaks/` and
// the architect's own local material; everything the product WRITES lands in
// one of two folders beside them:
//
//   `tmp/`     THE DISPOSABLE BATCH CELLS — the iteration and BPM sweeps and
//              the miscellaneous cell, `<N>_<tag>/<basename>.wav` with their
//              per-cell sidecar sets. Lowercase and named for what they are:
//              scratch, which the `'` load in place trashes wholesale.
//   `render/`  THE DELIVERABLE, named for the command that writes it: the
//              Ctrl+Alt+R output `<title>.wav` and its `.fingerprint`.
//
// Both are composed from the SOURCE path, whose parent IS the project folder
// (the parent is "." for a bare filename, exactly as render_output_directory
// composes it). resolve_project (project_model.h) ignores directories, so
// neither folder can be mistaken for a source and neither needs an exclusion
// rule of its own. There is no migration: a project still carrying the old
// `renders/` simply has no batches, and a `<title>.wav` still in the project
// root is the legacy layout the project model refuses with the move to make.
inline constexpr const char* kBatchFolderName       = "tmp";
inline constexpr const char* kDeliverableFolderName = "render";

// `<source parent>/tmp` — the batch root every batch dispatcher creates into,
// the `l` listen walk and the `'` load editor enumerate, and the load in
// place's tail trashes.
std::filesystem::path project_batch_root(const std::string& source_audio_path);

// `<source parent>/render` — the deliverable's folder, created at dispatch if
// missing (do_render, render_pipeline.cpp) and read by the target view's
// current-title reuse rung.
std::filesystem::path project_deliverable_root(
    const std::string& source_audio_path);

// Batch-folder enumeration. The directory scan of
// `<source parent>/tmp/<batch>/<basename>.wav` and the per-entry .settings
// path helper. Both are consumed by the `l` listen-to-renders launcher and the
// `'` load editor (load_render_entry_in_place in input_key_dispatch.cpp).
// Reads
// only AppState (the source path); holds no audio, playback, or view state.
// The TYPE keeps its name: what changed in 2026-08-27's rename is the folder
// it walks, not the role it plays.
struct GuiRendersDir {
    AppState& app;

    explicit GuiRendersDir(AppState& app_) : app(app_) {}

    std::vector<AppState::RenderEntry> enumerate_render_entries();
    std::filesystem::path settings_path(
        const AppState::RenderEntry& e);
};
