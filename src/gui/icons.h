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
// EVERY ENTRY IS A ROW'S. The icons here are painted by the redesigned rows and
// nowhere else — the pointer cursor is not one of them: every cursor the product
// shows is a NAMED CURSOR FROM THE USER'S OWN XCURSOR THEME (architect
// 2026-08-03), so the platform ships no cursor art and draws none.
//
// GUI-ONLY, like text_shape: icons exist only where pixels do,
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
    // Render's OTHER face: while the history mode stands, Ctrl+Alt+R saves and
    // commits the checkpoint instead of rendering, so the button wears the
    // commit icon and the "Save and Commit" label (the swap's owner is
    // redesign_button_label /
    // redesign_button_icon; the mode is AppState::HistoryMode).
    VcsCommit,           // Render, while the history mode stands
    // Row 4, the icon row. (ZoomOut / ZoomIn lived here 2026-08-01..08-02, for
    // the icon row's zoom pair; both went with those buttons.)
    EditCopy,            // Copy phase resets
    EditPaste,           // Paste phase resets
    MusicNote16th,       // BPM editor
    BlackSum,            // Iteration mode
    GoJump,              // Follow mode
    PreviewRenderOn,     // Listen to a render
    DialogOkApply,       // Load a render in place as the baseline
    VcsDiff,             // The history mode (`h`)
    // THE WALK'S TWO ARROWS (2026-08-05), the history group's other pair: the
    // checkpoint walk's older (`,`) and newer (`.`) steps. Breeze's own
    // go-previous / go-next chevrons — go-jump's own construction minus its
    // destination dot, ONE closed outline per file whose limbs are one viewBox
    // unit thick, so the weight rides the icon's scale like every other
    // geometry here and there is no stroke to set.
    GoPrevious,          // Older checkpoint (`,`)
    GoNext,              // Newer checkpoint (`.`)
    // THE REVERT ACT'S GLYPH (2026-08-05), the history group's fourth: the
    // selected diff flags applied backwards into the live state (`Ctrl+H`).
    // Breeze's own document-revert — a page with an arrow curving back into it,
    // which is the act — and the FIRST committed file whose `d` uses the smooth
    // cubic (`s`), the one command the interpreter grew for it.
    DocumentRevert,      // Revert the selected differences (`Ctrl+H`)
    // Row 3, the tab row. BOTH states of the lock slot, which is always drawn:
    // the closed padlock for a read-only tab and the OPEN one for a writable
    // one (the slot's contract is at the tab painter).
    Lock,                // Locked: closed padlock, full color
    Unlock,              // Unlocked: open padlock, drawn dimmed by the caller
};

// Roster size, for the once-per-icon diagnostic latch in draw(). Keep it equal
// to the enumerator count above; a mismatch only costs that icon its latch (the
// latch is bounds-checked), never correctness.
inline constexpr int kIconCount = 18;

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
