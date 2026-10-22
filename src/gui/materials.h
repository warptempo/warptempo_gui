#pragma once

#include "render.h"   // GuiRect, GuiColor, the chokepoints

#include <cairo/cairo.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct AppState;

// THE MATERIALS — THE FORTEPIANO CHROME (architect 2026-10-21, a Salzburg
// Walter fortepiano: "we follow the Salzburg instrument as much as we can").
// THE FRAME OF RECORD is tmp/theme_next/out/cover_walnut1.png (the tablet at
// 300 %), its generator tmp/theme_next/make_tiles4.py (build_frame4); where a
// number here and that frame part, the frame is the law. Cool Edit's flat
// chrome becomes THREE MATERIALS — WALNUT, an even dark BRASS (the lock
// plate) and EBONY (the naturals) — and every lane, case, bar (row 8's,
// re-laid the same day: program_spec.h's row-8 bar fields), tab, ruler and
// line keeps its geometry exactly; only what fills it
// changes. This header is the one owner of the materials, the cover and bake
// rules, the regions, the tone rule on a material and the chrome's inks.
//
// THE MATERIALS ARE A DROP FOLDER: `assets/tiles/`, ONE MASTER PER MATERIAL
// NAMED BY ITS ROLE (kGuiMaterialNames: `Wood`, `Metal`, `Case`), each
// `<Name>.png` or `<Name>.jpg`, THE PNG WINNING where both stand (the icons'
// rule, icons.h) — Gemini-drawn 4K masters (assets/tiles/README.md) — and
// BESIDE EACH MASTER ITS PRE-RENDERED COVERS (architect 2026-10-21: "launch
// must not filter 17-megapixel masters"), `<Name>-<W>x<H>.png` at the two
// devices' standard windows, 2304 x 1440 and 1920 x 1080, rendered by
// tools/tiles/render_covers.py under the cover rule below with PIL's Lanczos
// (the mocks' own filter) and rerun after a drop. Read from the bundle as the
// icons are, BY NAME (GuiPlatform::bundled_tile_names and
// bundled_tile_bytes: the laptop's compiled-in folder, every file of it; the
// APK's `tiles/` assets, THE TABLET'S THREE COVERS AND NOT THE MASTERS,
// build_apk.sh). A MATERIAL WITH NEITHER A MASTER NOR A COVER, A FILE THAT
// IS NEITHER A PNG NOR A JPEG, OR A COVER NOT THE SIZE ITS NAME SAYS, IS THE
// LAUNCH'S HARD FAIL naming the file (materials::check, which reads each
// file's first 64 KiB alone); a window no cover reaches with no master to
// scale — the tablet's, were it ever larger than 2304 x 1440 — the bake's,
// the same way.
//
// THE COVER RULE (his "zoom to fit so there is no empty space on the screen,
// exactly"): each master is scaled by ONE factor s = max(W / w, H / h) over
// the CLIENT window W x H — the whole window, the caption to the border's
// last row — and CENTERED on it, the overhang cropped evenly both sides; no
// tiling, no mirror, no repeat, no seam, ever (the tablet: 1440 / 3072, the
// master 2580 x 1440, 138 px cut each side). A MATERIAL PIXEL ON SCREEN AT
// (x, y) IS THE COVER'S PIXEL AT (x, y), whatever paints it: the three are
// covers of the one window geometry, the client alone (the restored laptop's
// sizing frame round it took the brass's pixels beyond it 2026-10-21 to
// 2026-10-22; it wears Windows' constants since, render.h's caption block).
// THE COVER MATCH (the planner's ruling
// 2026-10-21, after the laptop's maximized 1920 x 1047 — labwc's panel
// takes 33 rows — matched no cover and fell to the masters): for a client
// W x H the bake takes THE SMALLEST BUNDLED COVER w x h WITH w >= W AND
// h >= H (by area; the exact size the smallest of all) AND DECODES ITS
// CENTER CROP — rows (h − H) / 2 on and columns (w − W) / 2 on, no
// filtering, the decode the bake (the 1920 x 1080 cover serves 1920 x 1047
// at a scale 1 % off the exact rule; a restored window shows the center of
// the same grain); ONLY A WINDOW BEYOND EVERY COVER IN A DIMENSION scales a
// master under the cover rule, by the icons' Lanczos-3 area filter
// (svg_icon::raster_regions) from one decode.
//
// THE BAKE RULE (architect: "absolutely has to be"; the cost table, cairo
// into a 2304 x 1440 surface: a solid fill 0.016 ms, a 1:1 blit of a
// window-sized bake at identity with EXTEND_NONE and FILTER_NEAREST 0.047 ms,
// an EXTEND_REFLECT pattern 1.73 ms, ~100x a solid): BAKE ONCE PER WINDOW
// GEOMETRY — its key the client size, the gui_scale (which moves the lanes)
// and True Colors (the bakes hold the window's bytes) — never at a palette
// install, a project open or a bevel key's OK. A frame compares the key
// alone; the layout is read at the bake. THE FIRST BAKE IS IMMEDIATE, at the
// first frame's PRE-PAINT HOOK (materials::ensure_settled, main.cpp — ahead
// of the backend's paint walk); a gui_scale commit and True Colors bake at
// once, one act one bake (materials::ensure). EVERY BAKE REINSTALLS THE
// PALETTE off the new Faces, so the next pre-paint widens that frame's
// damage to the whole client and rebuilds the flag cache from the new tiles
// (take_bake_landed; 2026-10-21). A CHANGE
// OF SIZE OF A BAKED WINDOW WAITS ON THE SETTLE GATE (2026-10-21, the
// laptop's frame drag, a configure per motion): no resize bakes (on_resize
// only stamps its arrival, materials::note_resize), and a paint that finds
// the size moved bakes nothing until kBakeSettle (150 ms) has passed with
// no further size change — the tick then arms the bake and damages the
// client (materials::settle_due), and that frame's pre-paint hook bakes.
// Meanwhile the painters draw from the LAST BAKE'S TILES where one still
// holds the fill, the material's flat mean where none does, and never a
// stale tile's pixel outside the new client. The tablet never resizes, so
// the gate costs it nothing (a transient size before the immersive insets
// settle is baked at once from the cover's crop, the settled one 150 ms
// later). EVERY GROUND FILL IS A 1:1 COPY FROM A BAKE AT THE SAME
// RECTANGLE; every line, bevel, ring and word a solid fill. ONLY THE PIXELS
// A MATERIAL IS EVER DRAWN ON ARE BAKED, as tiles of the window (the masters
// and covers never stay resident):
//   WOOD    the rows above the brass panel (the band's top to the tab
//           strip's foot; the caption's rows from 2026-10-21 to 2026-10-22,
//           when the caption left the wood) and below it (the dock bar to
//           the border's last row), window-wide (the program frame's two
//           columns beside the panel went 2026-10-21); the rows THE BORDER
//           UNDER ROW 8 stands on when a dialog editor's stack lifts it onto
//           the on-screen keyboard's band (modal_box.h); and THE VOID's copy
//           of the band's rows (below);
//   METAL   ONE TILE, THE WHOLE CLIENT (2026-10-21: the cards, the tooltip,
//           the modal family, the keyboard's band and the Settings popup
//           stand anywhere on it — the brass panel from the tab strip's top
//           to the dock bar until then; the sizing frame's band round the
//           client with it 2026-10-21 to 2026-10-22); and THE INACTIVE TAB'S
//           BODY rows (below);
//   CASE    THE CASE PATCHES (2026-10-21, a window-wide band per seat row
//           until then) — ONE g x g PATCH PER SEAT, at rest and pressed (one
//           line further in), for every case the band, row 8 and the render
//           player lay out in either state of the `h` view
//           (program_case_rects, paint_handler.h: the rows' own layouts),
//           each the cover at its own place under the case's ramp (below)
//           (the caption buttons' three faces with them 2026-10-21 to
//           2026-10-22).
// THE LAUNCH ROAD (2026-10-21): the bundle listed once, each material's
// source chosen by name, and ONLY THAT FILE READ, in one sized read; the
// three materials READ, DECODED, DERIVED AND CONVERTED ON THREE THREADS AT
// ONCE (std::jthread; each its own file, its own tiles, nothing shared), the
// tiles taken into the state on the main thread after the join. A decode is
// resvg's one render of the regions' union through the data-URL detour (the
// tablet's cairo has no image reader); at a cover's identity the kept
// pixels are copied straight out of it (svg_icon::raster_regions); the
// window's conversion is the transform's word through its table-driven
// encode (display_transform.h's p3_word_from_srgb_fast).
// RESIDENT at 300 % on 2304 x 1440 about 19 MB (the brass's one tile 13.3
// and its tab body 0.4, the wood 3.8 with its void, the ebony 1.4 — about
// fifty seats' patches), on the laptop's maximized 1920 x 1047 at 138 %
// about 10 MB (the brass 8.0, the wood 1.4) (ARGB32, computed off the
// geometry 2026-10-22 when the caption left the wood and the ebony and the
// sizing frame's band the brass — 19.5 and the brass 8.1 the day before,
// 8.7 and 3.0 MB measured that morning; materials::resident_bytes, which
// the trace line prints at every bake, is the measure).
//
// THE REGIONS — WHICH MATERIAL A SURFACE IS (GuiMaterialRegion, a scope the
// painter names):
//   WOOD   Cool Edit's band directly under the caption, its stack as its
//          painters have it (the pane's dark groove, its light line, the
//          pane); the band's panes; the tab strip's ground; the dock bar,
//          row 8 and the border under it, THE COVER AS DRAWN (the frame of
//          record). (THE CAPTION WAS THE WOOD 2026-10-21 to 2026-10-22, its
//          top light line kept, with the program frame's dark top row under
//          it; since 2026-10-22 it is flat ebony and the row is gone —
//          render.h's caption block.) THE VOID — the band's recess between
//          the panes — is the wood UNDER THE LUMINANCE RULE AT kWoodVoidLuma
//          (his one exception to "the wood as drawn"); every other wood Face
//          pixel is the cover itself.
//   METAL  ONE PANEL from the column's air to the dock bar, window-wide (the
//          air, the view bar's, the ruler's and the marker lane's grounds,
//          the cue labels' face boxes with them, the canvas's side margins
//          and its rings, the divider's face row in the margins, the foot)
//          and THE TWO TABS' BODIES, the strip's ground staying wood: the lit
//          tab the brass as drawn, the inactive tab's body the brass under
//          Cool Edit's measured 15-row gradient as a LUMA RATIO (kTabBodies'
//          Default row, each row's HLS lightness over the Default Face's
//          taken onto the brass Face — the frame's lighter inactive tab). The
//          divider's face row inside the canvas is the GPU's flat Face. AND
//          SINCE 2026-10-21 THE FAMILY THAT WAS FLAT (architect: "the light
//          brown should be brass, but the dark brown should stay a flat
//          surface — the user shouldn't be typing on a non-flat color";
//          "dropdowns should be brass"): THE CARDS AND THE TOOLTIP, THE
//          MODAL FAMILY'S BOX MARGINS, ITS RING, A PROMPT'S FACE INTERIOR AND
//          ITS CONTROLS ROW (modal_box.h: the dialog editors, the prompts,
//          the render player, the project picker, the progress modal), THE
//          ON-SCREEN KEYBOARD'S BAND and THE SETTINGS POPUP — each the brass
//          as drawn at its own place, its relief the brass Face's tones (the
//          laptop's sizing frame among them 2026-10-21 to 2026-10-22).
//   CASE   EVERY CASE (the band's, row 8's, the render player's). The
//          case's 20 ramp rows are the ebony UNDER
//          THE LUMINANCE RULE off its own Face with Cool Edit's case span,
//          kCaseRampTop at the top row to kCaseRampBottom at the bottom (HLS
//          lightness added, the saturation kept), its lines flat strokes off
//          that Face (kCaseHighlight, the shadow's two ends kCaseShadowFirst /
//          kCaseShadowLast and the corner kCaseCorner), the black outline Cool
//          Edit's constant. THE PRESSED AND CHECKED FACE (architect
//          2026-10-21, "the top and left should be darker than the case";
//          Cool Edit's constant 808080 / F8F8F8 rings, measured on a light
//          grey case, read as a raised white frame on the ebony): the outer
//          ring black on the top and left and THE CASE'S HIGHLIGHT on the
//          bottom and right, the inner ring THE CASE'S LAST SHADOW TONE on
//          the top and left and nothing on the bottom and right, the ramp one
//          line down to the outer ring there (paint_ce_case). Each seat's
//          patch is the case cover at its own place (the frame's: every seat
//          differs, no hash). A dead glyph is Cool Edit's glyph emboss, ITS
//          TONES THE CASE'S (architect 2026-10-21, "r1"): the silhouette and
//          the rim Cool Edit's measured ratios taken onto this ramp's mean
//          lightness, the light copy F8F8F8 (disabled_glyph_tones, below).
//          (The caption buttons wore the same ebony at a face of their own,
//          their Marlett glyphs the ink, 2026-10-21 to 2026-10-22.)
//   FLAT   NO GROUND SINCE 2026-10-21 (the family above went to the brass).
//          The scope's default still: what stays flat is named by its painter
//          — EVERY FIELD (a dialog editor's line, the lists' ground where you
//          pick, the time fields: the chrome's field pair), THE WORD BUTTONS
//          AND THE KEYBOARD'S KEYS ("ebony eventually, flat fine for now"),
//          every case's own lines and rings — their roles derived by
//          chrome_derive.h's rules FROM THE METAL'S FACE (the text's swaps
//          following it).
// UNTOUCHED: the well and everything the palette draws (the canvas, the view
// bar's black field and span, the cues' triangles and dots, the playhead),
// every glyph, the cases' black outline; and THE WINDOWS PARTS (2026-10-22,
// render.h's caption block): the caption's flat ground, its three buttons
// and the sizing frame, constants all.
//
// THE TONE RULE (architect 2026-10-21): EACH MATERIAL'S FACE IS ITS BAKED
// COVER'S MEAN COLOR, measured at the bake (materials::face) — the brass's
// over its ground tile as decoded (before the tab body is derived from it),
// THE WOOD'S OVER THE ROWS ABOVE AND BELOW THE PANEL WINDOW-WIDE, THE
// CAPTION'S AMONG THEM (before the void is derived), and THE EBONY'S OVER
// THE FULL-WIDTH ROWS ITS PATCHES ARE CUT FROM AND THE CAPTION ROW
// (each seat row's g + lw rows, window-wide), NOT OVER THE TILES: those
// rows were each material's sample when its tones were judged — the ebony's
// before the patches replaced the seat rows' bands, both before the caption
// left the wood and the ebony (2026-10-22), its rows decoded for the means
// alone since — and measuring them still keeps every tone derived from a
// Face where it shipped (bake_wood, bake_case) — and EVERY TONE A PAINTER PUTS
// DOWN ON A MATERIAL IS COOL EDIT'S TONE RULE (cool_edit_derive.h's kTones:
// HLS L' = a·L + b, the hue and saturation kept; the case's own span above)
// ON THAT FACE, AS A FLAT STROKE — no grain on a line: the 1-W light and
// dark lines, the pane frames, the grooves, the rings, row 8's bars' lines.
// Only the Face tone itself is the cover as drawn. THE ROAD IS ONE
// CHOKEPOINT: the
// painters keep their palette members — every panel tone a tone of the metal
// Face, the palette's `face` (render.h's install) — and set_surface_source
// (render.h) knows each flat tone by its bytes (set_tone_face) and swaps the
// source for the scope's material: the Face its cover, the tile under the
// fill's own extents where they meet the client; any other tone that
// material's flat stroke. NO
// PAINT-TIME ALPHA: a mitre's antialiased diagonal is the two sources.
//
// THE BEVEL KEYS (tuning keys with Settings rows, device_config.h): the
// device keys `bevel_light` / `bevel_dark` (percent, 100 the rule exactly):
// every LIGHTER-than-Face tone's per-channel factor f becomes
// 1 + (f − 1)·p / 100 with p the light key, every darker one the same with
// the dark key, on every material's flat strokes (set_bevel) — applied live at
// the OK, recomputing the solid tones alone, never a bake. (The mocks' light
// tones were a multiplicative shortcut that clips, FFD688; the HLS tone
// differing a little from them is the mock's inaccuracy.)
//
// THE INKS (constants, but the shadow's offset a key): ONE INK, kWordInk
// (architect 2026-10-21, v1 of five: "they should be lower contrast; only
// flags should be very high contrast"), ON EVERY WORD AND DIGIT ON THE CHROME
// BUT THE FLAGS — the caption's title (dulled to render.h's
// kCaptionInkInactive while the window is inactive, 2026-10-22), the tab
// labels, the ruler's digits and ticks, row 8's clock and the render
// player's two fields (on kTimeFieldGround, their rings the region's tones,
// geometry and pads unchanged) and row 8's state line (the caption
// buttons' glyphs 2026-10-21 to 2026-10-22: Windows' black since, render.h's
// caption block); THE WORDS
// that wore a drop shadow keep it — kWordShadow at (+d, +d) DEVICE PX, d the
// `text_shadow` key (materials::text_shadow_px; the settled mock's 2 at
// 300 %), on the title and the tab labels — and the ruler's digits and the
// state line their own (+1, +1) shadows. THE MARKER LANE'S RESTING LABELS
// stay kNearWhite, THE ONE HIGH-CONTRAST TEXT (a disabled cue's label fading
// from it toward the metal's Face; the triangles, the yellow head, the dots,
// the selected label's chrome text unchanged). (The old brass C9A961 on the
// title and the menu row's words, the near-white on the tab labels, the
// ruler, the caption glyphs, and the amber F0B030 on the clock, retired the
// same day.) THE SELECTION PAIR is the constant kSelectionFill under
// kSelectionText (chrome_derive.h's rows). The cards', the tooltip's and the
// dialogs' words keep the chrome's text roles. A disabled word's emboss is
// as before, its light copy the material's own light tone (materials::flat);
// a disabled glyph is Cool Edit's emboss (the caption's Marlett cell
// Windows' DSS_DISABLED emboss since 2026-10-22, render.h's caption block).

enum class GuiMaterial { Wood, Metal, Case };
inline constexpr int kGuiMaterialCount = 3;
// THE DROP NAMES, the masters' file stems, in the enum's order.
inline constexpr const char* kGuiMaterialNames[kGuiMaterialCount] = {
    "Wood", "Metal", "Case"};

// THE REGION A SURFACE STANDS IN (the head's map), the current one a scope's
// (GuiMaterialScope): which material a flat tone resolves to. Flat the
// default. (CaptionButton, the caption buttons' ebony, 2026-10-21 to
// 2026-10-22, is retired.)
enum class GuiMaterialRegion { Flat, Wood, Metal };

// THE FORTEPIANO CHROME'S CONSTANTS (the frame of record's; 0xRRGGBB, sRGB).
// THE VOID: the band's recess, the wood as drawn times this luma ratio.
inline constexpr double kWoodVoidLuma = 0.72;
// THE CASE (the generator's ebony_cases_tiled): the ramp's HLS lightness
// offset at its top and bottom rows and the lines' offsets.
inline constexpr double kCaseRampTop       = 0.10;
inline constexpr double kCaseRampBottom    = -0.04;
inline constexpr double kCaseHighlight     = 0.14;
inline constexpr double kCaseShadowFirst   = -0.08;
inline constexpr double kCaseShadowLast    = -0.10;
inline constexpr double kCaseCorner        = -0.10;
// THE INK (architect 2026-10-21, v1 his pick of five: "they should be lower
// contrast; only flags should be very high contrast"): ONE COLOR on every
// word and digit on the chrome but the flags — the caption's title, the tab
// labels, the ruler's digits and ticks, row 8's clock and the render
// player's two fields and the state line —
// over the words' shadow where a word wore it (the old brass C9A961, "too
// golden", retired). THE FLAG LABELS' NEAR-WHITE is the one high-contrast
// text, and the selection's text.
inline constexpr uint32_t kWordInk    = 0xE6D5A8;
inline constexpr uint32_t kWordShadow = 0x140C06;
inline constexpr uint32_t kNearWhite  = 0xF0F0F0;
// THE TIME FIELDS' GROUND (black) AND DIGITS (the ink; the amber F0B030
// until 2026-10-21).
inline constexpr uint32_t kTimeFieldGround = 0x000000;
inline constexpr uint32_t kTimeFieldDigits = kWordInk;
// THE SELECTION PAIR (architect 2026-10-21, "brass-like … the selection text
// white or near-white"): a constant fill and the near-white text everywhere
// the chrome's selected pair paints — the flag editor's selected substring,
// the lit popup rows, the lists' selected rows, the dialog editors'
// selections (chrome_derive.h's selected_fill / selected_text rows; the
// waveform's ink under the program text's other member, 2026-10-16..10-21,
// retired).
inline constexpr uint32_t kSelectionFill = 0x9C7B3A;
inline constexpr uint32_t kSelectionText = kNearWhite;

namespace materials {

// THE LAUNCH'S CHECK (the head): every material has a master or a cover in
// the bundle and every one of its files is a PNG or a JPEG. Answers the first
// error, prefixed `tiles: ` and naming the file — gui_main's hard fail.
std::optional<std::string> check();

// THE BAKE, AT ONCE (the head's BAKE RULE): when the key `app` gives
// differs from the baked one, every material is baked again from the bundle
// and the palette reinstalled off the new Faces (install_palette, render.h —
// its generation rebuilds every cached surface the tiles were painted into)
// — the client repainted by the caller's damage, the whole surface by the
// next pre-paint (take_bake_landed). A zero-sized client bakes
// nothing. A FAILURE is the bundle changed under the running process (check
// passed at launch): its stderr line and the hard fail. Callers: the one-act
// roads, GuiInputHandler::apply_gui_scale and apply_true_colors.
void ensure(const AppState& app);
// THE BAKE BEHIND THE SETTLE GATE (the head's BAKE RULE): the first bake at
// once; a changed key of a baked window only once the tick has armed it
// (settle_due), else nothing — the paint draws from the last bake's tiles.
// Caller: the pre-paint hook (main.cpp), before the backend paints any
// pixel.
void ensure_settled(const AppState& app);
// WHETHER A BAKE HAS LANDED since the last ask (either road above), the
// flag cleared: the pre-paint hook's question, answered by the whole
// client's damage and the flag cache's rebuild in that frame.
bool take_bake_landed();
// A SIZE CHANGE ARRIVED (GuiPaintHandler::on_resize): the gate's clock
// restarts and any armed bake waits again.
void note_resize();
// THE GATE'S RE-ARM (the loop's tick, main.cpp): true ONCE when a frame has
// found the size moved and kBakeSettle has passed since the last size
// change — the caller then damages the client, whose frame's pre-paint
// hook bakes.
bool settle_due();
// THE TRACE'S GATE, read once: WT_PAINT_TRACE=1 in the environment, or on
// Android, which cannot set one for an app, the system property
// `debug.warptempo.trace` == "1" (`adb shell setprop debug.warptempo.trace
// 1` before a launch); the two lines it opens are the bake's (materials.cpp)
// and the full-window chrome paint's (GuiPaintHandler::on_redraw), stderr,
// logcat on the tablet.
bool trace_enabled();
// Whether a bake has landed (construction state paints flat).
bool baked();
// THE MATERIAL'S FACE, its baked cover's mean, 0xRRGGBB sRGB.
uint32_t face(GuiMaterial m);

// THE BEVEL KEYS (the head), in percent: the solid tones recomputed.
void set_bevel(int light_percent, int dark_percent);
// THE WORDS' SHADOW OFFSET, device px (the `text_shadow` key, the head's
// INKS).
void set_text_shadow(int px);
int text_shadow_px();

// THE TONE TABLE: the flat tones a painter hands the surface chokepoint,
// each Cool Edit's tone of THE CHROME'S FACE (the palette's `face`, the
// metal's) — the Face itself and the panel tones a relief or a ground puts
// down (kTones' Face tones but the text, the label and the cue's shadow,
// which are ink) — rebuilt by every palette install.
void set_tone_face(uint32_t chrome_face);

// THE SURFACE CHOKEPOINT'S ROAD (render.h's set_surface_source), called with
// the fill's PATH ALREADY BUILT: `c` as a tone of the current region's
// material — the Face its baked tile under the path's extents where they
// meet the client (a path running past the window's edge, a cue label's box
// near it, takes the tile holding its visible part; the material's flat
// mean where no one tile holds that), the void its copy,
// any other tone the material's flat stroke — set as the source; false
// where `c` is no tone, the region is Flat or nothing is baked, the caller
// then setting it flat.
bool set_source(cairo_t* cr, GuiColor c);
// THE FLAT STROKE `c` resolves to in the current region (a tone's, or `c`
// itself): an ink that must wear a material's tone without its cover — a
// disabled word's emboss copies.
GuiColor flat(GuiColor c);
// ONE RECT OF A MATERIAL AS DRAWN, split over its tiles (the canvas
// column's face; client coordinates, the client's pixels alone): each part
// the cover, a part no tile holds the material's flat mean.
void paint_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m);

// THE CASE'S LINES (the head's CASE), flat strokes off the ebony Face under
// the bevel keys.
struct CaseLines {
    GuiColor highlight, shadow_first, shadow_last, corner;
};
CaseLines case_lines();
// THE DISABLED GLYPH'S TONES (architect 2026-10-21, "r1" and his pick v2;
// the rule at cool_edit_paint.h's kCeDisabledShadow): Cool Edit's emboss
// taken onto the live case — the silhouette at the case ramp's mean HLS
// lightness times kCeDisabledShadowRatio (0.84, his pick) on the ebony
// Face's hue and saturation, the rim black (kCeDisabledInset), the light
// copy Cool Edit's F8F8F8 — rebuilt with the flat strokes at every
// bake. 0xRRGGBB. Read by every disabled glyph (icons::draw_disabled), its
// cache keyed on the tones.
struct DisabledGlyphTones {
    uint32_t shadow, light, inset;
    bool operator==(const DisabledGlyphTones&) const = default;
};
DisabledGlyphTones disabled_glyph_tones();
// THE CASE'S RAMP on the g x g glyph seat at (x, y): the case patch baked for
// that seat (`down` the pressed one, one line further in), a copy at the
// same rectangle; a seat no patch was baked for (a size change waiting on
// the gate) takes the ramp's twenty flat stops off the ebony Face.
void paint_case_ramp(cairo_t* cr, int x, int y, int g, bool down);
// THE INACTIVE TAB'S BODY over `body` (the head's METAL): a copy of its
// baked rows at the same rectangle.
void paint_tab_body(cairo_t* cr, const GuiRect& body);

// THE RESIDENT BAKES' BYTES (the head's figures).
std::size_t resident_bytes();

} // namespace materials

// THE REGION SCOPE (GuiMaterialRegion): the region in force while it
// stands, the previous one back at its end. Main thread only.
class GuiMaterialScope {
public:
    explicit GuiMaterialScope(GuiMaterialRegion region);
    ~GuiMaterialScope();
    GuiMaterialScope(const GuiMaterialScope&)            = delete;
    GuiMaterialScope& operator=(const GuiMaterialScope&) = delete;

private:
    GuiMaterialRegion previous_;
};
// (THE ORIGIN SCOPE, GuiMaterialOriginScope — the client's origin offset
// under the sizing frame's painter, whose band round the client took the
// brass's tile, 2026-10-21 to 2026-10-22 — is retired with that band.)
