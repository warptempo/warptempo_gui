#pragma once

#include "app_state.h"

#include <filesystem>
#include <string>
#include <vector>

// THE BATCH FOLDER (architect 2026-08-27), and this file is the one home of
// its name and the composition of its path. A project folder holds its source,
// the source's sidecar set, `peaks/` and the architect's own local material;
// everything the product WRITES lands in a folder beside them, and `tmp/` is
// THE DISPOSABLE BATCH CELLS — the iteration and BPM sweeps and the
// miscellaneous cell, `<N>_<tag>/<basename>.wav` with their per-cell sidecar
// sets. Lowercase and named for what they are: scratch, which the `'` load in
// place trashes wholesale. The batch cells are the GUI's alone, which is why
// their folder is named here; THE DELIVERABLE'S FOLDER IS THE PARSER'S, both
// products writing it — kDeliverableFolderName / render_output_directory,
// render_output_naming.h.
//
// The root below is composed from the SOURCE path, whose parent IS the project
// folder (the parent is "." for a bare filename, exactly as
// render_output_directory composes it). resolve_project (project_model.h)
// ignores directories, so neither output folder can be mistaken for a source
// and neither needs an exclusion rule of its own. There is no migration: a
// project still carrying the old `renders/` simply has no batches, and a
// `<title>.wav` still in the project root is the legacy layout the project
// model refuses with the move to make.
inline constexpr const char* kBatchFolderName = "tmp";

// `<source parent>/tmp` — the batch root every batch dispatcher creates into,
// the `l` listen walk and the `'` load editor enumerate, and the load in
// place's tail trashes.
std::filesystem::path project_batch_root(const std::string& source_audio_path);

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

// THE RENDER ENTRY'S ID — its path relative to tmp/, `<batch_dir>/<basename>
// .wav`, always folder-qualified. One path per file, so the id is unique by
// filesystem construction; Tab autocomplete then discriminates on the short
// leading batch-folder name instead of deep value decimals inside
// near-identical cell basenames, and the painted `Load: <projects_path>/
// <name>/tmp/<id>` line is the entry's real on-disk path. TWO READERS since
// 2026-08-28, which is why it lives here rather than as the load editor's
// static: the `'` load editor resolves the typed identifier against these
// strings (load_editor_commit, input_key_dispatch.cpp), and the render
// player's load confirmation spells the entry it asks about with it
// ("Load `3_bpm/02.wav` in place?").
inline std::string render_entry_id(const AppState::RenderEntry& e) {
    return e.batch_folder.filename().string() + "/" + e.basename + ".wav";
}
