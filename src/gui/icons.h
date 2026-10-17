#pragma once

// THE ICON SET AND ITS ONE ROAD (architect 2026-10-06). An Icon is a GLYPH,
// one per enumerator below, drawn from THE ONE SET (kIconSet below):
// ADWAITA 3.22.0 ALONE (architect 2026-10-14), every drawing the
// adwaita-icon-theme 3.22.0 tarball's own src/fullcolor/ artwork at its 48
// slot, extracted as a standalone 48-unit file by
// tools/icon_set_adwaita_322/, nothing redrawn and nothing from another
// release — but ONE OWN FILE, the digital metronome (architect 2026-10-16:
// Warp's glyph and the program icon, the architect's own drawing, NOT
// Adwaita's, copied by the same tool; the set's README its provenance and
// licence) — EVERY DROP SHADOW REMOVED STRUCTURALLY at the extraction
// (architect 2026-10-15, "we'll go with none": a drop shadow is the live
// glyph's raised fiction, and Cool Edit's flat bitmaps never had one — the
// approved list tools/icon_set_adwaita_322/shadows.tsv); hard-coded (the `icons` device key and the Settings Icons row were
// removed 2026-10-12, docs/engineering/closed_questions.md). One scalable
// file per enumerator under assets/icons/<set>/ (its README the mapping, the
// provenance and the licences), PARSED AT LAUNCH through resvg, the one renderer of every set
// (svg_icon.h: the wrapper, the error rule and why a set is checked at its
// import), straight from the bundle — never copied into the config folder
// (load_svg_set, below) — and rasterised lazily per (glyph, device px) into
// an ARGB32 surface that a draw copies at an integer device px, no
// resample. THE INKS ARE THE DRAWINGS' OWN (gradients, strokes and
// opacities included), never a theme role; a DISABLED glyph is Cool Edit's
// emboss built from the live raster's mask and the drawing's own one-unit
// translate (draw_disabled) — no file of its own.
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
    DigitalMetronome,    // Warp — the architect's own digital metronome,
                         // the set's one own file (architect 2026-10-16:
                         // the tempo instrument; re-rendered 2026-10-17 at
                         // Adwaita 3.22's level of detail on the pda's
                         // grammar, its display the swing beside a quarter
                         // note, no lettering); appointment-new's clock retired
                         // ("it fits in theory, but it just didn't fit in
                         // practice")
    CameraPhoto,         // Phase Reset — the set's camera-photo, the phase
                         // snapshot: a reset re-grounds the synthesis phase
                         // to the source's at one frame and it decoheres
                         // from there (architect 2026-10-15)
    // THE VIEWPORT CONTROLS: Full Zoom Out, Center, the magnification
    // lamp (zoom.svg's three magnifiers retired 2026-10-16, "low contrast").
    ViewRestore,         // Full Zoom Out (bare `0`) — the set's
                         // view-restore, the view shrunk back whole
                         // (architect 2026-10-16)
    EditFind,            // Center on Focus (bare `c`) — the set's
                         // edit-find, the magnifier that finds the playhead
                         // (architect 2026-10-16)
    AudioVolumeHigh,     // Toggle Waveform Magnification — the set's
                         // audio-volume-high, the loud speaker for the
                         // louder waveform (architect 2026-10-16; zoom.svg's
                         // three magnifiers retired, "low contrast")
    // THE SINGLE-MARKER VERBS.
    ListAdd,             // Drop Marker (bare `s`)
    ListRemove,          // Delete Markers (`Delete`)
    ViewHidden,          // Toggle Disabled (`Ctrl+D`)
    InsertLink,          // Toggle Inherit (`Ctrl+N`)
    Merge,               // Flatten (`Ctrl+F`) — the set's edit-clear-all,
                         // the broom (architect 2026-10-14)
    BlackSum,            // Toggle Cumulative (`u`)
    GoJump,              // Toggle Follow (`f`)
    MusicNote16th,       // BPM Iterations (Ctrl+B)
    Mathmode,            // Toggle Grid Iterations (bare `i`)
    PreviewRenderOn,     // Play Renders
    DialogOkApply,       // Load in Place (the icon row and the render
                         // player's row): the walked version loaded into
                         // the editor in place. The set's edit-paste
                         // (architect 2026-10-14).
    DocumentOpenRecent,  // Toggle History View (`h`) — the set's
                         // document-open-recent, the clock turned back
                         // (architect 2026-10-16, the calendar of the
                         // night before retired: "give history the clock
                         // with backarrow back"); its digits Liberation
                         // Sans Bold outlines
    NetworkWiredDisconnected, // Toggle Session History (bare `g`, lit in
                         // Session) — the set's network-wired-disconnected,
                         // the unplugged cable: local means git off (the
                         // architect: "local means shut off git and stay
                         // local, so disconnect internet", 2026-10-15)
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
    AppIcon,             // the caption's icon — the digital metronome,
                         // DigitalMetronome's file byte for byte, the
                         // program icon outside the window too (architect
                         // 2026-10-16; tools/app_icon/)
};

// Roster size, the names' and the load's count (load_svg_set parses one
// document per enumerator). Keep it equal to the enumerator count above; a
// glyph joining or leaving restates this number.
inline constexpr int kIconCount = 60;

// THE FILE NAMES, the enumerators spelled in enum order: the set's
// `<name>.svg` (load_svg_set reads them). A misspelling is a missing file at
// the next launch, which fails it.
inline constexpr const char* kIconNames[] = {
    "DocumentOpen", "DocumentSave", "EditUndo", "EditRedo", "MediaRecord",
    "VcsCommit",
    "VcsPull", "AccessoriesDictionary", "InputTablet", "DigitalMetronome",
    "CameraPhoto",
    "ViewRestore", "EditFind", "AudioVolumeHigh", "ListAdd", "ListRemove",
    "ViewHidden", "InsertLink", "Merge", "BlackSum", "GoJump",
    "MusicNote16th", "Mathmode", "PreviewRenderOn",
    "DialogOkApply", "DocumentOpenRecent", "NetworkWiredDisconnected",
    "EditSelect",
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
// (svg_icon::rasterise) so it scales with the raster, the disabled copy's
// light half one more unit on the same shift (draw_disabled). THE BODY-CENTERING TEST: the body is the
// shipped file itself, shadow-less by construction (the head: every drop
// shadow removed at the extraction, architect 2026-10-15); its alpha >= 0.1
// bounds at 240 px give a bbox whose center the shift puts at the canvas
// centre. The table is GENERATED into icon_shifts.inc by
// tools/icon_center/gen_icon_shifts.py (never hand-edited), in kIconNames
// order. Where a shifted drawing crosses the canvas edge it clips, accepted.
// The shift is the product's, not the file's:
// the files are the extraction tool's output (two of them changed files and
// one the set's own, the set's README).
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

// THE DISABLED GLYPH — a dead button's face: COOL EDIT'S OWN (architect
// 2026-10-15). Cool Edit Pro 2.1 ships a disabled bitmap beside every
// toolbar glyph (BITMAP 221's strips, enabled | disabled), and that art is
// Windows' DSS_DISABLED emboss of the glyph's silhouette: the silhouette in
// 808080 over a F8F8F8 copy one bitmap px right and down
// (cool_edit_paint.h's kCeDisabledShadow / kCeDisabledLight, the measured
// bytes and the bitmaps) — two tones, no dark copy. The product builds the
// same thing mechanically, for every glyph of the set
// (svg_icon::embossed_copy), AND ADDS A THIRD TONE OF ITS OWN, not Cool
// Edit's (architect 2026-10-15 evening): THE INSET RIM, 505050
// (kCeDisabledInset), on the silhouette's pixels the light copy does not
// reach — a one-unit band inside the silhouette along its top-left edges,
// so the shadow line falls down and to the right as the pressed case goes;
// inside the silhouette because a dark copy peeking out up-left grew the
// glyph. The silhouette the live raster's ALPHA MASK (the body grey where
// the light copy overlaps it, the rim where it does not), the
// light copy THE DRAWING ITSELF TRANSLATED BY ONE ICON UNIT right and down
// (architect 2026-10-15, "the glyph's own line thickness": one of the file's
// 48 user units, seat_px / 48 device px — 1.25 at 300 %, 0.58 at 138 —
// universal), composed into the render transform as kIconShifts is and
// RASTERISED AT THAT OFFSET with exact antialiasing, never a shifted raster
// and never rounded to a device px; each mask's coverage kept, so
// antialiased edges stay soft. Built the first time a disabled face asks and
// cached beside the live raster, copied at the same seat. THE COPY REACHES
// ONE ICON UNIT PAST THE CELL, under one Windows px at every scale (20 / 48
// W in the case), and in the case it lands INSIDE THE CASE (render.h's THE
// CASE: the light line, the 20-W seat, the shadow line, the black line) — on
// the shadow line at rest, on the black line when a dead case stays checked
// (+1) — over the case its painter already put down, so the case's own
// damage covers it and nothing reaches a neighbor. Every dead glyph takes it
// — the band, row 8, the history groups, the render player's transport, the
// dialogs' cases. A disabled WORD keeps the chrome's own rule
// (show_embossed_run, render.h: the same Windows emboss in the chrome's
// roles); cue text keeps its fade (cool_edit_derive.h).
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
