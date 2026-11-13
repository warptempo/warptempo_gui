#pragma once

#include "gui_theme.h"   // GuiTheme, GuiThemeTones, the reference row, the slug rule

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// THE THEMES' LIST, THE THEME FILE AND THE DERIVATION (architect 2026-11-05,
// the theme arc; 2026-11-06, session 75: this file the derivation's one
// owner; architect 2026-11-11, session 110, THE THEME FILES: OUR OWN KEYED
// FORMAT, the files in the repository and the APK). A theme is one file of
// LIVE'S RAW ROLE VALUES — the roles THE DERIVATION below reads, under Live's
// own role names, never a derived tone — and the derivation, this file's,
// turns each into a row of GuiThemeTones at the launch's read.
//
// THE FORMAT (its one owner here). A theme file is `<slug>.theme`, ASCII
// text (UTF-8 in a comment), a line ending `\n` — a `\r` before it is
// stripped, so a file saved with CRLF reads the same — and the last line's
// `\n` optional. A line is EMPTY, A COMMENT (its first byte `#`, the rest
// anything), or A ROLE LINE `Name=HEX` exactly: no space anywhere, Name one
// of kThemeRoles (theme_roles.cpp; Live's role names, the one nested role
// flattened to the key `StandardVuMeter/Maximum` — Live's own meter clip box,
// the first `Maximum` inside the first `<StandardVuMeter>` block of an .ask),
// HEX 6 or 8 hex digits, case-blind on read (the converter writes uppercase),
// an 8-digit word's last byte its straight alpha — kept raw, the derivation
// compositing it (the COMPOSITED block). Every role of the table exactly
// once, in any order (the converter writes the table's). THE SLUG is the
// file's stem through THE SLUG RULE (theme_slug_of_stem, gui_theme.h), and a
// shipped file is named by its own slug (`default_dark_cool_medium.theme`,
// `sb_dash_mod.theme`): the slugs are the ones the `theme` key spelled when
// the app read Live's .ask files itself (2026-11-06 .. 11-11), so every
// config line keeps working.
//
// A FILE IS LEFT OUT, with one stderr line naming its path (its name, for a
// bundled file) and the first error, its line number where it has one, for:
// unreadable; a stem that spells no slug (no letter or digit); a malformed
// line; a role name the table lacks; a role twice; a role missing.
//
// THE FILES. The SHIPPED files are the repository's `themes/system/` and
// `themes/extra/`, packed by apk/build_apk.sh as the APK's `themes/system/`
// and `themes/extra/` assets and read in place at launch
// (GuiPlatform::bundled_theme_files) — each converted ONCE on the laptop
// from Live's or the pack's `.ask` (Ableton's theme XML) by
// tools/themes/ask_to_theme.py, never by hand but for the `_mod` files'
// recorded edits; NO .ASK FILE IS IN THE REPOSITORY OR THE APK, and the app
// reads none. THE USER'S files are `themes/*.theme` FLAT beside the device
// config (device_config_path()'s parent / "themes", the noise's
// composition): each regular file directly there whose name ends `.theme`;
// a subfolder, or any other file, is ignored silently; a MISSING OR
// UNLISTABLE folder is the ordinary state and silent (empty on his tablet).
// READ ONCE AT LAUNCH (load_gui_themes, gui_main, after the config and
// before the palette's first install): a file dropped while the app runs
// shows at the next launch, and the list is immutable for the process after
// the load.
//
// THE LIST — the value list's order, under THREE GROUPS, the headings exact
// (gui_theme.h's kGuiThemeGroup*):
//   SYSTEM        `default` FIRST — LIVE'S DEFAULT DARK COOL MEDIUM (architect
//                 2026-11-11, session 110, D4: "i plan to use cool medium a
//                 lot more"): a copy of `default_dark_cool_medium`'s row
//                 under the slug `default`, the opening theme — then BY SLUG
//                 every file of `themes/system/` (Live's stock themes and
//                 `immaterial_mod`, D5: alphabetical, so right after
//                 `immaterial`) AND THE COMPILED REFERENCE ROW as
//                 `default_dark_neutral_medium` (kGuiReferenceThemeTones,
//                 gui_theme.h: Live's Default Dark Neutral Medium, compiled,
//                 no file shipping it). Cool Medium is listed twice, as
//                 `default` and under its own slug.
//   EXTRA         every file of `themes/extra/` by slug: the Sonic Bloom
//                 pack as he curated it (2026-11-07), the six `_mod` files
//                 his hand edits, each recorded in its header comment.
//   USER-DEFINED  his own files beside the config, by slug (then by file
//                 name). A user file whose slug a shipped row bears
//                 (`default`, `default_dark_neutral_medium` and every SYSTEM
//                 and EXTRA slug) or an earlier user file bears is LEFT OUT
//                 with its line (D9: the earlier group wins).
// A heading shows only over rows (settings_editor.h's value_list_groups).
//
// A BUNDLED FILE IS THE PROGRAM'S OWN, so its fault is A BUILD DEFECT, the
// validation doctrine's class (1) as icons::load_set's: a shipped file left
// out by the rules above, a shipped file not named by its own slug, two
// shipped files of one slug, a shipped file slugging to `default` or
// `default_dark_neutral_medium`, or no `default_dark_cool_medium` among
// SYSTEM's files makes the load answer the error, and gui_main prints one
// line and opens no window.
//
// A CONFIG NAMING A SLUG NO ROW BEARS (a user file removed) OPENS IN
// `default` with one stderr line, the config's line untouched until he picks
// another (gui_main; the reader judges the shape alone, device_config.h's
// THE theme GRAMMAR).
//
// THE DERIVATION — our Default tone = Live's role, each Default number the
// reference row's (Live's Default Dark Neutral Medium; palette_file.h's
// static_asserts). A ROLE-NAMED role is read FLAT, its alpha byte dropped:
// each is 6-digit in every shipped file (audited 2026-11-06, session 81). A
// role found carrying alpha is COMPOSITED over the surface it paints on,
// never stripped (architect 2026-11-06, session 81, his "ok to implement"):
// the COMPOSITED block.
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
//     cue outline, text on lit
//                            111111 = ControlContrastFrame — THE ENABLED
//                                     CHIP'S OUTLINE (a disabled chip's is
//                                     the DERIVED block's inactive cue
//                                     outline), THE SELECTION STEM on every
//                                     selected chip, the rings, the ruler
//                                     head's outline; THE PRINT ON A
//                                     LIT KEY'S FACE too, its glyph and the
//                                     on-screen keyboard's lit cap, near-black
//                                     on the accent (architect 2026-11-11,
//                                     session 106: the text tone on the accent
//                                     is about 1.1 : 1; key_ink, materials.h)
//     text, cue fill         B5B5B5 = ControlForeground — THE ENABLED
//                                     MARKER'S CHIP too, every enabled chip
//                                     and the `h` view's added and changed
//                                     items, the keys' glyph tone, so a chip
//                                     and its label are one color (architect
//                                     2026-11-12, session 114: "for all
//                                     themes, the marker color should simply
//                                     be the icon glyph color. This will
//                                     unify"; on the light themes a
//                                     near-black chip on a mid-grey lane,
//                                     Live's own numbers, accepted)
//     dim text, well frame   757575 = TextDisabled (the scrub channel and the
//                                     tooltip border)
//     inactive cue           5D5D5D = BackgroundClip
//     cue                    FFAD56 = ViewCheckControlEnabledOn, THE
//                                     ACCENT: A LIT KEY'S WHOLE FACE, the
//                                     lamps', the seven doors' and the
//                                     on-screen keyboard's two lit keys alike
//                                     (architect 2026-11-11, session 106: "we
//                                     should go back to where the accent color
//                                     is the background of the lit lamp" —
//                                     Live's "on"; materials.h's THE KEY) and
//                                     NO MARKER since 2026-11-12 (the cue fill
//                                     above); immaterial_mod's file carries its
//                                     shaded gold here, its header the recipe)
//                                     — every lit key but the radio keys'
//                                     (below)
//     radio lit fill         B595FC = ScaleAwareness, THE PURPLE: A LIT RADIO
//                                     KEY'S WHOLE FACE, the eight of
//                                     redesign_button_is_radio — SOURCE /
//                                     TARGET, WARP / PHASE, FOLLOW / RESTR,
//                                     TRIM / ADD SEL (architect 2026-11-11,
//                                     session 107: two always-lit keys in the
//                                     accent read too bright at the top right
//                                     of his field of vision, "the purple is
//                                     perfectly fine in my peripheral field of
//                                     vision"; materials.h's THE RADIO KEYS).
//                                     Every theme's own role, immaterial_mod's
//                                     Immaterial's (its file changes the
//                                     accent alone)
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
//     clip lamp              FF0A0A = StandardVuMeter/Maximum, the one NESTED
//                                     role: Live's own meter clip box — THE
//                                     CLIP LAMP's lit face (architect
//                                     2026-11-05, his d6 "yes": a fault).
//                                     A key's lit look REFUSED for it:
//                                     "clip indicator is not a button"
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
//     level fill             007383 = RangeEditField over SurfaceBackground
//                                     (Live's volume color) — THE NOISE LEVEL
//                                     CASE'S FILL on the key face
//                                     (paint_level_case; architect 2026-11-11:
//                                     "give noise the volume color from the
//                                     ask ... so at 0 it wears the full vol
//                                     color, and at -inf it wears the button
//                                     case color"); a file's 8-digit word
//                                     composited over the face, never stripped
//                                     (architect 2026-11-06, session 81)
//     center                 = ink (a role is what it paints: the zero line IS
//                                   the ink, palette_file.h)
//     score pane             = ink (modal_box.h's score pane: "from ableton
//                                   palette directly")
//     grid                   303030 = TakeLaneTrackNotHighlighted over the
//                                     canvas (the quarter lines)
//     key dim ink            5D5D5D = ViewCheckControlDisabledOff over
//                                     SurfaceBackground (the key face; 8-digit
//                                     D3D3D340 on SB Cielo / Onyx) — a
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
//   are present in every shipped file and in Default.
//   DERIVED (the app's own tones, no Live role near them; every mix a + t ·
//   (b − a) per channel, rounded half to even and clamped):
//     playhead line          81E1EA = FFFFFF + 0.5 · (head − FFFFFF), white
//                                     EXACTLY HALF WAY to the head on every
//                                     theme (architect 2026-11-12, session
//                                     113): Live's measured fade FFFFFF <->
//                                     03C3D5 is linear in sRGB bytes, its true
//                                     mean the half; a channel whose distance
//                                     from 255 is odd lands on a .5, which
//                                     rounds to the even byte like every mix
//                                     here
//     inactive cue outline   242424 = ControlContrastFrame + 0.75 ·
//                                     (Desktop − ControlContrastFrame), THE
//                                     DISABLED CHIP'S OUTLINE, the `h` view's
//                                     removed items' too: the enabled chip's
//                                     outline moved 75 % OF THE WAY TOWARD
//                                     THE MARKER LANE it stands on, so a
//                                     disabled chip's edge recedes into the
//                                     lane (architect 2026-11-12, session 114:
//                                     "the disable outline, I think, should be
//                                     a different rule in the code, so no
//                                     change to the themes needed"; 75 % his
//                                     pick on the tablet over 50 %, SB
//                                     Gunmetal_mod 0B0D11 toward 24262D ->
//                                     1E2026). The selection stem of a
//                                     selected disabled chip, the ruler head's
//                                     outline and the lead-in ring stay the cue
//                                     outline (resolve_flag_face, render.cpp)
//     lit press fill         C68743 = THE ACCENT DARKENED BY THE PRESS RATIO:
//                                     the accent (cue) · (mean(press face) /
//                                     mean(key face)) per channel, the means
//                                     over the three sRGB bytes, rounded half
//                                     to even and clamped (42 / 54 = 0.778 on
//                                     Default: FFAD56 -> C68743) — A CHECKED
//                                     KEY'S FACE UNDER A STANDING PRESS
//                                     (architect 2026-11-11, session 106: "if
//                                     it's a lit lamp, it gets darkened based
//                                     on the starting point of the accent
//                                     color"; the press arc's ruling of
//                                     2026-11-05, his "ratio" on mocks d1 / i1
//                                     of tmp/s45). THE RATIO, NOT THE
//                                     DIFFERENCE: the ratio keeps the theme's
//                                     lit hue and scales with its own press
//                                     step. A theme whose key face is black
//                                     (no divisor) keeps the accent whole,
//                                     ratio 1; where the press face is lighter
//                                     than the key face the lit press comes
//                                     out lighter — Live's numbers, no rule
//     lit dim fill, text on lit dim
//                            83592C, 090909 = THE LIT-BUT-INERT PAIR: the
//                                     accent (cue) and the print on it (text
//                                     on lit), each moved by ONE SCALAR for
//                                     both tones and their three channels so
//                                     each keeps its hue, the means over the
//                                     three sRGB bytes, rounded half to even
//                                     and clamped, IN THE DIRECTION THE
//                                     DISABLED INK LIES FROM THE TEXT
//                                     (architect 2026-11-11, session 109,
//                                     his pick (a): "the same rule in
//                                     negative" on a light theme). A DARK
//                                     THEME, mean(text) >= mean(key dim ink):
//                                     EACH · (mean(key dim ink) / mean(text)),
//                                     toward black (93 / 181 on Default:
//                                     FFAD56 -> 83592C, 111111 -> 090909); a
//                                     theme whose text and key dim ink are
//                                     both black (no divisor) keeps both
//                                     whole, ratio 1. A LIGHT THEME,
//                                     mean(text) < mean(key dim ink): toward
//                                     white, 255 − (255 − tone) · ((255 −
//                                     mean(key dim ink)) / (255 −
//                                     mean(text))), the divisor never zero
//                                     (150 / 255 on Classic Medium Light:
//                                     FFB901 -> FFD66A, 383838 -> 8A8A8A).
//                                     The face and the glyph of A
//                                     CHECKED-BUT-DISABLED KEY (a lamp on a
//                                     locked tab or under grid iterations,
//                                     a lit key under an overlay, 2026-11-11,
//                                     session 107), dimmed as the disabled
//                                     glyph is dimmed against the resting one
//                                     (architect 2026-11-11, session 106: "dim
//                                     the accent and the glyph so both read as
//                                     a unit"; paint_key and key_ink,
//                                     materials.h)
//     radio lit press fill   8D74C4 = the radio lit fill · THE SAME PRESS
//                                     RATIO, mean(press face) / mean(key
//                                     face), per channel, one code path with
//                                     the lit press fill (42 / 54 on Default:
//                                     B595FC -> 8D74C4) — a checked radio
//                                     key's face under a standing press
//                                     (architect 2026-11-11, session 107),
//                                     the black key face's ratio 1 as above
//     radio lit dim fill     5D4D81 = the radio lit fill moved by THE SAME
//                                     DIM SCALAR on the same branch, per
//                                     channel, one code path with the lit dim
//                                     fill: toward black on a dark theme (93 /
//                                     181 on Default: B595FC -> 5D4D81),
//                                     toward white on a light one (150 / 255
//                                     on Classic Medium Light: BE98FF ->
//                                     D9C2FF) — a checked-but-disabled radio
//                                     key's face, under the same
//                                     `text_on_lit_dim` glyph (the same
//                                     rulings), the both-black ratio 1 as
//                                     above
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
// theme by hand, a `_mod` file of themes/extra/ (architect 2026-11-07,
// session 82: "i'd rather manually fix individual sb themes than make rules
// around their missing colors").
//
// The derivation is total and leaves no file out. ITS ARITHMETIC IS
// PYTHON'S, BIT FOR BIT (the derivation was first written in Python, and the
// proof of 2026-11-06 diffed every row on the tablet against its numbers):
// doubles in its operation order, Python's round() as nearbyint under the
// default rounding mode (half to even; theme_roles.cpp).

// Build THE LIST (the head's groups and order), once per process, before
// the palette's first install: `system_files` and `extra_files` the shipped
// files, each a map from FILE NAME (`<slug>.theme`, no folder) to the file's
// whole text — the APK's `themes/system/` and `themes/extra/` assets
// (GuiPlatform::bundled_theme_files), or any host's reading of the
// repository's folders — and `user_dir` the user's flat folder (an empty
// path is the absent folder: no config home). THE ERROR ARM is the head's
// build defect (a shipped file's fault), the message naming the file;
// nothing is listed then. A user file's fault is its stderr line alone.
std::optional<std::string> load_gui_themes(
    const std::map<std::string, std::string>& system_files,
    const std::map<std::string, std::string>& extra_files,
    const std::filesystem::path& user_dir);

// THE LIST in the value list's order, `default` first. Empty before the
// load.
const std::vector<GuiTheme>& gui_themes();

// THE ROW WHOSE SLUG IS `slug`, or nullptr when no row bears it: the
// settings editor's `theme` commit refuses such a slug (only a typed line
// can name one), the launch opens in `default` (the head).
const GuiTheme* find_gui_theme(std::string_view slug);

// The `default` row, Live's Default Dark Cool Medium (the list's first; the
// load must have answered no error).
const GuiTheme& default_gui_theme();
