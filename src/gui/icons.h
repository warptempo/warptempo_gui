#pragma once

// THE ICON SET AND ITS ONE ROAD (architect 2026-10-06). An Icon is a GLYPH,
// one per enumerator below, drawn from THE LIVE SET (gui_live_icon_set
// below): the device config's `icons` key when the line is present, else
// the live chrome's own (chrome_spec.h's icon_set — win2000's and cde's
// TANGO 0.8.90'S SCALABLE DRAWINGS, ReactOS's model of a Windows 2000
// desktop wearing Tango (Tango's own files only, nothing drawn, no GNOME
// file, a repeat known by position; the set the product drew itself before
// is git history), clearlooks' MIST, gnome-icon-theme 2.30's drawings under
// Mist's own folders). THE BUNDLED SETS are kIconSetKeys below — Tango,
// Mist and, since 2026-10-10, BREEZE, the neutral set for tuning colors
// (architect 2026-10-10: "a neutral set that will help me with tuning the
// colors"). One scalable file per enumerator under assets/icons/<set>/ (its
// README the mapping, the provenance and the licences), PARSED AT LAUNCH
// through resvg, the one renderer of every set
// (svg_icon.h: the wrapper, the error rule and why a set is checked at its
// import), straight from the bundle — never copied into the config folder
// (load_svg_set, below) — and rasterised lazily per (glyph, device px,
// bound color) into an ARGB32 surface that a draw copies at an integer
// device px, no resample. THE INKS ARE THE DRAWINGS' OWN (gradients,
// strokes and opacities included), never a theme role — EXCEPT A BOUND
// DRAWING's; a DISABLED glyph is the live spec's rule (draw_disabled).
//
// THE BOUND DRAWING (architect 2026-10-10: Breeze "should take whatever the
// font color is that sits on the face, the chrome. It's okay if they lose
// some information in the process"): a file that paints in `currentColor`
// — every Breeze file, monochrome, and no Tango or Mist file — WEARS THE
// TEXT ROLE OF THE SURFACE IT STANDS ON ("text over a fill is the theme's
// recorded pair", now for a glyph too): every draw names its surface
// (GuiSurface, render.h; surface_text resolves the role under the live
// chrome) and the glyph takes that role's live bytes — the label on the
// toolbars and the player's row, the caption's text on the caption (active
// or inactive as the title is), the card's text on the cards, the list's
// text pair on the list rows (resting or lit). THE ROAD IS RESVG'S OWN: the
// file's bytes are kept, and a bound parse hands resvg the user stylesheet
// `svg { color: #RRGGBB; }` (svg_icon::parse), which `currentColor`
// resolves against — the file is never edited at run time, and a file with
// no `currentColor` is parsed once with no stylesheet exactly as before, so
// ONE ROAD SERVES EVERY SET and no set is special-cased by name. The parses
// are held per (glyph, color) and the rasters keyed on the color, so a
// glyph standing on two surfaces at once (AppIcon on an active and an
// inactive caption) keeps both; a chrome pick that moves the twelve drops
// the bound parses and rasters (drop_bound_faces) and they refill lazily
// in the new colors. THE PICTURE UNDER TANGO AND MIST IS UNCHANGED: their
// files carry no `currentColor`, so every draw keys the one unbound parse.
//
// AT EVERY SCALE the glyph is drawn at gui_scale like every chrome length
// (100 % is a curiosity, architect 2026-10-06): in the program's case the
// drawing's square cell (Tango's 48 units; Breeze's 24, its own 24-px
// seat, so 1 unit is 1 W and a 1-unit stroke 3 device px at 300 %) fills
// the case's glyph seat (icon_glyph_px, render.h:
// scaled_px(24), the base's large seat, 72 device px at 300 %); elsewhere
// its site's own scaled 16 (the caption's icon, the list rows, the cards —
// Windows' small icon). The window-frame glyphs
// Windows drew in the button text — the caption's — are not drawings of the
// set (paint_handler.cpp's caption glyph block).
//
// THE PLACEMENT IS FIXED: in the program's case the cell sits at the case's
// highlight line from its corner (icon_case_lead_px, render.h — Cool Edit's
// (+1, +1), its 20-px bitmap's seat in its 23-W case, the product's 24-W
// seat in its 27-W one, architect 2026-10-09), filling the seat
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

#include "render.h"        // the case geometry, live_chrome_spec(), GuiSurface

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

// THE BUNDLED SETS — the `icons` device key's whole vocabulary, a closed
// compiled list in the Settings row's order (kIconSetChoiceSource,
// app_state.h), each a folder under assets/icons/ (the APK packs every
// folder there, android/app/build_apk.sh): Tango, Mist and Breeze
// (architect 2026-10-10). THE DISPLAY NAMES stand beside the keys, Title
// Case, the Settings row's combo and list.
inline constexpr const char* kIconSetKeys[]         = {"tango", "mist",
                                                       "breeze"};
inline constexpr const char* kIconSetDisplayNames[] = {"Tango", "Mist",
                                                       "Breeze"};
static_assert(std::size(kIconSetKeys) == std::size(kIconSetDisplayNames));

// THE `icons` KEY'S GRAMMAR — the ONE owner, asked by the device config's
// reader (any other word the launch's first-error hard fail: a hand edit is
// the only producer) and by the settings editor's Icons row (its refused
// commit): a bundled set's key, byte for byte — chrome_spec.h's
// is_chrome_key's shape.
constexpr const char* icon_set_key_for(std::string_view v) {
    for (const char* k : kIconSetKeys)
        if (v == k) return k;
    return nullptr;
}
constexpr bool is_icon_set_key(std::string_view v) {
    return icon_set_key_for(v) != nullptr;
}
inline constexpr const char* kIconSetGrammarReason =
    "must be tango, mist or breeze";
// EVERY CHROME'S OWN SET IS A BUNDLED ONE (ChromeSpec::icon_set, the set an
// absent `icons` line means).
static_assert(chrome_specs_all([](const ChromeSpec& s) {
    return is_icon_set_key(s.icon_set);
}));

// THE SET A CONFIG MEANS under a chrome: its `icons` value, or with no line
// (an empty value) that chrome's own (ChromeSpec::icon_set) — what the
// launch loads (gui_live_icon_set) and what the Settings row shows
// (recall_gui_setting_value, settings_io.cpp, against the config's own
// `chrome`, the next launch's).
inline std::string_view effective_icon_set(std::string_view icons,
                                           const ChromeSpec& chrome) {
    return icons.empty() ? std::string_view(chrome.icon_set) : icons;
}

// THE LIVE SET (architect 2026-10-10): the device config's `icons`
// override installed ONCE at launch (set_live_icon_set, gui_main, beside
// set_live_chrome_spec and before load_svg_set), else the live chrome
// spec's own — effective_icon_set against the live chrome. A Settings
// commit of `icons` writes the file and takes effect at the next launch,
// the chrome's rule: the parsed set is built on it.
namespace icons_detail {
inline const char* g_override = nullptr;   // a kIconSetKeys entry, or none
} // namespace icons_detail
inline std::string_view gui_live_icon_set() {
    return effective_icon_set(icons_detail::g_override != nullptr
                                  ? icons_detail::g_override
                                  : "",
                              live_chrome_spec());
}
// Precondition: `icons` is empty (no line) or is_icon_set_key, the config's
// reader having judged it. ONE CALLER, gui_main, once per launch.
inline void set_live_icon_set(std::string_view icons) {
    icons_detail::g_override = icons.empty() ? nullptr
                                             : icon_set_key_for(icons);
    assert(icons.empty() || icons_detail::g_override != nullptr);
}

// THE SET'S LOAD, once, in gui_main after the device config's read (whose
// `icons`, or with no line its `chrome`, names the set) and before the
// window: `set`'s files fetched from the bundle
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

// THE RASTERS DROPPED, on a scale change (GuiInputHandler::apply_gui_scale)
// and on a flip of True Colors (GuiInputHandler::toggle_true_colors — the
// rasters are converted for the window as they are made, svg_icon.h): the
// cache refills lazily at the new sizes and in the new conversion; the
// parsed drawings stay (a bound parse keys its sRGB color, which neither
// change moves).
void drop_rasters();

// THE BOUND FACES DROPPED (2026-10-10, the head's BOUND DRAWING): every
// bound parse and every raster made from one, on a chrome pick that moved
// the live twelve (GuiColorPicker::install_live_words — a scheme's load, a
// chrome element's pick, its Paste, its OLD) — the surfaces' text roles
// may have moved, and a picker's drag would otherwise leave one parse per
// color it passed through. They refill lazily in the new colors. The
// unbound drawings and their rasters stay: under Tango and Mist this drops
// nothing. A palette pick (the program's fifteen) never calls it: no
// surface a glyph stands on is the palette's.
void drop_bound_faces();

// Draw `icon`'s cell onto the square (x, y, size_px, size_px): the
// glyph's live raster at the integer px nearest size_px, copied at the
// integer device px nearest (x, y) — every caller passes integers. The
// callers pass icon_glyph_px() (the program's case's seat) or their own scaled
// 16. `surface` is what the glyph stands on — a bound drawing wears its
// text role (the head's BOUND DRAWING); an unbound one ignores it. Cairo
// state is saved and restored. Cannot fail: the launch's load parsed every
// drawing.
void draw(cairo_t* cr, Icon icon, double x, double y, double size_px,
          GuiSurface surface);

// THE DISABLED GLYPH — a dead button's face: REACTOS'S SATURATE (architect
// 2026-10-06; comctl32's toolbar draws a disabled 32-bpp image with
// ILS_SATURATE | ILS_ALPHA at 192, imagelist.c's saturate_image,
// svg_icon::saturated_copy) UNDER EVERY CHROME since the cased glyphs became
// the program's (architect 2026-10-09: Cool Edit's disabled art is drawn per
// glyph and the product has no such art, so its dead case takes this saturate
// of the live drawing; cool_edit_paint.h) — of the live raster, built the
// first time a disabled face asks and cached beside it, copied at the same
// seat. A disabled WORD keeps the chrome's own rule (show_embossed_run,
// render.h: Windows' DSS_DISABLED emboss, GTK's etch, Motif's stipple).
// A bound drawing's disabled face is derived from its raster in the
// surface's text role (`surface`, as draw's) — the rule applied to a glyph
// already in the label's color, as Windows' disabled rule falls on a text
// glyph (architect 2026-10-10, accepted).
void draw_disabled(cairo_t* cr, Icon icon, double x, double y,
                   double size_px, GuiSurface surface);

// THE CASED GLYPH, a toolbar button's: the cell at the case's fixed lead
// Windows px (icon_case_lead_px — the spec's seat, the head's PLACEMENT)
// from `case_x`/`case_y`, the case's own top-left corner, plus
// `button_shift_px`, the button's pressed/checked shift (paint_button_box's
// `ButtonBoxFace::shift`, one Windows px right and down). Every roster
// button's glyph goes through here or draw_cased_disabled, so the seat has
// one owner. A CASE IS THE PROGRAM'S: the cased glyph's surface is
// GuiSurface::ProgramCase by definition (the band, row 8, the player's
// row), so the two cased calls name none.
void draw_cased(cairo_t* cr, Icon icon, int case_x, int case_y,
                double size_px, int button_shift_px);

// draw_cased's DEAD-BUTTON sibling: the same seat and shift, the glyph's
// disabled face (draw_disabled).
void draw_cased_disabled(cairo_t* cr, Icon icon, int case_x, int case_y,
                         double size_px, int button_shift_px);

} // namespace icons
