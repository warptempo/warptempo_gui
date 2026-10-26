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
// THE NUMBERS HERE ARE THE RECORD (they were fitted to the approved frames of
// the main window and the render player, the tablet at 300 %). This header is the one owner of the grounds,
// the bake and its rules, the regions, the ring tones, the chrome's inks, the
// key's constants, the accent and the LED's glow. (The fortepiano chrome of
// 2026-10-21 to 2026-10-23 — the walnut, the brass and the ebony, their
// Gemini masters and pre-rendered covers under assets/tiles/, the bevel keys
// — is git history.)
//
// THE GROUND IS PROCEDURAL: BRUSHED BLACK
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
// one road (svg_icon::raster_regions, its Lanczos-3 area filter).
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
//           (the whole surface since the app's caption above it went,
//           2026-10-24, render.h's caption note): the band, the canvas column's margins, row 8, the
//           keyboard's band and every modal's controls row stand on it.
//   MARKER  ONE TILE, THE MARKER LANE'S ROWS, window-wide, its surface twice
//           the window's width (the LOOP).
//   KEYS    THE KEY PATCHES: ONE PER KEY SIZE, PER DEPTH, PER LAMP — every
//           size the geometry lays out (paint_handler.h's program_key_seats
//           — the band's and row 8's in both states of the `h` view, the
//           controls row's square key (the render player's nine and every
//           word button), the scrub's thumb and, on a platform with the
//           on-screen keyboard, every key of its three pages): a lamp key's
//           RESTING patch (depth 0, the LED dead), its CHECKED patch (one
//           device px down, lit) and its HELD patch (two, lit — architect
//           2026-10-23, the two depths; cool_edit_paint.h's paint_key), the
//           lit two with THE GLOW composited round that depth's LED
//           (led_glow below); a lampless key (the keyboard's, the thumb) the
//           depths it can stand at, no LED. Each patch is the key as the
//           key painter draws it at that depth, its face flat (THE KEY
//           below) — two keys of one size at one depth are the same bytes,
//           so a size is one patch whichever key stands there — and a key
//           is one blit at every depth, no blend computed at paint time. An
//           accent change rebuilds the lit patches alone (set_accent); the
//           rest stand.
// RESIDENT at 300 % on 2304 x 1440, 15 396 504 bytes (the arithmetic off
// the geometry, 2026-10-25): the ground's tile 13.3 MB (rows 0 .. 1440
// window-wide, the band's top the surface's first row since 2026-10-24);
// the lane's 0.94 MB, 2304 x 51 doubled; THE KEY PATCHES 1.19 MB, 13 of
// them — the one 69 x 69 lamp key (the band, row 8 and the controls row)
// at its three depths, the thumb's one 24 x 48, and the on-screen
// keyboard's four sizes, 219 / 220 / 676 x 87 at depths 0 and 2 and the
// 333 x 87 of Shift, Backspace, Enter and the symbol-mode key at 1 too
// (nine patches, 1.12 MB). On the laptop's maximized 1920 x 1047 at 200 %,
// 8 590 640 bytes (no keyboard; the lamp key's three 46 x 46 patches and
// the thumb's 16 x 32). materials::resident_bytes, which the trace line
// prints at every bake with the patches' count and time, is the measure.
//
// THE REGIONS — WHICH GROUND A SURFACE IS (GuiMaterialRegion, a scope the
// painter names): GROUND the brushed panel — the band and row 8 under and
// between their keys, the canvas column's side margins, the keyboard's
// band, the modal family's controls row; MARKER the marker lane's ground
// between the program ring's columns and the cue labels' face boxes on it;
// FLAT everything else — the ruler's flat ground, the view bar's
// field, the fields, the modal box, the cards and the tooltip, every key
// (a solid fill on every pixel, baked into its patch).
// UNTOUCHED: the well and everything the palette draws (the canvas, the
// cues' triangles and dots, the playhead) and the view bar's span (a flat
// fill of THE SPAN's own grey trio, below).
//
// THE TONE RULE: THE GROUND'S FACE IS ITS BAKED TILE'S MEAN COLOR
// (materials::face; about 1F1F21 on the shipped ground), and the chrome's
// ONLY RING, LINE AND EDGE TONES ARE THE TWO RING TONES OFF IT
// (ring_light / ring_dark below): light = HLS with
// L + .16 and S × .9, dark = L × .40 — about 48484B and 0C0C0D (the app's
// old hilight rule, L' = .492 L + .5, lands at L .5+ on any dark ground, the
// "too bright" he saw). THE SURFACE CHOKEPOINT knows the Face by its bytes:
// under a Ground or Marker scope a fill in the Face is that ground's tile
// under the fill's own extents, any other color flat (set_source). NO
// PAINT-TIME ALPHA.
//
// THE INKS (constants all; 0xRRGGBB, sRGB): THE BONE kBoneInk F0F0F0 on
// every word and digit the chrome prints at the body size — the logo
// lane's brand and legend, the flag labels, the clocks' digits, the tab name, the state line,
// the lists' rows, the cards, the prompts, the editors' text, the tooltip
// (the bone since 2026-10-23 night, like the flags, paint_shift_tooltip) —
// and on the keyboard's caps in their keys face (24 px at 300 %, 2026-10-24)
// and the ruler's digits and ticks; THE LEGEND INK kLegendInk
// C4C4C8, the keys' printed legends ALONE; THE DIM INK kDimInk
// 6A6A6E, a disabled key's legend alone. NO WORD
// WEARS A SHADOW but the cue labels and row 8's tab name and state line,
// over a shadow in the cue triangle's own shadow color, ONE LABEL QUANTUM
// RIGHT AND ONE DOWN, 3 and 3 device px at 300 % and scaled, the
// triangle's own offset since 2026-10-26 ~13:05 (architect 2026-10-26
// ~10:50, "be GUI scaled"; ~11:30, "shadow on", "the same
// shadow for the tab/git/etc text"; render.h's cue_label_shadow_offset_px,
// show_cue_label and show_ce_label, render.h's marker lane's label
// paragraph): the skin draws none elsewhere, the clocks' digits and the
// ruler's digits included (2026-10-25). Of the lines,
// the ruler's ticks alone wear one, one device px of black at (+1, +1)
// (render.h's ruler block).

enum class GuiMaterial { Ground, Marker };
inline constexpr int kGuiMaterialCount = 2;
// THE IMAGE ROAD'S NAMES (the head), the files' stems, in the enum's order.
inline constexpr const char* kGuiMaterialNames[kGuiMaterialCount] = {
    "Ground", "Marker"};

// THE REGION A SURFACE STANDS IN (the head's map), the current one a scope's
// (GuiMaterialScope). Flat the default.
enum class GuiMaterialRegion { Flat, Ground, Marker };

// THE PROCEDURAL GROUNDS (the head; the brushed ground's arguments).
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

// THE KEY (architect 2026-10-23): a flat face, a one-device-px black outline, a light top
// edge two rows deep (the light row, then the mean of it and the face), a
// two-row foot and a one-column right edge in the dark — the key stands
// proud — and the LED: unlit a dead lamp, lit THE ACCENT (accent_color).
// DOWN — checked one device px, held two (the two depths, 2026-10-23) —
// the face darker, the edges gone, a wall of the foot tone the depth's
// device px inside the outline's top row and left column and the LED the
// depth's device px right and down (cool_edit_paint.h's paint_key).
// THE FACE IS FLAT, 3A3A3C at rest and 2C2C2E down, with no grain and no
// gradient of any kind (architect 2026-10-25, "none": a crumpled-paper
// mottle on the face lived 2026-10-24 to 2026-10-25 and came off on the
// glass — with the eye already expecting a modeled key the grain read too
// strong, and none was his pick off a 0 .. 1.5 % series; the per-key
// patches it needed cost the on-screen keyboard 18.7 MB resident, his
// second reason).
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
// THE VIEW BAR'S SPAN (architect 2026-10-25, on the evening's mocks: "the
// trim bar should be a neutral gray", "sort of like the button color, maybe
// a little different so it doesn't blend in"): a raised block of a NEUTRAL
// GREY, its body kSpanFace — THE KEY'S FACE LIFTED (kKeyFace 3A3A3C to
// 505053) so it does not blend in — its top row and left column kSpanLight
// and its bottom row and right column kSpanDark, THE TONE RULE's light and
// dark off the body (ring_light_of / ring_dark_of above). His reason: the
// span's light bevel was the one place a LIGHT version of the waveform's
// ink showed; the ink lives on the waveform (and the LEDs' hue) alone. No
// preset touches the trio (render_trim_flags paints it, render.h's view
// bar block).
inline constexpr uint32_t kSpanFace  = 0x505053;
inline constexpr uint32_t kSpanLight = ring_light_of(kSpanFace);
inline constexpr uint32_t kSpanDark  = ring_dark_of(kSpanFace);
static_assert(kSpanLight == 0x78787C && kSpanDark == 0x202021);
// THE MEAN BEFORE THE FIRST BAKE (construction state, never on screen
// long): the base under the gradient's mean, 1 − .12 / 3.
inline constexpr uint32_t kGroundMeanEstimate = 0x212123;

// THE ACCENT OF AN INK (architect 2026-10-23, "agree to all"): the ink's
// HUE under the LED's fixed HLS saturation and lightness (kLedLitRecord's),
// so the default's Terminal green 49CA8A lands at 46DC92, and the inks
// 6FD98F, 6ED86E and 6ED8B5 at 46DC73, 46DC46 and 46DCAA, each an equally
// bright lamp of its ink's hue. Worn by the lit LEDs alone; no device key.
inline constexpr cool_edit_derive::Hls kLedLitHls =
    cool_edit_derive::hls_of(kLedLitRecord);
constexpr uint32_t accent_of_ink(uint32_t ink) {
    return cool_edit_derive::tone(ink, 0.0, kLedLitHls.l, 0.0, kLedLitHls.s);
}
// ONE KEY SIZE THE BAKE MAKES PATCHES FOR (the head's KEYS;
// program_key_seats, paint_handler.h): its size, whether it carries a lamp
// (its LED then dead at depth 0 and lit at 1 and 2, cool_edit_paint.h's
// paint_key), and the depths it can stand at, bit d for depth d.
struct GuiKeySeat {
    int      w = 0, h = 0;
    bool     lamp = true;
    uint8_t  depths = 0b111;
};

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
// waveform preset's ink (render.cpp's fill_program_palette): where it moved,
// the lit keys' patches — a lamp key's checked and held, whose LED and glow
// carry it — are rebuilt, the resting and the lampless patches standing, so
// a waveform pick relights every lamp at once. accent_color() is its one reader's road.
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

// A KEY'S BAKED PATCH (the head's KEYS): the patch for a key of `r`'s size
// at `depth` device px down (0 at rest, 1 checked, 2 held; key_depth),
// `lamp` whether it carries an LED (dead at 0, lit at 1 and 2), blitted 1:1
// at `r` — true; false where none is baked (before the first bake, a size
// the bake did not see — a keyboard key while a resize waits on the gate —
// or a depth it does not bake), the caller then painting the key itself
// (cool_edit_paint.h's paint_key_at_depth).
bool paint_key_patch(cairo_t* cr, const GuiRect& r, int depth, bool lamp);

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
