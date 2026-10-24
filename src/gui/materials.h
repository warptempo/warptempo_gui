#pragma once

#include "cool_edit_derive.h" // the HLS round trip the ring tones and the accent take
#include "render.h"           // GuiRect, GuiColor, the chokepoints

#include <cairo/cairo.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct AppState;

// THE MATERIALS — THE HI-FI PANEL SKIN (architect 2026-10-23 night, "the
// design is approved": "current attempt to replicate classical on modern is
// failing … let's embrace that"; his pick off 41 mocks, a late-80s black
// component deck — brushed black aluminium, blank keys with printed legends).
// THE FRAMES OF RECORD are tmp/theme_next/pivot1/out/pv_39b_hifi_cap40_tight_
// clock.png (the main window) and pv_41_player_tight_clock_cap40.png (the
// render player), the tablet at 300 %; their generators
// tmp/theme_next/make_pivot1.py (brushed, ring_tones), make_pivot3.py and
// make_pivot4.py are the numbers' source. Where a number here and the frame
// part, the frame is the law. This header is the one owner of the grounds,
// the bake and its rules, the regions, the ring tones, the chrome's inks, the
// key's constants, the accent and the LED's glow. (The fortepiano chrome of
// 2026-10-21 to 2026-10-23 — the walnut, the brass and the ebony, their
// Gemini masters and pre-rendered covers under assets/tiles/, the bevel keys
// — is git history.)
//
// THE GROUND IS PROCEDURAL (make_pivot1.brushed, ported): BRUSHED BLACK
// ALUMINIUM — uniform noise (numpy's legacy MT19937 at a fixed seed, 21,
// drawn row-major as numpy's RandomState.rand draws it, quantized to bytes),
// blurred along x alone by PIL's GaussianBlur((9, 0.4)) (three passes of its
// extended box blur, each rounded to bytes, x then y), normalized to zero mean
// and unit standard deviation, times kGroundStrength (.05), applied
// multiplicatively to the base kGroundBase (34, 34, 36) under the vertical
// gradient (1 − .12 y²) with y running −1 .. 1 over the WINDOW's rows; each
// channel rounded and clipped. THE MARKER LANE'S BRIGHTER BRUSHED (his
// "brighter marker lane, darker timestamp lane"): base kMarkerBase (48, 48,
// 50), seed 31, the same strength, NO vertical gradient, drawn at the lane's
// own rows of the seed's window-sized field (the stream entered at the
// lane's first row, so the strip is the mock's rows) and baked as a strip
// the lane's rows tall.
//
// THE LOOP (architect 2026-10-22, kept): THE LANE'S GROUND PANS WITH THE
// CUES. (1) THE STRIP IS MADE SEAMLESS AT THE BAKE by GIMP's tile-seamless
// method — out(x) = (1 − w(x))·A(x) + w(x)·A((x + W/2) mod W), w(x) =
// |2x/W − 1|, per channel, rounded — so its two ends meet with no seam,
// whether the strip is the procedural one or a dropped image. (2) THE
// PHASE: the lane's ground stands at a horizontal phase that MOVES WITH THE
// CONTENT AT CONSTANT ZOOM AND HOLDS ACROSS A ZOOM: at every change of THE
// DISPLAYED BASIS — the plate's published span, the basis the cues are
// painted on — where spp is unchanged the phase advances by the content's
// travel in device px, (old vp − new vp) / spp, accumulated in a double, and
// where spp moved (a zoom) it stands (follow_displayed_basis, at the plate's
// two publish sites, waveform_cache.cpp; every publish damages the marker
// lane, so a moved phase is always repainted). THE PERIOD IS THE TILE'S
// WIDTH, the window's. (3) THE BLIT stays the bake rule's 1:1 nearest copy at
// identity, EXTEND_NONE: the lane's tile is baked TWICE ITS WIDTH, so the
// phased copy is one read out of the doubled surface, and the phase enters at
// set_source's Marker Face — the lane's ground and the cue labels' face boxes
// move together at nearbyint(phase) mod W.
//
// THE IMAGE ROAD (his "we could also allow images to be slotted in as bg
// later"): A FOLDER `tiles/` BESIDE THE DEVICE CONFIG (device_config_path's
// folder — on the tablet under `adb shell run-as com.warptempo.gui`, on the
// laptop beside its config) with TWO NAMES, kGuiMaterialNames: `Ground.png`
// or `Ground.jpg` (the window's ground under THE COVER RULE: scaled by ONE
// factor s = max(W / w, H / h) to fit the client window W x H exactly and
// centered, the overhang cropped) and `Marker.png` or `Marker.jpg` (the
// lane's strip under THE STRIP RULE: scaled by ONE factor s = W / w to the
// window's width, its center rows the lane's rows; a picture too short for
// the lane at that factor is the bake's hard fail, never a stretch), THE PNG
// WINNING where both stand. A present file replaces the procedural source
// for that role at the bake; absent, the procedural ground; no key, no row,
// no card; every other name in the folder is ignored. Decoded by resvg's
// one road (svg_icon::raster_regions, the icons' Lanczos-3 area filter).
// A FILE OF EITHER NAME THAT IS NEITHER A PNG NOR A JPEG IS THE LAUNCH'S
// HARD FAIL naming the file (materials::check, which reads its first 64 KiB).
// (The pre-rendered covers `<Name>-<W>x<H>.png` and the bundled
// assets/tiles/ are no longer read: with no master to decode at launch
// their reason is gone.)
//
// THE BAKE RULE (architect 2026-10-21: "absolutely has to be"; the cost
// table, cairo into a 2304 x 1440 surface: a solid fill 0.016 ms, a 1:1
// blit of a window-sized bake at identity with EXTEND_NONE and
// FILTER_NEAREST 0.047 ms, an EXTEND_REFLECT pattern 1.73 ms): BAKE ONCE PER
// WINDOW GEOMETRY — its key the client size and the gui_scale (which move
// the lanes and the key seats; the strip pad a constant since 2026-10-23,
// program_spec.h), True Colors (the bakes hold the
// window's bytes) and the LED glow (led_glow below) — never at a palette
// install, a project open or any other Settings OK. A frame compares the
// key alone; the layout is read at the bake. The bake itself takes tens of
// ms (the blur over the window), once. THE FIRST BAKE IS IMMEDIATE, at the
// first frame's PRE-PAINT HOOK (materials::ensure_settled, main.cpp); a
// gui_scale commit, a led_glow commit and True Colors
// bake at once, one act one bake (materials::ensure). EVERY BAKE REINSTALLS
// THE PALETTE off the new ground's mean, so the next pre-paint widens that
// frame's damage to the whole client and rebuilds the flag cache from the
// new tiles (take_bake_landed). A CHANGE OF SIZE OF A BAKED WINDOW WAITS ON
// THE SETTLE GATE (the laptop's frame drag, a configure per motion): no
// resize bakes (on_resize only stamps its arrival, materials::note_resize),
// and a paint that finds the size moved bakes nothing until kBakeSettle (150
// ms) has passed with no further size change — the tick then arms the bake
// and damages the client (materials::settle_due), and that frame's pre-paint
// hook bakes. Meanwhile the painters draw from the LAST BAKE'S TILES where
// one still holds the fill, the material's flat mean where none does, and
// never a stale tile's pixel outside the new client. EVERY GROUND FILL IS A
// 1:1 COPY FROM A BAKE AT THE SAME RECTANGLE; every line, ring, key and word
// a solid fill. THE BAKED TILES:
//   GROUND  ONE TILE, THE BAND'S TOP TO THE WINDOW'S LAST ROW, window-wide
//           (the caption above it is one flat tone, render.h's caption
//           block): the band, the canvas column's margins, row 8, the
//           keyboard's band and every modal's controls row stand on it.
//   MARKER  ONE TILE, THE MARKER LANE'S ROWS, window-wide, its surface twice
//           the window's width (the LOOP).
//   LIT KEYS  TWO PATCHES PER KEY SIZE the band, row 8 and the render player
//           lay out (program_case_rects), ONE PER DOWN DEPTH — a lit key is
//           a key down, CHECKED ONE DEVICE PX and HELD TWO (architect
//           2026-10-23, the two depths; cool_edit_paint.h's paint_key): the
//           key as the key painter draws it at that depth with its LED lit
//           and THE GLOW composited round that depth's LED (led_glow below)
//           — so a lit key is one blit and nothing blends at paint time.
// RESIDENT at 300 % on 2304 x 1440 about 13.9 MB (the ground's tile 12.9,
// rows 40 .. 1440 window-wide; the lane's 0.94, 2304 x 51 doubled; the lit
// keys' patches about 38 KB, the one 69 x 69 size at its two depths); on
// the laptop's maximized 1920 x 1047 at 138 % about 8.2 MB (the lit keys'
// two 31 x 31 patches about 7.7 KB; the arithmetic off the geometry, 2026-10-23;
// materials::resident_bytes, which the trace line prints at every bake, is
// the measure).
//
// THE REGIONS — WHICH GROUND A SURFACE IS (GuiMaterialRegion, a scope the
// painter names): GROUND the brushed panel — the band and row 8 under and
// between their keys, the canvas column's side margins, the keyboard's
// band, the modal family's controls row; MARKER the marker lane's ground
// between the program ring's columns and the cue labels' face boxes on it;
// FLAT everything else — the caption, the ruler's flat ground, the view bar's
// field, the fields, the modal box, the cards and the tooltip, every key.
// UNTOUCHED: the well and everything the palette draws (the canvas, the view
// bar's span, the cues' triangles and dots, the playhead).
//
// THE TONE RULE: THE GROUND'S FACE IS ITS BAKED TILE'S MEAN COLOR
// (materials::face; about 1F1F21 on the shipped ground), and the chrome's
// ONLY RING, LINE AND EDGE TONES ARE THE TWO RING TONES OFF IT
// (make_pivot1.ring_tones; ring_light / ring_dark below): light = HLS with
// L + .16 and S × .9, dark = L × .40 — about 48484B and 0C0C0D (the app's
// old hilight rule, L' = .492 L + .5, lands at L .5+ on any dark ground, the
// "too bright" he saw). THE SURFACE CHOKEPOINT knows the Face by its bytes:
// under a Ground or Marker scope a fill in the Face is that ground's tile
// under the fill's own extents, any other color flat (set_source). NO
// PAINT-TIME ALPHA.
//
// THE INKS (constants all; 0xRRGGBB, sRGB): THE BONE kBoneInk F0F0F0 on
// every word and digit the chrome prints at the body size — the caption's
// title, the flag labels, the clocks' digits, the tab name, the state line,
// the lists' rows, the cards, the prompts, the editors' text, the tooltip
// (the bone since 2026-10-23 night, like the flags, paint_shift_tooltip) —
// and on the keyboard's caps in their keys face (34 px at 300 %, 2026-10-23
// night) and the ruler's digits and ticks; THE LEGEND INK kLegendInk
// C4C4C8, the keys' printed legends ALONE; THE DIM INK kDimInk
// 6A6A6E, a disabled key's legend alone (a dead caption button wears the
// bone since 2026-10-23, render.h's caption block). NO WORD
// WEARS A SHADOW but the ruler's digits and ticks (one device px of black at
// (+1, +1), render.h's ruler block): the frame of record draws none.

enum class GuiMaterial { Ground, Marker };
inline constexpr int kGuiMaterialCount = 2;
// THE IMAGE ROAD'S NAMES (the head), the files' stems, in the enum's order.
inline constexpr const char* kGuiMaterialNames[kGuiMaterialCount] = {
    "Ground", "Marker"};

// THE REGION A SURFACE STANDS IN (the head's map), the current one a scope's
// (GuiMaterialScope). Flat the default.
enum class GuiMaterialRegion { Flat, Ground, Marker };

// THE PROCEDURAL GROUNDS (the head; make_pivot1.brushed's arguments).
inline constexpr uint32_t kGroundBase     = 0x222224;
inline constexpr uint32_t kMarkerBase     = 0x303032;
inline constexpr uint32_t kGroundSeed     = 21;
inline constexpr uint32_t kMarkerSeed     = 31;
inline constexpr double   kGroundStrength = 0.05;
inline constexpr double   kGroundVGrad    = 0.12;
// PIL's GaussianBlur((9, 0.4)): the streaks' sigma along x and down y.
inline constexpr double   kBrushSigmaX    = 9.0;
inline constexpr double   kBrushSigmaY    = 0.4;

// THE INKS (the head).
inline constexpr uint32_t kBoneInk   = 0xF0F0F0;
inline constexpr uint32_t kLegendInk = 0xC4C4C8;
inline constexpr uint32_t kDimInk    = 0x6A6A6E;
// THE FLAT GROUNDS (architect 2026-10-23): the ruler's flat dark lane, the
// field (every field, the modal family's box, the cards and the tooltip),
// the time fields' and the view bar's black.
inline constexpr uint32_t kRulerGround     = 0x0E0E10;
inline constexpr uint32_t kFieldGround     = 0x0A0A0C;
inline constexpr uint32_t kTimeFieldGround = 0x000000;
// THE SELECTION PAIR (architect 2026-10-23: "the grey everywhere"; the brass
// pair retired): a flat grey bar under the bone — the lists' lit row, the
// flag editor's and the dialog editors' selected substring.
inline constexpr uint32_t kSelectionFill = 0x3E4044;
inline constexpr uint32_t kSelectionText = kBoneInk;

// THE KEY (architect 2026-10-23; make_pivot1.push_buttons with blank=True,
// make_pivot4.key): a flat face, a one-device-px black outline, a light top
// edge two rows deep (the light row, then the mean of it and the face), a
// two-row foot and a one-column right edge in the dark — the key stands
// proud — and the LED: unlit a dead lamp, lit THE ACCENT (accent_color).
// DOWN — checked one device px, held two (the two depths, 2026-10-23) —
// the face darker, the edges gone, a wall of the foot tone the depth's
// device px inside the outline's top row and left column and the LED the
// depth's device px right and down (cool_edit_paint.h's paint_key).
inline constexpr uint32_t kKeyFace        = 0x3A3A3C;
inline constexpr uint32_t kKeyFacePressed = 0x2C2C2E;
inline constexpr uint32_t kKeyTopLight    = 0x767678;
inline constexpr uint32_t kKeyFoot        = 0x0E0E10;
inline constexpr uint32_t kKeyOutline     = 0x000000;
inline constexpr uint32_t kLedUnlit       = 0x232325;
// THE LED'S LIT TONE OF RECORD (the mock's 46DC6E): its HLS saturation and
// lightness are the accent's, the hue the waveform ink's (accent_of_ink).
inline constexpr uint32_t kLedLitRecord   = 0x46DC6E;

// THE RING TONES OFF A GROUND'S MEAN (the head's TONE RULE): light L + .16
// with S × .9, dark L × .40, the hue kept.
constexpr uint32_t ring_light_of(uint32_t mean) {
    return cool_edit_derive::tone(mean, 1.0, 0.16, 0.9, 0.0);
}
constexpr uint32_t ring_dark_of(uint32_t mean) {
    return cool_edit_derive::tone(mean, 0.40, 0.0);
}
// THE MEAN BEFORE THE FIRST BAKE (construction state, never on screen
// long): the base under the gradient's mean, 1 − .12 / 3.
inline constexpr uint32_t kGroundMeanEstimate = 0x212123;

// THE ACCENT OF AN INK (architect 2026-10-23, "agree to all"): the ink's
// HUE under the LED's fixed HLS saturation and lightness (kLedLitRecord's),
// so Cool Edit's Default green lands near 46DC6E and a blue preset gives an
// equally bright blue lamp. Worn by the lit LEDs alone; no device key.
inline constexpr cool_edit_derive::Hls kLedLitHls =
    cool_edit_derive::hls_of(kLedLitRecord);
constexpr uint32_t accent_of_ink(uint32_t ink) {
    return cool_edit_derive::tone(ink, 0.0, kLedLitHls.l, 0.0, kLedLitHls.s);
}
namespace materials {

// THE LAUNCH'S CHECK (the head's IMAGE ROAD): every file of the `tiles/`
// folder beside the device config named for a role (Ground or Marker, .png
// or .jpg) is a PNG or a JPEG. Answers the first error, prefixed `tiles: `
// and naming the file — gui_main's hard fail. An absent folder answers
// nothing.
std::optional<std::string> check();

// THE BAKE, AT ONCE (the head's BAKE RULE): when the key `app` gives
// differs from the baked one, every ground is baked again and the palette
// reinstalled off the new mean (install_palette, render.h — its generation
// rebuilds every cached surface the tiles were painted into) — the client
// repainted by the caller's damage, the whole surface by the next pre-paint
// (take_bake_landed). A zero-sized client bakes nothing. A FAILURE is an
// image of the road's that will not decode (check passed its header): its
// stderr line and the hard fail. Callers: the one-act roads,
// GuiInputHandler::apply_gui_scale, apply_led_glow and apply_true_colors.
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
// THE GROUND'S FACE, its baked tile's mean, 0xRRGGBB sRGB
// (kGroundMeanEstimate before the first bake).
uint32_t face(GuiMaterial m);
// THE TWO RING TONES OFF THE GROUND'S FACE (the head's TONE RULE), sRGB.
uint32_t ring_light();
uint32_t ring_dark();

// THE ACCENT (accent_of_ink), set at every palette install from the live
// waveform preset's ink (render.cpp's fill_program_palette): the lit keys'
// patches are rebuilt with it (their glow carries it), so a waveform pick
// relights every lamp at once. accent_color() is its one reader's road.
void     set_accent(uint32_t accent);
uint32_t accent_color();
// THE LED GLOW (architect 2026-10-23, "agree to all"; the `led_glow` device
// key, device_config.h): a soft halo BAKED INTO THE LIT KEY'S PATCH — a
// falloff from the LED's rect, the accent at alpha .35 at the rect's edge
// fading to 0 at `px` device px out, composited over the key's own pixels
// inside its outline at the bake, never at paint; 0 none. Pushed at launch
// and at the settings editor's OK, which then bakes (the key's term).
void set_led_glow(int px);

// THE MARKER LANE'S PHASE (the brass's 2026-10-22 to 2026-10-23) FOLLOWS THE
// DISPLAYED BASIS (the head's LOOP): called at every publish of the plate's
// displayed fingerprint (the two sites in waveform_cache.cpp, the async swap
// and the synchronous rebuild) with its span and width — the phase advanced
// by the content's travel in device px, (old vp − new vp) / spp, where spp
// is unchanged, and held where it moved.
void follow_displayed_basis(int64_t vp_start, int64_t vp_end, int area_w);

// THE SURFACE CHOKEPOINT'S ROAD (render.h's set_surface_source), called with
// the fill's PATH ALREADY BUILT: `c` THE FACE (the palette's `face`, the
// ground's mean) under a Ground or Marker scope is that ground's baked tile
// under the path's extents where they meet the client (the tile holding the
// visible part; the mean where no one tile holds that) — set as the source,
// true; false for any other color, the Flat scope or before a bake, the
// caller then setting it flat.
bool set_source(cairo_t* cr, GuiColor c);
// ONE RECT OF A GROUND AS DRAWN, split over its tiles (client coordinates,
// the client's pixels alone): each part the tile, a part no tile holds the
// ground's flat mean.
void paint_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m);

// A LIT KEY'S BAKED PATCH (the head's LIT KEYS): the patch for a key of
// `r`'s size at `depth` device px down (1 checked, 2 held; key_depth),
// blitted 1:1 at `r` — true; false where none is baked (a key size the bake
// did not see, a depth it does not bake, or a size change waiting on the
// gate), the caller then painting the key at that depth with its plain lit
// LED.
bool paint_lit_key(cairo_t* cr, const GuiRect& r, int depth);

// THE DISABLED GLYPH'S TONES (icons::draw_disabled, which no band case calls
// since 2026-10-23: the keys print no glyph): Cool Edit's measured emboss,
// constants. 0xRRGGBB.
struct DisabledGlyphTones {
    uint32_t shadow, light, inset;
    bool operator==(const DisabledGlyphTones&) const = default;
};
DisabledGlyphTones disabled_glyph_tones();

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
