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
// THE SCALE (ruled 2026-10-20 ~18:55): each master is drawn at gui_scale /
// 300 — the tablet's 300 % takes it 1:1, the laptop's 138 % downscales it —
// so the grain reads the same per Windows px on every device, and a denser
// device scales up from the master; never a per-device tile. ONCE AT LAUNCH
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
// dock bar, row 8, the border under it, the program frame's line, the
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
// MetalVeryDark as drawn, the case's from the Case Face (its RAMP, its face,
// the tile always; only its lines are flat). The only copy ever baked is a
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
//          the caption, the menu row, the band;
//   Lower  the Face MetalDark as drawn, `recess` MetalVeryDark as drawn,
//          every other tone MetalDark's rule as a flat stroke — THE DEFAULT,
//          every chrome surface no scope names;
//   Case   the Face the Case as drawn, every tone the Case's rule as a flat
//          stroke — the
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
// room for every seat's own patch (paint_case_face). RESIDENT AT 300 % (the
// tablet), each ARGB32: the wood strip 2560 x 216 (2.2 MB) and the void's, a
// metal crop 2560 x 384 (3.9 MB), the case 1024 x 512 (2.1 MB) — each base
// once, plus an as-drawn copy where the window converts (display_transform.h)
// and the cases' squares ~17 KB a seat and face; every line a flat stroke
// since 2026-10-20 ~22:50 (the wood's from ~22:30) and baked no more: about
// 14 MB of bases on the tablet, 29 MB at most with every as-drawn copy (68
// before), about 12 MB on the laptop's 138 % (52 before; no as-drawn copies,
// the strips 1 MB, a metal crop the scaled master's whole 2524-px width).
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
// corner, kCaseRamp and its lines — derived from the CASE tile's Face, the
// ramp the case tile scaled per channel, the lines flat strokes of the Face's
// mean (2026-10-20 ~22:50), baked once per (seat, face): REST
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
