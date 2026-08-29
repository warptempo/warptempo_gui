#pragma once

#include "device_config.h"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

// THE PROJECT MODEL (architect 2026-08-27): A PROJECT IS A FOLDER DIRECTLY
// UNDER THE PROJECTS PATH, one level deep, and the folder's NAME is the
// project's name ("550 - 1"). `projects_path` is a DEVICE CONFIG key
// (device_config.h) — the laptop's is the clone's own `projects/`, the tablet's
// the app's external files dir's `projects/`, exactly the folder the sync
// convention pushes into — and this file owns the three questions the model
// asks of a filesystem and nothing else: what a folder's SOURCE is, which
// folders there are, and which one the program opens at startup. It creates
// nothing and reads no sidecar's CONTENT; the strict readers are the loaders'
// (file_loader.{h,cpp} and the frozen parser behind it).
//
// THE SOURCE IS DEFINED BY THE SIDECAR, AND BY NOTHING ELSE. A folder that
// carries any of the three sidecars — `<stem>.warpmarkers`,
// `<stem>.phaseresetmarkers`, `<stem>.settings` — names its source by that
// stem, and `<stem>.wav` must exist. A folder with NO sidecar at all is a NEW
// project iff it holds EXACTLY ONE `.wav`: that wav is the source, and the
// first open writes the template sidecars beside it as it always has. Every
// other state is INVALID and says why, one sentence each: more than one sidecar
// stem, the whole sorted set named so the words do not depend on the walk's
// order; no wav for the sidecar's stem; several wavs and no sidecar; no wav at
// all. The settings `title=` is never consulted — a title names the RENDER, and
// the sidecar stem is what names the piece.
//
// A WAV IN THE ROOT THAT IS NOT THE SOURCE IS THE LEGACY LAYOUT. Outputs live
// outside the project root (the deliverable in `render/`, named by the parser's
// render_output_naming.h because both products write it; the batch cells in
// `tmp/`, named by renders_dir.h), so a second wav beside the source is
// a file that has not moved yet, and the refusal names the move rather than
// guessing: "Move `<name>.wav` into render/". The folder is not opened until it
// is done. BOTH OUTPUT FOLDERS ARE INVISIBLE TO THE SOURCE RULE BY
// CONSTRUCTION: the walk below reads regular files only, so a directory —
// `render/`, `tmp/`, `peaks/`, `score/` — can never be a candidate and neither
// folder needs an exclusion rule of its own.
//
// A PROJECT'S NAME MUST BE ONE THE DEVICE CONFIG CAN CARRY. Validity is folder
// SHAPE (above) AND a config-nameable NAME: every successful open writes the
// folder's name into `last_project` (main.cpp), and that key's grammar is
// strict on the way back in, so a folder the grammar refuses would open once
// and make the NEXT launch fatal on a file only the program writes. The rule
// therefore lives at the MEMBERSHIP rather than at each opening road:
// enumerate_project_names below yields only names is_last_project_name
// (device_config.h) accepts, so the startup fallback and the Open project
// picker inherit it, and the ARGUMENT road — which walks no enumeration —
// asks the same predicate of the argument's own folder name and refuses with
// its ordinary sentence. The remembered road needs no test: the config reader
// already refused a name that fails the grammar.
//
// EXTENSIONS COMPARE EXACTLY, lowercase `.wav` and the three sidecar spellings
// as written: the product's own writers and the sync convention name every
// file this way, so a `.WAV` is simply not a source, and no case folding or
// other leniency is offered — strict knowledge required, no fallbacks.
//
// WHAT THIS DISSOLVES, recorded because each was an open question once: which
// render to print is the deliverable in `render/` (batches are auditions);
// where on the stick is the root; there is no recents list — `last_project`
// plus a dozen folder names is the whole of it; the picker's candidates are
// built when it opens and never kept fresh; source versus render is the
// sidecar stem versus the two output folders; and how the tablet knows the
// current project is that the app remembers it (`last_project`), the `current`
// file the sync script used to write having retired with the model.

// A resolved project: the folder's NAME (the project's name), the folder
// itself, and the SOURCE wav's full path — everything gui_main reads.
struct GuiProjectSource {
    std::string           name;
    std::filesystem::path folder;
    std::filesystem::path source;
};

// THE SOURCE OF ONE FOLDER, by the rule at the head of this file, or the one
// sentence that says why the folder is not a project (proper capitalization,
// no trailing period — the status line and the terminal both print it
// verbatim). The directory is walked ONCE (std::filesystem::directory_iterator,
// regular files only); a folder that cannot be walked refuses with the
// system's own words. Never creates anything; never reads a file's bytes.
std::expected<GuiProjectSource, std::string> resolve_project(
    const std::filesystem::path& folder);

// EVERY DIRECTORY DIRECTLY UNDER `projects_path` WHOSE NAME THE DEVICE CONFIG
// CAN CARRY, BY NAME, in PLAIN BYTE ORDER (std::string's operator<) — the ONE
// order the product uses for folder names, so the startup fallback's "first
// valid project" and the Open project picker's row walk agree on what "first"
// means. THE NAME GRAMMAR IS THE ONE MEMBERSHIP TEST HERE
// (is_last_project_name, device_config.h — see the head of this file); folder
// SHAPE is not asked, because shape is asked where a name is CHOSEN
// (resolve_project). A missing or unreadable `projects_path` answers EMPTY,
// and the caller's "no project under <projects_path>" is the whole diagnostic.
std::vector<std::string> enumerate_project_names(
    const std::filesystem::path& projects_path);

// WHAT THE PROGRAM OPENS AT STARTUP — the two roads, one owner:
//
//   * NO ARGUMENT (`argument == nullptr`; the tablet always, the laptop's bare
//     launch): `last_project`, when it is non-empty and resolves; otherwise
//     the FIRST folder in enumerate_project_names order that resolves; and
//     with none, the error "No project under <projects_path>", which the
//     caller prints as its blunt terminal line and exits 1 on. A
//     `last_project` naming a folder that is gone or invalid is the
//     LOAD-LENIENT class and falls through SILENTLY: the program wrote the
//     name, and the user removing or breaking the folder afterwards is a state
//     ordinary use produces, not a hand edit of a program-written file.
//   * WITH AN ARGUMENT (the laptop's `warptempo_gui <wav>`): BOTH QUESTIONS
//     ARE ASKED OF THE SPELLING GIVEN. The argument's OWN parent — made
//     absolute and lexically normalized, spelling work that follows no link —
//     must be a directory DIRECTLY under `projects_path`, which is asked of the
//     filesystem (`std::filesystem::equivalent`) so a relative spelling, a
//     trailing slash or a symlink in the config's own spelling still passes.
//     And the argument must BE that folder's RESOLVED source BY FILESYSTEM
//     IDENTITY (`equivalent` again, never a path compare): the walk above
//     follows links when it asks `is_regular_file`, so a project's source may
//     legitimately be a symlink, and identity is the only compare that accepts
//     the same file the picker opens. A source symlinked from somewhere else
//     is therefore a project; a spelling that reaches it from OUTSIDE the
//     projects path is not that project's source road. The folder's NAME is
//     asked the config grammar here too, this road walking no enumeration.
//     Anything else refuses
//     with the reason ("<path> is not a project source under
//     <projects_path>", or the folder's own invalidity), exit 1. The
//     launcher's "is this a source?" test lives here now, not in a shell
//     script.
//
// The app always has a project open: there is no empty state and no picker
// at startup — the picker is File → Open project, a dialog over a running project.
std::expected<GuiProjectSource, std::string> startup_source(
    const DeviceConfig& config, const char* argument);
