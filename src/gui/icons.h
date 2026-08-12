#pragma once

// THE IN-TREE BREEZE ICON RENDERER — the redesign's icon path, and the reason
// no SVG library enters this tree.
//
// The kdenlive rows draw Breeze icons, and a Breeze icon is a handful of paths
// in a square viewBox: no gradients, no references. That is small enough to
// interpret directly, and interpreting it keeps the icons as SOURCE (a `d`
// string beside its color) rather than as pixels baked at one scale — so an
// icon is crisp at every gui_scale, exactly like every other redesigned
// dimension.
//
// THE INTERPRETER HAS GROWN EXACTLY TWO FEATURES past the plain filled path it
// started as, each with a committed producer and each taken so that the `d`
// string in the table stays VERBATIM rather than being flattened by hand:
//   - a per-path TRANSLATE (dialog-ok-apply's and dialog-cancel's transform);
//   - the SMOOTH CUBIC `s` (document-revert's arrow lobes, 2026-08-05).
// Each is described where it is implemented (icons.cpp's table header and its
// `d`-interpreter header). A general matrix and a stroked-path arm were grown
// for distortionfx on 2026-08-11 and deleted hours later with it, that file
// being the only producer either ever had; git history holds both.
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
    // SAVE's OTHER face: while the history mode stands, Ctrl+S saves and then
    // commits the checkpoint, so the button wears the commit icon and the
    // "Save and Commit" label — and keeps both while the checkpoint publishes,
    // under "Committing..." (the swap's owner is redesign_button_label /
    // redesign_button_icon; the mode is AppState::HistoryMode). It was RENDER's
    // second face until 2026-08-08, when the act moved onto the save chord.
    VcsCommit,           // Save, in the history view and while publishing
    // Row 4, the icon row. (ZoomOut / ZoomIn lived here 2026-08-01..08-02, for
    // the icon row's zoom pair; both went with those buttons.)
    //
    // THE FOUR VIEW RADIOS' FACES (architect-picked 2026-08-11 off a rendered
    // candidate sheet): the S/T audio pair and the W/P marker pair wore shaped
    // LETTER GLYPHS from the row's first day until then — the row's only
    // non-icon buttons, and the reason the architect briefly ruled the radios
    // deleted altogether ("ugly letter blips"); he reversed that the same day
    // and gave them real glyphs instead, which is what killed the letter arm
    // (no producer left; the painter's shaped-letter branch went with it).
    //   SOURCE / TARGET are document-export / document-import, the arrow
    //   LEAVING a document and ENTERING one — his own metaphor, the source
    //   being where the audio comes FROM.
    //   WARP is speedometer, the gauge with the needle: warping IS a speed
    //   change, and "change speed" is kdenlive's own word for it. It was his
    //   FIRST pick, reversed to distortionfx (the spiral, "time bends") in the
    //   same breath, and RESTORED at his second look later the same day, which
    //   leaves distortionfx the recorded runner-up beside player-time.
    //   PHASE RESET is chronometer-start, the stopwatch with the solid play
    //   triangle in its dial: start the clock anew. Picked over
    //   chronometer-reset and view-refresh — indistinguishable from each other
    //   at row size, and chronometer-reset's dial does not survive the
    //   rendering — and over the bare chronometer.
    DocumentExport,      // Source audio view (bare `t`)
    DocumentImport,      // Target audio view (bare `t`)
    Speedometer,         // Warp markers (bare `p`)
    ChronometerStart,    // Phase reset markers (bare `p`)
    // THE TRIM BUTTON'S GLYPH (2026-08-11, the trim surface arc): Breeze's
    // EDIT-CUT — the scissors, ARCHITECT-PICKED from the rendered candidate
    // sheet the same day, superseding the planner's transform-crop of the
    // first cut (he read the crop frame as rectangle-select rather than
    // crop; its one-commit life is recorded at the table). Cutting IS
    // trimming, the scissors the universal glyph for it. One
    // `.ColorScheme-Text` path of m/c/l/z commands, inside the interpreter's
    // coverage.
    EditCut,             // Set trim from region (bare `x`)
    EditCopy,            // Copy phase resets
    EditPaste,           // Paste phase resets
    MusicNote16th,       // BPM editor
    BlackSum,            // Iteration mode
    GoJump,              // Follow mode
    PreviewRenderOn,     // Listen to a render
    DialogOkApply,       // Load a render in place as the baseline
    VcsDiff,             // The history mode (`h`)
    // THE CUMULATIVE READING'S TOGGLE (architect 2026-08-09), the history
    // group's second: Breeze's own deep-history — a clock face with a curl-back
    // arrow sweeping around it, reaching across the whole span at once, against
    // the iterative reading's one step at a time. THE SET'S FIRST TWO-COLOR
    // ICON, knowingly: the dial is .ColorScheme-Text and the arrow
    // .ColorScheme-Accent, so the arrow reads as the act over what it sweeps.
    // The table's entry records both resolved literals, the command-coverage
    // check and the four glyphs tried before it.
    DeepHistory,               // The cumulative reading (`u`)
    // THE WALK'S TWO ARROWS (2026-08-05 as go-previous / go-next, REGLYPHED
    // 2026-08-11): the checkpoint walk's older (`,`) and newer (`.`) steps wear
    // Breeze's keyframe-previous / keyframe-next — a stopwatch dial with a
    // solid triangle pointing into the past or the future. The architect's
    // reason is their neighbour: Deep-History is a CLOCK, so the steps beside it
    // read as clock steps. The chevrons they yielded are row 8's left and right
    // arrows now, and the succession with its runners-up is recorded at the
    // table entry.
    KeyframePrevious,    // Older checkpoint (`,`)
    KeyframeNext,        // Newer checkpoint (`.`)
    // The chevron pair, go-jump's own construction minus its destination dot:
    // ONE closed outline per file whose limbs are one viewBox unit thick, so
    // the weight rides the icon's scale like every other geometry here and
    // there is no stroke to set. Row 8's horizontal arrows are their whole
    // consumer list since 2026-08-11 (they served the walk 2026-08-05..11, and
    // both buttons at once for the few hours row 8 shared them).
    GoPrevious,          // The left arrow (bare Left)
    GoNext,              // The right arrow (bare Right)
    // THE REVERT ACT'S GLYPH (2026-08-05), the history group's third button: the
    // selected diff flags applied backwards into the live state (`Ctrl+H`).
    // Breeze's own document-revert — a page with an arrow curving back into it,
    // which is the act — and the FIRST committed file whose `d` uses the smooth
    // cubic (`s`), the one command the interpreter grew for it.
    DocumentRevert,      // Revert the selected differences (`Ctrl+H`)
    // Row 8, the transport row (2026-08-11, the touch arc's first surface):
    // SEVEN new glyphs for its eight buttons — the four cardinal arrows took
    // GoPrevious / GoNext for left and right, SHARED with the walk pair at
    // first (an Icon is a GLYPH, not a button: VcsCommit already serves two
    // faces) and theirs alone since the walk reglyphed to the keyframe pair
    // later that day — with GoUp / GoDown below completing the chevron family,
    // so all four arrows are the
    // same Breeze construction — one closed outline whose limbs are one
    // viewBox unit thick, no stroke to set. The transport four are Breeze's
    // own media-* set (the universal transport vocabulary; media-playback-
    // pause was the considered runner-up for the stop slot and lost — Space
    // STOPS, it does not pause, and the face must not promise a resume).
    // dialog-cancel (the circle-slash) is Breeze's one "cancel" glyph, no
    // runner-up considered — transcribed for the row's short-lived Esc button
    // (deleted the same day at the architect's live pass) and kept as the
    // RENDER button's mid-render Cancel face (redesign_button_icon).
    MediaSkipBackward,   // Go to start (bare Home)
    MediaPlaybackStart,  // Play (bare Space, the pair's live-while-stopped half)
    MediaPlaybackStop,   // Stop (bare Space, the pair's live-while-playing half)
    MediaSkipForward,    // Go to end (bare End)
    DialogCancel,        // Render's mid-render Cancel face (row 2)
    GoDown,              // The down arrow (bare Down)
    GoUp,                // The up arrow (bare Up)
    // Row 3, the tab row. BOTH states of the lock slot, which is always drawn:
    // the closed padlock for a read-only tab and the OPEN one for a writable
    // one (the slot's contract is at the tab painter).
    Lock,                // Locked: closed padlock, full color
    Unlock,              // Unlocked: open padlock, drawn dimmed by the caller
};

// Roster size, for the once-per-icon diagnostic latch in draw(). Keep it equal
// to the enumerator count above; a mismatch only costs that icon its latch (the
// latch is bounds-checked), never correctness.
inline constexpr int kIconCount = 33;

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
