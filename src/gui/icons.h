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
// px at 300 %, 72 at 360); elsewhere its site's own seat (the logo lane's
// quaver, the list rows, the cards). The window-frame glyphs Windows drew
// in the button text — the caption's, the app's own until 2026-10-24 — were
// never drawings of the set.
//
// THE PLACEMENT IS FIXED: in the program's case the cell sits at the case's
// highlight line from its corner (icon_case_lead_px, render.h — Cool Edit's
// (+1, +1), its 20-px bitmap's seat in its 23-W case, the product's 20-W
// seat in the same 23, architect 2026-10-09), filling the seat
// (icon_glyph_px), plus the key's down ride (no band key draws a glyph
// since the hi-fi skin, 2026-10-23, so no cased seat is drawn today). The
// CELL is never moved for its ink (Windows never did: 12 of the 15 STD strip
// cells with odd ink dimensions sit at the fixed offset); the DRAWING inside
// it is body-centered (THE GLYPHS BODY-CENTERED, below). The sites with no
// case — the list rows, the cards, the logo lane's quaver, the player's row —
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
    // THE VIEWPORT CONTROLS: Zoom to Fit, Center, the magnification
    // lamp (zoom.svg's three magnifiers retired 2026-10-16, "low contrast").
    FullZoomOut,         // Zoom to Fit (bare `0`) — the set's
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
    PlayerPrevious,      // the render player's Previous — the set's
                         // media-skip-backward (row 8's Go to Start wears it
                         // too since 2026-10-24, GoToStart's link)
    Play,                // the render player's Play (its Play/Pause button
                         // at rest, and the playing row's glyph in its list)
                         // — the set's media-playback-start
    Pause,               // Pause (the render player's row, while live) —
                         // the set's media-playback-pause
    PlayerNext,          // the render player's Next — the set's
                         // media-skip-forward (row 8's Go to End wears it
                         // too since 2026-10-24, GoToEnd's link)
    // THE RENDER PLAYER'S HIGHLIGHT PAIR, ITS OWN (architect 2026-10-22,
    // when the classical icon set drew the player's family in polished
    // steel and row 8's arrows in another metal): the pair does not wear row
    // 8's Down / Up. PNGs of the drop road alone, no Adwaita drawing beside
    // them; no key wears a glyph in the hi-fi skin (2026-10-23).
    PlayerDown,          // Highlight Next (the render player's down arrow)
                         // — the classical set's steel Player Down
    PlayerUp,            // Highlight Previous (the render player's up
                         // arrow) — the classical set's steel Player Up
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
                         // modified" — row 8's tab name names the tab; the
                         // tab-B copy with the other window in front
                         // stood 2026-10-12 to 2026-10-18)
    Settings,            // Settings (bare `;`) — the set's
                         // preferences-system
    // THE LIST ROWS' TWO GLYPHS (the folder overlay, the project picker),
    // BOXED DRAWINGS since 2026-10-23 (THE BOXED GLYPHS, below).
    FolderRow,           // a folder row — the planner's folder, a square
                         // with a tab at the top (architect 2026-10-23: "just
                         // a simple square with a tab at the top"; his pick
                         // variant c off three tabs, the body squarer, the
                         // tab a third of the width)
    WavRow,              // a wav row — the quaver, a symlink to Quaver.svg
                         // (architect 2026-10-23: "use the logo in the
                         // render player")
    // THE RENDER PLAYER'S ROW.
    RepeatOne,           // Toggle Repeat One — one glyph in both states,
                         // the lamp carrying the state; the set's
                         // media-playlist-repeat
    UpAFolder,           // Up a Folder (the render player) — the folder
                         // row's drawing, a symlink to FolderRow.svg
                         // (architect 2026-10-10: the plain folder, enough
                         // to tell it from the Highlight Previous arrow)
    // THE NOTIFICATION CARDS' CLASS GLYPHS (notifications.h), BOXED
    // DRAWINGS since 2026-10-24 (THE BOXED GLYPHS, below).
    CardNormal,          // a NORMAL card's glyph — the planner's i, a small
                         // rectangle over a larger one (architect
                         // 2026-10-24: "Spartan and minimalist … sit nicely
                         // with the other icons, the note and the folder";
                         // his pick I1; the set's dialog-information until
                         // then)
    CardCritical,        // a CRITICAL card's glyph — the planner's X, the
                         // caption's close at its stroke ratio (architect
                         // 2026-10-24: "a scalable X sort of like the close
                         // button in the caption, for the error"; his pick
                         // X1; a symlink to Cancel's process-stop until then)
    Close,               // Close (the render player) — the set's
                         // window-close
    Tooltips,            // Toggle Tooltips (bare backslash) — the set's
                         // help-browser
    JumpToDefiningMarker, // Jump to Defining Marker (Ctrl+J; its shift twin
                         // Ctrl+Shift+J, Copy Resolved Value, 2026-10-14) —
                         // the set's go-jump
    OpenTextEditor,      // Open Text Editor (bare Enter) — the set's
                         // accessories-text-editor (architect 2026-10-09)
    CopyPhaseResets,     // Copy Phase Resets (bare P; Ctrl+P its paste,
                         // Ctrl+Shift+P its state paste, architect
                         // 2026-10-24) — the phase reset's camera-photo, a
                         // symlink to PhaseReset.svg (the key prints COPY
                         // and draws no glyph in the hi-fi skin)
    GoToStart,           // Go to Start (Home; Shift+Home the piece's start,
                         // architect 2026-10-24) — the render player's
                         // media-skip-backward, a symlink to
                         // PlayerPrevious.svg (the key prints START)
    GoToEnd,             // Go to End (End; Shift+End the piece's end) — the
                         // render player's media-skip-forward, a symlink to
                         // PlayerNext.svg (the key prints END)
    DeleteFolder,        // Delete Folder (the render player) — the set's
                         // user-trash
    ValueDrag,           // Toggle Value Drag (bare `[`) — the pen, the
                         // drag it arms: the set's format-text-direction-rtl
                         // with its arrow and its shadows out, the nib at
                         // the lower left as drawn (architect 2026-10-18:
                         // "drop the arrows altogether"; the double arrow of
                         // 2026-10-11 retired); a changed file of the set,
                         // its README
    AppIcon,             // the program icon outside the window — the
                         // caption's quaver since 2026-10-23, a symlink to
                         // Quaver.svg (architect: "the new note glyph on the
                         // caption's color"; Warp's baton, AppIcon.png ->
                         // Warp.png, 2026-10-18 to 2026-10-23): the Android
                         // launcher icon a build product of this file
                         // (android/app/build_apk.sh's launcher step); the
                         // Linux icon packaging/warptempo_gui.svg, a link to
                         // Quaver.svg (docs/INSTALL.md). No painter draws
                         // it; the logo lane wears Quaver below
    Quaver,              // the logo lane's quaver (the caption's icon from
                         // 2026-10-23 until the caption went 2026-10-24;
                         // architect 2026-10-23, his
                         // pick row 4 of the caption sheet): the quaver,
                         // Gemini Argon's one-path conversion of the
                         // planner's bitmap at his prompt, a BOXED DRAWING
                         // (below); the wav rows wear it too (WavRow's link)
};

// Roster size, the names' and the load's count (load_set parses one
// document per enumerator). Keep it equal to the enumerator count above; a
// glyph joining or leaving restates this number.
inline constexpr int kIconCount = 64;

// THE FILE NAMES, the enumerators spelled in enum order: each enumerator's
// file in assets/icons/ is `<name>.png` if that file is there, else
// `<name>.svg` (load_set: THE PNG WINS, architect 2026-10-19). A misspelling
// is a missing file at the next launch, which fails it. THE REPEATS ARE
// SYMLINKS (architect 2026-10-19: "repeats should use symlinks"), relative,
// inside the folder, the canonical file the act that owns the drawing:
// AppIcon.svg -> Quaver.svg (2026-10-23; AppIcon.png -> Warp.png until
// then), BpmIterations.svg -> WavRow.svg, WavRow.svg -> Quaver.svg
// (2026-10-23), UpAFolder.svg -> FolderRow.svg, TransportPlay.svg -> Play.svg,
// and since 2026-10-24 CopyPhaseResets.svg -> PhaseReset.svg, GoToStart.svg ->
// PlayerPrevious.svg and GoToEnd.svg -> PlayerNext.svg (the folder's README
// lists them). Both bundle roads dereference a link (the laptop's
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
    "PlayerDown", "PlayerUp", "Cancel", "Down", "Up",
    "ReadOnly", "PreviousMarker", "NextMarker",
    "SwitchTab", "Settings", "FolderRow", "WavRow", "RepeatOne",
    "UpAFolder", "CardNormal", "CardCritical", "Close", "Tooltips",
    "JumpToDefiningMarker", "OpenTextEditor", "CopyPhaseResets",
    "GoToStart", "GoToEnd", "DeleteFolder", "ValueDrag",
    "AppIcon", "Quaver",
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
//
// THE BOXED GLYPHS (architect 2026-10-23: the caption's quaver, "use the
// logo in the render player, but then we need a folder icon as well … the
// same proportion as the caption"; the cards' i and X 2026-10-24, "sit
// nicely with the other icons, the note and the folder"): FOUR FILES —
// Quaver.svg (WavRow.svg its link), FolderRow.svg (UpAFolder.svg its link),
// CardNormal.svg and CardCritical.svg — are DRAWN TO A BOX, not centered on
// their ink: each a 34 x 40-unit viewBox (kGlyphBoxW x kGlyphBoxH) whose ink
// stands on rows 7..34, kGlyphBoxInkRows = 27 tall, and they are PLACED BY
// THE BOX — its rows centered on the seat, its left edge the seat's x. THEY
// ARE EXEMPT FROM THE BODY-CENTERING BY THEIR OWN FILES, not by a rule here:
// each root tag carries `data-body-shift="0 0"`, the declared-shift case
// above, so load_set takes the file's zero and measures nothing. ONE RULE
// FOR EVERY BOXED SEAT: the box is the ink's 40 / 27, ROUNDED AT THE BOX.
// A seat whose ink is authored in device px at 300 % takes icon_box_px —
// the logo lane's 21-px ink (skin_px(21 · 40 / 27), the box 31 tall and 26
// wide at 300 %) and the list rows' 27-px ink, .525 of the 51-px row
// (skin_px(40), the box 40 tall and 34 wide); a seat whose ink is already
// live device px, derived from live lengths, takes icon_box_live_px — the
// cards' ink, .525 of the first line's inside height (notifications.h's
// kNotificationGlyphRatio: the box 40 tall and 34 wide at 300 %).
inline constexpr double kGlyphBoxW       = 34.0;
inline constexpr double kGlyphBoxH       = 40.0;
inline constexpr double kGlyphBoxInkRows = 27.0;
// The box's height, device px, for a seat whose ink is `ink_px_at300`
// device px tall at 300 % (skin_px, rounded at the element, floored at 1).
inline int icon_box_px(double ink_px_at300) {
    return skin_px(ink_px_at300 * kGlyphBoxH / kGlyphBoxInkRows, 1);
}
// The box's height, device px, for a seat whose ink is `ink_px` LIVE device
// px tall (the same 40 / 27, rounded at the box with std::nearbyint,
// floored at 1).
inline int icon_box_live_px(double ink_px) {
    const int h = static_cast<int>(
        std::nearbyint(ink_px * kGlyphBoxH / kGlyphBoxInkRows));
    return h < 1 ? 1 : h;
}
// The box's width at height `box_h`, the viewBox's proportion, rounded.
inline int icon_box_w_px(int box_h) {
    return static_cast<int>(std::nearbyint(box_h * kGlyphBoxW / kGlyphBoxH));
}

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

// A BOXED GLYPH (THE BOXED GLYPHS, above) in its box: `box_h` device px
// tall (icon_box_px), its top-left (box_x, box_y). The raster is draw's
// square cell box_h a side, the drawing fitted to its height and centered
// across it (svg_icon::rasterise), so the cell stands half the cell's spare
// width left of the box's edge.
void draw_boxed(cairo_t* cr, Icon icon, int box_x, int box_y, int box_h);

// A BOXED GLYPH'S INK at `box_h` (architect 2026-10-24, the logo lane's
// quaver: "the glyph flush with the icons"): its lit columns and rows
// MEASURED OFF THE LIVE RASTER draw_boxed copies — a pixel is ink at alpha
// >= 128 — in device px from the box's top-left as draw_boxed places it,
// `left` / `top` the first ink column and row, `right` / `bottom` one past
// the last; never estimated from the file's box (the drawing's antialiasing
// and its fit to the raster decide where its ink lands). Measured once per
// (glyph, size), beside the raster in its cache (drop_rasters empties
// both). A raster with no ink answers four zeros. Its one reader is the
// logo lane's painter (paint_handler.cpp's paint_logo_lane).
struct BoxedInk {
    int left = 0, top = 0, right = 0, bottom = 0;
};
BoxedInk boxed_ink(Icon icon, int box_h);

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
// glyph. THE TONES ARE CONSTANTS since the hi-fi skin (2026-10-23;
// materials::disabled_glyph_tones, the record at cool_edit_paint.h's
// kCeDisabledShadow): the silhouette Cool Edit's 808080, the light copy
// F8F8F8, the rim black 000000 (his pick of 2026-10-21); the silhouette
// derived from the case's ramp 2026-10-21 to 2026-10-23 is git history. No
// band key wears a glyph in the skin, so this is the emboss for a caller
// that asks. The silhouette the
// live raster's ALPHA MASK (the body gray where
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
// dialogs' cases. A disabled WORD is a dead key's legend, dimmed (kDimInk,
// paint_key_legend; Windows' word emboss, show_embossed_run, retired
// 2026-10-23); cue text keeps its fade (cool_edit_derive.h).
void draw_disabled(cairo_t* cr, Icon icon, double x, double y,
                   double size_px);

} // namespace icons
