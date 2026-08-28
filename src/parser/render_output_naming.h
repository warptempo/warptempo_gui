#pragma once

#include "engine_settings.h"

#include <filesystem>
#include <optional>
#include <string>

// Composition of a render's on-disk output path from a directory and a name
// stem. Wav is the only render output, so a render composes exactly one path,
// the title-named `.wav` deliverable. Parser-side so the settings-reading
// producers — the GUI and warptempo_cli — compose identical paths from one
// implementation.

// THE DELIVERABLE'S FOLDER NAME (architect approval 2026-08-28). The project
// root holds the source and its sidecars and no output at all, so the
// title-named wav and its `.fingerprint` live in a folder beside them, named
// for the command that writes it. project_model.h owns the project-layout
// ruling. The GUI's disposable batch cells keep their own folder name in
// renders_dir.h — the batch cells are the GUI's alone, and this owner names
// only what both products write.
inline constexpr const char* kDeliverableFolderName = "render";

// The deliverable's directory: the project's `render/` folder — the source's
// parent directory, or "." when the parent is empty (a bare filename), then
// the folder name above. The GUI's Ctrl+Alt+R and warptempo_cli's insurance
// render compose it here and nowhere else, so one command's deliverable lands
// in one place whichever binary produced it. (architect approval 2026-08-28)
std::filesystem::path render_output_directory(
    const std::string& source_audio_path);

// The naming stem for the deliverable: the wav is title-named, so this is the
// title itself. The fingerprint sidecar and the .peaks cache are not composed
// here — they follow the output path directly.
std::string render_output_stem(const EngineSettings& es);

// The wav render's output path, composed as `dir / (stem + ".wav")`.
std::filesystem::path compose_render_output_path(
    const std::filesystem::path& dir,
    const std::string& stem);

// The staging name a deliverable publishes through: the final path's spelling
// with ".tmp" appended (path string plus ".tmp", never an extension swap).
// Every deliverable publication — the CLI's wav render, the GUI's wav engine
// path and reuse rungs — writes under this name first and rename-publishes to
// the final name. This is the single owner of the staging spelling, so the
// two products cannot drift, and render_output_source_collision below checks
// this staging name against the source alongside the final.
std::string render_staging_path(const std::string& final_path);

// Source-clobber guard. The render output must never overwrite the source
// audio itself. Composes the single-render deliverable wav path exactly as a
// render dispatch does (compose_render_output_path over
// render_output_directory / render_output_stem) and returns it when it
// resolves to the source, or nullopt when it does not collide — and its
// render_staging_path sibling is checked as well: publication opens the
// staging name with a truncating write BEFORE the render completes, so a
// staging name that resolves to the source destroys it just as surely as a
// final-name collision would (an existing staging file that is a symlink or
// hard link to the source). When the staging name is the collider, the
// returned path is the staging path itself, so the boundary diagnostics name
// the true colliding path. Two arms answer: std::filesystem::equivalent, an
// inode match that only succeeds when both paths exist, and beside it the
// lexically_normal spelling comparison. An empty source path yields nullopt.
//
// WHAT THE PREDICATE STILL GUARDS, now that the deliverable is composed one
// folder down (architect approval 2026-08-28): NOT a spelling. A title admits
// no '/' (validate_engine_setting, engine_settings_io.cpp), so the composed
// path is always `<source parent>/render/<title>.wav` — one component deeper
// than `<source parent>/<source name>`, with no `..` reachable to climb back
// out — and the lexically_normal arm can therefore never match. What stays
// reachable is the INODE arm, which fires only when both paths already exist:
// a `render/` that is a symlink to the project folder, or a
// `render/<title>.wav` (or its staging sibling) that is a hard link or symlink
// to the source. That is a real way to lose the source, so the predicate and
// its three boundary refusals stay.
//
// Three boundary callers refuse a collision at their own surface: the settings
// editor at commit (the colliding state is GUI-uncommittable), and the GUI
// loader and the warptempo_cli loader at load (a hand-edited sidecar composing
// the output onto the source is adversarial — refused first-error, stderr-only,
// in both products so a file set is loadable in both or neither). The `'`
// load-in-place needs no check of its own: entry sidecars are trusted, written
// once from an already-checked live store (comment retold to the current
// gesture and naming, architect approval 2026-08-04). Separately, the render
// worker is the breach backstop — its own inode-level check, not this
// predicate: it composes batch-folder paths too and covers write-time races
// only the worker can see, keeping its own exists-gated check.
std::optional<std::filesystem::path> render_output_source_collision(
    const EngineSettings& es,
    const std::string& source_audio_path);
