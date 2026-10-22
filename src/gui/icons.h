#pragma once

// THE ICONS AND THEIR ONE ROAD (architect 2026-10-06). An Icon is AN ACT'S
// (OR A FACE'S) GLYPH, one per enumerator below, ONE FILE PER ENUMERATOR in
// ONE FLAT FOLDER, assets/icons/, each file NAMED BY ITS ENUMERATOR, which is
// named by the act (architect 2026-10-19; the folder's README the mapping,
// the provenance and the licences). THE FOLDER IS THE DROP ROAD (architect
// 2026-10-19: "simply drop a new icon with the right name … the goal is I can
// build and push after any icon change, without needing to consult"): ANY
// ENUMERATOR MAY BE A PNG — his own drawings, made with Google's Gemini and
// cut out by him in GIMP, square with alpha at 256 px — and `<Name>.png`
// WINS OVER `<Name>.svg`, the Adwaita SVG left beside it as the fallback until
// he calls the seat settled. NOTHING STANDS BETWEEN THE DROP AND THE BUILD: no
// generator, no table, no README row and no code change when a PNG replaces an
// existing act's glyph (load_set below reads whichever file is there, and the
// body-centering is measured at the launch, THE GLYPHS BODY-CENTERED below).
// Where no PNG stands, the drawing is ADWAITA 3.22.0's (architect
// 2026-10-14): the adwaita-icon-theme 3.22.0 tarball's own src/fullcolor/
// artwork at its 48 slot, extracted as a standalone 48-unit file by
// tools/icon_set_adwaita_322/, nothing redrawn and nothing from another
// release — two seats CHANGED by the same tool and recorded under the licence
// in the folder's README: History's clock with its digits outlined, and Value
// Drag's pen with its arrow removed (2026-10-18) — EVERY DROP SHADOW REMOVED
// STRUCTURALLY at the extraction (architect 2026-10-15, "we'll go with none":
// a drop shadow is the live glyph's raised fiction, and Cool Edit's flat
// bitmaps never had one — the approved list
// tools/icon_set_adwaita_322/shadows.tsv). Hard-coded (the `icons` device key
// and the Settings Icons row were removed 2026-10-12,
// docs/engineering/closed_questions.md). PARSED AT LAUNCH, an SVG through
// resvg and a PNG decoded once by resvg and scaled by an area filter
// (svg_icon.h: the wrapper, the error rule, why an SVG is checked at its
// import, and the PNG's decode and filter), straight from the bundle — never
// copied into the config folder (load_set, below) — and rasterised lazily per
// (glyph, device px) into an ARGB32 surface that a draw copies at an integer
// device px, no resample. THE INKS ARE THE DRAWINGS' OWN (gradients, strokes
// and opacities included), never a theme role; a DISABLED glyph is Cool
// Edit's emboss built from the live raster's mask and the drawing's own
// one-unit translate (draw_disabled) — no file of its own.
//
// AT EVERY SCALE the glyph is drawn at gui_scale like every chrome length
// (100 % is a curiosity, architect 2026-10-06): in the program's case the
// drawing's square cell (every SVG a 48-unit root, the sheet's slot nested in
// it as its own viewport; a PNG's longer side read as the same 48 units)
// fills the case's glyph seat (icon_glyph_px, render.h: scaled_px(20), Cool
// Edit's own seat since 2026-10-09 ~10:25, program_spec.h's head — 60 device
// px at 300 %, 72 at 360); elsewhere its site's own scaled 16 (the caption's
// icon, the list rows, the cards — Windows' small icon). The window-frame
// glyphs Windows drew in the button text — the caption's — are not drawings
// of the set (paint_handler.cpp's caption glyph block).
//
// THE PLACEMENT IS FIXED: in the program's case the cell sits at the case's
// highlight line from its corner (icon_case_lead_px, render.h — Cool Edit's
// (+1, +1), its 20-px bitmap's seat in its 23-W case, the product's 20-W
// seat in the same 23, architect 2026-10-09), filling the seat
// (icon_glyph_px), plus the pressed/checked shift (draw_cased below). The
// CELL is never moved for its ink (Windows never did: 12 of the 15 STD strip
// cells with odd ink dimensions sit at the fixed offset); the DRAWING inside
// it is body-centered (THE GLYPHS BODY-CENTERED, below). The sites with no
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

// The glyphs, one per file of assets/icons/ (each file named by its
// enumerator — kIconNames below). AN ICON IS AN ACT'S (OR A FACE'S) GLYPH,
// ONE FILE PER ENUMERATOR, the enumerator the act's tooltip name in
// PascalCase ("Toggle" dropped, the lamp carrying it; a stateful button's
// faces suffixed — SavePull, TransportStop; a face a site shows
// that is no roster act named for its site — FolderRow, WavRow, CardNormal,
// CardCritical, AppIcon) (architect 2026-10-19: "since the actions are
// essentially complete, we should use the action name"); each comment names
// the act and, where an Adwaita drawing stands, the drawing it wears (the
// folder's README the table). A DRAWING WORN BY SEVERAL ACTS IS ONE FILE AND
// SYMLINKS (kIconNames' comment); the glyph a stateful button wears in each
// state is redesign_button_icon's (paint_handler.cpp).
enum class Icon {
    // THE ICON ROW'S FIRST GROUP.
    OpenProject,         // Open Project (and its shift twin, Revert) — the
                         // set's document-open (architect 2026-10-07)
    Save,                // Save — the set's document-save (architect
                         // 2026-10-14: the floppy retired)
    Undo,                // Undo — the set's edit-undo
    Redo,                // Redo — the set's edit-redo
    Render,              // Render (and the iteration sweep: the tooltip
                         // alone forks) — the set's media-record
    SaveCommit,          // Save's face in the history view and while the
                         // checkpoint publishes — the set's go-top
    SavePull,            // Save's face in the history view while GitHub is
                         // ahead — the set's go-bottom
    // THE VIEW PAIRS (architect 2026-10-14): the Source / Target pair (bare
    // 1 / 2) and the Warp / Phase-reset pair (bare 3 / 4), one glyph each.
    Source,              // Source — the set's accessories-dictionary, the
                         // primary source
    Target,              // Target — the set's input-tablet, the authoring
                         // surface
    Warp,                // Warp — the conductor's baton: the architect's
                         // GIMP cutout of Gemini Flash's drawing
                         // (2026-10-18; since ~14:15 Flash's redo at the
                         // set's outline weight, his pick B1 — the
                         // morning's outline-less cutout read thin beside
                         // the outlined glyphs), a PNG of the drop road
                         // (Warp.png, 256 x 256 since 2026-10-19; "the argon
                         // one is too cartoony", the SVG batons not taken);
                         // the folder's README its provenance
    PhaseReset,          // Phase Reset — the set's camera-photo, the phase
                         // snapshot: a reset re-grounds the synthesis phase
                         // to the source's at one frame and it decoheres
                         // from there (architect 2026-10-15)
    // THE VIEWPORT CONTROLS: Full Zoom Out, Center, the magnification
    // lamp (zoom.svg's three magnifiers retired 2026-10-16, "low contrast").
    FullZoomOut,         // Full Zoom Out (bare `0`) — the set's
                         // view-restore, the view shrunk back whole
                         // (architect 2026-10-16)
    Center,              // Center on Focus (bare `c`) — the set's
                         // edit-find, the magnifier that finds the playhead
                         // (architect 2026-10-16)
    WaveformMagnification, // Toggle Waveform Magnification — the set's
                         // audio-volume-high, the loud speaker for the
                         // louder waveform (architect 2026-10-16; zoom.svg's
                         // three magnifiers retired, "low contrast")
    // THE SINGLE-MARKER VERBS.
    DropMarker,          // Drop Marker (bare `s`) — the set's list-add
    DeleteMarkers,       // Delete Markers (`Delete`) — the set's list-remove
    Disabled,            // Toggle Disabled (`Ctrl+D`) — the set's
                         // audio-volume-muted
    Inherit,             // Toggle Inherit (`Ctrl+N`) — the set's insert-link
    Flatten,             // Flatten (`Ctrl+F`) — the set's edit-clear-all,
                         // the broom (architect 2026-10-14)
    Follow,             // Toggle Follow (`f`) — the set's start-here
    BpmIterations,       // BPM Iterations (Ctrl+B) — the set's
                         // audio-x-generic, WavRow's drawing (a symlink)
    GridIterations,      // Toggle Grid Iterations (bare `i`) — the set's
                         // x-office-spreadsheet
    PlayRenders,         // Play Renders — the set's applications-multimedia
    LoadInPlace,         // Load in Place (the icon row and the render
                         // player's row): the walked version loaded into
                         // the editor in place. The set's edit-paste
                         // (architect 2026-10-14).
    HistoryView,         // Toggle History View (`h`) — the set's
                         // document-open-recent, the clock turned back (its
                         // digits Liberation Sans Bold outlines), history's
                         // again since 2026-10-18 (architect: "go back to
                         // using the original git clock icon"; the calendar
                         // 2026-10-17 late evening to 2026-10-18); a changed
                         // file of the set, its README
    AddToSelection,     // Toggle Add to Selection — the set's
                         // edit-select-all
    Older,               // Older checkpoint (`,`) — the set's
                         // media-seek-backward
    Newer,               // Newer checkpoint (`.`) — the set's
                         // media-seek-forward
    Left,                // Row 8's left arrow (bare Left) — the set's
                         // go-previous
    Right,               // Row 8's right arrow (bare Right) — the set's
                         // go-next
    Revert,              // Revert (bare `v`): back to the viewed
                         // checkpoint. The set's document-revert
                         // (architect 2026-10-07 ~17:00)
    // ROW 8'S TRANSPORT: THE EDITOR'S PLAY / STOP, ONE BUTTON'S TWO FACES
    // (RedesignButton::TransportPlayStop: bare Space is one toggle with no
    // pause state), A PAIR OF ITS OWN (architect 2026-10-20): the editor's
    // toggle and the render player's transport wear different drawings, so
    // a drop for the one (the conductor raising the baton, bringing both
    // hands down) leaves the other's media glyphs standing. TransportPlay
    // is a SYMLINK to the player's Play.svg until a drop (kIconNames'
    // comment), parsed once more at the launch from the same bytes —
    // accepted, the raster cache is per enumerator anyway; TransportStop
    // owns its drawing outright, the render player wearing no stop.
    TransportPlay,       // the editor's Play (the face while stopped) —
                         // Play's drawing, a symlink
    TransportStop,       // the editor's Stop (the face while an audition
                         // runs) — the set's media-playback-stop, its own
                         // file, no symlink
    // THE RENDER PLAYER'S TRANSPORT, the set's media glyphs: its Play/Pause
    // button wears Pause while live.
    PlayerPrevious,      // the render player's Previous (row 8 has no skip
                         // since 2026-10-12) — the set's media-skip-backward
    Play,                // the render player's Play (its Play/Pause button
                         // at rest, and the playing row's glyph in its list)
                         // — the set's media-playback-start
    Pause,               // Pause (the render player's row, while live) —
                         // the set's media-playback-pause
    PlayerNext,          // the render player's Next — the set's
                         // media-skip-forward
    Cancel,              // Render's mid-render Cancel face — the set's
                         // process-stop
    Down,                // Row 8's down arrow (bare Down) — the set's go-down
    Up,                  // Row 8's up arrow (bare Up) — the set's go-up
    // THE READ-ONLY TOGGLE'S ONE PADLOCK, worn in both states (architect
    // 2026-10-21: "it's not supposed to have an open and closed state"; the
    // pair of 2026-10-14 reversed): its lit / sunken face says locked, as
    // every toggle's does.
    ReadOnly,            // Toggle Read-Only — changes-prevent
    // THE MARKER WALK AND THE TAB SWITCH.
    PreviousMarker,      // Previous Marker (Shift+Tab) — the set's go-first
    NextMarker,          // Next Marker (Tab) — the set's go-last
    SwitchTab,           // Switch Tab (Ctrl+Tab) in both tabs — the set's
                         // preferences-system-windows (architect
                         // 2026-10-18: "use the original, not the
                         // modified" — the tab strip names the tab; the
                         // tab-B copy with the other window in front
                         // stood 2026-10-12 to 2026-10-18)
    Settings,            // Settings (bare `;`) — the set's
                         // preferences-system
    // THE LIST ROWS' TWO GLYPHS (the folder overlay, the project picker).
    FolderRow,           // a folder row — the set's folder
    WavRow,              // a wav row — the set's audio-x-generic
    // THE RENDER PLAYER'S ROW.
    RepeatOne,           // Toggle Repeat One — one glyph in both states,
                         // the lamp carrying the state; the set's
                         // media-playlist-repeat
    UpAFolder,           // Up a Folder (the render player) — the folder
                         // row's drawing, a symlink to FolderRow.svg
                         // (architect 2026-10-10: the plain folder, enough
                         // to tell it from the Highlight Previous arrow)
    // THE NOTIFICATION CARDS' CLASS GLYPHS (notifications.h).
    CardNormal,          // a NORMAL card's glyph — the set's
                         // dialog-information
    CardCritical,        // a CRITICAL card's glyph — Cancel's drawing,
                         // process-stop (a symlink)
    Close,               // Close (the render player) — the set's
                         // window-close
    Tooltips,            // Toggle Tooltips (bare backslash) — the set's
                         // help-browser
    JumpToDefiningMarker, // Jump to Defining Marker (Ctrl+J; its shift twin
                         // Ctrl+Shift+J, Copy Resolved Value, 2026-10-14) —
                         // the set's go-jump
    OpenTextEditor,      // Open Text Editor (bare Enter) — the set's
                         // accessories-text-editor (architect 2026-10-09)
    DeleteFolder,        // Delete Folder (the render player) — the set's
                         // user-trash
    ValueDrag,           // Toggle Value Drag (bare `[`) — the pen, the
                         // drag it arms: the set's format-text-direction-rtl
                         // with its arrow and its shadows out, the nib at
                         // the lower left as drawn (architect 2026-10-18:
                         // "drop the arrows altogether"; the double arrow of
                         // 2026-10-11 retired); a changed file of the set,
                         // its README
    AppIcon,             // the caption's icon — Warp's baton, a symlink to
                         // Warp.png, the program icon outside the window too
                         // (a build product of this file since 2026-10-19:
                         // android/app/build_apk.sh's launcher step, and the
                         // Linux icon a link to it, docs/INSTALL.md)
};

// Roster size, the names' and the load's count (load_set parses one
// document per enumerator). Keep it equal to the enumerator count above; a
// glyph joining or leaving restates this number.
inline constexpr int kIconCount = 58;

// THE FILE NAMES, the enumerators spelled in enum order: each enumerator's
// file in assets/icons/ is `<name>.png` if that file is there, else
// `<name>.svg` (load_set: THE PNG WINS, architect 2026-10-19). A misspelling
// is a missing file at the next launch, which fails it. THE REPEATS ARE
// SYMLINKS (architect 2026-10-19: "repeats should use symlinks"), relative,
// inside the folder, the canonical file the act that owns the drawing:
// AppIcon.png -> Warp.png, BpmIterations.svg -> WavRow.svg, CardCritical.svg
// -> Cancel.svg, UpAFolder.svg -> FolderRow.svg and TransportPlay.svg ->
// Play.svg (the folder's README lists them). Both bundle roads dereference a link (the laptop's
// read_bundle_folder, platform_wayland.cpp; build_apk.sh's copy), so the
// load reads a link as the file it names; a PNG dropped for a repeated act
// replaces that act's glyph alone, the link's other wearer keeping the link.
inline constexpr const char* kIconNames[] = {
    "OpenProject", "Save", "Undo", "Redo", "Render", "SaveCommit",
    "SavePull", "Source", "Target", "Warp", "PhaseReset",
    "FullZoomOut", "Center", "WaveformMagnification", "DropMarker",
    "DeleteMarkers", "Disabled", "Inherit", "Flatten",
    "Follow", "BpmIterations", "GridIterations", "PlayRenders",
    "LoadInPlace", "HistoryView", "AddToSelection",
    "Older", "Newer", "Left", "Right", "Revert", "TransportPlay",
    "TransportStop", "PlayerPrevious", "Play", "Pause", "PlayerNext",
    "Cancel", "Down", "Up",
    "ReadOnly", "PreviousMarker", "NextMarker",
    "SwitchTab", "Settings", "FolderRow", "WavRow", "RepeatOne",
    "UpAFolder", "CardNormal", "CardCritical", "Close", "Tooltips",
    "JumpToDefiningMarker", "OpenTextEditor", "DeleteFolder", "ValueDrag",
    "AppIcon",
};
static_assert(std::size(kIconNames) == kIconCount);

// THE GLYPHS BODY-CENTERED (architect 2026-10-12: every glyph, no
// exceptions, "the simpler rule is just center all of them"). Each glyph is
// drawn translated by (dx, dy) in the drawing's own user units (every SVG a
// 48-unit root, a PNG's 48-unit frame; +x right, +y down), composed into
// resvg's render transform or the PNG's filter (svg_icon::rasterise) so it
// scales with the raster, the disabled copy's light half one more unit on the
// same shift (draw_disabled). THE SHIFT: the body is the shipped file itself —
// an Adwaita SVG shadow-less by construction (the head: every drop shadow
// removed at the extraction, architect 2026-10-15), a PNG as he cut it — its
// alpha >= 0.1 (>= 26/255) bounds give a bbox, and the shift puts the bbox
// center at the frame's center, rounded to 0.1 unit; a drawing with no pixel
// at 0.1 takes shift 0. THREE CASES, one per file in load_set:
//   - A BUILDER SVG CARRIES ITS SHIFT IN THE FILE, the root tag's
//     `data-body-shift="dx dy"` (two numbers, one decimal each, a space
//     between, e.g. "2.6 -0.1"), measured by tools/icon_set_adwaita_322/
//     body_shift.py with rsvg-convert at 240 px — the 2026-10-12 table's own
//     method, so the numbers are the ones judged then — and read by
//     svg_icon::parse from the tag's bytes before resvg sees them (resvg
//     ignores the attribute). It travels with the file: a symlinked repeat
//     carries it, no table, no name lookup (architect 2026-10-19: the launch
//     "used to be very snappy"; ~0.3 s of measuring at every launch was felt
//     on the laptop, the tablet slower still).
//   - AN SVG WITHOUT THE ATTRIBUTE (a dropped one, never the builder's) is
//     measured at the launch on resvg's render at 240 px
//     (svg_icon::body_center_shift; ~12 ms on average on the laptop, the
//     blurs and masks dominating). The two measures agree within 0.1 unit on
//     every builder file (2026-10-19).
//   - A PNG is measured at the launch on its decoded raster at its native
//     size (body_center_shift; free, no render), so a dropped PNG needs no
//     step.
// Where a shifted drawing crosses the cell's edge it clips, accepted. The
// shift is the product's: the builder writes it beside the drawing it
// measured, and nothing else in a file is the product's.

// THE LOAD, once, in gui_main before the window: the folder's files fetched
// from the bundle (GuiPlatform::bundled_icon_files — the repository's
// assets/icons/ on the laptop, the APK's icons/ assets on the tablet; READ IN
// PLACE, never copied into the config folder: nobody authors an icon on the
// device), then per enumerator in enum order `<name>.png` if present
// (svg_icon::parse_png), else `<name>.svg` (svg_icon::parse), parsed and its
// body shift taken from the file or measured (THE GLYPHS BODY-CENTERED,
// above), all before any draw — THE PNG WINS,
// an SVG of the same name beside it unread (the Adwaita fallback left on
// purpose, architect 2026-10-19); files of other names (the README, the
// licence texts) ignored. THE ERROR ARM'S PRODUCERS: the bundle unreadable
// (IO), neither file there, a file not well-formed SVG, an SVG whose
// data-body-shift is malformed, or a `.png` not a PNG (a build defect) —
// "icons: Warp.png: not a PNG", the first only, which gui_main
// prints and exits on (the validation doctrine's class (1)). A construct
// resvg does not draw passes the load: an SVG's import sheet is that check
// (svg_icon.h's error rule).
std::optional<std::string> load_set();

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::apply_true_colors — the
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
// same thing mechanically, for every glyph
// (svg_icon::embossed_copy), AND ADDS A THIRD TONE OF ITS OWN, not Cool
// Edit's (architect 2026-10-15 evening): THE INSET RIM (kCeDisabledInset,
// 505050 then, black since 2026-10-21), on the silhouette's pixels the light copy does not
// reach — a one-unit band inside the silhouette along its top-left edges,
// so the shadow line falls down and to the right as the pressed case goes;
// inside the silhouette because a dark copy peeking out up-left grew the
// glyph. THE TONES ARE THE CASE'S SINCE 2026-10-21 (architect, "r1": the
// constant 808080 read as a light grey blob on the ebony), and HIS OWN
// NUMBERS since his pick v2 the same night: the silhouette at the case
// ramp's mean lightness times 0.84 on the ebony's hue and saturation
// (2A1E1C on the shipped ebony; Cool Edit's 0.502 / 0.809 = 0.620 the rule
// it departs from), the rim black 000000 (the silhouette's times 0.625
// until then), the light copy F8F8F8 unchanged
// (materials::disabled_glyph_tones; the record at cool_edit_paint.h's
// kCeDisabledShadow). The silhouette the
// live raster's ALPHA MASK (the body grey where
// the light copy overlaps it, the rim where it does not), the
// light copy THE DRAWING ITSELF TRANSLATED BY ONE ICON UNIT right and down
// (architect 2026-10-15, "the glyph's own line thickness": one of the file's
// 48 user units, seat_px / 48 device px — 1.25 at 300 %, 0.58 at 138 —
// universal), composed into the render transform as the body shift is and
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
// `button_shift_px`, the button's pressed/checked shift (paint_roster_case's
// `ButtonBoxFace::shift`, one line right and down). Every roster
// button's glyph goes through here or draw_cased_disabled, so the seat has
// one owner.
void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px);

// draw_cased's DEAD-BUTTON sibling: the same seat and shift, the glyph's
// disabled face (draw_disabled).
void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px);

} // namespace icons
