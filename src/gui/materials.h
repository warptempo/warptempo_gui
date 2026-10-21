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
// the CLIENT window W x H — the whole window, the caption to the frame's
// bottom row — and CENTRED on it, the overhang cropped evenly both sides; no
// tiling, no mirror, no repeat, no seam, ever (the tablet: 1440 / 3072, the
// master 2580 x 1440, 138 px cut each side). A MATERIAL PIXEL ON SCREEN AT
// (x, y) IS THE COVER'S PIXEL AT (x, y), whatever paints it: the three are
// covers of the one window geometry. THE COVER MATCH (the planner's ruling
// 2026-10-21, after the laptop's maximized 1920 x 1047 — labwc's panel
// takes 33 rows — matched no cover and fell to the masters): for a client
// W x H the bake takes THE SMALLEST BUNDLED COVER w x h WITH w >= W AND
// h >= H (by area; the exact size the smallest of all) AND DECODES ITS
// CENTRE CROP — rows (h − H) / 2 on and columns (w − W) / 2 on, no
// filtering, the decode the bake (the 1920 x 1080 cover serves 1920 x 1047
// at a scale 1 % off the exact rule; a restored window shows the centre of
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
// first paint's head (materials::ensure_settled); a gui_scale commit and
// True Colors bake at once, one act one bake (materials::ensure). A CHANGE
// OF SIZE OF A BAKED WINDOW WAITS ON THE SETTLE GATE (2026-10-21, the
// laptop's frame drag, a configure per motion): no resize bakes (on_resize
// only stamps its arrival, materials::note_resize), and a paint that finds
// the size moved bakes nothing until kBakeSettle (150 ms) has passed with
// no further size change — the tick then arms the bake and damages the whole
// window (materials::settle_due), and that frame's first paint bakes.
// Meanwhile the painters draw from the LAST BAKE'S TILES where one still
// holds the fill, the material's flat mean where none does, and never a
// stale tile's pixel outside the new client. The tablet never resizes, so
// the gate costs it nothing (a transient size before the immersive insets
// settle is baked at once from the cover's crop, the settled one 150 ms
// later). EVERY GROUND FILL IS A 1:1 COPY FROM A BAKE AT THE SAME
// RECTANGLE; every line, bevel, ring and word a solid fill. ONLY THE PIXELS
// A MATERIAL IS EVER DRAWN ON ARE BAKED, as tiles of the window (the masters
// and covers never stay resident):
//   WOOD    the rows above the brass panel (the caption to the tab strip's
//           foot) and below it (the dock bar to the frame's bottom row),
//           window-wide, and the program frame's two columns beside the
//           panel; and THE VOID's copy of the band's rows (below);
//   METAL   the tab strip's rows to the canvas's top and the canvas's foot to
//           the dock bar, window-wide, and the canvas's two side margins
//           between (the canvas interior is the GPU's and never brass); and
//           THE INACTIVE TAB'S BODY rows (below);
//   CASE    THE CASE PATCHES (2026-10-21, a window-wide band per seat row
//           until then) — ONE g x g PATCH PER SEAT, at rest and pressed (one
//           line further in), for every case the band, row 8 and the render
//           player lay out in either state of the `h` view
//           (program_case_rects, paint_handler.h: the rows' own layouts),
//           each the cover at its own place under the case's ramp (below);
//           and THE CAPTION BUTTONS' THREE FACES.
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
// RESIDENT at 300 % on 2304 x 1440 8.7 MB (the wood 4.7, the brass 2.5, the
// ebony 1.5 — about fifty seats' patches; 2.7 as the seat rows' bands), on
// the laptop's maximized 1920 x 1047 at 138 % 3.0 MB (ARGB32, measured
// 2026-10-21; materials::resident_bytes, which the trace line prints at
// every bake).
//
// THE REGIONS — WHICH MATERIAL A SURFACE IS (GuiMaterialRegion, a scope the
// painter names):
//   WOOD   the caption and the menu row, ONE SLAB (no line under the caption,
//          its top light line kept, active and inactive alike — THE CAPTION
//          IS THE WOOD, architect 2026-10-21, "ok to delete other captions":
//          the 42 built-in captions, the product's flat `default` and the
//          `caption` key left the same day, palette_file.h's head); under
//          the menu row Cool Edit's stack as its painters
//          have it (the program frame's dark line, the pane's dark groove, its
//          light line, the pane); the band's panes; the tab strip's ground;
//          the dock bar, row 8 and the border under it; the frame's own
//          columns and bottom row, THE COVER AS DRAWN (the frame of record).
//          THE VOID — the band's recess between the panes — is the wood UNDER
//          THE LUMINANCE RULE AT kWoodVoidLuma (his one exception to "the
//          wood as drawn"); every other wood Face pixel is the cover itself.
//   METAL  ONE PANEL from the column's air to the dock bar inside the frame's
//          columns (the air, the view bar's, the ruler's and the marker
//          lane's grounds, the cue labels' face boxes with them, the canvas's
//          side margins and its rings, the divider's face row in the margins,
//          the foot) and THE TWO TABS' BODIES, the strip's ground staying
//          wood: the lit tab the brass as drawn, the inactive tab's body the
//          brass under Cool Edit's measured 15-row gradient as a LUMA RATIO
//          (kTabBodies' Default row, each row's HLS lightness over the
//          Default Face's taken onto the brass Face — the frame's lighter
//          inactive tab). The divider's face row inside the canvas is the
//          GPU's flat Face.
//   CASE   EVERY CASE (the band's, row 8's, the render player's) and THE
//          THREE CAPTION BUTTONS. The case's 20 ramp rows are the ebony UNDER
//          THE LUMINANCE RULE off its own Face with Cool Edit's case span,
//          kCaseRampTop at the top row to kCaseRampBottom at the bottom (HLS
//          lightness added, the saturation kept), its lines flat strokes off
//          that Face (kCaseHighlight, the shadow's two ends kCaseShadowFirst /
//          kCaseShadowLast and the corner kCaseCorner), the black outline and
//          the pressed and checked rings Cool Edit's constants; pressed, the
//          ramp one line down as before. Each seat's patch is the case cover
//          at its own place (the frame's: every seat differs, no hash). The
//          caption buttons the same ebony at kCaptionButtonFace, their light
//          lines the case's highlight and their dark its shadow's first tone,
//          their Marlett glyphs kNearWhite.
//   FLAT   EVERYTHING ELSE THAT IS CHROME — the pull-downs, the cards and the
//          tooltip, the modal family's box and its controls row (the render
//          player's ground), the on-screen keyboard, the dialog fields, the
//          laptop's sizing frame: today's painters, flat, their roles derived
//          by chrome_derive.h's rules FROM THE METAL'S FACE (the text's swaps
//          following it) — the scope's default.
// UNTOUCHED: the well and everything the palette draws (the canvas, the view
// bar's black field and span, the cues' triangles and dots, the playhead),
// every glyph, the cases' black outline and constant rings.
//
// THE TONE RULE (architect 2026-10-21): EACH MATERIAL'S FACE IS ITS BAKED
// COVER'S MEAN COLOR, measured over its baked pixels at the bake (the
// ebony's over its patches; materials::face), and EVERY TONE A PAINTER PUTS
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
// fill's own extents; any other tone that material's flat stroke. NO
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
// THE INKS (constants, but the shadow's offset a key): THE WORDS ON THE WOOD —
// the caption's title and the menu row's File / Edit / Settings, their
// access-key underlines with them — in kWordInk (old brass, his ink B) over a
// kWordShadow drop shadow (+d, +d) DEVICE PX, d the `text_shadow` key
// (materials::text_shadow_px; the settled mock's 2 at 300 %); THE TAB LABELS
// kNearWhite over the same shadow; THE RULER'S DIGITS AND TICKS and THE
// MARKER LANE'S RESTING LABELS kNearWhite (a disabled cue's label fading from
// it toward the metal's Face; the triangles, the yellow head, the dots, the
// selected label's chrome text unchanged); THE CLOCK — row 8's and the render
// player's two fields — kTimeFieldDigits on kTimeFieldGround, their rings the
// region's tones, geometry and pads unchanged. A disabled word's emboss and
// a disabled glyph's are as before, the word's light copy the material's own
// light tone (materials::flat).

enum class GuiMaterial { Wood, Metal, Case };
inline constexpr int kGuiMaterialCount = 3;
// THE DROP NAMES, the masters' file stems, in the enum's order.
inline constexpr const char* kGuiMaterialNames[kGuiMaterialCount] = {
    "Wood", "Metal", "Case"};

// THE REGION A SURFACE STANDS IN (the head's map), the current one a scope's
// (GuiMaterialScope): which material a flat tone resolves to. Flat the
// default.
enum class GuiMaterialRegion { Flat, Wood, Metal, CaptionButton };

// THE FORTEPIANO CHROME'S CONSTANTS (the frame of record's; 0xRRGGBB, sRGB).
// THE VOID: the band's recess, the wood as drawn times this luma ratio.
inline constexpr double kWoodVoidLuma = 0.72;
// THE CASE (the generator's ebony_cases_tiled): the ramp's HLS lightness
// offset at its top and bottom rows, the lines' offsets, and the caption
// buttons' face.
inline constexpr double kCaseRampTop       = 0.10;
inline constexpr double kCaseRampBottom    = -0.04;
inline constexpr double kCaseHighlight     = 0.14;
inline constexpr double kCaseShadowFirst   = -0.08;
inline constexpr double kCaseShadowLast    = -0.10;
inline constexpr double kCaseCorner        = -0.10;
inline constexpr double kCaptionButtonFace = 0.03;
// THE WORDS' INK AND SHADOW (old brass, his ink B; the gold D9B45A not
// taken), the near-white of the tab labels, the lanes' digits and labels and
// the caption buttons' glyphs.
inline constexpr uint32_t kWordInk    = 0xC9A961;
inline constexpr uint32_t kWordShadow = 0x140C06;
inline constexpr uint32_t kNearWhite  = 0xF0F0F0;
// THE TIME FIELDS' GROUND AND DIGITS: amber on black.
inline constexpr uint32_t kTimeFieldGround = 0x000000;
inline constexpr uint32_t kTimeFieldDigits = 0xF0B030;

namespace materials {

// THE LAUNCH'S CHECK (the head): every material has a master or a cover in
// the bundle and every one of its files is a PNG or a JPEG. Answers the first
// error, prefixed `tiles: ` and naming the file — gui_main's hard fail.
std::optional<std::string> check();

// THE BAKE, AT ONCE (the head's BAKE RULE): when the key `app` gives
// differs from the baked one, every material is baked again from the bundle
// and the palette reinstalled off the new Faces (install_palette, render.h —
// its generation rebuilds every cached surface the tiles were painted into)
// — the window repainted by the caller's damage. A zero-sized client bakes
// nothing. A FAILURE is the bundle changed under the running process (check
// passed at launch): its stderr line and the hard fail. Callers: the one-act
// roads, GuiInputHandler::apply_gui_scale and toggle_true_colors.
void ensure(const AppState& app);
// THE BAKE BEHIND THE SETTLE GATE (the head's BAKE RULE): the first bake at
// once; a changed key of a baked window only once the tick has armed it
// (settle_due), else nothing — the paint draws from the last bake's tiles.
// Caller: the head of GuiPaintHandler::on_redraw, before any pixel.
void ensure_settled(const AppState& app);
// A SIZE CHANGE ARRIVED (GuiPaintHandler::on_resize): the gate's clock
// restarts and any armed bake waits again.
void note_resize();
// THE GATE'S RE-ARM (the loop's tick, main.cpp): true ONCE when a paint has
// found the size moved and kBakeSettle has passed since the last size
// change — the caller then damages the whole window, whose frame bakes.
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
// material — the Face its baked tile under the path's extents (the
// material's flat mean where no one tile holds them), the void its copy,
// any other tone the material's flat stroke — set as the source; false
// where `c` is no tone, the region is Flat or nothing is baked, the caller
// then setting it flat.
bool set_source(cairo_t* cr, GuiColor c);
// THE FLAT STROKE `c` resolves to in the current region (a tone's, or `c`
// itself): an ink that must wear a material's tone without its cover — a
// disabled word's emboss copies.
GuiColor flat(GuiColor c);
// ONE RECT OF A MATERIAL AS DRAWN, split over its tiles (the frame's
// columns, the canvas column's face): each part the cover, a part no tile
// holds the material's flat mean.
void paint_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m);

// THE CASE'S LINES (the head's CASE), flat strokes off the ebony Face under
// the bevel keys.
struct CaseLines {
    GuiColor highlight, shadow_first, shadow_last, corner;
};
CaseLines case_lines();
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
