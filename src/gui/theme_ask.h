#pragma once

#include "gui_theme.h"   // GuiTheme, GuiThemeTones, the compiled row, the slug rule

#include <filesystem>
#include <string_view>
#include <vector>

// THE THEMES' LIST AND THE .ASK READER (architect 2026-11-05, the theme arc;
// 2026-11-06, session 75, THE DROPPED THEMES: this file the derivation's one
// owner). The app reads Ableton Live's theme files itself: `.ask`, Ableton's theme XML, a
// `<Theme>` holding one `<RoleName Value="#rrggbb" />` or `#rrggbbaa` per
// role. THE .ASK FILES NEVER ENTER THE REPOSITORY (Ableton's and the authors'
// files, THIRD_PARTY.md); the APK carries none; he drops them on the device.
//
// THE FOLDER IS `themes/` BESIDE THE DEVICE CONFIG (device_config_path()'s
// parent, the noise's composition), READ ONCE AT LAUNCH (load_gui_themes,
// gui_main, after the config and before the palette's first install): a sync
// while the app runs shows at the next launch, and the list is immutable for
// the process after the load.
//
// THE WALK. Each SUBFOLDER of `themes/` is A GROUP, its name lowercase in the
// slug's shape (is_theme_slug: `extra`, `sonic_bloom`), the value list's
// heading its name ALL CAPS (EXTRA, SONIC_BLOOM). Each regular `*.ask` file
// directly in a group folder is one theme, its slug THE SLUG RULE's
// (theme_slug_of_stem, gui_theme.h: "SB Red Greys.ask" -> `sb_red_greys`).
// Anything else in a group folder — a subfolder, a file not ending `.ask` —
// is ignored silently, and so is a file at the top of `themes/`.
//
// THE ORDER — the value list's. THE COMPILED GROUP FIRST, WARPTEMPO
// (kGuiCompiledThemeGroup): `default` (kGuiDefaultThemeTones, gui_theme.h —
// compiled, Default's .ask never read), then `immaterial_gold` when a dropped
// Immaterial.ask stands; then THE FOLDERS ALPHABETICALLY by name; within every
// group BY SLUG.
//
// IMMATERIAL_GOLD, A COMPILED RULE OVER A DROPPED FILE (architect 2026-11-05,
// session 56, his pick m1 of tmp/s56 — a separate theme, `immaterial`
// unchanged: "lowest overhead, most honest, and least churn if reversed";
// THE SHADE 2026-11-06, session 62, his pick x2 of tmp/s62): the first file
// of the walk whose slug is `immaterial` (the first group folder holding one)
// read through THE DERIVATION below with ViewCheckControlEnabledOn read from
// the same file's SearchIndication (ECCA6D, Immaterial's muted gold) SHADED
// IN OKLCH (Ottosson's matrices, `oklch` / `from_oklch` in theme_ask.cpp): its
// lightness set to L 78.8 — the lightness of the FF91C3 it replaces, so the
// accent sits over the ground at the ~55 L Live gives Default and Immaterial
// alike — its hue kept (89.7), its chroma scaled by 78.8 / L0, each channel
// clipped, encoded, rounded and clamped (`shade`): D6B762; every other tone
// Immaterial's, the lit press fill re-derived by the ratio. Listed only when
// that file is present and usable.
//
// A FILE IS LEFT OUT, WITH ONE stderr LINE naming its path and why, the
// launch going on: unreadable; a name that spells no slug (a stem of no
// letter or digit); a ROLE of kAskRoles missing (a Live 11 theme); the
// nested role missing; its slug already taken — the compiled group's two
// counting first (a dropped file slugging to `default` or `immaterial_gold`
// is left out), then earlier groups, then the same group's earlier file.
// A group folder whose name is not a slug, or is the compiled group's name,
// is left out with one line too. A MISSING OR UNLISTABLE `themes/` FOLDER,
// or a group folder that cannot be listed, is the ordinary state and silent:
// the list is the compiled group, `default` alone. Nothing more is checked
// (NO BACKSTOPS FOR ADVERSARIAL USE, CLAUDE.md): no XML validation, no size
// limit, no recovery.
//
// A CONFIG NAMING A SLUG NO ROW BEARS (a file removed on the laptop and
// mirrored away) OPENS IN DEFAULT with one stderr line, the config's line
// untouched until he picks another (gui_main; the reader judges the shape
// alone, device_config.h's THE theme GRAMMAR).
//
// THE ROLES READ (kAskRoles, theme_ask.cpp): a flat role is the FIRST
// `<Name Value="#rrggbb" />` (or 8 digits) of that name anywhere in the file,
// the hex case-blind; THE ONE NESTED ROLE, StandardVuMeter / Maximum, is the
// first `Maximum` inside the FIRST `<StandardVuMeter>` … `</StandardVuMeter>`
// block — a `Maximum` stands in every VU meter block (Overload, Disabled,
// Headphones, Sends …), so it is never read by the flat walk.
//
// THE DERIVATION — our Default tone = Live's role, each Default number the
// compiled row's (palette_file.h's static_asserts). A ROLE-NAMED role is
// read FLAT, its alpha byte dropped: each is 6-digit in every file he drops
// (audited 2026-11-06, session 81). A role found carrying alpha is
// COMPOSITED over the surface it paints on, never stripped (architect
// 2026-11-06, session 81, his "ok to implement"): the COMPOSITED block.
//
//   ROLE-NAMED (one Live role each):
//     ground                 242424 = SurfaceArea (ruling 2, 2026-11-05: the
//                                     arrangement's floor, his "#56" on Classic
//                                     Medium Light over the panels' DADADA)
//     lane, trim bar outside 2A2A2A = Desktop
//     card ink               2A2A2A = Desktop
//     key press face         2A2A2A = Desktop (the press arc, 2026-11-05: his
//                                     pick p2 over ControlBackground 1E1E1E —
//                                     "press in skeumorphic is darker")
//     key face, ruler face, pane, time field, magnify core
//                            363636 = SurfaceBackground (the core below)
//     cue outline, text on lit  111111 = ControlContrastFrame
//     text                   B5B5B5 = ControlForeground
//     dim text, well frame   757575 = TextDisabled (the scrub channel and the
//                                     tooltip border)
//     inactive cue           5D5D5D = BackgroundClip
//     lit fill AND cue       FFAD56 = ViewCheckControlEnabledOn
//                                     (immaterial_gold reads its shaded
//                                     SearchIndication instead, above)
//     playhead head          03C3D5 = RangeDefault
//     selection fill         637E86 = StandbySelectionBackground
//     selection text         070707 = StandbySelectionForeground (architect
//                                     2026-11-06, session 75: Live's own
//                                     partner of the bar; ControlContrastFrame
//                                     before it, our mispairing)
//     list press fill        B0DDEB = SelectionBackground (the press arc,
//                                     2026-11-05: "lighter for list
//                                     (highlight)")
//     list press text        070707 = SelectionForeground
//     card face              E76942 = Alert (the cards; THE LIMITER LAMP too,
//                                     architect 2026-11-05: "orange for limiter
//                                     and red for clipper" — a warning)
//     door lit fill          B595FC = ScaleAwareness (architect 2026-11-05, his
//                                     "yes" on mock s46_m2: the SEVEN OVERLAY
//                                     OPENERS' lit face, the "doors" class,
//                                     lavender — a lit door says a surface
//                                     stands, a lit lamp says a mode is on;
//                                     materials.h's THE KEY)
//     clip lamp              FF0A0A = StandardVuMeter / Maximum, the one NESTED
//                                     role: Live's own meter clip box — THE
//                                     CLIP LAMP's lit face (architect
//                                     2026-11-05, his d6 "yes": a fault).
//                                     FFAD56 REFUSED for a lamp: it is
//                                     ViewCheckControlEnabledOn, the lit KEY's
//                                     face, and "clip indicator is not a
//                                     button"
//     core                   363636 = SurfaceBackground, THE KEY FACE, on
//                                     every theme (architect 2026-11-07,
//                                     session 83, his pick i4 of tmp/s83 on
//                                     real MAGNIFY-lit captures, Default and
//                                     Immaterial judged: a lighter core from a
//                                     tone the palette already holds, no AA
//                                     halo) — the MAGNIFY-lit inner bar's solid
//                                     fill, one step under Default's 393939
//                                     canvas, so the center line (the ink, over
//                                     the plate) reads hard across it, ruled
//                                     fine (his "yes"). Before it
//                                     ControlBackground (1E1E1E on Default,
//                                     session 80, 2026-11-06; the record at
//                                     render.h's THE CORE)
//   COMPOSITED (Live's role over the surface it paints on, straight alpha
//   aa/255, a 6-digit word opaque and its bytes kept exactly; rounded half to
//   even, clamped):
//     ink                    191919 = WaveformColor over DetailViewBackground
//     canvas                 393939 = DetailGridTiles over DetailViewBackground
//     level audio fill       007383 = RangeEditField over SurfaceBackground
//                                     (Live's volume mini-slider, "green") —
//                                     the AUDIO level case's fill on the key
//                                     face (paint_level_case); 8-digit on SB
//                                     Viridian (F26000AF over 333333 = B65210)
//                                     and SB Red Greys (7B0000B3 over 3A3A3A =
//                                     681111), architect 2026-11-06, session 81
//     level noise fill       3C6AB6 = ChosenPreListen over SurfaceBackground
//                                     (Live's cue mini-slider, "blue") — the
//                                     NOISE level case's fill; 6-digit
//                                     everywhere, composited with AUDIO's for
//                                     symmetry (the two cases one road)
//     center                 = ink (a role is what it paints: the zero line IS
//                                   the ink, palette_file.h)
//     score pane             = ink (modal_box.h's score pane: "from ableton
//                                   palette directly")
//     grid                   303030 = TakeLaneTrackNotHighlighted over the
//                                     canvas (the quarter lines)
//     key dim ink            5D5D5D = ViewCheckControlDisabledOff over
//                                     SurfaceBackground (the key face; 8-digit
//                                     D3D3D340 on SB Cielo / Morea / Onyx) — a
//                                     disabled key's glyph and legend, the
//                                     struck clip lamp's square
//     trim span              696969 = ScrollbarInnerHandle over Desktop (the
//                                     trim bar's outside)
//     ruler tick             919191 = DetailViewRulerMarkings over
//                                     SurfaceBackground (the ruler face; Live's
//                                     light ticks)
//   Each of these four was a Default mix of two roles; the role nearest it in
//   hex on Default is the faithful reading, so every theme now reads Live's own
//   number (architect 2026-11-07, session 82). Default's key dim ink 5D5D5D
//   equals inactive cue's BackgroundClip — Live's own number, accepted. The four
//   are present in all 31 files he drops and in Default.
//   DERIVED (the app's own tones, no Live role near them; every mix a + t ·
//   (b − a) per channel, rounded half to even and clamped):
//     playhead line          85E2EB = FFFFFF + 0.483 · (head − FFFFFF)
//                                     (ruling 4: Default keeps his pick, every
//                                     other theme the same ratio, white toward
//                                     the head — not the mean)
//     lit press fill         C68743 = lit fill · (mean(press face) / mean(face))
//                                     per channel (42 / 54 = 0.778 on Default;
//                                     Immaterial FF91C3 · 0.737 = BC6B90) — a
//                                     checked key under a standing press
//                                     (architect 2026-11-05, the press arc's
//                                     second landing, his "ratio" on mocks d1 /
//                                     i1 of tmp/s45: "darken the lit key by the
//                                     relationship ... so the lit press is
//                                     derived"). THE RATIO, NOT THE DIFFERENCE
//                                     (d2 / i2, ruled out): the ratio keeps the
//                                     theme's lit hue and scales with its own
//                                     press step — Classic Medium Light's step
//                                     is 60 levels, the difference would nearly
//                                     black its FFB901, the ratio scales it by
//                                     86 / 146 = 0.589.
//     door lit press fill    8D74C4 = door lit fill · the same ratio per channel
//                                     (B595FC · 0.778 on Default) — a checked
//                                     door under a standing press, exactly as
//                                     the lamps' lit press fill is derived
//                                     (architect 2026-11-05, the doors)
//
// THE SCANNER IS NEITHER A ROLE NOR A DERIVED TONE: FFFFFF on every theme,
// CANONICAL — Live itself paints its scanner white on every theme tested,
// Default and the built-in themes included, so no theme's file carries it
// (architect 2026-11-07, session 82: "it comes from all live themes tested -
// including the default and builtin"; 2026-11-06: "a look online shows it's
// canonically full white", his Live 12 reference shot). It is never proposed
// as a role or a mix.
//
// Where a theme makes the lane, the trim's outside or the dim text coincide
// with the ground, that is Live's own number — accepted, not a bug, no
// backstop (architect 2026-11-05).
//
// THE SONIC BLOOM FILES' UNDEFINED ROLES: a pack file may carry a role it does
// not use as 000000 (ScrollbarInnerHandle black on 12 SB files,
// DetailViewRulerMarkings on 4). NO RULE CORRECTS IT: he edits the individual
// .ask file by hand (architect 2026-11-07, session 82: "i'd rather manually fix
// individual sb themes than make rules around their missing colors").
//
// A file whose SurfaceBackground is black (the press ratio's divisor zero) is
// left out with its line, and immaterial_gold is not listed over an
// Immaterial.ask whose SearchIndication is black (the chroma ratio's divisor
// zero): no stock or pack file is either, the arm only keeps the arithmetic
// defined.
//
// THE ARITHMETIC IS PYTHON'S, BIT FOR BIT (the derivation was first written
// in Python, and the proof of 2026-11-06 diffed every row on the tablet
// against its numbers): doubles in its operation order, Python's round() as
// nearbyint under the default rounding mode (half to even), a pow-based cube
// root, CPython's degrees / radians factors and its float modulo
// (theme_ask.cpp).

// Build THE LIST from `themes_dir` (the head's walk and order), once per
// process, before the palette's first install. An empty path is the absent
// folder (no config home).
void load_gui_themes(const std::filesystem::path& themes_dir);

// THE LIST in the value list's order, the compiled group first. Before the
// load it is `default` alone.
const std::vector<GuiTheme>& gui_themes();

// THE ROW WHOSE SLUG IS `slug`, or nullptr when no row bears it: the
// settings editor's `theme` commit refuses such a slug (only a typed line
// can name one), the launch opens in default (the head).
const GuiTheme* find_gui_theme(std::string_view slug);

// The compiled `default` row (the list's first).
const GuiTheme& default_gui_theme();
