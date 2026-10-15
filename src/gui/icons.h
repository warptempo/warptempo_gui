#pragma once

// THE ICON SET AND ITS ONE ROAD (architect 2026-10-06). An Icon is a GLYPH,
// one per enumerator below, drawn from THE ONE SET (kIconSet below):
// ADWAITA 3.22.0 ALONE (architect 2026-10-14), every drawing the
// adwaita-icon-theme 3.22.0 tarball's own src/fullcolor/ artwork at its 48
// slot, extracted as a standalone 48-unit file by
// tools/icon_set_adwaita_322/, nothing redrawn and nothing from another
// release; hard-coded (the `icons` device key and the Settings Icons row were
// removed 2026-10-12, docs/engineering/closed_questions.md). One scalable
// file per enumerator under assets/icons/<set>/ (its README the mapping, the
// provenance and the licences), PARSED AT LAUNCH through resvg, the one renderer of every set
// (svg_icon.h: the wrapper, the error rule and why a set is checked at its
// import), straight from the bundle — never copied into the config folder
// (load_svg_set, below) — and rasterised lazily per (glyph, device px) into
// an ARGB32 surface that a draw copies at an integer device px, no
// resample. THE INKS ARE THE DRAWINGS' OWN (gradients, strokes and
// opacities included), never a theme role; a DISABLED glyph is ReactOS's
// saturate (draw_disabled).
//
// AT EVERY SCALE the glyph is drawn at gui_scale like every chrome length
// (100 % is a curiosity, architect 2026-10-06): in the program's case the
// drawing's square cell (every file a 48-unit root, the sheet's slot nested
// in it as its own viewport) fills the case's glyph seat (icon_glyph_px, render.h: scaled_px(20),
// Cool Edit's own seat since 2026-10-09 ~10:25, program_spec.h's head — 60
// device px at 300 %, 72 at 360); elsewhere its site's own scaled 16 (the
// caption's icon, the list rows, the cards — Windows' small icon). The
// window-frame glyphs
// Windows drew in the button text — the caption's — are not drawings of the
// set (paint_handler.cpp's caption glyph block).
//
// THE PLACEMENT IS FIXED: in the program's case the cell sits at the case's
// highlight line from its corner (icon_case_lead_px, render.h — Cool Edit's
// (+1, +1), its 20-px bitmap's seat in its 23-W case, the product's 20-W
// seat in the same 23, architect 2026-10-09), filling the seat
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

#include <cassert>
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
                         // set's document-open (architect 2026-10-07)
    DocumentSave,        // Save — the set's document-save (architect
                         // 2026-10-14: the floppy retired)
    EditUndo,            // Undo
    EditRedo,            // Redo
    MediaRecord,         // Render (and the iteration sweep: the tooltip
                         // alone forks)
    VcsCommit,           // Save's face in the history view and while the
                         // checkpoint publishes
    VcsPull,             // Save's face in the history view while GitHub is
                         // ahead
    // THE VIEW PAIRS (architect 2026-10-14): the Source / Target pair (bare
    // 1 / 2) and the Warp / Phase-reset pair (bare 3 / 4), one glyph each.
    AccessoriesDictionary, // Source — the primary source
    InputTablet,         // Target — the authoring surface
    MailMarkJunk,        // Warp — the warped sheet
    WeatherFog,          // Phase Reset — the waves in phase
    // THE MAGNIFIERS.
    ZoomFitBest,         // Full Zoom Out (bare `0`)
    ZoomOriginal,        // Center on Focus (bare `c`)
    ZoomInY,             // Toggle Waveform Magnification
    // THE SINGLE-MARKER VERBS.
    ListAdd,             // Drop Marker (bare `s`)
    ListRemove,          // Delete Markers (`Delete`)
    ViewHidden,          // Toggle Disabled (`Ctrl+D`)
    InsertLink,          // Toggle Inherit (`Ctrl+N`)
    Merge,               // Flatten (`Ctrl+F`) — the set's edit-clear-all,
                         // the broom (architect 2026-10-14)
    BlackSum,            // Toggle Cumulative (`u`)
    GoJump,              // Toggle Follow (`f`)
    TimelineLift,        // Toggle Restrict Undo (`z`)
    MusicNote16th,       // BPM Iterations (Ctrl+B)
    Mathmode,            // Toggle Grid Iterations (bare `i`)
    PreviewRenderOn,     // Play Renders
    DialogOkApply,       // Load in Place (the icon row and the render
                         // player's row): the walked version loaded into
                         // the editor in place. The set's edit-paste
                         // (architect 2026-10-14).
    VcsDiff,             // Toggle History View (`h`)
    ShallowHistory,      // Toggle History Walk (bare `g`, lit in Session) —
                         // the set's x-office-calendar, the dated page:
                         // the walk through dated commits (architect
                         // 2026-10-07)
    EditSelect,          // Toggle Add to Selection
    KeyframePrevious,    // Older checkpoint (`,`)
    KeyframeNext,        // Newer checkpoint (`.`)
    GoPrevious,          // Row 8's left arrow (bare Left)
    GoNext,              // Row 8's right arrow (bare Right)
    DocumentRevert,      // Revert (bare `v`): back to the viewed
                         // checkpoint. The set's document-revert
                         // (architect 2026-10-07 ~17:00)
    // ROW 8'S TRANSPORT. PLAY AND STOP ARE ONE BUTTON'S TWO FACES
    // (RedesignButton::TransportPlayStop: bare Space is one toggle with no
    // pause state); the render player's own Play/Pause button wears
    // MediaPlaybackPause while live.
    MediaSkipBackward,   // the render player's Previous (row 8 has no skip since 2026-10-12)
    MediaPlaybackStart,  // Play (the face while stopped)
    MediaPlaybackStop,   // Stop (the face while an audition runs)
    MediaPlaybackPause,  // Pause (the render player's row, while live)
    MediaSkipForward,    // the render player's Next
    DialogCancel,        // Render's mid-render Cancel face
    GoDown,              // Row 8's down arrow (bare Down)
    GoUp,                // Row 8's up arrow (bare Up)
    // THE READ-ONLY TOGGLE'S PADLOCK PAIR (architect 2026-10-14): the open
    // padlock while unlocked, the closed one while locked
    // (redesign_button_icon); its lit / sunken face still says locked, as
    // every toggle's does.
    ChangesAllow,        // Toggle Read-Only while unlocked
    ChangesPrevent,      // Toggle Read-Only while locked
    // THE MARKER WALK AND THE TAB SWITCH.
    BboxPrev,            // Previous Marker (Shift+Tab)
    BboxNext,            // Next Marker (Tab)
    TabDetach,           // Switch Tab (Ctrl+Tab) on tab A: the upper left
                         // window in front
    TabDetachB,          // Switch Tab on tab B (architect 2026-10-12): the
                         // same two windows with the lower right one in
                         // front, so the front window is the tab shown
                         // (redesign_button_icon); a changed file of the
                         // set (element order alone), its README
    SettingsConfigure,   // Settings (bare `;`)
    // THE LIST ROWS' TWO GLYPHS (the folder overlay, the project picker).
    Folder,              // a folder row; Up a Folder (the render player,
                         // architect 2026-10-10: the plain folder, enough to
                         // tell it from the Highlight Previous arrow)
    AudioXWav,           // a wav row
    // THE RENDER PLAYER'S ROW.
    MediaRepeatSingle,   // Toggle Repeat One — one glyph in both states,
                         // the lamp carrying the state
    // THE NOTIFICATION CARDS' CLASS GLYPHS (notifications.h).
    DialogInformation,   // a NORMAL card's glyph
    DialogError,         // a CRITICAL card's glyph
    WindowClose,         // Close (the render player)
    HelpWhatsthis,       // Toggle Tooltips (bare backslash)
    GoJumpDeclaration,   // Jump to Defining Marker (Ctrl+J; its shift twin
                         // Ctrl+Shift+J, Copy Resolved Value, 2026-10-14)
    AccessoriesTextEditor, // Open Text Editor (bare Enter) — the set's
                           // accessories-text-editor (architect 2026-10-09)
    EditDelete,          // Delete Folder (the render player)
    ValueDrag,           // Toggle Value Drag (bare `[`) — a vertical double
                         // arrow, the drag it arms (architect 2026-10-11:
                         // "a double-sided arrow with an up and a down";
                         // never a zoom glyph)
    AppIcon,             // the caption's icon
};

// Roster size, the names' and the load's count (load_svg_set parses one
// document per enumerator). Keep it equal to the enumerator count above; a
// glyph joining or leaving restates this number.
inline constexpr int kIconCount = 61;

// THE FILE NAMES, the enumerators spelled in enum order: the set's
// `<name>.svg` (load_svg_set reads them). A misspelling is a missing file at
// the next launch, which fails it.
inline constexpr const char* kIconNames[] = {
    "DocumentOpen", "DocumentSave", "EditUndo", "EditRedo", "MediaRecord",
    "VcsCommit",
    "VcsPull", "AccessoriesDictionary", "InputTablet", "MailMarkJunk",
    "WeatherFog",
    "ZoomFitBest", "ZoomOriginal", "ZoomInY", "ListAdd", "ListRemove",
    "ViewHidden", "InsertLink", "Merge", "BlackSum", "GoJump",
    "TimelineLift", "MusicNote16th", "Mathmode", "PreviewRenderOn",
    "DialogOkApply", "VcsDiff", "ShallowHistory", "EditSelect",
    "KeyframePrevious", "KeyframeNext", "GoPrevious", "GoNext",
    "DocumentRevert", "MediaSkipBackward", "MediaPlaybackStart",
    "MediaPlaybackStop", "MediaPlaybackPause", "MediaSkipForward",
    "DialogCancel", "GoDown", "GoUp", "ChangesAllow", "ChangesPrevent",
    "BboxPrev",
    "BboxNext", "TabDetach", "TabDetachB", "SettingsConfigure", "Folder", "AudioXWav",
    "MediaRepeatSingle", "DialogInformation", "DialogError", "WindowClose",
    "HelpWhatsthis",
    "GoJumpDeclaration", "AccessoriesTextEditor", "EditDelete", "ValueDrag",
    "AppIcon",
};
static_assert(std::size(kIconNames) == kIconCount);

// THE ONE SET, a folder under assets/icons/ (the APK packs it,
// android/app/build_apk.sh): Adwaita 3.22.0, hard-coded (architect
// 2026-10-14).
inline constexpr const char* kIconSet = "adwaita-3.22";

// THE GLYPHS BODY-CENTERED (architect 2026-10-12: every glyph, no
// exceptions, "the simpler rule is just center all of them"): each is drawn
// translated by (dx, dy) in the SVG's own user units (every file a 48-unit
// root; +x right, +y down), composed into resvg's render transform
// (svg_icon::rasterise) so it scales with the raster, the disabled copy
// derived from the shifted render. THE BODY-CENTERING TEST: the body is the
// drawing with its shadows removed STRUCTURALLY (an element under a
// feGaussianBlur filter, a fill of a radial gradient fading to transparent,
// an id or inkscape:label containing "shadow"); its alpha >= 0.1 bounds at
// 240 px give a bbox whose center the shift puts at the canvas centre. The
// table is GENERATED into icon_shifts.inc by tools/icon_center/gen_icon_shifts.py
// (never hand-edited), in kIconNames order. Where a shifted shadow crosses the
// canvas edge it clips, accepted. The shift is the product's, not the file's:
// the files are the extraction tool's output (two of them changed files, the
// set's README).
struct IconShift {
    const char* name;   // the enumerator's name, kIconNames' spelling
    float       dx;
    float       dy;
};
inline constexpr IconShift kIconShifts[] = {
#include "icon_shifts.inc"
};

// THE SET (architect 2026-10-12): the one bundled set, loaded once at launch
// (load_svg_set, below).
inline std::string_view gui_live_icon_set() { return kIconSet; }

// THE SET'S LOAD, once, in gui_main before the window: `set`'s files fetched from the bundle
// (GuiPlatform::bundled_icon_files — the repository's assets/icons/<set>/ on
// the laptop, the APK's icons/<set>/ assets on the tablet; READ IN PLACE,
// never copied into the config folder: nobody authors an icon set on the
// device),
// then every enumerator's `<name>.svg` parsed in enum order before any draw
// (svg_icon::parse; files of other names, the README among them, ignored).
// THE ERROR ARM'S PRODUCERS: the bundle unreadable (IO), a file missing or
// not well-formed SVG (a build defect) — "icon set 'adwaita-3.22': EditDelete.svg:
// not well-formed SVG (resvg error 7)", the first only, which gui_main
// prints and exits on (the validation doctrine's class (1)). A construct
// resvg does not draw passes the load: the set's import sheet is that check
// (svg_icon.h's error rule).
std::optional<std::string> load_svg_set(std::string_view set);

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::toggle_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion; the
// parsed drawings stay. (No color pick drops anything: no glyph wears a
// role, the head's rule.)
void drop_rasters();

// Draw `icon`'s cell onto the square (x, y, size_px, size_px): the
// glyph's live raster at the integer px nearest size_px, copied at the
// integer device px nearest (x, y) — every caller passes integers. The
// callers pass icon_glyph_px() (the program's case's seat) or their own scaled
// 16. Cairo state is saved and restored. Cannot fail: the launch's load
// parsed every drawing.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px);

// THE DISABLED GLYPH — a dead button's face: REACTOS'S SATURATE (architect
// 2026-10-06; comctl32's toolbar draws a disabled 32-bpp image with
// ILS_SATURATE | ILS_ALPHA at 192, imagelist.c's saturate_image,
// svg_icon::saturated_copy) UNDER EVERY CHROME since the cased glyphs became
// the program's (architect 2026-10-09: Cool Edit's disabled art is drawn per
// glyph and the product has no such art, so its dead case takes this saturate
// of the live drawing; cool_edit_paint.h) — of the live raster, built the
// first time a disabled face asks and cached beside it, copied at the same
// seat. A disabled WORD keeps the chrome's own rule (show_embossed_run,
// render.h: Windows' DSS_DISABLED emboss).
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
