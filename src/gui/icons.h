#pragma once

// THE PRODUCT'S OWN ICON SET AND ITS IN-TREE RENDERER — and the reason no SVG
// library enters this tree.
//
// THE SET IS OUR OWN, SCALABLE, IN THE WINDOWS 95 TOOLBAR IDIOM (architect
// 2026-10-06: "the icons go full scalable, our own set"): one drawing per
// enumerator below, committed as assets/icons/warptempo/<Enumerator>.svg with
// its provenance in that folder's README. Windows 95 is always the source: an
// uncontrovertible Microsoft case is always taken (comctl32's toolbar strips,
// Marlett, Media Player, Sound Recorder, Explorer 95 — redrawn as period
// reference, never copied bytes); Chicago95's composition only where it
// agrees with Windows or Windows has no 16-px original; an ORIGINAL is a
// modern symbol retrofied into the idiom. The README's table names each
// icon's kind and source.
//
// EACH FILE IS A 16-UNIT CELL, ONE UNIT ONE WINDOWS PX (viewBox 0 0 16 16, the
// toolbar bitmap's own 16-px cell): filled paths only, M L H V C A Z, no
// transform, no stroke, every fill one of Windows' twenty solids. That is
// small enough to interpret directly, and interpreting it keeps the icons as
// SOURCE (a `d` string beside its fill, icons.cpp's table, copied VERBATIM —
// a diff between the table and the file is a transcription bug and nothing
// else) rather than pixels baked at one scale. THE GLYPH IS DRAWN AT
// gui_scale LIKE EVERY CHROME LENGTH: in a toolbar case the cell fills the
// live vocabulary's glyph seat (icon_glyph_px, render.h: scaled_px(16) in
// win95, 4 device px a unit at 400 %; scaled_px(24) in win2000, the large
// case's seat, 4.5 device px a unit at 300 % — the 16-unit drawings fill it
// until the Tango road brings art of that size), elsewhere its site's own
// scaled 16; one road at every scale (100 % is a curiosity, architect
// 2026-10-06).
//
// THE INKS ARE THE DRAWINGS' OWN (architect 2026-10-06): every path wears its
// file's fill, black included, on every theme — period pixel art, as Windows'
// own toolbar bitmaps were. No path takes a theme role. A DISABLED glyph is
// the emboss (draw_engraved below), where the theme's roles enter. The
// window-frame glyphs Windows drew in the button text — the caption's —
// are not drawings of this set (paint_handler.cpp's caption glyph block).
//
// THE PLACEMENT IS FIXED: in a toolbar case the cell sits at (3, 3) Windows
// px from the case's corner (icon_case_lead_px, render.h — Windows' own seat,
// the 16-px bitmap's in its small 23 x 22 button and the 24-px one's in its
// large 31 x 30, the live chrome vocabulary's case), filling the seat
// (icon_glyph_px), plus the pressed/checked shift (draw_cased below). Nothing is centred on its ink: Windows never did (12 of
// the 15 STD strip cells with odd ink dimensions sit at the fixed offset).
// The sites with no case — the list rows, the cards, the caption's icon, the
// player's row — keep their own placements and draw the same cell.
//
// NO CURSOR IS AN ICON: every cursor the product shows is a NAMED
// CURSOR FROM THE USER'S OWN XCURSOR THEME (architect 2026-08-03), so the
// platform ships no cursor art and draws none.
//
// GUI-ONLY, like text_shape: icons exist only where pixels do,
// and warptempo_cli must never carry this TU.

#include "render.h"        // GuiColor, the case geometry

#include <cairo/cairo.h>

namespace icons {

// The icon set, one entry per committed drawing (assets/icons/warptempo/,
// each file named by its enumerator). The enumerator names are the set's
// file names and nothing more; an Icon is a GLYPH, not a button, so several
// buttons may wear one, and the glyph a stateful button wears in each state
// is redesign_button_icon's (paint_handler.cpp). FIVE PAIRS SHARE ONE
// DRAWING (their two files byte-identical, one table row): DialogCancel and
// WindowClose, KeyframePrevious and MediaSkipBackward, KeyframeNext and
// MediaSkipForward, BboxPrev and GoPrevious, BboxNext and GoNext.
enum class Icon {
    // THE ICON ROW'S FIRST GROUP.
    DocumentSave,        // Save — STD_FILESAVE, the floppy
    EditUndo,            // Undo — STD_UNDO, one swoop
    EditRedo,            // Redo — STD_REDOW, the mirror
    MediaRecord,         // Render (and the iteration sweep: the tooltip
                         // alone forks) — Sound Recorder's maroon dot
    VcsCommit,           // Save's face in the history view and while the
                         // checkpoint publishes — Save's floppy with a
                         // check in its label
    VcsPull,             // Save's face in the history view while GitHub is
                         // ahead — the cyan arrow onto a bar
    // THE VIEW GROUP (bare 1 / 2 / 3): the page family, its arrow leaving
    // for the source, entering for the target, snapping back to a reset bar
    // for the phase-reset view.
    DocumentExport,      // Source+Warp
    DocumentImport,      // Target+Warp
    ChronometerStart,    // Target+Phase
    // THE MAGNIFIERS.
    ZoomFitBest,         // Full Zoom Out (bare `0`) — corner brackets
    ZoomOriginal,        // Center on Focus (bare `c`) — the figure one
    ZoomInY,             // Toggle Waveform Magnification — the plus
    // THE SINGLE-MARKER VERBS.
    ListAdd,             // Drop Marker (bare `s`) — the plus
    ListRemove,          // Delete Markers (`Delete`) — the minus
    ViewHidden,          // Toggle Disabled (`Ctrl+D`) — the red no-sign
    InsertLink,          // Toggle Inherit (`Ctrl+N`) — the chain
    Merge,               // Flatten (`Ctrl+F`) — two tracks joining
    BlackSum,            // Toggle Cumulative (`u`) — the sigma
    GoJump,              // Toggle Follow (`f`) — an arrow crossing the
                         // view group's page
    TimelineLift,        // Toggle Restrict Undo (`z`) — the sheaf: the
                         // page family's sheet and a second behind it
                         // under a binder clip
    MusicNote16th,       // BPM Iterations (Ctrl+B) — the metronome
    Mathmode,            // Toggle Grid Iterations (bare `i`) — the grid
    PreviewRenderOn,     // Play Renders — Media Player's window with a play
    DialogOkApply,       // Load in Place (the icon row and the render
                         // player's row) — Marlett's check
    VcsDiff,             // Toggle History View (`h`) — Windows 98's
                         // History, the folder with the clock
    ShallowHistory,      // Toggle History Walk (bare `g`, lit in Session) —
                         // History View's clock on the page family's sheet
    EditSelect,          // Toggle Add to Selection — the pointer with a plus
    KeyframePrevious,    // Older checkpoint (`,`) — MediaSkipBackward's
    KeyframeNext,        // Newer checkpoint (`.`) — MediaSkipForward's
    GoPrevious,          // Row 8's left arrow (bare Left)
    GoNext,              // Row 8's right arrow (bare Right)
    DocumentRevert,      // Revert (bare `v`) — the page with a bent arrow
    // ROW 8'S TRANSPORT: Media Player's glyphs. PLAY AND STOP ARE ONE
    // BUTTON'S TWO FACES (RedesignButton::TransportPlayStop: bare Space is
    // one toggle with no pause state); the render player's own Play/Pause
    // button wears MediaPlaybackPause while live.
    MediaSkipBackward,   // Go to Start (bare Home)
    MediaPlaybackStart,  // Play (the face while stopped)
    MediaPlaybackStop,   // Stop (the face while an audition runs)
    MediaPlaybackPause,  // Pause (the render player's row, while live)
    MediaSkipForward,    // Go to End (bare End)
    DialogCancel,        // Render's mid-render Cancel face — WindowClose's
    GoDown,              // Row 8's down arrow (bare Down)
    GoUp,                // Row 8's up arrow (bare Up)
    // THE READ-ONLY TOGGLE'S TWO STATES, swapped by redesign_button_icon.
    Lock,                // Locked: the closed padlock
    Unlock,              // Unlocked: the open padlock
    // THE MARKER WALK AND THE TAB SWITCH.
    BboxPrev,            // Previous Marker (Shift+Tab) — GoPrevious's
    BboxNext,            // Next Marker (Tab) — GoNext's
    TabDetach,           // Switch Tab (Ctrl+Tab) — two overlapping folder
                         // tabs of one height, a close-up of the folder's
                         // tab, the body cropped by the cell
    SettingsConfigure,   // Settings (bare `;`) — STD_PROPERTIES
    // THE LIST ROWS' TWO GLYPHS (the folder overlay, the project picker).
    Folder,              // a folder row — the shell's closed folder, its
                         // tab angled
    AudioXWav,           // a wav row — the Wave Sound page
    // THE RENDER PLAYER'S ROW.
    MediaRepeatSingle,   // Toggle Repeat One — one glyph in both states,
                         // the lamp carrying the state
    GoParentFolder,      // Up a Folder — the folder row's folder with
                         // Explorer 95's VIEW_PARENTFOLDER bent arrow
    // THE NOTIFICATION CARDS' CLASS GLYPHS (notifications.h).
    DialogInformation,   // a NORMAL card's glyph — the balloon with an i
    DialogError,         // a CRITICAL card's glyph — the red disc with an X
    WindowClose,         // Close (the render player) — Marlett's close
    EditCopy,            // Copy Resolved Value (Ctrl+C) — STD_COPY
    HelpWhatsthis,       // Toggle Tooltips (bare backslash) — STD_HELP
    GoJumpDeclaration,   // Jump to Defining Marker (Ctrl+J) — the jump arc
    EditDelete,          // Delete Folder (the render player) — STD_DELETE
    AppIcon,             // the caption's icon and the program icon — the
                         // sixteenth note, in silver
};

// Roster size, for the once-per-icon diagnostic latch in draw(). Keep it equal
// to the enumerator count above; a mismatch only costs that icon its latch (the
// latch is bounds-checked), never correctness. A glyph joining or leaving
// restates this number.
inline constexpr int kIconCount = 58;

// Draw `icon`'s 16-unit cell onto the square (x, y, size_px, size_px), each
// path in its own fill, in file order (the table, icons.cpp). The callers pass
// icon_glyph_px() (a toolbar case's seat) or their own scaled 16. Cairo state is saved and restored;
// the caller's source, path and matrix survive untouched.
//
// A malformed `d` string is a PROGRAMMING ERROR, not a runtime state — the
// strings are in-tree constants — so a parse failure emits one stderr line and
// draws nothing. That is deliberately all the machinery there is: a silent
// fallback would hide a transcription typo forever.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px);

// THE ENGRAVED GLYPH — a disabled button's face (architect 2026-10-02, the AB
// set; Windows' DrawState DSS_DISABLED): the glyph's DISABLED MASK painted
// TWICE — first in the theme's HILIGHT `offset_px` right and down (the caller
// passes one Windows px, relief_line_px), then in its Shadow at (x, y) — so
// the dead glyph reads as cut into the face. THE MASK IS WINDOWS 95'S
// (architect 2026-10-06, the faithful reading of the DSS_DISABLED ruling):
// its toolbar turned the colour bitmap into a mono mask in which WHITE and
// the BUTTON FACE (silver) are background and every other pixel is ink, and
// drew that mask; here it is the union of the drawing's paths whose fill is
// neither White nor Silver, the White and Silver paths cutting it in file
// order — so a layered drawing (a black silhouette with white and silver
// insets: the pages, the padlocks, the metronome) keeps its outlines and
// insets dead, never a grey slab. THE GLYPH HALF OF THE DISABLED EMBOSS and
// the set's one disabled face (architect 2026-10-03; the word half is
// show_embossed_run, render.h, the rule at the palette block). Same square,
// same validation and the same one-stderr rule as draw above.
void draw_engraved(cairo_t* cr, Icon icon, double x, double y, double size_px,
                   double offset_px);

// THE CASED GLYPH, a toolbar button's: the cell at the case's fixed (3, 3)
// Windows px (icon_case_lead_px — Windows' own seat, the head's PLACEMENT)
// from `case_x`/`case_y`, the case's own top-left corner, plus
// `button_shift_px`, the button's pressed/checked shift (paint_button_box's
// `ButtonBoxFace::shift`, one Windows px right and down). Every roster
// button's glyph goes through here or draw_cased_disabled, so the seat has
// one owner.
void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px);

// draw_cased's DEAD-BUTTON sibling: the same seat and shift, the glyph
// engraved (draw_engraved, `offset_px` its light copy's offset).
void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px,
                         double offset_px);

} // namespace icons
