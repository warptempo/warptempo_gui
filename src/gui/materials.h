#pragma once

#include "render.h"   // GuiRect, GuiColor, the lane accessors

#include <cairo/cairo.h>
#include <cstdint>
#include <optional>
#include <string>

// THE MATERIALS — THE WOOD CHROME (architect 2026-10-20, "this mockup is
// effectively what I was going for"; the mock of record for the structure
// tmp/wood_mock/bevel/B_rule_rgb.png, its generator retex.py's rule_map_rgb
// the formula's model): the chrome's FLAT TONES BECOME MATERIALS. Every lane,
// case, gripper, end bar, tab, ruler and line keeps its geometry exactly;
// only what fills it changes. This header is the one owner of the tiles, the
// regions, the tone rule on a material and the wood chrome's constants.
//
// THE TILES ARE A DROP FOLDER (his "get the code right, then decide which
// tile"): `assets/tiles/`, ONE FILE PER MATERIAL NAMED BY ITS ROLE —
// kGuiMaterialNames, `Wood`, `WoodDark`, `MetalDark`, `MetalVeryDark`,
// `Case` — each `.png` or `.jpg`, THE PNG WINNING where both stand (the
// icons' rule, icons.h), Gemini-drawn 4K masters for the architect
// (assets/tiles/README.md). Read from the bundle as the icons are
// (GuiPlatform::bundled_tile_files: the laptop's compiled-in folder, the
// APK's `tiles/` assets), decoded by the icons' resvg road and scaled by
// their Lanczos-3 area filter (svg_icon.h's tiles' road). A MISSING TILE OR
// ONE THAT IS NOT A PNG OR A JPEG IS THE LAUNCH'S HARD FAIL naming the file
// (materials::load, gui_main's one stderr line and no window).
//
// THE SCALE (ruled 2026-10-20 ~18:55, retold ~23:35): each master is drawn at
// gui_scale / 300 x tile_scale / 100 — the `tile_scale` key (percent, [10,
// 100], default 50; a tuning key with a Settings row, live at the OK) — so
// the grain reads the same per Windows px on every device; never a per-device
// tile. THE TILES ARE DOWNSCALED LIKE THE ICONS (architect 2026-10-20 ~23:35:
// "the icons are still downscaled at 300%, which makes them look sharp. the
// wood and metal look upscaled … we should downscale the wood/metal a similar
// amount, so that at 300% they are still downscaled"): the first drawing, at
// gui_scale / 300 alone, took the tablet's 300 % 1:1, and the placeholder
// masters carry about half their pixel count in real detail (a 2x Lanczos
// round trip changes them by under 1 % RMSE), so they read soft; at 50 % the
// same detail is drawn on half the pixels and the grain halves in size. A
// strip narrower than the window mirror-repeats, as an outgrown window
// always has. ONCE AT LAUNCH
// (and again at a gui_scale commit, which re-reads the files): ONLY THE
// REGION KEPT IS EVER RENDERED, and the masters never stay resident.
//   WOOD and WOOD_DARK: A STRIP the scaled master's whole width by the upper
//     region's height (the caption, the menu row, the program frame's top
//     row and the band), cropped from the master's vertical centre, NEVER
//     REPEATED: the window's columns take the strip's CENTRED columns, and a
//     window wider than the strip mirror-repeats it.
//   THE METALS AND THE CASE: A CROP from the scaled master's centre,
//     kMetalCrop / kCaseCrop device px at most, MIRROR-TILED (flipped at
//     every repeat; the brushing is horizontal, so no seam shows), anchored
//     at the client's corner.
// RESIDENT, per material: the scaled crop in sRGB (the base) and its
// as-drawn copy where the window converts (the report's arithmetic at
// kMetalCrop's comment).
//
// THE REGIONS: WHICH MATERIAL A SURFACE IS. WOOD — the caption (its gradient
// gone: the caption IS the wood, active and inactive alike), the menu row and
// the toolbar band with its panes, grooves and edge columns; WOOD_DARK — THE
// BAND'S VOID, its stretch where no pane stands (`ce_recess` there); CASE —
// every button case's interior (the band's, row 8's, the render player's)
// and THE CAPTION'S THREE BUTTONS (ruled ~20:15); METAL_VERY_DARK — EVERY
// RECESS BELOW THE BAND: the tab strip's ground behind the tabs (`ce_recess`
// there), the column's air, the ruler's and the marker lane's ground (the
// cue labels' face boxes with it), the column's foot; METAL_DARK —
// EVERYTHING ELSE THAT IS CHROME: the tabs (Cool Edit's measured tab bodies
// retired, the body the metal like every tone), the column's margins, the
// dock bar, row 8, the border under it, the program frame's line from the
// band's foot down (THE FRAME LINE TAKES THE MATERIAL OF THE REGION IT STANDS
// BESIDE, architect 2026-10-20 ~23:10: its top row and its columns alongside
// the menu row and the band the WOOD's flat tones, paint_program_frame), the
// pull-downs, cards and tooltip, the modal box and its controls row, the
// render player, the on-screen keyboard, the dialog fields, the laptop's
// sizing frame. UNTOUCHED: the well and everything the palette draws (the
// canvas, the view bar's black field and span, the cues, the playhead, the
// ruler's E0E0E0 ink), every glyph, every word but the caption's and the
// menu row's, and the cases' black outline and constant rings.
//
// THE TONE RULE (architect 2026-10-20 ~20:15: "take the Windows formula for a
// color and the bevel line … calculate whatever luminance would be needed to
// give the actual Windows bevel rule"): EACH MATERIAL'S FACE IS ITS BAKED
// TILE'S MEAN COLOR (materials::face, measured at the load), and EVERY TONE A
// PAINTER PUTS DOWN ON A MATERIAL IS COOL EDIT'S TONE RULE (cool_edit_derive.h:
// kTones' HLS L' = a·L + b, the hue and saturation kept; the case's own
// measured ramp, kCaseRamp and its lines) APPLIED TO THAT MATERIAL'S FACE, and
// a FLAT STROKE (architect 2026-10-20 ~22:50, "yes yes" off two whole-window
// mocks, after the wood's ~22:30: the grain rides no line on any material):
// the rule's tone itself, one opaque solid source (the material's Face mean
// scaled per channel by tone / Face, the factors the rule's, so a red wood's
// highlight does not clip to orange). The material's own Face tone is the
// tile AS DRAWN; the wood's tones derive from the Wood Face, the void is
// WoodDark as drawn, the metals' tones from the MetalDark Face, a recess
// MetalVeryDark as drawn — EXCEPT THE CASE (2026-10-20 ~23:10, "yes"):
// Cool Edit's measured case on the DEFAULT Face, every tone scaled per
// channel so the rest case's interior mean is the Case tile's mean (its RAMP
// the tile always, only its lines flat; paint_case_face below, the
// arithmetic at materials.cpp's derive_case_tones). The only copy ever baked is a
// tile as drawn; the painters' colors reach the strokes through
// set_surface_source (render.h), which knows each flat tone by its bytes (the
// tone table, set_tone_face) — so the painters keep their palette members and
// only the chokepoint swaps a solid source for the flat stroke. NO
// PAINT-TIME ALPHA, nothing composited: an antialiased edge stays the
// renderer's (a mitre's diagonal is the two flat sources). THE CHROME'S
// DERIVED ROLES (chrome_derive.h) derive from the METAL Face by the existing
// rules (install_palette), so the pull-downs, cards and dialogs read as
// metal, the text's swaps following that Face.
//
// THE BEVELS ARE TUNABLE (architect 2026-10-20 ~20:30, "make it a tuneable
// config temporarily … for all tuneable fields"): the device keys
// `bevel_light` and `bevel_dark` (percent, 100 the rule exactly;
// device_config.h), each with a Settings row, applied live at OK — every
// LIGHTER-than-Face tone's per-channel factor f becomes 1 + (f − 1)·p / 100
// with p the light key, every DARKER one the same with the dark key, on every
// material (set_bevel).

enum class GuiMaterial { Wood, WoodDark, MetalDark, MetalVeryDark, Case };
inline constexpr int kGuiMaterialCount = 5;
// THE DROP NAMES, the tiles' file stems, in the enum's order.
inline constexpr const char* kGuiMaterialNames[kGuiMaterialCount] = {
    "Wood", "WoodDark", "MetalDark", "MetalVeryDark", "Case"};

// THE REGION A SURFACE STANDS IN (the head's map), the current one a scope's
// (GuiMaterialScope): which material a flat tone resolves to.
//   Upper  the Face the Wood as drawn, `recess` WoodDark as drawn, every other
//          tone the Wood's rule as a flat stroke —
//          the caption, the menu row, the band, and the program frame's line
//          beside them (2026-10-20 ~23:10);
//   Lower  the Face MetalDark as drawn, `recess` MetalVeryDark as drawn,
//          every other tone MetalDark's rule as a flat stroke — THE DEFAULT,
//          every chrome surface no scope names;
//   Case   the Face the Case as drawn, every other tone THE CASE'S OWN LINE
//          as a flat stroke — a lighter-than-Face tone the case's highlight,
//          a darker one its shadow's first tone (2026-10-20 ~23:10) — the
//          caption's three buttons (the cases themselves are
//          paint_case_face's).
// A recess that is no `recess` tone — the column's air and foot, the ruler's
// and the marker lane's ground — is painted as MetalVeryDark by name
// (paint_material_rect) at its painter.
enum class GuiMaterialRegion { Upper, Lower, Case };

// THE WOOD CHROME'S CONSTANTS (the mocks', 2026-10-20; 0xRRGGBB, sRGB):
// THE CAPTION'S TITLE AND THE MENU ROW'S WORDS, their access-key underlines
// with them, BRUSHED GOLD over a ONE-DEVICE-PX SHADOW at (+1, +1) — a device
// px, not a Windows px: the shadow is the mock's, a hairline at every scale.
inline constexpr uint32_t kWoodTextInk    = 0xD9B45A;
inline constexpr uint32_t kWoodTextShadow = 0x3A2008;
// THE TIME FIELDS' GROUND AND DIGITS ("the background is black there, as
// Gemini had it"): row 8's clock and, the same painter's, the render
// player's two fields — black under amber, their ring the metal's
// field-dark / field-light tones and their geometry and pads unchanged.
inline constexpr uint32_t kTimeFieldGround = 0x000000;
inline constexpr uint32_t kTimeFieldDigits = 0xF0B030;

namespace materials {

// THE CROPS' LIMITS, device px (the head's SCALE paragraph): EVERY STRIP AND
// CROP AT MOST kTileCropW WIDE — the widest window either device shows (the
// tablet's 2304, the laptop's 1920-px screen) inside one period, so the
// woods' strip covers the window unrepeated and no metal mirror seam crosses
// it (a fill inside one period is a plain blit; a fill across a seam takes
// cairo's general road) — a metal's crop kMetalCropH tall (its vertical
// flips along the horizontal brushing), the case's kCaseCropW x kCaseCropH,
// room for every seat's own patch (paint_case_face). THESE ARE LIMITS: the
// crops read their sizes from the scaled master (materials.cpp's crop_of), and
// the case's per-seat patches (side <= 20 W) fit the crop or fold past it
// (paint_case_face's fold). The masters are 5488 x 3056, so at the tablet's
// 300 % and the default 50 % the scaled master is 2744 x 1528, still wider
// than kTileCropW, and RESIDENT, each ARGB32, nothing moved from the 1:1
// drawing: the wood strip 2560 x 216 (2.2 MB) and the void's, a metal crop
// 2560 x 384 (3.9 MB), the case 1024 x 512 (2.1 MB) — about 14 MB of bases,
// 29 MB at most with every as-drawn copy (display_transform.h), the cases'
// squares ~17 KB a seat and face; on the laptop's 138 % and 50 % the scaled
// master is 1262 x 704, narrower than its 1920-px window: the woods' strips
// (1262 x 216, 1.1 MB each) mirror-repeat, a metal crop is 1262 x 384 (1.9
// MB), about 7 MB of bases (12 MB at 100 %, the 2524-px crops).
inline constexpr int kTileCropW  = 2560;
inline constexpr int kMetalCropH = 384;
inline constexpr int kCaseCropW  = 1024;
inline constexpr int kCaseCropH  = 512;

// LOAD EVERY TILE at the live gui_scale (render.h's gui_scale_percent) and the
// live upper region's height (the lane accessors), measuring each Face; drops
// every baked copy. Answers the first error, prefixed `tiles: ` and naming
// the file — the launch's hard fail. gui_main calls it before install_palette
// (which reads the metal's Face) and the scale's chokepoint calls it again
// (GuiInputHandler::apply_gui_scale).
std::optional<std::string> load();
// Whether a load has landed (construction state paints flat).
bool loaded();
// THE MATERIAL'S FACE, its baked tile's mean, 0xRRGGBB sRGB.
uint32_t face(GuiMaterial m);

// THE BEVEL KEYS (the head), in percent: every baked copy dropped, the
// next paint rebaking what it asks for.
void set_bevel(int light_percent, int dark_percent);
// THE TILE SCALE KEY (the head's SCALE paragraph), in percent [10, 100]: it
// takes effect at the next load().
void set_tile_scale(int percent);
// TRUE COLORS (display_transform.h): every baked copy holds window bytes, so
// a flip of the switch drops them all (install_true_colors' apply shape).
void drop_baked();

// THE TONE TABLE: the flat tones a painter hands the surface chokepoint,
// each Cool Edit's tone of the CHROME'S Face (the palette's `face`, the
// metal's since 2026-10-20) — the Face itself and the panel tones the
// relief draws (kTones' Face tones but the text, the label and the cue's
// shadow, which are ink) — rebuilt by every palette install.
void set_tone_face(uint32_t chrome_face);

// THE CLIENT'S WIDTH, set by every paint (on_redraw): the wood strip's
// columns are centred on it.
void set_client_width(int w);

// THE SURFACE CHOKEPOINT'S ROAD (render.h's set_surface_source): `c` as a
// tone of the current region's material, its pattern set as the source;
// false where `c` is no tone (a constant, an ink) or nothing is loaded — the
// caller then sets it flat.
bool set_source(cairo_t* cr, GuiColor c);
// THE MATERIAL AS DRAWN as the source, anchored as its region anchors it.
void set_material_source(cairo_t* cr, GuiMaterial m);

// THE CASE (the head's CASE; cool_edit_paint.h's paint_ce_case): Cool
// Edit's measured case — the ramp, its highlight, its shadow ramp and
// corner, kCaseRamp and its lines — KEEPS THE TILE'S BRIGHTNESS (architect
// 2026-10-20 ~23:10, "yes"): derived on the DEFAULT Face as Default's own
// case, then every tone scaled per channel by one factor per channel, F_c =
// the Case tile's mean over the Default ramp's mean, so the rest case's
// interior mean (the ramp's rows, area-weighted) is the tile's mean; the
// pressed and checked ramp the same factors (materials.cpp's
// derive_case_tones, the arithmetic). The ramp the case tile scaled per row
// by its tone over the tile's mean, the lines flat strokes of the Face's
// mean (2026-10-20 ~22:50) under the bevel keys, baked once per (seat, face): REST
// the whole square inside the black line (`r`'s first w − lw rows and
// columns), DOWN the ramp alone inside the constant rings. PER-SEAT VARIATION
// (his "nice little touch", Cool Edit's own slightly different cases): each
// seat samples the case tile at a deterministic offset of its own, a hash of
// `seat` — stable across frames and launches — so no two cases wear the same
// patch. Paints that part of `r`; the black line and the rings are the
// caller's.
void paint_case_face(cairo_t* cr, const GuiRect& r, bool down, uint32_t seat);

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

// ONE RECT OF A MATERIAL AS DRAWN (a recess named by its painter, the
// caption's wood): aliased, the rect whole.
void paint_material_rect(cairo_t* cr, const GuiRect& r, GuiMaterial m);
