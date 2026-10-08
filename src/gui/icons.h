#pragma once

// THE ICON SET AND ITS ONE ROAD (architect 2026-10-06). An Icon is a GLYPH,
// one per enumerator below, drawn from the chrome spec's SET (chrome_spec.h's
// icon_set, gui_live_icon_set below — win2000's TANGO 0.8.90'S SCALABLE
// DRAWINGS, ReactOS's model of a Windows 2000 desktop wearing Tango (Tango's
// own files only, nothing drawn, no GNOME file, a repeat known by position;
// the set the product drew itself before is git history), or clearlooks'
// MIST, gnome-icon-theme 2.30's drawings under Mist's own folders). One
// scalable file per enumerator under assets/icons/<set>/ (its README the
// mapping, the provenance and the licences), PARSED AT LAUNCH through resvg, the one renderer of every set
// (svg_icon.h: the wrapper, the error rule and why a set is checked at its
// import), straight from the bundle — never copied into the config folder
// (load_svg_set, below) — and rasterised lazily per (glyph, device px) into
// an ARGB32 surface that a draw copies at an integer device px, no
// resample. THE INKS
// ARE THE DRAWINGS' OWN (gradients, strokes and opacities included), never a
// theme role; a DISABLED glyph is ReactOS's saturate (draw_disabled).
//
// AT EVERY SCALE the glyph is drawn at gui_scale like every chrome length
// (100 % is a curiosity, architect 2026-10-06): in a toolbar case the
// 48-unit cell fills the case's glyph seat (icon_glyph_px, render.h:
// scaled_px(24), the large case's seat, 72 device px at 300 %); elsewhere
// its site's own scaled 16 (the caption's icon, the list rows, the cards —
// Windows' small icon). The window-frame glyphs
// Windows drew in the button text — the caption's — are not drawings of the
// set (paint_handler.cpp's caption glyph block).
//
// THE PLACEMENT IS FIXED: in a toolbar case the cell sits at the spec's lead
// from the case's corner (icon_case_lead_px, render.h — win2000's (3, 3),
// Windows' own seat, the 24-px bitmap's in its large 31 x 30 button as the
// 16-px one's in its small 23 x 22; clearlooks' (4, 4), GTK's in its 32 x
// 32 tool button), filling the seat
// (icon_glyph_px), plus the pressed/checked shift (draw_cased below).
// Nothing is centred on its ink: Windows never did (12 of the 15 STD strip
// cells with odd ink dimensions sit at the fixed offset). The sites with no
// case — the list rows, the cards, the caption's icon, the player's row —
// keep their own placements and draw the same cell.
//
// NO CURSOR IS AN ICON: every cursor the product shows is a NAMED
// CURSOR FROM THE USER'S OWN XCURSOR THEME (architect 2026-08-03), so the
// platform ships no cursor art and draws none.
//
// GUI-ONLY, like text_shape: icons exist only where pixels do,
// and warptempo_cli must never carry this TU (nor svg_icon's, nor resvg).

#include "render.h"        // the case geometry, live_chrome_spec()

#include <cairo/cairo.h>

#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace icons {

// The glyphs, one per file of the set (assets/icons/<set>/, each file named
// by its enumerator — kIconNames below). The enumerator names are the set's
// file names and nothing more; each comment names the ACT the glyph serves,
// and the set's README names the drawing it wears. An Icon is a GLYPH, not a
// button, so several buttons may wear one, and the glyph a stateful button
// wears in each state is redesign_button_icon's (paint_handler.cpp). THE SET
// MAY WEAR ONE DRAWING FOR SEVERAL GLYPHS, the files byte-identical (each
// set's README lists its repeats).
enum class Icon {
    // THE ICON ROW'S FIRST GROUP.
    DocumentOpen,        // Open Project (and its shift twin, Revert) — the
                         // sets' document-open (architect 2026-10-07)
    DocumentSave,        // Save
    EditUndo,            // Undo
    EditRedo,            // Redo
    MediaRecord,         // Render (and the iteration sweep: the tooltip
                         // alone forks)
    VcsCommit,           // Save's face in the history view and while the
                         // checkpoint publishes
    VcsPull,             // Save's face in the history view while GitHub is
                         // ahead
    // THE VIEW GROUP (bare 1 / 2 / 3): one family of three.
    DocumentExport,      // Source+Warp
    DocumentImport,      // Target+Warp
    ChronometerStart,    // Target+Phase
    // THE MAGNIFIERS.
    ZoomFitBest,         // Full Zoom Out (bare `0`)
    ZoomOriginal,        // Center on Focus (bare `c`)
    ZoomInY,             // Toggle Waveform Magnification
    // THE SINGLE-MARKER VERBS.
    ListAdd,             // Drop Marker (bare `s`)
    ListRemove,          // Delete Markers (`Delete`)
    ViewHidden,          // Toggle Disabled (`Ctrl+D`)
    InsertLink,          // Toggle Inherit (`Ctrl+N`)
    Merge,               // Flatten (`Ctrl+F`)
    BlackSum,            // Toggle Cumulative (`u`)
    GoJump,              // Toggle Follow (`f`)
    TimelineLift,        // Toggle Restrict Undo (`z`)
    MusicNote16th,       // BPM Iterations (Ctrl+B)
    Mathmode,            // Toggle Grid Iterations (bare `i`)
    PreviewRenderOn,     // Play Renders
    DialogOkApply,       // Load in Place (the icon row and the render
                         // player's row): the walked version loaded into
                         // the editor in place. Tango's view-refresh, the
                         // reload's arrows (architect 2026-10-07); Mist's
                         // gnome emblem-default, the green check, GNOME's
                         // accept (architect 2026-10-07 ~17:00). A
                         // deliberate asymmetry: Windows 2000 is the
                         // reference behind Clearlooks and its set may lag.
    VcsDiff,             // Toggle History View (`h`)
    ShallowHistory,      // Toggle History Walk (bare `g`, lit in Session) —
                         // the sets' x-office-calendar, the dated page:
                         // the walk through dated commits (architect
                         // 2026-10-07)
    EditSelect,          // Toggle Add to Selection
    KeyframePrevious,    // Older checkpoint (`,`)
    KeyframeNext,        // Newer checkpoint (`.`)
    GoPrevious,          // Row 8's left arrow (bare Left)
    GoNext,              // Row 8's right arrow (bare Right)
    DocumentRevert,      // Revert (bare `v`): back to the viewed
                         // checkpoint. Tango's appointment-new, the clock
                         // (architect 2026-10-07); Mist's gnome
                         // document-revert, the page with the yellow return
                         // arrow, GNOME's revert, rhyming with Load in
                         // Place's accept (architect 2026-10-07 ~17:00; the
                         // same asymmetry as DialogOkApply's)
    // ROW 8'S TRANSPORT. PLAY AND STOP ARE ONE BUTTON'S TWO FACES
    // (RedesignButton::TransportPlayStop: bare Space is one toggle with no
    // pause state); the render player's own Play/Pause button wears
    // MediaPlaybackPause while live.
    MediaSkipBackward,   // Go to Start (bare Home)
    MediaPlaybackStart,  // Play (the face while stopped)
    MediaPlaybackStop,   // Stop (the face while an audition runs)
    MediaPlaybackPause,  // Pause (the render player's row, while live)
    MediaSkipForward,    // Go to End (bare End)
    DialogCancel,        // Render's mid-render Cancel face
    GoDown,              // Row 8's down arrow (bare Down)
    GoUp,                // Row 8's up arrow (bare Up)
    // THE READ-ONLY TOGGLE'S TWO STATES, swapped by redesign_button_icon.
    Lock,                // Locked
    Unlock,              // Unlocked
    // THE MARKER WALK AND THE TAB SWITCH.
    BboxPrev,            // Previous Marker (Shift+Tab)
    BboxNext,            // Next Marker (Tab)
    TabDetach,           // Switch Tab (Ctrl+Tab)
    SettingsConfigure,   // Settings (bare `;`)
    // THE LIST ROWS' TWO GLYPHS (the folder overlay, the project picker).
    Folder,              // a folder row
    AudioXWav,           // a wav row
    // THE RENDER PLAYER'S ROW.
    MediaRepeatSingle,   // Toggle Repeat One — one glyph in both states,
                         // the lamp carrying the state
    GoParentFolder,      // Up a Folder
    // THE NOTIFICATION CARDS' CLASS GLYPHS (notifications.h).
    DialogInformation,   // a NORMAL card's glyph
    DialogError,         // a CRITICAL card's glyph
    WindowClose,         // Close (the render player)
    EditCopy,            // Copy Resolved Value (Ctrl+C)
    HelpWhatsthis,       // Toggle Tooltips (bare backslash)
    GoJumpDeclaration,   // Jump to Defining Marker (Ctrl+J)
    EditDelete,          // Delete Folder (the render player)
    AppIcon,             // the caption's icon and the program icon outside
                         // the window (tools/app_icon)
};

// Roster size, the names' and the load's count (load_svg_set parses one
// document per enumerator). Keep it equal to the enumerator count above; a
// glyph joining or leaving restates this number.
inline constexpr int kIconCount = 59;

// THE FILE NAMES, the enumerators spelled in enum order: the set's
// `<name>.svg` (load_svg_set reads them). A misspelling is a missing file at
// the next launch, which fails it.
inline constexpr const char* kIconNames[] = {
    "DocumentOpen", "DocumentSave", "EditUndo", "EditRedo", "MediaRecord",
    "VcsCommit",
    "VcsPull", "DocumentExport", "DocumentImport", "ChronometerStart",
    "ZoomFitBest", "ZoomOriginal", "ZoomInY", "ListAdd", "ListRemove",
    "ViewHidden", "InsertLink", "Merge", "BlackSum", "GoJump",
    "TimelineLift", "MusicNote16th", "Mathmode", "PreviewRenderOn",
    "DialogOkApply", "VcsDiff", "ShallowHistory", "EditSelect",
    "KeyframePrevious", "KeyframeNext", "GoPrevious", "GoNext",
    "DocumentRevert", "MediaSkipBackward", "MediaPlaybackStart",
    "MediaPlaybackStop", "MediaPlaybackPause", "MediaSkipForward",
    "DialogCancel", "GoDown", "GoUp", "Lock", "Unlock", "BboxPrev",
    "BboxNext", "TabDetach", "SettingsConfigure", "Folder", "AudioXWav",
    "MediaRepeatSingle", "GoParentFolder", "DialogInformation",
    "DialogError", "WindowClose", "EditCopy", "HelpWhatsthis",
    "GoJumpDeclaration", "EditDelete", "AppIcon",
};
static_assert(std::size(kIconNames) == kIconCount);

// THE LIVE SET: the live chrome spec's (chrome_spec.h's icon_set, the
// bundled folder's name), read after set_live_chrome_spec.
inline std::string_view gui_live_icon_set() {
    return live_chrome_spec().icon_set;
}

// THE SET'S LOAD, once, in gui_main after the device config's read (whose
// `chrome` names the set) and before the window: `set`'s files fetched from the bundle
// (GuiPlatform::bundled_icon_files — the repository's assets/icons/<set>/ on
// the laptop, the APK's icons/<set>/ assets on the tablet; READ IN PLACE,
// never copied into the config folder: nobody authors an icon set on the
// device),
// then every enumerator's `<name>.svg` parsed in enum order before any draw
// (svg_icon::parse; files of other names, the README among them, ignored).
// THE ERROR ARM'S PRODUCERS: the bundle unreadable (IO), a file missing or
// not well-formed SVG (a build defect) — "icon set 'tango': EditDelete.svg:
// not well-formed SVG (resvg error 7)", the first only, which gui_main
// prints and exits on (the validation doctrine's class (1)). A construct
// resvg does not draw passes the load: the set's import sheet is that check
// (svg_icon.h's error rule).
std::optional<std::string> load_svg_set(std::string_view set);

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale):
// the cache refills lazily at the new sizes; the parsed drawings stay.
void drop_rasters();

// Draw `icon`'s cell onto the square (x, y, size_px, size_px): the
// glyph's live raster at the integer px nearest size_px, copied at the
// integer device px nearest (x, y) — every caller passes integers. The
// callers pass icon_glyph_px() (a toolbar case's seat) or their own scaled
// 16. Cairo state is saved and restored. Cannot fail: the launch's load
// parsed every drawing.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px);

// THE DISABLED GLYPH — a dead button's face, by the live spec's
// disabled_glyph (chrome_spec.h): win2000 REACTOS'S SATURATE (architect
// 2026-10-06; comctl32's toolbar draws a disabled 32-bpp image with
// ILS_SATURATE | ILS_ALPHA at 192, imagelist.c's saturate_image,
// svg_icon::saturated_copy), clearlooks GTK 2'S INSENSITIVE ICON
// (2026-10-07; gdk_pixbuf_saturate_and_pixelate (0.8, TRUE),
// svg_icon::saturated_pixelated_copy, its checker one device px a cell) —
// of the live raster, built the first time a disabled face asks and cached
// beside it (one rule per process: the chrome is chosen once), copied at
// the same seat. A
// disabled WORD keeps Windows' DSS_DISABLED emboss (show_embossed_run,
// render.h), whose mono mask a Tango drawing's gradients cannot give.
void draw_disabled(cairo_t* cr, Icon icon, double x, double y,
                   double size_px);

// THE CASED GLYPH, a toolbar button's: the cell at the case's fixed lead
// Windows px (icon_case_lead_px — the spec's seat, the head's PLACEMENT)
// from `case_x`/`case_y`, the case's own top-left corner, plus
// `button_shift_px`, the button's pressed/checked shift (paint_button_box's
// `ButtonBoxFace::shift`, one Windows px right and down). Every roster
// button's glyph goes through here or draw_cased_disabled, so the seat has
// one owner.
void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px);

// draw_cased's DEAD-BUTTON sibling: the same seat and shift, the glyph's
// disabled face (draw_disabled).
void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px);

} // namespace icons
