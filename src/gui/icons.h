#pragma once

// THE IN-TREE BREEZE ICON RENDERER — the redesign's icon path, and the reason
// no SVG library enters this tree.
//
// The kdenlive rows draw Breeze icons, and a Breeze icon is a handful of filled
// paths in a square viewBox: no gradients, no strokes, no transforms, no
// references. That is small enough to interpret directly, and interpreting it
// keeps the icons as SOURCE (a `d` string beside its color) rather than as
// pixels baked at one scale — so an icon is crisp at every gui_scale, exactly
// like every other redesigned dimension.
//
// PROVENANCE: the SVGs the tables were transcribed from are committed under
// assets/icons/breeze/. They are the record of what this code draws; the
// d-strings here are copied from them VERBATIM, so a diff between the two is a
// transcription bug and nothing else. They are read by no code at runtime — the
// product ships no icon files and reads none.
//
// GUI-ONLY, like text_shape and color_config: icons exist only where pixels do,
// and warptempo_cli must never carry this TU.

#include "render.h"        // GuiColor

#include <cairo/cairo.h>

namespace icons {

// The icon set, one entry per committed SVG.
enum class Icon {
    // Row 2, the toolbar.
    DocumentSave,        // Save
    EditUndo,            // Undo
    EditRedo,            // Redo
    MediaRecord,         // Render
    // Row 4, the icon row.
    EditCopy,            // Copy phase resets
    EditPaste,           // Paste phase resets
    MusicNote16th,       // BPM editor
    MediaPlaylistRepeat, // Iteration mode
    PreviewRenderOn,     // Listen to a render
    DialogOkApply,       // Commit a render as the baseline
};

// Draw `icon` with its viewBox mapped onto the square (x, y, size_px, size_px),
// filling each of its paths in that path's OWN color (the colors are the SVGs'
// and are hard-coded per the redesign color ruling — see the table).
//
// Uniform scale, no aspect fitting: every icon here is square by construction
// (viewBox 0 0 22 22). Cairo state is saved and restored; the caller's source,
// path and matrix survive untouched.
//
// THE DISABLED FACE'S DIMMING rides the last two arguments, through the shared
// mix_color owner (render.h): each path's own color is RETAINED by `keep_own`
// and made up with `mixed_with`, so a greyed-out button's icon keeps its shape
// and loses its life. It is a MIX, not an alpha — the redesign composites
// nothing and every color that reaches cairo is opaque — and it is per-path,
// which is what lets a multi-colored icon (media-record's red beside a white
// glyph) dim as one object without either part being special-cased.
// keep_own == 1 (the default, which every enabled caller takes) leaves the
// table's colors bit-identical.
//
// A malformed `d` string is a PROGRAMMING ERROR, not a runtime state — the
// strings are in-tree constants — so a parse failure emits one stderr line and
// draws nothing. That is deliberately all the machinery there is: a silent
// fallback would hide a transcription typo forever.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px,
          double keep_own = 1.0,
          GuiColor mixed_with = GuiColor{0.0, 0.0, 0.0});

} // namespace icons
