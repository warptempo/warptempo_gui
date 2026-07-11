#pragma once

#include "engine_settings.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Co-equal composition of a render's on-disk output paths from a directory,
// a name stem, and a per-file extension. No path is derived from another by
// extension swap: the warptempo_maps pair's two files are two entries of one
// composed list, ordered by the extension list. Parser-side so the
// settings-reading producers — the GUI and warptempo_cli — compose identical
// paths from one implementation.

// The format's ordered extension list — the single owner of the
// format-to-extensions mapping:
//   wav             -> {.wav}
//   warptempo_maps  -> {.warpframemap, .phaseresetframemap}
//   generic_map     -> {.warpframemap}
//   midi_map        -> {.miditempomap}
// For the pair the warp column comes first and the phase reset column
// second; order-dependent callers read the entries in this order.
std::vector<std::string> render_output_extensions(
    const std::string& output_format);

// The directory source-sibling outputs are composed into: the source's
// parent directory, or "." when the parent is empty (a bare filename).
std::filesystem::path render_output_directory(
    const std::string& source_audio_path);

// The naming stem for a source-sibling render. The map artifacts
// (.warpframemap, .phaseresetframemap, .miditempomap) are project files
// named by the source stem, siblings of the source exactly like the
// authoring sidecars (.warpmarkers, .phaseresetmarkers, .settings), so for
// the three map formats this returns source_stem. The wav deliverable is
// title-named: the title, prefixed with `limiter=false;` when the limiter is
// off (the clean-float wav render). Only wav reaches the title arm, so the
// clean-float prefix is wav-scoped by construction; this is its single owner.
// The fingerprint sidecar and the .peaks cache are not composed here — they
// follow the output path directly.
std::string render_output_stem(const EngineSettings& es,
                               const std::string& source_stem);

// The full output-path list for a render of `output_format`: one entry per
// extension, each composed as `dir / (stem + extension)`. For warptempo_maps
// the first entry is the warp column and the second the phase reset column,
// by render_output_extensions' order.
std::vector<std::filesystem::path> compose_render_output_paths(
    const std::filesystem::path& dir,
    const std::string& stem,
    const std::string& output_format);

// Source-clobber guard. The render output must never overwrite the source
// audio itself. Composes the single-render source-sibling output paths exactly
// as a render dispatch does (compose_render_output_paths over
// render_output_directory / render_output_stem) and returns the first output
// path that resolves to the source, or nullopt when none collide. Every path
// of the format is checked, so the warptempo_maps pair's second file is
// covered too. std::filesystem::equivalent is an inode match that only
// succeeds when both paths exist, so the lexically_normal comparison is the
// fallback that catches the not-yet-written output that will land on the
// source. An empty source path yields nullopt.
//
// Four boundary callers refuse a collision at their own surface: the settings
// editor at commit (the colliding state is GUI-uncommittable), the GUI loader
// and the warptempo_cli loader at load (a hand-edited sidecar composing the
// output onto the source is adversarial — refused first-error, stderr-only, in
// both products so a file set is loadable in both or neither), and the
// Ctrl+Alt+C commit pre-mutation guard (a hand-edited entry .settings that
// composes onto the source aborts before the first marker or AppState
// mutation). Separately, the render worker is the breach backstop: it composes
// batch-folder paths too and keeps its own exists-gated check.
std::optional<std::filesystem::path> render_output_source_collision(
    const EngineSettings& es,
    const std::string& source_audio_path);
