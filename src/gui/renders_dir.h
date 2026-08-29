#pragma once

#include "app_state.h"
#include "engine_settings.h"

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
// render_output_naming.h. What that folder has here is the PRUNE below, which
// keeps it to the current title's deliverable alone: the parser NAMES the
// folder because both products write it, and the GUI KEEPS it because the
// player is what lists it.
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
// the RENDER PLAYER lists (its `tmp/` folders) and the Synchronize act
// mirrors, and the load in place's tail trashes.
std::filesystem::path project_batch_root(const std::string& source_audio_path);

// THE DELIVERABLE FOLDER'S PRUNE (architect 2026-08-29: "player should only
// list a file if it matches the current title, and delete the rest also").
//
// THE DEFINITION: `render/` holds THE CURRENT TITLE'S DELIVERABLE AND NOTHING
// ELSE — `<title>.wav` and its `<title>.fingerprint` — so what the Synchronize
// mirror keeps on the stick is exactly what this keeps on disk. The two sets
// are ONE definition, external_sync.h rule 1's, which is why a retitle no
// longer leaves the previous title's deliverable behind for the player to list
// and the mirror to sweep off the volume on its next run.
//
// TWO CALLERS, both named at the site that makes them: the RENDER PLAYER'S
// LISTING (GuiRenderPlayer::deliverable_wav, render_player.cpp — which is
// also the opener's "is there anything to play" question and the root row's)
// and THE DELIVERABLE'S PUBLISH (the single archival render's GUI-thread
// completion, dispatch_single_archival_render in input_render_dispatch.cpp,
// on Success and on the deliverable arm alone — a batch cell publishes into
// `tmp/`, a different folder, which is never pruned). The publish's prune runs
// after do_render has written the wav AND its fingerprint, so the current
// title's pair is complete on disk before any other title's pair goes.
//
// WARPTEMPO_CLI GETS NO PRUNE, the recorded asymmetry: it writes the same
// `render/<title>.wav` through the same parser owner, but it is the headless
// insurance render and this is GUI-side machinery — a CLI render leaves a
// previous title's deliverable standing until the GUI next lists the folder or
// publishes into it, which is the first moment the definition is asked for.
//
// A RUNNING RENDER IS NEVER AT RISK. Every deliverable publishes through the
// staging name `<title>.wav.tmp` and a rename (render_staging_path), so a wav
// being written is not a `.wav` at all and no walk here can see it; and a
// render publishing the CURRENT title's deliverable is the match this keeps.
// A render publishing a PREVIOUS title's — the title was edited in the
// settings editor while the render ran, which kills nothing — races only its
// own already-renamed wav: the prune may take that wav between the rename and
// the fingerprint write, leaving an orphan sidecar the next prune removes.
// That is the ruling working, not a breach: the folder is the current title's.
//
// REFUSALS. An empty source path or an empty stem makes the prune a NO-OP that
// deletes nothing — never "delete everything because nothing matches" (the
// title's grammar refuses empty at every input surface, validate_engine_setting
// in engine_settings_io.cpp; this is the breach backstop). A directory that is
// not there is nothing to prune. The classification runs to completion before
// the first removal, so no directory_iterator is ever live while its own
// directory is being changed, and the non-throwing walk's `ec` ends the walk
// where it stands — every entry it did classify is still a positive
// identification, so those are removed and nothing beyond them is. A `remove`
// failure is one stderr line and the loop continues.
//
// WHAT IT NEVER TOUCHES: directories, and every extension other than `.wav`
// and the fingerprint sidecar's (render_cache.h owns that spelling), so a
// `.peaks`, a staging `.tmp` and the architect's own material in there survive.
void prune_render_folder(const std::string& source_audio_path,
                         const EngineSettings& es);

// Batch-folder enumeration. The directory scan of
// `<source parent>/tmp/<batch>/<basename>.wav` and the per-entry .settings
// path helper. The scan is the RENDER PLAYER's (its `tmp/` listings and its
// "are there any cells" question, render_player.cpp); the .settings helper is
// the load act's (load_render_entry_in_place, input_key_dispatch.cpp).
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
// filesystem construction, and the folder-qualified spelling is the entry's
// real on-disk path under tmp/. ONE READER, re-greped: the RENDER PLAYER's
// load confirmation, which spells the entry it asks about with it ("Load
// `3_bpm/02.wav` in place?", render_player_load_in_place in
// input_key_dispatch.cpp). It lives here rather than at that one site because
// the id is a property of the entry, which this header owns. (The typed load
// editor resolved a user's typed identifier against these strings, and its
// Tab completed over them, until the player took the renders subject on
// 2026-08-28.)
inline std::string render_entry_id(const AppState::RenderEntry& e) {
    return e.batch_folder.filename().string() + "/" + e.basename + ".wav";
}
