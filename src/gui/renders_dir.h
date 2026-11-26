#pragma once

#include "app_state.h"
#include "engine_settings.h"
#include "failure.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// THE BATCH FOLDER (architect 2026-08-27), and this file is the one home of
// its name and the composition of its path. A project folder holds its source,
// the source's sidecar set, `peaks/` and the architect's own local material;
// everything the product WRITES lands in a folder beside them, and `tmp/` is
// THE DISPOSABLE BATCH CELLS — the iteration and BPM sweeps, the
// miscellaneous cell and the trim presets' renders,
// `<N>_<tag>/<basename>.wav` with their per-cell sidecar
// sets. Lowercase and named for what they are: scratch, which the `'` load in
// place deletes wholesale (every batch folder, permanently). The batch cells are the GUI's alone, which is why
// their folder is named here; THE DELIVERABLE'S FOLDER IS THE PARSER'S, both
// products writing it — kDeliverableFolderName / render_output_directory,
// render_output_naming.h. What that folder has here is the PRUNE below, which
// keeps it to the current title's deliverable alone: the parser NAMES the
// folder because both products write it, and the GUI KEEPS it because the
// GUI's own archival render is what publishes into it.
//
// The root below is composed from the SOURCE path, whose parent IS the project
// folder (the parent is "." for a bare filename, exactly as
// render_output_directory composes it). resolve_project (project_model.h)
// ignores directories, so neither output folder can be mistaken for a source
// and neither needs an exclusion rule of its own. A `<title>.wav` in the
// project root is a layout the project model refuses with the move to make.
inline constexpr const char* kBatchFolderName = "tmp";

// `<source parent>/tmp` — the batch root every batch dispatcher creates into,
// the RENDER PLAYER lists (its `tmp/` folders) and the load in place's tail
// empties.
std::filesystem::path project_batch_root(const std::string& source_audio_path);

// THE BATCH ROOT'S NUMBERING. It lives here because the scan is pure
// filesystem over the root this file already composes, no AppState reader in
// it, and THE RENDER PLAYER'S OPEN needs the same answer as the input
// handler's dispatchers with no back-pointer into that class. Result of one walk over a batch root: the highest leading-index
// `<digits>_...` folder, and that folder's own filename.
struct RendersBatchScan {
    int         max_index = 0;             // 0 when none / dir missing
    std::string max_index_folder_name;     // filename of the max-index
                                           // folder; empty when none
};

// Scan `renders_dir` for the highest leading-index batch folder. FIVE CALL
// SITES share this one walk (the one list of them; every other site points
// here): the iteration sweep (run_iteration_sweep_render) and the BPM sweep
// (render_bpm_sweep) use `max_index + 1` alone for their next batch folder (a
// missing/empty dir yields max_index 0, so the first folder is index 1); the
// miscellaneous cell's allocator (allocate_miscellaneous_cell,
// Ctrl+Alt+Shift+R) also reads `max_index_folder_name` to decide
// append-vs-new; the trim presets' allocator (allocate_trim_preset_folder)
// reads it for the plain Render's append-vs-new and `max_index + 1` alone
// for Render All's always-new folder; and THE RENDER PLAYER'S OPEN
// (GuiRenderPlayer::open) reads `max_index_folder_name` to name the NEWEST
// batch folder to enter (architect 2026-09-15) — "most recent" being this
// index, never a filesystem timestamp a sync or a copy can disturb. A tie keeps the first
// `<digits>_` folder at that index (strict `>` update), exact for the
// always->=1 product folders.
RendersBatchScan max_renders_batch_index(
    const std::filesystem::path& renders_dir);

// THE TRIM PRESETS' BATCH FOLDER (architect 2026-11-25: "New render folder
// category"), `<N>_presets/`: the presets list's Render joins the newest
// folder when it is one, else makes a new one, and its Render All always makes
// a new one (GuiInputHandler::trim_presets_render). The token is DATA,
// lowercase (the BPM sweep's `bpm` token's rule, render_bpm_sweep). ITS CELLS
// TAKE THE SWEEPS' INDEX STYLE, `<number>_<name>.wav` (trim_presets_render's
// cell naming: Render All numbers from 1, a Render takes the folder's next
// number), so the folder lists by every folder's one numbered rule
// (GuiRendersDir::enumerate_render_entries).
inline constexpr const char* kTrimPresetsBatchTag = "presets";
inline bool is_trim_presets_batch_folder(const std::string& folder_name) {
    return folder_name.ends_with(std::string("_") + kTrimPresetsBatchTag);
}

// THE FAILURE WHEN A BATCH FOLDER CANNOT BE MADE (architect 2026-08-30; the
// two-clause shape 2026-09-02). FOUR CALL SITES create one under the root
// above (the one list of them; every other site points here) — the
// iteration sweep (run_iteration_sweep_render), the BPM sweep
// (render_bpm_sweep), the miscellaneous cell's allocator
// (allocate_miscellaneous_cell) and the trim presets' allocator
// (allocate_trim_preset_folder, serving Render and Render All; architect
// 2026-11-25). This composes BOTH halves of
// their failure once, so they cannot drift on either surface
// (GuiFailure, failure.h): the diagnostic is the tagged stderr line with the
// WHOLE path (`render-bpm: Could not create '/…/tmp/3_bpm': Permission
// denied`), and the display is the card's. A PATH IN A SENTENCE IS ITS BASENAME (failure.h), which on the
// card is the folder the render would have gone into — `3_iterations`,
// `2_miscellaneous` — the only part of it the user authored. SINGLE-QUOTED,
// the product's one quoting form on a card (notifications.h's card rules).
// `tag` is the road's own stderr tag, the
// one thing the lines differ by. Advisory in class: the dispatch
// refuses and cards, and no partial batch is left.
inline GuiFailure render_folder_creation_failure(
        const char*                  tag,
        const std::filesystem::path& folder,
        const std::error_code&       ec) {
    GuiFailure f;
    f.diagnostic = std::string(tag) + ": Could not create '" +
                   folder.string() + "': " + ec.message();
    f.display    = "Could not create the render folder '" +
                   folder.filename().string() + "': " + ec.message();
    return f;
}

// THE DELIVERABLE FOLDER'S PRUNE (architect 2026-08-29: "player should only
// list a file if it matches the current title, and delete the rest also").
//
// THE DEFINITION: `render/` holds THE CURRENT TITLE'S DELIVERABLE AND NOTHING
// ELSE — `<title>.wav` and its `<title>.fingerprint` — and THIS IS THE ONE
// PLACE THAT DEFINITION LIVES. A retitle leaves the previous title's pair
// standing until this prune takes it off disk.
//
// ONE CALLER, named at the site that makes it: THE DELIVERABLE'S PUBLISH (the single archival render's GUI-thread
// completion, dispatch_single_archival_render in input_render_dispatch.cpp,
// on Success and on the deliverable arm alone — a batch cell publishes into
// `tmp/`, a different folder, which is never pruned). The publish's prune runs
// after do_render has written the wav AND its fingerprint, so the current
// title's pair is complete on disk before any other title's pair goes. The
// render player lists `tmp/` alone, never `render/` (the ruling at the head
// of render_player.h), so a stale pair survives until the next deliverable
// publishes.
//
// WARPTEMPO_CLI GETS NO PRUNE — the FIRST of the CLI's four recorded
// asymmetries, which are enumerated once at `cli_main.cpp`'s publish and
// nowhere else: it writes the same
// `render/<title>.wav` through the same parser owner, but it is the headless
// insurance render and this is GUI-side machinery — a CLI render leaves a
// previous title's deliverable standing until the GUI next PUBLISHES into the
// folder, which is the one moment the definition is asked for.
//
// A RUNNING RENDER IS NEVER AT RISK. Every deliverable publishes through the
// staging name `<title>.wav.tmp` and a rename (render_staging_path), so a wav
// being written is not a `.wav` at all and no walk here can see it; and a
// render publishing the CURRENT title's deliverable is the match this keeps.
// A render publishing a PREVIOUS title's — the title was edited in the
// settings editor while the render ran, which kills nothing — is the accepted
// adversarial case recorded at the prune's one trigger
// (input_render_dispatch.cpp's completion), and that record is the one to
// read: the whole pair goes at that render's own completion. There is no race
// to describe here, this trigger being the only one and running on the GUI
// thread after do_render has written both files.
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

// THE ONE DELETION OF A BATCH FOLDER (architect 2026-09-29): PERMANENT — a
// native remove_all, never a trash — and BOUNDED: `folder` goes
// only if it is a directory standing DIRECTLY under `batch_root` (the
// project's `tmp/`, project_batch_root), so nothing outside `tmp/` and never
// `tmp/` itself can go. The verdict says which way it went, and the callers
// report it on their own surfaces (the answers differ by act):
//   Removed     — the folder is gone;
//   OutOfBounds — not a directory directly under `batch_root` (a stray path,
//                 `tmp/` itself, a last component of `.` or `..`, a file,
//                 or a status query that failed — indeterminate is not a
//                 directory), left alone;
//   Failed      — the remove itself failed, `ec` holding the system's words
//                 (a partial removal leaves what it could not take).
// The bounds test is a BREACH-CLASS BELT on a destructive act: both callers'
// paths come from that root's own listing, so no producer reaches
// OutOfBounds, which each answers on stderr alone; Failed is IO.
// TWO CALLERS: the render player's Delete (GuiRenderPlayer::
// delete_batch_folders, the folders a listing parked — the root's, the
// listed folder from inside it, a folder's last render's) and the load in
// place's tail (load_render_entry_in_place, every folder under `tmp/` through
// list_batch_folders below). No trash: the tablet's trash cannot be
// recovered, and by the time he loads a cell in place he has chosen his
// keeper.
enum class BatchFolderRemoval { Removed, OutOfBounds, Failed };
BatchFolderRemoval remove_batch_folder(const std::filesystem::path& batch_root,
                                       const std::filesystem::path& folder,
                                       std::error_code& ec);

// THE BATCH CELL'S FILE SET, the one enumeration of what a cell is on disk:
// the wav, its staging sibling, the fingerprint attestation, and the three
// load-in-place sidecars do_render writes beside a batch wav
// (render_pipeline.cpp's publish_load_in_place_critical_batch_sidecars
// authors them; the load road composes the same three,
// input_key_dispatch.cpp). Every path is COMPOSED from its own owner
// (compose_render_output_path, render_staging_path, fingerprint_sidecar_path,
// sidecar_path) from the cell's folder and basename, never found by walking
// the folder, so a removal takes what that cell is and nothing that happens
// to sit beside it. TWO READERS, each removing the set with its own report:
// a failed cell's removal (remove_failed_batch_cell,
// input_render_dispatch.cpp) and the render player's one-render delete
// (GuiRenderPlayer::delete_render, architect 2026-11-25).
std::vector<std::filesystem::path> batch_cell_file_set(
    const std::filesystem::path& folder, const std::string& basename);

// EVERY DIRECTORY DIRECTLY UNDER `batch_root`, classified WHOLE before the
// caller removes anything, so no iterator is live while the folder changes
// (the prune's own shape). Non-throwing (directory_walk.h): `ec` holds the
// fault a failed status query or walk met, and the list is what the walk saw.
// An absent root, or a root that is not a directory, is an empty list with
// `ec` clear. Entries that are not directories — no product writes a file
// there — are left out and left alone.
std::vector<std::filesystem::path> list_batch_folders(
    const std::filesystem::path& batch_root, std::error_code& ec);

// A BATCH CELL'S NUMBER, the one parse of it: a wav stem's leading run of
// digits, followed by `_` or the stem's end (`3`, `03_2nd theme` -> 3; the
// digits after the first underscore are the descriptor's, a trim preset's
// name may begin with them), or std::nullopt for a stem that is no numbered
// cell. TWO READERS: the listing's order (GuiRendersDir::
// enumerate_render_entries) and the trim preset's cell allocator, whose next
// number is the folder's highest plus one (GuiInputHandler::
// allocate_trim_preset_cell; architect 2026-11-25), so the number a cell is
// listed by is the number the allocator counts.
std::optional<int> render_cell_number(std::string_view stem);

// Batch-folder enumeration. The directory scan of
// `<source parent>/tmp/<batch>/<basename>.wav` and the per-entry .settings
// path helper. The scan is the RENDER PLAYER's (its `tmp/` listings and its
// "are there any cells" question, render_player.cpp); the .settings helper is
// the load act's (load_render_entry_in_place, input_key_dispatch.cpp).
// Reads
// only AppState (the source path); holds no audio, playback, or view state.
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
// real on-disk path under tmp/. FOUR READERS, the load in place's two
// (input_key_dispatch.cpp) — the RENDER PLAYER's load confirmation, which
// spells the entry it asks about with it, single-quoted ("Load
// '3_bpm/02.wav' in place?", render_player_load_in_place), and the load's
// missing-wav refusal (load_render_entry_in_place) — and one render's
// delete's two (architect 2026-11-25): its question ("Delete
// '3_bpm/02.wav'?", render_player_delete) and its failure card
// (GuiRenderPlayer::delete_render). It lives here because
// the id is a property of the entry, which this header owns.
inline std::string render_entry_id(const AppState::RenderEntry& e) {
    return e.batch_folder.filename().string() + "/" + e.basename + ".wav";
}
